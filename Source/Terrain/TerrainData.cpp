#include "pch.h"
#include "TerrainData.h"
#include "MemoryStats.h"
#include "Terrain.h"

namespace
{
	std::string NormalizePath(std::string path)
	{
		std::replace(path.begin(), path.end(), '/', '\\');
		if (!std::filesystem::path(path).is_absolute())
		{
			const size_t first = path.find_first_not_of('\\');
			path.erase(0, first == std::string::npos ? path.size() : first);
		}
		return path;
	}

	std::wstring FilePath(const std::string& path)
	{
		std::filesystem::path p(string_to_wstring(path));
		return p.is_absolute() ? p.wstring() : PathManager::GetI()->GetMovePathW(string_to_wstring(path));
	}

	// 경로별 공유 (강한 참조: Play 종료로 씬을 다시 읽을 때도 저장 안 된 편집이 남아 있도록)
	std::map<std::string, std::shared_ptr<TerrainData>>& DataCache()
	{
		static std::map<std::string, std::shared_ptr<TerrainData>> cache;
		return cache;
	}
	std::map<std::string, std::shared_ptr<TerrainLayer>>& LayerCache()
	{
		static std::map<std::string, std::shared_ptr<TerrainLayer>> cache;
		return cache;
	}

	constexpr uint32_t kMagic = 0x4454564E;   // "NVTD"
	constexpr uint32_t kVersion = 5;   // 2: 나무 프로토타입(JSON) + 인스턴스, 3: 지형 생성기 설정(JSON) + 기준 스냅샷, 4: 컬러 맵, 5: 디테일

	template <typename T> void WritePod(std::ofstream& os, const T& v) { os.write(reinterpret_cast<const char*>(&v), sizeof(T)); }
	template <typename T> bool ReadPod(std::ifstream& is, T& v) { is.read(reinterpret_cast<char*>(&v), sizeof(T)); return (bool)is; }
	void WriteString(std::ofstream& os, const std::string& s) { WritePod(os, (uint32_t)s.size()); os.write(s.data(), s.size()); }
	bool ReadString(std::ifstream& is, std::string& s)
	{
		uint32_t n = 0;
		if (!ReadPod(is, n) || n > (1u << 24)) return false;   // 나무 프로토타입 JSON 도 이 형식
		s.resize(n);
		is.read(s.data(), n);
		return (bool)is;
	}
}

// ================================================================== TerrainLayer
std::string TerrainLayer::Name() const
{
	return std::filesystem::path(Path).stem().string();
}

GfxShaderResourceView* TerrainLayer::DiffuseSRV()
{
	if (!m_DiffuseLoaded)
	{
		m_DiffuseLoaded = true;
		m_Diffuse = DiffusePath.empty() ? nullptr : ResourceManager::GetI()->LoadTexture(string_to_wstring(DiffusePath));
	}
	return m_Diffuse.Get();
}

void TerrainLayer::SetDiffuse(const std::string& path)
{
	DiffusePath = NormalizePath(path);
	m_DiffuseLoaded = false;
	m_Diffuse = nullptr;
}

GfxShaderResourceView* TerrainLayer::HeightSRV()
{
	if (!m_HeightLoaded)
	{
		m_HeightLoaded = true;
		m_Height = HeightPath.empty() ? nullptr : ResourceManager::GetI()->LoadTexture(string_to_wstring(HeightPath));
	}
	return m_Height.Get();
}

void TerrainLayer::SetHeight(const std::string& path)
{
	HeightPath = NormalizePath(path);
	m_HeightLoaded = false;
	m_Height = nullptr;
}

GfxShaderResourceView* TerrainLayer::NormalSRV()
{
	if (!m_NormalLoaded)
	{
		m_NormalLoaded = true;
		m_Normal = NormalPath.empty() ? nullptr : ResourceManager::GetI()->LoadTexture(string_to_wstring(NormalPath));
	}
	return m_Normal.Get();
}

void TerrainLayer::SetNormal(const std::string& path)
{
	NormalPath = NormalizePath(path);
	m_NormalLoaded = false;
	m_Normal = nullptr;
}

bool TerrainLayer::Save() const
{
	json j;
	j["diffuse"] = DiffusePath;
	j["tileSize"] = { TileSize.x, TileSize.y };
	j["tileOffset"] = { TileOffset.x, TileOffset.y };
	j["tint"] = { Tint.x, Tint.y, Tint.z, Tint.w };
	if (!NormalPath.empty())
	{
		j["normalMap"] = NormalPath;
		j["normalScale"] = NormalScale;
	}
	if (!HeightPath.empty())
	{
		j["height"] = HeightPath;
		j["heightAmplitude"] = HeightAmplitude;
		j["heightBase"] = HeightBase;
	}
	std::ofstream os(FilePath(Path), std::ios::binary | std::ios::trunc);
	if (!os)
		return false;
	os << j.dump(4);
	return true;
}

std::shared_ptr<TerrainLayer> TerrainLayer::Load(const std::string& rawPath)
{
	const std::string path = NormalizePath(rawPath);
	if (path.empty())
		return nullptr;
	auto it = LayerCache().find(path);
	if (it != LayerCache().end())
		return it->second;
	std::ifstream in(FilePath(path), std::ios::binary);
	if (!in)
		return nullptr;
	json j = json::parse(in, nullptr, false);
	if (j.is_discarded())
		return nullptr;
	auto layer = std::make_shared<TerrainLayer>();
	layer->Path = path;
	layer->DiffusePath = NormalizePath(j.value("diffuse", std::string()));
	if (j.contains("tileSize")) layer->TileSize = Vec2(j["tileSize"][0], j["tileSize"][1]);
	if (j.contains("tileOffset")) layer->TileOffset = Vec2(j["tileOffset"][0], j["tileOffset"][1]);
	if (j.contains("tint")) layer->Tint = XMFLOAT4(j["tint"][0], j["tint"][1], j["tint"][2], j["tint"][3]);
	layer->HeightPath = NormalizePath(j.value("height", std::string()));
	layer->HeightAmplitude = (std::max)(0.0f, j.value("heightAmplitude", layer->HeightAmplitude));
	layer->HeightBase = std::clamp(j.value("heightBase", layer->HeightBase), 0.0f, 1.0f);
	layer->NormalPath = NormalizePath(j.value("normalMap", std::string()));
	layer->NormalScale = j.value("normalScale", layer->NormalScale);
	LayerCache()[path] = layer;
	return layer;
}

std::shared_ptr<TerrainLayer> TerrainLayer::Create(const std::string& rawPath, const std::string& diffusePath)
{
	auto layer = std::make_shared<TerrainLayer>();
	layer->Path = NormalizePath(rawPath);
	layer->DiffusePath = NormalizePath(diffusePath);
	layer->Save();
	LayerCache()[layer->Path] = layer;
	return layer;
}

std::vector<std::string> TerrainLayer::ListAvailable()
{
	std::vector<std::string> out;
	auto scan = [&](const std::wstring& root, const std::wstring& prefix) {
		std::error_code ec;
		if (!std::filesystem::exists(root, ec))
			return;
		for (const auto& e : std::filesystem::recursive_directory_iterator(root, ec))
			if (e.is_regular_file() && e.path().extension() == L".terrainlayer")
				out.push_back(wstring_to_string(prefix + std::filesystem::relative(e.path(), root, ec).wstring()));
	};
	scan(PathManager::GetI()->GetMovePathW(L"Resources\\Packages\\"), L"Resources\\Packages\\");
	scan(PathManager::GetI()->GetMovePathW(L"Assets\\"), L"Assets\\");
	return out;
}

// ================================================================== TerrainData
std::string TerrainData::Name() const
{
	return std::filesystem::path(Path).stem().string();
}

int TerrainData::MaxDepth() const
{
	int depth = 0;
	while (((HeightmapResolution - 1) >> (depth + 1)) >= NodeGrid())
		++depth;
	return depth;
}

void TerrainData::Allocate()
{
	Heights.assign((size_t)HeightmapResolution * HeightmapResolution, 0.0f);
	Control.assign((size_t)ControlResolution * ControlResolution * 4, 0);
	for (size_t i = 0; i < Control.size(); i += 4)
		Control[i] = 255;   // 첫 레이어 100%
}

float TerrainData::GetHeightSample(int x, int z) const
{
	x = std::clamp(x, 0, HeightmapResolution - 1);
	z = std::clamp(z, 0, HeightmapResolution - 1);
	return Heights[(size_t)z * HeightmapResolution + x];
}

float TerrainData::GetHeight(float x, float z) const
{
	const float fx = std::clamp(x / CellSizeX(), 0.0f, (float)(HeightmapResolution - 1));
	const float fz = std::clamp(z / CellSizeZ(), 0.0f, (float)(HeightmapResolution - 1));
	const int ix = (std::min)((int)fx, HeightmapResolution - 2);
	const int iz = (std::min)((int)fz, HeightmapResolution - 2);
	const float tx = fx - ix, tz = fz - iz;
	const float h00 = GetHeightSample(ix, iz), h10 = GetHeightSample(ix + 1, iz);
	const float h01 = GetHeightSample(ix, iz + 1), h11 = GetHeightSample(ix + 1, iz + 1);
	// 렌더링 메시와 같은 대각선 (0,0)-(1,1)
	const float h = tz >= tx ? h00 + tz * (h01 - h00) + tx * (h11 - h01)
		: h00 + tx * (h10 - h00) + tz * (h11 - h10);
	return h * Size.y;
}

float TerrainData::GetUncarvedHeight(float x, float z) const
{
	if (UncarvedHeights.size() != Heights.size())
		return GetHeight(x, z);
	const float fx = std::clamp(x / CellSizeX(), 0.0f, (float)(HeightmapResolution - 1));
	const float fz = std::clamp(z / CellSizeZ(), 0.0f, (float)(HeightmapResolution - 1));
	const int ix = (std::min)((int)fx, HeightmapResolution - 2);
	const int iz = (std::min)((int)fz, HeightmapResolution - 2);
	const float tx = fx - ix, tz = fz - iz;
	auto at = [&](int xx, int zz) { return UncarvedHeights[(size_t)zz * HeightmapResolution + xx]; };
	const float h0 = at(ix, iz) + (at(ix + 1, iz) - at(ix, iz)) * tx;
	const float h1 = at(ix, iz + 1) + (at(ix + 1, iz + 1) - at(ix, iz + 1)) * tx;
	return (h0 + (h1 - h0) * tz) * Size.y;
}

Vec3 TerrainData::GetNormal(float x, float z) const
{
	const float dx = CellSizeX(), dz = CellSizeZ();
	const float hl = GetHeight(x - dx, z), hr = GetHeight(x + dx, z);
	const float hd = GetHeight(x, z - dz), hu = GetHeight(x, z + dz);
	Vec3 n(-(hr - hl) / (2.0f * dx), 1.0f, -(hu - hd) / (2.0f * dz));
	n.Normalize();
	return n;
}

bool TerrainData::Raycast(const Vec3& origin, const Vec3& dirIn, float maxDistance, float& outT) const
{
	Vec3 dir = dirIn;
	const float len = dir.Length();
	if (len < 1e-8f)
		return false;
	dir /= len;

	// 지형 상자와 교차 구간
	const Vec3 bmin(0.0f, -1.0f, 0.0f), bmax(Size.x, Size.y + 1.0f, Size.z);
	float t0 = 0.0f, t1 = maxDistance * len;
	for (int a = 0; a < 3; ++a)
	{
		const float o = (&origin.x)[a], d = (&dir.x)[a], mn = (&bmin.x)[a], mx = (&bmax.x)[a];
		if (fabsf(d) < 1e-8f)
		{
			if (o < mn || o > mx) return false;
			continue;
		}
		float ta = (mn - o) / d, tb = (mx - o) / d;
		if (ta > tb) std::swap(ta, tb);
		t0 = (std::max)(t0, ta);
		t1 = (std::min)(t1, tb);
		if (t0 > t1) return false;
	}

	// 칸 크기의 절반 간격으로 전진하다가 지면 아래로 들어가면 이분 탐색
	const float step = (std::max)(0.05f, (std::min)(CellSizeX(), CellSizeZ()) * 0.5f);
	auto above = [&](float t) { const Vec3 p = origin + dir * t; return p.y - GetHeight(p.x, p.z); };
	float prevT = t0, prevA = above(t0);
	if (prevA < 0.0f)
	{
		outT = t0 / len;
		return true;
	}
	for (float t = t0 + step; t <= t1 + step; t += step)
	{
		const float tc = (std::min)(t, t1);
		const float a = above(tc);
		if (a < 0.0f)
		{
			float lo = prevT, hi = tc;
			for (int i = 0; i < 20; ++i)
			{
				const float mid = (lo + hi) * 0.5f;
				if (above(mid) < 0.0f) hi = mid; else lo = mid;
			}
			outT = hi / len;
			return true;
		}
		prevT = tc;
		prevA = a;
		if (tc >= t1)
			break;
	}
	return false;
}

// ---- LOD 쿼드트리 ----
void TerrainData::RebuildAllNodes()
{
	const int depthCount = MaxDepth() + 1;
	m_Nodes.assign(depthCount, {});
	for (int d = 0; d < depthCount; ++d)
		m_Nodes[d].assign((size_t)1 << (2 * d), Node());
	RebuildNodes(0, 0, HeightmapResolution - 1, HeightmapResolution - 1);
}

void TerrainData::RebuildNodes(int x0, int z0, int x1, int z1)
{
	const int maxDepth = MaxDepth();
	if ((int)m_Nodes.size() != maxDepth + 1)
	{
		RebuildAllNodes();
		return;
	}
	const int grid = NodeGrid();
	// 깊은(작은) 노드부터: 부모는 자식의 높이 범위/오차를 이어받는다
	for (int d = maxDepth; d >= 0; --d)
	{
		const int count = 1 << d;
		const int cells = NodeCells(d);
		const int step = NodeStep(d);
		// 경계 격자점은 양쪽 노드에 속한다
		const int nx0 = (std::max)(0, (x0 - 1) / cells), nx1 = (std::min)(count - 1, x1 / cells);
		const int nz0 = (std::max)(0, (z0 - 1) / cells), nz1 = (std::min)(count - 1, z1 / cells);
		for (int nz = nz0; nz <= nz1; ++nz)
			for (int nx = nx0; nx <= nx1; ++nx)
			{
				Node& node = m_Nodes[d][(size_t)nz * count + nx];
				const int ox = nx * cells, oz = nz * cells;
				if (d == maxDepth)
				{
					// 가장 깊은 노드 = 높이맵 그대로 (오차 0)
					float mn = FLT_MAX, mx = -FLT_MAX;
					for (int z = 0; z <= cells; ++z)
						for (int x = 0; x <= cells; ++x)
						{
							const float h = GetHeightSample(ox + x, oz + z);
							mn = (std::min)(mn, h);
							mx = (std::max)(mx, h);
						}
					node.MinHeight = mn * Size.y;
					node.MaxHeight = mx * Size.y;
					node.Error = 0.0f;
					continue;
				}
				// 자식 4개
				const auto& children = m_Nodes[d + 1];
				const int cc = count * 2;
				float mn = FLT_MAX, mx = -FLT_MAX, childErr = 0.0f;
				for (int k = 0; k < 4; ++k)
				{
					const Node& c = children[(size_t)(nz * 2 + (k >> 1)) * cc + nx * 2 + (k & 1)];
					mn = (std::min)(mn, c.MinHeight);
					mx = (std::max)(mx, c.MaxHeight);
					childErr = (std::max)(childErr, c.Error);
				}
				// 이 노드 간격(step)으로 그렸을 때, 자식 간격(step/2) 격자점에서 생기는 높이 차
				const int half = step / 2;
				float err = 0.0f;
				for (int z = 0; z <= cells; z += half)
					for (int x = 0; x <= cells; x += half)
					{
						if (x % step == 0 && z % step == 0)
							continue;
						const int cx = (std::min)(x / step * step, cells - step), cz = (std::min)(z / step * step, cells - step);
						const float tx = (float)(x - cx) / step, tz = (float)(z - cz) / step;
						const float h00 = GetHeightSample(ox + cx, oz + cz), h10 = GetHeightSample(ox + cx + step, oz + cz);
						const float h01 = GetHeightSample(ox + cx, oz + cz + step), h11 = GetHeightSample(ox + cx + step, oz + cz + step);
						const float approx = tz >= tx ? h00 + tz * (h01 - h00) + tx * (h11 - h01) : h00 + tx * (h10 - h00) + tz * (h11 - h10);
						err = (std::max)(err, fabsf(approx - GetHeightSample(ox + x, oz + z)));
					}
				node.MinHeight = mn;
				node.MaxHeight = mx;
				node.Error = childErr + err * Size.y;
			}
	}
}

void TerrainData::OnHeightsChanged(int x0, int z0, int x1, int z1)
{
	x0 = std::clamp(x0, 0, HeightmapResolution - 1); x1 = std::clamp(x1, 0, HeightmapResolution - 1);
	z0 = std::clamp(z0, 0, HeightmapResolution - 1); z1 = std::clamp(z1, 0, HeightmapResolution - 1);
	if (m_HeightDirty && !m_HeightFullUpload)
	{
		m_HeightDirtyRect[0] = (std::min)(m_HeightDirtyRect[0], x0);
		m_HeightDirtyRect[1] = (std::min)(m_HeightDirtyRect[1], z0);
		m_HeightDirtyRect[2] = (std::max)(m_HeightDirtyRect[2], x1);
		m_HeightDirtyRect[3] = (std::max)(m_HeightDirtyRect[3], z1);
	}
	else if (!m_HeightDirty)
	{
		m_HeightDirtyRect[0] = x0; m_HeightDirtyRect[1] = z0; m_HeightDirtyRect[2] = x1; m_HeightDirtyRect[3] = z1;
	}
	m_HeightDirty = true;
	RebuildNodes(x0, z0, x1, z1);
	++Revision;
	Dirty = true;
}

void TerrainData::OnControlChanged(int x0, int z0, int x1, int z1)
{
	x0 = std::clamp(x0, 0, ControlResolution - 1); x1 = std::clamp(x1, 0, ControlResolution - 1);
	z0 = std::clamp(z0, 0, ControlResolution - 1); z1 = std::clamp(z1, 0, ControlResolution - 1);
	if (m_ControlDirty && !m_ControlFullUpload)
	{
		m_ControlDirtyRect[0] = (std::min)(m_ControlDirtyRect[0], x0);
		m_ControlDirtyRect[1] = (std::min)(m_ControlDirtyRect[1], z0);
		m_ControlDirtyRect[2] = (std::max)(m_ControlDirtyRect[2], x1);
		m_ControlDirtyRect[3] = (std::max)(m_ControlDirtyRect[3], z1);
	}
	else if (!m_ControlDirty)
	{
		m_ControlDirtyRect[0] = x0; m_ControlDirtyRect[1] = z0; m_ControlDirtyRect[2] = x1; m_ControlDirtyRect[3] = z1;
	}
	m_ControlDirty = true;
	++ControlRevision;
	Dirty = true;
}

// ---- 디테일 ----
void TerrainData::AddDetailPrototype(const DetailPrototype& proto)
{
	DetailPrototypes.push_back(proto);
	DetailDensity.emplace_back((size_t)DetailResolution * DetailResolution, (uint8_t)0);
	OnDetailsChanged();
}

void TerrainData::RemoveDetailPrototype(int index)
{
	if (index < 0 || index >= (int)DetailPrototypes.size())
		return;
	DetailPrototypes.erase(DetailPrototypes.begin() + index);
	if (index < (int)DetailDensity.size())
		DetailDensity.erase(DetailDensity.begin() + index);
	OnDetailsChanged();
}

void TerrainData::SetDetailResolution(int resolution)
{
	resolution = std::clamp(resolution, 32, 4096);
	if (resolution == DetailResolution)
		return;
	const int old = DetailResolution;
	for (auto& map : DetailDensity)
	{
		std::vector<uint8_t> out((size_t)resolution * resolution);
		for (int z = 0; z < resolution; ++z)
			for (int x = 0; x < resolution; ++x)
			{
				const float fx = (x + 0.5f) * old / resolution - 0.5f, fz = (z + 0.5f) * old / resolution - 0.5f;
				const int x0 = std::clamp((int)floorf(fx), 0, old - 1), z0 = std::clamp((int)floorf(fz), 0, old - 1);
				const int x1 = (std::min)(x0 + 1, old - 1), z1 = (std::min)(z0 + 1, old - 1);
				const float tx = std::clamp(fx - x0, 0.0f, 1.0f), tz = std::clamp(fz - z0, 0.0f, 1.0f);
				auto at = [&](int ax, int az) { return map.size() == (size_t)old * old ? (float)map[(size_t)az * old + ax] : 0.0f; };
				const float v = (at(x0, z0) * (1 - tx) + at(x1, z0) * tx) * (1 - tz) + (at(x0, z1) * (1 - tx) + at(x1, z1) * tx) * tz;
				out[(size_t)z * resolution + x] = (uint8_t)std::lround(std::clamp(v, 0.0f, 255.0f));
			}
		map.swap(out);
	}
	DetailResolution = resolution;
	OnDetailsChanged();
}

// 밀도 맵 칸 중심 = (i + 0.5) / 해상도 × 크기
float TerrainData::GetDetailDensity(int proto, float x, float z) const
{
	if (proto < 0 || proto >= (int)DetailDensity.size())
		return 0.0f;
	const std::vector<uint8_t>& map = DetailDensity[proto];
	const int res = DetailResolution;
	if (map.size() != (size_t)res * res)
		return 0.0f;
	const float fx = x / Size.x * res - 0.5f, fz = z / Size.z * res - 0.5f;
	const int x0 = std::clamp((int)floorf(fx), 0, res - 1), z0 = std::clamp((int)floorf(fz), 0, res - 1);
	const int x1 = (std::min)(x0 + 1, res - 1), z1 = (std::min)(z0 + 1, res - 1);
	const float tx = std::clamp(fx - floorf(fx), 0.0f, 1.0f), tz = std::clamp(fz - floorf(fz), 0.0f, 1.0f);
	const float a = map[(size_t)z0 * res + x0], b = map[(size_t)z0 * res + x1];
	const float c = map[(size_t)z1 * res + x0], d = map[(size_t)z1 * res + x1];
	return ((a * (1 - tx) + b * tx) * (1 - tz) + (c * (1 - tx) + d * tx) * tz) / 255.0f;
}

float TerrainData::GetLayerWeight(uint32_t layerMask, float x, float z) const
{
	const int channels = (std::min)((int)Layers.size(), 4);
	if (channels == 0)
		return 1.0f;   // 레이어 없음 = 어디서나
	if ((layerMask & ((1u << channels) - 1)) == ((1u << channels) - 1))
		return 1.0f;
	const int res = ControlResolution;
	const float fx = std::clamp(x / Size.x, 0.0f, 1.0f) * (res - 1), fz = std::clamp(z / Size.z, 0.0f, 1.0f) * (res - 1);
	const int x0 = (int)fx, z0 = (int)fz, x1 = (std::min)(x0 + 1, res - 1), z1 = (std::min)(z0 + 1, res - 1);
	const float tx = fx - x0, tz = fz - z0;
	float w = 0.0f;
	for (int c = 0; c < channels; ++c)
	{
		if (!((layerMask >> c) & 1u))
			continue;
		auto at = [&](int ax, int az) { return Control[((size_t)az * res + ax) * 4 + c] / 255.0f; };
		w += (at(x0, z0) * (1 - tx) + at(x1, z0) * tx) * (1 - tz) + (at(x0, z1) * (1 - tx) + at(x1, z1) * tx) * tz;
	}
	return std::clamp(w, 0.0f, 1.0f);
}

void TerrainData::SetHeightmapResolution(int resolution)
{
	resolution = std::clamp(resolution, 33, 4097);
	if (resolution == HeightmapResolution)
		return;
	// 기존 높이를 쌍선형 보간으로 옮긴다
	std::vector<float> old = Heights;
	const int oldRes = HeightmapResolution;
	HeightmapResolution = resolution;
	Heights.assign((size_t)resolution * resolution, 0.0f);
	for (int z = 0; z < resolution; ++z)
		for (int x = 0; x < resolution; ++x)
		{
			const float fx = (float)x / (resolution - 1) * (oldRes - 1), fz = (float)z / (resolution - 1) * (oldRes - 1);
			const int ix = (std::min)((int)fx, oldRes - 2), iz = (std::min)((int)fz, oldRes - 2);
			const float tx = fx - ix, tz = fz - iz;
			auto at = [&](int a, int b) { return old[(size_t)b * oldRes + a]; };
			const float h = (at(ix, iz) * (1 - tx) + at(ix + 1, iz) * tx) * (1 - tz) + (at(ix, iz + 1) * (1 - tx) + at(ix + 1, iz + 1) * tx) * tz;
			Heights[(size_t)z * resolution + x] = h;
		}
	m_HeightTex = nullptr;
	m_HeightSRV = nullptr;
	m_HeightDirty = m_HeightFullUpload = true;
	RebuildAllNodes();
	++Revision;
	Dirty = true;
}

void TerrainData::SetSize(const Vec3& size)
{
	Size = Vec3((std::max)(1.0f, size.x), (std::max)(1.0f, size.y), (std::max)(1.0f, size.z));
	RebuildAllNodes();
	++Revision;
	Dirty = true;
}

// ---- GPU ----
size_t TerrainData::CpuBytes() const
{
	size_t bytes = Heights.capacity() * sizeof(float) + Control.capacity() + TreeInstances.capacity() * sizeof(TerrainTreeInstance);
	for (const auto& map : DetailDensity)
		bytes += map.capacity();
	for (const auto& level : m_Nodes)
		bytes += level.capacity() * sizeof(Node);
	return bytes;
}

size_t TerrainData::GpuBytes() const
{
	return MemoryStats::ResourceBytes(m_HeightTex.Get()) + MemoryStats::ResourceBytes(m_ControlTex.Get());
}

GfxShaderResourceView* TerrainData::HeightSRV()
{
	GfxDevice* device = Application::GetI()->GetDevice();
	GfxContext* dc = Application::GetI()->GetDeviceContext();
	if (m_HeightTex == nullptr)
	{
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = desc.Height = HeightmapResolution;
		desc.MipLevels = desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R32_FLOAT;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		D3D11_SUBRESOURCE_DATA init = { Heights.data(), (UINT)(HeightmapResolution * sizeof(float)), 0 };
		if (FAILED(device->CreateTexture2D(&desc, &init, m_HeightTex.GetAddressOf())) ||
			FAILED(device->CreateShaderResourceView(m_HeightTex.Get(), nullptr, m_HeightSRV.GetAddressOf())))
			return nullptr;
		m_HeightDirty = m_HeightFullUpload = false;
	}
	else if (m_HeightDirty)
	{
		D3D11_BOX box = { (UINT)m_HeightDirtyRect[0], (UINT)m_HeightDirtyRect[1], 0, (UINT)m_HeightDirtyRect[2] + 1, (UINT)m_HeightDirtyRect[3] + 1, 1 };
		if (m_HeightFullUpload)
			box = { 0, 0, 0, (UINT)HeightmapResolution, (UINT)HeightmapResolution, 1 };
		const float* src = Heights.data() + (size_t)box.top * HeightmapResolution + box.left;
		dc->UpdateSubresource(m_HeightTex.Get(), 0, &box, src, (UINT)(HeightmapResolution * sizeof(float)), 0);
		m_HeightDirty = m_HeightFullUpload = false;
	}
	return m_HeightSRV.Get();
}

GfxShaderResourceView* TerrainData::ControlSRV()
{
	GfxDevice* device = Application::GetI()->GetDevice();
	GfxContext* dc = Application::GetI()->GetDeviceContext();
	if (m_ControlTex == nullptr)
	{
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = desc.Height = ControlResolution;
		desc.MipLevels = desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		D3D11_SUBRESOURCE_DATA init = { Control.data(), (UINT)(ControlResolution * 4), 0 };
		if (FAILED(device->CreateTexture2D(&desc, &init, m_ControlTex.GetAddressOf())) ||
			FAILED(device->CreateShaderResourceView(m_ControlTex.Get(), nullptr, m_ControlSRV.GetAddressOf())))
			return nullptr;
		m_ControlDirty = m_ControlFullUpload = false;
	}
	else if (m_ControlDirty)
	{
		D3D11_BOX box = { (UINT)m_ControlDirtyRect[0], (UINT)m_ControlDirtyRect[1], 0, (UINT)m_ControlDirtyRect[2] + 1, (UINT)m_ControlDirtyRect[3] + 1, 1 };
		if (m_ControlFullUpload)
			box = { 0, 0, 0, (UINT)ControlResolution, (UINT)ControlResolution, 1 };
		const uint8_t* src = Control.data() + ((size_t)box.top * ControlResolution + box.left) * 4;
		dc->UpdateSubresource(m_ControlTex.Get(), 0, &box, src, (UINT)(ControlResolution * 4), 0);
		m_ControlDirty = m_ControlFullUpload = false;
	}
	return m_ControlSRV.Get();
}

void TerrainData::SetColorMap(std::vector<uint8_t> colorMap)
{
	if (!colorMap.empty() && colorMap.size() != (size_t)ControlResolution * ControlResolution * 4)
		colorMap.clear();
	if (colorMap.empty() != ColorMap.empty())
	{
		m_ColorTex.Reset();
		m_ColorSRV.Reset();
	}
	ColorMap.swap(colorMap);
	m_ColorDirty = true;
	Dirty = true;
}

GfxShaderResourceView* TerrainData::ColorMapSRV()
{
	if (ColorMap.empty())
		return nullptr;
	if (m_ColorTex == nullptr)
	{
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = desc.Height = ControlResolution;
		desc.MipLevels = desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		D3D11_SUBRESOURCE_DATA init = { ColorMap.data(), (UINT)(ControlResolution * 4), 0 };
		GfxDevice* device = Application::GetI()->GetDevice();
		if (FAILED(device->CreateTexture2D(&desc, &init, m_ColorTex.GetAddressOf())) ||
			FAILED(device->CreateShaderResourceView(m_ColorTex.Get(), nullptr, m_ColorSRV.GetAddressOf())))
			return nullptr;
		m_ColorDirty = false;
	}
	else if (m_ColorDirty)
	{
		Application::GetI()->GetDeviceContext()->UpdateSubresource(m_ColorTex.Get(), 0, nullptr, ColorMap.data(), (UINT)(ControlResolution * 4), 0);
		m_ColorDirty = false;
	}
	return m_ColorSRV.Get();
}

// ---- 파일 ----
bool TerrainData::Save()
{
	const std::wstring file = FilePath(Path);
	std::error_code ec;
	std::filesystem::create_directories(std::filesystem::path(file).parent_path(), ec);
	std::ofstream os(file, std::ios::binary | std::ios::trunc);
	if (!os)
		return false;
	WritePod(os, kMagic);
	WritePod(os, kVersion);
	WritePod(os, (int32_t)HeightmapResolution);
	WritePod(os, Size.x); WritePod(os, Size.y); WritePod(os, Size.z);
	WritePod(os, (int32_t)ControlResolution);
	WritePod(os, (int32_t)Layers.size());
	for (const auto& layer : Layers)
		WriteString(os, layer ? layer->Path : std::string());
	// 높이는 Unity 처럼 16비트로 저장 (600m 기준 약 9mm 정밀도)
	std::vector<uint16_t> packed(Heights.size());
	for (size_t i = 0; i < Heights.size(); ++i)
		packed[i] = (uint16_t)std::lround(std::clamp(Heights[i], 0.0f, 1.0f) * 65535.0f);
	os.write(reinterpret_cast<const char*>(packed.data()), packed.size() * sizeof(uint16_t));
	os.write(reinterpret_cast<const char*>(Control.data()), Control.size());
	// 나무: 프로토타입 설정(JSON 문자열) + 인스턴스 배열
	nlohmann::json protos = nlohmann::json::array();
	for (const TreeDesc& d : TreePrototypes)
		protos.push_back(d.ToJson());
	WriteString(os, protos.dump());
	WritePod(os, (int32_t)TreeInstances.size());
	if (!TreeInstances.empty())
		os.write(reinterpret_cast<const char*>(TreeInstances.data()), TreeInstances.size() * sizeof(TerrainTreeInstance));
	// v3: 지형 생성기 (설정 JSON + Current Terrain 기준 스냅샷 16비트)
	WriteString(os, Generator.ToJson().dump());
	const bool snapshot = BaseSnapshot.size() == Heights.size();
	WritePod(os, (int32_t)(snapshot ? BaseSnapshot.size() : 0));
	if (snapshot)
	{
		std::vector<uint16_t> snap(BaseSnapshot.size());
		for (size_t i = 0; i < snap.size(); ++i)
			snap[i] = (uint16_t)std::lround(std::clamp(BaseSnapshot[i], 0.0f, 1.0f) * 65535.0f);
		os.write(reinterpret_cast<const char*>(snap.data()), snap.size() * sizeof(uint16_t));
	}
	// v4: 컬러 맵 (없으면 0)
	WritePod(os, (int32_t)ColorMap.size());
	if (!ColorMap.empty())
		os.write(reinterpret_cast<const char*>(ColorMap.data()), ColorMap.size());
	// v5: 디테일 (설정 + 프로토타입 JSON, 해상도, 종류마다 밀도 맵)
	nlohmann::json details;
	details["settings"] = Details.ToJson();
	details["prototypes"] = nlohmann::json::array();
	for (const DetailPrototype& p : DetailPrototypes)
		details["prototypes"].push_back(p.ToJson());
	WriteString(os, details.dump());
	WritePod(os, (int32_t)DetailResolution);
	WritePod(os, (int32_t)DetailPrototypes.size());
	for (size_t i = 0; i < DetailPrototypes.size(); ++i)
	{
		const size_t n = (size_t)DetailResolution * DetailResolution;
		if (i < DetailDensity.size() && DetailDensity[i].size() == n)
			os.write(reinterpret_cast<const char*>(DetailDensity[i].data()), n);
		else
			os.write(std::string(n, '\0').data(), n);
	}
	Dirty = false;
	return (bool)os;
}

std::shared_ptr<TerrainData> TerrainData::Load(const std::string& rawPath)
{
	const std::string path = NormalizePath(rawPath);
	if (path.empty())
		return nullptr;
	auto it = DataCache().find(path);
	if (it != DataCache().end())
		return it->second;

	std::ifstream is(FilePath(path), std::ios::binary);
	if (!is)
		return nullptr;
	uint32_t magic = 0, version = 0;
	int32_t res = 0, controlRes = 0, layerCount = 0;
	Vec3 size;
	if (!ReadPod(is, magic) || magic != kMagic || !ReadPod(is, version) || !ReadPod(is, res) ||
		!ReadPod(is, size.x) || !ReadPod(is, size.y) || !ReadPod(is, size.z) || !ReadPod(is, controlRes) || !ReadPod(is, layerCount))
		return nullptr;
	if (res < 33 || res > 4097 || controlRes < 16 || controlRes > 4096 || layerCount < 0 || layerCount > kMaxLayers)
		return nullptr;

	auto data = std::make_shared<TerrainData>();
	data->Path = path;
	data->HeightmapResolution = res;
	data->Size = size;
	data->ControlResolution = controlRes;
	for (int i = 0; i < layerCount; ++i)
	{
		std::string layerPath;
		if (!ReadString(is, layerPath))
			return nullptr;
		if (auto layer = TerrainLayer::Load(layerPath))
			data->Layers.push_back(layer);
	}
	std::vector<uint16_t> packed((size_t)res * res);
	is.read(reinterpret_cast<char*>(packed.data()), packed.size() * sizeof(uint16_t));
	data->Heights.resize(packed.size());
	for (size_t i = 0; i < packed.size(); ++i)
		data->Heights[i] = packed[i] / 65535.0f;
	data->Control.resize((size_t)controlRes * controlRes * 4);
	is.read(reinterpret_cast<char*>(data->Control.data()), data->Control.size());
	if (!is)
		return nullptr;
	if (version >= 2)
	{
		std::string protos;
		int32_t treeCount = 0;
		if (ReadString(is, protos) && ReadPod(is, treeCount) && treeCount >= 0 && treeCount < 10000000)
		{
			const nlohmann::json j = nlohmann::json::parse(protos, nullptr, false);
			if (j.is_array())
				for (const auto& p : j)
				{
					TreeDesc d;
					d.FromJson(p);
					data->TreePrototypes.push_back(d);
				}
			data->TreeInstances.resize((size_t)treeCount);
			if (treeCount > 0)
				is.read(reinterpret_cast<char*>(data->TreeInstances.data()), (size_t)treeCount * sizeof(TerrainTreeInstance));
			if (!is)
				data->TreeInstances.clear();
		}
	}
	if (version >= 3)
	{
		std::string gen;
		int32_t snapCount = 0;
		if (ReadString(is, gen))
			data->Generator.FromJson(nlohmann::json::parse(gen, nullptr, false));
		if (ReadPod(is, snapCount) && snapCount == res * res)
		{
			std::vector<uint16_t> snap((size_t)snapCount);
			is.read(reinterpret_cast<char*>(snap.data()), snap.size() * sizeof(uint16_t));
			if (is)
			{
				data->BaseSnapshot.resize(snap.size());
				for (size_t i = 0; i < snap.size(); ++i)
					data->BaseSnapshot[i] = snap[i] / 65535.0f;
			}
		}
	}
	if (version >= 4)
	{
		int32_t colorBytes = 0;
		if (ReadPod(is, colorBytes) && colorBytes == controlRes * controlRes * 4)
		{
			data->ColorMap.resize((size_t)colorBytes);
			is.read(reinterpret_cast<char*>(data->ColorMap.data()), colorBytes);
			if (!is)
				data->ColorMap.clear();
		}
	}
	if (version >= 5)
	{
		std::string json;
		int32_t detailRes = 0, detailCount = 0;
		if (ReadString(is, json) && ReadPod(is, detailRes) && ReadPod(is, detailCount) && detailRes >= 32 && detailRes <= 4096 && detailCount >= 0 && detailCount < 256)
		{
			const nlohmann::json j = nlohmann::json::parse(json, nullptr, false);
			if (j.is_object())
			{
				if (j.contains("settings"))
					data->Details.FromJson(j["settings"]);
				if (j.contains("prototypes") && j["prototypes"].is_array())
					for (const auto& p : j["prototypes"])
					{
						DetailPrototype d;
						d.FromJson(p);
						data->DetailPrototypes.push_back(d);
					}
			}
			data->DetailResolution = detailRes;
			data->DetailPrototypes.resize((std::min)(data->DetailPrototypes.size(), (size_t)detailCount));
			for (int i = 0; i < detailCount; ++i)
			{
				std::vector<uint8_t> map((size_t)detailRes * detailRes);
				is.read(reinterpret_cast<char*>(map.data()), map.size());
				if (!is)
					break;
				if (i < (int)data->DetailPrototypes.size())
					data->DetailDensity.push_back(std::move(map));
			}
			data->DetailPrototypes.resize(data->DetailDensity.size());
		}
	}
	data->RebuildAllNodes();
	DataCache()[path] = data;
	return data;
}

std::shared_ptr<TerrainData> TerrainData::Create(const std::string& rawPath, int resolution, const Vec3& size)
{
	auto data = std::make_shared<TerrainData>();
	data->Path = NormalizePath(rawPath);
	data->HeightmapResolution = std::clamp(resolution, 33, 4097);
	data->Size = size;
	data->Allocate();
	data->RebuildAllNodes();
	data->Save();
	DataCache()[data->Path] = data;
	return data;
}

void TerrainData::SaveAllDirty()
{
	for (auto& pair : DataCache())
		if (pair.second && pair.second->Dirty)
			pair.second->Save();
}

bool TerrainData::AnyDirty()
{
	for (Terrain* t : Terrain::GetActiveTerrains())
		if (std::shared_ptr<TerrainData> d = t ? t->GetTerrainData() : nullptr; d && d->Dirty)
			return true;
	return false;
}

void TerrainData::DropUnsaved()
{
	int dropped = 0;
	for (auto it = DataCache().begin(); it != DataCache().end();)
		if (it->second && it->second->Dirty)
		{
			it = DataCache().erase(it);
			++dropped;
		}
		else
			++it;
	if (dropped > 0)
		EditorLog::Write("Terrain", "dropped %d unsaved terrain data from the cache (scene closed without saving)", dropped);
}

void TerrainData::SetGenerated(int resolution, const Vec3& size, std::vector<float>&& heights, int controlResolution, std::vector<uint8_t>&& control)
{
	if (resolution != HeightmapResolution)
	{
		m_HeightTex = nullptr;
		m_HeightSRV = nullptr;
	}
	if (controlResolution != ControlResolution)
	{
		m_ControlTex = nullptr;
		m_ControlSRV = nullptr;
	}
	HeightmapResolution = resolution;
	ControlResolution = controlResolution;
	Size = size;
	Heights = std::move(heights);
	Control = std::move(control);
	m_HeightDirty = m_HeightFullUpload = true;
	m_ControlDirty = m_ControlFullUpload = true;
	RebuildAllNodes();
	++Revision;
	++ControlRevision;
	Dirty = false;   // 만든 것 — 저장하지 않는다
}

void TerrainData::RestoreState(int resolution, const Vec3& size, const std::vector<float>& heights, const std::vector<uint8_t>& control,
	const std::vector<std::shared_ptr<TerrainLayer>>& layers)
{
	if (resolution != HeightmapResolution)
	{
		m_HeightTex = nullptr;
		m_HeightSRV = nullptr;
	}
	HeightmapResolution = resolution;
	Size = size;
	Heights = heights;
	Control = control;
	Layers = layers;
	m_HeightDirty = m_HeightFullUpload = true;
	m_ControlDirty = m_ControlFullUpload = true;
	++ControlRevision;
	RebuildAllNodes();
	++Revision;
	Dirty = true;
}
