#pragma once

class EditorCamera;

// Scene 뷰 위에 ImGui DrawList 로 그리는 오버레이 (배경 그라디언트, 그리드, 카메라 프러스텀 등).
// 3D 좌표를 뷰 영역(Scene 창의 이미지 영역)으로 투영하며, 카메라 뒤쪽 선분은 클리핑한다.
class SceneViewOverlay
{
public:
	// Scene 창이 렌더 이미지를 그린 직후 호출 (viewMin/viewMax: 화면 좌표)
	static void Begin(const ImVec2& viewMin, const ImVec2& viewMax, EditorCamera* camera);
	static void End();
	static bool IsActive() { return s_Active; }

	// Gizmo 가 쓰는 화면 영역: min/max 는 (0,0)~(폭,높이), offset 은 이미지 왼쪽 위 화면 좌표.
	// 오버레이가 활성화되지 않았으면 현재 ImGui 창의 콘텐츠 영역을 돌려준다.
	static void GetViewRect(ImVec2& regionMin, ImVec2& regionMax, ImVec2& offset)
	{
		if (s_Active)
		{
			regionMin = ImVec2(0, 0);
			regionMax = ImVec2(s_Max.x - s_Min.x, s_Max.y - s_Min.y);
			offset = s_Min;
			return;
		}
		ImVec2 wp = ImGui::GetWindowPos();
		regionMin = ImGui::GetWindowContentRegionMin();
		regionMax = ImGui::GetWindowContentRegionMax();
		offset = ImVec2(regionMin.x + wp.x, regionMin.y + wp.y);
	}

	// Unity 스타일 하늘/지평선 그라디언트 (이미지 뒤에 그린다)
	static void DrawBackground(const ImVec2& viewMin, const ImVec2& viewMax, EditorCamera* camera);

	static void DrawGrid();
	static void DrawLine(const XMFLOAT3& a, const XMFLOAT3& b, ImU32 color, float thickness = 1.0f);
	// 카메라 원점에서 far 사각형으로 모이는 피라미드 형태(Unity 카메라 기즈모)
	static void DrawFrustum(const XMMATRIX& worldMatrix, float nearZ, float farZ, float fovYDegrees, ImU32 color);
	// 광원 기즈모 (Unity 모양): 화면 크기가 일정한 해 아이콘 + 방향 표시(원 + 평행선 + 화살표). kind: 0 Directional, 1 Point, 2 Spot
	static void DrawLightGizmo(const XMFLOAT3& position, const XMFLOAT3& dir, int kind, bool selected);

	static bool Project(const XMFLOAT3& world, ImVec2& out);

private:
	static bool s_Active;
	static ImVec2 s_Min;
	static ImVec2 s_Max;
	static XMMATRIX s_ViewProj;
	static XMFLOAT3 s_CameraPos;
};
