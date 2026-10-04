#pragma once
#include <windows.h>
#include <filesystem>
#include <string>
#include <vector>

// Download service only. The existing HubApp remains the sole Hub UI.
class HubEngineInstaller
{
public:
    struct Engine { std::string Version; std::wstring Root; bool Managed = true; };
    struct Release { std::string Version; long long Size = 0; };
    // Android 빌드 지원 모듈 (OpenJDK · Android SDK · NDK) — 모든 엔진 버전이 같이 쓰는 공용 폴더
    struct AndroidPackage { std::string Id, Label; long long Size = 0; bool Installed = false; };
    struct AndroidState
    {
        bool Known = false;                 // 서비스에서 한 번이라도 상태를 받았다
        std::wstring Root;
        bool Installed = false;
        std::vector<std::string> Missing;   // 빠진 구성 요소 이름
        long long DownloadBytes = 0;        // 목록을 받은 뒤에만 (빠진 것만 합친 크기)
        std::string LicenseId, License;     // 목록을 받은 뒤에만
        std::vector<AndroidPackage> Packages;
    };
    HubEngineInstaller();
    ~HubEngineInstaller();
    bool Enabled() const { return !m_Service.empty(); }
    bool Busy() const { return m_Process != nullptr; }
    bool Installing() const { return Busy() && (m_Command == "install" || m_Command == "android-install"); }
    // acceptLicense: 사용자가 Hub 에서 Android SDK 라이선스에 직접 동의했을 때만 true (android-install)
    bool Start(const std::string& command, const std::string& version = {}, const std::wstring& root = {}, bool acceptLicense = false);
    void Poll();
    void Cancel();
    std::wstring EditorFor(const std::string& version) const;
    static std::filesystem::path StateRoot();
    const std::vector<Engine>& Engines() const { return m_Engines; }
    const std::vector<Release>& Releases() const { return m_Releases; }
    const AndroidState& Android() const { return m_Android; }
    const std::string& Command() const { return m_Command; }   // 마지막 (또는 진행 중) 요청
    const std::string& Message() const { return m_Message; }
    float Progress() const { return m_Progress; }
    bool Failed() const { return m_Failed; }
private:
    void LoadInstalled();
    std::wstring m_Service;
    std::filesystem::path m_Job;
    HANDLE m_Process = nullptr;
    std::string m_Command, m_Message;
    std::vector<Engine> m_Engines;
    std::vector<Release> m_Releases;
    AndroidState m_Android;
    float m_Progress = 0;
    bool m_Failed = false;
    ULONGLONG m_NextPoll = 0;
};
