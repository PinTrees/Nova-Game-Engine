#pragma once
#include "Component.h"

class UIGraphic;

// Unity 의 Button (UI).
//  - Interactable, Transition = Color Tint (Target Graphic 의 색에 상태 색을 곱함: Normal / Highlighted / Pressed / Selected / Disabled)
//  - On Click (): Inspector 에서 지정한 호출 목록 (대상 GameObject + C# 스크립트의 public 메서드, 또는 GameObject.SetActive)
//    + C# 의 button.onClick.AddListener(...) 로 등록한 것
// 입력은 UISystem 이 Game 뷰 마우스로 판정해 SetPointer / Click 을 부른다.
class Button : public Component
{
public:
	enum class Transition { None = 0, ColorTint = 1, SpriteSwap = 2, Animation = 3 };
	enum class State { Normal, Highlighted, Pressed, Selected, Disabled };

	struct PersistentCall
	{
		uint64 Target = 0;          // 대상 GameObject fileID
		std::string Method;         // "클래스.메서드" 또는 "GameObject.SetActive"
		std::string ParamType;      // "", "int", "float", "string", "bool"
		std::string Argument;       // 인자 (문자열로 저장)
		int CallState = 2;          // 0 Off, 1 Editor And Runtime, 2 Runtime Only
	};

	Button();
	virtual ~Button();

	bool IsInteractable() const { return m_Interactable; }
	void SetInteractable(bool v) { m_Interactable = v; }
	// 입력 (UISystem): 마우스가 위에 있는지, 누르고 있는지
	void SetPointer(bool hovered, bool pressed);
	// 클릭: 등록된 호출 실행 (Inspector 목록 → C# 리스너)
	void Click();
	// 매 프레임: 상태 색을 Target Graphic 에 (Fade Duration 동안 부드럽게)
	void UpdateVisual(float dt, bool playing);
	UIGraphic* GetTargetGraphic();
	const std::vector<PersistentCall>& GetOnClick() const { return m_OnClick; }
	void AddOnClick(const PersistentCall& call) { m_OnClick.push_back(call); }

	static const std::vector<Button*>& All() { return s_All; }

	virtual void OnInspectorGUI() override;
	virtual bool UsesUnityInspector() const override { return true; }
	virtual const char* InspectorIconName() const override { return "ui_button"; }

	GENERATE_COMPONENT_BODY(Button)

private:
	void DrawOnClickList();

	bool m_Interactable = true;
	Transition m_Transition = Transition::ColorTint;
	uint64 m_TargetGraphic = 0;   // 0 = 이 GameObject 의 Graphic
	float m_Normal[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	float m_Highlighted[4] = { 0.961f, 0.961f, 0.961f, 1.0f };
	float m_Pressed[4] = { 0.784f, 0.784f, 0.784f, 1.0f };
	float m_Selected[4] = { 0.961f, 0.961f, 0.961f, 1.0f };
	float m_Disabled[4] = { 0.784f, 0.784f, 0.784f, 0.502f };
	float m_ColorMultiplier = 1.0f;
	float m_FadeDuration = 0.1f;
	int m_Navigation = 1;   // Automatic
	std::vector<PersistentCall> m_OnClick;

	// 실행 중
	bool m_Hovered = false, m_Down = false;
	float m_Current[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	static std::vector<Button*> s_All;
};
REGISTER_COMPONENT(Button)
