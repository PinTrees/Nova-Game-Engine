#include "pch.h"
#include "D3D12Tools.h"
#include "ShaderCross.h"
#include "CliServer.h"
#include "GfxD3D12.h"
#include "GfxTest.h"
#include "RhiTest.h"

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

	// 두 RGBA8 이미지 비교 (VulkanTools 와 같은 기준): 성분 차이 최대 · 평균, 2 · 8 을 넘는 화소 수 (+ 차이 x4 PNG)
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

namespace D3D12Tools
{
	void RegisterEditor()
	{
		CliServer::Register("d3d12", "DirectX 12 backend tools: {op: shaders|gfx-test|rhi-test} (nova d3d12 help)", [](const nlohmann::json& args, nlohmann::json& result, std::string& error) {
			const std::string op = args.value("op", std::string("help"));
			if (op == "help")
			{
				result = { { "ops", { "shaders [--path file.fx]: compile every .fx (or one) to DXIL, report each pass",
					"gfx-test [--out folder] [--width] [--height]: render the Gfx test scene on a headless DirectX 12 device and on the DirectX 11 engine device, compare pixels",
					"rhi-test [--out folder] [--width] [--height]: same with the RHI test scene (Rhi::Device API)" } } };
				return true;
			}
			const int w = std::clamp(args.value("width", 960), 16, 4096), h = std::clamp(args.value("height", 540), 16, 4096);
			const std::string outDir = args.value("out", std::string());
			auto file = [&](const std::string& name) { return outDir.empty() ? std::wstring() : (std::filesystem::path(string_to_wstring(outDir)) / name).wstring(); };
			if (op == "rhi-test")
			{
				RhiTest::Result res[2];
				bool ok[2] = {};
				nlohmann::json list = nlohmann::json::array();
				const GraphicsAPI apis[2] = { GraphicsAPI::DirectX11, GraphicsAPI::DirectX12 };
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
					list.push_back(item);
				}
				result = { { "width", w }, { "height", h }, { "results", list } };
				if (ok[0] && ok[1])
					result["diff"] = Compare(res[0].Rgba, res[1].Rgba, w, h, file("rhi_diff_DirectX12.png"));
				return true;
			}
			if (op == "gfx-test")
			{
				GfxTest::Result d12, dx;
				std::string d12Error, dxError;
				bool d12Ok = false, dxOk = false;
				std::string device;
				{
					// 화면 없는 DirectX 12 장치 (끝나면 효과 · 자원 → RHI → 컨텍스트 → 장치 순으로 놓는다)
					ComPtr<GfxDevice> gdev;
					ComPtr<GfxContext> gctx;
					if (GfxD3D12::CreateDevice(nullptr, gdev.GetAddressOf(), gctx.GetAddressOf(), d12Error))
					{
						device = GfxD3D12::Description(gdev.Get());
						std::unique_ptr<Rhi::Device> rhi = GfxD3D12::CreateRhiDevice(gdev.Get(), gctx.Get(), d12Error);
						if (rhi)
							d12Ok = GfxTest::Render(gdev.Get(), gctx.Get(), rhi.get(), w, h, d12, d12Error);
						GfxD3D12::WaitIdle(gdev.Get());
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
				nlohmann::json d12Item = { { "api", "DirectX12" }, { "device", device }, { "ok", d12Ok } };
				nlohmann::json dxItem = { { "api", "DirectX11" }, { "ok", dxOk } };
				std::string err;
				if (d12Ok)
				{
					d12Item["loadMs"] = d12.LoadMs;
					d12Item["drawMs"] = d12.DrawMs;
					if (!outDir.empty() && SavePng(d12.Rgba, w, h, file("gfx_DirectX12.png"), err)) d12Item["png"] = wstring_to_string(file("gfx_DirectX12.png"));
				}
				else d12Item["error"] = d12Error;
				if (dxOk)
				{
					dxItem["loadMs"] = dx.LoadMs;
					dxItem["drawMs"] = dx.DrawMs;
					if (!outDir.empty() && SavePng(dx.Rgba, w, h, file("gfx_DirectX11.png"), err)) dxItem["png"] = wstring_to_string(file("gfx_DirectX11.png"));
				}
				else dxItem["error"] = dxError;
				result = { { "width", w }, { "height", h }, { "results", { dxItem, d12Item } } };
				if (d12Ok && dxOk)
					result["diff"] = Compare(dx.Rgba, d12.Rgba, w, h, file("gfx_diff_DirectX12.png"));
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
				nlohmann::json list = nlohmann::json::array();
				int ok = 0, passes = 0, failed = 0;
				for (const auto& f : files)
				{
					EditorLog::Heartbeat();
					ShaderCross::EffectDxil e;
					const bool good = ShaderCross::CompileEffectDxil(f.wstring(), e) && e.Error.empty();
					nlohmann::json errors = nlohmann::json::array();
					if (!good) errors.push_back(e.Error);
					int unresolved = 0;
					for (const auto& p : e.Passes)
					{
						++passes;
						if (!p.Error.empty())
						{
							++failed;
							errors.push_back(p.Technique + "/" + p.Pass + ": " + p.Error);
						}
						for (const auto& s : p.Stages)
							for (const auto& b : s.Bindings)
								if (b.Binding < 0) ++unresolved;
					}
					if (good) ++ok;
					list.push_back({ { "file", wstring_to_string(f.filename().wstring()) }, { "passes", e.Passes.size() }, { "passesOk", e.PassesOk() },
						{ "unresolvedBindings", unresolved }, { "errors", errors } });
				}
				result = { { "effects", files.size() }, { "effectsOk", ok }, { "passes", passes }, { "passesFailed", failed }, { "files", list } };
				return true;
			}
			error = "unknown op '" + op + "' (nova d3d12 help)";
			return false;
		});
	}
}
