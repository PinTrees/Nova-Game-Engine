#include "pch.h"
#include "VulkanTools.h"
#include "ShaderCross.h"
#include "CliServer.h"
#include "GfxVk.h"
#include "GfxTest.h"
#include "RhiTest.h"
#include "VkLoader.h"
#include <fstream>

namespace
{
	bool SavePng(const std::vector<uint8_t>& rgba, int w, int h, const std::wstring& file, std::string& error)
	{
		DirectX::Image img = {};
		img.width = w;
		img.height = h;
		img.format = DXGI_FORMAT_R8G8B8A8_UNORM;
		img.rowPitch = (size_t)w * 4;
		img.slicePitch = img.rowPitch * h;
		img.pixels = const_cast<uint8_t*>(rgba.data());
		std::error_code ec;
		std::filesystem::create_directories(std::filesystem::path(file).parent_path(), ec);
		if (FAILED(DirectX::SaveToWICFile(img, DirectX::WIC_FLAGS_NONE, DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), file.c_str())))
		{
			error = "PNG save failed";
			return false;
		}
		return true;
	}

	// 두 RGBA8 이미지 비교: 성분 차이 최대 · 평균, 차이가 2 · 8 을 넘는 화소 수 (+ out 이 있으면 차이 x4 PNG)
	nlohmann::json Compare(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, int w, int h, const std::wstring& diffFile)
	{
		int maxDiff = 0;
		double sum = 0;
		int over2 = 0, over8 = 0;
		std::vector<uint8_t> diff(a.size(), 255);
		for (size_t i = 0; i + 3 < a.size() && i + 3 < b.size(); i += 4)
		{
			int px = 0;
			for (int c = 0; c < 3; ++c)
				px = (std::max)(px, std::abs((int)a[i + c] - (int)b[i + c]));
			maxDiff = (std::max)(maxDiff, px);
			sum += px;
			if (px > 2) ++over2;
			if (px > 8) ++over8;
			diff[i] = diff[i + 1] = diff[i + 2] = (uint8_t)(std::min)(255, px * 4);
		}
		nlohmann::json r = { { "max", maxDiff }, { "mean", sum / (std::max)(1, w * h) }, { "pixelsOver2", over2 }, { "pixelsOver8", over8 } };
		std::string err;
		if (!diffFile.empty() && SavePng(diff, w, h, diffFile, err))
			r["png"] = wstring_to_string(diffFile);
		return r;
	}
}

namespace VulkanTools
{
	void RegisterEditor()
	{
		// nova vulkan shaders [--path Shaders/32. InstancedBasic.fx]
		CliServer::Register("vulkan", "Vulkan backend tools: {op: shaders, path?} (nova vulkan help)", [](const nlohmann::json& args, nlohmann::json& result, std::string& error) {
			const std::string op = args.value("op", std::string("help"));
			if (op == "help")
			{
				result = { { "ops", { "shaders [--path file.fx]: compile every .fx (or one) to Vulkan SPIR-V, dump each stage to ShaderCache/SPIRV/dump/*.spv (check with spirv-val)",
					"gfx-test [--out folder] [--width] [--height]: render the Gfx test scene on a headless Vulkan device and on the DirectX 11 engine device, compare pixels",
					"rhi-test [--out folder] [--width] [--height]: same with the RHI test scene (Rhi::Device API)" } } };
				return true;
			}
			if (op == "rhi-test")
			{
				const int w = std::clamp(args.value("width", 960), 16, 4096), h = std::clamp(args.value("height", 540), 16, 4096);
				const std::string outDir = args.value("out", std::string());
				auto file = [&](const std::string& name) { return outDir.empty() ? std::wstring() : (std::filesystem::path(string_to_wstring(outDir)) / name).wstring(); };
				VkLoader::ResetValidationCounts();
				RhiTest::Result res[2];
				bool ok[2] = {};
				nlohmann::json list = nlohmann::json::array();
				const GraphicsAPI apis[2] = { GraphicsAPI::DirectX11, GraphicsAPI::Vulkan };
				for (int i = 0; i < 2; ++i)
				{
					EditorLog::Heartbeat();
					nlohmann::json item = { { "api", GraphicsAPIToKey(apis[i]) } };
					std::string err;
					{
						std::unique_ptr<Rhi::Device> dev = Rhi::CreateDevice(apis[i], err);
						ok[i] = dev && RhiTest::RenderLitScene(*dev, w, h, res[i], err);
						if (dev) dev->Finish();
					}
					item["ok"] = ok[i];
					if (ok[i])
					{
						item["device"] = res[i].Device;
						item["loadMs"] = res[i].LoadMs;
						item["drawMs"] = res[i].DrawMs;
						std::string e2;
						const std::wstring png = file(std::string("rhi_") + GraphicsAPIToKey(apis[i]) + ".png");
						if (!png.empty() && SavePng(res[i].Rgba, w, h, png, e2)) item["png"] = wstring_to_string(png);
					}
					else item["error"] = err;
					if (apis[i] == GraphicsAPI::Vulkan)
					{
						item["validationErrors"] = VkLoader::ValidationErrorCount();
						item["validationWarnings"] = VkLoader::ValidationWarningCount();
					}
					list.push_back(item);
				}
				result = { { "width", w }, { "height", h }, { "results", list } };
				if (ok[0] && ok[1])
					result["diff"] = Compare(res[0].Rgba, res[1].Rgba, w, h, file("rhi_diff_Vulkan.png"));
				return true;
			}
			if (op == "gfx-test")
			{
				const int w = std::clamp(args.value("width", 960), 16, 4096), h = std::clamp(args.value("height", 540), 16, 4096);
				const std::string outDir = args.value("out", std::string());
				auto file = [&](const char* name) { return outDir.empty() ? std::wstring() : (std::filesystem::path(string_to_wstring(outDir)) / name).wstring(); };
				GfxTest::Result vk, dx;
				std::string vkError, dxError;
				bool vkOk = false, dxOk = false;
				std::string device;
				VkLoader::ResetValidationCounts();
				{
					// 화면 없는 Vulkan 장치 (끝나면 효과 · 자원 → RHI → 컨텍스트 → 장치 순으로 놓는다)
					ComPtr<GfxDevice> gdev;
					ComPtr<GfxContext> gctx;
					if (GfxVk::CreateDevice(nullptr, gdev.GetAddressOf(), gctx.GetAddressOf(), vkError))
					{
						device = GfxVk::Description(gdev.Get());
						std::unique_ptr<Rhi::Device> rhi = GfxVk::CreateRhiDevice(gdev.Get(), gctx.Get(), vkError);
						if (rhi)
							vkOk = GfxTest::Render(gdev.Get(), gctx.Get(), rhi.get(), w, h, vk, vkError);
						GfxVk::WaitIdle(gdev.Get());
						rhi.reset();
					}
					gctx.Reset();
					gdev.Reset();
				}
				EditorLog::Heartbeat();
				if (Gfx::Device() && Gfx::Device()->Native())
					dxOk = GfxTest::Render(Gfx::Device(), Gfx::Context(), Rhi::Main(), w, h, dx, dxError);
				else
					dxError = "the engine device is not DirectX 11";
				nlohmann::json vkItem = { { "api", "Vulkan" }, { "device", device }, { "ok", vkOk }, { "validationErrors", VkLoader::ValidationErrorCount() },
					{ "validationWarnings", VkLoader::ValidationWarningCount() }, { "validation", VkLoader::ValidationEnabled() } };
				nlohmann::json dxItem = { { "api", "DirectX11" }, { "ok", dxOk } };
				std::string err;
				if (vkOk)
				{
					vkItem["loadMs"] = vk.LoadMs;
					vkItem["drawMs"] = vk.DrawMs;
					if (!outDir.empty() && SavePng(vk.Rgba, w, h, file("gfx_Vulkan.png"), err)) vkItem["png"] = wstring_to_string(file("gfx_Vulkan.png"));
				}
				else vkItem["error"] = vkError;
				if (dxOk)
				{
					dxItem["loadMs"] = dx.LoadMs;
					dxItem["drawMs"] = dx.DrawMs;
					if (!outDir.empty() && SavePng(dx.Rgba, w, h, file("gfx_DirectX11.png"), err)) dxItem["png"] = wstring_to_string(file("gfx_DirectX11.png"));
				}
				else dxItem["error"] = dxError;
				result = { { "width", w }, { "height", h }, { "results", { dxItem, vkItem } } };
				if (vkOk && dxOk)
					result["diff"] = Compare(dx.Rgba, vk.Rgba, w, h, file("gfx_diff_Vulkan.png"));
				return true;
			}
			if (op == "shaders")
			{
				std::vector<std::filesystem::path> files;
				const std::string one = args.value("path", std::string());
				std::error_code ec;
				if (!one.empty())
					files.push_back(std::filesystem::absolute(string_to_wstring(one), ec));
				else
					for (const auto& f : std::filesystem::directory_iterator(L"../Shaders", ec))
						if (f.path().extension() == L".fx")
							files.push_back(f.path());
				const std::filesystem::path dump = std::filesystem::path(L"ShaderCache") / L"SPIRV" / L"dump";
				std::filesystem::create_directories(dump, ec);
				nlohmann::json list = nlohmann::json::array();
				int ok = 0, passes = 0, failed = 0, modules = 0;
				for (const auto& f : files)
				{
					ShaderCross::EffectSpirv e;
					const bool good = ShaderCross::CompileEffectSpirv(f.wstring(), e) && e.Error.empty();
					const std::string fname = wstring_to_string(f.filename().wstring());
					nlohmann::json errors = nlohmann::json::array();
					if (!good)
						errors.push_back(e.Error);
					int index = 0;
					for (const auto& p : e.Passes)
					{
						++passes;
						if (!p.Error.empty())
						{
							++failed;
							errors.push_back(p.Technique + "/" + p.Pass + ": " + p.Error);
						}
						for (const auto& s : p.Stages)
						{
							const std::wstring out = f.stem().wstring() + L"_" + std::to_wstring(index) + L"_" + string_to_wstring(FxParser::StageName(s.StageType)) + L".spv";
							std::ofstream(dump / out, std::ios::binary | std::ios::trunc).write(reinterpret_cast<const char*>(s.Code.data()), s.Code.size() * 4);
							++modules;
						}
						++index;
					}
					if (good) ++ok;
					list.push_back({ { "file", fname }, { "passes", e.Passes.size() }, { "passesOk", e.PassesOk() }, { "bindings", e.BindingCount }, { "errors", errors } });
				}
				result = { { "effects", files.size() }, { "effectsOk", ok }, { "passes", passes }, { "passesFailed", failed }, { "modules", modules },
					{ "dump", wstring_to_string(std::filesystem::absolute(dump, ec).wstring()) }, { "files", list } };
				return true;
			}
			error = "unknown op '" + op + "' (nova vulkan help)";
			return false;
		});
	}
}
