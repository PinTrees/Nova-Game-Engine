#include "pch.h"
#include "GfxD3D12Internal.h"

// Gfx DirectX 12 컨텍스트: D3D11 즉시 컨텍스트의 뜻을 D3D12 명령 목록으로.
//  - 그리기마다: 디스크립터 표 (샘플러 → 뷰 링 — 모자라면 제출할 수 있어 장벽보다 먼저) → 읽는 · 쓰는 자원의 상태 장벽 → 루트 · PSO (키 캐시) → 표 · 타깃 · 정점 · 동적 상태
//  - 타깃 · 상태가 같으면 다시 묶지 않는다 (Epoch 가 바뀌면 = 새 명령 목록 → 모두 다시)
namespace GfxD3D12Impl
{
	namespace
	{
		const D3D11_RASTERIZER_DESC& DefaultRs() { static const D3D11_RASTERIZER_DESC d = DefaultRasterizer(); return d; }
		const D3D11_BLEND_DESC& DefaultBs() { static const D3D11_BLEND_DESC d = DefaultBlend(); return d; }
		const D3D11_DEPTH_STENCIL_DESC& DefaultDs() { static const D3D11_DEPTH_STENCIL_DESC d = DefaultDepthStencil(); return d; }
		uint64_t DefaultRsHash() { static const uint64_t h = HashRasterizer(DefaultRs()); return h; }
		uint64_t DefaultBsHash() { static const uint64_t h = HashBlend(DefaultBs()); return h; }
		uint64_t DefaultDsHash() { static const uint64_t h = HashDepthStencil(DefaultDs()); return h; }

		Rtv* AsRtv(GfxRenderTargetView* v) { return v && v->Api() == GfxApi::DirectX12 ? static_cast<Rtv*>(v) : nullptr; }
		Dsv* AsDsv(GfxDepthStencilView* v) { return v && v->Api() == GfxApi::DirectX12 ? static_cast<Dsv*>(v) : nullptr; }
		Srv* AsSrv(GfxShaderResourceView* v) { return v && v->Api() == GfxApi::DirectX12 ? static_cast<Srv*>(v) : nullptr; }
		Uav* AsUav(GfxUnorderedAccessView* v) { return v && v->Api() == GfxApi::DirectX12 ? static_cast<Uav*>(v) : nullptr; }
		Query* AsQuery(GfxQuery* q) { return q && q->Api() == GfxApi::DirectX12 ? static_cast<Query*>(q) : nullptr; }
		Sampler* AsSampler(GfxSamplerState* s) { return s && s->Api() == GfxApi::DirectX12 ? static_cast<Sampler*>(s) : nullptr; }

		// 버퍼 읽기 = 한 상태 (정점 · 인덱스 · 상수 · SRV · 간접 인자 · 복사 원본 · 예측) — 읽기끼리 오갈 때 장벽이 없게
		constexpr D3D12_RESOURCE_STATES kBufferRead = D3D12_RESOURCE_STATE_GENERIC_READ;
		// 컴퓨트 큐 목록이 다룰 수 있는 상태 (픽셀 셰이더 자원 · 렌더 타깃 · 깊이 · 인덱스 는 그래픽 전용)
		constexpr D3D12_RESOURCE_STATES kComputeStates = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_UNORDERED_ACCESS |
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT | D3D12_RESOURCE_STATE_COPY_DEST | D3D12_RESOURCE_STATE_COPY_SOURCE;
		constexpr D3D12_RESOURCE_STATES kComputeRead = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
			D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT | D3D12_RESOURCE_STATE_COPY_SOURCE;
		// 비동기 컴퓨트 중: 읽기 상태를 컴퓨트 큐에서 되는 것으로 (쓰기 · 복사 상태는 그대로)
		D3D12_RESOURCE_STATES ComputeState(D3D12_RESOURCE_STATES s, bool buffer)
		{
			if (s == D3D12_RESOURCE_STATE_UNORDERED_ACCESS || s == D3D12_RESOURCE_STATE_COPY_DEST || s == D3D12_RESOURCE_STATE_COPY_SOURCE) return s;
			return buffer ? kComputeRead : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
		}

		D3D12_PRIMITIVE_TOPOLOGY_TYPE TopologyType(D3D11_PRIMITIVE_TOPOLOGY t)
		{
			switch (t)
			{
			case D3D11_PRIMITIVE_TOPOLOGY_POINTLIST: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
			case D3D11_PRIMITIVE_TOPOLOGY_LINELIST: case D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP:
			case D3D11_PRIMITIVE_TOPOLOGY_LINELIST_ADJ: case D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ:
				return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
			default:
				return t >= D3D11_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH : D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
			}
		}

		bool IsStrip(D3D11_PRIMITIVE_TOPOLOGY t)
		{
			return t == D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP || t == D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP || t == D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ ||
				t == D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ;
		}

		UINT64 Lcm(UINT64 a, UINT64 b)
		{
			UINT64 x = a, y = b;
			while (y) { const UINT64 t = x % y; x = y; y = t; }
			return a / x * b;
		}

		// 새 명령 목록 (Epoch) 이면 묶은 것을 모두 잊는다 → 다음 그리기 · 디스패치가 다시 묶는다
		void ResetBound(Ctx& c)
		{
			if (c.BoundEpoch == c.D->Epoch) return;
			c.BoundEpoch = c.D->Epoch;
			c.BoundRoot = nullptr;
			c.BoundComputeRoot = nullptr;
			c.BoundPipeline = nullptr;
			c.BoundTargets = 0;
			c.BoundTopo = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
			memset(c.BoundVbs, 0, sizeof(c.BoundVbs));
			c.BoundIb = {};
			c.DynamicDirty = true;
		}
	}

	Ctx::~Ctx()
	{
		if (D && D->Immediate == this) D->Immediate = nullptr;
	}

	// ============================================================ 상태 장벽
	void Ctx::Transition(Resource& r, UINT baseMip, UINT mips, UINT baseLayer, UINT layers, D3D12_RESOURCE_STATES state)
	{
		if (!r.Res || r.Fixed || r.States.empty()) return;
		const bool async = D->AsyncOpen;
		if (async) state = ComputeState(state, r.Buffer);
		if (r.Buffer)
		{
			D3D12_RESOURCE_STATES& cur = r.States[0];
			if (r.StateEpoch != D->CurEpoch())
			{
				// 앞 명령 목록이 끝나며 COMMON 으로 감쇠했다 → 이번 첫 사용은 장벽 없이 승격 (버퍼는 어느 상태로나)
				r.StateEpoch = D->CurEpoch();
				cur = state;
				return;
			}
			if (cur == state) return;
			D3D12_RESOURCE_BARRIER b = {};
			b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			b.Transition.pResource = r.Res.Get();
			b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			b.Transition.StateBefore = cur;
			b.Transition.StateAfter = state;
			Barriers.push_back(b);
			cur = state;
			return;
		}
		const UINT lastMip = (std::min)(baseMip + mips, r.Mips), lastLayer = (std::min)(baseLayer + layers, r.Layers);
		// 비동기 컴퓨트: 그래픽 전용 상태에서 나오는 장벽은 그래픽 목록에 (컴퓨트 큐가 기다리는 제출) — 나머지는 컴퓨트 목록에
		auto target = [&](D3D12_RESOURCE_STATES before) -> std::vector<D3D12_RESOURCE_BARRIER>& {
			return async && (before & ~kComputeStates) ? PreBarriers : Barriers;
		};
		// 자원 전체가 같은 상태에서 같이 바뀌면 장벽 하나 (ALL_SUBRESOURCES)
		if (baseMip == 0 && lastMip == r.Mips && baseLayer == 0 && lastLayer == r.Layers)
		{
			const D3D12_RESOURCE_STATES first = r.States[0];
			bool uniform = true;
			for (D3D12_RESOURCE_STATES s : r.States)
				if (s != first) { uniform = false; break; }
			if (uniform)
			{
				if (first == state) return;
				D3D12_RESOURCE_BARRIER b = {};
				b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				b.Transition.pResource = r.Res.Get();
				b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
				b.Transition.StateBefore = first;
				b.Transition.StateAfter = state;
				target(first).push_back(b);
				std::fill(r.States.begin(), r.States.end(), state);
				return;
			}
		}
		for (UINT plane = 0; plane < r.Planes; ++plane)
			for (UINT layer = baseLayer; layer < lastLayer; ++layer)
				for (UINT m = baseMip; m < lastMip; ++m)
				{
					const UINT sub = r.Sub(m, layer, plane);
					if (r.States[sub] == state) continue;
					D3D12_RESOURCE_BARRIER b = {};
					b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
					b.Transition.pResource = r.Res.Get();
					b.Transition.Subresource = sub;
					b.Transition.StateBefore = r.States[sub];
					b.Transition.StateAfter = state;
					target(r.States[sub]).push_back(b);
					r.States[sub] = state;
				}
	}

	void Ctx::FlushBarriers()
	{
		if (Barriers.empty()) return;
		D->Cmd()->ResourceBarrier((UINT)Barriers.size(), Barriers.data());
		Barriers.clear();
	}

	void Ctx::FlushPreBarriers()
	{
		if (PreBarriers.empty()) return;
		D->GraphicsCmd()->ResourceBarrier((UINT)PreBarriers.size(), PreBarriers.data());
		PreBarriers.clear();
	}

	// ============================================================ 비동기 컴퓨트
	void Ctx::BeginAsyncCompute()
	{
		if (!SupportsAsyncCompute() || D->AsyncOpen || D->Lost) return;
		FlushBarriers();   // 그래픽 목록에
		SavedUavDirty = UavDirty;
		UavDirty = false;
		D->OpenCompute();
	}

	void Ctx::EndAsyncCompute()
	{
		if (!D->AsyncOpen) return;
		if (UavDirty) UavBarrier();
		FlushBarriers();   // 컴퓨트 목록에
		D->SubmitCompute();   // 그래픽 (앞 장벽 포함) → 컴퓨트 큐가 기다린 뒤 실행
		UavDirty = SavedUavDirty;
	}

	void Ctx::WaitAsyncCompute()
	{
		if (D->AsyncOpen) EndAsyncCompute();
		if (D->ComputePending == 0) return;
		// End 뒤에 기록한 그래픽 일은 컴퓨트와 겹쳐 돌게 먼저 보내고, 그 다음 그래픽 제출부터 컴퓨트 결과를 기다린다
		D->Submit(false);
		D->QueueWait(D->ComputeFence.Get(), D->ComputePending);
		D->ComputePending = 0;
	}

	void Ctx::UavBarrier()
	{
		D3D12_RESOURCE_BARRIER b = {};
		b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		Barriers.push_back(b);
		UavDirty = false;
	}

	void Ctx::AfterSubmit()
	{
		BoundEpoch = ~0ull;
		Predicated = false;
	}

	bool Ctx::IsBoundTarget(const Image* img, UINT baseMip, UINT mips, UINT baseLayer, UINT layers) const
	{
		auto overlap = [&](const ViewInfo& v) {
			return v.Img == img && v.BaseMip < baseMip + mips && baseMip < v.BaseMip + v.Mips && v.BaseLayer < baseLayer + layers && baseLayer < v.BaseLayer + v.Layers;
		};
		for (UINT i = 0; i < RtvCount; ++i)
			if (Rtv* r = AsRtv(Rtvs[i].Get()))
				if (overlap(r->V)) return true;
		if (Dsv* d = AsDsv(DsvView.Get()))
			if (!d->V.ReadOnly && overlap(d->V)) return true;
		return false;
	}

	// ============================================================ 정점 입력 · 출력 · 래스터 상태
	void Ctx::IASetVertexBuffers(UINT start, UINT count, GfxBuffer* const* buffers, const UINT* strides, const UINT* offsets)
	{
		for (UINT i = 0; i < count && start + i < D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT; ++i)
			Vbs[start + i] = { buffers ? buffers[i] : nullptr, strides ? strides[i] : 0, offsets ? offsets[i] : 0 };
	}

	void Ctx::IAGetVertexBuffers(UINT start, UINT count, GfxBuffer** buffers, UINT* strides, UINT* offsets)
	{
		for (UINT i = 0; i < count && start + i < D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT; ++i)
		{
			const VB& v = Vbs[start + i];
			if (buffers) { buffers[i] = v.Buf.Get(); if (buffers[i]) buffers[i]->AddRef(); }
			if (strides) strides[i] = v.Stride;
			if (offsets) offsets[i] = v.Offset;
		}
	}

	void Ctx::IAGetIndexBuffer(GfxBuffer** b, DXGI_FORMAT* f, UINT* o)
	{
		if (b) { *b = Ib.Get(); if (*b) (*b)->AddRef(); }
		if (f) *f = IbFormat;
		if (o) *o = IbOffset;
	}

	void Ctx::SOSetTargets(UINT count, GfxBuffer* const* buffers, const UINT*)
	{
		if (count && buffers && buffers[0])
			D->Once("so", "%s", "stream output is not supported");
	}

	void Ctx::OMSetRenderTargets(UINT count, GfxRenderTargetView* const* rtvs, GfxDepthStencilView* dsv)
	{
		count = (std::min)(count, 8u);
		bool same = DsvView.Get() == dsv && RtvCount == count;
		for (UINT i = 0; same && i < count; ++i)
			same = Rtvs[i].Get() == (rtvs ? rtvs[i] : nullptr);
		if (same) return;
		++TargetsSerial;
		RtvCount = count;
		for (UINT i = 0; i < 8; ++i)
			Rtvs[i] = i < count && rtvs ? rtvs[i] : nullptr;
		DsvView = dsv;
	}

	void Ctx::OMGetRenderTargets(UINT count, GfxRenderTargetView** rtvs, GfxDepthStencilView** dsv)
	{
		for (UINT i = 0; rtvs && i < count; ++i)
		{
			rtvs[i] = i < 8 ? Rtvs[i].Get() : nullptr;
			if (rtvs[i]) rtvs[i]->AddRef();
		}
		if (dsv) { *dsv = DsvView.Get(); if (*dsv) (*dsv)->AddRef(); }
	}

	void Ctx::OMSetDepthStencilState(GfxDepthStencilState* s, UINT ref)
	{
		Ds = s;
		if (StencilRef != ref) DynamicDirty = true;
		StencilRef = ref;
	}

	void Ctx::OMGetDepthStencilState(GfxDepthStencilState** s, UINT* ref)
	{
		if (s) { *s = Ds.Get(); if (*s) (*s)->AddRef(); }
		if (ref) *ref = StencilRef;
	}

	void Ctx::OMSetBlendState(GfxBlendState* s, const FLOAT f[4], UINT mask)
	{
		Bs = s;
		for (int i = 0; i < 4; ++i) BlendFactor[i] = f ? f[i] : 1.0f;
		SampleMask = mask;
		DynamicDirty = true;
	}

	void Ctx::OMGetBlendState(GfxBlendState** s, FLOAT f[4], UINT* mask)
	{
		if (s) { *s = Bs.Get(); if (*s) (*s)->AddRef(); }
		if (f) for (int i = 0; i < 4; ++i) f[i] = BlendFactor[i];
		if (mask) *mask = SampleMask;
	}

	void Ctx::RSSetViewports(UINT count, const D3D11_VIEWPORT* v)
	{
		ViewportCount = (std::min)(count, 16u);
		for (UINT i = 0; i < ViewportCount; ++i) Viewports[i] = v[i];
		DynamicDirty = true;
	}

	void Ctx::RSGetViewports(UINT* count, D3D11_VIEWPORT* v)
	{
		if (v) for (UINT i = 0; i < (std::min)(*count, ViewportCount); ++i) v[i] = Viewports[i];
		*count = ViewportCount;
	}

	void Ctx::RSSetScissorRects(UINT count, const D3D11_RECT* r)
	{
		ScissorCount = (std::min)(count, 16u);
		for (UINT i = 0; i < ScissorCount; ++i) Scissors[i] = r[i];
		DynamicDirty = true;
	}

	void Ctx::RSGetScissorRects(UINT* count, D3D11_RECT* r)
	{
		if (r) for (UINT i = 0; i < (std::min)(*count, ScissorCount); ++i) r[i] = Scissors[i];
		*count = ScissorCount;
	}

	void Ctx::OutsideViews(const char* stage, UINT count, GfxShaderResourceView* const* views)
	{
		for (UINT i = 0; views && i < count; ++i)
			if (views[i])
			{
				D->Once(std::string("srv:") + stage, "%sSetShaderResources with a view outside effects is ignored", stage);
				break;
			}
	}

	// ============================================================ 그리기
	void Ctx::ResolveBuffer(GfxResource* gb, D3D12_GPU_VIRTUAL_ADDRESS& gpu, Resource*& res)
	{
		gpu = 0;
		res = nullptr;
		Buf* b = BufOf(gb);
		if (!b) return;
		if (b->Shadow.empty())
		{
			if (!b->R.Res) return;
			gpu = b->R.Res->GetGPUVirtualAddress();
			res = &b->R;
			if (b->Desc.Usage == D3D11_USAGE_STAGING) b->R.LastUse = D->Recording();
			return;
		}
		// DYNAMIC: 링 자리가 이번 조각이 아니면 (조각이 바뀜 — 앞 조각은 다시 쓰일 수 있다) 사본에서 다시 올린다
		if (!b->HasLoc || !D->LocCurrent(b->Loc))
		{
			const UINT64 align = b->Desc.StructureByteStride ? Lcm(256, b->Desc.StructureByteStride) : 256;
			b->HasLoc = D->Upload(b->Shadow.size(), align, b->Loc);
			if (!b->HasLoc) return;
			memcpy(b->Loc.Cpu, b->Shadow.data(), b->Shadow.size());
		}
		gpu = b->Loc.Gpu;
	}

	bool Ctx::BuildTables(std::vector<std::pair<int, D3D12_GPU_DESCRIPTOR_HANDLE>>& tables, bool& usesUav)
	{
		tables.clear();
		usesUav = false;
		Program* P = Prog.Get();
		auto value = [&](int element) -> const GfxD3D12Shared::BindingValue* {
			return element >= 0 && element < (int)Values.size() ? &Values[element] : nullptr;
		};
		// 1) 샘플러 표 (같은 조합은 캐시) — 힙이 차면 제출할 수 있어 상태 장벽보다 먼저
		thread_local std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> handles;
		thread_local std::vector<uint64_t> key;
		for (const auto& st : P->Stages)
		{
			if (st.SamplerParam < 0) continue;
			handles.clear();
			key.clear();
			for (const auto& slot : st.Samplers)
			{
				const auto* v = value(slot.Element);
				Sampler* s = v ? AsSampler(v->Sampler) : nullptr;
				handles.push_back(s ? s->Cpu : slot.Comparison ? D->DefaultCompare : D->DefaultSampler);
				key.push_back(s ? s->Id : slot.Comparison ? 1 : 2);
			}
			D3D12_GPU_DESCRIPTOR_HANDLE gpu;
			if (!D->SamplerTable(handles, key, gpu)) return false;
			tables.push_back({ st.SamplerParam, gpu });
		}
		// 2) 뷰 표: 모든 단계를 한 덩어리로 (중간에 링이 차 제출해도 앞 단계 칸이 덮이지 않게)
		UINT total = 0;
		for (const auto& st : P->Stages) total += (UINT)st.Resources.size();
		if (!total) return true;
		D3D12_CPU_DESCRIPTOR_HANDLE cpu0;
		D3D12_GPU_DESCRIPTOR_HANDLE gpu0;
		if (!D->AllocViews(total, cpu0, gpu0)) return false;
		const UINT inc = D->ViewRing.Increment();
		ID3D12Device* dev = D->Device.Get();
		UINT at = 0;
		for (const auto& st : P->Stages)
		{
			if (st.ResourceParam < 0) continue;
			tables.push_back({ st.ResourceParam, { gpu0.ptr + (UINT64)at * inc } });
			for (const auto& slot : st.Resources)
			{
				const D3D12_CPU_DESCRIPTOR_HANDLE dst = { cpu0.ptr + (SIZE_T)at * inc };
				++at;
				const auto* v = value(slot.Element);
				switch (slot.Kind)
				{
				case GfxD3D12Shared::SlotKind::Cbv:
				{
					D3D12_CONSTANT_BUFFER_VIEW_DESC cd = {};
					if (v && v->Cbv.Gpu)
					{
						cd.BufferLocation = v->Cbv.Gpu;
						cd.SizeInBytes = (UINT)AlignUp((std::max<uint32_t>)(v->CbvSize, 16), 256);
					}
					else
					{
						cd.BufferLocation = D->ZeroCb->GetGPUVirtualAddress();
						cd.SizeInBytes = 65536;
					}
					dev->CreateConstantBufferView(&cd, dst);
					break;
				}
				case GfxD3D12Shared::SlotKind::Srv:
				{
					Srv* s = v ? AsSrv(v->View) : nullptr;
					if (s && s->V.Img)
					{
						const ViewInfo& vi = s->V;
						if (IsBoundTarget(vi.Img, vi.BaseMip, vi.Mips, vi.BaseLayer, vi.Layers))
						{
							// D3D11 은 렌더 타깃으로 묶인 텍스처를 셰이더에서 풀어 버린다 → 빈 텍스처
							D->Once("feedback:" + P->Name, "%s", (P->Name + ": a texture that is also the current render target was sampled - unbound like D3D11").c_str());
							s = nullptr;
						}
						else
							Transition(vi.Img->R, vi.BaseMip, vi.Mips, vi.BaseLayer, vi.Layers, vi.Img->DepthFormat ? kDepthRead : kShaderRead);
					}
					if (s && s->V.Buffer)
					{
						Buf* b = s->V.Buffer;
						if (!b->Shadow.empty())
						{
							// DYNAMIC 버퍼 SRV: 지금 링 자리로 디스크립터를 새로
							D3D12_GPU_VIRTUAL_ADDRESS gpu;
							Resource* r;
							ResolveBuffer(b, gpu, r);
							if (!b->HasLoc || !s->V.ElementBytes) { s = nullptr; }
							else
							{
								D3D12_SHADER_RESOURCE_VIEW_DESC sd = s->V.SrvDesc;
								sd.Buffer.FirstElement = (b->Loc.Offset + s->V.ByteOffset) / s->V.ElementBytes;
								dev->CreateShaderResourceView(b->Loc.Res, &sd, dst);
								break;
							}
						}
						else
							Transition(b->R, 0, 1, 0, 1, kBufferRead);
					}
					if (s && s->V.Cpu.ptr)
						dev->CopyDescriptorsSimple(1, dst, s->V.Cpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
					else
						dev->CopyDescriptorsSimple(1, dst, D->NullSrv[(std::min)(slot.Dimension, 11)], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
					break;
				}
				case GfxD3D12Shared::SlotKind::Uav:
				{
					Uav* u = v ? AsUav(v->Uav) : nullptr;
					if (u && u->V.Img)
						Transition(u->V.Img->R, u->V.BaseMip, u->V.Mips, u->V.BaseLayer, u->V.Layers, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
					else if (u && u->V.Buffer)
						Transition(u->V.Buffer->R, 0, 1, 0, 1, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
					if (u && u->V.Cpu.ptr)
					{
						dev->CopyDescriptorsSimple(1, dst, u->V.Cpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
						usesUav = true;
					}
					else
						dev->CopyDescriptorsSimple(1, dst, D->NullUav[(std::min)(slot.Dimension, 11)], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
					break;
				}
				default:
					break;
				}
			}
		}
		return true;
	}

	bool Ctx::PrepareDraw(bool indexed)
	{
		if (D->Lost) return false;
		if (D->AsyncOpen)
		{
			D->Once("draw-in-async", "%s", "draw inside BeginAsyncCompute/EndAsyncCompute - the async compute block was ended here");
			EndAsyncCompute();
		}
		if (!Prog)
		{
			D->Once("no-program", "%s", "draw without an applied effect pass - skipped");
			return false;
		}
		if (Prog->IsCompute)
		{
			D->Once("draw-compute", "%s", "draw with a compute pass applied - skipped");
			return false;
		}
		if (ViewportCount == 0 || Viewports[0].Width <= 0 || Viewports[0].Height <= 0)
		{
			D->Once("no-viewport", "%s", "draw without a viewport - skipped");
			return false;
		}
		InputLayout* layout = Layout && Layout->Api() == GfxApi::DirectX12 ? static_cast<InputLayout*>(Layout.Get()) : nullptr;

		// ---- 디스크립터 표 (제출할 수 있는 곳은 여기뿐 — 아래 장벽보다 먼저)
		thread_local std::vector<std::pair<int, D3D12_GPU_DESCRIPTOR_HANDLE>> tables;
		bool usesUav = false;
		if (!BuildTables(tables, usesUav)) return false;

		// ---- 타깃
		Rtv* colors[8] = {};
		UINT colorCount = 0;
		for (UINT i = 0; i < RtvCount; ++i)
			if ((colors[i] = AsRtv(Rtvs[i].Get())) != nullptr) colorCount = i + 1;
		Dsv* dsv = AsDsv(DsvView.Get());
		for (UINT i = 0; i < colorCount; ++i)
			if (colors[i])
				Transition(colors[i]->V.Img->R, colors[i]->V.BaseMip, 1, colors[i]->V.BaseLayer, colors[i]->V.Layers, D3D12_RESOURCE_STATE_RENDER_TARGET);
		if (dsv)
			Transition(dsv->V.Img->R, dsv->V.BaseMip, 1, dsv->V.BaseLayer, dsv->V.Layers, dsv->V.ReadOnly ? kDepthRead : D3D12_RESOURCE_STATE_DEPTH_WRITE);

		// ---- 정점 · 인덱스 버퍼
		D3D12_VERTEX_BUFFER_VIEW vbs[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
		UINT slotMask = 0;
		if (layout)
			for (const auto& e : layout->Elements)
				slotMask |= 1u << e.InputSlot;
		for (UINT s = 0; s < D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT; ++s)
		{
			if (!(slotMask & (1u << s))) continue;
			D3D12_GPU_VIRTUAL_ADDRESS gpu;
			Resource* r;
			ResolveBuffer(Vbs[s].Buf.Get(), gpu, r);
			if (!gpu)
			{
				D->Once("no-vb", "%s", "draw with an empty vertex buffer slot - skipped");
				return false;
			}
			if (r) Transition(*r, 0, 1, 0, 1, kBufferRead);
			Buf* b = BufOf(Vbs[s].Buf.Get());
			const UINT size = b->Desc.ByteWidth;
			vbs[s] = { gpu + Vbs[s].Offset, size > Vbs[s].Offset ? size - Vbs[s].Offset : 0, Vbs[s].Stride };
		}
		D3D12_INDEX_BUFFER_VIEW ib = {};
		if (indexed)
		{
			D3D12_GPU_VIRTUAL_ADDRESS gpu;
			Resource* r;
			ResolveBuffer(Ib.Get(), gpu, r);
			if (!gpu) return false;
			if (r) Transition(*r, 0, 1, 0, 1, kBufferRead);
			const UINT size = BufOf(Ib.Get())->Desc.ByteWidth;
			ib = { gpu + IbOffset, size > IbOffset ? size - IbOffset : 0, IbFormat == DXGI_FORMAT_R32_UINT ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT };
		}
		if (UavDirty) UavBarrier();
		FlushBarriers();

		// ---- 파이프라인
		const D3D11_RASTERIZER_DESC& rs = Rs ? static_cast<Rasterizer*>(Rs.Get())->Dsc : DefaultRs();
		const D3D11_BLEND_DESC& bs = Bs ? static_cast<Blend*>(Bs.Get())->Dsc : DefaultBs();
		const D3D11_DEPTH_STENCIL_DESC& ds = Ds ? static_cast<DepthStencil*>(Ds.Get())->Dsc : DefaultDs();
		PipelineKey key;
		memset(&key, 0, sizeof(key));
		key.Program = Prog->Id;
		key.Layout = layout ? layout->Id : 0;
		key.Raster = Rs ? static_cast<Rasterizer*>(Rs.Get())->Hash : DefaultRsHash();
		key.Blend = Bs ? static_cast<Blend*>(Bs.Get())->Hash : DefaultBsHash();
		key.Depth = Ds ? static_cast<DepthStencil*>(Ds.Get())->Hash : DefaultDsHash();
		key.ColorCount = (uint8_t)colorCount;
		uint32_t samples = 1;
		for (UINT i = 0; i < colorCount; ++i)
			if (colors[i])
			{
				key.Colors[i] = colors[i]->V.Format;
				samples = colors[i]->V.Img->Samples;
			}
		if (dsv)
		{
			key.DepthFormat = dsv->V.Format;
			samples = dsv->V.Img->Samples;
			key.ReadOnlyDepth = dsv->V.ReadOnly ? 1 : 0;
		}
		key.Samples = (uint8_t)samples;
		key.SampleMask = SampleMask;
		key.TopologyType = (uint8_t)TopologyType(Topo);
		key.Cut = IsStrip(Topo) && indexed ? (IbFormat == DXGI_FORMAT_R32_UINT ? 2 : 1) : 0;
		ID3D12PipelineState* pipe = nullptr;
		if (LastPipe && key == LastKey) pipe = LastPipe;
		else if (!D->GetPipeline(key, Prog.Get(), layout, rs, bs, ds, &pipe)) return false;
		LastKey = key;
		LastPipe = pipe;

		// ---- 묶기 (새 명령 목록이면 모두 다시)
		ID3D12GraphicsCommandList* cl = D->Cmd();
		ResetBound(*this);
		if (BoundRoot != Prog->Root.Get())
		{
			cl->SetGraphicsRootSignature(Prog->Root.Get());
			BoundRoot = Prog->Root.Get();
		}
		if (BoundPipeline != pipe)
		{
			cl->SetPipelineState(pipe);
			BoundPipeline = pipe;
		}
		for (const auto& [param, gpu] : tables)
			cl->SetGraphicsRootDescriptorTable((UINT)param, gpu);
		if (BoundTargets != TargetsSerial)
		{
			D3D12_CPU_DESCRIPTOR_HANDLE rt[8];
			for (UINT i = 0; i < colorCount; ++i)
				rt[i] = colors[i] ? colors[i]->V.Cpu : D->NullRtv;
			const D3D12_CPU_DESCRIPTOR_HANDLE dh = dsv ? dsv->V.Cpu : D3D12_CPU_DESCRIPTOR_HANDLE{};
			cl->OMSetRenderTargets(colorCount, colorCount ? rt : nullptr, FALSE, dsv ? &dh : nullptr);
			BoundTargets = TargetsSerial;
		}
		const D3D12_PRIMITIVE_TOPOLOGY topo = (D3D12_PRIMITIVE_TOPOLOGY)Topo;
		if (topo != BoundTopo)
		{
			cl->IASetPrimitiveTopology(topo);
			BoundTopo = topo;
		}
		for (UINT s = 0; s < D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT; ++s)
		{
			if (!(slotMask & (1u << s))) continue;
			if (memcmp(&BoundVbs[s], &vbs[s], sizeof(vbs[s])) == 0) continue;
			cl->IASetVertexBuffers(s, 1, &vbs[s]);
			BoundVbs[s] = vbs[s];
		}
		if (indexed && memcmp(&BoundIb, &ib, sizeof(ib)) != 0)
		{
			cl->IASetIndexBuffer(&ib);
			BoundIb = ib;
		}

		// ---- 동적 상태 (뷰포트 · 가위 · 블렌드 상수 · 스텐실 기준). D3D12 는 가위가 늘 켜져 있다 → 끈 래스터 상태는 큰 사각형
		if (DynamicDirty)
		{
			D3D12_VIEWPORT vp[16];
			D3D12_RECT sc[16];
			const UINT n = (std::max)(1u, ViewportCount);
			for (UINT i = 0; i < n; ++i)
			{
				const D3D11_VIEWPORT& v = Viewports[(std::min)(i, ViewportCount - 1)];
				vp[i] = { v.TopLeftX, v.TopLeftY, v.Width, v.Height, v.MinDepth, v.MaxDepth };
				if (rs.ScissorEnable && i < ScissorCount)
					sc[i] = { Scissors[i].left, Scissors[i].top, Scissors[i].right, Scissors[i].bottom };
				else
					sc[i] = { 0, 0, D3D12_VIEWPORT_BOUNDS_MAX, D3D12_VIEWPORT_BOUNDS_MAX };
			}
			cl->RSSetViewports(n, vp);
			cl->RSSetScissorRects(n, sc);
			cl->OMSetBlendFactor(BlendFactor);
			cl->OMSetStencilRef(StencilRef);
			DynamicDirty = false;
		}
		if (usesUav) UavDirty = true;
		++D->DrawsSinceSubmit;
		return true;
	}

	void Ctx::ApplyPredicate(ID3D12GraphicsCommandList* cl, bool on)
	{
		Query* q = AsQuery(Predicate.Get());
		if (on && q && q->Predicate.Res && q->Recorded)
		{
			Transition(q->Predicate, 0, 1, 0, 1, D3D12_RESOURCE_STATE_PREDICATION);
			FlushBarriers();
			cl->SetPredication(q->Predicate.Res.Get(), 0, D3D12_PREDICATION_OP_EQUAL_ZERO);   // 0 (가려짐) 이면 건너뜀
			Predicated = true;
		}
		else if (!on && Predicated)
		{
			cl->SetPredication(nullptr, 0, D3D12_PREDICATION_OP_EQUAL_ZERO);
			Predicated = false;
		}
	}

	void Ctx::DrawInstanced(UINT c, UINT n, UINT s, UINT si)
	{
		if (!PrepareDraw(false)) return;
		ID3D12GraphicsCommandList* cl = D->Cmd();
		if (Predicate) ApplyPredicate(cl, true);
		cl->DrawInstanced(c, n, s, si);
		if (Predicated) ApplyPredicate(cl, false);   // 예측은 그리기에만 (복사 · 지우기는 D3D12 에서도 걸린다)
	}

	void Ctx::DrawIndexedInstanced(UINT c, UINT n, UINT s, INT b, UINT si)
	{
		if (!Ib || !PrepareDraw(true)) return;
		ID3D12GraphicsCommandList* cl = D->Cmd();
		if (Predicate) ApplyPredicate(cl, true);
		cl->DrawIndexedInstanced(c, n, s, b, si);
		if (Predicated) ApplyPredicate(cl, false);
	}

	void Ctx::DrawAuto() { D->Once("drawauto", "%s", "DrawAuto (stream output) is not supported"); }

	void Ctx::Dispatch(UINT x, UINT y, UINT z)
	{
		if (D->Lost || !x || !y || !z) return;
		if (!Prog || !Prog->IsCompute || !Prog->Compute)
		{
			D->Once("dispatch-noprog", "%s", "Dispatch without an applied compute pass - skipped");
			return;
		}
		thread_local std::vector<std::pair<int, D3D12_GPU_DESCRIPTOR_HANDLE>> tables;
		bool usesUav = false;
		if (!BuildTables(tables, usesUav)) return;
		if (UavDirty) UavBarrier();
		FlushBarriers();
		ID3D12GraphicsCommandList* cl = D->Cmd();
		if (D->AsyncOpen)
		{
			// 컴퓨트 큐 목록: 묶음을 따라가지 않고 늘 (디스패치 수가 적다)
			cl->SetComputeRootSignature(Prog->Root.Get());
			cl->SetPipelineState(Prog->Compute.Get());
		}
		else
		{
			ResetBound(*this);
			if (BoundComputeRoot != Prog->Root.Get())
			{
				cl->SetComputeRootSignature(Prog->Root.Get());
				BoundComputeRoot = Prog->Root.Get();
			}
			cl->SetPipelineState(Prog->Compute.Get());
			BoundPipeline = Prog->Compute.Get();   // PSO 는 그래픽과 하나 — 다음 그리기가 다시 묶는다
		}
		for (const auto& [param, gpu] : tables)
			cl->SetComputeRootDescriptorTable((UINT)param, gpu);
		cl->Dispatch(x, y, z);
		UavDirty = true;   // 다음 읽기 · 쓰기 (간접 인자 · 정점 · SRV · 다음 디스패치) 앞에서 UAV 장벽
	}

	bool Ctx::DrawIndexedInstancedIndirect(GfxBuffer* args, UINT offset)
	{
		Buf* b = BufOf(args);
		if (!b || !b->R.Res || (offset & 3)) return false;
		if (!Ib || !PrepareDraw(true)) return true;
		Transition(b->R, 0, 1, 0, 1, kBufferRead);   // 간접 인자 (PrepareDraw 뒤 — 거기서 제출이 일어나도 상태가 맞게)
		FlushBarriers();
		ID3D12GraphicsCommandList* cl = D->Cmd();
		if (Predicate) ApplyPredicate(cl, true);
		cl->ExecuteIndirect(D->DrawIndexedSig.Get(), 1, b->R.Res.Get(), offset, nullptr, 0);   // D3D 인자 배치 그대로 (20 바이트)
		if (Predicated) ApplyPredicate(cl, false);
		return true;
	}

	bool Ctx::DrawInstancedIndirect(GfxBuffer* args, UINT offset)
	{
		Buf* b = BufOf(args);
		if (!b || !b->R.Res || (offset & 3)) return false;
		if (!PrepareDraw(false)) return true;
		Transition(b->R, 0, 1, 0, 1, kBufferRead);
		FlushBarriers();
		ID3D12GraphicsCommandList* cl = D->Cmd();
		if (Predicate) ApplyPredicate(cl, true);
		cl->ExecuteIndirect(D->DrawSig.Get(), 1, b->R.Res.Get(), offset, nullptr, 0);
		if (Predicated) ApplyPredicate(cl, false);
		return true;
	}

	bool Ctx::ClearUnorderedAccessViewUint(GfxUnorderedAccessView* view, const UINT values[4])
	{
		Uav* u = AsUav(view);
		if (!u || !u->V.Cpu.ptr) return false;
		if (D->Lost) return true;
		D3D12_CPU_DESCRIPTOR_HANDLE cpu;
		D3D12_GPU_DESCRIPTOR_HANDLE gpu;
		if (!D->AllocViews(1, cpu, gpu)) return false;
		D->Device->CopyDescriptorsSimple(1, cpu, u->V.Cpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		Resource& r = u->V.Img ? u->V.Img->R : u->V.Buffer->R;
		if (u->V.Img) Transition(r, u->V.BaseMip, 1, u->V.BaseLayer, u->V.Layers, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		else Transition(r, 0, 1, 0, 1, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		if (UavDirty) UavBarrier();
		FlushBarriers();
		D->Cmd()->ClearUnorderedAccessViewUint(gpu, u->V.Cpu, r.Res.Get(), values, 0, nullptr);
		UavDirty = true;
		return true;
	}

	bool Ctx::SetPredication(GfxQuery* predicate, BOOL value)
	{
		if (!predicate)
		{
			Predicate = nullptr;
			return true;
		}
		Query* q = AsQuery(predicate);
		if (!q || q->Dsc.Query != D3D11_QUERY_OCCLUSION_PREDICATE || value || !q->Predicate.Res)
			return false;   // value TRUE (보이면 건너뜀) 는 쓰지 않는다
		if (!q->Recorded)
		{
			Predicate = nullptr;
			return false;   // 결과가 없다 — 예측 없이 그린다
		}
		Predicate = predicate;
		return true;
	}

	// ============================================================ 지우기 (D3D: 쓰기 마스크 · 가위와 상관없다)
	void Ctx::ClearRenderTargetView(GfxRenderTargetView* view, const FLOAT c[4])
	{
		Rtv* v = AsRtv(view);
		if (!v || !v->V.Img || D->Lost) return;
		EndAsyncCompute();
		Transition(v->V.Img->R, v->V.BaseMip, 1, v->V.BaseLayer, v->V.Layers, D3D12_RESOURCE_STATE_RENDER_TARGET);
		FlushBarriers();
		D->Cmd()->ClearRenderTargetView(v->V.Cpu, c, 0, nullptr);   // sRGB 뷰면 D3D12 가 바꿔 저장한다 (D3D11 과 같음)
	}

	void Ctx::ClearDepthStencilView(GfxDepthStencilView* view, UINT flags, FLOAT depth, UINT8 stencil)
	{
		Dsv* v = AsDsv(view);
		if (!v || !v->V.Img || D->Lost) return;
		if (v->V.ReadOnly)
		{
			D->Once("clear-readonly-dsv", "%s", "ClearDepthStencilView on a read-only depth view - skipped");
			return;
		}
		D3D12_CLEAR_FLAGS f = (D3D12_CLEAR_FLAGS)0;
		if (flags & D3D11_CLEAR_DEPTH) f |= D3D12_CLEAR_FLAG_DEPTH;
		if ((flags & D3D11_CLEAR_STENCIL) && HasStencil(v->V.Format)) f |= D3D12_CLEAR_FLAG_STENCIL;
		if (!f) return;
		EndAsyncCompute();
		Transition(v->V.Img->R, v->V.BaseMip, 1, v->V.BaseLayer, v->V.Layers, D3D12_RESOURCE_STATE_DEPTH_WRITE);
		FlushBarriers();
		D->Cmd()->ClearDepthStencilView(v->V.Cpu, f, depth, stencil, 0, nullptr);
	}

	// ============================================================ 자원
	HRESULT Ctx::Map(GfxResource* r, UINT sub, D3D11_MAP type, UINT flags, D3D11_MAPPED_SUBRESOURCE* m)
	{
		if (!m || D->Lost) return E_FAIL;
		*m = {};
		auto waitFor = [&](uint64_t serial) -> bool {
			if (!serial || serial <= D->Completed || D->IsDone(serial)) return true;
			if (flags & D3D11_MAP_FLAG_DO_NOT_WAIT)
			{
				if (serial >= D->Recording()) D->Submit(false);
				return false;
			}
			D->WaitSerial(serial);
			return true;
		};
		if (Buf* b = BufOf(r))
		{
			if (!b->Shadow.empty())
			{
				// DYNAMIC: CPU 사본 (Unmap 때 링으로)
				m->pData = b->Shadow.data();
				m->RowPitch = m->DepthPitch = b->Desc.ByteWidth;
				return S_OK;
			}
			if (b->Desc.Usage != D3D11_USAGE_STAGING || !b->Mapped)
			{
				D->Once("map-default", "%s", "Map on a DEFAULT/IMMUTABLE buffer is not allowed");
				return E_INVALIDARG;
			}
			if (!waitFor(b->R.LastUse)) return DXGI_ERROR_WAS_STILL_DRAWING;
			m->pData = b->Mapped;
			m->RowPitch = m->DepthPitch = b->Desc.ByteWidth;
			return S_OK;
		}
		Image* t = ImageOf(r);
		if (!t || sub >= t->R.Layers * t->R.Mips) return E_INVALIDARG;
		if (t->Staging)
		{
			if (!waitFor(t->R.LastUse)) return DXGI_ERROR_WAS_STILL_DRAWING;
			const auto& fp = t->Footprints[sub];
			m->pData = t->StagingCpu + fp.Offset;
			m->RowPitch = fp.Footprint.RowPitch;
			m->DepthPitch = fp.Footprint.RowPitch * t->Rows[sub];
			return S_OK;
		}
		if (t->Usage == D3D11_USAGE_DYNAMIC && (type == D3D11_MAP_WRITE_DISCARD || type == D3D11_MAP_WRITE_NO_OVERWRITE || type == D3D11_MAP_WRITE))
		{
			const D3D12_RESOURCE_DESC rd = t->R.Res->GetDesc();
			D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp;
			UINT rows;
			UINT64 rowBytes, total;
			D->Device->GetCopyableFootprints(&rd, sub, 1, 0, &fp, &rows, &rowBytes, &total);
			UploadLoc loc;
			if (!D->Upload(total, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT, loc)) return E_OUTOFMEMORY;
			fp.Offset = loc.Offset;
			t->Pending[sub] = { loc, fp };
			m->pData = loc.Cpu;
			m->RowPitch = fp.Footprint.RowPitch;
			m->DepthPitch = fp.Footprint.RowPitch * rows;
			return S_OK;
		}
		D->Once("map-texture", "%s", "Map on a DEFAULT texture is not allowed");
		return E_INVALIDARG;
	}

	void Ctx::Unmap(GfxResource* r, UINT sub)
	{
		if (Buf* b = BufOf(r))
		{
			if (!b->Shadow.empty())
			{
				const UINT64 align = b->Desc.StructureByteStride ? Lcm(256, b->Desc.StructureByteStride) : 256;
				b->HasLoc = D->Upload(b->Shadow.size(), align, b->Loc);
				if (b->HasLoc) memcpy(b->Loc.Cpu, b->Shadow.data(), b->Shadow.size());
			}
			return;
		}
		Image* t = ImageOf(r);
		if (!t) return;
		auto it = t->Pending.find(sub);
		if (it == t->Pending.end()) return;
		const auto [loc, fp] = it->second;
		t->Pending.erase(it);
		const UINT mip = sub % t->R.Mips, layer = sub / t->R.Mips;
		Transition(t->R, mip, 1, layer, 1, D3D12_RESOURCE_STATE_COPY_DEST);
		FlushBarriers();
		D3D12_TEXTURE_COPY_LOCATION src = {};
		src.pResource = loc.Res;
		src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		src.PlacedFootprint = fp;
		D3D12_TEXTURE_COPY_LOCATION dst = {};
		dst.pResource = t->R.Res.Get();
		dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		dst.SubresourceIndex = sub;
		D->Cmd()->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
	}

	void Ctx::UpdateSubresource(GfxResource* r, UINT sub, const D3D11_BOX* box, const void* data, UINT row, UINT depth)
	{
		if (!data || D->Lost) return;
		if (Buf* b = BufOf(r))
		{
			const UINT off = box ? box->left : 0;
			const UINT size = box ? box->right - box->left : b->Desc.ByteWidth;
			if (!size || off + size > b->Desc.ByteWidth) return;
			if (!b->Shadow.empty())
			{
				memcpy(b->Shadow.data() + off, data, size);
				b->HasLoc = false;
				return;
			}
			if (b->Desc.Usage == D3D11_USAGE_STAGING)
			{
				D->WaitSerial(b->R.LastUse);
				memcpy(b->Mapped + off, data, size);
				return;
			}
			UploadLoc loc;
			if (!D->Upload(size, 16, loc)) return;
			memcpy(loc.Cpu, data, size);
			Transition(b->R, 0, 1, 0, 1, D3D12_RESOURCE_STATE_COPY_DEST);
			FlushBarriers();
			D->Cmd()->CopyBufferRegion(b->R.Res.Get(), off, loc.Res, loc.Offset, size);
			return;
		}
		Image* t = ImageOf(r);
		if (!t || sub >= t->R.Layers * t->R.Mips) return;
		if (t->Staging)
		{
			D->WaitSerial(t->R.LastUse);
			const auto& fp = t->Footprints[sub];
			for (UINT z = 0; z < fp.Footprint.Depth; ++z)
				for (UINT y = 0; y < t->Rows[sub]; ++y)
					memcpy(t->StagingCpu + fp.Offset + ((UINT64)z * t->Rows[sub] + y) * fp.Footprint.RowPitch,
						static_cast<const uint8_t*>(data) + (size_t)z * depth + (size_t)y * row, (size_t)t->RowBytes[sub]);
			return;
		}
		const UINT mip = sub % t->R.Mips, layer = sub / t->R.Mips;
		Transition(t->R, mip, 1, layer, 1, D3D12_RESOURCE_STATE_COPY_DEST);
		FlushBarriers();
		D->UploadImage(*t, sub, box, data, row, depth);
	}

	void Ctx::CopyImageRegion(Image& dst, UINT dstSub, UINT x, UINT y, UINT z, Image& src, UINT srcSub, const D3D11_BOX* box)
	{
		const UINT sm = srcSub % src.R.Mips, sl = srcSub / src.R.Mips, dm = dstSub % dst.R.Mips, dl = dstSub / dst.R.Mips;
		Transition(src.R, sm, 1, sl, 1, D3D12_RESOURCE_STATE_COPY_SOURCE);
		Transition(dst.R, dm, 1, dl, 1, D3D12_RESOURCE_STATE_COPY_DEST);
		FlushBarriers();
		const UINT planes = (std::min)(src.R.Planes, dst.R.Planes);
		for (UINT p = 0; p < planes; ++p)
		{
			D3D12_TEXTURE_COPY_LOCATION s = {}, d = {};
			s.pResource = src.R.Res.Get();
			s.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			s.SubresourceIndex = src.R.Sub(sm, sl, p);
			d.pResource = dst.R.Res.Get();
			d.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			d.SubresourceIndex = dst.R.Sub(dm, dl, p);
			D3D12_BOX b;
			if (box) b = { box->left, box->top, box->front, box->right, box->bottom, box->back };
			// 깊이 · 스텐실 · MSAA 는 서브리소스 전체만 (상자 없이)
			D->Cmd()->CopyTextureRegion(&d, x, y, z, &s, box && !src.DepthFormat && src.Samples == 1 ? &b : nullptr);
		}
	}

	void Ctx::CopyResource(GfxResource* dst, GfxResource* src)
	{
		if (D->Lost) return;
		Buf* bd = BufOf(dst), * bs = BufOf(src);
		if (bd && bs)
		{
			const UINT size = (std::min)(bs->Desc.ByteWidth, bd->Desc.ByteWidth);
			const D3D11_BOX box = { 0, 0, 0, size, 1, 1 };
			CopySubresourceRegion(dst, 0, 0, 0, 0, src, 0, &box);
			return;
		}
		Image* td = ImageOf(dst), * ts = ImageOf(src);
		if (!td || !ts) return;
		if (!td->Staging && !ts->Staging && td->R.Res && ts->R.Res)
		{
			// 같은 모양의 GPU 텍스처 둘: 한 번에 (깊이 · 스텐실 면 포함)
			TransitionAll(ts->R, D3D12_RESOURCE_STATE_COPY_SOURCE);
			TransitionAll(td->R, D3D12_RESOURCE_STATE_COPY_DEST);
			FlushBarriers();
			D->Cmd()->CopyResource(td->R.Res.Get(), ts->R.Res.Get());
			return;
		}
		const UINT mips = (std::min)(td->R.Mips, ts->R.Mips), layers = (std::min)(td->R.Layers, ts->R.Layers);
		for (UINT layer = 0; layer < layers; ++layer)
			for (UINT m = 0; m < mips; ++m)
				CopySubresourceRegion(dst, layer * td->R.Mips + m, 0, 0, 0, src, layer * ts->R.Mips + m, nullptr);
	}

	void Ctx::CopySubresourceRegion(GfxResource* dst, UINT dstSub, UINT x, UINT y, UINT z, GfxResource* src, UINT srcSub, const D3D11_BOX* box)
	{
		if (D->Lost) return;
		Buf* bd = BufOf(dst), * bs = BufOf(src);
		if (bd && bs)
		{
			if (!bd->Shadow.empty()) { D->Once("copy-dynamic", "%s", "copy into a DYNAMIC buffer is not supported"); return; }
			D3D12_GPU_VIRTUAL_ADDRESS sg;
			Resource* sr;
			ResolveBuffer(src, sg, sr);
			if (!sg || !bd->R.Res) return;
			const UINT off = box ? box->left : 0, size = box ? box->right - box->left : bs->Desc.ByteWidth;
			if (!size || off + size > bs->Desc.ByteWidth || x + size > bd->Desc.ByteWidth) return;
			if (sr) Transition(*sr, 0, 1, 0, 1, kBufferRead);
			Transition(bd->R, 0, 1, 0, 1, D3D12_RESOURCE_STATE_COPY_DEST);
			FlushBarriers();
			ID3D12Resource* srcRes = sr ? sr->Res.Get() : bs->Loc.Res;
			const UINT64 srcOff = (sr ? 0 : bs->Loc.Offset) + off;
			D->Cmd()->CopyBufferRegion(bd->R.Res.Get(), x, srcRes, srcOff, size);
			if (bd->Desc.Usage == D3D11_USAGE_STAGING) bd->R.LastUse = D->Recording();
			return;
		}
		Image* td = ImageOf(dst), * ts = ImageOf(src);
		if (!td || !ts || dstSub >= td->R.Layers * td->R.Mips || srcSub >= ts->R.Layers * ts->R.Mips) return;
		if (!td->Staging && !ts->Staging)
		{
			CopyImageRegion(*td, dstSub, x, y, z, *ts, srcSub, box);
			return;
		}
		// 텍스처 ↔ STAGING (CPU 쪽 버퍼의 서브리소스 배치)
		D3D12_BOX b;
		if (box) b = { box->left, box->top, box->front, box->right, box->bottom, box->back };
		D3D12_TEXTURE_COPY_LOCATION s = {}, d = {};
		if (!ts->Staging && td->Staging)
		{
			const UINT sm = srcSub % ts->R.Mips, sl = srcSub / ts->R.Mips;
			Transition(ts->R, sm, 1, sl, 1, D3D12_RESOURCE_STATE_COPY_SOURCE);
			Transition(td->R, 0, 1, 0, 1, D3D12_RESOURCE_STATE_COPY_DEST);
			FlushBarriers();
			s.pResource = ts->R.Res.Get();
			s.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			s.SubresourceIndex = srcSub;
			d.pResource = td->Staging.Get();
			d.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
			d.PlacedFootprint = td->Footprints[dstSub];
			D->Cmd()->CopyTextureRegion(&d, x, y, z, &s, box && !ts->DepthFormat ? &b : nullptr);
			td->R.LastUse = D->Recording();
		}
		else if (ts->Staging && !td->Staging)
		{
			const UINT dm = dstSub % td->R.Mips, dl = dstSub / td->R.Mips;
			Transition(ts->R, 0, 1, 0, 1, kBufferRead);
			Transition(td->R, dm, 1, dl, 1, D3D12_RESOURCE_STATE_COPY_DEST);
			FlushBarriers();
			s.pResource = ts->Staging.Get();
			s.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
			s.PlacedFootprint = ts->Footprints[srcSub];
			d.pResource = td->R.Res.Get();
			d.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			d.SubresourceIndex = dstSub;
			D->Cmd()->CopyTextureRegion(&d, x, y, z, &s, box ? &b : nullptr);
			ts->R.LastUse = D->Recording();
		}
		else
			D->Once("copy-staging", "%s", "staging to staging texture copies are not supported");
	}

	void Ctx::GenerateMips(GfxShaderResourceView* view)
	{
		Srv* v = AsSrv(view);
		if (!v || !v->V.Img || v->V.Mips < 2 || D->Lost) return;
		Image& img = *v->V.Img;
		const bool volume = img.Dim == D3D12_RESOURCE_DIMENSION_TEXTURE3D;
		if ((img.Dim != D3D12_RESOURCE_DIMENSION_TEXTURE2D && !volume) || img.Samples > 1 || IsBlockCompressed(v->V.Format) || IsIntegerFormat(v->V.Format))
		{
			D->Once("genmips-kind", "%s", "GenerateMips: only 2D (array / cube) and 3D float textures are supported");
			return;
		}
		if (!(img.Bind & D3D11_BIND_RENDER_TARGET))
		{
			D->Once("genmips-rt", "%s", "GenerateMips on a texture without D3D11_RESOURCE_MISC_GENERATE_MIPS / render target bind - skipped");
			return;
		}
		ID3D12PipelineState* pso = D->MipPipeline(v->V.Format, volume);
		if (!pso) return;
		EndAsyncCompute();
		Dev* d = D.Get();
		// 밉 하나 (2D: 배열 조각 하나, 3D: 깊이 조각 하나) 를 그린다
		auto drawOne = [&](UINT m, const D3D12_SHADER_RESOURCE_VIEW_DESC& sd, const D3D12_RENDER_TARGET_VIEW_DESC& rd, UINT constant) -> bool {
			D3D12_CPU_DESCRIPTOR_HANDLE cpu;
			D3D12_GPU_DESCRIPTOR_HANDLE gpu;
			if (!D->AllocViews(1, cpu, gpu)) return false;
			D->Device->CreateShaderResourceView(img.R.Res.Get(), &sd, cpu);
			const D3D12_CPU_DESCRIPTOR_HANDLE rtv = D->RtvHeap.Alloc();
			D->Device->CreateRenderTargetView(img.R.Res.Get(), &rd, rtv);
			D->Defer([d, rtv]() { d->RtvHeap.Free(rtv); });
			FlushBarriers();
			ID3D12GraphicsCommandList* cl = D->Cmd();
			cl->SetGraphicsRootSignature(D->MipRoot.Get());
			cl->SetPipelineState(pso);
			cl->SetGraphicsRoot32BitConstant(0, constant, 0);
			cl->SetGraphicsRootDescriptorTable(1, gpu);
			cl->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
			const D3D12_VIEWPORT vp = { 0, 0, (float)img.MipW(m), (float)img.MipH(m), 0, 1 };
			const D3D12_RECT sc = { 0, 0, (LONG)img.MipW(m), (LONG)img.MipH(m) };
			cl->RSSetViewports(1, &vp);
			cl->RSSetScissorRects(1, &sc);
			cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			cl->DrawInstanced(3, 1, 0, 0);
			return true;
		};
		if (volume)
		{
			for (UINT m = v->V.BaseMip + 1; m < v->V.BaseMip + v->V.Mips; ++m)
			{
				Transition(img.R, m - 1, 1, 0, 1, kShaderRead);
				Transition(img.R, m, 1, 0, 1, D3D12_RESOURCE_STATE_RENDER_TARGET);
				D3D12_SHADER_RESOURCE_VIEW_DESC sd = {};
				sd.Format = v->V.Format;
				sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
				sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				sd.Texture3D = { m - 1, 1, 0.0f };
				const UINT depth = img.MipD(m);
				for (UINT z = 0; z < depth; ++z)
				{
					D3D12_RENDER_TARGET_VIEW_DESC rd = {};
					rd.Format = v->V.Format;
					rd.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
					rd.Texture3D = { m, z, 1 };
					const float w = (z + 0.5f) / depth;
					UINT bits;
					memcpy(&bits, &w, 4);
					if (!drawOne(m, sd, rd, bits)) return;
				}
			}
		}
		else
			for (UINT layer = v->V.BaseLayer; layer < v->V.BaseLayer + v->V.Layers; ++layer)
				for (UINT m = v->V.BaseMip + 1; m < v->V.BaseMip + v->V.Mips; ++m)
				{
					Transition(img.R, m - 1, 1, layer, 1, kShaderRead);
					Transition(img.R, m, 1, layer, 1, D3D12_RESOURCE_STATE_RENDER_TARGET);
					D3D12_SHADER_RESOURCE_VIEW_DESC sd = {};
					sd.Format = v->V.Format;
					sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
					sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
					sd.Texture2DArray = { m - 1, 1, layer, 1, 0, 0.0f };
					D3D12_RENDER_TARGET_VIEW_DESC rd = {};
					rd.Format = v->V.Format;
					rd.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
					rd.Texture2DArray = { m, layer, 1, 0 };
					if (!drawOne(m, sd, rd, 0)) return;
				}
		Invalidate();   // 다음 그리기가 루트 · PSO · 타깃 · 뷰포트를 다시
	}

	// ============================================================ 쿼리
	void Ctx::Begin(GfxQuery* q)
	{
		Query* g = AsQuery(q);
		if (!g || D->Lost || !g->Heap) return;
		if (g->Type == D3D12_QUERY_TYPE_TIMESTAMP) return;
		EndAsyncCompute();   // 오클루전 · 파이프라인 통계는 그래픽 큐에서
		if (g->Open) D->Cmd()->EndQuery(g->Heap.Get(), g->Type, 0);   // 짝이 없던 Begin
		D->Cmd()->BeginQuery(g->Heap.Get(), g->Type, 0);
		g->Open = true;
	}

	void Ctx::End(GfxQuery* q)
	{
		Query* g = AsQuery(q);
		if (!g || D->Lost) return;
		if (g->Heap && (g->Open || g->Type == D3D12_QUERY_TYPE_TIMESTAMP))
		{
			if (g->Type != D3D12_QUERY_TYPE_TIMESTAMP) EndAsyncCompute();   // 타임스탬프는 지금 목록 (컴퓨트 큐도 된다)
			ID3D12GraphicsCommandList* cl = D->Cmd();
			cl->EndQuery(g->Heap.Get(), g->Type, 0);
			cl->ResolveQueryData(g->Heap.Get(), g->Type, 0, 1, g->Readback.Get(), 0);
			if (g->Predicate.Res)
			{
				Transition(g->Predicate, 0, 1, 0, 1, D3D12_RESOURCE_STATE_COPY_DEST);
				FlushBarriers();
				cl->ResolveQueryData(g->Heap.Get(), g->Type, 0, 1, g->Predicate.Res.Get(), 0);
			}
			g->Open = false;
			g->Recorded = true;
		}
		g->Serial = D->Recording();
	}

	HRESULT Ctx::GetData(GfxQuery* q, void* data, UINT size, UINT flags)
	{
		Query* g = AsQuery(q);
		if (!g) return E_FAIL;
		if (!g->Serial) return S_FALSE;
		if (!D->IsDone(g->Serial))
		{
			if (D->Lost) return E_FAIL;
			if (g->Serial >= D->Recording() && !(flags & D3D11_ASYNC_GETDATA_DONOTFLUSH)) D->Submit(false);
			return S_FALSE;
		}
		const bool have = g->Recorded && g->ReadbackCpu;
		switch (g->Dsc.Query)
		{
		case D3D11_QUERY_TIMESTAMP_DISJOINT:
			if (data && size >= sizeof(D3D11_QUERY_DATA_TIMESTAMP_DISJOINT))
				*static_cast<D3D11_QUERY_DATA_TIMESTAMP_DISJOINT*>(data) = { D->TimestampFrequency, FALSE };
			return S_OK;
		case D3D11_QUERY_TIMESTAMP:
		{
			uint64_t v = 0;
			if (have) memcpy(&v, g->ReadbackCpu, 8);
			if (data && size >= sizeof(UINT64)) *static_cast<UINT64*>(data) = v;
			return S_OK;
		}
		case D3D11_QUERY_OCCLUSION:
		case D3D11_QUERY_OCCLUSION_PREDICATE:
		{
			uint64_t v = 1;   // 기록하지 못한 쿼리 = 보임
			if (have) memcpy(&v, g->ReadbackCpu, 8);
			if (g->Dsc.Query == D3D11_QUERY_OCCLUSION_PREDICATE)
			{
				if (data && size >= sizeof(BOOL)) *static_cast<BOOL*>(data) = v != 0;
			}
			else if (data && size >= sizeof(UINT64)) *static_cast<UINT64*>(data) = v;
			return S_OK;
		}
		case D3D11_QUERY_PIPELINE_STATISTICS:
		{
			D3D11_QUERY_DATA_PIPELINE_STATISTICS st = {};
			static_assert(sizeof(D3D11_QUERY_DATA_PIPELINE_STATISTICS) == sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS), "same layout");
			if (have) memcpy(&st, g->ReadbackCpu, sizeof(st));
			if (data && size >= sizeof(st)) *static_cast<D3D11_QUERY_DATA_PIPELINE_STATISTICS*>(data) = st;
			return S_OK;
		}
		case D3D11_QUERY_EVENT:
			if (data && size >= sizeof(BOOL)) *static_cast<BOOL*>(data) = TRUE;
			return S_OK;
		default:
			if (data && size) memset(data, 0, size);
			return S_OK;
		}
	}

	void Ctx::Flush() { D->Submit(false); }

	void Ctx::ClearState()
	{
		Layout = nullptr;
		Topo = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
		for (auto& v : Vbs) v = VB();
		Ib = nullptr;
		for (auto& r : Rtvs) r = nullptr;
		RtvCount = 0;
		DsvView = nullptr;
		Ds = nullptr;
		Bs = nullptr;
		Rs = nullptr;
		StencilRef = 0;
		SampleMask = 0xFFFFFFFF;
		for (float& f : BlendFactor) f = 1.0f;
		ViewportCount = ScissorCount = 0;
		Prog = nullptr;
		Values.clear();
		Held.clear();
		Predicate = nullptr;
		LastPipe = nullptr;
		++TargetsSerial;
		DynamicDirty = true;
	}
}

// ============================================================ 효과 → 컨텍스트
namespace GfxD3D12Shared
{
	void SetProgram(GfxContext* context, GfxObject* program, const BindingValue* values, uint32_t count)
	{
		auto* c = static_cast<GfxD3D12Impl::Ctx*>(context);
		c->Prog = static_cast<GfxD3D12Impl::Program*>(program);
		c->Values.assign(values, values + count);
		// 값의 뷰 · 샘플러를 다음 Apply 까지 잡아 둔다 (D3D 의 컨텍스트가 묶인 SRV 를 잡는 것과 같이)
		c->Held.resize((size_t)count * 3);
		for (uint32_t i = 0; i < count; ++i)
		{
			c->Held[i * 3] = values[i].View;
			c->Held[i * 3 + 1] = values[i].Sampler;
			c->Held[i * 3 + 2] = values[i].Uav;
		}
	}
}

// ============================================================ 텍스처 → CPU 이미지
namespace GfxD3D12
{
	using namespace GfxD3D12Impl;

	HRESULT CaptureTexture(GfxContext* context, GfxResource* texture, DirectX::ScratchImage& out)
	{
		auto* c = static_cast<Ctx*>(context);
		Dev* d = c->D.Get();
		GfxD3D12Impl::Image* t = ImageOf(texture);
		if (!t || IsBlockCompressed(t->Dxgi) || d->Lost) return E_INVALIDARG;
		c->FinishAsync();
		if (t->Samples > 1) return E_NOTIMPL;
		const bool vol = t->Dim == D3D12_RESOURCE_DIMENSION_TEXTURE3D;   // 3D: 밉 0 의 깊이 조각 전부
		const bool all = !vol && (t->Cube || t->R.Layers > 1);
		HRESULT hr = vol ? out.Initialize3D(t->Dxgi, t->Width, t->Height, t->Depth, 1)
			: t->Cube ? out.InitializeCube(t->Dxgi, t->Width, t->Height, (std::max)(1u, t->R.Layers / 6), t->R.Mips)
			: all ? out.Initialize2D(t->Dxgi, t->Width, t->Height, t->R.Layers, t->R.Mips)
			: out.Initialize2D(t->Dxgi, t->Width, t->Height, 1, 1);
		if (FAILED(hr)) return hr;
		const UINT layers = all ? t->R.Layers : 1, mips = all ? t->R.Mips : 1;
		auto copyOut = [&](const uint8_t* base, const std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT>& fps, const std::vector<UINT>& rows,
			const std::vector<UINT64>& rowBytes, UINT stride) {
			for (UINT layer = 0; layer < layers; ++layer)
				for (UINT m = 0; m < mips; ++m)
				{
					const UINT i = layer * stride + m;
					const UINT slices = vol ? fps[i].Footprint.Depth : 1;
					for (UINT z = 0; z < slices; ++z)
					{
						const DirectX::Image* img = out.GetImage(m, vol ? 0 : layer, vol ? z : 0);
						const uint8_t* src = base + fps[i].Offset + (UINT64)z * rows[i] * fps[i].Footprint.RowPitch;
						for (UINT y = 0; img && y < rows[i]; ++y)
							memcpy(img->pixels + y * img->rowPitch, src + (UINT64)y * fps[i].Footprint.RowPitch, (size_t)(std::min<UINT64>)(rowBytes[i], img->rowPitch));
					}
				}
		};
		if (t->Staging)
		{
			// CPU 쪽 텍스처: 기다린 뒤 그대로
			d->WaitSerial(t->R.LastUse);
			copyOut(t->StagingCpu, t->Footprints, t->Rows, t->RowBytes, t->R.Mips);
			return S_OK;
		}
		// 리드백 버퍼로 복사 → 제출 · 기다림 (깊이 · 스텐실은 깊이 면만)
		const D3D12_RESOURCE_DESC rd = t->R.Res->GetDesc();
		std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> fps(t->R.Mips * t->R.Layers);
		std::vector<UINT> rows(fps.size());
		std::vector<UINT64> rowBytes(fps.size());
		UINT64 total = 0;
		d->Device->GetCopyableFootprints(&rd, 0, (UINT)fps.size(), 0, fps.data(), rows.data(), rowBytes.data(), &total);
		D3D12_HEAP_PROPERTIES hp = {};
		hp.Type = D3D12_HEAP_TYPE_READBACK;
		D3D12_RESOURCE_DESC bd = {};
		bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		bd.Width = total;
		bd.Height = 1;
		bd.DepthOrArraySize = 1;
		bd.MipLevels = 1;
		bd.SampleDesc.Count = 1;
		bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		ComPtr<ID3D12Resource> rb;
		if (FAILED(d->Device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&rb))))
			return E_OUTOFMEMORY;
		c->Transition(t->R, 0, mips, 0, layers, D3D12_RESOURCE_STATE_COPY_SOURCE);
		c->FlushBarriers();
		ID3D12GraphicsCommandList* cl = d->Cmd();
		for (UINT layer = 0; layer < layers; ++layer)
			for (UINT m = 0; m < mips; ++m)
			{
				const UINT sub = layer * t->R.Mips + m;
				D3D12_TEXTURE_COPY_LOCATION s = {}, dl = {};
				s.pResource = t->R.Res.Get();
				s.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
				s.SubresourceIndex = sub;
				dl.pResource = rb.Get();
				dl.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
				dl.PlacedFootprint = fps[sub];
				cl->CopyTextureRegion(&dl, 0, 0, 0, &s, nullptr);
			}
		d->Submit(true);
		if (d->Lost) return E_FAIL;
		void* p = nullptr;
		if (FAILED(rb->Map(0, nullptr, &p))) return E_FAIL;
		copyOut(static_cast<const uint8_t*>(p), fps, rows, rowBytes, t->R.Mips);
		const D3D12_RANGE none = { 0, 0 };
		rb->Unmap(0, &none);
		return S_OK;
	}
}
