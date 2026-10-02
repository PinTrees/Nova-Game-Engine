#include "pch.h"
#include "NavData.h"
#include "Recast.h"
#include "DetourNavMesh.h"
#include "DetourNavMeshBuilder.h"
#include "DetourNavMeshQuery.h"
#include "SceneViewOverlay.h"
#include <fstream>
#include <thread>
#include <atomic>
#include <cmath>

namespace
{
	constexpr uint32_t kMagic = 0x4D4E564E;   // 'NVNM'
	constexpr uint32_t kVersion = 1;
	constexpr int kTileSize = 128;            // 복셀
	constexpr int kMaxPathPolys = 4096;
	constexpr int kMaxCorners = 512;

	// Recast 로그는 끈다 (타일마다 여러 스레드에서 만든다)
	class QuietContext : public rcContext
	{
	public:
		QuietContext() : rcContext(false) {}
	};

	struct TileResult
	{
		int X = 0, Y = 0;
		unsigned char* Data = nullptr;
		int Size = 0;
	};

	const dtQueryFilter& Filter()
	{
		static dtQueryFilter filter;   // 기본: 모든 플래그(우리는 걸을 수 있는 다각형에 1 을 단다)
		return filter;
	}

	struct FileHeader
	{
		uint32_t Magic, Version;
		NavBakeSettings Settings;
		float BoundsMin[3], BoundsMax[3];
		dtNavMeshParams Params;
		int TileCount;
	};
}

NavData::NavData() {}
NavData::~NavData() { Reset(); }

void NavData::Reset()
{
	if (m_Query) dtFreeNavMeshQuery(m_Query);
	if (m_Mesh) dtFreeNavMesh(m_Mesh);
	m_Query = nullptr;
	m_Mesh = nullptr;
	TileCount = 0;
	PolyCount = 0;
}

bool NavData::InitQuery()
{
	m_Query = dtAllocNavMeshQuery();
	return m_Query && dtStatusSucceed(m_Query->init(m_Mesh, 8192));
}

bool NavData::ContainsXZ(const Vec3& p) const
{
	return p.x >= BoundsMin[0] && p.x <= BoundsMax[0] && p.z >= BoundsMin[2] && p.z <= BoundsMax[2];
}

// ---------------------------------------------------------------------- 굽기
bool NavData::Bake(const std::vector<float>& verts, const std::vector<int>& tris, const Vec3& boundsMin, const Vec3& boundsMax,
	const NavBakeSettings& s, std::string& log)
{
	Reset();
	const auto t0 = std::chrono::steady_clock::now();
	Settings = s;
	const int ntris = (int)(tris.size() / 3), nverts = (int)(verts.size() / 3);
	if (ntris == 0)
	{
		log = "no geometry (static colliders) in the bake bounds";
		return false;
	}
	float bmin[3] = { boundsMin.x, boundsMin.y, boundsMin.z }, bmax[3] = { boundsMax.x, boundsMax.y, boundsMax.z };
	// 칸이 너무 많으면 복셀을 키운다 (4096² 칸)
	float cs = (std::max)(0.05f, s.CellSize);
	const double area = (double)(bmax[0] - bmin[0]) * (bmax[2] - bmin[2]);
	if (area / ((double)cs * cs) > 4096.0 * 4096.0)
		cs = (float)std::sqrt(area / (4096.0 * 4096.0));
	Settings.CellSize = cs;
	const float ch = (std::max)(0.02f, cs * 0.5f);

	int gw = 0, gh = 0;
	rcCalcGridSize(bmin, bmax, cs, &gw, &gh);
	const int tw = (gw + kTileSize - 1) / kTileSize, th = (gh + kTileSize - 1) / kTileSize;
	const float tileWorld = kTileSize * cs;

	dtNavMeshParams params{};
	rcVcopy(params.orig, bmin);
	params.tileWidth = tileWorld;
	params.tileHeight = tileWorld;
	params.maxTiles = tw * th;
	params.maxPolys = 1 << 16;
	m_Mesh = dtAllocNavMesh();
	if (m_Mesh == nullptr || dtStatusFailed(m_Mesh->init(&params)))
	{
		log = "could not create the nav mesh";
		Reset();
		return false;
	}

	rcConfig base{};
	base.cs = cs;
	base.ch = ch;
	base.walkableSlopeAngle = std::clamp(s.MaxSlope, 0.0f, 89.0f);
	base.walkableHeight = (int)std::ceil(s.AgentHeight / ch);
	base.walkableClimb = (int)std::floor(s.StepHeight / ch);
	base.walkableRadius = (int)std::ceil(s.AgentRadius / cs);
	base.maxEdgeLen = (int)(12.0f / cs);
	base.maxSimplificationError = 1.3f;
	base.minRegionArea = 8 * 8;
	base.mergeRegionArea = 20 * 20;
	base.maxVertsPerPoly = 6;
	base.tileSize = kTileSize;
	base.borderSize = base.walkableRadius + 3;
	base.width = base.height = kTileSize + base.borderSize * 2;
	base.detailSampleDist = cs * 6.0f < 0.9f ? 0.0f : cs * 6.0f;
	base.detailSampleMaxError = ch;

	// 삼각형을 타일(+ 테두리)마다 나눈다
	const float border = base.borderSize * cs;
	std::vector<std::vector<int>> buckets((size_t)tw * th);
	for (int t = 0; t < ntris; ++t)
	{
		float mnx = FLT_MAX, mxx = -FLT_MAX, mnz = FLT_MAX, mxz = -FLT_MAX;
		for (int k = 0; k < 3; ++k)
		{
			const float* v = &verts[(size_t)tris[t * 3 + k] * 3];
			mnx = (std::min)(mnx, v[0]); mxx = (std::max)(mxx, v[0]);
			mnz = (std::min)(mnz, v[2]); mxz = (std::max)(mxz, v[2]);
		}
		const int x0 = std::clamp((int)std::floor((mnx - border - bmin[0]) / tileWorld), 0, tw - 1);
		const int x1 = std::clamp((int)std::floor((mxx + border - bmin[0]) / tileWorld), 0, tw - 1);
		const int z0 = std::clamp((int)std::floor((mnz - border - bmin[2]) / tileWorld), 0, th - 1);
		const int z1 = std::clamp((int)std::floor((mxz + border - bmin[2]) / tileWorld), 0, th - 1);
		for (int z = z0; z <= z1; ++z)
			for (int x = x0; x <= x1; ++x)
				buckets[(size_t)z * tw + x].push_back(t);
	}

	auto buildTile = [&](int tx, int ty) -> TileResult
	{
		TileResult out;
		out.X = tx;
		out.Y = ty;
		const std::vector<int>& idx = buckets[(size_t)ty * tw + tx];
		if (idx.empty())
			return out;
		rcConfig cfg = base;
		cfg.bmin[0] = bmin[0] + tx * tileWorld - border;
		cfg.bmin[1] = bmin[1];
		cfg.bmin[2] = bmin[2] + ty * tileWorld - border;
		cfg.bmax[0] = bmin[0] + (tx + 1) * tileWorld + border;
		cfg.bmax[1] = bmax[1];
		cfg.bmax[2] = bmin[2] + (ty + 1) * tileWorld + border;

		QuietContext ctx;
		std::vector<int> local(idx.size() * 3);
		for (size_t i = 0; i < idx.size(); ++i)
			for (int k = 0; k < 3; ++k)
				local[i * 3 + k] = tris[(size_t)idx[i] * 3 + k];
		std::vector<unsigned char> areas(idx.size(), 0);

		rcHeightfield* solid = rcAllocHeightfield();
		rcCompactHeightfield* chf = nullptr;
		rcContourSet* cset = nullptr;
		rcPolyMesh* pmesh = nullptr;
		rcPolyMeshDetail* dmesh = nullptr;
		bool ok = solid && rcCreateHeightfield(&ctx, *solid, cfg.width, cfg.height, cfg.bmin, cfg.bmax, cfg.cs, cfg.ch);
		if (ok)
		{
			rcMarkWalkableTriangles(&ctx, cfg.walkableSlopeAngle, verts.data(), nverts, local.data(), (int)idx.size(), areas.data());
			ok = rcRasterizeTriangles(&ctx, verts.data(), nverts, local.data(), areas.data(), (int)idx.size(), *solid, cfg.walkableClimb);
		}
		if (ok)
		{
			rcFilterLowHangingWalkableObstacles(&ctx, cfg.walkableClimb, *solid);
			rcFilterLedgeSpans(&ctx, cfg.walkableHeight, cfg.walkableClimb, *solid);
			rcFilterWalkableLowHeightSpans(&ctx, cfg.walkableHeight, *solid);
			chf = rcAllocCompactHeightfield();
			ok = chf && rcBuildCompactHeightfield(&ctx, cfg.walkableHeight, cfg.walkableClimb, *solid, *chf);
		}
		rcFreeHeightField(solid);
		ok = ok && rcErodeWalkableArea(&ctx, cfg.walkableRadius, *chf)
			&& rcBuildDistanceField(&ctx, *chf)
			&& rcBuildRegions(&ctx, *chf, cfg.borderSize, cfg.minRegionArea, cfg.mergeRegionArea);
		if (ok)
		{
			cset = rcAllocContourSet();
			ok = cset && rcBuildContours(&ctx, *chf, cfg.maxSimplificationError, cfg.maxEdgeLen, *cset) && cset->nconts > 0;
		}
		if (ok)
		{
			pmesh = rcAllocPolyMesh();
			ok = pmesh && rcBuildPolyMesh(&ctx, *cset, cfg.maxVertsPerPoly, *pmesh);
		}
		if (ok)
		{
			dmesh = rcAllocPolyMeshDetail();
			ok = dmesh && rcBuildPolyMeshDetail(&ctx, *pmesh, *chf, cfg.detailSampleDist, cfg.detailSampleMaxError, *dmesh);
		}
		if (ok && pmesh->nverts < 0xffff && pmesh->npolys > 0)
		{
			for (int i = 0; i < pmesh->npolys; ++i)
				if (pmesh->areas[i] == RC_WALKABLE_AREA)
				{
					pmesh->areas[i] = 0;
					pmesh->flags[i] = 1;
				}
			dtNavMeshCreateParams p{};
			p.verts = pmesh->verts;
			p.vertCount = pmesh->nverts;
			p.polys = pmesh->polys;
			p.polyAreas = pmesh->areas;
			p.polyFlags = pmesh->flags;
			p.polyCount = pmesh->npolys;
			p.nvp = pmesh->nvp;
			p.detailMeshes = dmesh->meshes;
			p.detailVerts = dmesh->verts;
			p.detailVertsCount = dmesh->nverts;
			p.detailTris = dmesh->tris;
			p.detailTriCount = dmesh->ntris;
			p.walkableHeight = s.AgentHeight;
			p.walkableRadius = s.AgentRadius;
			p.walkableClimb = s.StepHeight;
			p.tileX = tx;
			p.tileY = ty;
			p.tileLayer = 0;
			rcVcopy(p.bmin, pmesh->bmin);
			rcVcopy(p.bmax, pmesh->bmax);
			p.cs = cfg.cs;
			p.ch = cfg.ch;
			p.buildBvTree = true;
			if (!dtCreateNavMeshData(&p, &out.Data, &out.Size))
			{
				out.Data = nullptr;
				out.Size = 0;
			}
		}
		rcFreeCompactHeightfield(chf);
		rcFreeContourSet(cset);
		rcFreePolyMesh(pmesh);
		rcFreePolyMeshDetail(dmesh);
		return out;
	};

	std::vector<TileResult> results((size_t)tw * th);
	std::atomic<int> next{ 0 };
	auto worker = [&]() {
		for (int i = next++; i < tw * th; i = next++)
			results[i] = buildTile(i % tw, i / tw);
	};
	{
		const int threads = (std::max)(1, (std::min)((int)std::thread::hardware_concurrency() - 1, tw * th));
		std::vector<std::thread> pool;
		for (int i = 0; i < threads; ++i)
			pool.emplace_back(worker);
		for (auto& t : pool)
			t.join();
	}
	for (TileResult& r : results)
	{
		if (r.Data == nullptr)
			continue;
		if (dtStatusSucceed(m_Mesh->addTile(r.Data, r.Size, DT_TILE_FREE_DATA, 0, nullptr)))
		{
			++TileCount;
			PolyCount += ((const dtMeshHeader*)r.Data)->polyCount;
		}
		else
			dtFree(r.Data);
	}
	rcVcopy(BoundsMin, bmin);
	rcVcopy(BoundsMax, bmax);
	if (!InitQuery() || PolyCount == 0)
	{
		log = "nothing walkable was found (check Max Slope / Step Height / Agent Radius)";
		Reset();
		return false;
	}
	const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	char buf[256];
	snprintf(buf, sizeof(buf), "%d tiles, %d polygons from %d triangles (voxel %.2f m), %.0f ms", TileCount, PolyCount, ntris, cs, ms);
	log = buf;
	return true;
}

// ---------------------------------------------------------------------- 저장 / 읽기
bool NavData::Save(const std::wstring& path) const
{
	if (m_Mesh == nullptr)
		return false;
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out)
		return false;
	const dtNavMesh* mesh = m_Mesh;
	FileHeader h{};
	h.Magic = kMagic;
	h.Version = kVersion;
	h.Settings = Settings;
	rcVcopy(h.BoundsMin, BoundsMin);
	rcVcopy(h.BoundsMax, BoundsMax);
	h.Params = *mesh->getParams();
	for (int i = 0; i < mesh->getMaxTiles(); ++i)
		if (const dtMeshTile* t = mesh->getTile(i); t && t->header && t->dataSize)
			++h.TileCount;
	out.write((const char*)&h, sizeof(h));
	for (int i = 0; i < mesh->getMaxTiles(); ++i)
	{
		const dtMeshTile* t = mesh->getTile(i);
		if (!t || !t->header || !t->dataSize)
			continue;
		const int size = t->dataSize;
		out.write((const char*)&size, sizeof(size));
		out.write((const char*)t->data, size);
	}
	return (bool)out;
}

bool NavData::Load(const std::wstring& path)
{
	Reset();
	std::ifstream in(path, std::ios::binary);
	if (!in)
		return false;
	FileHeader h{};
	in.read((char*)&h, sizeof(h));
	if (!in || h.Magic != kMagic || h.Version != kVersion || h.TileCount < 0 || h.TileCount > 1000000)
		return false;
	m_Mesh = dtAllocNavMesh();
	if (m_Mesh == nullptr || dtStatusFailed(m_Mesh->init(&h.Params)))
	{
		Reset();
		return false;
	}
	for (int i = 0; i < h.TileCount; ++i)
	{
		int size = 0;
		in.read((char*)&size, sizeof(size));
		if (!in || size <= 0 || size > 256 * 1024 * 1024)
			break;
		unsigned char* data = (unsigned char*)dtAlloc(size, DT_ALLOC_PERM);
		in.read((char*)data, size);
		if (!in || dtStatusFailed(m_Mesh->addTile(data, size, DT_TILE_FREE_DATA, 0, nullptr)))
		{
			dtFree(data);
			break;
		}
		++TileCount;
		PolyCount += ((const dtMeshHeader*)data)->polyCount;
	}
	Settings = h.Settings;
	rcVcopy(BoundsMin, h.BoundsMin);
	rcVcopy(BoundsMax, h.BoundsMax);
	if (!InitQuery())
	{
		Reset();
		return false;
	}
	return PolyCount > 0;
}

// ---------------------------------------------------------------------- 질의
bool NavData::Sample(const Vec3& p, float maxDistance, Vec3& out) const
{
	if (m_Query == nullptr)
		return false;
	const float ext[3] = { maxDistance, (std::max)(maxDistance, Settings.AgentHeight), maxDistance };
	dtPolyRef ref = 0;
	float pt[3];
	if (dtStatusFailed(m_Query->findNearestPoly(&p.x, ext, &Filter(), &ref, pt)) || ref == 0)
		return false;
	out = Vec3(pt[0], pt[1], pt[2]);
	return true;
}

bool NavData::FindPath(const Vec3& start, const Vec3& end, std::vector<Vec3>& corners, bool* partial) const
{
	corners.clear();
	if (partial) *partial = false;
	if (m_Query == nullptr)
		return false;
	const float snap = (std::max)(2.0f, Settings.AgentRadius * 4.0f);
	const float ext[3] = { snap, (std::max)(snap, Settings.AgentHeight * 2.0f), snap };
	dtPolyRef sRef = 0, eRef = 0;
	float sPt[3], ePt[3];
	if (dtStatusFailed(m_Query->findNearestPoly(&start.x, ext, &Filter(), &sRef, sPt)) || sRef == 0 ||
		dtStatusFailed(m_Query->findNearestPoly(&end.x, ext, &Filter(), &eRef, ePt)) || eRef == 0)
		return false;
	static thread_local std::vector<dtPolyRef> polys(kMaxPathPolys);
	int np = 0;
	if (dtStatusFailed(m_Query->findPath(sRef, eRef, sPt, ePt, &Filter(), polys.data(), &np, kMaxPathPolys)) || np == 0)
		return false;
	float endPt[3] = { ePt[0], ePt[1], ePt[2] };
	if (polys[np - 1] != eRef)
	{
		if (partial) *partial = true;
		m_Query->closestPointOnPoly(polys[np - 1], ePt, endPt, nullptr);
	}
	float straight[kMaxCorners * 3];
	unsigned char flags[kMaxCorners];
	dtPolyRef refs[kMaxCorners];
	int ns = 0;
	if (dtStatusFailed(m_Query->findStraightPath(sPt, endPt, polys.data(), np, straight, flags, refs, &ns, kMaxCorners)) || ns == 0)
		return false;
	for (int i = 0; i < ns; ++i)
		corners.push_back(Vec3(straight[i * 3], straight[i * 3 + 1], straight[i * 3 + 2]));
	if (corners.size() == 1)
		corners.push_back(corners[0]);
	return true;
}

bool NavData::MoveAlongSurface(const Vec3& from, const Vec3& to, Vec3& out) const
{
	out = to;
	if (m_Query == nullptr)
		return false;
	const float ext[3] = { (std::max)(0.5f, Settings.AgentRadius), Settings.AgentHeight, (std::max)(0.5f, Settings.AgentRadius) };
	dtPolyRef ref = 0;
	float pt[3];
	if (dtStatusFailed(m_Query->findNearestPoly(&from.x, ext, &Filter(), &ref, pt)) || ref == 0)
		return false;
	dtPolyRef visited[16];
	int nv = 0;
	float res[3];
	if (dtStatusFailed(m_Query->moveAlongSurface(ref, pt, &to.x, &Filter(), res, visited, &nv, 16)) || nv == 0)
		return false;
	float h = res[1];
	if (dtStatusSucceed(m_Query->getPolyHeight(visited[nv - 1], res, &h)))
		res[1] = h;
	out = Vec3(res[0], res[1], res[2]);
	return true;
}

// ---------------------------------------------------------------------- Scene 뷰 표시
void NavData::DrawGizmo(unsigned int fill, unsigned int edge, int maxTriangles) const
{
	if (m_Mesh == nullptr)
		return;
	const dtNavMesh* mesh = m_Mesh;
	ImDrawList* dl = ImGui::GetWindowDrawList();
	ImVec2 rmin, rmax, off;
	SceneViewOverlay::GetViewRect(rmin, rmax, off);
	dl->PushClipRect(rmin, rmax, true);
	int drawn = 0;
	auto project = [](const float* v, ImVec2& o) { return SceneViewOverlay::Project(XMFLOAT3(v[0], v[1] + 0.03f, v[2]), o); };
	for (int ti = 0; ti < mesh->getMaxTiles() && drawn < maxTriangles; ++ti)
	{
		const dtMeshTile* tile = mesh->getTile(ti);
		if (!tile || !tile->header)
			continue;
		for (int pi = 0; pi < tile->header->polyCount && drawn < maxTriangles; ++pi)
		{
			const dtPoly* poly = &tile->polys[pi];
			if (poly->getType() == DT_POLYTYPE_OFFMESH_CONNECTION)
				continue;
			const dtPolyDetail* pd = &tile->detailMeshes[pi];
			for (int j = 0; j < pd->triCount; ++j)
			{
				const unsigned char* t = &tile->detailTris[(pd->triBase + j) * 4];
				ImVec2 p[3];
				bool visible = true;
				for (int k = 0; k < 3 && visible; ++k)
				{
					const float* v = t[k] < poly->vertCount ? &tile->verts[poly->verts[t[k]] * 3] : &tile->detailVerts[(pd->vertBase + t[k] - poly->vertCount) * 3];
					visible = project(v, p[k]);
				}
				if (visible)
					dl->AddTriangleFilled(p[0], p[1], p[2], fill);
				++drawn;
			}
			// 바깥 가장자리 (이웃이 없는 변)
			for (int j = 0; j < poly->vertCount; ++j)
			{
				if (poly->neis[j] != 0)
					continue;
				ImVec2 a, b;
				if (project(&tile->verts[poly->verts[j] * 3], a) && project(&tile->verts[poly->verts[(j + 1) % poly->vertCount] * 3], b))
					dl->AddLine(a, b, edge, 1.5f);
			}
		}
	}
	dl->PopClipRect();
}
