#include "pch.h"
#include "HubEngineInstaller.h"
#include <fstream>
#include <nlohmann/json.hpp>

namespace
{
    std::wstring Wide(const std::string& text)
    {
        int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0);
        std::wstring value(n, L'\0'); MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), value.data(), n); return value;
    }
    std::string Utf8(const std::wstring& text)
    {
        int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0, nullptr, nullptr);
        std::string value(n, '\0'); WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), value.data(), n, nullptr, nullptr); return value;
    }
    std::filesystem::path EnvPath(const wchar_t* name)
    {
        wchar_t value[32768] = {}; GetEnvironmentVariableW(name, value, 32768); return value;
    }
    nlohmann::json Read(const std::filesystem::path& file)
    {
        std::ifstream in(file); return nlohmann::json::parse(in, nullptr, false);
    }
    void ReadAndroid(HubEngineInstaller::AndroidState& android, const nlohmann::json& data)
    {
        // 상태는 매번 바뀐다. 라이선스 · 구성 요소 목록은 목록을 받은 요청에만 있으므로 없으면 앞의 것을 둔다
        android.Known = true;
        android.Root = Wide(data.value("root", std::string()));
        android.Installed = data.value("installed", false);
        android.Missing.clear();
        if (data.contains("missing") && data["missing"].is_array())
            for (const auto& name : data["missing"]) if (name.is_string()) android.Missing.push_back(name.get<std::string>());
        if (data.contains("license") && data["license"].is_string())
        {
            android.LicenseId = data.value("licenseId", std::string());
            android.License = data["license"].get<std::string>();
        }
        if (data.contains("packages") && data["packages"].is_array())
        {
            android.Packages.clear();
            for (const auto& item : data["packages"])
                android.Packages.push_back({item.value("id", std::string()), item.value("label", std::string()), item.value("size", 0LL), item.value("installed", false)});
            android.DownloadBytes = data.value("downloadBytes", 0LL);
        }
        else if (android.Installed) android.DownloadBytes = 0;
    }
}
std::filesystem::path HubEngineInstaller::StateRoot()
{
    auto custom = EnvPath(L"NOVA_HUB_STATE_DIR");
    return custom.empty() ? EnvPath(L"LOCALAPPDATA") / L"NOVA" / L"Hub" : custom;
}
HubEngineInstaller::HubEngineInstaller()
{
    wchar_t exe[32768] = {}; GetModuleFileNameW(nullptr, exe, 32768);
    auto service = std::filesystem::path(exe).parent_path() / L"NovaHubService.exe";
    if (std::filesystem::is_regular_file(service)) m_Service = service.wstring();
    LoadInstalled();
}
HubEngineInstaller::~HubEngineInstaller()
{
    Cancel(); if (m_Process) CloseHandle(m_Process);
}
void HubEngineInstaller::LoadInstalled()
{
    m_Engines.clear(); auto data = Read(StateRoot() / L"engines.json");
    if (!data.is_object() || !data.contains("engines") || !data["engines"].is_array()) return;
    for (const auto& item : data["engines"])
    {
        try
        {
            Engine engine{item.at("version").get<std::string>(), Wide(item.at("root").get<std::string>()), item.value("managed", true)};
            if (std::filesystem::is_regular_file(std::filesystem::path(engine.Root) / L"Binaries" / L"NovaEngine.exe") &&
                std::filesystem::is_regular_file(std::filesystem::path(engine.Root) / L"Binaries" / L"NovaCore.dll")) m_Engines.push_back(engine);
        }
        catch (...) { }
    }
}
std::wstring HubEngineInstaller::EditorFor(const std::string& version) const
{
    for (const auto& engine : m_Engines)
        if (version.empty() || version == engine.Version) return (std::filesystem::path(engine.Root) / L"Binaries" / L"NovaEngine.exe").wstring();
    return {};
}
bool HubEngineInstaller::Start(const std::string& command, const std::string& version, const std::wstring& root, bool acceptLicense)
{
    if (!Enabled() || Busy()) return false;
    try
    {
        m_Job = StateRoot() / L"jobs" / (std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
        std::filesystem::create_directories(m_Job);
        const auto engineRoot = EnvPath(L"NOVA_HUB_ENGINE_ROOT");
        nlohmann::json request = {{"command", command}, {"stateRoot", Utf8(StateRoot().wstring())},
            {"engineRoot", engineRoot.empty() ? nlohmann::json(nullptr) : nlohmann::json(Utf8(engineRoot.wstring()))},
            {"version", version}, {"root", Utf8(root)}, {"parentPid", GetCurrentProcessId()}, {"acceptLicense", acceptLicense}};
        auto file = m_Job / L"request.json";
        { std::ofstream out(file); out << request.dump(); if (!out) throw std::runtime_error("설치 요청을 저장하지 못했습니다."); }
        std::wstring line = L"\"" + m_Service + L"\" --request \"" + file.wstring() + L"\"";
        STARTUPINFOW si = {sizeof(si)}; PROCESS_INFORMATION pi = {};
        if (!CreateProcessW(m_Service.c_str(), line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
            std::filesystem::path(m_Service).parent_path().c_str(), &si, &pi)) throw std::runtime_error("엔진 설치 서비스를 실행하지 못했습니다.");
        CloseHandle(pi.hThread); m_Process = pi.hProcess; m_Command = command; m_Progress = 0; m_Failed = false;
        m_Message = command == "catalog" ? "엔진 버전을 불러오는 중..." : command == "remove" ? "엔진을 제거하는 중..." :
            command == "android-catalog" ? "Android 빌드 도구 목록을 불러오는 중..." : command == "android-status" ? "Android 빌드 도구를 확인하는 중..." : "다운로드를 준비하는 중...";
        return true;
    }
    catch (const std::exception& e) { m_Message = e.what(); m_Failed = true; return false; }
}
void HubEngineInstaller::Cancel()
{
    if (Busy()) { std::ofstream out(m_Job / L"cancel"); out << "cancel"; }
}
void HubEngineInstaller::Poll()
{
    if (!Busy() || GetTickCount64() < m_NextPoll) return;
    m_NextPoll = GetTickCount64() + 150;
    try
    {
        auto progress = Read(m_Job / L"progress.json");
        if (progress.is_object())
        {
            m_Message = progress.value("message", m_Message);
            if (progress.contains("fraction") && progress["fraction"].is_number()) m_Progress = progress["fraction"].get<float>();
        }
        if (WaitForSingleObject(m_Process, 0) != WAIT_OBJECT_0) return;
        auto result = Read(m_Job / L"result.json");
        CloseHandle(m_Process); m_Process = nullptr;
        m_Failed = !result.is_object() || !result.value("ok", false);
        if (m_Failed) m_Message = result.is_object() ? result.value("error", "설치 서비스가 종료되었습니다. 다시 시도하세요.") : "설치 서비스가 종료되었습니다. 다시 시도하세요.";
        else
        {
            if (m_Command == "catalog" || m_Command == "install")
            {
                m_Releases.clear();
                for (const auto& release : result.value("releases", nlohmann::json::array())) m_Releases.push_back({release.at("version").get<std::string>(), release.at("size").get<long long>()});
            }
            if (result.contains("android") && result["android"].is_object()) ReadAndroid(m_Android, result["android"]);
            m_Message = m_Command == "install" ? "엔진 설치 완료. 프로젝트를 만들거나 열 수 있습니다." : m_Command == "remove" ? "엔진을 제거했습니다. 프로젝트는 보존됩니다." :
                m_Command == "android-install" ? "Android 빌드 지원 설치 완료. 모든 엔진 버전이 함께 씁니다." :
                "설치할 엔진 버전을 선택하세요.";
        }
        LoadInstalled();
    }
    catch (const std::exception& e) { m_Message = e.what(); m_Failed = true; if (m_Process && WaitForSingleObject(m_Process, 0) == WAIT_OBJECT_0) { CloseHandle(m_Process); m_Process = nullptr; } }
}
