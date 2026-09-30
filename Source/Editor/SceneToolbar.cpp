#include "pch.h"
#include "SceneToolbar.h"
#include "EditorCamera.h"
#include "EditorTheme.h"
#include "UnityGUI.h"
#include "GameObjectMenu.h"

namespace
{
	using namespace SceneToolbar;

	// (개발/검증용) NOVA_TOOL=0..5 로 시작 도구 지정 (View, Move, Rotate, Scale, Rect, Transform)
	Tool InitialTool()
	{
		char buf[8] = {};
		if (::GetEnvironmentVariableA("NOVA_TOOL", buf, sizeof(buf)) > 0 && buf[0] >= '0' && buf[0] <= '5')
			return (Tool)(buf[0] - '0');
		return Tool::Move;
	}

	// ---- 툴바 상태 (Scene 뷰가 하나이므로 전역) ----
	struct State
	{
		Tool tool = InitialTool();
		PivotMode pivot = PivotMode::Pivot;
		HandleSpace space = HandleSpace::Local;

		float snapIncrement = 1.0f;
		bool gridSnap = false;
		bool autoSnap = false;

		bool shadedWire = false;
		bool sceneLighting = true;
		bool view2D = false;
		bool audio = false;
		bool effects = true;
		bool visibility = true;
		bool grid = true;
		bool gizmos = true;

		// 드롭다운 안의 세부 옵션 (현재는 저장만)
		bool fxSkybox = true, fxFog = true, fxFlares = true, fxAlwaysRefresh = false, fxPost = true, fxParticles = true;
		bool giz3DIcons = true, gizSelOutline = true, gizSelWire = true;
	} s;

	// ---- 색 (Unity 다크 테마) ----
	const ImU32 kBarBg      = IM_COL32(44, 44, 44, 255);
	const ImU32 kBarLine    = IM_COL32(25, 25, 25, 255);
	const ImU32 kBtn        = IM_COL32(66, 66, 66, 255);
	const ImU32 kBtnHover   = IM_COL32(84, 84, 84, 255);
	const ImU32 kBtnActive  = IM_COL32(48, 96, 140, 255);
	const ImU32 kBtnActiveH = IM_COL32(58, 110, 158, 255);
	const ImU32 kText       = IM_COL32(200, 200, 200, 255);
	const ImU32 kSep        = IM_COL32(28, 28, 28, 255);

	const float kBtnH = 22.0f;   // 버튼 높이 (툴바 26px 안에서 위아래 2px 여백)

	ImU32 IconTint(bool active) { return active ? IM_COL32(240, 240, 240, 255) : IM_COL32_WHITE; }

	// 보이지 않는 버튼으로 영역을 만들고 클릭/호버를 돌려준다.
	bool Hit(const char* id, float x0, float y0, float x1, float y1, bool& hovered, const char* tip = nullptr)
	{
		ImGui::SetCursorScreenPos(ImVec2(x0, y0));
		bool pressed = ImGui::InvisibleButton(id, ImVec2(x1 - x0, y1 - y0));
		hovered = ImGui::IsItemHovered();
		if (tip && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_DelayNormal))
			ImGui::SetTooltip("%s", tip);
		return pressed;
	}

	void Bg(ImDrawList* dl, float x0, float y0, float x1, float y1, bool active, bool hovered, float rounding = 3.0f, ImDrawFlags flags = 0)
	{
		ImU32 c = active ? (hovered ? kBtnActiveH : kBtnActive) : (hovered ? kBtnHover : kBtn);
		dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), c, rounding, flags);
	}

	// 아이콘 하나짜리 토글/버튼 (폭 24). 누르면 true.
	bool IconButton(ImDrawList* dl, const char* id, const char* icon, float x, float y, bool active, const char* tip, float w = 24.0f)
	{
		bool hov = false;
		bool clicked = Hit(id, x, y, x + w, y + kBtnH, hov, tip);
		Bg(dl, x, y, x + w, y + kBtnH, active, hov);
		UnityGUI::DrawIcon(dl, icon, ImVec2(floorf(x + (w - 16.0f) * 0.5f), floorf(y + (kBtnH - 16.0f) * 0.5f)), 16.0f, IconTint(active));
		return clicked;
	}

	// 아이콘(토글) + 드롭다운 화살표가 붙은 버튼 (폭 37). 반환: 0 없음, 1 아이콘 클릭, 2 화살표 클릭
	int SplitButton(ImDrawList* dl, const char* id, const char* icon, float x, float y, bool active, const char* tip)
	{
		const float wIcon = 24.0f, wArrow = 13.0f;
		bool hovIcon = false, hovArrow = false;
		std::string idA = std::string(id) + "_i", idB = std::string(id) + "_a";
		bool cIcon = Hit(idA.c_str(), x, y, x + wIcon, y + kBtnH, hovIcon, tip);
		bool cArrow = Hit(idB.c_str(), x + wIcon, y, x + wIcon + wArrow, y + kBtnH, hovArrow);
		Bg(dl, x, y, x + wIcon, y + kBtnH, active, hovIcon, 3.0f, ImDrawFlags_RoundCornersLeft);
		Bg(dl, x + wIcon, y, x + wIcon + wArrow, y + kBtnH, false, hovArrow, 3.0f, ImDrawFlags_RoundCornersRight);
		UnityGUI::DrawIcon(dl, icon, ImVec2(floorf(x + (wIcon - 16.0f) * 0.5f), floorf(y + (kBtnH - 16.0f) * 0.5f)), 16.0f, IconTint(active));
		UnityGUI::DrawIcon(dl, "dropdown", ImVec2(floorf(x + wIcon + (wArrow - 12.0f) * 0.5f), floorf(y + (kBtnH - 12.0f) * 0.5f)), 12.0f);
		return cIcon ? 1 : (cArrow ? 2 : 0);
	}

	// 아이콘 + 글자 + 화살표 드롭다운 (Pivot / Local). 폭을 돌려주려면 measure 를 사용한다.
	float LabelDropWidth(const char* label)
	{
		return 6.0f + 16.0f + 4.0f + ImGui::CalcTextSize(label).x + 4.0f + 12.0f + 4.0f;
	}

	bool LabelDrop(ImDrawList* dl, const char* id, const char* icon, const char* label, float x, float y, const char* tip)
	{
		float w = LabelDropWidth(label);
		bool hov = false;
		bool clicked = Hit(id, x, y, x + w, y + kBtnH, hov, tip);
		Bg(dl, x, y, x + w, y + kBtnH, false, hov);
		UnityGUI::DrawIcon(dl, icon, ImVec2(floorf(x + 6.0f), floorf(y + (kBtnH - 16.0f) * 0.5f)), 16.0f);
		ImFont* font = ImGui::GetFont();
		float fs = ImGui::GetFontSize();
		dl->AddText(font, fs, ImVec2(floorf(x + 26.0f), floorf(y + (kBtnH - fs) * 0.5f + 0.5f)), kText, label);
		UnityGUI::DrawIcon(dl, "dropdown", ImVec2(floorf(x + w - 16.0f), floorf(y + (kBtnH - 12.0f) * 0.5f)), 12.0f);
		return clicked;
	}

	void Separator(ImDrawList* dl, float x, float y)
	{
		dl->AddLine(ImVec2(floorf(x) + 0.5f, y + 2.0f), ImVec2(floorf(x) + 0.5f, y + kBtnH - 2.0f), kSep);
	}

	// 드롭다운 팝업 (Unity 컨텍스트 메뉴 스타일)
	bool BeginDrop(const char* id, float x, float bottomY, float width = 170.0f)
	{
		ImGui::SetNextWindowPos(ImVec2(x, bottomY));
		GameObjectMenu::SetMenuWidth(width);
		GameObjectMenu::PushContextStyle();
		bool open = ImGui::BeginPopup(id);
		if (!open)
			GameObjectMenu::PopContextStyle();
		return open;
	}

	void EndDrop()
	{
		ImGui::EndPopup();
		GameObjectMenu::PopContextStyle();
	}

	bool Check(const char* label, bool* value)
	{
		return ImGui::MenuItem(label, nullptr, value);
	}

	// ---- Scene 카메라 설정 (사용자별 저장) ----
	SceneCameraSettings s_Cam;
	SceneCameraSettings s_AppliedCam;
	bool s_CamLoaded = false;

	std::wstring CameraSettingsFile()
	{
		wchar_t buf[MAX_PATH] = {};
		if (::GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH) == 0)
			return L"SceneCamera.json";
		return std::wstring(buf) + L"\\NOVA\\Editor\\SceneCamera.json";
	}

	void LoadCameraSettings()
	{
		s_CamLoaded = true;
		std::ifstream in(CameraSettingsFile());
		if (!in)
			return;
		json j = json::parse(in, nullptr, false);
		if (j.is_discarded() || !j.is_object())
			return;
		s_Cam.FieldOfView = std::clamp(j.value("fieldOfView", s_Cam.FieldOfView), 4.0f, 120.0f);
		s_Cam.NearClip = (std::max)(0.01f, j.value("nearClip", s_Cam.NearClip));
		s_Cam.FarClip = (std::max)(s_Cam.NearClip + 0.1f, j.value("farClip", s_Cam.FarClip));
		s_Cam.Easing = j.value("easing", s_Cam.Easing);
		s_Cam.Acceleration = j.value("acceleration", s_Cam.Acceleration);
		s_Cam.SpeedMin = (std::max)(0.001f, j.value("speedMin", s_Cam.SpeedMin));
		s_Cam.SpeedMax = (std::max)(s_Cam.SpeedMin, j.value("speedMax", s_Cam.SpeedMax));
		s_Cam.Speed = std::clamp(j.value("speed", s_Cam.Speed), s_Cam.SpeedMin, s_Cam.SpeedMax);
	}

	void SaveCameraSettings()
	{
		const std::filesystem::path file(CameraSettingsFile());
		std::error_code ec;
		std::filesystem::create_directories(file.parent_path(), ec);
		json j;
		j["fieldOfView"] = s_Cam.FieldOfView;
		j["nearClip"] = s_Cam.NearClip;
		j["farClip"] = s_Cam.FarClip;
		j["easing"] = s_Cam.Easing;
		j["acceleration"] = s_Cam.Acceleration;
		j["speed"] = s_Cam.Speed;
		j["speedMin"] = s_Cam.SpeedMin;
		j["speedMax"] = s_Cam.SpeedMax;
		std::ofstream os(file, std::ios::trunc);
		if (os)
			os << j.dump(4);
	}

	// Unity 의 Scene Camera 오버레이 패널 (어두운 창, 레이블 | 값)
	void DrawCameraPanel(EditorCamera* camera)
	{
		SceneCameraSettings& c = s_Cam;
		bool changed = false;
		const float labelW = 128.0f;
		auto label = [&](const char* text) {
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(text);
			ImGui::SameLine(labelW);
			ImGui::SetNextItemWidth(-1);
		};

		ImGui::PushFont(UnityGUI::BoldFont());
		ImGui::TextUnformatted("Scene Camera");
		ImGui::PopFont();
		ImGui::Separator();

		label("Field of View");
		changed |= ImGui::SliderFloat("##fov", &c.FieldOfView, 4.0f, 120.0f, "%.0f");

		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted("Clipping Planes");
		ImGui::SameLine(labelW);
		const float half = (ImGui::GetContentRegionAvail().x - 8.0f) * 0.5f;
		ImGui::TextUnformatted("Near");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(half - ImGui::CalcTextSize("Near").x - 6.0f);
		changed |= ImGui::DragFloat("##near", &c.NearClip, 0.01f, 0.01f, 100.0f, "%.2f");
		ImGui::SameLine();
		ImGui::TextUnformatted("Far");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(-1);
		changed |= ImGui::DragFloat("##far", &c.FarClip, 10.0f, 1.0f, 1000000.0f, "%.0f");

		ImGui::Spacing();
		ImGui::PushFont(UnityGUI::BoldFont());
		ImGui::TextUnformatted("Navigation");
		ImGui::PopFont();
		label("Camera Easing");
		changed |= ImGui::Checkbox("##easing", &c.Easing);
		label("Camera Acceleration");
		changed |= ImGui::Checkbox("##accel", &c.Acceleration);
		label("Camera Speed");
		changed |= ImGui::SliderFloat("##speed", &c.Speed, c.SpeedMin, c.SpeedMax, "%.2f", ImGuiSliderFlags_Logarithmic);
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted("");
		ImGui::SameLine(labelW);
		ImGui::TextUnformatted("Min");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(half - ImGui::CalcTextSize("Min").x - 6.0f);
		changed |= ImGui::DragFloat("##smin", &c.SpeedMin, 0.01f, 0.001f, 100.0f, "%.3f");
		ImGui::SameLine();
		ImGui::TextUnformatted("Max");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(-1);
		changed |= ImGui::DragFloat("##smax", &c.SpeedMax, 0.05f, 0.01f, 1000.0f, "%.2f");

		ImGui::Spacing();
		ImGui::Separator();
		if (ImGui::Button("Reset", ImVec2(80, 0)))
		{
			c = SceneCameraSettings();
			changed = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Reset Scene Camera") && camera != nullptr)
		{
			camera->LookAt(XMFLOAT3(7.0f, 5.0f, -19.0f), XMFLOAT3(0.0f, 1.0f, -5.0f), XMFLOAT3(0.0f, 1.0f, 0.0f));
			camera->UpdateViewMatrix();
		}

		if (changed)
		{
			c.FieldOfView = std::clamp(c.FieldOfView, 4.0f, 120.0f);
			c.NearClip = std::clamp(c.NearClip, 0.01f, 100.0f);
			c.FarClip = (std::max)(c.FarClip, c.NearClip + 0.1f);
			c.SpeedMin = (std::max)(0.001f, c.SpeedMin);
			c.SpeedMax = (std::max)(c.SpeedMin, c.SpeedMax);
			c.Speed = std::clamp(c.Speed, c.SpeedMin, c.SpeedMax);
			ApplyCameraLens(camera);
		}
		// 드래그/입력이 끝났을 때 저장
		if (changed && !ImGui::IsAnyItemActive())
			SaveCameraSettings();
		if (ImGui::IsItemDeactivatedAfterEdit() || (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && ImGui::IsWindowHovered()))
			SaveCameraSettings();
	}
}

namespace SceneToolbar
{
	Tool CurrentTool() { return s.tool; }
	void SetTool(Tool tool) { s.tool = tool; }
	PivotMode Pivot() { return s.pivot; }
	HandleSpace Space() { return s.space; }
	bool GizmosVisible() { return s.gizmos; }
	bool GridVisible() { return s.grid; }
	bool SnapEnabled() { return s.gridSnap || ImGui::GetIO().KeyCtrl; }
	float SnapIncrement() { return (std::max)(0.001f, s.snapIncrement); }
	bool PostProcessingVisible() { return s.effects && s.fxPost; }
	bool ParticlesVisible() { return s.effects && s.fxParticles; }

	SceneCameraSettings& CameraSettings()
	{
		if (!s_CamLoaded)
			LoadCameraSettings();
		return s_Cam;
	}

	void ApplyCameraLens(EditorCamera* camera, bool force)
	{
		if (camera == nullptr)
			return;
		const SceneCameraSettings& c = CameraSettings();
		if (!force && c.FieldOfView == s_AppliedCam.FieldOfView && c.NearClip == s_AppliedCam.NearClip && c.FarClip == s_AppliedCam.FarClip)
			return;
		s_AppliedCam = c;
		camera->SetLens(XMConvertToRadians(c.FieldOfView), camera->GetAspect(), c.NearClip, c.FarClip);
		EditorLog::Write("Camera", "scene camera lens fov %.1f near %.3f far %.1f", c.FieldOfView, c.NearClip, c.FarClip);
	}

	void HandleShortcuts(bool viewHovered)
	{
		ImGuiIO& io = ImGui::GetIO();
		if (!viewHovered || io.WantTextInput || io.KeyCtrl || io.KeyAlt || ImGui::IsMouseDown(ImGuiMouseButton_Right))
			return;
		if (ImGui::IsKeyPressed(ImGuiKey_Q, false)) s.tool = Tool::View;
		if (ImGui::IsKeyPressed(ImGuiKey_W, false)) s.tool = Tool::Move;
		if (ImGui::IsKeyPressed(ImGuiKey_E, false)) s.tool = Tool::Rotate;
		if (ImGui::IsKeyPressed(ImGuiKey_R, false)) s.tool = Tool::Scale;
		if (ImGui::IsKeyPressed(ImGuiKey_T, false)) s.tool = Tool::Rect;
		if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) s.tool = Tool::Transform;
	}

	void DrawTopBar(float width, EditorCamera* camera)
	{
		ImDrawList* dl = ImGui::GetWindowDrawList();
		ImVec2 origin = ImGui::GetCursorScreenPos();
		origin.x = floorf(origin.x);
		origin.y = floorf(origin.y);
		const float y = origin.y + 2.0f;
		const float bottom = origin.y + kTopBarHeight;

		dl->AddRectFilled(origin, ImVec2(origin.x + width, bottom), kBarBg);
		dl->AddLine(ImVec2(origin.x, bottom - 0.5f), ImVec2(origin.x + width, bottom - 0.5f), kBarLine);

		// ------------------------------ 왼쪽: Pivot / Local / 스냅 ------------------------------
		float x = origin.x + 4.0f;

		const char* pivotLabel = s.pivot == PivotMode::Pivot ? "Pivot" : "Center";
		const char* pivotIcon = s.pivot == PivotMode::Pivot ? "pivot" : "pivot_center";
		float pivotX = x;
		if (LabelDrop(dl, "##pivot", pivotIcon, pivotLabel, x, y, "Tool Handle Position"))
			ImGui::OpenPopup("pivot_menu");
		x += LabelDropWidth(pivotLabel) + 3.0f;

		const char* spaceLabel = s.space == HandleSpace::Local ? "Local" : "Global";
		const char* spaceIcon = s.space == HandleSpace::Local ? "space_local" : "space_global";
		float spaceX = x;
		if (LabelDrop(dl, "##space", spaceIcon, spaceLabel, x, y, "Tool Handle Rotation"))
			ImGui::OpenPopup("space_menu");
		x += LabelDropWidth(spaceLabel) + 7.0f;

		Separator(dl, x, y);
		x += 6.0f;

		// 스냅 간격 입력 (Unity 의 Grid Size 필드)
		{
			ImGui::SetCursorScreenPos(ImVec2(x, y));
			ImGui::PushStyleColor(ImGuiCol_FrameBg, EditorTheme::Rgb(34, 34, 34));
			ImGui::PushStyleColor(ImGuiCol_Border, EditorTheme::Rgb(20, 20, 20));
			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5.0f, floorf((kBtnH - ImGui::GetFontSize()) * 0.5f)));
			ImGui::SetNextItemWidth(40.0f);
			ImGui::DragFloat("##snapinc", &s.snapIncrement, 0.05f, 0.001f, 1000.0f, "%g");
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_DelayNormal))
				ImGui::SetTooltip("Grid Size");
			ImGui::PopStyleVar(3);
			ImGui::PopStyleColor(2);
			x += 40.0f + 4.0f;
		}

		float gridSnapX = x;
		int r = SplitButton(dl, "##gridsnap", "snap_grid", x, y, s.gridSnap, "Grid Snapping");
		if (r == 1) s.gridSnap = !s.gridSnap;
		if (r == 2) ImGui::OpenPopup("gridsnap_menu");
		x += 37.0f + 2.0f;

		float autoSnapX = x;
		r = SplitButton(dl, "##autosnap", "snap_move", x, y, s.autoSnap, "Increment Snapping");
		if (r == 1) s.autoSnap = !s.autoSnap;
		if (r == 2) ImGui::OpenPopup("autosnap_menu");
		x += 37.0f;
		const float leftEnd = x;

		// ------------------------------ 오른쪽: 드로우 모드 / 2D / 라이팅 ... / 기즈모 ------------------------------
		const float rightTotal = 12.0f + 6.0f + 24.0f * 4 + 37.0f + 8.0f + 26.0f + 24.0f + 37.0f + 24.0f + 37.0f + 37.0f + 37.0f + 4.0f;
		float rx = (std::max)(leftEnd + 10.0f, origin.x + width - rightTotal);

		dl->PushClipRect(ImVec2(leftEnd + 4.0f, origin.y), ImVec2(origin.x + width, bottom), true);

		UnityGUI::DrawIcon(dl, "grip", ImVec2(rx - 2.0f, y + 3.0f), 16.0f);
		rx += 12.0f + 6.0f;

		RenderManager* rm = RenderManager::GetI();
		if (IconButton(dl, "##wire", "draw_wire", rx, y, rm->WireFrameMode, "Wireframe")) rm->WireFrameMode = !rm->WireFrameMode;
		rx += 24.0f;
		if (IconButton(dl, "##shwire", "draw_shaded_wire", rx, y, s.shadedWire, "Shaded Wireframe")) s.shadedWire = !s.shadedWire;
		rx += 24.0f;
		if (IconButton(dl, "##shaded", "draw_shaded", rx, y, !rm->WireFrameMode && !s.shadedWire, "Shaded"))
		{
			rm->WireFrameMode = false;
			s.shadedWire = false;
		}
		rx += 24.0f;
		if (IconButton(dl, "##light", "scene_light", rx, y, s.sceneLighting, "Scene Lighting")) s.sceneLighting = !s.sceneLighting;
		rx += 24.0f;
		float bugX = rx;
		r = SplitButton(dl, "##bug", "bug", rx, y, rm->InstancingMode, "Debug Options (Instancing)");
		if (r == 1) rm->InstancingMode = !rm->InstancingMode;
		if (r == 2) ImGui::OpenPopup("bug_menu");
		rx += 37.0f;

		Separator(dl, rx + 3.0f, y);
		rx += 8.0f;

		{
			bool hov = false;
			bool clicked = Hit("##2d", rx, y, rx + 26.0f, y + kBtnH, hov, "2D Mode");
			Bg(dl, rx, y, rx + 26.0f, y + kBtnH, s.view2D, hov);
			ImFont* font = ImGui::GetFont();
			float fs = ImGui::GetFontSize();
			ImVec2 ts = ImGui::CalcTextSize("2D");
			dl->AddText(font, fs, ImVec2(floorf(rx + (26.0f - ts.x) * 0.5f), floorf(y + (kBtnH - fs) * 0.5f + 0.5f)), s.view2D ? IM_COL32(240, 240, 240, 255) : kText, "2D");
			if (clicked) s.view2D = !s.view2D;
			rx += 26.0f;
		}
		if (IconButton(dl, "##audio", s.audio ? "audio_on" : "audio_off", rx, y, s.audio, "Toggle audio on/off")) s.audio = !s.audio;
		rx += 24.0f;
		float effectsX = rx;
		r = SplitButton(dl, "##fx", "effects", rx, y, s.effects, "Effects");
		if (r == 1) s.effects = !s.effects;
		if (r == 2) ImGui::OpenPopup("fx_menu");
		rx += 37.0f;
		if (IconButton(dl, "##vis", "eye", rx, y, s.visibility, "Scene Visibility")) s.visibility = !s.visibility;
		rx += 24.0f;
		float gridX = rx;
		r = SplitButton(dl, "##grid", "layers", rx, y, s.grid, "Grid");
		if (r == 1) s.grid = !s.grid;
		if (r == 2) ImGui::OpenPopup("grid_menu");
		rx += 37.0f;
		float camX = rx;
		r = SplitButton(dl, "##cam", "scene_camera", rx, y, false, "Scene Camera");
		if (r != 0) ImGui::OpenPopup("cam_menu");
		rx += 37.0f;
		float gizX = rx;
		r = SplitButton(dl, "##giz", "gizmos", rx, y, s.gizmos, "Gizmos");
		if (r == 1) s.gizmos = !s.gizmos;
		if (r == 2) ImGui::OpenPopup("giz_menu");

		dl->PopClipRect();

		// ------------------------------ 드롭다운 팝업들 ------------------------------
		if (BeginDrop("pivot_menu", pivotX, bottom))
		{
			if (ImGui::MenuItem("Pivot", "Z", s.pivot == PivotMode::Pivot)) s.pivot = PivotMode::Pivot;
			if (ImGui::MenuItem("Center", "Z", s.pivot == PivotMode::Center)) s.pivot = PivotMode::Center;
			EndDrop();
		}
		if (BeginDrop("space_menu", spaceX, bottom))
		{
			if (ImGui::MenuItem("Local", "X", s.space == HandleSpace::Local)) s.space = HandleSpace::Local;
			if (ImGui::MenuItem("Global", "X", s.space == HandleSpace::Global)) s.space = HandleSpace::Global;
			EndDrop();
		}
		if (BeginDrop("gridsnap_menu", gridSnapX, bottom, 190.0f))
		{
			Check("Grid Snapping", &s.gridSnap);
			if (ImGui::MenuItem("Reset Increment (1)")) s.snapIncrement = 1.0f;
			EndDrop();
		}
		if (BeginDrop("autosnap_menu", autoSnapX, bottom, 190.0f))
		{
			Check("Increment Snapping", &s.autoSnap);
			EndDrop();
		}
		if (BeginDrop("bug_menu", bugX, bottom, 190.0f))
		{
			Check("Instancing", &rm->InstancingMode);
			Check("Wireframe", &rm->WireFrameMode);
			EndDrop();
		}
		if (BeginDrop("fx_menu", effectsX, bottom, 190.0f))
		{
			Check("Skybox", &s.fxSkybox);
			Check("Fog", &s.fxFog);
			Check("Flares", &s.fxFlares);
			Check("Always Refresh", &s.fxAlwaysRefresh);
			Check("Post Processing", &s.fxPost);
			Check("Particle Systems", &s.fxParticles);
			EndDrop();
		}
		if (BeginDrop("grid_menu", gridX, bottom, 190.0f))
		{
			Check("Show Grid", &s.grid);
			Check("Grid Snapping", &s.gridSnap);
			EndDrop();
		}
		// 카메라 드롭다운 = Scene Camera 패널 (시야각, 클리핑, 이동 속도)
		ImGui::SetNextWindowPos(ImVec2(camX + 37.0f, bottom), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(330.0f, 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 8));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 5));
		ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.22f, 0.22f, 0.22f, 1.0f));
		if (ImGui::BeginPopup("cam_menu"))
		{
			DrawCameraPanel(camera);
			ImGui::EndPopup();
		}
		ImGui::PopStyleColor();
		ImGui::PopStyleVar(2);
		if (BeginDrop("giz_menu", gizX - 30.0f, bottom, 190.0f))
		{
			Check("Gizmos", &s.gizmos);
			ImGui::Separator();
			Check("3D Icons", &s.giz3DIcons);
			Check("Selection Outline", &s.gizSelOutline);
			Check("Selection Wire", &s.gizSelWire);
			EndDrop();
		}

		// 툴바 아래로 커서를 이동해 뷰가 이어서 그려지게 한다
		ImGui::SetCursorScreenPos(ImVec2(origin.x, bottom));
	}

	void DrawToolPalette(const ImVec2& viewMin, const ImVec2& viewMax)
	{
		struct Item { Tool tool; const char* icon; const char* tip; };
		static const Item items[] = {
			{ Tool::View,      "tool_hand",      "Hand Tool (Q)" },
			{ Tool::Move,      "tool_move",      "Move Tool (W)" },
			{ Tool::Rotate,    "tool_rotate",    "Rotate Tool (E)" },
			{ Tool::Scale,     "tool_scale",     "Scale Tool (R)" },
			{ Tool::Rect,      "tool_rect",      "Rect Tool (T)" },
			{ Tool::Transform, "tool_transform", "Move, Rotate or Scale selected objects (Y)" },
		};

		ImDrawList* dl = ImGui::GetWindowDrawList();
		const float w = 40.0f, rowH = 21.0f;
		const float x = floorf(viewMin.x);
		float y = floorf(viewMin.y);

		const float headerH = 12.0f + 26.0f;
		const float totalH = (std::min)(headerH + rowH * 6.0f + 4.0f, viewMax.y - viewMin.y);
		dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + totalH), IM_COL32(40, 40, 40, 240));
		dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + totalH), IM_COL32(20, 20, 20, 255));
		dl->PushClipRect(ImVec2(x, y), ImVec2(x + w, y + totalH), true);

		// 위쪽 그립 (Overlay 드래그 손잡이 모양)
		UnityGUI::DrawIcon(dl, "grip", ImVec2(x + (w - 16.0f) * 0.5f, y - 1.0f), 16.0f);
		y += 12.0f;

		// 뷰 도구 종류 (큐브 + 드롭다운)
		UnityGUI::DrawIcon(dl, "tool_cube", ImVec2(x + 5.0f, y + 5.0f), 16.0f);
		UnityGUI::DrawIcon(dl, "dropdown", ImVec2(x + 24.0f, y + 7.0f), 12.0f);
		y += 26.0f;

		for (const Item& item : items)
		{
			bool hov = false;
			std::string id = std::string("##tool_") + item.icon;
			bool clicked = Hit(id.c_str(), x, y, x + w, y + rowH, hov, item.tip);
			bool active = s.tool == item.tool;
			if (active)
				dl->AddRectFilled(ImVec2(x + 1.0f, y), ImVec2(x + w - 1.0f, y + rowH), hov ? kBtnActiveH : IM_COL32(88, 88, 88, 255));
			else if (hov)
				dl->AddRectFilled(ImVec2(x + 1.0f, y), ImVec2(x + w - 1.0f, y + rowH), IM_COL32(70, 70, 70, 255));
			UnityGUI::DrawIcon(dl, item.icon, ImVec2(floorf(x + (w - 16.0f) * 0.5f), floorf(y + (rowH - 16.0f) * 0.5f)), 16.0f, active ? IM_COL32(255, 255, 255, 255) : IM_COL32(200, 200, 200, 255));
			if (clicked)
				s.tool = item.tool;
			y += rowH;
		}

		dl->PopClipRect();
	}
}
