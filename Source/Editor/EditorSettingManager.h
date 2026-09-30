#pragma once
#include <nlohmann/json.hpp>
#include "Utils.h"

using json = nlohmann::json;

class EditorSetting
{
public:
	wstring LastOpenedScenePath;

    friend void to_json(json& j, const EditorSetting& setting);
    friend void from_json(const json& j, EditorSetting& setting);
};

class EditorSettingManager
{
private:
	static EditorSetting* m_pSetting;

public:
    static void Init();
	static void Load();
	static void Save();

	static void SetLastOpenedScenePath(wstring scenePath);
    static EditorSetting* GetSetting() { return m_pSetting; }
};

