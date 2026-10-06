#include "pch.h"
#include "TilePalette.h"
#include "TilemapComponents.h"
#include "TileAsset.h"
#include "SceneViewOverlay.h"
#include "SelectionManager.h"
#include "SceneManager.h"
#include "UndoSystem.h"
#include "EditorGUIManager.h"
#include "ResourceManager.h"
#include "Debug.h"
#include "ImGui/imgui_internal.h"
#include <filesystem>

namespace fs = std::filesystem;

namespace TilePalette
{
	namespace
	{
		Tool s_Tool = Tool::None;
		std::string s_Folder;
		std::string s_Selected;
		uint8 s_Flags = 0;
		GameObject* s_Active = nullptr;
		GameObject* s_LastSelection = nullptr;

		// 붓질 하나 (누른 순간 ~ 뗀 순간)
		bool s_Stroke = false;
		Tool s_StrokeTool = Tool::None;
		int s_StartX = 0, s_StartY = 0, s_LastX = 0, s_LastY = 0;
		bool s_Changed = false;
		bool s_HasHover = false;
		int s_HoverX = 0, s_HoverY = 0;
		bool s_HasView = false;
		EditorExtensions::SceneViewContext s_LastView;

		std::string Normalize(std::string p)
		{
			std::replace(p.begin(), p.end(), '/', '\\');
			while (!p.empty() && p.back() == '\\')
				p.pop_back();
			return p;
		}

		bool WindowOpen()
		{
			TilePaletteWindow* w = TilePaletteWindow::Instance();
			return w && w->GetIsOpened();
		}

		XMFLOAT3 F3(const Vec3& v) { return XMFLOAT3(v.x, v.y, v.z); }
	}

	Tool CurrentTool() { return s_Tool; }
	void SetTool(Tool tool) { s_Tool = tool; }

	const char* ToolName(Tool tool)
	{
		switch (tool)
		{
		case Tool::Brush: return "brush";
		case Tool::Box: return "box";
		case Tool::Picker: return "picker";
		case Tool::Eraser: return "eraser";
		case Tool::Fill: return "fill";
		default: return "none";
		}
	}

	bool ParseTool(const std::string& name, Tool& out)
	{
		static const std::pair<const char*, Tool> names[] = {
			{ "none", Tool::None }, { "brush", Tool::Brush }, { "paint", Tool::Brush }, { "box", Tool::Box }, { "picker", Tool::Picker },
			{ "pick", Tool::Picker }, { "eraser", Tool::Eraser }, { "erase", Tool::Eraser }, { "fill", Tool::Fill } };
		for (const auto& [n, t] : names)
			if (name == n) { out = t; return true; }
		return false;
	}

	const std::string& Folder() { return s_Folder; }
	void SetFolder(const std::string& folder) { s_Folder = Normalize(folder); }

	std::vector<std::string> Palettes()
	{
		std::vector<std::string> out;
		auto add = [&](const std::string& f) {
			if (std::find(out.begin(), out.end(), f) == out.end())
				out.push_back(f);
		};
		for (const std::string& t : TileAssets::FindAll("Assets"))
			add(Normalize(wstring_to_string(fs::path(string_to_wstring(t)).parent_path().wstring())));
		std::error_code ec;
		for (const auto& e : fs::directory_iterator(TileAssets::FullPath("Assets\\Palettes"), ec))
			if (e.is_directory(ec))
				add(Normalize(TileAssets::ProjectRelative(e.path().wstring())));
		if (!s_Folder.empty())
			add(s_Folder);
		std::sort(out.begin(), out.end());
		return out;
	}

	bool CreatePalette(const std::string& name, std::string& folderOut, std::string& error)
	{
		if (name.empty() || name.find_first_of("\\/:*?\"<>|") != std::string::npos)
		{
			error = "bad palette name";
			return false;
		}
		folderOut = "Assets\\Palettes\\" + name;
		std::error_code ec;
		fs::create_directories(TileAssets::FullPath(folderOut), ec);
		if (ec)
		{
			error = "cannot create " + folderOut;
			return false;
		}
		SetFolder(folderOut);
		return true;
	}

	std::vector<std::string> TilesFromTexture(const std::string& texture, const std::string& folder, std::string& error)
	{
		std::vector<std::string> made;
		const std::string dir = Normalize(folder.empty() ? (s_Folder.empty() ? std::string("Assets\\Palettes\\Default") : s_Folder) : folder);
		for (const std::string& sprite : TileAssets::SpritesOf(texture))
		{
			const size_t hash = sprite.find('#');
			const std::string name = hash != std::string::npos ? sprite.substr(hash + 1) : wstring_to_string(fs::path(string_to_wstring(sprite)).stem().wstring());
			const std::string path = dir + "\\" + name + ".tile";
			if (!fs::exists(TileAssets::FullPath(path)))   // 이미 있으면 그대로 (색 · 콜라이더를 고쳐 놓았을 수 있다)
			{
				TileAssets::Tile t;
				t.Sprite = sprite;
				if (!TileAssets::Save(path, t, error))
					return {};
			}
			made.push_back(path);
		}
		if (made.empty())
			error = "no sprites in " + texture;
		return made;
	}

	const std::string& SelectedTile() { return s_Selected; }
	void SelectTile(const std::string& tile) { s_Selected = Normalize(tile); }
	uint8 BrushFlags() { return s_Flags; }
	void SetBrushFlags(uint8 flags) { s_Flags = flags & 15; }

	void Rotate(bool clockwise)
	{
		const int r = s_Flags & Tilemap::RotateMask;
		s_Flags = (uint8)((s_Flags & ~Tilemap::RotateMask) | ((r + (clockwise ? 3 : 1)) & 3));
	}

	void Flip(bool x) { s_Flags ^= x ? Tilemap::FlipX : Tilemap::FlipY; }

	std::vector<Tilemap*> SceneTilemaps()
	{
		std::vector<Tilemap*> out;
		Scene* scene = SceneManager::GetI()->GetCurrentScene();
		if (!scene)
			return out;
		for (GameObject* go : scene->GameObjectsView())
			if (Tilemap* t = go ? go->GetComponent<Tilemap>() : nullptr)
				out.push_back(t);
		return out;
	}

	Tilemap* ActiveTilemap()
	{
		// Hierarchy 에서 Tilemap 을 고르면 그것이 대상 (Unity 와 같다)
		GameObject* sel = SelectionManager::GetSelectedObjectType() == SelectionType::GAMEOBJECT ? SelectionManager::GetSelectedGameObject() : nullptr;
		if (sel != s_LastSelection)
		{
			s_LastSelection = sel;
			if (sel && GameObject::IsAlive(sel) && sel->GetComponent<Tilemap>())
				s_Active = sel;
		}
		if (s_Active && GameObject::IsAlive(s_Active))
			if (Tilemap* t = s_Active->GetComponent<Tilemap>())
			{
				const auto all = SceneTilemaps();   // 씬을 바꾸면 남은 포인터는 다른 씬의 것
				if (std::find(all.begin(), all.end(), t) != all.end())
					return t;
			}
		const auto all = SceneTilemaps();
		s_Active = all.empty() ? nullptr : all.front()->GetGameObject();
		return all.empty() ? nullptr : all.front();
	}

	void SetActiveTilemap(Tilemap* map) { s_Active = map ? map->GetGameObject() : nullptr; }

	int ApplyAt(Tilemap* map, int x, int y, Tool tool)
	{
		if (map == nullptr)
			return 0;
		switch (tool)
		{
		case Tool::Brush:
			return !s_Selected.empty() && map->SetTile(x, y, s_Selected, s_Flags) ? 1 : 0;
		case Tool::Eraser:
			return map->EraseTile(x, y) ? 1 : 0;
		case Tool::Fill:
			return s_Selected.empty() ? 0 : map->FloodFill(x, y, s_Selected, s_Flags);
		case Tool::Picker:
			if (map->HasTile(x, y))
			{
				s_Selected = map->GetTile(x, y);
				s_Flags = map->GetFlags(x, y);
				s_Tool = Tool::Brush;   // Unity: 고른 뒤 붓으로
			}
			return 0;
		default:
			return 0;
		}
	}

	bool HoverCell(int& x, int& y)
	{
		x = s_HoverX;
		y = s_HoverY;
		return s_HasHover;
	}

	bool CellToScreen(Tilemap* map, int x, int y, float& sx, float& sy)
	{
		if (!s_HasView || map == nullptr)
			return false;
		const Vec3 w = Vec3::Transform(map->CellCenterLocal(x, y), map->GetGameObject()->GetTransform()->GetWorldMatrix());
		const Vec4 clip = Vec4::Transform(Vec4(w.x, w.y, w.z, 1.0f), s_LastView.View * s_LastView.Proj);
		if (clip.w <= 1e-5f)
			return false;
		const float nx = clip.x / clip.w, ny = clip.y / clip.w;
		sx = s_LastView.ViewMin.x + (nx * 0.5f + 0.5f) * (s_LastView.ViewMax.x - s_LastView.ViewMin.x);
		sy = s_LastView.ViewMin.y + (0.5f - ny * 0.5f) * (s_LastView.ViewMax.y - s_LastView.ViewMin.y);
		return true;
	}

	bool SceneGUI(const EditorExtensions::SceneViewContext& ctx)
	{
		s_HasHover = false;
		s_HasView = true;
		s_LastView = ctx;
		Tilemap* map = s_Tool != Tool::None && WindowOpen() ? ActiveTilemap() : nullptr;
		if (map == nullptr)
		{
			s_Stroke = false;
			return false;
		}
		ImGuiIO& io = ImGui::GetIO();
		// 단축키 (Scene 뷰 위, 글자 입력 중이 아닐 때)
		if (ctx.Hovered && !io.WantTextInput && !io.KeyCtrl && !io.KeyAlt && !ImGui::IsMouseDown(ImGuiMouseButton_Right))
		{
			if (ImGui::IsKeyPressed(ImGuiKey_B, false)) s_Tool = Tool::Brush;
			if (ImGui::IsKeyPressed(ImGuiKey_U, false)) s_Tool = Tool::Box;
			if (ImGui::IsKeyPressed(ImGuiKey_I, false)) s_Tool = Tool::Picker;
			if (ImGui::IsKeyPressed(ImGuiKey_D, false)) s_Tool = Tool::Eraser;
			if (ImGui::IsKeyPressed(ImGuiKey_G, false)) s_Tool = Tool::Fill;
			if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket, false)) { if (io.KeyShift) Flip(true); else Rotate(false); }
			if (ImGui::IsKeyPressed(ImGuiKey_RightBracket, false)) { if (io.KeyShift) Flip(false); else Rotate(true); }
			for (ImGuiKey k : { ImGuiKey_Escape, ImGuiKey_Q, ImGuiKey_W, ImGuiKey_E, ImGuiKey_R, ImGuiKey_T, ImGuiKey_Y })
				if (ImGui::IsKeyPressed(k, false))
					s_Tool = Tool::None;   // 다른 Scene 도구로 (그 키는 Scene 툴바도 받는다)
			if (s_Tool == Tool::None)
			{
				s_Stroke = false;
				return false;
			}
		}
		if (!ctx.RayValid)
			return ctx.Hovered;

		// 마우스 광선 → Tilemap 평면 (로컬 z = 0)
		GameObject* go = map->GetGameObject();
		const Matrix world = go->GetTransform()->GetWorldMatrix();
		const Vec3 origin(world._41, world._42, world._43);
		Vec3 normal(world._31, world._32, world._33);
		normal.Normalize();
		const float denom = ctx.RayDir.Dot(normal);
		if (fabsf(denom) < 1e-6f)
			return ctx.Hovered;
		const float t = (origin - ctx.RayOrigin).Dot(normal) / denom;
		if (t < 0.0f || t > ctx.RayLength)
			return ctx.Hovered;
		const Vec3 hit = ctx.RayOrigin + ctx.RayDir * t;
		int cx, cy;
		map->LocalToCell(Vec3::Transform(hit, world.Invert()), cx, cy);
		if (ctx.Hovered)
		{
			s_HasHover = true;
			s_HoverX = cx;
			s_HoverY = cy;
		}

		// 지금 붓질의 도구 (Brush 에서 Shift = 지우개, Ctrl = 고르기)
		Tool tool = s_Tool;
		if (tool == Tool::Brush && io.KeyShift) tool = Tool::Eraser;
		if (tool == Tool::Brush && io.KeyCtrl) tool = Tool::Picker;

		// 칸 둘레 (월드)
		const Vec2 size = map->CellSize(), pitch = map->CellPitch();
		auto box = [&](int x0, int y0, int x1, int y1, ImU32 color, float thickness) {
			const Vec3 a = map->CellToLocal((std::min)(x0, x1), (std::min)(y0, y1));
			const Vec3 b = map->CellToLocal((std::max)(x0, x1), (std::max)(y0, y1)) + Vec3(size.x, size.y, 0.0f);
			const Vec3 p[4] = { Vec3::Transform(Vec3(a.x, a.y, 0), world), Vec3::Transform(Vec3(b.x, a.y, 0), world),
				Vec3::Transform(Vec3(b.x, b.y, 0), world), Vec3::Transform(Vec3(a.x, b.y, 0), world) };
			for (int i = 0; i < 4; ++i)
				SceneViewOverlay::DrawLine(F3(p[i]), F3(p[(i + 1) & 3]), color, thickness);
		};
		// 마우스 둘레의 옅은 격자 (Unity 는 팔레트를 열면 Grid 를 보여 준다)
		{
			const int r = 6;
			const ImU32 faint = IM_COL32(255, 255, 255, 40);
			for (int i = -r; i <= r + 1; ++i)
			{
				const float gx = (cx + i) * pitch.x, gy = (cy + i) * pitch.y;
				const float x0 = (cx - r) * pitch.x, x1 = (cx + r + 1) * pitch.x, y0 = (cy - r) * pitch.y, y1 = (cy + r + 1) * pitch.y;
				SceneViewOverlay::DrawLine(F3(Vec3::Transform(Vec3(gx, y0, 0), world)), F3(Vec3::Transform(Vec3(gx, y1, 0), world)), faint, 1.0f);
				SceneViewOverlay::DrawLine(F3(Vec3::Transform(Vec3(x0, gy, 0), world)), F3(Vec3::Transform(Vec3(x1, gy, 0), world)), faint, 1.0f);
			}
		}
		const ImU32 color = tool == Tool::Eraser ? IM_COL32(255, 110, 90, 240) : IM_COL32(120, 200, 255, 240);
		if (s_Stroke && s_StrokeTool == Tool::Box)
			box(s_StartX, s_StartY, cx, cy, color, 2.0f);
		else if (ctx.Hovered)
			box(cx, cy, cx, cy, color, 2.0f);

		// 누르기 · 끌기 · 떼기 (Alt = 카메라 궤도)
		if (ctx.Hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt)
		{
			s_Stroke = true;
			s_StrokeTool = tool;
			s_StartX = s_LastX = cx;
			s_StartY = s_LastY = cy;
			s_Changed = false;
			SetActiveTilemap(map);
			static const char* names[] = { "", "Paint Tiles", "Box Fill Tiles", "Pick Tile", "Erase Tiles", "Flood Fill Tiles" };
			Undo::SetActionName(names[(int)tool]);
			if (tool != Tool::Box)
				s_Changed |= ApplyAt(map, cx, cy, tool) > 0;
		}
		else if (s_Stroke && ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			if ((s_StrokeTool == Tool::Brush || s_StrokeTool == Tool::Eraser) && (cx != s_LastX || cy != s_LastY))
			{
				// 지난 칸 → 이 칸 직선 (빠르게 끌어도 빈칸이 없게)
				const int dx = cx - s_LastX, dy = cy - s_LastY;
				const int steps = (std::max)(abs(dx), abs(dy));
				for (int i = 1; i <= steps; ++i)
					s_Changed |= ApplyAt(map, s_LastX + (int)lroundf(dx * (float)i / steps), s_LastY + (int)lroundf(dy * (float)i / steps), s_StrokeTool) > 0;
				s_LastX = cx;
				s_LastY = cy;
			}
		}
		if (s_Stroke && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			if (s_StrokeTool == Tool::Box && !s_Selected.empty())
				s_Changed |= map->BoxFill(s_StartX, s_StartY, cx, cy, io.KeyShift ? std::string() : s_Selected, s_Flags) > 0;
			s_Stroke = false;
		}
		if (s_Changed)
		{
			Undo::Touch(go);   // 고르지 않은 Tilemap 이어도 다음 확정에서 다시 직렬화
			if (!s_Stroke)
				s_Changed = false;
		}
		return ctx.Hovered || s_Stroke;
	}
}

// ------------------------------------------------------------------ 창
TilePaletteWindow::TilePaletteWindow()
	: EditorWindow("Tile Palette", ICON_FA_TABLE_CELLS)
{
	s_Instance = this;
	SetIsOpened(false);
}

TilePaletteWindow::~TilePaletteWindow()
{
	if (s_Instance == this)
		s_Instance = nullptr;
}

void TilePaletteWindow::Focus()
{
	if (!s_Instance) return;
	s_Instance->SetIsOpened(true);
	ImGui::SetWindowFocus(s_Instance->GetImGuiName().c_str());
}

void TilePaletteWindow::BeforeBegin()
{
	ImGui::SetNextWindowSize(ImVec2(420.0f, 560.0f), ImGuiCond_FirstUseEver);
	// 처음 열면 Inspector 옆 탭으로 (Unity 에서도 보통 오른쪽에 붙여 쓴다)
	if (EditorWindow* inspector = EditorGUIManager::GetI()->FindWindow("Inspector"))
		if (ImGuiWindow* w = ImGui::FindWindowByName(inspector->GetImGuiName().c_str()); w && w->DockId)
			ImGui::SetNextWindowDockID(w->DockId, ImGuiCond_FirstUseEver);
}

void TilePaletteWindow::ToolRow()
{
	using TilePalette::Tool;
	struct Btn { Tool T; const char* Icon; const char* Tip; };
	static const Btn buttons[] = {
		{ Tool::Brush, ICON_FA_PAINTBRUSH, "Paint with active brush (B)\nShift = erase, Ctrl = pick" },
		{ Tool::Box, ICON_FA_VECTOR_SQUARE, "Box Fill (U)" },
		{ Tool::Picker, ICON_FA_EYE_DROPPER, "Picker (I)" },
		{ Tool::Eraser, ICON_FA_ERASER, "Eraser (D)" },
		{ Tool::Fill, ICON_FA_FILL_DRIP, "Flood Fill (G)" } };
	const ImVec2 bs(30.0f, 26.0f);
	for (const Btn& b : buttons)
	{
		const bool on = TilePalette::CurrentTool() == b.T;
		if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
		if (ImGui::Button(b.Icon, bs))
			TilePalette::SetTool(on ? Tool::None : b.T);
		if (on) ImGui::PopStyleColor();
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", b.Tip);
		ImGui::SameLine(0.0f, 2.0f);
	}
	ImGui::SameLine(0.0f, 14.0f);
	if (ImGui::Button(ICON_FA_ROTATE_LEFT, bs)) TilePalette::Rotate(false);
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rotate counter-clockwise ([)");
	ImGui::SameLine(0.0f, 2.0f);
	if (ImGui::Button(ICON_FA_ROTATE_RIGHT, bs)) TilePalette::Rotate(true);
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rotate clockwise (])");
	ImGui::SameLine(0.0f, 2.0f);
	if (ImGui::Button(ICON_FA_ARROWS_LEFT_RIGHT, bs)) TilePalette::Flip(true);
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Flip X (Shift+[)");
	ImGui::SameLine(0.0f, 2.0f);
	if (ImGui::Button(ICON_FA_ARROWS_UP_DOWN, bs)) TilePalette::Flip(false);
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Flip Y (Shift+])");
}

void TilePaletteWindow::OnRender()
{
	ToolRow();
	ImGui::Separator();

	// Active Tilemap
	Tilemap* active = TilePalette::ActiveTilemap();
	const auto maps = TilePalette::SceneTilemaps();
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted("Active Tilemap");
	ImGui::SameLine(120.0f);
	ImGui::SetNextItemWidth(-1.0f);
	if (ImGui::BeginCombo("##activeTilemap", active ? active->GetGameObject()->GetName().c_str() : "None (GameObject > 2D Object > Tilemap)"))
	{
		for (Tilemap* m : maps)
		{
			ImGui::PushID(m);
			if (ImGui::Selectable(m->GetGameObject()->GetName().c_str(), m == active))
				TilePalette::SetActiveTilemap(m);
			ImGui::PopID();
		}
		ImGui::EndCombo();
	}

	// 팔레트 (폴더)
	const auto palettes = TilePalette::Palettes();
	if (TilePalette::Folder().empty() && !palettes.empty())
		TilePalette::SetFolder(palettes.front());
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted("Palette");
	ImGui::SameLine(120.0f);
	ImGui::SetNextItemWidth(-36.0f);
	const std::string cur = TilePalette::Folder();
	if (ImGui::BeginCombo("##palette", cur.empty() ? "Create New Palette" : wstring_to_string(fs::path(string_to_wstring(cur)).filename().wstring()).c_str()))
	{
		for (const std::string& p : palettes)
			if (ImGui::Selectable(p.c_str(), p == cur))
				TilePalette::SetFolder(p);
		ImGui::EndCombo();
	}
	ImGui::SameLine(0.0f, 4.0f);
	if (ImGui::Button(ICON_FA_FOLDER_PLUS, ImVec2(28.0f, 0.0f)))
		ImGui::OpenPopup("##newPalette");
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Create New Palette (Assets/Palettes/<name>)");
	if (ImGui::BeginPopup("##newPalette"))
	{
		ImGui::TextUnformatted("Name");
		ImGui::SetNextItemWidth(200.0f);
		const bool enter = ImGui::InputText("##name", m_NewName, sizeof(m_NewName), ImGuiInputTextFlags_EnterReturnsTrue);
		if (ImGui::Button("Create") || enter)
		{
			std::string folder, err;
			if (TilePalette::CreatePalette(m_NewName, folder, err))
				ImGui::CloseCurrentPopup();
			else
				Debug::LogError(err);
		}
		ImGui::EndPopup();
	}
	ImGui::Separator();
	TileGrid();
}

void TilePaletteWindow::TileGrid()
{
	const std::string folder = TilePalette::Folder();
	const unsigned long long now = GetTickCount64();
	if (folder != m_ListedFolder || now - m_ListedAt > 1000)
	{
		m_ListedFolder = folder;
		m_ListedAt = now;
		m_Tiles.clear();
		if (!folder.empty())
		{
			// 이 폴더 바로 안의 .tile 만 (하위 폴더는 다른 팔레트)
			std::error_code ec;
			for (const auto& e : fs::directory_iterator(TileAssets::FullPath(folder), ec))
				if (e.is_regular_file(ec) && TileAssets::IsTilePath(wstring_to_string(e.path().filename().wstring())))
					m_Tiles.push_back(folder + "\\" + wstring_to_string(e.path().filename().wstring()));
			std::sort(m_Tiles.begin(), m_Tiles.end());
		}
	}

	const ImVec2 avail = ImGui::GetContentRegionAvail();
	const float footer = 46.0f;
	ImGui::BeginChild("##tiles", ImVec2(0.0f, (std::max)(80.0f, avail.y - footer)), ImGuiChildFlags_Border);
	ImDrawList* dl = ImGui::GetWindowDrawList();
	const float cell = m_CellSize, gap = 4.0f;
	const int cols = (std::max)(1, (int)((ImGui::GetContentRegionAvail().x + gap) / (cell + gap)));
	for (int i = 0; i < (int)m_Tiles.size(); ++i)
	{
		if (i % cols != 0)
			ImGui::SameLine(0.0f, gap);
		const std::string& path = m_Tiles[i];
		ImGui::PushID(i);
		const ImVec2 p0 = ImGui::GetCursorScreenPos();
		if (ImGui::InvisibleButton("##tile", ImVec2(cell, cell)))
		{
			TilePalette::SelectTile(path);
			if (TilePalette::CurrentTool() == TilePalette::Tool::None || TilePalette::CurrentTool() == TilePalette::Tool::Eraser || TilePalette::CurrentTool() == TilePalette::Tool::Picker)
				TilePalette::SetTool(TilePalette::Tool::Brush);
		}
		const bool hovered = ImGui::IsItemHovered();
		const bool selected = TilePalette::SelectedTile() == path;
		dl->AddRectFilled(p0, ImVec2(p0.x + cell, p0.y + cell), IM_COL32(40, 40, 40, 255));
		if (const TileAssets::Tile* t = TileAssets::Get(path); t && t->HasSprite && t->Texture)
		{
			// 칸 안에 그림 비율대로
			const float aspect = t->Size.y > 0 ? t->Size.x / t->Size.y : 1.0f;
			float w = cell - 6.0f, h = cell - 6.0f;
			if (aspect > 1.0f) h = w / aspect; else w = h * aspect;
			const ImVec2 a(p0.x + (cell - w) * 0.5f, p0.y + (cell - h) * 0.5f);
			const ImU32 tint = IM_COL32((int)(t->Color[0] * 255), (int)(t->Color[1] * 255), (int)(t->Color[2] * 255), (int)(t->Color[3] * 255));
			// 그림 파일은 Project 창과 같은 텍스처 (sRGB 로 읽은 장면용 텍스처는 ImGui 에서 어둡게 보인다)
			ImTextureID tex = (ImTextureID)t->Texture;
			if (t->Sprite.rfind("builtin:", 0) != 0)
				if (GfxShaderResourceView* plain = ResourceManager::GetI()->LoadTexture(string_to_wstring(t->Sprite.substr(0, t->Sprite.find('#')))).Get())
					tex = (ImTextureID)plain;
			dl->AddImage(tex, a, ImVec2(a.x + w, a.y + h), ImVec2(t->UV.x, t->UV.y), ImVec2(t->UV.z, t->UV.w), tint);
		}
		else
			dl->AddText(ImVec2(p0.x + 4.0f, p0.y + cell * 0.5f - 7.0f), IM_COL32(200, 120, 120, 255), "?");
		if (selected)
			dl->AddRect(p0, ImVec2(p0.x + cell, p0.y + cell), IM_COL32(90, 170, 255, 255), 0.0f, 0, 2.5f);
		else if (hovered)
			dl->AddRect(p0, ImVec2(p0.x + cell, p0.y + cell), IM_COL32(200, 200, 200, 160));
		if (hovered)
			ImGui::SetTooltip("%s", wstring_to_string(fs::path(string_to_wstring(path)).stem().wstring()).c_str());
		ImGui::PopID();
	}
	if (m_Tiles.empty())
	{
		ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(150, 150, 150, 255));
		ImGui::TextWrapped(folder.empty() ? "Create a palette (folder button above), then drag a sprite or a sliced tileset (Sprite Mode = Multiple) here."
			: "Drag a sprite or a sliced tileset (Sprite Mode = Multiple) here: one Tile asset per sprite.");
		ImGui::PopStyleColor();
	}
	// 남은 영역 전체가 끌어 놓기 대상
	const ImVec2 rest = ImGui::GetContentRegionAvail();
	ImGui::Dummy(ImVec2((std::max)(1.0f, rest.x), (std::max)(1.0f, rest.y)));
	ImGui::EndChild();
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_FILE"))
		{
			const std::string dropped(static_cast<const char*>(payload->Data));
			std::string err;
			std::string target = folder;
			if (target.empty() && !TilePalette::CreatePalette("Default", target, err))
				Debug::LogError(err);
			else if (TileAssets::IsTilePath(dropped))
				TilePalette::SelectTile(dropped);
			else if (TilePalette::TilesFromTexture(dropped, target, err).empty())
				Debug::LogError("Tile Palette: " + err);
			m_ListedAt = 0;
		}
		ImGui::EndDragDropTarget();
	}

	// 아래: 고른 타일 · 붓 모양
	const std::string& sel = TilePalette::SelectedTile();
	const uint8 f = TilePalette::BrushFlags();
	char buf[256];
	snprintf(buf, sizeof(buf), "Brush: %s   rotation %d deg%s%s", sel.empty() ? "(none)" : wstring_to_string(fs::path(string_to_wstring(sel)).stem().wstring()).c_str(),
		(f & Tilemap::RotateMask) * 90, (f & Tilemap::FlipX) ? "  flip X" : "", (f & Tilemap::FlipY) ? "  flip Y" : "");
	ImGui::TextUnformatted(buf);
	ImGui::SetNextItemWidth(160.0f);
	ImGui::SliderFloat("##zoom", &m_CellSize, 24.0f, 96.0f, "tile %.0f px");
}
