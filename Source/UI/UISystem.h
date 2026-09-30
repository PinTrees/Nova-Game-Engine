#pragma once
#include <string>

class Scene;
class GameObject;

// Unity UGUI 의 실행부 (CanvasUpdateRegistry + EventSystem + 그리기).
//  - Update(): 매 프레임. 캔버스 레이아웃(RectTransform → Transform), Play 중 마우스 입력(버튼 Hover/Press/Click), 버튼 색
//  - RenderGameView(): Game 뷰 렌더 타깃 위에 Screen Space - Overlay 캔버스를 그린다 (Sort Order 순)
//  - RenderSceneView(): Scene 뷰에 캔버스를 월드(1 픽셀 = 1 단위)로 그리고 캔버스/선택 요소 테두리
//  - Create(): GameObject > UI 메뉴 (Canvas 가 없으면 Canvas + EventSystem 을 함께 만든다)
namespace UISystem
{
	void Update();
	void RenderGameView(ID3D11RenderTargetView* rtv, UINT width, UINT height, int display);
	void RenderSceneView(ID3D11RenderTargetView* rtv, UINT width, UINT height, const Matrix& view, const Matrix& proj, const Vec3& cameraPosition);

	// kind: "Canvas", "EventSystem", "Image", "Text", "Button", "Panel"
	GameObject* Create(const std::string& kind, Scene* scene, GameObject* selected);
	// UI 컴포넌트가 있는데 RectTransform 이 없으면 붙인다 (Transform 바로 다음)
	void EnsureRectTransform(GameObject* go);
	// 스크립트가 UI 컴포넌트를 붙일 때: RectTransform 도 같은 프레임 대기열에 (바로 GetComponent 할 수 있게)
	void QueueRectTransformFor(GameObject* go, Component* added);
	int LastDrawCalls();
}
