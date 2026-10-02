#pragma once
#include "Component.h"

class UIGraphic;

// 끌기(드래그)를 받는 UI (Slider, ScrollRect). 좌표 = 캔버스 월드(화면 픽셀, y 위)
class IUIDragHandler
{
public:
	virtual ~IUIDragHandler() = default;
	// 점 = 캔버스 월드 좌표 (Overlay = 화면 픽셀, World / Camera 캔버스 = 마우스 광선이 캔버스 평면과 만나는 3D 점)
	virtual void OnBeginDrag(const Vec3& point) {}
	virtual void OnDrag(const Vec3& point, const Vec3& delta) = 0;
	virtual void OnEndDrag(const Vec3& point) {}
	// Slider 처럼 누르는 순간부터 끄는 것인지 (ScrollRect 는 조금 움직인 뒤부터 = 버튼 클릭 취소)
	virtual bool DragsImmediately() const { return false; }
};

// 마우스 휠을 받는 UI (ScrollRect)
class IUIScrollHandler
{
public:
	virtual ~IUIScrollHandler() = default;
	virtual void OnScroll(float wheel) = 0;
};

// Unity 의 Selectable: 상호작용 가능한 UI 의 공통 부모 (Button, Toggle, Slider, InputField).
//  - Interactable, Transition = Color Tint (Target Graphic 에 상태 색을 곱함), Navigation
//  - 입력은 UISystem 이 판정해 SetPointer / OnPointerDown / OnClick / OnSelect ... 를 부른다
class UISelectable : public Component
{
public:
	enum class Transition { None = 0, ColorTint = 1, SpriteSwap = 2, Animation = 3 };

	UISelectable();
	virtual ~UISelectable();

	bool IsInteractable() const { return m_Interactable; }
	void SetInteractable(bool v) { m_Interactable = v; }
	void SetPointer(bool hovered, bool pressed) { m_Hovered = hovered; m_Down = pressed; }
	bool IsSelected() const { return m_Selected; }

	virtual void OnPointerDown(const Vec3& point) {}
	virtual void OnClick() {}
	virtual void OnSelect() { m_Selected = true; }
	virtual void OnDeselect() { m_Selected = false; }
	// 선택된 동안 매 프레임 (InputField 의 키보드 입력)
	virtual void OnUpdateSelected() {}
	// 레이아웃 전에 (Slider 의 Fill/Handle 기준점, Toggle 의 체크 표시)
	virtual void UpdateBeforeLayout(float dt, bool playing) {}

	// 매 프레임: 상태 색을 Target Graphic 에
	void UpdateVisual(float dt, bool playing);
	UIGraphic* GetTargetGraphic();

	static const std::vector<UISelectable*>& All() { return s_All; }

protected:
	void DrawSelectableInspector();
	void SelectableToJson(json& j) const;
	void SelectableFromJson(const json& j);
	static GameObject* FindObject(uint64 fileID);

	bool m_Interactable = true;
	Transition m_Transition = Transition::ColorTint;
	uint64 m_TargetGraphic = 0;   // 0 = 이 GameObject 의 Graphic
	float m_Normal[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	float m_Highlighted[4] = { 0.961f, 0.961f, 0.961f, 1.0f };
	float m_Pressed[4] = { 0.784f, 0.784f, 0.784f, 1.0f };
	float m_SelectedColor[4] = { 0.961f, 0.961f, 0.961f, 1.0f };
	float m_Disabled[4] = { 0.784f, 0.784f, 0.784f, 0.502f };
	float m_ColorMultiplier = 1.0f;
	float m_FadeDuration = 0.1f;
	int m_Navigation = 1;   // Automatic

	bool m_Hovered = false, m_Down = false, m_Selected = false;
	float m_Current[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

private:
	static std::vector<UISelectable*> s_All;
};
