#include "pch.h"
#include "NavData.h"
#include "Recast.h"
#include "DetourCommon.h"
#include "DetourNavMesh.h"
#include "DetourNavMeshBuilder.h"
#include "DetourNavMeshQuery.h"
#include "DetourTileCache.h"
#include "DetourTileCacheBuilder.h"
#include "fastlz.h"
#include "SceneViewOverlay.h"
#include <fstream>
#include <thread>
#include <atomic>
#include <cmath>

namespace
{
	constexpr uint32_t kMagic = 0x4D4E564E;   // 'NVNM'
	constexpr uint32_t kVersion = 3;          // 3 = 머리 뒤에 플래그 (1 = 2D 표면), 2 = TileCache 층 (그대로 읽는다), 1 = 다시 굽는다
	constexpr int kTileSize = 64;             // 복셀 (장애물이 바뀌면 이 크기만큼 다시 만든다)
	constexpr int kMaxLayersPerTile = 32;
	constexpr int kMaxObstacles = 1024;
	constexpr int kMaxPathPolys = 4096;
	constexpr int kMaxCorners = 512;

	// Recast 로그는 끈다 (타일마다 여러 스레드에서 만든다)
	class QuietContext : public rcContext
	{
	public:
		QuietContext() : rcContext(false) {}
	};

	// 층 데이터 압축 (RecastDemo 와 같은 FastLZ)
	struct FastLZCompressor : public dtTileCacheCompressor
	{
		int maxCompressedSize(const int bufferSize) override { return (int)(bufferSize * 1.05f) + 66; }
		dtStatus compress(const unsigned char* buffer, const int bufferSize, unsigned char* compressed, const int, int* compressedSize) override
		{
			*compressedSize = fastlz_compress((const void*)buffer, bufferSize, compressed);
			return DT_SUCCESS;
		}
		dtStatus decompress(const unsigned char* compressed, const int compressedSize, unsigned char* buffer, const int maxBufferSize, int* bufferSize) override
		{
			*bufferSize = fastlz_decompress(compressed, compressedSize, buffer, maxBufferSize);
			return *bufferSize < 0 ? DT_FAILURE : DT_SUCCESS;
		}
	};

	struct MeshProcess : public dtTileCacheMeshProcess
	{
		const NavData* Owner = nullptr;
		void process(dtNavMeshCreateParams* params, unsigned char* polyAreas, unsigned short* polyFlags) override
		{
			Owner->ProcessTile(params, polyAreas, polyFlags);
		}
	};

	struct LayerBlob
	{
		unsigned char* Data = nullptr;
		int Size = 0;
	};

	struct NavTileBlob
	{
		unsigned char* Data = nullptr;
		int Size = 0;
	};

	const dtQueryFilter& Filter()
	{
		static dtQueryFilter filter;   // 기본: 모든 플래그(걸을 수 있는 다각형·링크에 1 을 단다)
		return filter;
	}

	int NextPow2(int v)
	{
		int p = 1;
		while (p < v)
			p <<= 1;
		return p;
	}

	struct FileHeader
	{
		uint32_t Magic, Version;
		NavBakeSettings Settings;
		float BoundsMin[3], BoundsMax[3];
		dtNavMeshParams MeshParams;
		dtTileCacheParams CacheParams;
		int LayerCount;
	};

	// 압축한 층 하나 → Detour 타일 데이터 (dtTileCache::buildNavMeshTile 과 같은 순서, 장애물 없이 — 여러 스레드에서)
	NavTileBlob BuildNavTile(const dtTileCacheParams& tp, dtTileCacheCompressor* comp, dtTileCacheMeshProcess* proc, const dtCompressedTile* tile)
	{
		NavTileBlob out;
		dtTileCacheAlloc alloc;
		dtTileCacheLayer* layer = nullptr;
		dtTileCacheContourSet* lcset = nullptr;
		dtTileCachePolyMesh* lmesh = nullptr;
		const int climbVx = (int)(tp.walkableClimb / tp.ch);
		bool ok = dtStatusSucceed(dtDecompressTileCacheLayer(&alloc, comp, tile->data, tile->dataSize, &layer))
			&& dtStatusSucceed(dtBuildTileCacheRegions(&alloc, *layer, climbVx));
		if (ok)
		{
			lcset = dtAllocTileCacheContourSet(&alloc);
			ok = lcset && dtStatusSucceed(dtBuildTileCacheContours(&alloc, *layer, climbVx, tp.maxSimplificationError, *lcset));
		}
		if (ok)
		{
			lmesh = dtAllocTileCachePolyMesh(&alloc);
			ok = lmesh && dtStatusSucceed(dtBuildTileCachePolyMesh(&alloc, *lcset, *lmesh));
		}
		if (ok && lmesh->npolys > 0)
		{
			dtNavMeshCreateParams p{};
			p.verts = lmesh->verts;
			p.vertCount = lmesh->nverts;
			p.polys = lmesh->polys;
			p.polyAreas = lmesh->areas;
			p.polyFlags = lmesh->flags;
			p.polyCount = lmesh->npolys;
			p.nvp = DT_VERTS_PER_POLYGON;
			p.walkableHeight = tp.walkableHeight;
			p.walkableRadius = tp.walkableRadius;
			p.walkableClimb = tp.walkableClimb;
			p.tileX = tile->header->tx;
			p.tileY = tile->header->ty;
			p.tileLayer = tile->header->tlayer;
			p.cs = tp.cs;
			p.ch = tp.ch;
			p.buildBvTree = false;
			dtVcopy(p.bmin, tile->header->bmin);
			dtVcopy(p.bmax, tile->header->bmax);
			proc->process(&p, lmesh->areas, lmesh->flags);
			if (!dtCreateNavMeshData(&p, &out.Data, &out.Size))
			{
				out.Data = nullptr;
				out.Size = 0;
			}
		}
		dtFreeTileCachePolyMesh(&alloc, lmesh);
		dtFreeTileCacheContourSet(&alloc, lcset);
		if (layer)
			dtFreeTileCacheLayer(&alloc, layer);
		return out;
	}

	template <typename F>
	void ParallelFor(int count, F&& f)
	{
		std::atomic<int> next{ 0 };
		auto worker = [&]() {
			for (int i = next++; i < count; i = next++)
				f(i);
		};
		const int threads = (std::max)(1, (std::min)((int)std::thread::hardware_concurrency() - 1, count));
		std::vector<std::thread> pool;
		for (int i = 0; i < threads; ++i)
			pool.emplace_back(worker);
		for (auto& t : pool)
			t.join();
	}
}

NavData::NavData()
{
	m_Alloc = std::make_unique<dtTileCacheAlloc>();
	m_Compressor = std::make_unique<FastLZCompressor>();
	auto proc = std::make_unique<MeshProcess>();
	proc->Owner = this;
	m_Process = std::move(proc);
}

NavData::~NavData() { Reset(); }

void NavData::Reset()
{
	if (m_Query) dtFreeNavMeshQuery(m_Query);
	if (m_Mesh) dtFreeNavMesh(m_Mesh);
	if (m_Cache) dtFreeTileCache(m_Cache);
	m_Query = nullptr;
	m_Mesh = nullptr;
	m_Cache = nullptr;
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

void NavData::CountPolys()
{
	TileCount = 0;
	PolyCount = 0;
	if (m_Mesh == nullptr)
		return;
	const dtNavMesh* mesh = m_Mesh;
	for (int i = 0; i < mesh->getMaxTiles(); ++i)
		if (const dtMeshTile* t = mesh->getTile(i); t && t->header)
		{
			++TileCount;
			PolyCount += t->header->polyCount - t->header->offMeshConCount;
		}
}

// ---------------------------------------------------------------------- 타일 만들기 (TileCache 가 부른다)
void NavData::ProcessTile(dtNavMeshCreateParams* params, unsigned char* polyAreas, unsigned short* polyFlags) const
{
	for (int i = 0; i < params->polyCount; ++i)
	{
		const bool walkable = polyAreas[i] == DT_TILECACHE_WALKABLE_AREA;
		polyAreas[i] = 0;
		polyFlags[i] = walkable ? 1 : 0;
	}
	// Off-Mesh Link — Detour 가 시작 점이 이 타일 안인 것만 넣는다
	if (!m_Links.empty())
	{
		params->offMeshConVerts = m_LinkVerts.data();
		params->offMeshConRad = m_LinkRads.data();
		params->offMeshConDir = m_LinkDirs.data();
		params->offMeshConAreas = m_LinkAreas.data();
		params->offMeshConFlags = m_LinkFlags.data();
		params->offMeshConUserID = m_LinkIds.data();
		params->offMeshConCount = (int)m_Links.size();
	}
}

void NavData::RebuildLinkArrays()
{
	const size_t n = m_Links.size();
	m_LinkVerts.resize(n * 6);
	m_LinkRads.resize(n);
	m_LinkDirs.resize(n);
	m_LinkAreas.assign(n, 0);
	m_LinkFlags.assign(n, 1);
	m_LinkIds.resize(n);
	for (size_t i = 0; i < n; ++i)
	{
		const NavLink& l = m_Links[i];
		float* v = &m_LinkVerts[i * 6];
		v[0] = l.Start.x; v[1] = l.Start.y; v[2] = l.Start.z;
		v[3] = l.End.x; v[4] = l.End.y; v[5] = l.End.z;
		// 끝점은 반지름만큼 깎인 가장자리 밖에 놓이기 쉬우니 반지름 + 여유 안에서 메시를 찾는다
		m_LinkRads[i] = (std::max)(0.5f, Settings.AgentRadius + 0.3f) + l.Width * 0.5f;
		m_LinkDirs[i] = l.Bidirectional ? DT_OFFMESH_CON_BIDIR : 0;
		m_LinkIds[i] = (unsigned int)i;
	}
}

bool NavData::BuildAllTiles()
{
	if (m_Cache == nullptr || m_Mesh == nullptr)
		return false;
	std::vector<const dtCompressedTile*> tiles;
	for (int i = 0; i < m_Cache->getTileCount(); ++i)
		if (const dtCompressedTile* t = m_Cache->getTile(i); t && t->header)
			tiles.push_back(t);
	std::vector<NavTileBlob> built(tiles.size());
	const dtTileCacheParams params = *m_Cache->getParams();
	dtTileCacheCompressor* comp = m_Compressor.get();
	dtTileCacheMeshProcess* proc = m_Process.get();
	ParallelFor((int)tiles.size(), [&](int i) { built[i] = BuildNavTile(params, comp, proc, tiles[i]); });
	for (NavTileBlob& b : built)
	{
		if (b.Data == nullptr)
			continue;
		if (dtStatusFailed(m_Mesh->addTile(b.Data, b.Size, DT_TILE_FREE_DATA, 0, nullptr)))
			dtFree(b.Data);
	}
	CountPolys();
	return true;
}

// ---------------------------------------------------------------------- 굽기
bool NavData::Bake(const std::vector<float>& verts, const std::vector<int>& tris, const Vec3& boundsMin, const Vec3& boundsMax,
	const NavBakeSettings& s, std::string& log, const std::vector<NavBlocker2D>* blockers2D)
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
	base.maxVertsPerPoly = DT_VERTS_PER_POLYGON;
	base.borderSize = base.walkableRadius + 3;
	// 층의 가로·세로는 255 칸까지 (테두리 포함)
	base.tileSize = (std::min)(kTileSize, 255 - base.borderSize * 2);
	if (base.tileSize < 16)
	{
		log = "Agent Radius is too large for the Voxel Size (raise Voxel Size)";
		return false;
	}
	base.width = base.height = base.tileSize + base.borderSize * 2;
	const int tileSize = base.tileSize;

	int gw = 0, gh = 0;
	rcCalcGridSize(bmin, bmax, cs, &gw, &gh);
	const int tw = (gw + tileSize - 1) / tileSize, th = (gh + tileSize - 1) / tileSize;
	const float tileWorld = tileSize * cs;

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

	// 타일마다 복셀화 → 높이 층 → 압축 (여러 스레드)
	dtTileCacheCompressor* comp = m_Compressor.get();
	auto rasterize = [&](int tx, int ty, std::vector<LayerBlob>& out)
	{
		const std::vector<int>& idx = buckets[(size_t)ty * tw + tx];
		if (idx.empty())
			return;
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
		rcHeightfieldLayerSet* lset = nullptr;
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
		// 2D 장애물: 깎기 전에 걸을 수 없게 (반지름만큼 더 물러난다).
		// 칸 가운데만 보면 칸보다 얇은 벽 (Edge Collider 2D) 이 빠진다 → 삼각형을 반 칸 키운 볼록 다각형으로 (닿는 칸은 모두)
		if (ok && blockers2D)
			for (const NavBlocker2D& b : *blockers2D)
			{
				const float mnx = (std::min)({ b.X[0], b.X[1], b.X[2] }), mxx = (std::max)({ b.X[0], b.X[1], b.X[2] });
				const float mnz = (std::min)({ b.Z[0], b.Z[1], b.Z[2] }), mxz = (std::max)({ b.Z[0], b.Z[1], b.Z[2] });
				if (mxx < cfg.bmin[0] || mnx > cfg.bmax[0] || mxz < cfg.bmin[2] || mnz > cfg.bmax[2])
					continue;
				const float h = cfg.cs * 0.5f;
				std::vector<std::pair<float, float>> pts;
				for (int i = 0; i < 3; ++i)
					for (int k = 0; k < 4; ++k)
						pts.push_back({ b.X[i] + (k & 1 ? h : -h), b.Z[i] + (k & 2 ? h : -h) });
				// 볼록 껍질 (단조 사슬) — 반시계
				std::sort(pts.begin(), pts.end());
				auto cross = [](const std::pair<float, float>& o, const std::pair<float, float>& a, const std::pair<float, float>& c)
				{ return (a.first - o.first) * (c.second - o.second) - (a.second - o.second) * (c.first - o.first); };
				std::vector<std::pair<float, float>> hull(pts.size() * 2);
				size_t n = 0;
				for (size_t i = 0; i < pts.size(); ++i)
				{
					while (n >= 2 && cross(hull[n - 2], hull[n - 1], pts[i]) <= 0.0f) --n;
					hull[n++] = pts[i];
				}
				for (size_t i = pts.size() - 1, t = n + 1; i-- > 0;)
				{
					while (n >= t && cross(hull[n - 2], hull[n - 1], pts[i]) <= 0.0f) --n;
					hull[n++] = pts[i];
				}
				if (n > 1) --n;   // 마지막 = 처음
				std::vector<float> v;
				for (size_t i = 0; i < n; ++i)
				{
					v.push_back(hull[i].first);
					v.push_back(0.0f);
					v.push_back(hull[i].second);
				}
				if (n >= 3)
					rcMarkConvexPolyArea(&ctx, v.data(), (int)n, cfg.bmin[1], cfg.bmax[1], RC_NULL_AREA, *chf);
			}
		ok = ok && rcErodeWalkableArea(&ctx, cfg.walkableRadius, *chf);
		if (ok)
		{
			lset = rcAllocHeightfieldLayerSet();
			ok = lset && rcBuildHeightfieldLayers(&ctx, *chf, cfg.borderSize, cfg.walkableHeight, *lset);
		}
		if (ok)
		{
			for (int i = 0; i < (std::min)(lset->nlayers, kMaxLayersPerTile); ++i)
			{
				const rcHeightfieldLayer* layer = &lset->layers[i];
				dtTileCacheLayerHeader header{};
				header.magic = DT_TILECACHE_MAGIC;
				header.version = DT_TILECACHE_VERSION;
				header.tx = tx;
				header.ty = ty;
				header.tlayer = i;
				dtVcopy(header.bmin, layer->bmin);
				dtVcopy(header.bmax, layer->bmax);
				header.width = (unsigned char)layer->width;
				header.height = (unsigned char)layer->height;
				header.minx = (unsigned char)layer->minx;
				header.maxx = (unsigned char)layer->maxx;
				header.miny = (unsigned char)layer->miny;
				header.maxy = (unsigned char)layer->maxy;
				header.hmin = (unsigned short)layer->hmin;
				header.hmax = (unsigned short)layer->hmax;
				LayerBlob blob;
				if (dtStatusSucceed(dtBuildTileCacheLayer(comp, &header, layer->heights, layer->areas, layer->cons, &blob.Data, &blob.Size)))
					out.push_back(blob);
			}
		}
		rcFreeHeightfieldLayerSet(lset);
		rcFreeCompactHeightfield(chf);
	};
	std::vector<std::vector<LayerBlob>> layers((size_t)tw * th);
	ParallelFor(tw * th, [&](int i) { rasterize(i % tw, i / tw, layers[i]); });
	int layerCount = 0;
	for (auto& l : layers)
		layerCount += (int)l.size();
	if (layerCount == 0)
	{
		log = "nothing walkable was found (check Max Slope / Step Height / Agent Radius)";
		return false;
	}

	dtTileCacheParams tcp{};
	rcVcopy(tcp.orig, bmin);
	tcp.cs = cs;
	tcp.ch = ch;
	tcp.width = tileSize;
	tcp.height = tileSize;
	tcp.walkableHeight = s.AgentHeight;
	tcp.walkableRadius = s.AgentRadius;
	tcp.walkableClimb = s.StepHeight;
	tcp.maxSimplificationError = base.maxSimplificationError;
	tcp.maxTiles = NextPow2(layerCount);
	tcp.maxObstacles = kMaxObstacles;

	dtNavMeshParams params{};
	rcVcopy(params.orig, bmin);
	params.tileWidth = tileWorld;
	params.tileHeight = tileWorld;
	params.maxTiles = NextPow2(layerCount);
	params.maxPolys = 1 << 16;

	m_Cache = dtAllocTileCache();
	m_Mesh = dtAllocNavMesh();
	bool ok = m_Cache && m_Mesh && dtStatusSucceed(m_Cache->init(&tcp, m_Alloc.get(), m_Compressor.get(), m_Process.get()))
		&& dtStatusSucceed(m_Mesh->init(&params));
	for (auto& l : layers)
		for (LayerBlob& b : l)
		{
			if (!ok || dtStatusFailed(m_Cache->addTile(b.Data, b.Size, DT_COMPRESSEDTILE_FREE_DATA, nullptr)))
				dtFree(b.Data);
		}
	if (!ok)
	{
		log = "could not create the nav mesh";
		Reset();
		return false;
	}
	rcVcopy(BoundsMin, bmin);
	rcVcopy(BoundsMax, bmax);
	RebuildLinkArrays();
	BuildAllTiles();
	if (!InitQuery() || PolyCount == 0)
	{
		log = "nothing walkable was found (check Max Slope / Step Height / Agent Radius)";
		Reset();
		return false;
	}
	++Revision;
	const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	char buf[256];
	snprintf(buf, sizeof(buf), "%d tiles, %d polygons from %d triangles (voxel %.2f m), %.0f ms", TileCount, PolyCount, ntris, cs, ms);
	log = buf;
	return true;
}

// ---------------------------------------------------------------------- 저장 / 읽기 (압축한 층만 — 읽을 때 타일을 만든다)
bool NavData::Save(const std::wstring& path) const
{
	if (m_Mesh == nullptr || m_Cache == nullptr)
		return false;
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out)
		return false;
	FileHeader h{};
	h.Magic = kMagic;
	h.Version = kVersion;
	h.Settings = Settings;
	rcVcopy(h.BoundsMin, BoundsMin);
	rcVcopy(h.BoundsMax, BoundsMax);
	h.MeshParams = *m_Mesh->getParams();
	h.CacheParams = *m_Cache->getParams();
	for (int i = 0; i < m_Cache->getTileCount(); ++i)
		if (const dtCompressedTile* t = m_Cache->getTile(i); t && t->header && t->dataSize)
			++h.LayerCount;
	out.write((const char*)&h, sizeof(h));
	const uint32_t flags = Plane2D ? 1u : 0u;
	out.write((const char*)&flags, sizeof(flags));
	for (int i = 0; i < m_Cache->getTileCount(); ++i)
	{
		const dtCompressedTile* t = m_Cache->getTile(i);
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
	if (!in || h.Magic != kMagic || (h.Version != kVersion && h.Version != 2) || h.LayerCount < 0 || h.LayerCount > 1000000)
		return false;
	uint32_t flags = 0;
	if (h.Version >= 3)
		in.read((char*)&flags, sizeof(flags));
	Plane2D = (flags & 1u) != 0;
	Settings = h.Settings;
	rcVcopy(BoundsMin, h.BoundsMin);
	rcVcopy(BoundsMax, h.BoundsMax);
	m_Cache = dtAllocTileCache();
	m_Mesh = dtAllocNavMesh();
	if (m_Cache == nullptr || m_Mesh == nullptr || dtStatusFailed(m_Cache->init(&h.CacheParams, m_Alloc.get(), m_Compressor.get(), m_Process.get()))
		|| dtStatusFailed(m_Mesh->init(&h.MeshParams)))
	{
		Reset();
		return false;
	}
	for (int i = 0; i < h.LayerCount; ++i)
	{
		int size = 0;
		in.read((char*)&size, sizeof(size));
		if (!in || size <= 0 || size > 64 * 1024 * 1024)
			break;
		unsigned char* data = (unsigned char*)dtAlloc(size, DT_ALLOC_PERM);
		in.read((char*)data, size);
		if (!in || dtStatusFailed(m_Cache->addTile(data, size, DT_COMPRESSEDTILE_FREE_DATA, nullptr)))
		{
			dtFree(data);
			break;
		}
	}
	RebuildLinkArrays();
	BuildAllTiles();
	if (!InitQuery())
	{
		Reset();
		return false;
	}
	++Revision;
	return PolyCount > 0;
}

// ---------------------------------------------------------------------- Off-Mesh Link / 장애물
void NavData::SetLinks(const std::vector<NavLink>& links)
{
	std::vector<NavLink> old = std::move(m_Links);
	m_Links = links;
	RebuildLinkArrays();
	if (m_Cache == nullptr || m_Mesh == nullptr)
		return;
	// 링크는 시작 점이 든 타일에 들어간다 → 옛 시작 점과 새 시작 점의 타일만 다시
	std::vector<std::pair<int, int>> done;
	const dtTileCacheParams* tp = m_Cache->getParams();
	auto rebuild = [&](const Vec3& p)
	{
		const int tx = (int)std::floor((p.x - tp->orig[0]) / (tp->width * tp->cs));
		const int ty = (int)std::floor((p.z - tp->orig[2]) / (tp->height * tp->cs));
		if (std::find(done.begin(), done.end(), std::make_pair(tx, ty)) != done.end())
			return;
		done.emplace_back(tx, ty);
		m_Cache->buildNavMeshTilesAt(tx, ty, m_Mesh);
	};
	for (const NavLink& l : old)
		rebuild(l.Start);
	for (const NavLink& l : m_Links)
		rebuild(l.Start);
	CountPolys();
	++Revision;
}

uint32_t NavData::AddObstacle(const NavObstacleShape& s)
{
	if (m_Cache == nullptr)
		return 0;
	// Unity 처럼 에이전트 반지름만큼 넓혀 깎는다 (층에는 이미 깎인 가장자리가 없어서).
	// 아래로는 Step Height 만큼 더 — 바닥에 놓인 장애물이 바닥 면을 확실히 덮게
	const float r = Settings.AgentRadius, down = (std::max)(0.1f, Settings.StepHeight);
	dtObstacleRef ref = 0;
	dtStatus st;
	if (s.Box)
	{
		const float c[3] = { s.Center.x, s.Center.y - down * 0.5f, s.Center.z };
		const float he[3] = { s.HalfExtents.x + r, s.HalfExtents.y + down * 0.5f, s.HalfExtents.z + r };
		st = m_Cache->addBoxObstacle(c, he, s.YRadians, &ref);
	}
	else
	{
		const float p[3] = { s.Center.x, s.Center.y - down, s.Center.z };
		st = m_Cache->addObstacle(p, s.Radius + r, s.Height + down, &ref);
	}
	if (dtStatusFailed(st))
		return 0;   // 요청이 꽉 찼다 (한 프레임 64 개) — 다음 프레임에 다시
	m_ObstaclesPending = true;
	return ref;
}

void NavData::RemoveObstacle(uint32_t id)
{
	if (m_Cache && id && dtStatusSucceed(m_Cache->removeObstacle(id)))
		m_ObstaclesPending = true;
}

void NavData::UpdateObstacles()
{
	if (m_Cache == nullptr || m_Mesh == nullptr || !m_ObstaclesPending)
		return;
	// update 한 번에 타일 하나씩 다시 만든다 — 한 프레임에 최대 64 타일, 남으면 다음 프레임
	bool upToDate = false;
	for (int i = 0; i < 64 && !upToDate; ++i)
		if (dtStatusFailed(m_Cache->update(0.0f, m_Mesh, &upToDate)))
			break;
	m_ObstaclesPending = !upToDate;
	CountPolys();
	++Revision;
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

bool NavData::FindPath(const Vec3& start, const Vec3& end, std::vector<Vec3>& corners, bool* partial, std::vector<unsigned char>* flagsOut) const
{
	corners.clear();
	if (flagsOut) flagsOut->clear();
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
	{
		corners.push_back(Vec3(straight[i * 3], straight[i * 3 + 1], straight[i * 3 + 2]));
		if (flagsOut)
			flagsOut->push_back((flags[i] & DT_STRAIGHTPATH_OFFMESH_CONNECTION) && i + 1 < ns ? CornerLinkStart : 0);
	}
	if (corners.size() == 1)
	{
		corners.push_back(corners[0]);
		if (flagsOut) flagsOut->push_back(0);
	}
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
	dl->PushClipRect(ImVec2(off.x + rmin.x, off.y + rmin.y), ImVec2(off.x + rmax.x, off.y + rmax.y), true);   // Project 는 화면 좌표
	int drawn = 0;
	// 2D 표면: 내비 (x, z) → 월드 (x, y) 를 표면의 z 바로 앞에
	auto project = [this](const float* v, ImVec2& o) {
		return SceneViewOverlay::Project(Plane2D ? XMFLOAT3(v[0], v[2], Gizmo2DZ - 0.01f) : XMFLOAT3(v[0], v[1] + 0.03f, v[2]), o);
	};
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
		// Off-Mesh Link: 두 끝을 잇는 호 (Detour 디버그 그림과 같이 거리의 1/4 높이)
		for (int i = 0; i < tile->header->offMeshConCount; ++i)
		{
			const dtOffMeshConnection* con = &tile->offMeshCons[i];
			const float* a = &con->pos[0];
			const float* b = &con->pos[3];
			const float len = std::sqrt((b[0] - a[0]) * (b[0] - a[0]) + (b[2] - a[2]) * (b[2] - a[2]));
			const ImU32 c = IM_COL32(255, 196, 0, 230);
			ImVec2 prev;
			bool havePrev = false;
			for (int k = 0; k <= 16; ++k)
			{
				const float u = k / 16.0f;
				const float v[3] = { a[0] + (b[0] - a[0]) * u, a[1] + (b[1] - a[1]) * u + len * 0.25f * 4.0f * u * (1.0f - u), a[2] + (b[2] - a[2]) * u };
				ImVec2 s;
				const bool ok = project(v, s);
				if (ok && havePrev)
					dl->AddLine(prev, s, c, 2.0f);
				prev = s;
				havePrev = ok;
			}
			ImVec2 sa, sb;
			if (project(a, sa)) dl->AddCircleFilled(sa, 4.0f, c);
			if (project(b, sb)) dl->AddCircle(sb, 4.0f, c, 0, (con->flags & DT_OFFMESH_CON_BIDIR) ? 3.0f : 1.5f);
		}
	}
	dl->PopClipRect();
}
