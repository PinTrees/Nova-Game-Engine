#pragma once
#include "Component.h"
#include "SpriteBatch.h"
#include "Physics2DComponents.h"
#include <functional>
#include <unordered_map>

// Unity 의 2D Tilemap (com.unity.2d.tilemap): Grid (부모) + Tilemap · Tilemap Renderer · Tilemap Collider 2D (자식)
//  - 칸 (x, y) 의 왼쪽 아래 = Tilemap 로컬 (x · (Cell Size.x + Cell Gap.x), y · (Cell Size.y + Cell Gap.y)) — Rectangle 배치, XYZ
//  - 타일 그림은 기준점이 칸의 Tile Anchor (기본 0.5, 0.5 = 칸 가운데) 에 놓인다. 크기 = 그림 픽셀 / Pixels Per Unit
//  - 칸마다 회전 (90° 단위) · 뒤집기 (Tile Palette 의 [ ] · Shift+[ ])

// Grid: 자식 Tilemap 의 칸 크기 · 간격 (Unity 의 Grid — Cell Layout Rectangle, Cell Swizzle XYZ)
class Grid : public Component
{
public:
	Grid() { m_InspectorTitleName = "Grid"; }
	Vec3 CellSize = Vec3(1, 1, 0);
	Vec3 CellGap = Vec3(0, 0, 0);
	Vec2 Pitch() const { return Vec2(CellSize.x + CellGap.x, CellSize.y + CellGap.y); }

	void OnInspectorGUI() override;
	bool UsesUnityInspector() const override { return true; }
	bool HasEnabledToggle() const override { return false; }
	const char* InspectorIconName() const override { return "grid"; }

	GENERATE_COMPONENT_BODY(Grid)
};
REGISTER_PACKAGE_COMPONENT(Grid)

class Tilemap : public Component
{
public:
	// 칸 하나: 타일 (m_Tiles 의 번호) + 모양 (비트 0..1 = 반시계 90° 회전 수, 2 = X 뒤집기, 3 = Y 뒤집기)
	enum Flags : uint8 { RotateMask = 3, FlipX = 4, FlipY = 8 };
	struct Cell { uint16 Tile = 0; uint8 Flags = 0; };

	Tilemap() { m_InspectorTitleName = "Tilemap"; }

	float Color[4] = { 1, 1, 1, 1 };
	Vec3 TileAnchor = Vec3(0.5f, 0.5f, 0.0f);

	// 타일 놓기 (tile 이 비면 지우기). 바뀌었으면 true
	bool SetTile(int x, int y, const std::string& tile, uint8 flags = 0);
	bool EraseTile(int x, int y) { return SetTile(x, y, std::string()); }
	std::string GetTile(int x, int y) const;   // 없으면 ""
	bool HasTile(int x, int y) const { return m_Cells.count(Key(x, y)) != 0; }
	uint8 GetFlags(int x, int y) const;
	// 사각형 채우기 (두 모서리 포함) — 바뀐 칸 수
	int BoxFill(int x0, int y0, int x1, int y1, const std::string& tile, uint8 flags = 0);
	// (x, y) 와 같은 타일 (빈 칸이면 빈 칸) 로 이어진 칸을 바꾼다. 범위 = 지금 cellBounds (+ 시작 칸) — Unity 와 같다
	int FloodFill(int x, int y, const std::string& tile, uint8 flags = 0);
	void ClearAllTiles();
	int Count() const { return (int)m_Cells.size(); }
	// cellBounds: 타일이 있는 칸의 최소 · 최대 (포함). 비면 false
	bool CellBounds(int& minX, int& minY, int& maxX, int& maxY) const;
	// 쓰고 있는 타일 경로 (중복 없이 — 칸이 바뀔 때만 다시 센다)
	const std::vector<std::string>& UsedTiles() const;
	void ForEachCell(const std::function<void(int x, int y, const std::string& tile, uint8 flags)>& fn) const;
	const std::string& TilePath(uint16 index) const { return m_Tiles[index]; }
	template <class Fn> void ForEachRaw(Fn fn) const { for (const auto& [k, c] : m_Cells) fn(KeyX(k), KeyY(k), c); }

	// 칸 좌표 ↔ Tilemap 로컬 (Grid 의 칸 크기 · 간격)
	Grid* LayoutGrid() const;   // 자기 · 부모 중 처음 Grid
	Vec2 CellSize() const;
	Vec2 CellPitch() const;
	Vec3 CellToLocal(int x, int y) const;          // 칸 왼쪽 아래
	Vec3 CellCenterLocal(int x, int y) const;      // 칸 + Tile Anchor
	void LocalToCell(const Vec3& local, int& x, int& y) const;
	Vec3 CellToWorld(int x, int y) const;
	void WorldToCell(const Vec3& world, int& x, int& y) const;

	// 칸 · 값이 바뀌면 + (렌더러 · 콜라이더가 다시 만든다)
	uint32 Revision() const { return m_Revision; }
	void MarkChanged();

	void OnInspectorGUI() override;
	bool UsesUnityInspector() const override { return true; }
	bool HasEnabledToggle() const override { return false; }
	const char* InspectorIconName() const override { return "tilemap"; }

private:
	std::vector<std::string> m_Tiles;                 // 타일 경로 (칸이 번호로 가리킨다)
	std::unordered_map<int64, Cell> m_Cells;
	uint32 m_Revision = 1;
	mutable std::vector<std::string> m_Used;
	mutable uint32 m_UsedRevision = 0;

	static int64 Key(int x, int y) { return ((int64)x << 32) | (uint32)y; }
	static int KeyX(int64 k) { return (int)(k >> 32); }
	static int KeyY(int64 k) { return (int)(uint32)(k & 0xffffffff); }
	uint16 TileIndex(const std::string& tile);
	void CompactTiles();   // 쓰지 않는 경로 정리 (저장할 때)

	GENERATE_COMPONENT_BODY(Tilemap)
};
REGISTER_PACKAGE_COMPONENT(Tilemap)

// Tilemap Renderer: 같은 GameObject 의 Tilemap 을 SpriteBatch 로 (Unity 의 Chunk 모드 — 타일맵 하나가 한 덩어리로 정렬)
class TilemapRenderer : public Component, public SpriteSource
{
public:
	TilemapRenderer() { m_InspectorTitleName = "Tilemap Renderer"; }
	int SortingOrder = 0;
	int SortingLayerId = 0;

	// SpriteSource
	GameObject* SpriteOwner() const override { return m_pGameObject; }
	bool SpriteEnabled() const override { return m_Enabled; }
	void CollectSprites(SpriteBatch& batch) override;
	bool SpriteLocalBounds(Vec3& bmin, Vec3& bmax) override;

	void OnInspectorGUI() override;
	bool UsesUnityInspector() const override { return true; }
	const char* InspectorIconName() const override { return "tilemap_renderer"; }

	int QuadCount() { Rebuild(); return (int)m_Quads.size(); }

private:
	// Tilemap 로컬 사각형 (칸 · 타일 · 모양이 바뀔 때만 다시)
	struct Quad { Vec3 P[4]; Vec2 UV[4]; uint32 Color; GfxShaderResourceView* Texture; bool Point; };
	std::vector<Quad> m_Quads;
	Vec3 m_Min = Vec3(0, 0, 0), m_Max = Vec3(0, 0, 0);
	size_t m_Signature = 0;
	void Rebuild();

	GENERATE_COMPONENT_BODY(TilemapRenderer)
};
REGISTER_PACKAGE_COMPONENT(TilemapRenderer)

// Tilemap Collider 2D: 콜라이더가 있는 타일 (Collider Type Sprite · Grid) → 상자. 맞닿은 칸 상자는 큰 사각형으로 합친다
//  (모양은 같고 Box2D 모양 수가 줄어 맞닿은 칸 사이 "걸림" 도 없다 — Unity 의 Composite Collider 2D 를 붙인 것과 비슷)
class TilemapCollider2D : public Collider2D
{
public:
	TilemapCollider2D() { m_InspectorTitleName = "Tilemap Collider 2D"; m_NeedsFit = false; }
	void BuildShapes(std::vector<Shape2D>& out) override;
	void Outline(std::vector<std::vector<Vec2>>& loops) override;
	void Update() override { CheckChanged(); }
	void OnInspectorGUI() override;
	const char* InspectorIconName() const override { return "tilemap_collider"; }
	int ShapeCount();   // 합친 뒤 상자 수

private:
	struct Rect { float X0, Y0, X1, Y1; };
	void BuildRects(std::vector<Rect>& out);
	size_t m_Seen = 0;
	void CheckChanged();   // Tilemap · 타일 에셋 · Grid 가 바뀌면 Touch (Play 중 — 물리가 모양을 다시 만든다)

	GENERATE_COMPONENT_BODY(TilemapCollider2D)
};
REGISTER_PACKAGE_COMPONENT(TilemapCollider2D)
