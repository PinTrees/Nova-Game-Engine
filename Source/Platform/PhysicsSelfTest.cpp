#include "pch.h"
#include "PhysicsSelfTest.h"
#include "GameObjectFactory.h"
#include "MonoBehaviour.h"
#include "CapsuleCollider.h"
#include "AnimationPose.h"
#include "SkinnedMesh.h"
#include "SkinnedMeshRenderer.h"
#include "AnimationPlayer.h"

namespace
{
	FILE* Log()
	{
		static FILE* fp = nullptr;
		static bool opened = false;
		if (!opened)
		{
			opened = true;
			char path[512] = {};
			if (::GetEnvironmentVariableA("NOVA_PHYSICS_TEST", path, sizeof(path)) > 0)
				fopen_s(&fp, path, "w");
		}
		return fp;
	}

	// 쿼터니언 → 회전축/각도(도)
	void AxisAngle(const Quaternion& q0, Vec3& axis, float& deg)
	{
		Quaternion q = q0;
		q.Normalize();
		if (q.w < 0.0f) q = Quaternion(-q.x, -q.y, -q.z, -q.w);
		float s = sqrtf((std::max)(0.0f, 1.0f - q.w * q.w));
		deg = XMConvertToDegrees(2.0f * acosf(std::clamp(q.w, -1.0f, 1.0f)));
		axis = s > 1e-5f ? Vec3(q.x / s, q.y / s, q.z / s) : Vec3(0, 0, 0);
	}

	// 검사용 스크립트: 정해진 동작을 하고 상태를 기록한다
	class PhysicsProbe : public MonoBehaviour
	{
	public:
		enum class Mode { Observe, Torque, ImpulseAtPoint, AngularVelocity, Summary };
		Mode mode = Mode::Observe;
		Vec3 param = Vec3::Zero;      // 토크 / 충격량 / 각속도
		Vec3 localPoint = Vec3::Zero; // ImpulseAtPoint 의 로컬 작용점
		int torqueSteps = 0;
		int logEvery = 25;
		std::vector<int> extraLogSteps;
		int step = 0;

		void FixedUpdate() override
		{
			RigidBody* rb = gameObject()->GetComponent<RigidBody>();
			++step;
			if (mode == Mode::Torque && rb && step <= torqueSteps)
				rb->AddTorque(param);
			if (mode == Mode::ImpulseAtPoint && rb && step == 1)
			{
				Vec3 world = Vec3::Transform(localPoint, GetTransform()->GetWorldMatrix());
				rb->AddForceAtPosition(param, world, ForceMode::Impulse);
			}
			if (mode == Mode::AngularVelocity && rb && step == 1)
				rb->SetAngularVelocity(param);

			// 기록은 이전 스텝의 결과 (FixedUpdate 는 시뮬레이션 직전에 불린다)
			const int done = step - 1;
			FILE* fp = Log();
			if (fp == nullptr)
				return;
			if (mode == Mode::Summary)
			{
				if (done == 400)
					WriteSummary(fp);
				return;
			}
			const bool extra = std::find(extraLogSteps.begin(), extraLogSteps.end(), done) != extraLogSteps.end();
			if (rb && done > 0 && (done % logEvery == 0 || extra))
			{
				Transform* tr = GetTransform();
				Vec3 p = tr->GetPosition(), v = rb->GetVelocity(), w = rb->GetAngularVelocity(), e = tr->GetEulerAngle();
				Vec3 axis;
				float deg;
				AxisAngle(tr->GetRotation(), axis, deg);
				fprintf(fp, "%-14s t=%5.2f pos(%7.3f %7.3f %7.3f) v(%6.3f %6.3f %6.3f)|%.3f| w(%6.3f %6.3f %6.3f)|%.3f| euler(%7.2f %7.2f %7.2f) axis(%5.2f %5.2f %5.2f) %6.2fdeg %s\n",
					gameObject()->GetName().c_str(), done * 0.02f, p.x, p.y, p.z, v.x, v.y, v.z, v.Length(), w.x, w.y, w.z, w.Length(),
					e.x, e.y, e.z, axis.x, axis.y, axis.z, deg, rb->IsSleeping() ? "asleep" : "");
				fflush(fp);
			}
		}

		void WriteSummary(FILE* fp)
		{
			int total = 0, asleep = 0, below = 0, bad = 0;
			float minY = FLT_MAX;
			for (GameObject* go : SceneManager::GetI()->GetCurrentScene()->GetAllGameObjects())
			{
				RigidBody* rb = go->GetComponent<RigidBody>();
				if (rb == nullptr || !rb->GetUseGravity())
					continue;
				++total;
				Vec3 p = go->GetTransform()->GetPosition();
				if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) ++bad;
				if (p.y < -0.05f)
				{
					++below;
					fprintf(fp, "  BELOW FLOOR: %s pos(%.3f %.3f %.3f)\n", go->GetName().c_str(), p.x, p.y, p.z);
				}
				if (go->GetName().rfind("T_Pile", 0) == 0 || go->GetName().rfind("T_Tower", 0) == 0)
				{
					Vec3 e = go->GetTransform()->GetEulerAngle();
					fprintf(fp, "  %-10s pos(%7.3f %7.3f %7.3f) euler(%6.1f %6.1f %6.1f) %s\n", go->GetName().c_str(), p.x, p.y, p.z, e.x, e.y, e.z, rb->IsSleeping() ? "asleep" : "awake");
				}
				if (rb->IsSleeping()) ++asleep;
				minY = (std::min)(minY, p.y);
			}
			fprintf(fp, "SUMMARY t=8.00 dynamic(with gravity)=%d asleep=%d below_floor=%d nan=%d minY=%.3f\n", total, asleep, below, bad, minY);
			fflush(fp);
		}
	};

	GameObject* Add(Scene* scene, GameObject* g, const std::string& name, const Vec3& pos, const Vec3& euler = Vec3::Zero, const Vec3& scale = Vec3::One)
	{
		g->SetName(name);
		g->GetTransform()->SetLocalScale(scale);
		g->GetTransform()->SetLocalEulerAngles(euler);
		g->GetTransform()->SetPosition(pos);
		scene->AddRootGameObject(g);
		return g;
	}

	PhysicsProbe* Probe(GameObject* g, PhysicsProbe::Mode mode = PhysicsProbe::Mode::Observe)
	{
		PhysicsProbe* p = g->AddComponent<PhysicsProbe>();
		p->mode = mode;
		return p;
	}

	// 오일러 ↔ 쿼터니언 왕복 검사 (Unity 규약: Z → X → Y)
	void CheckEulerRoundTrip(FILE* fp)
	{
		float maxErr = 0.0f;
		unsigned seed = 12345;
		auto rnd = [&]() { seed = seed * 1103515245u + 12345u; return ((seed >> 8) & 0xffff) / 65535.0f; };
		for (int i = 0; i < 2000; ++i)
		{
			Vec3 e(rnd() * 360.0f - 180.0f, rnd() * 360.0f - 180.0f, rnd() * 360.0f - 180.0f);
			if (i % 10 == 0) e.x = (i % 20 == 0) ? 90.0f : -90.0f;   // 짐벌 락 경우도 포함
			Quaternion q = Transform::EulerToQuaternion(e);
			Quaternion q2 = Transform::EulerToQuaternion(Transform::ToEulerAngles(q));
			maxErr = (std::max)(maxErr, 1.0f - fabsf(q.Dot(q2)));
		}
		// Unity 와 같은 순서인지: Euler(90,0,0) 은 +Y 를 +Z 로, Euler(0,90,0) 은 +Z 를 +X 로 보낸다 (왼손 좌표계)
		Vec3 a = Vec3::Transform(Vec3(0, 1, 0), Transform::EulerToQuaternion(Vec3(90, 0, 0)));
		Vec3 b = Vec3::Transform(Vec3(0, 0, 1), Transform::EulerToQuaternion(Vec3(0, 90, 0)));
		Vec3 c = Vec3::Transform(Vec3(1, 0, 0), Transform::EulerToQuaternion(Vec3(0, 0, 90)));
		fprintf(fp, "EULER roundtrip max(1-|dot|)=%.2e  X90*(0,1,0)=(%.2f %.2f %.2f)  Y90*(0,0,1)=(%.2f %.2f %.2f)  Z90*(1,0,0)=(%.2f %.2f %.2f)\n",
			maxErr, a.x, a.y, a.z, b.x, b.y, b.z, c.x, c.y, c.z);
	}
}

namespace PhysicsSelfTest
{
	void Build(Scene* scene)
	{
		FILE* fp = Log();
		if (fp)
			CheckEulerRoundTrip(fp);

		using M = PhysicsProbe::Mode;

		// 바닥: 60 x 60 Plane (Mesh Collider)
		Add(scene, GameObjectFactory::CreatePlane(), "T_Floor", Vec3(0, 0, 0), Vec3::Zero, Vec3(40, 1, 40));   // 400 x 400 (굴러가도 떨어지지 않게)

		// 1) 토크: 무중력 큐브/구에 1 초 동안 토크 (0,1,0) → ω = τ/I·t (큐브 I = 1/6, 구 I = 0.1)
		{
			GameObject* c = Add(scene, GameObjectFactory::CreateCube(), "T_TorqueCube", Vec3(-12, 5, -8));
			RigidBody* rb = c->AddComponent<RigidBody>();
			rb->SetUseGravity(false);
			PhysicsProbe* p = Probe(c, M::Torque);
			p->param = Vec3(0, 1, 0);
			p->torqueSteps = 50;
			GameObject* s = Add(scene, GameObjectFactory::CreateSphere(), "T_TorqueBall", Vec3(-9, 5, -8));
			RigidBody* rs = s->AddComponent<RigidBody>();
			rs->SetUseGravity(false);
			PhysicsProbe* ps = Probe(s, M::Torque);
			ps->param = Vec3(0, 1, 0);
			ps->torqueSteps = 50;
		}

		// 2) 모서리에 충격량: J=(0,0,1) at 로컬 (0.5,0.5,0) → v = (0,0,1), ω = I⁻¹(r×J) = 6·(0.5,-0.5,0) = (3,-3,0)
		{
			GameObject* c = Add(scene, GameObjectFactory::CreateCube(), "T_Impulse", Vec3(-6, 5, -8));
			RigidBody* rb = c->AddComponent<RigidBody>();
			rb->SetUseGravity(false);
			rb->SetAngularDamping(0.0f);
			PhysicsProbe* p = Probe(c, M::ImpulseAtPoint);
			p->param = Vec3(0, 0, 1);
			p->localPoint = Vec3(0.5f, 0.5f, 0.0f);
			p->logEvery = 50;
		}

		// 3) 짐벌 락 통과: X 축 2 rad/s 회전 → 0.75 초에 86°, 0.80 초에 91.7° (오일러 x=±90° 부근)
		{
			GameObject* c = Add(scene, GameObjectFactory::CreateCube(), "T_Gimbal", Vec3(-3, 5, -8));
			RigidBody* rb = c->AddComponent<RigidBody>();
			rb->SetUseGravity(false);
			rb->SetAngularDamping(0.0f);
			PhysicsProbe* p = Probe(c, M::AngularVelocity);
			p->param = Vec3(2, 0, 0);
			p->extraLogSteps = { 37, 38, 39, 40, 41, 79 };
		}

		// 4) 경사면: 25° (구는 굴러 내려감 a = 5/7·g·sin25 = 2.96, 상자는 마찰 0.6 > tan25 로 정지)
		//            35° (상자가 미끄러짐 a = g(sin35 - 0.6cos35) = 0.81, 캡슐은 굴러 내려감)
		struct RampDef { float angle; float z; const char* tag; };
		for (RampDef r : { RampDef{ 25.0f, 0.0f, "25" }, RampDef{ 35.0f, 6.0f, "35" } })
		{
			GameObject* ramp = Add(scene, GameObjectFactory::CreateCube(), std::string("T_Ramp") + r.tag, Vec3(4, 3, r.z), Vec3(0, 0, r.angle), Vec3(12, 0.5f, 4));
			Transform* rt = ramp->GetTransform();
			const Vec3 up = rt->GetUp(), along = rt->GetRight();   // 경사면 법선 / 경사 방향
			const Vec3 top = rt->GetPosition() + along * (along.y > 0 ? 4.0f : -4.0f);

			GameObject* ball = Add(scene, GameObjectFactory::CreateSphere(), std::string("T_Ball") + r.tag, top + up * (0.25f + 0.5f + 0.01f) + Vec3(0, 0, -1.2f));
			ball->AddComponent<RigidBody>();
			Probe(ball)->logEvery = 25;
			GameObject* box = Add(scene, GameObjectFactory::CreateCube(), std::string("T_Box") + r.tag, top + up * (0.25f + 0.5f + 0.01f) + Vec3(0, 0, 1.2f), Vec3(0, 0, r.angle));
			box->AddComponent<RigidBody>();
			Probe(box)->logEvery = 25;
			if (r.angle > 30.0f)
			{
				// 캡슐을 경사 방향과 수직으로 눕혀서 (Z 축) 굴러가게
				GameObject* cap = Add(scene, GameObjectFactory::CreateCapsule(), "T_Capsule35", top + up * (0.25f + 0.5f + 0.01f), Vec3(90, 0, 0));
				cap->AddComponent<RigidBody>();
				Probe(cap)->logEvery = 50;
			}
		}

		// 5) 탑 쌓기: 상자 6 개 (Mesh Collider 바닥 위 / Box Collider 받침 위)
		for (int i = 0; i < 6; ++i)
			Add(scene, GameObjectFactory::CreateCube(), "T_Tower" + std::to_string(i), Vec3(-12, 0.5f + i * 1.0f, 6))->AddComponent<RigidBody>();
		Add(scene, GameObjectFactory::CreateCube(), "T_Platform", Vec3(-20, 0.25f, 6), Vec3::Zero, Vec3(4, 0.5f, 4));
		for (int i = 0; i < 6; ++i)
			Add(scene, GameObjectFactory::CreateCube(), "T_TowerB" + std::to_string(i), Vec3(-20, 1.0f + i * 1.0f, 6))->AddComponent<RigidBody>();

		// 6) 섞어서 떨어뜨리기: 상자/구/캡슐 18 개 (서로 여러 번 충돌)
		unsigned seed = 7;
		auto rnd = [&]() { seed = seed * 1103515245u + 12345u; return ((seed >> 8) & 0xffff) / 65535.0f; };
		for (int i = 0; i < 18; ++i)
		{
			const Vec3 pos(18.0f + rnd() * 3.0f, 2.0f + i * 1.2f, -2.0f + rnd() * 3.0f);
			const Vec3 rot(rnd() * 360.0f, rnd() * 360.0f, rnd() * 360.0f);
			GameObject* g = nullptr;
			if (i % 3 == 0) g = GameObjectFactory::CreateCube();
			else if (i % 3 == 1) g = GameObjectFactory::CreateSphere();
			else g = GameObjectFactory::CreateCapsule();
			Add(scene, g, "T_Pile" + std::to_string(i), pos, rot)->AddComponent<RigidBody>();
		}

		// 요약 (8 초 시점): 중력 있는 모든 바디가 바닥 위에 있고 잠들었는지, NaN 이 없는지
		GameObject* summary = GameObjectFactory::CreateEmpty("T_Summary");
		scene->AddRootGameObject(summary);
		Probe(summary, M::Summary);
	}
}

namespace PhysicsSelfTest
{
	void RunParentTest(Scene* scene, const char* logPath)
	{
		FILE* fp = nullptr;
		fopen_s(&fp, logPath, "w");
		if (fp == nullptr || scene == nullptr)
			return;
		auto world = [](GameObject* g) {
			Transform* t = g->GetTransform();
			Vec3 p = t->GetPosition(), e = t->GetEulerAngle(), s = t->GetScale();
			char buf[256];
			sprintf_s(buf, "pos(%.3f %.3f %.3f) rot(%.2f %.2f %.2f) scale(%.3f %.3f %.3f)", p.x, p.y, p.z, e.x, e.y, e.z, s.x, s.y, s.z);
			return std::string(buf);
		};

		GameObject* parent = Add(scene, GameObjectFactory::CreateCube(), "P_Parent", Vec3(2, 1, 0), Vec3(0, 45, 0), Vec3(2, 1, 1));
		GameObject* child = Add(scene, GameObjectFactory::CreateSphere(), "P_Child", Vec3(4, 2, 1), Vec3(10, 20, 30));
		GameObject* grand = Add(scene, GameObjectFactory::CreateCapsule(), "P_Grand", Vec3(-1, 3, 2));

		fprintf(fp, "before  child %s\n", world(child).c_str());
		child->SetParent(parent);                  // Hierarchy 에서 드래그하는 것과 같은 동작 (월드 유지)
		fprintf(fp, "after   child %s  local(%.3f %.3f %.3f)\n", world(child).c_str(), child->GetTransform()->GetLocalPosition().x, child->GetTransform()->GetLocalPosition().y, child->GetTransform()->GetLocalPosition().z);
		fprintf(fp, "before  grand %s\n", world(grand).c_str());
		grand->SetParent(child);
		fprintf(fp, "after   grand %s\n", world(grand).c_str());
		parent->SetParent(child);                  // 자손 밑으로 옮기기 → 무시되어야 함
		fprintf(fp, "cycle-guard parent->parent=%s\n", parent->GetParent() ? parent->GetParent()->GetName().c_str() : "(root)");
		grand->SetParent(nullptr);                 // 루트로 꺼내기 (월드 유지)
		fprintf(fp, "to-root grand %s roots=%zu all=%zu\n", world(grand).c_str(), scene->GetRootGameObjects().size(), scene->GetAllGameObjects().size());
		grand->SetParent(child);

		// 저장 → 다시 읽기
		json j = *scene;
		Scene* loaded = new Scene();
		from_json(j, *loaded);
		for (GameObject* g : loaded->GetAllGameObjects())
			if (g->GetName().rfind("P_", 0) == 0)
				fprintf(fp, "reload  %-9s parent=%-9s %s\n", g->GetName().c_str(), g->GetParent() ? g->GetParent()->GetName().c_str() : "(root)", world(g).c_str());
		fprintf(fp, "reload counts: all=%zu (original all=%zu)\n", loaded->GetAllGameObjects().size(), scene->GetAllGameObjects().size());
		fclose(fp);
		delete loaded;
	}
}

namespace PhysicsSelfTest
{
	// CPU 로 스키닝한 정점들의 AABB (애니메이션/단위/좌표축 확인용)
	static void SkinnedBounds(SkinnedMeshRenderer* r, Vec3& mn, Vec3& mx)
	{
		mn = Vec3(FLT_MAX, FLT_MAX, FLT_MAX);
		mx = Vec3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
		auto mesh = r->GetMesh();
		const auto& bones = r->GetFinalTransforms();
		if (mesh == nullptr || bones.empty())
			return;
		Matrix world = r->GetGameObject()->GetTransform()->GetWorldMatrix();
		for (size_t i = 0; i < mesh->Vertices.size(); i += 7)
		{
			const auto& v = mesh->Vertices[i];
			float w[4] = { v.weights.x, v.weights.y, v.weights.z, 1.0f - v.weights.x - v.weights.y - v.weights.z };
			XMVECTOR p = XMVectorZero();
			for (int k = 0; k < 4; ++k)
			{
				if (w[k] <= 0.0f) continue;
				size_t bi = (std::min)((size_t)v.boneIndices[k], bones.size() - 1);
				p += w[k] * XMVector3TransformCoord(XMLoadFloat3(&v.pos), XMLoadFloat4x4(&bones[bi]));
			}
			Vec3 q = Vec3::Transform(Vec3(p), world);
			mn = Vec3::Min(mn, q);
			mx = Vec3::Max(mx, q);
		}
	}

	void RunAnimationTest(Scene* scene, const char* logPath)
	{
		FILE* fp = nullptr;
		fopen_s(&fp, logPath, "w");
		if (fp == nullptr || scene == nullptr)
			return;

		struct Case { const char* name; const char* model; const char* clip; float x; };
		const Case cases[] = {
			{ "A_Character", GameObjectFactory::kDefaultCharacterModel, GameObjectFactory::kDefaultCharacterIdle, 0.0f },
			{ "A_Rapier", "Resources\\Packages\\Character\\Animations\\Rapier_Idle.fbx", "Resources\\Packages\\Character\\Animations\\Rapier_Idle.fbx", 2.0f },
		};
		for (const Case& c : cases)
		{
			const double t0 = (double)::GetTickCount64();
			auto file = ResourceManager::GetI()->LoadMeshFile(c.model);
			const double t1 = (double)::GetTickCount64();
			fprintf(fp, "== %s  model=%s  load=%.0fms\n", c.name, c.model, t1 - t0); fflush(fp);
			if (file == nullptr) { fprintf(fp, "  model load failed\n"); fflush(fp); continue; }
			fprintf(fp, "  skinnedMeshes=%zu staticMeshes=%zu skeletons=%zu clipsInModel=%zu\n", file->SkinnedMeshs.size(), file->Meshs.size(), file->Avatas.size(), file->SkinnedData.AnimationClips.size()); fflush(fp);
			for (auto& sm : file->SkinnedMeshs)
				fprintf(fp, "  skinned '%s' verts=%zu indices=%zu subsets=%zu bones=%zu\n", sm->Name.c_str(), sm->Vertices.size(), sm->Indices.size(), sm->Subsets.size(), sm->BoneNames.size()); fflush(fp);
			if (!file->Avatas.empty())
				fprintf(fp, "  skeleton nodes=%zu unitScale=%.4f\n", file->Avatas[0]->NodeNames.size(), file->Avatas[0]->UnitScale); fflush(fp);

			auto clipFile = ResourceManager::GetI()->LoadMeshFile(c.clip);
			if (clipFile && !clipFile->SkinnedData.AnimationClips.empty() && !file->Avatas.empty())
			{
				auto clip = clipFile->SkinnedData.AnimationClips[0];
				auto map = AnimationPose::MapChannels(*clip, *file->Avatas[0]);
				int mapped = 0;
				for (int m : map) mapped += m >= 0;
				fprintf(fp, "  clip '%s' duration=%.3fs channels=%zu mappedToModel=%d\n", clip->Name.c_str(), clip->Duration, clip->Channels.size(), mapped); fflush(fp);
			}



			GameObject* character = GameObjectFactory::CreateCharacter(c.name, c.model, c.clip);
			character->GetTransform()->SetPosition(Vec3(c.x, 0.0f, 0.0f));
			scene->AddRootGameObject(character);
			AnimationPlayer* anim = character->GetComponent<AnimationPlayer>();

			for (GameObject* child : character->GetChildren())
			{
				SkinnedMeshRenderer* r = child->GetComponent<SkinnedMeshRenderer>();
				if (r == nullptr) continue;
				int unmapped = 0;
				Vec3 mn, mx;
				r->ResetToBindPose();
				SkinnedBounds(r, mn, mx);
				fprintf(fp, "  [%s] bind   bounds (%.3f %.3f %.3f) - (%.3f %.3f %.3f) size(%.3f %.3f %.3f)\n", child->GetName().c_str(), mn.x, mn.y, mn.z, mx.x, mx.y, mx.z, mx.x - mn.x, mx.y - mn.y, mx.z - mn.z); fflush(fp);
				if (anim && anim->Play())
				{
					for (float t : { 0.0f, 0.5f, 1.0f })
					{
						anim->SetTime(t);
						anim->Sample();
						SkinnedBounds(r, mn, mx);
						fprintf(fp, "  [%s] t=%.1f  bounds (%.3f %.3f %.3f) - (%.3f %.3f %.3f)\n", child->GetName().c_str(), t, mn.x, mn.y, mn.z, mx.x, mx.y, mx.z); fflush(fp);
					}
					// 두 시점의 본 행렬 차이 (애니메이션이 실제로 움직이는지)
					anim->SetTime(0.0f); anim->Sample();
					auto a = r->GetFinalTransforms();
					anim->SetTime(0.7f); anim->Sample();
					auto b = r->GetFinalTransforms();
					float diff = 0.0f;
					for (size_t k = 0; k < a.size() && k < b.size(); ++k)
						for (int e = 0; e < 16; ++e)
							diff = (std::max)(diff, fabsf((&a[k]._11)[e] - (&b[k]._11)[e]));
					fprintf(fp, "  [%s] max bone matrix change t0→t0.7 = %.4f (bones=%zu)\n", child->GetName().c_str(), diff, a.size()); fflush(fp);
					anim->Stop();
					anim->Sample();
				}
				(void)unmapped;
			}
		}
		fclose(fp);
	}
}
