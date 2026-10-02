#pragma once
#include "UISelectable.h"
#include "UIEventList.h"

// Unity 의 Dropdown (UI) — TMP_Dropdown 도 이것.
//  - 누르면 Template(꺼 둔 자식)을 복제해 "Dropdown List" 를 캔버스 맨 위에 띄운다: 옵션마다 Item 을 복제해 글자 · 체크 표시
//  - 항목을 누르면 Value 가 바뀌고 닫힌다, 목록 밖을 누르면 닫힌다 (뒤에 투명한 Blocker)
//  - Caption Text = 고른 옵션 글자, Options (글자 + 이미지), On Value Changed (Int32)
// 항목 · Blocker 는 Toggle 로 만들어져 있고, 눌려서 isOn 이 바뀐 것을 보고 고른다 (Unity Template 구조 그대로).
class Dropdown : public UISelectable
{
public:
	struct Option { std::string Text; std::string Image; };
	std::vector<Option> Options;

	Dropdown();
	virtual ~Dropdown();

	int GetValue() const { return m_Value; }
	void SetValue(int v, bool notify = true);
	void Show();
	void Hide();
	bool IsExpanded() const { return m_ListID != 0; }
	void RefreshShownValue();
	void SetParts(uint64 templ, uint64 caption, uint64 itemText) { m_Template = templ; m_CaptionText = caption; m_ItemText = itemText; }

	virtual void OnClick() override;
	virtual void UpdateBeforeLayout(float dt, bool playing) override;
	virtual void RemapFileIDs(const std::unordered_map<uint64, uint64>& map) override;

	virtual void OnInspectorGUI() override;
	virtual bool UsesUnityInspector() const override { return true; }
	virtual const char* InspectorIconName() const override { return "ui_button"; }

	GENERATE_COMPONENT_BODY(Dropdown)

private:
	uint64 m_Template = 0, m_CaptionText = 0, m_CaptionImage = 0, m_ItemText = 0, m_ItemImage = 0;
	int m_Value = 0;
	UIEventList m_OnValueChanged;

	// 열린 목록 (Play 중에만, 저장하지 않음)
	uint64 m_ListID = 0, m_BlockerID = 0;
	std::vector<uint64> m_ItemToggles;   // 항목마다 Toggle 이 있는 GameObject
	std::vector<bool> m_ItemStates;      // 띄울 때의 isOn (바뀌면 = 눌렸다)
	bool m_BlockerState = false;
};
REGISTER_COMPONENT(Dropdown)
