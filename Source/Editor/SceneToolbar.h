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

	Tool CurrentTool();
	void SetTool(Tool tool);
	PivotMode Pivot();
	HandleSpace Space();

	bool GizmosVisible();      // 툴바의 Gizmos 토글
	bool GridVisible();        // Grid 드롭다운의 Show Grid
	bool SnapEnabled();        // Ctrl 누름 또는 스냅 토글
	float SnapIncrement();     // 이동 스냅 간격(유닛)

	// Scene 창 콘텐츠 맨 위에 툴바를 그린다 (현재 커서 위치, 폭 = width). 그린 뒤 커서는 툴바 아래로 이동한다.
	void DrawTopBar(float width, EditorCamera* camera);

	// 뷰(이미지 영역) 왼쪽 위에 도구 팔레트를 겹쳐 그린다. 뷰 위에 그려진 뒤 호출해야 클릭이 우선한다.
	void DrawToolPalette(const ImVec2& viewMin, const ImVec2& viewMax);

	// Q/W/E/R/T/Y 도구 단축키 (뷰가 호버되어 있고 우클릭 카메라 조작 중이 아닐 때)
	void HandleShortcuts(bool viewHovered);
}
