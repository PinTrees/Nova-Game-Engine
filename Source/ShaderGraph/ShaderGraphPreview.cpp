#include "pch.h"
#include "ShaderGraphPreview.h"
#include "ShaderGraphRuntime.h"
#include "Effects.h"
#include "SpriteBatch.h"
#include <filesystem>

namespace fs = std::filesystem;
using namespace ShaderGraph;

namespace
{
	constexpr int kColumns = 16;
	const auto s_Start = std::chrono::steady_clock::now();
}

ShaderGraphPreview::ShaderGraphPreview()
{
	m_Path = (fs::path(PathManager::GetI()->GetContentPathW()) / L"Library" / L"ShaderGraph" / L"_Preview.fx").lexically_normal().wstring();
}

ShaderGraphPreview::~ShaderGraphPreview() = default;

bool ShaderGraphPreview::EnsureTarget(Target& t, int w, int h)
{
	if (t.Rtv && t.W == w && t.H == h)
		return true;
	auto device = Application::GetI()->GetDevice();
	D3D11_TEXTURE2D_DESC td = {};
	td.Width = (UINT)w;
	td.Height = (UINT)h;
	td.MipLevels = td.ArraySize = 1;
	td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	td.SampleDesc.Count = 1;
	td.Usage = D3D11_USAGE_DEFAULT;
	td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	t = Target();
	if (FAILED(device->CreateTexture2D(&td, nullptr, t.Color.GetAddressOf())))
		return false;
	device->CreateRenderTargetView(t.Color.Get(), nullptr, t.Rtv.GetAddressOf());
	device->CreateShaderResourceView(t.Color.Get(), nullptr, t.Srv.GetAddressOf());
	t.W = w;
	t.H = h;
	return t.Rtv != nullptr;
}

GfxShaderResourceView* ShaderGraphPreview::Texture(const std::string& path)
{
	if (path.empty())
		return SpriteBatch::WhiteTexture();
	auto it = m_Textures.find(path);
	if (it == m_Textures.end())
		it = m_Textures.emplace(path, ResourceManager::GetI()->LoadTexture(string_to_wstring(path))).first;
	return it->second ? it->second.Get() : SpriteBatch::WhiteTexture();
}

void ShaderGraphPreview::Update(const Graph& g, uint64 revision, bool nodes, bool main)
{
	using clock = std::chrono::steady_clock;
	if (revision != m_Revision)
	{
		m_Revision = revision;
		m_Dirty = true;
		m_ChangeTime = clock::now();
	}
	// 바뀐 뒤 0.25 초 (값을 끌어 바꾸는 동안 매 프레임 컴파일하지 않게)
	if (m_Dirty && clock::now() - m_ChangeTime > std::chrono::milliseconds(250))
	{
		m_Dirty = false;
		const CodeResult code = GeneratePreview(g);
		if (!code.Error.empty())
			m_Error = code.Error;
		else if (code.Hlsl != m_LastHlsl || !m_Fx)
		{
			if (!WriteIfChanged(m_Path, code.Hlsl))
				m_Error = "cannot write the preview shader";
			else
			{
				m_LastHlsl = code.Hlsl;
				m_Pending = g;
				m_Waiting = true;
				std::string e;
				CompileInBackground(m_Path, e);
			}
		}
		else
			m_Graph = g;   // 같은 코드 (위치만 바뀜 등)
	}
	if (m_Waiting)
	{
		std::string e;
		const int state = CompileInBackground(m_Path, e);
		if (state != 0)
		{
			m_Waiting = false;
			if (state < 0)
				m_Error = e;
			else
			{
				auto fx = std::make_unique<Effect>(Application::GetI()->GetDevice(), m_Path);   // 캐시 적중
				if (fx->GetFX() && fx->GetFX()->IsValid() && fx->GetFX()->GetTechniqueByName("SGPreviewNodeTech")->IsValid())
				{
					m_Fx = std::move(fx);
					m_Graph = m_Pending;
					m_Error.clear();
					m_Tiles.clear();
					int i = 0;
					for (const Node& n : m_Graph.Nodes)
						m_Tiles[n.Id] = i++;
				}
				else
					m_Error = "preview shader failed (see Logs/Editor.log)";
			}
		}
	}
	if (m_Fx)
		Render(nodes, main);
}

void ShaderGraphPreview::Render(bool nodes, bool main)
{
	FxEffect* fx = m_Fx->GetFX();
	FxTechnique* nodeTech = fx->GetTechniqueByName("SGPreviewNodeTech");
	FxTechnique* mainTech = fx->GetTechniqueByName("SGPreviewMainTech");
	auto var = [&](const std::string& n) -> FxVar* {
		FxVar* v = fx->GetVariableByName(n.c_str());
		return v && v->IsValid() ? v : nullptr;
	};

	// 속성 기본값 · 그림 · 시간
	for (const Property& p : m_Graph.Properties)
		if (FxVar* v = var("gSG_" + Sanitize(p.Ref)))
		{
			if (p.Type == "Texture2D") v->SetResource(Texture(p.Texture));
			else v->SetFloatVector(p.Value);
		}
	for (const Node& n : m_Graph.Nodes)
	{
		const std::string tex = n.Options.value("texture", std::string());
		if (n.Type == "Sample Texture 2D" && !tex.empty())
			if (FxVar* v = var("gSG_NodeTex" + std::to_string(n.Id)))
				v->SetResource(Texture(tex));
	}
	if (FxVar* v = var("gSGTime"))
	{
		const float t = std::chrono::duration<float>(std::chrono::steady_clock::now() - s_Start).count();
		const float tv[4] = { t, sinf(t), cosf(t), ImGui::GetIO().DeltaTime };
		v->SetFloatVector(tv);
	}
	FxVar* params = var("gSGPreview");

	auto ctx = Application::GetI()->GetDeviceContext();
	ComPtr<GfxRenderTargetView> oldRtv;
	ComPtr<GfxDepthStencilView> oldDsv;
	ctx->OMGetRenderTargets(1, oldRtv.GetAddressOf(), oldDsv.GetAddressOf());
	UINT vpCount = 1;
	D3D11_VIEWPORT oldVp = {};
	ctx->RSGetViewports(&vpCount, &oldVp);
	ctx->OMSetDepthStencilState(nullptr, 0);
	ctx->OMSetBlendState(nullptr, nullptr, 0xffffffff);
	ctx->RSSetState(nullptr);
	ctx->IASetInputLayout(nullptr);
	ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// 노드: 아틀라스 칸마다 (뷰포트만 바꿔) 삼각형 하나
	if (nodes && !m_Tiles.empty() && nodeTech && nodeTech->IsValid())
	{
		const int rows = ((int)m_Tiles.size() + kColumns - 1) / kColumns;
		if (EnsureTarget(m_Atlas, kColumns * kTile, rows * kTile))
		{
			GfxRenderTargetView* rtvs[1] = { m_Atlas.Rtv.Get() };
			ctx->OMSetRenderTargets(1, rtvs, nullptr);
			const float clear[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
			ctx->ClearRenderTargetView(m_Atlas.Rtv.Get(), clear);
			for (const auto& [id, tile] : m_Tiles)
			{
				const D3D11_VIEWPORT vp = { (float)((tile % kColumns) * kTile), (float)((tile / kColumns) * kTile), (float)kTile, (float)kTile, 0.0f, 1.0f };
				ctx->RSSetViewports(1, &vp);
				const float p[4] = { (float)id, 0, 0, 0 };
				if (params) params->SetFloatVector(p);
				nodeTech->GetPassByIndex(0)->Apply(0, ctx);
				ctx->Draw(3, 0);
			}
		}
	}
	// Main Preview
	if (main && mainTech && mainTech->IsValid() && EnsureTarget(m_Main, kMainSize, kMainSize))
	{
		GfxRenderTargetView* rtvs[1] = { m_Main.Rtv.Get() };
		ctx->OMSetRenderTargets(1, rtvs, nullptr);
		const D3D11_VIEWPORT vp = { 0, 0, (float)kMainSize, (float)kMainSize, 0.0f, 1.0f };
		ctx->RSSetViewports(1, &vp);
		const float p[4] = { -1.0f, (float)Shape, Yaw, Pitch };
		if (params) params->SetFloatVector(p);
		mainTech->GetPassByIndex(0)->Apply(0, ctx);
		ctx->Draw(3, 0);
	}

	GfxShaderResourceView* nullSRV[16] = {};
	ctx->PSSetShaderResources(0, 16, nullSRV);
	GfxRenderTargetView* restore[1] = { oldRtv.Get() };
	ctx->OMSetRenderTargets(1, restore, oldDsv.Get());
	if (vpCount > 0)
		ctx->RSSetViewports(1, &oldVp);
}

ImTextureID ShaderGraphPreview::NodeTexture(int nodeId, ImVec2& uv0, ImVec2& uv1) const
{
	auto it = m_Tiles.find(nodeId);
	if (it == m_Tiles.end() || !m_Atlas.Srv)
		return nullptr;
	const int tile = it->second;
	const float w = (float)m_Atlas.W, h = (float)m_Atlas.H;
	uv0 = ImVec2((tile % kColumns) * kTile / w, (tile / kColumns) * kTile / h);
	uv1 = ImVec2(uv0.x + kTile / w, uv0.y + kTile / h);
	return (ImTextureID)m_Atlas.Srv.Get();
}

ImTextureID ShaderGraphPreview::MainTexture() const
{
	return m_Main.Srv ? (ImTextureID)m_Main.Srv.Get() : nullptr;
}
