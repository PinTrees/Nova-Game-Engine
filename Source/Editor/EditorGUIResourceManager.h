#pragma once
#include "EditorGUIStyle.h"

struct FontLoadContainer
{
	string FontKey;
	EditorTextStyle FontStyle;
};

class EditorGUIResourceManager
{
	SINGLE_HEADER(EditorGUIResourceManager)

public:
	// 로드된 폰트들을 관리할 맵 (폰트 이름 -> ImFont* 맵핑)
	unordered_map<string, ImFont*> m_FontMap;
	unordered_map<string, bool> m_LoadFontTask;

	vector<FontLoadContainer> loadedAA;

public:
	void Init();

	ImFont* LoadFont(EditorTextStyle style);

private: 
	void LoadFontAsync(FontLoadContainer container);
};

