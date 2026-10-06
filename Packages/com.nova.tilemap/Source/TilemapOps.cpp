#include "pch.h"
#include "TilemapOps.h"
#include "TilemapComponents.h"
#include "TileAsset.h"
#include "TilePalette.h"
#include "SceneManager.h"
#include "UndoSystem.h"

namespace TilemapOps
{
	namespace
	{
		Tilemap* FindTilemap(const json& a, std::string& error)
		{
			const std::string name = a.value("tilemap", std::string());
			if (name.empty())
			{
				Tilemap* t = TilePalette::ActiveTilemap();
				if (!t) error = "no Tilemap in the scene (nova tilemap create)";
				return t;
			}
			for (Tilemap* t : TilePalette::SceneTilemaps())
				if (t->GetGameObject()->GetName() == name)
					return t;
			error = "no Tilemap named " + name;
			return nullptr;
		}

		GameObject* FindObject(const std::string& name)
		{
			Scene* scene = SceneManager::GetI()->GetCurrentScene();
			if (!scene) return nullptr;
			for (GameObject* go : scene->GameObjectsView())
				if (go && go->GetName() == name)
					return go;
			return nullptr;
		}

		bool Int2(const json& a, const char* key, int& x, int& y)
		{
			if (!a.contains(key) || !a[key].is_array() || a[key].size() < 2)
				return false;
			x = (int)floor(a[key][0].get<double>());
			y = (int)floor(a[key][1].get<double>());
			return true;
		}

		bool XY(const json& a, int& x, int& y, std::string& error)
		{
			if (Int2(a, "cell", x, y))
				return true;
			if (!a.contains("x") || !a.contains("y") || !a["x"].is_number() || !a["y"].is_number())
			{
				error = "--x --y (or --cell x,y)";
				return false;
			}
			x = (int)floor(a["x"].get<double>());
			y = (int)floor(a["y"].get<double>());
			return true;
		}

		// --rotation 0|90|180|270 (반시계) --flipx --flipy
		uint8 FlagsOf(const json& a, uint8 base = 0)
		{
			uint8 f = base;
			if (a.contains("rotation") && a["rotation"].is_number())
			{
				const int r = (((int)lround(a["rotation"].get<double>() / 90.0) % 4) + 4) % 4;
				f = (uint8)((f & ~Tilemap::RotateMask) | r);
			}
			if (a.value("flipx", false)) f |= Tilemap::FlipX;
			if (a.value("flipy", false)) f |= Tilemap::FlipY;
			return f;
		}

		std::string TileArg(const json& a)
		{
			std::string t = a.value("tile", std::string());
			std::replace(t.begin(), t.end(), '/', '\\');
			return t;
		}

		void Touch(Tilemap* map, const std::string& action)
		{
			Undo::SetActionName(action);
			Undo::Touch(map->GetGameObject());
			Undo::RequestCheck();
		}

		json Info(Tilemap* map)
		{
			json r;
			r["tilemap"] = map->GetGameObject()->GetName();
			r["count"] = map->Count();
			int x0, y0, x1, y1;
			if (map->CellBounds(x0, y0, x1, y1))
				r["bounds"] = { { "min", { x0, y0 } }, { "max", { x1, y1 } } };
			r["tiles"] = map->UsedTiles();
			const Vec2 cs = map->CellSize(), cp = map->CellPitch();
			r["cellSize"] = { cs.x, cs.y };
			r["cellPitch"] = { cp.x, cp.y };
			r["grid"] = map->LayoutGrid() ? map->LayoutGrid()->GetGameObject()->GetName() : std::string();
			if (TilemapRenderer* tr = map->GetGameObject()->GetComponent<TilemapRenderer>())
				r["quads"] = tr->QuadCount();
			if (TilemapCollider2D* col = map->GetGameObject()->GetComponent<TilemapCollider2D>())
				r["shapes"] = col->ShapeCount();
			return r;
		}

		json PaletteState()
		{
			json r;
			r["tool"] = TilePalette::ToolName(TilePalette::CurrentTool());
			r["palette"] = TilePalette::Folder();
			r["selected"] = TilePalette::SelectedTile();
			r["flags"] = TilePalette::BrushFlags();
			Tilemap* t = TilePalette::ActiveTilemap();
			r["active"] = t ? t->GetGameObject()->GetName() : std::string();
			int hx, hy;
			if (TilePalette::HoverCell(hx, hy))
				r["hover"] = { hx, hy };
			r["window"] = TilePaletteWindow::Instance() && TilePaletteWindow::Instance()->GetIsOpened();
			return r;
		}

		bool TileJson(const std::string& path, json& r, std::string& error)
		{
			const TileAssets::Tile* t = TileAssets::Get(path);
			if (!t) { error = "cannot read tile " + path; return false; }
			r = { { "path", path }, { "sprite", t->Sprite }, { "color", { t->Color[0], t->Color[1], t->Color[2], t->Color[3] } },
				{ "colliderType", TileAssets::ColliderName(t->Collider) }, { "hasSprite", t->HasSprite }, { "size", { t->Size.x, t->Size.y } } };
			return true;
		}
	}

	json Help()
	{
		return {
			{ "create", "[--name Tilemap] [--grid <Grid object>] [--collider] [--order n]: Grid + child Tilemap (+ Tilemap Renderer)" },
			{ "tile.create", "--path Assets/Tiles/x.tile --sprite <sprite> [--color r,g,b,a] [--collider none|sprite|grid]" },
			{ "tile.fromtexture", "--texture <png> [--folder <palette folder>] [--collider ...]: one .tile per sprite (sliced: per slice)" },
			{ "tile.info", "--path <tile>" },
			{ "set", "--x --y --tile <tile> [--rotation 0|90|180|270] [--flipx] [--flipy] [--tilemap name]" },
			{ "erase", "--x --y [--tilemap]" },
			{ "box", "--from x,y --to x,y [--tile <tile> | (none = erase)] [--rotation ...] [--tilemap]" },
			{ "fill", "--x --y --tile <tile>: flood fill within the tilemap bounds [--tilemap]" },
			{ "clear", "[--tilemap]: ClearAllTiles" },
			{ "get", "--x --y [--tilemap]: tile + flags" },
			{ "info", "[--tilemap]: count, bounds, tiles, quads, collider shapes" },
			{ "collider", "[--tilemap] [--remove]: add (or remove) a Tilemap Collider 2D" },
			{ "window", "open Window > Tile Palette" },
			{ "palette", "[--folder F] [--create name]: choose / create a palette (folder of .tile)" },
			{ "select", "--tile <tile> [--rotation] [--flipx] [--flipy]: brush tile" },
			{ "tool", "--tool brush|box|picker|eraser|fill|none" },
			{ "active", "--tilemap <name>: Active Tilemap of the palette" },
			{ "paint", "--x --y [--tool ...]: apply the palette tool to one cell (like a Scene view click)" },
			{ "state", "palette state (tool, palette, selected tile, active tilemap, hover cell)" },
			{ "batch", "steps: [{op, ...}] as one undo step" } };
	}

	GameObject* CreateRectangular(Scene* scene, GameObject* parent, const std::string& name)
	{
		if (scene == nullptr)
			return nullptr;
		GameObject* gridGo = parent && parent->GetComponent<Grid>() ? parent : nullptr;
		if (gridGo == nullptr)
		{
			gridGo = new GameObject("Grid");
			gridGo->AddComponent<Grid>();
			if (parent)
				gridGo->SetParent(parent, false);
			else
				scene->AddRootGameObject(gridGo);
		}
		GameObject* go = new GameObject(name);
		go->AddComponent<Tilemap>();
		go->AddComponent<TilemapRenderer>();
		go->SetParent(gridGo, false);
		scene->RegisterGameObjectTree(gridGo);
		TilePalette::SetActiveTilemap(go->GetComponent<Tilemap>());
		Undo::SetActionName("Create Tilemap");
		Undo::RequestCheck();
		return go;
	}

	bool Run(const std::string& op, const json& a, json& r, std::string& e)
	{
		if (op == "help") { r = Help(); return true; }
		if (op == "batch")
		{
			if (!a.contains("steps") || !a["steps"].is_array()) { e = "steps: [{op, ...}]"; return false; }
			json results = json::array();
			for (const json& s : a["steps"])
			{
				json sr;
				const std::string sop = s.value("op", std::string());
				if (sop == "batch" || !Run(sop, s, sr, e)) { e = sop + ": " + e; return false; }
				results.push_back(sr);
			}
			Undo::SetActionName("Tilemap Batch");
			r = { { "steps", results.size() }, { "results", results } };
			return true;
		}

		// ---- 타일 에셋
		if (op == "tile.create")
		{
			std::string path = a.value("path", std::string());
			if (path.empty()) { e = "--path Assets/.../x.tile"; return false; }
			if (!TileAssets::IsTilePath(path)) path += ".tile";
			TileAssets::Tile t;
			t.Sprite = a.value("sprite", std::string());
			if (a.contains("color") && a["color"].is_array() && a["color"].size() >= 3)
				for (size_t i = 0; i < a["color"].size() && i < 4; ++i)
					t.Color[i] = a["color"][i].get<float>();
			if (a.contains("collider") && !TileAssets::ParseCollider(a["collider"].get<std::string>(), t.Collider)) { e = "--collider none|sprite|grid"; return false; }
			if (!TileAssets::Save(path, t, e)) return false;
			return TileJson(path, r, e);
		}
		if (op == "tile.fromtexture")
		{
			const std::string tex = a.value("texture", a.value("path", std::string()));
			const std::vector<std::string> made = TilePalette::TilesFromTexture(tex, a.value("folder", std::string()), e);
			if (made.empty()) return false;
			if (a.contains("collider"))
			{
				TileAssets::ColliderType ct;
				if (!TileAssets::ParseCollider(a["collider"].get<std::string>(), ct)) { e = "--collider none|sprite|grid"; return false; }
				for (const std::string& p : made)
					if (const TileAssets::Tile* t = TileAssets::Get(p))
					{
						TileAssets::Tile copy = *t;
						copy.Collider = ct;
						if (!TileAssets::Save(p, copy, e)) return false;
					}
			}
			r = { { "tiles", made }, { "count", made.size() } };
			return true;
		}
		if (op == "tile.info")
			return TileJson(a.value("path", std::string()), r, e);

		// ---- 팔레트 상태
		if (op == "window")
		{
			TilePaletteWindow::Focus();
			r = PaletteState();
			return true;
		}
		if (op == "palette")
		{
			if (a.contains("create"))
			{
				std::string folder;
				if (!TilePalette::CreatePalette(a["create"].get<std::string>(), folder, e)) return false;
			}
			if (a.contains("folder")) TilePalette::SetFolder(a["folder"].get<std::string>());
			r = PaletteState();
			r["palettes"] = TilePalette::Palettes();
			return true;
		}
		if (op == "select")
		{
			const std::string t = TileArg(a);
			if (!t.empty() && !TileAssets::Get(t)) { e = "cannot read tile " + t; return false; }
			TilePalette::SelectTile(t);
			TilePalette::SetBrushFlags(FlagsOf(a));
			r = PaletteState();
			return true;
		}
		if (op == "tool")
		{
			TilePalette::Tool tool;
			if (!TilePalette::ParseTool(a.value("tool", std::string()), tool)) { e = "--tool brush|box|picker|eraser|fill|none"; return false; }
			TilePalette::SetTool(tool);
			r = PaletteState();
			return true;
		}
		if (op == "state")
		{
			r = PaletteState();
			int cx, cy;
			float sx, sy;
			std::string ignore;
			if (Int2(a, "cell", cx, cy) && TilePalette::CellToScreen(FindTilemap(a, ignore), cx, cy, sx, sy))
				r["screen"] = { sx, sy };
			return true;
		}

		// ---- 씬
		if (op == "create")
		{
			Scene* scene = SceneManager::GetI()->GetCurrentScene();
			GameObject* parent = nullptr;
			if (a.contains("grid"))
			{
				parent = FindObject(a["grid"].get<std::string>());
				if (!parent) { e = "no GameObject named " + a["grid"].get<std::string>(); return false; }
			}
			GameObject* go = CreateRectangular(scene, parent, a.value("name", std::string("Tilemap")));
			if (!go) { e = "no scene"; return false; }
			if (a.value("collider", false))
				go->AddComponent<TilemapCollider2D>();
			if (a.contains("order"))
				go->GetComponent<TilemapRenderer>()->SortingOrder = a["order"].get<int>();
			r = Info(go->GetComponent<Tilemap>());
			return true;
		}
		if (op == "active")
		{
			Tilemap* map = FindTilemap(a, e);
			if (!map) return false;
			TilePalette::SetActiveTilemap(map);
			r = PaletteState();
			return true;
		}

		Tilemap* map = FindTilemap(a, e);
		if (!map) return false;
		int x = 0, y = 0;
		if (op == "set" || op == "erase" || op == "get" || op == "fill" || op == "paint")
			if (!XY(a, x, y, e)) return false;
		if (op == "set")
		{
			const std::string t = TileArg(a);
			if (t.empty()) { e = "--tile <tile> (erase: nova tilemap erase)"; return false; }
			const bool changed = map->SetTile(x, y, t, FlagsOf(a));
			Touch(map, "Set Tile");
			r = { { "changed", changed }, { "count", map->Count() } };
			return true;
		}
		if (op == "erase")
		{
			const bool changed = map->EraseTile(x, y);
			Touch(map, "Erase Tile");
			r = { { "changed", changed }, { "count", map->Count() } };
			return true;
		}
		if (op == "get")
		{
			r = { { "tile", map->GetTile(x, y) }, { "flags", map->GetFlags(x, y) }, { "rotation", (map->GetFlags(x, y) & Tilemap::RotateMask) * 90 },
				{ "flipx", (map->GetFlags(x, y) & Tilemap::FlipX) != 0 }, { "flipy", (map->GetFlags(x, y) & Tilemap::FlipY) != 0 } };
			return true;
		}
		if (op == "box")
		{
			int x0, y0, x1, y1;
			if (!Int2(a, "from", x0, y0) || !Int2(a, "to", x1, y1)) { e = "--from x,y --to x,y"; return false; }
			const int n = map->BoxFill(x0, y0, x1, y1, TileArg(a), FlagsOf(a));
			Touch(map, "Box Fill Tiles");
			r = { { "changed", n }, { "count", map->Count() } };
			return true;
		}
		if (op == "fill")
		{
			const std::string t = TileArg(a);
			if (t.empty()) { e = "--tile <tile>"; return false; }
			const int n = map->FloodFill(x, y, t, FlagsOf(a));
			Touch(map, "Flood Fill Tiles");
			r = { { "changed", n }, { "count", map->Count() } };
			return true;
		}
		if (op == "clear")
		{
			map->ClearAllTiles();
			Touch(map, "Clear All Tiles");
			r = { { "count", 0 } };
			return true;
		}
		if (op == "info") { r = Info(map); return true; }
		if (op == "collider")
		{
			GameObject* go = map->GetGameObject();
			TilemapCollider2D* col = go->GetComponent<TilemapCollider2D>();
			if (a.value("remove", false))
			{
				if (col && SceneManager::GetI()->GetCurrentScene()) SceneManager::GetI()->GetCurrentScene()->DestroyComponent(col);
			}
			else if (!col)
				go->AddComponent<TilemapCollider2D>();
			Touch(map, "Tilemap Collider 2D");
			r = Info(map);
			return true;
		}
		if (op == "paint")
		{
			TilePalette::Tool tool = TilePalette::CurrentTool();
			if (a.contains("tool") && !TilePalette::ParseTool(a["tool"].get<std::string>(), tool)) { e = "--tool brush|picker|eraser|fill"; return false; }
			if (tool == TilePalette::Tool::None || tool == TilePalette::Tool::Box) { e = "paint needs a brush, picker, eraser or fill tool (box: nova tilemap box)"; return false; }
			const int n = TilePalette::ApplyAt(map, x, y, tool);
			Touch(map, "Paint Tiles");
			r = PaletteState();
			r["changed"] = n;
			r["count"] = map->Count();
			return true;
		}
		e = "unknown op " + op + " (nova tilemap help)";
		return false;
	}
}
