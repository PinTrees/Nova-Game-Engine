#pragma once
#include <string>

// 에디터 시작 로딩 창 (Unity 의 프로젝트 여는 진행 창과 같은 역할).
//  - 별도 스레드의 작은 창이라 메인 스레드가 셰이더 컴파일 등으로 멈춰 있어도 계속 그려진다.
//  - 진행 막대 + 현재 작업 이름, 한 작업이 오래 걸리면 막대 위로 빛이 흘러 멈춘 게 아님을 보여 준다.
//  - 메인 에디터 창은 첫 프레임이 준비될 때까지 숨겨 두고, 보이는 순간 로딩 창을 닫는다.
namespace LoadingScreen
{
	void Begin(const std::wstring& subtitle);
	void SetSubtitle(const std::wstring& subtitle);
	void End();
	bool IsActive();

	// 전체 진행률(0~1)과 현재 작업
	void SetProgress(float progress, const std::wstring& status);
	// 진행률은 그대로 두고 설명만 바꾼다 (예: "Importing Model.fbx")
	void SetStatus(const std::wstring& status);

	// 셰이더 단계: [from, to] 구간을 expected 개수로 나눠 한 개씩 올린다
	void BeginShaderPhase(float from, float to, int expected);
	void OnShader(const std::wstring& fileName, bool fromCache);
}
