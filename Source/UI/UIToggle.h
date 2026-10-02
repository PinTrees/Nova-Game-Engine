#pragma once
#include "UISelectable.h"
#include "UIEventList.h"

// Unity 의 Toggle (UI): 누르면 isOn 이 바뀌고 Graphic(체크 표시)이 보이거나 사라진다.
//  - Toggle Transition: None(바로) / Fade(0.1초)
//  - On Value Changed (Boolean): Dynamic 이면 새 isOn 이 인자로
class Toggle : public UISelectable
{
public:
	Toggle();

	bool IsOn() const { return m_IsOn; }
	void SetIsOn(bool on, bool notify = true);
	void SetGraphic(uint64 fileID) { m_Graphic = fileID; }

	virtual void OnClick() override;
	virtual void UpdateBeforeLayout(float dt, bool playing) override;

	virtual void OnInspectorGUI() override;
	virtual bool UsesUnityInspector() const override { return true; }
	virtual const char* InspectorIconName() const override { return "ui_toggle"; }

	GENERATE_COMPONENT_BODY(Toggle)

private:
	bool m_IsOn = true;
	int m_ToggleTransition = 1;   // 0 None, 1 Fade
	uint64 m_Graphic = 0;         // 체크 표시 GameObject
public:
	virtual void RemapFileIDs(const std::unordered_map<uint64, uint64>& map) override;
private:
	uint64 m_Group = 0;
	UIEventList m_OnValueChanged;
	float m_Alpha = 1.0f;         // 실행 중 체크 표시 투명도
};
REGISTER_COMPONENT(Toggle)
