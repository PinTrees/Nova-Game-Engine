#pragma once
#include <string>
#include <nlohmann/json.hpp>
#include "ScriptEngine.h"

// C# 직렬화 필드 Inspector (MonoBehaviour 컴포넌트 · ScriptableObject 에셋이 같이 쓴다).
//  값은 JSON (C# Bridge 의 WriteValue 와 같은 모양): 숫자 · 글자 · [x,y,z] · fileID · 에셋 경로 · 배열 · 객체 (Serializable)
namespace ScriptFieldGUI
{
	// 필드 하나 (Header · Space · Tooltip 포함). key = 고르기 창을 구분하는 값 (컴포넌트 · 에셋마다 다르게). 바꿨으면 true
	bool Field(const ScriptEngine::FieldInfo& f, nlohmann::json& value, const std::string& key, int indent = 0);
	// 필드 목록: values 의 각 이름 (없으면 기본값으로 그린다). 바뀐 필드는 values 에 쓴다. 바뀐 필드 이름들 (없으면 빈 목록)
	std::vector<std::string> Fields(const std::vector<ScriptEngine::FieldInfo>& fields, nlohmann::json& values, const std::string& key, int indent = 0);
}
