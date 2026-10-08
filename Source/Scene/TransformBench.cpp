#include "pch.h"
#include "TransformBench.h"
#include "CliServer.h"
#include "SceneManager.h"
#include "Scene.h"
#include "GameObject.h"
#include "GameObjectFactory.h"
#include "Transform.h"
#include "SceneCulling.h"
#include "TransformStore.h"
#include "MeshFilter.h"
#include "MeshRenderer.h"
#include "Mesh.h"
#include "JobSystem.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <random>

namespace TransformBench
{
	namespace
	{
		using Clock = std::chrono::steady_clock;
		double MsSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

		// parent 아래에 depth 단계까지 단계마다 children 개 (큐브, 작게)
		void AddChildren(GameObject* parent, int children, int depth, int& count)
		{
			if (depth <= 0)
				return;
			for (int c = 0; c < children; ++c)
			{
				GameObject* go = GameObjectFactory::CreatePrimitive(PrimitiveType::Cube, "BenchChild");
				go->SetParent(parent, false);
				Transform* t = go->GetTransform();
				const float a = 6.2831853f * (float)c / (float)children;
				t->SetLocalPosition(Vec3(cosf(a) * 1.5f, 0.6f, sinf(a) * 1.5f));
				t->SetLocalScale(Vec3(0.4f, 0.4f, 0.4f));
				++count;
				AddChildren(go, children, depth - 1, count);
			}
		}

		// ---- 검사: 예전 방식 (바꿀 때마다 사슬을 그대로 계산) 과 같은 값인가
		struct RefPose { XMMATRIX M; XMVECTOR R; XMVECTOR S; };
		RefPose RefWorld(Transform* t)
		{
			const Vec3 s = t->GetLocalScale(), p = t->GetLocalPosition();
			const Quaternion q = t->GetLocalRotation();
			const XMMATRIX local = XMMatrixMultiply(XMMatrixMultiply(XMMatrixScalingFromVector(s), XMMatrixRotationQuaternion(q)), XMMatrixTranslationFromVector(p));
			shared_ptr<Transform> parent = t->GetParent();
			if (!parent)
				return { local, q, s };
			const RefPose pr = RefWorld(parent.get());
			return { XMMatrixMultiply(local, pr.M), XMQuaternionNormalize(XMQuaternionMultiply(q, pr.R)), XMVectorMultiply(s, pr.S) };
		}

		float MatrixError(const XMFLOAT4X4& a, const XMFLOAT4X4& b)
		{
			float e = 0.0f;
			for (int r = 0; r < 4; ++r)
				for (int c = 0; c < 4; ++c)
					e = (std::max)(e, fabsf(a.m[r][c] - b.m[r][c]) / (std::max)(1.0f, fabsf(b.m[r][c])));
			return e;
		}

		nlohmann::json HierarchyTest(Scene* scene)
		{
			const auto t0 = Clock::now();
			std::mt19937 rng(1234);
			auto U = [&](float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); };
			auto I = [&](int n) { return std::uniform_int_distribution<int>(0, n - 1)(rng); };
			auto RandRot = [&]() { return Quaternion::CreateFromYawPitchRoll(U(-3.1f, 3.1f), U(-1.5f, 1.5f), U(-3.1f, 3.1f)); };
			const int N = 300;
			std::vector<GameObject*> objs;
			for (int i = 0; i < N; ++i)
			{
				GameObject* go = GameObjectFactory::CreateEmpty("TfTest");
				if (i == 0 || I(10) < 3)
					scene->AddRootGameObject(go);
				else
					go->SetParent(objs[(size_t)I((int)objs.size())], false);
				Transform* t = go->GetTransform();
				t->SetLocalPosition(Vec3(U(-5, 5), U(-5, 5), U(-5, 5)));
				t->SetLocalRotation(RandRot());
				t->SetLocalScale(Vec3(U(0.5f, 2.0f), U(0.5f, 2.0f), U(0.5f, 2.0f)));
				objs.push_back(go);
			}
			int checks = 0, bad = 0, jobChecks = 0, jobBad = 0, reparents = 0;
			float maxErr = 0.0f;
			std::string first;
			auto verifyAll = [&](const char* when) {
				for (GameObject* go : objs)
				{
					Transform* t = go->GetTransform();
					const RefPose r = RefWorld(t);
					XMFLOAT4X4 rm;
					XMStoreFloat4x4(&rm, r.M);
					const Matrix m = t->GetWorldMatrix();
					float err = MatrixError(m, rm);
					const Vec3 pos = t->GetPosition();
					err = (std::max)(err, fabsf(pos.x - rm._41) + fabsf(pos.y - rm._42) + fabsf(pos.z - rm._43));
					const Quaternion q = t->GetRotation();
					err = (std::max)(err, 1.0f - fabsf(XMVectorGetX(XMQuaternionDot(q, r.R))));
					const Vec3 s = t->GetScale();
					err = (std::max)(err, fabsf(s.x - XMVectorGetX(r.S)) + fabsf(s.y - XMVectorGetY(r.S)) + fabsf(s.z - XMVectorGetZ(r.S)));
					maxErr = (std::max)(maxErr, err);
					++checks;
					if (err > 1e-3f)
					{
						if (first.empty())
							first = std::string(when) + ": '" + go->GetName() + "' error " + std::to_string(err);
						++bad;
					}
				}
			};
			for (int op = 0; op < 3000; ++op)
			{
				GameObject* go = objs[(size_t)I(N)];
				Transform* t = go->GetTransform();
				switch (I(9))
				{
				case 0: t->SetLocalPosition(Vec3(U(-5, 5), U(-5, 5), U(-5, 5))); break;
				case 1: t->SetLocalRotation(RandRot()); break;
				case 2: t->SetLocalScale(Vec3(U(0.5f, 2.0f), U(0.5f, 2.0f), U(0.5f, 2.0f))); break;
				case 3: t->SetPosition(Vec3(U(-20, 20), U(-20, 20), U(-20, 20))); break;
				case 4: t->SetRotation(RandRot()); break;
				case 5:
				{
					GameObject* p = I(5) == 0 ? nullptr : objs[(size_t)I(N)];
					go->SetParent(p, I(2) == 0);   // 자손 밑으로는 GameObject 가 거절한다
					++reparents;
					break;
				}
				case 6: (void)objs[(size_t)I(N)]->GetTransform()->GetWorldMatrix(); break;   // 늦은 계산 (그 사슬만)
				case 7: TransformStore::Flush(); break;
				case 8:
				{
					// 더러운 채로 잡에서 읽기 — 배열을 바꾸지 않고 그 자리에서 계산한 값이 같아야 한다
					std::vector<XMFLOAT4X4> ref(objs.size()), got(objs.size());
					for (size_t k = 0; k < objs.size(); ++k)
						XMStoreFloat4x4(&ref[k], RefWorld(objs[k]->GetTransform()).M);
					Jobs::ParallelFor((int)objs.size(), 8, [&](int b, int e) {
						for (int k = b; k < e; ++k)
							got[(size_t)k] = objs[(size_t)k]->GetTransform()->GetWorldMatrix();
					}, "Transform Test Read");
					for (size_t k = 0; k < objs.size(); ++k)
					{
						++jobChecks;
						if (MatrixError(got[k], ref[k]) > 1e-3f)
							++jobBad;
					}
					break;
				}
				}
				if (op % 100 == 99)
					verifyAll("during");
			}
			TransformStore::Flush();
			verifyAll("after flush");
			for (GameObject* go : objs)
				if (go->GetParent() == nullptr)
					scene->DestroyGameObject(go);
			const bool ok = bad == 0 && jobBad == 0 && checks > 0 && jobChecks > 0;
			char detail[256];
			snprintf(detail, sizeof(detail), "%d transforms, 3000 random ops (%d reparents): %d / %d world checks off, job reads %d / %d off, max error %.2e%s%s",
				N, reparents, bad, checks, jobBad, jobChecks, maxErr, first.empty() ? "" : " — first: ", first.c_str());
			return { { "name", "hierarchy: lazy + batched world values match the eager chain (also read from jobs while dirty)" }, { "ok", ok }, { "detail", detail }, { "ms", MsSince(t0) } };
		}

		// 절두체 (SceneCulling 과 같은 평면) 로 직접: 보이면 1, 밖이면 0, 경계 (오차 안) 면 -1
		int BruteVisible(const XMFLOAT4 planes[6], const Vec3& mn, const Vec3& mx)
		{
			const Vec3 c = (mn + mx) * 0.5f, h = (mx - mn) * 0.5f;
			bool edge = false;
			for (int i = 0; i < 6; ++i)
			{
				const XMFLOAT4& p = planes[i];
				const float d = p.x * c.x + p.y * c.y + p.z * c.z + p.w;
				const float r = h.x * fabsf(p.x) + h.y * fabsf(p.y) + h.z * fabsf(p.z);
				if (fabsf(d + r) < 1e-3f)
					edge = true;
				else if (d < -r)
					return 0;
			}
			return edge ? -1 : 1;
		}

		nlohmann::json CullingTest(Scene* scene)
		{
			const auto t0 = Clock::now();
			std::mt19937 rng(77);
			auto U = [&](float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); };
			auto I = [&](int n) { return std::uniform_int_distribution<int>(0, n - 1)(rng); };
			std::vector<GameObject*> roots, all;
			for (int r = 0; r < 300; ++r)
			{
				GameObject* go = GameObjectFactory::CreatePrimitive(PrimitiveType::Cube, "CullTest");
				scene->AddRootGameObject(go);
				go->GetTransform()->SetPosition(Vec3(U(-150, 150), U(-5, 5), U(-150, 150)));
				roots.push_back(go);
				all.push_back(go);
				for (int c = 0; c < 4; ++c)
				{
					GameObject* ch = GameObjectFactory::CreatePrimitive(PrimitiveType::Cube, "CullTestChild");
					ch->SetParent(go, false);
					ch->GetTransform()->SetLocalPosition(Vec3(U(-3, 3), U(0, 2), U(-3, 3)));
					all.push_back(ch);
				}
			}
			const XMMATRIX viewProj = XMMatrixLookAtLH(XMVectorSet(0.0f, 30.0f, -100.0f, 1.0f), XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f), XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f))
				* XMMatrixPerspectiveFovLH(XMConvertToRadians(60.0f), 16.0f / 9.0f, 0.3f, 400.0f);
			XMFLOAT4 planes[6];
			{
				XMFLOAT4X4 f;
				XMStoreFloat4x4(&f, viewProj);
				auto col = [&](int c) { return XMVectorSet(f.m[0][c], f.m[1][c], f.m[2][c], f.m[3][c]); };
				const XMVECTOR c0 = col(0), c1 = col(1), c2 = col(2), c3 = col(3);
				const XMVECTOR p[6] = { c3 + c0, c3 - c0, c3 + c1, c3 - c1, c3 - c2, c2 };
				for (int i = 0; i < 6; ++i)
					XMStoreFloat4(&planes[i], XMPlaneNormalize(p[i]));
			}
			int checks = 0, wrong = 0, visibleTotal = 0, meshSwaps = 0;
			std::string first;
			for (int frame = 0; frame < 40; ++frame)
			{
				// 움직이기 (루트 · 자식), 몇 프레임마다 메시 바꾸기 (MeshFilter — 상자가 커진다), 끝의 몇 프레임은 그대로
				if (frame < 36)
				{
					for (GameObject* go : roots)
						if (I(5) == 0)
							go->GetTransform()->SetPosition(Vec3(U(-150, 150), U(-5, 5), U(-150, 150)));
					for (GameObject* go : all)
						if (go->GetParent() && I(10) == 0)
							go->GetTransform()->SetLocalPosition(Vec3(U(-3, 3), U(0, 2), U(-3, 3)));
					if (frame % 10 == 5)
						for (int k = 0; k < 20; ++k)
							if (MeshFilter* mf = all[(size_t)I((int)all.size())]->GetComponent<MeshFilter>())
							{
								mf->SetMesh(GameObjectFactory::GetPrimitiveMesh(PrimitiveType::Plane), GameObjectFactory::GetBuiltinMeshPath(PrimitiveType::Plane), 0);
								++meshSwaps;
							}
				}
				SceneCulling::SetEditorView(false);
				SceneCulling::Update(scene);
				SceneCulling::Cull(viewProj, false);
				for (GameObject* go : all)
				{
					MeshRenderer* mr = go->GetComponent<MeshRenderer>();
					if (!mr)
						continue;
					auto mesh = mr->GetMesh();
					if (!mesh || mesh->Vertices.empty())
						continue;
					Vec3 mn(FLT_MAX, FLT_MAX, FLT_MAX), mx(-FLT_MAX, -FLT_MAX, -FLT_MAX);
					for (const auto& v : mesh->Vertices)
					{
						mn = Vec3::Min(mn, Vec3(v.pos));
						mx = Vec3::Max(mx, Vec3(v.pos));
					}
					const Matrix w = go->GetTransform()->GetWorldMatrix();
					const Vec3 c = (mn + mx) * 0.5f, h = (mx - mn) * 0.5f;
					const Vec3 wc = Vec3::Transform(c, w);
					const Vec3 wh(h.x * fabsf(w._11) + h.y * fabsf(w._21) + h.z * fabsf(w._31), h.x * fabsf(w._12) + h.y * fabsf(w._22) + h.z * fabsf(w._32),
						h.x * fabsf(w._13) + h.y * fabsf(w._23) + h.z * fabsf(w._33));
					const int expect = BruteVisible(planes, wc - wh, wc + wh);
					if (expect < 0)
						continue;
					const bool got = SceneCulling::IsVisible(mr);
					++checks;
					visibleTotal += got ? 1 : 0;
					if (got != (expect == 1))
					{
						++wrong;
						if (first.empty())
							first = "frame " + std::to_string(frame) + ": '" + go->GetName() + "' expected " + (expect ? "visible" : "hidden");
					}
				}
			}
			for (GameObject* go : roots)
				scene->DestroyGameObject(go);
			const nlohmann::json info = SceneCulling::Info();
			char detail[320];
			snprintf(detail, sizeof(detail), "%zu renderers x 40 frames (moves, %d mesh swaps, still frames): %d / %d visibility decisions differ from a brute-force frustum test, %d visible%s%s",
				all.size(), meshSwaps, wrong, checks, visibleTotal, first.empty() ? "" : " — first: ", first.c_str());
			return { { "name", "culling: registry + version skips + octree reuse + SIMD test agree with brute force" }, { "ok", wrong == 0 && checks > 1000 }, { "detail", detail }, { "ms", MsSince(t0) },
				{ "culling", info } };
		}
	}

	void RegisterEditor()
	{
		CliServer::Register("transform", "Transform (data-oriented SoA store) / culling: {op: info|test|bench, roots?: 2000, children?: 9, depth?: 1, frames?: 30} — builds a cube hierarchy in the current scene, moves the roots every frame, times hierarchy update / world reads / culling, then deletes it",
			[](const nlohmann::json& args, nlohmann::json& result, std::string& error) {
				const std::string op = args.value("op", std::string("bench"));
				if (op == "info")
				{
					result = TransformStore::Info();
					result["culling"] = SceneCulling::Info();
					return true;
				}
				if (op != "bench" && op != "test")
				{
					error = "unknown op '" + op + "' (bench, test, info)";
					return false;
				}
				if (Application::IsPlaying())
				{
					error = "stop Play first (the benchmark edits the scene)";
					return false;
				}
				Scene* scene = SceneManager::GetI()->GetCurrentScene();
				if (!scene)
				{
					error = "no scene is open";
					return false;
				}
				if (op == "test")
				{
					nlohmann::json tests = nlohmann::json::array();
					tests.push_back(HierarchyTest(scene));
					tests.push_back(CullingTest(scene));
					bool all = true;
					for (const auto& t : tests)
						all = all && t.value("ok", false);
					result = { { "ok", all }, { "tests", tests } };
					return true;
				}
				const int roots = (std::max)(1, args.value("roots", 2000));
				const int children = (std::max)(0, args.value("children", 9));
				const int depth = (std::max)(0, args.value("depth", 1));
				const int frames = (std::max)(1, args.value("frames", 30));

				// 짓기: 루트는 바둑판 (가운데가 원점)
				auto t0 = Clock::now();
				std::vector<GameObject*> rootObjects;
				std::vector<Vec3> basePositions;
				int count = 0;
				const int side = (int)ceilf(sqrtf((float)roots));
				for (int r = 0; r < roots; ++r)
				{
					GameObject* go = GameObjectFactory::CreatePrimitive(PrimitiveType::Cube, "BenchRoot");
					scene->AddRootGameObject(go);
					const Vec3 p((float)(r % side - side / 2) * 5.0f, 0.5f, (float)(r / side - side / 2) * 5.0f);
					go->GetTransform()->SetPosition(p);
					rootObjects.push_back(go);
					basePositions.push_back(p);
					++count;
					AddChildren(go, children, depth, count);
				}
				const double buildMs = MsSince(t0);

				std::vector<Transform*> all;
				all.reserve((size_t)count);
				for (GameObject* go : scene->GameObjectsView())
					if (go && go->GetTransform())
						all.push_back(go->GetTransform());

				// 카메라: 위에서 비스듬히 원점을 본다 (바둑판의 앞쪽 일부가 보인다)
				const XMMATRIX view = XMMatrixLookAtLH(XMVectorSet(0.0f, 40.0f, -120.0f, 1.0f), XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f), XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
				const XMMATRIX proj = XMMatrixPerspectiveFovLH(XMConvertToRadians(60.0f), 16.0f / 9.0f, 0.3f, 1000.0f);
				const XMMATRIX viewProj = view * proj;

				SceneCulling::SetEditorView(false);
				SceneCulling::Update(scene);   // 처음 넣기 (재지 않는다)

				double moveMs = 0, flushMs = 0, readMs = 0, cullUpdateMovedMs = 0, cullMs = 0, cullUpdateStaticMs = 0;
				float checksum = 0.0f;
				for (int f = 0; f < frames; ++f)
				{
					// 1) 루트 움직이기 (위치 + 회전) — 계층 갱신
					t0 = Clock::now();
					const float time = (float)f * 0.05f;
					for (size_t i = 0; i < rootObjects.size(); ++i)
					{
						Transform* t = rootObjects[i]->GetTransform();
						t->SetPosition(basePositions[i] + Vec3(0.0f, sinf(time + (float)i) * 0.5f, 0.0f));
						t->SetRotation(Quaternion::CreateFromYawPitchRoll(time + (float)i * 0.1f, 0.0f, 0.0f));
					}
					moveMs += MsSince(t0);

					// 1b) 바뀐 계층을 한 번에 (데이터 지향 Transform — 예전에는 1) 에서 즉시 계산했다)
					t0 = Clock::now();
					TransformStore::Flush();
					flushMs += MsSince(t0);

					// 2) 모든 오브젝트의 월드 행렬 읽기 (렌더러 · 물리가 하는 일)
					t0 = Clock::now();
					for (Transform* t : all)
					{
						const Matrix m = t->GetWorldMatrix();
						checksum += m._41;
					}
					readMs += MsSince(t0);

					// 3) 컬링 갱신 (움직인 렌더러를 옥트리에 다시) · 4) 절두체 검사
					t0 = Clock::now();
					SceneCulling::Update(scene);
					cullUpdateMovedMs += MsSince(t0);
					t0 = Clock::now();
					SceneCulling::Cull(viewProj, false);
					cullMs += MsSince(t0);
				}
				// 5) 아무것도 안 움직이는 프레임의 컬링 갱신 (변하지 않은 렌더러를 확인만)
				for (int f = 0; f < frames; ++f)
				{
					t0 = Clock::now();
					SceneCulling::Update(scene);
					cullUpdateStaticMs += MsSince(t0);
				}
				const int visible = SceneCulling::LastStats(false).Visible;

				t0 = Clock::now();
				for (GameObject* go : rootObjects)
					scene->DestroyGameObject(go);
				const double destroyMs = MsSince(t0);

				const double n = (double)frames;
				result = { { "objects", count }, { "roots", roots }, { "children", children }, { "depth", depth }, { "frames", frames },
					{ "buildMs", buildMs }, { "destroyMs", destroyMs },
					{ "moveRootsMs", moveMs / n }, { "flushMs", flushMs / n }, { "readWorldMs", readMs / n }, { "cullUpdateMovedMs", cullUpdateMovedMs / n },
					{ "cullMs", cullMs / n }, { "cullUpdateStaticMs", cullUpdateStaticMs / n }, { "visible", visible },
					{ "totalMovedFrameMs", (moveMs + flushMs + readMs + cullUpdateMovedMs + cullMs) / n }, { "checksum", checksum } };
				return true;
			});
	}
}
