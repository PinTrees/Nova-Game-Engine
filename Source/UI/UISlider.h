#pragma once
#include "UISelectable.h"
#include "UIEventList.h"

// Unity 의 Slider (UI): 값(Min..Max)에 따라 Fill Rect 의 기준점을 늘이고 Handle Rect 를 옮긴다.
//  - 누르거나 끌면 마우스 위치로 값이 바뀐다 (Handle 의 부모 사각형 = 움직일 수 있는 범위)
//  - Direction: Left To Right / Right To Left / Bottom To Top / Top To Bottom, Whole Numbers
//  - On Value Changed (Single): Dynamic 이면 새 값이 인자로 (체력 바, 볼륨 조절)
class Slider : public UISelectable, public IUIDragHandler
{
public:
	enum class Direction { LeftToRight = 0, RightToLeft = 1, BottomToTop = 2, TopToBottom = 3 };

	Slider();

	float GetValue() const { return m_Value; }
	void SetValue(float value, bool notify = true);
	float GetMin() const { return m_Min; }
	float GetMax() const { return m_Max; }
	void SetMin(float v) { m_Min = v; SetValue(m_Value, false); }
	void SetMax(float v) { m_Max = v; SetValue(m_Value, false); }
	bool GetWholeNumbers() const { return m_WholeNumbers; }
	void SetWholeNumbers(bool v) { m_WholeNumbers = v; SetValue(m_Value, false); }
	float GetNormalized() const;
	void SetNormalized(float t, bool notify = true);
	void SetRects(uint64 fill, uint64 handle) { m_FillRect = fill; m_HandleRect = handle; }

	virtual void OnPointerDown(const Vec2& point) override;
	virtual void OnDrag(const Vec2& point, const Vec2& delta) override;
	virtual bool DragsImmediately() const override { return true; }
	virtual void UpdateBeforeLayout(float dt, bool playing) override;

	virtual void OnInspectorGUI() override;
	virtual bool UsesUnityInspector() const override { return true; }
	virtual const char* InspectorIconName() const override { return "ui_slider"; }

	GENERATE_COMPONENT_BODY(Slider)

private:
	void SetFromPoint(const Vec2& point);
	bool Reversed() const { return m_Direction == Direction::RightToLeft || m_Direction == Direction::TopToBottom; }
	bool Vertical() const { return m_Direction == Direction::BottomToTop || m_Direction == Direction::TopToBottom; }

	uint64 m_FillRect = 0, m_HandleRect = 0;
	Direction m_Direction = Direction::LeftToRight;
	float m_Min = 0.0f, m_Max = 1.0f;
	bool m_WholeNumbers = false;
	float m_Value = 0.0f;
	UIEventList m_OnValueChanged;
};
REGISTER_COMPONENT(Slider)
