#include "pch.h"
#include "TilemapComponents.h"
#include "TileAsset.h"
#include "UnityGUI.h"
#include "SpriteRenderer.h"
#include "UndoSystem.h"

namespace
{
	json V3(const Vec3& v) { return { v.x, v.y, v.z }; }
	Vec3 ReadV3(const json& j, const char* key, const Vec3& d)
	{
		if (!j.contains(key) || !j[key].is_array() || j[key].size() != 3)
			return d;
		return Vec3(j[key][0].get<float>(), j[key][1].get<float>(), j[key][2].get<float>());
	}

	void HashMix(size_t& h, size_t v) { h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2); }
	void HashFloat(size_t& h, float f) { uint32 u; memcpy(&u, &f, 4); HashMix(h, u); }

	// 칸의 모양 (회전 · 뒤집기) 을 Tile Anchor 기준 점에 적용
	Vec2 ApplyFlags(Vec2 p, uint8 flags)
	{
		if (flags & Tilemap::FlipX) p.x = -p.x;
		if (flags & Tilemap::FlipY) p.y = -p.y;
		for (int r = 0; r < (flags & Tilemap::RotateMask); ++r)
			p = Vec2(-p.y, p.x);   // 반시계 90°
		return p;
	}

	// 타일 그림 사각형 (칸 + 모양) → Tilemap 로컬 네 점 (왼쪽 아래 · 오른쪽 아래 · 오른쪽 위 · 왼쪽 위)
	void TileCorners(const Tilemap& map, int x, int y, uint8 flags, const TileAssets::Tile& t, Vec3 out[4])
	{
		const float x0 = -t.Pivot.x * t.Size.x, x1 = (1.0f - t.Pivot.x) * t.Size.x;
		const float y0 = -t.Pivot.y * t.Size.y, y1 = (1.0f - t.Pivot.y) * t.Size.y;
		const Vec2 c[4] = { Vec2(x0, y0), Vec2(x1, y0), Vec2(x1, y1), Vec2(x0, y1) };
		const Vec3 o = map.CellCenterLocal(x, y);
		for (int i = 0; i < 4; ++i)
		{
			const Vec2 p = ApplyFlags(c[i], flags);
			out[i] = Vec3(o.x + p.x, o.y + p.y, o.z);
		}
	}

	Tilemap* SiblingTilemap(GameObject* go) { return go ? go->GetComponent<Tilemap>() : nullptr; }

	bool Near(float a, float b) { return fabsf(a - b) < 1e-4f; }
}

// ------------------------------------------------------------------ Grid
void Grid::OnInspectorGUI()
{
	bool changed = false;
	changed |= UnityGUI::Vector3("Cell Size", &CellSize.x);
	changed |= UnityGUI::Vector3("Cell Gap", &CellGap.x);
	CellSize.x = (std::max)(0.0001f, CellSize.x);
	CellSize.y = (std::max)(0.0001f, CellSize.y);
	CellGap.x = (std::max)(-CellSize.x * 0.99f, CellGap.x);
	CellGap.y = (std::max)(-CellSize.y * 0.99f, CellGap.y);
	static const char* layouts[] = { "Rectangle" };
	static const char* swizzles[] = { "XYZ" };
	int zero = 0;
	UnityGUI::Dropdown("Cell Layout", &zero, layouts, 1, 0, true);
	UnityGUI::Dropdown("Cell Swizzle", &zero, swizzles, 1, 0, true);
	if (changed && m_pGameObject)
		for (GameObject* child : m_pGameObject->GetChildren())
			if (Tilemap* t = SiblingTilemap(child))
				t->MarkChanged();
}

GENERATE_COMPONENT_FUNC_TOJSON(Grid)
{
	json j;
	SERIALIZE_TYPE(j, Grid);
	j["cellSize"] = V3(CellSize);
	j["cellGap"] = V3(CellGap);
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(Grid)
{
	CellSize = ReadV3(j, "cellSize", Vec3(1, 1, 0));
	CellGap = ReadV3(j, "cellGap", Vec3(0, 0, 0));
}

// ------------------------------------------------------------------ Tilemap
uint16 Tilemap::TileIndex(const std::string& tile)
{
	for (size_t i = 0; i < m_Tiles.size(); ++i)
		if (m_Tiles[i] == tile)
			return (uint16)i;
	if (m_Tiles.size() >= 0xffff)
		CompactTiles();
	m_Tiles.push_back(tile);
	return (uint16)(m_Tiles.size() - 1);
}

void Tilemap::CompactTiles()
{
	std::vector<int> remap(m_Tiles.size(), -1);
	std::vector<std::string> used;
	for (auto& [k, c] : m_Cells)
	{
		if (remap[c.Tile] < 0)
		{
			remap[c.Tile] = (int)used.size();
			used.push_back(m_Tiles[c.Tile]);
		}
		c.Tile = (uint16)remap[c.Tile];
	}
	m_Tiles = std::move(used);
}

void Tilemap::MarkChanged()
{
	++m_Revision;
	if (m_pGameObject)
		if (TilemapCollider2D* col = m_pGameObject->GetComponent<TilemapCollider2D>())
			col->Touch();
}

bool Tilemap::SetTile(int x, int y, const std::string& tile, uint8 flags)
{
	const int64 k = Key(x, y);
	auto it = m_Cells.find(k);
	if (tile.empty())
	{
		if (it == m_Cells.end())
			return false;
		m_Cells.erase(it);
		MarkChanged();
		return true;
	}
	const uint16 idx = TileIndex(tile);
	if (it != m_Cells.end() && it->second.Tile == idx && it->second.Flags == flags)
		return false;
	m_Cells[k] = Cell{ idx, flags };
	MarkChanged();
	return true;
}

std::string Tilemap::GetTile(int x, int y) const
{
	auto it = m_Cells.find(Key(x, y));
	return it == m_Cells.end() ? std::string() : m_Tiles[it->second.Tile];
}

uint8 Tilemap::GetFlags(int x, int y) const
{
	auto it = m_Cells.find(Key(x, y));
	return it == m_Cells.end() ? 0 : it->second.Flags;
}

int Tilemap::BoxFill(int x0, int y0, int x1, int y1, const std::string& tile, uint8 flags)
{
	if (x0 > x1) std::swap(x0, x1);
	if (y0 > y1) std::swap(y0, y1);
	int n = 0;
	for (int y = y0; y <= y1; ++y)
		for (int x = x0; x <= x1; ++x)
			n += SetTile(x, y, tile, flags) ? 1 : 0;
	return n;
}

int Tilemap::FloodFill(int x, int y, const std::string& tile, uint8 flags)
{
	const std::string from = GetTile(x, y);
	if (from == tile)
		return 0;
	int bx0 = x, by0 = y, bx1 = x, by1 = y;
	int mx0, my0, mx1, my1;
	if (CellBounds(mx0, my0, mx1, my1))
	{
		bx0 = (std::min)(bx0, mx0); by0 = (std::min)(by0, my0);
		bx1 = (std::max)(bx1, mx1); by1 = (std::max)(by1, my1);
	}
	int n = 0;
	std::vector<std::pair<int, int>> stack = { { x, y } };
	while (!stack.empty())
	{
		auto [cx, cy] = stack.back();
		stack.pop_back();
		if (cx < bx0 || cx > bx1 || cy < by0 || cy > by1 || GetTile(cx, cy) != from)
			continue;
		SetTile(cx, cy, tile, flags);
		++n;
		stack.push_back({ cx + 1, cy });
		stack.push_back({ cx - 1, cy });
		stack.push_back({ cx, cy + 1 });
		stack.push_back({ cx, cy - 1 });
	}
	return n;
}

void Tilemap::ClearAllTiles()
{
	if (m_Cells.empty())
		return;
	m_Cells.clear();
	m_Tiles.clear();
	MarkChanged();
}

bool Tilemap::CellBounds(int& minX, int& minY, int& maxX, int& maxY) const
{
	if (m_Cells.empty())
		return false;
	minX = minY = INT_MAX;
	maxX = maxY = INT_MIN;
	for (const auto& [k, c] : m_Cells)
	{
		const int x = KeyX(k), y = KeyY(k);
		minX = (std::min)(minX, x); minY = (std::min)(minY, y);
		maxX = (std::max)(maxX, x); maxY = (std::max)(maxY, y);
	}
	return true;
}

const std::vector<std::string>& Tilemap::UsedTiles() const
{
	if (m_UsedRevision == m_Revision)
		return m_Used;
	m_UsedRevision = m_Revision;
	std::vector<bool> used(m_Tiles.size(), false);
	for (const auto& [k, c] : m_Cells)
		used[c.Tile] = true;
	m_Used.clear();
	for (size_t i = 0; i < m_Tiles.size(); ++i)
		if (used[i])
			m_Used.push_back(m_Tiles[i]);
	return m_Used;
}

void Tilemap::ForEachCell(const std::function<void(int, int, const std::string&, uint8)>& fn) const
{
	for (const auto& [k, c] : m_Cells)
		fn(KeyX(k), KeyY(k), m_Tiles[c.Tile], c.Flags);
}

Grid* Tilemap::LayoutGrid() const
{
	for (GameObject* g = m_pGameObject; g; g = g->GetParent())
		if (Grid* grid = g->GetComponent<Grid>())
			return grid;
	return nullptr;
}

Vec2 Tilemap::CellSize() const
{
	const Grid* g = LayoutGrid();
	return g ? Vec2(g->CellSize.x, g->CellSize.y) : Vec2(1, 1);
}

Vec2 Tilemap::CellPitch() const
{
	const Grid* g = LayoutGrid();
	return g ? g->Pitch() : Vec2(1, 1);
}

Vec3 Tilemap::CellToLocal(int x, int y) const
{
	const Vec2 p = CellPitch();
	return Vec3(x * p.x, y * p.y, 0.0f);
}

Vec3 Tilemap::CellCenterLocal(int x, int y) const
{
	const Vec2 s = CellSize();
	return CellToLocal(x, y) + Vec3(TileAnchor.x * s.x, TileAnchor.y * s.y, TileAnchor.z);
}

void Tilemap::LocalToCell(const Vec3& local, int& x, int& y) const
{
	const Vec2 p = CellPitch();
	x = (int)floorf(local.x / p.x);
	y = (int)floorf(local.y / p.y);
}

Vec3 Tilemap::CellToWorld(int x, int y) const
{
	return Vec3::Transform(CellToLocal(x, y), m_pGameObject->GetTransform()->GetWorldMatrix());
}

void Tilemap::WorldToCell(const Vec3& world, int& x, int& y) const
{
	LocalToCell(Vec3::Transform(world, m_pGameObject->GetTransform()->GetWorldMatrix().Invert()), x, y);
}

void Tilemap::OnInspectorGUI()
{
	bool changed = false;
	changed |= UnityGUI::Color("Color", Color);
	changed |= UnityGUI::Vector3("Tile Anchor", &TileAnchor.x);
	static const char* orientations[] = { "XY" };
	int zero = 0;
	UnityGUI::Dropdown("Orientation", &zero, orientations, 1, 0, true);
	if (changed)
		MarkChanged();
	if (UnityGUI::FoldoutPlain("Info", 0, false))
	{
		char buf[128];
		int x0, y0, x1, y1;
		if (CellBounds(x0, y0, x1, y1))
			snprintf(buf, sizeof(buf), "%d tiles, %d kinds, bounds (%d, %d) .. (%d, %d)", Count(), (int)UsedTiles().size(), x0, y0, x1, y1);
		else
			snprintf(buf, sizeof(buf), "empty");
		UnityGUI::ValueLabel("Tiles", buf, 1);
	}
	if (Count() == 0)
		UnityGUI::HelpBox("Window > Tile Palette: drag a sliced tileset into a palette, pick a tile, then paint in the Scene view.", false);
}

// 칸 = 평평한 정수 배열 [x, y, 타일 번호, 모양, ...] + 쓰는 타일 경로 (쓰지 않는 경로는 빼고 번호를 다시 매긴다)
GENERATE_COMPONENT_FUNC_TOJSON(Tilemap)
{
	json j;
	SERIALIZE_TYPE(j, Tilemap);
	j["color"] = { Color[0], Color[1], Color[2], Color[3] };
	j["tileAnchor"] = V3(TileAnchor);
	std::vector<int> remap(m_Tiles.size(), -1);
	json tiles = json::array();
	std::vector<std::pair<int64, Cell>> cells(m_Cells.begin(), m_Cells.end());
	std::sort(cells.begin(), cells.end(), [](const auto& a, const auto& b) {
		return KeyY(a.first) != KeyY(b.first) ? KeyY(a.first) < KeyY(b.first) : KeyX(a.first) < KeyX(b.first);
	});
	std::vector<int> flat;
	flat.reserve(cells.size() * 4);
	for (const auto& [k, c] : cells)
	{
		if (remap[c.Tile] < 0)
		{
			remap[c.Tile] = (int)tiles.size();
			tiles.push_back(m_Tiles[c.Tile]);
		}
		flat.push_back(KeyX(k));
		flat.push_back(KeyY(k));
		flat.push_back(remap[c.Tile]);
		flat.push_back(c.Flags);
	}
	j["tiles"] = tiles;
	j["cells"] = flat;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(Tilemap)
{
	if (j.contains("color") && j["color"].is_array() && j["color"].size() == 4)
		for (int i = 0; i < 4; ++i)
			Color[i] = j["color"][i].get<float>();
	TileAnchor = ReadV3(j, "tileAnchor", Vec3(0.5f, 0.5f, 0.0f));
	m_Tiles.clear();
	m_Cells.clear();
	if (j.contains("tiles") && j["tiles"].is_array())
		for (const auto& t : j["tiles"])
			m_Tiles.push_back(t.is_string() ? t.get<std::string>() : std::string());
	if (j.contains("cells") && j["cells"].is_array())
	{
		const json& c = j["cells"];
		for (size_t i = 0; i + 3 < c.size(); i += 4)
		{
			const int idx = c[i + 2].get<int>();
			if (idx < 0 || idx >= (int)m_Tiles.size())
				continue;
			m_Cells[Key(c[i].get<int>(), c[i + 1].get<int>())] = Cell{ (uint16)idx, (uint8)c[i + 3].get<int>() };
		}
	}
	MarkChanged();
}

// ------------------------------------------------------------------ Tilemap Renderer
void TilemapRenderer::Rebuild()
{
	Tilemap* map = SiblingTilemap(m_pGameObject);
	if (map == nullptr)
	{
		m_Quads.clear();
		m_Signature = 0;
		return;
	}
	// 쓰는 타일을 확인한다 (파일 · 그림이 바뀌었으면 TileAssets::Revision 이 오른다)
	const std::vector<std::string>& used = map->UsedTiles();
	std::vector<const TileAssets::Tile*> tiles;
	for (const std::string& p : used)
		tiles.push_back(TileAssets::Get(p));
	size_t sig = 1;
	HashMix(sig, map->Revision());
	HashMix(sig, TileAssets::Revision());
	const Vec2 cs = map->CellSize(), cp = map->CellPitch();
	HashFloat(sig, cs.x); HashFloat(sig, cs.y); HashFloat(sig, cp.x); HashFloat(sig, cp.y);
	if (sig == m_Signature)
		return;
	m_Signature = sig;
	m_Quads.clear();
	m_Min = Vec3(FLT_MAX, FLT_MAX, -0.01f);
	m_Max = Vec3(-FLT_MAX, -FLT_MAX, 0.01f);
	std::unordered_map<std::string, const TileAssets::Tile*> byPath;
	for (size_t i = 0; i < used.size(); ++i)
		byPath[used[i]] = tiles[i];
	m_Quads.reserve(map->Count());
	map->ForEachCell([&](int x, int y, const std::string& path, uint8 flags) {
		const TileAssets::Tile* t = byPath[path];
		if (t == nullptr || !t->HasSprite)
			return;
		Quad q;
		TileCorners(*map, x, y, flags, *t, q.P);
		q.UV[0] = Vec2(t->UV.x, t->UV.w);
		q.UV[1] = Vec2(t->UV.z, t->UV.w);
		q.UV[2] = Vec2(t->UV.z, t->UV.y);
		q.UV[3] = Vec2(t->UV.x, t->UV.y);
		const float col[4] = { map->Color[0] * t->Color[0], map->Color[1] * t->Color[1], map->Color[2] * t->Color[2], map->Color[3] * t->Color[3] };
		q.Color = SpriteBatch::PackColor(col);
		q.Texture = t->Texture;
		q.Point = t->Point;
		for (const Vec3& p : q.P)
		{
			m_Min.x = (std::min)(m_Min.x, p.x); m_Min.y = (std::min)(m_Min.y, p.y);
			m_Max.x = (std::max)(m_Max.x, p.x); m_Max.y = (std::max)(m_Max.y, p.y);
		}
		m_Quads.push_back(q);
	});
	// 같은 텍스처끼리 (그리기 호출을 줄인다 — 타일끼리는 겹치지 않으니 순서가 보이지 않는다). 아래 줄부터 = 겹치는 큰 타일은 위 줄이 앞
	std::stable_sort(m_Quads.begin(), m_Quads.end(), [](const Quad& a, const Quad& b) { return a.Texture < b.Texture; });
}

void TilemapRenderer::CollectSprites(SpriteBatch& batch)
{
	if (m_pGameObject == nullptr)
		return;
	Rebuild();
	if (m_Quads.empty())
		return;
	const Matrix world = m_pGameObject->GetTransform()->GetWorldMatrix();
	batch.Begin(SortingLayerId, SortingOrder, m_pGameObject->GetTransform()->GetPosition());
	Vec3 p[4];
	for (const Quad& q : m_Quads)
	{
		for (int i = 0; i < 4; ++i)
			p[i] = Vec3::Transform(q.P[i], world);
		batch.Quad(p, q.UV, q.Color, q.Texture, q.Point);
	}
}

bool TilemapRenderer::SpriteLocalBounds(Vec3& bmin, Vec3& bmax)
{
	Rebuild();
	if (m_Quads.empty())
		return false;
	bmin = m_Min;
	bmax = m_Max;
	return true;
}

void TilemapRenderer::OnInspectorGUI()
{
	static const char* modes[] = { "Chunk" };
	int zero = 0;
	UnityGUI::Dropdown("Mode", &zero, modes, 1, 0, true);
	SpriteRenderer::SortingFields(SortingLayerId, SortingOrder);
	if (SiblingTilemap(m_pGameObject) == nullptr)
		UnityGUI::HelpBox("Tilemap Renderer draws the Tilemap on the same GameObject.");
}

GENERATE_COMPONENT_FUNC_TOJSON(TilemapRenderer)
{
	json j;
	SERIALIZE_TYPE(j, TilemapRenderer);
	j["enabled"] = m_Enabled;
	j["sortingOrder"] = SortingOrder;
	j["sortingLayerID"] = SortingLayerId;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(TilemapRenderer)
{
	m_Enabled = j.value("enabled", true);
	SortingOrder = j.value("sortingOrder", 0);
	SortingLayerId = j.value("sortingLayerID", 0);
	m_Signature = 0;
}

// ------------------------------------------------------------------ Tilemap Collider 2D
void TilemapCollider2D::BuildRects(std::vector<Rect>& out)
{
	Tilemap* map = SiblingTilemap(m_pGameObject);
	if (map == nullptr)
		return;
	const Vec2 cs = map->CellSize(), cp = map->CellPitch();
	const bool mergeable = Near(cs.x, cp.x) && Near(cs.y, cp.y);   // 간격이 있으면 칸끼리 닿지 않는다
	std::unordered_map<std::string, const TileAssets::Tile*> byPath;
	for (const std::string& p : map->UsedTiles())
		byPath[p] = TileAssets::Get(p);
	struct XY { int X, Y; };
	std::vector<XY> full;   // 칸 전체를 덮는 칸 (합칠 수 있다)
	map->ForEachCell([&](int x, int y, const std::string& path, uint8 flags) {
		const TileAssets::Tile* t = byPath[path];
		if (t == nullptr || t->Collider == TileAssets::ColliderType::None)
			return;
		const Vec3 c0 = map->CellToLocal(x, y);
		Rect cell{ c0.x, c0.y, c0.x + cs.x, c0.y + cs.y };
		Rect r = cell;
		if (t->Collider == TileAssets::ColliderType::Sprite)
		{
			if (!t->HasSprite)
				return;
			Vec3 q[4];
			TileCorners(*map, x, y, flags, *t, q);
			r = { FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX };
			for (const Vec3& p : q)
			{
				r.X0 = (std::min)(r.X0, p.x); r.Y0 = (std::min)(r.Y0, p.y);
				r.X1 = (std::max)(r.X1, p.x); r.Y1 = (std::max)(r.Y1, p.y);
			}
		}
		const bool isCell = Near(r.X0, cell.X0) && Near(r.Y0, cell.Y0) && Near(r.X1, cell.X1) && Near(r.Y1, cell.Y1);
		if (isCell && mergeable)
			full.push_back({ x, y });
		else if (r.X1 - r.X0 > 1e-5f && r.Y1 - r.Y0 > 1e-5f)
			out.push_back(r);
	});
	// 큰 사각형으로 합치기: 아래 줄 · 왼쪽부터 오른쪽으로 늘이고, 같은 폭으로 위 줄들을 붙인다
	std::sort(full.begin(), full.end(), [](const XY& a, const XY& b) { return a.Y != b.Y ? a.Y < b.Y : a.X < b.X; });
	auto key = [](int x, int y) { return ((int64)x << 32) | (uint32)y; };
	std::unordered_map<int64, bool> open;   // 아직 쓰지 않은 칸
	open.reserve(full.size());
	for (const XY& c : full)
		open[key(c.X, c.Y)] = true;
	auto take = [&](int x, int y) {
		auto it = open.find(key(x, y));
		return it != open.end() && it->second;
	};
	for (const XY& c : full)
	{
		if (!take(c.X, c.Y))
			continue;
		int x1 = c.X;
		while (take(x1 + 1, c.Y))
			++x1;
		int y1 = c.Y;
		for (;;)
		{
			bool row = true;
			for (int x = c.X; x <= x1 && row; ++x)
				row = take(x, y1 + 1);
			if (!row)
				break;
			++y1;
		}
		for (int y = c.Y; y <= y1; ++y)
			for (int x = c.X; x <= x1; ++x)
				open[key(x, y)] = false;
		const Vec3 a = map->CellToLocal(c.X, c.Y), b = map->CellToLocal(x1 + 1, y1 + 1);
		out.push_back({ a.x, a.y, b.x, b.y });
	}
}

void TilemapCollider2D::BuildShapes(std::vector<Shape2D>& out)
{
	std::vector<Rect> rects;
	BuildRects(rects);
	for (const Rect& r : rects)
	{
		Shape2D s;
		s.K = Shape2D::Polygon;
		s.Points = { Offset + Vec2(r.X0, r.Y0), Offset + Vec2(r.X1, r.Y0), Offset + Vec2(r.X1, r.Y1), Offset + Vec2(r.X0, r.Y1) };
		out.push_back(s);
	}
}

void TilemapCollider2D::Outline(std::vector<std::vector<Vec2>>& loops)
{
	std::vector<Rect> rects;
	BuildRects(rects);
	for (const Rect& r : rects)
		loops.push_back({ Offset + Vec2(r.X0, r.Y0), Offset + Vec2(r.X1, r.Y0), Offset + Vec2(r.X1, r.Y1), Offset + Vec2(r.X0, r.Y1) });
}

int TilemapCollider2D::ShapeCount()
{
	std::vector<Rect> rects;
	BuildRects(rects);
	return (int)rects.size();
}

void TilemapCollider2D::CheckChanged()
{
	Tilemap* map = SiblingTilemap(m_pGameObject);
	if (map == nullptr)
		return;
	for (const std::string& p : map->UsedTiles())
		TileAssets::Get(p);   // 파일이 바뀌었는지 (0.5 초마다)
	size_t sig = 1;
	HashMix(sig, map->Revision());
	HashMix(sig, TileAssets::Revision());
	const Vec2 cp = map->CellPitch(), cs = map->CellSize();
	HashFloat(sig, cp.x); HashFloat(sig, cp.y); HashFloat(sig, cs.x); HashFloat(sig, cs.y);
	if (sig != m_Seen)
	{
		m_Seen = sig;
		Touch();
	}
}

void TilemapCollider2D::OnInspectorGUI()
{
	if (CommonInspector())
		Touch();
	char buf[64];
	snprintf(buf, sizeof(buf), "%d boxes (touching tiles merged)", ShapeCount());
	UnityGUI::ValueLabel("Shapes", buf);
	if (SiblingTilemap(m_pGameObject) == nullptr)
		UnityGUI::HelpBox("Tilemap Collider 2D needs a Tilemap on the same GameObject.");
}

GENERATE_COMPONENT_FUNC_TOJSON(TilemapCollider2D)
{
	json j;
	SERIALIZE_TYPE(j, TilemapCollider2D);
	CommonToJson(j);
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(TilemapCollider2D)
{
	CommonFromJson(j);
}
