#include "pch.h"
#include "VfxRuntime.h"
#include "VisualEffect.h"
#include "ParticleTextures.h"
#include "RenderLayers.h"
#include "SpriteBatch.h"
#include "Effects.h"
#include "Profiler.h"
#include "VfxMeshes.h"
#include <map>

// GPU 버퍼 (Visual Effect 하나 = 시스템마다 한 벌)
namespace
{
	using Microsoft::WRL::ComPtr;

	struct Buf
	{
		ComPtr<GfxBuffer> B;
		ComPtr<GfxShaderResourceView> Srv;
		ComPtr<GfxUnorderedAccessView> Uav;
		UINT Bytes = 0;
	};

	constexpr UINT kStride = 112;       // 58. VFX.fx 의 kStride (96 + 사용자 속성 16)
	constexpr UINT kMeshVertexBytes = 24;   // Output Mesh 정점: 자리 · 법선
	constexpr UINT kEventBytes = 48;
	constexpr UINT kMaxEvents = 4096;   // 부모 시스템이 한 프레임에 보내는 GPU Event 의 위 한계 (죽음 · Rate 따로)
	constexpr UINT kTrailVertBytes = 64;
	constexpr UINT kStateBytes = 48;    // 0 칸 카운터, 4 살아 있는 수, 8..32 경계 (순서 키), 32 최대 크기
}

struct VfxGpuSystem
{
	Buf Particles, State, Events, Program;
	Buf Trail, TrailVerts;              // 꼬리: 기록 · 띠 조각 (정점 버퍼)
	Buf SortKeys, Sorted;               // 정렬: 키 · 정렬된 사본 (정점 버퍼)
	Buf Sdf;                            // Collide with SDF 의 거리장 (칸마다 float)
	const void* SdfSource = nullptr;    // 올린 거리장 (Vfx::BakeSdf 결과 — 바뀌면 다시 올린다)
	UINT Capacity = 0, EventCapacity = 0, TrailPoints = 0, SortCount = 0;
	bool NeedsReset = true;
	bool TrailReady = false;            // 이번 프레임 띠 조각을 만들었다
	uint64 ProgramKey = 0;              // 블록 목록이 바뀌었는가 (에셋 · 속성 · 변환)
	Vfx::Encoded Enc;
	ComPtr<GfxBuffer> Staging[3];       // 살아 있는 수 · 경계 (몇 프레임 뒤에 읽는다)
	ComPtr<GfxQuery> Ready[3];
	bool Pending[3] = {};
	int Next = 0;
	int Alive = 0;
	bool HasBounds = false;             // 시뮬레이션 공간 (Local 이면 Visual Effect 기준) 의 경계
	Vec3 BoundsMin, BoundsMax;
	float MaxSize = 0.0f;
};

struct VfxGpu
{
	std::vector<VfxGpuSystem> Systems;
	float Time = 0.0f;
	uint64 DrawnMark = 0;               // 마지막으로 어느 뷰에든 그린 프레임 (VfxRuntime::MarkFrame 번호)
};

void VfxGpuDeleter::operator()(VfxGpu* gpu) const { delete gpu; }

namespace
{
	std::unique_ptr<Effect> s_Effect;
	int s_State = 0;   // 0 아직, 1 됨, -1 실패
	std::string s_Error;
	FxPass *s_Reset = nullptr, *s_Spawn = nullptr, *s_Update = nullptr, *s_Alpha = nullptr, *s_Add = nullptr;
	FxPass *s_TrailBuild = nullptr, *s_TrailAlpha = nullptr, *s_TrailAdd = nullptr;
	FxPass *s_SortKeys = nullptr, *s_SortLocal = nullptr, *s_SortGlobal = nullptr, *s_SortGather = nullptr;
	FxPass *s_Opaque = nullptr, *s_MeshAlpha = nullptr, *s_MeshAdd = nullptr, *s_MeshOpaque = nullptr;
	ComPtr<GfxInputLayout> s_Layout, s_TrailLayout, s_MeshLayout;
	struct Vars
	{
		FxVar *ViewProj, *World, *CamRight, *Time, *CamUp, *Dt, *CamPos, *CamFwd, *Capacity, *SpawnCount, *FromEvents, *EventCapacity, *EmitOnDie, *EmitRate,
			*InitStart, *InitCount, *UpdateStart, *UpdateCount, *Seed, *LocalSpace, *TrailPoints, *TrailInterval, *TrailWidth, *SortCount, *SortK, *SortJ,
			*Output0, *Output1, *DepthParams, *WorldInv, *CollViewProj, *CollInvViewProj, *CollParams, *CollCam, *CollViewport, *SunDir, *SunColor, *Ambient;
		FxVar *Program, *Particles, *State, *Events, *EventsIn, *Trail, *TrailVerts, *SortKeys, *Sorted, *Texture, *SceneDepth, *CollDepth, *Sdf;
	} s_V = {};
	uint64 s_FrameMark = 1, s_SimulatedMark = 0;
	// 쓰지 않는 칸에 묶는 작은 버퍼 (Vulkan · GL 은 셰이더가 선언한 버퍼가 모두 묶여 있어야 한다).
	//  칸마다 따로 — 같은 UAV 를 두 칸에 묶으면 DirectX 11 이 묶기 전체를 무시한다 (Update 가 아무것도 쓰지 않았다)
	Buf s_Dummy[3];
	int s_LastDraws = 0, s_LastSystems = 0, s_LastCulled = 0;
	// Output Mesh 의 기본 메시 (크기 1, 가운데 0)
	struct MeshBuf { ComPtr<GfxBuffer> Vb, Ib; UINT Count = 0; bool Tried = false; };
	std::map<std::string, MeshBuf> s_Meshes;
	// 깊이 버퍼 충돌: 프레임의 첫 뷰 (시뮬레이션하는 뷰) 의 장면 깊이
	struct CollisionView { bool On = false; GfxShaderResourceView* Depth = nullptr; };

	GfxDevice* Dev() { return Application::GetI()->GetDevice(); }

	bool Init()
	{
		if (s_State != 0)
			return s_State > 0;
		s_State = -1;
		s_Effect = std::make_unique<Effect>(Dev(), L"../Shaders/58. VFX.fx");
		FxEffect* fx = s_Effect->GetFX();
		if (fx == nullptr)
		{
			s_Error = "58. VFX.fx failed to load";
			EditorLog::Write("VFX", "%s - Visual Effects are not drawn", s_Error.c_str());
			s_Effect.reset();
			return false;
		}
		auto pass = [&](const char* name) -> FxPass* {
			FxTechnique* t = fx->GetTechniqueByName(name);
			FxPass* p = t && t->IsValid() ? t->GetPassByIndex(0) : nullptr;
			return p && p->IsValid() ? p : nullptr;
		};
		s_Reset = pass("ResetTech");
		s_Spawn = pass("SpawnTech");
		s_Update = pass("UpdateTech");
		s_Alpha = pass("AlphaTech");
		s_Add = pass("AdditiveTech");
		s_TrailBuild = pass("TrailTech");
		s_TrailAlpha = pass("TrailAlphaTech");
		s_TrailAdd = pass("TrailAdditiveTech");
		s_SortKeys = pass("SortKeysTech");
		s_SortLocal = pass("SortLocalTech");
		s_SortGlobal = pass("SortGlobalTech");
		s_SortGather = pass("SortGatherTech");
		s_Opaque = pass("OpaqueTech");
		s_MeshAlpha = pass("MeshAlphaTech");
		s_MeshAdd = pass("MeshAdditiveTech");
		s_MeshOpaque = pass("MeshOpaqueTech");
		if (!s_Reset || !s_Spawn || !s_Update || !s_Alpha || !s_Add)
		{
			s_Error = "58. VFX.fx: a technique is missing or failed";
			EditorLog::Write("VFX", "%s", s_Error.c_str());
			s_Effect.reset();
			return false;
		}
		if (!s_TrailBuild || !s_TrailAlpha || !s_TrailAdd || !s_SortKeys || !s_SortLocal || !s_SortGlobal || !s_SortGather)
			EditorLog::Write("VFX", "58. VFX.fx: trail or sort techniques failed - trails and sorting are off");
		if (!s_Opaque || !s_MeshAlpha || !s_MeshAdd || !s_MeshOpaque)
			EditorLog::Write("VFX", "58. VFX.fx: mesh or opaque techniques failed - mesh outputs are not drawn");
		D3DX11_PASS_DESC pd = {};
		s_Add->GetDesc(&pd);
		const D3D11_INPUT_ELEMENT_DESC desc[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
			{ "VELOCITY", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
			{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
		};
		if (FAILED(Dev()->CreateInputLayout(desc, _countof(desc), pd.pIAInputSignature, pd.IAInputSignatureSize, s_Layout.GetAddressOf())))
		{
			s_Error = "VFX input layout failed";
			EditorLog::Write("VFX", "%s", s_Error.c_str());
			s_Effect.reset();
			return false;
		}
		if (s_TrailAdd)
		{
			D3DX11_PASS_DESC td = {};
			s_TrailAdd->GetDesc(&td);
			const D3D11_INPUT_ELEMENT_DESC trail[] = {
				{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
				{ "POSITION", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
				{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
				{ "TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
			};
			if (FAILED(Dev()->CreateInputLayout(trail, _countof(trail), td.pIAInputSignature, td.IAInputSignatureSize, s_TrailLayout.GetAddressOf())))
				EditorLog::Write("VFX", "trail input layout failed - trails are not drawn");
		}
		if (s_MeshAdd)
		{
			// 0 번 = 메시 정점 (자리 · 법선), 1 번 = 파티클 (인스턴스)
			D3DX11_PASS_DESC md = {};
			s_MeshAdd->GetDesc(&md);
			const D3D11_INPUT_ELEMENT_DESC mesh[] = {
				{ "POSITION", 1, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
				{ "VELOCITY", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
				{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
				{ "TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 48, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
			};
			if (FAILED(Dev()->CreateInputLayout(mesh, _countof(mesh), md.pIAInputSignature, md.IAInputSignatureSize, s_MeshLayout.GetAddressOf())))
				EditorLog::Write("VFX", "mesh input layout failed - mesh outputs are not drawn");
		}
		auto var = [&](const char* n) { return fx->GetVariableByName(n); };
		s_V = { var("gViewProj"), var("gWorld"), var("gCamRight"), var("gTime"), var("gCamUp"), var("gDt"), var("gCamPos"), var("gCamFwd"), var("gCapacity"),
			var("gSpawnCount"), var("gFromEvents"), var("gEventCapacity"), var("gEmitOnDie"), var("gEmitRate"), var("gInitStart"), var("gInitCount"),
			var("gUpdateStart"), var("gUpdateCount"), var("gSeed"), var("gLocalSpace"), var("gTrailPoints"), var("gTrailInterval"), var("gTrailWidth"),
			var("gSortCount"), var("gSortK"), var("gSortJ"), var("gOutput0"), var("gOutput1"), var("gDepthParams"),
			var("gWorldInv"), var("gCollViewProj"), var("gCollInvViewProj"), var("gCollParams"), var("gCollCam"), var("gCollViewport"), var("gSunDir"), var("gSunColor"), var("gAmbient"),
			var("gProgram"), var("gParticles"), var("gState"), var("gEvents"), var("gEventsIn"), var("gTrail"), var("gTrailVerts"), var("gSortKeys"), var("gSorted"),
			var("gTexture"), var("gSceneDepth"), var("gCollDepth"), var("gSdf") };
		s_State = 1;
		EditorLog::Write("VFX", "58. VFX.fx loaded");
		return true;
	}

	void SetU(FxVar* v, uint32_t x) { if (v && v->IsValid()) v->SetRawValue(&x, 0, 4); }
	void SetF(FxVar* v, float x) { if (v && v->IsValid()) v->SetRawValue(&x, 0, 4); }
	void SetV(FxVar* v, const float* x) { if (v && v->IsValid()) v->SetRawValue(x, 0, 16); }
	void SetV3(FxVar* v, const Vec3& x) { if (v && v->IsValid()) v->SetRawValue(&x.x, 0, 12); }
	void SetM(FxVar* v, const Matrix& m) { if (v && v->IsValid()) v->SetMatrix(&m._11); }

	// GPU 가 쓰는 raw 버퍼 (bind 에 정점 버퍼 등). 처음은 0 (수명 0 = 빈 칸)
	bool EnsureRaw(Buf& b, UINT bytes, UINT bind, bool srv, bool* recreated = nullptr)
	{
		bytes = (std::max)((bytes + 15) & ~15u, 16u);
		if (b.B && b.Bytes == bytes)
			return true;
		Buf n;
		D3D11_BUFFER_DESC d = {};
		d.ByteWidth = bytes;
		d.Usage = D3D11_USAGE_DEFAULT;
		d.BindFlags = bind | D3D11_BIND_UNORDERED_ACCESS | (srv ? D3D11_BIND_SHADER_RESOURCE : 0);
		d.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
		std::vector<uint8_t> zero(bytes, 0);
		D3D11_SUBRESOURCE_DATA init = { zero.data(), 0, 0 };
		if (FAILED(Dev()->CreateBuffer(&d, &init, n.B.GetAddressOf())))
			return false;
		D3D11_UNORDERED_ACCESS_VIEW_DESC u = {};
		u.Format = DXGI_FORMAT_R32_TYPELESS;
		u.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		u.Buffer.NumElements = bytes / 4;
		u.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
		if (FAILED(Dev()->CreateUnorderedAccessView(n.B.Get(), &u, n.Uav.GetAddressOf())))
			return false;
		if (srv)
		{
			D3D11_SHADER_RESOURCE_VIEW_DESC s = {};
			s.Format = DXGI_FORMAT_R32_TYPELESS;
			s.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
			s.BufferEx.NumElements = bytes / 4;
			s.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
			if (FAILED(Dev()->CreateShaderResourceView(n.B.Get(), &s, n.Srv.GetAddressOf())))
				return false;
		}
		n.Bytes = bytes;
		b = n;
		if (recreated) *recreated = true;
		return true;
	}

	// 블록 목록 (CPU 가 쓰는 float4 구조 버퍼, 커지기만 함)
	bool UploadProgram(GfxContext* dc, Buf& b, const std::vector<std::array<float, 4>>& data)
	{
		const UINT bytes = (UINT)((std::max)(data.size(), (size_t)1) * 16);
		if (!b.B || b.Bytes < bytes)
		{
			Buf n;
			D3D11_BUFFER_DESC d = {};
			d.ByteWidth = (std::max)(bytes, 256u);
			d.Usage = D3D11_USAGE_DYNAMIC;
			d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			d.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
			d.StructureByteStride = 16;
			if (FAILED(Dev()->CreateBuffer(&d, nullptr, n.B.GetAddressOf())))
				return false;
			D3D11_SHADER_RESOURCE_VIEW_DESC s = {};
			s.Format = DXGI_FORMAT_UNKNOWN;
			s.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
			s.Buffer.NumElements = d.ByteWidth / 16;
			if (FAILED(Dev()->CreateShaderResourceView(n.B.Get(), &s, n.Srv.GetAddressOf())))
				return false;
			n.Bytes = d.ByteWidth;
			b = n;
		}
		D3D11_MAPPED_SUBRESOURCE m;
		if (FAILED(dc->Map(b.B.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
			return false;
		memcpy(m.pData, data.data(), data.size() * 16);
		dc->Unmap(b.B.Get(), 0);
		return true;
	}

	void WriteBytes(GfxContext* dc, Buf& b, UINT from, const void* data, UINT bytes)
	{
		const D3D11_BOX box = { from, 0, 0, from + bytes, 1, 1 };
		dc->UpdateSubresource(b.B.Get(), 0, &box, data, 0, 0);
	}

	void ZeroBytes(GfxContext* dc, Buf& b, UINT from, UINT to)
	{
		const uint32_t zero[8] = {};
		WriteBytes(dc, b, from, zero, to - from);
	}

	Buf& Dummy(int i)
	{
		if (!s_Dummy[i].B)
			EnsureRaw(s_Dummy[i], 64, 0, true);
		return s_Dummy[i];
	}

	using Srvs = std::initializer_list<std::pair<FxVar*, GfxShaderResourceView*>>;
	using Uavs = std::initializer_list<std::pair<FxVar*, GfxUnorderedAccessView*>>;

	// 커널 하나: 뷰를 넣고 돌린 뒤 바로 푼다 (다음 단계가 같은 버퍼를 정점 버퍼 · SRV 로 쓴다)
	void RunGroups(GfxContext* dc, FxPass* pass, Srvs srvs, Uavs uavs, UINT groups)
	{
		if (groups == 0)
			return;
		for (const auto& [var, view] : srvs) if (var && var->IsValid()) var->SetResource(view);
		for (const auto& [var, view] : uavs) if (var && var->IsValid()) var->SetUnorderedAccessView(view);
		pass->Apply(0, dc);
		dc->Dispatch(groups, 1, 1);
		for (const auto& [var, view] : srvs) if (var && var->IsValid()) var->SetResource(nullptr);
		for (const auto& [var, view] : uavs) if (var && var->IsValid()) var->SetUnorderedAccessView(nullptr);
		GfxShaderResourceView* ns[8] = {};
		GfxUnorderedAccessView* nu[8] = {};
		dc->CSSetShaderResources(0, 8, ns);
		dc->CSSetUnorderedAccessViews(0, 8, nu, nullptr);
	}

	void Run(GfxContext* dc, FxPass* pass, Srvs srvs, Uavs uavs, UINT threads)
	{
		RunGroups(dc, pass, srvs, uavs, (threads + 63) / 64);
	}

	uint64 HashBytes(uint64 h, const void* data, size_t n)
	{
		const uint8_t* p = static_cast<const uint8_t*>(data);
		for (size_t i = 0; i < n; ++i)
			h = (h ^ p[i]) * 1099511628211ull;
		return h;
	}

	// 셰이더의 OrderKey 를 되돌린다
	float FromOrderKey(uint32_t u)
	{
		const uint32_t v = (u & 0x80000000u) ? (u & 0x7FFFFFFFu) : ~u;
		float f;
		memcpy(&f, &v, 4);
		return f;
	}

	// 살아 있는 수 · 경계: 몇 프레임 전 복사를 기다리지 않고 읽는다
	void ReadState(GfxContext* dc, VfxGpuSystem& g)
	{
		for (int i = 0; i < 3; ++i)
		{
			const int k = (g.Next + i) % 3;
			if (!g.Pending[k] || dc->GetData(g.Ready[k].Get(), nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
				continue;
			D3D11_MAPPED_SUBRESOURCE m;
			if (dc->Map(g.Staging[k].Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m) != S_OK)
				continue;
			const uint32_t* u = static_cast<const uint32_t*>(m.pData);
			g.Alive = (int)u[1];
			g.HasBounds = g.Alive > 0;
			if (g.HasBounds)
			{
				g.BoundsMin = Vec3(FromOrderKey(u[2]), FromOrderKey(u[3]), FromOrderKey(u[4]));
				g.BoundsMax = Vec3(FromOrderKey(u[5]), FromOrderKey(u[6]), FromOrderKey(u[7]));
				g.MaxSize = (std::max)(0.0f, FromOrderKey(u[8]));
			}
			dc->Unmap(g.Staging[k].Get(), 0);
			g.Pending[k] = false;
		}
	}

	void CopyState(GfxContext* dc, VfxGpuSystem& g)
	{
		if (!g.Staging[g.Next])
		{
			D3D11_BUFFER_DESC d = {};
			d.ByteWidth = kStateBytes;
			d.Usage = D3D11_USAGE_STAGING;
			d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			Dev()->CreateBuffer(&d, nullptr, g.Staging[g.Next].GetAddressOf());
			D3D11_QUERY_DESC q = { D3D11_QUERY_EVENT, 0 };
			Dev()->CreateQuery(&q, g.Ready[g.Next].GetAddressOf());
		}
		if (g.Staging[g.Next] && g.Ready[g.Next] && !g.Pending[g.Next])
		{
			dc->CopyResource(g.Staging[g.Next].Get(), g.State.B.Get());
			dc->End(g.Ready[g.Next].Get());
			g.Pending[g.Next] = true;
		}
		g.Next = (g.Next + 1) % 3;
	}

	// Update 전에 살아 있는 수 · 경계를 처음 값으로 (최소 = 가장 큰 키, 최대 = 0)
	void ResetStateStats(GfxContext* dc, VfxGpuSystem& g)
	{
		const uint32_t init[8] = { 0u, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0u, 0u, 0u, 0u };
		WriteBytes(dc, g.State, 4, init, 32);
	}

	UINT NextPow2(UINT v)
	{
		UINT p = 512;
		while (p < v) p <<= 1;
		return p;
	}

	// Output Mesh 의 메시 (VfxMeshes: 엔진 기본 · 모델 파일) → 정점 · 인덱스 버퍼
	MeshBuf* GetMesh(const std::string& name)
	{
		MeshBuf& m = s_Meshes[name];
		if (m.Tried)
			return m.Count > 0 ? &m : nullptr;
		m.Tried = true;
		const auto cpu = Vfx::LoadCpuMesh(name);
		if (!cpu->Error.empty())
			return nullptr;
		D3D11_BUFFER_DESC d = {};
		d.Usage = D3D11_USAGE_IMMUTABLE;
		d.ByteWidth = (UINT)(cpu->PosNormal.size() * sizeof(float));
		d.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		D3D11_SUBRESOURCE_DATA init = { cpu->PosNormal.data(), 0, 0 };
		if (FAILED(Dev()->CreateBuffer(&d, &init, m.Vb.GetAddressOf())))
			return nullptr;
		d.ByteWidth = (UINT)(cpu->Indices.size() * sizeof(uint32_t));
		d.BindFlags = D3D11_BIND_INDEX_BUFFER;
		init.pSysMem = cpu->Indices.data();
		if (FAILED(Dev()->CreateBuffer(&d, &init, m.Ib.GetAddressOf())))
			return nullptr;
		m.Count = (UINT)cpu->Indices.size();
		return &m;
	}

	// 시스템의 Collide with SDF 메시 · 해상도 (첫 블록 — 시스템마다 거리장 하나)
	bool SdfOf(const Vfx::System& s, std::string& mesh, int& res)
	{
		for (const Vfx::Block& b : s.Update)
			if (b.Enabled && b.Type == "CollideSDF")
			{
				mesh = "Sphere";
				res = 48;
				for (auto it = b.Params.begin(); it != b.Params.end(); ++it)
				{
					if (it.key() == "Mesh" && it->is_string() && !it->get<std::string>().empty()) mesh = it->get<std::string>();
					if (it.key() == "Resolution" && it->is_number()) res = std::clamp((int)std::round(it->get<float>()), 16, 96);
				}
				return true;
			}
		return false;
	}

	// 이 에셋에 깊이 버퍼 충돌 블록이 있는가
	bool UsesDepthCollision(const Vfx::Asset& a)
	{
		for (const Vfx::System& s : a.Systems)
			if (s.Enabled)
				for (const Vfx::Block& b : s.Update)
					if (b.Enabled && b.Type == "CollideDepth")
						return true;
		return false;
	}

	void Simulate(GfxContext* dc, VisualEffect* vfx, const CollisionView& coll)
	{
		const std::shared_ptr<const Vfx::Asset> asset = vfx->GetAsset();
		if (!asset)
			return;
		if (!vfx->Gpu())
			vfx->SetGpu(VfxGpuPtr(new VfxGpu()));
		VfxGpu& gpu = *vfx->Gpu();
		const bool reset = vfx->TakeResetRequest();
		auto& states = vfx->Systems();
		if (gpu.Systems.size() != asset->Systems.size())
			gpu.Systems.resize(asset->Systems.size());
		if (reset)
		{
			gpu.Time = 0.0f;
			gpu.DrawnMark = s_FrameMark;   // 다시 시작: 경계를 다시 잴 때까지 보이는 것으로
			for (VfxGpuSystem& g : gpu.Systems)
			{
				g.NeedsReset = true;
				g.HasBounds = false;
			}
		}
		// 화면 밖 (지난 프레임에 어느 뷰에도 그리지 않음 · 경계를 안다): Unity 처럼 시뮬레이션도 쉰다 (Culling = Always Simulate 면 계속)
		bool anyBounds = false;
		for (const VfxGpuSystem& g : gpu.Systems) anyBounds |= g.HasBounds;
		const bool culled = asset->CullingMode == Vfx::Culling::SimulateWhenVisible && anyBounds && gpu.DrawnMark + 1 < s_FrameMark;
		vfx->SetCulled(culled);
		const float dt = vfx->TakePendingDt();
		for (auto& st : states) if (culled) st.PendingSpawn = 0;
		if (culled)
			return;
		gpu.Time += dt;
		const Matrix world = vfx->GetGameObject()->GetTransform()->GetWorldMatrix();

		// GPU Event 를 보내는 시스템 (자식이 있다): 죽을 때 · 초당 (Rate 는 자식들 가운데 가장 큰 값)
		std::vector<bool> emitsDie(asset->Systems.size(), false);
		std::vector<float> emitRate(asset->Systems.size(), 0.0f);
		for (const Vfx::System& s : asset->Systems)
			if (s.Enabled && !s.SpawnCtx.Parent.empty())
				if (const int p = asset->FindSystem(s.SpawnCtx.Parent); p >= 0)
				{
					if (s.SpawnCtx.Trigger == Vfx::EventTrigger::Rate) emitRate[p] = (std::max)(emitRate[p], s.SpawnCtx.EventRate);
					else emitsDie[p] = true;
				}

		// 속성 · 변환 지문 (같으면 블록 목록을 다시 만들지 않는다)
		uint64 propKey = 1469598103934665603ull;
		propKey = HashBytes(propKey, &world._11, sizeof(float) * 16);
		for (const auto& [name, o] : vfx->Overrides)
		{
			propKey = HashBytes(propKey, name.data(), name.size());
			propKey = HashBytes(propKey, o.Value.data(), sizeof(float) * 4);
			propKey = HashBytes(propKey, &o.Enabled, 1);
		}
		const uint64 revision = vfx->AssetRevision();   // 주소가 아니라 판 (새 에셋이 같은 주소에 올 수 있다)
		propKey = HashBytes(propKey, &revision, sizeof(revision));
		const uint64 subgraphs = Vfx::DependencyRevision(*asset);   // 쓰는 Sub Graph 파일이 바뀌면
		propKey = HashBytes(propKey, &subgraphs, sizeof(subgraphs));

		SetM(s_V.World, world);
		SetM(s_V.WorldInv, world.Invert());
		SetF(s_V.Time, gpu.Time);
		SetF(s_V.Dt, dt);
		std::vector<int> alive(asset->Systems.size(), 0);
		for (int i : asset->SimulationOrder())
		{
			const Vfx::System& sys = asset->Systems[i];
			VfxGpuSystem& g = gpu.Systems[i];
			VisualEffect::SystemState& st = states[i];
			g.TrailReady = false;
			if (!sys.Enabled)
			{
				st.PendingSpawn = 0;
				continue;
			}
			const UINT capacity = (UINT)std::clamp(sys.Capacity, 1, 4 * 1024 * 1024);
			bool recreated = false;
			if (!EnsureRaw(g.Particles, capacity * kStride, D3D11_BIND_VERTEX_BUFFER, false, &recreated) || !EnsureRaw(g.State, kStateBytes, 0, false))
				continue;
			g.Capacity = capacity;
			if (recreated)
				g.NeedsReset = true;
			const bool emits = emitsDie[i] || emitRate[i] > 0.0f;
			if (emits)
			{
				g.EventCapacity = (std::min)(capacity, kMaxEvents);
				if (!EnsureRaw(g.Events, 16 + 2 * g.EventCapacity * kEventBytes, 0, true))
					continue;
			}
			// 꼬리 버퍼 (켜진 시스템만)
			const Vfx::Output& o = sys.OutputCtx;
			const UINT trailPoints = o.Trail && s_TrailBuild ? (UINT)std::clamp(o.TrailPoints, 2, 32) : 0u;
			if (trailPoints > 0)
			{
				if (!EnsureRaw(g.Trail, capacity * trailPoints * 16, 0, false) || !EnsureRaw(g.TrailVerts, capacity * trailPoints * kTrailVertBytes, D3D11_BIND_VERTEX_BUFFER, false))
					continue;
			}
			g.TrailPoints = trailPoints;
			const uint64 key = HashBytes(propKey, &i, sizeof(i));
			if (g.ProgramKey != key || !g.Program.B)
			{
				Vfx::Encode(sys, vfx->Properties(), &world._11, g.Enc, asset.get());
				if (!UploadProgram(dc, g.Program, g.Enc.Program))
					continue;
				g.ProgramKey = key;
			}
			SetU(s_V.Capacity, capacity);
			SetU(s_V.InitStart, g.Enc.InitStart);
			SetU(s_V.InitCount, g.Enc.InitCount);
			SetU(s_V.UpdateStart, g.Enc.UpdateStart);
			SetU(s_V.UpdateCount, g.Enc.UpdateCount);
			SetU(s_V.LocalSpace, sys.Local ? 1u : 0u);
			SetU(s_V.TrailPoints, trailPoints);
			SetF(s_V.TrailInterval, trailPoints > 0 ? (std::max)(0.005f, o.TrailLength / trailPoints) : 1.0f);
			SetF(s_V.TrailWidth, o.TrailWidth);
			if (g.NeedsReset)
			{
				Run(dc, s_Reset, {}, { { s_V.Particles, g.Particles.Uav.Get() } }, capacity);
				ZeroBytes(dc, g.State, 0, 16);
				g.NeedsReset = false;
				g.Alive = 0;
				g.HasBounds = false;
				for (bool& p : g.Pending) p = false;
			}
			// Spawn: 스스로 (CPU 가 센 수) 또는 부모의 GPU Event 자리에서
			if (!sys.SpawnCtx.Parent.empty())
			{
				const int p = asset->FindSystem(sys.SpawnCtx.Parent);
				VfxGpuSystem* parent = p >= 0 ? &gpu.Systems[p] : nullptr;
				if (dt > 0.0f && parent && parent->Events.B)
				{
					const UINT per = (UINT)std::clamp(sys.SpawnCtx.CountPerEvent, 1, 4096);
					SetU(s_V.FromEvents, sys.SpawnCtx.Trigger == Vfx::EventTrigger::Rate ? 2u : 1u);
					SetU(s_V.SpawnCount, per);
					SetU(s_V.EventCapacity, parent->EventCapacity);
					SetU(s_V.Seed, vfx->NextSeed());
					Run(dc, s_Spawn, { { s_V.Program, g.Program.Srv.Get() }, { s_V.EventsIn, parent->Events.Srv.Get() } },
						{ { s_V.Particles, g.Particles.Uav.Get() }, { s_V.State, g.State.Uav.Get() } }, parent->EventCapacity * per);
				}
			}
			else if (st.PendingSpawn > 0)
			{
				SetU(s_V.FromEvents, 0u);
				SetU(s_V.SpawnCount, st.PendingSpawn);
				SetU(s_V.EventCapacity, 0u);
				SetU(s_V.Seed, vfx->NextSeed());
				Run(dc, s_Spawn, { { s_V.Program, g.Program.Srv.Get() }, { s_V.EventsIn, Dummy(0).Srv.Get() } },
					{ { s_V.Particles, g.Particles.Uav.Get() }, { s_V.State, g.State.Uav.Get() } }, st.PendingSpawn);
			}
			st.PendingSpawn = 0;
			// Update (멈춤 · 일시 정지면 건너뛴다 — 다음 프레임에 GPU Event 를 다시 쓰지 않게 이벤트 수도 그대로)
			if (dt > 0.0f)
			{
				ResetStateStats(dc, g);
				if (emits)
					ZeroBytes(dc, g.Events, 0, 16);
				SetU(s_V.EmitOnDie, emitsDie[i] ? 1u : 0u);
				SetF(s_V.EmitRate, emitRate[i]);
				SetU(s_V.EventCapacity, g.EventCapacity);
				// Collide with SDF: 메시 거리장 (처음 · 바뀌면 굽고 올린다)
				GfxShaderResourceView* sdfSrv = Dummy(2).Srv.Get();
				{
					std::string mesh;
					int res = 48;
					if (SdfOf(sys, mesh, res))
					{
						const auto sdf = Vfx::BakeSdf(mesh, res);
						if (sdf->Error.empty())
						{
							if (g.SdfSource != sdf.get() && EnsureRaw(g.Sdf, (UINT)(sdf->Distance.size() * 4), 0, true))
							{
								WriteBytes(dc, g.Sdf, 0, sdf->Distance.data(), (UINT)(sdf->Distance.size() * 4));
								g.SdfSource = sdf.get();
							}
							if (g.SdfSource == sdf.get())
								sdfSrv = g.Sdf.Srv.Get();
						}
					}
				}
				Run(dc, s_Update, { { s_V.Program, g.Program.Srv.Get() }, { s_V.CollDepth, coll.On && coll.Depth ? coll.Depth : SpriteBatch::WhiteTexture() }, { s_V.Sdf, sdfSrv } },
					{ { s_V.Particles, g.Particles.Uav.Get() }, { s_V.State, g.State.Uav.Get() },
					  { s_V.Events, emits ? g.Events.Uav.Get() : Dummy(0).Uav.Get() }, { s_V.Trail, trailPoints > 0 ? g.Trail.Uav.Get() : Dummy(1).Uav.Get() } }, capacity);
				CopyState(dc, g);
			}
			// 꼬리 띠 조각 (카메라와 상관없어 프레임마다 한 번)
			if (trailPoints > 0)
			{
				Run(dc, s_TrailBuild, {}, { { s_V.Particles, g.Particles.Uav.Get() }, { s_V.Trail, g.Trail.Uav.Get() }, { s_V.TrailVerts, g.TrailVerts.Uav.Get() } },
					capacity * trailPoints);
				g.TrailReady = true;
			}
			ReadState(dc, g);
			alive[i] = g.Alive;
		}
		vfx->SetAliveCounts(alive);
	}

	// Alpha 시스템: 이 카메라에서 먼 것부터 (키 → 512 개씩 그룹 정렬 → 큰 단계 → 사본)
	bool SortForView(GfxContext* dc, VfxGpuSystem& g, bool local)
	{
		if (!s_SortKeys || !s_SortLocal || !s_SortGlobal || !s_SortGather)
			return false;
		const UINT n = NextPow2(g.Capacity);
		if (!EnsureRaw(g.SortKeys, n * 8, 0, false) || !EnsureRaw(g.Sorted, g.Capacity * kStride, D3D11_BIND_VERTEX_BUFFER, false))
			return false;
		g.SortCount = n;
		SetU(s_V.Capacity, g.Capacity);
		SetU(s_V.SortCount, n);
		SetU(s_V.LocalSpace, local ? 1u : 0u);
		Run(dc, s_SortKeys, {}, { { s_V.Particles, g.Particles.Uav.Get() }, { s_V.SortKeys, g.SortKeys.Uav.Get() } }, n);
		SetU(s_V.SortJ, 0u);
		RunGroups(dc, s_SortLocal, {}, { { s_V.SortKeys, g.SortKeys.Uav.Get() } }, n / 512);
		for (UINT k = 1024; k <= n; k <<= 1)
		{
			SetU(s_V.SortK, k);
			for (UINT j = k >> 1; j >= 512; j >>= 1)
			{
				SetU(s_V.SortJ, j);
				Run(dc, s_SortGlobal, {}, { { s_V.SortKeys, g.SortKeys.Uav.Get() } }, n / 2);
			}
			SetU(s_V.SortJ, 1u);   // 512 안의 단계 (j = 256..1)
			RunGroups(dc, s_SortLocal, {}, { { s_V.SortKeys, g.SortKeys.Uav.Get() } }, n / 512);
		}
		Run(dc, s_SortGather, {}, { { s_V.Particles, g.Particles.Uav.Get() }, { s_V.SortKeys, g.SortKeys.Uav.Get() }, { s_V.Sorted, g.Sorted.Uav.Get() } }, g.Capacity);
		return true;
	}

	bool Drawable(VisualEffect* vfx)
	{
		return vfx->IsEnabled() && vfx->ActiveInHierarchy() && vfx->GetAsset() && RenderLayers::Visible(vfx->GetGameObject());
	}

	// Visual Effect 의 월드 경계 (모든 시스템 + 자기 자리 둘레 1 m). 아는 시스템이 없으면 false (= 보이는 것으로)
	bool WorldBounds(VisualEffect* vfx, Vec3& mn, Vec3& mx)
	{
		const auto asset = vfx->GetAsset();
		VfxGpu* gpu = vfx->Gpu();
		if (!asset || !gpu)
			return false;
		const Matrix world = vfx->GetGameObject()->GetTransform()->GetWorldMatrix();
		const Vec3 origin(world._41, world._42, world._43);
		mn = origin - Vec3(1, 1, 1);
		mx = origin + Vec3(1, 1, 1);
		bool any = false;
		for (size_t i = 0; i < asset->Systems.size() && i < gpu->Systems.size(); ++i)
		{
			const VfxGpuSystem& g = gpu->Systems[i];
			if (!g.HasBounds || !asset->Systems[i].Enabled)
				continue;
			any = true;
			const Vfx::Output& o = asset->Systems[i].OutputCtx;
			// 크기 · 꼬리 · 몇 프레임 늦은 값 만큼 넉넉하게
			const float pad = g.MaxSize * (o.Orientation == Vfx::Orient::AlongVelocity ? 3.0f : 1.0f) + (o.Trail ? 1.5f : 0.0f) + 0.5f;
			Vec3 a = g.BoundsMin, b = g.BoundsMax;
			if (asset->Systems[i].Local)
			{
				Vec3 c[8];
				for (int k = 0; k < 8; ++k)
					c[k] = Vec3::Transform(Vec3((k & 1) ? b.x : a.x, (k & 2) ? b.y : a.y, (k & 4) ? b.z : a.z), world);
				a = b = c[0];
				for (int k = 1; k < 8; ++k) { a = Vec3::Min(a, c[k]); b = Vec3::Max(b, c[k]); }
			}
			mn = Vec3::Min(mn, a - Vec3(pad, pad, pad));
			mx = Vec3::Max(mx, b + Vec3(pad, pad, pad));
		}
		return any;
	}

	// 상자가 화면 안 (절두체 평면 6 개 — 행 벡터 viewProj 의 열로)
	bool BoxVisible(const Matrix& vp, const Vec3& mn, const Vec3& mx)
	{
		const float planes[6][4] = {
			{ vp._14 + vp._11, vp._24 + vp._21, vp._34 + vp._31, vp._44 + vp._41 },
			{ vp._14 - vp._11, vp._24 - vp._21, vp._34 - vp._31, vp._44 - vp._41 },
			{ vp._14 + vp._12, vp._24 + vp._22, vp._34 + vp._32, vp._44 + vp._42 },
			{ vp._14 - vp._12, vp._24 - vp._22, vp._34 - vp._32, vp._44 - vp._42 },
			{ vp._13, vp._23, vp._33, vp._43 },
			{ vp._14 - vp._13, vp._24 - vp._23, vp._34 - vp._33, vp._44 - vp._43 },
		};
		for (const auto& p : planes)
		{
			// 평면 쪽으로 가장 먼 꼭짓점이 뒤에 있으면 상자 전체가 밖
			const Vec3 v(p[0] >= 0 ? mx.x : mn.x, p[1] >= 0 ? mx.y : mn.y, p[2] >= 0 ? mx.z : mn.z);
			if (p[0] * v.x + p[1] * v.y + p[2] * v.z + p[3] < 0.0f)
				return false;
		}
		return true;
	}
}

// VisualEffect 의 기즈모 · 통계용
bool VfxGpuBounds(VisualEffect* vfx, Vec3& mn, Vec3& mx)
{
	return WorldBounds(vfx, mn, mx);
}

namespace VfxRuntime
{
	void MarkFrame() { ++s_FrameMark; }
	int LastDrawCalls() { return s_LastDraws; }
	int LastSystemCount() { return s_LastSystems; }
	int LastCulledCount() { return s_LastCulled; }
	const std::string& LastError() { return s_Error; }

	std::string InfoJson()
	{
		json effects = json::array();
		for (VisualEffect* v : VisualEffect::All())
		{
			json systems = json::array();
			if (auto asset = v->GetAsset())
				for (size_t i = 0; i < asset->Systems.size(); ++i)
					systems.push_back({ { "name", asset->Systems[i].Name }, { "alive", v->SystemAliveCount((int)i) } });
			json e = { { "object", v->GetGameObject() ? v->GetGameObject()->GetName() : std::string() }, { "asset", v->AssetPath },
				{ "alive", v->AliveParticleCount() }, { "culled", v->IsCulled() }, { "systems", systems } };
			Vec3 mn, mx;
			if (WorldBounds(v, mn, mx))
				e["bounds"] = { { mn.x, mn.y, mn.z }, { mx.x, mx.y, mx.z } };
			effects.push_back(e);
		}
		const json r = { { "gpu", s_State > 0 }, { "error", s_Error }, { "drawCalls", s_LastDraws }, { "culled", s_LastCulled }, { "effects", effects } };
		return r.dump();
	}

	bool Supported()
	{
		GfxContext* dc = Application::GetI()->GetDeviceContext();
		return dc && dc->SupportsGpuDriven() && Init();
	}

	void Render(const Matrix& view, const Matrix& proj, GfxRenderTargetView* rtv, GfxDepthStencilView* dsv, const ParticleRenderer::Environment* env)
	{
		s_LastDraws = 0;
		s_LastSystems = 0;
		s_LastCulled = 0;
		if (rtv == nullptr || VisualEffect::All().empty())
			return;
		GfxContext* dc = Application::GetI()->GetDeviceContext();
		if (!dc->SupportsGpuDriven())
		{
			if (s_Error.empty())
			{
				s_Error = "this graphics device has no compute support - Visual Effects are not drawn";
				EditorLog::Write("VFX", "%s", s_Error.c_str());
			}
			return;
		}
		if (!Init())
			return;

		const Matrix camWorld = view.Invert();
		const Matrix viewProj = view * proj;
		GfxRenderTargetView* rtvs[1] = { rtv };

		// 프레임의 첫 뷰에서 시뮬레이션 (Game · Scene 뷰가 같은 결과를 그린다)
		if (s_SimulatedMark != s_FrameMark)
		{
			s_SimulatedMark = s_FrameMark;
			PROFILE_SCOPE("VFX.Simulate");
			// 깊이 버퍼 충돌: 이 뷰의 장면 깊이를 compute 가 읽는다 (쓰기 깊이로 묶여 있으면 읽을 수 없어 잠깐 푼다)
			CollisionView coll;
			bool wantDepth = false;
			for (VisualEffect* vfx : VisualEffect::All())
				if (vfx->IsEnabled() && vfx->ActiveInHierarchy() && vfx->GetAsset() && UsesDepthCollision(*vfx->GetAsset()))
					wantDepth = true;
			coll.On = wantDepth && env && env->DepthSRV;
			coll.Depth = coll.On ? env->DepthSRV : nullptr;
			const float collParams[4] = { coll.On ? 1.0f : 0.0f, proj._33, proj._43, 0.0f };
			const float collCam[4] = { camWorld._41, camWorld._42, camWorld._43, 0.0f };
			SetM(s_V.CollViewProj, viewProj);
			SetM(s_V.CollInvViewProj, viewProj.Invert());
			SetV(s_V.CollParams, collParams);
			SetV(s_V.CollCam, collCam);
			D3D11_VIEWPORT vp = {};
			UINT vpCount = 1;
			dc->RSGetViewports(&vpCount, &vp);
			const float collViewport[4] = { vp.TopLeftX, vp.TopLeftY, vp.Width, vp.Height };
			SetV(s_V.CollViewport, collViewport);
			static bool s_Logged = false;
			if (coll.On && !s_Logged)
			{
				s_Logged = true;
				EditorLog::Write("VFX", "depth collision on: viewport %.0f,%.0f %.0fx%.0f", vp.TopLeftX, vp.TopLeftY, vp.Width, vp.Height);
			}
			if (coll.On)
				dc->OMSetRenderTargets(0, nullptr, nullptr);
			for (VisualEffect* vfx : VisualEffect::All())
				if (vfx->IsEnabled() && vfx->ActiveInHierarchy() && vfx->GetAsset())
					Simulate(dc, vfx, coll);
			if (coll.On)
				dc->OMSetRenderTargets(1, rtvs, dsv);
		}

		const Vec3 camRight(camWorld._11, camWorld._12, camWorld._13);
		const Vec3 camUp(camWorld._21, camWorld._22, camWorld._23);
		const Vec3 camForward(camWorld._31, camWorld._32, camWorld._33);
		const Vec3 camPos(camWorld._41, camWorld._42, camWorld._43);

		// 화면 안의 Visual Effect 를 먼 것부터 (투명 물체끼리의 순서)
		std::vector<std::pair<float, VisualEffect*>> list;
		for (VisualEffect* vfx : VisualEffect::All())
		{
			if (!Drawable(vfx) || !vfx->Gpu())
				continue;
			Vec3 mn, mx;
			if (WorldBounds(vfx, mn, mx) && !BoxVisible(viewProj, mn, mx))
			{
				++s_LastCulled;
				continue;
			}
			vfx->Gpu()->DrawnMark = s_FrameMark;
			list.push_back({ (vfx->GetGameObject()->GetTransform()->GetPosition() - camPos).Dot(camForward), vfx });
		}
		if (list.empty())
			return;
		std::stable_sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.first > b.first; });

		PROFILE_SCOPE("VFX.Draw");
		const bool sceneDepth = env && env->DepthReadOnly && env->DepthSRV;
		dc->OMSetRenderTargets(1, rtvs, sceneDepth ? env->DepthReadOnly : dsv);
		dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		SetM(s_V.ViewProj, viewProj);
		SetV3(s_V.CamRight, camRight);
		SetV3(s_V.CamUp, camUp);
		SetV3(s_V.CamPos, camPos);
		SetV3(s_V.CamFwd, camForward);
		const float depthParams[4] = { proj._33, proj._43, 0.0f, sceneDepth ? 1.0f : 0.0f };
		SetV(s_V.DepthParams, depthParams);
		// Output Mesh 의 빛: 방향광 0 (없으면 위에서) · 환경광 (Volume 의 Indirect Lighting)
		{
			const bool sun = env && env->HasSun;
			const float sunColor[4] = { sun ? env->SunColor.x : 0.8f, sun ? env->SunColor.y : 0.8f, sun ? env->SunColor.z : 0.8f, 0.0f };
			const float ind[3] = { env ? env->Indirect.x : 1.0f, env ? env->Indirect.y : 1.0f, env ? env->Indirect.z : 1.0f };
			const float ambient[4] = { 0.35f * ind[0], 0.37f * ind[1], 0.42f * ind[2], 0.0f };
			SetV(s_V.SunColor, sunColor);
			SetV(s_V.Ambient, ambient);
		}

		for (const auto& [dist, vfx] : list)
		{
			const auto asset = vfx->GetAsset();
			VfxGpu& gpu = *vfx->Gpu();
			SetM(s_V.World, vfx->GetGameObject()->GetTransform()->GetWorldMatrix());
			for (size_t i = 0; i < asset->Systems.size() && i < gpu.Systems.size(); ++i)
			{
				const Vfx::System& sys = asset->Systems[i];
				VfxGpuSystem& g = gpu.Systems[i];
				if (!sys.Enabled || !g.Particles.B || g.Capacity == 0)
					continue;
				const Vfx::Output& o = sys.OutputCtx;
				// 정렬 (compute) 은 그리기 상태를 묶기 전에 — 깊이 SRV 를 풀어 둔 채로
				const bool sorted = !o.TrailOnly && o.Sorted() && SortForView(dc, g, sys.Local);
				const float out0[4] = { (float)(int)o.Look, (float)(int)o.Orientation, o.Stretch, sceneDepth ? (std::max)(0.0f, o.SoftDistance) : 0.0f };
				const float out1[4] = { (std::max)(0.0f, o.Intensity), (float)(std::max)(1, o.FlipbookColumns), (float)(std::max)(1, o.FlipbookRows), (std::max)(0.0f, o.FlipbookFps) };
				SetV(s_V.Output0, out0);
				SetV(s_V.Output1, out1);
				{
					const Vec3 sd = env && env->HasSun ? Vec3(env->SunDirection.x, env->SunDirection.y, env->SunDirection.z) : Vec3(0.3f, -1.0f, 0.2f);
					Vec3 n = sd;
					n.Normalize();
					const float sunDir[4] = { n.x, n.y, n.z, o.Lit ? 1.0f : 0.0f };
					SetV(s_V.SunDir, sunDir);
				}
				SetU(s_V.LocalSpace, sys.Local ? 1u : 0u);
				if (s_V.SceneDepth) s_V.SceneDepth->SetResource(sceneDepth ? env->DepthSRV : nullptr);
				GfxShaderResourceView* tex = o.Look == Vfx::Shape::Texture && !o.Texture.empty() ? ParticleTextures::Get(o.Texture) : nullptr;
				if (s_V.Texture) s_V.Texture->SetResource(tex ? tex : SpriteBatch::WhiteTexture());
				const bool alpha = o.BlendMode == Vfx::Blend::Alpha;
				const bool opaque = o.BlendMode == Vfx::Blend::Opaque;
				// 꼬리 먼저 (파티클이 꼬리 위에 보이게)
				if (g.TrailReady && g.TrailPoints > 0 && s_TrailLayout)
				{
					const UINT stride = kTrailVertBytes, offset = 0;
					GfxBuffer* vb = g.TrailVerts.B.Get();
					dc->IASetInputLayout(s_TrailLayout.Get());
					dc->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
					(alpha ? s_TrailAlpha : s_TrailAdd)->Apply(0, dc);
					dc->DrawInstanced(4, g.Capacity * g.TrailPoints, 0, 0);
					++s_LastDraws;
				}
				if (!o.TrailOnly)
				{
					GfxBuffer* vb = sorted ? g.Sorted.B.Get() : g.Particles.B.Get();
					// 불투명: 깊이를 쓴다 (읽기 전용 깊이 · 깊이 SRV 를 풀고 쓰기 깊이로)
					if (opaque)
					{
						if (s_V.SceneDepth) s_V.SceneDepth->SetResource(nullptr);
						GfxShaderResourceView* nullSrv[8] = {};
						dc->PSSetShaderResources(0, 8, nullSrv);
						dc->OMSetRenderTargets(1, rtvs, dsv);
					}
					MeshBuf* mesh = o.Look == Vfx::Shape::Mesh && s_MeshLayout ? GetMesh(o.Mesh) : nullptr;
					FxPass* meshPass = opaque ? s_MeshOpaque : alpha ? s_MeshAlpha : s_MeshAdd;
					if (mesh && meshPass)
					{
						// Output Mesh: 메시 정점 (0 번) + 파티클 인스턴스 (1 번)
						GfxBuffer* vbs[2] = { mesh->Vb.Get(), vb };
						const UINT strides[2] = { kMeshVertexBytes, kStride }, offsets[2] = { 0, 0 };
						dc->IASetInputLayout(s_MeshLayout.Get());
						dc->IASetVertexBuffers(0, 2, vbs, strides, offsets);
						dc->IASetIndexBuffer(mesh->Ib.Get(), DXGI_FORMAT_R32_UINT, 0);
						dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
						meshPass->Apply(0, dc);
						dc->DrawIndexedInstanced(mesh->Count, g.Capacity, 0, 0, 0);
						dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
						GfxBuffer* nullVbs[2] = {};
						const UINT zeros[2] = {};
						dc->IASetVertexBuffers(0, 2, nullVbs, zeros, zeros);
						dc->IASetIndexBuffer(nullptr, DXGI_FORMAT_R32_UINT, 0);
						++s_LastDraws;
					}
					else if (o.Look != Vfx::Shape::Mesh && (!opaque || s_Opaque))
					{
						const UINT stride = kStride, offset = 0;
						dc->IASetInputLayout(s_Layout.Get());
						dc->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
						(opaque ? s_Opaque : alpha ? s_Alpha : s_Add)->Apply(0, dc);
						dc->DrawInstanced(4, g.Capacity, 0, 0);
						++s_LastDraws;
					}
					if (opaque)
						dc->OMSetRenderTargets(1, rtvs, sceneDepth ? env->DepthReadOnly : dsv);
				}
				++s_LastSystems;
				// 다음 시스템의 정렬 compute 가 깊이를 쓸 수 있게 SRV 를 푼다
				if (s_V.SceneDepth) s_V.SceneDepth->SetResource(nullptr);
				GfxShaderResourceView* nullSrv[8] = {};
				dc->PSSetShaderResources(0, 8, nullSrv);
			}
		}
		if (s_V.Texture) s_V.Texture->SetResource(nullptr);
		if (s_V.SceneDepth) s_V.SceneDepth->SetResource(nullptr);
		GfxBuffer* nullVb = nullptr;
		const UINT zero = 0;
		dc->IASetVertexBuffers(0, 1, &nullVb, &zero, &zero);
		GfxShaderResourceView* nullSRV[8] = {};
		dc->PSSetShaderResources(0, 8, nullSRV);
		if (sceneDepth)
			dc->OMSetRenderTargets(1, rtvs, dsv);
		dc->OMSetBlendState(nullptr, nullptr, 0xffffffff);
		dc->OMSetDepthStencilState(nullptr, 0);
		dc->RSSetState(nullptr);
		dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	}
}
