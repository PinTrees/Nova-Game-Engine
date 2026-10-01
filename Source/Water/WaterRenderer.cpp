#include "pch.h"
#include "WaterRenderer.h"
#include "WaterBody.h"
#include "WaterProfile.h"
#include "WaterWaves.h"
#include "Effects.h"
#include "LightHelper.h"
#include "Terrain.h"
#include "TerrainData.h"
#include "ResourceManager.h"
#include "WaterShore.h"

namespace
{
	// ---------------------------------------------------------------- 바다 격자 (카메라 LOD)
	//  레벨 0 = -64~64 칸 전체, 레벨 1~ = 같은 크기에서 가운데(-30~30 칸, 안쪽 레벨과 2 칸 겹침)를 뺀 고리
	constexpr int kLevels = 10;
	constexpr int kHalf = 64;
	constexpr int kHole = 30;
	constexpr float kBaseCell = 0.5f;

	struct SurfaceVertex
	{
		XMFLOAT3 Pos;
		XMFLOAT2 Flow;
		XMFLOAT2 UV;
		float Edge;
		float Rapids;    // 강: 급류 정도 0~1 (하류 경사 2 % 부터, 10 % 에서 1)
	};

	// 물보라 입자 하나 = 정점 6 개 (VS 가 카메라 쪽 사각형으로 펼침)
	struct SprayVertex
	{
		XMFLOAT3 Pos;
		XMFLOAT2 Corner;   // -1~1
		float Seed;
		float Size;
	};

	struct BodyMesh
	{
		uint64_t Hash = 0;
		ComPtr<ID3D11Buffer> VB, IB;
		UINT IndexCount = 0;
		ComPtr<ID3D11Buffer> SprayVB;
		UINT SprayVertices = 0;
	};

	std::unique_ptr<Effect> s_Effect;
	bool s_Failed = false;
	ComPtr<ID3D11InputLayout> s_OceanLayout, s_SurfaceLayout, s_SprayLayout;
	ComPtr<ID3D11Buffer> s_GridVB, s_GridIB;
	UINT s_GridIndexCount = 0;
	std::unordered_map<const WaterBody*, BodyMesh> s_Meshes;
	ComPtr<ID3D11ShaderResourceView> s_NormalA, s_NormalB, s_Foam, s_Caustics;
	// 화면 색 복사
	ComPtr<ID3D11Texture2D> s_ColorCopy;
	ComPtr<ID3D11ShaderResourceView> s_ColorCopySRV;
	// 지형 높이 지도 (바다의 얕은 물 파도 감쇠)
	ComPtr<ID3D11ShaderResourceView> s_ShoreSRV;
	ComPtr<ID3D11ShaderResourceView> s_MaskSRV;
	uint64_t s_MaskKey = 0;
	uint64_t s_ShoreHash = 0;
	XMFLOAT4 s_ShoreRect(0, 0, 0, 0);
	bool s_HasShore = false;
	int s_LastCount = 0;

	// 효과 변수에 넘길 float4 (임시 배열)
	struct F4
	{
		float V[4];
		F4(float x, float y, float z, float w) : V{ x, y, z, w } {}
		operator const float*() const { return V; }
	};

	uint64_t Mix(uint64_t h, uint64_t v) { return h ^ (v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2)); }
	uint64_t Bits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

	bool Init(ID3D11Device* device)
	{
		if (s_Effect || s_Failed)
			return s_Effect != nullptr;
		s_Effect = std::make_unique<Effect>(device, L"../Shaders/46. Water.fx");
		ID3DX11Effect* fx = s_Effect->GetFX();
		if (fx == nullptr || !fx->GetTechniqueByName("OceanTech")->IsValid())
		{
			s_Effect.reset();
			s_Failed = true;
			EditorLog::Write("Water", "46. Water.fx failed to load - water is not drawn");
			return false;
		}
		D3DX11_PASS_DESC pd;
		const D3D11_INPUT_ELEMENT_DESC ocean[] = { { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 } };
		fx->GetTechniqueByName("OceanTech")->GetPassByIndex(0)->GetDesc(&pd);
		device->CreateInputLayout(ocean, 1, pd.pIAInputSignature, pd.IAInputSignatureSize, s_OceanLayout.GetAddressOf());
		const D3D11_INPUT_ELEMENT_DESC surface[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 2, DXGI_FORMAT_R32_FLOAT, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 3, DXGI_FORMAT_R32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0 } };
		fx->GetTechniqueByName("SurfaceTech")->GetPassByIndex(0)->GetDesc(&pd);
		device->CreateInputLayout(surface, 5, pd.pIAInputSignature, pd.IAInputSignatureSize, s_SurfaceLayout.GetAddressOf());
		const D3D11_INPUT_ELEMENT_DESC spray[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 1, DXGI_FORMAT_R32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 2, DXGI_FORMAT_R32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 } };
		fx->GetTechniqueByName("SprayTech")->GetPassByIndex(0)->GetDesc(&pd);
		device->CreateInputLayout(spray, 4, pd.pIAInputSignature, pd.IAInputSignatureSize, s_SprayLayout.GetAddressOf());

		// 격자: 거친 레벨부터 (같은 그리기 안에서 뒤의 삼각형이 덮으므로 고운 레벨이 겹친 곳을 차지)
		std::vector<XMFLOAT3> verts;
		std::vector<uint32_t> idx;
		const int side = kHalf * 2 + 1;
		for (int level = kLevels - 1; level >= 0; --level)
		{
			const uint32_t base = (uint32_t)verts.size();
			for (int z = -kHalf; z <= kHalf; ++z)
				for (int x = -kHalf; x <= kHalf; ++x)
					verts.push_back(XMFLOAT3((float)x, (float)z, (float)level));
			for (int z = -kHalf; z < kHalf; ++z)
				for (int x = -kHalf; x < kHalf; ++x)
				{
					if (level > 0 && x >= -kHole && x < kHole && z >= -kHole && z < kHole)
						continue;
					const uint32_t i0 = base + (uint32_t)((z + kHalf) * side + (x + kHalf));
					const uint32_t i1 = i0 + 1, i2 = i0 + side, i3 = i2 + 1;
					// 대각선 방향을 번갈아 (물결이 한쪽으로 기운 무늬가 생기지 않게)
					if (((x + z) & 1) == 0) { idx.insert(idx.end(), { i0, i2, i1, i1, i2, i3 }); }
					else { idx.insert(idx.end(), { i0, i2, i3, i0, i3, i1 }); }
				}
		}
		D3D11_BUFFER_DESC bd = {};
		bd.Usage = D3D11_USAGE_IMMUTABLE;
		bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		bd.ByteWidth = (UINT)(verts.size() * sizeof(XMFLOAT3));
		D3D11_SUBRESOURCE_DATA init = { verts.data(), 0, 0 };
		device->CreateBuffer(&bd, &init, s_GridVB.GetAddressOf());
		bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
		bd.ByteWidth = (UINT)(idx.size() * sizeof(uint32_t));
		init.pSysMem = idx.data();
		device->CreateBuffer(&bd, &init, s_GridIB.GetAddressOf());
		s_GridIndexCount = (UINT)idx.size();

		const std::wstring dir = L"Resources\\Packages\\Water\\Textures\\";
		s_NormalA = ResourceManager::GetI()->LoadTexture(dir + L"WaterNormalA.png");
		s_NormalB = ResourceManager::GetI()->LoadTexture(dir + L"WaterNormalB.png");
		s_Foam = ResourceManager::GetI()->LoadTexture(dir + L"WaterFoam.png");
		s_Caustics = ResourceManager::GetI()->LoadTexture(dir + L"WaterCaustics.png");
		EditorLog::Write("Water", "water renderer ready: ocean grid %zu vertices, %u triangles; textures %s",
			verts.size(), s_GridIndexCount / 3, s_NormalA && s_NormalB && s_Foam && s_Caustics ? "ok" : "MISSING");
		return true;
	}

	// ---------------------------------------------------------------- 호수 윤곽 → 삼각형 (귀 자르기)
	void Triangulate(const std::vector<XMFLOAT2>& poly, std::vector<uint32_t>& out)
	{
		const int n = (int)poly.size();
		if (n < 3)
			return;
		float area = 0;
		for (int i = 0, j = n - 1; i < n; j = i++)
			area += poly[j].x * poly[i].y - poly[i].x * poly[j].y;
		std::vector<int> v(n);
		for (int i = 0; i < n; ++i)
			v[i] = area > 0 ? i : n - 1 - i;   // 반시계로
		auto cross = [&](int a, int b, int c) {
			return (poly[b].x - poly[a].x) * (poly[c].y - poly[a].y) - (poly[b].y - poly[a].y) * (poly[c].x - poly[a].x);
		};
		auto inside = [&](int p, int a, int b, int c) {
			return cross(a, b, p) >= 0 && cross(b, c, p) >= 0 && cross(c, a, p) >= 0;
		};
		int guard = 0;
		while (v.size() > 3 && guard++ < n * n)
		{
			bool cut = false;
			const int m = (int)v.size();
			for (int i = 0; i < m; ++i)
			{
				const int a = v[(i + m - 1) % m], b = v[i], c = v[(i + 1) % m];
				if (cross(a, b, c) <= 1e-6f)
					continue;
				bool ear = true;
				for (int k = 0; k < m && ear; ++k)
				{
					const int p = v[k];
					if (p != a && p != b && p != c && inside(p, a, b, c))
						ear = false;
				}
				if (!ear)
					continue;
				out.insert(out.end(), { (uint32_t)a, (uint32_t)c, (uint32_t)b });
				v.erase(v.begin() + i);
				cut = true;
				break;
			}
			if (!cut)
				break;   // 꼬인 윤곽: 남은 것은 부채꼴로
		}
		for (size_t i = 1; i + 1 < v.size(); ++i)
			out.insert(out.end(), { (uint32_t)v[0], (uint32_t)v[i + 1], (uint32_t)v[i] });
	}

	uint64_t BodyHash(const WaterBody& b)
	{
		uint64_t h = (uint64_t)b.BodyType * 977 + b.Points.size();
		XMFLOAT4X4 w;
		XMStoreFloat4x4(&w, b.WorldMatrix());
		for (int k = 0; k < 16; ++k)
			h = Mix(h, Bits((&w._11)[k]));
		for (const auto& p : b.Points)
			for (float f : { p.Position.x, p.Position.y, p.Position.z, p.Width, p.Depth, p.Speed })
				h = Mix(h, Bits(f));
		h = Mix(h, Bits(b.FlowSpeed));
		h = Mix(h, std::hash<std::string>()(b.Profile));
		return h;
	}

	BodyMesh* SurfaceMesh(ID3D11Device* device, const WaterBody& b)
	{
		BodyMesh& m = s_Meshes[&b];
		const uint64_t h = BodyHash(b);
		if (m.Hash == h && m.VB)
			return &m;
		m = BodyMesh();
		m.Hash = h;
		std::vector<SurfaceVertex> verts;
		std::vector<uint32_t> idx;
		if (b.BodyType == WaterBody::Type::Lake)
		{
			const auto curve = b.Curve(3.0f);
			const float y = b.SurfaceY();
			std::vector<XMFLOAT2> poly;
			for (const auto& c : curve)
			{
				poly.push_back(XMFLOAT2(c.Position.x, c.Position.z));
				verts.push_back({ XMFLOAT3(c.Position.x, y, c.Position.z), XMFLOAT2(0, 0), XMFLOAT2(0, 0), 1.0f, 0.0f });
			}
			Triangulate(poly, idx);
		}
		else if (b.BodyType == WaterBody::Type::River)
		{
			const auto curve = b.Curve(2.0f);
			const float flowScale = b.FlowSpeed * WaterProfiles::Get(b.Profile).FlowSpeed;
			constexpr int across = 9;
			// 급류: 앞뒤 8 m 의 하류 경사 (내려가는 비율)
			std::vector<float> rapids(curve.size(), 0.0f);
			for (size_t i = 0; i < curve.size(); ++i)
			{
				size_t a = i, e = i;
				while (a > 0 && curve[i].Distance - curve[a].Distance < 8.0f) --a;
				while (e + 1 < curve.size() && curve[e].Distance - curve[i].Distance < 8.0f) ++e;
				const float run = curve[e].Distance - curve[a].Distance;
				const float drop = curve[a].Position.y - curve[e].Position.y;
				rapids[i] = run > 0.5f ? std::clamp((drop / run - 0.02f) / 0.08f, 0.0f, 1.0f) : 0.0f;   // 2 % 부터, 10 % 면 1
			}
			for (size_t i = 0; i < curve.size(); ++i)
			{
				const auto& c = curve[i];
				const Vec3 right(c.Tangent.z, 0.0f, -c.Tangent.x);
				for (int k = 0; k < across; ++k)
				{
					const float s = (float)k / (across - 1) * 2.0f - 1.0f;
					const Vec3 p = c.Position + right * (s * c.Width * 0.5f);
					const float speed = c.Speed * flowScale * (1.0f - 0.6f * s * s) * (1.0f + 1.5f * rapids[i]);   // 급류는 빠르다
					verts.push_back({ XMFLOAT3(p.x, p.y, p.z), XMFLOAT2(c.Tangent.x * speed, c.Tangent.z * speed), XMFLOAT2((s + 1) * 0.5f, c.Distance), fabsf(s), rapids[i] });
				}
			}
			// 물보라: 급류(0.35 이상) 구간에 3 m 마다 급류 정도만큼 (씨앗 고정 → 메시가 바뀔 때만 다시)
			std::vector<SprayVertex> spray;
			uint32_t rng = 1234567u;
			auto rnd = [&]() { rng = rng * 1664525u + 1013904223u; return (rng >> 8) / 16777216.0f; };
			float nextAt = 0.0f;
			for (size_t i = 0; i < curve.size(); ++i)
			{
				const auto& c = curve[i];
				if (rapids[i] < 0.35f || c.Distance < nextAt)
					continue;
				nextAt = c.Distance + 3.0f;
				const Vec3 right(c.Tangent.z, 0.0f, -c.Tangent.x);
				const int count = 1 + (int)(rapids[i] * 4.0f);
				for (int n = 0; n < count; ++n)
				{
					const float s = rnd() * 1.6f - 0.8f;
					const Vec3 p = c.Position + right * (s * c.Width * 0.5f) + c.Tangent * (rnd() * 3.0f);
					const float seed = rnd();
					const float size = (0.8f + rnd() * 1.4f) * (0.5f + rapids[i]);
					const XMFLOAT2 corners[6] = { { -1, -1 }, { -1, 1 }, { 1, 1 }, { -1, -1 }, { 1, 1 }, { 1, -1 } };
					for (const auto& cr : corners)
						spray.push_back({ XMFLOAT3(p.x, p.y, p.z), cr, seed, size });
				}
			}
			if (!spray.empty())
			{
				D3D11_BUFFER_DESC sd = {};
				sd.Usage = D3D11_USAGE_IMMUTABLE;
				sd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
				sd.ByteWidth = (UINT)(spray.size() * sizeof(SprayVertex));
				D3D11_SUBRESOURCE_DATA si = { spray.data(), 0, 0 };
				device->CreateBuffer(&sd, &si, m.SprayVB.GetAddressOf());
				m.SprayVertices = (UINT)spray.size();
			}
			for (int i = 0; i + 1 < (int)curve.size(); ++i)
				for (int k = 0; k + 1 < across; ++k)
				{
					const uint32_t a = i * across + k, bb = a + 1, c = a + across, d = c + 1;
					idx.insert(idx.end(), { a, c, bb, bb, c, d });
				}
		}
		if (verts.empty() || idx.empty())
			return nullptr;
		D3D11_BUFFER_DESC bd = {};
		bd.Usage = D3D11_USAGE_IMMUTABLE;
		bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		bd.ByteWidth = (UINT)(verts.size() * sizeof(SurfaceVertex));
		D3D11_SUBRESOURCE_DATA init = { verts.data(), 0, 0 };
		device->CreateBuffer(&bd, &init, m.VB.GetAddressOf());
		bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
		bd.ByteWidth = (UINT)(idx.size() * sizeof(uint32_t));
		init.pSysMem = idx.data();
		device->CreateBuffer(&bd, &init, m.IB.GetAddressOf());
		m.IndexCount = (UINT)idx.size();
		return &m;
	}

	// 화면 색 복사본 (대상과 같은 크기·형식)
	ID3D11ShaderResourceView* CopySceneColor(ID3D11Device* device, ID3D11DeviceContext* dc, ID3D11RenderTargetView* target)
	{
		ComPtr<ID3D11Resource> res;
		target->GetResource(res.GetAddressOf());
		ComPtr<ID3D11Texture2D> tex;
		if (FAILED(res.As(&tex)))
			return nullptr;
		D3D11_TEXTURE2D_DESC desc;
		tex->GetDesc(&desc);
		D3D11_RENDER_TARGET_VIEW_DESC rtv;
		target->GetDesc(&rtv);
		bool remake = !s_ColorCopy;
		if (s_ColorCopy)
		{
			D3D11_TEXTURE2D_DESC cur;
			s_ColorCopy->GetDesc(&cur);
			remake = cur.Width != desc.Width || cur.Height != desc.Height || cur.Format != desc.Format;
		}
		if (remake)
		{
			s_ColorCopy.Reset();
			s_ColorCopySRV.Reset();
			D3D11_TEXTURE2D_DESC cd = desc;
			cd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			cd.MiscFlags = 0;
			cd.CPUAccessFlags = 0;
			cd.Usage = D3D11_USAGE_DEFAULT;
			cd.MipLevels = 1;
			if (FAILED(device->CreateTexture2D(&cd, nullptr, s_ColorCopy.GetAddressOf())))
				return nullptr;
			D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
			sd.Format = rtv.Format;
			sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			sd.Texture2D.MipLevels = 1;
			device->CreateShaderResourceView(s_ColorCopy.Get(), &sd, s_ColorCopySRV.GetAddressOf());
		}
		if (desc.MipLevels == 1)
			dc->CopyResource(s_ColorCopy.Get(), tex.Get());
		else
			dc->CopySubresourceRegion(s_ColorCopy.Get(), 0, 0, 0, 0, tex.Get(), 0, nullptr);
		return s_ColorCopySRV.Get();
	}

	// 지형 높이 지도 / 바다 마스크 (WaterShore) → 텍스처. 바뀔 때만 올린다
	ComPtr<ID3D11ShaderResourceView> MakeTexture(ID3D11Device* device, DXGI_FORMAT format, const void* data, UINT pitch)
	{
		D3D11_TEXTURE2D_DESC td = {};
		td.Width = td.Height = WaterShore::kRes;
		td.MipLevels = td.ArraySize = 1;
		td.Format = format;
		td.SampleDesc.Count = 1;
		td.Usage = D3D11_USAGE_IMMUTABLE;
		td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		D3D11_SUBRESOURCE_DATA init = { data, pitch, 0 };
		ComPtr<ID3D11Texture2D> tex;
		ComPtr<ID3D11ShaderResourceView> srv;
		if (SUCCEEDED(device->CreateTexture2D(&td, &init, tex.GetAddressOf())))
			device->CreateShaderResourceView(tex.Get(), nullptr, srv.GetAddressOf());
		return srv;
	}

	void UpdateShoreMap(ID3D11Device* device)
	{
		WaterShore::Update();
		s_HasShore = WaterShore::Has();
		if (!s_HasShore || s_ShoreHash == WaterShore::Revision())
			return;
		s_ShoreHash = WaterShore::Revision();
		s_ShoreSRV = MakeTexture(device, DXGI_FORMAT_R32_FLOAT, WaterShore::Heights().data(), WaterShore::kRes * sizeof(float));
		s_ShoreRect = WaterShore::Rect();
	}

	// 바다 마스크 (해수면마다, 지도가 바뀌면 다시)
	ID3D11ShaderResourceView* OceanMaskSRV(ID3D11Device* device, float seaLevel)
	{
		if (!s_HasShore)
			return nullptr;
		uint32_t bits;
		memcpy(&bits, &seaLevel, 4);
		const uint64_t key = Mix(WaterShore::Revision(), bits);
		if (key != s_MaskKey || !s_MaskSRV)
		{
			s_MaskKey = key;
			const auto& m = WaterShore::OceanMask(seaLevel);
			s_MaskSRV = m.empty() ? nullptr : MakeTexture(device, DXGI_FORMAT_R8_UNORM, m.data(), WaterShore::kRes);
		}
		return s_MaskSRV.Get();
	}

	XMFLOAT3 Lin(const float c[3]) { return XMFLOAT3(powf(c[0], 2.2f), powf(c[1], 2.2f), powf(c[2], 2.2f)); }

	void SetBody(ID3DX11Effect* fx, const WaterBody& b)
	{
		const WaterProfile& p = WaterProfiles::Get(b.Profile);
		const WaterWaves::Set& waves = b.Waves();
		XMFLOAT4 wa[WaterWaves::kMaxWaves] = {}, wb[WaterWaves::kMaxWaves] = {};
		const int count = (int)(std::min)(waves.Waves.size(), (size_t)WaterWaves::kMaxWaves);
		for (int i = 0; i < count; ++i)
		{
			const auto& w = waves.Waves[i];
			wa[i] = XMFLOAT4(w.DirX, w.DirZ, w.K, w.Amplitude);
			wb[i] = XMFLOAT4(w.Omega, w.Phase, w.Q, 0.0f);
		}
		auto var = [&](const char* n) { return fx->GetVariableByName(n); };
		var("gBodyType")->AsScalar()->SetInt((int)b.BodyType);
		var("gWaveCount")->AsScalar()->SetInt(count);
		var("gSurfaceY")->AsScalar()->SetFloat(b.SurfaceY());
		var("gBaseCell")->AsScalar()->SetFloat(kBaseCell);
		var("gWaveA")->AsVector()->SetFloatVectorArray(&wa[0].x, 0, WaterWaves::kMaxWaves);
		var("gWaveB")->AsVector()->SetFloatVectorArray(&wb[0].x, 0, WaterWaves::kMaxWaves);
		// 흡수: Clarity 를 지나면 Absorption 비율만 남는다 → σ = -ln(a) / Clarity
		const float clarity = (std::max)(0.1f, p.Clarity);
		const XMFLOAT3 sigma(-logf(std::clamp(p.Absorption[0], 0.001f, 0.999f)) / clarity, -logf(std::clamp(p.Absorption[1], 0.001f, 0.999f)) / clarity,
			-logf(std::clamp(p.Absorption[2], 0.001f, 0.999f)) / clarity);
		var("gSigma")->AsVector()->SetFloatVector(F4(sigma.x, sigma.y, sigma.z, 0));
		var("gTurbidity")->AsScalar()->SetFloat(p.Turbidity);
		const XMFLOAT3 sc = Lin(p.ScatterColor), ss = Lin(p.SubsurfaceColor);
		var("gScatter")->AsVector()->SetFloatVector(F4(sc.x, sc.y, sc.z, 0));
		var("gClarity")->AsScalar()->SetFloat(clarity);
		var("gSSSColor")->AsVector()->SetFloatVector(F4(ss.x, ss.y, ss.z, 0));
		var("gSSS")->AsScalar()->SetFloat(p.Subsurface);
		var("gNormalParams")->AsVector()->SetFloatVector(F4(p.NormalStrength, p.NormalTiling, p.NormalSpeed, 0));
		const float wind = XMConvertToRadians(p.WindDirection);
		var("gWindDir")->AsVector()->SetFloatVector(F4(cosf(wind), sinf(wind), 0, 0));
		var("gMaxAmp")->AsScalar()->SetFloat((std::max)(0.05f, waves.MaxAmplitude * 0.6f));
		var("gHasShoreMap")->AsScalar()->SetFloat(b.BodyType == WaterBody::Type::Ocean && s_HasShore ? 1.0f : 0.0f);
		var("gFoamParams")->AsVector()->SetFloatVector(F4(p.FoamAmount, p.ShoreFoam, p.FoamTiling, 0));
		var("gLightParams")->AsVector()->SetFloatVector(F4(p.Smoothness, p.Reflection, p.Refraction, 0));
		var("gCausticParams")->AsVector()->SetFloatVector(F4(p.Caustics, p.CausticsDepth, p.CausticsTiling, 0));
		// 물가 파도: 높이, 마루 사이 수심(m), 위상 속도(rad/s)
		var("gShoreWave")->AsVector()->SetFloatVector(F4(b.BodyType == WaterBody::Type::Ocean ? p.ShoreWaveHeight * b.WaveScale : 0.0f, 1.6f, 1.3f, 0));
		var("gShoreRect")->AsVector()->SetFloatVector(&s_ShoreRect.x);
		ID3D11ShaderResourceView* mask = b.BodyType == WaterBody::Type::Ocean ? OceanMaskSRV(Application::GetI()->GetDevice(), b.SurfaceY()) : nullptr;
		var("gOceanMask")->AsShaderResource()->SetResource(mask);
		var("gHasOceanMask")->AsScalar()->SetFloat(mask ? 1.0f : 0.0f);
	}
}

namespace WaterRenderer
{
	int LastDrawCount() { return s_LastCount; }

	void Draw(const View& v)
	{
		s_LastCount = 0;
		std::vector<const WaterBody*> bodies;
		for (const WaterBody* b : WaterBody::All())
			if (b->IsActiveBody())
				bodies.push_back(b);
		// 지워진 바디의 메시 정리
		for (auto it = s_Meshes.begin(); it != s_Meshes.end();)
			it = std::find(WaterBody::All().begin(), WaterBody::All().end(), it->first) == WaterBody::All().end() ? s_Meshes.erase(it) : std::next(it);
		if (bodies.empty() || v.Context == nullptr || v.Target == nullptr || v.DepthSRV == nullptr || v.DepthReadOnly == nullptr)
			return;
		ID3D11Device* device = Application::GetI()->GetDevice();
		if (!Init(device))
			return;
		ID3D11DeviceContext* dc = v.Context;
		ID3DX11Effect* fx = s_Effect->GetFX();

		// 상태 보관
		ComPtr<ID3D11BlendState> prevBlend;
		float prevFactor[4];
		UINT prevMask = 0;
		dc->OMGetBlendState(prevBlend.GetAddressOf(), prevFactor, &prevMask);
		ComPtr<ID3D11DepthStencilState> prevDSS;
		UINT prevRef = 0;
		dc->OMGetDepthStencilState(prevDSS.GetAddressOf(), &prevRef);
		ComPtr<ID3D11RasterizerState> prevRS;
		dc->RSGetState(prevRS.GetAddressOf());

		// 1) 화면 색 복사 (묶인 대상은 잠시 풀고)
		ID3D11RenderTargetView* nullRTV = nullptr;
		dc->OMSetRenderTargets(1, &nullRTV, nullptr);
		ID3D11ShaderResourceView* colorSRV = CopySceneColor(device, dc, v.Target);
		UpdateShoreMap(device);
		if (colorSRV == nullptr)
		{
			dc->OMSetRenderTargets(1, &v.Target, v.Depth);
			return;
		}

		// 프레임 값
		const XMMATRIX viewProj = v.ViewMatrix * v.Proj;
		XMFLOAT4X4 proj;
		XMStoreFloat4x4(&proj, v.Proj);
		auto var = [&](const char* n) { return fx->GetVariableByName(n); };
		var("gViewProj")->AsMatrix()->SetMatrix(reinterpret_cast<const float*>(&viewProj));
		var("gEyePos")->AsVector()->SetFloatVector(F4(v.Eye.x, v.Eye.y, v.Eye.z, 0));
		var("gTime")->AsScalar()->SetFloat(WaterWaves::Time());
		var("gProjParams")->AsVector()->SetFloatVector(F4(proj._33, proj._43, 0, 0));
		var("gViewport")->AsVector()->SetFloatVector(F4(v.Viewport.TopLeftX, v.Viewport.TopLeftY, v.Viewport.Width, v.Viewport.Height));
		AtmospherePass::Bind(fx, v.Atmosphere ? *v.Atmosphere : AtmospherePass::Params(), v.Eye, v.Sky);   // 안개·대기 (없으면 끔)
		XMFLOAT3 sunDir(0.3f, 0.8f, 0.4f), sunColor(1.0f, 0.95f, 0.85f), ambient(0.18f, 0.22f, 0.28f);
		if (v.Sun)
		{
			XMStoreFloat3(&sunDir, XMVector3Normalize(-XMLoadFloat3(&v.Sun->Direction)));
			sunColor = XMFLOAT3(powf((std::max)(0.0f, v.Sun->Diffuse.x), 2.2f), powf((std::max)(0.0f, v.Sun->Diffuse.y), 2.2f), powf((std::max)(0.0f, v.Sun->Diffuse.z), 2.2f));
			ambient = XMFLOAT3(powf((std::max)(0.0f, v.Sun->Ambient.x), 2.2f) + 0.05f, powf((std::max)(0.0f, v.Sun->Ambient.y), 2.2f) + 0.06f, powf((std::max)(0.0f, v.Sun->Ambient.z), 2.2f) + 0.08f);
		}
		var("gSunDir")->AsVector()->SetFloatVector(F4(sunDir.x, sunDir.y, sunDir.z, 0));
		var("gSunIntensity")->AsScalar()->SetFloat(1.0f);
		var("gSunColor")->AsVector()->SetFloatVector(F4(sunColor.x, sunColor.y, sunColor.z, 0));
		var("gAmbient")->AsVector()->SetFloatVector(F4(ambient.x, ambient.y, ambient.z, 0));
		float mips = 1.0f;
		if (v.Sky)
		{
			D3D11_SHADER_RESOURCE_VIEW_DESC sd;
			v.Sky->GetDesc(&sd);
			mips = (float)(sd.ViewDimension == D3D11_SRV_DIMENSION_TEXTURECUBE ? sd.TextureCube.MipLevels : 1);
		}
		var("gCubeMips")->AsScalar()->SetFloat(mips);
		var("gHasSky")->AsScalar()->SetFloat(v.Sky ? 1.0f : 0.0f);
		const XMMATRIX invViewProj = XMMatrixInverse(nullptr, viewProj);
		var("gInvViewProj")->AsMatrix()->SetMatrix(reinterpret_cast<const float*>(&invViewProj));
		// 카메라가 어느 물 아래인가 (바다·호수 = 수면 아래, 강 = 그 위치 수면 아래)
		const WaterBody* under = nullptr;
		{
			const Vec3 eye(v.Eye.x, v.Eye.y, v.Eye.z);
			float best = FLT_MAX;
			for (const WaterBody* b : bodies)
			{
				float h;
				if (b->Sample(eye, h) && eye.y < h - 0.05f && h - eye.y < best)
				{
					best = h - eye.y;
					under = b;
				}
			}
		}
		var("gSceneColor")->AsShaderResource()->SetResource(colorSRV);
		var("gSceneDepth")->AsShaderResource()->SetResource(v.DepthSRV);
		var("gSky")->AsShaderResource()->SetResource(v.Sky);
		var("gNormalA")->AsShaderResource()->SetResource(s_NormalA.Get());
		var("gNormalB")->AsShaderResource()->SetResource(s_NormalB.Get());
		var("gFoamTex")->AsShaderResource()->SetResource(s_Foam.Get());
		var("gCausticsTex")->AsShaderResource()->SetResource(s_Caustics.Get());
		var("gShoreMap")->AsShaderResource()->SetResource(s_ShoreSRV.Get());
		// 해 그림자
		var("gSunShadow")->AsShaderResource()->SetResource(v.SunShadow);
		var("gHasSunShadow")->AsScalar()->SetFloat(v.SunShadow && v.SunShadowData.x > 0.0f ? 1.0f : 0.0f);
		var("gSunShadowTransforms")->AsMatrix()->SetMatrixArray(reinterpret_cast<const float*>(v.SunShadowTransforms), 0, 4);
		var("gCascadeSpheres")->AsVector()->SetFloatVectorArray(&v.CascadeSpheres[0].x, 0, 4);
		var("gShadowParams")->AsVector()->SetFloatVector(&v.ShadowParams.x);
		var("gSunShadowData")->AsVector()->SetFloatVector(&v.SunShadowData.x);

		// 2) 물 (깊이 읽기 전용)
		dc->OMSetRenderTargets(1, &v.Target, v.DepthReadOnly);
		dc->RSSetViewports(1, &v.Viewport);
		dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		// 수중: 먼저 화면 전체에 물속 흡수·산란 (수면은 그 위에 아랫면으로)
		if (under)
		{
			SetBody(fx, *under);
			var("gEyeUnder")->AsScalar()->SetFloat(1.0f);
			dc->IASetInputLayout(nullptr);
			ID3D11Buffer* nullVB = nullptr;
			UINT zero = 0;
			dc->IASetVertexBuffers(0, 1, &nullVB, &zero, &zero);
			fx->GetTechniqueByName("UnderwaterTech")->GetPassByIndex(0)->Apply(0, dc);
			dc->Draw(3, 0);
		}
		auto drawBody = [&](const WaterBody* b, bool depthOnly) {
			const bool ocean = b->BodyType == WaterBody::Type::Ocean;
			BodyMesh* mesh = ocean ? nullptr : SurfaceMesh(device, *b);
			if (!ocean && mesh == nullptr)
				return false;
			SetBody(fx, *b);
			var("gEyeUnder")->AsScalar()->SetFloat(b == under ? 1.0f : 0.0f);
			const char* tech = ocean ? (depthOnly ? "OceanDepthTech" : "OceanTech") : (depthOnly ? "SurfaceDepthTech" : "SurfaceTech");
			UINT stride = ocean ? sizeof(XMFLOAT3) : sizeof(SurfaceVertex), offset = 0;
			dc->IASetInputLayout(ocean ? s_OceanLayout.Get() : s_SurfaceLayout.Get());
			ID3D11Buffer* vb = ocean ? s_GridVB.Get() : mesh->VB.Get();
			dc->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
			dc->IASetIndexBuffer(ocean ? s_GridIB.Get() : mesh->IB.Get(), DXGI_FORMAT_R32_UINT, 0);
			fx->GetTechniqueByName(tech)->GetPassByIndex(0)->Apply(0, dc);
			dc->DrawIndexed(ocean ? s_GridIndexCount : mesh->IndexCount, 0, 0);
			return true;
		};
		for (const WaterBody* b : bodies)
			s_LastCount += drawBody(b, false) ? 1 : 0;

		// 3) 물 깊이 쓰기 (깊이 SRV 를 풀고 쓰기 DSV 로)
		var("gSceneDepth")->AsShaderResource()->SetResource(nullptr);
		var("gSceneColor")->AsShaderResource()->SetResource(nullptr);
		ID3D11ShaderResourceView* nullSRV[16] = {};
		dc->PSSetShaderResources(0, 16, nullSRV);
		dc->VSSetShaderResources(0, 16, nullSRV);
		dc->OMSetRenderTargets(1, &v.Target, v.Depth);
		for (const WaterBody* b : bodies)
			drawBody(b, true);

		// 4) 강 급류 물보라 (반투명, 깊이 검사만)
		for (const WaterBody* b : bodies)
		{
			if (b->BodyType != WaterBody::Type::River)
				continue;
			auto it = s_Meshes.find(b);
			if (it == s_Meshes.end() || !it->second.SprayVB)
				continue;
			SetBody(fx, *b);
			UINT stride = sizeof(SprayVertex), offset = 0;
			dc->IASetInputLayout(s_SprayLayout.Get());
			ID3D11Buffer* vb = it->second.SprayVB.Get();
			dc->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
			fx->GetTechniqueByName("SprayTech")->GetPassByIndex(0)->Apply(0, dc);
			dc->Draw(it->second.SprayVertices, 0);
		}

		dc->VSSetShaderResources(0, 16, nullSRV);
		dc->OMSetBlendState(prevBlend.Get(), prevFactor, prevMask);
		dc->OMSetDepthStencilState(prevDSS.Get(), prevRef);
		dc->RSSetState(prevRS.Get());
		dc->IASetInputLayout(nullptr);
	}
}
