// Jolt Physics 백엔드.
// 이 파일은 미리 컴파일된 헤더(pch)를 쓰지 않는다: Windows.h 의 min/max 매크로가 정의되기 전에 Jolt 헤더를 먼저 포함해야 하기 때문.
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/EmptyShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Geometry/AABox.h>

#include <mutex>
#include <thread>
#include <unordered_set>

#include "pch.h"
#include "PhysicsManager.h"
#include "MonoBehaviour.h"
#include "RigidBody.h"
#include "BoxCollider.h"
#include "SphereCollider.h"
#include "CapsuleCollider.h"
#include "MeshCollider.h"
#include "Mesh.h"

SINGLE_BODY(PhysicsManager)

namespace
{
	// ------------------------------------------------------------------ 변환 도우미
	inline JPH::Vec3 ToJ(const Vec3& v) { return JPH::Vec3(v.x, v.y, v.z); }
	inline JPH::RVec3 ToJR(const Vec3& v) { return JPH::RVec3(v.x, v.y, v.z); }
	inline JPH::Quat ToJ(const Quaternion& q) { JPH::Quat r(q.x, q.y, q.z, q.w); return r.Normalized(); }
	inline Vec3 FromJ(JPH::Vec3Arg v) { return Vec3(v.GetX(), v.GetY(), v.GetZ()); }
#ifdef JPH_DOUBLE_PRECISION
	inline Vec3 FromJ(JPH::RVec3Arg v) { return Vec3((float)v.GetX(), (float)v.GetY(), (float)v.GetZ()); }
#endif
	inline Quaternion FromJ(JPH::QuatArg q) { return Quaternion(q.GetX(), q.GetY(), q.GetZ(), q.GetW()); }

	// ------------------------------------------------------------------ 레이어
	namespace Layers
	{
		constexpr JPH::ObjectLayer NonMoving = 0;
		constexpr JPH::ObjectLayer Moving = 1;
		constexpr JPH::ObjectLayer Count = 2;
	}

	class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface
	{
	public:
		JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
		JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
		{
			return JPH::BroadPhaseLayer(layer == Layers::NonMoving ? 0 : 1);
		}
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
		const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
		{
			return (JPH::BroadPhaseLayer::Type)layer == 0 ? "NON_MOVING" : "MOVING";
		}
#endif
	};

	class ObjectVsBroadPhaseFilter final : public JPH::ObjectVsBroadPhaseLayerFilter
	{
	public:
		bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer bp) const override
		{
			return layer == Layers::NonMoving ? (JPH::BroadPhaseLayer::Type)bp == 1 : true;
		}
	};

	class ObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter
	{
	public:
		bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override
		{
			return !(a == Layers::NonMoving && b == Layers::NonMoving);
		}
	};

	// 형상 사용자 데이터 = 콜라이더 컴포넌트 ID (0 과 구분하기 위해 상위 비트 표시)
	constexpr JPH::uint64 kColliderTag = 1ull << 40;
	inline JPH::uint64 EncodeCollider(int id) { return kColliderTag | (JPH::uint64)(JPH::uint32)id; }
	inline bool DecodeCollider(JPH::uint64 data, JPH::uint32& id)
	{
		if ((data & kColliderTag) == 0)
			return false;
		id = (JPH::uint32)(data & 0xffffffffull);
		return true;
	}
	inline JPH::uint64 PairKey(JPH::uint32 a, JPH::uint32 b)
	{
		return a < b ? ((JPH::uint64)a << 32) | b : ((JPH::uint64)b << 32) | a;
	}

	bool IsActiveInHierarchy(GameObject* go)
	{
		for (GameObject* g = go; g != nullptr; g = g->GetParent())
			if (!g->IsActive())
				return false;
		return true;
	}

	void HashCombine(size_t& h, size_t v) { h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2); }
	void HashFloat(size_t& h, float f, float quantum = 0.0f)
	{
		if (quantum > 0.0f)
			f = roundf(f / quantum) * quantum;
		HashCombine(h, std::hash<float>()(f));
	}
	void HashVec(size_t& h, const Vec3& v, float quantum = 0.0f) { HashFloat(h, v.x, quantum); HashFloat(h, v.y, quantum); HashFloat(h, v.z, quantum); }
}

// ====================================================================== JoltWorld
struct PhysicsManager::JoltWorld
{
	// 콜라이더 테이블 (시뮬레이션 중 읽기 전용 → 작업 스레드에서 안전하게 조회)
	struct ColliderEntry
	{
		Collider* collider = nullptr;
		GameObject* owner = nullptr;     // 바디를 소유한 GameObject (Rigidbody 가 있는 부모일 수 있음)
		bool trigger = false;
		uint32 layerBit = 1;             // 콜라이더가 붙은 GameObject 의 레이어 비트
		uint32 excludeMask = 0;          // 콜라이더 + Rigidbody 의 Exclude Layers
		uint32 includeMask = 0;          // 콜라이더 + Rigidbody 의 Include Layers
		int priority = 0;                // Layer Override Priority
	};

	struct BodyRecord
	{
		GameObject* owner = nullptr;
		RigidBody* rb = nullptr;
		JPH::BodyID id;
		size_t signature = 0;
		bool dynamic = false;
		bool kinematic = false;
		bool interpolate = false;
		std::vector<Collider*> colliders;

		Vec3 lastPos;              // 마지막으로 Transform 과 동기화한 값 (사용자가 Transform 을 바꿨는지 판정)
		Quaternion lastRot;
		Vec3 prevPos, curPos;      // Interpolate 용
		Quaternion prevRot, curRot;
	};

	struct TouchInfo
	{
		JPH::uint32 a = 0, b = 0;
		bool trigger = false;
		JPH::BodyID bodyA, bodyB;
	};

	class ContactListener final : public JPH::ContactListener
	{
	public:
		JoltWorld* world = nullptr;

		void Handle(const JPH::Body& b1, const JPH::Body& b2, const JPH::ContactManifold& m, JPH::ContactSettings& s)
		{
			JPH::uint32 c1 = 0, c2 = 0;
			if (!DecodeCollider(b1.GetShape()->GetSubShapeUserData(m.mSubShapeID1), c1) ||
				!DecodeCollider(b2.GetShape()->GetSubShapeUserData(m.mSubShapeID2), c2))
				return;
			auto i1 = world->colliders.find(c1);
			auto i2 = world->colliders.find(c2);
			if (i1 == world->colliders.end() || i2 == world->colliders.end())
				return;

			const bool trigger = i1->second.trigger || i2->second.trigger;
			if (trigger)
				s.mIsSensor = true;   // 트리거: 통과시키고 이벤트만 보낸다
			else
			{
				// Unity: 충돌 메시지는 둘 중 하나가 Dynamic Rigidbody 일 때만
				if (b1.GetMotionType() != JPH::EMotionType::Dynamic && b2.GetMotionType() != JPH::EMotionType::Dynamic)
					return;
			}

			std::lock_guard<std::mutex> lock(world->touchMutex);
			TouchInfo& t = world->touching[PairKey(c1, c2)];
			t.a = c1;
			t.b = c2;
			t.trigger = trigger;
			t.bodyA = b1.GetID();
			t.bodyB = b2.GetID();
		}

		// Layer Overrides: 상대 레이어가 Exclude 에 있으면 접촉을 만들지 않는다.
		// 두 콜라이더의 설정이 충돌하면 Layer Override Priority 가 높은 쪽을 따른다 (Unity 와 동일).
		JPH::ValidateResult OnContactValidate(const JPH::Body& b1, const JPH::Body& b2, JPH::RVec3Arg, const JPH::CollideShapeResult& r) override
		{
			JPH::uint32 c1 = 0, c2 = 0;
			if (!DecodeCollider(b1.GetShape()->GetSubShapeUserData(r.mSubShapeID1), c1) ||
				!DecodeCollider(b2.GetShape()->GetSubShapeUserData(r.mSubShapeID2), c2))
				return JPH::ValidateResult::AcceptContact;
			auto i1 = world->colliders.find(c1);
			auto i2 = world->colliders.find(c2);
			if (i1 == world->colliders.end() || i2 == world->colliders.end())
				return JPH::ValidateResult::AcceptContact;
			const ColliderEntry& A = i1->second;
			const ColliderEntry& B = i2->second;

			// 각 콜라이더가 상대를 어떻게 보는지: -1 제외, +1 포함, 0 의견 없음
			auto opinion = [](const ColliderEntry& self, const ColliderEntry& other) {
				if (self.excludeMask & other.layerBit) return -1;
				if (self.includeMask & other.layerBit) return 1;
				return 0;
			};
			const int a = opinion(A, B), b = opinion(B, A);
			int decision = 0;
			if (a != 0 && b != 0 && a != b)
				decision = A.priority >= B.priority ? a : b;
			else
				decision = a != 0 ? a : b;
			return decision < 0 ? JPH::ValidateResult::RejectContact : JPH::ValidateResult::AcceptContact;
		}

		void OnContactAdded(const JPH::Body& b1, const JPH::Body& b2, const JPH::ContactManifold& m, JPH::ContactSettings& s) override { Handle(b1, b2, m, s); }
		void OnContactPersisted(const JPH::Body& b1, const JPH::Body& b2, const JPH::ContactManifold& m, JPH::ContactSettings& s) override { Handle(b1, b2, m, s); }
	};

	BroadPhaseLayers bpLayers;
	ObjectVsBroadPhaseFilter objVsBp;
	ObjectLayerPairFilter objPair;
	ContactListener listener;

	std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator;
	std::unique_ptr<JPH::JobSystemThreadPool> jobSystem;
	std::unique_ptr<JPH::PhysicsSystem> physics;

	std::unordered_map<JPH::uint64, BodyRecord> bodies;              // key: 소유 GameObject 의 InstanceID
	std::unordered_map<JPH::uint32, ColliderEntry> colliders;        // key: 콜라이더 컴포넌트 ID
	std::unordered_map<RigidBody*, JPH::uint64> rigidToOwner;

	std::mutex touchMutex;
	std::unordered_map<JPH::uint64, TouchInfo> touching;             // 이번 스텝에 닿아 있는 콜라이더 쌍
	std::unordered_map<JPH::uint64, TouchInfo> prevTouching;         // 지난 스텝

	JPH::BodyInterface& BI() { return physics->GetBodyInterface(); }

	BodyRecord* Find(RigidBody* rb)
	{
		auto it = rigidToOwner.find(rb);
		if (it == rigidToOwner.end())
			return nullptr;
		auto b = bodies.find(it->second);
		return b == bodies.end() || b->second.id.IsInvalid() ? nullptr : &b->second;
	}
};

// ====================================================================== 형상 만들기
namespace
{
	using World = PhysicsManager::JoltWorld;

	// Rigidbody 가 붙은 가장 가까운 조상(자기 자신 포함). 없으면 nullptr
	GameObject* FindRigidOwner(GameObject* go)
	{
		for (GameObject* g = go; g != nullptr; g = g->GetParent())
			if (RigidBody* rb = g->GetComponent<RigidBody>())
				return g;
		return nullptr;
	}

	void CollectColliders(GameObject* go, std::vector<Collider*>& out)
	{
		for (const auto& c : go->GetComponents())
		{
			Collider* col = dynamic_cast<Collider*>(c.get());
			if (col == nullptr || !col->IsEnabled())
				continue;
			if (dynamic_cast<BoxCollider*>(col) || dynamic_cast<SphereCollider*>(col) || dynamic_cast<CapsuleCollider*>(col) || dynamic_cast<MeshCollider*>(col))
				out.push_back(col);
		}
	}

	// 콜라이더 하나를 형상 설정으로 만든다. 위치/회전은 월드 기준으로 돌려준다.
	JPH::Ref<JPH::ShapeSettings> MakeLeaf(Collider* col, bool mustBeConvex, Vec3& outPos, Quaternion& outRot)
	{
		Transform* tr = col->GetGameObject()->GetTransform();
		const Matrix world = tr->GetWorldMatrix();
		const Vec3 scale = tr->GetScale();
		const Vec3 absScale(fabsf(scale.x), fabsf(scale.y), fabsf(scale.z));
		outRot = tr->GetRotation();
		outPos = Vec3::Transform(col->GetCenter(), world);

		JPH::Ref<JPH::ShapeSettings> leaf;
		if (auto* box = dynamic_cast<BoxCollider*>(col))
		{
			Vec3 size = box->GetSize();
			Vec3 half(fabsf(size.x) * absScale.x * 0.5f, fabsf(size.y) * absScale.y * 0.5f, fabsf(size.z) * absScale.z * 0.5f);
			half = Vec3::Max(half, Vec3(0.001f, 0.001f, 0.001f));
			float minHalf = (std::min)((std::min)(half.x, half.y), half.z);
			leaf = new JPH::BoxShapeSettings(ToJ(half), (std::min)(JPH::cDefaultConvexRadius, minHalf * 0.5f));
		}
		else if (auto* sphere = dynamic_cast<SphereCollider*>(col))
		{
			float r = sphere->GetRadius() * (std::max)((std::max)(absScale.x, absScale.y), absScale.z);
			leaf = new JPH::SphereShapeSettings((std::max)(r, 0.001f));
		}
		else if (auto* capsule = dynamic_cast<CapsuleCollider*>(col))
		{
			float r, half;
			capsule->GetScaledDimensions(scale, r, half);
			r = (std::max)(r, 0.001f);
			if (half <= 1e-4f)
				leaf = new JPH::SphereShapeSettings(r);
			else
			{
				leaf = new JPH::CapsuleShapeSettings(half, r);
				// Jolt 캡슐은 Y 축 방향 → Direction 에 맞게 회전
				JPH::Quat extra = JPH::Quat::sIdentity();
				if (capsule->GetDirection() == 0) extra = JPH::Quat::sRotation(JPH::Vec3::sAxisZ(), -0.5f * JPH::JPH_PI);
				if (capsule->GetDirection() == 2) extra = JPH::Quat::sRotation(JPH::Vec3::sAxisX(), 0.5f * JPH::JPH_PI);
				outRot = FromJ((ToJ(outRot) * extra).Normalized());
			}
		}
		else if (auto* meshCol = dynamic_cast<MeshCollider*>(col))
		{
			outPos = tr->GetPosition();   // Mesh Collider 는 Center 가 없다
			Mesh* mesh = meshCol->GetMesh();
			if (mesh == nullptr || mesh->Vertices.empty() || mesh->Indices.size() < 3)
				return nullptr;

			// 사용할 인덱스 범위 (서브셋별 VertexStart 보정)
			std::vector<std::pair<size_t, size_t>> ranges;   // (index start, count)
			std::vector<JPH::uint32> bases;
			if (mesh->Subsets.empty())
			{
				ranges.push_back({ 0, mesh->Indices.size() });
				bases.push_back(0);
			}
			else
				for (const auto& s : mesh->Subsets)
				{
					ranges.push_back({ (size_t)s.FaceStart * 3, (size_t)s.FaceCount * 3 });
					bases.push_back(s.VertexStart);
				}

			auto scaled = [&](size_t vi) {
				const auto& p = mesh->Vertices[(std::min)(vi, mesh->Vertices.size() - 1)].pos;
				return JPH::Vec3(p.x * scale.x, p.y * scale.y, p.z * scale.z);
			};

			if (meshCol->IsConvex() || mustBeConvex)
			{
				JPH::Array<JPH::Vec3> points;
				size_t stride = (std::max)((size_t)1, mesh->Vertices.size() / 1500);
				for (size_t i = 0; i < mesh->Vertices.size(); i += stride)
					points.push_back(scaled(i));
				JPH::Ref<JPH::ConvexHullShapeSettings> hull = new JPH::ConvexHullShapeSettings(points);
				if (!hull->Create().HasError())
					leaf = hull;
				else
				{
					// 평면처럼 납작한 메시: 얇은 상자로 대체
					JPH::AABox box;
					for (const auto& p : points) box.Encapsulate(p);
					JPH::Vec3 half = JPH::Vec3::sMax(box.GetExtent(), JPH::Vec3::sReplicate(0.01f));
					JPH::Ref<JPH::BoxShapeSettings> bs = new JPH::BoxShapeSettings(half, 0.005f);
					JPH::Vec3 c = box.GetCenter();
					leaf = new JPH::RotatedTranslatedShapeSettings(c, JPH::Quat::sIdentity(), bs);
					bs->mUserData = EncodeCollider(col->GetInstanceID());
				}
			}
			else
			{
				JPH::VertexList verts;
				verts.reserve(mesh->Vertices.size());
				for (size_t i = 0; i < mesh->Vertices.size(); ++i)
				{
					JPH::Vec3 p = scaled(i);
					verts.push_back(JPH::Float3(p.GetX(), p.GetY(), p.GetZ()));
				}
				JPH::IndexedTriangleList tris;
				for (size_t r = 0; r < ranges.size(); ++r)
					for (size_t n = ranges[r].first; n + 3 <= ranges[r].first + ranges[r].second && n + 3 <= mesh->Indices.size(); n += 3)
						tris.push_back(JPH::IndexedTriangle(mesh->Indices[n] + bases[r], mesh->Indices[n + 1] + bases[r], mesh->Indices[n + 2] + bases[r]));
				// 음수 스케일이면 삼각형 방향이 뒤집힌다
				if (scale.x * scale.y * scale.z < 0.0f)
					for (auto& t : tris) std::swap(t.mIdx[1], t.mIdx[2]);
				leaf = new JPH::MeshShapeSettings(std::move(verts), std::move(tris));
			}
		}

		if (leaf != nullptr)
			leaf->mUserData = EncodeCollider(col->GetInstanceID());
		return leaf;
	}

	// 바디의 형상 (여러 콜라이더면 StaticCompound). 콜라이더가 없으면 nullptr
	JPH::RefConst<JPH::Shape> BuildShape(const std::vector<Collider*>& cols, const Vec3& bodyPos, const Quaternion& bodyRot, bool dynamic)
	{
		const JPH::Quat invBody = ToJ(bodyRot).Conjugated();
		struct Part { JPH::Ref<JPH::ShapeSettings> s; JPH::Vec3 pos; JPH::Quat rot; };
		std::vector<Part> parts;
		for (Collider* c : cols)
		{
			Vec3 p;
			Quaternion r;
			JPH::Ref<JPH::ShapeSettings> leaf = MakeLeaf(c, dynamic, p, r);
			if (leaf == nullptr)
				continue;
			Part part;
			part.s = leaf;
			part.pos = invBody * (ToJ(p) - ToJ(bodyPos));
			part.rot = (invBody * ToJ(r)).Normalized();
			parts.push_back(part);
		}
		if (parts.empty())
			return nullptr;

		JPH::Ref<JPH::ShapeSettings> final;
		if (parts.size() == 1)
		{
			const Part& p = parts[0];
			const bool identity = p.pos.LengthSq() < 1e-10f && p.rot.IsClose(JPH::Quat::sIdentity(), 1e-8f);
			final = identity ? p.s : JPH::Ref<JPH::ShapeSettings>(new JPH::RotatedTranslatedShapeSettings(p.pos, p.rot, p.s));
		}
		else
		{
			JPH::Ref<JPH::StaticCompoundShapeSettings> compound = new JPH::StaticCompoundShapeSettings();
			for (const Part& p : parts)
				compound->AddShape(p.pos, p.rot, p.s);
			final = compound;
		}

		JPH::ShapeSettings::ShapeResult result = final->Create();
		if (result.HasError())
		{
			OutputDebugStringA(("[Physics] Shape error: " + std::string(result.GetError().c_str()) + "\n").c_str());
			return nullptr;
		}
		return result.Get();
	}

	size_t ComputeSignature(GameObject* owner, RigidBody* rb, const std::vector<Collider*>& cols)
	{
		size_t h = 0;
		HashVec(h, owner->GetTransform()->GetScale(), 1e-4f);
		if (rb != nullptr)
		{
			HashCombine(h, 1);
			HashFloat(h, rb->GetMass());
			HashFloat(h, rb->GetLinearDamping());
			HashFloat(h, rb->GetAngularDamping());
			HashCombine(h, rb->GetUseGravity());
			HashCombine(h, rb->IsKinematic());
			HashCombine(h, (size_t)rb->GetInterpolation());
			HashCombine(h, (size_t)rb->GetCollisionDetection());
			HashCombine(h, rb->GetExcludeLayers());
			HashCombine(h, rb->GetIncludeLayers());
			for (int i = 0; i < 3; ++i)
			{
				HashCombine(h, rb->IsPositionFrozen(i));
				HashCombine(h, rb->IsRotationFrozen(i));
			}
		}
		for (Collider* c : cols)
		{
			HashCombine(h, (size_t)c);
			HashCombine(h, c->IsTrigger());
			HashCombine(h, c->GetExcludeLayers());
			HashCombine(h, c->GetIncludeLayers());
			HashCombine(h, (size_t)c->GetLayerOverridePriority());
			HashCombine(h, c->GetGameObject()->GetLayerIndex());
			HashVec(h, c->GetCenter());
			if (auto* b = dynamic_cast<BoxCollider*>(c)) HashVec(h, b->GetSize());
			if (auto* s = dynamic_cast<SphereCollider*>(c)) HashFloat(h, s->GetRadius());
			if (auto* k = dynamic_cast<CapsuleCollider*>(c)) { HashFloat(h, k->GetRadius()); HashFloat(h, k->GetHeight()); HashCombine(h, k->GetDirection()); }
			if (auto* m = dynamic_cast<MeshCollider*>(c)) { HashCombine(h, (size_t)m->GetMesh()); HashCombine(h, m->IsConvex()); }
			// 자식 콜라이더: 소유자까지의 로컬 변환 체인 (정확한 값이라 흔들리지 않음)
			for (GameObject* g = c->GetGameObject(); g != nullptr && g != owner; g = g->GetParent())
			{
				Transform* t = g->GetTransform();
				HashVec(h, t->GetLocalPosition());
				HashVec(h, t->GetLocalEulerAngles());
				HashVec(h, t->GetLocalScale());
			}
		}
		return h;
	}

	// (개발/검증용) NOVA_PHYSICS_LOG=<파일 경로> 이면 이벤트와 바디 위치를 기록한다
	FILE* PhysLog()
	{
		static FILE* fp = nullptr;
		static bool checked = false;
		if (!checked)
		{
			checked = true;
			char path[512] = {};
			if (::GetEnvironmentVariableA("NOVA_PHYSICS_LOG", path, sizeof(path)) > 0)
				fopen_s(&fp, path, "w");
		}
		return fp;
	}

	bool NearlyEqual(const Vec3& a, const Vec3& b) { return (a - b).LengthSquared() < 1e-10f; }
	bool NearlyEqual(const Quaternion& a, const Quaternion& b) { return fabsf(a.Dot(b)) > 1.0f - 1e-7f; }

	void DispatchToObject(GameObject* go, Collider* other, bool trigger, int kind)
	{
		if (go == nullptr)
			return;
		for (const auto& component : go->GetComponents())
		{
			MonoBehaviour* script = dynamic_cast<MonoBehaviour*>(component.get());
			if (script == nullptr || !script->IsEnabled())
				continue;
			if (trigger)
			{
				if (kind == 0) script->OnTriggerEnter(other);
				else if (kind == 1) script->OnTriggerStay(other);
				else script->OnTriggerExit(other);
			}
			else
			{
				if (kind == 0) script->OnCollisionEnter(other);
				else if (kind == 1) script->OnCollisionStay(other);
				else script->OnCollisionExit(other);
			}
		}
	}
}

// ====================================================================== PhysicsManager
PhysicsManager::PhysicsManager()
{
}

PhysicsManager::~PhysicsManager()
{
	Exit();
}

void PhysicsManager::Init()
{
	if (m_JoltInitialized)
		return;
	JPH::RegisterDefaultAllocator();
	JPH::Factory::sInstance = new JPH::Factory();
	JPH::RegisterTypes();
	m_JoltInitialized = true;
}

void PhysicsManager::SetGravity(const Vec3& gravity)
{
	m_Gravity = gravity;
	if (m_World)
		m_World->physics->SetGravity(ToJ(gravity));
}

void PhysicsManager::Start()
{
	Init();
	Exit();

	m_World = std::make_unique<JoltWorld>();
	JoltWorld& w = *m_World;
	w.listener.world = &w;
	w.tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(16 * 1024 * 1024);
	int threads = (std::max)(1, (int)std::thread::hardware_concurrency() - 1);
	w.jobSystem = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, threads);
	w.physics = std::make_unique<JPH::PhysicsSystem>();
	w.physics->Init(8192, 0, 8192, 8192, w.bpLayers, w.objVsBp, w.objPair);
	w.physics->SetContactListener(&w.listener);
	w.physics->SetGravity(ToJ(m_Gravity));
	{
		// 정지 접촉 시 허용하는 겹침 (Jolt 기본 2cm → 5mm). Unity 처럼 물체가 바닥에 눈에 띄게 파묻히지 않게 한다
		JPH::PhysicsSettings settings = w.physics->GetPhysicsSettings();
		settings.mPenetrationSlop = 0.005f;
		w.physics->SetPhysicsSettings(settings);
	}
	// Unity(PhysX) 기본값과 같이 마찰/반발 계수는 두 값의 평균으로 합친다 (Jolt 기본: 마찰 = 기하 평균, 반발 = 최댓값)
	w.physics->SetCombineFriction([](const JPH::Body& a, const JPH::SubShapeID&, const JPH::Body& b, const JPH::SubShapeID&) { return 0.5f * (a.GetFriction() + b.GetFriction()); });
	w.physics->SetCombineRestitution([](const JPH::Body& a, const JPH::SubShapeID&, const JPH::Body& b, const JPH::SubShapeID&) { return 0.5f * (a.GetRestitution() + b.GetRestitution()); });
	m_Accumulator = 0.0f;
	m_StepCount = 0;

	StepSimulation(0.0f);   // dt 0: 바디만 만든다
}

void PhysicsManager::Exit()
{
	if (!m_World)
		return;
	JoltWorld& w = *m_World;
	JPH::BodyInterface& bi = w.BI();
	for (auto& kv : w.bodies)
	{
		if (kv.second.rb)
			kv.second.rb->_SetBodyId(0xffffffff);
		if (!kv.second.id.IsInvalid())
		{
			bi.RemoveBody(kv.second.id);
			bi.DestroyBody(kv.second.id);
		}
	}
	w.bodies.clear();
	m_World.reset();
}

// 매 프레임: 고정 간격으로 나누어 시뮬레이션 (Unity 의 Fixed Timestep)
void PhysicsManager::Update(float deltaTime)
{
	if (!m_World)
		return;

	m_Accumulator += (std::min)(deltaTime, m_MaxAllowedTimestep);
	int steps = 0;
	while (m_Accumulator >= m_FixedTimestep && steps < 8)
	{
		StepSimulation(m_FixedTimestep);
		m_Accumulator -= m_FixedTimestep;
		++steps;
		if (!m_World)
			return;
	}
	if (steps == 8)
		m_Accumulator = 0.0f;

	// Interpolate: 두 스텝 사이 위치를 보간해 Transform 에 쓴다
	const float alpha = std::clamp(m_Accumulator / m_FixedTimestep, 0.0f, 1.0f);
	for (auto& kv : m_World->bodies)
	{
		JoltWorld::BodyRecord& r = kv.second;
		if (!r.dynamic || !r.interpolate || r.id.IsInvalid())
			continue;
		Transform* tr = r.owner->GetTransform();
		tr->SetPosition(Vec3::Lerp(r.prevPos, r.curPos, alpha));
		tr->SetRotation(Quaternion::Slerp(r.prevRot, r.curRot, alpha));
		r.lastPos = tr->GetPosition();
		r.lastRot = tr->GetRotation();
	}
}

void PhysicsManager::StepSimulation(float dt)
{
	Scene* scene = SceneManager::GetI()->GetCurrentScene();
	if (scene == nullptr || !m_World)
		return;
	JoltWorld& w = *m_World;
	JPH::BodyInterface& bi = w.BI();
	std::vector<GameObject*> all = scene->GetAllGameObjects();

	// 1) FixedUpdate (Unity: 물리 시뮬레이션 직전)
	if (dt > 0.0f)
	{
		for (GameObject* go : all)
		{
			if (!IsActiveInHierarchy(go))
				continue;
			for (const auto& c : go->GetComponents())
			{
				if (MonoBehaviour* mb = dynamic_cast<MonoBehaviour*>(c.get()))
					if (!mb->IsEnabled())
						continue;
				c->FixedUpdate();
			}
		}
		all = scene->GetAllGameObjects();
	}

	// 2) 바디 동기화: 소유자별 콜라이더 모으기
	std::unordered_map<GameObject*, std::vector<Collider*>> owned;
	for (GameObject* go : all)
	{
		if (!IsActiveInHierarchy(go))
			continue;
		GameObject* owner = FindRigidOwner(go);
		std::vector<Collider*> cols;
		CollectColliders(go, cols);
		if (owner == nullptr)
		{
			if (!cols.empty())
				owned[go].insert(owned[go].end(), cols.begin(), cols.end());
		}
		else
		{
			auto& list = owned[owner];
			list.insert(list.end(), cols.begin(), cols.end());
		}
	}

	// 사라진 소유자 제거
	std::unordered_map<JPH::uint64, GameObject*> ownedIds;
	for (auto& kv : owned)
		ownedIds[kv.first->GetInstanceID()] = kv.first;
	for (auto it = w.bodies.begin(); it != w.bodies.end();)
	{
		auto found = ownedIds.find(it->first);
		const bool alive = found != ownedIds.end() && found->second == it->second.owner;
		if (!alive)
		{
			if (!it->second.id.IsInvalid())
			{
				bi.RemoveBody(it->second.id);
				bi.DestroyBody(it->second.id);
			}
			it = w.bodies.erase(it);
		}
		else
			++it;
	}

	// 생성 / 재생성
	w.colliders.clear();
	w.rigidToOwner.clear();
	for (auto& kv : owned)
	{
		GameObject* owner = kv.first;
		RigidBody* rb = owner->GetComponent<RigidBody>();
		const std::vector<Collider*>& cols = kv.second;
		const JPH::uint64 key = owner->GetInstanceID();
		const size_t sig = ComputeSignature(owner, rb, cols);

		for (Collider* c : cols)
		{
			JoltWorld::ColliderEntry e;
			e.collider = c;
			e.owner = owner;
			// Unity: 볼록이 아닌 Mesh Collider 는 트리거가 될 수 없다
			MeshCollider* mc = dynamic_cast<MeshCollider*>(c);
			e.trigger = c->IsTrigger() && !(mc != nullptr && !mc->IsConvex());
			e.layerBit = 1u << (c->GetGameObject()->GetLayerIndex() & 31);
			e.excludeMask = c->GetExcludeLayers() | (rb ? rb->GetExcludeLayers() : 0u);
			e.includeMask = c->GetIncludeLayers() | (rb ? rb->GetIncludeLayers() : 0u);
			e.priority = c->GetLayerOverridePriority();
			w.colliders[(JPH::uint32)c->GetInstanceID()] = e;
		}

		auto existing = w.bodies.find(key);
		if (existing != w.bodies.end() && existing->second.signature == sig)
		{
			if (rb) w.rigidToOwner[rb] = key;
			continue;
		}

		Vec3 keepVel = Vec3::Zero, keepAng = Vec3::Zero;
		if (existing != w.bodies.end())
		{
			if (!existing->second.id.IsInvalid())
			{
				keepVel = FromJ(bi.GetLinearVelocity(existing->second.id));
				keepAng = FromJ(bi.GetAngularVelocity(existing->second.id));
				bi.RemoveBody(existing->second.id);
				bi.DestroyBody(existing->second.id);
			}
			w.bodies.erase(existing);
		}

		Transform* tr = owner->GetTransform();
		const Vec3 pos = tr->GetPosition();
		const Quaternion rot = tr->GetRotation();

		bool frozenAll = false;
		JPH::uint8 dofs = 0x3f;
		if (rb)
		{
			for (int i = 0; i < 3; ++i)
			{
				if (rb->IsPositionFrozen(i)) dofs &= ~(JPH::uint8)(1u << i);
				if (rb->IsRotationFrozen(i)) dofs &= ~(JPH::uint8)(1u << (i + 3));
			}
			frozenAll = dofs == 0;
		}
		const bool kinematic = rb && (rb->IsKinematic() || frozenAll);
		const bool dynamic = rb && !kinematic;

		JPH::RefConst<JPH::Shape> shape = BuildShape(cols, pos, rot, dynamic);
		const bool emptyShape = shape == nullptr;
		if (emptyShape)
		{
			if (rb == nullptr)
				continue;   // 콜라이더 없는 정적 오브젝트는 바디가 필요 없다
			shape = JPH::EmptyShapeSettings().Create().Get();
		}

		JPH::BodyCreationSettings bcs(shape, ToJR(pos), ToJ(rot),
			rb == nullptr ? JPH::EMotionType::Static : (kinematic ? JPH::EMotionType::Kinematic : JPH::EMotionType::Dynamic),
			rb == nullptr ? Layers::NonMoving : Layers::Moving);
		bcs.mFriction = 0.6f;       // Unity 기본 Physics Material
		bcs.mRestitution = 0.0f;
		bcs.mUserData = key;
		if (rb)
		{
			bcs.mLinearDamping = rb->GetLinearDamping();
			bcs.mAngularDamping = rb->GetAngularDamping();
			bcs.mGravityFactor = rb->GetUseGravity() ? 1.0f : 0.0f;
			bcs.mMaxAngularVelocity = 50.0f;   // Unity Physics.defaultMaxAngularSpeed (rad/s)
			bcs.mCollideKinematicVsNonDynamic = kinematic;   // 키네마틱 ↔ 정적 트리거 이벤트 (Unity 와 동일)
			if (rb->GetCollisionDetection() != RigidBody::CollisionDetection::Discrete)
				bcs.mMotionQuality = JPH::EMotionQuality::LinearCast;
			if (dynamic)
			{
				bcs.mAllowedDOFs = (JPH::EAllowedDOFs)dofs;
				if (emptyShape)
				{
					bcs.mOverrideMassProperties = JPH::EOverrideMassProperties::MassAndInertiaProvided;
					bcs.mMassPropertiesOverride.mMass = rb->GetMass();
					bcs.mMassPropertiesOverride.mInertia = JPH::Mat44::sScale(JPH::Vec3::sReplicate(0.1f * rb->GetMass()));
				}
				else
				{
					bcs.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
					bcs.mMassPropertiesOverride.mMass = rb->GetMass();
				}
			}
		}

		JPH::BodyID id = bi.CreateAndAddBody(bcs, dynamic ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
		if (id.IsInvalid())
			continue;

		JoltWorld::BodyRecord rec;
		rec.owner = owner;
		rec.rb = rb;
		rec.id = id;
		rec.signature = sig;
		rec.dynamic = dynamic;
		rec.kinematic = kinematic;
		rec.interpolate = rb && rb->GetInterpolation() != RigidBody::Interpolation::None;
		rec.colliders = cols;
		rec.lastPos = rec.prevPos = rec.curPos = pos;
		rec.lastRot = rec.prevRot = rec.curRot = rot;
		if (rb)
		{
			rb->_SetBodyId(id.GetIndexAndSequenceNumber());
			if (dynamic)
			{
				Vec3 v = keepVel + rb->_TakePendingVelocity();
				Vec3 a = keepAng + rb->_TakePendingAngularVelocity();
				bi.SetLinearAndAngularVelocity(id, ToJ(v), ToJ(a));
			}
			w.rigidToOwner[rb] = key;
		}
		w.bodies[key] = rec;
	}

	if (dt <= 0.0f)
		return;

	// 3) Transform → 바디 (사용자가 옮긴 경우 / 키네마틱 / 정적)
	for (auto& kv : w.bodies)
	{
		JoltWorld::BodyRecord& r = kv.second;
		if (r.id.IsInvalid())
			continue;
		Transform* tr = r.owner->GetTransform();
		const Vec3 pos = tr->GetPosition();
		const Quaternion rot = tr->GetRotation();
		const bool moved = !NearlyEqual(pos, r.lastPos) || !NearlyEqual(rot, r.lastRot);
		if (r.kinematic)
			bi.MoveKinematic(r.id, ToJR(pos), ToJ(rot), dt);
		else if (moved)
			bi.SetPositionAndRotation(r.id, ToJR(pos), ToJ(rot), r.dynamic ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
		r.lastPos = pos;
		r.lastRot = rot;
		if (moved)
		{
			r.prevPos = r.curPos = pos;
			r.prevRot = r.curRot = rot;
		}
	}

	// 4) 시뮬레이션
	{
		std::lock_guard<std::mutex> lock(w.touchMutex);
		w.touching.clear();
	}
	w.physics->Update(dt, 1, w.tempAllocator.get(), w.jobSystem.get());

	// 5) 바디 → Transform (Dynamic)
	for (auto& kv : w.bodies)
	{
		JoltWorld::BodyRecord& r = kv.second;
		if (!r.dynamic || r.id.IsInvalid())
			continue;
		JPH::RVec3 p;
		JPH::Quat q;
		bi.GetPositionAndRotation(r.id, p, q);
		r.prevPos = r.curPos;
		r.prevRot = r.curRot;
		r.curPos = Vec3((float)p.GetX(), (float)p.GetY(), (float)p.GetZ());
		r.curRot = FromJ(q);
		if (!r.interpolate)
		{
			Transform* tr = r.owner->GetTransform();
			tr->SetPosition(r.curPos);
			tr->SetRotation(r.curRot);
			r.lastPos = tr->GetPosition();
			r.lastRot = tr->GetRotation();
		}
	}

	// 6) 충돌 / 트리거 이벤트 (Enter / Stay / Exit)
	std::unordered_map<JPH::uint64, JoltWorld::TouchInfo> current;
	{
		std::lock_guard<std::mutex> lock(w.touchMutex);
		current = w.touching;
	}
	// 잠든 바디끼리 닿아 있던 쌍은 콜백이 오지 않으므로 그대로 유지한다 (Unity 도 잠든 동안 Stay 를 보내지 않음)
	std::vector<JoltWorld::TouchInfo> exits;
	for (auto& kv : w.prevTouching)
	{
		if (current.count(kv.first))
			continue;
		const JoltWorld::TouchInfo& t = kv.second;
		const bool bothExist = w.colliders.count(t.a) && w.colliders.count(t.b);
		const bool asleep = bothExist && !bi.IsActive(t.bodyA) && !bi.IsActive(t.bodyB) && bi.IsAdded(t.bodyA) && bi.IsAdded(t.bodyB);
		if (asleep)
			current[kv.first] = t;
		else
			exits.push_back(t);
	}

	auto dispatch = [&](const JoltWorld::TouchInfo& t, int kind, bool sleepingStay) {
		auto ia = w.colliders.find(t.a);
		auto ib = w.colliders.find(t.b);
		if (ia == w.colliders.end() || ib == w.colliders.end() || sleepingStay)
			return;
		const auto& A = ia->second;
		const auto& B = ib->second;
		if (FILE* fp = PhysLog())
		{
			static const char* kinds[] = { "Enter", "Stay", "Exit" };
			if (kind != 1)
				fprintf(fp, "[step %d] On%s%s  %s <-> %s\n", m_StepCount, t.trigger ? "Trigger" : "Collision", kinds[kind],
					A.collider->GetGameObject()->GetName().c_str(), B.collider->GetGameObject()->GetName().c_str());
		}
		DispatchToObject(A.collider->GetGameObject(), B.collider, t.trigger, kind);
		if (A.owner != A.collider->GetGameObject()) DispatchToObject(A.owner, B.collider, t.trigger, kind);
		DispatchToObject(B.collider->GetGameObject(), A.collider, t.trigger, kind);
		if (B.owner != B.collider->GetGameObject()) DispatchToObject(B.owner, A.collider, t.trigger, kind);
	};

	for (auto& kv : current)
	{
		const bool isNew = w.prevTouching.count(kv.first) == 0;
		const bool sleeping = !isNew && !bi.IsActive(kv.second.bodyA) && !bi.IsActive(kv.second.bodyB);
		dispatch(kv.second, isNew ? 0 : 1, sleeping);
	}
	for (const auto& t : exits)
		dispatch(t, 2, false);

	w.prevTouching = std::move(current);

	++m_StepCount;
	if (FILE* fp = PhysLog())
	{
		if (m_StepCount == 1 || m_StepCount % 50 == 0)
		{
			fprintf(fp, "[step %d] t=%.2fs\n", m_StepCount, m_StepCount * dt);
			for (auto& kv : w.bodies)
			{
				const JoltWorld::BodyRecord& r = kv.second;
				if (!r.dynamic) continue;
				Transform* tr = r.owner->GetTransform();
				Vec3 p = tr->GetPosition();
				Vec3 e = tr->GetEulerAngle();
				fprintf(fp, "   %-12s pos(%7.3f %7.3f %7.3f) rot(%6.1f %6.1f %6.1f) %s\n", r.owner->GetName().c_str(), p.x, p.y, p.z, e.x, e.y, e.z,
					bi.IsActive(r.id) ? "awake" : "asleep");
			}
		}
		fflush(fp);
	}
}

// ====================================================================== Raycast
bool PhysicsManager::Raycast(const Vec3& origin, const Vec3& direction, RaycastHit& hit, float maxDistance, bool hitTriggers)
{
	if (!m_World)
		return false;
	Vec3 dir = direction;
	if (dir.LengthSquared() < 1e-12f)
		return false;
	dir.Normalize();

	JoltWorld& w = *m_World;
	JPH::RRayCast ray{ ToJR(origin), ToJ(dir * maxDistance) };
	JPH::RayCastSettings settings;
	JPH::AllHitCollisionCollector<JPH::CastRayCollector> collector;
	w.physics->GetNarrowPhaseQuery().CastRay(ray, settings, collector);
	if (!collector.HadHit())
		return false;
	collector.Sort();

	for (const JPH::RayCastResult& r : collector.mHits)
	{
		JPH::BodyLockRead lock(w.physics->GetBodyLockInterface(), r.mBodyID);
		if (!lock.Succeeded())
			continue;
		const JPH::Body& body = lock.GetBody();
		JPH::uint32 cid = 0;
		if (!DecodeCollider(body.GetShape()->GetSubShapeUserData(r.mSubShapeID2), cid))
			continue;
		auto it = w.colliders.find(cid);
		if (it == w.colliders.end())
			continue;
		if (it->second.trigger && !hitTriggers)
			continue;

		JPH::RVec3 point = ray.GetPointOnRay(r.mFraction);
		hit.point = Vec3((float)point.GetX(), (float)point.GetY(), (float)point.GetZ());
		hit.normal = FromJ(body.GetWorldSpaceSurfaceNormal(r.mSubShapeID2, point));
		hit.distance = r.mFraction * maxDistance;
		hit.collider = it->second.collider;
		hit.gameObject = it->second.collider->GetGameObject();
		return true;
	}
	return false;
}

// ====================================================================== RigidBody API
bool PhysicsManager::GetLinearVelocity(RigidBody* rb, Vec3& out)
{
	if (!m_World) return false;
	auto* r = m_World->Find(rb);
	if (!r) return false;
	out = FromJ(m_World->BI().GetLinearVelocity(r->id));
	return true;
}

bool PhysicsManager::SetLinearVelocity(RigidBody* rb, const Vec3& v)
{
	if (!m_World) return false;
	auto* r = m_World->Find(rb);
	if (!r) return false;
	if (r->dynamic)
		m_World->BI().SetLinearVelocity(r->id, ToJ(v));
	return true;
}

bool PhysicsManager::GetAngularVelocity(RigidBody* rb, Vec3& out)
{
	if (!m_World) return false;
	auto* r = m_World->Find(rb);
	if (!r) return false;
	out = FromJ(m_World->BI().GetAngularVelocity(r->id));
	return true;
}

bool PhysicsManager::SetAngularVelocity(RigidBody* rb, const Vec3& wv)
{
	if (!m_World) return false;
	auto* r = m_World->Find(rb);
	if (!r) return false;
	if (r->dynamic)
		m_World->BI().SetAngularVelocity(r->id, ToJ(wv));
	return true;
}

bool PhysicsManager::AddForce(RigidBody* rb, const Vec3& force, ForceMode mode)
{
	if (!m_World) return false;
	auto* r = m_World->Find(rb);
	if (!r) return false;
	if (!r->dynamic) return true;
	JPH::BodyInterface& bi = m_World->BI();
	const float mass = rb->GetMass();
	switch (mode)
	{
	case ForceMode::Force:          bi.AddForce(r->id, ToJ(force)); break;
	case ForceMode::Acceleration:   bi.AddForce(r->id, ToJ(force * mass)); break;
	case ForceMode::Impulse:        bi.AddImpulse(r->id, ToJ(force)); break;
	case ForceMode::VelocityChange: bi.AddImpulse(r->id, ToJ(force * mass)); break;
	}
	return true;
}

bool PhysicsManager::AddTorque(RigidBody* rb, const Vec3& torque, ForceMode mode)
{
	if (!m_World) return false;
	auto* r = m_World->Find(rb);
	if (!r) return false;
	if (!r->dynamic) return true;
	JPH::BodyInterface& bi = m_World->BI();
	switch (mode)
	{
	case ForceMode::Force:
		bi.AddTorque(r->id, ToJ(torque));
		break;
	case ForceMode::Impulse:
		bi.AddAngularImpulse(r->id, ToJ(torque));
		break;
	case ForceMode::Acceleration:
	case ForceMode::VelocityChange:
	{
		// 관성 텐서를 곱해 질량과 무관하게 만든다
		JPH::BodyLockRead lock(m_World->physics->GetBodyLockInterface(), r->id);
		if (!lock.Succeeded()) break;
		const JPH::Body& body = lock.GetBody();
		JPH::Mat44 invI = body.GetInverseInertia();
		JPH::Vec3 t = ToJ(torque);
		JPH::Vec3 scaled = invI.Inversed3x3() * t;
		lock.ReleaseLock();
		if (mode == ForceMode::Acceleration) bi.AddTorque(r->id, scaled);
		else bi.AddAngularImpulse(r->id, scaled);
		break;
	}
	}
	return true;
}

bool PhysicsManager::AddForceAtPosition(RigidBody* rb, const Vec3& force, const Vec3& position, ForceMode mode)
{
	if (!m_World) return false;
	auto* r = m_World->Find(rb);
	if (!r) return false;
	if (!r->dynamic) return true;
	JPH::BodyInterface& bi = m_World->BI();
	const float mass = rb->GetMass();
	switch (mode)
	{
	case ForceMode::Force:          bi.AddForce(r->id, ToJ(force), ToJR(position)); break;
	case ForceMode::Acceleration:   bi.AddForce(r->id, ToJ(force * mass), ToJR(position)); break;
	case ForceMode::Impulse:        bi.AddImpulse(r->id, ToJ(force), ToJR(position)); break;
	case ForceMode::VelocityChange: bi.AddImpulse(r->id, ToJ(force * mass), ToJR(position)); break;
	}
	return true;
}

bool PhysicsManager::MovePosition(RigidBody* rb, const Vec3& position)
{
	if (!m_World) return false;
	auto* r = m_World->Find(rb);
	if (!r) return false;
	Transform* tr = r->owner->GetTransform();
	if (r->kinematic)
	{
		tr->SetPosition(position);   // 다음 스텝에 MoveKinematic 으로 부드럽게 이동 (접촉 발생)
		return true;
	}
	m_World->BI().SetPosition(r->id, ToJR(position), JPH::EActivation::Activate);
	tr->SetPosition(position);
	r->lastPos = r->prevPos = r->curPos = tr->GetPosition();
	return true;
}

bool PhysicsManager::MoveRotation(RigidBody* rb, const Quaternion& rotation)
{
	if (!m_World) return false;
	auto* r = m_World->Find(rb);
	if (!r) return false;
	Transform* tr = r->owner->GetTransform();
	if (r->kinematic)
	{
		tr->SetRotation(rotation);
		return true;
	}
	m_World->BI().SetRotation(r->id, ToJ(rotation), JPH::EActivation::Activate);
	tr->SetRotation(rotation);
	r->lastRot = r->prevRot = r->curRot = tr->GetRotation();
	return true;
}

bool PhysicsManager::IsSleeping(RigidBody* rb)
{
	if (!m_World) return false;
	auto* r = m_World->Find(rb);
	return r && r->dynamic && !m_World->BI().IsActive(r->id);
}

void PhysicsManager::Sleep(RigidBody* rb)
{
	if (!m_World) return;
	if (auto* r = m_World->Find(rb))
		m_World->BI().DeactivateBody(r->id);
}

void PhysicsManager::WakeUp(RigidBody* rb)
{
	if (!m_World) return;
	if (auto* r = m_World->Find(rb))
		m_World->BI().ActivateBody(r->id);
}

Vec3 PhysicsManager::GetWorldCenterOfMass(RigidBody* rb)
{
	if (m_World)
		if (auto* r = m_World->Find(rb))
		{
			JPH::RVec3 c = m_World->BI().GetCenterOfMassPosition(r->id);
			return Vec3((float)c.GetX(), (float)c.GetY(), (float)c.GetZ());
		}
	return rb && rb->GetGameObject() ? rb->GetGameObject()->GetTransform()->GetPosition() : Vec3::Zero;
}
