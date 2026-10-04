#pragma once
#include <string>
#include <utility>
#include <vector>

// Unity 의 BuildPipeline: Build Settings 의 씬들로 Windows 실행 파일을 만든다.
//  <출력 폴더>/<제품>.exe (+ 엔진 DLL)
//  <출력 폴더>/<제품>_Data/
//      player.json                       제품 이름, 씬 목록, 창 모드/해상도
//      Assets/...                        씬과 씬이 참조하는 에셋만 (재질, 프리팹, Animator, 오디오, 텍스처, 모델 ...)
//      Resources/...                     엔진 패키지 중 참조하는 것 + 하늘 텍스처
//      ProjectSettings/, ProjectSetting/fonts, Shaders/
//      Library/ScriptAssemblies/Assembly-CSharp.dll
//      Binaries/Scripting (NovaScriptCore), Binaries/ShaderCache (셰이더 컴파일 결과 → 빠른 시작)
// 씬 JSON 을 시작으로 문자열 값 중 실제 파일을 가리키는 것을 따라가며(재귀) 필요한 파일만 모은다.
// 복사는 작업 스레드에서 하고, 에디터는 진행 창을 보여 준다. 실행하려면 대상 PC 에 .NET 8 런타임이 필요하다(C# 스크립트).
namespace BuildPipeline
{
	struct Options
	{
		std::wstring OutputFolder;
		bool Run = false;           // Build And Run
		bool Development = false;   // pdb 포함
		bool Reveal = true;         // 끝나면 탐색기로 결과 표시 (Run 이 아닐 때)
	};

	// 검사(씬, 스크립트 컴파일) 후 백그라운드 빌드 시작. 실패하면 false + error
	bool Start(const Options& options, std::string& error);
	bool IsRunning();
	float Progress();               // 0..1
	std::string Status();
	// 매 프레임 (에디터): 끝났으면 Console 에 결과, Build And Run 이면 실행
	void Update();
	// 진행 창 (빌드 중일 때만)
	void DrawProgress();

	// 게임에 들어가는 에셋 (씬 · 씬이 참조하는 파일 · ProjectSettings 와 그것이 참조하는 파일 · 하늘 · 글꼴): (루트 기준 경로, 전체 경로).
	//  플레이어 빌드와 같은 규칙 — 안드로이드 내보내기 (nova android export) 가 쓴다
	std::vector<std::pair<std::wstring, std::wstring>> CollectGameFiles(const std::vector<std::string>& scenes);
}
