#pragma once
#include "ParticleCurves.h"

class ParticleSystem;

// Particle System 의 에디터 부분
//  - 미리보기: Play 모드가 아닐 때 선택한 Particle System(가장 위 Particle System 부터 자식까지)을 재생한다 (Unity 와 같음)
//  - Scene 뷰 오른쪽 아래 "Particle Effect" 창: Play/Pause, Restart, Stop, Playback Speed, Playback Time, Particles
//  - Inspector 위젯: 모듈 머리글, MinMaxCurve / MinMaxGradient 필드, 곡선·그라디언트 편집 팝업
namespace ParticleSystemEditor
{
	// 선택한 오브젝트가 속한 Particle System 묶음의 맨 위 (없으면 nullptr)
	ParticleSystem* SelectedRoot();
	void UpdatePreview(float dt);
	float PlaybackSpeedFor(ParticleSystem* ps);

	// Scene 뷰 오버레이. OverlayRect 가 false 면 그리지 않는다 (클릭 선택에서 이 영역을 빼기 위해 먼저 부른다)
	bool OverlayRect(const ImVec2& viewMin, const ImVec2& viewMax, ImVec2& rectMin, ImVec2& rectMax);
	void DrawOverlay(const ImVec2& viewMin, const ImVec2& viewMax);

	// ---- Inspector 위젯 ----
	// 모듈 머리글 [✓] 이름. enabled 가 nullptr 이면 체크박스 없음, supported = false 면 회색(아직 지원 안 함). 열려 있으면 true
	bool ModuleHeader(const char* id, const char* title, bool* enabled, bool defaultOpen = false, bool supported = true);
	void ModuleEnd();
	// 오른쪽 ▼ 로 모드(Constant / Curve / Random Between Two Constants / Random Between Two Curves)를 고른다
	bool CurveField(const char* label, MinMaxCurve* curve, int indent = 0, bool allowCurves = true);
	// colorModes = false 면 Gradient / Random Between Two Gradients 만 (Color over Lifetime)
	bool GradientField(const char* label, MinMaxGradient* gradient, int indent = 0, bool colorModes = true);
	// 텍스처 [ 이름 ⊙ ] (내장 입자 텍스처 · Project 의 이미지 끌어 놓기) — Line · Trail Renderer 도 쓴다
	bool TexturePicker(const char* label, std::string* value, const std::string& key);
}
