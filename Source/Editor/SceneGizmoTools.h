#pragma once

class EditorCamera;

// Scene 뷰의 Move / Rotate / Scale / Rect / Transform 조작 핸들.
// 선택된 GameObject 의 Transform 을 직접 수정하며, 도구/Pivot/Local·Global/스냅은 SceneToolbar 의 상태를 따른다.
// SceneViewOverlay::Begin ~ End 사이에서 호출해야 한다 (3D → 화면 투영을 그 안에서 한다).
namespace SceneGizmoTools
{
	// viewMin/viewMax: Scene 이미지 영역(화면 좌표), viewHovered: 마우스가 뷰 위에 있고 팔레트/툴바 위가 아님
	void Update(EditorCamera* camera, const ImVec2& viewMin, const ImVec2& viewMax, bool viewHovered);

	// 핸들을 드래그하는 중인지 (카메라 이동 등 다른 조작과 충돌 방지용)
	bool IsDragging();

	// 다른 도구(지형 브러시 등)가 마우스를 쓰는 동안 변환 핸들과 클릭 선택을 끈다 (카메라 조작은 유지)
	void SetSuppressed(bool suppressed);
}
