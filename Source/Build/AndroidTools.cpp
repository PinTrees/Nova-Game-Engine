#include "pch.h"
#include "AndroidTools.h"
#include "ShaderCross.h"
#include "ShaderCrossJson.h"
#include "CliServer.h"
#include "PathManager.h"
#include "BuildPipeline.h"
#include "BuildSettings.h"
#include "UISystem.h"
#include "App.h"
#include <fstream>

namespace AndroidTools
{
	namespace
	{
		namespace fs = std::filesystem;

		// JSON 문자열 안의 '\' 경로 구분자 → '/' (안드로이드 파일 시스템). Windows 엔진도 '/' 를 그대로 읽는다
		void Slashes(nlohmann::json& j)
		{
			if (j.is_string())
			{
				std::string s = j.get<std::string>();
				if (s.find('\\') != std::string::npos && s.find('.') != std::string::npos)
				{
					std::replace(s.begin(), s.end(), '\\', '/');
					j = s;
				}
			}
			else if (j.is_object() || j.is_array())
				for (auto& v : j)
					Slashes(v);
		}

		bool LooksLikeJson(const fs::path& p)
		{
			std::ifstream in(p, std::ios::binary);
			char c = 0;
			while (in.get(c))
				if (!isspace((unsigned char)c) && (unsigned char)c != 0xEF && (unsigned char)c != 0xBB && (unsigned char)c != 0xBF)
					return c == '{' || c == '[';
			return false;
		}

		// 게임 데이터 (APK 의 assets/game): 플레이어 빌드와 같은 에셋 모음 + 경로를 '/' 로 + player.json + files.txt (기기가 풀어 놓을 목록)
		bool Export(const nlohmann::json& args, nlohmann::json& result, std::string& error)
		{
			const std::string outArg = args.value("out", std::string());
			if (outArg.empty()) { error = "--out folder is required"; return false; }
			std::vector<std::string> scenes;
			std::string list = args.value("scenes", std::string());
			for (size_t pos = 0; !list.empty() && pos <= list.size();)
			{
				const size_t next = list.find(',', pos);
				std::string s = list.substr(pos, next == std::string::npos ? std::string::npos : next - pos);
				if (!s.empty()) scenes.push_back(s);
				if (next == std::string::npos) break;
				pos = next + 1;
			}
			if (scenes.empty()) scenes = BuildSettings::EnabledScenes();
			if (scenes.empty()) { error = "no scenes (--scenes or Build Settings)"; return false; }
			std::error_code ec;
			for (std::string& s : scenes)
			{
				std::replace(s.begin(), s.end(), '/', '\\');
				if (!fs::exists(PathManager::GetI()->GetMovePathW(string_to_wstring(s)), ec)) { error = "scene not found: " + s; return false; }
			}
			const fs::path game = fs::path(string_to_wstring(outArg)) / L"game";
			fs::remove_all(game, ec);
			fs::create_directories(game, ec);
			std::vector<std::string> files;
			uint64_t bytes = 0;
			int converted = 0;
			for (const auto& [rel, full] : BuildPipeline::CollectGameFiles(scenes))
			{
				std::string r = wstring_to_string(rel);
				std::replace(r.begin(), r.end(), '\\', '/');
				const fs::path dst = game / string_to_wstring(r);
				fs::create_directories(dst.parent_path(), ec);
				const fs::path src(full);
				if (fs::file_size(src, ec) < (64ull << 20) && LooksLikeJson(src))
				{
					std::ifstream in(src);
					nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
					if (!j.is_discarded())
					{
						Slashes(j);
						std::ofstream(dst, std::ios::binary | std::ios::trunc) << j.dump(1);
						++converted;
					}
					else fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
				}
				else fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
				bytes += fs::file_size(dst, ec);
				files.push_back(r);
			}
			nlohmann::json player = { { "productName", BuildSettings::ProductName() }, { "graphicsAPIs", { "OpenGL" } } };
			nlohmann::json sceneList = nlohmann::json::array();
			for (std::string s : scenes)
			{
				std::replace(s.begin(), s.end(), '\\', '/');
				sceneList.push_back(s);
			}
			player["scenes"] = sceneList;
			std::ofstream(game / L"player.json", std::ios::trunc) << player.dump(4);
			files.push_back("player.json");
			std::ofstream manifest(game / L"files.txt", std::ios::binary | std::ios::trunc);
			for (const std::string& f : files) manifest << f << "\n";
			result = { { "files", files.size() }, { "jsonConverted", converted }, { "bytes", bytes }, { "scenes", sceneList },
				{ "out", wstring_to_string(fs::absolute(game, ec).wstring()) } };
			return true;
		}

		// 비교 기준 그림: 열린 씬의 게임 카메라를 플레이어와 같은 순서 (PlayerRuntime::Render) 로 W x H 텍스처에 frames 번 그려 PNG 로.
		//  안드로이드의 엔진 장면 검사 (-e test scene) 가 같은 크기 · 같은 프레임 수로 그린 그림과 비교한다
		bool Reference(const nlohmann::json& args, nlohmann::json& result, std::string& error)
		{
			const int w = std::clamp(args.value("width", 960), 16, 4096), h = std::clamp(args.value("height", 540), 16, 4096);
			const int frames = std::clamp(args.value("frames", 10), 1, 600);
			const std::string outArg = args.value("out", std::string());
			if (outArg.empty()) { error = "--out file.png is required"; return false; }
			std::shared_ptr<Camera> camera = DisplayManager::GetI()->GetCameraForDisplay(0);
			if (!camera) { error = "the open scene has no game camera"; return false; }
			GfxDevice* dev = Application::GetI()->GetDevice();
			GfxContext* ctx = Application::GetI()->GetDeviceContext();
			D3D11_TEXTURE2D_DESC td = {};
			td.Width = (UINT)w;
			td.Height = (UINT)h;
			td.MipLevels = 1;
			td.ArraySize = 1;
			td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			td.SampleDesc.Count = 1;
			td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
			ComPtr<GfxTexture2D> color, depthTex;
			ComPtr<GfxRenderTargetView> rtv;
			ComPtr<GfxDepthStencilView> dsv;
			if (FAILED(dev->CreateTexture2D(&td, nullptr, color.GetAddressOf())) || FAILED(dev->CreateRenderTargetView(color.Get(), nullptr, rtv.GetAddressOf())))
			{
				error = "render target failed";
				return false;
			}
			td.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
			td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
			dev->CreateTexture2D(&td, nullptr, depthTex.GetAddressOf());
			dev->CreateDepthStencilView(depthTex.Get(), nullptr, dsv.GetAddressOf());
			GfxRenderTargetView* rtvs[1] = { rtv.Get() };
			RenderManager::GetI()->SetViewport(w, h);
			PostProcessingManager::GetI()->SetSSAO(w, h, camera.get());
			for (int i = 0; i < frames; ++i)
			{
				ctx->OMSetRenderTargets(1, rtvs, dsv.Get());
				camera->SetAspect((float)w / (float)h);
				camera->LateUpdate();
				RenderManager::GetI()->CameraViewProjectionMatrix = camera->View() * camera->Proj();
				Application::GetI()->GetApp()->OnSceneRender(rtv.Get(), camera.get());
				UISystem::RenderGameView(rtv.Get(), (UINT)w, (UINT)h, 0, camera.get(), Application::GetI()->GetApp()->SceneDepth((UINT)w, (UINT)h));
			}
			DirectX::ScratchImage captured;
			if (FAILED(Gfx::CaptureTexture(ctx, color.Get(), captured))) { error = "capture failed"; return false; }
			const DirectX::Image* img = captured.GetImage(0, 0, 0);
			for (size_t y = 0; y < img->height; ++y)
				for (size_t x = 0; x < img->width; ++x)
					img->pixels[y * img->rowPitch + x * 4 + 3] = 255;
			const fs::path out(string_to_wstring(outArg));
			std::error_code ec;
			fs::create_directories(out.parent_path(), ec);
			if (FAILED(DirectX::SaveToWICFile(*img, DirectX::WIC_FLAGS_NONE, DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), out.c_str()))) { error = "png save failed"; return false; }
			result = { { "path", outArg }, { "width", w }, { "height", h }, { "frames", frames } };
			return true;
		}
	}

	void RegisterEditor()
	{
		// nova android shaders --out Android/build/assets/Shaders [--path one.fx]
		CliServer::Register("android", "Android build tools: {op: shaders, out, path?} (nova android help)", [](const nlohmann::json& args, nlohmann::json& result, std::string& error) {
			const std::string op = args.value("op", std::string("help"));
			if (op == "help")
			{
				result = { { "ops", { "shaders --out folder [--path file.fx]: convert every .fx to OpenGL ES 3.20 (<name>.json for the APK assets/Shaders)",
					"export --out folder [--scenes a.scene,b.scene]: game data for the APK (folder/game: scenes + referenced assets, '/' paths, player.json, files.txt)", "reference --out file.png [--width 960 --height 540 --frames 10]: render the open scene's game camera like the player (DX11 reference for the Android scene test)" } } };
				return true;
			}
			if (op == "export")
				return Export(args, result, error);
			if (op == "reference")
				return Reference(args, result, error);
			if (op == "shaders")
			{
				const std::string outArg = args.value("out", std::string());
				if (outArg.empty()) { error = "--out folder is required"; return false; }
				const std::filesystem::path out = string_to_wstring(outArg);
				std::error_code ec;
				std::filesystem::create_directories(out, ec);
				std::vector<std::filesystem::path> files;
				const std::string one = args.value("path", std::string());
				if (!one.empty())
					files.push_back(std::filesystem::absolute(string_to_wstring(one), ec));
				else
					for (const auto& f : std::filesystem::directory_iterator(std::filesystem::path(PathManager::GetI()->GetEnginePathW()) / L"Shaders", ec))
						if (f.path().extension() == L".fx")
							files.push_back(f.path());
				std::sort(files.begin(), files.end());
				int passes = 0, failed = 0, written = 0;
				nlohmann::json errors = nlohmann::json::array();
				for (const auto& f : files)
				{
					EditorLog::Heartbeat();
					ShaderCross::EffectGlsl e;
					if (!ShaderCross::CompileEffectGles(f.wstring(), e) || !e.Error.empty())
					{
						errors.push_back(wstring_to_string(f.filename().wstring()) + ": " + e.Error.substr(0, 300));
						continue;
					}
					for (const auto& p : e.Passes)
					{
						++passes;
						if (!p.Error.empty())
						{
							++failed;
							if (errors.size() < 40) errors.push_back(wstring_to_string(f.filename().wstring()) + " " + p.Technique + "/" + p.Pass + ": " + p.Error.substr(0, 300));
						}
					}
					std::ofstream(out / (f.stem().wstring() + L".json"), std::ios::binary | std::ios::trunc) << ShaderCross::Json::ToJson(e, kGlesShaderVersion).dump();
					++written;
				}
				result = { { "effects", files.size() }, { "written", written }, { "passes", passes }, { "passesFailed", failed }, { "out", wstring_to_string(std::filesystem::absolute(out, ec).wstring()) },
					{ "errors", errors } };
				return true;
			}
			error = "unknown op '" + op + "' (nova android help)";
			return false;
		});
	}
}
