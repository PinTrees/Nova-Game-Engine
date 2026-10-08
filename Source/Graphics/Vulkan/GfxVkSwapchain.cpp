#include "pch.h"
#include "GfxVkInternal.h"

// Vulkan 창 표시: 창마다 스왑체인 (본 창 + ImGui 가 만든 OS 창).
//  엔진은 늘 텍스처(RGBA8)에 그리고 Present 가 스왑체인 이미지로 복사(blit — RGBA → BGRA 도)한다 (GL 과 같은 방식).
//  Vulkan 은 프레임버퍼 행 0 = 위 → 뒤집지 않는다. 창 크기가 바뀌거나 OUT_OF_DATE 면 다시 만든다.
//  수직 동기 0 = MAILBOX (없으면 IMMEDIATE), 1 이상 = FIFO. 본 창 기준으로 CPU 가 GPU 보다 2 프레임 넘게 앞서 가지 않는다
namespace GfxVkImpl
{
	Dev::Swap* Dev::SwapFor(HWND wnd, std::string& error)
	{
		auto it = Swaps.find(wnd);
		if (it != Swaps.end()) return it->second.get();
		auto s = std::make_unique<Swap>();
		s->Wnd = wnd;
		if (!CreateSurface(*s, error))
		{
			DestroySwapchain(*s, true);
			return nullptr;
		}
		Swap* p = s.get();
		Swaps[wnd] = std::move(s);
		return p;
	}

	bool Dev::CreateSurface(Swap& s, std::string& error)
	{
		if (!VkLoader::HasSurfaceExtensions() || !vkCreateWin32SurfaceKHR)
		{
			error = "VK_KHR_win32_surface is not available";
			return false;
		}
		VkWin32SurfaceCreateInfoKHR si = { VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
		si.hinstance = ::GetModuleHandleW(nullptr);
		si.hwnd = s.Wnd;
		VkResult r = vkCreateWin32SurfaceKHR(VkLoader::Instance(), &si, nullptr, &s.Surface);
		if (r != VK_SUCCESS)
		{
			error = std::string("vkCreateWin32SurfaceKHR: ") + VkLoader::ResultName(r);
			s.Surface = VK_NULL_HANDLE;
			return false;
		}
		VkBool32 present = VK_FALSE;
		vkGetPhysicalDeviceSurfaceSupportKHR(Phys, QueueFamily, s.Surface, &present);
		if (!present)
		{
			error = "the graphics queue cannot present to this window";
			return false;
		}
		return true;
	}

	void Dev::DestroySwapchain(Swap& s, bool surfaceToo)
	{
		if (Device)
		{
			for (VkSemaphore x : s.Acquire) vkDestroySemaphore(Device, x, nullptr);
			for (VkSemaphore x : s.Done) vkDestroySemaphore(Device, x, nullptr);
			if (s.Chain) vkDestroySwapchainKHR(Device, s.Chain, nullptr);
		}
		s.Acquire.clear();
		s.Done.clear();
		s.Images.clear();
		s.Chain = VK_NULL_HANDLE;
		if (surfaceToo && s.Surface)
		{
			vkDestroySurfaceKHR(VkLoader::Instance(), s.Surface, nullptr);
			s.Surface = VK_NULL_HANDLE;
		}
	}

	void Dev::DestroyAllSwapchains()
	{
		for (auto& [w, s] : Swaps)
			DestroySwapchain(*s, true);
		Swaps.clear();
	}

	void Dev::ReleaseWindow(HWND wnd)
	{
		auto it = Swaps.find(wnd);
		if (it == Swaps.end()) return;
		// 그 창의 이미지를 쓰는 명령이 다 끝난 뒤 (창 닫기는 드물다)
		Submit(false);
		if (Device) vkDeviceWaitIdle(Device);
		Poll();
		DestroySwapchain(*it->second, true);
		Swaps.erase(it);
	}

	bool Dev::RecreateSwapchain(Swap& s, uint32_t width, uint32_t height, int interval)
	{
		// 앞 스왑체인 이미지를 쓰는 명령이 모두 끝난 뒤 (창 크기 바꾸기는 드물다)
		Submit(false);
		vkDeviceWaitIdle(Device);
		Poll();
		VkSurfaceCapabilitiesKHR caps;
		if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(Phys, s.Surface, &caps) != VK_SUCCESS) return false;
		VkExtent2D extent = caps.currentExtent.width != UINT32_MAX ? caps.currentExtent : VkExtent2D{ width, height };
		if (extent.width == 0 || extent.height == 0) return false;   // 최소화
		uint32_t n = 0;
		vkGetPhysicalDeviceSurfaceFormatsKHR(Phys, s.Surface, &n, nullptr);
		std::vector<VkSurfaceFormatKHR> formats(n);
		vkGetPhysicalDeviceSurfaceFormatsKHR(Phys, s.Surface, &n, formats.data());
		VkSurfaceFormatKHR fmt = formats.empty() ? VkSurfaceFormatKHR{ VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR } : formats[0];
		for (const auto& f : formats)
			if ((f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
			{
				fmt = f;   // UNORM (백버퍼 값 그대로 — DX11 스왑체인과 같음)
				break;
			}
		vkGetPhysicalDeviceSurfacePresentModesKHR(Phys, s.Surface, &n, nullptr);
		std::vector<VkPresentModeKHR> modes(n);
		vkGetPhysicalDeviceSurfacePresentModesKHR(Phys, s.Surface, &n, modes.data());
		auto has = [&](VkPresentModeKHR m) { return std::find(modes.begin(), modes.end(), m) != modes.end(); };
		VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
		if (interval <= 0)
			mode = has(VK_PRESENT_MODE_MAILBOX_KHR) ? VK_PRESENT_MODE_MAILBOX_KHR : has(VK_PRESENT_MODE_IMMEDIATE_KHR) ? VK_PRESENT_MODE_IMMEDIATE_KHR : VK_PRESENT_MODE_FIFO_KHR;
		uint32_t images = (std::max)(caps.minImageCount + 1, 3u);
		if (caps.maxImageCount) images = (std::min)(images, caps.maxImageCount);

		VkSwapchainCreateInfoKHR ci = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
		ci.surface = s.Surface;
		ci.minImageCount = images;
		ci.imageFormat = fmt.format;
		ci.imageColorSpace = fmt.colorSpace;
		ci.imageExtent = extent;
		ci.imageArrayLayers = 1;
		ci.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		ci.preTransform = (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR : caps.currentTransform;
		ci.compositeAlpha = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR : VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
		ci.presentMode = mode;
		ci.clipped = VK_TRUE;
		ci.oldSwapchain = s.Chain;
		VkSwapchainKHR chain = VK_NULL_HANDLE;
		const VkResult r = vkCreateSwapchainKHR(Device, &ci, nullptr, &chain);
		DestroySwapchain(s, false);   // 앞 것 (oldSwapchain 으로 넘긴 뒤)
		if (r != VK_SUCCESS)
		{
			EditorLog::Write("Vulkan", "vkCreateSwapchainKHR failed: %s", VkLoader::ResultName(r));
			return false;
		}
		s.Chain = chain;
		s.Format = fmt.format;
		s.Extent = extent;
		s.RequestW = width;
		s.RequestH = height;
		s.Mode = mode;
		s.Interval = interval;
		vkGetSwapchainImagesKHR(Device, chain, &n, nullptr);
		s.Images.resize(n);
		vkGetSwapchainImagesKHR(Device, chain, &n, s.Images.data());
		VkSemaphoreCreateInfo si = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
		s.Acquire.resize(n + 1);
		s.Done.resize(n);
		for (auto& x : s.Acquire) vkCreateSemaphore(Device, &si, nullptr, &x);
		for (auto& x : s.Done) vkCreateSemaphore(Device, &si, nullptr, &x);
		s.AcquireIndex = 0;
		const char* modeName = mode == VK_PRESENT_MODE_MAILBOX_KHR ? "mailbox" : mode == VK_PRESENT_MODE_IMMEDIATE_KHR ? "immediate" : "fifo";
		EditorLog::Write("Vulkan", "swapchain %u x %u, %u images, format %d, %s%s", extent.width, extent.height, n, (int)fmt.format, modeName,
			s.Wnd == Window ? "" : " (window)");
		return true;
	}

	void Dev::PresentFrame(Swap& s, Tex2D* backBuffer, int width, int height, int interval, bool pace)
	{
		if (Lost) return;
		if (Immediate) Immediate->FinishAsync();   // 프레임 끝: 컴퓨트 결과를 기다린 뒤 (다음 프레임이 그 자원을 다시 쓰기 전에)
		if (!s.Surface || !backBuffer || width <= 0 || height <= 0 || s.Failed)
		{
			Submit(false);
			return;
		}
		if (!s.Chain || s.RequestW != (uint32_t)width || s.RequestH != (uint32_t)height || s.Interval != interval)
		{
			if (!RecreateSwapchain(s, (uint32_t)width, (uint32_t)height, interval))
			{
				Submit(false);
				return;
			}
		}
		VkSemaphore acquire = s.Acquire[s.AcquireIndex++ % s.Acquire.size()];
		uint32_t index = 0;
		VkResult r = vkAcquireNextImageKHR(Device, s.Chain, 1000ull * 1000 * 1000, acquire, VK_NULL_HANDLE, &index);
		if (r == VK_ERROR_OUT_OF_DATE_KHR)
		{
			s.Interval = -1;   // 다음 프레임에 다시 만든다
			Submit(false);
			return;
		}
		if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR)
		{
			EditorLog::Write("Vulkan", "vkAcquireNextImageKHR: %s", VkLoader::ResultName(r));
			if (r == VK_ERROR_DEVICE_LOST) Lost = true;
			if (r == VK_ERROR_SURFACE_LOST_KHR) s.Failed = true;
			Submit(false);
			return;
		}

		// 텍스처 → 스왑체인 이미지 (블릿: 크기 · RGBA ↔ BGRA)
		Ctx* c = Immediate;
		Image& src = backBuffer->I;
		if (c)
		{
			c->BeforeTransfer();
			c->TransitionNow(src, 0, 1, 0, 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
		}
		VkImage dst = s.Images[index];
		auto barrier = [&](VkImageLayout from, VkImageLayout to) {
			VkImageMemoryBarrier2 b = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
			b.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
			b.srcAccessMask = from == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_2_MEMORY_WRITE_BIT;
			b.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
			b.dstAccessMask = to == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR ? 0 : VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
			b.oldLayout = from;
			b.newLayout = to;
			b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			b.image = dst;
			b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			VkDependencyInfo dep = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
			dep.imageMemoryBarrierCount = 1;
			dep.pImageMemoryBarriers = &b;
			vkCmdPipelineBarrier2(Cmd(), &dep);
		};
		barrier(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
		VkImageBlit blit = {};
		blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		blit.srcOffsets[1] = { (int32_t)(std::min)(src.Width, s.Extent.width), (int32_t)(std::min)(src.Height, s.Extent.height), 1 };
		blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		blit.dstOffsets[1] = blit.srcOffsets[1];
		vkCmdBlitImage(Cmd(), src.Handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);
		barrier(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

		VkSemaphore done = s.Done[index];
		Submit(false, acquire, done);
		const uint64_t serial = Submitted;
		VkPresentInfoKHR pi = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
		pi.waitSemaphoreCount = 1;
		pi.pWaitSemaphores = &done;
		pi.swapchainCount = 1;
		pi.pSwapchains = &s.Chain;
		pi.pImageIndices = &index;
		r = vkQueuePresentKHR(Queue, &pi);
		if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR)
			s.Interval = -1;   // 다음 프레임에 다시 만든다
		else if (r != VK_SUCCESS)
		{
			Once("present-fail", "%s", (std::string("vkQueuePresentKHR: ") + VkLoader::ResultName(r)).c_str());
			if (r == VK_ERROR_DEVICE_LOST) Lost = true;
		}
		if (!pace) return;
		// 2 프레임 넘게 앞서 가지 않는다 (링 · 디스크립터 풀이 끝없이 늘지 않게)
		Frames.push_back(serial);
		while (Frames.size() > 2)
		{
			WaitSerial(Frames.front());
			Frames.pop_front();
		}
	}
}
