#pragma once
#include <string>
#include <vector>

class GameObject;

// Unity 의 UnityEvent 에 Inspector 로 넣는 호출 목록 (On Click (), On Value Changed (Single) 등).
// 호출 = 대상 GameObject + "클래스.메서드" (또는 GameObject.SetActive), 인자는
//  - Static: Inspector 에 적은 값
//  - Dynamic: 이벤트가 넘기는 값 (Slider 의 값, Toggle 의 isOn, InputField 의 글자) — 메서드 인자 형식이 같을 때
struct UIPersistentCall
{
	uint64 Target = 0;
	std::string Method;
	std::string ParamType;   // "", "int", "float", "string", "bool"
	std::string Argument;
	int CallState = 2;       // 0 Off, 1 Editor And Runtime, 2 Runtime Only
	bool Dynamic = false;
};

class UIEventList
{
public:
	std::vector<UIPersistentCall> Calls;

	// dynamicValue = 이벤트 값을 문자열로 (float/bool/string)
	void Invoke(GameObject* owner, const char* eventName, const std::string& dynamicValue = std::string()) const;
	// Inspector: title = "On Click ()", dynamicType = "" / "float" / "bool" / "string"
	void Draw(const char* title, const char* dynamicType);

	nlohmann::json ToJson() const;
	void FromJson(const nlohmann::json& j);
};
