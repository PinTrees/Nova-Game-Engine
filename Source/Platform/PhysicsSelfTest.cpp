#include "pch.h"
#include "PhysicsSelfTest.h"
#include "GameObjectFactory.h"
#include "MonoBehaviour.h"
#include "CapsuleCollider.h"
#include "AnimationPose.h"
#include "SkinnedMesh.h"
#include "SkinnedMeshRenderer.h"
#include "AnimationPlayer.h"
#include "Animator.h"
#include "AnimatorController.h"
#include "TerrainEditor.h"

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

namespace PhysicsSelfTest
{
	void RunAnimatorTest(Scene* scene, const char* logPath)
	{
		FILE* fp = nullptr;
		fopen_s(&fp, logPath, "w");
		if (fp == nullptr || scene == nullptr)
			return;

		// ---- 컨트롤러 만들기 (Rapier 모델 = Bip001 리그, GreatSword 클립도 같은 리그) ----
		const std::string dir = std::filesystem::path(logPath).parent_path().string();
		const std::string ctrlPath = dir + "\\SwordFighter.controller";
		const std::string model = "Resources\\Packages\\Character\\Animations\\Rapier_Idle.fbx";
		auto c = AnimatorController::Create(ctrlPath);
		const int sIdle = c->AddState(0, "Rapier Idle", -40, 0);
		const int sGs = c->AddState(0, "GreatSword Idle", 300, 110);
		const int sRoot = c->AddState(0, "GreatSword Root", 300, -110);
		AnimatorLayer& layer = c->Layers[0];
		layer.States[sIdle].ClipPath = model;
		layer.States[sGs].ClipPath = "Resources\\Packages\\Character\\Animations\\GreatSword_Idle_Pose.FBX";
		layer.States[sRoot].ClipPath = "Resources\\Packages\\Character\\Animations\\GreatSword_Idle_Root.FBX";
		for (auto& s : layer.States)
			s.LoadClip();
		layer.ExitX = 640;
		c->RenameParameter(c->AddParameter(AnimatorTypes::ParamType::Bool), "GreatSword");
		c->RenameParameter(c->AddParameter(AnimatorTypes::ParamType::Trigger), "Pose");
		c->RenameParameter(c->AddParameter(AnimatorTypes::ParamType::Float), "Speed");

		auto addT = [&](const char* from, const char* to, bool exitTime, float exitAt, float duration, const char* param, AnimatorTypes::ConditionMode mode) {
			const int i = c->AddTransition(0, from, to);
			AnimatorTransition& t = c->Layers[0].Transitions[i];
			t.HasExitTime = exitTime;
			t.ExitTime = exitAt;
			t.Duration = duration;
			if (param)
				t.Conditions.push_back({ param, mode, 0.0f });
			return i;
		};
		addT("Rapier Idle", "GreatSword Idle", false, 0, 0.3f, "GreatSword", AnimatorTypes::ConditionMode::If);
		addT("GreatSword Idle", "Rapier Idle", false, 0, 0.3f, "GreatSword", AnimatorTypes::ConditionMode::IfNot);
		const int any = addT(AnimatorTypes::kAnyState, "GreatSword Root", false, 0, 0.2f, "Pose", AnimatorTypes::ConditionMode::If);
		c->Layers[0].Transitions[any].CanTransitionToSelf = false;
		addT("GreatSword Root", "GreatSword Idle", true, 0.9f, 0.25f, nullptr, AnimatorTypes::ConditionMode::If);
		c->Commit();
		for (const auto& s : c->Layers[0].States)
			fprintf(fp, "state '%s' clip=%s length=%.3fs\n", s.Name.c_str(), s.Clip ? s.Clip->Name.c_str() : "(none)", s.Clip ? s.Clip->GetClipEndTime() : 0.0f);

		GameObject* go = GameObjectFactory::CreateAnimatedCharacter("SwordFighter", model, ctrlPath);
		go->GetTransform()->SetPosition(Vec3(2.0f, 0.0f, 0.0f));
		scene->AddRootGameObject(go);
		Animator* a = go->GetComponent<Animator>();
		SkinnedMeshRenderer* renderer = nullptr;
		for (GameObject* child : go->GetChildren())
			if (!renderer) renderer = child->GetComponent<SkinnedMeshRenderer>();
		if (a == nullptr || renderer == nullptr)
		{
			fprintf(fp, "FAIL: animator=%p renderer=%p\n", (void*)a, (void*)renderer);
			fclose(fp);
			return;
		}

		int failures = 0;
		auto state = [&](int index) { return index >= 0 ? c->Layers[0].States[index].Name : std::string("-"); };
		auto log = [&](const char* tag) {
			const Animator::LayerRuntime* r = a->GetLayerRuntime(0);
			fprintf(fp, "%-28s current=%-16s t=%.3f  next=%-16s blend=%.2f  GreatSword=%d Pose=%d\n", tag, state(r->Current).c_str(), r->Time,
				state(r->Next).c_str(), r->Next >= 0 ? r->TransitionElapsed / r->TransitionDuration : 0.0f, (int)a->GetBool("GreatSword"), (int)a->GetBool("Pose"));
			fflush(fp);
		};
		auto expect = [&](const char* what, bool ok) {
			fprintf(fp, "  %s %s\n", ok ? "PASS" : "FAIL", what);
			failures += !ok;
		};
		auto maxDiff = [](const std::vector<XMFLOAT4X4>& x, const std::vector<XMFLOAT4X4>& y) {
			float d = 0.0f;
			for (size_t k = 0; k < x.size() && k < y.size(); ++k)
				for (int e = 0; e < 16; ++e)
					d = (std::max)(d, fabsf((&x[k]._11)[e] - (&y[k]._11)[e]));
			return d;
		};

		a->Rebind();
		log("start");
		expect("default state = Rapier Idle", a->GetCurrentStateName() == "Rapier Idle");
		a->Update(1.0f);
		log("+1.0s");
		expect("no transition without condition", !a->IsInTransition());
		const auto poseIdle = renderer->GetFinalTransforms();

		a->SetBool("GreatSword", true);
		a->Update(0.05f);
		log("GreatSword=true +0.05s");
		expect("transition started to GreatSword Idle", a->IsInTransition() && a->GetLayerRuntime(0)->Next == sGs);
		a->Update(0.1f);
		log("+0.1s (mid blend)");
		const auto poseMid = renderer->GetFinalTransforms();
		a->Update(0.3f);
		log("+0.3s");
		expect("arrived at GreatSword Idle", a->GetCurrentStateName() == "GreatSword Idle" && !a->IsInTransition());
		const auto poseGs = renderer->GetFinalTransforms();
		const float dAB = maxDiff(poseIdle, poseGs), dAM = maxDiff(poseIdle, poseMid), dBM = maxDiff(poseGs, poseMid);
		fprintf(fp, "  pose diff idle-gs=%.4f idle-mid=%.4f gs-mid=%.4f\n", dAB, dAM, dBM);
		expect("cross-fade pose lies between the two states", dAB > 0.01f && dAM > 0.001f && dBM > 0.001f);

		a->SetTrigger("Pose");
		a->Update(0.05f);
		log("SetTrigger(Pose) +0.05s");
		expect("Any State transition to GreatSword Root", a->IsInTransition() && a->GetLayerRuntime(0)->Next == sRoot);
		expect("trigger consumed", !a->GetBool("Pose"));
		a->Update(0.25f);
		log("+0.25s");
		expect("now in GreatSword Root", a->GetCurrentStateName() == "GreatSword Root");

		// Exit Time 0.9 에 GreatSword Idle 로 돌아와야 한다
		const float rootLength = c->Layers[0].States[sRoot].Clip ? c->Layers[0].States[sRoot].Clip->GetClipEndTime() : 1.0f;
		float waited = 0.0f;
		while (a->GetCurrentStateName() == "GreatSword Root" && !a->IsInTransition() && waited < rootLength * 2.0f + 1.0f)
		{
			a->Update(1.0f / 60.0f);
			waited += 1.0f / 60.0f;
		}
		log("exit time reached");
		fprintf(fp, "  waited %.3fs (root length %.3fs, expected leave at %.3fs)\n", waited, rootLength, rootLength * 0.9f - 0.2f);
		expect("exit-time transition Root -> GreatSword Idle", a->IsInTransition() && a->GetLayerRuntime(0)->Next == sGs);
		a->Update(0.3f);

		a->SetBool("GreatSword", false);
		for (int f = 0; f < 24; ++f)   // 0.4초를 프레임 단위로 (전이는 시작 프레임 다음부터 진행된다)
			a->Update(1.0f / 60.0f);
		log("GreatSword=false +0.4s");
		expect("back to Rapier Idle", a->GetCurrentStateName() == "Rapier Idle" && !a->IsInTransition());

		// CrossFade / Play API
		a->CrossFade("GreatSword Root", 0.2f);
		a->Update(0.1f);
		log("CrossFade(Root,0.2) +0.1s");
		expect("CrossFade in progress", a->IsInTransition() && a->GetLayerRuntime(0)->Next == sRoot);
		a->Play("Rapier Idle");
		log("Play(Rapier Idle)");
		expect("Play jumps immediately", a->GetCurrentStateName() == "Rapier Idle" && !a->IsInTransition());

		// 저장 파일 다시 읽기 (캐시를 거치지 않고 JSON 만 확인)
		{
			std::ifstream in(ctrlPath);
			json j = json::parse(in, nullptr, false);
			const bool ok = !j.is_discarded() && j["layers"][0]["states"].size() == 3 && j["layers"][0]["transitions"].size() == 4 && j["parameters"].size() == 3;
			expect("controller file saved (3 states, 4 transitions, 3 parameters)", ok);
		}

		a->Rebind();
		fprintf(fp, "RESULT: %s (%d failures)\n", failures == 0 ? "ALL PASS" : "FAILED", failures);
		fclose(fp);
	}
}

namespace PhysicsSelfTest
{
	void RunTerrainTest(Scene* scene, const char* logPath)
	{
		FILE* fp = nullptr;
		fopen_s(&fp, logPath, "w");
		if (fp == nullptr || scene == nullptr)
			return;

		// ---- 데이터: 300 x 60 x 300 m, 높이맵 513 ----
		const std::string dir = std::filesystem::path(logPath).parent_path().string();
		auto data = TerrainData::Create(dir + "\\TestTerrain.terraindata", 513, Vec3(300.0f, 60.0f, 300.0f));
		GameObject* go = GameObjectFactory::CreateEmpty("T_Terrain");
		go->GetTransform()->SetPosition(Vec3(20.0f, 0.0f, -20.0f));
		Terrain* terrain = go->AddComponent<Terrain>();
		terrain->SetTerrainData(data);
		go->AddComponent<TerrainCollider>();
		scene->AddRootGameObject(go);
		const Vec3 origin = terrain->GetPosition();
		auto W = [&](float x, float z) { return origin + Vec3(x, 0.0f, z); };   // 지형 로컬 → 월드

		const double t0 = (double)::GetTickCount64();
		// 바닥 4m 로 평탄화 (Set Height + Flatten All 과 같은 동작)
		std::fill(data->Heights.begin(), data->Heights.end(), 4.0f / data->Size.y);
		data->OnHeightsChanged(0, 0, data->HeightmapResolution - 1, data->HeightmapResolution - 1);

		using namespace TerrainEditor;
		// 언덕 (Raise, Soft Round)
		struct Hill { float x, z, size, seconds; };
		const Hill hills[] = { { 90, 170, 90, 1.2f }, { 150, 230, 60, 1.6f }, { 210, 120, 110, 0.9f }, { 60, 60, 50, 1.4f } };
		for (const Hill& h : hills)
		{
			SetBrush(0, h.size, 100.0f);
			for (int i = 0; i < 30; ++i)
				ApplyBrush(terrain, PaintTool::RaiseLower, W(h.x, h.z), h.seconds / 30.0f, false);
		}
		// 움푹한 곳 (Shift = Lower)
		SetBrush(0, 40.0f, 100.0f);
		for (int i = 0; i < 20; ++i)
			ApplyBrush(terrain, PaintTool::RaiseLower, W(150, 80), 0.02f, true);
		// 잔 기복 (Noise 브러시)
		SetBrush(3, 30.0f, 60.0f);
		for (int i = 0; i < 60; ++i)
		{
			const float x = 20.0f + fmodf(i * 97.31f, 260.0f), z = 20.0f + fmodf(i * 53.77f, 260.0f);
			ApplyBrush(terrain, PaintTool::RaiseLower, W(x, z), 0.15f, false);
		}
		// 평평한 고원 (Set Height 18m, Hard Round)
		SetBrush(1, 45.0f, 100.0f);
		SetTargetHeight(18.0f);
		for (int i = 0; i < 20; ++i)
			ApplyBrush(terrain, PaintTool::SetHeight, W(240, 240), 0.1f, false);
		// 가장자리 다듬기 (Smooth)
		SetBrush(0, 70.0f, 100.0f);
		for (int i = 0; i < 10; ++i)
			ApplyBrush(terrain, PaintTool::SmoothHeight, W(240, 240), 0.1f, false);
		const double t1 = (double)::GetTickCount64();

		// ---- 텍스처 레이어 ----
		for (const char* name : { "Grass", "Dirt", "Rock", "Sand" })
			if (auto layer = TerrainLayer::Load(std::string("Resources\\Packages\\Terrain\\Layers\\") + name + ".terrainlayer"))
				data->Layers.push_back(layer);
		// 가파르거나 높은 곳 = Rock, 낮은 곳 = Sand (스크립트 칠하기)
		const int cres = data->ControlResolution;
		for (int z = 0; z < cres; ++z)
			for (int x = 0; x < cres; ++x)
			{
				const float lx = (float)x / (cres - 1) * data->Size.x, lz = (float)z / (cres - 1) * data->Size.z;
				const float h = data->GetHeight(lx, lz);
				const float slope = 1.0f - data->GetNormal(lx, lz).y;
				float w[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
				const float rock = std::clamp((slope - 0.12f) * 8.0f + (h - 30.0f) * 0.08f, 0.0f, 1.0f);
				const float sand = std::clamp((4.5f - h) * 0.6f, 0.0f, 1.0f);
				w[2] = rock;
				w[3] = sand * (1.0f - rock);
				w[0] = (std::max)(0.0f, 1.0f - w[2] - w[3]);
				uint8_t* px = &data->Control[((size_t)z * cres + x) * 4];
				for (int c = 0; c < 4; ++c) px[c] = (uint8_t)std::lround(w[c] * 255.0f);
			}
		data->OnControlChanged(0, 0, cres - 1, cres - 1);
		// 흙길 (Paint Texture 브러시, 레이어 1 = Dirt)
		SetBrush(0, 9.0f, 100.0f);
		SetSelectedLayer(1);
		for (int i = 0; i <= 80; ++i)
		{
			const float s = i / 80.0f;
			ApplyBrush(terrain, PaintTool::PaintTexture, W(30 + s * 250, 150 + sinf(s * 6.0f) * 40), 0.5f, false);
		}
		const bool saved = data->Save();
		// NOVA_TERRAIN_LOD=1 이면 Scene 뷰에 쿼드트리 노드 경계를 켜고 Terrain Settings 탭을 연다
		if (::GetEnvironmentVariableA("NOVA_TERRAIN_LOD", nullptr, 0) > 0)
		{
			terrain->ShowLodNodesRef() = true;
			SetTool(Tool::Settings);
		}

		fprintf(fp, "terrain res=%d size=(%.0f %.0f %.0f) nodeGrid=%d maxDepth=%d sculpt=%.0fms saved=%d\n", data->HeightmapResolution,
			data->Size.x, data->Size.y, data->Size.z, data->NodeGrid(), data->MaxDepth(), t1 - t0, (int)saved);
		for (int d = 0; d <= data->MaxDepth(); ++d)
		{
			float maxErr = 0.0f;
			const int n = 1 << d;
			for (int z = 0; z < n; ++z)
				for (int x = 0; x < n; ++x)
					maxErr = (std::max)(maxErr, data->GetNode(d, x, z).Error);
			fprintf(fp, "  depth %d: %4d nodes, cell step %2d (%.2fm), max error %.3fm\n", d, n * n, data->NodeStep(d), data->NodeStep(d) * data->CellSizeX(), maxErr);
		}
		const TerrainData::Node& root = data->GetNode(0, 0, 0);
		fprintf(fp, "  root height range %.2f .. %.2f m\n", root.MinHeight, root.MaxHeight);

		// 광선 검사 (지형 위에서 아래로)
		for (const Vec3& p : { W(90, 170), W(240, 240), W(150, 80) })
		{
			Vec3 hit;
			const bool ok = terrain->Raycast(p + Vec3(0, 100, 0), Vec3(0, -1, 0), 200.0f, hit);
			fprintf(fp, "  raycast at (%.0f, %.0f): %s y=%.3f  SampleHeight=%.3f\n", p.x, p.z, ok ? "hit" : "MISS", hit.y, terrain->SampleHeight(p) + origin.y);
		}

		// ---- 물리: 공을 떨어뜨린다 (Play 에서 NOVA_PHYSICS_LOG 로 확인) ----
		const Vec3 balls[] = { W(240, 240), W(90, 170), W(150, 80), W(200, 60), W(120, 120) };
		for (int i = 0; i < 5; ++i)
		{
			GameObject* ball = GameObjectFactory::CreateSphere("T_Ball" + std::to_string(i));
			const float ground = terrain->SampleHeight(balls[i]) + origin.y;
			ball->GetTransform()->SetPosition(Vec3(balls[i].x, ground + 6.0f, balls[i].z));
			ball->AddComponent<RigidBody>();
			scene->AddRootGameObject(ball);
			fprintf(fp, "  ball T_Ball%d at (%.1f, %.1f) ground=%.3f expected rest y=%.3f (if it does not roll)\n", i, balls[i].x, balls[i].z, ground, ground + 0.5f);
		}
		fclose(fp);
	}
}

namespace PhysicsSelfTest
{
	void RunPrefabTest(Scene* scene, const char* logPath)
	{
		FILE* fp = nullptr;
		fopen_s(&fp, logPath, "w");
		if (fp == nullptr || scene == nullptr)
			return;
		int failures = 0;
		auto expect = [&](const char* what, bool ok) { fprintf(fp, "  %s %s\n", ok ? "PASS" : "FAIL", what); fflush(fp); failures += !ok; };
		auto find = [&](uint64 id) { return SceneManager::GetI()->GetCurrentScene()->FindByFileID(id); };

		// 1) 원본: 큐브 + 자식 구
		GameObject* root = GameObjectFactory::CreateCube("P_Root");
		GameObject* child = GameObjectFactory::CreateSphere("P_Child");
		scene->AddRootGameObject(root);
		scene->AddRootGameObject(child);
		child->SetParent(root, false);
		child->GetTransform()->SetLocalPosition(Vec3(0, 1.5f, 0));
		root->GetTransform()->SetPosition(Vec3(-6, 0.5f, 6));

		const std::string dir = std::filesystem::path(logPath).parent_path().string();
		const std::string asset = dir + "\\TestPrefab.prefab";
		expect("create prefab asset", PrefabUtility::SaveAsPrefabAssetAndConnect(root, asset));
		expect("source object became instance root", PrefabUtility::GetInstanceRoot(child) == root);
		fprintf(fp, "  asset objects = %d\n", PrefabUtility::CountAssetObjects(asset));

		// 2) 인스턴스 두 개
		GameObject* a = PrefabUtility::InstantiatePrefab(asset, scene);
		GameObject* b = PrefabUtility::InstantiatePrefab(asset, scene);
		expect("instantiate A and B", a && b && a->GetChildCount() == 1 && b->GetChildCount() == 1);
		a->GetTransform()->SetPosition(Vec3(-3, 0.5f, 6));
		b->GetTransform()->SetPosition(Vec3(0, 0.5f, 6));
		const uint64 aID = a->GetFileID(), bID = b->GetFileID();
		const uint64 aChildID = a->GetChildren()[0]->GetFileID(), bChildID = b->GetChildren()[0]->GetFileID();
		expect("instances have their own fileIDs", aID != bID && aID != root->GetFileID() && aChildID != bChildID);

		// 3) 오버라이드: A 자식의 크기, 루트 위치는 오버라이드가 아니어야 함
		a->GetChildren()[0]->GetTransform()->SetLocalScale(Vec3(2, 2, 2));
		{
			json jc = *a->GetChildren()[0];
			bool scaleOverride = false;
			for (const auto& o : jc["prefab"]["overrides"]) { fprintf(fp, "  A child override: %s\n", o.get<std::string>().c_str()); scaleOverride |= o.get<std::string>() == "Transform#0/m_LocalScale"; }
			expect("A child scale is an override", scaleOverride);
			json ja = *a;
			expect("root position is not listed as override", ja["prefab"]["overrides"].empty());
		}

		// 4) 씬을 JSON 으로 다시 만들어도 (저장/불러오기, Undo 와 같은 경로) 값과 연결이 유지
		{
			json sj = *SceneManager::GetI()->GetCurrentScene();
			SceneManager::GetI()->RestoreSceneState(sj.dump());
			GameObject* a2 = find(aID);
			expect("reload keeps instance and override", a2 && a2->GetPrefabLink().Root && a2->GetChildCount() == 1 &&
				fabsf(a2->GetChildren()[0]->GetTransform()->GetLocalScale().x - 2.0f) < 1e-4f && fabsf(a2->GetTransform()->GetPosition().x + 3.0f) < 1e-4f);
		}

		// 5) Apply All (A) → B 에 전파
		expect("apply all", PrefabUtility::ApplyAll(find(aID)));
		{
			GameObject* b2 = find(bID);
			const float bs = b2 && b2->GetChildCount() ? b2->GetChildren()[0]->GetTransform()->GetLocalScale().x : -1.0f;
			fprintf(fp, "  B child scale after apply = %.3f\n", bs);
			expect("B got the applied scale", fabsf(bs - 2.0f) < 1e-4f);
			expect("B root position unchanged", b2 && fabsf(b2->GetTransform()->GetPosition().x) < 1e-4f);
			json ja = *find(aID)->GetChildren()[0];
			expect("A has no overrides after apply", ja["prefab"]["overrides"].empty());
		}

		// 6) B 에서 이름과 컴포넌트 추가 → Revert All 로 되돌리기
		{
			GameObject* b2 = find(bID);
			b2->GetChildren()[0]->SetName("Renamed");
			b2->GetChildren()[0]->AddComponent<RigidBody>();
			const auto desc = PrefabUtility::GetOverrideDescriptions(b2);
			for (const auto& d : desc) fprintf(fp, "  B override: %s\n", d.c_str());
			expect("B has name + added component overrides", desc.size() >= 2);
			PrefabUtility::RevertAll(b2);
			GameObject* b3 = find(bID);
			expect("revert restores name and removes added component", b3 && b3->GetChildren()[0]->GetName() == "P_Child" && b3->GetChildren()[0]->GetComponent<RigidBody>() == nullptr);
		}

		// 7) 에셋 파일을 직접 바꾸면 다시 읽을 때 인스턴스에 반영
		{
			std::ifstream in(asset);
			json j = json::parse(in);
			in.close();
			j["root"]["children"][0]["name"] = "ChangedInAsset";
			std::ofstream(asset) << j.dump(2);
			json sj = *SceneManager::GetI()->GetCurrentScene();
			SceneManager::GetI()->RestoreSceneState(sj.dump());
			GameObject* b4 = find(bID);
			expect("asset change propagates on reload", b4 && b4->GetChildren()[0]->GetName() == "ChangedInAsset");
		}

		// 8) Unpack
		PrefabUtility::UnpackCompletely(find(bID));
		expect("unpack clears links", !PrefabUtility::IsPartOfPrefabInstance(find(bID)) && !PrefabUtility::IsPartOfPrefabInstance(find(bChildID)));

		fprintf(fp, "RESULT: %s (%d failures)\n", failures == 0 ? "ALL PASS" : "FAILED", failures);
		fclose(fp);
	}
}
