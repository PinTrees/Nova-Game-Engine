#include "pch.h"
#include "ModelPlacement.h"
#include "GameObject.h"
#include "GameObjectFactory.h"
#include "Transform.h"
#include "MeshFilter.h"
#include "MeshRenderer.h"
#include "SkinnedMeshRenderer.h"
#include "Mesh.h"
#include "SkinnedMesh.h"
#include "SkinnedData.h"
#include "LODGroup.h"
#include "Scene.h"
#include "SceneManager.h"
#include "ResourceManager.h"
#include "PathManager.h"
#include "SelectionManager.h"
#include "UndoSystem.h"
#include "VrmImport.h"
#include "FBXLoader.h"
#include "EditorLog.h"
#include "CliServer.h"
#include <filesystem>
#include <regex>
#include <map>

namespace
{
	// 노드 로컬 행렬 (행 벡터) → Transform (위치 · 회전 · 배율)
	void ApplyLocal(GameObject* go, CXMMATRIX m)
	{
		XMVECTOR s, r, t;
		if (!XMMatrixDecompose(&s, &r, &t, m))
			return;
		Transform* tr = go->GetTransform();
		tr->SetLocalScale(Vec3(XMVectorGetX(s), XMVectorGetY(s), XMVectorGetZ(s)));
		tr->SetLocalRotation(Quaternion(r));
		tr->SetLocalPosition(Vec3(XMVectorGetX(t), XMVectorGetY(t), XMVectorGetZ(t)));
	}

	// 씬에 넣기 전의 부모-자식 (씬 목록을 건드리지 않는다 — GameObjectFactory::AddSkinnedChildren 과 같음)
	void Attach(GameObject* child, GameObject* parent)
	{
		GameObjectFactory::AttachChild(child, parent);
	}

	// 정적 메시 i 를 이 GameObject 에 (Mesh Filter + Mesh Renderer, 재질 칸)
	void AddMesh(GameObject* go, const std::wstring& relPath, int index, const std::shared_ptr<Mesh>& mesh, const std::vector<std::wstring>& materials)
	{
		go->AddComponent<MeshFilter>()->SetMesh(mesh, relPath, index);
		MeshRenderer* mr = go->AddComponent<MeshRenderer>();
		int slots = 1;
		for (const auto& subset : mesh->Subsets)
			slots = (std::max)(slots, (int)subset.MaterialIndex + 1);
		for (int m = 0; m < slots; ++m)
			mr->SetMaterialPath(m, m < (int)materials.size() && !materials[m].empty() ? materials[m] : std::wstring(L"builtin:Default-Material"));
	}

	// `이름_LOD숫자` 인 자식이 있는 GameObject 마다 LOD Group (씬에 넣은 뒤 — 렌더러를 fileID 로 가리킨다)
	int SetupLODs(GameObject* go)
	{
		int made = 0;
		static const std::regex kLod(R"(^(.*)_LOD(\d+)$)", std::regex::icase);
		std::map<int, std::vector<GameObject*>> levels;
		for (GameObject* c : go->Children())
		{
			std::smatch m;
			const std::string name = c->GetName();
			if (std::regex_match(name, m, kLod))
				levels[std::stoi(m[2].str())].push_back(c);
		}
		if (levels.size() >= 2)   // LOD 가 둘 이상일 때만 (충돌용 UCX_.._LOD0 하나 같은 것은 그냥 메시)
		{
			// Unity 처럼: LOD 0 = 60 %, 그다음 반씩, 마지막 LOD 는 1 % 까지
			std::vector<LODGroup::LOD> lods;
			int i = 0;
			for (auto& [level, objects] : levels)
			{
				if (i >= LODGroup::kMaxLODs)
					break;
				LODGroup::LOD lod;
				lod.ScreenRelativeTransitionHeight = (i + 1 == (int)levels.size() && i > 0) ? 0.01f : 0.6f * powf(0.5f, (float)i);
				// 그 LOD 노드와 자식 중 렌더러가 있는 것
				std::function<void(GameObject*)> visit = [&](GameObject* o) {
					if (o->GetComponent<MeshRenderer>() || o->GetComponent<SkinnedMeshRenderer>())
						lod.Renderers.push_back(o->GetFileID());
					for (GameObject* cc : o->Children())
						visit(cc);
				};
				for (GameObject* o : objects)
					visit(o);
				lods.push_back(lod);
				++i;
			}
			LODGroup* group = go->GetComponent<LODGroup>();
			if (!group)
				group = go->AddComponent<LODGroup>();
			group->SetLODs(lods);
			group->RecalculateBounds();
			++made;
		}
		for (GameObject* c : go->Children())
			made += SetupLODs(c);
		return made;
	}

	std::wstring ProjectFile(const std::string& path)
	{
		std::filesystem::path p(string_to_wstring(path));
		if (p.is_absolute())
			return p.wstring();
		return PathManager::GetI()->GetMovePathW(p.wstring());
	}

	// 정적 모델 → GameObject 트리 (아직 씬 밖)
	GameObject* BuildStatic(const std::string& name, const std::string& rel, MeshFile& file)
	{
		const std::wstring relW = string_to_wstring(rel);
		std::vector<std::wstring> materials;
		const std::wstring ext = std::filesystem::path(relW).extension().wstring();
		if (_wcsicmp(ext.c_str(), L".glb") == 0)
			materials = VrmImport::ExtractMaterials(relW);   // GLB 의 묻힌 재질 (Unity 의 Extract Materials 자리)
		else if (_wcsicmp(ext.c_str(), L".fbx") == 0)
			materials = FBXLoader::ExtractMaterials(relW);   // FBX 재질 색 · 그림 (프로젝트 Assets 의 모델만)

		GameObject* root = new GameObject(name);
		const SkeletonAvataData* tree = file.Avatas.empty() ? nullptr : file.Avatas[0].get();
		const int nodes = tree ? (int)tree->NodeNames.size() : 0;
		if (nodes == 0)
		{
			// 노드 트리가 없으면 메시마다 자식
			for (int i = 0; i < (int)file.Meshs.size(); ++i)
			{
				GameObject* child = new GameObject(file.Meshs[i]->Name.empty() ? "Mesh" + std::to_string(i) : file.Meshs[i]->Name);
				AddMesh(child, relW, i, file.Meshs[i], materials);
				Attach(child, root);
			}
			return root;
		}

		// 메시 → 노드 (이름이 같은 노드, 같은 이름이 여럿이면 차례로)
		std::map<std::string, std::vector<int>> byName;
		for (int n = 0; n < nodes; ++n)
			byName[tree->NodeNames[n]].push_back(n);
		std::vector<std::vector<int>> meshesOf(nodes);
		for (int i = 0; i < (int)file.Meshs.size(); ++i)
		{
			auto it = byName.find(file.Meshs[i]->Name);
			int node = 0;
			if (it != byName.end() && !it->second.empty())
			{
				node = it->second.front();
				if (it->second.size() > 1)
					it->second.erase(it->second.begin());
			}
			meshesOf[node].push_back(i);
		}

		// 파일 단위 (FBX cm → m). 정점에는 Scale Factor 만 들어 있어 노드 위치에도 Scale Factor 를 곱한다
		const float sf = file.ScaleFactor > 0.0f ? file.ScaleFactor : 1.0f;
		const float fileScale = tree->UnitScale / sf;
		auto localOf = [&](int n) {
			XMFLOAT4X4 m = tree->BindLocal[n];
			m._41 *= sf; m._42 *= sf; m._43 *= sf;
			return XMLoadFloat4x4(&m);
		};
		// 노드 0 (파일 루트) 의 변환 + 파일 단위는 최상위 자식에 접어 넣는다 — 루트 GameObject 는 원점 · 배율 1
		const XMMATRIX top = localOf(0) * XMMatrixScaling(fileScale, fileScale, fileScale);

		std::vector<int> children0;
		std::vector<std::vector<int>> kids(nodes);
		for (int n = 1; n < nodes; ++n)
		{
			const int p = tree->BoneHierarchy[n];
			if (p <= 0 || p >= n)
				children0.push_back(n);
			else
				kids[p].push_back(n);
		}
		for (int i : meshesOf[0])
			AddMesh(root, relW, i, file.Meshs[i], materials);

		// 노드 하나 (자식 없음) 짜리 모델: Unity 처럼 루트에 바로
		if (children0.size() == 1 && kids[children0[0]].empty() && meshesOf[0].empty())
		{
			const int n = children0[0];
			for (int i : meshesOf[n])
				AddMesh(root, relW, i, file.Meshs[i], materials);
			XMVECTOR s, r, t;
			if (XMMatrixDecompose(&s, &r, &t, localOf(n) * top))
			{
				root->GetTransform()->SetLocalScale(Vec3(XMVectorGetX(s), XMVectorGetY(s), XMVectorGetZ(s)));
				root->GetTransform()->SetLocalRotation(Quaternion(r));
			}
			return root;
		}

		std::function<void(int, GameObject*, bool)> build = [&](int n, GameObject* parent, bool topLevel) {
			GameObject* go = new GameObject(tree->NodeNames[n].empty() ? "Node" + std::to_string(n) : tree->NodeNames[n]);
			for (int i : meshesOf[n])
				AddMesh(go, relW, i, file.Meshs[i], materials);
			Attach(go, parent);
			ApplyLocal(go, topLevel ? localOf(n) * top : localOf(n));
			for (int c : kids[n])
				build(c, go, false);
		};
		for (int n : children0)
			build(n, root, true);
		return root;
	}
}

namespace ModelPlacement
{
	GameObject* Instantiate(const std::string& path, GameObject* parent, const Vec3& position, std::string* error)
	{
		auto fail = [&](const std::string& message) -> GameObject* { if (error) *error = message; EditorLog::Write("Model", "%s", message.c_str()); return nullptr; };
		const std::wstring abs = ProjectFile(path);
		std::error_code ec;
		if (!std::filesystem::exists(abs, ec))
			return fail("model not found: " + path);
		Scene* scene = SceneManager::GetI()->GetCurrentScene();
		if (scene == nullptr)
			return fail("no scene");
		const std::string rel = wstring_to_string(PathManager::GetI()->GetCutSolutionPath(abs));
		const std::string name = std::filesystem::path(abs).stem().string();
		auto file = ResourceManager::GetI()->LoadMeshFile(rel);
		if (!file || (file->Meshs.empty() && file->SkinnedMeshs.empty()))
			return fail("could not load a mesh from " + path + " (FBX · GLB · glTF · VRM)");

		Undo::SetActionName("Instantiate Model");
		GameObject* root = !file->SkinnedMeshs.empty()
			? GameObjectFactory::CreateAnimatedCharacter(name, rel)   // 캐릭터 (예전과 같음)
			: BuildStatic(name, rel, *file);
		if (parent == nullptr)
		{
			scene->AddRootGameObject(root);
			root->GetTransform()->SetLocalPosition(position);
		}
		else
		{
			root->SetParent(parent, false);
			scene->RegisterGameObjectTree(root);
		}
		const int lodGroups = SetupLODs(root);   // 씬에 넣은 뒤 (렌더러 fileID · 범위)
		SelectionManager::SetSelectedGameObject(root);
		EditorLog::Write("Model", "placed %s (%d static meshes, %d skinned, %d LOD Groups)", rel.c_str(), (int)file->Meshs.size(), (int)file->SkinnedMeshs.size(), lodGroups);
		return root;
	}

	void RegisterEditor()
	{
		// nova modelfile place <path> [--parent P] [--position x,y,z] | info <path>
		CliServer::Register("modelfile", "model file op: {op: place | info, path, parent?, position?} (nova modelfile help)", [](const nlohmann::json& args, nlohmann::json& result, std::string& error) {
			const std::string op = args.value("op", std::string("help"));
			const std::string path = args.value("path", std::string());
			if (op == "help")
			{
				result = { { "ops", { "place <path> [--parent P] [--position x,y,z]: put a model in the scene like a Project-window drop (Mesh Renderers per node, _LODn nodes -> LOD Group)",
					"info <path>: static / skinned meshes and nodes" } } };
				return true;
			}
			if (op == "info")
			{
				auto file = ResourceManager::GetI()->LoadMeshFile(wstring_to_string(PathManager::GetI()->GetCutSolutionPath(ProjectFile(path))));
				if (!file) { error = "could not load " + path; return false; }
				nlohmann::json meshes = nlohmann::json::array(), nodes = nlohmann::json::array();
				for (const auto& m : file->Meshs)
					meshes.push_back({ { "name", m->Name }, { "subsets", m->Subsets.size() }, { "vertices", m->Vertices.size() } });
				if (!file->Avatas.empty())
					for (const auto& n : file->Avatas[0]->NodeNames)
						nodes.push_back(n);
				// 스킨 메시: 서브셋마다 파일 재질 번호 (재질 칸 = 파일 재질 번호 — Unity 가져오기가 서브메시 순서를 칸으로 옮긴다)
				nlohmann::json skinned = nlohmann::json::array();
				for (const auto& m : file->SkinnedMeshs)
				{
					nlohmann::json mats = nlohmann::json::array();
					for (const auto& s : m->Subsets)
						mats.push_back(s.MaterialIndex);
					nlohmann::json bounds = nlohmann::json::array();
					if (!file->Avatas.empty() && file->Avatas[0])
					{
						XMFLOAT3 mn[SkinnedMesh::kBindCandidates], mx[SkinnedMesh::kBindCandidates];
						m->BindCandidateBounds(*file->Avatas[0], mn, mx);
						for (int c = 0; c < SkinnedMesh::kBindCandidates; ++c)
							bounds.push_back({ mn[c].x, mn[c].y, mn[c].z, mx[c].x, mx[c].y, mx[c].z });
					}
					skinned.push_back({ { "name", m->Name }, { "subsetMaterials", mats }, { "vertices", m->Vertices.size() }, { "bindBounds", bounds } });
				}
				result = { { "staticMeshes", meshes }, { "skinnedMeshes", file->SkinnedMeshs.size() }, { "skinned", skinned }, { "nodes", nodes },
					{ "unitScale", file->Avatas.empty() ? 1.0f : file->Avatas[0]->UnitScale } };
				return true;
			}
			if (op == "place")
			{
				GameObject* parent = nullptr;
				Scene* scene = SceneManager::GetI()->GetCurrentScene();
				const std::string parentName = args.value("parent", std::string());
				if (!parentName.empty() && scene)
					for (GameObject* go : scene->GetAllGameObjects())
						if (go && go->GetName() == parentName) { parent = go; break; }
				if (!parentName.empty() && !parent) { error = "no GameObject '" + parentName + "'"; return false; }
				Vec3 pos(0, 0, 0);
				if (args.contains("position") && args["position"].is_array() && args["position"].size() == 3)
					pos = Vec3(args["position"][0].get<float>(), args["position"][1].get<float>(), args["position"][2].get<float>());
				GameObject* root = Instantiate(path, parent, pos, &error);
				if (!root)
					return false;
				Undo::Touch(root);
				Undo::RequestCheck();
				// GameObject 수 · 렌더러 수 · 월드 범위 (메시 정점 → 월드)
				int count = 0, renderers = 0;
				Vec3 mn(FLT_MAX, FLT_MAX, FLT_MAX), mx(-FLT_MAX, -FLT_MAX, -FLT_MAX);
				std::function<void(GameObject*)> visit = [&](GameObject* o) {
					++count;
					if (MeshRenderer* mr = o->GetComponent<MeshRenderer>())
						if (auto mesh = mr->GetMesh())
						{
							++renderers;
							const Matrix w = o->GetTransform()->GetWorldMatrix();
							for (const auto& v : mesh->Vertices)
							{
								const Vec3 p = Vec3::Transform(Vec3(v.pos), w);
								mn = Vec3::Min(mn, p);
								mx = Vec3::Max(mx, p);
							}
						}
					for (GameObject* c : o->Children())
						visit(c);
				};
				visit(root);
				result = { { "name", root->GetName() }, { "gameObjects", count }, { "meshRenderers", renderers } };
				if (renderers > 0)
					result["bounds"] = { { mn.x, mn.y, mn.z }, { mx.x, mx.y, mx.z } };
				return true;
			}
			error = "unknown op '" + op + "' (nova modelfile help)";
			return false;
		});
	}
}
