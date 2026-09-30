#pragma once

class Scene;
class GameObject;

// Unity 의 GameObject 생성 메뉴 (Hierarchy 우클릭 / [+] 버튼 / 상단 GameObject 메뉴가 공유한다).
namespace GameObjectMenu
{
	// Unity 컨텍스트 메뉴처럼 밝은 회색 배경의 팝업 스타일. BeginPopup 앞에서 Push, EndPopup 뒤에서 Pop.
	void PushContextStyle();
	void PopContextStyle();

	// 팝업 최소 폭 지정 (BeginPopup 직전에 호출)
	void SetMenuWidth(float width);

	// Create Empty, 2D Object, 3D Object, Effects, Light, Audio, Video, UI (Canvas), AI, UI Toolkit,
	// Rendering, Volume, Camera ... 항목. 엔진이 지원하지 않는 항목은 비활성으로 표시한다.
	// parent 가 nullptr 이면 씬 루트에 만든다. 새로 만든 오브젝트는 자동 선택된다.
	void DrawCreateItems(Scene* scene, GameObject* parent);
}
