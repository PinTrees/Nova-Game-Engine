#pragma once
#include <string>

class Scene;
class GameObject;

// Unity UGUI 의 실행부 (CanvasUpdateRegistry + EventSystem + 그리기).
//  - Update(): 매 프레임. 캔버스 레이아웃(RectTransform → Transform), Play 중 마우스 입력(버튼 Hover/Press/Click), 버튼 색
//  - RenderGameView(): Game 뷰 렌더 타깃 위에 World / Camera 캔버스(게임 카메라 + 씬 깊이) → Screen Space - Overlay 캔버스 (Sort Order 순)
//  - RenderSceneView(): Scene 뷰에 캔버스를 월드(1 픽셀 = 1 단위)로 그리고 캔버스/선택 요소 테두리
//  - Create(): GameObject > UI 메뉴 (Canvas 가 없으면 Canvas + EventSystem 을 함께 만든다)
namespace UISystem
{
	void Update();
	// 게임 화면 점(픽셀, 왼쪽 아래 0,0) 에 있는 맨 위 UI 그래픽 (Overlay 먼저, World / Camera 캔버스는 마우스 광선으로). 없으면 nullptr
	GameObject* RaycastScreen(float x, float y);
	// camera / depth = 이 화면을 그린 게임 카메라와 씬 깊이 (World · Camera 캔버스, 없으면 그리지 않음 / 가리지 않음)
	void RenderGameView(GfxRenderTargetView* rtv, UINT width, UINT height, int display, class Camera* camera = nullptr, GfxDepthStencilView* depth = nullptr);
	void RenderSceneView(GfxRenderTargetView* rtv, UINT width, UINT height, const Matrix& view, const Matrix& proj, const Vec3& cameraPosition);
	// 캔버스를 화면 width x height 로 다시 레이아웃 (Game 뷰가 아닌 크기로 그릴 때 — 비교 기준 그림 등). 0 x 0 = Game 뷰 크기로 되돌림
	void LayoutForScreen(UINT width, UINT height);

	// kind: "Canvas", "EventSystem", "Image", "Text", "Button", "Panel", "Toggle", "Slider", "InputField", "ScrollView"
	GameObject* Create(const std::string& kind, Scene* scene, GameObject* selected);
	// 선택(키보드 포커스)된 UI 를 푼다 (InputField 의 Enter/Esc)
	void ClearSelection();
	void OnSceneUnloading();   // Play 중 씬 교체 직전 (선택·포인터 대상 해제)
	// UI 컴포넌트가 있는데 RectTransform 이 없으면 붙인다 (Transform 바로 다음)
	void EnsureRectTransform(GameObject* go);
	// 스크립트가 UI 컴포넌트를 붙일 때: RectTransform 도 같은 프레임 대기열에 (바로 GetComponent 할 수 있게)
	void QueueRectTransformFor(GameObject* go, Component* added);
	int LastDrawCalls();
}
