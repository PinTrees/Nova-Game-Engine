#include "pch.h"
#include "RenderLayers.h"
#include "SpriteBatch.h"
#include "Effects.h"
#include "TagsAndLayers.h"
#include "Light2D.h"

struct SpriteBatchVertex { float X, Y, Z, U, V; uint32 Color; };   // SpriteBatch::Vertex 와 같은 배치

namespace
{
	std::vector<SpriteSource*> s_Sources;
	std::unique_ptr<Effect> s_Effect;
	bool s_Failed = false;
	ComPtr<GfxInputLayout> s_Layout;
	ComPtr<GfxBuffer> s_VB;
	UINT s_VBCapacity = 0;
	ComPtr<GfxShaderResourceView> s_White;
	int s_LastDrawCalls = 0, s_LastSprites = 0;
	uint32 s_Seq = 0;

	// ---- 2D 빛 (Light2D.h): 빛 목록 · 그림자 텍스처 (빛마다 한 채널 — 0 ~ 3 = [0], 4 ~ 7 = [1])
	constexpr int kMaxLights2D = 32;
	constexpr int kMaxShadowLights2D = 8;
	struct Light2DData { Vec4 PosRadius, Color, Dir, Extra; Light2D* Src; };
	ComPtr<GfxTexture2D> s_ShadowTex[2];
	ComPtr<GfxRenderTargetView> s_ShadowRTV[2];
	ComPtr<GfxShaderResourceView> s_ShadowSRV[2];
	UINT s_ShadowW = 0, s_ShadowH = 0;
	ComPtr<GfxShaderResourceView> s_Black;
	ComPtr<GfxBuffer> s_ShadowVB;
	UINT s_ShadowVBCapacity = 0;

	GfxShaderResourceView* BlackTexture()
	{
		if (s_Black == nullptr)
		{
			const uint32 black = 0;
			D3D11_TEXTURE2D_DESC td = {};
			td.Width = td.Height = 1;
			td.MipLevels = td.ArraySize = 1;
			td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			td.SampleDesc.Count = 1;
			td.Usage = D3D11_USAGE_IMMUTABLE;
			td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			D3D11_SUBRESOURCE_DATA data = { &black, 4, 0 };
			ComPtr<GfxTexture2D> tex;
			auto device = Application::GetI()->GetDevice();
			if (SUCCEEDED(device->CreateTexture2D(&td, &data, tex.GetAddressOf())))
				device->CreateShaderResourceView(tex.Get(), nullptr, s_Black.GetAddressOf());
		}
		return s_Black.Get();
	}

	bool EnsureShadowTargets(UINT w, UINT h)
	{
		if (s_ShadowTex[0] && s_ShadowW == w && s_ShadowH == h)
			return true;
		auto device = Application::GetI()->GetDevice();
		for (int i = 0; i < 2; ++i)
		{
			s_ShadowSRV[i].Reset();
			s_ShadowRTV[i].Reset();
			s_ShadowTex[i].Reset();
			D3D11_TEXTURE2D_DESC td = {};
			td.Width = w;
			td.Height = h;
			td.MipLevels = td.ArraySize = 1;
			td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			td.SampleDesc.Count = 1;
			td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
			if (FAILED(device->CreateTexture2D(&td, nullptr, s_ShadowTex[i].GetAddressOf())) ||
				FAILED(device->CreateRenderTargetView(s_ShadowTex[i].Get(), nullptr, s_ShadowRTV[i].GetAddressOf())) ||
				FAILED(device->CreateShaderResourceView(s_ShadowTex[i].Get(), nullptr, s_ShadowSRV[i].GetAddressOf())))
			{
				s_ShadowTex[i].Reset();
				s_ShadowW = s_ShadowH = 0;
				return false;
			}
		}
		s_ShadowW = w;
		s_ShadowH = h;
		return true;
	}

	bool PointInLoop(const std::vector<Vec2>& loop, const Vec2& p)
	{
		bool inside = false;
		for (size_t i = 0, j = loop.size() - 1; i < loop.size(); j = i++)
		{
			const Vec2& a = loop[i];
			const Vec2& b = loop[j];
			if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x)
				inside = !inside;
		}
		return inside;
	}

	// 그림자 모양: 빛을 등진 모서리를 빛 반대쪽으로 길게 민 사각형 (자기 그림자면 모양 안쪽도). 정점 색 a = 가리는 정도
	void BuildShadowGeometry(const Light2DData& L, std::vector<SpriteBatchVertex>& out)
	{
		const Vec2 lp(L.PosRadius.x, L.PosRadius.y);
		const float radius = L.PosRadius.w;
		const float reach = radius * 2.0f + 1.0f;
		const uint32 c = (uint32)(std::clamp(L.Src->ShadowStrength, 0.0f, 1.0f) * 255.0f + 0.5f) << 24;
		std::vector<std::vector<Vec2>> loops;
		for (ShadowCaster2D* sc : ShadowCaster2D::All())
		{
			if (!sc->ActiveAndEnabled())
				continue;
			float z = 0.0f;
			if (!sc->WorldOutline(loops, z))
				continue;
			for (const auto& loop : loops)
			{
				// 빛이 닿지 않는 것 · 빛이 안에 있는 것은 건너뛴다
				Vec2 bmin(FLT_MAX, FLT_MAX), bmax(-FLT_MAX, -FLT_MAX);
				for (const Vec2& p : loop) { bmin.x = (std::min)(bmin.x, p.x); bmin.y = (std::min)(bmin.y, p.y); bmax.x = (std::max)(bmax.x, p.x); bmax.y = (std::max)(bmax.y, p.y); }
				const float cx = std::clamp(lp.x, bmin.x, bmax.x), cy = std::clamp(lp.y, bmin.y, bmax.y);
				if ((cx - lp.x) * (cx - lp.x) + (cy - lp.y) * (cy - lp.y) > radius * radius || PointInLoop(loop, lp))
					continue;
				auto push = [&](const Vec2& a, const Vec2& b, const Vec2& d) {
					out.push_back({ a.x, a.y, z, 0, 0, c });
					out.push_back({ b.x, b.y, z, 0, 0, c });
					out.push_back({ d.x, d.y, z, 0, 0, c });
				};
				for (size_t i = 0; i < loop.size(); ++i)
				{
					const Vec2& a = loop[i];
					const Vec2& b = loop[(i + 1) % loop.size()];
					const Vec2 edge = b - a;
					const Vec2 outward(edge.y, -edge.x);   // 반시계 고리의 바깥쪽
					const Vec2 mid = (a + b) * 0.5f;
					if (outward.Dot(mid - lp) <= 0.0f)
						continue;   // 빛을 향한 모서리
					Vec2 da = a - lp, db = b - lp;
					da.Normalize();
					db.Normalize();
					const Vec2 fa = a + da * reach, fb = b + db * reach;
					push(a, b, fb);
					push(a, fb, fa);
				}
				if (sc->SelfShadows)
					for (size_t i = 1; i + 1 < loop.size(); ++i)
						push(loop[0], loop[i], loop[i + 1]);
			}
		}
	}

	bool Init()
	{
		if (s_Effect || s_Failed)
			return s_Effect != nullptr;
		auto device = Application::GetI()->GetDevice();
		s_Effect = std::make_unique<Effect>(device, L"../Shaders/51. Sprite.fx");
		if (s_Effect->GetFX() == nullptr)
		{
			s_Effect.reset();
			s_Failed = true;
			EditorLog::Write("Sprites", "51. Sprite.fx failed to load - sprites are not drawn");
			return false;
		}
		D3DX11_PASS_DESC pass = {};
		s_Effect->GetFX()->GetTechniqueByName("SpriteTech")->GetPassByIndex(0)->GetDesc(&pass);
		const D3D11_INPUT_ELEMENT_DESC desc[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};
		if (FAILED(device->CreateInputLayout(desc, 3, pass.pIAInputSignature, pass.IAInputSignatureSize, s_Layout.GetAddressOf())))
		{
			s_Effect.reset();
			s_Failed = true;
			EditorLog::Write("Sprites", "sprite input layout failed");
			return false;
		}
		return true;
	}
}

// 렌더러 목록 접근 (SpriteBatch 의 비공개 자료를 Render 에서 쓴다)
struct SpriteBatchAccess
{
	static SpriteBatch& Frame() { static SpriteBatch b; return b; }
};

// ------------------------------------------------------------------ SpriteSource
SpriteSource::SpriteSource()
{
	s_Sources.push_back(this);
}

SpriteSource::~SpriteSource()
{
	s_Sources.erase(std::remove(s_Sources.begin(), s_Sources.end(), this), s_Sources.end());
}

const std::vector<SpriteSource*>& SpriteSource::All()
{
	return s_Sources;
}

bool SpriteSource::ActiveInHierarchy() const
{
	GameObject* owner = SpriteOwner();
	if (owner == nullptr || !SpriteEnabled())
		return false;
	for (GameObject* g = owner; g != nullptr; g = g->GetParent())
		if (!g->IsActive())
			return false;
	return true;
}

// ------------------------------------------------------------------ SpriteBatch
uint32 SpriteBatch::PackColor(const float rgba[4])
{
	auto b = [](float v) { return (uint32)(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
	return b(rgba[0]) | (b(rgba[1]) << 8) | (b(rgba[2]) << 16) | (b(rgba[3]) << 24);
}

GfxShaderResourceView* SpriteBatch::WhiteTexture()
{
	if (s_White == nullptr)
	{
		const uint32 white = 0xFFFFFFFF;
		D3D11_TEXTURE2D_DESC td = {};
		td.Width = td.Height = 1;
		td.MipLevels = td.ArraySize = 1;
		td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		td.SampleDesc.Count = 1;
		td.Usage = D3D11_USAGE_IMMUTABLE;
		td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		D3D11_SUBRESOURCE_DATA data = { &white, 4, 0 };
		ComPtr<GfxTexture2D> tex;
		auto device = Application::GetI()->GetDevice();
		if (SUCCEEDED(device->CreateTexture2D(&td, &data, tex.GetAddressOf())))
			device->CreateShaderResourceView(tex.Get(), nullptr, s_White.GetAddressOf());
	}
	return s_White.Get();
}

void SpriteBatch::Begin(int sortingLayerId, int sortingOrder, const Vec3& pivotWorld)
{
	const Vec3 v = Vec3::Transform(pivotWorld, m_View);
	m_Groups.push_back({ TagsAndLayers::SortingLayerIndex(sortingLayerId), sortingOrder, v.z, s_Seq++, (uint32)m_Tris.size(), 0 });
	m_Normal = nullptr;
}

void SpriteBatch::Quad(const Vec3 p[4], const Vec2 uv[4], uint32 color, GfxShaderResourceView* texture, bool point)
{
	if ((color >> 24) == 0)
		return;
	const uint32 c[3] = { color, color, color };
	const Vec3 a[3] = { p[0], p[1], p[2] }, b[3] = { p[0], p[2], p[3] };
	const Vec2 ua[3] = { uv[0], uv[1], uv[2] }, ub[3] = { uv[0], uv[2], uv[3] };
	Triangle(a, ua, c, texture, point);
	Triangle(b, ub, c, texture, point);
}

void SpriteBatch::Triangle(const Vec3 p[3], const Vec2 uv[3], const uint32 color[3], GfxShaderResourceView* texture, bool point)
{
	if (m_Groups.empty())
		Begin(0, 0, p[0]);
	for (int i = 0; i < 3; ++i)
		m_Vertices.push_back({ p[i].x, p[i].y, p[i].z, uv[i].x, uv[i].y, color[i] });
	m_Tris.push_back({ texture ? texture : WhiteTexture(), point, m_Normal });
	++m_Groups.back().Count;
}

int SpriteBatch::LastDrawCalls() { return s_LastDrawCalls; }
int SpriteBatch::LastSpriteCount() { return s_LastSprites; }

void SpriteBatch::Render(const Matrix& view, const Matrix& proj, GfxRenderTargetView* rtv, GfxDepthStencilView* dsv)
{
	s_LastDrawCalls = 0;
	s_LastSprites = 0;
	if (s_Sources.empty() || rtv == nullptr)
		return;
	SpriteBatch& b = SpriteBatchAccess::Frame();
	b.m_Vertices.clear();
	b.m_Tris.clear();
	b.m_Groups.clear();
	b.m_View = view;
	s_Seq = 0;
	// 그리는 중에 목록이 바뀌지 않게 복사본
	const std::vector<SpriteSource*> sources = s_Sources;
	for (SpriteSource* s : sources)
		if (s->ActiveInHierarchy() && RenderLayers::Visible(s->SpriteOwner()))   // Camera 의 Culling Mask
			s->CollectSprites(b);
	if (b.m_Tris.empty() || !Init())
		return;
	s_LastSprites = (int)b.m_Groups.size();

	// 2D 빛: 켜진 Light 2D 가 하나라도 있으면 Lit (Global 은 앞에, 그림자 빛은 8 개까지 채널을 받는다)
	std::vector<Light2DData> lights;
	int shadowLights = 0;
	for (Light2D* l : Light2D::All())
	{
		if ((int)lights.size() >= kMaxLights2D || !l->ActiveAndEnabled() || !RenderLayers::Visible(l->GetGameObject()))
			continue;
		const Matrix m = l->GetGameObject()->GetTransform()->GetWorldMatrix();
		const Vec3 pos = m.Translation();
		Vec3 up = Vec3::TransformNormal(Vec3(0, 1, 0), m);
		Vec2 dir(up.x, up.y);
		if (dir.LengthSquared() < 1e-8f) dir = Vec2(0, 1);
		dir.Normalize();
		Light2DData d;
		d.Src = l;
		const bool global = l->LightType == Light2D::Type::Global;
		d.PosRadius = Vec4(pos.x, pos.y, global ? 0.0f : l->InnerRadius, global ? -1.0f : (std::max)(0.01f, l->OuterRadius));
		d.Color = Vec4(l->Color[0] * l->Intensity, l->Color[1] * l->Intensity, l->Color[2] * l->Intensity, std::clamp(l->Falloff, 0.0f, 1.0f));
		const float outer = std::clamp(l->OuterAngle, 0.0f, 360.0f), inner = std::clamp(l->InnerAngle, 0.0f, outer);
		d.Dir = outer >= 359.9f ? Vec4(dir.x, dir.y, 1.0f, -2.0f)
		                        : Vec4(dir.x, dir.y, cosf(XMConvertToRadians(inner * 0.5f)), cosf(XMConvertToRadians(outer * 0.5f)));
		int channel = -1;
		if (!global && l->Shadows && shadowLights < kMaxShadowLights2D)
			channel = shadowLights++;
		d.Extra = Vec4(l->NormalMapDistance, (float)channel, 0.0f, 0.0f);
		if (global) lights.insert(lights.begin(), d); else lights.push_back(d);
	}
	const bool lit = !lights.empty();

	// Sorting Layer → Order in Layer → 먼 것부터 → 넣은 순서
	std::vector<Group> groups = b.m_Groups;
	std::sort(groups.begin(), groups.end(), [](const Group& x, const Group& y) {
		if (x.Layer != y.Layer) return x.Layer < y.Layer;
		if (x.Order != y.Order) return x.Order < y.Order;
		if (fabsf(x.Depth - y.Depth) > 1e-5f) return x.Depth > y.Depth;
		return x.Seq < y.Seq;
	});
	// 정렬한 순서로 정점을 다시 쌓고, 같은 텍스처 · 필터가 이어지면 한 번에
	struct Draw { GfxShaderResourceView* Texture; bool Point; GfxShaderResourceView* Normal; UINT Start, Count; };
	std::vector<Vertex> verts;
	verts.reserve(b.m_Vertices.size());
	std::vector<Draw> draws;
	for (const Group& g : groups)
		for (uint32 t = g.First; t < g.First + g.Count; ++t)
		{
			const Tri& tri = b.m_Tris[t];
			GfxShaderResourceView* normal = lit ? tri.Normal : nullptr;
			if (draws.empty() || draws.back().Texture != tri.Texture || draws.back().Point != tri.Point || draws.back().Normal != normal)
				draws.push_back({ tri.Texture, tri.Point, normal, (UINT)verts.size(), 0 });
			for (int k = 0; k < 3; ++k)
				verts.push_back(b.m_Vertices[(size_t)t * 3 + k]);
			draws.back().Count += 3;
		}

	auto device = Application::GetI()->GetDevice();
	auto ctx = Application::GetI()->GetDeviceContext();
	if (verts.size() > s_VBCapacity || !s_VB)
	{
		s_VBCapacity = (std::max)((UINT)verts.size(), (std::max)(s_VBCapacity * 2u, 1536u));
		D3D11_BUFFER_DESC bd = {};
		bd.ByteWidth = s_VBCapacity * sizeof(Vertex);
		bd.Usage = D3D11_USAGE_DYNAMIC;
		bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		s_VB.Reset();
		if (FAILED(device->CreateBuffer(&bd, nullptr, s_VB.GetAddressOf())))
		{
			s_VBCapacity = 0;
			return;
		}
	}
	D3D11_MAPPED_SUBRESOURCE mapped;
	if (FAILED(ctx->Map(s_VB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
		return;
	memcpy(mapped.pData, verts.data(), verts.size() * sizeof(Vertex));
	ctx->Unmap(s_VB.Get(), 0);

	FxEffect* fx = s_Effect->GetFX();
	XMFLOAT4X4 m;
	XMStoreFloat4x4(&m, view * proj);
	fx->GetVariableByName("gViewProj")->AsMatrix()->SetMatrix(&m._11);

	// 그림자: 빛마다 가림 모양을 화면 크기 텍스처의 한 채널에 (MAX)
	GfxShaderResourceView* shadowSRV[2] = { BlackTexture(), BlackTexture() };
	if (lit && shadowLights > 0)
	{
		D3D11_VIEWPORT vp = {};
		UINT vpCount = 1;
		ctx->RSGetViewports(&vpCount, &vp);
		const UINT w = (UINT)(std::max)(1.0f, vp.Width), h = (UINT)(std::max)(1.0f, vp.Height);
		std::vector<SpriteBatchVertex> sv;
		std::vector<std::pair<int, std::pair<UINT, UINT>>> ranges;   // 채널, (처음, 개수)
		for (const Light2DData& L : lights)
		{
			if (L.Extra.y < 0.0f)
				continue;
			const UINT first = (UINT)sv.size();
			BuildShadowGeometry(L, sv);
			if (sv.size() > first)
				ranges.push_back({ (int)L.Extra.y, { first, (UINT)sv.size() - first } });
		}
		if (!ranges.empty() && EnsureShadowTargets(w, h))
		{
			if (sv.size() > s_ShadowVBCapacity || !s_ShadowVB)
			{
				s_ShadowVBCapacity = (std::max)((UINT)sv.size(), (std::max)(s_ShadowVBCapacity * 2u, 768u));
				D3D11_BUFFER_DESC bd = {};
				bd.ByteWidth = s_ShadowVBCapacity * sizeof(SpriteBatchVertex);
				bd.Usage = D3D11_USAGE_DYNAMIC;
				bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
				bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
				s_ShadowVB.Reset();
				if (FAILED(device->CreateBuffer(&bd, nullptr, s_ShadowVB.GetAddressOf())))
					s_ShadowVBCapacity = 0;
			}
			D3D11_MAPPED_SUBRESOURCE sm;
			if (s_ShadowVB && SUCCEEDED(ctx->Map(s_ShadowVB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &sm)))
			{
				memcpy(sm.pData, sv.data(), sv.size() * sizeof(SpriteBatchVertex));
				ctx->Unmap(s_ShadowVB.Get(), 0);
				const float zero[4] = { 0, 0, 0, 0 };
				ctx->ClearRenderTargetView(s_ShadowRTV[0].Get(), zero);
				ctx->ClearRenderTargetView(s_ShadowRTV[1].Get(), zero);
				ctx->IASetInputLayout(s_Layout.Get());
				ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				const UINT sstride = sizeof(SpriteBatchVertex), soffset = 0;
				GfxBuffer* svb = s_ShadowVB.Get();
				ctx->IASetVertexBuffers(0, 1, &svb, &sstride, &soffset);
				static const char* kTech[4] = { "ShadowRTech", "ShadowGTech", "ShadowBTech", "ShadowATech" };
				for (int target = 0; target < 2; ++target)
				{
					GfxRenderTargetView* srtv[1] = { s_ShadowRTV[target].Get() };
					ctx->OMSetRenderTargets(1, srtv, nullptr);
					for (const auto& r : ranges)
						if (r.first / 4 == target)
						{
							fx->GetTechniqueByName(kTech[r.first % 4])->GetPassByIndex(0)->Apply(0, ctx);
							ctx->Draw(r.second.second, r.second.first);
							++s_LastDrawCalls;
						}
				}
				shadowSRV[0] = s_ShadowSRV[0].Get();
				shadowSRV[1] = s_ShadowSRV[1].Get();
			}
		}
	}

	GfxRenderTargetView* rtvs[1] = { rtv };
	ctx->OMSetRenderTargets(1, rtvs, dsv);
	ctx->IASetInputLayout(s_Layout.Get());
	ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	const UINT stride = sizeof(Vertex), offset = 0;
	GfxBuffer* vb = s_VB.Get();
	ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);

	FxVar* texVar = fx->GetVariableByName("gTexture")->AsShaderResource();
	FxVar* srgbVar = fx->GetVariableByName("gSrgb")->AsVector();
	auto isSrgb = [](GfxShaderResourceView* srv) {
		D3D11_SHADER_RESOURCE_VIEW_DESC d = {};
		srv->GetDesc(&d);
		switch (d.Format)
		{
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
		case DXGI_FORMAT_BC1_UNORM_SRGB: case DXGI_FORMAT_BC2_UNORM_SRGB: case DXGI_FORMAT_BC3_UNORM_SRGB: case DXGI_FORMAT_BC7_UNORM_SRGB:
			return true;
		default:
			return false;
		}
	};
	FxPass* linear = fx->GetTechniqueByName(lit ? "SpriteLitTech" : "SpriteTech")->GetPassByIndex(0);
	FxPass* point = fx->GetTechniqueByName(lit ? "SpriteLitPointTech" : "SpritePointTech")->GetPassByIndex(0);
	FxVar* normalVar = fx->GetVariableByName("gNormalMap")->AsShaderResource();
	FxVar* infoVar = fx->GetVariableByName("gLight2DInfo")->AsVector();
	if (lit)
	{
		D3D11_VIEWPORT vp = {};
		UINT vpCount = 1;
		ctx->RSGetViewports(&vpCount, &vp);
		const XMFLOAT4 screen(1.0f / (std::max)(1.0f, vp.Width), 1.0f / (std::max)(1.0f, vp.Height), (float)shadowLights, 0.0f);
		fx->GetVariableByName("gScreen")->AsVector()->SetFloatVector(&screen.x);
		Vec4 posRadius[kMaxLights2D], color[kMaxLights2D], dirs[kMaxLights2D], extra[kMaxLights2D];
		for (size_t i = 0; i < lights.size(); ++i)
		{
			posRadius[i] = lights[i].PosRadius;
			color[i] = lights[i].Color;
			dirs[i] = lights[i].Dir;
			extra[i] = lights[i].Extra;
		}
		const UINT n = (UINT)lights.size();
		fx->GetVariableByName("gLightPosRadius")->AsVector()->SetFloatVectorArray(&posRadius[0].x, 0, n);
		fx->GetVariableByName("gLightColor")->AsVector()->SetFloatVectorArray(&color[0].x, 0, n);
		fx->GetVariableByName("gLightDir")->AsVector()->SetFloatVectorArray(&dirs[0].x, 0, n);
		fx->GetVariableByName("gLightExtra")->AsVector()->SetFloatVectorArray(&extra[0].x, 0, n);
		fx->GetVariableByName("gShadow0")->AsShaderResource()->SetResource(shadowSRV[0]);
		fx->GetVariableByName("gShadow1")->AsShaderResource()->SetResource(shadowSRV[1]);
	}
	for (const Draw& d : draws)
	{
		texVar->SetResource(d.Texture);
		const XMFLOAT4 srgb(isSrgb(d.Texture) ? 1.0f : 0.0f, 0, 0, 0);
		srgbVar->SetFloatVector(&srgb.x);
		if (lit)
		{
			normalVar->SetResource(d.Normal ? d.Normal : BlackTexture());
			const XMFLOAT4 info((float)lights.size(), d.Normal ? 1.0f : 0.0f, d.Normal && isSrgb(d.Normal) ? 1.0f : 0.0f, 0.0f);
			infoVar->SetFloatVector(&info.x);
		}
		(d.Point ? point : linear)->Apply(0, ctx);
		ctx->Draw(d.Count, d.Start);
		++s_LastDrawCalls;
	}
	texVar->SetResource(nullptr);
	if (lit)
	{
		normalVar->SetResource(nullptr);
		fx->GetVariableByName("gShadow0")->AsShaderResource()->SetResource(nullptr);
		fx->GetVariableByName("gShadow1")->AsShaderResource()->SetResource(nullptr);
	}
	GfxShaderResourceView* nullSRV[4] = {};
	ctx->PSSetShaderResources(0, 4, nullSRV);
	ctx->OMSetBlendState(nullptr, nullptr, 0xffffffff);
	ctx->OMSetDepthStencilState(nullptr, 0);
	ctx->RSSetState(nullptr);
}
