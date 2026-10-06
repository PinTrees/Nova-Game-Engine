#pragma once
#include "EditorWindow.h"
#include "EditorExtensions.h"
#include <string>
#include <vector>

class Tilemap;
class GameObject;

// Unity 의 Tile Palette (Window > Tile Palette) 상태 — 창 · Scene 뷰 붓 · CLI 가 같이 쓴다
//  - 팔레트 = .tile 이 들어 있는 폴더 (Create New Palette = Assets/Palettes/<이름>). 그림을 끌어 놓으면 잘라 놓은 스프라이트마다 .tile 을 만든다
//  - 도구: Brush (B) · Box Fill (U) · Picker (I) · Eraser (D) · Flood Fill (G). [ ] = 반시계 · 시계 회전, Shift+[ ] = X · Y 뒤집기
//    Brush 에서 Shift = 지우기, Ctrl = 고르기 (Unity 와 같다). Esc · Q/W/E/R/T/Y = 도구 끄기
//  - Active Tilemap: Hierarchy 에서 Tilemap 을 고르면 그것, 아니면 마지막으로 칠한 · 첫 Tilemap
namespace TilePalette
{
	enum class Tool { None, Brush, Box, Picker, Eraser, Fill };
	Tool CurrentTool();
	void SetTool(Tool tool);
	const char* ToolName(Tool tool);
	bool ParseTool(const std::string& name, Tool& out);

	const std::string& Folder();          // 지금 팔레트 폴더 (프로젝트 기준)
	void SetFolder(const std::string& folder);
	std::vector<std::string> Palettes();  // .tile 이 있는 폴더 + Assets/Palettes 의 폴더
	bool CreatePalette(const std::string& name, std::string& folderOut, std::string& error);
	// 그림 (잘라 놓았으면 스프라이트마다) → 폴더에 .tile. 만든 · 이미 있던 타일 경로
	std::vector<std::string> TilesFromTexture(const std::string& texture, const std::string& folder, std::string& error);

	const std::string& SelectedTile();
	void SelectTile(const std::string& tile);
	uint8 BrushFlags();
	void SetBrushFlags(uint8 flags);
	void Rotate(bool clockwise);
	void Flip(bool x);

	Tilemap* ActiveTilemap();
	void SetActiveTilemap(Tilemap* map);
	std::vector<Tilemap*> SceneTilemaps();

	// 칠하기 (Scene 뷰 붓 · CLI 의 paint) — 칸 하나에 지금 도구. 바뀐 칸 수
	int ApplyAt(Tilemap* map, int x, int y, Tool tool);

	bool SceneGUI(const EditorExtensions::SceneViewContext& ctx);
	// CLI 검사용: 마지막 Scene 뷰 프레임의 마우스 칸 (없으면 false)
	bool HoverCell(int& x, int& y);
	// CLI 검사용: 칸 가운데의 화면 좌표 (마지막 Scene 뷰 프레임의 카메라). Scene 뷰가 없으면 false
	bool CellToScreen(Tilemap* map, int x, int y, float& sx, float& sy);
}

class TilePaletteWindow : public EditorWindow
{
public:
	TilePaletteWindow();
	~TilePaletteWindow();
	static TilePaletteWindow* Instance() { return s_Instance; }
	static void Focus();

protected:
	void BeforeBegin() override;
	void OnRender() override;

private:
	static inline TilePaletteWindow* s_Instance = nullptr;
	char m_NewName[64] = "New Palette";
	std::vector<std::string> m_Tiles;
	unsigned long long m_ListedAt = 0;
	std::string m_ListedFolder;
	float m_CellSize = 48.0f;
	void ToolRow();
	void TileGrid();
};
