#include "pch.h"
#include "PackageManagerWindow.h"
#include "PackageManager.h"
#include "UnityGUI.h"
#include <algorithm>
#include <cctype>

PackageManagerWindow* PackageManagerWindow::s_Instance = nullptr;

namespace
{
	const ImU32 kToolbarBg = IM_COL32(60, 60, 60, 255);
	const ImU32 kListBg    = IM_COL32(48, 48, 48, 255);
	const ImU32 kDetailBg  = IM_COL32(56, 56, 56, 255);
	const ImU32 kLine      = IM_COL32(30, 30, 30, 255);
	const ImU32 kRowSel    = IM_COL32(44, 93, 135, 255);
	const ImU32 kRowHover  = IM_COL32(70, 70, 70, 255);
	const ImU32 kText      = IM_COL32(210, 210, 210, 255);
	const ImU32 kTextDim   = IM_COL32(140, 140, 140, 255);
	const ImU32 kGood      = IM_COL32(120, 200, 120, 255);
	const ImU32 kBad       = IM_COL32(230, 110, 100, 255);

	std::string Lower(std::string s)
	{
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
		return s;
	}

	bool Matches(const PackageInfo& p, const std::string& q)
	{
		if (q.empty()) return true;
		if (Lower(p.DisplayName).find(q) != std::string::npos || Lower(p.Name).find(q) != std::string::npos) return true;
		for (const std::string& k : p.Keywords)
			if (Lower(k).find(q) != std::string::npos) return true;
		return false;
	}

	// 폭에 맞춰 줄바꿈한 글 (ImGui 기본 줄바꿈)
	void Wrapped(const std::string& text, float width, ImU32 color)
	{
		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
		ImGui::PushStyleColor(ImGuiCol_Text, color);
		ImGui::TextUnformatted(text.c_str());
		ImGui::PopStyleColor();
		ImGui::PopTextWrapPos();
	}
}

PackageManagerWindow::PackageManagerWindow()
	: EditorWindow("Package Manager", ICON_FA_BOX_OPEN)
{
	s_Instance = this;
	SetIsOpened(false);   // Window > Package Manager
}

void PackageManagerWindow::Open(const char* select)
{
	if (s_Instance == nullptr)
		return;
	PackageManager::Refresh();
	s_Instance->SetIsOpened(true);
	s_Instance->m_FocusNext = true;
	if (select)
		s_Instance->m_Selected = select;
}

void PackageManagerWindow::Close()
{
	if (s_Instance)
		s_Instance->SetIsOpened(false);
}

bool PackageManagerWindow::IsOpen() { return s_Instance && s_Instance->GetIsOpened(); }

void PackageManagerWindow::BeforeBegin()
{
	const ImGuiViewport* vp = ImGui::GetMainViewport();
	ImGui::SetNextWindowSize(ImVec2(900.0f, 560.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
	if (m_FocusNext)
	{
		ImGui::SetNextWindowFocus();
		m_FocusNext = false;
	}
}

void PackageManagerWindow::PushStyle() { ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0)); }
void PackageManagerWindow::PopStyle() { ImGui::PopStyleVar(); }

void PackageManagerWindow::OnRender()
{
	ImDrawList* dl = ImGui::GetWindowDrawList();
	const ImVec2 origin = ImGui::GetCursorScreenPos();
	const ImVec2 avail = ImGui::GetContentRegionAvail();
	const float toolbarH = 26.0f;
	const float footerH = PackageManager::RestartRequired() ? 26.0f : 0.0f;
	const float listW = (std::min)(300.0f, avail.x * 0.4f);

	// ---- 위 막대: 보기 · 새로 고침 · 검색 ----
	dl->AddRectFilled(origin, ImVec2(origin.x + avail.x, origin.y + toolbarH), kToolbarBg);
	dl->AddLine(ImVec2(origin.x, origin.y + toolbarH - 1), ImVec2(origin.x + avail.x, origin.y + toolbarH - 1), kLine);
	ImGui::SetCursorScreenPos(ImVec2(origin.x + 6.0f, origin.y + 3.0f));
	static const char* kViews[] = { "Packages: In Project", "Packages: NOVA Registry" };
	ImGui::SetNextItemWidth(190.0f);
	ImGui::Combo("##view", &m_View, kViews, 2);
	ImGui::SameLine();
	if (ImGui::Button(ICON_FA_ROTATE " Refresh"))
		PackageManager::Refresh();
	ImGui::SameLine();
	if (ImGui::Button(ICON_FA_PLUS " Add from disk..."))
	{
		// Unity: + > Add package from disk... (package.json 을 고른다)
		const std::wstring file = EditorUtility::OpenFileDialog(PathManager::GetI()->GetMovePathW(L""), L"Select package.json", std::vector<std::wstring>{ L"json" });
		if (!file.empty())
		{
			std::string error, name;
			m_LastError = PackageManager::AddFromDisk(file, error, &name) ? std::string() : error;
			if (!name.empty())
			{
				m_Selected = name;
				m_View = 0;
			}
		}
	}
	ImGui::SameLine(avail.x - 216.0f);
	ImGui::SetNextItemWidth(210.0f);
	ImGui::InputTextWithHint("##search", ICON_FA_MAGNIFYING_GLASS " Search", m_Search, sizeof(m_Search));

	// ---- 목록 ----
	std::vector<const PackageInfo*> rows;
	const std::string q = Lower(m_Search);
	if (m_View == 0)
	{
		for (const PackageInfo* p : PackageManager::InProject())
			if (Matches(*p, q)) rows.push_back(p);
	}
	else
	{
		for (const PackageInfo& p : PackageManager::Registry())
			if (Matches(p, q)) rows.push_back(&p);
	}
	if (m_Selected.empty() && !rows.empty())
		m_Selected = rows.front()->Name;

	const ImVec2 listMin(origin.x, origin.y + toolbarH);
	const ImVec2 listMax(origin.x + listW, origin.y + avail.y - footerH);
	dl->AddRectFilled(listMin, listMax, kListBg);
	dl->AddLine(ImVec2(listMax.x, listMin.y), ImVec2(listMax.x, listMax.y), kLine);
	ImGui::SetCursorScreenPos(listMin);
	ImGui::BeginChild("##pmlist", ImVec2(listW, listMax.y - listMin.y), false);
	{
		ImDrawList* ldl = ImGui::GetWindowDrawList();
		const float rowH = 24.0f;
		ImVec2 p = ImGui::GetCursorScreenPos();
		// 묶음 머리 (Unity: "Packages - Unity")
		ldl->AddText(UnityGUI::BoldFont(), ImGui::GetFontSize(), ImVec2(p.x + 8.0f, p.y + 5.0f), kTextDim, m_View == 0 ? "In Project" : "Packages - NOVA");
		ImGui::Dummy(ImVec2(listW, rowH));
		for (const PackageInfo* pk : rows)
		{
			p = ImGui::GetCursorScreenPos();
			ImGui::PushID(pk->Name.c_str());
			const bool clicked = ImGui::InvisibleButton("##row", ImVec2(listW, rowH));
			ImGui::PopID();
			if (clicked)
				m_Selected = pk->Name;
			const bool sel = m_Selected == pk->Name;
			if (sel || ImGui::IsItemHovered())
				ldl->AddRectFilled(p, ImVec2(p.x + listW, p.y + rowH), sel ? kRowSel : kRowHover);
			const bool inProject = PackageManager::IsInProject(pk->Name);
			const std::string err = PackageManager::LoadError(pk->Name);
			const char* mark = !err.empty() ? ICON_FA_TRIANGLE_EXCLAMATION : (inProject ? ICON_FA_CHECK : "");
			ldl->AddText(ImVec2(p.x + 8.0f, p.y + 5.0f), !err.empty() ? kBad : kGood, mark);
			ldl->AddText(ImVec2(p.x + 28.0f, p.y + 5.0f), kText, pk->DisplayName.c_str());
			const ImVec2 vs = ImGui::CalcTextSize(pk->Version.c_str());
			ldl->AddText(ImVec2(p.x + listW - vs.x - 10.0f, p.y + 5.0f), kTextDim, pk->Version.c_str());
		}
		if (rows.empty())
		{
			ImGui::SetCursorPosX(10.0f);
			ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), m_View == 0 ? "No packages in this project yet.\nSwitch to NOVA Registry to add one." : "No packages found.");
		}
	}
	ImGui::EndChild();

	// ---- 자세히 ----
	const ImVec2 detMin(listMax.x + 1.0f, listMin.y);
	const ImVec2 detMax(origin.x + avail.x, listMax.y);
	dl->AddRectFilled(detMin, detMax, kDetailBg);
	ImGui::SetCursorScreenPos(detMin);
	ImGui::BeginChild("##pmdetail", ImVec2(detMax.x - detMin.x, detMax.y - detMin.y), false);
	if (const PackageInfo* p = PackageManager::Find(m_Selected))
	{
		const float pad = 16.0f;
		const float w = detMax.x - detMin.x - pad * 2.0f;
		const bool inProject = PackageManager::IsInProject(p->Name);
		ImGui::SetCursorPos(ImVec2(pad, 14.0f));
		ImGui::PushFont(UnityGUI::BoldFont());
		ImGui::SetWindowFontScale(1.45f);
		ImGui::TextUnformatted(p->DisplayName.c_str());
		ImGui::SetWindowFontScale(1.0f);
		ImGui::PopFont();

		// 오른쪽 위: Install / Remove
		{
			const char* label = inProject ? (p->Embedded ? "Embedded" : "Remove") : "Install";
			const float bw = 90.0f;
			ImGui::SetCursorPos(ImVec2(detMax.x - detMin.x - pad - bw, 14.0f));
			ImGui::BeginDisabled(p->Embedded || Application::IsPlaying());
			if (ImGui::Button(label, ImVec2(bw, 24.0f)))
			{
				std::string error;
				const bool ok = inProject ? PackageManager::Remove(p->Name, error) : PackageManager::Add(p->Name, error);
				m_LastError = ok ? std::string() : error;
			}
			ImGui::EndDisabled();
		}

		ImGui::SetCursorPosX(pad);
		ImGui::TextColored(ImVec4(0.65f, 0.65f, 0.65f, 1.0f), "Version %s  ·  %s", p->Version.c_str(), p->Embedded ? "Embedded in project" : (p->Local ? "Local (from disk)" : "NOVA Registry"));
		ImGui::SetCursorPosX(pad);
		ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.55f, 1.0f), "%s", p->Name.c_str());
		if (!p->Author.empty())
		{
			ImGui::SetCursorPosX(pad);
			ImGui::TextColored(ImVec4(0.65f, 0.65f, 0.65f, 1.0f), "By %s", p->Author.c_str());
		}
		ImGui::Dummy(ImVec2(0, 6));
		ImGui::SetCursorPosX(pad);
		Wrapped(p->Description, w, kText);

		// 상태
		ImGui::Dummy(ImVec2(0, 8));
		ImGui::SetCursorPosX(pad);
		const std::string err = PackageManager::LoadError(p->Name);
		if (!err.empty())
			Wrapped(std::string(ICON_FA_TRIANGLE_EXCLAMATION " Failed to load: ") + err, w, kBad);
		else if (PackageManager::IsLoaded(p->Name))
			ImGui::TextColored(ImVec4(0.47f, 0.78f, 0.47f, 1.0f), ICON_FA_CHECK " In this project — loaded");
		else if (inProject)
			ImGui::TextColored(ImVec4(0.9f, 0.75f, 0.4f, 1.0f), "In this project — not loaded (restart the editor)");
		else
			ImGui::TextColored(ImVec4(0.65f, 0.65f, 0.65f, 1.0f), "Not in this project. Install adds it to Packages/manifest.json — only installed packages are loaded and included in builds.");
		if (!m_LastError.empty())
		{
			ImGui::SetCursorPosX(pad);
			Wrapped(m_LastError, w, kBad);
		}

		// 컴포넌트 · 키워드
		if (!p->Components.empty())
		{
			ImGui::Dummy(ImVec2(0, 8));
			ImGui::SetCursorPosX(pad);
			ImGui::PushFont(UnityGUI::BoldFont());
			ImGui::TextUnformatted("Components");
			ImGui::PopFont();
			for (const PackageComponentInfo& c : p->Components)
			{
				ImGui::SetCursorPosX(pad + 8.0f);
				ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "%s", c.Display.c_str());
				ImGui::SameLine();
				ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.55f, 1.0f), "  Add Component > %s", c.Category.c_str());
			}
		}
		if (!p->Dependencies.empty())
		{
			ImGui::Dummy(ImVec2(0, 8));
			ImGui::SetCursorPosX(pad);
			std::string deps;
			for (const auto& d : p->Dependencies) deps += (deps.empty() ? "" : ", ") + d.first + " " + d.second;
			ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.55f, 1.0f), "Dependencies: %s", deps.c_str());
		}
		if (!p->Native.empty())
		{
			ImGui::Dummy(ImVec2(0, 8));
			ImGui::SetCursorPosX(pad);
			std::string libs;
			for (const std::string& n : p->Native) libs += (libs.empty() ? "" : ", ") + n;
			ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.55f, 1.0f), "Native: %s", libs.c_str());
		}
		ImGui::Dummy(ImVec2(0, 4));
		ImGui::SetCursorPosX(pad);
		ImGui::TextColored(ImVec4(0.45f, 0.45f, 0.45f, 1.0f), "%s", wstring_to_string(p->Folder).c_str());
	}
	ImGui::EndChild();

	// ---- 아래 안내 ----
	if (footerH > 0.0f)
	{
		const ImVec2 f0(origin.x, origin.y + avail.y - footerH);
		dl->AddRectFilled(f0, ImVec2(origin.x + avail.x, origin.y + avail.y), IM_COL32(90, 70, 30, 255));
		dl->AddText(ImVec2(f0.x + 10.0f, f0.y + 5.0f), IM_COL32(240, 220, 170, 255),
			ICON_FA_TRIANGLE_EXCLAMATION "  A removed package is still used by the open scene. Restart the editor to unload it.");
	}
}
