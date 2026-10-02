#include "pch.h"
#include "RenderLayers.h"
#include "SpriteBatch.h"
#include "Effects.h"
#include "TagsAndLayers.h"

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
	m_Tris.push_back({ texture ? texture : WhiteTexture(), point });
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

	// Sorting Layer → Order in Layer → 먼 것부터 → 넣은 순서
	std::vector<Group> groups = b.m_Groups;
	std::sort(groups.begin(), groups.end(), [](const Group& x, const Group& y) {
		if (x.Layer != y.Layer) return x.Layer < y.Layer;
		if (x.Order != y.Order) return x.Order < y.Order;
		if (fabsf(x.Depth - y.Depth) > 1e-5f) return x.Depth > y.Depth;
		return x.Seq < y.Seq;
	});
	// 정렬한 순서로 정점을 다시 쌓고, 같은 텍스처 · 필터가 이어지면 한 번에
	struct Draw { GfxShaderResourceView* Texture; bool Point; UINT Start, Count; };
	std::vector<Vertex> verts;
	verts.reserve(b.m_Vertices.size());
	std::vector<Draw> draws;
	for (const Group& g : groups)
		for (uint32 t = g.First; t < g.First + g.Count; ++t)
		{
			const Tri& tri = b.m_Tris[t];
			if (draws.empty() || draws.back().Texture != tri.Texture || draws.back().Point != tri.Point)
				draws.push_back({ tri.Texture, tri.Point, (UINT)verts.size(), 0 });
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

	GfxRenderTargetView* rtvs[1] = { rtv };
	ctx->OMSetRenderTargets(1, rtvs, dsv);
	ctx->IASetInputLayout(s_Layout.Get());
	ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	const UINT stride = sizeof(Vertex), offset = 0;
	GfxBuffer* vb = s_VB.Get();
	ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);

	FxEffect* fx = s_Effect->GetFX();
	XMFLOAT4X4 m;
	XMStoreFloat4x4(&m, view * proj);
	fx->GetVariableByName("gViewProj")->AsMatrix()->SetMatrix(&m._11);
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
	FxPass* linear = fx->GetTechniqueByName("SpriteTech")->GetPassByIndex(0);
	FxPass* point = fx->GetTechniqueByName("SpritePointTech")->GetPassByIndex(0);
	for (const Draw& d : draws)
	{
		texVar->SetResource(d.Texture);
		const XMFLOAT4 srgb(isSrgb(d.Texture) ? 1.0f : 0.0f, 0, 0, 0);
		srgbVar->SetFloatVector(&srgb.x);
		(d.Point ? point : linear)->Apply(0, ctx);
		ctx->Draw(d.Count, d.Start);
		++s_LastDrawCalls;
	}
	texVar->SetResource(nullptr);
	GfxShaderResourceView* nullSRV[1] = {};
	ctx->PSSetShaderResources(0, 1, nullSRV);
	ctx->OMSetBlendState(nullptr, nullptr, 0xffffffff);
	ctx->OMSetDepthStencilState(nullptr, 0);
	ctx->RSSetState(nullptr);
}
