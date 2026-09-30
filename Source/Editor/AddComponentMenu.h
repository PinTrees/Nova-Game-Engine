#pragma once
#include <functional>
#include <memory>

class GameObject;
class Component;

// Unity 의 Add Component 팝업: 위쪽 검색창, "Component" 헤더, 카테고리 목록(>)과 하위 목록.
// 검색어를 입력하면 모든 카테고리에서 이름으로 찾는다. ↑/↓ 이동, Enter 추가, ← 또는 Backspace(빈 검색어)로 뒤로.
namespace AddComponentMenu
{
	// "Add Component" 버튼 바로 아래에 팝업을 연다 (버튼 영역 min/max 를 넘긴다)
	void Open(const ImVec2& buttonMin, const ImVec2& buttonMax);

	// (개발/검증용) 팝업을 연 뒤 카테고리로 들어가거나("Physics") 검색어("?col")를 넣는다
	void DevPreset(const char* preset);

	// 매 프레임 호출. 컴포넌트를 고르면 onAdd 로 새 컴포넌트를 넘긴다.
	void Draw(GameObject* target, const std::function<void(std::shared_ptr<Component>)>& onAdd);
}
