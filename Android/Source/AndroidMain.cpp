#include "pch.h"
#include "Rhi.h"
#include "RhiTest.h"
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

// NOVA 안드로이드 진입점 (NativeActivity + NDK native_app_glue — Java 코드 없음).
//  - 검사 실행기: `am start -n com.nova.engine/android.app.NativeActivity -e test rhi` → 화면 없는 EGL (pbuffer) 의
//    OpenGL ES 3.2 로 엔진 검사 장면을 그려 앱 외부 파일 폴더에 BMP · 결과 JSON 을 쓰고 끝낸다 (Tools/tests/android.ps1)
//  - 플레이어 셸 (-e test 없이): 창 표면 · 프레임 루프 · 생명 주기 (내렸다 올리기, 회전) · 터치. 엔진 런타임이 이 위에 올라간다.
//    상태 변화는 logcat 의 "NOVA_EVENT {json}" 줄로 알린다
std::unique_ptr<Rhi::Device> CreateGlesRhiDevice(std::string& error);   // GLESRhi.cpp

namespace
{
	android_app* s_App = nullptr;
	std::string s_FilesDir;
	std::mutex s_LogLock;

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

	void RunTest(const std::string& test, int width, int height)
	{
		const auto t0 = std::chrono::steady_clock::now();
		std::string error, device, png;
		bool ok = false;
		double loadMs = 0, drawMs = 0;
		{
			AndroidPlatform::Egl egl;
			if (egl.Init(error))
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
					else error = "unknown test '" + test + "'";
					dev->Finish();
				}
			}
		}
		const double totalMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		char line[2048];
		snprintf(line, sizeof(line), "{\"test\":\"%s\",\"ok\":%s,\"device\":\"%s\",\"image\":\"%s\",\"width\":%d,\"height\":%d,\"loadMs\":%.1f,\"drawMs\":%.1f,\"totalMs\":%.1f,\"error\":\"%s\"}",
			test.c_str(), ok ? "true" : "false", JsonEscape(device).c_str(), png.c_str(), width, height, loadMs, drawMs, totalMs, JsonEscape(error).c_str());
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

// ---- 엔진 쪽 도우미 (pch.h 선언)
namespace EditorLog
{
	void Write(const char* category, const char* format, ...)
	{
		char msg[4096];
		va_list ap;
		va_start(ap, format);
		vsnprintf(msg, sizeof(msg), format, ap);
		va_end(ap);
		__android_log_print(ANDROID_LOG_INFO, "NOVA", "[%s] %s", category, msg);
		std::lock_guard<std::mutex> lock(s_LogLock);
		if (!s_FilesDir.empty())
			std::ofstream(s_FilesDir + "/Editor.log", std::ios::app) << "[" << category << "] " << msg << "\n";
	}
}

std::wstring string_to_wstring(const std::string& str)
{
	std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
	return conv.from_bytes(str);
}

std::string wstring_to_string(const std::wstring& wstr)
{
	std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
	return conv.to_bytes(wstr);
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
	std::filesystem::remove(s_FilesDir + "/Editor.log", ec);

	JNIEnv* env = nullptr;
	activity->vm->AttachCurrentThread(&env, nullptr);
	const std::string test = IntentExtra(env, activity->clazz, "test");
	const std::string size = IntentExtra(env, activity->clazz, "size");
	activity->vm->DetachCurrentThread();

	if (!test.empty())
	{
		int w = 960, h = 540;
		if (!size.empty()) sscanf(size.c_str(), "%dx%d", &w, &h);
		Log("NOVA start: test='%s' size %dx%d files %s", test.c_str(), w, h, s_FilesDir.c_str());
		RunTest(test, w, h);
		Log("NOVA done");
		ANativeActivity_finish(activity);
		while (PumpEvents(app, true)) {}   // 끝날 때까지 이벤트를 비운다 (glue 규칙)
		return;
	}

	// 플레이어 셸: 창이 생기면 그리고, 내리면 멈추고, 다시 올리면 이어서
	AndroidPlatform::Shell shell(app, s_FilesDir);
	while (PumpEvents(app, !shell.Running()))
		shell.Frame();
	Log("NOVA done");
}
