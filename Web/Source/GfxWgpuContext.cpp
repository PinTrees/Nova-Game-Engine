#include "pch.h"
#include "GfxWgpuInternal.h"

// Gfx WebGPU 컨텍스트: D3D11 즉시 컨텍스트를 명령 인코더 위에 흉내 낸다
//  - 렌더 패스는 그리기 때 연다 (지금 타깃 + 예약된 지우기 = loadOp clear). 타깃이 바뀌거나 복사 · 디스패치 · 제출이면 닫는다
//  - 파이프라인 = 프로그램 · 변형 · 입력 배치 · 상태 · 타깃 형식 · 정점 간격 · 토폴로지, 바인드 그룹 = 값 (프레임마다 캐시를 비운다)
//  - 프레임 안의 쓰기 (UpdateSubresource · Unmap): 그 자원을 지금 인코더가 이미 썼으면 먼저 제출 (queue.write 는 제출 앞에 실행되므로)
using namespace GfxWgpuImpl;

namespace
{
	WGPUBlendFactor BlendFactorOf(D3D11_BLEND b, bool alpha)
	{
		switch (b)
		{
		case D3D11_BLEND_ZERO: return WGPUBlendFactor_Zero;
		case D3D11_BLEND_ONE: return WGPUBlendFactor_One;
		case D3D11_BLEND_SRC_COLOR: return alpha ? WGPUBlendFactor_SrcAlpha : WGPUBlendFactor_Src;
		case D3D11_BLEND_INV_SRC_COLOR: return alpha ? WGPUBlendFactor_OneMinusSrcAlpha : WGPUBlendFactor_OneMinusSrc;
		case D3D11_BLEND_SRC_ALPHA: return WGPUBlendFactor_SrcAlpha;
		case D3D11_BLEND_INV_SRC_ALPHA: return WGPUBlendFactor_OneMinusSrcAlpha;
		case D3D11_BLEND_DEST_ALPHA: return WGPUBlendFactor_DstAlpha;
		case D3D11_BLEND_INV_DEST_ALPHA: return WGPUBlendFactor_OneMinusDstAlpha;
		case D3D11_BLEND_DEST_COLOR: return alpha ? WGPUBlendFactor_DstAlpha : WGPUBlendFactor_Dst;
		case D3D11_BLEND_INV_DEST_COLOR: return alpha ? WGPUBlendFactor_OneMinusDstAlpha : WGPUBlendFactor_OneMinusDst;
		case D3D11_BLEND_SRC_ALPHA_SAT: return WGPUBlendFactor_SrcAlphaSaturated;
		case D3D11_BLEND_BLEND_FACTOR: return WGPUBlendFactor_Constant;
		case D3D11_BLEND_INV_BLEND_FACTOR: return WGPUBlendFactor_OneMinusConstant;
		default: return WGPUBlendFactor_One;   // 두 번째 소스 (SRC1) — 쓰지 않는다
		}
	}

	WGPUBlendOperation BlendOp(D3D11_BLEND_OP o)
	{
		switch (o)
		{
		case D3D11_BLEND_OP_SUBTRACT: return WGPUBlendOperation_Subtract;
		case D3D11_BLEND_OP_REV_SUBTRACT: return WGPUBlendOperation_ReverseSubtract;
		case D3D11_BLEND_OP_MIN: return WGPUBlendOperation_Min;
		case D3D11_BLEND_OP_MAX: return WGPUBlendOperation_Max;
		default: return WGPUBlendOperation_Add;
		}
	}

	WGPUStencilOperation StencilOp(D3D11_STENCIL_OP o)
	{
		switch (o)
		{
		case D3D11_STENCIL_OP_ZERO: return WGPUStencilOperation_Zero;
		case D3D11_STENCIL_OP_REPLACE: return WGPUStencilOperation_Replace;
		case D3D11_STENCIL_OP_INCR_SAT: return WGPUStencilOperation_IncrementClamp;
		case D3D11_STENCIL_OP_DECR_SAT: return WGPUStencilOperation_DecrementClamp;
		case D3D11_STENCIL_OP_INVERT: return WGPUStencilOperation_Invert;
		case D3D11_STENCIL_OP_INCR: return WGPUStencilOperation_IncrementWrap;
		case D3D11_STENCIL_OP_DECR: return WGPUStencilOperation_DecrementWrap;
		default: return WGPUStencilOperation_Keep;
		}
	}

	WGPUStencilFaceState StencilFace(const D3D11_DEPTH_STENCILOP_DESC& d, bool enabled)
	{
		WGPUStencilFaceState s = {};
		if (!enabled)
		{
			s.compare = WGPUCompareFunction_Always;
			s.failOp = s.depthFailOp = s.passOp = WGPUStencilOperation_Keep;
			return s;
		}
		s.compare = Compare(d.StencilFunc);
		s.failOp = StencilOp(d.StencilFailOp);
		s.depthFailOp = StencilOp(d.StencilDepthFailOp);
		s.passOp = StencilOp(d.StencilPassOp);
		return s;
	}

	WGPUPrimitiveTopology Topology(D3D11_PRIMITIVE_TOPOLOGY t, bool& strip)
	{
		strip = false;
		switch (t)
		{
		case D3D11_PRIMITIVE_TOPOLOGY_POINTLIST: return WGPUPrimitiveTopology_PointList;
		case D3D11_PRIMITIVE_TOPOLOGY_LINELIST: return WGPUPrimitiveTopology_LineList;
		case D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP: strip = true; return WGPUPrimitiveTopology_LineStrip;
		case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP: strip = true; return WGPUPrimitiveTopology_TriangleStrip;
		default: return WGPUPrimitiveTopology_TriangleList;
		}
	}

	template <class T> T* Cast(GfxObject* o) { return o ? static_cast<T*>(o) : nullptr; }
}

Ctx::~Ctx()
{
	EndPass();
	if (D && D->Immediate == this) D->Immediate = nullptr;
}

void Ctx::Touch(GfxResource* r)
{
	if (TexInfo* t = TexOf(r)) t->LastUse = D->EncoderSerial;
	else if (Buf* b = BufOf(r)) b->LastUse = D->EncoderSerial;
}

// ------------------------------------------------------------------ 상태
void Ctx::IASetVertexBuffers(UINT start, UINT count, GfxBuffer* const* buffers, const UINT* strides, const UINT* offsets)
{
	for (UINT i = 0; i < count && start + i < 16; ++i)
	{
		Vbs[start + i].Buf = buffers ? buffers[i] : nullptr;
		Vbs[start + i].Stride = strides ? strides[i] : 0;
		Vbs[start + i].Offset = offsets ? offsets[i] : 0;
	}
}

void Ctx::IAGetVertexBuffers(UINT start, UINT count, GfxBuffer** buffers, UINT* strides, UINT* offsets)
{
	for (UINT i = 0; i < count && start + i < 16; ++i)
	{
		if (buffers) { buffers[i] = Vbs[start + i].Buf.Get(); if (buffers[i]) buffers[i]->AddRef(); }
		if (strides) strides[i] = Vbs[start + i].Stride;
		if (offsets) offsets[i] = Vbs[start + i].Offset;
	}
}

void Ctx::IAGetIndexBuffer(GfxBuffer** b, DXGI_FORMAT* f, UINT* o)
{
	if (b) { *b = Ib.Get(); if (*b) (*b)->AddRef(); }
	if (f) *f = IbFormat;
	if (o) *o = IbOffset;
}

void Ctx::OMSetRenderTargets(UINT count, GfxRenderTargetView* const* rtvs, GfxDepthStencilView* dsv)
{
	count = (std::min)(count, 8u);
	bool same = count == RtvCount && dsv == DsvView.Get();
	for (UINT i = 0; i < count && same; ++i)
		same = Rtvs[i].Get() == (rtvs ? rtvs[i] : nullptr);
	if (same)
		return;
	EndPass();
	FlushClears();   // 예약된 지우기는 옛 타깃에서 실행 (새 타깃에 남은 것도 — 순서를 지키려고)
	for (UINT i = 0; i < 8; ++i)
		Rtvs[i] = i < count && rtvs ? rtvs[i] : nullptr;
	RtvCount = count;
	DsvView = dsv;
	TargetsDirty = true;
}

void Ctx::OMGetRenderTargets(UINT count, GfxRenderTargetView** rtvs, GfxDepthStencilView** dsv)
{
	for (UINT i = 0; i < count && rtvs; ++i)
	{
		rtvs[i] = i < 8 ? Rtvs[i].Get() : nullptr;
		if (rtvs[i]) rtvs[i]->AddRef();
	}
	if (dsv) { *dsv = DsvView.Get(); if (*dsv) (*dsv)->AddRef(); }
}

void Ctx::OMSetBlendState(GfxBlendState* s, const FLOAT f[4], UINT mask)
{
	Bs = s;
	if (f) memcpy(BlendFactor, f, sizeof(BlendFactor));
	else for (float& v : BlendFactor) v = 1.0f;
	SampleMask = mask;
	DynamicDirty = true;
}

void Ctx::OMGetBlendState(GfxBlendState** s, FLOAT f[4], UINT* mask)
{
	if (s) { *s = Bs.Get(); if (*s) (*s)->AddRef(); }
	if (f) memcpy(f, BlendFactor, sizeof(BlendFactor));
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

void Ctx::ClearState()
{
	EndPass();
	FlushClears();
	Layout.Reset();
	Topo = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
	for (VB& v : Vbs) v = VB();
	Ib.Reset();
	for (auto& r : Rtvs) r.Reset();
	RtvCount = 0;
	DsvView.Reset();
	Ds.Reset(); Bs.Reset(); Rs.Reset();
	StencilRef = 0;
	for (float& f : BlendFactor) f = 1.0f;
	SampleMask = 0xFFFFFFFF;
	ViewportCount = ScissorCount = 0;
	Prog.Reset();
	Values.clear();
	Held.clear();
	TargetsDirty = true;
}

void Ctx::Flush() { D->Submit(); }

namespace GfxWgpuImpl
{
	void SetProgram(GfxContext* context, GfxObject* program, const BindingValue* values, uint32_t count)
	{
		auto* c = static_cast<Ctx*>(context);
		c->Prog = static_cast<Program*>(program);
		c->Values.assign(values, values + (program ? count : 0));
		c->Held.clear();
		for (const BindingValue& v : c->Values)
		{
			if (v.View) c->Held.push_back(v.View);
			if (v.Uav) c->Held.push_back(v.Uav);
			if (v.Sampler) c->Held.push_back(v.Sampler);
		}
	}
}

// ------------------------------------------------------------------ 패스 · 지우기
void Ctx::EndPass()
{
	if (!Pass) return;
	wgpuRenderPassEncoderEnd(Pass);
	wgpuRenderPassEncoderRelease(Pass);
	Pass = nullptr;
	BoundPipe = nullptr;
	BoundGroup = nullptr;
	BoundOffsets.clear();
	BoundIb = nullptr;
	for (auto& b : BoundVb) b = nullptr;
	DynamicDirty = true;
}

bool Ctx::BeginPass()
{
	if (Pass && !TargetsDirty) return true;
	EndPass();
	WGPURenderPassColorAttachment colors[8] = {};
	UINT colorCount = 0;
	for (UINT i = 0; i < RtvCount; ++i)
	{
		WGPURenderPassColorAttachment& ca = colors[i];
		ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
		ViewInfo* v = Rtvs[i] ? ViewOf(Rtvs[i].Get()) : nullptr;
		if (!v || !v->View)
			continue;
		colorCount = i + 1;
		ca.view = v->View;
		auto pc = PendingColor.find(Rtvs[i].Get());
		if (pc != PendingColor.end())
		{
			ca.loadOp = WGPULoadOp_Clear;
			ca.clearValue = { pc->second[0], pc->second[1], pc->second[2], pc->second[3] };
			PendingColor.erase(pc);
		}
		else
			ca.loadOp = WGPULoadOp_Load;
		ca.storeOp = WGPUStoreOp_Store;
		Touch(v->Res.Get());
	}
	WGPURenderPassDepthStencilAttachment ds = {};
	ViewInfo* dv = DsvView ? ViewOf(DsvView.Get()) : nullptr;
	if (dv && dv->View)
	{
		ds.view = dv->View;
		const bool stencil = dv->Tex && (dv->Tex->Format == WGPUTextureFormat_Depth24PlusStencil8 || dv->Tex->Format == WGPUTextureFormat_Depth32FloatStencil8);
		auto pd = PendingDepth.find(DsvView.Get());
		const UINT flags = pd != PendingDepth.end() ? pd->second.Flags : 0;
		ds.depthLoadOp = (flags & D3D11_CLEAR_DEPTH) ? WGPULoadOp_Clear : WGPULoadOp_Load;
		ds.depthClearValue = pd != PendingDepth.end() ? pd->second.Depth : 1.0f;
		ds.depthStoreOp = WGPUStoreOp_Store;
		if (stencil)
		{
			ds.stencilLoadOp = (flags & D3D11_CLEAR_STENCIL) ? WGPULoadOp_Clear : WGPULoadOp_Load;
			ds.stencilClearValue = pd != PendingDepth.end() ? pd->second.Stencil : 0;
			ds.stencilStoreOp = WGPUStoreOp_Store;
		}
		else
		{
			ds.stencilLoadOp = WGPULoadOp_Undefined;
			ds.stencilStoreOp = WGPUStoreOp_Undefined;
		}
		if (pd != PendingDepth.end()) PendingDepth.erase(pd);
		Touch(dv->Res.Get());
	}
	if (colorCount == 0 && !ds.view)
		return false;   // 타깃 없음
	WGPURenderPassDescriptor rp = {};
	rp.colorAttachmentCount = colorCount;
	rp.colorAttachments = colors;
	rp.depthStencilAttachment = ds.view ? &ds : nullptr;
	Pass = wgpuCommandEncoderBeginRenderPass(D->Enc(), &rp);
	++FrameStats.Passes;
	TargetsDirty = false;
	DynamicDirty = true;
	return Pass != nullptr;
}

// 예약된 지우기를 빈 패스로 실행 (타깃이 바뀌기 전 · 복사 · 제출 앞)
void Ctx::FlushClears()
{
	if (PendingColor.empty() && PendingDepth.empty()) return;
	EndPass();
	for (auto& [view, c] : PendingColor)
	{
		ViewInfo* v = ViewOf(view);
		if (!v || !v->View) continue;
		WGPURenderPassColorAttachment ca = {};
		ca.view = v->View;
		ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
		ca.loadOp = WGPULoadOp_Clear;
		ca.storeOp = WGPUStoreOp_Store;
		ca.clearValue = { c[0], c[1], c[2], c[3] };
		WGPURenderPassDescriptor rp = {};
		rp.colorAttachmentCount = 1;
		rp.colorAttachments = &ca;
		WGPURenderPassEncoder p = wgpuCommandEncoderBeginRenderPass(D->Enc(), &rp);
		wgpuRenderPassEncoderEnd(p);
		wgpuRenderPassEncoderRelease(p);
		Touch(v->Res.Get());
	}
	for (auto& [view, c] : PendingDepth)
	{
		ViewInfo* v = ViewOf(view);
		if (!v || !v->Tex) continue;
		const bool stencil = v->Tex->Format == WGPUTextureFormat_Depth24PlusStencil8 || v->Tex->Format == WGPUTextureFormat_Depth32FloatStencil8;
		// 여러 조각 DSV (그림자 캐스케이드 · 큐브): 조각마다
		for (UINT l = 0; l < (std::max)(1u, v->Layers); ++l)
		{
			WGPUTextureView tv = l == 0 ? v->View : D->MakeView(*v->Tex, WGPUTextureViewDimension_2D, v->Tex->Format, WGPUTextureAspect_All, v->BaseMip, 1, v->BaseLayer + l, 1);
			WGPURenderPassDepthStencilAttachment ds = {};
			ds.view = tv;
			ds.depthLoadOp = (c.Flags & D3D11_CLEAR_DEPTH) ? WGPULoadOp_Clear : WGPULoadOp_Load;
			ds.depthStoreOp = WGPUStoreOp_Store;
			ds.depthClearValue = c.Depth;
			if (stencil)
			{
				ds.stencilLoadOp = (c.Flags & D3D11_CLEAR_STENCIL) ? WGPULoadOp_Clear : WGPULoadOp_Load;
				ds.stencilStoreOp = WGPUStoreOp_Store;
				ds.stencilClearValue = c.Stencil;
			}
			else
			{
				ds.stencilLoadOp = WGPULoadOp_Undefined;
				ds.stencilStoreOp = WGPUStoreOp_Undefined;
			}
			WGPURenderPassDescriptor rp = {};
			rp.depthStencilAttachment = &ds;
			WGPURenderPassEncoder p = wgpuCommandEncoderBeginRenderPass(D->Enc(), &rp);
			wgpuRenderPassEncoderEnd(p);
			wgpuRenderPassEncoderRelease(p);
			if (l != 0) wgpuTextureViewRelease(tv);
		}
		Touch(v->Res.Get());
	}
	PendingColor.clear();
	PendingDepth.clear();
	PendingHeld.clear();
}

void Ctx::ClearRenderTargetView(GfxRenderTargetView* rtv, const FLOAT color[4])
{
	if (!rtv) return;
	// 지금 패스에 붙은 타깃이면 패스를 닫고 다음 패스의 loadOp 로, 아니면 예약 (다음 사용 전에 실행)
	bool attached = false;
	for (UINT i = 0; i < RtvCount; ++i) attached |= Rtvs[i].Get() == rtv;
	if (attached && Pass) { EndPass(); TargetsDirty = true; }
	PendingColor[rtv] = { color[0], color[1], color[2], color[3] };
	++FrameStats.Clears;
	PendingHeld.push_back(rtv);
	if (!attached)
		FlushClears();
}

void Ctx::ClearDepthStencilView(GfxDepthStencilView* dsv, UINT flags, FLOAT depth, UINT8 stencil)
{
	if (!dsv) return;
	const bool attached = DsvView.Get() == dsv;
	if (attached && Pass) { EndPass(); TargetsDirty = true; }
	DepthClear& c = PendingDepth[dsv];
	c.Flags |= flags;
	if (flags & D3D11_CLEAR_DEPTH) c.Depth = depth;
	if (flags & D3D11_CLEAR_STENCIL) c.Stencil = stencil;
	PendingHeld.push_back(dsv);
	ViewInfo* v = ViewOf(dsv);
	if (!attached || (v && v->Layers > 1))
		FlushClears();
}

// ------------------------------------------------------------------ 바인딩
// 깊이 형식 뷰를 보통 텍스처 칸에 묶은 칸들 (변형 = 그 칸의 표본 종류 unfilterable-float)
uint64_t Ctx::VariantOf(Program& p)
{
	uint64_t mask = 0;
	for (size_t i = 0; i < p.Bindings.size() && i < 64; ++i)
	{
		const ProgramBinding& b = p.Bindings[i];
		if (b.Type != ProgramBinding::Kind::Texture || b.Sample != WGPUTextureSampleType_Float || b.Binding >= (int)Values.size()) continue;
		if (ViewInfo* v = ViewOf(Values[b.Binding].View); v && v->DepthFormat)
			mask |= 1ull << i;
	}
	return mask;
}

static Program::Layout& LayoutFor(Dev* d, Program& p, uint64_t variant)
{
	auto it = p.Layouts.find(variant);
	if (it != p.Layouts.end()) return it->second;
	std::set<int> unfilterable;
	for (size_t i = 0; i < p.Bindings.size() && i < 64; ++i)
		if (variant & (1ull << i)) unfilterable.insert(p.Bindings[i].Binding);
	std::set<int> nonFiltering;
	for (const auto& [tex, smp] : p.SamplerPairs)
		if (unfilterable.count(tex)) nonFiltering.insert(smp);
	std::vector<WGPUBindGroupLayoutEntry> entries;
	for (const ProgramBinding& b : p.Bindings)
	{
		WGPUBindGroupLayoutEntry e = {};
		e.binding = (uint32_t)b.Binding;
		e.visibility = b.Visibility;
		switch (b.Type)
		{
		case ProgramBinding::Kind::Uniform: e.buffer.type = WGPUBufferBindingType_Uniform; e.buffer.hasDynamicOffset = p.DynamicUniforms > 0; break;
		case ProgramBinding::Kind::Storage: e.buffer.type = WGPUBufferBindingType_Storage; break;
		case ProgramBinding::Kind::ReadOnlyStorage: e.buffer.type = WGPUBufferBindingType_ReadOnlyStorage; break;
		case ProgramBinding::Kind::Texture:
			e.texture.sampleType = unfilterable.count(b.Binding) ? WGPUTextureSampleType_UnfilterableFloat : b.Sample;
			e.texture.viewDimension = b.Dim;
			break;
		case ProgramBinding::Kind::DepthTexture: e.texture.sampleType = WGPUTextureSampleType_Depth; e.texture.viewDimension = b.Dim; break;
		case ProgramBinding::Kind::Sampler: e.sampler.type = nonFiltering.count(b.Binding) ? WGPUSamplerBindingType_NonFiltering : WGPUSamplerBindingType_Filtering; break;
		case ProgramBinding::Kind::ComparisonSampler: e.sampler.type = WGPUSamplerBindingType_Comparison; break;
		case ProgramBinding::Kind::StorageTexture:
			e.storageTexture.access = b.Access;
			e.storageTexture.format = b.StorageFormat;
			e.storageTexture.viewDimension = b.Dim;
			break;
		default: continue;
		}
		entries.push_back(e);
	}
	Program::Layout l;
	WGPUBindGroupLayoutDescriptor gd = {};
	gd.entryCount = entries.size();
	gd.entries = entries.data();
	l.Group = wgpuDeviceCreateBindGroupLayout(d->Device, &gd);
	WGPUPipelineLayoutDescriptor pd = {};
	pd.bindGroupLayoutCount = 1;
	pd.bindGroupLayouts = &l.Group;
	l.Pipeline = wgpuDeviceCreatePipelineLayout(d->Device, &pd);
	if (p.Compute && !p.Stages.empty())
	{
		WGPUComputePipelineDescriptor cd = {};
		cd.layout = l.Pipeline;
		cd.compute.module = p.Stages[0].Module;
		cd.compute.entryPoint = p.Stages[0].Entry.c_str();
		l.ComputePipe = wgpuDeviceCreateComputePipeline(d->Device, &cd);
	}
	return p.Layouts[variant] = l;
}

WGPUBindGroup Ctx::BuildGroup(Program& p, uint64_t variant, std::vector<uint32_t>& offsets, bool compute)
{
	Program::Layout& layout = LayoutFor(D.Get(), p, variant);
	std::set<int> nonFiltering;
	for (size_t i = 0; i < p.Bindings.size() && i < 64; ++i)
		if (variant & (1ull << i))
			for (const auto& [tex, smp] : p.SamplerPairs)
				if (tex == p.Bindings[i].Binding) nonFiltering.insert(smp);
	offsets.clear();
	std::vector<WGPUBindGroupEntry> entries;
	std::vector<uint64_t> key;
	key.push_back(p.Id);
	key.push_back(variant);
	auto storageBuffer = [&](const BindingValue& v, WGPUBindGroupEntry& e) {
		ViewInfo* vi = v.Uav ? ViewOf(v.Uav) : v.View ? ViewOf(v.View) : nullptr;
		Buf* b = vi ? vi->Buffer : nullptr;
		if (b && b->Desc.Usage == D3D11_USAGE_DYNAMIC && b->HasLoc)
		{
			e.buffer = b->Loc.Buffer;
			e.offset = b->Loc.Offset + vi->BufOffset;
			e.size = vi->BufSize;
			key.push_back(b->Loc.BufferId); key.push_back(e.offset);
		}
		else if (b && b->Handle)
		{
			e.buffer = b->Handle;
			e.offset = vi->BufOffset;
			e.size = (std::min)(vi->BufSize, b->Size - vi->BufOffset);
			key.push_back(b->Id); key.push_back(e.offset);
			b->LastUse = D->EncoderSerial;
		}
		else
		{
			if (!D->DummyBuffer)
			{
				WGPUBufferDescriptor bd = {};
				bd.size = 4096;
				bd.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
				D->DummyBuffer = wgpuDeviceCreateBuffer(D->Device, &bd);
			}
			e.buffer = D->DummyBuffer;
			e.offset = 0;
			e.size = 4096;
			key.push_back(0);
		}
	};
	for (const ProgramBinding& b : p.Bindings)
	{
		const BindingValue v = b.Binding < (int)Values.size() ? Values[b.Binding] : BindingValue();
		WGPUBindGroupEntry e = {};
		e.binding = (uint32_t)b.Binding;
		switch (b.Type)
		{
		case ProgramBinding::Kind::Uniform:
			if (v.Ubo.Buffer)
			{
				e.buffer = v.Ubo.Buffer;
				e.size = (std::max)(16u, v.Range);
				if (p.DynamicUniforms > 0) { e.offset = 0; offsets.push_back((uint32_t)v.Ubo.Offset); }
				else e.offset = v.Ubo.Offset;
				key.push_back(v.Ubo.BufferId); key.push_back(p.DynamicUniforms > 0 ? 0 : v.Ubo.Offset); key.push_back(e.size);
			}
			else
			{
				RingLoc zero;
				static const uint8_t zeros[256] = {};
				D->RingWrite(zeros, 256, 256, zero);
				e.buffer = zero.Buffer;
				e.size = 256;
				if (p.DynamicUniforms > 0) offsets.push_back((uint32_t)zero.Offset); else e.offset = zero.Offset;
				key.push_back(zero.BufferId); key.push_back(p.DynamicUniforms > 0 ? 0 : zero.Offset);
			}
			break;
		case ProgramBinding::Kind::Storage:
		case ProgramBinding::Kind::ReadOnlyStorage:
			storageBuffer(v, e);
			break;
		case ProgramBinding::Kind::Texture:
		case ProgramBinding::Kind::DepthTexture:
		{
			ViewInfo* vi = v.View ? ViewOf(v.View) : nullptr;
			if (vi && vi->View && !vi->Buffer && vi->Dim == b.Dim)
			{
				e.textureView = vi->View;
				key.push_back(vi->Id);
				Touch(vi->Res.Get());
			}
			else
			{
				if (vi && vi->View && vi->Dim != b.Dim)
					D->Once("dim" + p.Name + std::to_string(b.Binding), "%s: a texture view of another dimension was bound - dummy used", p.Name.c_str());
				const WGPUTextureSampleType st = b.Type == ProgramBinding::Kind::DepthTexture ? WGPUTextureSampleType_Depth :
					(variant && b.Sample == WGPUTextureSampleType_Float) ? WGPUTextureSampleType_Float : b.Sample;
				e.textureView = D->DummyView(b.Dim, st);
				key.push_back(0);
			}
			break;
		}
		case ProgramBinding::Kind::Sampler:
		case ProgramBinding::Kind::ComparisonSampler:
		{
			Sampler* s = Cast<Sampler>(v.Sampler);
			const bool cmp = b.Type == ProgramBinding::Kind::ComparisonSampler;
			if (s && s->Handle && s->Comparison == cmp)
			{
				if (nonFiltering.count(b.Binding) && !cmp)
				{
					if (!s->Nearest)
					{
						WGPUSamplerDescriptor sd = {};
						sd.addressModeU = sd.addressModeV = sd.addressModeW = WGPUAddressMode_ClampToEdge;
						sd.magFilter = sd.minFilter = WGPUFilterMode_Nearest;
						sd.mipmapFilter = WGPUMipmapFilterMode_Nearest;
						sd.lodMaxClamp = 32.0f;
						sd.maxAnisotropy = 1;
						s->Nearest = wgpuDeviceCreateSampler(D->Device, &sd);
					}
					e.sampler = s->Nearest;
				}
				else
					e.sampler = s->Handle;
				key.push_back(s->Id * 2 + (nonFiltering.count(b.Binding) ? 1 : 0));
			}
			else
			{
				if (!D->DummySampler)
				{
					WGPUSamplerDescriptor sd = {};
					sd.addressModeU = sd.addressModeV = sd.addressModeW = WGPUAddressMode_ClampToEdge;
					sd.magFilter = sd.minFilter = WGPUFilterMode_Nearest;
					sd.mipmapFilter = WGPUMipmapFilterMode_Nearest;
					sd.lodMaxClamp = 32.0f;
					sd.maxAnisotropy = 1;
					D->DummySampler = wgpuDeviceCreateSampler(D->Device, &sd);
					D->DummyNearest = D->DummySampler;
					sd.compare = WGPUCompareFunction_LessEqual;
					D->DummyCompare = wgpuDeviceCreateSampler(D->Device, &sd);
				}
				e.sampler = cmp ? D->DummyCompare : D->DummySampler;
				key.push_back(cmp ? 1 : 2);
			}
			break;
		}
		case ProgramBinding::Kind::StorageTexture:
		{
			ViewInfo* vi = v.Uav ? ViewOf(v.Uav) : nullptr;
			if (vi && vi->View && vi->Format == b.StorageFormat)
			{
				e.textureView = vi->View;
				key.push_back(vi->Id);
				Touch(vi->Res.Get());
			}
			else
			{
				e.textureView = D->DummyStorage(b.StorageFormat, b.Dim);
				key.push_back(0);
			}
			break;
		}
		default:
			continue;
		}
		entries.push_back(e);
	}
	(void)compute;
	const uint64_t h = HashBytes(key.data(), key.size() * sizeof(uint64_t));
	auto it = D->BindGroups.find(h);
	if (it != D->BindGroups.end()) return it->second;
	WGPUBindGroupDescriptor gd = {};
	gd.layout = layout.Group;
	gd.entryCount = entries.size();
	gd.entries = entries.data();
	WGPUBindGroup g = wgpuDeviceCreateBindGroup(D->Device, &gd);
	D->BindGroups[h] = g;
	++FrameStats.NewGroups;
	return g;
}

// ------------------------------------------------------------------ 그리기
bool Ctx::PrepareDraw(bool indexed, UINT startInstance, uint64_t& instanceShift)
{
	instanceShift = 0;
	++FrameStats.Calls;
	Program* p = Prog.Get();
	if (!p || p->Compute) { ++FrameStats.SkipNoProgram; return false; }
	if (!BeginPass()) { ++FrameStats.SkipNoTarget; return false; }

	// ---- 파이프라인
	auto* il = Cast<InputLayout>(Layout.Get());
	const D3D11_RASTERIZER_DESC rs = Rs ? Cast<Rasterizer>(Rs.Get())->Dsc : DefaultRasterizer();
	const D3D11_BLEND_DESC bs = Bs ? Cast<Blend>(Bs.Get())->Dsc : DefaultBlend();
	const D3D11_DEPTH_STENCIL_DESC dss = Ds ? Cast<DepthStencil>(Ds.Get())->Dsc : DefaultDepthStencil();
	const uint64_t variant = VariantOf(*p);
	PipelineKey key = {};
	key.Program = p->Id;
	key.Variant = variant;
	key.Layout = il ? il->Id : 0;
	key.Raster = Rs ? Cast<Rasterizer>(Rs.Get())->Hash : 0;
	key.Blend = Bs ? Cast<Blend>(Bs.Get())->Hash : 0;
	key.Depth = Ds ? Cast<DepthStencil>(Ds.Get())->Hash : 0;
	for (int s = 0; s < 8; ++s) key.Strides[s] = Vbs[s].Stride;
	UINT samples = 1;
	for (UINT i = 0; i < RtvCount; ++i)
		if (ViewInfo* v = Rtvs[i] ? ViewOf(Rtvs[i].Get()) : nullptr)
		{
			key.Colors[i] = (uint32_t)v->Format;
			key.ColorCount = (uint8_t)(i + 1);
			if (v->Tex) samples = v->Tex->Samples;
		}
	ViewInfo* dv = DsvView ? ViewOf(DsvView.Get()) : nullptr;
	if (dv) { key.DepthFormat = (uint32_t)dv->Format; if (dv->Tex) samples = dv->Tex->Samples; }
	key.Samples = (uint8_t)samples;
	key.SampleMask = SampleMask;
	bool strip = false;
	key.Topology = (uint8_t)Topology(Topo, strip);
	key.StripIndex = strip && indexed ? (IbFormat == DXGI_FORMAT_R32_UINT ? 2 : 1) : 0;
	WGPURenderPipeline pipe = nullptr;
	auto pit = D->Pipelines.find(key);
	if (pit != D->Pipelines.end())
		pipe = pit->second;
	else
	{
		Program::Layout& layout = LayoutFor(D.Get(), *p, variant);
		// 정점 입력: 입력 배치의 원소 중 셰이더가 읽는 것 (의미 → location)
		std::vector<WGPUVertexAttribute> attrs[16];
		// 행렬 · 배열 입력 (float4x4 WORLD : WORLD0) 은 위치를 여러 개 차지한다: 배치의 WORLD1..3 = 위치 + 의미 번호 차이
		auto split = [](const std::string& s, std::string& base, int& index) {
			size_t n = s.size();
			while (n > 0 && isdigit((unsigned char)s[n - 1])) --n;
			base = s.substr(0, n);
			index = n < s.size() ? atoi(s.c_str() + n) : 0;
		};
		auto locationOf = [&](const std::string& semantic) -> int {
			std::string base; int index;
			split(semantic, base, index);
			int best = -1, bestIndex = -1;
			for (const auto& [sem, loc] : p->VertexInputs)
			{
				std::string b2; int i2;
				split(sem, b2, i2);
				if (b2 == base && i2 <= index && i2 > bestIndex) { best = loc + (index - i2); bestIndex = i2; }
			}
			return best;
		};
		std::set<int> used;
		if (il)
			for (const InputLayout::Element& e : il->Elements)
			{
				const int loc = e.Format != WGPUVertexFormat_Undefined ? locationOf(e.Semantic) : -1;
				if (loc >= 0 && used.insert(loc).second)
					attrs[e.Slot].push_back({ e.Format, e.Offset, (uint32_t)loc });
			}
		// 진단: 셰이더 입력 중 배치에 없는 것 (D3D 는 입력 배치를 만들 때 실패한다)
		for (const auto& [sem, loc] : p->VertexInputs)
		{
			bool found = false;
			if (il) for (const InputLayout::Element& e : il->Elements) found |= e.Semantic == sem && e.Format != WGPUVertexFormat_Undefined;
			if (!found)
			{
				std::string have;
				if (il) for (const InputLayout::Element& e : il->Elements) have += e.Semantic + "@" + std::to_string(e.Slot) + "/" + std::to_string((int)e.Format) + " ";
				D->Once("vin" + p->Name + sem, ("%s: vertex input " + sem + " (location " + std::to_string(loc) + ") not in the input layout [" + have + "]").c_str(), p->Name.c_str());
			}
		}
		int lastSlot = -1;
		for (int s = 0; s < 16; ++s) if (!attrs[s].empty()) lastSlot = s;
		WGPUVertexBufferLayout vbl[16] = {};
		for (int s = 0; s <= lastSlot; ++s)
		{
			vbl[s].arrayStride = (Vbs[s].Stride + 3) & ~3u;
			vbl[s].stepMode = attrs[s].empty() ? WGPUVertexStepMode_VertexBufferNotUsed : il->PerInstance[s] ? WGPUVertexStepMode_Instance : WGPUVertexStepMode_Vertex;
			vbl[s].attributeCount = attrs[s].size();
			vbl[s].attributes = attrs[s].data();
		}
		WGPURenderPipelineDescriptor rd = {};
		rd.label = p->Name.c_str();
		rd.layout = layout.Pipeline;
		const Program::Stage* vs = nullptr;
		const Program::Stage* fsStage = nullptr;
		for (const Program::Stage& st : p->Stages)
		{
			if (st.Flag == WGPUShaderStage_Vertex) vs = &st;
			if (st.Flag == WGPUShaderStage_Fragment) fsStage = &st;
		}
		if (!vs) { ++FrameStats.SkipNoVs; return false; }
		rd.vertex.module = vs->Module;
		rd.vertex.entryPoint = vs->Entry.c_str();
		rd.vertex.bufferCount = (size_t)(lastSlot + 1);
		rd.vertex.buffers = vbl;
		rd.primitive.topology = (WGPUPrimitiveTopology)key.Topology;
		rd.primitive.stripIndexFormat = key.StripIndex == 2 ? WGPUIndexFormat_Uint32 : key.StripIndex == 1 ? WGPUIndexFormat_Uint16 : WGPUIndexFormat_Undefined;
		rd.primitive.frontFace = rs.FrontCounterClockwise ? WGPUFrontFace_CCW : WGPUFrontFace_CW;
		rd.primitive.cullMode = rs.CullMode == D3D11_CULL_NONE ? WGPUCullMode_None : rs.CullMode == D3D11_CULL_FRONT ? WGPUCullMode_Front : WGPUCullMode_Back;
		WGPUPrimitiveDepthClipControl clip = {};
		if (!rs.DepthClipEnable && dv)
		{
			clip.chain.sType = WGPUSType_PrimitiveDepthClipControl;
			clip.unclippedDepth = true;
			rd.primitive.nextInChain = &clip.chain;
		}
		WGPUDepthStencilState ds = {};
		if (dv)
		{
			const bool tri = key.Topology == WGPUPrimitiveTopology_TriangleList || key.Topology == WGPUPrimitiveTopology_TriangleStrip;
			ds.format = dv->Format;
			ds.depthWriteEnabled = dss.DepthEnable && dss.DepthWriteMask == D3D11_DEPTH_WRITE_MASK_ALL;
			ds.depthCompare = dss.DepthEnable ? Compare(dss.DepthFunc) : WGPUCompareFunction_Always;
			const bool stencil = dv->Format == WGPUTextureFormat_Depth24PlusStencil8 || dv->Format == WGPUTextureFormat_Depth32FloatStencil8;
			ds.stencilFront = StencilFace(dss.FrontFace, stencil && dss.StencilEnable);
			ds.stencilBack = StencilFace(dss.BackFace, stencil && dss.StencilEnable);
			ds.stencilReadMask = stencil && dss.StencilEnable ? dss.StencilReadMask : 0xFFFFFFFF;
			ds.stencilWriteMask = stencil && dss.StencilEnable ? dss.StencilWriteMask : 0;
			ds.depthBias = tri ? rs.DepthBias : 0;
			ds.depthBiasSlopeScale = tri ? rs.SlopeScaledDepthBias : 0.0f;
			ds.depthBiasClamp = tri ? rs.DepthBiasClamp : 0.0f;
			rd.depthStencil = &ds;
		}
		rd.multisample.count = samples;
		rd.multisample.mask = SampleMask;
		rd.multisample.alphaToCoverageEnabled = bs.AlphaToCoverageEnable && samples > 1;
		WGPUColorTargetState targets[8] = {};
		WGPUBlendState blends[8] = {};
		WGPUFragmentState fs = {};
		if (fsStage)
		{
			for (UINT i = 0; i < key.ColorCount; ++i)
			{
				targets[i].format = (WGPUTextureFormat)key.Colors[i];
				if (key.Colors[i] == 0) continue;
				const D3D11_RENDER_TARGET_BLEND_DESC& b = bs.RenderTarget[bs.IndependentBlendEnable ? i : 0];
				targets[i].writeMask = (p->PixelOutputs & (1u << i)) ? (WGPUColorWriteMaskFlags)(b.RenderTargetWriteMask & 0xF) : WGPUColorWriteMask_None;
				if (b.BlendEnable)
				{
					blends[i].color = { BlendOp(b.BlendOp), BlendFactorOf(b.SrcBlend, false), BlendFactorOf(b.DestBlend, false) };
					blends[i].alpha = { BlendOp(b.BlendOpAlpha), BlendFactorOf(b.SrcBlendAlpha, true), BlendFactorOf(b.DestBlendAlpha, true) };
					targets[i].blend = &blends[i];
				}
			}
			fs.module = fsStage->Module;
			fs.entryPoint = fsStage->Entry.c_str();
			fs.targetCount = key.ColorCount;
			fs.targets = targets;
			rd.fragment = &fs;
		}
		pipe = wgpuDeviceCreateRenderPipeline(D->Device, &rd);
		D->Pipelines[key] = pipe;
		++FrameStats.NewPipelines;
	}
	if (!pipe) { ++FrameStats.SkipNoPipeline; return false; }
	if (pipe != BoundPipe)
	{
		wgpuRenderPassEncoderSetPipeline(Pass, pipe);
		BoundPipe = pipe;
	}

	// ---- 바인드 그룹
	std::vector<uint32_t> offsets;
	WGPUBindGroup group = BuildGroup(*p, variant, offsets, false);
	if (group != BoundGroup || offsets != BoundOffsets)
	{
		wgpuRenderPassEncoderSetBindGroup(Pass, 0, group, offsets.size(), offsets.data());
		BoundGroup = group;
		BoundOffsets = offsets;
	}

	// ---- 정점 · 인덱스 버퍼 (SV_InstanceID 를 쓰는 셰이더는 시작 인스턴스를 인스턴스 버퍼 오프셋으로 — D3D 처럼 0 부터)
	const bool shift = p->UsesInstanceIndex && startInstance > 0;
	for (int s = 0; s < 16; ++s)
	{
		const VB& v = Vbs[s];
		Buf* b = BufOf(v.Buf.Get());
		if (!b) continue;
		WGPUBuffer handle = b->Handle;
		uint64_t offset = v.Offset;
		if (b->Desc.Usage == D3D11_USAGE_DYNAMIC)
		{
			if (!b->HasLoc) continue;
			handle = b->Loc.Buffer;
			offset += b->Loc.Offset;
		}
		else
			b->LastUse = D->EncoderSerial;
		if (shift && il && il->PerInstance[s])
			offset += (uint64_t)startInstance * v.Stride;
		if (handle != BoundVb[s] || offset != BoundVbOffset[s])
		{
			wgpuRenderPassEncoderSetVertexBuffer(Pass, (uint32_t)s, handle, offset, WGPU_WHOLE_SIZE);
			BoundVb[s] = handle;
			BoundVbOffset[s] = offset;
		}
	}
	if (shift) instanceShift = startInstance;
	if (indexed)
	{
		Buf* b = BufOf(Ib.Get());
		if (!b) { ++FrameStats.SkipNoIndex; return false; }
		WGPUBuffer handle = b->Handle;
		uint64_t offset = IbOffset;
		if (b->Desc.Usage == D3D11_USAGE_DYNAMIC)
		{
			if (!b->HasLoc) { ++FrameStats.SkipDynIndex; return false; }
			handle = b->Loc.Buffer;
			offset += b->Loc.Offset;
		}
		else
			b->LastUse = D->EncoderSerial;
		if (handle != BoundIb || offset != BoundIbOffset)
		{
			wgpuRenderPassEncoderSetIndexBuffer(Pass, handle, IbFormat == DXGI_FORMAT_R32_UINT ? WGPUIndexFormat_Uint32 : WGPUIndexFormat_Uint16, offset, WGPU_WHOLE_SIZE);
			BoundIb = handle;
			BoundIbOffset = offset;
		}
	}

	// ---- 뷰포트 · 가위 · 블렌드 상수 · 스텐실 (WebGPU 는 타깃 밖으로 나가면 오류 → 자른다)
	if (DynamicDirty)
	{
		UINT tw = 0, th = 0;
		ViewInfo* first = nullptr;
		for (UINT i = 0; i < RtvCount && !first; ++i) first = Rtvs[i] ? ViewOf(Rtvs[i].Get()) : nullptr;
		if (!first) first = dv;
		if (first && first->Tex) { tw = first->Tex->MipW(first->BaseMip); th = first->Tex->MipH(first->BaseMip); }
		D3D11_VIEWPORT vp = ViewportCount ? Viewports[0] : D3D11_VIEWPORT{ 0, 0, (float)tw, (float)th, 0, 1 };
		float x0 = (std::max)(0.0f, vp.TopLeftX), y0 = (std::max)(0.0f, vp.TopLeftY);
		float x1 = (std::min)((float)tw, vp.TopLeftX + vp.Width), y1 = (std::min)((float)th, vp.TopLeftY + vp.Height);
		if (x1 <= x0 || y1 <= y0) { x0 = y0 = 0; x1 = y1 = 1; }
		wgpuRenderPassEncoderSetViewport(Pass, x0, y0, x1 - x0, y1 - y0, std::clamp(vp.MinDepth, 0.0f, 1.0f), std::clamp(vp.MaxDepth, 0.0f, 1.0f));
		if (rs.ScissorEnable && ScissorCount)
		{
			const D3D11_RECT& r = Scissors[0];
			const LONG sx0 = std::clamp<LONG>(r.left, 0, (LONG)tw), sy0 = std::clamp<LONG>(r.top, 0, (LONG)th);
			const LONG sx1 = std::clamp<LONG>(r.right, sx0, (LONG)tw), sy1 = std::clamp<LONG>(r.bottom, sy0, (LONG)th);
			wgpuRenderPassEncoderSetScissorRect(Pass, (uint32_t)sx0, (uint32_t)sy0, (uint32_t)(sx1 - sx0), (uint32_t)(sy1 - sy0));
		}
		else
			wgpuRenderPassEncoderSetScissorRect(Pass, 0, 0, tw, th);
		const WGPUColor bc = { BlendFactor[0], BlendFactor[1], BlendFactor[2], BlendFactor[3] };
		wgpuRenderPassEncoderSetBlendConstant(Pass, &bc);
		wgpuRenderPassEncoderSetStencilReference(Pass, StencilRef);
		DynamicDirty = false;
	}
	return true;
}

void Ctx::DrawInstanced(UINT vertexCount, UINT instanceCount, UINT startVertex, UINT startInstance)
{
	uint64_t shift = 0;
	if (!PrepareDraw(false, startInstance, shift)) return;
	++FrameStats.Draws;
	wgpuRenderPassEncoderDraw(Pass, vertexCount, instanceCount, startVertex, shift ? 0 : startInstance);
}

void Ctx::DrawIndexedInstanced(UINT indexCount, UINT instanceCount, UINT startIndex, INT baseVertex, UINT startInstance)
{
	uint64_t shift = 0;
	if (!PrepareDraw(true, startInstance, shift)) return;
	++FrameStats.Draws;
	wgpuRenderPassEncoderDrawIndexed(Pass, indexCount, instanceCount, startIndex, baseVertex, shift ? 0 : startInstance);
}

void Ctx::Dispatch(UINT x, UINT y, UINT z)
{
	Program* p = Prog.Get();
	if (!p || !p->Compute || x == 0 || y == 0 || z == 0) return;
	EndPass();
	FlushClears();
	const uint64_t variant = VariantOf(*p);
	Program::Layout& layout = LayoutFor(D.Get(), *p, variant);
	if (!layout.ComputePipe) return;
	std::vector<uint32_t> offsets;
	WGPUBindGroup group = BuildGroup(*p, variant, offsets, true);
	WGPUComputePassEncoder cp = wgpuCommandEncoderBeginComputePass(D->Enc(), nullptr);
	wgpuComputePassEncoderSetPipeline(cp, layout.ComputePipe);
	wgpuComputePassEncoderSetBindGroup(cp, 0, group, offsets.size(), offsets.data());
	wgpuComputePassEncoderDispatchWorkgroups(cp, x, y, z);
	++FrameStats.Dispatches;
	wgpuComputePassEncoderEnd(cp);
	wgpuComputePassEncoderRelease(cp);
}

// ------------------------------------------------------------------ 자원 쓰기 · 읽기
namespace
{
	void OnMapped(WGPUBufferMapAsyncStatus status, void* user)
	{
		*static_cast<int*>(user) = status == WGPUBufferMapAsyncStatus_Success ? 2 : 0;
	}
}

HRESULT Ctx::Map(GfxResource* resource, UINT sub, D3D11_MAP type, UINT flags, D3D11_MAPPED_SUBRESOURCE* mapped)
{
	(void)flags;
	if (!mapped) return E_INVALIDARG;
	if (Buf* b = BufOf(resource))
	{
		if (!b->Shadow.empty())   // DYNAMIC · 올리기 STAGING: CPU 사본
		{
			mapped->pData = b->Shadow.data();
			mapped->RowPitch = mapped->DepthPitch = (UINT)b->Size;
			return S_OK;
		}
		if (type == D3D11_MAP_READ && b->Handle)
		{
			if (b->MapState == 2)
			{
				mapped->pData = const_cast<void*>(wgpuBufferGetConstMappedRange(b->Handle, 0, b->Size));
				mapped->RowPitch = mapped->DepthPitch = (UINT)b->Size;
				return mapped->pData ? S_OK : E_FAIL;
			}
			if (b->MapState == 0)
			{
				D->Submit();   // 복사가 먼저 실행되도록
				b->MapState = 1;
				wgpuBufferMapAsync(b->Handle, WGPUMapMode_Read, 0, b->Size, OnMapped, &b->MapState);
			}
			return DXGI_ERROR_WAS_STILL_DRAWING;   // 웹은 기다릴 수 없다 — 다음 프레임에 다시
		}
		return E_INVALIDARG;
	}
	TexInfo* t = TexOf(resource);
	if (!t) return E_INVALIDARG;
	UINT bw, bh;
	const UINT bpp = BlockBytes(t->Format, bw, bh);
	const UINT mip = sub % t->Mips;
	const UINT w = (t->MipW(mip) + bw - 1) / bw, h = (t->MipH(mip) + bh - 1) / bh;
	if (t->Usage == D3D11_USAGE_STAGING && t->Staging)
	{
		if (t->MapState == 2)
		{
			const uint8_t* base = static_cast<const uint8_t*>(wgpuBufferGetConstMappedRange(t->Staging, 0, (t->StagingBytes + 3) & ~3ull));
			if (!base) return E_FAIL;
			mapped->pData = const_cast<uint8_t*>(base + t->SubOffset[sub]);
			mapped->RowPitch = t->SubRowPitch[sub];
			mapped->DepthPitch = t->SubRowPitch[sub] * h;
			return S_OK;
		}
		if (t->MapState == 0)
		{
			D->Submit();
			t->MapState = 1;
			wgpuBufferMapAsync(t->Staging, WGPUMapMode_Read, 0, (t->StagingBytes + 3) & ~3ull, OnMapped, &t->MapState);
		}
		return DXGI_ERROR_WAS_STILL_DRAWING;
	}
	// DYNAMIC 텍스처: 서브리소스의 CPU 사본 (Unmap 때 writeTexture)
	std::vector<uint8_t>& p = t->Pending[sub];
	p.resize((size_t)w * bpp * h * t->MipD(mip));
	mapped->pData = p.data();
	mapped->RowPitch = w * bpp;
	mapped->DepthPitch = w * bpp * h;
	return S_OK;
}

void Ctx::Unmap(GfxResource* resource, UINT sub)
{
	if (Buf* b = BufOf(resource))
	{
		if (b->Desc.Usage == D3D11_USAGE_DYNAMIC)
			b->HasLoc = D->RingWrite(b->Shadow.data(), b->Size, 256, b->Loc);
		else if (!b->Shadow.empty() && b->Handle)
		{
			if (b->LastUse == D->EncoderSerial) D->Submit();
			wgpuQueueWriteBuffer(D->Queue, b->Handle, 0, b->Shadow.data(), b->Size);
		}
		else if (b->MapState == 2 && b->Handle)
		{
			wgpuBufferUnmap(b->Handle);
			b->MapState = 0;
		}
		return;
	}
	TexInfo* t = TexOf(resource);
	if (!t) return;
	if (t->Usage == D3D11_USAGE_STAGING)
	{
		if (t->MapState == 2 && t->Staging) { wgpuBufferUnmap(t->Staging); t->MapState = 0; }
		return;
	}
	auto it = t->Pending.find(sub);
	if (it == t->Pending.end()) return;
	if (t->LastUse == D->EncoderSerial) D->Submit();
	UINT bw, bh;
	const UINT bpp = BlockBytes(t->Format, bw, bh);
	const UINT mip = sub % t->Mips;
	D->UploadSub(*t, sub, nullptr, it->second.data(), ((t->MipW(mip) + bw - 1) / bw) * bpp, 0);
	t->Pending.erase(it);
}

void Ctx::UpdateSubresource(GfxResource* resource, UINT sub, const D3D11_BOX* box, const void* data, UINT rowPitch, UINT depthPitch)
{
	if (!data) return;
	if (Buf* b = BufOf(resource))
	{
		const UINT start = box ? box->left : 0;
		const UINT size = box ? box->right - box->left : b->Desc.ByteWidth;
		if (start >= b->Size || size == 0) return;
		if (b->Desc.Usage == D3D11_USAGE_DYNAMIC || !b->Shadow.empty())
		{
			if (b->Shadow.empty()) b->Shadow.assign(b->Size, 0);
			memcpy(b->Shadow.data() + start, data, (std::min<size_t>)(size, b->Size - start));
			if (b->Desc.Usage == D3D11_USAGE_DYNAMIC) { b->HasLoc = D->RingWrite(b->Shadow.data(), b->Size, 256, b->Loc); return; }
		}
		if (!b->Handle) return;
		if (b->LastUse == D->EncoderSerial) D->Submit();
		const uint64_t at = start & ~3ull;
		const uint64_t end = (std::min<uint64_t>)(b->Size, ((uint64_t)start + size + 3) & ~3ull);
		if (at == start && end - at == size)
			wgpuQueueWriteBuffer(D->Queue, b->Handle, start, data, size);
		else
		{
			// 4 바이트에 맞지 않는 범위: 사본이 있으면 그것으로, 없으면 0 으로 채운다 (드물다)
			std::vector<uint8_t> tmp(end - at, 0);
			if (!b->Shadow.empty()) memcpy(tmp.data(), b->Shadow.data() + at, tmp.size());
			memcpy(tmp.data() + (start - at), data, size);
			wgpuQueueWriteBuffer(D->Queue, b->Handle, at, tmp.data(), tmp.size());
			D->Once("unaligned-update", "UpdateSubresource on a buffer range that is not 4-byte aligned%s", "");
		}
		return;
	}
	TexInfo* t = TexOf(resource);
	if (!t || !t->Handle) return;
	if (t->LastUse == D->EncoderSerial) D->Submit();
	D->UploadSub(*t, sub, box, data, rowPitch, depthPitch);
}

void Ctx::CopyResource(GfxResource* dst, GfxResource* src)
{
	EndPass();
	FlushClears();
	Buf* bd = BufOf(dst);
	Buf* bs = BufOf(src);
	if (bd && bs)
	{
		WGPUBuffer sh = bs->Handle;
		uint64_t so = 0;
		if (bs->Desc.Usage == D3D11_USAGE_DYNAMIC) { if (!bs->HasLoc) return; sh = bs->Loc.Buffer; so = bs->Loc.Offset; }
		if (!sh || !bd->Handle) return;
		if (bd->MapState == 2) { wgpuBufferUnmap(bd->Handle); bd->MapState = 0; }
		wgpuCommandEncoderCopyBufferToBuffer(D->Enc(), sh, so, bd->Handle, 0, (std::min)(bd->Size, bs->Size));
		bd->LastUse = bs->LastUse = D->EncoderSerial;
		return;
	}
	TexInfo* td = TexOf(dst);
	TexInfo* ts = TexOf(src);
	if (!td || !ts || !ts->Handle) return;
	for (UINT l = 0; l < (std::min)(td->Layers, ts->Layers); ++l)
		for (UINT m = 0; m < (std::min)(td->Mips, ts->Mips); ++m)
			CopySubresourceRegion(dst, l * td->Mips + m, 0, 0, 0, src, l * ts->Mips + m, nullptr);
}

void Ctx::CopySubresourceRegion(GfxResource* dst, UINT dstSub, UINT x, UINT y, UINT z, GfxResource* src, UINT srcSub, const D3D11_BOX* box)
{
	EndPass();
	FlushClears();
	Buf* bd = BufOf(dst);
	Buf* bs = BufOf(src);
	if (bd && bs)
	{
		WGPUBuffer sh = bs->Handle;
		uint64_t so = 0;
		if (bs->Desc.Usage == D3D11_USAGE_DYNAMIC) { if (!bs->HasLoc) return; sh = bs->Loc.Buffer; so = bs->Loc.Offset; }
		if (!sh || !bd->Handle) return;
		const uint64_t from = box ? box->left : 0, size = box ? box->right - box->left : bs->Desc.ByteWidth;
		if ((from | x | size) & 3) { D->Once("copy-align", "CopySubresourceRegion on buffers needs 4-byte aligned ranges%s", ""); return; }
		wgpuCommandEncoderCopyBufferToBuffer(D->Enc(), sh, so + from, bd->Handle, x, size);
		bd->LastUse = bs->LastUse = D->EncoderSerial;
		return;
	}
	TexInfo* td = TexOf(dst);
	TexInfo* ts = TexOf(src);
	if (!td || !ts || !ts->Handle) return;
	const UINT sm = srcSub % ts->Mips, sl = srcSub / ts->Mips;
	const UINT dm = dstSub % td->Mips, dl = dstSub / td->Mips;
	UINT bw, bh;
	const UINT bpp = BlockBytes(ts->Format, bw, bh);
	UINT sx = 0, sy = 0, sz = 0, w = ts->MipW(sm), h = ts->MipH(sm), d = ts->Dim == WGPUTextureDimension_3D ? ts->MipD(sm) : 1;
	if (box) { sx = box->left; sy = box->top; sz = box->front; w = box->right - box->left; h = box->bottom - box->top; d = (std::max)(1u, box->back - box->front); }
	w = (w + bw - 1) / bw * bw;
	h = (h + bh - 1) / bh * bh;
	WGPUImageCopyTexture s = {};
	s.texture = ts->Handle;
	s.mipLevel = sm;
	s.origin = { sx, sy, ts->Dim == WGPUTextureDimension_3D ? sz : sl };
	s.aspect = WGPUTextureAspect_All;
	const WGPUExtent3D ext = { w, h, d };
	ts->LastUse = D->EncoderSerial;
	if (td->Usage == D3D11_USAGE_STAGING && td->Staging)
	{
		if (td->MapState == 2) { wgpuBufferUnmap(td->Staging); td->MapState = 0; }
		WGPUImageCopyBuffer b = {};
		b.buffer = td->Staging;
		b.layout.offset = td->SubOffset[dstSub] + (uint64_t)(y / bh) * td->SubRowPitch[dstSub] + (uint64_t)(x / bw) * bpp;
		b.layout.bytesPerRow = td->SubRowPitch[dstSub];
		b.layout.rowsPerImage = (td->MipH(dm) + bh - 1) / bh;
		wgpuCommandEncoderCopyTextureToBuffer(D->Enc(), &s, &b, &ext);
		return;
	}
	if (!td->Handle) return;
	WGPUImageCopyTexture t = {};
	t.texture = td->Handle;
	t.mipLevel = dm;
	t.origin = { x, y, td->Dim == WGPUTextureDimension_3D ? z : dl };
	t.aspect = WGPUTextureAspect_All;
	wgpuCommandEncoderCopyTextureToTexture(D->Enc(), &s, &t, &ext);
	td->LastUse = D->EncoderSerial;
}

void Ctx::GenerateMips(GfxShaderResourceView* srv)
{
	ViewInfo* v = ViewOf(srv);
	if (!v || !v->Tex || !v->Tex->Handle || v->Tex->Mips < 2 || IsCompressed(v->Tex->Format) || IsDepthFormat(v->Tex->Format)) return;
	EndPass();
	FlushClears();
	TexInfo& t = *v->Tex;
	for (UINT l = 0; l < t.Layers; ++l)
		for (UINT m = 1; m < t.Mips; ++m)
		{
			WGPUTextureView src = D->MakeView(t, WGPUTextureViewDimension_2D, v->Format, WGPUTextureAspect_All, m - 1, 1, l, 1);
			WGPUTextureView dst = D->MakeView(t, WGPUTextureViewDimension_2D, v->Format, WGPUTextureAspect_All, m, 1, l, 1);
			if (src && dst) D->Blit(src, dst, v->Format);
			if (src) wgpuTextureViewRelease(src);
			if (dst) wgpuTextureViewRelease(dst);
		}
	t.LastUse = D->EncoderSerial;
}

// 쿼리: 웹은 결과를 기다릴 수 없다 — 그리기를 막지 않는 값 (오클루전 = 보임, 시간 = 끊김)
HRESULT Ctx::GetData(GfxQuery* query, void* data, UINT size, UINT)
{
	if (!query || !data) return S_OK;
	D3D11_QUERY_DESC d;
	query->GetDesc(&d);
	memset(data, 0, size);
	switch (d.Query)
	{
	case D3D11_QUERY_OCCLUSION:
		if (size >= 8) *static_cast<UINT64*>(data) = 1;
		break;
	case D3D11_QUERY_OCCLUSION_PREDICATE:
	case D3D11_QUERY_EVENT:
		if (size >= sizeof(BOOL)) *static_cast<BOOL*>(data) = TRUE;
		break;
	case D3D11_QUERY_TIMESTAMP_DISJOINT:
		if (size >= sizeof(D3D11_QUERY_DATA_TIMESTAMP_DISJOINT))
		{
			auto* t = static_cast<D3D11_QUERY_DATA_TIMESTAMP_DISJOINT*>(data);
			t->Frequency = 1000000000ull;
			t->Disjoint = TRUE;
		}
		break;
	default:
		break;
	}
	return S_OK;
}
