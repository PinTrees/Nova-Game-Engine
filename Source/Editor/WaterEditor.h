#pragma once

class WaterBody;
class EditorCamera;

// 물 편집 (호수 윤곽·강 가운데 선의 점).
//  Inspector 의 Edit Points 를 켜면 Scene 뷰에서: 점 끌기 = 지형 위로 옮김, Ctrl+클릭 = 점 추가(가까운 변에 끼움 / 강 끝이면 늘림),
//  Shift+클릭 또는 Delete = 선택한 점 지우기. 켜져 있는 동안 이동 핸들·클릭 선택은 쉰다 (지형 브러시처럼)
namespace WaterEditor
{
	void InspectorPoints(WaterBody& body);   // WaterBody::OnInspectorGUI 의 Shape 부분
	void DrawPoints(const WaterBody& body);  // 선택된 물의 점 표시 (OnDrawGizmos)
	// Scene 뷰 (SceneViewOverlay::Begin ~ End 사이). 점 편집 중이면 true → 변환 핸들/선택을 끈다
	bool SceneGUI(EditorCamera* camera, const ImVec2& viewMin, const ImVec2& viewMax, bool viewHovered);
	bool IsEditing();
}
