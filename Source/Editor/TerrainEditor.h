#pragma once

class Terrain;
class TerrainData;
class EditorCamera;

// Unity Terrain Inspector 의 편집 도구.
//  - 도구 막대: Create Neighbor Terrains / Paint Terrain / Paint Trees / Paint Details / Terrain Settings
//  - Paint Terrain: Raise or Lower Terrain, Paint Texture, Set Height, Smooth Height (+ Paint Holes, Stamp Terrain 은 아직 없음)
//  - 브러시 모양 4개, Brush Size, Opacity. Scene 뷰에 지형 표면을 따라 브러시 원을 그린다.
namespace TerrainEditor
{
	enum class Tool { CreateNeighbor, PaintTerrain, PaintTrees, PaintDetails, Settings, Generate };
	enum class PaintTool { RaiseLower, PaintHoles, PaintTexture, SetHeight, SmoothHeight, StampTerrain };

	void DrawInspector(Terrain* terrain);

	// Scene 뷰 (SceneViewOverlay::Begin ~ End 사이에서 호출). 지형 편집 도구가 마우스를 쓰면 true → 선택/변환 핸들을 끈다.
	bool SceneGUI(EditorCamera* camera, const ImVec2& viewMin, const ImVec2& viewMax, bool viewHovered);

	// TerrainData 오브젝트 필드 (+ 선택 팝업, Create New). 바뀌면 true
	bool TerrainDataField(const char* label, std::string& path, const char* popupId);

	// 브러시 한 번 적용 (월드 위치, 경과 시간). 검사/스크립트용으로도 쓴다.
	void ApplyBrush(Terrain* terrain, PaintTool tool, const Vec3& worldPosition, float deltaTime, bool shift);

	// 나무: 프리셋으로 프로토타입 추가, 지형 전체에 무작위로 count 그루 (간격 = Tree Density). 놓은 수를 돌려준다
	void AddTreePrototype(TerrainData& data, int preset);
	int MassPlaceTrees(Terrain* terrain, int count);

	// 디테일 (풀·꽃·돌): 프리셋 파일(.detail)로 종류 추가 → 번호. 고른 종류의 밀도를 지형 전체에 value(0~1)로. 둘 다 Undo 기록
	int AddDetailPrototype(const std::shared_ptr<TerrainData>& data, const std::string& presetPath);
	void FillDetails(Terrain* terrain, int proto, float value);
	// 디테일 브러시 한 번 (검사·스크립트용): 고른 종류를 목표 밀도(0~1)로
	void PaintDetails(Terrain* terrain, int proto, const Vec3& worldPosition, float target, float deltaTime);

	// 현재 설정 (검사용)
	void SetTool(Tool tool);
	void SetPaintTool(PaintTool tool);
	void SetBrush(int shape, float size, float opacity);
	void SetTargetHeight(float meters);
	void SetSelectedLayer(int layer);
}
