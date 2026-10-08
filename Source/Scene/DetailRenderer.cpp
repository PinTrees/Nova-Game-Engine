#include "pch.h"
#include "WeatherState.h"
#include "RenderLayers.h"
#include "DetailRenderer.h"
#include "Terrain.h"
#include "TerrainData.h"
#include "Effects.h"
#include "RenderManager.h"
#include "UMaterial.h"
#include "Profiler.h"
#include "SceneCulling.h"
#include <chrono>
#include <execution>
#include <unordered_map>
#include <map>

namespace
{
	using Clock = std::chrono::steady_clock;
	constexpr float kChunk = 16.0f;           // m: 조각 한 변
	constexpr int kBuildPerFrame = 48;        // 이미 그리고 있는 지형에서 한 프레임에 다시 만드는 조각 수
	constexpr int kFirstBuild = 1200;         // 처음 보이는 지형은 한 번에 (빈 들판이 잠깐 보이지 않게)
	DetailRenderer::Stats s_Stats[2];

	// 거리 비율 → 남기는 비율 (셰이더 DetailKeep 과 같은 식)
	float Keep(float t)
	{
		auto smooth = [](float a, float b, float x) { const float u = std::clamp((x - a) / (b - a), 0.0f, 1.0f); return u * u * (3.0f - 2.0f * u); };
		return (1.0f - 0.9f * smooth(0.12f, 0.7f, t)) * (1.0f - smooth(0.85f, 1.0f, t));
	}

	struct Rng
	{
		uint32_t s;
		explicit Rng(uint32_t seed) : s(seed * 747796405u + 2891336453u) {}
		float operator()()
		{
			s = s * 747796405u + 2891336453u;
			uint32_t w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;
			w = (w >> 22u) ^ w;
			return (w & 0xFFFFFFu) / 16777216.0f;
		}
	};

	// ================================================================ 덩어리 메시
	struct DetailVertex
	{
		XMFLOAT3 Pos;
		XMFLOAT3 Normal;
		XMFLOAT2 UV;      // x 잎 가로 (-1~1), y 잎 세로 (0~1)
		XMFLOAT4 Data;    // x 휨 가중, y 부위 (0 잎, 1 꽃잎, 2 꽃 가운데, 3 돌, 4 줄기), z 난수, w AO
	};
	static_assert(sizeof(DetailVertex) == 48, "셰이더 DetailVertexIn 과 같은 배치");

	struct MeshData
	{
		std::vector<DetailVertex> V;
		std::vector<uint32_t> I;
	};

	XMFLOAT3 F3(FXMVECTOR v) { XMFLOAT3 f; XMStoreFloat3(&f, v); return f; }

	// 잎 한 장: 뿌리에서 위로, 잎 면이 향한 쪽(yaw)으로 기울며(lean) 끝으로 갈수록 더 휜다(bend). 끝은 한 점
	void AddBlade(MeshData& m, const XMFLOAT3& root, float yaw, float height, float width, float lean, float bend, int segments,
		float rnd, float ao, float part, XMFLOAT3* tipOut = nullptr, XMFLOAT3* tangentOut = nullptr)
	{
		const XMVECTOR f = XMVectorSet(sinf(yaw), 0.0f, cosf(yaw), 0.0f);
		const XMVECTOR side = XMVectorSet(cosf(yaw), 0.0f, -sinf(yaw), 0.0f);
		const XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
		const uint32_t base = (uint32_t)m.V.size();
		XMVECTOR p = XMLoadFloat3(&root);
		const float segLen = height / segments;
		XMVECTOR t = up;
		for (int i = 0; i <= segments; ++i)
		{
			const float s = (float)i / segments;
			const float theta = lean + bend * s;
			t = XMVectorAdd(XMVectorScale(f, sinf(theta)), XMVectorScale(up, cosf(theta)));
			const XMFLOAT3 n = F3(XMVector3Normalize(XMVector3Cross(t, side)));
			if (i < segments)
			{
				const float w = width * 0.5f * (1.0f - powf(s, 1.8f) * 0.85f);
				m.V.push_back({ F3(XMVectorSubtract(p, XMVectorScale(side, w))), n, XMFLOAT2(-1.0f, s), XMFLOAT4(0, part, rnd, ao) });
				m.V.push_back({ F3(XMVectorAdd(p, XMVectorScale(side, w))), n, XMFLOAT2(1.0f, s), XMFLOAT4(0, part, rnd, ao) });
			}
			else
			{
				m.V.push_back({ F3(p), n, XMFLOAT2(0.0f, 1.0f), XMFLOAT4(0, part, rnd, ao) });
				if (tipOut)
					*tipOut = F3(p);
				if (tangentOut)
					*tangentOut = F3(t);
			}
			// 다음 줄: 구간 가운데의 기울기로 한 칸
			const float thetaMid = lean + bend * (s + 0.5f / segments);
			p = XMVectorAdd(p, XMVectorScale(XMVectorAdd(XMVectorScale(f, sinf(thetaMid)), XMVectorScale(up, cosf(thetaMid))), segLen));
		}
		for (int i = 0; i < segments - 1; ++i)
		{
			const uint32_t a = base + i * 2;
			m.I.insert(m.I.end(), { a, a + 2, a + 1, a + 1, a + 2, a + 3 });
		}
		const uint32_t last = base + (segments - 1) * 2;
		m.I.insert(m.I.end(), { last, last + 2, last + 1 });
	}

	XMVECTOR Perpendicular(FXMVECTOR h)
	{
		const XMVECTOR axis = fabsf(XMVectorGetZ(h)) < 0.9f ? XMVectorSet(0, 0, 1, 0) : XMVectorSet(1, 0, 0, 0);
		return XMVector3Normalize(XMVector3Cross(h, axis));
	}

	// 꽃송이: 줄기 끝에서 위(+ 줄기 방향)를 보는 꽃잎 여러 장 (컵처럼 살짝 들림) + 가운데 원반
	void AddFlowerHead(MeshData& m, const XMFLOAT3& tip, const XMFLOAT3& tangent, float len, int petals, float rnd)
	{
		const XMVECTOR P = XMLoadFloat3(&tip);
		const XMVECTOR h = XMVector3Normalize(XMVectorAdd(XMVectorScale(XMLoadFloat3(&tangent), 0.35f), XMVectorSet(0, 0.65f, 0, 0)));
		const XMVECTOR e1 = Perpendicular(h);
		const XMVECTOR e2 = XMVector3Cross(h, e1);
		const float cup = 0.25f + 0.35f * rnd;
		const float rows[3] = { 0.0f, 0.55f, 1.0f };
		const float widths[3] = { 0.14f, 0.46f, 0.24f };
		for (int k = 0; k < petals; ++k)
		{
			const float a = XM_2PI * k / petals + rnd * 1.7f;
			const XMVECTOR d = XMVectorAdd(XMVectorScale(e1, cosf(a)), XMVectorScale(e2, sinf(a)));
			const XMVECTOR dir = XMVector3Normalize(XMVectorAdd(XMVectorScale(d, cosf(cup)), XMVectorScale(h, sinf(cup))));
			const XMVECTOR sp = XMVector3Normalize(XMVector3Cross(h, d));
			const XMFLOAT3 n = F3(XMVector3Normalize(XMVector3Cross(dir, sp)));
			const XMVECTOR start = XMVectorAdd(P, XMVectorAdd(XMVectorScale(h, len * 0.05f), XMVectorScale(d, len * 0.1f)));
			const uint32_t base = (uint32_t)m.V.size();
			const float petalLen = len * (0.9f + 0.2f * sinf(a * 3.7f + rnd * 9.0f));
			for (int r = 0; r < 3; ++r)
			{
				const XMVECTOR c = XMVectorAdd(start, XMVectorScale(dir, petalLen * rows[r]));
				const float w = petalLen * widths[r] * 0.5f;
				m.V.push_back({ F3(XMVectorSubtract(c, XMVectorScale(sp, w))), n, XMFLOAT2(-1.0f, rows[r]), XMFLOAT4(0, 1.0f, rnd, 1.0f) });
				m.V.push_back({ F3(XMVectorAdd(c, XMVectorScale(sp, w))), n, XMFLOAT2(1.0f, rows[r]), XMFLOAT4(0, 1.0f, rnd, 1.0f) });
			}
			for (int r = 0; r < 2; ++r)
			{
				const uint32_t a0 = base + r * 2;
				m.I.insert(m.I.end(), { a0, a0 + 2, a0 + 1, a0 + 1, a0 + 2, a0 + 3 });
			}
		}
		// 가운데 (부채꼴 원반, 살짝 볼록)
		const uint32_t center = (uint32_t)m.V.size();
		const XMFLOAT3 hn = F3(h);
		m.V.push_back({ F3(XMVectorAdd(P, XMVectorScale(h, len * 0.16f))), hn, XMFLOAT2(0, 0), XMFLOAT4(0, 2.0f, rnd, 1.0f) });
		constexpr int kRing = 7;
		for (int k = 0; k < kRing; ++k)
		{
			const float a = XM_2PI * k / kRing;
			const XMVECTOR d = XMVectorAdd(XMVectorScale(e1, cosf(a)), XMVectorScale(e2, sinf(a)));
			m.V.push_back({ F3(XMVectorAdd(P, XMVectorAdd(XMVectorScale(h, len * 0.08f), XMVectorScale(d, len * 0.26f)))), F3(XMVector3Normalize(XMVectorAdd(h, XMVectorScale(d, 0.5f)))),
				XMFLOAT2(0, 1), XMFLOAT4(0, 2.0f, rnd, 0.8f) });
		}
		for (int k = 0; k < kRing; ++k)
			m.I.insert(m.I.end(), { center, center + 1 + (uint32_t)k, center + 1 + (uint32_t)((k + 1) % kRing) });
	}

	// 정이십면체 (level 0 = 12 정점, 1 = 42 정점)
	void Icosphere(int level, std::vector<XMFLOAT3>& verts, std::vector<uint32_t>& tris)
	{
		const float t = (1.0f + sqrtf(5.0f)) * 0.5f;
		verts = { { -1, t, 0 }, { 1, t, 0 }, { -1, -t, 0 }, { 1, -t, 0 }, { 0, -1, t }, { 0, 1, t }, { 0, -1, -t }, { 0, 1, -t },
			{ t, 0, -1 }, { t, 0, 1 }, { -t, 0, -1 }, { -t, 0, 1 } };
		for (auto& v : verts)
			v = F3(XMVector3Normalize(XMLoadFloat3(&v)));
		tris = { 0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11, 1, 5, 9, 5, 11, 4, 11, 10, 2, 10, 7, 6, 7, 1, 8,
			3, 9, 4, 3, 4, 2, 3, 2, 6, 3, 6, 8, 3, 8, 9, 4, 9, 5, 2, 4, 11, 6, 2, 10, 8, 6, 7, 9, 8, 1 };
		for (int l = 0; l < level; ++l)
		{
			std::map<std::pair<uint32_t, uint32_t>, uint32_t> mids;
			auto mid = [&](uint32_t a, uint32_t b) {
				const auto key = std::make_pair((std::min)(a, b), (std::max)(a, b));
				if (auto it = mids.find(key); it != mids.end())
					return it->second;
				verts.push_back(F3(XMVector3Normalize(XMVectorAdd(XMLoadFloat3(&verts[a]), XMLoadFloat3(&verts[b])))));
				return mids[key] = (uint32_t)verts.size() - 1;
			};
			std::vector<uint32_t> next;
			for (size_t i = 0; i < tris.size(); i += 3)
			{
				const uint32_t a = tris[i], b = tris[i + 1], c = tris[i + 2];
				const uint32_t ab = mid(a, b), bc = mid(b, c), ca = mid(c, a);
				next.insert(next.end(), { a, ab, ca, b, bc, ab, c, ca, bc, ab, bc, ca });
			}
			tris.swap(next);
		}
	}

	// 돌 하나: 찌그러진 구를 납작하게, 반쯤 묻히게
	void AddPebble(MeshData& m, const XMFLOAT3& center, float size, float yaw, float flat, float rnd, int level)
	{
		std::vector<XMFLOAT3> sv;
		std::vector<uint32_t> st;
		Icosphere(level, sv, st);
		const uint32_t base = (uint32_t)m.V.size();
		const float k1 = rnd * 37.0f, k2 = rnd * 91.0f;
		const float sx = size * 0.5f, sy = size * 0.5f * flat, sz = size * 0.5f * (0.7f + 0.3f * rnd);
		const float cs = cosf(yaw), sn = sinf(yaw);
		float minY = 1e30f, maxY = -1e30f;
		for (const XMFLOAT3& d : sv)
		{
			// 낮은 주파수 굴곡 + 한쪽이 깎인 면
			float r = 1.0f + 0.13f * sinf(d.x * 3.1f + d.y * 1.7f + k1) + 0.09f * sinf(d.z * 4.3f - d.x * 2.2f + k2);
			r -= 0.12f * (std::max)(0.0f, d.x * 0.6f + d.z * 0.8f - 0.4f);
			const float x = d.x * r * sx, y = d.y * r * sy, z = d.z * r * sz;
			const XMFLOAT3 p(center.x + x * cs - z * sn, center.y + y - sy * 0.35f, center.z + x * sn + z * cs);
			m.V.push_back({ p, XMFLOAT3(0, 0, 0), XMFLOAT2(0, 0), XMFLOAT4(0, 3.0f, rnd, 1.0f) });
			minY = (std::min)(minY, p.y);
			maxY = (std::max)(maxY, p.y);
		}
		for (size_t i = 0; i < st.size(); i += 3)
		{
			const uint32_t a = base + st[i], b = base + st[i + 1], c = base + st[i + 2];
			// 바깥을 보게 (정이십면체 감김 방향 → 왼손 좌표계에서 앞면)
			m.I.insert(m.I.end(), { a, c, b });
			const XMVECTOR pa = XMLoadFloat3(&m.V[a].Pos), pb = XMLoadFloat3(&m.V[b].Pos), pc = XMLoadFloat3(&m.V[c].Pos);
			const XMVECTOR n = XMVector3Cross(XMVectorSubtract(pb, pa), XMVectorSubtract(pc, pa));
			for (uint32_t idx : { a, b, c })
				XMStoreFloat3(&m.V[idx].Normal, XMVectorAdd(XMLoadFloat3(&m.V[idx].Normal), n));
		}
		const XMVECTOR c0 = XMLoadFloat3(&center);
		for (uint32_t i = base; i < (uint32_t)m.V.size(); ++i)
		{
			XMVECTOR n = XMVector3Normalize(XMLoadFloat3(&m.V[i].Normal));
			// 감김 방향이 반대였어도 바깥을 보게
			if (XMVectorGetX(XMVector3Dot(n, XMVectorSubtract(XMLoadFloat3(&m.V[i].Pos), c0))) < 0.0f)
				n = XMVectorNegate(n);
			XMStoreFloat3(&m.V[i].Normal, n);
			// AO: 땅에 닿는 아래쪽 어둡게
			m.V[i].Data.w = 0.35f + 0.65f * powf(std::clamp((m.V[i].Pos.y - minY) / (std::max)(maxY - minY, 1e-4f), 0.0f, 1.0f), 0.6f);
		}
	}

	MeshData BuildMesh(const DetailPrototype& p, int lod, float& maxY)
	{
		MeshData m;
		Rng rng((uint32_t)p.Seed * 9781u + 17u);
		const bool coarse = lod > 0;
		static const int kSegments[3] = { 3, 2, 1 };
		static const float kWidth[3] = { 1.0f, 1.7f, 2.6f };
		const float radius = (std::max)(p.Radius, 0.0f);
		auto outwardYaw = [](const XMFLOAT3& root, float jitter, float rnd) {
			return fabsf(root.x) + fabsf(root.z) > 1e-4f ? atan2f(root.x, root.z) + (rnd - 0.5f) * jitter : rnd * XM_2PI;
		};
		// 잎 여러 장. 난수는 LOD 와 상관없이 같은 순서로 뽑아 LOD1 (앞쪽 절반)·LOD2 (앞쪽 1/4) 가 LOD0 의 같은 자리에 오게
		auto blades = [&](int count, float heightScale, float part) {
			const int n = lod == 0 ? count : (std::max)(1, (count + (lod == 1 ? 1 : 3)) / (lod == 1 ? 2 : 4));
			for (int i = 0; i < count; ++i)
			{
				const float rd = rng(), ra = rng(), ry = rng(), rh = rng(), rw = rng(), rl = rng(), rr = rng();
				if (i >= n)
					continue;
				const float d = radius * sqrtf(rd);
				const float a = ra * XM_2PI;
				const XMFLOAT3 root(cosf(a) * d, 0.0f, sinf(a) * d);
				const float edge = radius > 0.0f ? d / radius : 0.5f;
				const float h = p.Height * heightScale * (0.55f + 0.45f * rh) * (1.0f - 0.25f * edge);
				const float w = p.BladeWidth * (0.7f + 0.6f * rw) * kWidth[(std::min)(lod, 2)];
				const float lean = p.Lean * (0.25f + 0.75f * edge) * (0.6f + 0.8f * rl);
				AddBlade(m, root, outwardYaw(root, 1.6f, ry), h, w, lean, p.Bend * (0.5f + rl), kSegments[(std::min)(lod, 2)], rr, 0.65f + 0.35f * edge, part);
			}
		};
		switch (p.Type)
		{
		case DetailPrototype::Kind::Pebble:
			for (int k = 0; k < p.Blades; ++k)
			{
				const float rd = rng(), ra = rng(), rs = rng(), ry = rng(), rq = rng(), rr = rng();
				const float d = k == 0 ? 0.0f : radius * sqrtf(rd);
				const XMFLOAT3 c(cosf(ra * XM_2PI) * d, 0.0f, sinf(ra * XM_2PI) * d);
				const float size = p.BladeWidth * (0.45f + 0.75f * rs) * (k == 0 ? 1.2f : 1.0f);
				AddPebble(m, c, size, ry * XM_2PI, 0.45f + 0.35f * rq, rr, coarse ? 0 : 1);
			}
			break;
		case DetailPrototype::Kind::Flower:
			blades(p.Blades, 0.45f, 0.0f);
			for (int k = 0; k < p.Heads; ++k)
			{
				const float rd = rng(), ra = rng(), ry = rng(), rh = rng(), rl = rng(), rc = rng();
				const float d = radius * 0.6f * sqrtf(rd);
				const XMFLOAT3 root(cosf(ra * XM_2PI) * d, 0.0f, sinf(ra * XM_2PI) * d);
				XMFLOAT3 tip, tangent;
				AddBlade(m, root, outwardYaw(root, 1.0f, ry), p.Height * (0.75f + 0.25f * rh), p.BladeWidth * 0.35f, p.Lean * 0.4f * rl, p.Bend * 0.3f,
					coarse ? 2 : 5, rc, 0.85f, 4.0f, &tip, &tangent);
				AddFlowerHead(m, tip, tangent, p.HeadSize * (0.8f + 0.4f * rc), p.Petals, rc);
			}
			break;
		default:
			blades(p.Blades, 1.0f, 0.0f);
			break;
		}
		// 휨 가중 = (높이 / 덩어리 높이)²: 바람이 끝일수록 많이 휘게. 돌은 0
		maxY = 0.01f;
		for (const DetailVertex& v : m.V)
			maxY = (std::max)(maxY, v.Pos.y);
		for (DetailVertex& v : m.V)
			if (v.Data.y != 3.0f)
			{
				const float t = std::clamp(v.Pos.y / maxY, 0.0f, 1.0f);
				v.Data.x = t * t;
			}
		return m;
	}

	struct GpuMesh
	{
		ComPtr<GfxBuffer> VB, IB;
		UINT IndexCount = 0;
		int VertexCount = 0;
		float MaxY = 0.0f;
		uint32_t LastUse = 0;
	};
	std::unordered_map<std::string, std::shared_ptr<GpuMesh>> s_Meshes;

	std::shared_ptr<GpuMesh> GetMesh(const DetailPrototype& proto, int lod)
	{
		thread_local std::string key;   // 다시 쓰는 버퍼 (찾을 때마다 임시 문자열을 만들지 않게)
		char tail[16];
		snprintf(tail, sizeof(tail), "#%d", lod);
		key.assign(proto.MeshKey());
		key.append(tail);
		if (auto it = s_Meshes.find(key); it != s_Meshes.end())
		{
			it->second->LastUse = SceneCulling::FrameIndex();
			return it->second;
		}
		auto g = std::make_shared<GpuMesh>();
		const MeshData m = BuildMesh(proto, lod, g->MaxY);
		g->VertexCount = (int)m.V.size();
		g->IndexCount = (UINT)m.I.size();
		g->LastUse = SceneCulling::FrameIndex();
		if (!m.V.empty() && !m.I.empty())
		{
			auto device = Application::GetI()->GetDevice();
			D3D11_BUFFER_DESC bd = {};
			bd.Usage = D3D11_USAGE_IMMUTABLE;
			bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
			bd.ByteWidth = (UINT)(m.V.size() * sizeof(DetailVertex));
			D3D11_SUBRESOURCE_DATA init = { m.V.data(), 0, 0 };
			device->CreateBuffer(&bd, &init, g->VB.GetAddressOf());
			bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
			bd.ByteWidth = (UINT)(m.I.size() * sizeof(uint32_t));
			init.pSysMem = m.I.data();
			device->CreateBuffer(&bd, &init, g->IB.GetAddressOf());
		}
		EditorLog::Write("Detail", "mesh '%s' LOD%d: %d vertices, %u triangles", proto.Name.c_str(), lod, g->VertexCount, g->IndexCount / 3);
		// 슬라이더를 끌면 모양이 많이 생기므로 오래 안 쓴 것부터 버린다
		if (s_Meshes.size() > 64)
			for (auto i = s_Meshes.begin(); i != s_Meshes.end();)
				i = SceneCulling::FrameIndex() - i->second->LastUse > 600 ? s_Meshes.erase(i) : std::next(i);
		s_Meshes[key] = g;
		return g;
	}

	// ================================================================ 효과 변수
	struct DetailVars
	{
		FxEffect* Fx = nullptr;
		FxTechnique* Tech = nullptr;
		FxVar *Wind = nullptr, *WindParams = nullptr, *Cam = nullptr, *Healthy = nullptr, *Dry = nullptr,
			*Flower = nullptr, *Center = nullptr, *Params = nullptr;
		void Bind(FxEffect* fx, const char* tech)
		{
			Fx = fx;
			Tech = fx->GetTechniqueByName(tech);
			Wind = fx->GetVariableByName("gDetailWind")->AsVector();
			WindParams = fx->GetVariableByName("gDetailWindParams")->AsVector();
			Cam = fx->GetVariableByName("gDetailCam")->AsVector();
			Healthy = fx->GetVariableByName("gDetailHealthy")->AsVector();
			Dry = fx->GetVariableByName("gDetailDry")->AsVector();
			Flower = fx->GetVariableByName("gDetailFlower")->AsVector();
			Center = fx->GetVariableByName("gDetailCenter")->AsVector();
			Params = fx->GetVariableByName("gDetailParams")->AsVector();
		}
		bool Valid() const { return Tech && Tech->IsValid(); }
	};

	enum { kMain, kShadow, kNormalDepth };
	DetailVars& Vars(int pass)
	{
		static DetailVars vars[3];
		static bool bound = false;
		if (!bound)
		{
			bound = true;
			vars[kMain].Bind(Effects::InstancedBasicFX->GetFX(), "DetailTech");
			vars[kShadow].Bind(Effects::BuildShadowMapFX->GetFX(), "DetailShadowTech");
			vars[kNormalDepth].Bind(Effects::SsaoNormalDepthFX->GetFX(), "DetailNormalDepthTech");
			if (!vars[kMain].Valid() || !vars[kShadow].Valid() || !vars[kNormalDepth].Valid())
				EditorLog::Write("Detail", "detail techniques missing (main %d, shadow %d, normalDepth %d)", vars[kMain].Valid(), vars[kShadow].Valid(), vars[kNormalDepth].Valid());
		}
		return vars[pass];
	}

	void SetVec(FxVar* v, const XMFLOAT4& f)
	{
		if (v && v->IsValid())
			v->SetFloatVector(&f.x);
	}
	void SetMatrix(FxEffect* fx, const char* name, CXMMATRIX m)
	{
		if (auto* v = fx->GetVariableByName(name)->AsMatrix(); v && v->IsValid())
			v->SetMatrix(reinterpret_cast<const float*>(&m));
	}

	// ================================================================ 인스턴스 / 입력 배치
	struct InstanceData
	{
		XMFLOAT4 PosYaw;   // 지면 위치(월드) + 회전
		XMFLOAT4 Scale;    // 폭, 높이, 색 난수, 순위
		XMFLOAT4 Ground;   // 지면 법선 + 바람 위상
	};
	static_assert(sizeof(InstanceData) == 48, "셰이더 DetailInstanceIn 과 같은 배치");

	ComPtr<GfxInputLayout> s_Layout;

	bool EnsureLayout()
	{
		static bool tried = false;
		if (tried)
			return s_Layout != nullptr;
		tried = true;
		DetailVars& v = Vars(kMain);
		if (!v.Valid())
			return false;
		const D3D11_INPUT_ELEMENT_DESC desc[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "DETAIL", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "INSTANCE", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
			{ "INSTANCE", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
			{ "INSTANCE", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
		};
		D3DX11_PASS_DESC pd;
		v.Tech->GetPassByIndex(0)->GetDesc(&pd);
		const HRESULT hr = Application::GetI()->GetDevice()->CreateInputLayout(desc, _countof(desc), pd.pIAInputSignature, pd.IAInputSignatureSize, s_Layout.GetAddressOf());
		if (FAILED(hr))
			EditorLog::Write("Detail", "input layout failed hr=0x%08X", (unsigned)hr);
		return s_Layout != nullptr;
	}

	// 목록들을 이어 붙여 한 버퍼에 (종류·LOD 순서 = 그리는 순서)
	template <typename Set>
	bool UploadSet(GfxContext* dc, Set& set)
	{
		const UINT count = (UINT)set.Total;
		if (count == 0)
			return false;
		if (count > set.Capacity)
		{
			set.Capacity = (std::max)(count, set.Capacity * 2 + 4096);
			D3D11_BUFFER_DESC bd = {};
			bd.Usage = D3D11_USAGE_DYNAMIC;
			bd.ByteWidth = set.Capacity * sizeof(InstanceData);
			bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
			bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			set.Buffer.Reset();
			Application::GetI()->GetDevice()->CreateBuffer(&bd, nullptr, set.Buffer.GetAddressOf());
		}
		D3D11_MAPPED_SUBRESOURCE mapped;
		if (!set.Buffer || FAILED(dc->Map(set.Buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
			return false;
		InstanceData* dst = (InstanceData*)mapped.pData;
		for (const auto& b : set.Batches)
			for (const auto& list : b.Lists)
			{
				if (!list.empty())
					memcpy(dst, list.data(), list.size() * sizeof(InstanceData));
				dst += list.size();
			}
		dc->Unmap(set.Buffer.Get(), 0);
		return true;
	}

	void ExtractPlanes(CXMMATRIX m, XMFLOAT4 planes[6])
	{
		XMFLOAT4X4 f;
		XMStoreFloat4x4(&f, m);
		auto col = [&](int c) { return XMVectorSet(f.m[0][c], f.m[1][c], f.m[2][c], f.m[3][c]); };
		const XMVECTOR c0 = col(0), c1 = col(1), c2 = col(2), c3 = col(3);
		const XMVECTOR p[6] = { c3 + c0, c3 - c0, c3 + c1, c3 - c1, c3 - c2, c2 };
		for (int i = 0; i < 6; ++i)
			XMStoreFloat4(&planes[i], XMPlaneNormalize(p[i]));
	}

	// ================================================================ 조각 캐시
	struct Chunk
	{
		std::vector<std::vector<InstanceData>> Protos;   // 종류마다 (순위 순)
		Vec3 Min = Vec3(0, 0, 0), Max = Vec3(0, 0, 0);    // 월드 경계
		size_t Content = 0, Pending = 0;
		unsigned Revs[3] = { ~0u, ~0u, ~0u };
		size_t ProtoHash = 0;
		bool Built = false, NeedsBuild = true;
		uint32_t LastUse = 0;
		int X = 0, Z = 0;
		float Dist = 0.0f;
	};
	struct TerrainCache
	{
		std::unordered_map<uint64_t, Chunk> Chunks;
		Vec3 Origin = Vec3(1e30f, 0, 0);
		uint32_t LastUse = 0;
	};
	std::unordered_map<const TerrainData*, TerrainCache> s_Cache;

	struct Hasher
	{
		uint64_t h = 1469598103934665603ull;
		void Bytes(const void* p, size_t n)
		{
			const uint8_t* b = (const uint8_t*)p;
			size_t i = 0;
			for (; i + 8 <= n; i += 8)
			{
				uint64_t w;
				memcpy(&w, b + i, 8);
				h = (h ^ w) * 1099511628211ull;
				h ^= h >> 29;
			}
			for (; i < n; ++i)
				h = (h ^ b[i]) * 1099511628211ull;
		}
		template <typename T> void Pod(const T& v) { Bytes(&v, sizeof(T)); }
	};

	bool UsesControl(const TerrainData& d)
	{
		const int channels = (std::min)((int)d.Layers.size(), 4);
		if (channels <= 0)
			return false;
		const uint32_t all = (1u << channels) - 1;
		for (const DetailPrototype& p : d.DetailPrototypes)
			if ((p.LayerMask & all) != all)
				return true;
		return false;
	}

	// 조각 내용 해시: 조각이 읽는 밀도 칸·높이 샘플·(필요하면) 컨트롤 칸
	size_t ChunkContent(const TerrainData& d, float x0, float z0, float x1, float z1, bool useControl)
	{
		Hasher hs;
		const int res = d.DetailResolution;
		const int cx0 = std::clamp((int)floorf(x0 / d.Size.x * res - 1.5f), 0, res - 1), cx1 = std::clamp((int)ceilf(x1 / d.Size.x * res + 0.5f), 0, res - 1);
		const int cz0 = std::clamp((int)floorf(z0 / d.Size.z * res - 1.5f), 0, res - 1), cz1 = std::clamp((int)ceilf(z1 / d.Size.z * res + 0.5f), 0, res - 1);
		for (const auto& map : d.DetailDensity)
			if (map.size() == (size_t)res * res)
				for (int z = cz0; z <= cz1; ++z)
					hs.Bytes(&map[(size_t)z * res + cx0], (size_t)(cx1 - cx0 + 1));
		const int hres = d.HeightmapResolution;
		const int hx0 = std::clamp((int)floorf(x0 / d.CellSizeX()) - 1, 0, hres - 1), hx1 = std::clamp((int)ceilf(x1 / d.CellSizeX()) + 1, 0, hres - 1);
		const int hz0 = std::clamp((int)floorf(z0 / d.CellSizeZ()) - 1, 0, hres - 1), hz1 = std::clamp((int)ceilf(z1 / d.CellSizeZ()) + 1, 0, hres - 1);
		for (int z = hz0; z <= hz1; ++z)
			hs.Bytes(&d.Heights[(size_t)z * hres + hx0], (size_t)(hx1 - hx0 + 1) * sizeof(float));
		if (useControl)
		{
			const int cres = d.ControlResolution;
			const int ax0 = std::clamp((int)floorf(x0 / d.Size.x * (cres - 1)) - 1, 0, cres - 1), ax1 = std::clamp((int)ceilf(x1 / d.Size.x * (cres - 1)) + 1, 0, cres - 1);
			const int az0 = std::clamp((int)floorf(z0 / d.Size.z * (cres - 1)) - 1, 0, cres - 1), az1 = std::clamp((int)ceilf(z1 / d.Size.z * (cres - 1)) + 1, 0, cres - 1);
			for (int z = az0; z <= az1; ++z)
				hs.Bytes(&d.Control[((size_t)z * cres + ax0) * 4], (size_t)(ax1 - ax0 + 1) * 4);
		}
		return (size_t)hs.h;
	}

	size_t ProtoHash(const TerrainData& d)
	{
		Hasher hs;
		for (const DetailPrototype& p : d.DetailPrototypes)
			hs.Pod(p.Hash());
		hs.Pod(d.Details.Hash());
		hs.Pod(d.DetailResolution);
		hs.Pod(d.Size);
		return (size_t)hs.h;
	}

	// 조각 하나 만들기: 종류마다 층화 격자(칸마다 무작위 한 점) 후보 → 밀도 확률로 남기기 → 경사·레이어 거르기
	void BuildChunk(Chunk& c, const TerrainData& d, const Vec3& origin)
	{
		const float x0 = c.X * kChunk, z0 = c.Z * kChunk;
		const float x1 = (std::min)(x0 + kChunk, d.Size.x), z1 = (std::min)(z0 + kChunk, d.Size.z);
		c.Protos.assign(d.DetailPrototypes.size(), {});
		Vec3 mn(1e30f, 1e30f, 1e30f), mx(-1e30f, -1e30f, -1e30f);
		float pad = 0.0f, top = 0.0f;
		const int res = d.DetailResolution;
		for (size_t pi = 0; pi < d.DetailPrototypes.size(); ++pi)
		{
			const DetailPrototype& P = d.DetailPrototypes[pi];
			const float density = P.Density * d.Details.DensityScale;
			if (density <= 0.0f || pi >= d.DetailDensity.size() || d.DetailDensity[pi].size() != (size_t)res * res)
				continue;
			// 빠른 거절: 조각 안 밀도 칸이 모두 0
			{
				const auto& map = d.DetailDensity[pi];
				const int cx0 = std::clamp((int)floorf(x0 / d.Size.x * res - 1.0f), 0, res - 1), cx1 = std::clamp((int)ceilf(x1 / d.Size.x * res), 0, res - 1);
				const int cz0 = std::clamp((int)floorf(z0 / d.Size.z * res - 1.0f), 0, res - 1), cz1 = std::clamp((int)ceilf(z1 / d.Size.z * res), 0, res - 1);
				bool any = false;
				for (int z = cz0; z <= cz1 && !any; ++z)
					for (int x = cx0; x <= cx1 && !any; ++x)
						any = map[(size_t)z * res + x] != 0;
				if (!any)
					continue;
			}
			const float cell = 1.0f / sqrtf(density);
			const int gx = (std::max)(1, (int)ceilf((x1 - x0) / cell)), gz = (std::max)(1, (int)ceilf((z1 - z0) / cell));
			const float sx = (x1 - x0) / gx, sz = (z1 - z0) / gz;
			const float cosMax = cosf(XMConvertToRadians(std::clamp(P.MaxSlope, 0.0f, 90.0f))) - 1e-4f;
			Rng rng((uint32_t)c.X * 73856093u ^ (uint32_t)c.Z * 19349663u ^ (uint32_t)(pi + 1) * 83492791u ^ (uint32_t)P.Seed * 2654435761u);
			auto& list = c.Protos[pi];
			for (int iz = 0; iz < gz; ++iz)
				for (int ix = 0; ix < gx; ++ix)
				{
					const float rx = rng(), rz = rng(), accept = rng(), ryaw = rng(), rs = rng(), rw = rng(), rc = rng(), rk = rng(), rp = rng();
					const float x = x0 + (ix + rx) * sx, z = z0 + (iz + rz) * sz;
					float dens = d.GetDetailDensity((int)pi, x, z);
					if (dens <= 0.0f || accept >= dens)
						continue;
					if (P.LayerMask != 0xFu && accept >= dens * d.GetLayerWeight(P.LayerMask, x, z))
						continue;
					const Vec3 n = d.GetNormal(x, z);
					if (n.y < cosMax)
						continue;
					const float y = d.GetHeight(x, z);
					const float hs = P.MinScale + (P.MaxScale - P.MinScale) * rs;
					const float ws = hs * (1.0f + (rw - 0.5f) * 2.0f * P.WidthVariation);
					InstanceData inst;
					inst.PosYaw = XMFLOAT4(origin.x + x, origin.y + y, origin.z + z, ryaw * XM_2PI);
					inst.Scale = XMFLOAT4(ws, hs, rc, rk);
					inst.Ground = XMFLOAT4(n.x, n.y, n.z, rp);
					list.push_back(inst);
					mn = Vec3((std::min)(mn.x, inst.PosYaw.x), (std::min)(mn.y, inst.PosYaw.y), (std::min)(mn.z, inst.PosYaw.z));
					mx = Vec3((std::max)(mx.x, inst.PosYaw.x), (std::max)(mx.y, inst.PosYaw.y), (std::max)(mx.z, inst.PosYaw.z));
				}
			std::sort(list.begin(), list.end(), [](const InstanceData& a, const InstanceData& b) { return a.Scale.w < b.Scale.w; });
			const float extent = (P.Type == DetailPrototype::Kind::Pebble ? P.BladeWidth : P.Height) * P.MaxScale;
			pad = (std::max)(pad, (P.Radius + extent) * P.MaxScale * 2.0f);
			top = (std::max)(top, extent * 1.2f);
		}
		c.Min = Vec3(mn.x - pad, mn.y - pad, mn.z - pad);
		c.Max = Vec3(mx.x + pad, mx.y + top + pad, mx.z + pad);
		c.Content = c.Pending;
		c.Built = true;
		c.NeedsBuild = false;
	}

	// 지형 하나: 카메라 근처 조각을 확인하고 바뀐 것을 다시 만든다 (화면마다 한 번)
	void UpdateTerrain(Terrain* terrain, TerrainData& d, const Vec3& camPos, DetailRenderer::Stats& st)
	{
		TerrainCache& tc = s_Cache[&d];
		const uint32_t frame = SceneCulling::FrameIndex();
		tc.LastUse = frame;
		const Vec3 origin = terrain->GetPosition();
		if ((tc.Origin - origin).LengthSquared() > 1e-8f)
		{
			tc.Chunks.clear();
			tc.Origin = origin;
		}
		// 프로토타입과 밀도 맵 수를 맞춘다 (불러온 파일이 어긋났을 때)
		const size_t mapSize = (size_t)d.DetailResolution * d.DetailResolution;
		d.DetailDensity.resize(d.DetailPrototypes.size());
		for (auto& map : d.DetailDensity)
			if (map.size() != mapSize)
				map.assign(mapSize, 0);

		const size_t protoHash = ProtoHash(d);
		const bool useControl = UsesControl(d);
		const float D = (std::max)(d.Details.Distance, 1.0f);
		const Vec3 cam = camPos - origin;
		const int nx = (std::max)(1, (int)ceilf(d.Size.x / kChunk)), nz = (std::max)(1, (int)ceilf(d.Size.z / kChunk));
		const int ix0 = std::clamp((int)floorf((cam.x - D) / kChunk), 0, nx - 1), ix1 = std::clamp((int)floorf((cam.x + D) / kChunk), 0, nx - 1);
		const int iz0 = std::clamp((int)floorf((cam.z - D) / kChunk), 0, nz - 1), iz1 = std::clamp((int)floorf((cam.z + D) / kChunk), 0, nz - 1);
		if (cam.x + D < 0 || cam.z + D < 0 || cam.x - D > d.Size.x || cam.z - D > d.Size.z)
			return;
		bool anyBuilt = false;
		std::vector<Chunk*> todo;
		for (int iz = iz0; iz <= iz1; ++iz)
			for (int ix = ix0; ix <= ix1; ++ix)
			{
				const float x0 = ix * kChunk, z0 = iz * kChunk, x1 = (std::min)(x0 + kChunk, d.Size.x), z1 = (std::min)(z0 + kChunk, d.Size.z);
				const float dx = (std::max)({ x0 - cam.x, 0.0f, cam.x - x1 }), dz = (std::max)({ z0 - cam.z, 0.0f, cam.z - z1 });
				const float dist = sqrtf(dx * dx + dz * dz);
				if (dist > D)
					continue;
				Chunk& c = tc.Chunks[((uint64_t)(uint32_t)ix << 32) | (uint32_t)iz];
				c.X = ix;
				c.Z = iz;
				c.LastUse = frame;
				c.Dist = dist;
				anyBuilt |= c.Built;
				if (c.Revs[0] != d.Revision || c.Revs[1] != d.DetailRevision || c.Revs[2] != d.ControlRevision || c.ProtoHash != protoHash || !c.Built)
				{
					c.Revs[0] = d.Revision;
					c.Revs[1] = d.DetailRevision;
					c.Revs[2] = d.ControlRevision;
					c.ProtoHash = protoHash;
					c.Pending = ChunkContent(d, x0, z0, x1, z1, useControl) ^ (protoHash * 31u);
					if (!c.Built || c.Pending != c.Content)
						c.NeedsBuild = true;
				}
				if (c.NeedsBuild)
					todo.push_back(&c);
			}
		if (!todo.empty())
		{
			const auto t0 = Clock::now();
			std::sort(todo.begin(), todo.end(), [](const Chunk* a, const Chunk* b) { return a->Dist < b->Dist; });
			const size_t limit = (std::min)(todo.size(), (size_t)(anyBuilt ? kBuildPerFrame : kFirstBuild));
			std::for_each(std::execution::par, todo.begin(), todo.begin() + limit, [&](Chunk* c) { BuildChunk(*c, d, origin); });
			st.Built += (int)limit;
			const float ms = std::chrono::duration<float, std::milli>(Clock::now() - t0).count();
			st.BuildMs += ms;
			if (!anyBuilt)
			{
				size_t instances = 0;
				for (size_t i = 0; i < limit; ++i)
					for (const auto& list : todo[i]->Protos)
						instances += list.size();
				EditorLog::Write("Detail", "%s: built %d chunks (%zu clumps, %d types) in %.1f ms", d.Name().c_str(), (int)limit, instances, (int)d.DetailPrototypes.size(), ms);
			}
		}
		// 오래 안 쓴 조각 버리기
		if ((frame & 63u) == 0)
			for (auto it = tc.Chunks.begin(); it != tc.Chunks.end();)
				it = frame - it->second.LastUse > 240 ? tc.Chunks.erase(it) : std::next(it);
		st.Cached += (int)tc.Chunks.size();
	}

	// ================================================================ 그리기 목록 (지형 × 종류 × LOD)
	struct Batch
	{
		const DetailPrototype* Proto = nullptr;
		const DetailSettings* Settings = nullptr;
		float Distance = 90.0f;
		std::vector<InstanceData> Lists[3];   // LOD0, LOD1, LOD2
	};
	// 그리기 목록 두 벌: [0] 카메라 (깊이 사전 패스 → 본 패스가 같은 목록·버퍼), [1] 그림자 (캐스케이드들이 같은 목록·버퍼)
	//  화면(게임/에디터)·프레임이 바뀔 때만 다시 모으고 한 번만 올린다
	struct ListSet
	{
		std::vector<Batch> Batches;
		ComPtr<GfxBuffer> Buffer;
		UINT Capacity = 0;
		uint32_t Frame = ~0u;
		int View = -1;
		int Chunks = 0;
		size_t Total = 0;
	};
	ListSet s_Sets[2];
	uint32_t s_UpdateFrame[2] = { ~0u, ~0u };
	DetailRenderer::Stats s_Pending[2];
	float s_Time = 0.0f;
}

namespace DetailRenderer
{
	const Stats& LastStats(bool editor) { return s_Stats[editor ? 1 : 0]; }

	bool GetMeshInfo(const DetailPrototype& proto, int& vertices, int& triangles, int lod)
	{
		auto mesh = GetMesh(proto, lod);
		if (!mesh)
			return false;
		vertices = mesh->VertexCount;
		triangles = (int)(mesh->IndexCount / 3);
		return true;
	}

	void CollectMemory(std::vector<MemoryStats::Item>& gpu, std::vector<MemoryStats::Item>& cpu)
	{
		for (const auto& [key, mesh] : s_Meshes)
			if (mesh)
				gpu.push_back({ "Detail mesh " + std::to_string(std::hash<std::string>()(key) % 100000) + " (LOD" + key.substr(key.rfind('#') + 1) + ")",
					MemoryStats::ResourceBytes(mesh->VB.Get()) + MemoryStats::ResourceBytes(mesh->IB.Get()) });
		for (const ListSet& set : s_Sets)
			if (set.Buffer)
				gpu.push_back({ "Detail instance buffer", MemoryStats::ResourceBytes(set.Buffer.Get()) });
		size_t bytes = 0;
		for (const auto& [data, tc] : s_Cache)
			for (const auto& [key, c] : tc.Chunks)
				for (const auto& list : c.Protos)
					bytes += list.capacity() * sizeof(InstanceData);
		if (bytes > 0)
			cpu.push_back({ "Detail chunks", bytes });
	}

	void DrawAll(Pass pass, bool editor)
	{
		const auto& terrains = Terrain::GetActiveTerrains();
		bool anyDetails = false;
		for (Terrain* t : terrains)
			if (auto d = t->GetTerrainData(); d && !d->DetailPrototypes.empty())
				anyDetails = true;
		if (!anyDetails)
		{
			if (pass == Pass::Main)
				s_Stats[editor ? 1 : 0] = Stats();
			return;
		}
		PROFILE_SCOPE("Details");
		PROFILE_GPU("Details");
		const int passIndex = pass == Pass::Main ? kMain : (pass == Pass::Shadow ? kShadow : kNormalDepth);
		DetailVars& v = Vars(passIndex);
		if (!v.Valid() || !EnsureLayout())
			return;
		RenderManager* rm = RenderManager::GetI();
		const bool shadow = pass == Pass::Shadow;
		const bool editorView = shadow ? rm->RenderingEditorView : editor;
		const XMMATRIX view = editorView ? rm->EditorCameraViewMatrix : rm->CameraViewMatrix;
		const XMMATRIX viewProj = editorView ? rm->EditorCameraViewProjectionMatrix : rm->CameraViewProjectionMatrix;
		const XMVECTOR camPosV = XMMatrixInverse(nullptr, view).r[3];
		const Vec3 camPos(XMVectorGetX(camPosV), XMVectorGetY(camPosV), XMVectorGetZ(camPosV));
		const uint32_t frame = SceneCulling::FrameIndex();
		const int vi = editorView ? 1 : 0;
		// 바람 시간은 프레임마다 한 번: 깊이 사전 패스와 본 패스의 잎 위치가 같아야 깊이 EQUAL 이 맞는다
		static uint32_t s_TimeFrame = ~0u;
		if (s_TimeFrame != frame)
		{
			static const auto start = Clock::now();
			s_Time = std::chrono::duration<float>(Clock::now() - start).count();
			s_TimeFrame = frame;
		}

		// ---- 화면마다 한 번: 조각 캐시 갱신
		if (s_UpdateFrame[vi] != frame)
		{
			PROFILE_SCOPE("Details.Update");
			s_UpdateFrame[vi] = frame;
			s_Pending[vi] = Stats();
			for (Terrain* terrain : terrains)
			{
				auto data = terrain->GetTerrainData();
				if (!data || data->DetailPrototypes.empty() || !terrain->IsEnabled() || !terrain->GetDraw() || (terrain->GetGameObject() && !terrain->GetGameObject()->IsActiveInHierarchy()))
					continue;
				UpdateTerrain(terrain, *data, camPos, s_Pending[vi]);
			}
			// 사라진 지형의 캐시
			for (auto it = s_Cache.begin(); it != s_Cache.end();)
				it = frame - it->second.LastUse > 600 ? s_Cache.erase(it) : std::next(it);
		}

		// ---- 그리기 목록: 카메라 = 절두체 안 조각, 그림자 = Shadow Distance 안 조각 (캐스케이드 절두체는 GPU 가 자른다)
		//      → 거리만큼 앞쪽(순위 낮은) 덩어리 → 카메라: 덩어리 거리로 LOD, 그림자: LOD1
		ListSet& set = s_Sets[shadow ? 1 : 0];
		Stats stats = s_Pending[vi];
		GfxContext* dc = Application::GetI()->GetDeviceContext();
		if (set.Frame != frame || set.View != vi)
		{
			PROFILE_SCOPE("Details.Collect");
			set.Frame = frame;
			set.View = vi;
			set.Batches.clear();
			set.Chunks = 0;
			set.Total = 0;
			XMFLOAT4 planes[6];
			ExtractPlanes(viewProj, planes);
			for (Terrain* terrain : terrains)
			{
				auto data = terrain->GetTerrainData();
				if (!data || data->DetailPrototypes.empty() || !terrain->IsEnabled() || !terrain->GetDraw() || (terrain->GetGameObject() && !terrain->GetGameObject()->IsActiveInHierarchy()))
					continue;
				if (!shadow && !RenderLayers::Visible(terrain->GetGameObject()))
					continue;   // Camera 의 Culling Mask (지형의 레이어)
				auto it = s_Cache.find(data.get());
				if (it == s_Cache.end())
					continue;
				const TerrainData& d = *data;
				const float D = (std::max)(d.Details.Distance, 1.0f);
				const float lod0 = (std::min)(D * 0.15f, 15.0f), lod1 = (std::min)(D * 0.4f, 40.0f);
				const size_t first = set.Batches.size();
				for (const DetailPrototype& p : d.DetailPrototypes)
				{
					Batch b;
					b.Proto = &p;
					b.Settings = &d.Details;
					b.Distance = D;
					set.Batches.push_back(std::move(b));
				}
				for (auto& [key, c] : it->second.Chunks)
				{
					if (!c.Built || c.LastUse + 2 < frame)
						continue;
					if (!shadow)
					{
						bool inside = true;
						for (int i = 0; i < 6 && inside; ++i)
						{
							const XMFLOAT4& pl = planes[i];
							const float px = pl.x >= 0 ? c.Max.x : c.Min.x, py = pl.y >= 0 ? c.Max.y : c.Min.y, pz = pl.z >= 0 ? c.Max.z : c.Min.z;
							inside = pl.x * px + pl.y * py + pl.z * pz + pl.w >= 0.0f;
						}
						if (!inside)
							continue;
					}
					const float dx = (std::max)({ c.Min.x - camPos.x, 0.0f, camPos.x - c.Max.x });
					const float dy = (std::max)({ c.Min.y - camPos.y, 0.0f, camPos.y - c.Max.y });
					const float dz = (std::max)({ c.Min.z - camPos.z, 0.0f, camPos.z - c.Max.z });
					const float nearDist = sqrtf(dx * dx + dy * dy + dz * dz);
					if (shadow && nearDist > d.Details.ShadowDistance)
						continue;
					const float keep = Keep(nearDist / D);
					if (keep <= 0.0f)
						continue;
					++set.Chunks;
					for (size_t pi = 0; pi < c.Protos.size() && pi < d.DetailPrototypes.size(); ++pi)
					{
						if (shadow && !d.DetailPrototypes[pi].CastShadows)
							continue;
						const auto& list = c.Protos[pi];
						const auto end = std::lower_bound(list.begin(), list.end(), keep, [](const InstanceData& a, float k) { return a.Scale.w < k; });
						Batch& b = set.Batches[first + pi];
						if (shadow)
						{
							// 그림자는 흐릿하므로 절반 밀도 (앞쪽 절반 순위) + 가장 거친 메시 (LOD2)
							const auto half = std::lower_bound(list.begin(), end, keep * 0.5f, [](const InstanceData& a, float k) { return a.Scale.w < k; });
							b.Lists[2].insert(b.Lists[2].end(), list.begin(), half);
							continue;
						}
						for (auto i = list.begin(); i != end; ++i)
						{
							const float ex = i->PosYaw.x - camPos.x, ey = i->PosYaw.y - camPos.y, ez = i->PosYaw.z - camPos.z;
							const float d2 = ex * ex + ey * ey + ez * ez;
							b.Lists[d2 > lod1 * lod1 ? 2 : (d2 > lod0 * lod0 ? 1 : 0)].push_back(*i);
						}
					}
				}
			}
			for (const Batch& b : set.Batches)
				for (const auto& list : b.Lists)
					set.Total += list.size();
			if (set.Total > 0 && !UploadSet(dc, set))
				set.Total = 0;
		}
		stats.Chunks = set.Chunks;
		if (set.Total == 0)
		{
			if (pass == Pass::Main)
				s_Stats[editor ? 1 : 0] = stats;
			return;
		}
		GfxBuffer* inst = set.Buffer.Get();
		// 그림자: 덩어리가 이 캐스케이드 텍셀보다 충분히 클 때만 (먼 캐스케이드에서는 보이지 않으므로)
		const float texel = shadow ? rm->ShadowTexelWorld : 0.0f;

		if (pass == Pass::Main)
		{
			static const XMMATRIX toTex(0.5f, 0.0f, 0.0f, 0.0f, 0.0f, -0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.5f, 0.5f, 0.0f, 1.0f);
			Effects::InstancedBasicFX->SetViewProj(viewProj);
			SetMatrix(v.Fx, "gViewProjTex", viewProj * toTex);
			ShaderSetting setting = UMaterial::GetDefault()->GetShaderSetting();
			setting.UseShadowMap = 1;
			setting.UseSsaoMap = 1;   // SSAO (Volume 이 끄면 흰 맵)
			Effects::InstancedBasicFX->SetShaderSetting(setting);
		}
		else if (pass == Pass::NormalDepth)
		{
			SetMatrix(v.Fx, "gView", view);
			SetMatrix(v.Fx, "gWorldViewProj", viewProj);
		}

		ComPtr<GfxDepthStencilState> prevDSS;
		UINT prevRef = 0;
		dc->OMGetDepthStencilState(prevDSS.GetAddressOf(), &prevRef);
		ComPtr<GfxRasterizerState> prevRS;
		dc->RSGetState(prevRS.GetAddressOf());
		dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		dc->IASetInputLayout(s_Layout.Get());

		UINT offset = 0;
		for (const Batch& b : set.Batches)
		{
			const DetailPrototype& p = *b.Proto;
			const size_t n0 = b.Lists[0].size() + b.Lists[1].size() + b.Lists[2].size(), n1 = 0;
			if (n0 + n1 == 0)
				continue;
			if (shadow && texel > 0.0f)
			{
				const float size = p.Type == DetailPrototype::Kind::Pebble ? p.BladeWidth : (std::max)(p.Height * 0.5f, p.Radius);
				if (texel > size * 0.2f)
				{
					offset += (UINT)(n0 + n1);
					continue;
				}
			}
			// 바람·거리 (지형마다 다를 수 있다)
			const DetailSettings& ds = *b.Settings;
			const float yaw = XMConvertToRadians(ds.WindDirection);
			SetVec(v.Cam, XMFLOAT4(camPos.x, camPos.y, camPos.z, b.Distance));
			// 날씨 바람은 휘는 정도만 (빠르기를 바꾸면 물결 위상이 튄다)
			SetVec(v.Wind, XMFLOAT4(sinf(yaw), cosf(yaw), s_Time * ds.WindSpeed, ds.WindBending * std::clamp(WeatherState::Get().WindStrength, 0.0f, 4.0f)));
			for (int lod = 0; lod < 3; ++lod)
			{
				const UINT count = (UINT)b.Lists[lod].size();
				if (count == 0)
					continue;
				auto mesh = GetMesh(p, lod);
				if (!mesh || !mesh->VB)
				{
					offset += count;
					continue;
				}
				SetVec(v.WindParams, XMFLOAT4(1.0f / (std::max)(ds.WindSize, 0.5f), p.WindResponse, mesh->MaxY, s_Time));
				SetVec(v.Healthy, XMFLOAT4(p.HealthyColor.x, p.HealthyColor.y, p.HealthyColor.z, p.DryAmount));
				SetVec(v.Dry, XMFLOAT4(p.DryColor.x, p.DryColor.y, p.DryColor.z, p.NoiseSpread));
				SetVec(v.Flower, XMFLOAT4(p.FlowerColor.x, p.FlowerColor.y, p.FlowerColor.z, p.ColorVariation));
				SetVec(v.Center, XMFLOAT4(p.CenterColor.x, p.CenterColor.y, p.CenterColor.z, p.TipColor));
				SetVec(v.Params, XMFLOAT4(p.Translucency, p.Smoothness, (float)(int)p.Type, p.GroundAlign));
				const UINT strides[2] = { sizeof(DetailVertex), sizeof(InstanceData) }, offsets[2] = { 0, 0 };
				GfxBuffer* vbs[2] = { mesh->VB.Get(), inst };
				dc->IASetVertexBuffers(0, 2, vbs, strides, offsets);
				dc->IASetIndexBuffer(mesh->IB.Get(), DXGI_FORMAT_R32_UINT, 0);
				v.Tech->GetPassByIndex(0)->Apply(0, dc);
				dc->DrawIndexedInstanced(mesh->IndexCount, count, 0, 0, offset);
				offset += count;
				++stats.DrawCalls;
				stats.Instances += (int)count;
			}
		}
		GfxBuffer* none = nullptr;
		UINT zero = 0;
		dc->IASetVertexBuffers(1, 1, &none, &zero, &zero);
		dc->OMSetDepthStencilState(prevDSS.Get(), prevRef);
		dc->RSSetState(prevRS.Get());
		if (pass == Pass::Main)
			s_Stats[editor ? 1 : 0] = stats;
	}
}
