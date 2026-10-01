#include "pch.h"
#include "UIRenderer.h"
#include "Effects.h"

UIRenderer& UIRenderer::Get()
{
	static UIRenderer s_Instance;
	return s_Instance;
}

uint32 UIRenderer::PackColor(const float rgba[4])
{
	auto b = [](float v) { return (uint32)(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
	return b(rgba[0]) | (b(rgba[1]) << 8) | (b(rgba[2]) << 16) | (b(rgba[3]) << 24);
}

bool UIRenderer::Init()
{
	if (m_Effect || m_Failed)
		return m_Effect != nullptr;
	auto device = Application::GetI()->GetDevice();
	m_Effect = std::make_unique<Effect>(device, L"../Shaders/42. UI.fx");
	if (m_Effect->GetFX() == nullptr)
	{
		m_Effect.reset();
		m_Failed = true;
		EditorLog::Write("UI", "42. UI.fx failed to load - UI is not drawn");
		return false;
	}
	// 정점 형식 = 이펙트 패스의 입력 서명
	D3DX11_PASS_DESC pass = {};
	m_Effect->GetFX()->GetTechniqueByName("UITech")->GetPassByIndex(0)->GetDesc(&pass);
	const D3D11_INPUT_ELEMENT_DESC desc[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};
	if (FAILED(device->CreateInputLayout(desc, 3, pass.pIAInputSignature, pass.IAInputSignatureSize, m_Layout.GetAddressOf())))
	{
		m_Effect.reset();
		m_Failed = true;
		EditorLog::Write("UI", "UI input layout failed");
		return false;
	}
	return true;
}

GfxShaderResourceView* UIRenderer::WhiteTexture()
{
	if (m_White == nullptr)
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
			device->CreateShaderResourceView(tex.Get(), nullptr, m_White.GetAddressOf());
	}
	return m_White.Get();
}

void UIRenderer::Begin()
{
	m_Vertices.clear();
	m_Indices.clear();
	m_Commands.clear();
	m_ClipOn = false;
}

void UIRenderer::SetClip(bool enabled, const Vec4& worldRect)
{
	m_ClipOn = enabled;
	m_ClipRect = worldRect;
}

void UIRenderer::Reserve(GfxShaderResourceView* texture)
{
	// 같은 텍스처·같은 잘라내기가 이어지면 한 번의 그리기로 합친다 (순서는 유지: 뒤에 그린 것이 위)
	const bool sameClip = !m_Commands.empty() && m_Commands.back().Clip == m_ClipOn && (!m_ClipOn || m_Commands.back().ClipRect == m_ClipRect);
	if (m_Commands.empty() || m_Commands.back().Texture != texture || !sameClip)
		m_Commands.push_back({ texture, (UINT)m_Indices.size(), 0, m_ClipOn, m_ClipRect });
}

void UIRenderer::AddQuad(const Vec3 p[4], const Vec2 uv[4], uint32 color, GfxShaderResourceView* texture)
{
	if ((color >> 24) == 0)
		return;
	if (texture == nullptr)
		texture = WhiteTexture();
	Reserve(texture);
	const uint32 base = (uint32)m_Vertices.size();
	for (int i = 0; i < 4; ++i)
		m_Vertices.push_back({ p[i].x, p[i].y, p[i].z, uv[i].x, uv[i].y, color });
	for (uint32 i : { 0u, 1u, 2u, 0u, 2u, 3u })
		m_Indices.push_back(base + i);
	m_Commands.back().IndexCount += 6;
}

void UIRenderer::AddTriangle(const Vec3 p[3], const Vec2 uv[3], uint32 color, GfxShaderResourceView* texture)
{
	if ((color >> 24) == 0)
		return;
	if (texture == nullptr)
		texture = WhiteTexture();
	Reserve(texture);
	const uint32 base = (uint32)m_Vertices.size();
	for (int i = 0; i < 3; ++i)
	{
		m_Vertices.push_back({ p[i].x, p[i].y, p[i].z, uv[i].x, uv[i].y, color });
		m_Indices.push_back(base + i);
	}
	m_Commands.back().IndexCount += 3;
}

void UIRenderer::AddLine(const Vec3& a, const Vec3& b, float thickness, uint32 color)
{
	Vec3 d = b - a;
	if (d.LengthSquared() < 1e-8f)
		return;
	d.Normalize();
	// 캔버스 평면(z) 위의 선: 진행 방향에 수직인 방향으로 두께
	Vec3 n(-d.y, d.x, 0.0f);
	if (n.LengthSquared() < 1e-8f)
		n = Vec3(1, 0, 0);
	n.Normalize();
	n *= thickness * 0.5f;
	const Vec3 p[4] = { a - n, a + n, b + n, b - n };
	const Vec2 uv[4] = { Vec2(0, 0), Vec2(0, 0), Vec2(0, 0), Vec2(0, 0) };
	AddQuad(p, uv, color, WhiteTexture());
}

void UIRenderer::Flush(GfxRenderTargetView* rtv, UINT width, UINT height, const Matrix& viewProj, GfxDepthStencilView* dsv)
{
	m_LastDrawCalls = 0;
	if (m_Indices.empty() || rtv == nullptr || !Init())
		return;
	auto device = Application::GetI()->GetDevice();
	auto ctx = Application::GetI()->GetDeviceContext();

	// 동적 버퍼 (모자라면 두 배로)
	auto ensure = [&](ComPtr<GfxBuffer>& buf, UINT& cap, UINT need, UINT stride, UINT bind) {
		if (need <= cap && buf)
			return true;
		cap = (std::max)(need, cap * 2u);
		cap = (std::max)(cap, 1024u);
		D3D11_BUFFER_DESC bd = {};
		bd.ByteWidth = cap * stride;
		bd.Usage = D3D11_USAGE_DYNAMIC;
		bd.BindFlags = bind;
		bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		buf.Reset();
		return SUCCEEDED(device->CreateBuffer(&bd, nullptr, buf.GetAddressOf()));
	};
	if (!ensure(m_VB, m_VBCapacity, (UINT)m_Vertices.size(), sizeof(Vertex), D3D11_BIND_VERTEX_BUFFER) ||
		!ensure(m_IB, m_IBCapacity, (UINT)m_Indices.size(), sizeof(uint32), D3D11_BIND_INDEX_BUFFER))
		return;
	D3D11_MAPPED_SUBRESOURCE mapped;
	if (FAILED(ctx->Map(m_VB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
		return;
	memcpy(mapped.pData, m_Vertices.data(), m_Vertices.size() * sizeof(Vertex));
	ctx->Unmap(m_VB.Get(), 0);
	if (FAILED(ctx->Map(m_IB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
		return;
	memcpy(mapped.pData, m_Indices.data(), m_Indices.size() * sizeof(uint32));
	ctx->Unmap(m_IB.Get(), 0);

	GfxRenderTargetView* rtvs[1] = { rtv };
	ctx->OMSetRenderTargets(1, rtvs, dsv);
	D3D11_VIEWPORT vp = { 0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f };
	ctx->RSSetViewports(1, &vp);
	ctx->IASetInputLayout(m_Layout.Get());
	ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	const UINT stride = sizeof(Vertex), offset = 0;
	GfxBuffer* vb = m_VB.Get();
	ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
	ctx->IASetIndexBuffer(m_IB.Get(), DXGI_FORMAT_R32_UINT, 0);

	FxEffect* fx = m_Effect->GetFX();
	XMFLOAT4X4 m;
	XMStoreFloat4x4(&m, viewProj);
	fx->GetVariableByName("gViewProj")->AsMatrix()->SetMatrix(&m._11);
	FxVar* texVar = fx->GetVariableByName("gTexture")->AsShaderResource();
	FxPass* pass = fx->GetTechniqueByName(dsv ? "UISceneTech" : "UITech")->GetPassByIndex(0);
	// 잘라내기 사각형(캔버스 월드) → 화면 픽셀 (네 모서리를 투영한 경계 상자)
	auto scissorOf = [&](const Command& c) {
		D3D11_RECT full = { 0, 0, (LONG)width, (LONG)height };
		if (!c.Clip)
			return full;
		const Vec2 corners[4] = { Vec2(c.ClipRect.x, c.ClipRect.y), Vec2(c.ClipRect.z, c.ClipRect.y), Vec2(c.ClipRect.z, c.ClipRect.w), Vec2(c.ClipRect.x, c.ClipRect.w) };
		float x0 = FLT_MAX, y0 = FLT_MAX, x1 = -FLT_MAX, y1 = -FLT_MAX;
		for (const Vec2& k : corners)
		{
			const Vec4 clip = Vec4::Transform(Vec4(k.x, k.y, 0.0f, 1.0f), viewProj);
			if (clip.w <= 1e-5f)
				return full;   // 카메라 뒤: 자르지 않는다
			const float sx = (clip.x / clip.w * 0.5f + 0.5f) * width;
			const float sy = (1.0f - (clip.y / clip.w * 0.5f + 0.5f)) * height;
			x0 = (std::min)(x0, sx); y0 = (std::min)(y0, sy);
			x1 = (std::max)(x1, sx); y1 = (std::max)(y1, sy);
		}
		D3D11_RECT r;
		r.left = (LONG)std::clamp(floorf(x0), 0.0f, (float)width);
		r.top = (LONG)std::clamp(floorf(y0), 0.0f, (float)height);
		r.right = (LONG)std::clamp(ceilf(x1), 0.0f, (float)width);
		r.bottom = (LONG)std::clamp(ceilf(y1), 0.0f, (float)height);
		return r;
	};
	for (const Command& c : m_Commands)
	{
		if (c.IndexCount == 0)
			continue;
		texVar->SetResource(c.Texture);
		pass->Apply(0, ctx);
		const D3D11_RECT sc = scissorOf(c);
		if (sc.right <= sc.left || sc.bottom <= sc.top)
			continue;   // 완전히 잘림
		ctx->RSSetScissorRects(1, &sc);
		ctx->DrawIndexed(c.IndexCount, c.IndexStart, 0);
		++m_LastDrawCalls;
	}
	texVar->SetResource(nullptr);
	GfxShaderResourceView* nullSRV[1] = {};
	ctx->PSSetShaderResources(0, 1, nullSRV);
	// 다른 그리기에 영향이 없도록 상태를 기본으로
	ctx->OMSetBlendState(nullptr, nullptr, 0xffffffff);
	ctx->OMSetDepthStencilState(nullptr, 0);
	ctx->RSSetState(nullptr);
}
