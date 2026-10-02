#include "pch.h"
#include "SpriteAnimator.h"
#include "SpriteRenderer.h"
#include "UnityGUI.h"
#include "UISprites.h"
#include "EngineTime.h"
#include "EditorExtensions.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace
{
	struct Cached { long long Stamp = 0; std::shared_ptr<SpriteAnimClip> Clip; };
	std::map<std::string, Cached> s_Clips;

	std::wstring Full(const std::string& path) { return PathManager::GetI()->GetMovePathW(string_to_wstring(path)); }

	long long Stamp(const std::wstring& full)
	{
		std::error_code ec;
		const auto t = fs::last_write_time(full, ec);
		return ec ? 0 : (long long)t.time_since_epoch().count();
	}

	std::string ProjectRelative(std::string p)
	{
		const std::string root = wstring_to_string(PathManager::GetI()->GetContentPathW());
		if (!root.empty() && _strnicmp(p.c_str(), root.c_str(), root.size()) == 0)
			p = p.substr(root.size());
		return p;
	}

	std::string Stem(const std::string& path)
	{
		return wstring_to_string(fs::path(string_to_wstring(path)).stem().wstring());
	}
}

// ------------------------------------------------------------------ 클립 파일
namespace SpriteAnimClips
{
	std::shared_ptr<const SpriteAnimClip> Load(const std::string& path)
	{
		if (path.empty())
			return nullptr;
		const std::wstring full = Full(path);
		const long long stamp = Stamp(full);
		Cached& c = s_Clips[path];
		if (c.Clip && c.Stamp == stamp)
			return c.Clip;
		c.Stamp = stamp;
		c.Clip.reset();
		std::ifstream in(full);
		const json j = json::parse(in, nullptr, false);
		if (!j.is_object())
			return nullptr;
		auto clip = std::make_shared<SpriteAnimClip>();
		clip->Name = Stem(path);
		clip->Fps = (std::max)(0.1f, j.value("fps", 12.0f));
		clip->Loop = j.value("loop", true);
		if (j.contains("frames") && j["frames"].is_array())
			for (const json& f : j["frames"])
				if (f.is_string())
					clip->Frames.push_back(f.get<std::string>());
		c.Clip = clip;
		return clip;
	}

	bool Save(const std::string& path, const SpriteAnimClip& clip)
	{
		const std::wstring full = Full(path);
		std::error_code ec;
		fs::create_directories(fs::path(full).parent_path(), ec);
		std::ofstream os(full, std::ios::trunc);
		if (!os)
			return false;
		os << json{ { "fps", clip.Fps }, { "loop", clip.Loop }, { "frames", clip.Frames } }.dump(2);
		return true;
	}

	// Project 창에서 골랐을 때: fps · loop · 프레임 목록 (작은 그림) + 미리 보기
	void DrawClipInspector(const std::string& path)
	{
		auto loaded = Load(path);
		if (!loaded)
		{
			UnityGUI::HelpBox("Cannot read this sprite animation.");
			return;
		}
		SpriteAnimClip clip = *loaded;
		bool changed = false;
		UnityGUI::Label(clip.Name.c_str(), 0, true);
		changed |= UnityGUI::Float("Frame Rate", &clip.Fps);
		clip.Fps = (std::max)(0.1f, clip.Fps);
		changed |= UnityGUI::Toggle("Loop Time", &clip.Loop);
		char buf[64];
		snprintf(buf, sizeof(buf), "%d frames, %.2f s", (int)clip.Frames.size(), clip.Length());
		UnityGUI::ValueLabel("Length", buf);
		// 움직이는 미리 보기 (편집기 시간으로)
		if (!clip.Frames.empty())
		{
			const int f = (int)(ImGui::GetTime() * clip.Fps) % (int)clip.Frames.size();
			UISprites::Info info;
			if (UISprites::Get(clip.Frames[f], info) && info.Texture)
			{
				const float side = 128.0f, k = side / (std::max)(info.Size.x, info.Size.y);
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 24.0f);
				ImGui::Image((ImTextureID)info.Texture, ImVec2(info.Size.x * k, info.Size.y * k), ImVec2(info.UV.x, info.UV.y), ImVec2(info.UV.z, info.UV.w));
			}
		}
		UnityGUI::Spacing(4.0f);
		int remove = -1;
		for (int i = 0; i < (int)clip.Frames.size(); ++i)
		{
			ImGui::PushID(i);
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 18.0f);
			ImGui::Text("%2d  %s", i, UISprites::DisplayName(clip.Frames[i]).c_str());
			ImGui::SameLine(260.0f);
			if (ImGui::SmallButton(ICON_FA_MINUS))
				remove = i;
			ImGui::PopID();
		}
		if (remove >= 0)
		{
			clip.Frames.erase(clip.Frames.begin() + remove);
			changed = true;
		}
		// 그림을 끌어 놓으면 끝에 프레임 추가
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 18.0f);
		ImGui::Button("Drop a sprite here to add a frame", ImVec2(260.0f, 0));
		if (ImGui::BeginDragDropTarget())
		{
			const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET_FILE");
			if (!p) p = ImGui::AcceptDragDropPayload("PNG_FILE");
			if (p)
			{
				const std::string dropped = ProjectRelative((const char*)p->Data);
				if (UISprites::IsImagePath(dropped))
				{
					clip.Frames.push_back(dropped);
					changed = true;
				}
			}
			ImGui::EndDragDropTarget();
		}
		if (changed)
			Save(path, clip);
	}

	void RegisterEditorAssetType()
	{
		EditorExtensions::AssetType t;
		t.Owner = "engine";
		t.Extension = ".spriteanim";
		t.Icon = "sprite_animator";
		t.CreateMenu = "Sprite Animation";
		t.DefaultName = "New Sprite Animation";
		t.Create = [](const std::string& path) { Save(path, SpriteAnimClip()); };
		t.Inspector = [](const std::string& path) { DrawClipInspector(path); };
		EditorExtensions::RegisterAssetType(t);
	}
}

// ------------------------------------------------------------------ 컴포넌트
SpriteAnimator::SpriteAnimator()
{
	m_InspectorTitleName = "Sprite Animator";
}

std::string SpriteAnimator::CurrentClip() const
{
	return m_Current >= 0 && m_Current < (int)m_Clips.size() ? Stem(m_Clips[m_Current]) : std::string();
}

bool SpriteAnimator::Play(const std::string& clip)
{
	for (int i = 0; i < (int)m_Clips.size(); ++i)
		if (Stem(m_Clips[i]) == clip)
		{
			m_Current = i;
			m_Time = 0.0f;
			m_Frame = -1;
			m_Playing = true;
			if (auto c = SpriteAnimClips::Load(m_Clips[i]))
				ShowFrame(*c, 0);
			return true;
		}
	return false;
}

void SpriteAnimator::Start()
{
	if (PlayOnAwake && !m_Clips.empty())
		Play(Stem(m_Clips[std::clamp(m_Default, 0, (int)m_Clips.size() - 1)]));
}

void SpriteAnimator::ShowFrame(const SpriteAnimClip& c, int frame)
{
	if (frame == m_Frame || c.Frames.empty())
		return;
	m_Frame = frame;
	if (SpriteRenderer* sr = m_pGameObject ? m_pGameObject->GetComponent<SpriteRenderer>() : nullptr)
		sr->SetSprite(c.Frames[std::clamp(frame, 0, (int)c.Frames.size() - 1)]);
}

void SpriteAnimator::Update()
{
	if (!m_Playing || m_Current < 0 || m_Current >= (int)m_Clips.size())
		return;
	auto c = SpriteAnimClips::Load(m_Clips[m_Current]);
	if (!c || c->Frames.empty())
		return;
	m_Time += (std::max)(0.0f, ::Time::DeltaTime()) * Speed;
	int frame = (int)floorf(m_Time * c->Fps);
	const int n = (int)c->Frames.size();
	if (c->Loop)
		frame %= n;
	else if (frame >= n)
	{
		frame = n - 1;
		m_Playing = false;   // 한 번 재생: 마지막 프레임에서 멈춤
	}
	ShowFrame(*c, (std::max)(0, frame));
}

void SpriteAnimator::OnInspectorGUI()
{
	// Clips: .spriteanim 목록 (끌어 놓기 · + / -), 첫 번째 칸 왼쪽 점 = Default
	UnityGUI::Label("Clips", 0, true);
	int remove = -1;
	for (int i = 0; i < (int)m_Clips.size(); ++i)
	{
		ImGui::PushID(i);
		auto c = SpriteAnimClips::Load(m_Clips[i]);
		char label[32];
		snprintf(label, sizeof(label), "%s Clip %d", i == m_Default ? "*" : " ", i);
		std::string text = Stem(m_Clips[i]) + (c ? "  (" + std::to_string(c->Frames.size()) + " frames, " + std::to_string((int)c->Fps) + " fps)" : "  (missing)");
		ImVec2 fmin, fmax;
		const int pressed = UnityGUI::ObjectFieldButtons(label, text.c_str(), "sprite_animator", nullptr, 0, &fmin, &fmax, 1);
		if (pressed == -1)
			m_Default = i;
		ImGui::SameLine();
		if (ImGui::SmallButton(ICON_FA_MINUS))
			remove = i;
		ImGui::PopID();
	}
	if (remove >= 0)
	{
		m_Clips.erase(m_Clips.begin() + remove);
		m_Default = std::clamp(m_Default, 0, (std::max)(0, (int)m_Clips.size() - 1));
	}
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 18.0f);
	ImGui::Button("Drop a .spriteanim here to add a clip", ImVec2(280.0f, 0));
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET_FILE"))
		{
			const std::string dropped = ProjectRelative((const char*)p->Data);
			if (fs::path(dropped).extension() == ".spriteanim")
				m_Clips.push_back(dropped);
		}
		ImGui::EndDragDropTarget();
	}
	UnityGUI::HelpBox("The clip marked * plays first (click its circle button). C#: GetComponent<SpriteAnimator>().Play(\"Run\")", false);
	UnityGUI::Toggle("Play On Awake", &PlayOnAwake);
	UnityGUI::Float("Speed", &Speed);
	if (Application::IsPlaying())
	{
		char buf[96];
		snprintf(buf, sizeof(buf), "%s  frame %d%s", CurrentClip().c_str(), m_Frame, m_Playing ? "" : "  (stopped)");
		UnityGUI::ValueLabel("Now", buf);
	}
	if (m_pGameObject && m_pGameObject->GetComponent<SpriteRenderer>() == nullptr)
		UnityGUI::HelpBox("Needs a Sprite Renderer on the same GameObject.");
}

GENERATE_COMPONENT_FUNC_TOJSON(SpriteAnimator)
{
	json j;
	SERIALIZE_TYPE(j, SpriteAnimator);
	j["enabled"] = m_Enabled;
	j["clips"] = m_Clips;
	j["defaultClip"] = m_Default;
	j["playOnAwake"] = PlayOnAwake;
	j["speed"] = Speed;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(SpriteAnimator)
{
	m_Enabled = j.value("enabled", true);
	m_Clips.clear();
	if (j.contains("clips") && j["clips"].is_array())
		for (const json& c : j["clips"])
			if (c.is_string())
				m_Clips.push_back(c.get<std::string>());
	m_Default = j.value("defaultClip", 0);
	PlayOnAwake = j.value("playOnAwake", true);
	Speed = j.value("speed", 1.0f);
}
