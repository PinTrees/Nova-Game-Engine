#include "pch.h"
#include "AddComponentMenu.h"
#include "ComponentFactory.h"
#include "ScriptEngine.h"
#include "CSharpScript.h"
#include "UnityGUI.h"
#include "EditorTheme.h"
#include <algorithm>
#include <cctype>

namespace
{
	// 등록된 타입 이름 → Unity 메뉴에서의 카테고리 / 표시 이름 / 아이콘
	struct Entry
	{
		std::string type;       // ComponentFactory 에 등록된 이름
		std::string display;    // 메뉴에 보이는 이름
		std::string category;
		const char* icon;
		bool single;            // 한 GameObject 에 하나만 (Unity 의 DisallowMultipleComponent)
	};

	struct KnownInfo { const char* type; const char* display; const char* category; const char* icon; bool single; };
	const KnownInfo kKnown[] = {
		{ "MeshFilter",          "Mesh Filter",           "Mesh",          "mesh_filter",       true  },
		{ "MeshRenderer",        "Mesh Renderer",         "Mesh",          "mesh_renderer",     true  },
		{ "SkinnedMeshRenderer", "Skinned Mesh Renderer", "Mesh",          "skinned_mesh_renderer", true  },
		{ "RigidBody",           "Rigidbody",             "Physics",       "rigidbody",         true  },
		{ "BoxCollider",         "Box Collider",          "Physics",       "box_collider",      false },
		{ "SphereCollider",      "Sphere Collider",       "Physics",       "sphere_collider",   false },
		{ "CapsuleCollider",     "Capsule Collider",      "Physics",       "capsule_collider",  false },
		{ "MeshCollider",        "Mesh Collider",         "Physics",       "mesh_collider",     false },
		{ "TerrainCollider",     "Terrain Collider",      "Physics",       "terrain_collider",  true  },
		{ "CharacterController", "Character Controller",  "Physics",       "capsule_collider",  true  },
		{ "Terrain",             "Terrain",               "Miscellaneous", "terrain",           true  },
		{ "Tree",                "Tree",                  "Miscellaneous", "terrain_trees",     true  },
		{ "Rock",                "Rock",                  "Miscellaneous", "terrain_paint",     true  },
		{ "RockScatter",         "Rock Scatter",          "Miscellaneous", "terrain_paint",     true  },
		{ "Volume",              "Volume",                "Miscellaneous", "volume",            true  },
		{ "WaterBody",           "Water Body",            "Water",         "terrain_paint",     true  },
		{ "TerrainSpline",       "Terrain Spline",        "Miscellaneous", "terrain_paint",     true  },
		{ "Buoyancy",            "Buoyancy",              "Water",         "terrain_paint",     true  },
		{ "ParticleSystem",      "Particle System",       "Effects",       "particle_system",   true  },
		{ "AudioSource",         "Audio Source",          "Audio",         "audio_source",      false },
		{ "AudioListener",       "Audio Listener",        "Audio",         "audio_listener",    true  },
		{ "Camera",              "Camera",                "Rendering",     "camera",            true  },
		{ "Light",               "Light",                 "Rendering",     "light_directional", true  },
		{ "AnimationPlayer",     "Animation",             "Miscellaneous", "animation",         true  },
		{ "Animator",            "Animator",              "Miscellaneous", "animator",          true  },
		// UI (Unity 의 UI / Layout / Event 분류)
		{ "UIImage",             "Image",                 "UI",            "ui_image",          true  },
		{ "Text",                "Text",                  "UI",            "ui_text",           true  },
		{ "Button",              "Button",                "UI",            "ui_button",         true  },
		{ "Toggle",              "Toggle",                "UI",            "ui_toggle",         true  },
		{ "Slider",              "Slider",                "UI",            "ui_slider",         true  },
		{ "InputField",          "Input Field",           "UI",            "ui_input_field",    true  },
		{ "ScrollRect",          "Scroll Rect",           "UI",            "ui_scroll_rect",    true  },
		{ "Mask",                "Mask",                  "UI",            "ui_mask",           true  },
		{ "RectMask2D",          "Rect Mask 2D",          "UI",            "ui_mask",           true  },
		{ "RectTransform",       "Rect Transform",        "Layout",        "rect_transform",    true  },
		{ "Canvas",              "Canvas",                "Layout",        "canvas",            true  },
		{ "CanvasScaler",        "Canvas Scaler",         "Layout",        "canvas_scaler",     true  },
		{ "GraphicRaycaster",    "Graphic Raycaster",     "Event",         "graphic_raycaster", true  },
		{ "EventSystem",         "Event System",          "Event",         "event_system",      true  },
	};

	// 메뉴에 보이지 않는 타입 (항상 있거나 직접 붙일 수 없는 기반 클래스)
	bool IsHidden(const std::string& type)
	{
		return type == "Transform" || type == "Collider" || type == "Component" || type == "MonoBehaviour" || type == "CSharpScript";
	}

	std::vector<Entry> BuildEntries()
	{
		std::vector<Entry> out;
		for (const std::string& type : ComponentFactory::Instance().GetComponentTypes())
		{
			// 표에 있는 네이티브 컴포넌트만 (REGISTER_SCRIPT 로 등록된 C++ 예제 스크립트는 보이지 않는다)
			for (const KnownInfo& k : kKnown)
				if (type == k.type && !IsHidden(type))
					out.push_back(Entry{ type, k.display, k.category, k.icon, k.single });
		}
		// Scripts 는 프로젝트 C# 스크립트만 (Unity 의 Add Component > Scripts)
		for (const ScriptEngine::ClassInfo& c : ScriptEngine::Classes())
			out.push_back(Entry{ "script:" + c.FullName, c.Name, "Scripts", "script_cs", false });
		std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) { return a.display < b.display; });
		return out;
	}

	std::string Lower(std::string s)
	{
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
		return s;
	}

	bool HasComponentOfType(GameObject* go, const std::string& type)
	{
		for (const auto& c : go->GetComponents())
			if (c && c->GetType() == type)
				return true;
		return false;
	}

	// ---- 팝업 상태 ----
	bool s_OpenRequest = false;
	ImVec2 s_Pos;
	float s_Width = 230.0f;
	char s_Search[128] = {};
	std::string s_Category;     // 비어 있으면 카테고리 목록
	int s_Cursor = 0;           // 키보드로 고른 행
	bool s_FocusSearch = false;

	// ---- Unity 다크 팝업 색 ----
	const ImU32 kBg       = IM_COL32(56, 56, 56, 255);
	const ImU32 kHeaderBg = IM_COL32(62, 62, 62, 255);
	const ImU32 kBorder   = IM_COL32(26, 26, 26, 255);
	const ImU32 kRowHover = IM_COL32(44, 93, 135, 255);
	const ImU32 kText     = IM_COL32(210, 210, 210, 255);
	const ImU32 kTextDim  = IM_COL32(120, 120, 120, 255);

	const float kRowH = 20.0f;
	const float kListH = 320.0f;

	enum class RowKind { Category, Component, Back };
	struct Row
	{
		RowKind kind;
		std::string label;
		const Entry* entry = nullptr;
		bool disabled = false;
	};

	// 한 행을 그리고 클릭되었으면 true
	bool DrawRow(ImDrawList* dl, const Row& row, int index, bool& hoveredOut)
	{
		ImVec2 p = ImGui::GetCursorScreenPos();
		const float w = ImGui::GetContentRegionAvail().x;
		ImGui::PushID(index);
		bool clicked = ImGui::InvisibleButton("##row", ImVec2(w, kRowH));
		ImGui::PopID();
		bool hovered = ImGui::IsItemHovered();
		hoveredOut = hovered;
		if (hovered && ImGui::GetIO().MouseDelta.x * ImGui::GetIO().MouseDelta.x + ImGui::GetIO().MouseDelta.y * ImGui::GetIO().MouseDelta.y > 0.0f)
			s_Cursor = index;   // 마우스를 움직이면 선택 행이 따라온다
		const bool selected = s_Cursor == index;

		if (selected && !row.disabled)
			dl->AddRectFilled(p, ImVec2(p.x + w, p.y + kRowH), kRowHover);

		const float fs = ImGui::GetFontSize();
		const float ty = floorf(p.y + (kRowH - fs) * 0.5f + 0.5f);
		float tx = p.x + 8.0f;
		if (row.kind == RowKind::Component && row.entry)
		{
			UnityGUI::DrawIcon(dl, row.entry->icon, ImVec2(p.x + 6.0f, p.y + 2.0f), 16.0f, row.disabled ? IM_COL32(255, 255, 255, 90) : IM_COL32_WHITE);
			tx = p.x + 28.0f;
		}
		dl->AddText(ImVec2(tx, ty), row.disabled ? kTextDim : kText, row.label.c_str());
		if (row.kind == RowKind::Category)
			UnityGUI::DrawIcon(dl, "arrow_right", ImVec2(p.x + w - 20.0f, p.y + 2.0f), 16.0f);

		if (row.disabled && hovered)
			ImGui::SetTooltip("The GameObject already contains this component.");
		return clicked && !row.disabled;
	}
}

namespace AddComponentMenu
{
	void Open(const ImVec2& buttonMin, const ImVec2& buttonMax)
	{
		s_OpenRequest = true;
		s_Width = (std::max)(230.0f, buttonMax.x - buttonMin.x);
		// 아래 공간이 모자라면 버튼 위쪽에 연다 (Unity 와 동일)
		const float popupH = 24.0f + 30.0f + kListH + 8.0f;
		const float screenBottom = ImGui::GetMainViewport()->WorkPos.y + ImGui::GetMainViewport()->WorkSize.y;
		s_Pos = buttonMax.y + 2.0f + popupH <= screenBottom ? ImVec2(buttonMin.x, buttonMax.y + 2.0f) : ImVec2(buttonMin.x, (std::max)(0.0f, buttonMin.y - 2.0f - popupH));
		s_Search[0] = 0;
		s_Category.clear();
		s_Cursor = 0;
		s_FocusSearch = true;
	}

	void DevPreset(const char* preset)
	{
		if (preset == nullptr || preset[0] == 0)
			return;
		if (preset[0] == '?')
			strncpy_s(s_Search, preset + 1, _TRUNCATE);
		else
			s_Category = preset;
	}

	void Draw(GameObject* target, const std::function<void(std::shared_ptr<Component>)>& onAdd)
	{
		if (s_OpenRequest)
		{
			ImGui::OpenPopup("##AddComponentMenu");
			s_OpenRequest = false;
		}

		ImGui::SetNextWindowPos(s_Pos, ImGuiCond_Appearing);
		ImGui::SetNextWindowSize(ImVec2(s_Width, 0.0f));
		ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGui::ColorConvertU32ToFloat4(kBg));
		ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(kBorder));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 0.0f);

		if (!ImGui::BeginPopup("##AddComponentMenu"))
		{
			ImGui::PopStyleVar(4);
			ImGui::PopStyleColor(2);
			return;
		}

		ImDrawList* dl = ImGui::GetWindowDrawList();
		const float w = ImGui::GetContentRegionAvail().x;

		// ---- 검색창 ----
		{
			ImGui::Dummy(ImVec2(0.0f, 5.0f));
			ImGui::SetCursorPosX(6.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 9.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(24.0f, 2.0f));
			ImGui::PushStyleColor(ImGuiCol_FrameBg, EditorTheme::Rgb(42, 42, 42));
			ImGui::PushStyleColor(ImGuiCol_Border, EditorTheme::Rgb(58, 121, 187));
			if (s_FocusSearch)
			{
				ImGui::SetKeyboardFocusHere();
				s_FocusSearch = false;
			}
			ImGui::SetNextItemWidth(w - 12.0f);
			if (ImGui::InputText("##search", s_Search, sizeof(s_Search)))
				s_Cursor = 0;
			ImVec2 smin = ImGui::GetItemRectMin();
			UnityGUI::DrawIcon(dl, "search", ImVec2(smin.x + 5.0f, smin.y + 2.0f), 14.0f);
			ImGui::PopStyleColor(2);
			ImGui::PopStyleVar(3);
			ImGui::Dummy(ImVec2(0.0f, 5.0f));
		}

		const std::string query = Lower(s_Search);
		const bool searching = !query.empty();
		static std::vector<Entry> entries;
		entries = BuildEntries();

		// ---- 헤더 ("Component" 또는 카테고리 이름 + 뒤로 화살표) ----
		{
			ImVec2 p = ImGui::GetCursorScreenPos();
			const float h = 24.0f;
			dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), kHeaderBg);
			dl->AddLine(ImVec2(p.x, p.y), ImVec2(p.x + w, p.y), kBorder);
			dl->AddLine(ImVec2(p.x, p.y + h - 1.0f), ImVec2(p.x + w, p.y + h - 1.0f), kBorder);
			std::string title = searching ? "Search" : (s_Category.empty() ? "Component" : s_Category);
			ImFont* bold = UnityGUI::BoldFont();
			const float fs = bold->FontSize;
			ImVec2 ts = bold->CalcTextSizeA(fs, FLT_MAX, 0.0f, title.c_str());
			dl->AddText(bold, fs, ImVec2(floorf(p.x + (w - ts.x) * 0.5f), floorf(p.y + (h - fs) * 0.5f)), kText, title.c_str());
			bool back = false;
			if (!searching && !s_Category.empty())
			{
				ImGui::SetCursorScreenPos(p);
				if (ImGui::InvisibleButton("##back", ImVec2(28.0f, h)))
					back = true;
				UnityGUI::DrawIcon(dl, "arrow_left", ImVec2(p.x + 6.0f, p.y + 4.0f), 16.0f);
			}
			ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
			if (back)
			{
				s_Category.clear();
				s_Cursor = 0;
			}
		}

		// ---- 행 목록 ----
		std::vector<Row> rows;
		if (searching)
		{
			for (const Entry& e : entries)
				if (Lower(e.display).find(query) != std::string::npos || Lower(e.type).find(query) != std::string::npos)
					rows.push_back({ RowKind::Component, e.display, &e, e.single && HasComponentOfType(target, e.type) });
		}
		else if (s_Category.empty())
		{
			std::vector<std::string> cats;
			for (const Entry& e : entries)
				if (std::find(cats.begin(), cats.end(), e.category) == cats.end())
					cats.push_back(e.category);
			std::sort(cats.begin(), cats.end());
			for (const std::string& c : cats)
				rows.push_back({ RowKind::Category, c });
		}
		else
		{
			for (const Entry& e : entries)
				if (e.category == s_Category)
					rows.push_back({ RowKind::Component, e.display, &e, e.single && HasComponentOfType(target, e.type) });
		}

		// 키보드: ↑/↓ 이동, Enter 선택, → 들어가기, ← 뒤로
		if (!rows.empty())
		{
			if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) s_Cursor = (std::min)(s_Cursor + 1, (int)rows.size() - 1);
			if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) s_Cursor = (std::max)(s_Cursor - 1, 0);
		}
		s_Cursor = std::clamp(s_Cursor, 0, (std::max)(0, (int)rows.size() - 1));
		bool activateCursor = !rows.empty() && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter) ||
			(!searching && ImGui::IsKeyPressed(ImGuiKey_RightArrow) && rows[s_Cursor].kind == RowKind::Category));
		if (!searching && !s_Category.empty() && (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_Backspace)))
		{
			s_Category.clear();
			s_Cursor = 0;
		}

		int clickedIndex = -1;
		const float listH = (std::min)(kListH, (std::max)(kRowH, rows.size() * kRowH));
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(kBg));
		ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 10.0f);
		if (ImGui::BeginChild("##rows", ImVec2(w, listH), false))
		{
			ImDrawList* cdl = ImGui::GetWindowDrawList();
			for (int i = 0; i < (int)rows.size(); ++i)
			{
				bool hovered = false;
				if (DrawRow(cdl, rows[i], i, hovered))
					clickedIndex = i;
				if (i == s_Cursor && (ImGui::IsKeyPressed(ImGuiKey_DownArrow) || ImGui::IsKeyPressed(ImGuiKey_UpArrow)))
					ImGui::SetScrollHereY(0.5f);
			}
			if (rows.empty())
				ImGui::TextDisabled("  No Results");
		}
		ImGui::EndChild();
		ImGui::PopStyleVar();
		ImGui::PopStyleColor();
		ImGui::Dummy(ImVec2(0.0f, 4.0f));

		if (activateCursor && clickedIndex < 0 && !rows[s_Cursor].disabled)
			clickedIndex = s_Cursor;

		if (clickedIndex >= 0)
		{
			const Row& row = rows[clickedIndex];
			if (row.kind == RowKind::Category)
			{
				s_Category = row.label;
				s_Cursor = 0;
			}
			else if (row.kind == RowKind::Component && row.entry)
			{
				if (row.entry->type.rfind("script:", 0) == 0)
					onAdd(CSharpScript::Create(row.entry->type.substr(7)));
				else if (auto c = ComponentFactory::Instance().CreateComponent(row.entry->type))
					onAdd(c);
				ImGui::CloseCurrentPopup();
			}
		}

		if (ImGui::IsKeyPressed(ImGuiKey_Escape))
			ImGui::CloseCurrentPopup();

		ImGui::EndPopup();
		ImGui::PopStyleVar(4);
		ImGui::PopStyleColor(2);
	}

	bool IsListed(const std::string& type)
	{
		if (IsHidden(type))
			return false;
		for (const KnownInfo& k : kKnown)
			if (type == k.type)
				return true;
		return false;
	}

	std::string DisplayName(const std::string& type)
	{
		for (const KnownInfo& k : kKnown)
			if (type == k.type)
				return k.display;
		return type;
	}
}
