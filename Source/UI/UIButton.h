#pragma once
#include "UISelectable.h"
#include "UIEventList.h"

// Unity 의 Button (UI): Selectable + On Click ()
//  - On Click (): Inspector 에서 지정한 호출 (대상 GameObject + C# public 메서드 / GameObject.SetActive)
//    그다음 C# 의 button.onClick.AddListener(...) 로 등록한 것 (Unity 와 같은 순서)
class Button : public UISelectable
{
public:
	Button();

	virtual void OnClick() override;
	const UIEventList& GetOnClick() const { return m_OnClick; }
	void AddOnClick(const UIPersistentCall& call) { m_OnClick.Calls.push_back(call); }

	virtual void OnInspectorGUI() override;
	virtual bool UsesUnityInspector() const override { return true; }
	virtual const char* InspectorIconName() const override { return "ui_button"; }

	GENERATE_COMPONENT_BODY(Button)

private:
	UIEventList m_OnClick;
};
REGISTER_COMPONENT(Button)
