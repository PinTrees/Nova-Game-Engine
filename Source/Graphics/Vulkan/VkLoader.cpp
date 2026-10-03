#include "pch.h"
#include "VkLoader.h"
#include <mutex>

#define NOVA_VK_DEFINE(name) PFN_##name name = nullptr;
NOVA_VK_GLOBAL_FUNCTIONS(NOVA_VK_DEFINE)
NOVA_VK_INSTANCE_FUNCTIONS(NOVA_VK_DEFINE)
NOVA_VK_OPTIONAL_INSTANCE_FUNCTIONS(NOVA_VK_DEFINE)
NOVA_VK_DEVICE_FUNCTIONS(NOVA_VK_DEFINE)
#undef NOVA_VK_DEFINE

namespace
{
	std::mutex s_Lock;
	HMODULE s_Dll = nullptr;
	PFN_vkGetInstanceProcAddr s_GetInstanceProcAddr = nullptr;
	VkInstance s_Instance = VK_NULL_HANDLE;
	VkDebugUtilsMessengerEXT s_Messenger = VK_NULL_HANDLE;
	int s_Refs = 0;
	uint32_t s_Version = 0;
	bool s_Validation = false;
	bool s_SyncValidation = false;   // 검증 레이어의 동기화 검사 (NOVA_VK_SYNC_VALIDATION=1 — 느리다)
	bool s_Surface = false;
	std::atomic<int> s_Errors{ 0 }, s_Warnings{ 0 };
	std::set<int32_t> s_Reported;   // 같은 메시지(id)는 한 번만 기록
	std::mutex s_ReportLock;

	VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT type,
		const VkDebugUtilsMessengerCallbackDataEXT* data, void*)
	{
		const bool error = (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0;
		// 검증 메시지만 센다 (로더의 일반 메시지 — 다른 프로그램의 레이어 JSON 이 없다 등 — 는 기록만)
		if (type & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT)
		{
			if (error) ++s_Errors;
			else ++s_Warnings;
		}
		{
			std::lock_guard<std::mutex> lock(s_ReportLock);
			if (!s_Reported.insert(data->messageIdNumber).second || s_Reported.size() > 200)
				return VK_FALSE;
		}
		EditorLog::Write("Vulkan", "%s %s: %s", error ? "ERROR" : "warning", data->pMessageIdName ? data->pMessageIdName : "",
			data->pMessage ? data->pMessage : "");
		return VK_FALSE;
	}

	bool WantValidation()
	{
		char buf[8] = {};
		if (::GetEnvironmentVariableA("NOVA_VK_VALIDATION", buf, sizeof(buf)))
			return buf[0] == '1';
#ifdef _DEBUG
		return true;
#else
		return false;
#endif
	}
}

namespace VkLoader
{
	bool Acquire(std::string& error)
	{
		std::lock_guard<std::mutex> lock(s_Lock);
		if (s_Instance)
		{
			++s_Refs;
			return true;
		}
		if (!s_Dll)
			s_Dll = ::LoadLibraryW(L"vulkan-1.dll");
		if (!s_Dll)
		{
			error = "vulkan-1.dll not found (no Vulkan driver)";
			return false;
		}
		s_GetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(::GetProcAddress(s_Dll, "vkGetInstanceProcAddr"));
		if (!s_GetInstanceProcAddr)
		{
			error = "vkGetInstanceProcAddr not found in vulkan-1.dll";
			return false;
		}
#define NOVA_VK_LOAD_GLOBAL(name) name = reinterpret_cast<PFN_##name>(s_GetInstanceProcAddr(VK_NULL_HANDLE, #name));
		NOVA_VK_GLOBAL_FUNCTIONS(NOVA_VK_LOAD_GLOBAL)
#undef NOVA_VK_LOAD_GLOBAL
		if (!vkCreateInstance || !vkEnumerateInstanceVersion)
		{
			error = "Vulkan 1.1+ loader required";
			return false;
		}
		vkEnumerateInstanceVersion(&s_Version);
		if (s_Version < VK_API_VERSION_1_3)
		{
			error = "Vulkan 1.3 required (loader " + std::to_string(VK_API_VERSION_MAJOR(s_Version)) + "." + std::to_string(VK_API_VERSION_MINOR(s_Version)) + ")";
			return false;
		}

		// 확장 · 레이어
		uint32_t n = 0;
		vkEnumerateInstanceExtensionProperties(nullptr, &n, nullptr);
		std::vector<VkExtensionProperties> exts(n);
		vkEnumerateInstanceExtensionProperties(nullptr, &n, exts.data());
		auto hasExt = [&](const char* name) {
			for (const auto& e : exts)
				if (strcmp(e.extensionName, name) == 0) return true;
			return false;
		};
		std::vector<const char*> enable;
		s_Surface = hasExt(VK_KHR_SURFACE_EXTENSION_NAME) && hasExt(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
		if (s_Surface)
		{
			enable.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
			enable.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
		}
		const bool debugUtils = hasExt(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		if (debugUtils)
			enable.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		std::vector<const char*> layers;
		s_Validation = false;
		if (WantValidation() && debugUtils)
		{
			vkEnumerateInstanceLayerProperties(&n, nullptr);
			std::vector<VkLayerProperties> props(n);
			vkEnumerateInstanceLayerProperties(&n, props.data());
			for (const auto& p : props)
				if (strcmp(p.layerName, "VK_LAYER_KHRONOS_validation") == 0)
				{
					layers.push_back("VK_LAYER_KHRONOS_validation");
					s_Validation = true;
				}
		}
		// 동기화 검사 (장벽 빠짐 · 경쟁 탐지): 레이어가 주는 VK_EXT_validation_features 로 켠다
		s_SyncValidation = false;
		char syncEnv[8] = {};
		if (s_Validation && ::GetEnvironmentVariableA("NOVA_VK_SYNC_VALIDATION", syncEnv, sizeof(syncEnv)) && syncEnv[0] == '1')
		{
			uint32_t ln = 0;
			vkEnumerateInstanceExtensionProperties("VK_LAYER_KHRONOS_validation", &ln, nullptr);
			std::vector<VkExtensionProperties> lexts(ln);
			vkEnumerateInstanceExtensionProperties("VK_LAYER_KHRONOS_validation", &ln, lexts.data());
			for (const auto& e : lexts)
				if (strcmp(e.extensionName, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME) == 0)
				{
					enable.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
					s_SyncValidation = true;
				}
		}

		VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
		app.pApplicationName = "NOVA";
		app.pEngineName = "NOVA";
		app.apiVersion = VK_API_VERSION_1_3;
		VkInstanceCreateInfo ci = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
		ci.pApplicationInfo = &app;
		ci.enabledExtensionCount = (uint32_t)enable.size();
		ci.ppEnabledExtensionNames = enable.data();
		ci.enabledLayerCount = (uint32_t)layers.size();
		ci.ppEnabledLayerNames = layers.data();
		VkDebugUtilsMessengerCreateInfoEXT dm = { VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
		dm.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		dm.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT;
		dm.pfnUserCallback = DebugCallback;
		const VkValidationFeatureEnableEXT syncFeature = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
		VkValidationFeaturesEXT vf = { VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT };
		vf.enabledValidationFeatureCount = 1;
		vf.pEnabledValidationFeatures = &syncFeature;
		if (s_Validation)
			ci.pNext = &dm;   // 인스턴스 만들기 · 없애기 중의 메시지도
		if (s_SyncValidation)
		{
			dm.pNext = &vf;
			ci.enabledExtensionCount = (uint32_t)enable.size();
			ci.ppEnabledExtensionNames = enable.data();
		}
		VkResult r = vkCreateInstance(&ci, nullptr, &s_Instance);
		if (r != VK_SUCCESS && s_Validation)
		{
			// 레이어가 깨져 있으면 레이어 없이 다시
			ci.enabledLayerCount = 0;
			ci.pNext = nullptr;
			if (s_SyncValidation) { enable.pop_back(); ci.enabledExtensionCount = (uint32_t)enable.size(); }
			s_Validation = s_SyncValidation = false;
			r = vkCreateInstance(&ci, nullptr, &s_Instance);
		}
		if (r != VK_SUCCESS)
		{
			error = std::string("vkCreateInstance failed: ") + ResultName(r);
			s_Instance = VK_NULL_HANDLE;
			return false;
		}
#define NOVA_VK_LOAD(name) name = reinterpret_cast<PFN_##name>(s_GetInstanceProcAddr(s_Instance, #name));
		NOVA_VK_INSTANCE_FUNCTIONS(NOVA_VK_LOAD)
		NOVA_VK_OPTIONAL_INSTANCE_FUNCTIONS(NOVA_VK_LOAD)
		NOVA_VK_DEVICE_FUNCTIONS(NOVA_VK_LOAD)
#undef NOVA_VK_LOAD
		std::string missing;
#define NOVA_VK_CHECK(name) if (!name) missing += std::string(missing.empty() ? "" : ", ") + #name;
		NOVA_VK_INSTANCE_FUNCTIONS(NOVA_VK_CHECK)
		NOVA_VK_DEVICE_FUNCTIONS(NOVA_VK_CHECK)
#undef NOVA_VK_CHECK
		if (!missing.empty())
		{
			// 창 표시 함수만 없는 것은 괜찮다 (화면 없는 검사 장치)
			bool onlySurface = true;
			for (const char* s : { "vkCreateDevice", "vkQueueSubmit2", "vkCmdBeginRendering", "vkCmdPipelineBarrier2", "vkCreateImage", "vkCmdDraw" })
				if (missing.find(s) != std::string::npos) onlySurface = false;
			if (!onlySurface)
			{
				error = "Vulkan functions missing: " + missing;
				vkDestroyInstance(s_Instance, nullptr);
				s_Instance = VK_NULL_HANDLE;
				return false;
			}
			s_Surface = false;
		}
		dm.pNext = nullptr;
		if (s_Validation && vkCreateDebugUtilsMessengerEXT)
			vkCreateDebugUtilsMessengerEXT(s_Instance, &dm, nullptr, &s_Messenger);
		s_Refs = 1;
		EditorLog::Write("Vulkan", "instance %u.%u.%u, validation %s, surface %s", VK_API_VERSION_MAJOR(s_Version), VK_API_VERSION_MINOR(s_Version),
			VK_API_VERSION_PATCH(s_Version), s_Validation ? (s_SyncValidation ? "on + synchronization" : "on") : "off", s_Surface ? "yes" : "no");
		return true;
	}

	void Release()
	{
		std::lock_guard<std::mutex> lock(s_Lock);
		if (s_Refs <= 0 || --s_Refs > 0)
			return;
		if (s_Messenger && vkDestroyDebugUtilsMessengerEXT)
			vkDestroyDebugUtilsMessengerEXT(s_Instance, s_Messenger, nullptr);
		s_Messenger = VK_NULL_HANDLE;
		if (s_Instance)
			vkDestroyInstance(s_Instance, nullptr);
		s_Instance = VK_NULL_HANDLE;
		// DLL 은 열어 둔다 (다시 만들 때 · 드라이버 스레드가 남아 있을 수 있음)
	}

	VkInstance Instance() { return s_Instance; }
	uint32_t InstanceVersion() { return s_Version; }
	bool ValidationEnabled() { return s_Validation; }
	bool HasSurfaceExtensions() { return s_Surface; }
	int ValidationErrorCount() { return s_Errors; }
	int ValidationWarningCount() { return s_Warnings; }
	void ResetValidationCounts()
	{
		s_Errors = 0;
		s_Warnings = 0;
		std::lock_guard<std::mutex> lock(s_ReportLock);
		s_Reported.clear();
	}

	const char* ResultName(VkResult r)
	{
		switch (r)
		{
		case VK_SUCCESS: return "VK_SUCCESS";
		case VK_NOT_READY: return "VK_NOT_READY";
		case VK_TIMEOUT: return "VK_TIMEOUT";
		case VK_INCOMPLETE: return "VK_INCOMPLETE";
		case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
		case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
		case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
		case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
		case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
		case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
		case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
		case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
		case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
		case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
		case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
		case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
		default: return "VK_ERROR";
		}
	}
}
