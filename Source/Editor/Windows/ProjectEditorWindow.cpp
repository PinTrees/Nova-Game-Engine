#include "pch.h"
#include "ProjectEditorWindow.h"
#include "Utils.h"
#include "EditorGUI.h"
#include "SceneManager.h"
#include "PathManager.h"
#include "AnimatorController.h"
#include "AnimatorEditorWindow.h"
#include "UndoSystem.h"
#include "UnityGUI.h"
#include "SkinnedMesh.h"
#include "VolumeProfile.h"
#include "AudioClip.h"
#include "AudioManager.h"
#include "ScriptEngine.h"
#include <shellapi.h>

std::wstring ProjectEditorWindow::solutionDirectory = L"";
std::wstring ProjectEditorWindow::currentDirectory = L"";

namespace
{
	// ---- Unity 다크 스킨 색 ----
	const ImU32 kBg        = IM_COL32(51, 51, 51, 255);
	const ImU32 kTreeBg    = IM_COL32(56, 56, 56, 255);
	const ImU32 kToolbar   = IM_COL32(60, 60, 60, 255);
	const ImU32 kLine      = IM_COL32(35, 35, 35, 255);
	const ImU32 kText      = IM_COL32(210, 210, 210, 255);
	const ImU32 kTextDim   = IM_COL32(150, 150, 150, 255);
	const ImU32 kSelFocus  = IM_COL32(44, 93, 135, 255);
	const ImU32 kSelIdle   = IM_COL32(77, 77, 77, 255);
	const ImU32 kHover     = IM_COL32(69, 69, 69, 255);
	constexpr float kToolbarH = 24.0f, kCrumbH = 22.0f, kBottomH = 20.0f, kRowH = 18.0f;

	enum class Kind { Folder, Scene, Prefab, Material, Model, Texture, Controller, TerrainData, TerrainLayer, VolumeProfile, Audio, Script, Text, Other };

	Kind KindOf(const std::wstring& ext, bool dir)
	{
		if (dir) return Kind::Folder;
		if (ext == L".scene") return Kind::Scene;
		if (ext == L".prefab") return Kind::Prefab;
		if (ext == L".mat") return Kind::Material;
		if (ext == L".fbx" || ext == L".obj") return Kind::Model;
		if (ext == L".png" || ext == L".jpg" || ext == L".jpeg" || ext == L".tga" || ext == L".dds" || ext == L".bmp") return Kind::Texture;
		if (ext == L".controller") return Kind::Controller;
		if (ext == L".terraindata") return Kind::TerrainData;
		if (ext == L".terrainlayer") return Kind::TerrainLayer;
		if (ext == L".volumeprofile") return Kind::VolumeProfile;
		if (ext == L".wav") return Kind::Audio;
		if (ext == L".cs") return Kind::Script;
		if (ext == L".txt" || ext == L".json" || ext == L".md") return Kind::Text;
		return Kind::Other;
	}

	const char* IconOf(Kind k, bool open = false)
	{
		switch (k)
		{
		case Kind::Folder: return open ? "folder_open" : "folder";
		case Kind::Scene: return "asset_scene";
		case Kind::Prefab: return "prefab";
		case Kind::Material: return "asset_material";
		case Kind::Model: return "asset_model";
		case Kind::Texture: return "asset_texture";
		case Kind::Controller: return "animator_controller";
		case Kind::TerrainData: return "terrain";
		case Kind::TerrainLayer: return "asset_terrain_layer";
		case Kind::VolumeProfile: return "volume_profile";
		case Kind::Audio: return "audio_clip";
		case Kind::Script: return "script_cs";
		default: return "asset_text";
		}
	}

	// 타입 필터 (툴바의 도형 아이콘)
	const char* kFilterNames[] = { "All", "Scene", "Prefab", "Material", "Model", "Texture", "AnimatorController", "TerrainData", "TerrainLayer", "VolumeProfile", "AudioClip", "MonoScript" };
	const Kind kFilterKinds[] = { Kind::Other, Kind::Scene, Kind::Prefab, Kind::Material, Kind::Model, Kind::Texture, Kind::Controller, Kind::TerrainData, Kind::TerrainLayer, Kind::VolumeProfile, Kind::Audio, Kind::Script };

	// 목록에서 숨기는 파일: 가져오기 캐시, 메타, 숨김 파일
	bool IsHidden(const fs::path& p, const std::wstring& ext)
	{
		const std::wstring name = p.filename().wstring();
		return name.empty() || name[0] == L'.' || ext == L".meta" || ext == L".mesh" || ext == L".animations" || ext == L".skeletons";
	}

	std::string Lower(std::string s) { std::transform(s.begin(), s.end(), s.begin(), ::tolower); return s; }

	std::wstring UniquePath(const fs::path& dir, const std::wstring& base, const std::wstring& ext)
	{
		fs::path p = dir / (base + ext);
		for (int n = 1; fs::exists(p); ++n)
			p = dir / (base + L" " + std::to_wstring(n) + ext);
		return p.wstring();
	}

	bool MoveToRecycleBin(const fs::path& p)
	{
		std::wstring from = p.wstring();
		from.push_back(L'\0');   // 이중 널 종료
		SHFILEOPSTRUCTW op = {};
		op.wFunc = FO_DELETE;
		op.pFrom = from.c_str();
		op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT;
		return ::SHFileOperationW(&op) == 0;
	}

	// 오른쪽 목록/격자 위의 행 배경
	void RowBackground(ImDrawList* dl, ImVec2 a, ImVec2 b, bool selected, bool hovered, bool focused)
	{
		if (selected)
			dl->AddRectFilled(a, b, focused ? kSelFocus : kSelIdle);
		else if (hovered)
			dl->AddRectFilled(a, b, kHover);
	}
}

ProjectEditorWindow::ProjectEditorWindow()
	: EditorWindow("Project", ICON_FA_FOLDER)
{
	solutionDirectory = PathManager::GetI()->GetMovePathW(L"Assets\\");
	currentDirectory = solutionDirectory;
	m_PackagesRoot = fs::path(PathManager::GetI()->GetEnginePathW()) / L"Resources" / L"Packages";
	m_Expanded.insert(fs::path(solutionDirectory).lexically_normal().wstring());
}

ProjectEditorWindow::~ProjectEditorWindow()
{
}

// ------------------------------------------------------------------ 폴더 읽기 (1초 캐시)
const std::vector<ProjectEditorWindow::Entry>& ProjectEditorWindow::List(const fs::path& dir)
{
	const std::wstring key = dir.lexically_normal().wstring();
	const double now = ::GetTickCount64() / 1000.0;
	auto it = m_Cache.find(key);
	if (it != m_Cache.end() && now - it->second.first < 1.0)
		return it->second.second;
	std::vector<Entry> list;
	std::error_code ec;
	for (const auto& e : fs::directory_iterator(dir, ec))
	{
		Entry en;
		en.Path = e.path();
		en.IsDir = e.is_directory(ec);
		en.Ext = e.path().extension().wstring();
		std::transform(en.Ext.begin(), en.Ext.end(), en.Ext.begin(), ::towlower);
		if (IsHidden(en.Path, en.Ext))
			continue;
		en.Name = wstring_to_string(e.path().filename().wstring());
		if (en.IsDir)
			for (const auto& sub : fs::directory_iterator(e.path(), ec))
				if (sub.is_directory(ec)) { en.HasSubDirs = true; break; }
		list.push_back(en);
	}
	// 폴더 먼저, 그다음 이름순 (대소문자 무시)
	std::sort(list.begin(), list.end(), [](const Entry& a, const Entry& b) {
		if (a.IsDir != b.IsDir) return a.IsDir;
		return Lower(a.Name) < Lower(b.Name);
	});
	auto& slot = m_Cache[key];
	slot.first = now;
	slot.second = std::move(list);
	return slot.second;
}

std::string ProjectEditorWindow::RelativeDisplayPath(const fs::path& p) const
{
	std::wstring rel;
	const std::wstring s = p.lexically_normal().wstring();
	const std::wstring pkg = m_PackagesRoot.lexically_normal().wstring();
	if (s.rfind(pkg, 0) == 0)
		rel = L"Packages" + s.substr(pkg.size());
	else
		rel = PathManager::GetI()->GetCutSolutionPath(s);
	std::replace(rel.begin(), rel.end(), L'\\', L'/');
	while (!rel.empty() && rel.back() == L'/') rel.pop_back();
	return wstring_to_string(rel);
}

void ProjectEditorWindow::Navigate(const fs::path& dir)
{
	currentDirectory = dir.lexically_normal().wstring();
	// 트리에서 그 폴더까지 펼친다
	for (fs::path p = dir.lexically_normal(); !p.empty() && p != p.parent_path(); p = p.parent_path())
		m_Expanded.insert(p.wstring());
}

// ------------------------------------------------------------------ 메인
void ProjectEditorWindow::OnRender()
{
	m_Focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	const ImVec2 avail = ImGui::GetContentRegionAvail();
	if (avail.x < 80 || avail.y < 60)
		return;
	if (!fs::exists(currentDirectory))
	{
		EditorLog::Write("Project", "current directory missing '%s' -> Assets", wstring_to_string(currentDirectory).c_str());
		currentDirectory = solutionDirectory;
	}
	{
		static std::wstring lastDir;
		if (lastDir != currentDirectory)
		{
			EditorLog::Write("Project", "current directory = '%s'", wstring_to_string(currentDirectory).c_str());
			lastDir = currentDirectory;
		}
	}

	DrawToolbar(pos, avail.x);
	const float bodyY = pos.y + kToolbarH;
	const float bodyH = avail.y - kToolbarH;
	m_TreeWidth = std::clamp(m_TreeWidth, 120.0f, avail.x - 200.0f);
	DrawTree(ImVec2(pos.x, bodyY), ImVec2(m_TreeWidth, bodyH));

	// 스플리터
	ImGui::SetCursorScreenPos(ImVec2(pos.x + m_TreeWidth - 2, bodyY));
	ImGui::InvisibleButton("##projsplit", ImVec2(4, bodyH));
	if (ImGui::IsItemHovered() || ImGui::IsItemActive())
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
	if (ImGui::IsItemActive())
		m_TreeWidth += ImGui::GetIO().MouseDelta.x;
	ImGui::GetWindowDrawList()->AddLine(ImVec2(pos.x + m_TreeWidth, bodyY), ImVec2(pos.x + m_TreeWidth, bodyY + bodyH), kLine);

	DrawContent(ImVec2(pos.x + m_TreeWidth + 1, bodyY), ImVec2(avail.x - m_TreeWidth - 1, bodyH));

	// 삭제 확인 (Unity: "Delete selected asset?")
	if (!m_PendingDelete.empty())
		ImGui::OpenPopup("Delete selected asset?");
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14, 12));
	if (ImGui::BeginPopupModal("Delete selected asset?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextUnformatted(RelativeDisplayPath(m_PendingDelete).c_str());
		ImGui::TextDisabled("You can restore it from the Recycle Bin.");
		ImGui::Spacing();
		if (ImGui::Button("Delete", ImVec2(100, 0)))
		{
			const bool ok = MoveToRecycleBin(m_PendingDelete);
			EditorLog::Write("Project", "delete %s -> %s", RelativeDisplayPath(m_PendingDelete).c_str(), ok ? "recycle bin" : "failed");
			if (SelectionManager::GetSelectedFile() == m_PendingDelete.wstring())
				SelectionManager::ClearSelection();
			m_PendingDelete.clear();
			InvalidateCache();
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
		{
			m_PendingDelete.clear();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
	ImGui::PopStyleVar();

	// 단축키 (Project 창에 포커스가 있을 때)
	ImGuiIO& io = ImGui::GetIO();
	if (m_Focused && !io.WantTextInput && m_RenamePath.empty())
	{
		const std::wstring sel = SelectionManager::GetSelectedFile();
		if (!sel.empty() && fs::exists(sel))
		{
			if (ImGui::IsKeyPressed(ImGuiKey_F2, false))
			{
				m_RenamePath = sel;
				strncpy_s(m_RenameBuffer, wstring_to_string(fs::path(sel).stem().wstring()).c_str(), _TRUNCATE);
				m_RenameFrames = 0;
			}
			if (ImGui::IsKeyPressed(ImGuiKey_Delete, false))
				m_PendingDelete = sel;
		}
	}
}

// ------------------------------------------------------------------ 툴바
void ProjectEditorWindow::DrawToolbar(ImVec2 pos, float width)
{
	ImDrawList* dl = ImGui::GetWindowDrawList();
	dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + kToolbarH), kToolbar);
	dl->AddLine(ImVec2(pos.x, pos.y + kToolbarH - 1), ImVec2(pos.x + width, pos.y + kToolbarH - 1), kLine);

	// + ▾ (Create)
	ImGui::SetCursorScreenPos(ImVec2(pos.x + 4, pos.y + 2));
	if (ImGui::InvisibleButton("##projcreate", ImVec2(32, kToolbarH - 4)))
		ImGui::OpenPopup("##projcreatemenu");
	const bool createHovered = ImGui::IsItemHovered();
	if (createHovered)
		dl->AddRectFilled(ImVec2(pos.x + 4, pos.y + 2), ImVec2(pos.x + 36, pos.y + kToolbarH - 2), IM_COL32(80, 80, 80, 255), 3.0f);
	UnityGUI::DrawIcon(dl, "plus", ImVec2(pos.x + 8, pos.y + 5), 14.0f);
	UnityGUI::DrawIcon(dl, "dropdown", ImVec2(pos.x + 24, pos.y + 8), 8.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 5));
	if (ImGui::BeginPopup("##projcreatemenu"))
	{
		DrawCreateMenu(currentDirectory);
		ImGui::EndPopup();
	}
	ImGui::PopStyleVar();

	// 검색 (둥근 입력칸 + 돋보기, 지우기 x)
	const float rightButtons = 4 * 24.0f + 8.0f;
	const float searchX = pos.x + 48, searchW = (std::max)(80.0f, width - 48 - rightButtons - 12);
	ImGui::SetCursorScreenPos(ImVec2(searchX, pos.y + 3));
	ImGui::SetNextItemWidth(searchW);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(22, 2));
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 9.0f);
	ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.16f, 0.16f, 0.16f, 1.0f));
	ImGui::InputText("##projsearch", m_Search, sizeof(m_Search));
	ImGui::PopStyleColor();
	ImGui::PopStyleVar(2);
	UnityGUI::DrawIcon(dl, "search", ImVec2(searchX + 6, pos.y + 7), 11.0f, IM_COL32(170, 170, 170, 255));
	if (m_Search[0])
	{
		ImGui::SetCursorScreenPos(ImVec2(searchX + searchW - 20, pos.y + 4));
		if (ImGui::InvisibleButton("##projsearchclear", ImVec2(16, 16)))
			m_Search[0] = 0;
		dl->AddText(ImVec2(searchX + searchW - 16, pos.y + 4), kTextDim, "x");
	}

	// 오른쪽 버튼: 타입 필터 / 라벨 / 즐겨찾기 / 새로고침
	float bx = pos.x + width - rightButtons;
	auto iconButton = [&](const char* id, const char* icon, bool enabled, bool active, const char* tip) {
		ImGui::SetCursorScreenPos(ImVec2(bx, pos.y + 2));
		const bool pressed = ImGui::InvisibleButton(id, ImVec2(22, kToolbarH - 4)) && enabled;
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("%s", tip);
			if (enabled) dl->AddRectFilled(ImVec2(bx, pos.y + 2), ImVec2(bx + 22, pos.y + kToolbarH - 2), IM_COL32(80, 80, 80, 255), 3.0f);
		}
		if (active)
			dl->AddRectFilled(ImVec2(bx, pos.y + 2), ImVec2(bx + 22, pos.y + kToolbarH - 2), IM_COL32(70, 96, 128, 255), 3.0f);
		UnityGUI::DrawIcon(dl, icon, ImVec2(bx + 4, pos.y + 5), 14.0f, enabled ? IM_COL32_WHITE : IM_COL32(110, 110, 110, 255));
		bx += 24.0f;
		return pressed;
	};
	if (iconButton("##projfilter", "filter_type", true, m_TypeFilter != 0, "Search by Type"))
		ImGui::OpenPopup("##projfiltermenu");
	iconButton("##projlabel", "label", false, false, "Search by Label (not supported yet)");
	iconButton("##projfav", "star", false, false, "Favorites (not supported yet)");
	if (iconButton("##projrefresh", "refresh", true, false, "Refresh"))
		InvalidateCache();
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 5));
	if (ImGui::BeginPopup("##projfiltermenu"))
	{
		for (int i = 0; i < IM_ARRAYSIZE(kFilterNames); ++i)
			if (ImGui::MenuItem(kFilterNames[i], nullptr, m_TypeFilter == i))
				m_TypeFilter = i;
		ImGui::EndPopup();
	}
	ImGui::PopStyleVar();
}

void ProjectEditorWindow::DrawCreateMenu(const fs::path& dir)
{
	if (ImGui::MenuItem("Folder"))
	{
		const std::wstring p = UniquePath(dir, L"New Folder", L"");
		std::error_code ec;
		fs::create_directory(p, ec);
		InvalidateCache();
		m_RenamePath = p;   // Unity 처럼 바로 이름 바꾸기
		strncpy_s(m_RenameBuffer, wstring_to_string(fs::path(p).filename().wstring()).c_str(), _TRUNCATE);
		m_RenameFrames = 0;
	}
	// Unity 6: Create > Scripting > MonoBehaviour Script (만든 뒤 바로 이름 바꾸기, 이름을 바꾸면 클래스 이름도 따라감)
	if (ImGui::BeginMenu("Scripting"))
	{
		if (ImGui::MenuItem("MonoBehaviour Script"))
		{
			const std::wstring p = ScriptEngine::CreateScriptAsset(dir.wstring());
			InvalidateCache();
			m_RenamePath = p;
			strncpy_s(m_RenameBuffer, wstring_to_string(fs::path(p).stem().wstring()).c_str(), _TRUNCATE);
			m_RenameFrames = 0;
			SelectionManager::SetSelectedFile(p);
		}
		ImGui::EndMenu();
	}
	ImGui::Separator();
	if (ImGui::MenuItem("Material"))
	{
		UMaterial::Create(wstring_to_string(dir.wstring()));
		InvalidateCache();
	}
	if (ImGui::MenuItem("Animator Controller"))
	{
		const std::wstring p = UniquePath(dir, L"New Animator Controller", L".controller");
		AnimatorController::Create(wstring_to_string(PathManager::GetI()->GetCutSolutionPath(p)));
		SelectionManager::SetSelectedFile(p);
		InvalidateCache();
	}
	if (ImGui::MenuItem("Terrain Layer"))
	{
		const std::wstring p = UniquePath(dir, L"New Terrain Layer", L".terrainlayer");
		TerrainLayer::Create(wstring_to_string(PathManager::GetI()->GetCutSolutionPath(p)), "Resources\\Packages\\Terrain\\Layers\\Grass.png");
		SelectionManager::SetSelectedFile(p);
		InvalidateCache();
	}
	if (ImGui::MenuItem("Volume Profile"))
	{
		const std::string rel = VolumeProfile::CreateAsset(wstring_to_string(dir.wstring()), "New Volume Profile");
		SelectionManager::SetSelectedFile(PathManager::GetI()->GetMovePathW(string_to_wstring(rel)));
		InvalidateCache();
	}
}

// ------------------------------------------------------------------ 왼쪽 폴더 트리
void ProjectEditorWindow::DrawTree(ImVec2 pos, ImVec2 size)
{
	ImGui::SetCursorScreenPos(pos);
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(kTreeBg));
	ImGui::BeginChild("##projtree", size, false);
	const ImVec2 origin = ImGui::GetCursorScreenPos();
	float y = origin.y + 4.0f;
	DrawTreeNode(fs::path(solutionDirectory), "Assets", 0, y, origin.x, size.x);
	if (fs::exists(m_PackagesRoot))
		DrawTreeNode(m_PackagesRoot, "Packages", 0, y, origin.x, size.x);
	ImGui::SetCursorScreenPos(ImVec2(origin.x, y));
	ImGui::Dummy(ImVec2(size.x, 4));
	ImGui::EndChild();
	ImGui::PopStyleColor();
}

void ProjectEditorWindow::DrawTreeNode(const fs::path& dir, const std::string& label, int depth, float& y, float x0, float width)
{
	ImDrawList* dl = ImGui::GetWindowDrawList();
	const std::wstring key = dir.lexically_normal().wstring();
	const bool expanded = m_Expanded.count(key) > 0;
	const bool selected = fs::path(currentDirectory).lexically_normal().wstring() == key;
	bool hasSub = false;
	for (const Entry& e : List(dir))
		if (e.IsDir) { hasSub = true; break; }

	const std::string idKey = wstring_to_string(key);   // ID = 경로 내용 (주소가 아니라)
	ImGui::PushID(idKey.c_str());
	const ImVec2 a(x0, y), b(x0 + width, y + kRowH);
	ImGui::SetCursorScreenPos(a);
	ImGui::InvisibleButton("##row", ImVec2(width, kRowH));
	const bool hovered = ImGui::IsItemHovered();
	const float ax = x0 + 6.0f + depth * 14.0f;
	bool toggle = false;
	if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
	{
		if (hasSub && ImGui::GetIO().MousePos.x < ax + 14.0f)
			toggle = true;   // 화살표
		else
			currentDirectory = key;
	}
	if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && hasSub)
		toggle = true;
	if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
	{
		m_ContextPath = dir;
		m_ContextOnEmpty = true;
		ImGui::OpenPopup("##projctx");
	}
	AcceptGameObjectDrop(dir);   // 폴더에 GameObject 를 놓으면 그 폴더에 프리팹
	RowBackground(dl, a, b, selected, hovered, m_Focused);
	if (hasSub)
		UnityGUI::DrawIcon(dl, expanded ? "arrow_down" : "arrow_right", ImVec2(ax, y + 4), 10.0f, IM_COL32(180, 180, 180, 255));
	UnityGUI::DrawIcon(dl, (expanded && hasSub) ? "folder_open" : "folder", ImVec2(ax + 14, y + 1), 16.0f);
	dl->AddText(ImVec2(ax + 34, y + 1), kText, label.c_str());
	DrawItemContextMenu();
	ImGui::PopID();
	if (toggle)
	{
		if (expanded) m_Expanded.erase(key);
		else m_Expanded.insert(key);
	}
	y += kRowH;

	if (expanded)
		for (const Entry& e : List(dir))
			if (e.IsDir)
				DrawTreeNode(e.Path, e.Name, depth + 1, y, x0, width);
}

// ------------------------------------------------------------------ 오른쪽 내용
void ProjectEditorWindow::DrawBreadcrumb(ImVec2 pos, float width)
{
	ImDrawList* dl = ImGui::GetWindowDrawList();
	dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + kCrumbH), IM_COL32(56, 56, 56, 255));
	dl->AddLine(ImVec2(pos.x, pos.y + kCrumbH - 1), ImVec2(pos.x + width, pos.y + kCrumbH - 1), kLine);
	float x = pos.x + 8.0f;
	if (m_Search[0])
	{
		const std::string t = std::string("Search: ") + (m_TypeFilter ? kFilterNames[m_TypeFilter] : "All");
		dl->AddText(ImVec2(x, pos.y + 3), kText, t.c_str());
		return;
	}
	// "Assets > A > B" (또는 Packages > ...) — 조각을 누르면 그 폴더로
	const std::string rel = RelativeDisplayPath(currentDirectory);
	fs::path root = rel.rfind("Packages", 0) == 0 ? m_PackagesRoot : fs::path(solutionDirectory);
	std::vector<std::string> parts;
	std::stringstream ss(rel);
	std::string part;
	while (std::getline(ss, part, '/'))
		if (!part.empty()) parts.push_back(part);
	fs::path accum = root;
	for (size_t i = 0; i < parts.size(); ++i)
	{
		if (i > 0)
		{
			accum /= string_to_wstring(parts[i]);
			UnityGUI::DrawIcon(dl, "arrow_right", ImVec2(x, pos.y + 7), 8.0f, IM_COL32(160, 160, 160, 255));
			x += 14.0f;
		}
		const ImVec2 ts = ImGui::CalcTextSize(parts[i].c_str());
		ImGui::SetCursorScreenPos(ImVec2(x - 2, pos.y + 2));
		ImGui::PushID((int)i);
		if (ImGui::InvisibleButton("##crumb", ImVec2(ts.x + 4, kCrumbH - 4)))
			Navigate(accum);
		const bool last = i + 1 == parts.size();
		dl->AddText(ImVec2(x, pos.y + 3), ImGui::IsItemHovered() ? IM_COL32(255, 255, 255, 255) : (last ? kText : kTextDim), parts[i].c_str());
		ImGui::PopID();
		x += ts.x + 6.0f;
	}
}

void ProjectEditorWindow::DrawContent(ImVec2 pos, ImVec2 size)
{
	ImDrawList* dl = ImGui::GetWindowDrawList();
	DrawBreadcrumb(pos, size.x);
	const ImVec2 listPos(pos.x, pos.y + kCrumbH);
	const ImVec2 listSize(size.x, size.y - kCrumbH - kBottomH);

	// 보여 줄 항목: 검색어가 있으면 Assets + Packages 전체에서, 없으면 현재 폴더.
	// 전체 폴더를 도는 건 비싸므로 같은 검색 조건이면 1초 동안 결과를 재사용한다.
	std::vector<Entry> items;
	if (m_Search[0] || m_TypeFilter != 0)
	{
		const std::string needle = Lower(m_Search);
		const std::string searchKey = needle + "|" + std::to_string(m_TypeFilter);
		const double now = ImGui::GetTime();
		if (searchKey != m_SearchKey || now - m_SearchTime > 1.0)
		{
			m_SearchKey = searchKey;
			m_SearchTime = now;
			m_SearchResults.clear();
			for (const fs::path& root : { fs::path(solutionDirectory), m_PackagesRoot })
			{
				std::error_code ec;
				if (!fs::exists(root, ec))
					continue;
				for (const auto& e : fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec))
				{
					Entry en;
					en.Path = e.path();
					en.IsDir = e.is_directory(ec);
					en.Ext = e.path().extension().wstring();
					std::transform(en.Ext.begin(), en.Ext.end(), en.Ext.begin(), ::towlower);
					if (IsHidden(en.Path, en.Ext))
						continue;
					en.Name = wstring_to_string(e.path().filename().wstring());
					if (!needle.empty() && Lower(en.Name).find(needle) == std::string::npos)
						continue;
					if (m_TypeFilter != 0 && KindOf(en.Ext, en.IsDir) != kFilterKinds[m_TypeFilter])
						continue;
					m_SearchResults.push_back(en);
					if (m_SearchResults.size() > 2000) break;
				}
			}
			std::sort(m_SearchResults.begin(), m_SearchResults.end(), [](const Entry& a, const Entry& b) { if (a.IsDir != b.IsDir) return a.IsDir; return Lower(a.Name) < Lower(b.Name); });
		}
		items = m_SearchResults;
	}
	else
		items = List(currentDirectory);

	ImGui::SetCursorScreenPos(listPos);
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(kBg));
	ImGui::BeginChild("##projlist", listSize, false);
	const ImVec2 origin = ImGui::GetCursorScreenPos();
	const float width = ImGui::GetContentRegionAvail().x;
	float y = origin.y + 2.0f;
	if (m_IconSize < 0.05f)
	{
		for (const Entry& e : items)
			DrawEntryRow(e, origin.x, y, width, 0);
	}
	else
	{
		// 격자: 아이콘 크기 48 ~ 112
		const float tile = 48.0f + m_IconSize * 64.0f;
		const float cellW = tile + 16.0f, cellH = tile + 34.0f;
		const int cols = (std::max)(1, (int)((width - 8.0f) / cellW));
		int i = 0;
		for (const Entry& e : items)
		{
			const ImVec2 p(origin.x + 8.0f + (i % cols) * cellW, y + (i / cols) * cellH);
			DrawEntryTile(e, p, tile);
			++i;
		}
		y += ((i + cols - 1) / cols) * cellH;
	}
	if (items.empty())
		dl->AddText(ImVec2(origin.x + 12, origin.y + 8), kTextDim, m_Search[0] ? "No results" : "This folder is empty");

	// 남은 빈 곳: 선택 해제, 우클릭 Create, GameObject 드롭 = 프리팹
	const float restH = (std::max)(24.0f, listSize.y - (y - origin.y));
	ImGui::SetCursorScreenPos(ImVec2(origin.x, y));
	ImGui::InvisibleButton("##projempty", ImVec2(width, restH));
	if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
		SelectionManager::ClearSelection();
	if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
	{
		m_ContextPath = currentDirectory;
		m_ContextOnEmpty = true;
		ImGui::OpenPopup("##projctx");
	}
	AcceptGameObjectDrop(currentDirectory);
	DrawItemContextMenu();
	ImGui::EndChild();
	ImGui::PopStyleColor();
	// 목록 창 전체도 드롭 대상 (항목 위에 놓아도 현재 폴더에 만든다)
	AcceptGameObjectDrop(currentDirectory);

	DrawBottomBar(ImVec2(pos.x, pos.y + size.y - kBottomH), size.x);
}

void ProjectEditorWindow::DrawEntryRow(const Entry& e, float x, float& y, float width, int depth)
{
	ImDrawList* dl = ImGui::GetWindowDrawList();
	const std::wstring full = e.Path.wstring();
	const bool selected = SelectionManager::GetSelectedFile() == full;
	const Kind kind = KindOf(e.Ext, e.IsDir);

	const std::string idKey = wstring_to_string(full);
	ImGui::PushID(idKey.c_str());
	const ImVec2 a(x, y), b(x + width, y + kRowH);
	ImGui::SetCursorScreenPos(a);
	ImGui::InvisibleButton("##row", ImVec2(width, kRowH), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
	const bool hovered = ImGui::IsItemHovered();
	RowBackground(dl, a, b, selected, hovered, m_Focused);

	// FBX 는 하위 에셋(메시, 애니메이션 클립)을 펼칠 수 있다 (Unity 와 같음)
	const float ix = x + 8.0f + depth * 16.0f;
	shared_ptr<MeshFile> model;
	if (kind == Kind::Model && depth == 0)
	{
		// 모델 로드는 느리므로(수백 ms~) 펼쳤을 때만 한다. 화살표는 항상 보인다.
		const bool open = m_ExpandedAssets.count(full) > 0;
		if (open)
			model = ResourceManager::GetI()->LoadFbxModel(wstring_to_string(PathManager::GetI()->GetCutSolutionPath(full)));
		if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && ImGui::GetIO().MousePos.x < ix + 12.0f)
		{
			if (open) m_ExpandedAssets.erase(full); else m_ExpandedAssets.insert(full);
		}
		else
			HandleEntryInteraction(e, hovered);
		UnityGUI::DrawIcon(dl, open ? "arrow_down" : "arrow_right", ImVec2(ix, y + 4), 10.0f, IM_COL32(180, 180, 180, 255));
	}
	else
		HandleEntryInteraction(e, hovered);
	BeginDragSource(e);

	const float iconX = ix + 14.0f;
	UnityGUI::DrawIcon(dl, IconOf(kind), ImVec2(iconX, y + 1), 16.0f);
	if (m_RenamePath == e.Path)
	{
		ImGui::SetCursorScreenPos(ImVec2(iconX + 20, y));
		ImGui::SetNextItemWidth((std::max)(80.0f, width - (iconX + 30 - x)));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3, 1));
		if (m_RenameFrames++ == 0)
			ImGui::SetKeyboardFocusHere();
		const bool enter = ImGui::InputText("##rename", m_RenameBuffer, sizeof(m_RenameBuffer), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
		ImGui::PopStyleVar();
		const bool lost = m_RenameFrames > 2 && !ImGui::IsItemActive();
		if (ImGui::IsKeyPressed(ImGuiKey_Escape))
			m_RenamePath.clear();
		else if (enter || lost)
		{
			if (m_RenameBuffer[0])
			{
				const fs::path to = e.Path.parent_path() / (string_to_wstring(m_RenameBuffer) + (e.IsDir ? L"" : e.Path.extension().wstring()));
				std::error_code ec;
				if (to != e.Path && !fs::exists(to))
				{
					fs::rename(e.Path, to, ec);
					if (!ec && _wcsicmp(to.extension().c_str(), L".cs") == 0)
						ScriptEngine::RenameScriptClass(to.wstring(), e.Path.stem().string());
					EditorLog::Write("Project", "rename %s -> %s %s", RelativeDisplayPath(e.Path).c_str(), RelativeDisplayPath(to).c_str(), ec ? ec.message().c_str() : "ok");
					if (!ec && selected)
						SelectionManager::SetSelectedFile(to.wstring());
				}
			}
			m_RenamePath.clear();
			InvalidateCache();
		}
	}
	else
	{
		// Unity 처럼 확장자는 숨긴다 (폴더는 그대로)
		const std::string label = e.IsDir ? e.Name : wstring_to_string(e.Path.stem().wstring());
		dl->AddText(ImVec2(iconX + 21, y + 1), selected ? IM_COL32(255, 255, 255, 255) : kText, label.c_str());
		if (m_Search[0] && hovered)
			ImGui::SetTooltip("%s", RelativeDisplayPath(e.Path).c_str());
	}
	DrawItemContextMenu();
	ImGui::PopID();
	y += kRowH;

	// FBX 하위 에셋
	if (model && m_ExpandedAssets.count(full))
	{
		auto sub = [&](const char* icon, const std::string& name, const char* payloadType, int index) {
			const ImVec2 sa(x, y), sb(x + width, y + kRowH);
			ImGui::PushID(name.c_str());
			ImGui::PushID(index);
			ImGui::SetCursorScreenPos(sa);
			ImGui::InvisibleButton("##sub", ImVec2(width, kRowH));
			RowBackground(dl, sa, sb, false, ImGui::IsItemHovered(), m_Focused);
			if (ImGui::IsItemClicked())
				SelectionManager::SetSelectedFile(full);
			if (payloadType && ImGui::BeginDragDropSource())
			{
				const std::string payload = e.Path.string() + "\\" + std::to_string(index);
				ImGui::SetDragDropPayload(payloadType, payload.c_str(), payload.size() + 1);
				ImGui::Text("%s", name.c_str());
				ImGui::EndDragDropSource();
			}
			UnityGUI::DrawIcon(dl, icon, ImVec2(iconX + 16, y + 1), 16.0f);
			dl->AddText(ImVec2(iconX + 37, y + 1), kTextDim, name.c_str());
			ImGui::PopID();
			ImGui::PopID();
			y += kRowH;
		};
		for (int i = 0; i < (int)model->SkinnedMeshs.size(); ++i)
			sub("skinned_mesh_renderer", model->SkinnedMeshs[i]->Name, "FBX_SKINNED_MESH", i);
		for (int i = 0; i < (int)model->Meshs.size(); ++i)
			sub("mesh_small", model->Meshs[i]->Name, "FBX_MESH", i);
		for (int i = 0; i < (int)model->SkinnedData.AnimationClips.size(); ++i)
			sub("animation_clip", model->SkinnedData.AnimationClips[i]->Name, nullptr, i);
	}
}

void ProjectEditorWindow::DrawEntryTile(const Entry& e, ImVec2 p, float tile)
{
	ImDrawList* dl = ImGui::GetWindowDrawList();
	const std::wstring full = e.Path.wstring();
	const bool selected = SelectionManager::GetSelectedFile() == full;
	const Kind kind = KindOf(e.Ext, e.IsDir);
	const std::string idKey = wstring_to_string(full);
	ImGui::PushID(idKey.c_str());
	ImGui::SetCursorScreenPos(p);
	ImGui::InvisibleButton("##tile", ImVec2(tile + 8, tile + 30), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
	const bool hovered = ImGui::IsItemHovered();
	HandleEntryInteraction(e, hovered);
	BeginDragSource(e);
	if (selected || hovered)
		dl->AddRectFilled(p, ImVec2(p.x + tile + 8, p.y + tile + 6), selected ? (m_Focused ? kSelFocus : kSelIdle) : kHover, 3.0f);
	// 텍스처는 그림 자체, 그 외는 큰 아이콘
	ImTextureID thumb = nullptr;
	if (kind == Kind::Texture)
		thumb = (ImTextureID)ResourceManager::GetI()->LoadTexture(PathManager::GetI()->GetCutSolutionPath(full)).Get();
	if (thumb)
		dl->AddImage(thumb, ImVec2(p.x + 4, p.y + 3), ImVec2(p.x + 4 + tile, p.y + 3 + tile));
	else
		UnityGUI::DrawIcon(dl, IconOf(kind), ImVec2(p.x + 4 + tile * 0.1f, p.y + 3 + tile * 0.1f), tile * 0.8f);
	const std::string label = e.IsDir ? e.Name : wstring_to_string(e.Path.stem().wstring());
	const ImVec4 clip(p.x, p.y + tile + 6, p.x + tile + 8, p.y + tile + 30);
	const ImVec2 ts = ImGui::CalcTextSize(label.c_str());
	const float tx = p.x + (std::max)(0.0f, (tile + 8 - ts.x) * 0.5f);
	if (selected)
		dl->AddRectFilled(ImVec2(tx - 2, p.y + tile + 7), ImVec2((std::min)(tx + ts.x + 2, p.x + tile + 8), p.y + tile + 23), m_Focused ? kSelFocus : kSelIdle, 3.0f);
	dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(tx, p.y + tile + 8), kText, label.c_str(), nullptr, 0.0f, &clip);
	DrawItemContextMenu();
	ImGui::PopID();
}

void ProjectEditorWindow::HandleEntryInteraction(const Entry& e, bool hovered)
{
	if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right))
		SelectionManager::SetSelectedFile(e.Path.wstring());
	if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		Open(e);
	if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
	{
		m_ContextPath = e.Path;
		m_ContextOnEmpty = false;
		ImGui::OpenPopup("##projctx");
	}
	if (e.IsDir)
		AcceptGameObjectDrop(e.Path);
}

void ProjectEditorWindow::BeginDragSource(const Entry& e)
{
	if (e.IsDir || !ImGui::BeginDragDropSource())
		return;
	const Kind kind = KindOf(e.Ext, false);
	const std::string rel = wstring_to_string(PathManager::GetI()->GetCutSolutionPath(e.Path.wstring()));
	const std::string abs = e.Path.string();
	switch (kind)
	{
	case Kind::Prefab: ImGui::SetDragDropPayload("PREFAB_FILE", rel.c_str(), rel.size() + 1); break;
	case Kind::Model: ImGui::SetDragDropPayload("FBX_FILE", abs.c_str(), abs.size() + 1); break;
	case Kind::Texture: ImGui::SetDragDropPayload("PNG_FILE", abs.c_str(), abs.size() + 1); break;
	case Kind::Material: ImGui::SetDragDropPayload("MAT_FILE", rel.c_str(), rel.size() + 1); break;
	case Kind::Controller: ImGui::SetDragDropPayload("CONTROLLER_FILE", rel.c_str(), rel.size() + 1); break;
	default: ImGui::SetDragDropPayload("ASSET_FILE", rel.c_str(), rel.size() + 1); break;
	}
	UnityGUI::DrawIcon(ImGui::GetWindowDrawList(), IconOf(kind), ImGui::GetCursorScreenPos(), 16.0f);
	ImGui::Dummy(ImVec2(18, 16));
	ImGui::SameLine();
	ImGui::TextUnformatted(e.Name.c_str());
	ImGui::EndDragDropSource();
}

bool ProjectEditorWindow::AcceptGameObjectDrop(const fs::path& dir)
{
	if (!ImGui::BeginDragDropTarget())
		return false;
	bool created = false;
	if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("GAME_OBJECT"))
	{
		GameObject* go = *(GameObject**)payload->Data;
		if (go != nullptr)
		{
			const std::wstring file = UniquePath(dir, string_to_wstring(go->GetName()), L".prefab");
			Undo::SetActionName("Create Prefab");
			created = PrefabUtility::SaveAsPrefabAssetAndConnect(go, wstring_to_string(PathManager::GetI()->GetCutSolutionPath(file)));
			Undo::RequestCheck();
			InvalidateCache();
		}
	}
	ImGui::EndDragDropTarget();
	return created;
}

void ProjectEditorWindow::Open(const Entry& e)
{
	const Kind kind = KindOf(e.Ext, e.IsDir);
	switch (kind)
	{
	case Kind::Folder:
		Navigate(e.Path);
		break;
	case Kind::Scene:
		SceneManager::GetI()->LoadScene(PathManager::GetI()->GetCutSolutionPath(e.Path.wstring()));
		break;
	case Kind::Controller:
		SelectionManager::SetSelectedFile(e.Path.wstring());
		AnimatorEditorWindow::Focus();
		break;
	case Kind::Prefab:
	case Kind::Model:
	case Kind::Material:
	case Kind::TerrainData:
	case Kind::TerrainLayer:
	case Kind::VolumeProfile:
		break;
	case Kind::Script:
		ScriptEngine::OpenInCodeEditor(e.Path.wstring(), 1);   // Preferences 의 External Script Editor (기본 NOVA Code)
		break;
	case Kind::Audio:
		// 더블클릭 = 미리 듣기
		SelectionManager::SetSelectedFile(e.Path.wstring());
		AudioManager::PlayPreview(AudioClip::Load(wstring_to_string(PathManager::GetI()->GetCutSolutionPath(e.Path.wstring()))));
		break;   // 에디터 안에서 다루는 에셋 (Inspector 로 확인)
	default:
		::ShellExecuteW(nullptr, L"open", e.Path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
		break;
	}
}

void ProjectEditorWindow::DrawItemContextMenu()
{
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 5));
	if (ImGui::BeginPopup("##projctx"))
	{
		const fs::path target = m_ContextPath;
		const fs::path folder = m_ContextOnEmpty || fs::is_directory(target) ? target : target.parent_path();
		if (ImGui::BeginMenu("Create"))
		{
			DrawCreateMenu(folder);
			ImGui::EndMenu();
		}
		if (ImGui::MenuItem("Show in Explorer"))
		{
			const std::wstring args = L"/select,\"" + target.wstring() + L"\"";
			::ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
		}
		if (!m_ContextOnEmpty)
		{
			if (ImGui::MenuItem("Open"))
			{
				Entry e;
				e.Path = target;
				e.IsDir = fs::is_directory(target);
				e.Ext = target.extension().wstring();
				std::transform(e.Ext.begin(), e.Ext.end(), e.Ext.begin(), ::towlower);
				Open(e);
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Rename", "F2"))
			{
				m_RenamePath = target;
				strncpy_s(m_RenameBuffer, wstring_to_string(fs::is_directory(target) ? target.filename().wstring() : target.stem().wstring()).c_str(), _TRUNCATE);
				m_RenameFrames = 0;
			}
			if (ImGui::MenuItem("Delete", "Del"))
				m_PendingDelete = target;
			if (ImGui::MenuItem("Copy Path"))
				ImGui::SetClipboardText(RelativeDisplayPath(target).c_str());
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Refresh", "Ctrl+R"))
			InvalidateCache();
		ImGui::EndPopup();
	}
	ImGui::PopStyleVar();
}

// ------------------------------------------------------------------ 하단 바
void ProjectEditorWindow::DrawBottomBar(ImVec2 pos, float width)
{
	ImDrawList* dl = ImGui::GetWindowDrawList();
	dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + kBottomH), IM_COL32(60, 60, 60, 255));
	dl->AddLine(pos, ImVec2(pos.x + width, pos.y), kLine);
	// 선택한 에셋 경로 (없으면 현재 폴더) - Unity 와 같이 아이콘 + 경로
	const std::wstring sel = SelectionManager::GetSelectedObjectType() == SelectionType::FILE ? SelectionManager::GetSelectedFile() : std::wstring();
	const fs::path shown = (!sel.empty() && fs::exists(sel)) ? fs::path(sel) : fs::path(currentDirectory);
	std::error_code ec;
	const bool dir = fs::is_directory(shown, ec);
	std::wstring ext = shown.extension().wstring();
	std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
	UnityGUI::DrawIcon(dl, IconOf(KindOf(ext, dir)), ImVec2(pos.x + 6, pos.y + 2), 16.0f);
	const std::string path = RelativeDisplayPath(shown);
	const ImVec4 clip(pos.x, pos.y, pos.x + width - 90, pos.y + kBottomH);
	dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(pos.x + 26, pos.y + 2), kText, path.c_str(), nullptr, 0.0f, &clip);

	// 아이콘 크기 슬라이더 (맨 왼쪽 = 목록)
	const float sx = pos.x + width - 76, sw = 64;
	ImGui::SetCursorScreenPos(ImVec2(sx, pos.y + 2));
	ImGui::InvisibleButton("##projiconsize", ImVec2(sw, kBottomH - 4));
	if (ImGui::IsItemActive())
		m_IconSize = std::clamp((ImGui::GetIO().MousePos.x - sx - 4) / (sw - 8), 0.0f, 1.0f);
	const float cy = pos.y + kBottomH * 0.5f;
	dl->AddRectFilled(ImVec2(sx + 4, cy - 1.5f), ImVec2(sx + sw - 4, cy + 1.5f), IM_COL32(40, 40, 40, 255), 1.5f);
	dl->AddCircleFilled(ImVec2(sx + 4 + (sw - 8) * m_IconSize, cy), 5.0f, IM_COL32(170, 170, 170, 255));
}
