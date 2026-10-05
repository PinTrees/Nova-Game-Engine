#include "pch.h"
#include "VfxRuntime.h"
#include "VisualEffect.h"
#include "ParticleTextures.h"
#include "RenderLayers.h"
#include "SpriteBatch.h"
#include "Effects.h"
#include "Profiler.h"

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

	constexpr UINT kStride = 96;        // 58. VFX.fx 의 kStride
	constexpr UINT kEventBytes = 48;
	constexpr UINT kMaxEvents = 4096;   // 부모 시스템이 한 프레임에 보내는 GPU Event 의 위 한계
}

struct VfxGpuSystem
{
	Buf Particles, State, Events, EventCount, Program;
	UINT Capacity = 0, EventCapacity = 0;
	bool NeedsReset = true;
	uint64 ProgramKey = 0;              // 블록 목록이 바뀌었는가 (에셋 · 속성 · 변환)
	Vfx::Encoded Enc;
	ComPtr<GfxBuffer> Staging[3];       // 살아 있는 수 (몇 프레임 뒤에 읽는다)
	ComPtr<GfxQuery> Ready[3];
	bool Pending[3] = {};
	int Next = 0;
	int Alive = 0;
};

struct VfxGpu
{
	std::vector<VfxGpuSystem> Systems;
	float Time = 0.0f;
};

void VfxGpuDeleter::operator()(VfxGpu* gpu) const { delete gpu; }

namespace
{
	std::unique_ptr<Effect> s_Effect;
	int s_State = 0;   // 0 아직, 1 됨, -1 실패
	std::string s_Error;
	FxPass *s_Reset = nullptr, *s_Spawn = nullptr, *s_Update = nullptr, *s_Alpha = nullptr, *s_Add = nullptr;
	ComPtr<GfxInputLayout> s_Layout;
	struct Vars
	{
		FxVar *ViewProj, *World, *CamRight, *Time, *CamUp, *Dt, *CamPos, *Capacity, *SpawnCount, *FromEvents, *EventCapacity, *EmitOnDie,
			*InitStart, *InitCount, *UpdateStart, *UpdateCount, *Seed, *LocalSpace, *Output0, *Output1, *DepthParams;
		FxVar *Program, *Particles, *State, *EventsOut, *EventCountOut, *EventsIn, *EventCountIn, *Texture, *SceneDepth;
	} s_V = {};
	uint64 s_FrameMark = 1, s_SimulatedMark = 0;
	// 쓰지 않는 칸에 묶는 작은 버퍼 (Vulkan · GL 은 셰이더가 선언한 버퍼가 모두 묶여 있어야 한다).
	//  칸마다 따로 — 같은 UAV 를 두 칸에 묶으면 DirectX 11 이 묶기 전체를 무시한다 (Update 가 아무것도 쓰지 않았다)
	Buf s_Dummy[2];
	int s_LastDraws = 0, s_LastSystems = 0;

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
		if (!s_Reset || !s_Spawn || !s_Update || !s_Alpha || !s_Add)
		{
			s_Error = "58. VFX.fx: a technique is missing or failed";
			EditorLog::Write("VFX", "%s", s_Error.c_str());
			s_Effect.reset();
			return false;
		}
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
		auto var = [&](const char* n) { return fx->GetVariableByName(n); };
		s_V = { var("gViewProj"), var("gWorld"), var("gCamRight"), var("gTime"), var("gCamUp"), var("gDt"), var("gCamPos"), var("gCapacity"),
			var("gSpawnCount"), var("gFromEvents"), var("gEventCapacity"), var("gEmitOnDie"), var("gInitStart"), var("gInitCount"),
			var("gUpdateStart"), var("gUpdateCount"), var("gSeed"), var("gLocalSpace"), var("gOutput0"), var("gOutput1"), var("gDepthParams"),
			var("gProgram"), var("gParticles"), var("gState"), var("gEventsOut"), var("gEventCountOut"), var("gEventsIn"), var("gEventCountIn"),
			var("gTexture"), var("gSceneDepth") };
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

	void ZeroBytes(GfxContext* dc, Buf& b, UINT from, UINT to)
	{
		const uint32_t zero[4] = {};
		const D3D11_BOX box = { from, 0, 0, to, 1, 1 };
		dc->UpdateSubresource(b.B.Get(), 0, &box, zero, 0, 0);
	}

	void ClearUint(GfxContext* dc, Buf& b)
	{
		const UINT zero[4] = {};
		if (!dc->ClearUnorderedAccessViewUint(b.Uav.Get(), zero))
		{
			std::vector<uint8_t> z(b.Bytes, 0);
			const D3D11_BOX box = { 0, 0, 0, b.Bytes, 1, 1 };
			dc->UpdateSubresource(b.B.Get(), 0, &box, z.data(), 0, 0);
		}
	}

	Buf& Dummy(int i = 0)
	{
		if (!s_Dummy[i].B)
			EnsureRaw(s_Dummy[i], 64, 0, true);
		return s_Dummy[i];
	}

	using Srvs = std::initializer_list<std::pair<FxVar*, GfxShaderResourceView*>>;
	using Uavs = std::initializer_list<std::pair<FxVar*, GfxUnorderedAccessView*>>;

	// 커널 하나: 뷰를 넣고 돌린 뒤 바로 푼다 (다음 단계가 같은 버퍼를 정점 버퍼 · SRV 로 쓴다)
	void Run(GfxContext* dc, FxPass* pass, Srvs srvs, Uavs uavs, UINT threads)
	{
		if (threads == 0)
			return;
		for (const auto& [var, view] : srvs) if (var && var->IsValid()) var->SetResource(view);
		for (const auto& [var, view] : uavs) if (var && var->IsValid()) var->SetUnorderedAccessView(view);
		pass->Apply(0, dc);
		dc->Dispatch((threads + 63) / 64, 1, 1);
		for (const auto& [var, view] : srvs) if (var && var->IsValid()) var->SetResource(nullptr);
		for (const auto& [var, view] : uavs) if (var && var->IsValid()) var->SetUnorderedAccessView(nullptr);
		GfxShaderResourceView* ns[8] = {};
		GfxUnorderedAccessView* nu[8] = {};
		dc->CSSetShaderResources(0, 8, ns);
		dc->CSSetUnorderedAccessViews(0, 8, nu, nullptr);
	}

	uint64 HashBytes(uint64 h, const void* data, size_t n)
	{
		const uint8_t* p = static_cast<const uint8_t*>(data);
		for (size_t i = 0; i < n; ++i)
			h = (h ^ p[i]) * 1099511628211ull;
		return h;
	}

	// 살아 있는 수: 몇 프레임 전 복사를 기다리지 않고 읽는다
	void ReadAlive(GfxContext* dc, VfxGpuSystem& g)
	{
		for (int i = 0; i < 3; ++i)
		{
			const int k = (g.Next + i) % 3;
			if (!g.Pending[k] || dc->GetData(g.Ready[k].Get(), nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
				continue;
			D3D11_MAPPED_SUBRESOURCE m;
			if (dc->Map(g.Staging[k].Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m) != S_OK)
				continue;
			g.Alive = (int)static_cast<const uint32_t*>(m.pData)[1];
			dc->Unmap(g.Staging[k].Get(), 0);
			g.Pending[k] = false;
		}
	}

	void CopyAlive(GfxContext* dc, VfxGpuSystem& g)
	{
		if (!g.Staging[g.Next])
		{
			D3D11_BUFFER_DESC d = {};
			d.ByteWidth = 16;
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

	void Simulate(GfxContext* dc, VisualEffect* vfx)
	{
		const std::shared_ptr<const Vfx::Asset> asset = vfx->GetAsset();
		if (!asset)
			return;
		if (!vfx->Gpu())
			vfx->SetGpu(VfxGpuPtr(new VfxGpu()));
		VfxGpu& gpu = *vfx->Gpu();
		const bool reset = vfx->TakeResetRequest();
		const float dt = vfx->TakePendingDt();
		auto& states = vfx->Systems();
		if (gpu.Systems.size() != asset->Systems.size())
			gpu.Systems.resize(asset->Systems.size());
		if (reset)
		{
			gpu.Time = 0.0f;
			for (VfxGpuSystem& g : gpu.Systems)
				g.NeedsReset = true;
		}
		gpu.Time += dt;
		const Matrix world = vfx->GetGameObject()->GetTransform()->GetWorldMatrix();

		// GPU Event 를 보내는 시스템 (자식이 있다)
		std::vector<bool> emits(asset->Systems.size(), false);
		for (const Vfx::System& s : asset->Systems)
			if (s.Enabled && !s.SpawnCtx.Parent.empty())
				if (const int p = asset->FindSystem(s.SpawnCtx.Parent); p >= 0)
					emits[p] = true;

		// 속성 · 변환 지문 (같으면 블록 목록을 다시 만들지 않는다)
		uint64 propKey = 1469598103934665603ull;
		propKey = HashBytes(propKey, &world._11, sizeof(float) * 16);
		for (const auto& [name, o] : vfx->Overrides)
		{
			propKey = HashBytes(propKey, name.data(), name.size());
			propKey = HashBytes(propKey, o.Value.data(), sizeof(float) * 4);
			propKey = HashBytes(propKey, &o.Enabled, 1);
		}
		propKey = HashBytes(propKey, asset.get(), sizeof(void*));

		SetM(s_V.World, world);
		SetF(s_V.Time, gpu.Time);
		SetF(s_V.Dt, dt);
		std::vector<int> alive(asset->Systems.size(), 0);
		for (int i : asset->SimulationOrder())
		{
			const Vfx::System& sys = asset->Systems[i];
			VfxGpuSystem& g = gpu.Systems[i];
			VisualEffect::SystemState& st = states[i];
			if (!sys.Enabled)
			{
				st.PendingSpawn = 0;
				continue;
			}
			const UINT capacity = (UINT)std::clamp(sys.Capacity, 1, 4 * 1024 * 1024);
			bool recreated = false;
			if (!EnsureRaw(g.Particles, capacity * kStride, D3D11_BIND_VERTEX_BUFFER, false, &recreated) || !EnsureRaw(g.State, 16, 0, false))
				continue;
			g.Capacity = capacity;
			if (recreated)
				g.NeedsReset = true;
			if (emits[i])
			{
				g.EventCapacity = (std::min)(capacity, kMaxEvents);
				if (!EnsureRaw(g.Events, g.EventCapacity * kEventBytes, 0, true) || !EnsureRaw(g.EventCount, 16, 0, true))
					continue;
			}
			const uint64 key = HashBytes(propKey, &i, sizeof(i));
			if (g.ProgramKey != key || !g.Program.B)
			{
				Vfx::Encode(sys, vfx->Properties(), &world._11, g.Enc);
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
			if (g.NeedsReset)
			{
				Run(dc, s_Reset, {}, { { s_V.Particles, g.Particles.Uav.Get() } }, capacity);
				ZeroBytes(dc, g.State, 0, 16);
				g.NeedsReset = false;
				g.Alive = 0;
				for (bool& p : g.Pending) p = false;
			}
			// Spawn: 스스로 (CPU 가 센 수) 또는 부모의 GPU Event 자리에서
			if (!sys.SpawnCtx.Parent.empty())
			{
				const int p = asset->FindSystem(sys.SpawnCtx.Parent);
				VfxGpuSystem* parent = p >= 0 ? &gpu.Systems[p] : nullptr;
				if (dt > 0.0f && parent && parent->Events.B && parent->EventCount.B)
				{
					const UINT per = (UINT)std::clamp(sys.SpawnCtx.CountPerEvent, 1, 4096);
					SetU(s_V.FromEvents, 1u);
					SetU(s_V.SpawnCount, per);
					SetU(s_V.EventCapacity, parent->EventCapacity);
					SetU(s_V.Seed, vfx->NextSeed());
					Run(dc, s_Spawn, { { s_V.Program, g.Program.Srv.Get() }, { s_V.EventsIn, parent->Events.Srv.Get() }, { s_V.EventCountIn, parent->EventCount.Srv.Get() } },
						{ { s_V.Particles, g.Particles.Uav.Get() }, { s_V.State, g.State.Uav.Get() } }, parent->EventCapacity * per);
				}
			}
			else if (st.PendingSpawn > 0)
			{
				SetU(s_V.FromEvents, 0u);
				SetU(s_V.SpawnCount, st.PendingSpawn);
				SetU(s_V.EventCapacity, 0u);
				SetU(s_V.Seed, vfx->NextSeed());
				Run(dc, s_Spawn, { { s_V.Program, g.Program.Srv.Get() }, { s_V.EventsIn, Dummy(0).Srv.Get() }, { s_V.EventCountIn, Dummy(1).Srv.Get() } },
					{ { s_V.Particles, g.Particles.Uav.Get() }, { s_V.State, g.State.Uav.Get() } }, st.PendingSpawn);
			}
			st.PendingSpawn = 0;
			// Update (멈춤 · 일시 정지면 건너뛴다 — 다음 프레임에 GPU Event 를 다시 쓰지 않게 이벤트 수도 그대로)
			if (dt > 0.0f)
			{
				ZeroBytes(dc, g.State, 4, 8);
				if (emits[i])
					ClearUint(dc, g.EventCount);
				SetU(s_V.EmitOnDie, emits[i] ? 1u : 0u);
				SetU(s_V.EventCapacity, g.EventCapacity);
				Run(dc, s_Update, { { s_V.Program, g.Program.Srv.Get() } },
					{ { s_V.Particles, g.Particles.Uav.Get() }, { s_V.State, g.State.Uav.Get() },
					  { s_V.EventsOut, emits[i] ? g.Events.Uav.Get() : Dummy(0).Uav.Get() }, { s_V.EventCountOut, emits[i] ? g.EventCount.Uav.Get() : Dummy(1).Uav.Get() } }, capacity);
				CopyAlive(dc, g);
			}
			ReadAlive(dc, g);
			alive[i] = g.Alive;
		}
		vfx->SetAliveCounts(alive);
	}

	bool Drawable(VisualEffect* vfx)
	{
		return vfx->IsEnabled() && vfx->ActiveInHierarchy() && vfx->GetAsset() && RenderLayers::Visible(vfx->GetGameObject());
	}
}

namespace VfxRuntime
{
	void MarkFrame() { ++s_FrameMark; }
	int LastDrawCalls() { return s_LastDraws; }
	int LastSystemCount() { return s_LastSystems; }
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
			effects.push_back({ { "object", v->GetGameObject() ? v->GetGameObject()->GetName() : std::string() }, { "asset", v->AssetPath },
				{ "alive", v->AliveParticleCount() }, { "systems", systems } });
		}
		const json r = { { "gpu", s_State > 0 }, { "error", s_Error }, { "drawCalls", s_LastDraws }, { "effects", effects } };
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

		// 프레임의 첫 뷰에서 시뮬레이션 (Game · Scene 뷰가 같은 결과를 그린다)
		if (s_SimulatedMark != s_FrameMark)
		{
			s_SimulatedMark = s_FrameMark;
			PROFILE_SCOPE("VFX.Simulate");
			for (VisualEffect* vfx : VisualEffect::All())
				if (vfx->IsEnabled() && vfx->ActiveInHierarchy() && vfx->GetAsset())
					Simulate(dc, vfx);
		}

		const Matrix camWorld = view.Invert();
		const Vec3 camRight(camWorld._11, camWorld._12, camWorld._13);
		const Vec3 camUp(camWorld._21, camWorld._22, camWorld._23);
		const Vec3 camForward(camWorld._31, camWorld._32, camWorld._33);
		const Vec3 camPos(camWorld._41, camWorld._42, camWorld._43);

		// 먼 Visual Effect 부터 (투명 물체끼리의 순서)
		std::vector<std::pair<float, VisualEffect*>> list;
		for (VisualEffect* vfx : VisualEffect::All())
			if (Drawable(vfx) && vfx->Gpu())
				list.push_back({ (vfx->GetGameObject()->GetTransform()->GetPosition() - camPos).Dot(camForward), vfx });
		if (list.empty())
			return;
		std::stable_sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.first > b.first; });

		PROFILE_SCOPE("VFX.Draw");
		GfxRenderTargetView* rtvs[1] = { rtv };
		const bool sceneDepth = env && env->DepthReadOnly && env->DepthSRV;
		dc->OMSetRenderTargets(1, rtvs, sceneDepth ? env->DepthReadOnly : dsv);
		dc->IASetInputLayout(s_Layout.Get());
		dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		SetM(s_V.ViewProj, view * proj);
		SetV3(s_V.CamRight, camRight);
		SetV3(s_V.CamUp, camUp);
		SetV3(s_V.CamPos, camPos);
		const float depthParams[4] = { proj._33, proj._43, 0.0f, sceneDepth ? 1.0f : 0.0f };
		SetV(s_V.DepthParams, depthParams);
		if (s_V.SceneDepth) s_V.SceneDepth->SetResource(sceneDepth ? env->DepthSRV : nullptr);

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
				const float out0[4] = { (float)(int)o.Look, (float)(int)o.Orientation, o.Stretch, sceneDepth ? (std::max)(0.0f, o.SoftDistance) : 0.0f };
				const float out1[4] = { (std::max)(0.0f, o.Intensity), (float)(std::max)(1, o.FlipbookColumns), (float)(std::max)(1, o.FlipbookRows), (std::max)(0.0f, o.FlipbookFps) };
				SetV(s_V.Output0, out0);
				SetV(s_V.Output1, out1);
				SetU(s_V.LocalSpace, sys.Local ? 1u : 0u);
				GfxShaderResourceView* tex = o.Look == Vfx::Shape::Texture && !o.Texture.empty() ? ParticleTextures::Get(o.Texture) : nullptr;
				if (s_V.Texture) s_V.Texture->SetResource(tex ? tex : SpriteBatch::WhiteTexture());
				const UINT stride = kStride, offset = 0;
				GfxBuffer* vb = g.Particles.B.Get();
				dc->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
				(o.BlendMode == Vfx::Blend::Alpha ? s_Alpha : s_Add)->Apply(0, dc);
				dc->DrawInstanced(4, g.Capacity, 0, 0);
				++s_LastDraws;
				++s_LastSystems;
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
