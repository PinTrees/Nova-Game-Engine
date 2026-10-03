#include "pch.h"
#include "GfxVkInternal.h"

// Gfx Vulkan 컨텍스트: D3D11 즉시 컨텍스트의 뜻을 Vulkan 명령으로.
//  - 렌더링(동적 렌더링)은 그리기 때 시작하고, 타깃이 바뀌거나 복사 · 지우기 · 배치 바꾸기가 필요하면 끝낸다
//  - 그리기마다: 타깃 · 읽는 이미지의 배치 맞추기 → 파이프라인 (키 캐시) → 디스크립터 집합 (내용 캐시) → 정점 · 인덱스 → 동적 상태
namespace GfxVkImpl
{
	namespace
	{
		VkDeviceSize AlignUp(VkDeviceSize v, VkDeviceSize a) { return a > 1 ? (v + a - 1) / a * a : v; }

		VkImageLayout ReadLayout(const Image& img)
		{
			return img.Fmt.Depth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		}

		float LinearToSrgb(float c)
		{
			c = std::clamp(c, 0.0f, 1.0f);
			return c <= 0.0031308f ? c * 12.92f : 1.055f * powf(c, 1.0f / 2.4f) - 0.055f;
		}

		bool IsSrgb(VkFormat f)
		{
			return f == VK_FORMAT_R8G8B8A8_SRGB || f == VK_FORMAT_B8G8R8A8_SRGB || f == VK_FORMAT_BC1_RGBA_SRGB_BLOCK || f == VK_FORMAT_BC2_SRGB_BLOCK ||
				f == VK_FORMAT_BC3_SRGB_BLOCK || f == VK_FORMAT_BC7_SRGB_BLOCK;
		}

		// 바인딩 선언 (spv::Dim · 배열) 에 맞는 뷰 종류
		VkImageViewType ExpectedViewType(const GfxVkShared::BindingDesc& b)
		{
			switch (b.Dim)
			{
			case 0: return b.Arrayed ? VK_IMAGE_VIEW_TYPE_1D_ARRAY : VK_IMAGE_VIEW_TYPE_1D;
			case 2: return VK_IMAGE_VIEW_TYPE_3D;
			case 3: return b.Arrayed ? VK_IMAGE_VIEW_TYPE_CUBE_ARRAY : VK_IMAGE_VIEW_TYPE_CUBE;
			default: return b.Arrayed ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
			}
		}

		const D3D11_RASTERIZER_DESC& DefaultRs() { static const D3D11_RASTERIZER_DESC d = DefaultRasterizer(); return d; }
		const D3D11_BLEND_DESC& DefaultBs() { static const D3D11_BLEND_DESC d = DefaultBlend(); return d; }
		const D3D11_DEPTH_STENCIL_DESC& DefaultDs() { static const D3D11_DEPTH_STENCIL_DESC d = DefaultDepthStencil(); return d; }
		uint64_t DefaultRsHash() { static const uint64_t h = HashRasterizer(DefaultRs()); return h; }
		uint64_t DefaultBsHash() { static const uint64_t h = HashBlend(DefaultBs()); return h; }
		uint64_t DefaultDsHash() { static const uint64_t h = HashDepthStencil(DefaultDs()); return h; }

		Rtv* AsRtv(GfxRenderTargetView* v) { return v && v->Api() == GfxApi::Vulkan ? static_cast<Rtv*>(v) : nullptr; }
		Dsv* AsDsv(GfxDepthStencilView* v) { return v && v->Api() == GfxApi::Vulkan ? static_cast<Dsv*>(v) : nullptr; }
		Srv* AsSrv(GfxShaderResourceView* v) { return v && v->Api() == GfxApi::Vulkan ? static_cast<Srv*>(v) : nullptr; }
	}

	Ctx::~Ctx()
	{
		if (D && D->Immediate == this) D->Immediate = nullptr;
	}

	// ============================================================ 장벽 · 렌더링
	void Ctx::EndRendering()
	{
		if (!Rendering) return;
		vkCmdEndRendering(D->Cmd());
		Rendering = false;
		NeedBarrier = true;
	}

	void Ctx::FlushBarrier()
	{
		if (!NeedBarrier) return;
		Barrier({});
	}

	void Ctx::Barrier(const std::vector<VkImageMemoryBarrier2>& images)
	{
		if (images.empty() && !NeedBarrier) return;
		VkMemoryBarrier2 mb = { VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
		mb.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		mb.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
		mb.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		mb.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
		VkDependencyInfo dep = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
		if (NeedBarrier)
		{
			dep.memoryBarrierCount = 1;
			dep.pMemoryBarriers = &mb;
		}
		dep.imageMemoryBarrierCount = (uint32_t)images.size();
		dep.pImageMemoryBarriers = images.data();
		vkCmdPipelineBarrier2(D->Cmd(), &dep);
		NeedBarrier = false;
	}

	void Ctx::Transition(Image& img, UINT baseMip, UINT mips, UINT baseLayer, UINT layers, VkImageLayout newLayout, std::vector<VkImageMemoryBarrier2>& out)
	{
		if (!img.Handle) return;
		const UINT lastLayer = (std::min)(baseLayer + layers, img.Layers), lastMip = (std::min)(baseMip + mips, img.Mips);
		for (UINT layer = baseLayer; layer < lastLayer; ++layer)
		{
			UINT m = baseMip;
			while (m < lastMip)
			{
				const VkImageLayout from = img.LayoutOf(m, layer);
				if (from == newLayout) { ++m; continue; }
				// 같은 옛 배치가 이어지는 밉들을 한 장벽으로
				UINT end = m + 1;
				while (end < lastMip && img.LayoutOf(end, layer) == from) ++end;
				VkImageMemoryBarrier2 b = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
				b.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
				b.srcAccessMask = from == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_2_MEMORY_WRITE_BIT;
				b.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
				b.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
				b.oldLayout = from;
				b.newLayout = newLayout;
				b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				b.image = img.Handle;
				b.subresourceRange = { img.Fmt.Aspect, m, end - m, layer, 1 };
				out.push_back(b);
				for (UINT k = m; k < end; ++k) img.LayoutOf(k, layer) = newLayout;
				m = end;
			}
		}
	}

	void Ctx::TransitionNow(Image& img, UINT baseMip, UINT mips, UINT baseLayer, UINT layers, VkImageLayout newLayout)
	{
		std::vector<VkImageMemoryBarrier2> b;
		Transition(img, baseMip, mips, baseLayer, layers, newLayout, b);
		if (!b.empty()) EndRendering();
		Barrier(b);
	}

	void Ctx::BeforeTransfer()
	{
		EndRendering();
		FlushBarrier();
	}

	void Ctx::AfterSubmit()
	{
		Rendering = false;
		NeedBarrier = true;   // 앞 제출의 쓰기 → 다음 명령 (같은 큐라도 실행 의존이 저절로 생기지 않는다)
		BoundEpoch = ~0ull;
	}

	bool Ctx::IsWritableTarget(const Image* img, UINT baseMip, UINT mips, UINT baseLayer, UINT layers) const
	{
		auto overlap = [&](const ViewInfo& v) {
			return v.Img == img && v.BaseMip < baseMip + mips && baseMip < v.BaseMip + v.Mips && v.BaseLayer < baseLayer + layers && baseLayer < v.BaseLayer + v.Layers;
		};
		for (UINT i = 0; i < RtvCount; ++i)
			if (Rtv* r = AsRtv(Rtvs[i].Get()))
				if (overlap(r->V)) return true;
		if (Dsv* d = AsDsv(DsvView.Get()))
			if (!d->V.ReadOnlyDepth && overlap(d->V)) return true;
		return false;
	}

	// ============================================================ 정점 입력
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

	// ============================================================ 출력 · 래스터 상태
	void Ctx::OMSetRenderTargets(UINT count, GfxRenderTargetView* const* rtvs, GfxDepthStencilView* dsv)
	{
		count = (std::min)(count, 8u);
		bool same = DsvView.Get() == dsv && RtvCount == count;
		for (UINT i = 0; same && i < count; ++i)
			same = Rtvs[i].Get() == (rtvs ? rtvs[i] : nullptr);
		if (same) return;
		EndRendering();
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
	void Ctx::ResolveBuffer(GfxResource* gb, VkBuffer& buffer, VkDeviceSize& offset)
	{
		buffer = VK_NULL_HANDLE;
		offset = 0;
		Buf* b = BufOf(gb);
		if (!b) return;
		if (b->Buffer)
		{
			buffer = b->Buffer;
			if (b->Desc.Usage == D3D11_USAGE_STAGING) b->LastUse = D->Recording();
			return;
		}
		// DYNAMIC: 링 위치가 이번 기록에서 쓸 수 없으면 (조각이 바뀜) 사본에서 다시 올린다
		if (!b->HasLoc || !D->RingCurrent(b->Loc))
		{
			uint8_t* p = D->RingAlloc(b->Shadow.size(), 256, b->Loc);
			if (!p) { b->HasLoc = false; return; }
			memcpy(p, b->Shadow.data(), b->Shadow.size());
			b->HasLoc = true;
		}
		buffer = b->Loc.Buffer;
		offset = b->Loc.Offset;
	}

	VkDescriptorSet Ctx::BuildSet(std::vector<uint32_t>& dynamicOffsets, std::vector<VkImageMemoryBarrier2>& barriers, bool& ok)
	{
		ok = true;
		BindingLayout* L = Prog->Layout.Get();
		dynamicOffsets.clear();
		thread_local std::vector<uint64_t> key;
		thread_local std::vector<VkDescriptorImageInfo> images;
		thread_local std::vector<VkDescriptorBufferInfo> buffers;
		key.clear();
		images.assign(L->Elements, {});
		buffers.assign(L->Elements, {});
		key.push_back(L->Id);
		if (Values.size() < L->Elements) { ok = false; return VK_NULL_HANDLE; }
		for (uint32_t bi = 0; bi < (uint32_t)L->Bindings.size(); ++bi)
		{
			const GfxVkShared::BindingDesc& b = L->Bindings[bi];
			const uint32_t base = L->Offset[bi];
			for (uint32_t e = 0; e < b.Count; ++e)
			{
				const GfxVkShared::BindingValue& v = Values[base + e];
				switch (b.Type)
				{
				case GfxVkShared::BindingType::UniformBuffer:
				{
					if (!v.Ubo.Buffer) { ok = false; return VK_NULL_HANDLE; }
					buffers[base + e] = { v.Ubo.Buffer, L->DynamicUbo ? 0 : (VkDeviceSize)v.Ubo.Offset, (VkDeviceSize)(std::max<uint32_t>)(16, v.Range) };
					key.push_back(v.Ubo.BufferId);
					key.push_back(L->DynamicUbo ? v.Range : ((uint64_t)v.Ubo.Offset << 32 | v.Range));
					if (L->DynamicUbo) dynamicOffsets.push_back(v.Ubo.Offset);
					break;
				}
				case GfxVkShared::BindingType::SampledImage:
				{
					Srv* s = bi < Prog->Used.size() && Prog->Used[bi] ? AsSrv(v.View) : nullptr;   // 이 pass 가 쓰지 않는 칸 = 더미 (배치를 바꾸지 않는다)
					if (s && s->V.Img && s->V.Img->Handle)
					{
						const ViewInfo& vi = s->V;
						if (vi.Type != ExpectedViewType(b) && !(b.Dim == 1 && !b.Arrayed && vi.Type == VK_IMAGE_VIEW_TYPE_2D))
						{
							D->Once("viewtype:" + Prog->Name + ":" + std::to_string(bi), "%s",
								(Prog->Name + ": texture view type does not match the shader declaration (binding " + std::to_string(bi) + ") - using an empty texture").c_str());
							s = nullptr;
						}
						else if (b.Depth != vi.Img->Fmt.Depth && b.Depth)
						{
							D->Once("viewdepth:" + Prog->Name + ":" + std::to_string(bi), "%s",
								(Prog->Name + ": a color texture is bound to a shadow (comparison) slot - using an empty depth texture").c_str());
							s = nullptr;
						}
						else if (IsWritableTarget(vi.Img, vi.BaseMip, vi.Mips, vi.BaseLayer, vi.Layers))
						{
							// D3D11 은 렌더 타깃으로 묶인 텍스처를 셰이더에서 풀어 버린다 → 빈 텍스처
							D->Once("feedback:" + Prog->Name, "%s", (Prog->Name + ": a texture that is also the current render target was sampled - unbound like D3D11").c_str());
							s = nullptr;
						}
					}
					else s = nullptr;
					if (s)
					{
						const VkImageLayout layout = ReadLayout(*s->V.Img);
						Transition(*s->V.Img, s->V.BaseMip, s->V.Mips, s->V.BaseLayer, s->V.Layers, layout, barriers);
						images[base + e] = { VK_NULL_HANDLE, s->V.View, layout };
						key.push_back(s->V.Id);
					}
					else
					{
						const Dev::Dummy& dm = D->DummyImage(b);
						images[base + e] = { VK_NULL_HANDLE, dm.View, dm.Layout };
						key.push_back(dm.Id);
					}
					break;
				}
				case GfxVkShared::BindingType::Sampler:
				{
					auto* s = v.Sampler && v.Sampler->Api() == GfxApi::Vulkan ? static_cast<Sampler*>(v.Sampler) : nullptr;
					if (s)
					{
						images[base + e] = { s->Handle, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED };
						key.push_back(s->Id);
					}
					else
					{
						images[base + e] = { b.Comparison ? D->DummyCompare : D->DummySampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED };
						key.push_back(b.Comparison ? D->DummyCompareId : D->DummySamplerId);
					}
					break;
				}
				default:
					break;
				}
			}
		}
		// 같은 내용의 집합이 지금 풀에 있으면 그대로
		if (SetPoolGeneration != D->PoolGeneration)
		{
			SetCache.clear();
			SetPoolGeneration = D->PoolGeneration;
		}
		const uint64_t h = HashBytes(key.data(), key.size() * sizeof(uint64_t));
		auto it = SetCache.find(h);
		if (it != SetCache.end() && it->second.first == key)
			return it->second.second;
		VkDescriptorSet set = D->AllocateSet(L->SetLayout);
		if (!set) { ok = false; return VK_NULL_HANDLE; }
		if (SetPoolGeneration != D->PoolGeneration)
		{
			// 풀이 바뀌었다 (앞 풀이 꽉 참) — 캐시의 집합들은 옛 풀
			SetCache.clear();
			SetPoolGeneration = D->PoolGeneration;
		}
		thread_local std::vector<VkWriteDescriptorSet> writes;
		writes.clear();
		for (uint32_t bi = 0; bi < (uint32_t)L->Bindings.size(); ++bi)
		{
			const GfxVkShared::BindingDesc& b = L->Bindings[bi];
			VkWriteDescriptorSet w = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
			w.dstSet = set;
			w.dstBinding = bi;
			w.descriptorCount = b.Count;
			const uint32_t base = L->Offset[bi];
			switch (b.Type)
			{
			case GfxVkShared::BindingType::UniformBuffer:
				w.descriptorType = L->DynamicUbo ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
				w.pBufferInfo = &buffers[base];
				break;
			case GfxVkShared::BindingType::SampledImage:
				w.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
				w.pImageInfo = &images[base];
				break;
			case GfxVkShared::BindingType::Sampler:
				w.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
				w.pImageInfo = &images[base];
				break;
			default:
				continue;
			}
			writes.push_back(w);
		}
		vkUpdateDescriptorSets(D->Device, (uint32_t)writes.size(), writes.data(), 0, nullptr);
		SetCache[h] = { key, set };
		return set;
	}

	bool Ctx::PrepareDraw(bool indexed)
	{
		if (D->Lost) return false;
		if (!Prog)
		{
			D->Once("no-program", "%s", "draw without an applied effect pass - skipped");
			return false;
		}
		if (ViewportCount == 0 || Viewports[0].Width <= 0 || Viewports[0].Height <= 0)
		{
			D->Once("no-viewport", "%s", "draw without a viewport - skipped");
			return false;
		}
		InputLayout* layout = Layout && Layout->Api() == GfxApi::Vulkan ? static_cast<InputLayout*>(Layout.Get()) : nullptr;

		// ---- 타깃
		thread_local std::vector<VkImageMemoryBarrier2> barriers;
		barriers.clear();
		Rtv* colors[8] = {};
		UINT colorCount = 0;
		for (UINT i = 0; i < RtvCount; ++i)
			if ((colors[i] = AsRtv(Rtvs[i].Get())) != nullptr) colorCount = i + 1;
		Dsv* dsv = AsDsv(DsvView.Get());
		for (UINT i = 0; i < colorCount; ++i)
			if (colors[i])
				Transition(*colors[i]->V.Img, colors[i]->V.BaseMip, 1, colors[i]->V.BaseLayer, colors[i]->V.Layers, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, barriers);
		const VkImageLayout depthLayout = dsv && dsv->V.ReadOnlyDepth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		if (dsv)
			Transition(*dsv->V.Img, dsv->V.BaseMip, 1, dsv->V.BaseLayer, dsv->V.Layers, depthLayout, barriers);

		// ---- 디스크립터 (읽는 이미지의 배치도 여기서)
		thread_local std::vector<uint32_t> offsets;
		bool ok = true;
		VkDescriptorSet set = BuildSet(offsets, barriers, ok);
		if (!ok || !set) return false;

		// ---- 정점 · 인덱스 버퍼 (링 다시 올리기는 렌더링 밖이 아니어도 된다 — 호스트 메모리 쓰기)
		VkBuffer vbs[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
		VkDeviceSize vbOffsets[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
		if (layout)
			for (UINT s = 0; s < D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT; ++s)
				if (layout->SlotMask & (1u << s))
				{
					ResolveBuffer(Vbs[s].Buf.Get(), vbs[s], vbOffsets[s]);
					if (!vbs[s])
					{
						D->Once("no-vb", "%s", "draw with an empty vertex buffer slot - skipped");
						return false;
					}
					vbOffsets[s] += Vbs[s].Offset;
				}
		VkBuffer ib = VK_NULL_HANDLE;
		VkDeviceSize ibOffset = 0;
		if (indexed)
		{
			ResolveBuffer(Ib.Get(), ib, ibOffset);
			if (!ib) return false;
			ibOffset += IbOffset;
		}

		if (!barriers.empty())
		{
			EndRendering();
			Barrier(barriers);
		}

		// ---- 렌더링 시작 (타깃이 바뀌었거나 앞에서 끝냈으면)
		VkCommandBuffer cb = D->Cmd();
		if (!Rendering)
		{
			FlushBarrier();
			VkRenderingAttachmentInfo ca[8] = {};
			VkRenderingAttachmentInfo da = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
			uint32_t w = UINT32_MAX, h = UINT32_MAX, layers = UINT32_MAX;
			for (UINT i = 0; i < colorCount; ++i)
			{
				ca[i].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
				if (!colors[i]) continue;
				const ViewInfo& v = colors[i]->V;
				ca[i].imageView = v.View;
				ca[i].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				ca[i].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
				ca[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
				w = (std::min)(w, v.Img->MipW(v.BaseMip));
				h = (std::min)(h, v.Img->MipH(v.BaseMip));
				layers = (std::min)(layers, v.Layers);
			}
			VkRenderingInfo ri = { VK_STRUCTURE_TYPE_RENDERING_INFO };
			if (dsv)
			{
				const ViewInfo& v = dsv->V;
				da.imageView = v.View;
				da.imageLayout = depthLayout;
				da.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
				da.storeOp = dsv->V.ReadOnlyDepth ? VK_ATTACHMENT_STORE_OP_NONE : VK_ATTACHMENT_STORE_OP_STORE;
				w = (std::min)(w, v.Img->MipW(v.BaseMip));
				h = (std::min)(h, v.Img->MipH(v.BaseMip));
				layers = (std::min)(layers, v.Layers);
				if (v.Img->Fmt.Depth) ri.pDepthAttachment = &da;
				if (v.Img->Fmt.Stencil) ri.pStencilAttachment = &da;
			}
			if (w == UINT32_MAX)
			{
				// 타깃 없음: 뷰포트 크기
				w = (uint32_t)(std::max)(1.0f, Viewports[0].TopLeftX + Viewports[0].Width);
				h = (uint32_t)(std::max)(1.0f, Viewports[0].TopLeftY + Viewports[0].Height);
				layers = 1;
			}
			RenderArea = { { 0, 0 }, { w, h } };
			ri.renderArea = RenderArea;
			ri.layerCount = layers == UINT32_MAX ? 1 : layers;
			ri.colorAttachmentCount = colorCount;
			ri.pColorAttachments = ca;
			vkCmdBeginRendering(cb, &ri);
			Rendering = true;
			DynamicDirty = true;   // 가위 기본값 = 렌더 영역
		}

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
			key.ReadOnlyDepth = dsv->V.ReadOnlyDepth ? 1 : 0;
		}
		key.Samples = (uint8_t)samples;
		key.SampleMask = SampleMask;
		uint32_t patch = 0;
		key.Topology = (uint8_t)VkMap::Topology(Topo, patch);
		key.PatchPoints = (uint8_t)patch;
		key.Strip = VkMap::IsStrip(Topo) && indexed ? 1 : 0;
		const UINT viewports = D->Features.multiViewport ? (std::max)(1u, ViewportCount) : 1u;
		key.Viewports = (uint8_t)viewports;
		VkPipeline pipe = D->GetPipeline(key, Prog.Get(), layout, rs, bs, ds);
		if (!pipe) return false;

		if (BoundEpoch != D->Epoch)
		{
			BoundEpoch = D->Epoch;
			BoundPipeline = VK_NULL_HANDLE;
			BoundSet = VK_NULL_HANDLE;
			DynamicDirty = true;
		}
		bool rebindSet = false;
		if (pipe != BoundPipeline)
		{
			vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
			BoundPipeline = pipe;
			rebindSet = true;   // 다른 효과의 파이프라인 배치일 수 있다
		}
		if (rebindSet || set != BoundSet || offsets != BoundOffsets)
		{
			vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, Prog->Layout->PipelineLayout, 0, 1, &set, (uint32_t)offsets.size(), offsets.data());
			BoundSet = set;
			BoundOffsets = offsets;
		}
		if (layout)
			for (UINT s = 0; s < D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT; ++s)
				if (layout->SlotMask & (1u << s))
				{
					const VkDeviceSize stride = Vbs[s].Stride;
					vkCmdBindVertexBuffers2(cb, s, 1, &vbs[s], &vbOffsets[s], nullptr, &stride);
				}
		if (indexed)
			vkCmdBindIndexBuffer(cb, ib, ibOffset, IbFormat == DXGI_FORMAT_R32_UINT ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16);

		// ---- 동적 상태 (뷰포트 · 가위 · 블렌드 상수 · 스텐실 기준)
		if (DynamicDirty)
		{
			VkViewport vp[16];
			VkRect2D sc[16];
			for (UINT i = 0; i < viewports; ++i)
			{
				const D3D11_VIEWPORT& v = Viewports[(std::min)(i, ViewportCount - 1)];
				vp[i] = { v.TopLeftX, v.TopLeftY, v.Width, v.Height, v.MinDepth, v.MaxDepth };
				if (rs.ScissorEnable && i < ScissorCount)
				{
					const D3D11_RECT& r = Scissors[i];
					const LONG x0 = (std::max)(0L, r.left), y0 = (std::max)(0L, r.top);
					const LONG x1 = (std::max)(x0, r.right), y1 = (std::max)(y0, r.bottom);
					sc[i] = { { (int32_t)x0, (int32_t)y0 }, { (uint32_t)(x1 - x0), (uint32_t)(y1 - y0) } };
				}
				else
					sc[i] = RenderArea;
			}
			vkCmdSetViewport(cb, 0, viewports, vp);
			vkCmdSetScissor(cb, 0, viewports, sc);
			vkCmdSetBlendConstants(cb, BlendFactor);
			vkCmdSetStencilReference(cb, VK_STENCIL_FACE_FRONT_AND_BACK, StencilRef);
			DynamicDirty = false;
		}
		++D->DrawsSinceSubmit;
		return true;
	}

	void Ctx::Draw(UINT c, UINT s)
	{
		if (PrepareDraw(false)) vkCmdDraw(D->Cmd(), c, 1, s, 0);
	}

	void Ctx::DrawInstanced(UINT c, UINT n, UINT s, UINT si)
	{
		if (PrepareDraw(false)) vkCmdDraw(D->Cmd(), c, n, s, si);
	}

	void Ctx::DrawIndexedInstanced(UINT c, UINT n, UINT s, INT b, UINT si)
	{
		if (!Ib) return;
		if (PrepareDraw(true)) vkCmdDrawIndexed(D->Cmd(), c, n, s, b, si);
	}

	void Ctx::DrawAuto() { D->Once("drawauto", "%s", "DrawAuto (stream output) is not supported"); }
	void Ctx::Dispatch(UINT, UINT, UINT) { D->Once("dispatch", "%s", "compute dispatch is not supported yet"); }

	// ============================================================ 지우기 (D3D: 쓰기 마스크 · 가위와 상관없다)
	void Ctx::ClearRenderTargetView(GfxRenderTargetView* view, const FLOAT c[4])
	{
		Rtv* v = AsRtv(view);
		if (!v || !v->V.Img || D->Lost) return;
		Image& img = *v->V.Img;
		float color[4] = { c[0], c[1], c[2], c[3] };
		if (IsSrgb(v->V.Format) && !IsSrgb(img.Format))
			for (int i = 0; i < 3; ++i) color[i] = LinearToSrgb(color[i]);   // sRGB 뷰로 지우기 = 저장 값은 sRGB 로 바뀐 값
		VkClearColorValue cv = {};
		if (img.Fmt.Integer)
			for (int i = 0; i < 4; ++i) cv.uint32[i] = (uint32_t)c[i];
		else
			for (int i = 0; i < 4; ++i) cv.float32[i] = IsSrgb(v->V.Format) ? c[i] : color[i];
		// 렌더링 중이고 이 뷰가 지금 타깃 전체이면 렌더링 안에서
		if (Rendering)
		{
			for (UINT i = 0; i < RtvCount; ++i)
				if (Rtvs[i].Get() == view && RenderArea.extent.width == img.MipW(v->V.BaseMip) && RenderArea.extent.height == img.MipH(v->V.BaseMip))
				{
					VkClearAttachment ca = { VK_IMAGE_ASPECT_COLOR_BIT, i, {} };
					ca.clearValue.color = cv;
					const VkClearRect rect = { RenderArea, 0, v->V.Layers };
					vkCmdClearAttachments(D->Cmd(), 1, &ca, 1, &rect);
					return;
				}
		}
		BeforeTransfer();
		TransitionNow(img, v->V.BaseMip, 1, v->V.BaseLayer, v->V.Layers, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
		const VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, v->V.BaseMip, 1, img.Type == VK_IMAGE_TYPE_3D ? 0 : v->V.BaseLayer,
			img.Type == VK_IMAGE_TYPE_3D ? 1 : v->V.Layers };
		vkCmdClearColorImage(D->Cmd(), img.Handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &cv, 1, &range);
		NeedBarrier = true;
	}

	void Ctx::ClearDepthStencilView(GfxDepthStencilView* view, UINT flags, FLOAT depth, UINT8 stencil)
	{
		Dsv* v = AsDsv(view);
		if (!v || !v->V.Img || D->Lost) return;
		Image& img = *v->V.Img;
		VkImageAspectFlags aspect = 0;
		if (flags & D3D11_CLEAR_DEPTH) aspect |= VK_IMAGE_ASPECT_DEPTH_BIT;
		if ((flags & D3D11_CLEAR_STENCIL) && img.Fmt.Stencil) aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
		if (!aspect) return;
		const VkClearDepthStencilValue value = { depth, stencil };
		if (Rendering && DsvView.Get() == view && !v->V.ReadOnlyDepth &&
			RenderArea.extent.width == img.MipW(v->V.BaseMip) && RenderArea.extent.height == img.MipH(v->V.BaseMip))
		{
			VkClearAttachment ca = { aspect, 0, {} };
			ca.clearValue.depthStencil = value;
			const VkClearRect rect = { RenderArea, 0, v->V.Layers };
			vkCmdClearAttachments(D->Cmd(), 1, &ca, 1, &rect);
			return;
		}
		BeforeTransfer();
		TransitionNow(img, v->V.BaseMip, 1, v->V.BaseLayer, v->V.Layers, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
		const VkImageSubresourceRange range = { aspect, v->V.BaseMip, 1, v->V.BaseLayer, v->V.Layers };
		vkCmdClearDepthStencilImage(D->Cmd(), img.Handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &value, 1, &range);
		NeedBarrier = true;
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
			if (b->Desc.Usage != D3D11_USAGE_STAGING || !b->Mem.Mapped)
			{
				D->Once("map-default", "%s", "Map on a DEFAULT/IMMUTABLE buffer is not allowed");
				return E_INVALIDARG;
			}
			if (!waitFor(b->LastUse)) return DXGI_ERROR_WAS_STILL_DRAWING;
			m->pData = b->Mem.Mapped;
			m->RowPitch = m->DepthPitch = b->Desc.ByteWidth;
			return S_OK;
		}
		Image* t = ImageOf(r);
		if (!t || sub >= t->Layers * t->Mips) return E_INVALIDARG;
		const UINT mip = sub % t->Mips;
		const UINT64 row = VkMap::RowBytes(t->Fmt, t->MipW(mip)), slice = VkMap::SliceBytes(t->Fmt, t->MipW(mip), t->MipH(mip));
		if (t->Staging)
		{
			if (!waitFor(t->LastUse)) return DXGI_ERROR_WAS_STILL_DRAWING;
			m->pData = t->Mem.Mapped + t->SubOffset[sub];
			m->RowPitch = (UINT)row;
			m->DepthPitch = (UINT)slice;
			return S_OK;
		}
		if (t->Usage == D3D11_USAGE_DYNAMIC && (type == D3D11_MAP_WRITE_DISCARD || type == D3D11_MAP_WRITE_NO_OVERWRITE || type == D3D11_MAP_WRITE))
		{
			RingLoc loc;
			uint8_t* p = D->RingAlloc(slice * t->MipD(mip), (std::max<VkDeviceSize>)(16, t->Fmt.Bytes), loc);
			if (!p) return E_OUTOFMEMORY;
			t->Pending[sub] = { loc, p };
			m->pData = p;
			m->RowPitch = (UINT)row;
			m->DepthPitch = (UINT)slice;
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
				uint8_t* p = D->RingAlloc(b->Shadow.size(), 256, b->Loc);
				if (p)
				{
					memcpy(p, b->Shadow.data(), b->Shadow.size());
					b->HasLoc = true;
				}
				else b->HasLoc = false;
			}
			return;
		}
		Image* t = ImageOf(r);
		if (!t) return;
		auto it = t->Pending.find(sub);
		if (it == t->Pending.end()) return;
		const RingLoc loc = it->second.first;
		t->Pending.erase(it);
		const UINT mip = sub % t->Mips, layer = sub / t->Mips;
		BeforeTransfer();
		TransitionNow(*t, mip, 1, layer, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
		VkBufferImageCopy c = {};
		c.bufferOffset = loc.Offset;
		c.imageSubresource = { t->Fmt.Depth ? (VkImageAspectFlags)VK_IMAGE_ASPECT_DEPTH_BIT : (VkImageAspectFlags)VK_IMAGE_ASPECT_COLOR_BIT, mip,
			t->Type == VK_IMAGE_TYPE_3D ? 0 : layer, 1 };
		c.imageExtent = { t->MipW(mip), t->MipH(mip), t->MipD(mip) };
		vkCmdCopyBufferToImage(D->Cmd(), loc.Buffer, t->Handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
		NeedBarrier = true;
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
				D->WaitSerial(b->LastUse);
				memcpy(b->Mem.Mapped + off, data, size);
				return;
			}
			RingLoc loc;
			uint8_t* p = D->RingAlloc(size, 16, loc);
			if (!p) return;
			memcpy(p, data, size);
			BeforeTransfer();
			const VkBufferCopy c = { loc.Offset, off, size };
			vkCmdCopyBuffer(D->Cmd(), loc.Buffer, b->Buffer, 1, &c);
			NeedBarrier = true;
			return;
		}
		Image* t = ImageOf(r);
		if (!t || sub >= t->Layers * t->Mips) return;
		if (t->Staging)
		{
			D->WaitSerial(t->LastUse);
			const UINT mip = sub % t->Mips;
			const UINT64 dstRow = VkMap::RowBytes(t->Fmt, t->MipW(mip));
			const UINT rows = t->Fmt.Compressed ? (std::max)(1u, (t->MipH(mip) + 3) / 4) : t->MipH(mip);
			for (UINT y = 0; y < rows; ++y)
				memcpy(t->Mem.Mapped + t->SubOffset[sub] + y * dstRow, static_cast<const uint8_t*>(data) + (size_t)y * row, (size_t)dstRow);
			return;
		}
		const UINT mip = sub % t->Mips, layer = sub / t->Mips;
		BeforeTransfer();
		TransitionNow(*t, mip, 1, layer, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
		D->UploadImage(D->Cmd(), *t, sub, box, data, row, depth);
		NeedBarrier = true;
	}

	void Ctx::CopyImageRegion(Image& dst, UINT dstSub, UINT x, UINT y, UINT z, Image& src, UINT srcSub, const D3D11_BOX* box)
	{
		const UINT sm = srcSub % src.Mips, sl = srcSub / src.Mips, dm = dstSub % dst.Mips, dl = dstSub / dst.Mips;
		const UINT bx = box ? box->left : 0, by = box ? box->top : 0, bz = box ? box->front : 0;
		const UINT w = box ? box->right - box->left : src.MipW(sm), h = box ? box->bottom - box->top : src.MipH(sm), d = box ? box->back - box->front : src.MipD(sm);
		if (!w || !h || !d) return;
		BeforeTransfer();
		std::vector<VkImageMemoryBarrier2> b;
		Transition(src, sm, 1, sl, 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, b);
		Transition(dst, dm, 1, dl, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, b);
		Barrier(b);
		VkImageCopy regions[2] = {};
		uint32_t count = 0;
		auto add = [&](VkImageAspectFlags aspect) {
			VkImageCopy& c = regions[count++];
			c.srcSubresource = { aspect, sm, src.Type == VK_IMAGE_TYPE_3D ? 0 : sl, 1 };
			c.srcOffset = { (int32_t)bx, (int32_t)by, (int32_t)(src.Type == VK_IMAGE_TYPE_3D ? bz : 0) };
			c.dstSubresource = { aspect, dm, dst.Type == VK_IMAGE_TYPE_3D ? 0 : dl, 1 };
			c.dstOffset = { (int32_t)x, (int32_t)y, (int32_t)(dst.Type == VK_IMAGE_TYPE_3D ? z : 0) };
			c.extent = { w, h, src.Type == VK_IMAGE_TYPE_3D ? d : 1 };
		};
		if (src.Fmt.Depth)
		{
			add(VK_IMAGE_ASPECT_DEPTH_BIT);
			if (src.Fmt.Stencil && dst.Fmt.Stencil) add(VK_IMAGE_ASPECT_STENCIL_BIT);
		}
		else add(VK_IMAGE_ASPECT_COLOR_BIT);
		vkCmdCopyImage(D->Cmd(), src.Handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst.Handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, count, regions);
		NeedBarrier = true;
	}

	void Ctx::CopyResource(GfxResource* dst, GfxResource* src)
	{
		if (D->Lost) return;
		Buf* bd = BufOf(dst), * bs = BufOf(src);
		if (bd && bs)
		{
			VkBuffer s, d2;
			VkDeviceSize so, dO;
			ResolveBuffer(src, s, so);
			if (!bd->Buffer) { D->Once("copy-dynamic", "%s", "copy into a DYNAMIC buffer is not supported"); return; }
			ResolveBuffer(dst, d2, dO);
			if (!s) return;
			BeforeTransfer();
			const VkBufferCopy c = { so, dO, (std::min)(bs->Desc.ByteWidth, bd->Desc.ByteWidth) };
			vkCmdCopyBuffer(D->Cmd(), s, d2, 1, &c);
			NeedBarrier = true;
			return;
		}
		Image* td = ImageOf(dst), * ts = ImageOf(src);
		if (!td || !ts) return;
		const UINT mips = (std::min)(td->Mips, ts->Mips), layers = (std::min)(td->Layers, ts->Layers);
		for (UINT layer = 0; layer < layers; ++layer)
			for (UINT m = 0; m < mips; ++m)
				CopySubresourceRegion(dst, layer * td->Mips + m, 0, 0, 0, src, layer * ts->Mips + m, nullptr);
	}

	void Ctx::CopySubresourceRegion(GfxResource* dst, UINT dstSub, UINT x, UINT y, UINT z, GfxResource* src, UINT srcSub, const D3D11_BOX* box)
	{
		if (D->Lost) return;
		Buf* bd = BufOf(dst), * bs = BufOf(src);
		if (bd && bs)
		{
			VkBuffer s, d2;
			VkDeviceSize so, dO;
			ResolveBuffer(src, s, so);
			if (!bd->Buffer) { D->Once("copy-dynamic", "%s", "copy into a DYNAMIC buffer is not supported"); return; }
			ResolveBuffer(dst, d2, dO);
			if (!s) return;
			const UINT off = box ? box->left : 0, size = box ? box->right - box->left : bs->Desc.ByteWidth;
			BeforeTransfer();
			const VkBufferCopy c = { so + off, dO + x, size };
			vkCmdCopyBuffer(D->Cmd(), s, d2, 1, &c);
			NeedBarrier = true;
			return;
		}
		Image* td = ImageOf(dst), * ts = ImageOf(src);
		if (!td || !ts || dstSub >= td->Layers * td->Mips || srcSub >= ts->Layers * ts->Mips) return;
		if (td->Handle && ts->Handle)
		{
			CopyImageRegion(*td, dstSub, x, y, z, *ts, srcSub, box);
			return;
		}
		// 이미지 ↔ STAGING (버퍼): 서브리소스 전체 (box 는 원점부터의 크기만 따른다)
		const UINT sm = srcSub % ts->Mips, sl = srcSub / ts->Mips, dm = dstSub % td->Mips, dl = dstSub / td->Mips;
		BeforeTransfer();
		if (ts->Handle && td->Staging)
		{
			TransitionNow(*ts, sm, 1, sl, 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
			VkBufferImageCopy c = {};
			c.bufferOffset = td->SubOffset[dstSub];
			c.imageSubresource = { ts->Fmt.Depth ? (VkImageAspectFlags)VK_IMAGE_ASPECT_DEPTH_BIT : (VkImageAspectFlags)VK_IMAGE_ASPECT_COLOR_BIT, sm,
				ts->Type == VK_IMAGE_TYPE_3D ? 0 : sl, 1 };
			c.imageOffset = { box ? (int32_t)box->left : 0, box ? (int32_t)box->top : 0, 0 };
			c.imageExtent = { (std::min)(ts->MipW(sm), td->MipW(dm)), (std::min)(ts->MipH(sm), td->MipH(dm)), (std::min)(ts->MipD(sm), td->MipD(dm)) };
			vkCmdCopyImageToBuffer(D->Cmd(), ts->Handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, td->Staging, 1, &c);
			td->LastUse = D->Recording();
		}
		else if (ts->Staging && td->Handle)
		{
			TransitionNow(*td, dm, 1, dl, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
			VkBufferImageCopy c = {};
			c.bufferOffset = ts->SubOffset[srcSub];
			c.imageSubresource = { td->Fmt.Depth ? (VkImageAspectFlags)VK_IMAGE_ASPECT_DEPTH_BIT : (VkImageAspectFlags)VK_IMAGE_ASPECT_COLOR_BIT, dm,
				td->Type == VK_IMAGE_TYPE_3D ? 0 : dl, 1 };
			c.imageOffset = { (int32_t)x, (int32_t)y, (int32_t)(td->Type == VK_IMAGE_TYPE_3D ? z : 0) };
			c.imageExtent = { (std::min)(ts->MipW(sm), td->MipW(dm)), (std::min)(ts->MipH(sm), td->MipH(dm)), (std::min)(ts->MipD(sm), td->MipD(dm)) };
			vkCmdCopyBufferToImage(D->Cmd(), ts->Staging, td->Handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
			ts->LastUse = D->Recording();
		}
		else
			D->Once("copy-staging", "%s", "staging to staging texture copies are not supported");
		NeedBarrier = true;
	}

	void Ctx::GenerateMips(GfxShaderResourceView* view)
	{
		Srv* v = AsSrv(view);
		if (!v || !v->V.Img || v->V.Mips < 2 || D->Lost) return;
		Image& img = *v->V.Img;
		BeforeTransfer();
		const VkFilter filter = img.Fmt.Integer || img.Fmt.Depth ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
		for (UINT layer = v->V.BaseLayer; layer < v->V.BaseLayer + v->V.Layers; ++layer)
			for (UINT m = v->V.BaseMip + 1; m < v->V.BaseMip + v->V.Mips; ++m)
			{
				std::vector<VkImageMemoryBarrier2> b;
				Transition(img, m - 1, 1, layer, 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, b);
				Transition(img, m, 1, layer, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, b);
				Barrier(b);
				VkImageBlit blit = {};
				blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, m - 1, layer, 1 };
				blit.srcOffsets[1] = { (int32_t)img.MipW(m - 1), (int32_t)img.MipH(m - 1), (int32_t)img.MipD(m - 1) };
				blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, m, layer, 1 };
				blit.dstOffsets[1] = { (int32_t)img.MipW(m), (int32_t)img.MipH(m), (int32_t)img.MipD(m) };
				vkCmdBlitImage(D->Cmd(), img.Handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, img.Handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, filter);
				NeedBarrier = true;
			}
	}

	// ============================================================ 쿼리
	void Ctx::Begin(GfxQuery* q)
	{
		auto* g = q && q->Api() == GfxApi::Vulkan ? static_cast<Query*>(q) : nullptr;
		if (g && g->Dsc.Query == D3D11_QUERY_OCCLUSION)
			D->Once("occlusion", "%s", "occlusion queries are not supported yet (result = 1)");
	}

	void Ctx::End(GfxQuery* q)
	{
		auto* g = q && q->Api() == GfxApi::Vulkan ? static_cast<Query*>(q) : nullptr;
		if (!g || D->Lost) return;
		if (g->Pool && g->Dsc.Query != D3D11_QUERY_OCCLUSION)
		{
			// 앞 결과가 아직 GPU 에 있으면 끝날 때까지 (보통은 이미 읽은 쿼리를 다시 쓴다)
			if (g->Serial && !D->IsDone(g->Serial)) D->WaitSerial(g->Serial);
			vkResetQueryPool(D->Device, g->Pool, 0, 1);
			vkCmdWriteTimestamp2(D->Cmd(), VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, g->Pool, 0);
		}
		g->Serial = D->Recording();
	}

	HRESULT Ctx::GetData(GfxQuery* q, void* data, UINT size, UINT flags)
	{
		auto* g = q && q->Api() == GfxApi::Vulkan ? static_cast<Query*>(q) : nullptr;
		if (!g) return E_FAIL;
		if (!g->Serial) return S_FALSE;
		if (!D->IsDone(g->Serial))
		{
			if (D->Lost) return E_FAIL;
			if (g->Serial >= D->Recording() && !(flags & D3D11_ASYNC_GETDATA_DONOTFLUSH)) D->Submit(false);
			return S_FALSE;
		}
		switch (g->Dsc.Query)
		{
		case D3D11_QUERY_TIMESTAMP_DISJOINT:
			if (data && size >= sizeof(D3D11_QUERY_DATA_TIMESTAMP_DISJOINT))
				*static_cast<D3D11_QUERY_DATA_TIMESTAMP_DISJOINT*>(data) = { (UINT64)(1e9 / (std::max)(1e-6f, D->Props.limits.timestampPeriod)), g->Pool ? FALSE : TRUE };
			return S_OK;
		case D3D11_QUERY_TIMESTAMP:
		{
			uint64_t v = 0;
			if (g->Pool) vkGetQueryPoolResults(D->Device, g->Pool, 0, 1, sizeof(v), &v, sizeof(v), VK_QUERY_RESULT_64_BIT);
			if (data && size >= sizeof(UINT64)) *static_cast<UINT64*>(data) = v;
			return S_OK;
		}
		case D3D11_QUERY_OCCLUSION:
			if (data && size >= sizeof(UINT64)) *static_cast<UINT64*>(data) = 1;
			return S_OK;
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
		EndRendering();
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
		DynamicDirty = true;
	}
}

// ============================================================ 효과 → 컨텍스트
namespace GfxVkShared
{
	void SetProgram(GfxContext* context, GfxObject* program, const BindingValue* values, uint32_t count)
	{
		auto* c = static_cast<GfxVkImpl::Ctx*>(context);
		c->Prog = static_cast<GfxVkImpl::Program*>(program);
		c->Values.assign(values, values + count);
		// 값의 뷰 · 샘플러를 다음 Apply 까지 잡아 둔다 (D3D 의 컨텍스트가 묶인 SRV 를 잡는 것과 같이)
		c->Held.resize((size_t)count * 2);
		for (uint32_t i = 0; i < count; ++i)
		{
			c->Held[i * 2] = values[i].View;
			c->Held[i * 2 + 1] = values[i].Sampler;
		}
	}
}

// ============================================================ 텍스처 → CPU 이미지
namespace GfxVk
{
	using namespace GfxVkImpl;

	HRESULT CaptureTexture(GfxContext* context, GfxResource* texture, DirectX::ScratchImage& out)
	{
		auto* c = static_cast<Ctx*>(context);
		Dev* d = c->D.Get();
		GfxVkImpl::Image* t = ImageOf(texture);
		if (!t || t->Fmt.Compressed || d->Lost) return E_INVALIDARG;
		const bool all = t->Cube || t->Layers > 1;
		HRESULT hr = t->Type == VK_IMAGE_TYPE_3D ? E_NOTIMPL
			: t->Cube ? out.InitializeCube(t->Dxgi, t->Width, t->Height, (std::max)(1u, t->Layers / 6), t->Mips)
			: all ? out.Initialize2D(t->Dxgi, t->Width, t->Height, t->Layers, t->Mips)
			: out.Initialize2D(t->Dxgi, t->Width, t->Height, 1, 1);
		if (FAILED(hr)) return hr;
		const UINT layers = all ? t->Layers : 1, mips = all ? t->Mips : 1;
		if (t->Staging)
		{
			// CPU 쪽 텍스처: 기다린 뒤 그대로
			d->WaitSerial(t->LastUse);
			for (UINT layer = 0; layer < layers; ++layer)
				for (UINT m = 0; m < mips; ++m)
				{
					const DirectX::Image* img = out.GetImage(m, layer, 0);
					const UINT64 row = VkMap::RowBytes(t->Fmt, t->MipW(m));
					for (UINT y = 0; img && y < t->MipH(m); ++y)
						memcpy(img->pixels + y * img->rowPitch, t->Mem.Mapped + t->SubOffset[layer * t->Mips + m] + y * row, (size_t)(std::min<UINT64>)(row, img->rowPitch));
				}
			return S_OK;
		}
		// 읽기 버퍼 (호스트 캐시 메모리) 로 복사 → 제출 · 기다림
		std::vector<VkDeviceSize> offsets;
		VkDeviceSize size = 0;
		for (UINT layer = 0; layer < layers; ++layer)
			for (UINT m = 0; m < mips; ++m)
			{
				size = AlignUp(size, 16);
				offsets.push_back(size);
				size += VkMap::SliceBytes(t->Fmt, t->MipW(m), t->MipH(m));
			}
		VkBufferCreateInfo bi = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
		bi.size = size;
		bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		VkBuffer buffer = VK_NULL_HANDLE;
		if (vkCreateBuffer(d->Device, &bi, nullptr, &buffer) != VK_SUCCESS) return E_FAIL;
		VkMemoryRequirements req;
		vkGetBufferMemoryRequirements(d->Device, buffer, &req);
		Allocation mem;
		if (!d->Mem.Allocate(req, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT, true, mem))
		{
			vkDestroyBuffer(d->Device, buffer, nullptr);
			return E_OUTOFMEMORY;
		}
		vkBindBufferMemory(d->Device, buffer, mem.Memory, mem.Offset);
		c->BeforeTransfer();
		c->TransitionNow(*t, 0, mips, 0, layers, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
		std::vector<VkBufferImageCopy> regions;
		for (UINT layer = 0; layer < layers; ++layer)
			for (UINT m = 0; m < mips; ++m)
			{
				VkBufferImageCopy r = {};
				r.bufferOffset = offsets[layer * mips + m];
				r.imageSubresource = { t->Fmt.Depth ? (VkImageAspectFlags)VK_IMAGE_ASPECT_DEPTH_BIT : (VkImageAspectFlags)VK_IMAGE_ASPECT_COLOR_BIT, m, layer, 1 };
				r.imageExtent = { t->MipW(m), t->MipH(m), 1 };
				regions.push_back(r);
			}
		vkCmdCopyImageToBuffer(d->Cmd(), t->Handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, (uint32_t)regions.size(), regions.data());
		c->NeedBarrier = true;
		d->Submit(true);
		hr = d->Lost ? E_FAIL : S_OK;
		if (SUCCEEDED(hr))
			for (UINT layer = 0; layer < layers; ++layer)
				for (UINT m = 0; m < mips; ++m)
				{
					const DirectX::Image* img = out.GetImage(m, layer, 0);
					if (!img) continue;
					const UINT64 row = VkMap::RowBytes(t->Fmt, t->MipW(m));
					for (UINT y = 0; y < t->MipH(m); ++y)
						memcpy(img->pixels + y * img->rowPitch, mem.Mapped + offsets[layer * mips + m] + y * row, (size_t)(std::min<UINT64>)(row, img->rowPitch));
				}
		vkDestroyBuffer(d->Device, buffer, nullptr);   // 제출을 기다렸다
		d->Mem.Free(mem);
		return hr;
	}
}
