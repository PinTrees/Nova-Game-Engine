#include "pch.h"
#include "RockRenderer.h"
#include "Rock.h"
#include "RockScatter.h"
#include "Effects.h"
#include "RenderManager.h"
#include "Transform.h"
#include "ResourceManager.h"
#include "UMaterial.h"
#include "Profiler.h"
#include <unordered_map>

namespace
{
	// ================================================================ 메시 (모양 + LOD 마다 하나)
	struct GpuMesh
	{
		ComPtr<ID3D11Buffer> VB, IB;
		UINT IndexCount = 0;
		int VertexCount = 0;
		Vec3 BoundsMin, BoundsMax;
		double Ms = 0.0;
		std::vector<XMFLOAT3> Positions;   // LOD0 만 (선택)
		std::vector<uint32_t> Indices;
		uint64_t LastUse = 0;
	};
	std::unordered_map<std::string, std::shared_ptr<GpuMesh>> s_Meshes;
	uint64_t s_Frame = 0;
	RockRenderer::Stats s_Stats[2];

	std::shared_ptr<GpuMesh> GetMesh(const RockDesc& desc, int lod)
	{
		const std::string key = desc.MeshKey() + "#" + std::to_string(lod);
		auto it = s_Meshes.find(key);
		if (it != s_Meshes.end())
		{
			it->second->LastUse = s_Frame;
			return it->second;
		}
		const RockGenerator::Mesh m = RockGenerator::Generate(desc.Params, lod);
		auto g = std::make_shared<GpuMesh>();
		g->VertexCount = (int)m.Vertices.size();
		g->IndexCount = (UINT)m.Indices.size();
		g->BoundsMin = Vec3(m.BoundsMin.x, m.BoundsMin.y, m.BoundsMin.z);
		g->BoundsMax = Vec3(m.BoundsMax.x, m.BoundsMax.y, m.BoundsMax.z);
		g->Ms = m.Ms;
		g->LastUse = s_Frame;
		if (!m.Vertices.empty() && !m.Indices.empty())
		{
			auto device = Application::GetI()->GetDevice();
			D3D11_BUFFER_DESC bd = {};
			bd.Usage = D3D11_USAGE_IMMUTABLE;
			bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
			bd.ByteWidth = (UINT)(m.Vertices.size() * sizeof(RockGenerator::Vertex));
			D3D11_SUBRESOURCE_DATA init = { m.Vertices.data(), 0, 0 };
			device->CreateBuffer(&bd, &init, g->VB.GetAddressOf());
			bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
			bd.ByteWidth = (UINT)(m.Indices.size() * sizeof(uint32_t));
			init.pSysMem = m.Indices.data();
			device->CreateBuffer(&bd, &init, g->IB.GetAddressOf());
		}
		if (lod == 0)
		{
			g->Positions.reserve(m.Vertices.size());
			for (const auto& v : m.Vertices)
				g->Positions.push_back(v.Pos);
			g->Indices = m.Indices;
		}
		EditorLog::Write("Rock", "rock mesh %s LOD%d: %d vertices, %u triangles, %.0f ms", RockDesc::PresetName(desc.PresetIndex), lod, g->VertexCount, g->IndexCount / 3, m.Ms);
		// 슬라이더를 끌면 모양이 많이 생기므로 오래 안 쓴 것부터 버린다
		if (s_Meshes.size() > 96)
			for (auto i = s_Meshes.begin(); i != s_Meshes.end();)
				i = s_Frame - i->second->LastUse > 600 ? s_Meshes.erase(i) : std::next(i);
		s_Meshes[key] = g;
		return g;
	}

	// ================================================================ 효과 변수
	struct RockVars
	{
		FxEffect* Fx = nullptr;
		FxTechnique* Tech = nullptr;
		FxVar *Base = nullptr, *Strata = nullptr, *Moss = nullptr, *Params = nullptr, *Params2 = nullptr;
		FxVar* Detail = nullptr;
		void Bind(FxEffect* fx, const char* tech)
		{
			Fx = fx;
			Tech = fx->GetTechniqueByName(tech);
			Base = fx->GetVariableByName("gRockBaseColor")->AsVector();
			Strata = fx->GetVariableByName("gRockStrataColor")->AsVector();
			Moss = fx->GetVariableByName("gRockMossColor")->AsVector();
			Params = fx->GetVariableByName("gRockParams")->AsVector();
			Params2 = fx->GetVariableByName("gRockParams2")->AsVector();
			Detail = fx->GetVariableByName("gRockDetail")->AsShaderResource();
		}
		bool Valid() const { return Tech && Tech->IsValid(); }
	};

	enum { kMain, kShadow, kNormalDepth };
	RockVars& Vars(int pass)
	{
		static RockVars vars[3];
		static bool bound = false;
		if (!bound)
		{
			bound = true;
			vars[kMain].Bind(Effects::InstancedBasicFX->GetFX(), "RockTech");
			vars[kShadow].Bind(Effects::BuildShadowMapFX->GetFX(), "RockShadowTech");
			vars[kNormalDepth].Bind(Effects::SsaoNormalDepthFX->GetFX(), "RockNormalDepthTech");
			if (!vars[kMain].Valid() || !vars[kShadow].Valid() || !vars[kNormalDepth].Valid())
				EditorLog::Write("Rock", "rock techniques missing (main %d, shadow %d, normalDepth %d)", vars[kMain].Valid(), vars[kShadow].Valid(), vars[kNormalDepth].Valid());
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

	ID3D11ShaderResourceView* DetailTexture()
	{
		static ComPtr<ID3D11ShaderResourceView> s_Tex;
		static bool tried = false;
		if (!tried)
		{
			tried = true;
			s_Tex = ResourceManager::GetI()->LoadTexture(L"Resources\\Packages\\Rock\\Textures\\RockDetail.png");
			if (!s_Tex)
				EditorLog::Write("Rock", "RockDetail.png missing");
		}
		return s_Tex.Get();
	}

	// ================================================================ 입력 배치 / 인스턴스
	struct InstanceData
	{
		XMFLOAT4X4 World;
		XMFLOAT4 Extra;   // x 색 변화 (0~1)
	};
	static_assert(sizeof(InstanceData) == 80, "셰이더 RockInstanceIn 과 같은 배치");

	ComPtr<ID3D11InputLayout> s_Layout;
	ComPtr<ID3D11Buffer> s_InstanceBuffer;
	UINT s_InstanceCapacity = 0;

	bool EnsureLayout()
	{
		static bool tried = false;
		if (tried)
			return s_Layout != nullptr;
		tried = true;
		RockVars& v = Vars(kMain);
		if (!v.Valid())
			return false;
		const D3D11_INPUT_ELEMENT_DESC desc[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "WORLD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
			{ "WORLD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
			{ "WORLD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
			{ "WORLD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 48, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
			{ "INSTANCE", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 64, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
		};
		D3DX11_PASS_DESC pd;
		v.Tech->GetPassByIndex(0)->GetDesc(&pd);
		const HRESULT hr = Application::GetI()->GetDevice()->CreateInputLayout(desc, _countof(desc), pd.pIAInputSignature, pd.IAInputSignatureSize, s_Layout.GetAddressOf());
		if (FAILED(hr))
			EditorLog::Write("Rock", "input layout failed hr=0x%08X", (unsigned)hr);
		return s_Layout != nullptr;
	}

	ID3D11Buffer* UploadInstances(ID3D11DeviceContext* dc, const std::vector<InstanceData>& data)
	{
		const UINT count = (UINT)data.size();
		if (count > s_InstanceCapacity)
		{
			s_InstanceCapacity = (std::max)(count, s_InstanceCapacity * 2 + 256);
			D3D11_BUFFER_DESC bd = {};
			bd.Usage = D3D11_USAGE_DYNAMIC;
			bd.ByteWidth = s_InstanceCapacity * sizeof(InstanceData);
			bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
			bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			s_InstanceBuffer.Reset();
			Application::GetI()->GetDevice()->CreateBuffer(&bd, nullptr, s_InstanceBuffer.GetAddressOf());
		}
		D3D11_MAPPED_SUBRESOURCE mapped;
		if (!s_InstanceBuffer || FAILED(dc->Map(s_InstanceBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
			return nullptr;
		memcpy(mapped.pData, data.data(), count * sizeof(InstanceData));
		dc->Unmap(s_InstanceBuffer.Get(), 0);
		return s_InstanceBuffer.Get();
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

	float Hash2(float x, float z)
	{
		const float h = sinf(x * 12.9898f + z * 78.233f) * 43758.5453f;
		return h - floorf(h);
	}

	struct Batch
	{
		const RockDesc* Desc = nullptr;
		std::vector<InstanceData> Lists[3];
	};
}

namespace RockRenderer
{
	const Stats& LastStats(bool editor) { return s_Stats[editor ? 1 : 0]; }

	bool GetMeshInfo(const RockDesc& desc, MeshInfo& out, int lod)
	{
		auto mesh = GetMesh(desc, lod);
		if (!mesh)
			return false;
		out.BoundsMin = mesh->BoundsMin;
		out.BoundsMax = mesh->BoundsMax;
		out.Vertices = mesh->VertexCount;
		out.Triangles = (int)(mesh->IndexCount / 3);
		out.Ms = mesh->Ms;
		out.Positions = &mesh->Positions;
		out.Indices = &mesh->Indices;
		return true;
	}

	void CollectMemory(std::vector<MemoryStats::Item>& gpu, std::vector<MemoryStats::Item>& cpu)
	{
		for (const auto& [key, mesh] : s_Meshes)
		{
			if (!mesh)
				continue;
			const std::string name = "Rock mesh " + std::to_string(std::hash<std::string>()(key) % 100000) + " (" + key.substr(key.rfind('#') + 1) + ")";
			gpu.push_back({ name, MemoryStats::ResourceBytes(mesh->VB.Get()) + MemoryStats::ResourceBytes(mesh->IB.Get()) });
			cpu.push_back({ name + " pick data", mesh->Positions.capacity() * sizeof(XMFLOAT3) + mesh->Indices.capacity() * sizeof(uint32_t) });
		}
	}

	void DrawAll(Pass pass, bool editor)
	{
		if (Rock::All().empty() && RockScatter::All().empty())
			return;
		PROFILE_SCOPE("Rocks");
		PROFILE_GPU("Rocks");
		++s_Frame;
		const int passIndex = pass == Pass::Main ? kMain : (pass == Pass::Shadow ? kShadow : kNormalDepth);
		RockVars& v = Vars(passIndex);
		if (!v.Valid() || !EnsureLayout())
			return;
		RenderManager* rm = RenderManager::GetI();
		const bool shadow = pass == Pass::Shadow;
		const bool editorView = shadow ? rm->RenderingEditorView : editor;
		const XMMATRIX view = editorView ? rm->EditorCameraViewMatrix : rm->CameraViewMatrix;
		const XMMATRIX viewProj = editorView ? rm->EditorCameraViewProjectionMatrix : rm->CameraViewProjectionMatrix;
		const XMVECTOR camPosV = XMMatrixInverse(nullptr, view).r[3];
		const Vec3 camPos(XMVectorGetX(camPosV), XMVectorGetY(camPosV), XMVectorGetZ(camPosV));
		XMFLOAT4 planes[6];
		ExtractPlanes(shadow ? rm->LightViewProjection : viewProj, planes);
		const int planeCount = shadow ? 5 : 6;

		// ---- 모으기: 설정마다 묶음, 절두체, LOD (화면 크기 = 지름 / 거리)
		std::unordered_map<size_t, Batch> batches;
		Stats stats;
		auto add = [&](const RockDesc& desc, const XMFLOAT4X4& world) {
			if (shadow && !desc.CastShadows)
				return;
			auto mesh = GetMesh(desc, 0);
			if (!mesh || mesh->VertexCount == 0)
				return;
			const float sx = Vec3(world._11, world._12, world._13).Length();
			const float sy = Vec3(world._21, world._22, world._23).Length();
			const float sz = Vec3(world._31, world._32, world._33).Length();
			const Vec3 c = (mesh->BoundsMin + mesh->BoundsMax) * 0.5f;
			const Vec3 center = XMVector3TransformCoord(c, XMLoadFloat4x4(&world));
			const float radius = (mesh->BoundsMax - mesh->BoundsMin).Length() * 0.5f * (std::max)(sx, (std::max)(sy, sz));
			for (int i = 0; i < planeCount; ++i)
				if (planes[i].x * center.x + planes[i].y * center.y + planes[i].z * center.z + planes[i].w < -radius)
					return;
			const float screen = 2.0f * radius / (std::max)((center - camPos).Length(), 0.01f) * desc.LodBias;
			if (screen < 0.004f)
				return;
			int lod = screen > 0.25f ? 0 : (screen > 0.07f ? 1 : 2);
			if (shadow)
				lod = (std::min)(lod + 1, 2);   // 그림자는 한 단계 거칠게 (윤곽만)
			Batch& b = batches[desc.Hash()];
			b.Desc = &desc;
			InstanceData inst;
			inst.World = world;
			inst.Extra = XMFLOAT4(Hash2(world._41, world._43), 0, 0, 0);
			b.Lists[lod].push_back(inst);
			++stats.Rocks;
		};
		// Rock 컴포넌트
		for (Rock* rock : Rock::All())
		{
			if (!rock->IsDrawable())
				continue;
			XMFLOAT4X4 world;
			XMStoreFloat4x4(&world, rock->GetGameObject()->GetTransform()->GetWorldMatrix());
			add(rock->Desc, world);
		}
		// 흩뿌린 바위 (변형 몇 개 × 인스턴스 여럿)
		for (RockScatter* scatter : RockScatter::All())
		{
			if (!scatter->IsDrawable())
				continue;
			const auto& variants = scatter->VariantDescs();
			for (const RockScatter::Instance& inst : scatter->Instances())
				add(variants[inst.Variant], inst.World);
		}
		if (batches.empty())
		{
			if (pass == Pass::Main)
				s_Stats[editor ? 1 : 0] = stats;
			return;
		}

		// ---- 패스 공통 값
		ID3D11DeviceContext* dc = Application::GetI()->GetDeviceContext();
		if (pass == Pass::Main)
		{
			static const XMMATRIX toTex(0.5f, 0.0f, 0.0f, 0.0f, 0.0f, -0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.5f, 0.5f, 0.0f, 1.0f);
			Effects::InstancedBasicFX->SetViewProj(viewProj);
			SetMatrix(v.Fx, "gViewProjTex", viewProj * toTex);
			ShaderSetting setting = UMaterial::GetDefault()->GetShaderSetting();
			setting.UseShadowMap = 1;
			Effects::InstancedBasicFX->SetShaderSetting(setting);
		}
		else if (pass == Pass::NormalDepth)
		{
			SetMatrix(v.Fx, "gView", view);
			SetMatrix(v.Fx, "gWorldViewProj", viewProj);
		}
		if (v.Detail && v.Detail->IsValid())
			v.Detail->SetResource(DetailTexture());

		ComPtr<ID3D11DepthStencilState> prevDSS;
		UINT prevRef = 0;
		dc->OMGetDepthStencilState(prevDSS.GetAddressOf(), &prevRef);
		ComPtr<ID3D11RasterizerState> prevRS;
		dc->RSGetState(prevRS.GetAddressOf());
		dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		dc->IASetInputLayout(s_Layout.Get());

		for (auto& [hash, b] : batches)
		{
			const RockDesc& d = *b.Desc;
			SetVec(v.Base, d.BaseColor);
			SetVec(v.Strata, d.StrataColor);
			SetVec(v.Moss, d.MossColor);
			SetVec(v.Params, XMFLOAT4(d.StrataContrast, d.Moss, d.DetailStrength, d.DetailScale));
			SetVec(v.Params2, XMFLOAT4(d.Smoothness, d.ColorVariation, (float)(std::max)(d.Params.Strata, 3), d.Params.SizeY));
			for (int lod = 0; lod < 3; ++lod)
			{
				if (b.Lists[lod].empty())
					continue;
				auto mesh = GetMesh(d, lod);
				if (!mesh || !mesh->VB)
					continue;
				ID3D11Buffer* inst = UploadInstances(dc, b.Lists[lod]);
				if (!inst)
					continue;
				const UINT strides[2] = { sizeof(RockGenerator::Vertex), sizeof(InstanceData) }, offsets[2] = { 0, 0 };
				ID3D11Buffer* vbs[2] = { mesh->VB.Get(), inst };
				dc->IASetVertexBuffers(0, 2, vbs, strides, offsets);
				dc->IASetIndexBuffer(mesh->IB.Get(), DXGI_FORMAT_R32_UINT, 0);
				v.Tech->GetPassByIndex(0)->Apply(0, dc);
				dc->DrawIndexedInstanced(mesh->IndexCount, (UINT)b.Lists[lod].size(), 0, 0, 0);
				++stats.DrawCalls;
				(lod == 0 ? stats.Lod0 : lod == 1 ? stats.Lod1 : stats.Lod2) += (int)b.Lists[lod].size();
			}
		}
		ID3D11Buffer* none = nullptr;
		UINT zero = 0;
		dc->IASetVertexBuffers(1, 1, &none, &zero, &zero);
		dc->OMSetDepthStencilState(prevDSS.Get(), prevRef);
		dc->RSSetState(prevRS.Get());
		if (pass == Pass::Main)
			s_Stats[editor ? 1 : 0] = stats;
	}
}
