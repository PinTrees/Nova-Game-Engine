#pragma once

class TerrainSpline;
class EditorCamera;

// 지형 스플라인 점 편집 (공용 SplinePointEditor 사용). Road 점은 지면 높이에 놓는다
namespace TerrainSplineEditor
{
	void InspectorPoints(TerrainSpline& spline);
	void DrawPoints(const TerrainSpline& spline);
	bool SceneGUI(EditorCamera* camera, const ImVec2& viewMin, const ImVec2& viewMax, bool viewHovered);
}
