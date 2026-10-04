#pragma once
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan.h>
#include <string>

// Vulkan 함수 로더 — SDK 의 vulkan-1.lib 없이 vulkan-1.dll (그래픽 드라이버에 포함) 에서 실행 중에 함수를 불러온다.
//  - 프로세스에 VkInstance 하나 (VkLoader::Instance). 장치 함수도 vkGetInstanceProcAddr 로 받는다
//    (드라이버가 어느 장치로 보낼지 고르는 디스패치 함수 — 장치가 여럿이어도 같은 포인터를 쓴다)
//  - 새 함수가 필요하면 아래 목록에 한 줄 더하면 된다
#define NOVA_VK_GLOBAL_FUNCTIONS(X) \
	X(vkCreateInstance) \
	X(vkEnumerateInstanceExtensionProperties) \
	X(vkEnumerateInstanceLayerProperties) \
	X(vkEnumerateInstanceVersion)

#define NOVA_VK_INSTANCE_FUNCTIONS(X) \
	X(vkDestroyInstance) \
	X(vkEnumeratePhysicalDevices) \
	X(vkGetPhysicalDeviceProperties) \
	X(vkGetPhysicalDeviceProperties2) \
	X(vkGetPhysicalDeviceFeatures2) \
	X(vkGetPhysicalDeviceQueueFamilyProperties) \
	X(vkGetPhysicalDeviceMemoryProperties) \
	X(vkGetPhysicalDeviceFormatProperties) \
	X(vkGetPhysicalDeviceImageFormatProperties) \
	X(vkEnumerateDeviceExtensionProperties) \
	X(vkCreateDevice) \
	X(vkGetDeviceProcAddr) \
	X(vkDestroySurfaceKHR) \
	X(vkGetPhysicalDeviceSurfaceSupportKHR) \
	X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) \
	X(vkGetPhysicalDeviceSurfaceFormatsKHR) \
	X(vkGetPhysicalDeviceSurfacePresentModesKHR) \
	X(vkCreateWin32SurfaceKHR)

// 있으면 쓰는 것 (없어도 장치를 만든다)
#define NOVA_VK_OPTIONAL_INSTANCE_FUNCTIONS(X) \
	X(vkCreateDebugUtilsMessengerEXT) \
	X(vkDestroyDebugUtilsMessengerEXT) \
	X(vkSetDebugUtilsObjectNameEXT) \
	X(vkCmdBeginDebugUtilsLabelEXT) \
	X(vkCmdEndDebugUtilsLabelEXT) \
	X(vkCmdBeginConditionalRenderingEXT) \
	X(vkCmdEndConditionalRenderingEXT)

#define NOVA_VK_DEVICE_FUNCTIONS(X) \
	X(vkDestroyDevice) \
	X(vkGetDeviceQueue) \
	X(vkDeviceWaitIdle) \
	X(vkQueueSubmit2) \
	X(vkQueueWaitIdle) \
	X(vkAllocateMemory) \
	X(vkFreeMemory) \
	X(vkMapMemory) \
	X(vkUnmapMemory) \
	X(vkCreateBuffer) \
	X(vkDestroyBuffer) \
	X(vkGetBufferMemoryRequirements) \
	X(vkBindBufferMemory) \
	X(vkCreateImage) \
	X(vkDestroyImage) \
	X(vkGetImageMemoryRequirements) \
	X(vkBindImageMemory) \
	X(vkCreateImageView) \
	X(vkDestroyImageView) \
	X(vkCreateSampler) \
	X(vkDestroySampler) \
	X(vkCreateShaderModule) \
	X(vkDestroyShaderModule) \
	X(vkCreateDescriptorSetLayout) \
	X(vkDestroyDescriptorSetLayout) \
	X(vkCreatePipelineLayout) \
	X(vkDestroyPipelineLayout) \
	X(vkCreateGraphicsPipelines) \
	X(vkCreateComputePipelines) \
	X(vkDestroyPipeline) \
	X(vkCreatePipelineCache) \
	X(vkDestroyPipelineCache) \
	X(vkCreateDescriptorPool) \
	X(vkDestroyDescriptorPool) \
	X(vkResetDescriptorPool) \
	X(vkAllocateDescriptorSets) \
	X(vkUpdateDescriptorSets) \
	X(vkCreateCommandPool) \
	X(vkDestroyCommandPool) \
	X(vkResetCommandPool) \
	X(vkAllocateCommandBuffers) \
	X(vkBeginCommandBuffer) \
	X(vkEndCommandBuffer) \
	X(vkCreateSemaphore) \
	X(vkDestroySemaphore) \
	X(vkWaitSemaphores) \
	X(vkGetSemaphoreCounterValue) \
	X(vkCreateFence) \
	X(vkDestroyFence) \
	X(vkWaitForFences) \
	X(vkResetFences) \
	X(vkCreateQueryPool) \
	X(vkDestroyQueryPool) \
	X(vkResetQueryPool) \
	X(vkGetQueryPoolResults) \
	X(vkCmdPipelineBarrier2) \
	X(vkCmdBeginRendering) \
	X(vkCmdEndRendering) \
	X(vkCmdBindPipeline) \
	X(vkCmdBindDescriptorSets) \
	X(vkCmdBindVertexBuffers2) \
	X(vkCmdBindIndexBuffer) \
	X(vkCmdSetViewport) \
	X(vkCmdSetScissor) \
	X(vkCmdSetBlendConstants) \
	X(vkCmdSetStencilReference) \
	X(vkCmdDraw) \
	X(vkCmdDrawIndexed) \
	X(vkCmdDrawIndirect) \
	X(vkCmdDrawIndexedIndirect) \
	X(vkCmdDispatch) \
	X(vkCmdFillBuffer) \
	X(vkCmdCopyQueryPoolResults) \
	X(vkCmdClearAttachments) \
	X(vkCmdClearColorImage) \
	X(vkCmdClearDepthStencilImage) \
	X(vkCmdCopyBuffer) \
	X(vkCmdCopyImage) \
	X(vkCmdCopyBufferToImage) \
	X(vkCmdCopyImageToBuffer) \
	X(vkCmdBlitImage) \
	X(vkCmdResolveImage) \
	X(vkCmdWriteTimestamp2) \
	X(vkCmdBeginQuery) \
	X(vkCmdEndQuery) \
	X(vkCreateSwapchainKHR) \
	X(vkDestroySwapchainKHR) \
	X(vkGetSwapchainImagesKHR) \
	X(vkAcquireNextImageKHR) \
	X(vkQueuePresentKHR)

#define NOVA_VK_DECLARE(name) extern PFN_##name name;
NOVA_VK_GLOBAL_FUNCTIONS(NOVA_VK_DECLARE)
NOVA_VK_INSTANCE_FUNCTIONS(NOVA_VK_DECLARE)
NOVA_VK_OPTIONAL_INSTANCE_FUNCTIONS(NOVA_VK_DECLARE)
NOVA_VK_DEVICE_FUNCTIONS(NOVA_VK_DECLARE)
#undef NOVA_VK_DECLARE

namespace VkLoader
{
	// vulkan-1.dll 을 열고 VkInstance 를 만든다 (처음 한 번, 이후는 참조만 +1). 검증 레이어 = Debug 빌드 + SDK 가 있을 때
	//  (환경 변수 NOVA_VK_VALIDATION=0/1 로 끄고 켤 수 있다). 실패하면 false + error (드라이버 없음 · 1.3 미만 …)
	bool Acquire(std::string& error);
	void Release();      // 마지막이면 인스턴스를 없앤다
	VkInstance Instance();
	uint32_t InstanceVersion();
	bool ValidationEnabled();
	bool HasSurfaceExtensions();   // VK_KHR_surface + VK_KHR_win32_surface (창에 그리기)
	// 검증 레이어 메시지 수 (검사 명령이 보고한다)
	int ValidationErrorCount();
	int ValidationWarningCount();
	void ResetValidationCounts();

	const char* ResultName(VkResult r);
}
