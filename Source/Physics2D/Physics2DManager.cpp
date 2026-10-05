#include "pch.h"
#include "Physics2DManager.h"
#include "Physics2DComponents.h"
#include "Physics2DJoints.h"
#include "Physics2DSettings.h"
#include "PhysicsManager.h"
#include "CSharpScript.h"
#include "ComponentIndex.h"
#include <box2d/box2d.h>

namespace
{
	constexpr float kDeg = 3.14159265358979f / 180.0f;
	constexpr int kSubSteps = 4;

	struct BodyRec
	{
		GameObject* Owner = nullptr;
		Rigidbody2D* Rb = nullptr;            // nullptr = 정적 (콜라이더만)
		std::shared_ptr<Rigidbody2D> RbOwner; // 제거된 컴포넌트를 Box2D 몸체가 지워질 때까지 붙잡아 둔다
		b2BodyId Id = b2_nullBodyId;
		std::vector<b2ShapeId> Shapes;
		size_t ShapeSig = 0;
		int Type = -1;
		Vec2 LastPos;                          // 마지막으로 Transform 과 맞춘 값 (사용자가 옮겼는지)
		float LastAngle = 0.0f;
		Vec2 FrozenPos;                        // Freeze Position X / Y
		bool Seen = false;
	};

	struct ContactKey
	{
		Collider2D* A; Collider2D* B;
		bool operator==(const ContactKey& o) const { return A == o.A && B == o.B; }
	};
	struct ContactHash { size_t operator()(const ContactKey& k) const { return std::hash<void*>()(k.A) ^ (std::hash<void*>()(k.B) * 31); } };
	struct ContactInfoRec { Vec2 Point, Normal, RelVel; };   // Normal: A → B

	b2WorldId s_World = b2_nullWorldId;
	Scene* s_Scene = nullptr;
	std::unordered_map<GameObject*, BodyRec> s_Bodies;
	std::unordered_map<uint64, Collider2D*> s_ShapeOwners;      // 모양 → 콜라이더 (지운 모양의 끝 이벤트도 찾게)
	std::unordered_set<Collider2D*> s_Alive;
	std::unordered_map<ContactKey, ContactInfoRec, ContactHash> s_Touching;   // 충돌 중 (A < B 순서 아님 — 이벤트 그대로)
	std::unordered_set<ContactKey, ContactHash> s_Triggers;    // A = 센서, B = 방문자
	uint32 s_FilterVersion = 0;
	float s_Accumulator = 0.0f;

	uint64 Key(b2ShapeId id) { uint64 k = 0; memcpy(&k, &id, sizeof(id)); return k; }
	uint64 PackBody(b2BodyId id) { uint64 k = 0; memcpy(&k, &id, sizeof(id)); return k; }
	b2BodyId UnpackBody(uint64 k) { b2BodyId id; memcpy(&id, &k, sizeof(id)); return id; }
	b2Vec2 B(const Vec2& v) { return b2Vec2{ v.x, v.y }; }
	Vec2 V(b2Vec2 v) { return Vec2(v.x, v.y); }

	bool ActiveInHierarchy(GameObject* go)
	{
		for (GameObject* g = go; g; g = g->GetParent())
			if (!g->IsActive())
				return false;
		return go != nullptr;
	}

	// GameObject 월드 → 2D (위치 x, y · Z 회전)
	void World2D(GameObject* go, Vec2& pos, float& angle)
	{
		const Matrix m = go->GetTransform()->GetWorldMatrix();
		pos = Vec2(m._41, m._42);
		angle = atan2f(m._12, m._11);
	}

	Rigidbody2D* FindRigidbody(GameObject* go)
	{
		for (GameObject* g = go; g; g = g->GetParent())
			if (Rigidbody2D* rb = ComponentIndex::Of(g).Rigid2D; rb && rb->IsEnabled() && ActiveInHierarchy(g))   // 기억한 분류
				return rb;
		return nullptr;
	}

	b2Filter FilterFor(Collider2D* c)
	{
		const int layer = c->GetGameObject()->GetLayerIndex() & 31;
		b2Filter f = b2DefaultFilter();
		f.categoryBits = 1ull << layer;
		f.maskBits = Physics2DSettings::CollisionMask(layer);
		return f;
	}

	void HashMix(size_t& h, size_t v) { h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2); }
	// 1e-4 단위로 반올림해서 (움직이는 몸체의 행렬 계산 오차로 매 프레임 모양을 다시 만들지 않게)
	void HashFloat(size_t& h, float f) { HashMix(h, (size_t)(int64)llroundf(f * 10000.0f)); }

	// 콜라이더의 로컬 모양 → 몸체 공간 (콜라이더 GameObject 월드 행렬 → 몸체의 역 2D 변환)
	void CreateShapes(BodyRec& rec, const std::vector<Collider2D*>& colliders)
	{
		Vec2 bodyPos; float bodyAngle;
		World2D(rec.Owner, bodyPos, bodyAngle);
		const float c = cosf(-bodyAngle), s = sinf(-bodyAngle);
		for (Collider2D* col : colliders)
		{
			std::vector<Shape2D> shapes;
			col->BuildShapes(shapes);
			const Matrix m = col->GetGameObject()->GetTransform()->GetWorldMatrix();
			const float sx = Vec2(m._11, m._12).Length(), sy = Vec2(m._21, m._22).Length();
			const float radiusScale = (std::max)(sx, sy);
			auto toBody = [&](const Vec2& p) {
				const Vec3 w = Vec3::Transform(Vec3(p.x, p.y, 0.0f), m);
				const float dx = w.x - bodyPos.x, dy = w.y - bodyPos.y;
				return b2Vec2{ c * dx - s * dy, s * dx + c * dy };
			};
			b2ShapeDef sd = b2DefaultShapeDef();
			sd.density = 1.0f;
			sd.material.friction = (std::max)(0.0f, col->Friction);
			sd.material.restitution = std::clamp(col->Bounciness, 0.0f, 1.0f);
			sd.isSensor = col->IsTrigger;
			sd.enableSensorEvents = true;
			sd.enableContactEvents = true;
			sd.filter = FilterFor(col);
			sd.userData = col;
			sd.updateBodyMass = false;
			for (const Shape2D& sh : shapes)
			{
				b2ShapeId id = b2_nullShapeId;
				if (sh.K == Shape2D::Circle && !sh.Points.empty())
				{
					b2Circle circle{ toBody(sh.Points[0]), sh.Radius * radiusScale };
					id = b2CreateCircleShape(rec.Id, &sd, &circle);
				}
				else if (sh.K == Shape2D::Capsule && sh.Points.size() == 2)
				{
					b2Capsule cap{ toBody(sh.Points[0]), toBody(sh.Points[1]), sh.Radius * radiusScale };
					id = b2CreateCapsuleShape(rec.Id, &sd, &cap);
				}
				else if (sh.K == Shape2D::Segment && sh.Points.size() == 2)
				{
					if (sh.Radius > 0.0f)
					{
						b2Capsule cap{ toBody(sh.Points[0]), toBody(sh.Points[1]), sh.Radius * radiusScale };
						id = b2CreateCapsuleShape(rec.Id, &sd, &cap);
					}
					else
					{
						b2Segment seg{ toBody(sh.Points[0]), toBody(sh.Points[1]) };
						id = b2CreateSegmentShape(rec.Id, &sd, &seg);
					}
				}
				else if (sh.K == Shape2D::Polygon && sh.Points.size() >= 3)
				{
					b2Vec2 pts[B2_MAX_POLYGON_VERTICES];
					const int n = (std::min)((int)sh.Points.size(), B2_MAX_POLYGON_VERTICES);
					for (int i = 0; i < n; ++i)
						pts[i] = toBody(sh.Points[i]);
					const b2Hull hull = b2ComputeHull(pts, n);
					if (hull.count >= 3)
					{
						const b2Polygon poly = b2MakePolygon(&hull, sh.Radius * radiusScale);
						id = b2CreatePolygonShape(rec.Id, &sd, &poly);
					}
				}
				if (B2_IS_NON_NULL(id))
				{
					rec.Shapes.push_back(id);
					s_ShapeOwners[Key(id)] = col;
				}
			}
		}
		// 질량: Rigidbody2D 의 Mass (모양의 넓이로 나눈 비율로 관성도 맞춘다)
		if (rec.Rb && rec.Rb->Type == Rigidbody2D::BodyType::Dynamic)
		{
			b2Body_ApplyMassFromShapes(rec.Id);
			b2MassData md = b2Body_GetMassData(rec.Id);
			const float mass = (std::max)(0.0001f, rec.Rb->Mass);
			if (md.mass > 0.0f)
			{
				md.rotationalInertia *= mass / md.mass;
				md.mass = mass;
			}
			else
			{
				md.mass = mass;
				md.rotationalInertia = mass * 0.1f;
			}
			b2Body_SetMassData(rec.Id, md);
		}
	}

	void DestroyShapes(BodyRec& rec)
	{
		for (b2ShapeId id : rec.Shapes)
		{
			s_ShapeOwners.erase(Key(id));
			if (b2Shape_IsValid(id))
				b2DestroyShape(id, false);
		}
		rec.Shapes.clear();
	}

	void DestroyBody(BodyRec& rec)
	{
		DestroyShapes(rec);
		if (b2Body_IsValid(rec.Id))
			b2DestroyBody(rec.Id);
		if (rec.Rb)
			rec.Rb->Body = 0;
	}

	b2BodyType TypeOf(Rigidbody2D* rb)
	{
		if (!rb) return b2_staticBody;
		switch (rb->Type)
		{
		case Rigidbody2D::BodyType::Kinematic: return b2_kinematicBody;
		case Rigidbody2D::BodyType::Static: return b2_staticBody;
		default: return b2_dynamicBody;
		}
	}

	void ApplyBodySettings(BodyRec& rec)
	{
		Rigidbody2D* rb = rec.Rb;
		if (!rb)
			return;
		b2Body_SetLinearDamping(rec.Id, (std::max)(0.0f, rb->LinearDamping));
		b2Body_SetAngularDamping(rec.Id, (std::max)(0.0f, rb->AngularDamping));
		b2Body_SetGravityScale(rec.Id, rb->GravityScale);
		b2Body_SetFixedRotation(rec.Id, rb->FreezeRotation);
		b2Body_SetBullet(rec.Id, rb->CollisionDetection == 1);
	}

	// 씬과 맞추기: 몸체 · 모양 만들기 / 지우기 / 다시, 사용자가 옮긴 Transform
	void Sync()
	{
		if (!s_Scene)
			return;
		std::unordered_map<GameObject*, std::vector<Collider2D*>> groups;
		std::unordered_map<GameObject*, Rigidbody2D*> rbs;
		s_Alive.clear();
		for (GameObject* go : s_Scene->GetAllGameObjects())
		{
			if (!go || !ActiveInHierarchy(go))
				continue;
			// 분류는 ComponentIndex 가 기억한다 (컴포넌트가 그대로면 dynamic_cast 없이 — 2D 몸체가 없는 3D 장면에서도 스텝마다 모든 오브젝트를 캐스트했다)
			const ComponentIndex::Entry& e = ComponentIndex::Of(go);
			if (Rigidbody2D* rb = e.Rigid2D; rb && rb->IsEnabled())
			{
				rbs[go] = rb;
				groups[go];   // 콜라이더 없는 몸체도
			}
			for (Collider2D* col : e.Colliders2D)
			{
				if (!col->IsEnabled())
					continue;
				Rigidbody2D* rb = FindRigidbody(go);
				groups[rb ? rb->GetGameObject() : go].push_back(col);
				s_Alive.insert(col);
			}
		}
		ComponentIndex::EndPass();   // 오래된 기억 정리 (2D 만 쓰는 게임에서도)
		for (auto& [go, rec] : s_Bodies)
			rec.Seen = false;
		const bool filtersChanged = s_FilterVersion != Physics2DSettings::Version();
		s_FilterVersion = Physics2DSettings::Version();
		for (auto& [owner, cols] : groups)
		{
			auto rbIt = rbs.find(owner);
			Rigidbody2D* rb = rbIt != rbs.end() ? rbIt->second : nullptr;
			// 모양 서명: 콜라이더 · 값 · 레이어 · 몸체에 대한 상대 자세 · 크기
			size_t sig = 0;
			const Matrix ownerInv = owner->GetTransform()->GetWorldMatrix().Invert();
			for (Collider2D* c : cols)
			{
				HashMix(sig, (size_t)c);
				HashMix(sig, c->Revision);
				HashMix(sig, (size_t)c->GetGameObject()->GetLayerIndex());
				if (c->GetGameObject() != owner)   // 자식의 콜라이더: 몸체에 대한 자세
				{
					const Matrix rel = c->GetGameObject()->GetTransform()->GetWorldMatrix() * ownerInv;
					for (int i = 0; i < 16; ++i) HashFloat(sig, (&rel._11)[i]);
				}
				const Matrix w = owner->GetTransform()->GetWorldMatrix();
				HashFloat(sig, Vec2(w._11, w._12).Length());
				HashFloat(sig, Vec2(w._21, w._22).Length());
			}
			HashMix(sig, rb ? (size_t)(int)rb->Type : 99);
			if (rb) HashFloat(sig, rb->Mass);
			BodyRec& rec = s_Bodies[owner];
			rec.Seen = true;
			const int type = (int)TypeOf(rb);
			if (B2_IS_NULL(rec.Id) || !b2Body_IsValid(rec.Id) || rec.Type != type || rec.Rb != rb)
			{
				if (B2_IS_NON_NULL(rec.Id))
					DestroyBody(rec);
				b2BodyDef bd = b2DefaultBodyDef();
				bd.type = (b2BodyType)type;
				Vec2 pos; float angle;
				World2D(owner, pos, angle);
				bd.position = B(pos);
				bd.rotation = b2MakeRot(angle);
				bd.userData = owner;
				if (rb)
				{
					bd.fixedRotation = rb->FreezeRotation;
					bd.isBullet = rb->CollisionDetection == 1;
					if (rb->HasPending)
					{
						bd.linearVelocity = B(rb->PendingVelocity);
						bd.angularVelocity = rb->PendingAngular * kDeg;
						rb->HasPending = false;
					}
				}
				rec.Id = b2CreateBody(s_World, &bd);
				rec.Owner = owner;
				rec.Rb = rb;
				rec.RbOwner = rb ? owner->GetComponent_SP<Rigidbody2D>() : nullptr;
				rec.Type = type;
				rec.ShapeSig = 0;
				rec.LastPos = pos;
				rec.LastAngle = angle;
				rec.FrozenPos = pos;
				if (rb)
				{
					rb->Body = PackBody(rec.Id);
					ApplyBodySettings(rec);
				}
			}
			if (rec.ShapeSig != sig)
			{
				DestroyShapes(rec);
				CreateShapes(rec, cols);
				rec.ShapeSig = sig;
			}
			else if (filtersChanged)
				for (b2ShapeId id : rec.Shapes)
					if (auto it = s_ShapeOwners.find(Key(id)); it != s_ShapeOwners.end())
						b2Shape_SetFilter(id, FilterFor(it->second));
			// 사용자가 Transform 을 옮겼으면 몸체도 (Unity: transform 을 바꾸면 순간 이동)
			Vec2 pos; float angle;
			World2D(owner, pos, angle);
			if ((pos - rec.LastPos).LengthSquared() > 1e-10f || fabsf(angle - rec.LastAngle) > 1e-5f)
			{
				b2Body_SetTransform(rec.Id, B(pos), b2MakeRot(angle));
				rec.LastPos = pos;
				rec.LastAngle = angle;
				rec.FrozenPos = pos;
			}
		}
		// 없어진 몸체
		for (auto it = s_Bodies.begin(); it != s_Bodies.end();)
			if (!it->second.Seen)
			{
				DestroyBody(it->second);
				it = s_Bodies.erase(it);
			}
			else
				++it;
		// 지운 콜라이더가 남긴 접촉
		for (auto it = s_Touching.begin(); it != s_Touching.end();)
			it = (s_Alive.count(it->first.A) && s_Alive.count(it->first.B)) ? std::next(it) : s_Touching.erase(it);
		for (auto it = s_Triggers.begin(); it != s_Triggers.end();)
			it = (s_Alive.count(it->A) && s_Alive.count(it->B)) ? std::next(it) : s_Triggers.erase(it);
	}

	// C# OnCollision / OnTrigger … 2D: 콜라이더의 GameObject 와 (다르면) 그 Rigidbody2D 의 GameObject 의 스크립트에
	void SendTo(GameObject* go, GameObject* other, bool trigger, int phase)
	{
		if (!go || !other)
			return;
		for (const auto& comp : go->GetComponents())
			if (CSharpScript* script = dynamic_cast<CSharpScript*>(comp.get()); script && script->IsEnabled())
				script->Collision2D(other, trigger, phase);
	}

	void Dispatch(Collider2D* self, Collider2D* other, bool trigger, int phase)
	{
		if (!self || !other || !s_Alive.count(self) || !s_Alive.count(other))
			return;
		GameObject* go = self->GetGameObject();
		GameObject* otherGo = other->GetGameObject();
		SendTo(go, otherGo, trigger, phase);
		if (Rigidbody2D* rb = FindRigidbody(go); rb && rb->GetGameObject() != go)
			SendTo(rb->GetGameObject(), otherGo, trigger, phase);
	}

	Collider2D* ColliderOf(b2ShapeId id)
	{
		auto it = s_ShapeOwners.find(Key(id));
		return it != s_ShapeOwners.end() ? it->second : nullptr;
	}

	Vec2 BodyVelocity(b2ShapeId id)
	{
		if (!b2Shape_IsValid(id))
			return Vec2(0, 0);
		return V(b2Body_GetLinearVelocity(b2Shape_GetBody(id)));
	}

	void ProcessEvents()
	{
		const b2ContactEvents ce = b2World_GetContactEvents(s_World);
		std::vector<std::pair<ContactKey, int>> calls;   // 이벤트를 모은 뒤 부른다 (스크립트가 세계를 바꿔도 안전하게)
		for (int i = 0; i < ce.beginCount; ++i)
		{
			const b2ContactBeginTouchEvent& e = ce.beginEvents[i];
			Collider2D* a = ColliderOf(e.shapeIdA);
			Collider2D* b = ColliderOf(e.shapeIdB);
			if (!a || !b)
				continue;
			ContactInfoRec info;
			info.Normal = V(e.manifold.normal);
			info.Point = e.manifold.pointCount > 0 ? V(e.manifold.points[0].point) : Vec2(0, 0);
			info.RelVel = BodyVelocity(e.shapeIdB) - BodyVelocity(e.shapeIdA);
			s_Touching[{ a, b }] = info;
			calls.push_back({ { a, b }, 0 });
		}
		for (int i = 0; i < ce.endCount; ++i)
		{
			const b2ContactEndTouchEvent& e = ce.endEvents[i];
			Collider2D* a = ColliderOf(e.shapeIdA);
			Collider2D* b = ColliderOf(e.shapeIdB);
			if (!a || !b)
				continue;
			if (s_Touching.erase({ a, b }) || s_Touching.erase({ b, a }))
				calls.push_back({ { a, b }, 2 });
		}
		const b2SensorEvents se = b2World_GetSensorEvents(s_World);
		std::vector<std::pair<ContactKey, int>> triggerCalls;
		for (int i = 0; i < se.beginCount; ++i)
		{
			Collider2D* sensor = ColliderOf(se.beginEvents[i].sensorShapeId);
			Collider2D* visitor = ColliderOf(se.beginEvents[i].visitorShapeId);
			if (!sensor || !visitor)
				continue;
			s_Triggers.insert({ sensor, visitor });
			triggerCalls.push_back({ { sensor, visitor }, 0 });
		}
		for (int i = 0; i < se.endCount; ++i)
		{
			Collider2D* sensor = ColliderOf(se.endEvents[i].sensorShapeId);
			Collider2D* visitor = ColliderOf(se.endEvents[i].visitorShapeId);
			if (!sensor || !visitor)
				continue;
			if (s_Triggers.erase({ sensor, visitor }))
				triggerCalls.push_back({ { sensor, visitor }, 2 });
		}
		// Stay: 이번 단계에 시작하지 않은 접촉 · 겹침
		for (const auto& [k, info] : s_Touching)
		{
			bool began = false;
			for (const auto& c : calls) began |= c.second == 0 && c.first == k;
			if (!began) calls.push_back({ k, 1 });
		}
		for (const ContactKey& k : s_Triggers)
		{
			bool began = false;
			for (const auto& c : triggerCalls) began |= c.second == 0 && c.first == k;
			if (!began) triggerCalls.push_back({ k, 1 });
		}
		for (const auto& [k, phase] : calls)
		{
			Dispatch(k.A, k.B, false, phase);
			Dispatch(k.B, k.A, false, phase);
		}
		for (const auto& [k, phase] : triggerCalls)
		{
			Dispatch(k.A, k.B, true, phase);
			Dispatch(k.B, k.A, true, phase);
		}
	}

	// 결과 → Transform (Dynamic · Kinematic), Freeze Position
	void WriteBack()
	{
		for (auto& [go, rec] : s_Bodies)
		{
			if (!rec.Rb || rec.Type == b2_staticBody || !b2Body_IsValid(rec.Id))
				continue;
			b2Vec2 p = b2Body_GetPosition(rec.Id);
			const float angle = b2Rot_GetAngle(b2Body_GetRotation(rec.Id));
			if (rec.Rb->FreezePositionX || rec.Rb->FreezePositionY)
			{
				b2Vec2 v = b2Body_GetLinearVelocity(rec.Id);
				if (rec.Rb->FreezePositionX) { p.x = rec.FrozenPos.x; v.x = 0.0f; }
				if (rec.Rb->FreezePositionY) { p.y = rec.FrozenPos.y; v.y = 0.0f; }
				b2Body_SetTransform(rec.Id, p, b2Body_GetRotation(rec.Id));
				b2Body_SetLinearVelocity(rec.Id, v);
			}
			Transform* t = go->GetTransform();
			const Vec3 old = t->GetPosition();
			if (fabsf(old.x - p.x) > 1e-6f || fabsf(old.y - p.y) > 1e-6f)
				t->SetPosition(Vec3(p.x, p.y, old.z));
			if (fabsf(angle - rec.LastAngle) > 1e-6f)
				t->SetRotation(Quaternion::CreateFromAxisAngle(Vec3(0, 0, 1), angle));
			World2D(go, rec.LastPos, rec.LastAngle);
		}
	}

	BodyRec* RecOf(Rigidbody2D* rb)
	{
		if (!rb || !rb->Body || B2_IS_NULL(s_World))
			return nullptr;
		auto it = s_Bodies.find(rb->GetGameObject());
		return it != s_Bodies.end() && b2Body_IsValid(it->second.Id) ? &it->second : nullptr;
	}

	struct RayContext { uint32 Mask; bool Triggers; Physics2DManager::Hit* Out; bool Found; };
	float RayCallback(b2ShapeId id, b2Vec2 point, b2Vec2 normal, float fraction, void* ctx)
	{
		RayContext* rc = (RayContext*)ctx;
		Collider2D* c = ColliderOf(id);
		if (!c || (c->IsTrigger && !rc->Triggers))
			return -1.0f;   // 걸러냄 (계속)
		rc->Out->Collider = c;
		rc->Out->Point = V(point);
		rc->Out->Normal = V(normal);
		rc->Out->Fraction = fraction;
		rc->Found = true;
		return fraction;   // 더 가까운 것만
	}

	struct OverlapContext { bool Triggers; Collider2D* Found; };
	bool OverlapCallback(b2ShapeId id, void* ctx)
	{
		OverlapContext* oc = (OverlapContext*)ctx;
		Collider2D* c = ColliderOf(id);
		if (!c || (c->IsTrigger && !oc->Triggers))
			return true;
		oc->Found = c;
		return false;
	}

	b2QueryFilter QueryFilter(uint32 mask)
	{
		b2QueryFilter f = b2DefaultQueryFilter();
		f.categoryBits = ~0ull;
		f.maskBits = (uint64)mask;
		return f;
	}
}

namespace Physics2DManager
{
	bool Active() { return B2_IS_NON_NULL(s_World); }
	int BodyCount() { return (int)s_Bodies.size(); }

	void Start(Scene* scene)
	{
		Exit();
		Physics2DSettings::ResetRuntime();
		b2WorldDef wd = b2DefaultWorldDef();
		wd.gravity = B(Physics2DSettings::Gravity());
		s_World = b2CreateWorld(&wd);
		s_Scene = scene;
		s_Accumulator = 0.0f;
		s_FilterVersion = Physics2DSettings::Version();
		Sync();   // Start 에서 Rigidbody2D 를 바로 쓸 수 있게 (Unity 와 같음)
		Physics2DJointsRuntime::Sync(s_Scene, s_World);
	}

	void Exit()
	{
		Physics2DJointsRuntime::Reset();
		if (B2_IS_NON_NULL(s_World))
			b2DestroyWorld(s_World);   // 몸체 · 모양도 함께
		for (auto& [go, rec] : s_Bodies)
			if (rec.Rb) rec.Rb->Body = 0;
		s_World = b2_nullWorldId;
		s_Bodies.clear();
		s_ShapeOwners.clear();
		s_Touching.clear();
		s_Triggers.clear();
		s_Alive.clear();
		s_Scene = nullptr;
		Physics2DSettings::ResetRuntime();
	}

	void Update(float deltaTime)
	{
		if (B2_IS_NULL(s_World))
			return;
		Scene* current = SceneManager::GetI()->GetCurrentScene();
		if (current != s_Scene)
		{
			Start(current);   // Play 중 SceneManager.LoadScene
			if (B2_IS_NULL(s_World))
				return;
		}
		const float step = PhysicsManager::GetI()->GetFixedTimestep();
		s_Accumulator += (std::min)(deltaTime, 0.25f);
		bool synced = false;
		while (s_Accumulator >= step)
		{
			s_Accumulator -= step;
			if (!synced) { Sync(); synced = true; }
			Physics2DJointsRuntime::Sync(s_Scene, s_World);
			// Kinematic MovePosition / MoveRotation: 이번 단계 동안 그곳으로
			for (auto& [go, rec] : s_Bodies)
				if (rec.Rb && (rec.Rb->HasMoveTarget || rec.Rb->HasMoveAngle))
				{
					const b2Vec2 p = rec.Rb->HasMoveTarget ? B(rec.Rb->MoveTarget) : b2Body_GetPosition(rec.Id);
					const b2Rot r = rec.Rb->HasMoveAngle ? b2MakeRot(rec.Rb->MoveAngle * kDeg) : b2Body_GetRotation(rec.Id);
					if (rec.Type == b2_kinematicBody)
						b2Body_SetTargetTransform(rec.Id, b2Transform{ p, r }, step);
					else
						b2Body_SetTransform(rec.Id, p, r);
					rec.Rb->HasMoveTarget = rec.Rb->HasMoveAngle = false;
				}
			b2World_Step(s_World, step, kSubSteps);
			WriteBack();
			Physics2DJointsRuntime::AfterStep();
			ProcessEvents();
			if (B2_IS_NULL(s_World))
				return;
		}
	}

	bool Raycast(const Vec2& origin, const Vec2& direction, float distance, uint32 layerMask, Hit& hit)
	{
		if (B2_IS_NULL(s_World))
			return false;
		Vec2 dir = direction;
		if (dir.LengthSquared() < 1e-12f)
			return false;
		dir.Normalize();
		const float d = std::isfinite(distance) ? (std::min)(distance, 1.0e6f) : 1.0e6f;
		RayContext rc{ layerMask, Physics2DSettings::QueriesHitTriggers(), &hit, false };
		b2World_CastRay(s_World, B(origin), B(dir * d), QueryFilter(layerMask), RayCallback, &rc);
		if (rc.Found)
			hit.Distance = hit.Fraction * d;
		return rc.Found;
	}

	Collider2D* OverlapCircle(const Vec2& center, float radius, uint32 layerMask)
	{
		if (B2_IS_NULL(s_World))
			return nullptr;
		const b2Vec2 p = B(center);
		const b2ShapeProxy proxy = b2MakeProxy(&p, 1, (std::max)(radius, 0.0001f));
		OverlapContext oc{ Physics2DSettings::QueriesHitTriggers(), nullptr };
		b2World_OverlapShape(s_World, &proxy, QueryFilter(layerMask), OverlapCallback, &oc);
		return oc.Found;
	}

	Collider2D* OverlapBox(const Vec2& center, const Vec2& size, float angleDegrees, uint32 layerMask)
	{
		if (B2_IS_NULL(s_World))
			return nullptr;
		const float hx = fabsf(size.x) * 0.5f, hy = fabsf(size.y) * 0.5f;
		const b2Vec2 pts[4] = { { -hx, -hy }, { hx, -hy }, { hx, hy }, { -hx, hy } };
		const b2ShapeProxy proxy = b2MakeOffsetProxy(pts, 4, 0.0f, B(center), b2MakeRot(angleDegrees * kDeg));
		OverlapContext oc{ Physics2DSettings::QueriesHitTriggers(), nullptr };
		b2World_OverlapShape(s_World, &proxy, QueryFilter(layerMask), OverlapCallback, &oc);
		return oc.Found;
	}

	bool ContactInfo(GameObject* self, GameObject* other, Vec2& point, Vec2& normal, Vec2& relVel)
	{
		for (const auto& [k, info] : s_Touching)
		{
			const bool selfIsA = k.A->GetGameObject() == self && k.B->GetGameObject() == other;
			const bool selfIsB = k.B->GetGameObject() == self && k.A->GetGameObject() == other;
			if (!selfIsA && !selfIsB)
				continue;
			point = info.Point;
			// Unity: 법선 = 상대 표면에서 나를 향해 (바닥에 선 캐릭터 = 위)
			normal = selfIsA ? -info.Normal : info.Normal;
			relVel = selfIsA ? info.RelVel : -info.RelVel;
			return true;
		}
		return false;
	}

	Vec2 Gravity()
	{
		return B2_IS_NON_NULL(s_World) ? V(b2World_GetGravity(s_World)) : Physics2DSettings::Gravity();
	}

	void SetGravity(const Vec2& g)
	{
		if (B2_IS_NON_NULL(s_World))
			b2World_SetGravity(s_World, B(g));
	}
}

// ------------------------------------------------------------------ Rigidbody2D (실행 중)
Vec2 Rigidbody2D::GetVelocity()
{
	if (BodyRec* r = RecOf(this)) return V(b2Body_GetLinearVelocity(r->Id));
	return HasPending ? PendingVelocity : Vec2(0, 0);
}

void Rigidbody2D::SetVelocity(const Vec2& v)
{
	if (BodyRec* r = RecOf(this)) { b2Body_SetLinearVelocity(r->Id, B(v)); return; }
	PendingVelocity = v;
	HasPending = true;
}

float Rigidbody2D::GetAngularVelocity()
{
	if (BodyRec* r = RecOf(this)) return b2Body_GetAngularVelocity(r->Id) / kDeg;
	return HasPending ? PendingAngular : 0.0f;
}

void Rigidbody2D::SetAngularVelocity(float degPerSec)
{
	if (BodyRec* r = RecOf(this)) { b2Body_SetAngularVelocity(r->Id, degPerSec * kDeg); return; }
	PendingAngular = degPerSec;
	HasPending = true;
}

Vec2 Rigidbody2D::GetPosition()
{
	if (BodyRec* r = RecOf(this)) return V(b2Body_GetPosition(r->Id));
	Vec2 p; float a;
	World2D(m_pGameObject, p, a);
	return p;
}

float Rigidbody2D::GetRotation()
{
	if (BodyRec* r = RecOf(this)) return b2Rot_GetAngle(b2Body_GetRotation(r->Id)) / kDeg;
	Vec2 p; float a;
	World2D(m_pGameObject, p, a);
	return a / kDeg;
}

void Rigidbody2D::AddForce(const Vec2& f, bool impulse)
{
	BodyRec* r = RecOf(this);
	if (!r || Type != BodyType::Dynamic) return;
	if (impulse) b2Body_ApplyLinearImpulseToCenter(r->Id, B(f), true);
	else b2Body_ApplyForceToCenter(r->Id, B(f), true);
}

void Rigidbody2D::AddForceAtPosition(const Vec2& f, const Vec2& worldPoint, bool impulse)
{
	BodyRec* r = RecOf(this);
	if (!r || Type != BodyType::Dynamic) return;
	if (impulse) b2Body_ApplyLinearImpulse(r->Id, B(f), B(worldPoint), true);
	else b2Body_ApplyForce(r->Id, B(f), B(worldPoint), true);
}

void Rigidbody2D::AddTorque(float t, bool impulse)
{
	BodyRec* r = RecOf(this);
	if (!r || Type != BodyType::Dynamic) return;
	// Unity 의 토크 단위는 Box2D 와 같다 (라디안 기준)
	if (impulse) b2Body_ApplyAngularImpulse(r->Id, t, true);
	else b2Body_ApplyTorque(r->Id, t, true);
}

void Rigidbody2D::MovePosition(const Vec2& p)
{
	MoveTarget = p;
	HasMoveTarget = true;
}

void Rigidbody2D::MoveRotation(float degrees)
{
	MoveAngle = degrees;
	HasMoveAngle = true;
}

void Rigidbody2D::ApplyDynamicSettings()
{
	if (BodyRec* r = RecOf(this))
		ApplyBodySettings(*r);
}
