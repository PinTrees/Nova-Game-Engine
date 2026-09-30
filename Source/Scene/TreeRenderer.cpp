#include "pch.h"
#include "TreeRenderer.h"
#include "Tree.h"
#include "TreeTextures.h"
#include "Terrain.h"
#include "TerrainData.h"
#include "Transform.h"
#include "Effects.h"
#include "RenderManager.h"
#include "UMaterial.h"
#include <chrono>
#include <map>
#include <unordered_map>

namespace
{
	float s_Time = 0.0f;
	TreeRenderer::Stats s_Stats[2];

	// ================================================================ 메시 (모양 + LOD 마다 하나)
	struct GpuMesh
	{
		ComPtr<ID3D11Buffer> VB, IB;
		uint32 BarkIndexCount = 0, LeafIndexCount = 0;
		int VertexCount = 0, BranchCount = 0, LeafCardCount = 0;
		Vec3 BoundsMin, BoundsMax;
		std::vector<XMFLOAT3> Positions;
		std::vector<uint32_t> Indices;
	};
	std::map<std::string, std::shared_ptr<GpuMesh>> s_Meshes;

	std::shared_ptr<GpuMesh> GetMesh(const TreeDesc& desc, int lod)
	{
		const std::string key = desc.MeshKey() + (lod ? "#1" : "#0");
		if (auto it = s_Meshes.find(key); it != s_Meshes.end())
			return it->second;

		const auto t0 = std::chrono::steady_clock::now();
		TreeMeshData data;
		TreeGenerator::Generate(desc.Params, data, lod);
		auto mesh = std::make_shared<GpuMesh>();
		mesh->BarkIndexCount = data.BarkIndexCount;
		mesh->LeafIndexCount = data.LeafIndexCount;
		mesh->VertexCount = (int)data.Vertices.size();
		mesh->BranchCount = data.BranchCount;
		mesh->LeafCardCount = data.LeafCardCount;
		mesh->BoundsMin = Vec3(data.BoundsMin.x, data.BoundsMin.y, data.BoundsMin.z);
		mesh->BoundsMax = Vec3(data.BoundsMax.x, data.BoundsMax.y, data.BoundsMax.z);
		mesh->Positions.reserve(data.Vertices.size());
		for (const TreeVertex& v : data.Vertices)
			mesh->Positions.push_back(v.Pos);
		mesh->Indices = data.Indices;
		if (!data.Vertices.empty() && !data.Indices.empty())
		{
			auto device = Application::GetI()->GetDevice();
			D3D11_BUFFER_DESC bd = {};
			bd.Usage = D3D11_USAGE_IMMUTABLE;
			bd.ByteWidth = (UINT)(sizeof(TreeVertex) * data.Vertices.size());
			bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
			D3D11_SUBRESOURCE_DATA init = { data.Vertices.data(), 0, 0 };
			device->CreateBuffer(&bd, &init, mesh->VB.GetAddressOf());
			bd.ByteWidth = (UINT)(sizeof(uint32_t) * data.Indices.size());
			bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
			init.pSysMem = data.Indices.data();
			device->CreateBuffer(&bd, &init, mesh->IB.GetAddressOf());
		}
		const float ms = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
		EditorLog::Write("Tree", "generated seed %d LOD%d: %d vertices, %u triangles, %d branches, %d leaf cards (%.1f ms)",
			desc.Params.Seed, lod, mesh->VertexCount, (unsigned)(data.Indices.size() / 3), mesh->BranchCount, mesh->LeafCardCount, ms);

		// 슬라이더를 끌면 모양이 많이 생기므로 쓰는 곳이 없는 것부터 버린다
		if (s_Meshes.size() > 64)
			for (auto it = s_Meshes.begin(); it != s_Meshes.end();)
				it = it->second.use_count() == 1 ? s_Meshes.erase(it) : std::next(it);
		s_Meshes[key] = mesh;
		return mesh;
	}

	// ================================================================ 효과 변수
	struct TreeVars
	{
		ID3DX11Effect* Fx = nullptr;
		ID3DX11EffectTechnique* Bark = nullptr;
		ID3DX11EffectTechnique* Leaf = nullptr;
		ID3DX11EffectTechnique* Impostor = nullptr;
		ID3DX11EffectTechnique* BarkBake = nullptr;
		ID3DX11EffectTechnique* LeafBake = nullptr;
		ID3DX11EffectVectorVariable* Wind = nullptr;
		ID3DX11EffectVectorVariable* WindParams = nullptr;
		ID3DX11EffectVectorVariable* BarkColor = nullptr;
		ID3DX11EffectVectorVariable* BarkParams = nullptr;
		ID3DX11EffectVectorVariable* MossColor = nullptr;
		ID3DX11EffectVectorVariable* LeafColor = nullptr;
		ID3DX11EffectVectorVariable* LeafColor2 = nullptr;
		ID3DX11EffectVectorVariable* LeafParams = nullptr;
		ID3DX11EffectVectorVariable* ImpostorParams = nullptr;
		ID3DX11EffectVectorVariable* ViewPos = nullptr;
		ID3DX11EffectShaderResourceVariable* LeafTex = nullptr;
		ID3DX11EffectShaderResourceVariable* BarkTex = nullptr;
		ID3DX11EffectShaderResourceVariable* ImpAlbedo = nullptr;
		ID3DX11EffectShaderResourceVariable* ImpNormal = nullptr;

		void Bind(ID3DX11Effect* fx, const char* bark, const char* leaf, const char* impostor)
		{
			Fx = fx;
			Bark = fx->GetTechniqueByName(bark);
			Leaf = fx->GetTechniqueByName(leaf);
			Impostor = fx->GetTechniqueByName(impostor);
			BarkBake = fx->GetTechniqueByName("TreeBarkBakeTech");
			LeafBake = fx->GetTechniqueByName("TreeLeafBakeTech");
			Wind = fx->GetVariableByName("gTreeWind")->AsVector();
			WindParams = fx->GetVariableByName("gTreeWindParams")->AsVector();
			BarkColor = fx->GetVariableByName("gTreeBarkColor")->AsVector();
			BarkParams = fx->GetVariableByName("gTreeBarkParams")->AsVector();
			MossColor = fx->GetVariableByName("gTreeMossColor")->AsVector();
			LeafColor = fx->GetVariableByName("gTreeLeafColor")->AsVector();
			LeafColor2 = fx->GetVariableByName("gTreeLeafColor2")->AsVector();
			LeafParams = fx->GetVariableByName("gTreeLeafParams")->AsVector();
			ImpostorParams = fx->GetVariableByName("gTreeImpostor")->AsVector();
			ViewPos = fx->GetVariableByName("gTreeViewPos")->AsVector();
			LeafTex = fx->GetVariableByName("gTreeLeafTex")->AsShaderResource();
			BarkTex = fx->GetVariableByName("gTreeBarkTex")->AsShaderResource();
			ImpAlbedo = fx->GetVariableByName("gTreeImpostorAlbedo")->AsShaderResource();
			ImpNormal = fx->GetVariableByName("gTreeImpostorNormal")->AsShaderResource();
		}
		bool Valid() const { return Bark && Bark->IsValid() && Leaf && Leaf->IsValid() && Impostor && Impostor->IsValid(); }
	};

	enum { kMain, kShadow, kNormalDepth };
	TreeVars& Vars(int pass)
	{
		static TreeVars vars[3];
		static bool bound = false;
		if (!bound)
		{
			bound = true;
			vars[kMain].Bind(Effects::InstancedBasicFX->GetFX(), "TreeBarkTech", "TreeLeafTech", "TreeImpostorTech");
			vars[kShadow].Bind(Effects::BuildShadowMapFX->GetFX(), "TreeShadowBarkTech", "TreeShadowLeafTech", "TreeShadowImpostorTech");
			vars[kNormalDepth].Bind(Effects::SsaoNormalDepthFX->GetFX(), "TreeNormalDepthBarkTech", "TreeNormalDepthLeafTech", "TreeNormalDepthImpostorTech");
			if (!vars[kMain].Valid() || !vars[kShadow].Valid() || !vars[kNormalDepth].Valid())
				EditorLog::Write("Tree", "tree techniques missing (main %d, shadow %d, normalDepth %d)", vars[kMain].Valid(), vars[kShadow].Valid(), vars[kNormalDepth].Valid());
		}
		return vars[pass];
	}

	void SetVec(ID3DX11EffectVectorVariable* v, const XMFLOAT4& f)
	{
		if (v && v->IsValid())
			v->SetFloatVector(&f.x);
	}
	void SetSrv(ID3DX11EffectShaderResourceVariable* v, ID3D11ShaderResourceView* srv)
	{
		if (v && v->IsValid())
			v->SetResource(srv);
	}
	void SetMatrix(ID3DX11Effect* fx, const char* name, CXMMATRIX m)
	{
		if (auto* v = fx->GetVariableByName(name)->AsMatrix(); v && v->IsValid())
			v->SetMatrix(reinterpret_cast<const float*>(&m));
	}

	// ================================================================ 입력 배치 / 인스턴스 버퍼
	struct InstanceData
	{
		XMFLOAT4X4 World;
		XMFLOAT4 Extra;   // x 색 변화, y 바람 위상(0~1), z LOD 섞기 문턱, w 섞기 쪽
	};
	static_assert(sizeof(InstanceData) == 80, "셰이더 TreeInstanceIn 과 같은 배치");

	ComPtr<ID3D11InputLayout> s_MeshLayout, s_ImpostorLayout;
	ComPtr<ID3D11Buffer> s_InstanceBuffer;
	UINT s_InstanceCapacity = 0;

	bool EnsureLayouts()
	{
		static bool tried = false;
		if (tried)
			return s_MeshLayout && s_ImpostorLayout;
		tried = true;
		TreeVars& v = Vars(kMain);
		if (!v.Valid())
			return false;
		auto device = Application::GetI()->GetDevice();
		const D3D11_INPUT_ELEMENT_DESC mesh[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "WIND", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "AXIS", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "PHASE", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 64, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "WORLD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
			{ "WORLD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
			{ "WORLD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
			{ "WORLD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 48, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
			{ "INSTANCE", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 64, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
		};
		D3DX11_PASS_DESC pd;
		v.Bark->GetPassByIndex(0)->GetDesc(&pd);
		HRESULT hr = device->CreateInputLayout(mesh, _countof(mesh), pd.pIAInputSignature, pd.IAInputSignatureSize, s_MeshLayout.GetAddressOf());
		if (FAILED(hr))
			EditorLog::Write("Tree", "mesh input layout failed hr=0x%08X", (unsigned)hr);
		v.Impostor->GetPassByIndex(0)->GetDesc(&pd);
		hr = device->CreateInputLayout(mesh + 6, 5, pd.pIAInputSignature, pd.IAInputSignatureSize, s_ImpostorLayout.GetAddressOf());
		if (FAILED(hr))
			EditorLog::Write("Tree", "impostor input layout failed hr=0x%08X", (unsigned)hr);
		return s_MeshLayout && s_ImpostorLayout;
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

	// ================================================================ 설정 값 (나무 한 종류)
	struct Impostor
	{
		ComPtr<ID3D11ShaderResourceView> Albedo, Normal;
		float FrameSize = 1.0f, CenterY = 0.0f;
		bool Tried = false;
	};
	std::unordered_map<size_t, Impostor> s_Impostors;
	constexpr int kFrames = 8;
	constexpr int kFrameRes = 256;

	void SetLook(TreeVars& v, const TreeDesc& d, const Impostor* imp, float windScale)
	{
		const float yaw = XMConvertToRadians(d.WindDirection);
		const float strength = std::clamp(d.WindStrength, 0.0f, 2.0f) * windScale;
		const float h = (std::max)(d.Params.Height, 0.1f);
		SetVec(v.Wind, XMFLOAT4(sinf(yaw) * strength, 0.0f, cosf(yaw) * strength, s_Time));
		SetVec(v.WindParams, XMFLOAT4(d.TrunkSway * h / 9.0f, d.BranchSway * std::clamp(h / 9.0f, 0.2f, 1.5f), d.LeafFlutter, 0.0f));
		SetVec(v.BarkColor, XMFLOAT4(d.BarkColor.x, d.BarkColor.y, d.BarkColor.z, d.Moss));
		SetVec(v.BarkParams, XMFLOAT4(d.RidgeDepth * 0.04f, d.RidgeFrequency, d.BarkSmoothness, d.BarkFleck));
		SetVec(v.MossColor, d.MossColor);
		SetVec(v.LeafColor, XMFLOAT4(d.LeafColor.x, d.LeafColor.y, d.LeafColor.z, d.LeafVariation));
		SetVec(v.LeafColor2, XMFLOAT4(d.LeafColor2.x, d.LeafColor2.y, d.LeafColor2.z, (float)(int)d.Params.Leaf));
		SetVec(v.LeafParams, XMFLOAT4(d.LeafTransmission, d.LeafSmoothness, (float)std::clamp(d.Params.LeavesPerCard, 1, 16), d.LeafLength));
		SetSrv(v.LeafTex, TreeTextures::Leaf((int)d.Params.Leaf, d.Params.LeavesPerCard, d.LeafLength));
		SetSrv(v.BarkTex, TreeTextures::Bark());
		if (imp)
		{
			SetVec(v.ImpostorParams, XMFLOAT4((float)kFrames, imp->FrameSize, imp->CenterY, 0.35f));
			SetSrv(v.ImpAlbedo, imp->Albedo.Get());
			SetSrv(v.ImpNormal, imp->Normal.Get());
		}
	}

	// ================================================================ 임포스터 굽기
	//  LOD0 을 물체 공간(단위 행렬, 바람 0)에서 8 방향 정사영으로 알베도·법선 아틀라스(가로 8 칸)에 그린다
	bool BakeImpostor(Impostor& imp, const TreeDesc& desc)
	{
		imp.Tried = true;
		auto mesh = GetMesh(desc, 0);
		TreeVars& v = Vars(kMain);
		if (!mesh || !mesh->VB || !v.BarkBake || !v.BarkBake->IsValid() || !EnsureLayouts())
			return false;
		const auto t0 = std::chrono::steady_clock::now();
		auto device = Application::GetI()->GetDevice();
		ID3D11DeviceContext* dc = Application::GetI()->GetDeviceContext();
		const UINT W = kFrames * kFrameRes, H = kFrameRes;

		ComPtr<ID3D11Texture2D> tex[2], depth;
		ComPtr<ID3D11RenderTargetView> rtv[2];
		ComPtr<ID3D11DepthStencilView> dsv;
		D3D11_TEXTURE2D_DESC td = {};
		td.Width = W; td.Height = H; td.MipLevels = 6; td.ArraySize = 1;
		td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT;
		td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
		for (int i = 0; i < 2; ++i)
		{
			if (FAILED(device->CreateTexture2D(&td, nullptr, tex[i].GetAddressOf())))
				return false;
			device->CreateRenderTargetView(tex[i].Get(), nullptr, rtv[i].GetAddressOf());
		}
		ComPtr<ID3D11ShaderResourceView> srv[2];
		for (int i = 0; i < 2; ++i)
			device->CreateShaderResourceView(tex[i].Get(), nullptr, srv[i].GetAddressOf());
		td.MipLevels = 1; td.MiscFlags = 0; td.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		if (FAILED(device->CreateTexture2D(&td, nullptr, depth.GetAddressOf())))
			return false;
		device->CreateDepthStencilView(depth.Get(), nullptr, dsv.GetAddressOf());

		// 지금 상태 보관
		ComPtr<ID3D11RenderTargetView> oldRtv[2];
		ComPtr<ID3D11DepthStencilView> oldDsv;
		dc->OMGetRenderTargets(2, oldRtv[0].GetAddressOf(), oldDsv.GetAddressOf());
		UINT vpCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
		D3D11_VIEWPORT oldVp[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
		dc->RSGetViewports(&vpCount, oldVp);
		ComPtr<ID3D11BlendState> oldBlend; float oldFactor[4]; UINT oldMask = 0;
		dc->OMGetBlendState(oldBlend.GetAddressOf(), oldFactor, &oldMask);
		ComPtr<ID3D11DepthStencilState> oldDss; UINT oldRef = 0;
		dc->OMGetDepthStencilState(oldDss.GetAddressOf(), &oldRef);
		ComPtr<ID3D11RasterizerState> oldRs;
		dc->RSGetState(oldRs.GetAddressOf());

		ID3D11RenderTargetView* targets[2] = { rtv[0].Get(), rtv[1].Get() };
		dc->OMSetRenderTargets(2, targets, dsv.Get());
		// 빈 곳: 잎 색(알파 0)으로 지워 밉에서 가장자리가 검게 번지지 않게, 법선은 위
		const float clearAlbedo[4] = { desc.LeafColor.x, desc.LeafColor.y, desc.LeafColor.z, 0.0f };
		const float clearNormal[4] = { 0.5f, 1.0f, 0.5f, 1.0f };
		dc->ClearRenderTargetView(rtv[0].Get(), clearAlbedo);
		dc->ClearRenderTargetView(rtv[1].Get(), clearNormal);
		dc->ClearDepthStencilView(dsv.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
		dc->OMSetBlendState(nullptr, nullptr, 0xffffffff);

		// 프레임 크기: 줄기 축에서 가장 먼 가로 거리와 높이 중 큰 쪽
		const Vec3 mn = mesh->BoundsMin, mx = mesh->BoundsMax;
		const float radius = (std::max)((std::max)(fabsf(mn.x), fabsf(mx.x)), (std::max)(fabsf(mn.z), fabsf(mx.z)));
		const float size = (std::max)(radius * 2.0f, mx.y - mn.y) * 1.06f;
		const float cy = (mn.y + mx.y) * 0.5f;

		SetLook(v, desc, nullptr, 0.0f);   // 바람 0
		std::vector<InstanceData> one(1);
		XMStoreFloat4x4(&one[0].World, XMMatrixIdentity());
		one[0].Extra = XMFLOAT4(0.5f, 0.0f, 0.0f, 0.0f);
		ID3D11Buffer* inst = UploadInstances(dc, one);
		const UINT strides[2] = { sizeof(TreeVertex), sizeof(InstanceData) }, offsets[2] = { 0, 0 };
		ID3D11Buffer* vbs[2] = { mesh->VB.Get(), inst };
		dc->IASetInputLayout(s_MeshLayout.Get());
		dc->IASetVertexBuffers(0, 2, vbs, strides, offsets);
		dc->IASetIndexBuffer(mesh->IB.Get(), DXGI_FORMAT_R32_UINT, 0);
		dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		const float dist = size * 2.0f;
		for (int k = 0; k < kFrames; ++k)
		{
			const D3D11_VIEWPORT vp = { (float)(k * kFrameRes), 0.0f, (float)kFrameRes, (float)kFrameRes, 0.0f, 1.0f };
			dc->RSSetViewports(1, &vp);
			const float a = XM_2PI * k / kFrames;   // 0 = 물체 +Z 쪽에서 봄 (셰이더 TreeImpostorVertex 와 같음)
			const XMVECTOR eye = XMVectorSet(sinf(a) * dist, cy, cosf(a) * dist, 1.0f);
			const XMMATRIX view = XMMatrixLookAtLH(eye, XMVectorSet(0, cy, 0, 1), XMVectorSet(0, 1, 0, 0));
			const XMMATRIX proj = XMMatrixOrthographicLH(size, size, 0.01f, dist * 2.0f + size);
			Effects::InstancedBasicFX->SetViewProj(view * proj);
			if (mesh->BarkIndexCount > 0)
			{
				v.BarkBake->GetPassByIndex(0)->Apply(0, dc);
				dc->DrawIndexedInstanced(mesh->BarkIndexCount, 1, 0, 0, 0);
			}
			if (mesh->LeafIndexCount > 0)
			{
				v.LeafBake->GetPassByIndex(0)->Apply(0, dc);
				dc->DrawIndexedInstanced(mesh->LeafIndexCount, 1, mesh->BarkIndexCount, 0, 0);
			}
		}

		// 되돌리기
		ID3D11RenderTargetView* restore[2] = { oldRtv[0].Get(), oldRtv[1].Get() };
		dc->OMSetRenderTargets(2, restore, oldDsv.Get());
		if (vpCount > 0)
			dc->RSSetViewports(vpCount, oldVp);
		dc->OMSetBlendState(oldBlend.Get(), oldFactor, oldMask);
		dc->OMSetDepthStencilState(oldDss.Get(), oldRef);
		dc->RSSetState(oldRs.Get());
		dc->GenerateMips(srv[0].Get());
		dc->GenerateMips(srv[1].Get());

		imp.Albedo = srv[0];
		imp.Normal = srv[1];
		imp.FrameSize = size;
		imp.CenterY = cy;
		const float ms = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
		EditorLog::Write("Tree", "impostor baked seed %d: %d frames %u x %u, frame %.1f m (%.1f ms)", desc.Params.Seed, kFrames, W, H, size, ms);
		return true;
	}

	Impostor* GetImpostor(const TreeDesc& desc, bool bake)
	{
		Impostor& imp = s_Impostors[desc.Hash()];
		if (!imp.Tried && bake)
			BakeImpostor(imp, desc);
		return imp.Albedo ? &imp : nullptr;
	}

	// ================================================================ 모으기
	struct Batch
	{
		const TreeDesc* Desc = nullptr;
		std::vector<InstanceData> Lists[3];   // LOD0, LOD1, 임포스터
	};
	std::unordered_map<size_t, Batch> s_Batches;

	// 지형 나무의 월드 행렬 캐시 (높이·나무·위치가 바뀔 때만 다시)
	struct TerrainCache
	{
		unsigned HeightRev = ~0u, TreeRev = ~0u;
		Vec3 Origin = Vec3(1e30f, 0, 0);
		std::vector<XMFLOAT4X4> Worlds;
	};
	std::unordered_map<const TerrainData*, TerrainCache> s_TerrainCache;

	const TerrainCache& TerrainWorlds(const TerrainData& data, const Vec3& origin)
	{
		TerrainCache& c = s_TerrainCache[&data];
		if (c.HeightRev == data.Revision && c.TreeRev == data.TreeRevision && (c.Origin - origin).LengthSquared() < 1e-8f)
			return c;
		c.HeightRev = data.Revision;
		c.TreeRev = data.TreeRevision;
		c.Origin = origin;
		c.Worlds.resize(data.TreeInstances.size());
		for (size_t i = 0; i < data.TreeInstances.size(); ++i)
		{
			const TerrainTreeInstance& t = data.TreeInstances[i];
			const float lx = t.X * data.Size.x, lz = t.Z * data.Size.z;
			const float y = data.GetHeight(lx, lz);
			const XMMATRIX w = XMMatrixScaling(t.WidthScale, t.HeightScale, t.WidthScale) * XMMatrixRotationY(t.Rotation)
				* XMMatrixTranslation(origin.x + lx, origin.y + y, origin.z + lz);
			XMStoreFloat4x4(&c.Worlds[i], w);
		}
		return c;
	}

	// 절두체 평면 (행 벡터 × 행렬 규약). planes 수 = 6 (그림자는 가까운 면 없이 5)
	void ExtractPlanes(CXMMATRIX m, XMFLOAT4 planes[6])
	{
		XMFLOAT4X4 f;
		XMStoreFloat4x4(&f, m);
		auto col = [&](int c) { return XMVectorSet(f.m[0][c], f.m[1][c], f.m[2][c], f.m[3][c]); };
		const XMVECTOR c0 = col(0), c1 = col(1), c2 = col(2), c3 = col(3);
		const XMVECTOR p[6] = { c3 + c0, c3 - c0, c3 + c1, c3 - c1, c3 - c2, c2 };
		for (int i = 0; i < 6; ++i)
		{
			const XMVECTOR n = XMPlaneNormalize(p[i]);
			XMStoreFloat4(&planes[i], n);
		}
	}

	float Hash2(float x, float z)
	{
		const float h = sinf(x * 12.9898f + z * 78.233f) * 43758.5453f;
		return h - floorf(h);
	}
}

namespace TreeRenderer
{
	void UpdateTime()
	{
		static const auto start = std::chrono::steady_clock::now();
		s_Time = std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
	}

	const Stats& LastStats(bool editor) { return s_Stats[editor ? 1 : 0]; }

	bool GetMeshInfo(const TreeDesc& desc, MeshInfo& out, int lod)
	{
		auto mesh = GetMesh(desc, lod);
		if (!mesh)
			return false;
		out.BoundsMin = mesh->BoundsMin;
		out.BoundsMax = mesh->BoundsMax;
		out.Vertices = mesh->VertexCount;
		out.Triangles = (int)((mesh->BarkIndexCount + mesh->LeafIndexCount) / 3);
		out.Branches = mesh->BranchCount;
		out.LeafCards = mesh->LeafCardCount;
		out.Positions = &mesh->Positions;
		out.Indices = &mesh->Indices;
		return true;
	}

	ImTextureID Thumbnail(const TreeDesc& desc, ImVec2& uv0, ImVec2& uv1)
	{
		Impostor* imp = GetImpostor(desc, true);
		uv0 = ImVec2(0.0f, 0.0f);
		uv1 = ImVec2(1.0f / kFrames, 1.0f);
		return imp ? (ImTextureID)imp->Albedo.Get() : (ImTextureID)nullptr;
	}

	void DrawAll(Pass pass, bool editor)
	{
		const int passIndex = pass == Pass::Main ? kMain : (pass == Pass::Shadow ? kShadow : kNormalDepth);
		TreeVars& v = Vars(passIndex);
		if (!v.Valid() || !EnsureLayouts())
			return;
		RenderManager* rm = RenderManager::GetI();
		const bool editorView = pass == Pass::Shadow ? rm->RenderingEditorView : editor;
		const XMMATRIX view = editorView ? rm->EditorCameraViewMatrix : rm->CameraViewMatrix;
		const XMMATRIX viewProj = editorView ? rm->EditorCameraViewProjectionMatrix : rm->CameraViewProjectionMatrix;
		const XMVECTOR camPosV = XMMatrixInverse(nullptr, view).r[3];
		const Vec3 camPos(XMVectorGetX(camPosV), XMVectorGetY(camPosV), XMVectorGetZ(camPosV));
		const bool shadow = pass == Pass::Shadow;
		XMFLOAT4 planes[6];
		ExtractPlanes(shadow ? rm->LightViewProjection : viewProj, planes);
		const int planeCount = shadow ? 5 : 6;   // 그림자: 빛 앞쪽(가까운 면) 캐스터도 그림자를 드리운다

		for (auto& kv : s_Batches)
			for (auto& list : kv.second.Lists)
				list.clear();
		Stats stats;

		// 나무 종류 하나의 준비물 (종류마다 한 번 찾고 나무마다 다시 찾지 않는다)
		struct Proto
		{
			const TreeDesc* Desc = nullptr;
			Batch* B = nullptr;
			Vec3 BoundsCenter;
			float BoundsRadius = 0.0f;
		};
		auto prepare = [&](const TreeDesc& desc) {
			Proto p;
			if (shadow && !desc.CastShadows)
				return p;
			auto mesh = GetMesh(desc, 0);
			if (!mesh || mesh->VertexCount == 0)
				return p;
			p.Desc = &desc;
			p.B = &s_Batches[desc.Hash()];   // 노드 기반 맵: 나중에 넣어도 이 포인터는 그대로
			p.B->Desc = &desc;
			p.BoundsCenter = (mesh->BoundsMin + mesh->BoundsMax) * 0.5f;
			p.BoundsRadius = (mesh->BoundsMax - mesh->BoundsMin).Length() * 0.5f;
			return p;
		};

		// ---- 한 그루 넣기: 절두체 → LOD (+ 섞기)
		auto add = [&](const Proto& proto, const XMFLOAT4X4& world, float tint, float phase) {
			if (proto.B == nullptr)
				return;
			const TreeDesc& desc = *proto.Desc;
			const Vec3 pos(world._41, world._42, world._43);
			const float sx = Vec3(world._11, world._12, world._13).Length();
			const float sy = Vec3(world._21, world._22, world._23).Length();
			const Vec3& bc = proto.BoundsCenter;
			const Vec3 center = pos + Vec3(bc.x * sx, bc.y * sy, bc.z * sx);
			const float radius = proto.BoundsRadius * (std::max)(sx, sy);
			for (int i = 0; i < planeCount; ++i)
				if (planes[i].x * center.x + planes[i].y * center.y + planes[i].z * center.z + planes[i].w < -radius)
					return;

			const float d = (pos - camPos).Length() / (std::max)(sy, 0.05f);
			if (d >= desc.CullDistance)
				return;
			Batch& b = *proto.B;
			auto emit = [&](int lod, float t, float side) {
				InstanceData inst;
				inst.World = world;
				inst.Extra = XMFLOAT4(tint, phase, t, side);
				b.Lists[lod].push_back(inst);
			};
			const float l1 = desc.LodDistance, l2 = desc.BillboardDistance, lc = desc.CullDistance;
			const float b1 = l1 * 0.1f, b2 = l2 * 0.1f, bc2 = lc * 0.05f;
			++stats.Trees;
			if (shadow)
			{
				emit(d < l1 ? 0 : (d < l2 ? 1 : 2), 0.0f, 0.0f);
				return;
			}
			if (d < l1 - b1 * 0.5f) emit(0, 0, 0);
			else if (d < l1 + b1 * 0.5f) { const float t = (d - (l1 - b1 * 0.5f)) / b1; emit(0, t, 0); emit(1, t, 1); }
			else if (d < l2 - b2 * 0.5f) emit(1, 0, 0);
			else if (d < l2 + b2 * 0.5f) { const float t = (d - (l2 - b2 * 0.5f)) / b2; emit(1, t, 0); emit(2, t, 1); }
			else if (d < lc - bc2) emit(2, 0, 0);
			else emit(2, (d - (lc - bc2)) / bc2, 0);   // 멀어지며 사라짐
		};

		// Tree 컴포넌트
		for (Tree* tree : Tree::All())
		{
			if (!tree->IsDrawable())
				continue;
			XMFLOAT4X4 world;
			XMStoreFloat4x4(&world, tree->GetGameObject()->GetTransform()->GetWorldMatrix());
			add(prepare(tree->Desc), world, 0.5f, Hash2(world._41, world._43));
		}
		// 지형에 칠한 나무
		for (Terrain* terrain : Terrain::GetActiveTerrains())
		{
			auto data = terrain->GetTerrainData();
			if (!data || !terrain->IsEnabled() || !terrain->GetDraw() || (terrain->GetGameObject() && !terrain->GetGameObject()->IsActive()))
				continue;
			if (data->TreeInstances.empty() || data->TreePrototypes.empty())
				continue;
			const TerrainCache& cache = TerrainWorlds(*data, terrain->GetPosition());
			std::vector<Proto> protos;
			for (const TreeDesc& d : data->TreePrototypes)
				protos.push_back(prepare(d));
			for (size_t i = 0; i < data->TreeInstances.size(); ++i)
			{
				const TerrainTreeInstance& t = data->TreeInstances[i];
				if (t.Prototype < 0 || t.Prototype >= (int)protos.size())
					continue;
				add(protos[t.Prototype], cache.Worlds[i], t.Tint, Hash2(t.X * 97.0f, t.Z * 131.0f));
			}
		}

		// ---- 임포스터는 그리기 전에 굽는다 (굽기가 효과 변수를 바꾸므로)
		for (auto& kv : s_Batches)
			if (!kv.second.Lists[2].empty())
				GetImpostor(*kv.second.Desc, true);

		// ---- 패스 공통 값
		ID3D11DeviceContext* dc = Application::GetI()->GetDeviceContext();
		XMFLOAT4 viewPos(camPos.x, camPos.y, camPos.z, 0.0f);
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
		else
		{
			// 임포스터 그림자는 빛을 바라본다: 방향광 = 빛 쪽 방향, 스포트/점광 = 광원 위치
			XMFLOAT4 light(0, 1, 0, 0);
			if (auto* sl = v.Fx->GetVariableByName("gShadowLight")->AsVector(); sl && sl->IsValid())
				sl->GetFloatVector(&light.x);
			viewPos = XMFLOAT4(light.x, light.y, light.z, light.w > 0.5f ? 0.0f : 1.0f);
		}
		SetVec(v.ViewPos, viewPos);

		ComPtr<ID3D11DepthStencilState> prevDSS;
		UINT prevRef = 0;
		dc->OMGetDepthStencilState(prevDSS.GetAddressOf(), &prevRef);
		ComPtr<ID3D11RasterizerState> prevRS;
		dc->RSGetState(prevRS.GetAddressOf());
		dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		for (auto& kv : s_Batches)
		{
			Batch& b = kv.second;
			if (b.Lists[0].empty() && b.Lists[1].empty() && b.Lists[2].empty())
				continue;
			Impostor* imp = b.Lists[2].empty() ? nullptr : GetImpostor(*b.Desc, false);
			SetLook(v, *b.Desc, imp, 1.0f);
			for (int lod = 0; lod < 2; ++lod)
			{
				if (b.Lists[lod].empty())
					continue;
				auto mesh = GetMesh(*b.Desc, lod);
				ID3D11Buffer* inst = mesh && mesh->VB ? UploadInstances(dc, b.Lists[lod]) : nullptr;
				if (!inst)
					continue;
				const UINT count = (UINT)b.Lists[lod].size();
				const UINT strides[2] = { sizeof(TreeVertex), sizeof(InstanceData) }, offsets[2] = { 0, 0 };
				ID3D11Buffer* vbs[2] = { mesh->VB.Get(), inst };
				dc->IASetInputLayout(s_MeshLayout.Get());
				dc->IASetVertexBuffers(0, 2, vbs, strides, offsets);
				dc->IASetIndexBuffer(mesh->IB.Get(), DXGI_FORMAT_R32_UINT, 0);
				if (mesh->BarkIndexCount > 0)
				{
					v.Bark->GetPassByIndex(0)->Apply(0, dc);
					dc->DrawIndexedInstanced(mesh->BarkIndexCount, count, 0, 0, 0);
					++stats.DrawCalls;
				}
				if (mesh->LeafIndexCount > 0)
				{
					v.Leaf->GetPassByIndex(0)->Apply(0, dc);
					dc->DrawIndexedInstanced(mesh->LeafIndexCount, count, mesh->BarkIndexCount, 0, 0);
					++stats.DrawCalls;
				}
				(lod == 0 ? stats.Lod0 : stats.Lod1) += (int)count;
			}
			if (imp && !b.Lists[2].empty())
			{
				ID3D11Buffer* inst = UploadInstances(dc, b.Lists[2]);
				if (inst)
				{
					const UINT stride = sizeof(InstanceData), offset = 0;
					ID3D11Buffer* none = nullptr;
					UINT zero = 0;
					dc->IASetVertexBuffers(0, 1, &none, &zero, &zero);
					dc->IASetVertexBuffers(1, 1, &inst, &stride, &offset);
					dc->IASetInputLayout(s_ImpostorLayout.Get());
					v.Impostor->GetPassByIndex(0)->Apply(0, dc);
					dc->DrawInstanced(6, (UINT)b.Lists[2].size(), 0, 0);
					++stats.DrawCalls;
					stats.Billboards += (int)b.Lists[2].size();
				}
			}
		}
		// 다음 그리기(다른 효과)를 위해 두 번째 정점 슬롯을 비운다
		ID3D11Buffer* none = nullptr;
		UINT zero = 0;
		dc->IASetVertexBuffers(1, 1, &none, &zero, &zero);
		dc->OMSetDepthStencilState(prevDSS.Get(), prevRef);
		dc->RSSetState(prevRS.Get());
		if (pass == Pass::Main)
			s_Stats[editor ? 1 : 0] = stats;

		// 설정이 바뀌어 쓰이지 않는 묶음은 버린다 (TreeDesc 포인터가 사라졌을 수 있다)
		for (auto it = s_Batches.begin(); it != s_Batches.end();)
		{
			const bool empty = it->second.Lists[0].empty() && it->second.Lists[1].empty() && it->second.Lists[2].empty();
			it = empty ? s_Batches.erase(it) : std::next(it);
		}
	}
}
