#pragma once
#include "EditorWindow.h"

class Camera;

// Unity 의 Game 뷰.
//  툴바: [Game ▾][Display 1 ▾][해상도 ▾][Scale ━○━ 0.46x] ... [Play Focused ▾][🐞] ... [🔇][⌨][Stats][Gizmos ▾]
//  - 해상도: Free Aspect / 비율(16:9 등) / 고정 해상도(1920x1080, 1080x1920 등) + 사용자 추가(+)
//    고정 해상도는 그 크기로 그린 뒤 뷰에 맞춰 축소해 가운데 표시 (Scale 슬라이더·휠로 확대, 가운데 버튼 드래그로 이동)
//  - Display: 카메라의 Target Display 가 같은 카메라만 그린다 (없으면 "No cameras rendering")
//  - Play Focused / Play Maximized / Play Unfocused: Play 시작 때 이 창을 앞으로 / 도킹 영역 전체로 / 그대로
//  - Stats: Unity 의 Statistics 창 (FPS, CPU, Batches, Tris, Verts, Screen, SetPass, 그림자, 스킨 메시, 애니메이션)
// 설정은 <프로젝트>/UserSettings/GameView.json 에 저장한다.
class GameViewEditorWindow
	: public EditorWindow
{
public:
	GameViewEditorWindow();
	~GameViewEditorWindow();

	virtual void OnRender() override;

	// EditorGUIManager 가 부른다
	static void OnPlayModeChanged(bool playing);
	static void SetDockRect(const ImVec2& min, const ImVec2& max);
	static void DrawMaximized();   // Play Maximized 중이면 도킹 영역 전체를 덮어 그린다

private:
	void DrawContent();
	void DrawToolbar(float width);
	void DrawView(ImVec2 pos, ImVec2 size);
	void DrawStats(ImVec2 viewMin, ImVec2 viewMax);
	void DrawSizeMenu();
	void EnsureTarget(UINT width, UINT height, Camera* camera);
	void RenderScene(Camera* camera);

	ComPtr<ID3D11Texture2D> m_Texture;
	ComPtr<ID3D11RenderTargetView> m_RTV;
	ComPtr<ID3D11ShaderResourceView> m_SRV;
	UINT m_Width = 0, m_Height = 0;
	Camera* m_LastCamera = nullptr;
	ImVec2 m_Pan = ImVec2(0, 0);   // 확대했을 때 이동량 (화면 픽셀)
	float m_LastFitScale = 1.0f;
};
