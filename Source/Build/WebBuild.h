#pragma once
#include <nlohmann/json.hpp>
#include <string>

// Unity 의 Web 빌드 (Build Settings → Web → Build / Build And Run): 브라우저 WebGPU 플레이어.
//  - 결과 폴더: index.html · game.json · game.data + 플레이어 (엔진만: nova.js · nova.wasm, C# 이 있으면: _framework — .NET 런타임과 엔진 한 wasm)
//    (WebTools::ExportGame — Build Settings 의 씬, 텍스처 = BC (PC 브라우저), 셰이더 = WGSL)
//  - Build And Run: 에디터 안의 작은 웹 서버 (127.0.0.1, wasm MIME · 교차 출처 격리 머리) 로 폴더를 열고 기본 브라우저로 연다 (Unity 와 같음)
// 플레이어: <엔진>/Binaries/Web (배포판) → <엔진>/Web/build/Release (엔진 개발 — Web/build.sh Release [host])
namespace WebBuild
{
	struct Options
	{
		std::wstring OutputFolder;
		bool Run = false;                // Build And Run (서버 + 브라우저)
		std::string TextureCompression;  // "" = dxt (PC 브라우저), none
	};

	// 에디터 (Build Settings): 진행 창과 함께 한 프레임 뒤에 빌드한다
	bool Start(const Options& options, std::string& error);
	bool IsRunning();
	void Update();          // 매 프레임 (에디터)
	void DrawProgress();    // 진행 창 (빌드 중일 때만)

	// CLI (nova web build): 바로 빌드. Run 이면 서버를 열고 url 을 돌려준다 (브라우저는 CLI 에서는 열지 않는다 — open 인자일 때만)
	bool Build(const Options& options, nlohmann::json& result, std::string& error);

	// 미리 보기 서버: 폴더를 http://localhost:<port>/ 로 (port 0 = 8600 부터 빈 곳). 반환 = url (실패하면 빈 문자열). 다시 부르면 폴더를 바꾼다
	std::string Serve(const std::wstring& folder, int port = 0);
	void StopServer();
	std::string ServerUrl();   // 열려 있지 않으면 빈 문자열

	// 플레이어가 있는지 (Build Settings 표시): 엔진만 판 · C# 판 폴더 (없으면 빈 문자열)
	std::wstring EnginePlayerDir();
	std::wstring DotnetPlayerDir();
}
