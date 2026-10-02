#include "pch.h"
#include "PackageManagerWindow.h"
#include "PackageManager.h"
#include "UnityGUI.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <shellapi.h>

PackageManagerWindow* PackageManagerWindow::s_Instance = nullptr;

namespace
{
	namespace fs = std::filesystem;

	// Unity 6 Package Manager 색
	const ImU32 kToolbarBg = IM_COL32(60, 60, 60, 255);
	const ImU32 kSidebarBg = IM_COL32(45, 45, 45, 255);
	const ImU32 kListBg    = IM_COL32(49, 49, 49, 255);
	const ImU32 kDetailBg  = IM_COL32(56, 56, 56, 255);
	const ImU32 kBoxBg     = IM_COL32(48, 48, 48, 255);
	const ImU32 kLine      = IM_COL32(28, 28, 28, 255);
	const ImU32 kRowSel    = IM_COL32(44, 93, 135, 255);
	const ImU32 kRowHover  = IM_COL32(64, 64, 64, 255);
	const ImU32 kSideSel   = IM_COL32(72, 72, 72, 255);
	const ImU32 kText      = IM_COL32(210, 210, 210, 255);
	const ImU32 kTextBright = IM_COL32(235, 235, 235, 255);
	const ImU32 kTextDim   = IM_COL32(150, 150, 150, 255);
	const ImU32 kLink      = IM_COL32(76, 126, 255, 255);
	const ImU32 kGood      = IM_COL32(120, 200, 120, 255);
	const ImU32 kUpdate    = IM_COL32(90, 160, 255, 255);
	const ImU32 kBad       = IM_COL32(230, 110, 100, 255);
	const ImU32 kTabLine   = IM_COL32(58, 121, 187, 255);

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

	bool IsFeature(const PackageInfo& p) { return p.Type == "feature"; }

	// Feature 는 묶인 패키지가 다 들어 있으면 "설치됨" (Unity 와 같음)
	bool Installed(const PackageInfo& p)
	{
		if (PackageManager::IsInProject(p.Name))
			return true;
		if (!IsFeature(p) || p.Dependencies.empty())
			return false;
		for (const auto& d : p.Dependencies)
			if (!PackageManager::IsInProject(d.first))
				return false;
		return true;
	}

	// 폭에 맞춰 줄바꿈한 글
	void Wrapped(const std::string& text, float width, ImU32 color)
	{
		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
		ImGui::PushStyleColor(ImGuiCol_Text, color);
		ImGui::TextUnformatted(text.c_str());
		ImGui::PopStyleColor();
		ImGui::PopTextWrapPos();
	}

	// 굵은 글자 (big = 제목 글꼴 22px — 키운 비트맵이 아니라 그 크기로 구운 글꼴)
	void Bold(const char* text, bool big = false, ImU32 color = kTextBright)
	{
		ImGui::PushFont(big ? UnityGUI::TitleFont() : UnityGUI::BoldFont());
		ImGui::PushStyleColor(ImGuiCol_Text, color);
		ImGui::TextUnformatted(text);
		ImGui::PopStyleColor();
		ImGui::PopFont();
	}

	// 파란 글자 링크 (없으면 흐리게). 눌렀으면 true
	bool Link(const char* label, bool enabled)
	{
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const ImVec2 size = ImGui::CalcTextSize(label);
		ImGui::PushID(label);
		const bool clicked = ImGui::InvisibleButton("##link", size) && enabled;
		const bool hover = ImGui::IsItemHovered() && enabled;
		ImGui::PopID();
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddText(p, enabled ? kLink : IM_COL32(100, 100, 100, 255), label);
		if (hover)
		{
			dl->AddLine(ImVec2(p.x, p.y + size.y), ImVec2(p.x + size.x, p.y + size.y), kLink);
			ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
		}
		return clicked;
	}

	void OpenPath(const std::wstring& path)
	{
		::ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	}

	std::string ReadFile(const std::wstring& path, size_t limit)
	{
		std::ifstream in(path, std::ios::binary);
		std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		if (s.size() > limit) s = s.substr(0, limit) + "\n...";
		return s;
	}

	// 패키지 DLL 이 빌드된 ABI (<DLL>.dll.abi)
	std::string AbiOf(const PackageInfo& p)
	{
		if (p.Native.empty())
			return std::string();
		std::string s = ReadFile((fs::path(p.Folder) / L"Plugins" / (string_to_wstring(p.Native[0]) + L".abi")).wstring(), 128);
		while (!s.empty() && (unsigned char)s.back() <= ' ') s.pop_back();
		return s;
	}

	// 아래 꺾쇠가 붙은 막대 버튼 (Unity: "+ ▾", "Sort: Name ▾")
	bool DropButton(const char* id, const char* label, float width)
	{
		ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(60, 60, 60, 255));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(78, 78, 78, 255));
		const bool r = ImGui::Button((std::string(label) + "  " ICON_FA_CARET_DOWN "##" + id).c_str(), ImVec2(width, 22.0f));
		ImGui::PopStyleColor(2);
		return r;
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
	ImGui::SetNextWindowSize(ImVec2(1100.0f, 640.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
	if (m_FocusNext)
	{
		ImGui::SetNextWindowFocus();
		m_FocusNext = false;
	}
}

void PackageManagerWindow::PushStyle() { ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0)); }
void PackageManagerWindow::PopStyle() { ImGui::PopStyleVar(); }

// 보기 · 검색 · 필터 · 정렬을 거친 목록 (features = Feature 묶음만 / 아니면 일반 패키지만)
std::vector<const PackageInfo*> PackageManagerWindow::Rows(bool features) const
{
	std::vector<const PackageInfo*> src;
	if (m_View == InProjectView)
	{
		src = PackageManager::InProject();
		for (const PackageInfo& p : PackageManager::Registry())
			if (IsFeature(p) && Installed(p) && std::find(src.begin(), src.end(), &p) == src.end())
				src.push_back(&p);
	}
	else
		for (const PackageInfo& p : PackageManager::Registry())
			if (m_View == RegistryView || PackageManager::HasUpdate(p.Name))
				src.push_back(&p);
	const std::string q = Lower(m_Search);
	std::vector<const PackageInfo*> out;
	for (const PackageInfo* p : src)
	{
		if (IsFeature(*p) != features || !Matches(*p, q))
			continue;
		if (m_Status == 1 && !Installed(*p)) continue;
		if (m_Status == 2 && Installed(*p)) continue;
		if (!m_Categories.empty() && !m_Categories.count(p->Category)) continue;
		out.push_back(p);
	}
	std::sort(out.begin(), out.end(), [&](const PackageInfo* a, const PackageInfo* b) {
		if (m_Sort == 2 && a->Date != b->Date) return a->Date > b->Date;
		const int c = _stricmp(a->DisplayName.c_str(), b->DisplayName.c_str());
		return m_Sort == 1 ? c > 0 : c < 0;
	});
	return out;
}

void PackageManagerWindow::OnRender()
{
	ImDrawList* dl = ImGui::GetWindowDrawList();
	const ImVec2 origin = ImGui::GetCursorScreenPos();
	const ImVec2 avail = ImGui::GetContentRegionAvail();
	const float toolbarH = 30.0f;
	const float footerH = PackageManager::RestartRequired() ? 26.0f : 0.0f;
	const float sideW = (std::min)(180.0f, avail.x * 0.18f);
	const float listW = (std::min)(340.0f, avail.x * 0.32f);
	const float bottom = origin.y + avail.y - footerH;

	DrawToolbar(dl, origin, avail.x, toolbarH);
	DrawSidebar(dl, ImVec2(origin.x, origin.y + toolbarH), ImVec2(origin.x + sideW, bottom));
	DrawList(dl, ImVec2(origin.x + sideW + 1.0f, origin.y + toolbarH), ImVec2(origin.x + sideW + 1.0f + listW, bottom));
	DrawDetail(dl, ImVec2(origin.x + sideW + listW + 2.0f, origin.y + toolbarH), ImVec2(origin.x + avail.x, bottom));

	// ---- 아래 안내 ----
	if (footerH > 0.0f)
	{
		const ImVec2 f0(origin.x, bottom);
		dl->AddRectFilled(f0, ImVec2(origin.x + avail.x, origin.y + avail.y), IM_COL32(90, 70, 30, 255));
		dl->AddText(ImVec2(f0.x + 10.0f, f0.y + 5.0f), IM_COL32(240, 220, 170, 255),
			ICON_FA_TRIANGLE_EXCLAMATION "  A removed package is still used by the open scene. Restart the editor to unload it.");
	}
}

// ------------------------------------------------------------------ 위 막대
void PackageManagerWindow::DrawToolbar(ImDrawList* dl, ImVec2 origin, float width, float height)
{
	dl->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), kToolbarBg);
	dl->AddLine(ImVec2(origin.x, origin.y + height - 1), ImVec2(origin.x + width, origin.y + height - 1), kLine);
	ImGui::SetCursorScreenPos(ImVec2(origin.x + 6.0f, origin.y + 4.0f));

	// + ▾ : Install package from disk...
	if (DropButton("add", ICON_FA_PLUS, 44.0f))
		ImGui::OpenPopup("##pmAdd");
	if (ImGui::BeginPopup("##pmAdd"))
	{
		if (ImGui::MenuItem("Install package from disk..."))
		{
			const std::wstring file = EditorUtility::OpenFileDialog(PathManager::GetI()->GetMovePathW(L""), L"Select package.json", std::vector<std::wstring>{ L"json" });
			if (!file.empty())
			{
				std::string error, name;
				m_LastError = PackageManager::AddFromDisk(file, error, &name) ? std::string() : error;
				if (!name.empty())
				{
					m_Selected = name;
					m_View = InProjectView;
				}
			}
		}
		ImGui::EndPopup();
	}

	// Sort ▾
	static const char* kSorts[] = { "Name (asc)", "Name (desc)", "Published date" };
	ImGui::SameLine(0, 10.0f);
	if (DropButton("sort", (std::string("Sort: ") + kSorts[m_Sort]).c_str(), 170.0f))
		ImGui::OpenPopup("##pmSort");
	if (ImGui::BeginPopup("##pmSort"))
	{
		for (int i = 0; i < 3; ++i)
			if (ImGui::MenuItem(kSorts[i], nullptr, m_Sort == i)) m_Sort = i;
		ImGui::EndPopup();
	}

	// Filters ▾
	const bool filtered = m_Status != 0 || !m_Categories.empty();
	ImGui::SameLine(0, 6.0f);
	if (DropButton("filters", filtered ? ICON_FA_FILTER " Filters (on)" : "Filters", 110.0f))
		ImGui::OpenPopup("##pmFilters");
	if (ImGui::BeginPopup("##pmFilters"))
	{
		ImGui::TextDisabled("Status");
		static const char* kStatus[] = { "All", "Installed", "Not installed" };
		for (int i = 0; i < 3; ++i)
			if (ImGui::RadioButton(kStatus[i], m_Status == i)) m_Status = i;
		ImGui::Separator();
		ImGui::TextDisabled("Categories");
		std::set<std::string> cats;
		for (const PackageInfo& p : PackageManager::Registry())
			if (!p.Category.empty()) cats.insert(p.Category);
		for (const std::string& c : cats)
		{
			bool on = m_Categories.count(c) > 0;
			if (ImGui::Checkbox(c.c_str(), &on))
			{
				if (on) m_Categories.insert(c);
				else m_Categories.erase(c);
			}
		}
		ImGui::EndPopup();
	}

	ImGui::SameLine(0, 6.0f);
	ImGui::BeginDisabled(!filtered);
	if (ImGui::Button("Clear Filters", ImVec2(0, 22.0f)))
	{
		m_Status = 0;
		m_Categories.clear();
	}
	ImGui::EndDisabled();
}

// ------------------------------------------------------------------ 왼쪽: 보기
void PackageManagerWindow::DrawSidebar(ImDrawList* dl, ImVec2 min, ImVec2 max)
{
	dl->AddRectFilled(min, max, kSidebarBg);
	dl->AddLine(ImVec2(max.x, min.y), ImVec2(max.x, max.y), kLine);
	int updates = 0;
	for (const PackageInfo& p : PackageManager::Registry())
		updates += PackageManager::HasUpdate(p.Name) ? 1 : 0;
	struct Item { const char* Icon; const char* Label; int View; int Badge; };
	const Item items[] = {
		{ ICON_FA_FOLDER_OPEN, "In Project", InProjectView, 0 },
		{ ICON_FA_CIRCLE_ARROW_UP, "Updates", UpdatesView, updates },
		{ ICON_FA_BOX, "NOVA Registry", RegistryView, 0 },
	};
	const float rowH = 28.0f;
	float y = min.y + 6.0f;
	for (const Item& it : items)
	{
		const ImVec2 r0(min.x, y), r1(max.x, y + rowH);
		ImGui::SetCursorScreenPos(r0);
		ImGui::PushID(it.Label);
		if (ImGui::InvisibleButton("##side", ImVec2(max.x - min.x, rowH)))
		{
			m_View = it.View;
			m_Selected.clear();
		}
		const bool hover = ImGui::IsItemHovered();
		ImGui::PopID();
		const bool sel = m_View == it.View;
		if (sel || hover)
			dl->AddRectFilled(r0, r1, sel ? kSideSel : kRowHover);
		if (sel)
			dl->AddRectFilled(r0, ImVec2(r0.x + 3.0f, r1.y), kTabLine);
		dl->AddText(ImVec2(r0.x + 14.0f, r0.y + 7.0f), sel ? kTextBright : kTextDim, it.Icon);
		dl->AddText(ImVec2(r0.x + 38.0f, r0.y + 7.0f), sel ? kTextBright : kText, it.Label);
		if (it.Badge > 0)
		{
			char b[8];
			snprintf(b, sizeof(b), "%d", it.Badge);
			const ImVec2 bs = ImGui::CalcTextSize(b);
			const ImVec2 c0(r1.x - bs.x - 22.0f, r0.y + 6.0f);
			dl->AddRectFilled(c0, ImVec2(c0.x + bs.x + 12.0f, c0.y + 16.0f), kUpdate, 8.0f);
			dl->AddText(ImVec2(c0.x + 6.0f, c0.y + 1.0f), IM_COL32_WHITE, b);
		}
		y += rowH;
	}
}

// ------------------------------------------------------------------ 가운데: 검색 + Features + Packages
void PackageManagerWindow::DrawList(ImDrawList* dl, ImVec2 min, ImVec2 max)
{
	const float w = max.x - min.x;
	const float footH = 26.0f;
	dl->AddRectFilled(min, max, kListBg);
	dl->AddLine(ImVec2(max.x, min.y), ImVec2(max.x, max.y), kLine);

	// 검색
	ImGui::SetCursorScreenPos(ImVec2(min.x + 8.0f, min.y + 7.0f));
	ImGui::SetNextItemWidth(w - 16.0f);
	ImGui::InputTextWithHint("##pmsearch", ICON_FA_MAGNIFYING_GLASS "  Search", m_Search, sizeof(m_Search));

	const std::vector<const PackageInfo*> features = Rows(true);
	const std::vector<const PackageInfo*> packages = Rows(false);
	if (m_Selected.empty() || (!PackageManager::Find(m_Selected)))
	{
		if (!features.empty()) m_Selected = features.front()->Name;
		else if (!packages.empty()) m_Selected = packages.front()->Name;
	}

	const float top = min.y + 38.0f;
	ImGui::SetCursorScreenPos(ImVec2(min.x, top));
	ImGui::BeginChild("##pmlist", ImVec2(w, max.y - top - footH), false);
	ImDrawList* ldl = ImGui::GetWindowDrawList();

	// 접는 머리 (▾ Features)
	auto header = [&](const char* label, bool* open) {
		const ImVec2 p = ImGui::GetCursorScreenPos();
		ImGui::PushID(label);
		if (ImGui::InvisibleButton("##hdr", ImVec2(w, 24.0f)))
			*open = !*open;
		ImGui::PopID();
		ldl->AddText(ImVec2(p.x + 8.0f, p.y + 5.0f), kTextDim, *open ? ICON_FA_CHEVRON_DOWN : ICON_FA_CHEVRON_RIGHT);
		ldl->AddText(UnityGUI::BoldFont(), ImGui::GetFontSize(), ImVec2(p.x + 26.0f, p.y + 5.0f), kText, label);
	};
	// 행 배경 · 선택
	auto row = [&](const PackageInfo* pk, float h) {
		const ImVec2 p = ImGui::GetCursorScreenPos();
		ImGui::PushID(pk->Name.c_str());
		if (ImGui::InvisibleButton("##row", ImVec2(w, h)))
		{
			m_Selected = pk->Name;
			m_LastError.clear();
		}
		const bool hover = ImGui::IsItemHovered();
		ImGui::PopID();
		const bool sel = m_Selected == pk->Name;
		if (sel || hover)
			ldl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), sel ? kRowSel : kRowHover);
		return p;
	};
	// 오른쪽 상태 아이콘: ⚠ 오류, ↑ 업데이트, ✓ 설치됨
	auto status = [&](const PackageInfo* pk, ImVec2 p, float h) {
		const std::string err = PackageManager::LoadError(pk->Name);
		const char* icon = !err.empty() ? ICON_FA_TRIANGLE_EXCLAMATION : (PackageManager::HasUpdate(pk->Name) ? ICON_FA_CIRCLE_ARROW_UP : (Installed(*pk) ? ICON_FA_CHECK : ""));
		const ImU32 col = !err.empty() ? kBad : (PackageManager::HasUpdate(pk->Name) ? kUpdate : kGood);
		ldl->AddText(ImVec2(p.x + w - 24.0f, p.y + (h - ImGui::GetFontSize()) * 0.5f), col, icon);
	};

	if (!features.empty())
	{
		header("Features", &m_FeaturesOpen);
		if (m_FeaturesOpen)
			for (const PackageInfo* f : features)
			{
				const float h = 42.0f;
				const ImVec2 p = row(f, h);
				ldl->AddText(ImVec2(p.x + 12.0f, p.y + 13.0f), kTextDim, ICON_FA_CUBES);
				ldl->AddText(ImVec2(p.x + 36.0f, p.y + 5.0f), kTextBright, f->DisplayName.c_str());
				char sub[32];
				snprintf(sub, sizeof(sub), "%d packages", (int)f->Dependencies.size());
				ldl->AddText(ImVec2(p.x + 36.0f, p.y + 22.0f), kTextDim, sub);
				status(f, p, h);
			}
	}
	header("Packages", &m_PackagesOpen);
	if (m_PackagesOpen)
		for (const PackageInfo* pk : packages)
		{
			const float h = 26.0f;
			const ImVec2 p = row(pk, h);
			ldl->AddText(ImVec2(p.x + 26.0f, p.y + 5.0f), kText, pk->DisplayName.c_str());
			// 프로젝트 버전 (업데이트가 있으면 지금 버전 → 새 버전)
			std::string ver = pk->Version;
			const std::string have = PackageManager::ManifestVersion(pk->Name);
			if (PackageManager::HasUpdate(pk->Name))
				ver = have + "  " ICON_FA_ARROW_RIGHT "  " + pk->Version;
			const ImVec2 vs = ImGui::CalcTextSize(ver.c_str());
			ldl->AddText(ImVec2(p.x + w - vs.x - 34.0f, p.y + 5.0f), kTextDim, ver.c_str());
			status(pk, p, h);
		}
	if (features.empty() && packages.empty())
	{
		ImGui::SetCursorPosX(12.0f);
		ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s", m_View == UpdatesView ? "All packages are up to date." :
			(m_View == InProjectView ? "No packages in this project yet.\nOpen NOVA Registry to install one." : "No packages found."));
	}
	ImGui::EndChild();

	// 아래: 마지막 새로 고침 + 새로 고침 버튼
	const ImVec2 f0(min.x, max.y - footH);
	dl->AddLine(f0, ImVec2(max.x, f0.y), kLine);
	const std::string last = "Last refresh: " + PackageManager::LastRefresh();
	dl->AddText(ImVec2(f0.x + 10.0f, f0.y + 6.0f), kTextDim, last.c_str());
	ImGui::SetCursorScreenPos(ImVec2(max.x - 30.0f, f0.y + 2.0f));
	ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
	if (ImGui::Button(ICON_FA_ROTATE "##pmrefresh", ImVec2(24.0f, 22.0f)))
		PackageManager::Refresh();
	ImGui::PopStyleColor();
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Refresh list");
}

// ------------------------------------------------------------------ 오른쪽: 자세히
void PackageManagerWindow::DrawDetail(ImDrawList* dl, ImVec2 min, ImVec2 max)
{
	dl->AddRectFilled(min, max, kDetailBg);
	ImGui::SetCursorScreenPos(min);
	ImGui::BeginChild("##pmdetail", ImVec2(max.x - min.x, max.y - min.y), false);
	const PackageInfo* p = PackageManager::Find(m_Selected);
	if (p == nullptr)
	{
		ImGui::EndChild();
		return;
	}
	const float pad = 18.0f;
	const float W = max.x - min.x;
	const float w = W - pad * 2.0f;
	const bool feature = IsFeature(*p);
	const bool inProject = PackageManager::IsInProject(p->Name);
	const bool installed = Installed(*p);
	const bool update = PackageManager::HasUpdate(p->Name);
	const bool busy = Application::IsPlaying();

	// ---- 머리: 이름 · 버전 · 출처 ----
	ImGui::SetCursorPos(ImVec2(pad, 14.0f));
	Bold(p->DisplayName.c_str(), true);
	ImGui::Dummy(ImVec2(0, 1));
	ImGui::SetCursorPosX(pad);
	{
		std::string line = feature ? std::string("Feature") : p->Version;
		if (!p->Date.empty()) line += "  " + std::string("\xc2\xb7") + "  " + p->Date;
		if (update) line += "   (installed " + PackageManager::ManifestVersion(p->Name) + ")";
		ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", line.c_str());
	}
	ImGui::SetCursorPosX(pad);
	{
		const std::string by = p->Author.empty() ? std::string() : " by " + p->Author;
		const char* from = p->Embedded ? "Embedded in this project" : (p->Local ? "From disk" : "From NOVA Registry");
		ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s%s", from, by.c_str());
	}

	// 링크
	ImGui::Dummy(ImVec2(0, 2));
	ImGui::SetCursorPosX(pad);
	if (Link("Documentation", true))
	{
		if (!p->Documentation.empty()) ::ShellExecuteA(nullptr, "open", p->Documentation.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
		else OpenPath(PathManager::GetI()->GetEnginePathW() + L"docs\\PACKAGES.md");   // 공식 패키지 안내
	}
	ImGui::SameLine(0, 16.0f);
	if (Link("Changelog", !p->Changelog.empty())) OpenPath(p->Changelog);
	ImGui::SameLine(0, 16.0f);
	if (Link("Licenses", !p->License.empty())) OpenPath(p->License);
	const float afterHeader = ImGui::GetCursorPosY();   // 오른쪽 위 버튼을 그린 뒤 여기로 돌아온다

	// 오른쪽 위 버튼: Install / Update to x / Locate · Manage ▾
	{
		float x = W - pad;
		const float bh = 24.0f;
		auto button = [&](const char* label, float bw, bool primary) {
			x -= bw;
			ImGui::SetCursorPos(ImVec2(x, 16.0f));
			x -= 6.0f;
			if (primary)
			{
				ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(52, 105, 170, 255));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(64, 125, 200, 255));
			}
			const bool r = ImGui::Button(label, ImVec2(bw, bh));
			if (primary) ImGui::PopStyleColor(2);
			return r;
		};
		ImGui::BeginDisabled(busy);
		if (installed && !p->Embedded)
		{
			if (button("Manage  " ICON_FA_CARET_DOWN, 92.0f, false))
				ImGui::OpenPopup("##pmManage");
		}
		if (installed && !feature)
		{
			if (button("Locate", 70.0f, false))
				OpenPath(p->Folder);
		}
		if (update)
		{
			const std::string label = "Update to " + p->Version;
			if (button(label.c_str(), ImGui::CalcTextSize(label.c_str()).x + 24.0f, true))
			{
				std::string error;
				m_LastError = PackageManager::Add(p->Name, error) ? std::string() : error;
			}
		}
		if (!installed)
		{
			if (button("Install", 84.0f, true))
			{
				std::string error;
				m_LastError = PackageManager::Add(p->Name, error) ? std::string() : error;
			}
		}
		ImGui::EndDisabled();
		if (ImGui::BeginPopup("##pmManage"))
		{
			if (ImGui::MenuItem(feature ? "Remove feature (keep its packages)" : "Remove", nullptr, false, inProject))
			{
				std::string error;
				m_LastError = PackageManager::Remove(p->Name, error) ? std::string() : error;
			}
			if (!feature && ImGui::MenuItem("Show in Explorer"))
				OpenPath(p->Folder);
			ImGui::EndPopup();
		}
	}

	// ---- 탭 ----
	ImGui::SetCursorPos(ImVec2(0.0f, afterHeader + 12.0f));
	const char* tabs[] = { "Details", "Version History", "Dependencies", feature ? "Packages" : "Components" };
	{
		const ImVec2 p0 = ImGui::GetCursorScreenPos();
		ImDrawList* ddl = ImGui::GetWindowDrawList();
		float x = p0.x + pad;
		for (int i = 0; i < 4; ++i)
		{
			const ImVec2 ts = ImGui::CalcTextSize(tabs[i]);
			ImGui::SetCursorScreenPos(ImVec2(x, p0.y));
			ImGui::PushID(i);
			if (ImGui::InvisibleButton("##tab", ImVec2(ts.x + 20.0f, 28.0f)))
				m_Tab = i;
			const bool hover = ImGui::IsItemHovered();
			ImGui::PopID();
			const bool sel = m_Tab == i;
			ddl->AddText(ImVec2(x + 10.0f, p0.y + 6.0f), sel ? kTextBright : (hover ? kText : kTextDim), tabs[i]);
			if (sel)
				ddl->AddRectFilled(ImVec2(x + 4.0f, p0.y + 25.0f), ImVec2(x + ts.x + 16.0f, p0.y + 27.0f), kTabLine);
			x += ts.x + 24.0f;
		}
		ddl->AddLine(ImVec2(p0.x + pad, p0.y + 27.0f), ImVec2(p0.x + W - pad, p0.y + 27.0f), IM_COL32(80, 80, 80, 255));
		ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + 36.0f));
	}

	// 정보 상자의 한 줄: 이름  값 [복사]
	auto boxRow = [&](const char* label, const std::string& value, bool copy, ImU32 color) {
		ImGui::SetCursorPosX(pad + 12.0f);
		ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s", label);
		ImGui::SameLine(pad + 180.0f);
		ImGui::PushStyleColor(ImGuiCol_Text, color);
		ImGui::TextUnformatted(value.c_str());
		ImGui::PopStyleColor();
		if (copy)
		{
			ImGui::SameLine(0, 8.0f);
			ImGui::PushID(label);
			ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
			if (ImGui::SmallButton(ICON_FA_COPY))
				ImGui::SetClipboardText(value.c_str());
			ImGui::PopStyleColor();
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Copy");
			ImGui::PopID();
		}
	};
	// 둥근 상자 (내용을 그린 뒤 뒤에 깐다)
	auto box = [&](auto&& body) {
		ImDrawList* ddl = ImGui::GetWindowDrawList();
		ddl->ChannelsSplit(2);
		ddl->ChannelsSetCurrent(1);
		const ImVec2 b0 = ImGui::GetCursorScreenPos();
		ImGui::Dummy(ImVec2(0, 6));
		body();
		ImGui::Dummy(ImVec2(0, 6));
		const float b1y = ImGui::GetCursorScreenPos().y;
		ddl->ChannelsSetCurrent(0);
		ddl->AddRectFilled(ImVec2(b0.x + pad, b0.y), ImVec2(b0.x + W - pad, b1y - 4.0f), kBoxBg, 4.0f);
		ddl->ChannelsMerge();
		ImGui::Dummy(ImVec2(0, 6));
	};

	switch (m_Tab)
	{
	case 0:   // Details
	{
		box([&] {
			boxRow("Technical Name", p->Name, true, kText);
			if (!feature)
			{
				const std::string abi = AbiOf(*p);
				if (p->Native.empty())
					boxRow("Binary", "C# only (no native DLL)", false, kText);
				else if (abi.empty())
					boxRow("Binary", p->Native[0] + " (not built)", false, kBad);
				else
					boxRow("Binary", abi + (abi == NOVA_PACKAGE_ABI_VERSION ? "  " ICON_FA_CIRCLE_CHECK : "  (different engine build)"), false,
						abi == NOVA_PACKAGE_ABI_VERSION ? kGood : kBad);
			}
			boxRow("Minimum Editor Version", p->MinEditor.empty() ? std::string("Any") : p->MinEditor, false, kText);
			if (!p->Category.empty())
				boxRow("Category", p->Category, false, kText);
		});
		ImGui::SetCursorPosX(pad);
		Wrapped(p->Description, w, kText);
		ImGui::Dummy(ImVec2(0, 8));
		// 상태
		ImGui::SetCursorPosX(pad);
		const std::string err = PackageManager::LoadError(p->Name);
		if (!err.empty())
			Wrapped(std::string(ICON_FA_TRIANGLE_EXCLAMATION " Failed to load: ") + err, w, kBad);
		else if (feature)
			ImGui::TextColored(ImVec4(0.65f, 0.65f, 0.65f, 1.0f), "%s", installed ? ICON_FA_CHECK " All packages of this feature are in the project" : "Install adds every package of this feature to the project.");
		else if (PackageManager::IsLoaded(p->Name))
			ImGui::TextColored(ImVec4(0.47f, 0.78f, 0.47f, 1.0f), ICON_FA_CHECK " In this project, loaded");
		else if (inProject)
			ImGui::TextColored(ImVec4(0.9f, 0.75f, 0.4f, 1.0f), "In this project, not loaded (restart the editor)");
		else
			Wrapped("Not in this project. Install adds it to Packages/manifest.json; only installed packages are loaded and included in builds.", w, IM_COL32(165, 165, 165, 255));
		if (!m_LastError.empty())
		{
			ImGui::SetCursorPosX(pad);
			Wrapped(m_LastError, w, kBad);
		}
		// 키워드
		if (!p->Keywords.empty())
		{
			ImGui::Dummy(ImVec2(0, 8));
			ImGui::SetCursorPosX(pad);
			ImDrawList* ddl = ImGui::GetWindowDrawList();
			float x = 0.0f;
			for (const std::string& k : p->Keywords)
			{
				const ImVec2 ks = ImGui::CalcTextSize(k.c_str());
				if (x > 0.0f && x + ks.x + 16.0f > w) { ImGui::NewLine(); ImGui::SetCursorPosX(pad); x = 0.0f; }
				if (x > 0.0f) ImGui::SameLine(0, 6.0f);
				const ImVec2 c = ImGui::GetCursorScreenPos();
				ddl->AddRectFilled(c, ImVec2(c.x + ks.x + 14.0f, c.y + ks.y + 6.0f), IM_COL32(70, 70, 70, 255), 9.0f);
				ddl->AddText(ImVec2(c.x + 7.0f, c.y + 3.0f), kText, k.c_str());
				ImGui::Dummy(ImVec2(ks.x + 14.0f, ks.y + 6.0f));
				x += ks.x + 20.0f;
			}
		}
		ImGui::Dummy(ImVec2(0, 8));
		ImGui::SetCursorPosX(pad);
		ImGui::TextColored(ImVec4(0.45f, 0.45f, 0.45f, 1.0f), "%s", wstring_to_string(p->Folder).c_str());
		break;
	}
	case 1:   // Version History
	{
		box([&] {
			ImGui::SetCursorPosX(pad + 12.0f);
			Bold(p->Version.c_str());
			ImGui::SameLine(0, 12.0f);
			ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s", p->Date.c_str());
			ImGui::SameLine(0, 12.0f);
			const std::string have = PackageManager::ManifestVersion(p->Name);
			if (installed && (have.empty() || have == p->Version))
				ImGui::TextColored(ImVec4(0.47f, 0.78f, 0.47f, 1.0f), "Installed");
			else if (!have.empty())
				ImGui::TextColored(ImVec4(0.35f, 0.63f, 1.0f, 1.0f), "Installed: %s", have.c_str());
		});
		ImGui::SetCursorPosX(pad);
		if (!p->Changelog.empty())
			Wrapped(ReadFile(p->Changelog, 6000), w, kText);
		else
			ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.55f, 1.0f), "No changelog (CHANGELOG.md) in this package.");
		break;
	}
	case 2:   // Dependencies
	{
		ImGui::SetCursorPosX(pad);
		Bold("Is using");
		box([&] {
			if (p->Dependencies.empty())
			{
				ImGui::SetCursorPosX(pad + 12.0f);
				ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.55f, 1.0f), "No dependencies");
			}
			for (const auto& d : p->Dependencies)
			{
				const PackageInfo* dp = PackageManager::Find(d.first);
				const bool have = PackageManager::IsInProject(d.first);
				boxRow(dp ? dp->DisplayName.c_str() : d.first.c_str(), d.second + (have ? "   " ICON_FA_CHECK " installed" : "   not installed"), false, have ? kGood : kTextDim);
			}
		});
		ImGui::SetCursorPosX(pad);
		Bold("Used by");
		box([&] {
			int n = 0;
			for (const PackageInfo& o : PackageManager::Registry())
				for (const auto& d : o.Dependencies)
					if (d.first == p->Name)
					{
						boxRow(o.DisplayName.c_str(), o.Version + (Installed(o) ? "   " ICON_FA_CHECK " installed" : ""), false, kText);
						++n;
					}
			if (n == 0)
			{
				ImGui::SetCursorPosX(pad + 12.0f);
				ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.55f, 1.0f), "No other package uses this one");
			}
		});
		break;
	}
	default:   // Components (패키지) / Packages (Feature)
	{
		if (feature)
		{
			for (const auto& d : p->Dependencies)
			{
				const PackageInfo* dp = PackageManager::Find(d.first);
				ImGui::SetCursorPosX(pad);
				const bool have = PackageManager::IsInProject(d.first);
				ImGui::TextColored(have ? ImVec4(0.47f, 0.78f, 0.47f, 1.0f) : ImVec4(0.45f, 0.45f, 0.45f, 1.0f), "%s", have ? ICON_FA_CHECK : ICON_FA_BOX);
				ImGui::SameLine(0, 10.0f);
				if (Link(dp ? dp->DisplayName.c_str() : d.first.c_str(), dp != nullptr))
				{
					m_Selected = d.first;
					m_Tab = 0;
				}
				ImGui::SameLine(0, 10.0f);
				ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.55f, 1.0f), "%s  %s", d.first.c_str(), d.second.c_str());
			}
		}
		else if (p->Components.empty())
		{
			ImGui::SetCursorPosX(pad);
			ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.55f, 1.0f), "This package adds no components (C# API or assets only).");
		}
		else
			box([&] {
				for (const PackageComponentInfo& c : p->Components)
					boxRow(c.Display.c_str(), "Add Component > " + c.Category, false, kText);
			});
		break;
	}
	}
	ImGui::Dummy(ImVec2(0, 12));
	ImGui::EndChild();
}
