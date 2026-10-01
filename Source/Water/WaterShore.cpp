#include "pch.h"
#include "WaterShore.h"
#include "Terrain.h"
#include "TerrainData.h"
#include <deque>
#include <map>

namespace
{
	std::vector<float> s_Heights;
	XMFLOAT4 s_Rect(0, 0, 0, 0);
	uint64_t s_Hash = 0, s_Revision = 0;
	bool s_Has = false;
	struct Mask { uint64_t Revision = 0; std::vector<uint8_t> Data; };
	std::map<uint32_t, Mask> s_Masks;   // 해수면 비트 → 마스크

	uint64_t Mix(uint64_t h, uint64_t v) { return h ^ (v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2)); }
	uint32_t Bits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

	bool ToCell(float x, float z, int& cx, int& cz)
	{
		if (!s_Has)
			return false;
		const float u = (x - s_Rect.x) * s_Rect.z, v = (z - s_Rect.y) * s_Rect.w;
		if (u < 0.0f || v < 0.0f || u >= 1.0f || v >= 1.0f)
			return false;
		cx = (std::min)((int)(u * WaterShore::kRes), WaterShore::kRes - 1);
		cz = (std::min)((int)(v * WaterShore::kRes), WaterShore::kRes - 1);
		return true;
	}
}

namespace WaterShore
{
	void Update()
	{
		uint64_t h = 0;
		float minX = FLT_MAX, minZ = FLT_MAX, maxX = -FLT_MAX, maxZ = -FLT_MAX;
		for (Terrain* t : Terrain::GetActiveTerrains())
		{
			auto data = t->GetTerrainData();
			if (!data)
				continue;
			const Vec3 p = t->GetPosition();
			h = Mix(h, (uint64_t)(uintptr_t)data.get());
			h = Mix(h, (uint64_t)data->Revision);
			h = Mix(h, Bits(p.x)); h = Mix(h, Bits(p.y)); h = Mix(h, Bits(p.z));
			minX = (std::min)(minX, p.x); minZ = (std::min)(minZ, p.z);
			maxX = (std::max)(maxX, p.x + data->Size.x); maxZ = (std::max)(maxZ, p.z + data->Size.z);
		}
		if (h == s_Hash && (s_Has || h == 0))
			return;
		s_Hash = h;
		++s_Revision;
		s_Masks.clear();
		s_Has = minX < maxX && minZ < maxZ;
		if (!s_Has)
		{
			s_Heights.clear();
			return;
		}
		s_Heights.assign((size_t)kRes * kRes, -1e4f);
		for (int z = 0; z < kRes; ++z)
			for (int x = 0; x < kRes; ++x)
			{
				const float wx = minX + (x + 0.5f) / kRes * (maxX - minX), wz = minZ + (z + 0.5f) / kRes * (maxZ - minZ);
				for (Terrain* t : Terrain::GetActiveTerrains())
				{
					auto data = t->GetTerrainData();
					const Vec3 p = t->GetPosition();
					if (data && wx >= p.x && wz >= p.z && wx <= p.x + data->Size.x && wz <= p.z + data->Size.z)
					{
						s_Heights[(size_t)z * kRes + x] = p.y + t->SampleHeight(Vec3(wx, 0, wz));
						break;
					}
				}
			}
		s_Rect = XMFLOAT4(minX, minZ, 1.0f / (maxX - minX), 1.0f / (maxZ - minZ));
	}

	bool Has() { return s_Has; }
	uint64_t Revision() { return s_Revision; }
	const std::vector<float>& Heights() { return s_Heights; }
	XMFLOAT4 Rect() { return s_Rect; }

	float HeightAt(float x, float z)
	{
		int cx, cz;
		return ToCell(x, z, cx, cz) ? s_Heights[(size_t)cz * kRes + cx] : -1e4f;
	}

	const std::vector<uint8_t>& OceanMask(float seaLevel)
	{
		static const std::vector<uint8_t> s_Empty;
		if (!s_Has)
			return s_Empty;
		Mask& m = s_Masks[Bits(seaLevel)];
		if (m.Revision == s_Revision && !m.Data.empty())
			return m.Data;
		m.Revision = s_Revision;
		m.Data.assign((size_t)kRes * kRes, 0);
		// 땅(해수면 이상) = 1, 지도 밖(-1e4 = 지형 없음)도 바다
		std::deque<int> open;
		for (int i = 0; i < kRes * kRes; ++i)
		{
			const float h = s_Heights[i];
			if (h >= seaLevel)
				m.Data[i] = 255;
			else if (h <= -9999.0f)
			{
				m.Data[i] = 255;
				open.push_back(i);
			}
		}
		// 가장자리의 물 칸에서 시작 (지도 밖은 끝없는 바다)
		for (int k = 0; k < kRes; ++k)
			for (int i : { k, (kRes - 1) * kRes + k, k * kRes, k * kRes + kRes - 1 })
				if (m.Data[i] == 0)
				{
					m.Data[i] = 255;
					open.push_back(i);
				}
		while (!open.empty())
		{
			const int i = open.front();
			open.pop_front();
			const int x = i % kRes, z = i / kRes;
			const int nb[4][2] = { { x - 1, z }, { x + 1, z }, { x, z - 1 }, { x, z + 1 } };
			for (const auto& n : nb)
			{
				if (n[0] < 0 || n[1] < 0 || n[0] >= kRes || n[1] >= kRes)
					continue;
				const int j = n[1] * kRes + n[0];
				if (m.Data[j] == 0)
				{
					m.Data[j] = 255;
					open.push_back(j);
				}
			}
		}
		int pools = 0;
		for (uint8_t v : m.Data) pools += v == 0;
		EditorLog::Write("Water", "ocean mask (sea level %.1f): %d inland cells excluded", seaLevel, pools);
		return m.Data;
	}

	bool OceanCovers(float seaLevel, float x, float z)
	{
		int cx, cz;
		if (!ToCell(x, z, cx, cz))
			return true;
		const auto& m = OceanMask(seaLevel);
		return m.empty() || m[(size_t)cz * kRes + cx] != 0;
	}
}
