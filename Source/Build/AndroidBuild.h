#pragma once
#include <string>
#include <vector>

// Unity 의 Android 빌드 (Build Settings → Android → Build / Build And Run): Gradle 없이 APK 를 만든다.
//  1) 에디터 (메인 스레드, 프레임마다 한 단계): 셰이더 → GLSL ES (assets/Shaders), 게임 데이터 (assets/game — 텍스처 굽기 · 메시 캐시, nova android export 와 같음)
//  2) 작업 스레드: AndroidManifest (패키지 이름 · 제품 이름 · 버전) → aapt2 link (+ assets) → libnova.so 넣기 (zip 에 직접, 압축 없이) → zipalign → apksigner (디버그 키)
//  3) Build And Run: adb 장치 (없으면 켜져 있는 MuMu 플레이어에 adb connect) → install → 실행
// 도구: Android SDK · JDK = ANDROID_HOME · JAVA_HOME → NOVA Hub 의 AndroidTools (엔진 옆 / %LOCALAPPDATA%\NOVA) → Android Studio 기본 위치.
// 플레이어 라이브러리: <엔진>/Android/Player/<ABI>/libnova.so (배포판) → <엔진>/Android/build/cmake/<ABI>-Release/libnova.so (엔진 개발 — python Android/build.py)
namespace AndroidBuild
{
	struct Options
	{
		std::wstring OutputApk;          // <폴더>/<제품>.apk
		bool Run = false;                // Build And Run
		std::string Device;              // adb 시리얼 (비면 첫 장치)
		std::string TextureCompression;  // "" = Player Settings, astc · etc2 · dxt · none
		int AppBundle = -1;              // -1 = Build Settings 의 Build App Bundle, 0 = .apk, 1 = .aab (Google Play)
		// 서명: 비면 Player Settings 의 Publishing Settings (Custom Keystore) · 이번 실행에 넣은 비밀번호, 그것도 없으면 디버그 키
		std::string Keystore, KeystorePass, KeyAlias, KeyAliasPass;
	};

	// 새 키 저장소 (Unity 의 Keystore Manager — Create New): keytool -genkeypair (RSA 2048, validityYears 년)
	bool CreateKeystore(const std::wstring& path, const std::string& storePass, const std::string& alias, const std::string& keyPass,
		const std::string& distinguishedName, int validityYears, std::string& error);

	bool Start(const Options& options, std::string& error);
	bool IsRunning();
	float Progress();
	std::string Status();
	void Update();          // 매 프레임 (에디터): 메인 스레드 단계 진행 · 끝나면 Console 에 결과
	void DrawProgress();    // 진행 창 (빌드 중일 때만)

	// 마지막 빌드 결과 (CLI nova android build-status)
	struct Result
	{
		bool Done = false, Success = false;
		std::string Error, Apk, Aab, Device, Log, Signer;
		double Seconds = 0.0;
		uint64_t Bytes = 0;
	};
	Result LastResult();

	// 도구 · 장치
	std::wstring FindSdk();
	std::wstring FindJava();
	std::wstring PlayerLibrary(const std::string& abi);   // 없으면 빈 문자열
	std::vector<std::string> Devices(bool connectMuMu);   // adb devices (connectMuMu = 켜진 MuMu VM 에 먼저 adb connect)
	std::string DefaultPackageName();                     // com.<회사>.<제품> (Unity 와 같은 기본값, 안드로이드 규칙에 맞게)
	std::string PackageName();                            // Player Settings 의 Package Name (비면 기본값)
}
