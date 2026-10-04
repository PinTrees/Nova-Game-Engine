#include "pch.h"
#include "AndroidTools.h"
#include "ShaderCross.h"
#include "ShaderCrossJson.h"
#include "CliServer.h"
#include "PathManager.h"
#include "BuildPipeline.h"
#include "BuildSettings.h"
#include "TextureCompressor.h"
#include "AssetImportSettings.h"
#include "UISystem.h"
#include "App.h"
#include "ResourceManager.h"
#include "SkinnedMesh.h"
#include "AndroidBuild.h"
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

		// .NET 어셈블리가 참조하는 어셈블리 이름 (메타데이터의 AssemblyRef 표, ECMA-335 II.22) — 기기에 BCL 중 쓰는 것만 넣으려고.
		//  PE → CLI 머리 → 메타데이터 루트 → #~ 스트림의 표 크기를 차례로 더해 AssemblyRef (0x23) 행의 Name 을 #Strings 에서 읽는다
		std::vector<std::string> AssemblyReferences(const fs::path& dll)
		{
			std::vector<std::string> out;
			std::ifstream in(dll, std::ios::binary);
			std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			auto u16 = [&](size_t o) -> uint32_t { return o + 2 <= d.size() ? (uint32_t)d[o] | (uint32_t)d[o + 1] << 8 : 0; };
			auto u32 = [&](size_t o) -> uint32_t { return o + 4 <= d.size() ? u16(o) | u16(o + 2) << 16 : 0; };
			if (d.size() < 0x40 || u16(0) != 0x5A4D) return out;
			const size_t pe = u32(0x3C);
			if (u32(pe) != 0x00004550) return out;
			const uint32_t sections = u16(pe + 6), optSize = u16(pe + 20);
			const size_t opt = pe + 24;
			const size_t dirs = opt + (u16(opt) == 0x20B ? 112 : 96);
			const uint32_t cliRva = u32(dirs + 14 * 8);
			auto offset = [&](uint32_t rva) -> size_t {
				for (uint32_t s = 0; s < sections; ++s)
				{
					const size_t sh = opt + optSize + (size_t)s * 40;
					const uint32_t va = u32(sh + 12), size = (std::max)(u32(sh + 8), u32(sh + 16)), raw = u32(sh + 20);
					if (rva >= va && rva < va + size) return (size_t)raw + (rva - va);
				}
				return 0;
			};
			const size_t cli = offset(cliRva);
			if (!cli) return out;
			const size_t meta = offset(u32(cli + 8));
			if (!meta || u32(meta) != 0x424A5342) return out;
			size_t p = meta + 16 + u32(meta + 12);
			const uint32_t streams = u16(p + 2);
			p += 4;
			size_t tables = 0, strings = 0;
			for (uint32_t s = 0; s < streams; ++s)
			{
				const uint32_t off = u32(p);
				std::string name;
				size_t q = p + 8;
				while (q < d.size() && d[q]) name += (char)d[q++];
				if (name == "#~" || name == "#-") tables = meta + off;
				if (name == "#Strings") strings = meta + off;
				p = p + 8 + ((name.size() + 4) & ~(size_t)3);
			}
			if (!tables || !strings) return out;
			const uint8_t heaps = d[tables + 6];
			const uint64_t valid = (uint64_t)u32(tables + 8) | (uint64_t)u32(tables + 12) << 32;
			uint32_t rows[64] = {};
			size_t q = tables + 24;
			for (int t = 0; t < 64; ++t)
				if (valid >> t & 1) { rows[t] = u32(q); q += 4; }
			const uint32_t str = heaps & 1 ? 4 : 2, guid = heaps & 2 ? 4 : 2, blob = heaps & 4 ? 4 : 2;
			auto idx = [&](int t) { return rows[t] < 65536 ? 2u : 4u; };
			auto coded = [&](std::initializer_list<int> ts, int bits) { uint32_t mx = 0; for (int t : ts) if (t >= 0) mx = (std::max)(mx, rows[t]); return mx < (1u << (16 - bits)) ? 2u : 4u; };
			const uint32_t typeDefOrRef = coded({ 0x02, 0x01, 0x1B }, 2), resolutionScope = coded({ 0x00, 0x1A, 0x23, 0x01 }, 2);
			const uint32_t memberRefParent = coded({ 0x02, 0x01, 0x1A, 0x06, 0x1B }, 3), hasConstant = coded({ 0x04, 0x08, 0x17 }, 2);
			const uint32_t hasCustomAttribute = coded({ 0x06, 0x04, 0x01, 0x02, 0x08, 0x09, 0x0A, 0x00, 0x0E, 0x17, 0x14, 0x11, 0x1A, 0x1B, 0x20, 0x23, 0x26, 0x27, 0x28, 0x2A, 0x2C, 0x2B }, 5);
			const uint32_t customAttributeType = coded({ -1, -1, 0x06, 0x0A, -1 }, 3), hasFieldMarshal = coded({ 0x04, 0x08 }, 1);
			const uint32_t hasDeclSecurity = coded({ 0x02, 0x06, 0x20 }, 2), hasSemantics = coded({ 0x14, 0x17 }, 1), methodDefOrRef = coded({ 0x06, 0x0A }, 1);
			const uint32_t memberForwarded = coded({ 0x04, 0x06 }, 1);
			const uint32_t size[0x23] = {
				2 + str + guid * 3,                                   // Module
				resolutionScope + str * 2,                            // TypeRef
				4 + str * 2 + typeDefOrRef + idx(0x04) + idx(0x06),   // TypeDef
				idx(0x04), 2 + str + blob, idx(0x06),                 // FieldPtr, Field, MethodPtr
				4 + 2 + 2 + str + blob + idx(0x08), idx(0x08),        // MethodDef, ParamPtr
				2 + 2 + str,                                          // Param
				idx(0x02) + typeDefOrRef,                             // InterfaceImpl
				memberRefParent + str + blob,                         // MemberRef
				2 + hasConstant + blob,                               // Constant
				hasCustomAttribute + customAttributeType + blob,      // CustomAttribute
				hasFieldMarshal + blob,                               // FieldMarshal
				2 + hasDeclSecurity + blob,                           // DeclSecurity
				2 + 4 + idx(0x02), 4 + idx(0x04), blob,               // ClassLayout, FieldLayout, StandAloneSig
				idx(0x02) + idx(0x14), idx(0x14), 2 + str + typeDefOrRef,   // EventMap, EventPtr, Event
				idx(0x02) + idx(0x17), idx(0x17), 2 + str + blob,     // PropertyMap, PropertyPtr, Property
				2 + idx(0x06) + hasSemantics,                         // MethodSemantics
				idx(0x02) + methodDefOrRef * 2,                       // MethodImpl
				str, blob,                                            // ModuleRef, TypeSpec
				2 + memberForwarded + str + idx(0x1A),                // ImplMap
				4 + idx(0x04), 8, 4,                                  // FieldRVA, EncLog, EncMap
				4 + 8 + 4 + blob + str * 2, 4, 12 };                  // Assembly, AssemblyProcessor, AssemblyOS
			for (int t = 0; t < 0x23; ++t)
				q += (size_t)rows[t] * size[t];
			const uint32_t refRow = 8 + 4 + blob + str * 2 + blob;
			for (uint32_t r = 0; r < rows[0x23]; ++r)
			{
				const size_t row = q + (size_t)r * refRow;
				const uint32_t nameIndex = str == 4 ? u32(row + 12 + blob) : u16(row + 12 + blob);
				std::string name;
				for (size_t c = strings + nameIndex; c < d.size() && d[c]; ++c) name += (char)d[c];
				if (!name.empty()) out.push_back(name);
			}
			return out;
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
			// 텍스처 압축: --texture-compression (astc · etc2 · dxt · none) 이 없으면 Player Settings 의 Android Texture Compression
			TextureCompressor::AndroidDefault texDefault = (TextureCompressor::AndroidDefault)std::clamp(BuildSettings::GetPlayer().AndroidTextureCompression, 0, 3);
			{
				std::string tc = args.value("texture-compression", std::string());
				for (char& ch : tc) ch = (char)tolower((unsigned char)ch);
				if (tc == "astc") texDefault = TextureCompressor::AndroidDefault::ASTC;
				else if (tc == "etc2") texDefault = TextureCompressor::AndroidDefault::ETC2;
				else if (tc == "dxt" || tc == "bc") texDefault = TextureCompressor::AndroidDefault::DXT;
				else if (tc == "none") texDefault = TextureCompressor::AndroidDefault::None;
				else if (!tc.empty()) { error = "--texture-compression must be astc, etc2, dxt or none"; return false; }
			}
			static const std::set<std::string> kBake = { ".png", ".jpg", ".jpeg", ".bmp", ".tga", ".tif", ".tiff", ".gif" };
			nlohmann::json textures = nlohmann::json::array();
			static const std::set<std::string> kModels = { ".fbx", ".gltf", ".glb", ".vrm" };   // MeshFile 이 읽는 형식
			nlohmann::json models = nlohmann::json::array();
			const auto t0 = std::chrono::steady_clock::now();
			for (const auto& [rel, full] : BuildPipeline::CollectGameFiles(scenes))
			{
				std::string r = wstring_to_string(rel);
				std::replace(r.begin(), r.end(), '\\', '/');
				const fs::path dst = game / string_to_wstring(r);
				fs::create_directories(dst.parent_path(), ec);
				const fs::path src(full);
				std::string ext = wstring_to_string(src.extension().wstring());
				for (char& ch : ext) ch = (char)tolower((unsigned char)ch);
				if (kBake.count(ext))
				{
					// 기기에는 그림 디코더 · 압축기가 없다 → 가져오기 설정대로 구운 DDS (<이름>.png.dds) 만 넣는다
					const AssetImport::TextureSettings ts = AssetImport::AppliesTo(src.wstring()) ? AssetImport::LoadTexture(src.wstring()) : AssetImport::TextureSettings::Raw();
					TextureCompressor::Result tr;
					std::string terr;
					const fs::path baked = dst.wstring() + L".dds";
					if (TextureCompressor::BuildAndroid(src.wstring(), ts, texDefault, baked.wstring(), tr, terr))
					{
						textures.push_back({ { "path", r }, { "format", tr.Format }, { "size", std::to_string(tr.Width) + "x" + std::to_string(tr.Height) }, { "mips", tr.Mips },
							{ "bytes", tr.Bytes }, { "psnr", std::round(tr.Psnr * 10.0) / 10.0 }, { "srgb", tr.Srgb } });
						bytes += fs::file_size(baked, ec);
						files.push_back(r + ".dds");
						EditorLog::Heartbeat();
						continue;
					}
					textures.push_back({ { "path", r }, { "error", terr } });
					EditorLog::Write("Android", "texture %s: %s (copied as is)", r.c_str(), terr.c_str());
				}
				if (kModels.count(ext))
				{
					// 기기에는 Assimp 가 없다 → 가져오기 설정대로 만든 메시 캐시 (.mesh · .animations · .skeletons) 만 넣는다 (원본 모델은 빼서 용량도 줄임).
					//  캐시가 없거나 설정이 바뀌었으면 여기서 가져온다 (에디터가 씬을 열 때와 같은 길)
					auto model = ResourceManager::GetI()->LoadMeshFile(wstring_to_string(rel));
					bool ok = model != nullptr;
					uint64_t modelBytes = 0;
					for (const wchar_t* suffix : { L".mesh", L".animations", L".skeletons" })
					{
						const fs::path cache = src.wstring() + suffix;
						if (!fs::is_regular_file(cache, ec)) { ok = false; break; }
						fs::copy_file(cache, dst.wstring() + suffix, fs::copy_options::overwrite_existing, ec);
						modelBytes += fs::file_size(cache, ec);
						files.push_back(r + wstring_to_string(suffix));
					}
					if (ok)
					{
						models.push_back({ { "path", r }, { "meshes", model->Meshs.size() }, { "skinnedMeshes", model->SkinnedMeshs.size() },
							{ "clips", model->SkinnedData.AnimationClips.size() }, { "skeletons", model->Avatas.size() }, { "bytes", modelBytes }, { "sourceBytes", fs::file_size(src, ec) } });
						bytes += modelBytes;
						EditorLog::Heartbeat();
						continue;
					}
					models.push_back({ { "path", r }, { "error", "mesh cache missing (import failed)" } });
					EditorLog::Write("Android", "model %s: mesh cache missing (copied as is)", r.c_str());
				}
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
			// C# 스크립트: 프로젝트에 Assembly-CSharp.dll 이 있으면 Mono 런타임의 어셈블리 (CoreLib · BCL) + NovaScriptCore + Assembly-CSharp 를 Managed/ 에
			//  (기기의 ScriptEngineAndroid 가 읽는다 — 없으면 스크립트 없이 돈다)
			nlohmann::json csharp = { { "included", false } };
			{
				const fs::path game_cs = PathManager::GetI()->GetMovePathW(L"Library\\ScriptAssemblies\\Assembly-CSharp.dll");
				const fs::path core = fs::path(PathManager::GetI()->GetEnginePathW()) / L"Binaries" / L"Scripting" / L"NovaScriptCore.dll";
				std::wstring monoLib, monoNative;
				if (fs::exists(game_cs, ec))
				{
					if (!MonoRuntime("x86_64", monoLib, monoNative) || !fs::exists(core, ec))
						csharp = { { "included", false }, { "error", !fs::exists(core, ec) ? "NovaScriptCore.dll missing in the engine" : "Mono runtime missing (Tools/fetch_android_mono.ps1)" } };
					else
					{
						const fs::path managed = game / L"Managed";
						fs::create_directories(managed, ec);
						uint64_t managedBytes = 0;
						int count = 0;
						auto add = [&](const fs::path& src) {
							fs::copy_file(src, managed / src.filename(), fs::copy_options::overwrite_existing, ec);
							managedBytes += fs::file_size(src, ec);
							files.push_back("Managed/" + wstring_to_string(src.filename().wstring()));
							++count;
						};
						// BCL 은 게임 · 엔진 어셈블리가 (AssemblyRef 로) 닿는 것만 — 모두 넣으면 21 MB
						std::map<std::string, fs::path> bcl;
						for (const auto& e : fs::directory_iterator(monoLib, ec))
							if (e.path().extension() == L".dll") bcl[wstring_to_string(e.path().stem().wstring())] = e.path();
						bcl["System.Private.CoreLib"] = fs::path(monoNative) / L"System.Private.CoreLib.dll";
						std::set<std::string> needed;
						std::vector<fs::path> queue = { core, game_cs, bcl["System.Private.CoreLib"] };
						for (const char* always : { "System.Runtime", "System.Private.Uri", "System.Runtime.InteropServices", "System.Console" })   // 리플렉션 · 런타임이 이름으로 찾는 것
							if (bcl.count(always)) queue.push_back(bcl[always]);
						while (!queue.empty())
						{
							const fs::path next = queue.back();
							queue.pop_back();
							if (!needed.insert(wstring_to_string(next.filename().wstring())).second) continue;
							add(next);
							for (const std::string& ref : AssemblyReferences(next))
								if (auto it = bcl.find(ref); it != bcl.end() && !needed.count(wstring_to_string(it->second.filename().wstring())))
									queue.push_back(it->second);
						}
						bytes += managedBytes;
						csharp = { { "included", true }, { "assemblies", count }, { "bytes", managedBytes } };
					}
				}
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
			// 첫 줄 = 내보낸 시각: 목록이 같아도 (압축 형식만 바꿈 등) 기기가 다시 풀게
			manifest << "# export " << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << "\n";
			for (const std::string& f : files) manifest << f << "\n";
			result = { { "files", files.size() }, { "jsonConverted", converted }, { "bytes", bytes }, { "scenes", sceneList },
				{ "textureCompression", TextureCompressor::AndroidDefaultName(texDefault) }, { "textures", textures }, { "models", models }, { "csharp", csharp },
				{ "seconds", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() },
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

		// build --out x.apk [--run] [--device serial] [--texture-compression ...]: Build Settings 의 Android Build 와 같은 작업을 시작 (끝은 build-status 로)
		bool Build(const std::string& op, const nlohmann::json& args, nlohmann::json& result, std::string& error)
		{
			if (op == "build")
			{
				AndroidBuild::Options o;
				o.OutputApk = string_to_wstring(args.value("out", std::string()));
				o.Run = args.value("run", false);
				o.Device = args.value("device", std::string());
				o.TextureCompression = args.value("texture-compression", std::string());
				if (o.OutputApk.empty()) { error = "--out file.apk is required"; return false; }
				if (!AndroidBuild::Start(o, error))
					return false;
				result = { { "started", true }, { "out", args.value("out", std::string()) } };
				return true;
			}
			const AndroidBuild::Result r = AndroidBuild::LastResult();
			result = { { "running", AndroidBuild::IsRunning() }, { "progress", AndroidBuild::Progress() }, { "status", AndroidBuild::Status() }, { "done", r.Done },
				{ "success", r.Success }, { "error", r.Error }, { "apk", r.Apk }, { "bytes", r.Bytes }, { "device", r.Device }, { "seconds", r.Seconds }, { "log", r.Log } };
			return true;
		}
	}

	bool ExportShaders(const nlohmann::json& args, nlohmann::json& result, std::string& error)
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
		{
			const std::filesystem::path engine = PathManager::GetI()->GetEnginePathW();
			for (const auto& f : std::filesystem::directory_iterator(engine / L"Shaders", ec))
				if (f.path().extension() == L".fx")
					files.push_back(f.path());
			// 공식 패키지의 셰이더 (Packages/<이름>/Shaders — 예: Toon 의 lilToon.fx). 기기는 이름 (<stem>.json) 으로 찾는다
			for (const auto& pkg : std::filesystem::directory_iterator(engine / L"Packages", ec))
				for (const auto& f : std::filesystem::directory_iterator(pkg.path() / L"Shaders", ec))
					if (f.path().extension() == L".fx")
						files.push_back(f.path());
		}
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

	bool ExportGame(const nlohmann::json& args, nlohmann::json& result, std::string& error)
	{
		return Export(args, result, error);
	}

	bool MonoRuntime(const std::string& abi, std::wstring& managedDir, std::wstring& nativeDir)
	{
		std::error_code ec;
		const fs::path engine = PathManager::GetI()->GetEnginePathW();
		const std::wstring rid = abi == "arm64-v8a" ? L"android-arm64" : L"android-x64";
		// 배포판: Android/Player/<ABI>/mono/{lib,native}, 엔진 개발: ThirdParty/MonoAndroid/<ABI>/runtimes/<rid>/{lib/net8.0,native}
		const fs::path release = engine / L"Android" / L"Player" / string_to_wstring(abi) / L"mono";
		const fs::path dev = engine / L"ThirdParty" / L"MonoAndroid" / string_to_wstring(abi) / L"runtimes" / rid;
		for (const auto& [lib, native] : { std::pair{ release / L"lib", release / L"native" }, std::pair{ dev / L"lib" / L"net8.0", dev / L"native" } })
			if (fs::exists(native / L"libmonosgen-2.0.so", ec) && fs::exists(native / L"System.Private.CoreLib.dll", ec) && fs::is_directory(lib, ec))
			{
				managedDir = lib.wstring();
				nativeDir = native.wstring();
				return true;
			}
		return false;
	}

	void RegisterEditor()
	{
		// nova android shaders --out Android/build/assets/Shaders [--path one.fx]
		CliServer::Register("android", "Android build tools: {op: shaders, out, path?} (nova android help)", [](const nlohmann::json& args, nlohmann::json& result, std::string& error) {
			const std::string op = args.value("op", std::string("help"));
			if (op == "help")
			{
				result = { { "ops", { "shaders --out folder [--path file.fx]: convert every .fx to OpenGL ES 3.20 (<name>.json for the APK assets/Shaders)",
					"export --out folder [--scenes a.scene,b.scene] [--texture-compression astc|etc2|dxt|none]: game data for the APK (folder/game: scenes + referenced assets, '/' paths, player.json, files.txt; images baked to <name>.dds, default = Player Settings Android Texture Compression; models (fbx, gltf, glb, vrm) as their mesh caches only)",
"reference --out file.png [--width 960 --height 540 --frames 10]: render the open scene's game camera like the player (DX11 reference for the Android scene test)",
					"build --out file.apk [--run] [--device serial] [--texture-compression astc|etc2|dxt|none]: Build Settings Android build (shaders + game data + APK, Build And Run installs and starts it); build-status: progress / result" } } };
				return true;
			}
			if (op == "export")
				return Export(args, result, error);
			if (op == "reference")
				return Reference(args, result, error);
			if (op == "shaders")
				return ExportShaders(args, result, error);
			if (op == "build" || op == "build-status")
				return Build(op, args, result, error);
			error = "unknown op '" + op + "' (nova android help)";
			return false;
		});
	}
}
