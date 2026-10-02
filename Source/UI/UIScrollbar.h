#pragma once
#include "UISelectable.h"
#include "UIEventList.h"

// Unity 의 Scrollbar (UI): Handle 의 기준점을 Value(0..1) · Size(0..1) 로 정한다 (Handle 의 부모 = 움직일 수 있는 범위).
//  - 손잡이를 끌면 따라오고, 손잡이 밖(트랙)을 누르면 그쪽으로 Size 만큼 넘어간다
//  - Number Of Steps (0 = 연속), Direction (Left To Right / Right To Left / Bottom To Top / Top To Bottom)
//  - Scroll Rect 의 Horizontal / Vertical Scrollbar 로 넣으면 서로 따라간다 (Size = 보이는 비율)
//  - On Value Changed (Single)
class Scrollbar : public UISelectable, public IUIDragHandler
{
public:
	enum class Direction { LeftToRight = 0, RightToLeft = 1, BottomToTop = 2, TopToBottom = 3 };

	Scrollbar();

	float GetValue() const { return m_Value; }
	void SetValue(float v, bool notify = true);
	float GetSize() const { return m_Size; }
	void SetSize(float s) { m_Size = std::clamp(s, 0.0f, 1.0f); }
	int GetNumberOfSteps() const { return m_Steps; }
	void SetNumberOfSteps(int n) { m_Steps = (std::max)(0, n); SetValue(m_Value, false); }
	Direction GetDirection() const { return m_Direction; }
	void SetDirection(Direction d) { m_Direction = d; }
	void SetHandle(uint64 handle) { m_HandleRect = handle; }
	bool Vertical() const { return m_Direction == Direction::BottomToTop || m_Direction == Direction::TopToBottom; }

	virtual void OnPointerDown(const Vec3& point) override;
	virtual void OnBeginDrag(const Vec3& point) override {}
	virtual void OnDrag(const Vec3& point, const Vec3& delta) override;
	virtual bool DragsImmediately() const override { return true; }
	virtual void UpdateBeforeLayout(float dt, bool playing) override;
	virtual void RemapFileIDs(const std::unordered_map<uint64, uint64>& map) override;

	virtual void OnInspectorGUI() override;
	virtual bool UsesUnityInspector() const override { return true; }
	virtual const char* InspectorIconName() const override { return "ui_slider"; }

	GENERATE_COMPONENT_BODY(Scrollbar)

private:
	bool Reversed() const { return m_Direction == Direction::RightToLeft || m_Direction == Direction::TopToBottom; }
	class RectTransform* Container();
	bool LocalInContainer(const Vec3& point, Vec2& local);

	uint64 m_HandleRect = 0;
	Direction m_Direction = Direction::LeftToRight;
	float m_Value = 0.0f, m_Size = 0.2f;
	int m_Steps = 0;
	UIEventList m_OnValueChanged;
	float m_DragOffset = 0.0f;   // 손잡이를 잡은 자리 (손잡이 가운데 기준, 컨테이너 로컬)
	bool m_DraggingHandle = false;
};
REGISTER_COMPONENT(Scrollbar)
