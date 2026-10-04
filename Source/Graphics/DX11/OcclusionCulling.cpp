#include "pch.h"
#include "OcclusionCulling.h"
#include "MeshGeometry.h"
#include "RenderStats.h"
#include "EditorLog.h"
#include "CliServer.h"
#include "Application.h"
#include "SceneCulling.h"

namespace
{
	OcclusionCulling::Stats s_Stats[2];   // Game · Scene 뷰
}

#ifndef __ANDROID__

namespace
{
	using Microsoft::WRL::ComPtr;

	// 57. OcclusionCulling.hlsl 의 cbOcclusion 과 같은 배치
	struct Constants
	{
		float ViewProj[16];
		float ViewSize[2];
		uint32_t ViewOrigin[2];
		uint32_t SrcSize[2];
		uint32_t DstSize[2];
		uint32_t Count, Frame, Mode, Levels;
	};
	static_assert(sizeof(Constants) == 112, "cbOcclusion");

	enum Kernel { KPrepare, KCompact, KCopyDepth, KReduce, KCull, KCount };
	ComPtr<ID3D11ComputeShader> s_Kernels[KCount];
	int s_KernelState = 0;   // 0 아직, 1 됨, -1 실패 (다시 시도하지 않는다)

	struct Buf
	{
		ComPtr<ID3D11Buffer> B;
		ComPtr<ID3D11ShaderResourceView> Srv;
		ComPtr<ID3D11UnorderedAccessView> Uav;
		UINT Bytes = 0;
	};

	struct View
	{
		uint32_t Frame = 0;          // 이 뷰를 그린 횟수 (기록 값)
		uint32_t LastUsed = 0;       // SceneCulling::FrameIndex
		bool Editor = false;
		Buf History;                 // 렌더러 자리마다 마지막으로 보인 Frame
		ComPtr<ID3D11Texture2D> HiZ;
		std::vector<ComPtr<ID3D11ShaderResourceView>> MipSrv;
		std::vector<ComPtr<ID3D11UnorderedAccessView>> MipUav;
		ComPtr<ID3D11ShaderResourceView> AllSrv;
		UINT W = 0, H = 0, Levels = 0;
		UINT FailedW = 0, FailedH = 0;   // 만들지 못한 크기 (프레임마다 다시 시도하지 않는다)
		ComPtr<ID3D11Buffer> Staging[3];   // 검사 · 보임 수 (몇 프레임 뒤에 읽는다)
		bool Pending[3] = {};
		int Next = 0;
	};
	std::unordered_map<const void*, View> s_Views;

	// 프레임 하나 (Begin ~ 본 패스) 동안 쓰는 버퍼 — 뷰끼리 차례로 다시 쓴다 (GPU 는 명령 순서대로)
	Buf s_Casters, s_Worlds, s_Items[2], s_Flags, s_Counters, s_Args[OcclusionCulling::SetCount], s_Out[OcclusionCulling::SetCount];
	ComPtr<ID3D11Buffer> s_Constants;
	std::vector<OcclusionCulling::Batch> s_Batches[2];
	ComPtr<ID3D11Texture2D> s_DepthTex;   // 깊이 SRV 를 만든 텍스처 (같으면 다시 쓴다)
	ComPtr<ID3D11ShaderResourceView> s_DepthSrv;

	struct Current
	{
		View* V = nullptr;
		uint32_t Casters = 0, Items[2] = {};
		float ViewProj[16] = {};
		bool Active = false;
	} s_Cur;

	ID3D11DeviceContext* Native(GfxContext* dc) { return dc ? static_cast<ID3D11DeviceContext*>(dc->Native()) : nullptr; }

	bool CompileKernels(ID3D11Device* dev)
	{
		if (s_KernelState != 0)
			return s_KernelState > 0;
		static const char* entries[KCount] = { "PrepareCS", "CompactCS", "CopyDepthCS", "ReduceCS", "CullCS" };
		static const char* defines[KCount] = { "KERNEL_PREPARE", "KERNEL_COMPACT", "KERNEL_COPYDEPTH", "KERNEL_REDUCE", "KERNEL_CULL" };
		const ULONGLONG start = ::GetTickCount64();
		for (int k = 0; k < KCount; ++k)
		{
			const D3D_SHADER_MACRO macros[] = { { defines[k], "1" }, { nullptr, nullptr } };
			ComPtr<ID3DBlob> blob, msgs;
			HRESULT hr = ::D3DCompileFromFile(L"../Shaders/57. OcclusionCulling.hlsl", macros, nullptr, entries[k], "cs_5_0",
				D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, blob.GetAddressOf(), msgs.GetAddressOf());
			if (SUCCEEDED(hr))
				hr = dev->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, s_Kernels[k].ReleaseAndGetAddressOf());
			if (FAILED(hr))
			{
				EditorLog::Write("Occlusion", "kernel %s failed (hr=0x%08X)%s%s — occlusion culling off", entries[k], (unsigned)hr,
					msgs ? "\n" : "", msgs ? (const char*)msgs->GetBufferPointer() : "");
				s_KernelState = -1;
				return false;
			}
		}
		D3D11_BUFFER_DESC cb = {};
		cb.ByteWidth = sizeof(Constants);
		cb.Usage = D3D11_USAGE_DYNAMIC;
		cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		if (FAILED(dev->CreateBuffer(&cb, nullptr, s_Constants.ReleaseAndGetAddressOf())))
		{
			s_KernelState = -1;
			return false;
		}
		EditorLog::Write("Occlusion", "kernels compiled in %llu ms", ::GetTickCount64() - start);
		s_KernelState = 1;
		return true;
	}

	// CPU 가 프레임마다 쓰는 구조 버퍼 (커지기만 함)
	bool EnsureStructured(ID3D11Device* dev, Buf& b, UINT stride, UINT count)
	{
		const UINT bytes = (std::max)(count, 1u) * stride;
		if (b.B && b.Bytes >= bytes)
			return true;
		const UINT cap = (std::max)(bytes, b.Bytes + b.Bytes / 2);
		b = Buf();
		D3D11_BUFFER_DESC d = {};
		d.ByteWidth = cap / stride * stride;
		d.Usage = D3D11_USAGE_DYNAMIC;
		d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		d.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		d.StructureByteStride = stride;
		if (FAILED(dev->CreateBuffer(&d, nullptr, b.B.GetAddressOf())))
			return false;
		D3D11_SHADER_RESOURCE_VIEW_DESC s = {};
		s.Format = DXGI_FORMAT_UNKNOWN;
		s.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		s.Buffer.NumElements = d.ByteWidth / stride;
		if (FAILED(dev->CreateShaderResourceView(b.B.Get(), &s, b.Srv.GetAddressOf())))
			return false;
		b.Bytes = d.ByteWidth;
		return true;
	}

	bool Upload(ID3D11DeviceContext* ctx, Buf& b, const void* data, size_t bytes)
	{
		if (bytes == 0)
			return true;
		D3D11_MAPPED_SUBRESOURCE m;
		if (FAILED(ctx->Map(b.B.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
			return false;
		memcpy(m.pData, data, bytes);
		ctx->Unmap(b.B.Get(), 0);
		return true;
	}

	// GPU 가 쓰는 raw 버퍼 (커지기만 함). keep = 키울 때 예전 내용을 옮긴다 (지난 프레임 기록)
	bool EnsureRaw(ID3D11Device* dev, ID3D11DeviceContext* ctx, Buf& b, UINT bytes, UINT bind, UINT misc, bool srv, bool keep = false)
	{
		bytes = (std::max)((bytes + 15) & ~15u, 16u);
		if (b.B && b.Bytes >= bytes)
			return true;
		const UINT cap = (std::max)(bytes, (b.Bytes + b.Bytes / 2 + 15) & ~15u);
		Buf n;
		D3D11_BUFFER_DESC d = {};
		d.ByteWidth = cap;
		d.Usage = D3D11_USAGE_DEFAULT;
		d.BindFlags = bind | D3D11_BIND_UNORDERED_ACCESS | (srv ? D3D11_BIND_SHADER_RESOURCE : 0);
		d.MiscFlags = misc | D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
		std::vector<uint8_t> zero(cap, 0);   // 기록 0 = 본 적 없음
		D3D11_SUBRESOURCE_DATA init = { zero.data(), 0, 0 };
		if (FAILED(dev->CreateBuffer(&d, &init, n.B.GetAddressOf())))
			return false;
		D3D11_UNORDERED_ACCESS_VIEW_DESC u = {};
		u.Format = DXGI_FORMAT_R32_TYPELESS;
		u.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		u.Buffer.NumElements = cap / 4;
		u.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
		if (FAILED(dev->CreateUnorderedAccessView(n.B.Get(), &u, n.Uav.GetAddressOf())))
			return false;
		if (srv)
		{
			D3D11_SHADER_RESOURCE_VIEW_DESC s = {};
			s.Format = DXGI_FORMAT_R32_TYPELESS;
			s.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
			s.BufferEx.NumElements = cap / 4;
			s.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
			if (FAILED(dev->CreateShaderResourceView(n.B.Get(), &s, n.Srv.GetAddressOf())))
				return false;
		}
		if (keep && b.B)
		{
			const D3D11_BOX box = { 0, 0, 0, b.Bytes, 1, 1 };
			ctx->CopySubresourceRegion(n.B.Get(), 0, 0, 0, 0, b.B.Get(), 0, &box);
		}
		n.Bytes = cap;
		b = n;
		return true;
	}

	// 지금 묶인 깊이 타깃의 SRV (R24G8 · R32 · R32G8X24 · R16 TYPELESS + SHADER_RESOURCE 일 때만)
	ID3D11ShaderResourceView* DepthSrv(ID3D11Device* dev, ID3D11DeviceContext* ctx)
	{
		ComPtr<ID3D11DepthStencilView> dsv;
		ctx->OMGetRenderTargets(0, nullptr, dsv.GetAddressOf());
		if (!dsv)
			return nullptr;
		ComPtr<ID3D11Resource> res;
		dsv->GetResource(res.GetAddressOf());
		ComPtr<ID3D11Texture2D> tex;
		if (!res || FAILED(res.As(&tex)))
			return nullptr;
		if (tex.Get() == s_DepthTex.Get() && s_DepthSrv)
			return s_DepthSrv.Get();
		D3D11_TEXTURE2D_DESC td;
		tex->GetDesc(&td);
		DXGI_FORMAT f = DXGI_FORMAT_UNKNOWN;
		switch (td.Format)
		{
		case DXGI_FORMAT_R24G8_TYPELESS: f = DXGI_FORMAT_R24_UNORM_X8_TYPELESS; break;
		case DXGI_FORMAT_R32_TYPELESS: f = DXGI_FORMAT_R32_FLOAT; break;
		case DXGI_FORMAT_R32G8X24_TYPELESS: f = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS; break;
		case DXGI_FORMAT_R16_TYPELESS: f = DXGI_FORMAT_R16_UNORM; break;
		default: break;
		}
		if (f == DXGI_FORMAT_UNKNOWN || !(td.BindFlags & D3D11_BIND_SHADER_RESOURCE) || td.SampleDesc.Count != 1)
			return nullptr;
		D3D11_SHADER_RESOURCE_VIEW_DESC s = {};
		s.Format = f;
		s.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		s.Texture2D.MipLevels = 1;
		ComPtr<ID3D11ShaderResourceView> srv;
		if (FAILED(dev->CreateShaderResourceView(tex.Get(), &s, srv.GetAddressOf())))
			return nullptr;
		s_DepthTex = tex;
		s_DepthSrv = srv;
		return s_DepthSrv.Get();
	}

	bool EnsureHiZ(ID3D11Device* dev, View& v, UINT w, UINT h)
	{
		if (v.HiZ && v.W == w && v.H == h)
			return true;
		if (v.FailedW == w && v.FailedH == h)
			return false;
		v.HiZ.Reset();
		v.MipSrv.clear();
		v.MipUav.clear();
		v.AllSrv.Reset();
		v.W = v.H = v.Levels = 0;
		UINT levels = 1;   // D3D 밉 크기 = 내림 (max(1, 크기 >> i))
		for (UINT m = (std::max)(w, h); m > 1; m >>= 1)
			++levels;
		D3D11_TEXTURE2D_DESC d = {};
		d.Width = w;
		d.Height = h;
		d.MipLevels = levels;
		d.ArraySize = 1;
		d.Format = DXGI_FORMAT_R32_FLOAT;
		d.SampleDesc.Count = 1;
		d.Usage = D3D11_USAGE_DEFAULT;
		d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		if (FAILED(dev->CreateTexture2D(&d, nullptr, v.HiZ.GetAddressOf())))
		{
			EditorLog::Write("Occlusion", "Hi-Z %u x %u (%u mips) failed — no occlusion in this view", w, h, levels);
			v.FailedW = w;
			v.FailedH = h;
			return false;
		}
		for (UINT i = 0; i < levels; ++i)
		{
			D3D11_SHADER_RESOURCE_VIEW_DESC s = {};
			s.Format = DXGI_FORMAT_R32_FLOAT;
			s.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			s.Texture2D.MostDetailedMip = i;
			s.Texture2D.MipLevels = 1;
			D3D11_UNORDERED_ACCESS_VIEW_DESC u = {};
			u.Format = DXGI_FORMAT_R32_FLOAT;
			u.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			u.Texture2D.MipSlice = i;
			ComPtr<ID3D11ShaderResourceView> srv;
			ComPtr<ID3D11UnorderedAccessView> uav;
			if (FAILED(dev->CreateShaderResourceView(v.HiZ.Get(), &s, srv.GetAddressOf())) || FAILED(dev->CreateUnorderedAccessView(v.HiZ.Get(), &u, uav.GetAddressOf())))
				return false;
			v.MipSrv.push_back(srv);
			v.MipUav.push_back(uav);
		}
		if (FAILED(dev->CreateShaderResourceView(v.HiZ.Get(), nullptr, v.AllSrv.GetAddressOf())))
			return false;
		v.W = w;
		v.H = h;
		v.Levels = levels;
		EditorLog::Write("Occlusion", "Hi-Z %u x %u, %u mips (%s view)", w, h, levels, v.Editor ? "Scene" : "Game");
		return true;
	}

	void SetConstants(ID3D11DeviceContext* ctx, const Constants& c)
	{
		D3D11_MAPPED_SUBRESOURCE m;
		if (SUCCEEDED(ctx->Map(s_Constants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
		{
			memcpy(m.pData, &c, sizeof(c));
			ctx->Unmap(s_Constants.Get(), 0);
		}
		ID3D11Buffer* cb = s_Constants.Get();
		ctx->CSSetConstantBuffers(0, 1, &cb);
	}

	// 커널 하나: SRV · UAV 를 묶고 돌린 뒤 바로 푼다 (다음 단계가 같은 버퍼를 다른 쓰임으로 묶는다)
	void Run(ID3D11DeviceContext* ctx, Kernel k, std::initializer_list<ID3D11ShaderResourceView*> srvs, std::initializer_list<ID3D11UnorderedAccessView*> uavs, UINT x, UINT y = 1)
	{
		ID3D11ShaderResourceView* s[4] = {};
		ID3D11UnorderedAccessView* u[4] = {};
		UINT ns = 0, nu = 0;
		for (auto* v : srvs) s[ns++] = v;
		for (auto* v : uavs) u[nu++] = v;
		ctx->CSSetShader(s_Kernels[k].Get(), nullptr, 0);
		ctx->CSSetShaderResources(0, ns, s);
		ctx->CSSetUnorderedAccessViews(0, nu, u, nullptr);
		if (x > 0 && y > 0)
			ctx->Dispatch(x, y, 1);
		ID3D11ShaderResourceView* ns4[4] = {};
		ID3D11UnorderedAccessView* nu4[4] = {};
		ctx->CSSetShaderResources(0, 4, ns4);
		ctx->CSSetUnorderedAccessViews(0, 4, nu4, nullptr);
	}

	UINT Groups(UINT n, UINT size) { return (n + size - 1) / size; }

	void Compact(ID3D11DeviceContext* ctx, Constants& c, int items, OcclusionCulling::Set set, uint32_t mode)
	{
		c.Count = s_Cur.Items[items];
		c.Mode = mode;
		SetConstants(ctx, c);
		Run(ctx, KCompact, { s_Items[items].Srv.Get(), s_Worlds.Srv.Get(), s_Flags.Srv.Get() }, { s_Args[set].Uav.Get(), s_Out[set].Uav.Get() }, Groups(c.Count, 64));
	}

	// 몇 프레임 전 검사 결과를 기다리지 않고 읽는다
	void ReadStats(ID3D11DeviceContext* ctx, View& v)
	{
		for (int i = 0; i < 3; ++i)
		{
			const int k = (v.Next + i) % 3;   // 오래된 것부터
			if (!v.Pending[k] || !v.Staging[k])
				continue;
			D3D11_MAPPED_SUBRESOURCE m;
			if (ctx->Map(v.Staging[k].Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m) != S_OK)
				continue;
			const uint32_t* p = static_cast<const uint32_t*>(m.pData);
			OcclusionCulling::Stats& st = s_Stats[v.Editor ? 1 : 0];
			st.Tested = (int)p[0];
			st.Visible = (int)p[1];
			++st.Frames;
			ctx->Unmap(v.Staging[k].Get(), 0);
			v.Pending[k] = false;
		}
	}
}

namespace OcclusionCulling
{
	bool Supported(GfxContext* dc)
	{
		ID3D11DeviceContext* ctx = Native(dc);
		if (!ctx)
			return false;
		ComPtr<ID3D11Device> dev;
		ctx->GetDevice(dev.GetAddressOf());
		return dev && dev->GetFeatureLevel() >= D3D_FEATURE_LEVEL_11_0;
	}

	bool Begin(GfxContext* dc, const void* view, bool editor, const float viewProj[16], Frame& frame, uint32_t slotCount)
	{
		s_Cur.Active = false;
		static const bool s_Off = [] { char v[8] = {}; return ::GetEnvironmentVariableA("NOVA_DEV_NOOCCLUSION", v, sizeof(v)) > 0 && v[0] == '1'; }();
		Stats& st = s_Stats[editor ? 1 : 0];
		st.Active = false;
		if (!Enabled || s_Off || view == nullptr || frame.Casters.empty() || !Supported(dc))
			return false;
		ID3D11DeviceContext* ctx = Native(dc);
		ComPtr<ID3D11Device> dev;
		ctx->GetDevice(dev.GetAddressOf());
		if (!CompileKernels(dev.Get()))
			return false;
		ID3D11ShaderResourceView* depth = DepthSrv(dev.Get(), ctx);
		if (!depth)
			return false;   // 깊이를 읽을 수 없는 타깃 (예: 창 깊이 버퍼) — CPU 컬링만

		// 묶음마다 자리 (후보 수만큼 이어서)
		for (int s = 0; s < 2; ++s)
		{
			std::vector<Batch>& batches = frame.Batches[s];
			for (Batch& b : batches) b.Candidates = 0;
			for (const Item& it : frame.Items[s]) ++batches[it.Batch].Candidates;
			std::vector<uint32_t> base(batches.size());
			uint32_t next = 0;
			for (size_t b = 0; b < batches.size(); ++b) { base[b] = next; next += batches[b].Candidates; }
			for (Item& it : frame.Items[s]) it.Base = base[it.Batch];
			s_Batches[s] = batches;
		}

		// 뷰 (지난 프레임 기록 · Hi-Z) — 오래 안 쓴 카메라는 지운다
		const uint32_t now = SceneCulling::FrameIndex();
		for (auto it = s_Views.begin(); it != s_Views.end();)
			it = (it->first != view && now - it->second.LastUsed > 600) ? s_Views.erase(it) : std::next(it);
		View& v = s_Views[view];
		v.Editor = editor;
		v.LastUsed = now;
		++v.Frame;

		const UINT casters = (UINT)frame.Casters.size();
		bool ok = EnsureStructured(dev.Get(), s_Casters, sizeof(Caster), casters) && Upload(ctx, s_Casters, frame.Casters.data(), casters * sizeof(Caster))
			&& EnsureStructured(dev.Get(), s_Worlds, 64, casters) && Upload(ctx, s_Worlds, frame.Worlds.data(), casters * 64)
			&& EnsureRaw(dev.Get(), ctx, s_Flags, casters * 4, 0, 0, true)
			&& EnsureRaw(dev.Get(), ctx, s_Counters, 16, 0, 0, false)
			&& EnsureRaw(dev.Get(), ctx, v.History, (slotCount + 1) * 4, 0, 0, false, true);
		for (int s = 0; s < 2 && ok; ++s)
			ok = EnsureStructured(dev.Get(), s_Items[s], sizeof(Item), (UINT)frame.Items[s].size()) && Upload(ctx, s_Items[s], frame.Items[s].data(), frame.Items[s].size() * sizeof(Item));
		for (int set = 0; set < SetCount && ok; ++set)
		{
			const int s = set == Main ? 1 : 0;
			std::vector<uint32_t> args;
			args.reserve(s_Batches[s].size() * 5);
			uint32_t next = 0;
			for (const Batch& b : s_Batches[s])
			{
				args.insert(args.end(), { b.IndexCount, 0u, b.StartIndex, (uint32_t)b.BaseVertex, next });
				next += b.Candidates;
			}
			ok = EnsureRaw(dev.Get(), ctx, s_Args[set], (UINT)args.size() * 4, 0, D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS, false)
				&& EnsureRaw(dev.Get(), ctx, s_Out[set], (UINT)frame.Items[s].size() * 64, D3D11_BIND_VERTEX_BUFFER, 0, false);
			if (ok && !args.empty())
			{
				const D3D11_BOX box = { 0, 0, 0, (UINT)args.size() * 4, 1, 1 };
				ctx->UpdateSubresource(s_Args[set].B.Get(), 0, &box, args.data(), 0, 0);
			}
		}
		if (!ok)
		{
			EditorLog::Write("Occlusion", "buffer creation failed — CPU culling for this view");
			return false;
		}

		s_Cur.V = &v;
		s_Cur.Casters = casters;
		s_Cur.Items[0] = (uint32_t)frame.Items[0].size();
		s_Cur.Items[1] = (uint32_t)frame.Items[1].size();
		memcpy(s_Cur.ViewProj, viewProj, sizeof(s_Cur.ViewProj));

		// 지난 프레임에 보였는가 → 깊이 프리패스 1 단계 목록
		Constants c = {};
		memcpy(c.ViewProj, viewProj, sizeof(c.ViewProj));
		c.Count = casters;
		c.Frame = v.Frame;
		SetConstants(ctx, c);
		Run(ctx, KPrepare, { s_Casters.Srv.Get() }, { v.History.Uav.Get(), s_Flags.Uav.Get() }, Groups(casters, 64));
		Compact(ctx, c, 0, DepthPhase1, 0);
		ctx->CSSetShader(nullptr, nullptr, 0);
		s_Cur.Active = true;
		st.Active = true;
		return true;
	}

	void Finish(GfxContext* dc)
	{
		if (!s_Cur.Active)
			return;
		s_Cur.Active = false;
		ID3D11DeviceContext* ctx = Native(dc);
		ComPtr<ID3D11Device> dev;
		ctx->GetDevice(dev.GetAddressOf());
		View& v = *s_Cur.V;

		// 깊이를 읽으려면 깊이 타깃을 잠시 푼다 (끝나면 그대로 다시)
		ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
		ID3D11DepthStencilView* dsv = nullptr;
		ctx->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs, &dsv);
		UINT rtCount = 0;
		for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) if (rtvs[i]) rtCount = i + 1;
		D3D11_VIEWPORT vp = {};
		UINT vpCount = 1;
		ctx->RSGetViewports(&vpCount, &vp);
		ID3D11ShaderResourceView* depth = DepthSrv(dev.Get(), ctx);
		ctx->OMSetRenderTargets(0, nullptr, nullptr);

		Constants c = {};
		memcpy(c.ViewProj, s_Cur.ViewProj, sizeof(c.ViewProj));
		c.Frame = v.Frame;
		const UINT w = vpCount ? (UINT)vp.Width : 0, h = vpCount ? (UINT)vp.Height : 0;
		if (depth && w > 0 && h > 0 && EnsureHiZ(dev.Get(), v, w, h))
		{
			// 깊이 → Hi-Z 0 번, 그 위로 2x2 씩 가장 먼 깊이
			c.ViewOrigin[0] = (uint32_t)vp.TopLeftX;
			c.ViewOrigin[1] = (uint32_t)vp.TopLeftY;
			c.DstSize[0] = w;
			c.DstSize[1] = h;
			SetConstants(ctx, c);
			Run(ctx, KCopyDepth, { depth }, { v.MipUav[0].Get() }, Groups(w, 8), Groups(h, 8));
			UINT sw = w, sh = h;
			for (UINT i = 1; i < v.Levels; ++i)
			{
				const UINT dw = (std::max)(sw >> 1, 1u), dh = (std::max)(sh >> 1, 1u);
				c.SrcSize[0] = sw; c.SrcSize[1] = sh;
				c.DstSize[0] = dw; c.DstSize[1] = dh;
				SetConstants(ctx, c);
				Run(ctx, KReduce, { v.MipSrv[i - 1].Get() }, { v.MipUav[i].Get() }, Groups(dw, 8), Groups(dh, 8));
				sw = dw;
				sh = dh;
			}
			c.Levels = v.Levels;
			c.ViewSize[0] = (float)w;
			c.ViewSize[1] = (float)h;
		}
		// Hi-Z 가 없으면 (Levels 0) 모두 보인다

		// 가려짐 검사 → 새로 보인 것 (깊이 2 단계) · 지금 보이는 것 (본 패스)
		const UINT zero[4] = {};
		ctx->ClearUnorderedAccessViewUint(s_Counters.Uav.Get(), zero);
		c.Count = s_Cur.Casters;
		SetConstants(ctx, c);
		Run(ctx, KCull, { s_Casters.Srv.Get(), v.AllSrv.Get() }, { v.History.Uav.Get(), s_Flags.Uav.Get(), s_Counters.Uav.Get() }, Groups(c.Count, 64));
		Compact(ctx, c, 0, DepthPhase2, 1);
		Compact(ctx, c, 1, Main, 2);
		ctx->CSSetShader(nullptr, nullptr, 0);
		ID3D11Buffer* nullCb = nullptr;
		ctx->CSSetConstantBuffers(0, 1, &nullCb);

		// 검사 수 · 보인 수 (몇 프레임 뒤에 읽는다)
		if (!v.Staging[v.Next])
		{
			D3D11_BUFFER_DESC d = {};
			d.ByteWidth = 16;
			d.Usage = D3D11_USAGE_STAGING;
			d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			dev->CreateBuffer(&d, nullptr, v.Staging[v.Next].GetAddressOf());
		}
		if (v.Staging[v.Next])
		{
			ctx->CopyResource(v.Staging[v.Next].Get(), s_Counters.B.Get());
			v.Pending[v.Next] = true;
		}
		v.Next = (v.Next + 1) % 3;
		ReadStats(ctx, v);

		ctx->OMSetRenderTargets(rtCount, rtvs, dsv);
		for (ID3D11RenderTargetView* r : rtvs) if (r) r->Release();
		if (dsv) dsv->Release();
	}

	void DrawIndirect(GfxContext* dc, Set set, uint32_t batch, MeshGeometry& geometry, uint32_t subset)
	{
		ID3D11DeviceContext* ctx = Native(dc);
		const int s = set == Main ? 1 : 0;
		if (!ctx || batch >= s_Batches[s].size() || !s_Args[set].B)
			return;
		geometry.BindForInstancing(dc);
		ID3D11Buffer* inst = s_Out[set].B.Get();
		const UINT stride = 64, offset = 0;
		ctx->IASetVertexBuffers(1, 1, &inst, &stride, &offset);
		const Batch& b = s_Batches[s][batch];
		RenderStats::AddDraw(b.IndexCount, geometry.GetSubset(subset).VertexCount, set == Main ? b.Candidates : 1);   // 본 패스 = 가려짐 전 후보 수
		ctx->DrawIndexedInstancedIndirect(s_Args[set].B.Get(), batch * 20);
	}
}

#else   // 안드로이드: GLES 는 절두체 컬링만

namespace OcclusionCulling
{
	bool Supported(GfxContext*) { return false; }
	bool Begin(GfxContext*, const void*, bool editor, const float*, Frame&, uint32_t) { s_Stats[editor ? 1 : 0].Active = false; return false; }
	void Finish(GfxContext*) {}
	void DrawIndirect(GfxContext*, Set, uint32_t, MeshGeometry&, uint32_t) {}
}

#endif

namespace OcclusionCulling
{
	const Stats& LastStats(bool editor) { return s_Stats[editor ? 1 : 0]; }

	void RegisterEditor()
	{
		// nova occlusion info | set --enabled false
		CliServer::Register("occlusion", "occlusion culling op: {op: info | set, enabled?} (nova occlusion info)", [](const nlohmann::json& args, nlohmann::json& result, std::string& error) {
			const std::string op = args.value("op", std::string("info"));
			if (op == "set")
			{
				if (args.contains("enabled"))
				{
					const auto& e = args["enabled"];
					Enabled = e.is_boolean() ? e.get<bool>() : (e.is_string() ? e.get<std::string>() != "false" : e.get<int>() != 0);
				}
			}
			else if (op != "info")
			{
				error = "unknown op (info | set --enabled true|false)";
				return false;
			}
			auto view = [](bool editor) {
				const Stats& s = LastStats(editor);
				return nlohmann::json{ { "active", s.Active }, { "tested", s.Tested }, { "visible", s.Visible }, { "culled", s.Tested - s.Visible }, { "frames", s.Frames } };
			};
			result = { { "supported", Supported(Application::GetI()->GetDeviceContext()) }, { "enabled", Enabled }, { "game", view(false) }, { "scene", view(true) } };
			return true;
		});
	}
}
