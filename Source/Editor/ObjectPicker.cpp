#include "pch.h"
#include "ObjectPicker.h"
#include "UnityGUI.h"
#include <filesystem>

namespace
{
	struct State
	{
		bool Open = false;
		bool FocusNext = false;
		bool ScrollToSelected = false;
		std::string Key;
		ObjectPicker::Options Opt;
		std::string Selected;
		bool Changed = false;
		char Search[128] = {};
		int Tab = 0;              // 0 Assets, 1 Scene
		float IconSize = 0.0f;    // 0 = 목록
		bool ShowPackages = true;
		float PreviewH = 86.0f;
	} s;

	const ImU32 kBg = IM_COL32(56, 56, 56, 255);
	const ImU32 kListBg = IM_COL32(48, 48, 48, 255);
	const ImU32 kSel = IM_COL32(44, 93, 135, 255);
	const ImU32 kHover = IM_COL32(69, 69, 69, 255);
	const ImU32 kText = IM_COL32(210, 210, 210, 255);
	const ImU32 kTextDim = IM_COL32(150, 150, 150, 255);
	const ImU32 kLine = IM_COL32(30, 30, 30, 255);

	std::string Lower(std::string v) { std::transform(v.begin(), v.end(), v.begin(), ::tolower); return v; }
	std::string NameOf(const std::string& path)
	{
		if (path.empty())
			return "None";
		if (path.rfind("builtin:", 0) == 0)   // 내장 에셋 (예: builtin:Default-Material)
			return path.substr(8);
		return std::filesystem::path(path).stem().string();
	}
	bool IsPackage(const std::string& path) { return _strnicmp(path.c_str(), "Resources\\Packages", 18) == 0; }

	void Select(const std::string& path, bool close)
	{
		if (path != s.Selected)
		{
			s.Selected = path;
			s.Changed = true;   // Unity 처럼 한 번 클릭으로 바로 할당
		}
		if (close)
			s.Open = false;
	}
}

namespace ObjectPicker
{
	void Open(const std::string& key, Options options)
	{
		s.Open = true;
		s.FocusNext = true;
		s.ScrollToSelected = true;
		s.Key = key;
		s.Opt = std::move(options);
		s.Selected = s.Opt.Current;
		s.Changed = false;
		s.Search[0] = 0;
		s.Tab = 0;
		EditorLog::Write("Editor", "object picker: Select %s (%d items)", s.Opt.TypeName.c_str(), (int)s.Opt.Items.size());
	}

	bool Poll(const std::string& key, std::string& outPath)
	{
		if (key != s.Key || !s.Changed)
			return false;
		s.Changed = false;
		outPath = s.Selected;
		return true;
	}

	bool IsOpenFor(const std::string& key) { return s.Open && s.Key == key; }

	void Draw()
	{
		if (!s.Open)
			return;
		const ImGuiViewport* vp = ImGui::GetMainViewport();
		ImGui::SetNextWindowSize(ImVec2(388, 728), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
		if (s.FocusNext)
		{
			ImGui::SetNextWindowFocus();
			s.FocusNext = false;
		}
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::ColorConvertU32ToFloat4(kBg));
		const std::string title = "Select " + s.Opt.TypeName + "###ObjectPicker";
		bool open = true;
		if (!ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoScrollbar))
		{
			ImGui::End();
			ImGui::PopStyleColor();
			ImGui::PopStyleVar(2);
			s.Open = open;
			return;
		}
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const float w = ImGui::GetContentRegionAvail().x;
		const float h = ImGui::GetContentRegionAvail().y;

		// ---- 검색창
		{
			ImGui::SetCursorScreenPos(ImVec2(p.x + 3, p.y + 3));
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(20, 2));
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
			ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.16f, 0.16f, 0.16f, 1.0f));
			ImGui::SetNextItemWidth(w - 6);
			if (ImGui::IsWindowAppearing())
				ImGui::SetKeyboardFocusHere();
			ImGui::InputText("##pickerSearch", s.Search, sizeof(s.Search));
			ImGui::PopStyleColor();
			ImGui::PopStyleVar(2);
			UnityGUI::DrawIcon(dl, "search", ImVec2(p.x + 7, p.y + 5), 14.0f);
		}
		const float tabY = p.y + 26.0f;

		// 보이는 항목 (검색 + 패키지 숨김)
		const std::string needle = Lower(s.Search);
		std::vector<std::string> items;
		int hiddenPackages = 0;
		if (s.Tab == 0)
			for (const std::string& it : s.Opt.Items)
			{
				if (!s.ShowPackages && IsPackage(it)) { ++hiddenPackages; continue; }
				if (!needle.empty() && Lower(NameOf(it)).find(needle) == std::string::npos)
					continue;
				items.push_back(it);
			}

		// ---- 탭 + 아이콘 크기 + 패키지 표시
		{
			const char* tabs[] = { "Assets", "Scene" };
			for (int i = 0; i < 2; ++i)
			{
				const float tx = p.x + i * 96.0f;
				ImGui::SetCursorScreenPos(ImVec2(tx, tabY));
				ImGui::PushID(i);
				if (ImGui::InvisibleButton("##tab", ImVec2(96, 20)))
					s.Tab = i;
				ImGui::PopID();
				if (s.Tab == i)
				{
					dl->AddRectFilled(ImVec2(tx, tabY), ImVec2(tx + 96, tabY + 20), IM_COL32(64, 64, 64, 255));
					dl->AddLine(ImVec2(tx, tabY + 19), ImVec2(tx + 96, tabY + 19), IM_COL32(58, 121, 187, 255), 2.0f);
				}
				const ImVec2 ts = ImGui::CalcTextSize(tabs[i]);
				dl->AddText(ImVec2(floorf(tx + (96 - ts.x) * 0.5f), tabY + 3), s.Tab == i ? kText : kTextDim, tabs[i]);
			}
			// 눈 + 숨긴 패키지 개수 (누르면 토글)
			const std::string count = std::to_string(hiddenPackages);
			const float eyeW = 22.0f + ImGui::CalcTextSize(count.c_str()).x + 6.0f;
			const float ex = p.x + w - eyeW - 2;
			ImGui::SetCursorScreenPos(ImVec2(ex, tabY));
			if (ImGui::InvisibleButton("##pkgToggle", ImVec2(eyeW, 20)))
				s.ShowPackages = !s.ShowPackages;
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip(s.ShowPackages ? "Hide package assets" : "Show package assets");
			dl->AddRectFilled(ImVec2(ex, tabY + 1), ImVec2(ex + eyeW, tabY + 19), IM_COL32(70, 70, 70, 255), 2.0f);
			UnityGUI::DrawIcon(dl, "eye", ImVec2(ex + 3, tabY + 2), 16.0f, s.ShowPackages ? IM_COL32_WHITE : IM_COL32(170, 170, 170, 255));
			if (!s.ShowPackages)
				dl->AddLine(ImVec2(ex + 4, tabY + 16), ImVec2(ex + 18, tabY + 4), IM_COL32(200, 200, 200, 255), 1.5f);
			dl->AddText(ImVec2(ex + 21, tabY + 3), kText, count.c_str());
			// 아이콘 크기 슬라이더
			const float sw = 50.0f, sx = ex - sw - 12.0f, cy = tabY + 10.0f;
			ImGui::SetCursorScreenPos(ImVec2(sx - 6, tabY));
			ImGui::InvisibleButton("##iconSize", ImVec2(sw + 12, 20));
			if (ImGui::IsItemActive())
				s.IconSize = std::clamp((ImGui::GetIO().MousePos.x - sx) / sw, 0.0f, 1.0f);
			dl->AddLine(ImVec2(sx, cy), ImVec2(sx + sw, cy), IM_COL32(110, 110, 110, 255), 2.0f);
			dl->AddCircleFilled(ImVec2(sx + sw * s.IconSize, cy), 5.0f, IM_COL32(190, 190, 190, 255));
		}

		// ---- 목록 / 격자
		const float listY = tabY + 21.0f;
		const float previewY = p.y + h - s.PreviewH;
		ImGui::SetCursorScreenPos(ImVec2(p.x, listY));
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(kListBg));
		ImGui::BeginChild("##pickerList", ImVec2(w, (std::max)(40.0f, previewY - listY - 4.0f)), false);
		{
			ImDrawList* ldl = ImGui::GetWindowDrawList();
			const ImVec2 o = ImGui::GetCursorScreenPos();
			const float lw = ImGui::GetContentRegionAvail().x;
			std::vector<std::string> rows;
			rows.push_back(std::string());   // None
			rows.insert(rows.end(), items.begin(), items.end());
			std::string clicked;
			bool clickedAny = false, doubleClicked = false;

			if (s.IconSize <= 0.02f)
			{
				const float rh = 18.0f;
				for (size_t i = 0; i < rows.size(); ++i)
				{
					const float y = o.y + i * rh;
					ImGui::SetCursorScreenPos(ImVec2(o.x, y));
					ImGui::PushID((int)i);
					if (ImGui::InvisibleButton("##row", ImVec2(lw, rh))) { clicked = rows[i]; clickedAny = true; }
					if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) { clicked = rows[i]; clickedAny = true; doubleClicked = true; }
					const bool sel = rows[i] == s.Selected;
					if (sel || ImGui::IsItemHovered())
						ldl->AddRectFilled(ImVec2(o.x, y), ImVec2(o.x + lw, y + rh), sel ? kSel : kHover);
					if (sel && s.ScrollToSelected) { ImGui::SetScrollHereY(0.3f); s.ScrollToSelected = false; }
					if (!rows[i].empty() && s.Opt.Icon)
						UnityGUI::DrawIcon(ldl, s.Opt.Icon, ImVec2(o.x + 16, y + 1), 16.0f);
					ldl->AddText(ImVec2(o.x + 34, y + 1), kText, NameOf(rows[i]).c_str());
					ImGui::PopID();
				}
				ImGui::SetCursorScreenPos(ImVec2(o.x, o.y + rows.size() * rh));
			}
			else
			{
				const float tile = 48.0f + s.IconSize * 56.0f;
				const int cols = (std::max)(1, (int)(lw / tile));
				for (size_t i = 0; i < rows.size(); ++i)
				{
					const float x = o.x + (i % cols) * tile, y = o.y + (i / cols) * (tile + 16.0f);
					ImGui::SetCursorScreenPos(ImVec2(x, y));
					ImGui::PushID((int)i);
					if (ImGui::InvisibleButton("##tile", ImVec2(tile, tile + 14))) { clicked = rows[i]; clickedAny = true; }
					if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) { clicked = rows[i]; clickedAny = true; doubleClicked = true; }
					const bool sel = rows[i] == s.Selected;
					if (sel || ImGui::IsItemHovered())
						ldl->AddRectFilled(ImVec2(x + 2, y + 2), ImVec2(x + tile - 2, y + tile + 12), sel ? kSel : kHover, 3.0f);
					if (!rows[i].empty() && s.Opt.Icon)
						UnityGUI::DrawIcon(ldl, s.Opt.Icon, ImVec2(x + tile * 0.2f, y + 4), tile * 0.6f);
					const std::string name = NameOf(rows[i]);
					ImVec4 clip(x + 2, y, x + tile - 2, y + tile + 14);
					const float tw = ImGui::CalcTextSize(name.c_str()).x;
					ldl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2((std::max)(x + 3, x + (tile - tw) * 0.5f), y + tile - 4), kText, name.c_str(), nullptr, 0.0f, &clip);
					ImGui::PopID();
				}
				const size_t lines = (rows.size() + cols - 1) / cols;
				ImGui::SetCursorScreenPos(ImVec2(o.x, o.y + lines * (tile + 16.0f)));
			}
			ImGui::Dummy(ImVec2(1, 4));
			if (clickedAny)
				Select(clicked, doubleClicked);

			// 키보드: 위/아래 이동, Enter = 확정, Esc = 닫기
			if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
			{
				auto it = std::find(rows.begin(), rows.end(), s.Selected);
				int idx = it == rows.end() ? 0 : (int)(it - rows.begin());
				if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && idx + 1 < (int)rows.size()) { Select(rows[idx + 1], false); s.ScrollToSelected = true; }
				if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && idx > 0) { Select(rows[idx - 1], false); s.ScrollToSelected = true; }
				if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) s.Open = false;
				if (ImGui::IsKeyPressed(ImGuiKey_Escape)) s.Open = false;
			}
		}
		ImGui::EndChild();
		ImGui::PopStyleColor();

		// ---- 미리보기 (위쪽 손잡이로 높이 조절)
		{
			ImGui::SetCursorScreenPos(ImVec2(p.x, previewY - 4));
			ImGui::InvisibleButton("##previewSplit", ImVec2(w, 6));
			if (ImGui::IsItemHovered() || ImGui::IsItemActive())
				ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
			if (ImGui::IsItemActive())
				s.PreviewH = std::clamp(s.PreviewH - ImGui::GetIO().MouseDelta.y, 40.0f, h * 0.6f);
			dl->AddRectFilled(ImVec2(p.x, previewY), ImVec2(p.x + w, p.y + h), IM_COL32(40, 40, 40, 255));
			dl->AddLine(ImVec2(p.x, previewY), ImVec2(p.x + w, previewY), kLine);
			dl->AddLine(ImVec2(p.x + w * 0.5f - 16, previewY + 3), ImVec2(p.x + w * 0.5f + 16, previewY + 3), IM_COL32(120, 120, 120, 255));
			dl->AddLine(ImVec2(p.x + w * 0.5f - 16, previewY + 6), ImVec2(p.x + w * 0.5f + 16, previewY + 6), IM_COL32(120, 120, 120, 255));
			float ty = previewY + 14.0f;
			float tx = p.x + 16.0f;
			if (!s.Selected.empty() && s.Opt.Icon)
			{
				UnityGUI::DrawIcon(dl, s.Opt.Icon, ImVec2(tx, ty), 40.0f);
				tx += 50.0f;
			}
			else
				tx += 70.0f;
			dl->AddText(ImVec2(tx, ty), kText, NameOf(s.Selected).c_str());
			if (!s.Selected.empty())
			{
				dl->AddText(ImVec2(tx, ty + 16), kTextDim, s.Selected.c_str());
				if (s.Opt.Describe)
					dl->AddText(ImVec2(tx, ty + 32), kTextDim, s.Opt.Describe(s.Selected).c_str());
				if (s.Opt.Preview)
				{
					ImGui::SetCursorScreenPos(ImVec2(p.x + w - 34, ty));
					if (ImGui::InvisibleButton("##previewPlay", ImVec2(24, 24)))
						s.Opt.Preview(s.Selected);
					const bool hov = ImGui::IsItemHovered();
					dl->AddRectFilled(ImVec2(p.x + w - 34, ty), ImVec2(p.x + w - 10, ty + 24), hov ? IM_COL32(84, 84, 84, 255) : IM_COL32(66, 66, 66, 255), 3.0f);
					const float bx = p.x + w - 26, by = ty + 6;
					dl->AddTriangleFilled(ImVec2(bx, by), ImVec2(bx, by + 12), ImVec2(bx + 10, by + 6), IM_COL32(220, 220, 220, 255));
				}
			}
		}

		ImGui::End();
		ImGui::PopStyleColor();
		ImGui::PopStyleVar(2);
		if (!open)
			s.Open = false;
	}
}
