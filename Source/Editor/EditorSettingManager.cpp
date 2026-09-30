#include "pch.h"
#include "EditorSettingManager.h"

void to_json(json& j, const EditorSetting& setting)
{
    j = json
    {
        { "LastOpenedScenePath", wstring_to_string(setting.LastOpenedScenePath) }
    };
}

void from_json(const json& j, EditorSetting& setting)
{
    setting.LastOpenedScenePath = string_to_wstring(j.at("LastOpenedScenePath").get<std::string>());
}

EditorSetting* EditorSettingManager::m_pSetting = nullptr;

void EditorSettingManager::Init()
{
    Load();
}

void EditorSettingManager::Load()
{
    //std::wstring settingsPath = Application::GetDataPath() + L"\\EditorSettings.json";
    std::wstring settingsPath = PathManager::GetI()->GetMovePathW(L"Assets\\EditorSettings.json");
    std::ifstream settingsFile(wstring_to_string(settingsPath));
    if (settingsFile.is_open())
    {
        json j;
        settingsFile >> j;
        m_pSetting = new EditorSetting(j.get<EditorSetting>());
        settingsFile.close();
    }
    else
    {
        m_pSetting = new EditorSetting;
    }
}

void EditorSettingManager::Save()
{
    //std::wstring settingsPath = Application::GetDataPath() + L"/EditorSettings.json";
    std::wstring settingsPath = PathManager::GetI()->GetMovePathW(L"Assets\\EditorSettings.json");

    json j = *m_pSetting;

    std::ofstream settingsFile(wstring_to_string(settingsPath));
    if (settingsFile.is_open()) {
        settingsFile << j.dump(4); 
        settingsFile.close();
    }
}

void EditorSettingManager::SetLastOpenedScenePath(wstring scenePath)
{
    m_pSetting->LastOpenedScenePath = scenePath;
    Save();
}