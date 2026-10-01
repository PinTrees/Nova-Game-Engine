#include "pch.h"
#include "SceneViewState.h"
#include "EditorCamera.h"
#include "SceneManager.h"
#include "Scene.h"
#include <chrono>
#include <fstream>
#include <filesystem>

namespace
{
	bool s_Started = false;
	std::wstring s_ScenePath;          // 지금 시점을 기록 중인 씬
	XMFLOAT3 s_LastPos = {}, s_LastLook = {};
	bool s_Pending = false;            // 움직였고 아직 저장 안 함
	std::chrono::steady_clock::time_point s_LastChange;

	std::wstring File() { return PathManager::GetI()->GetMovePathW(L"UserSettings\\SceneView.json"); }

	nlohmann::json Read()
	{
		std::ifstream in(File());
		if (!in)
			return nlohmann::json::object();
		nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
		return j.is_object() ? j : nlohmann::json::object();
	}

	std::string Key(const std::wstring& scenePath)
	{
		// 프로젝트 폴더 기준이면 그 상대 경로, 저장 안 한 씬은 "<untitled>"
		if (scenePath.empty())
			return "<untitled>";
		std::wstring p = scenePath;
		std::replace(p.begin(), p.end(), L'/', L'\\');
		return wstring_to_string(p);
	}

	bool Same(const XMFLOAT3& a, const XMFLOAT3& b, float eps)
	{
		return fabsf(a.x - b.x) < eps && fabsf(a.y - b.y) < eps && fabsf(a.z - b.z) < eps;
	}

	void Save(const std::wstring& scenePath, const XMFLOAT3& pos, const XMFLOAT3& look)
	{
		nlohmann::json j = Read();
		if (!j.contains("scenes") || !j["scenes"].is_object())
			j["scenes"] = nlohmann::json::object();
		j["scenes"][Key(scenePath)] = { { "position", { pos.x, pos.y, pos.z } }, { "look", { look.x, look.y, look.z } } };
		std::error_code ec;
		std::filesystem::create_directories(std::filesystem::path(File()).parent_path(), ec);
		std::ofstream os(File(), std::ios::trunc);
		if (os)
			os << j.dump(4);
	}

	bool Restore(EditorCamera* camera, const std::wstring& scenePath)
	{
		const nlohmann::json j = Read();
		if (!j.contains("scenes") || !j["scenes"].is_object())
			return false;
		const std::string key = Key(scenePath);
		if (!j["scenes"].contains(key))
			return false;
		const auto& s = j["scenes"][key];
		if (!s.contains("position") || !s.contains("look") || s["position"].size() < 3 || s["look"].size() < 3)
			return false;
		const XMFLOAT3 pos(s["position"][0].get<float>(), s["position"][1].get<float>(), s["position"][2].get<float>());
		XMFLOAT3 look(s["look"][0].get<float>(), s["look"][1].get<float>(), s["look"][2].get<float>());
		const float len = sqrtf(look.x * look.x + look.y * look.y + look.z * look.z);
		if (!(len > 1e-4f) || !std::isfinite(pos.x + pos.y + pos.z))
			return false;
		look = XMFLOAT3(look.x / len, look.y / len, look.z / len);
		// 바로 위·아래를 보면 LookAt 의 위 방향과 겹치므로 살짝 비켜 둔다
		if (fabsf(look.y) > 0.999f)
			look = XMFLOAT3(0.001f, look.y > 0 ? 0.999f : -0.999f, 0.0f);
		camera->LookAt(pos, XMFLOAT3(pos.x + look.x, pos.y + look.y, pos.z + look.z), XMFLOAT3(0.0f, 1.0f, 0.0f));
		camera->UpdateViewMatrix();
		return true;
	}
}

namespace SceneViewState
{
	void Update(EditorCamera* camera)
	{
		Scene* scene = SceneManager::GetI()->GetCurrentScene();
		if (camera == nullptr || scene == nullptr)
			return;
		const std::wstring path = scene->GetScenePath();
		const XMFLOAT3 pos = camera->GetPosition(), look = camera->GetLook();

		// 씬이 바뀜 (시작 포함): 이전 씬의 움직임을 마저 저장하고 새 씬의 시점으로
		if (!s_Started || path != s_ScenePath)
		{
			if (s_Started && s_Pending)
				Save(s_ScenePath, s_LastPos, s_LastLook);
			s_Started = true;
			s_ScenePath = path;
			s_Pending = false;
			// 검사용 시점(NOVA_SCENE_CAM / NOVA_DEV_SCENECAM)이 있으면 그것이 먼저
			const bool devCam = ::GetEnvironmentVariableA("NOVA_SCENE_CAM", nullptr, 0) > 0 || ::GetEnvironmentVariableA("NOVA_DEV_SCENECAM", nullptr, 0) > 0;
			const bool restored = !devCam && Restore(camera, path);
			EditorLog::Write("SceneView", "scene camera for %s: %s", Key(path).c_str(), restored ? "restored" : "default");
			s_LastPos = camera->GetPosition();
			s_LastLook = camera->GetLook();
			return;
		}

		// 움직임 감지 → 멈춘 지 0.5 초 뒤 저장 (끄는 중에는 저장하지 않는다)
		const auto now = std::chrono::steady_clock::now();
		if (!Same(pos, s_LastPos, 1e-3f) || !Same(look, s_LastLook, 1e-4f))
		{
			s_LastPos = pos;
			s_LastLook = look;
			s_Pending = true;
			s_LastChange = now;
			return;
		}
		if (s_Pending && std::chrono::duration<float>(now - s_LastChange).count() > 0.5f)
		{
			s_Pending = false;
			Save(path, pos, look);
		}
	}
}
