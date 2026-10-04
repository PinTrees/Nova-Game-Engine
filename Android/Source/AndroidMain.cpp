#include "pch.h"
#include "Rhi.h"
#include "RhiTest.h"
#include "GfxTest.h"
#include "GfxGLES.h"
#include "AndroidEngine.h"
#include "EditorApp.h"
#include "PlayerRuntime.h"
#include "AndroidPlatform.h"
#include <android_native_app_glue.h>
#include <android/asset_manager.h>
#include <android/log.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>
#include <jni.h>
#include <codecvt>
#include <fstream>
#include <locale>
#include "OcclusionCulling.h"

// NOVA 안드로이드 진입점 (NativeActivity + NDK native_app_glue — Java 코드 없음).
//  - 검사 실행기: `am start -n com.nova.engine/android.app.NativeActivity -e test rhi` → 화면 없는 EGL (pbuffer) 의
//    OpenGL ES 3.2 로 엔진 검사 장면을 그려 앱 외부 파일 폴더에 BMP · 결과 JSON 을 쓰고 끝낸다 (Tools/tests/android.ps1)
//  - 플레이어 셸 (-e test 없이): 창 표면 · 프레임 루프 · 생명 주기 (내렸다 올리기, 회전) · 터치. 엔진 런타임이 이 위에 올라간다.
//    상태 변화는 logcat 의 "NOVA_EVENT {json}" 줄로 알린다
std::unique_ptr<Rhi::Device> CreateGlesRhiDevice(std::string& error);   // GLESRhi.cpp
bool NovaReadAsset(const std::string& path, std::string& out);         // 아래

namespace
{
	android_app* s_App = nullptr;
	std::string s_FilesDir;

	void Log(const char* fmt, ...)
	{
		va_list ap;
		va_start(ap, fmt);
		__android_log_vprint(ANDROID_LOG_INFO, "NOVA", fmt, ap);
		va_end(ap);
	}

	// 인텐트 문자열 값 (am start -e key value)
	std::string IntentExtra(JNIEnv* env, jobject activity, const char* key)
	{
		jclass actClass = env->GetObjectClass(activity);
		jmethodID getIntent = env->GetMethodID(actClass, "getIntent", "()Landroid/content/Intent;");
		jobject intent = env->CallObjectMethod(activity, getIntent);
		if (!intent) return {};
		jclass intentClass = env->GetObjectClass(intent);
		jmethodID getExtra = env->GetMethodID(intentClass, "getStringExtra", "(Ljava/lang/String;)Ljava/lang/String;");
		jstring jkey = env->NewStringUTF(key);
		auto value = (jstring)env->CallObjectMethod(intent, getExtra, jkey);
		std::string out;
		if (value)
		{
			const char* c = env->GetStringUTFChars(value, nullptr);
			out = c;
			env->ReleaseStringUTFChars(value, c);
		}
		return out;
	}

	bool WriteBmp(const std::string& path, const std::vector<uint8_t>& rgba, int w, int h)
	{
		std::ofstream f(path, std::ios::binary | std::ios::trunc);
		if (!f) return false;
		const uint32_t imageSize = (uint32_t)w * h * 4, fileSize = 54 + imageSize;
		uint8_t hdr[54] = { 'B', 'M' };
		auto put32 = [&](int at, uint32_t v) { memcpy(hdr + at, &v, 4); };
		put32(2, fileSize);
		put32(10, 54);
		put32(14, 40);
		put32(18, (uint32_t)w);
		put32(22, (uint32_t)(-h));   // 위에서부터 (행 0 = 위)
		hdr[26] = 1;
		hdr[28] = 32;
		put32(34, imageSize);
		f.write((const char*)hdr, 54);
		std::vector<uint8_t> bgra(rgba.size());
		for (size_t i = 0; i + 3 < rgba.size(); i += 4)
		{
			bgra[i] = rgba[i + 2];
			bgra[i + 1] = rgba[i + 1];
			bgra[i + 2] = rgba[i];
			bgra[i + 3] = rgba[i + 3];
		}
		f.write((const char*)bgra.data(), bgra.size());
		return (bool)f;
	}

	std::string JsonEscape(const std::string& s)
	{
		std::string o;
		for (char c : s)
		{
			if (c == '"' || c == '\\') { o += '\\'; o += c; }
			else if (c == '\n') o += "\\n";
			else if ((unsigned char)c >= 0x20) o += c;
		}
		return o;
	}

	// APK 의 assets/game (nova android export) → 앱 파일 폴더의 game/. files.txt 가 바뀌었을 때만 다시 푼다
	bool ExtractGame(std::string& error)
	{
		AAssetManager* am = s_App->activity->assetManager;
		std::string manifest;
		if (!NovaReadAsset("game/files.txt", manifest)) { error = "no game data in the APK (nova android export)"; return false; }
		const std::filesystem::path root = std::filesystem::path(s_FilesDir) / "game";
		const std::string stamp = std::to_string(std::hash<std::string>{}(manifest)) + ":" + std::to_string(manifest.size());
		std::string old;
		{
			std::ifstream in(root / ".extracted");
			std::getline(in, old);
		}
		if (old == stamp) return true;
		const auto t0 = std::chrono::steady_clock::now();
		std::error_code ec;
		std::filesystem::remove_all(root, ec);
		std::filesystem::create_directories(root, ec);
		size_t files = 0, bytes = 0;
		size_t pos = 0;
		while (pos < manifest.size())
		{
			size_t end = manifest.find('\n', pos);
			if (end == std::string::npos) end = manifest.size();
			std::string rel = manifest.substr(pos, end - pos);
			pos = end + 1;
			if (!rel.empty() && rel.back() == '\r') rel.pop_back();
			if (rel.empty() || rel[0] == '#') continue;   // '#' 줄 = 내보낸 시각 (스탬프에만 쓴다)
			AAsset* a = AAssetManager_open(am, ("game/" + rel).c_str(), AASSET_MODE_STREAMING);
			if (!a) { error = "missing asset game/" + rel; return false; }
			const std::filesystem::path dst = root / rel;
			std::filesystem::create_directories(dst.parent_path(), ec);
			std::ofstream out(dst, std::ios::binary | std::ios::trunc);
			char buf[65536];
			int n;
			while ((n = AAsset_read(a, buf, sizeof(buf))) > 0) { out.write(buf, n); bytes += (size_t)n; }
			AAsset_close(a);
			++files;
		}
		std::ofstream(root / ".extracted", std::ios::trunc) << stamp;
		Log("game data extracted: %zu files, %.1f MB in %.0f ms", files, bytes / 1048576.0,
			std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
		return true;
	}

	// 엔진 장면 검사: 게임 데이터의 첫 씬을 화면 없이 (pbuffer) 몇 프레임 돌리고 백버퍼를 BMP 로
	bool RunSceneTest(int width, int height, int frames, std::string& png, double& loadMs, double& drawMs, std::string& error)
	{
		if (!ExtractGame(error)) return false;
		if (!PlayerRuntime::Detect()) { error = "game/player.json not found"; return false; }
		const auto t0 = std::chrono::steady_clock::now();
		EditorApp* app = new EditorApp(nullptr);   // 검사가 끝나면 앱이 끝난다 (지우지 않음)
		app->SetScreenSize((UINT)width, (UINT)height);
		if (!app->Init()) { error = "engine init failed (Logs/Editor.log)"; return false; }
		loadMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		const auto t1 = std::chrono::steady_clock::now();
		for (int i = 0; i < frames; ++i)
			app->Run();
		glFinish();
		drawMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count() / (std::max)(frames, 1);
		DirectX::ScratchImage img;
		if (FAILED(Gfx::CaptureTexture(Gfx::Context(), app->BackBufferTexture(), img))) { error = "backbuffer capture failed"; return false; }
		const DirectX::Image* im = img.GetImage(0, 0, 0);
		std::vector<uint8_t> rgba(im->pixels, im->pixels + (size_t)width * height * 4);
		png = s_FilesDir + "/scene_GLES.bmp";
		if (!WriteBmp(png, rgba, width, height)) { error = "bmp write failed: " + png; return false; }
		return true;
	}

	void RunTest(const std::string& test, int width, int height, int frames)
	{
		const auto t0 = std::chrono::steady_clock::now();
		std::string error, device, png, occlusion = "null";
		bool ok = false;
		double loadMs = 0, drawMs = 0;
		{
			AndroidPlatform::Egl egl;
			if (egl.Init(error) && test == "scene")
			{
				device = (const char*)glGetString(GL_VERSION);
				ok = RunSceneTest(width, height, frames, png, loadMs, drawMs, error);
				if (ok) occlusion = OcclusionCulling::InfoJson();   // 오클루전 컬링 검사 (몇 프레임 늦은 GPU 결과)
			}
			else if (error.empty())
			{
				std::unique_ptr<Rhi::Device> dev = CreateGlesRhiDevice(error);
				if (dev)
				{
					device = dev->Description();
					Log("device %s", device.c_str());
					if (test == "rhi")
					{
						RhiTest::Result r;
						ok = RhiTest::RenderLitScene(*dev, width, height, r, error);
						if (ok)
						{
							loadMs = r.LoadMs;
							drawMs = r.DrawMs;
							png = s_FilesDir + "/rhi_GLES.bmp";
							if (!WriteBmp(png, r.Rgba, r.Width, r.Height)) { ok = false; error = "bmp write failed: " + png; }
						}
					}
					else if (test == "gfx")
					{
						// Gfx 층 (엔진 렌더러와 같은 방식: GfxDevice/GfxContext + FxEffect) — PC 의 gfx-test DirectX11 그림과 비교
						ComPtr<GfxDevice> gdev;
						ComPtr<GfxContext> gctx;
						if (GfxGLES::CreateDevice(gdev.GetAddressOf(), gctx.GetAddressOf(), error))
						{
							std::unique_ptr<Rhi::Device> grhi = GfxGLES::CreateRhiDevice(gdev.Get(), gctx.Get(), error);
							GfxTest::Result r;
							ok = grhi && GfxTest::Render(gdev.Get(), gctx.Get(), grhi.get(), width, height, r, error);
							if (ok)
							{
								loadMs = r.LoadMs;
								drawMs = r.DrawMs;
								png = s_FilesDir + "/gfx_GLES.bmp";
								if (!WriteBmp(png, r.Rgba, r.Width, r.Height)) { ok = false; error = "bmp write failed: " + png; }
							}
							grhi.reset();
							gctx->ClearState();
						}
					}
					else if (test == "info")
					{
						// 확장 목록 (어떤 기능을 쓸 수 있는지 — logcat 의 NOVA_GL_EXTENSIONS 줄)
						GLint n = 0;
						glGetIntegerv(GL_NUM_EXTENSIONS, &n);
						std::string ext;
						for (GLint i = 0; i < n; ++i)
						{
							ext += (const char*)glGetStringi(GL_EXTENSIONS, (GLuint)i);
							ext += ' ';
						}
						Log("NOVA_GL_EXTENSIONS %s", ext.c_str());
						// 압축 텍스처 형식 (GL 번호, 16 진수 — ETC2 0x9274.., ASTC 0x93B0..)
						GLint nf = 0;
						glGetIntegerv(GL_NUM_COMPRESSED_TEXTURE_FORMATS, &nf);
						std::vector<GLint> fmts((size_t)(std::max)(nf, 0));
						if (nf > 0) glGetIntegerv(GL_COMPRESSED_TEXTURE_FORMATS, fmts.data());
						std::string fl;
						char hex[16];
						for (GLint f : fmts) { snprintf(hex, sizeof(hex), "%x ", f); fl += hex; }
						Log("NOVA_GL_COMPRESSED %d: %s", nf, fl.c_str());
						ok = true;
					}
					else error = "unknown test '" + test + "'";
					dev->Finish();
				}
			}
		}
		const double totalMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		char line[4096];
		snprintf(line, sizeof(line), "{\"test\":\"%s\",\"ok\":%s,\"device\":\"%s\",\"image\":\"%s\",\"width\":%d,\"height\":%d,\"loadMs\":%.1f,\"drawMs\":%.1f,\"totalMs\":%.1f,\"error\":\"%s\",\"occlusion\":%s}",
			test.c_str(), ok ? "true" : "false", JsonEscape(device).c_str(), png.c_str(), width, height, loadMs, drawMs, totalMs, JsonEscape(error).c_str(), occlusion.c_str());
		std::ofstream(s_FilesDir + "/result_" + test + ".json", std::ios::trunc) << line;
		Log("NOVA_TEST %s", line);
	}

	// 이벤트 처리 중 다 쓰면 다음 이벤트가 오기 전까지 기다린다 (그리는 중이면 0 = 바로)
	bool PumpEvents(android_app* app, bool block)
	{
		int events = 0;
		android_poll_source* source = nullptr;
		while (ALooper_pollOnce(block ? -1 : 0, nullptr, &events, (void**)&source) >= 0)
		{
			if (source) source->process(app, source);
			if (app->destroyRequested) return false;
			block = false;
		}
		return !app->destroyRequested;
	}
}

// APK 의 assets/ 에서 파일 하나 (GLESRhi 가 셰이더 JSON 을 읽는다)
bool NovaReadAsset(const std::string& path, std::string& out)
{
	if (!s_App || !s_App->activity->assetManager) return false;
	AAsset* a = AAssetManager_open(s_App->activity->assetManager, path.c_str(), AASSET_MODE_BUFFER);
	if (!a) return false;
	const off_t n = AAsset_getLength(a);
	out.assign((const char*)AAsset_getBuffer(a), (size_t)n);
	AAsset_close(a);
	return true;
}

void android_main(android_app* app)
{
	s_App = app;
	ANativeActivity* activity = app->activity;
	s_FilesDir = activity->externalDataPath ? activity->externalDataPath : (activity->internalDataPath ? activity->internalDataPath : "");
	std::error_code ec;
	std::filesystem::create_directories(s_FilesDir, ec);
	NovaAndroid::SetFilesDir(s_FilesDir);
	EditorLog::Init();   // 앱 파일 폴더의 Logs/Editor.log (+ logcat)
	PathManager::GetI()->Init();   // 엔진 · 프로젝트 루트 = 앱 파일 폴더의 game/

	JNIEnv* env = nullptr;
	activity->vm->AttachCurrentThread(&env, nullptr);
	const std::string test = IntentExtra(env, activity->clazz, "test");
	const std::string size = IntentExtra(env, activity->clazz, "size");
	const std::string framesArg = IntentExtra(env, activity->clazz, "frames");
	const std::string mode = IntentExtra(env, activity->clazz, "mode");   // shell = 게임 데이터가 있어도 셸 (검사)
	if (IntentExtra(env, activity->clazz, "occlusion") == "off")
		OcclusionCulling::Enabled = false;   // 오클루전 컬링 끔 (켠 화면과 비교하는 검사)
	activity->vm->DetachCurrentThread();

	if (!test.empty())
	{
		int w = 960, h = 540;
		if (!size.empty()) sscanf(size.c_str(), "%dx%d", &w, &h);
		Log("NOVA start: test='%s' size %dx%d files %s", test.c_str(), w, h, s_FilesDir.c_str());
		RunTest(test, w, h, framesArg.empty() ? 30 : atoi(framesArg.c_str()));
		Log("NOVA done");
		ANativeActivity_finish(activity);
		while (PumpEvents(app, true)) {}   // 끝날 때까지 이벤트를 비운다 (glue 규칙)
		return;
	}

	// 게임 데이터가 있으면 엔진 플레이어, 없으면 플레이어 셸 (창 · 루프 · 터치 확인용)
	std::string error;
	const bool game = mode != "shell" && ExtractGame(error) && PlayerRuntime::Detect();
	if (!game) Log("no game data (%s) - shell only", error.c_str());
	AndroidPlatform::Shell shell(app, s_FilesDir, game);
	while (PumpEvents(app, !shell.Running()))
		shell.Frame();
	Log("NOVA done");
}
