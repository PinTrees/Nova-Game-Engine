#include "pch.h"
#include "WeatherState.h"
#include "RenderLayers.h"
#include "TreeRenderer.h"
#include "Tree.h"
#include "TreeTextures.h"
#include "Terrain.h"
#include "TerrainData.h"
#include "Transform.h"
#include "Effects.h"
#include "RenderManager.h"
#include "UMaterial.h"
#include "Profiler.h"
#include "SceneCulling.h"
#include "OcclusionCulling.h"
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
		ComPtr<GfxBuffer> VB, IB;
		uint32 BarkIndexCount = 0, LeafIndexCount = 0;
		int VertexCount = 0, BranchCount = 0, LeafCardCount = 0;
		Vec3 BoundsMin, BoundsMax;
		std::vector<XMFLOAT3> Positions;
		std::vector<uint32_t> Indices;
	};
	std::map<std::string, std::shared_ptr<GpuMesh>> s_Meshes;

	std::shared_ptr<GpuMesh> GetMesh(const TreeDesc& desc, int lod)
	{
		// 잎 카드 모양(잎 텍스처의 잎 길이에 따른 외곽)도 메시에 들어가므로 키에 넣는다
		const std::string key = desc.MeshKey() + (lod ? "#1" : "#0") + "#L" + std::to_string((int)roundf(desc.LeafLength * 100.0f));
		if (auto it = s_Meshes.find(key); it != s_Meshes.end())
			return it->second;

		const auto t0 = std::chrono::steady_clock::now();
		TreeMeshData data;
		TreeParams params = desc.Params;
		params.LeafLengthForHull = desc.LeafLength;
		TreeGenerator::Generate(params, data, lod);
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
		FxEffect* Fx = nullptr;
		FxTechnique* Bark = nullptr;
		FxTechnique* Leaf = nullptr;
		FxTechnique* Impostor = nullptr;
		FxTechnique* BarkBake = nullptr;
		FxTechnique* LeafBake = nullptr;
		FxVar* Wind = nullptr;
		FxVar* WindParams = nullptr;
		FxVar* BarkColor = nullptr;
		FxVar* BarkParams = nullptr;
		FxVar* MossColor = nullptr;
		FxVar* LeafColor = nullptr;
		FxVar* LeafColor2 = nullptr;
		FxVar* LeafParams = nullptr;
		FxVar* ImpostorParams = nullptr;
		FxVar* ViewPos = nullptr;
		FxVar* LeafTex = nullptr;
		FxVar* BarkTex = nullptr;
		FxVar* ImpAlbedo = nullptr;
		FxVar* ImpNormal = nullptr;

		void Bind(FxEffect* fx, const char* bark, const char* leaf, const char* impostor)
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

	void SetVec(FxVar* v, const XMFLOAT4& f)
	{
		if (v && v->IsValid())
			v->SetFloatVector(&f.x);
	}
	void SetSrv(FxVar* v, GfxShaderResourceView* srv)
	{
		if (v && v->IsValid())
			v->SetResource(srv);
	}
	void SetMatrix(FxEffect* fx, const char* name, CXMMATRIX m)
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

	ComPtr<GfxInputLayout> s_MeshLayout, s_ImpostorLayout;
	ComPtr<GfxBuffer> s_InstanceBuffer;
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

	GfxBuffer* UploadInstances(GfxContext* dc, const std::vector<InstanceData>& data)
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
		ComPtr<GfxShaderResourceView> Albedo, Normal;
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
		GfxContext* dc = Application::GetI()->GetDeviceContext();
		const UINT W = kFrames * kFrameRes, H = kFrameRes;

		ComPtr<GfxTexture2D> tex[2], depth;
		ComPtr<GfxRenderTargetView> rtv[2];
		ComPtr<GfxDepthStencilView> dsv;
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
		ComPtr<GfxShaderResourceView> srv[2];
		for (int i = 0; i < 2; ++i)
			device->CreateShaderResourceView(tex[i].Get(), nullptr, srv[i].GetAddressOf());
		td.MipLevels = 1; td.MiscFlags = 0; td.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		if (FAILED(device->CreateTexture2D(&td, nullptr, depth.GetAddressOf())))
			return false;
		device->CreateDepthStencilView(depth.Get(), nullptr, dsv.GetAddressOf());

		// 지금 상태 보관
		ComPtr<GfxRenderTargetView> oldRtv[2];
		ComPtr<GfxDepthStencilView> oldDsv;
		dc->OMGetRenderTargets(2, oldRtv[0].GetAddressOf(), oldDsv.GetAddressOf());
		UINT vpCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
		D3D11_VIEWPORT oldVp[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
		dc->RSGetViewports(&vpCount, oldVp);
		ComPtr<GfxBlendState> oldBlend; float oldFactor[4]; UINT oldMask = 0;
		dc->OMGetBlendState(oldBlend.GetAddressOf(), oldFactor, &oldMask);
		ComPtr<GfxDepthStencilState> oldDss; UINT oldRef = 0;
		dc->OMGetDepthStencilState(oldDss.GetAddressOf(), &oldRef);
		ComPtr<GfxRasterizerState> oldRs;
		dc->RSGetState(oldRs.GetAddressOf());

		GfxRenderTargetView* targets[2] = { rtv[0].Get(), rtv[1].Get() };
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
		GfxBuffer* inst = UploadInstances(dc, one);
		const UINT strides[2] = { sizeof(TreeVertex), sizeof(InstanceData) }, offsets[2] = { 0, 0 };
		GfxBuffer* vbs[2] = { mesh->VB.Get(), inst };
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
		GfxRenderTargetView* restore[2] = { oldRtv[0].Get(), oldRtv[1].Get() };
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
		std::vector<XMFLOAT4> Spheres[3];      // 같은 순서의 경계 구 (오클루전 컬링 — 본 패스에서 GPU 가 거른다)
		bool Used = false;
	};
	std::unordered_map<size_t, Batch> s_Batches;

	// 화면 하나 동안 쓰는 나무 목록 (BeginView 뒤 첫 DrawAll 이 만든다): 패스·그림자 조각마다 다시 훑지 않게
	// 경계 구, 카메라 거리(크기 배율로 나눈 값)를 미리 구해 둔다. 거리상 안 그리는 나무는 넣지 않는다
	struct TreeRecord
	{
		Batch* B;
		XMFLOAT4X4 World;
		Vec3 Center;
		float Radius;
		float Dist;
		float Tint, Phase;
		bool CastShadows;
		uint32 LayerBit;   // 그 GameObject (Tree 또는 지형) 의 레이어 — Culling Mask
	};
	std::vector<TreeRecord> s_Records;
	uint32 s_RecordLayer = 0xFFFFFFFFu;
	bool s_RecordsValid = false;
	bool s_RecordsEditor = false;
	uint32_t s_RecordsFrame = 0;
	// 깊이 사전 패스의 LOD 목록을 같은 화면의 본 패스가 그대로 쓴다 (같은 카메라 → 같은 절두체·LOD)
	uint32_t s_ViewSerial = 0;
	uint32_t s_ListsSerial = ~0u;
	int s_ListsPass = -1;
	bool s_ListsEditor = false;
	int s_ListsTrees = 0;

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

	void CollectMemory(std::vector<MemoryStats::Item>& gpu, std::vector<MemoryStats::Item>& cpu)
	{
		for (const auto& [key, mesh] : s_Meshes)
		{
			if (!mesh)
				continue;
			const std::string name = "Tree mesh " + std::to_string(std::hash<std::string>()(key) % 100000) + (key.find("#1") != std::string::npos ? " (LOD1)" : " (LOD0)");
			gpu.push_back({ name, MemoryStats::ResourceBytes(mesh->VB.Get()) + MemoryStats::ResourceBytes(mesh->IB.Get()) });
			cpu.push_back({ name + " pick data", mesh->Positions.capacity() * sizeof(XMFLOAT3) + mesh->Indices.capacity() * sizeof(uint32_t) });
		}
		for (const auto& [key, imp] : s_Impostors)
			if (imp.Albedo)
				gpu.push_back({ "Impostor " + std::to_string(key % 100000), MemoryStats::ViewBytes(imp.Albedo.Get()) + MemoryStats::ViewBytes(imp.Normal.Get()) });
		TreeTextures::CollectMemory(gpu);
	}

	void BeginView()
	{
		s_RecordsValid = false;
		++s_ViewSerial;
	}

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
		PROFILE_SCOPE("Trees");
		PROFILE_GPU("Trees");
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
		// 그림자 LOD: 잎 카드는 그림자 맵에 여러 겹 칠해져(가까운 숲 = 그림자 픽셀 수천만) GPU 를 가장 많이 쓴다.
		// LOD1 은 카드 수가 절반이지만 카드가 커서 칠하는 면적은 거의 같다 → 픽셀을 줄이는 건 임포스터(빛을 바라보는 사각형 한 장)뿐.
		// 임포스터 한 프레임(256 px)의 텍셀이 캐스케이드 텍셀의 3.5 배 이하면(그림자 PCF 흐림과 비슷) 임포스터로 그린다
		constexpr float kShadowImpostorTexels = 900.0f;   // 나무 지름 / 캐스케이드 텍셀
		const float shadowTexel = shadow ? rm->ShadowTexelWorld : 0.0f;

		Stats stats;
		const int lodPass = shadow ? kShadow : kMain;   // 본 패스와 깊이 패스는 같은 목록

		// ---- 화면 하나에 한 번: 나무 목록 (종류 준비, 경계 구, 거리)
		if (!s_RecordsValid || s_RecordsEditor != editorView || s_RecordsFrame != SceneCulling::FrameIndex())
		{
			PROFILE_SCOPE("Trees.Collect");
			s_Records.clear();
			for (auto& kv : s_Batches)
				kv.second.Used = false;
			struct Proto
			{
				const TreeDesc* Desc = nullptr;
				Batch* B = nullptr;
				Vec3 BoundsCenter;
				float BoundsRadius = 0.0f;
			};
			auto prepare = [&](const TreeDesc& desc) {
				Proto p;
				auto mesh = GetMesh(desc, 0);
				if (!mesh || mesh->VertexCount == 0)
					return p;
				p.Desc = &desc;
				p.B = &s_Batches[desc.Hash()];   // 노드 기반 맵: 나중에 넣어도 이 포인터는 그대로
				p.B->Desc = &desc;
				p.B->Used = true;
				p.BoundsCenter = (mesh->BoundsMin + mesh->BoundsMax) * 0.5f;
				p.BoundsRadius = (mesh->BoundsMax - mesh->BoundsMin).Length() * 0.5f;
				return p;
			};
			auto record = [&](const Proto& proto, const XMFLOAT4X4& world, float tint, float phase) {
				if (proto.B == nullptr)
					return;
				const Vec3 pos(world._41, world._42, world._43);
				const float sx = Vec3(world._11, world._12, world._13).Length();
				const float sy = Vec3(world._21, world._22, world._23).Length();
				TreeRecord r;
				r.Dist = (pos - camPos).Length() / (std::max)(sy, 0.05f);
				if (r.Dist >= proto.Desc->CullDistance)
					return;
				const Vec3& bc = proto.BoundsCenter;
				r.B = proto.B;
				r.World = world;
				r.Center = pos + Vec3(bc.x * sx, bc.y * sy, bc.z * sx);
				r.Radius = proto.BoundsRadius * (std::max)(sx, sy);
				r.Tint = tint;
				r.Phase = phase;
				r.CastShadows = proto.Desc->CastShadows;
				r.LayerBit = s_RecordLayer;
				s_Records.push_back(r);
			};
			// Tree 컴포넌트
			for (Tree* tree : Tree::All())
			{
				if (!tree->IsDrawable())
					continue;
				XMFLOAT4X4 world;
				XMStoreFloat4x4(&world, tree->GetGameObject()->GetTransform()->GetWorldMatrix());
				s_RecordLayer = 1u << (tree->GetGameObject()->GetLayerIndex() & 31);
				record(prepare(tree->Desc), world, 0.5f, Hash2(world._41, world._43));
			}
			// 지형에 칠한 나무
			for (Terrain* terrain : Terrain::GetActiveTerrains())
			{
				auto data = terrain->GetTerrainData();
				if (!data || !terrain->IsEnabled() || !terrain->GetDraw() || (terrain->GetGameObject() && !terrain->GetGameObject()->IsActiveInHierarchy()))
					continue;
				if (data->TreeInstances.empty() || data->TreePrototypes.empty())
					continue;
				const TerrainCache& cache = TerrainWorlds(*data, terrain->GetPosition());
				s_RecordLayer = 1u << ((terrain->GetGameObject() ? terrain->GetGameObject()->GetLayerIndex() : 0) & 31);
				std::vector<Proto> protos;
				for (const TreeDesc& d : data->TreePrototypes)
					protos.push_back(prepare(d));
				for (size_t i = 0; i < data->TreeInstances.size(); ++i)
				{
					const TerrainTreeInstance& t = data->TreeInstances[i];
					if (t.Prototype < 0 || t.Prototype >= (int)protos.size())
						continue;
					record(protos[t.Prototype], cache.Worlds[i], t.Tint, Hash2(t.X * 97.0f, t.Z * 131.0f));
				}
			}
			// 설정이 바뀌어 쓰이지 않는 묶음은 버린다 (TreeDesc 포인터가 사라졌을 수 있다). 다른 노드 포인터는 그대로
			for (auto it = s_Batches.begin(); it != s_Batches.end();)
				it = it->second.Used ? std::next(it) : s_Batches.erase(it);
			s_RecordsValid = true;
			s_RecordsEditor = editorView;
			s_RecordsFrame = SceneCulling::FrameIndex();
			s_ListsSerial = ~0u;
		}

		// ---- 이번 패스: 절두체 → LOD (+ 섞기). 본 패스는 같은 화면의 깊이 패스 목록을 그대로 쓴다
		const bool reuse = !shadow && s_ListsSerial == s_ViewSerial && s_ListsEditor == editor && s_ListsPass == lodPass;
		if (reuse)
			stats.Trees = s_ListsTrees;
		else
		{
			for (auto& kv : s_Batches)
			{
				for (auto& list : kv.second.Lists)
					list.clear();
				for (auto& list : kv.second.Spheres)
					list.clear();
			}
			for (const TreeRecord& r : s_Records)
			{
				if (shadow && !r.CastShadows)
					continue;
				if (!(r.LayerBit & RenderLayers::ActiveMask()))
					continue;   // Camera / Light 의 Culling Mask
				bool inside = true;
				for (int i = 0; i < planeCount && inside; ++i)
					inside = planes[i].x * r.Center.x + planes[i].y * r.Center.y + planes[i].z * r.Center.z + planes[i].w >= -r.Radius;
				if (!inside)
					continue;
				const TreeDesc& desc = *r.B->Desc;
				const float d = r.Dist;
				Batch& b = *r.B;
				auto emit = [&](int lod, float t, float side) {
					InstanceData inst;
					inst.World = r.World;
					inst.Extra = XMFLOAT4(r.Tint, r.Phase, t, side);
					b.Lists[lod].push_back(inst);
					b.Spheres[lod].push_back(XMFLOAT4(r.Center.x, r.Center.y, r.Center.z, r.Radius * 1.1f));   // 바람에 흔들리는 만큼 넉넉히
				};
				const float l1 = desc.LodDistance, l2 = desc.BillboardDistance, lc = desc.CullDistance;
				const float b1 = l1 * 0.1f, b2 = l2 * 0.1f, bc2 = lc * 0.05f;
				++stats.Trees;
				if (shadow)
				{
					int lod = d < l1 ? 0 : (d < l2 ? 1 : 2);
					if (shadowTexel > 0.0f && 2.0f * r.Radius / shadowTexel <= kShadowImpostorTexels)
						lod = 2;
					emit(lod, 0.0f, 0.0f);
					continue;
				}
				if (d < l1 - b1 * 0.5f) emit(0, 0, 0);
				else if (d < l1 + b1 * 0.5f) { const float t = (d - (l1 - b1 * 0.5f)) / b1; emit(0, t, 0); emit(1, t, 1); }
				else if (d < l2 - b2 * 0.5f) emit(1, 0, 0);
				else if (d < l2 + b2 * 0.5f) { const float t = (d - (l2 - b2 * 0.5f)) / b2; emit(1, t, 0); emit(2, t, 1); }
				else if (d < lc - bc2) emit(2, 0, 0);
				else emit(2, (d - (lc - bc2)) / bc2, 0);   // 멀어지며 사라짐
			}
			s_ListsSerial = s_ViewSerial;
			s_ListsEditor = editor;
			s_ListsPass = lodPass;
			s_ListsTrees = stats.Trees;
		}

		// ---- 임포스터는 그리기 전에 굽는다 (굽기가 효과 변수를 바꾸므로)
		for (auto& kv : s_Batches)
			if (!kv.second.Lists[2].empty())
				GetImpostor(*kv.second.Desc, true);

		// ---- 패스 공통 값
		GfxContext* dc = Application::GetI()->GetDeviceContext();
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

		ComPtr<GfxDepthStencilState> prevDSS;
		UINT prevRef = 0;
		dc->OMGetDepthStencilState(prevDSS.GetAddressOf(), &prevRef);
		ComPtr<GfxRasterizerState> prevRS;
		dc->RSGetState(prevRS.GetAddressOf());
		dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		// 본 패스 + 이 뷰의 Hi-Z 가 있으면 (오클루전 컬링) 가려진 나무를 GPU 가 뺀다
		const bool occlusion = pass == Pass::Main && OcclusionCulling::HasHiZ();
		for (auto& kv : s_Batches)
		{
			Batch& b = kv.second;
			if (b.Lists[0].empty() && b.Lists[1].empty() && b.Lists[2].empty())
				continue;
			Impostor* imp = b.Lists[2].empty() ? nullptr : GetImpostor(*b.Desc, false);
			SetLook(v, *b.Desc, imp, (std::max)(0.0f, WeatherState::Get().WindStrength));   // 날씨 바람 (돌풍) 배율
			for (int lod = 0; lod < 2; ++lod)
			{
				if (b.Lists[lod].empty())
					continue;
				auto mesh = GetMesh(*b.Desc, lod);
				if (!mesh || !mesh->VB)
					continue;
				const UINT count = (UINT)b.Lists[lod].size();
				// 오클루전 컬링 (본 패스): 이 뷰의 Hi-Z 로 가려진 나무를 GPU 가 빼고 간접 그리기
				if (occlusion)
				{
					const OcclusionCulling::ListDraw draws[2] = {
						{ { mesh->BarkIndexCount, 0, 0, 0, 0 }, true },
						{ { mesh->LeafIndexCount, 0, mesh->BarkIndexCount, 0, 0 }, true } };
					const int list = OcclusionCulling::CullList(dc, b.Lists[lod].data(), sizeof(InstanceData), count, &b.Spheres[lod][0].x, draws, 2);
					if (list >= 0)
					{
						const UINT stride = sizeof(TreeVertex), offset = 0;
						GfxBuffer* vb = mesh->VB.Get();
						dc->IASetInputLayout(s_MeshLayout.Get());
						dc->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
						dc->IASetIndexBuffer(mesh->IB.Get(), DXGI_FORMAT_R32_UINT, 0);
						if (mesh->BarkIndexCount > 0)
						{
							PROFILE_GPU(lod == 0 ? "LOD0 Bark" : "LOD1 Bark");
							v.Bark->GetPassByIndex(0)->Apply(0, dc);
							OcclusionCulling::DrawList(dc, list, 0, 1);
							++stats.DrawCalls;
						}
						if (mesh->LeafIndexCount > 0)
						{
							PROFILE_GPU(lod == 0 ? "LOD0 Leaves" : "LOD1 Leaves");
							v.Leaf->GetPassByIndex(0)->Apply(0, dc);
							OcclusionCulling::DrawList(dc, list, 1, 1);
							++stats.DrawCalls;
						}
						(lod == 0 ? stats.Lod0 : stats.Lod1) += (int)count;
						continue;
					}
				}
				GfxBuffer* inst = UploadInstances(dc, b.Lists[lod]);
				if (!inst)
					continue;
				const UINT strides[2] = { sizeof(TreeVertex), sizeof(InstanceData) }, offsets[2] = { 0, 0 };
				GfxBuffer* vbs[2] = { mesh->VB.Get(), inst };
				dc->IASetInputLayout(s_MeshLayout.Get());
				dc->IASetVertexBuffers(0, 2, vbs, strides, offsets);
				dc->IASetIndexBuffer(mesh->IB.Get(), DXGI_FORMAT_R32_UINT, 0);
				if (mesh->BarkIndexCount > 0)
				{
					PROFILE_GPU(lod == 0 ? "LOD0 Bark" : "LOD1 Bark");   // Profiler 창: 단계별 GPU 시간
					v.Bark->GetPassByIndex(0)->Apply(0, dc);
					dc->DrawIndexedInstanced(mesh->BarkIndexCount, count, 0, 0, 0);
					++stats.DrawCalls;
				}
				if (mesh->LeafIndexCount > 0)
				{
					PROFILE_GPU(lod == 0 ? "LOD0 Leaves" : "LOD1 Leaves");
					v.Leaf->GetPassByIndex(0)->Apply(0, dc);
					dc->DrawIndexedInstanced(mesh->LeafIndexCount, count, mesh->BarkIndexCount, 0, 0);
					++stats.DrawCalls;
				}
				(lod == 0 ? stats.Lod0 : stats.Lod1) += (int)count;
			}
			if (imp && !b.Lists[2].empty() && occlusion)
			{
				const OcclusionCulling::ListDraw draws[1] = { { { 6, 0, 0, 0, 0 }, false } };
				const int list = OcclusionCulling::CullList(dc, b.Lists[2].data(), sizeof(InstanceData), (uint32_t)b.Lists[2].size(), &b.Spheres[2][0].x, draws, 1);
				if (list >= 0)
				{
					PROFILE_GPU("Impostors");
					GfxBuffer* none = nullptr;
					UINT zero = 0;
					dc->IASetVertexBuffers(0, 1, &none, &zero, &zero);
					dc->IASetInputLayout(s_ImpostorLayout.Get());
					v.Impostor->GetPassByIndex(0)->Apply(0, dc);
					OcclusionCulling::DrawList(dc, list, 0, 1);
					++stats.DrawCalls;
					stats.Billboards += (int)b.Lists[2].size();
					continue;
				}
			}
			if (imp && !b.Lists[2].empty())
			{
				GfxBuffer* inst = UploadInstances(dc, b.Lists[2]);
				if (inst)
				{
					PROFILE_GPU("Impostors");
					const UINT stride = sizeof(InstanceData), offset = 0;
					GfxBuffer* none = nullptr;
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
		GfxBuffer* none = nullptr;
		UINT zero = 0;
		dc->IASetVertexBuffers(1, 1, &none, &zero, &zero);
		dc->OMSetDepthStencilState(prevDSS.Get(), prevRef);
		dc->RSSetState(prevRS.Get());
		if (pass == Pass::Main)
			s_Stats[editor ? 1 : 0] = stats;

	}
}
