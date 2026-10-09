#pragma once

class SplineContainer;
class EditorCamera;

// Spline Container 의 점 (매듭) 편집: 공용 SplinePointEditor 로 끌기 · Ctrl+클릭 더하기 · Shift+클릭 / Delete 지우기.
//  점은 지면 (지형 · 물 밑바닥) 위에 놓인다 — 지면이 없으면 첫 점 높이의 평면
namespace SplineEditor
{
	void InspectorKnots(SplineContainer& spline);
	void DrawKnots(const SplineContainer& spline);
	bool SceneGUI(EditorCamera* camera, const ImVec2& viewMin, const ImVec2& viewMax, bool viewHovered);
}
