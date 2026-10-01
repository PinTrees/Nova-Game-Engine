#include "pch.h"
#include "SceneCulling.h"
#include "GameViewEditorWindow.h"
#include "MathHelper.h"
#include "App.h"
#include "UnityGUI.h"
#include "RenderStats.h"
#include "Animator.h"
#include "AnimationPlayer.h"
#include "AudioManager.h"
#include "UISystem.h"
#include <fstream>

namespace
{
	// ---------------------------------------------------------------- 해상도 목록 (Unity 의 GameViewSize)
	enum class SizeType { Free, Aspect, Fixed };
	struct GameViewSize
	{
		std::string Label;   // 비어 있으면 숫자만 표시
		SizeType Type = SizeType::Free;
		int W = 0, H = 0;
	};

	const std::vector<GameViewSize>& Builtins()
	{
		static const std::vector<GameViewSize> k = {
			{ "Free Aspect", SizeType::Free, 0, 0 },
			{ "", SizeType::Aspect, 5, 4 },
			{ "", SizeType::Aspect, 4, 3 },
			{ "", SizeType::Aspect, 3, 2 },
			{ "", SizeType::Aspect, 16, 10 },
			{ "", SizeType::Aspect, 16, 9 },
			{ "Full HD", SizeType::Fixed, 1920, 1080 },
			{ "QHD", SizeType::Fixed, 2560, 1440 },
			{ "4K UHD", SizeType::Fixed, 3840, 2160 },
		};
		return k;
	}

	std::string SizeText(const GameViewSize& s)
	{
		if (s.Type == SizeType::Free)
			return s.Label.empty() ? "Free Aspect" : s.Label;
		const std::string nums = s.Type == SizeType::Aspect
			? std::to_string(s.W) + ":" + std::to_string(s.H)
			: std::to_string(s.W) + "x" + std::to_string(s.H);
		return s.Label.empty() ? nums : s.Label + " (" + nums + ")";
	}

	// ---------------------------------------------------------------- 설정 (프로젝트별)
	struct Settings
	{
		std::vector<GameViewSize> Custom = { { "", SizeType::Fixed, 1080, 1920 } };
		int Selected = 0;          // Builtins + Custom 의 순번
		int Display = 0;
		int PlayMode = 0;          // 0 Play Focused, 1 Play Maximized, 2 Play Unfocused
		int ViewMode = 0;          // 0 Game (Simulator 는 미지원)
		float Scale = -1.0f;       // -1 = 뷰에 맞춤
		bool Stats = false;
		bool Gizmos = false;
		bool Mute = false;
		bool Shortcuts = true;
		bool Loaded = false;
	} g;

	std::wstring SettingsFile() { return PathManager::GetI()->GetMovePathW(L"UserSettings\\GameView.json"); }

	void LoadSettings()
	{
		g.Loaded = true;
		std::ifstream in(SettingsFile());
		if (!in)
			return;
		json j = json::parse(in, nullptr, false);
		if (j.is_discarded() || !j.is_object())
			return;
		if (j.contains("custom") && j["custom"].is_array())
		{
			g.Custom.clear();
			for (const json& c : j["custom"])
				g.Custom.push_back({ c.value("label", std::string()), (SizeType)c.value("type", 2), c.value("w", 1920), c.value("h", 1080) });
		}
		g.Selected = j.value("selected", 0);
		g.Display = std::clamp(j.value("display", 0), 0, 7);
		g.PlayMode = std::clamp(j.value("playMode", 0), 0, 2);
		g.Scale = j.value("scale", -1.0f);
		g.Stats = j.value("stats", false);
		g.Gizmos = j.value("gizmos", false);
		g.Mute = j.value("mute", false);
		g.Shortcuts = j.value("shortcuts", true);
	}

	void SaveSettings()
	{
		json j;
		json custom = json::array();
		for (const GameViewSize& c : g.Custom)
			custom.push_back({ { "label", c.Label }, { "type", (int)c.Type }, { "w", c.W }, { "h", c.H } });
		j["custom"] = custom;
		j["selected"] = g.Selected;
		j["display"] = g.Display;
		j["playMode"] = g.PlayMode;
		j["scale"] = g.Scale;
		j["stats"] = g.Stats;
		j["gizmos"] = g.Gizmos;
		j["mute"] = g.Mute;
		j["shortcuts"] = g.Shortcuts;
		std::error_code ec;
		std::filesystem::create_directories(std::filesystem::path(SettingsFile()).parent_path(), ec);
		std::ofstream os(SettingsFile(), std::ios::trunc);
		if (os)
			os << j.dump(4);
	}

	int SizeCount() { return (int)(Builtins().size() + g.Custom.size()); }
	const GameViewSize& SizeAt(int i)
	{
		const int nb = (int)Builtins().size();
		return i < nb ? Builtins()[i] : g.Custom[i - nb];
	}
	const GameViewSize& CurrentSize()
	{
		if (g.Selected < 0 || g.Selected >= SizeCount())
			g.Selected = 0;
		return SizeAt(g.Selected);
	}

	// ---------------------------------------------------------------- 상태
	GameViewEditorWindow* s_Instance = nullptr;
	bool s_Maximized = false;
	bool s_FocusMaximized = false;
	ImVec2 s_DockMin(0, 0), s_DockMax(0, 0);
	float s_SmoothDt = 1.0f / 60.0f;

	// C# 입력용: 마지막으로 그린 게임 이미지 위치와 크기, 포커스, 휠
	ImVec2 s_ImgMin(0, 0), s_ImgMax(1, 1);
	int s_GameW = 1, s_GameH = 1;
	bool s_InputFocus = false;
	float s_Scroll = 0.0f;

	// ---------------------------------------------------------------- 툴바 그리기 (Unity 6 다크 테마)
	const ImU32 kBar = IM_COL32(40, 40, 40, 255);
	const ImU32 kField = IM_COL32(56, 56, 56, 255);
	const ImU32 kFieldHover = IM_COL32(68, 68, 68, 255);
	const ImU32 kOn = IM_COL32(70, 96, 124, 255);
	const ImU32 kText = IM_COL32(200, 200, 200, 255);
	const ImU32 kTextDim = IM_COL32(130, 130, 130, 255);
	const ImU32 kViewBg = IM_COL32(30, 30, 30, 255);
	constexpr float kBarH = 21.0f;

	float TextY(float y, float h) { return floorf(y + (h - ImGui::GetFontSize()) * 0.5f + 0.5f); }

	// 드롭다운 필드: 글자 + ▾. 누르면 true
	bool DropField(ImDrawList* dl, const char* id, const std::string& text, float x, float y, float w)
	{
		ImGui::SetCursorScreenPos(ImVec2(x, y));
		const bool clicked = ImGui::InvisibleButton(id, ImVec2(w, kBarH));
		const bool hovered = ImGui::IsItemHovered();
		dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + kBarH), hovered ? kFieldHover : kField);
		ImVec4 clip(x, y, x + w - 16.0f, y + kBarH);
		dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(x + 5.0f, TextY(y, kBarH)), kText, text.c_str(), nullptr, 0.0f, &clip);
		UnityGUI::DrawIcon(dl, "dropdown", ImVec2(x + w - 14.0f, y + (kBarH - 12.0f) * 0.5f), 12.0f);
		return clicked;
	}

	// 아이콘/글자 토글 버튼
	bool ToggleButton(ImDrawList* dl, const char* id, const char* icon, const char* text, float x, float y, float w, bool on, const char* tip, bool enabled = true)
	{
		ImGui::SetCursorScreenPos(ImVec2(x, y));
		const bool clicked = ImGui::InvisibleButton(id, ImVec2(w, kBarH)) && enabled;
		const bool hovered = ImGui::IsItemHovered();
		if (tip && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_DelayNormal))
			ImGui::SetTooltip("%s", tip);
		dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + kBarH), on ? kOn : (hovered && enabled ? kFieldHover : kBar));
		const ImU32 tint = enabled ? IM_COL32_WHITE : IM_COL32(120, 120, 120, 255);
		if (icon)
			UnityGUI::DrawIcon(dl, icon, ImVec2(floorf(x + (w - 16.0f) * 0.5f), y + (kBarH - 16.0f) * 0.5f), 16.0f, tint);
		if (text)
			dl->AddText(ImVec2(floorf(x + (w - ImGui::CalcTextSize(text).x) * 0.5f), TextY(y, kBarH)), enabled ? kText : kTextDim, text);
		return clicked;
	}

	void BeginDarkPopupStyle()
	{
		ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 6));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 4));
	}
	void EndDarkPopupStyle()
	{
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor();
	}
}

// ================================================================== 창
GameViewEditorWindow::GameViewEditorWindow()
	: EditorWindow("Game", ICON_FA_GAMEPAD)
{
	s_Instance = this;
}

GameViewEditorWindow::~GameViewEditorWindow()
{
	if (s_Instance == this)
		s_Instance = nullptr;
}

void GameViewEditorWindow::OnPlayModeChanged(bool playing)
{
	if (!g.Loaded)
		LoadSettings();
	if (!playing)
	{
		s_Maximized = false;
		return;
	}
	if (g.PlayMode == 1)
	{
		s_Maximized = true;
		s_FocusMaximized = true;
	}
	else if (g.PlayMode == 0 && s_Instance)
		ImGui::SetWindowFocus(s_Instance->GetImGuiName().c_str());   // Play Focused: Game 탭을 앞으로
}

bool GameViewEditorWindow::HasInputFocus() { return s_InputFocus || s_Maximized; }

bool GameViewEditorWindow::MouseToGame(float& x, float& y)
{
	const ImVec2 m = ImGui::GetIO().MousePos;
	const float w = (std::max)(1.0f, s_ImgMax.x - s_ImgMin.x), h = (std::max)(1.0f, s_ImgMax.y - s_ImgMin.y);
	x = (m.x - s_ImgMin.x) / w * (float)s_GameW;
	y = (1.0f - (m.y - s_ImgMin.y) / h) * (float)s_GameH;   // Unity: 왼쪽 아래가 (0,0)
	return m.x >= s_ImgMin.x && m.x <= s_ImgMax.x && m.y >= s_ImgMin.y && m.y <= s_ImgMax.y;
}

ImVec2 GameViewEditorWindow::GameToScreen(float x, float y)
{
	const float w = (float)(std::max)(1, s_GameW), h = (float)(std::max)(1, s_GameH);
	return ImVec2(s_ImgMin.x + x / w * (s_ImgMax.x - s_ImgMin.x), s_ImgMin.y + (1.0f - y / h) * (s_ImgMax.y - s_ImgMin.y));
}

void GameViewEditorWindow::GameSize(int& w, int& h) { w = s_GameW; h = s_GameH; }

void GameViewEditorWindow::SetPlayerView(int width, int height, bool focused)
{
	s_ImgMin = ImVec2(0.0f, 0.0f);
	s_ImgMax = ImVec2((float)width, (float)height);
	s_GameW = width;
	s_GameH = height;
	s_InputFocus = focused;
	s_Scroll = focused ? ImGui::GetIO().MouseWheel : 0.0f;
}
float GameViewEditorWindow::ScrollDelta() { return s_Scroll; }

void GameViewEditorWindow::SetDockRect(const ImVec2& min, const ImVec2& max)
{
	s_DockMin = min;
	s_DockMax = max;
}

void GameViewEditorWindow::DrawMaximized()
{
	if (!s_Maximized || s_Instance == nullptr || s_DockMax.x <= s_DockMin.x)
		return;
	ImGui::SetNextWindowPos(s_DockMin);
	ImGui::SetNextWindowSize(ImVec2(s_DockMax.x - s_DockMin.x, s_DockMax.y - s_DockMin.y));
	if (s_FocusMaximized)
	{
		ImGui::SetNextWindowFocus();
		s_FocusMaximized = false;
	}
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar;
	if (ImGui::Begin("Game##maximized", nullptr, flags))
		s_Instance->DrawContent();
	ImGui::End();
	ImGui::PopStyleVar(3);
}

void GameViewEditorWindow::OnRender()
{
	if (!g.Loaded)
		LoadSettings();
	if (s_Maximized)
	{
		// Play Maximized 중에는 도킹 영역 전체 창(DrawMaximized)이 그린다
		ImGui::TextDisabled("  Maximized (Play Maximized)");
		return;
	}
	DrawContent();
}

void GameViewEditorWindow::DrawContent()
{
	// Game 창(또는 Play Maximized 창)이 포커스일 때만 게임이 키보드/마우스를 받는다 (Unity 와 같음)
	s_InputFocus = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	const ImVec2 avail = ImGui::GetContentRegionAvail();
	if (avail.x < 40.0f || avail.y < kBarH + 10.0f)
		return;
	DrawToolbar(avail.x);
	const ImVec2 viewPos(pos.x, pos.y + kBarH);
	const ImVec2 viewSize(avail.x, avail.y - kBarH);
	DrawView(viewPos, viewSize);
	ImGui::SetCursorScreenPos(pos);
	ImGui::Dummy(avail);
}

// ------------------------------------------------------------------ 툴바
void GameViewEditorWindow::DrawToolbar(float width)
{
	ImDrawList* dl = ImGui::GetWindowDrawList();
	const ImVec2 p = ImGui::GetCursorScreenPos();
	dl->AddRectFilled(p, ImVec2(p.x + width, p.y + kBarH), kBar);
	float x = p.x;
	const float y = p.y;

	// Game ▾ (Game / Simulator)
	if (DropField(dl, "##gvMode", "Game", x, y, 90.0f))
		ImGui::OpenPopup("##gvModeMenu");
	const float modeX = x;
	x += 91.0f;

	// Display 1 ▾
	if (DropField(dl, "##gvDisplay", "Display " + std::to_string(g.Display + 1), x, y, 80.0f))
		ImGui::OpenPopup("##gvDisplayMenu");
	const float displayX = x;
	x += 81.0f;

	// 해상도 ▾
	if (DropField(dl, "##gvSize", SizeText(CurrentSize()), x, y, 160.0f))
		ImGui::OpenPopup("##gvSizeMenu");
	const float sizeX = x;
	x += 170.0f;

	// Scale ━○━ 0.46x (최소값 = 뷰에 맞춘 배율)
	{
		dl->AddText(ImVec2(x, TextY(y, kBarH)), kText, "Scale");
		x += ImGui::CalcTextSize("Scale").x + 8.0f;
		const float minS = (std::min)(1.0f, m_LastFitScale);
		const float maxS = 5.0f;
		float cur = g.Scale < 0.0f ? m_LastFitScale : std::clamp(g.Scale, minS, maxS);
		const float sw = std::clamp(width * 0.1f, 80.0f, 150.0f);
		const float cy = y + kBarH * 0.5f;
		ImGui::SetCursorScreenPos(ImVec2(x - 6.0f, y));
		ImGui::InvisibleButton("##gvScale", ImVec2(sw + 12.0f, kBarH));
		if (ImGui::IsItemActive())
		{
			const float t = std::clamp((ImGui::GetIO().MousePos.x - x) / sw, 0.0f, 1.0f);
			cur = minS + (maxS - minS) * t;
			g.Scale = (fabsf(cur - m_LastFitScale) < 0.01f) ? -1.0f : cur;
			m_Pan = ImVec2(0, 0);
		}
		if (ImGui::IsItemDeactivated())
			SaveSettings();
		if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		{
			g.Scale = -1.0f;   // 더블클릭 = 맞춤으로
			SaveSettings();
		}
		const float t = maxS > minS ? (cur - minS) / (maxS - minS) : 0.0f;
		dl->AddLine(ImVec2(x, cy), ImVec2(x + sw, cy), IM_COL32(100, 100, 100, 255), 2.0f);
		dl->AddCircleFilled(ImVec2(x + sw * t, cy), 5.5f, ImGui::IsItemActive() ? IM_COL32(230, 230, 230, 255) : IM_COL32(190, 190, 190, 255));
		x += sw + 10.0f;
		char buf[32];
		snprintf(buf, sizeof(buf), "%.2gx", cur);
		if (cur < 1.0f) snprintf(buf, sizeof(buf), "%.2fx", cur);
		dl->AddText(ImVec2(x, TextY(y, kBarH)), kText, buf);
		x += ImGui::CalcTextSize("0.00x").x + 10.0f;
	}

	// 오른쪽: 🔇 ⌨ Stats Gizmos▾
	const float gizW = 58.0f, statsW = 40.0f, iconW = 24.0f;
	float rx = p.x + width - gizW;
	const float gizX = rx;
	{
		ImGui::SetCursorScreenPos(ImVec2(rx, y));
		const bool hit = ImGui::InvisibleButton("##gvGizmos", ImVec2(gizW - 14.0f, kBarH));
		const bool hov = ImGui::IsItemHovered();
		ImGui::SetCursorScreenPos(ImVec2(rx + gizW - 14.0f, y));
		const bool arrow = ImGui::InvisibleButton("##gvGizmosArrow", ImVec2(14.0f, kBarH));
		dl->AddRectFilled(ImVec2(rx, y), ImVec2(rx + gizW - 14.0f, y + kBarH), g.Gizmos ? kOn : (hov ? kFieldHover : kBar));
		dl->AddText(ImVec2(rx + 4.0f, TextY(y, kBarH)), kText, "Gizmos");
		UnityGUI::DrawIcon(dl, "dropdown", ImVec2(rx + gizW - 13.0f, y + (kBarH - 12.0f) * 0.5f), 12.0f);
		if (hit) { g.Gizmos = !g.Gizmos; SaveSettings(); }
		if (arrow) ImGui::OpenPopup("##gvGizmosMenu");
	}
	rx -= statsW + 1.0f;
	if (ToggleButton(dl, "##gvStats", nullptr, "Stats", rx, y, statsW, g.Stats, "Rendering statistics"))
	{
		g.Stats = !g.Stats;
		SaveSettings();
	}
	rx -= iconW + 1.0f;
	if (ToggleButton(dl, "##gvKeys", "keyboard", nullptr, rx, y, iconW, g.Shortcuts, "Game view shortcuts"))
	{
		g.Shortcuts = !g.Shortcuts;
		SaveSettings();
	}
	rx -= iconW + 1.0f;
	if (ToggleButton(dl, "##gvMute", g.Mute ? "audio_off" : "audio_on", nullptr, rx, y, iconW, g.Mute, "Mute Audio"))
	{
		g.Mute = !g.Mute;
		SaveSettings();
	}
	if (AudioManager::IsMuted() != g.Mute)
		AudioManager::SetMuted(g.Mute);

	// 가운데: Play Focused ▾ + 🐞 (남은 공간의 가운데)
	static const char* kPlayModes[] = { "Play Focused", "Play Maximized", "Play Unfocused" };
	const float playW = 120.0f;
	const float playX = floorf((std::max)(x, x + (rx - x - playW - 26.0f) * 0.5f));
	if (playX + playW + 26.0f < rx)
	{
		if (DropField(dl, "##gvPlay", kPlayModes[g.PlayMode], playX, y, playW))
			ImGui::OpenPopup("##gvPlayMenu");
		ToggleButton(dl, "##gvBug", "bug", nullptr, playX + playW + 2.0f, y, 22.0f, false, "Frame Debugger (not available yet)", false);
	}

	// ---------------- 팝업들
	BeginDarkPopupStyle();
	ImGui::SetNextWindowPos(ImVec2(modeX, y + kBarH));
	if (ImGui::BeginPopup("##gvModeMenu"))
	{
		ImGui::MenuItem("Game", nullptr, true);
		ImGui::MenuItem("Simulator", nullptr, false, false);
		ImGui::EndPopup();
	}
	ImGui::SetNextWindowPos(ImVec2(displayX, y + kBarH));
	if (ImGui::BeginPopup("##gvDisplayMenu"))
	{
		for (int i = 0; i < 8; ++i)
			if (ImGui::MenuItem(("Display " + std::to_string(i + 1)).c_str(), nullptr, g.Display == i))
			{
				g.Display = i;
				SaveSettings();
			}
		ImGui::EndPopup();
	}
	ImGui::SetNextWindowPos(ImVec2(sizeX, y + kBarH));
	if (ImGui::BeginPopup("##gvSizeMenu"))
	{
		DrawSizeMenu();
		ImGui::EndPopup();
	}
	ImGui::SetNextWindowPos(ImVec2(playX, y + kBarH));
	if (ImGui::BeginPopup("##gvPlayMenu"))
	{
		for (int i = 0; i < 3; ++i)
			if (ImGui::MenuItem(kPlayModes[i], nullptr, g.PlayMode == i))
			{
				g.PlayMode = i;
				SaveSettings();
			}
		ImGui::EndPopup();
	}
	ImGui::SetNextWindowPos(ImVec2(gizX - 120.0f, y + kBarH));
	if (ImGui::BeginPopup("##gvGizmosMenu"))
	{
		if (ImGui::MenuItem("Gizmos", nullptr, g.Gizmos)) { g.Gizmos = !g.Gizmos; SaveSettings(); }
		ImGui::Separator();
		ImGui::TextDisabled("Game view gizmos are not drawn yet");
		ImGui::EndPopup();
	}
	EndDarkPopupStyle();

	ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + kBarH));
}

void GameViewEditorWindow::DrawSizeMenu()
{
	static char s_Label[64] = {};
	static int s_Type = 1;   // 0 Aspect Ratio, 1 Fixed Resolution
	static int s_W = 1080, s_H = 1920;

	const int nb = (int)Builtins().size();
	int removeIndex = -1;
	for (int i = 0; i < SizeCount(); ++i)
	{
		if (i == nb)
			ImGui::Separator();
		ImGui::PushID(i);
		if (ImGui::MenuItem(SizeText(SizeAt(i)).c_str(), nullptr, g.Selected == i))
		{
			g.Selected = i;
			g.Scale = -1.0f;   // 해상도를 바꾸면 맞춤 배율로
			m_Pan = ImVec2(0, 0);
			SaveSettings();
			EditorLog::Write("View", "game view size = %s", SizeText(SizeAt(i)).c_str());
		}
		if (i >= nb && ImGui::BeginPopupContextItem("##sizeCtx"))
		{
			if (ImGui::MenuItem("Delete"))
				removeIndex = i;
			ImGui::EndPopup();
		}
		ImGui::PopID();
	}
	if (removeIndex >= 0)
	{
		g.Custom.erase(g.Custom.begin() + (removeIndex - nb));
		if (g.Selected == removeIndex) g.Selected = 0;
		else if (g.Selected > removeIndex) --g.Selected;
		SaveSettings();
	}
	ImGui::Separator();
	if (ImGui::Button("+", ImVec2(24, 0)))
		ImGui::OpenPopup("##gvAddSize");
	ImGui::SameLine();
	ImGui::TextDisabled("Add custom size (right-click a custom size to delete)");

	if (ImGui::BeginPopup("##gvAddSize"))
	{
		ImGui::TextUnformatted("Add");
		ImGui::Separator();
		ImGui::SetNextItemWidth(180);
		ImGui::InputText("Label", s_Label, sizeof(s_Label));
		static const char* kTypes[] = { "Aspect Ratio", "Fixed Resolution" };
		ImGui::SetNextItemWidth(180);
		ImGui::Combo("Type", &s_Type, kTypes, 2);
		ImGui::SetNextItemWidth(85);
		ImGui::InputInt("##w", &s_W, 0);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(85);
		ImGui::InputInt("Width & Height", &s_H, 0);
		s_W = std::clamp(s_W, 1, 16384);
		s_H = std::clamp(s_H, 1, 16384);
		if (ImGui::Button("OK", ImVec2(80, 0)))
		{
			g.Custom.push_back({ s_Label, s_Type == 0 ? SizeType::Aspect : SizeType::Fixed, s_W, s_H });
			g.Selected = SizeCount() - 1;
			g.Scale = -1.0f;
			SaveSettings();
			s_Label[0] = 0;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(80, 0)))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}
}

// ------------------------------------------------------------------ 뷰
void GameViewEditorWindow::EnsureTarget(UINT width, UINT height, Camera* camera)
{
	width = (std::max)(1u, width);
	height = (std::max)(1u, height);
	const bool resized = width != m_Width || height != m_Height || m_RTV == nullptr;
	if (resized)
	{
		m_Width = width;
		m_Height = height;
		m_Texture.Reset();
		m_RTV.Reset();
		m_SRV.Reset();
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = width;
		desc.Height = height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		auto device = Application::GetI()->GetDevice();
		if (SUCCEEDED(device->CreateTexture2D(&desc, nullptr, m_Texture.GetAddressOf())))
		{
			device->CreateRenderTargetView(m_Texture.Get(), nullptr, m_RTV.GetAddressOf());
			device->CreateShaderResourceView(m_Texture.Get(), nullptr, m_SRV.GetAddressOf());
		}
		RenderManager::GetI()->SetViewport(width, height);
		EditorLog::Write("View", "Game view render target %u x %u", width, height);
	}
	if (camera && (resized || camera != m_LastCamera))
		PostProcessingManager::GetI()->SetSSAO(width, height, camera);
	m_LastCamera = camera;
}

void GameViewEditorWindow::RenderScene(Camera* camera)
{
	auto context = Application::GetI()->GetDeviceContext();
	GfxRenderTargetView* oldRTV = nullptr;
	context->OMGetRenderTargets(1, &oldRTV, nullptr);
	GfxRenderTargetView* rtv = m_RTV.Get();
	context->OMSetRenderTargets(1, &rtv, nullptr);

	camera->SetAspect((float)m_Width / (float)m_Height);
	camera->LateUpdate();
	RenderManager::GetI()->CameraViewProjectionMatrix = camera->View() * camera->Proj();

	LARGE_INTEGER freq, t0, t1;
	::QueryPerformanceFrequency(&freq);
	::QueryPerformanceCounter(&t0);
	RenderStats::Begin();
	Application::GetI()->GetApp()->OnSceneRender(rtv, camera);
	UISystem::RenderGameView(rtv, m_Width, m_Height, g.Display);   // Screen Space - Overlay 캔버스 (맨 위)
	::QueryPerformanceCounter(&t1);
	RenderStats::Current.RenderMs = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart;
	RenderStats::End();

	context->OMSetRenderTargets(1, &oldRTV, nullptr);
	if (oldRTV)
		oldRTV->Release();
}

void GameViewEditorWindow::DrawView(ImVec2 pos, ImVec2 size)
{
	ImDrawList* dl = ImGui::GetWindowDrawList();
	const ImVec2 vmax(pos.x + size.x, pos.y + size.y);
	dl->AddRectFilled(pos, vmax, kViewBg);
	ImGui::SetCursorScreenPos(pos);
	ImGui::InvisibleButton("##gvView", ImVec2((std::max)(1.0f, size.x), (std::max)(1.0f, size.y)), ImGuiButtonFlags_MouseButtonMiddle);
	const bool hovered = ImGui::IsItemHovered();

	// 렌더 타깃 크기와 맞춤 배율
	const GameViewSize& gs = CurrentSize();
	float rtW = size.x, rtH = size.y, fit = 1.0f;
	if (gs.Type == SizeType::Aspect && gs.W > 0 && gs.H > 0)
	{
		const float a = (float)gs.W / (float)gs.H;
		if (size.x / size.y > a) { rtH = size.y; rtW = rtH * a; }
		else { rtW = size.x; rtH = rtW / a; }
	}
	else if (gs.Type == SizeType::Fixed)
	{
		rtW = (float)gs.W;
		rtH = (float)gs.H;
		fit = (std::min)(1.0f, (std::min)(size.x / rtW, size.y / rtH));
	}
	m_LastFitScale = fit;
	const float minS = (std::min)(1.0f, fit);
	float scale = g.Scale < 0.0f ? fit : std::clamp(g.Scale, minS, 5.0f);

	// 휠 확대 (Play 중에는 게임 입력과 겹치지 않게 끔), 가운데 버튼 드래그 = 이동
	if (hovered && !Application::IsPlaying() && ImGui::GetIO().MouseWheel != 0.0f)
	{
		scale = std::clamp(scale * powf(1.1f, ImGui::GetIO().MouseWheel), minS, 5.0f);
		g.Scale = fabsf(scale - fit) < 0.01f ? -1.0f : scale;
		SaveSettings();
	}
	const ImVec2 disp(floorf(rtW * scale), floorf(rtH * scale));
	if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f))
	{
		m_Pan.x += ImGui::GetIO().MouseDelta.x;
		m_Pan.y += ImGui::GetIO().MouseDelta.y;
	}
	const float maxPanX = (std::max)(0.0f, (disp.x - size.x) * 0.5f);
	const float maxPanY = (std::max)(0.0f, (disp.y - size.y) * 0.5f);
	m_Pan.x = std::clamp(m_Pan.x, -maxPanX, maxPanX);
	m_Pan.y = std::clamp(m_Pan.y, -maxPanY, maxPanY);
	const ImVec2 imgMin(floorf(pos.x + (size.x - disp.x) * 0.5f + m_Pan.x), floorf(pos.y + (size.y - disp.y) * 0.5f + m_Pan.y));
	const ImVec2 imgMax(imgMin.x + disp.x, imgMin.y + disp.y);
	s_ImgMin = imgMin;
	s_ImgMax = imgMax;
	s_GameW = (int)rtW;
	s_GameH = (int)rtH;
	s_Scroll = hovered ? ImGui::GetIO().MouseWheel : 0.0f;

	// 이 디스플레이를 그리는 카메라
	DisplayManager::GetI()->SetActiveDisplay(g.Display);
	shared_ptr<Camera> camera = DisplayManager::GetI()->GetCameraForDisplay(g.Display);
	dl->PushClipRect(pos, vmax, true);
	if (camera && rtW >= 1.0f && rtH >= 1.0f)
	{
		EnsureTarget((UINT)rtW, (UINT)rtH, camera.get());
		if (m_RTV)
		{
			RenderScene(camera.get());
			dl->AddImage((ImTextureID)m_SRV.Get(), imgMin, imgMax);
		}
	}
	else
	{
		// Unity: "Display 1  No cameras rendering"
		dl->AddRectFilled(imgMin, imgMax, IM_COL32(0, 0, 0, 255));
		const std::string line1 = "Display " + std::to_string(g.Display + 1);
		const char* line2 = "No cameras rendering";
		const ImVec2 c((imgMin.x + imgMax.x) * 0.5f, (imgMin.y + imgMax.y) * 0.5f);
		dl->AddText(ImVec2(c.x - ImGui::CalcTextSize(line1.c_str()).x * 0.5f, c.y - 18.0f), kText, line1.c_str());
		dl->AddText(ImVec2(c.x - ImGui::CalcTextSize(line2).x * 0.5f, c.y + 2.0f), kText, line2);
	}
	dl->PopClipRect();

	if (g.Stats)
		DrawStats(pos, vmax);
}

void GameViewEditorWindow::DrawStats(ImVec2 viewMin, ImVec2 viewMax)
{
	s_SmoothDt = s_SmoothDt * 0.95f + ImGui::GetIO().DeltaTime * 0.05f;
	const RenderStats::Frame& f = RenderStats::Last;

	int animPlaying = 0, animatorPlaying = 0;
	if (Scene* scene = SceneManager::GetI()->GetCurrentScene())
		for (GameObject* go : scene->GetAllGameObjects())
		{
			if (go == nullptr)
				continue;
			if (auto* a = go->GetComponent<AnimationPlayer>(); a && a->IsEnabled() && a->IsPlaying())
				++animPlaying;
			if (auto* a = go->GetComponent<Animator>(); a && a->IsEnabled() && a->GetController() && Application::IsPlaying())
				++animatorPlaying;
		}

	auto k = [](double v) {
		char b[32];
		if (v >= 1000000.0) snprintf(b, sizeof(b), "%.1fM", v / 1000000.0);
		else if (v >= 1000.0) snprintf(b, sizeof(b), "%.1fk", v / 1000.0);
		else snprintf(b, sizeof(b), "%.0f", v);
		return std::string(b);
	};
	const double screenMB = (double)m_Width * m_Height * 8.0 / (1024.0 * 1024.0);   // 색 4 + 깊이 4 바이트

	const float w = 300.0f;
	const float lh = ImGui::GetFontSize() + 1.0f;
	const float h = lh * 15.0f + 20.0f;
	const ImVec2 a(viewMax.x - w - 10.0f, viewMin.y + 8.0f);
	const ImVec2 b(a.x + w, a.y + h);
	ImDrawList* dl = ImGui::GetWindowDrawList();
	dl->AddRectFilled(a, b, IM_COL32(40, 40, 40, 215), 2.0f);
	const ImU32 cT = IM_COL32(225, 225, 225, 255), cD = IM_COL32(150, 150, 150, 255);
	float y = a.y + 6.0f;
	char buf[160];
	auto line = [&](float x, ImU32 col, const char* text) { dl->AddText(ImVec2(a.x + x, y), col, text); };

	line((w - ImGui::CalcTextSize("Statistics").x) * 0.5f, cT, "Statistics"); y += lh + 2.0f;
	ImFont* bold = UnityGUI::BoldFont();
	const AudioManager::Stats as = AudioManager::GetStats();
	dl->AddText(bold, bold->FontSize, ImVec2(a.x + 6.0f, y), cT, !as.Available ? "Audio (not started):" : (g.Mute ? "Audio (muted):" : "Audio:")); y += lh;
	snprintf(buf, sizeof(buf), "Level: %.1f dB%s", as.LevelDb, g.Mute ? " (MUTED)" : ""); line(14.0f, cD, buf);
	snprintf(buf, sizeof(buf), "DSP load: %.1f%%", as.DspLoadPercent); line(170.0f, cD, buf); y += lh;
	snprintf(buf, sizeof(buf), "Clipping: %.1f%%", as.ClippingPercent); line(14.0f, cD, buf);
	snprintf(buf, sizeof(buf), "Voices: %d", as.ActiveVoices); line(170.0f, cD, buf); y += lh + 4.0f;

	dl->AddText(bold, bold->FontSize, ImVec2(a.x + 6.0f, y), cT, "Graphics:");
	if (Application::IsPlaying())
		snprintf(buf, sizeof(buf), "%.1f FPS (%.1fms)", 1.0f / (std::max)(0.0001f, s_SmoothDt), s_SmoothDt * 1000.0f);
	else
		snprintf(buf, sizeof(buf), "- FPS (Playmode Off)");
	line(w - ImGui::CalcTextSize(buf).x - 8.0f, cT, buf); y += lh;
	snprintf(buf, sizeof(buf), "CPU: main %.1fms  render %.1fms", s_SmoothDt * 1000.0f, f.RenderMs); line(14.0f, cT, buf); y += lh;
	snprintf(buf, sizeof(buf), "Batches: %d", f.DrawCalls); line(14.0f, cT, buf);
	snprintf(buf, sizeof(buf), "Saved by batching: %d", f.SavedByBatching); line(150.0f, cT, buf); y += lh;
	snprintf(buf, sizeof(buf), "Tris: %s   Verts: %s", k((double)f.Triangles).c_str(), k((double)f.Vertices).c_str()); line(14.0f, cT, buf); y += lh;
	snprintf(buf, sizeof(buf), "Screen: %ux%u - %.1f MB", m_Width, m_Height, screenMB); line(14.0f, cT, buf); y += lh;
	snprintf(buf, sizeof(buf), "SetPass calls: %d", f.DrawCalls); line(14.0f, cT, buf);
	snprintf(buf, sizeof(buf), "Shadow casters: %d", f.ShadowCasters); line(150.0f, cT, buf); y += lh;
	snprintf(buf, sizeof(buf), "Visible skinned meshes: %d", f.VisibleSkinnedMeshes); line(14.0f, cT, buf); y += lh;
	{
		const SceneCulling::Stats& cs = SceneCulling::LastStats(false);
		snprintf(buf, sizeof(buf), "Frustum culling: %d / %d visible (octree %d nodes)", cs.Visible, cs.Objects, cs.Nodes);
		line(14.0f, cT, buf); y += lh;
	}
	snprintf(buf, sizeof(buf), "Animation components playing: %d", animPlaying); line(14.0f, cT, buf); y += lh;
	snprintf(buf, sizeof(buf), "Animator components playing: %d", animatorPlaying); line(14.0f, cT, buf); y += lh;
}
