// com.nova.tilemap 진입점: 컴포넌트 Grid · Tilemap · Tilemap Renderer · Tilemap Collider 2D (게임 빌드에도 — DLL 을 불러올 때 등록),
// Window > Tile Palette + Scene 뷰 붓, GameObject > 2D Object > Tilemap > Rectangular, .tile 에셋, CLI "tilemap", C# 함수 (Runtime/Tilemap.cs)
#include "pch.h"
#include "TilemapComponents.h"
#include "TileAsset.h"
#include "TilePalette.h"
#include "TilemapOps.h"
#include "ScriptBindings.h"
#include "CliServer.h"
#include "EditorExtensions.h"
#include "UnityGUI.h"
#include "SpriteRenderer.h"
#include "UISprites.h"
#include "Debug.h"

NOVA_PACKAGE_EXPORT const char* NovaPackage_Abi() { return NOVA_PACKAGE_ABI_VERSION; }

namespace
{
	constexpr const char* kPackage = "com.nova.tilemap";
	TilePaletteWindow* s_Window = nullptr;

	// Project 에서 .tile 을 고르면: Sprite · Color · Collider Type (Unity 의 Tile Inspector)
	void DrawTileInspector(const std::string& path)
	{
		const TileAssets::Tile* cur = TileAssets::Get(path);
		if (cur == nullptr)
		{
			UnityGUI::HelpBox(("Cannot read " + path).c_str());
			return;
		}
		TileAssets::Tile t = *cur;
		bool changed = false;
		const std::string spriteName = t.Sprite.empty() ? "None (Sprite)" : UISprites::DisplayName(t.Sprite);
		if (UnityGUI::ObjectField("Sprite", spriteName.c_str(), 0, "sprite_renderer"))
			ImGui::OpenPopup("##tileSprite");
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET_FILE"))
			{
				const std::string dropped(static_cast<const char*>(p->Data));
				if (UISprites::IsImagePath(dropped.substr(0, dropped.find('#'))))
				{
					t.Sprite = dropped;
					changed = true;
				}
			}
			ImGui::EndDragDropTarget();
		}
		if (ImGui::BeginPopup("##tileSprite"))
		{
			for (const std::string& s : UISprites::FindAll2D())
				if (ImGui::Selectable(s.c_str(), s == t.Sprite))
				{
					t.Sprite = s;
					changed = true;
				}
			ImGui::EndPopup();
		}
		changed |= UnityGUI::Color("Color", t.Color);
		static const char* colliders[] = { "None", "Sprite", "Grid" };
		int ct = (int)t.Collider;
		if (UnityGUI::Dropdown("Collider Type", &ct, colliders, 3))
		{
			t.Collider = (TileAssets::ColliderType)ct;
			changed = true;
		}
		if (t.HasSprite)
		{
			char buf[96];
			snprintf(buf, sizeof(buf), "%.3g x %.3g units", t.Size.x, t.Size.y);
			UnityGUI::ValueLabel("Sprite Size", buf);
		}
		if (changed)
		{
			std::string err;
			if (!TileAssets::Save(path, t, err))
				Debug::LogError(err);
		}
	}
}

NOVA_PACKAGE_EXPORT void NovaPackage_OnLoad()
{
	if (Application::IsPlayer())
		return;   // 게임 빌드에는 편집기가 없다
	s_Window = new TilePaletteWindow();
	EditorGUIManager::GetI()->RegisterWindow(s_Window);
	EditorExtensions::RegisterSceneTool(kPackage, TilePalette::SceneGUI);

	EditorExtensions::CreateMenuItem item;
	item.Owner = kPackage;
	item.Path = "2D Object/Tilemap/Rectangular";
	item.Create = [](Scene* scene, GameObject* parent) { return TilemapOps::CreateRectangular(scene, parent); };
	EditorExtensions::RegisterCreateMenu(item);

	// Project 창: Create > Tile, 더블클릭 = 팔레트 붓으로 고르기, Inspector = Sprite · Color · Collider Type
	EditorExtensions::AssetType t;
	t.Owner = kPackage;
	t.Extension = ".tile";
	t.Icon = "tile";
	t.CreateMenu = "Tile";
	t.DefaultName = "New Tile";
	t.Create = [](const std::string& path) {
		std::string err;
		TileAssets::Save(path, TileAssets::Tile(), err);
	};
	t.Open = [](const std::string& path) {
		TilePalette::SelectTile(path);
		if (TilePalette::CurrentTool() == TilePalette::Tool::None)
			TilePalette::SetTool(TilePalette::Tool::Brush);
		TilePaletteWindow::Focus();
	};
	t.Inspector = [](const std::string& path) { DrawTileInspector(path); };
	EditorExtensions::RegisterAssetType(t);

	// CLI: nova tilemap <op> [--인자 …]  → {"op": "...", ...}
	CliServer::Register("tilemap", "2D tilemap op: {op, ...args} (nova tilemap help)", [](const nlohmann::json& args, nlohmann::json& result, std::string& error) {
		const std::string op = args.value("op", std::string("help"));
		nlohmann::json opArgs = args;
		opArgs.erase("op");
		return TilemapOps::Run(op, opArgs, result, error);
	});
}

NOVA_PACKAGE_EXPORT void NovaPackage_OnUnload()
{
	CliServer::Unregister("tilemap");
	EditorExtensions::UnregisterOwner(kPackage);
	if (s_Window)
	{
		EditorGUIManager::GetI()->UnregisterWindow(s_Window);
		delete s_Window;
		s_Window = nullptr;
	}
}

// ---- C#: Grid · Tilemap · TilemapRenderer (Runtime/Tilemap.cs)
namespace
{
	template <class T> T* Find(uint64 id)
	{
		GameObject* go = ScriptBindings::FindObject(id);
		return go ? go->GetComponentIncludingPending<T>() : nullptr;
	}
	std::string Str(const char* s) { std::string r = s ? s : ""; std::replace(r.begin(), r.end(), '/', '\\'); return r; }
}

NOVA_PACKAGE_EXPORT int TM_SetTile(uint64 id, int x, int y, const char* tile, int flags)
{
	Tilemap* t = Find<Tilemap>(id);
	return t && t->SetTile(x, y, Str(tile), (uint8)(flags & 15)) ? 1 : 0;
}

NOVA_PACKAGE_EXPORT const char* TM_GetTile(uint64 id, int x, int y)
{
	Tilemap* t = Find<Tilemap>(id);
	return ScriptBindings::ReturnString(t ? t->GetTile(x, y) : std::string());
}

NOVA_PACKAGE_EXPORT int TM_GetFlags(uint64 id, int x, int y)
{
	Tilemap* t = Find<Tilemap>(id);
	return t ? t->GetFlags(x, y) : 0;
}

NOVA_PACKAGE_EXPORT int TM_BoxFill(uint64 id, int x0, int y0, int x1, int y1, const char* tile)
{
	Tilemap* t = Find<Tilemap>(id);
	return t ? t->BoxFill(x0, y0, x1, y1, Str(tile)) : 0;
}

NOVA_PACKAGE_EXPORT int TM_FloodFill(uint64 id, int x, int y, const char* tile)
{
	Tilemap* t = Find<Tilemap>(id);
	return t ? t->FloodFill(x, y, Str(tile)) : 0;
}

NOVA_PACKAGE_EXPORT void TM_Clear(uint64 id)
{
	if (Tilemap* t = Find<Tilemap>(id)) t->ClearAllTiles();
}

NOVA_PACKAGE_EXPORT int TM_Count(uint64 id)
{
	Tilemap* t = Find<Tilemap>(id);
	return t ? t->Count() : 0;
}

// out[4] = minX, minY, maxX, maxY (포함). 비면 0
NOVA_PACKAGE_EXPORT int TM_Bounds(uint64 id, int* out)
{
	Tilemap* t = Find<Tilemap>(id);
	return t && out && t->CellBounds(out[0], out[1], out[2], out[3]) ? 1 : 0;
}

// 0 = 칸 왼쪽 아래 (CellToWorld), 1 = 칸 + Tile Anchor (GetCellCenterWorld)
NOVA_PACKAGE_EXPORT void TM_CellToWorld(uint64 id, int x, int y, int center, float* out)
{
	Tilemap* t = Find<Tilemap>(id);
	if (!t || !out) return;
	const Vec3 local = center ? t->CellCenterLocal(x, y) : t->CellToLocal(x, y);
	const Vec3 w = Vec3::Transform(local, t->GetGameObject()->GetTransform()->GetWorldMatrix());
	out[0] = w.x; out[1] = w.y; out[2] = w.z;
}

NOVA_PACKAGE_EXPORT void TM_WorldToCell(uint64 id, float x, float y, float z, int* out)
{
	Tilemap* t = Find<Tilemap>(id);
	if (!t || !out) return;
	t->WorldToCell(Vec3(x, y, z), out[0], out[1]);
	out[2] = 0;
}

// 0..3 = color, 10..12 = tileAnchor
NOVA_PACKAGE_EXPORT float TM_GetFloat(uint64 id, int prop)
{
	Tilemap* t = Find<Tilemap>(id);
	if (!t) return 0.0f;
	if (prop >= 0 && prop < 4) return t->Color[prop];
	if (prop >= 10 && prop < 13) return (&t->TileAnchor.x)[prop - 10];
	return 0.0f;
}

NOVA_PACKAGE_EXPORT void TM_SetFloat(uint64 id, int prop, float v)
{
	Tilemap* t = Find<Tilemap>(id);
	if (!t) return;
	if (prop >= 0 && prop < 4) t->Color[prop] = v;
	else if (prop >= 10 && prop < 13) (&t->TileAnchor.x)[prop - 10] = v;
	else return;
	t->MarkChanged();
}

// Grid: 0..2 cellSize, 3..5 cellGap
NOVA_PACKAGE_EXPORT float GRID_Get(uint64 id, int prop)
{
	Grid* g = Find<Grid>(id);
	if (!g || prop < 0 || prop > 5) return 0.0f;
	return prop < 3 ? (&g->CellSize.x)[prop] : (&g->CellGap.x)[prop - 3];
}

NOVA_PACKAGE_EXPORT void GRID_Set(uint64 id, int prop, float v)
{
	Grid* g = Find<Grid>(id);
	if (!g || prop < 0 || prop > 5) return;
	(prop < 3 ? (&g->CellSize.x)[prop] : (&g->CellGap.x)[prop - 3]) = v;
	for (GameObject* child : g->GetGameObject()->GetChildren())
		if (Tilemap* t = child->GetComponent<Tilemap>())
			t->MarkChanged();
}

// TilemapRenderer: 0 sortingOrder, 1 sortingLayerID
NOVA_PACKAGE_EXPORT int TMR_GetInt(uint64 id, int prop)
{
	TilemapRenderer* r = Find<TilemapRenderer>(id);
	if (!r) return 0;
	return prop == 0 ? r->SortingOrder : r->SortingLayerId;
}

NOVA_PACKAGE_EXPORT void TMR_SetInt(uint64 id, int prop, int v)
{
	TilemapRenderer* r = Find<TilemapRenderer>(id);
	if (!r) return;
	if (prop == 0) r->SortingOrder = v; else r->SortingLayerId = v;
}
