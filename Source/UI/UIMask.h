#pragma once
#include "Component.h"
#include "UISelectable.h"

// Unity 의 Mask: 자식 UI 를 이 요소의 사각형 안으로 잘라 그린다 (이 엔진은 사각형 잘라내기, 스프라이트 모양 마스크는 아님).
// Show Mask Graphic 을 끄면 이 요소의 Image 는 보이지 않고 잘라내기만 한다.
class Mask : public Component
{
public:
	Mask();
	bool ShowMaskGraphic() const { return m_ShowMaskGraphic; }
	virtual void OnInspectorGUI() override;
	virtual bool UsesUnityInspector() const override { return true; }
	virtual const char* InspectorIconName() const override { return "ui_mask"; }
	GENERATE_COMPONENT_BODY(Mask)
private:
	bool m_ShowMaskGraphic = true;
};
REGISTER_COMPONENT(Mask)

// Unity 의 RectMask2D: 자식 UI 를 사각형 안으로 잘라 그린다 (Graphic 없이도 됨, Padding)
class RectMask2D : public Component
{
public:
	RectMask2D();
	Vec4 GetPadding() const { return m_Padding; }   // 왼쪽, 아래, 오른쪽, 위
	virtual void OnInspectorGUI() override;
	virtual bool UsesUnityInspector() const override { return true; }
	virtual const char* InspectorIconName() const override { return "ui_mask"; }
	GENERATE_COMPONENT_BODY(RectMask2D)
private:
	Vec4 m_Padding = Vec4(0, 0, 0, 0);
};
REGISTER_COMPONENT(RectMask2D)

// Unity 의 Scroll Rect: Content 를 끌거나 마우스 휠로 움직여 Viewport 안에서 보이는 부분을 바꾼다.
//  - Horizontal / Vertical, Movement Type(Unrestricted / Elastic / Clamped), Elasticity, Inertia, Deceleration Rate, Scroll Sensitivity
class ScrollRect : public Component, public IUIDragHandler, public IUIScrollHandler
{
public:
	enum class Movement { Unrestricted = 0, Elastic = 1, Clamped = 2 };

	ScrollRect();
	virtual ~ScrollRect();

	void SetContent(uint64 content, uint64 viewport) { m_Content = content; m_Viewport = viewport; }
	Vec2 GetNormalizedPosition();
	void SetNormalizedPosition(const Vec2& p);

	virtual void OnBeginDrag(const Vec2& point) override;
	virtual void OnDrag(const Vec2& point, const Vec2& delta) override;
	virtual void OnEndDrag(const Vec2& point) override;
	virtual void OnScroll(float wheel) override;
	void UpdateBeforeLayout(float dt, bool playing);

	static const std::vector<ScrollRect*>& All() { return s_All; }

	virtual void OnInspectorGUI() override;
	virtual bool UsesUnityInspector() const override { return true; }
	virtual const char* InspectorIconName() const override { return "ui_scroll_rect"; }
	GENERATE_COMPONENT_BODY(ScrollRect)

private:
	class RectTransform* ContentRect();
	class RectTransform* ViewportRect();
	// Content 가 Viewport 밖으로 나간 양 (Content 부모 공간, 이만큼 움직이면 안으로 들어옴)
	Vec2 CalculateOffset(const Vec2& extraDelta);
	// 월드 이동량 → Content 부모 공간 이동량
	Vec2 WorldToContentParent(const Vec2& worldDelta);

	uint64 m_Content = 0, m_Viewport = 0;
	bool m_Horizontal = true, m_Vertical = true;
	Movement m_Movement = Movement::Elastic;
	float m_Elasticity = 0.1f;
	bool m_Inertia = true;
	float m_Deceleration = 0.135f;
	float m_Sensitivity = 1.0f;

	// 실행 중
	bool m_Dragging = false;
	Vec2 m_DragStartPointer = Vec2(0, 0), m_ContentStart = Vec2(0, 0);
	Vec2 m_Velocity = Vec2(0, 0), m_PrevPosition = Vec2(0, 0);
	static std::vector<ScrollRect*> s_All;
};
REGISTER_COMPONENT(ScrollRect)
