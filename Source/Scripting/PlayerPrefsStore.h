#pragma once
#include <string>

// Unity 의 PlayerPrefs 저장소: 키 → 정수 · 실수 · 글자 (종류가 다르면 없는 것으로 — Unity 와 같음).
//  - 에디터: <프로젝트>/Library/PlayerPrefs.json (Play 를 멈춰도 남는다, 빌드한 게임과 따로 — Unity 도 따로)
//  - Windows 게임: persistentDataPath/PlayerPrefs.json, 안드로이드: 앱 파일 폴더, 웹: 브라우저 localStorage (새로 고쳐도 남는다)
//  - 바뀌면 프레임 끝에 쓴다 (Flush) — 앱이 갑자기 꺼져도 잃지 않게. Save() 는 바로
namespace PlayerPrefsStore
{
	bool Has(const std::string& key);
	void SetInt(const std::string& key, int value);
	int GetInt(const std::string& key, int defaultValue);
	void SetFloat(const std::string& key, float value);
	float GetFloat(const std::string& key, float defaultValue);
	void SetString(const std::string& key, const std::string& value);
	bool GetString(const std::string& key, std::string& out);
	void Delete(const std::string& key);
	void DeleteAll();
	void Save();
	void Flush();   // 바뀐 것이 있으면 쓰기 (프레임 끝)

	// Unity 의 Application.persistentDataPath: Windows = %USERPROFILE%\AppData\LocalLow\<회사>\<제품>, 안드로이드 = 앱 파일 폴더, 웹 = /persistent (메모리)
	std::string PersistentDataPath();
}
