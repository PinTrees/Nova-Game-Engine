#pragma once
#include <string>

// Unity 의 EditorPrefs: 사용자별 에디터 설정 (프로젝트와 무관).
// %LOCALAPPDATA%/NOVA/Editor/EditorPrefs.json 에 저장한다. 값을 바꾸면 바로 파일에 쓴다.
namespace EditorPrefs
{
	std::string GetString(const std::string& key, const std::string& defaultValue = std::string());
	int GetInt(const std::string& key, int defaultValue = 0);
	float GetFloat(const std::string& key, float defaultValue = 0.0f);
	bool GetBool(const std::string& key, bool defaultValue = false);
	void SetString(const std::string& key, const std::string& value);
	void SetInt(const std::string& key, int value);
	void SetFloat(const std::string& key, float value);
	void SetBool(const std::string& key, bool value);
	bool HasKey(const std::string& key);
	void DeleteKey(const std::string& key);
}
