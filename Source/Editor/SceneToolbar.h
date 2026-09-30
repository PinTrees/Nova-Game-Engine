#pragma once

class EditorCamera;

// Scene 뷰 상단 툴바(Pivot/Local, 스냅, 드로우 모드, 2D, 라이팅, 오디오, 이펙트, 가시성, 그리드, 카메라, 기즈모)와
// 뷰 위에 떠 있는 도구 팔레트(Hand/Move/Rotate/Scale/Rect/Transform) — Unity 6 Scene 뷰와 같은 구성.
namespace SceneToolbar
{
	enum class Tool { View, Move, Rotate, Scale, Rect, Transform };
	enum class PivotMode { Pivot, Center };
	enum class HandleSpace { Local, Global };

	constexpr float kTopBarHeight = 26.0f;

	// Scene 카메라 설정 (툴바 카메라 드롭다운 = Unity 의 Scene Camera 패널).
	// 사용자별 설정이라 %LOCALAPPDATA%/NOVA/Editor/SceneCamera.json 에 저장한다.
	struct SceneCameraSettings
	{
		float FieldOfView = 60.0f;     // 세로 시야각 (도)
		float NearClip = 0.3f;
		float FarClip = 10000.0f;
		bool Easing = true;            // 이동 시작/멈춤을 부드럽게
		bool Acceleration = true;      // 누르고 있을수록 빨라짐
		float Speed = 1.0f;            // 비행(우클릭 + WASD) 속도 배율
		float SpeedMin = 0.01f;
		float SpeedMax = 2.0f;
	};
	SceneCameraSettings& CameraSettings();
	// 설정을 카메라 투영에 반영 (바뀌었을 때만 실제로 SetLens)
	void ApplyCameraLens(EditorCamera* camera, bool force = false);

	Tool CurrentTool();
	void SetTool(Tool tool);
	PivotMode Pivot();
	HandleSpace Space();

	bool GizmosVisible();      // 툴바의 Gizmos 토글
	bool GridVisible();        // Grid 드롭다운의 Show Grid
	bool SnapEnabled();        // Ctrl 누름 또는 스냅 토글
	float SnapIncrement();     // 이동 스냅 간격(유닛)
	bool PostProcessingVisible();   // Effects 토글 + Effects 메뉴의 Post Processing (Scene 뷰 후처리 표시)
	bool ParticlesVisible();        // Effects 토글 + Effects 메뉴의 Particle Systems (Scene 뷰 입자 표시)

	// Scene 창 콘텐츠 맨 위에 툴바를 그린다 (현재 커서 위치, 폭 = width). 그린 뒤 커서는 툴바 아래로 이동한다.
	void DrawTopBar(float width, EditorCamera* camera);

	// 뷰(이미지 영역) 왼쪽 위에 도구 팔레트를 겹쳐 그린다. 뷰 위에 그려진 뒤 호출해야 클릭이 우선한다.
	void DrawToolPalette(const ImVec2& viewMin, const ImVec2& viewMax);

	// Q/W/E/R/T/Y 도구 단축키 (뷰가 호버되어 있고 우클릭 카메라 조작 중이 아닐 때)
	void HandleShortcuts(bool viewHovered);
}
