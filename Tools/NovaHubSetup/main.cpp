// Standalone /MT installer. The Hub payload is embedded as resource 101.
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <string>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include "HubPayload.h"
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
namespace fs = std::filesystem;
static HWND window, button, description;
static fs::path destination;
static bool installed = false;
static const char marker[] = "NOVA Hub standalone install\n";
static fs::path Known(REFKNOWNFOLDERID id)
{
    PWSTR raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(id, 0, nullptr, &raw))) throw std::runtime_error("사용자 폴더를 확인하지 못했습니다.");
    fs::path path(raw); CoTaskMemFree(raw); return path;
}
static void Shortcut(const fs::path& file, const fs::path& exe)
{
    IShellLinkW* link = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) throw std::runtime_error("바로가기를 만들지 못했습니다.");
    link->SetPath(exe.c_str()); link->SetWorkingDirectory(exe.parent_path().c_str()); link->SetDescription(L"NOVA Hub");
    IPersistFile* save = nullptr; const HRESULT query = link->QueryInterface(IID_PPV_ARGS(&save));
    HRESULT result = FAILED(query) ? query : save->Save(file.c_str(), TRUE);
    if (save) save->Release(); link->Release();
    if (FAILED(result)) throw std::runtime_error("바로가기를 저장하지 못했습니다.");
}
static void Install(bool shortcuts)
{
    destination = fs::absolute(destination).lexically_normal();
    if (destination == destination.root_path()) throw std::runtime_error("드라이브 루트에 설치할 수 없습니다.");
    for (auto parent = destination; !parent.empty() && parent != parent.root_path(); parent = parent.parent_path())
        if (fs::exists(parent) && (GetFileAttributesW(parent.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT)) throw std::runtime_error("설치 경로에 링크가 있습니다.");
    if (fs::exists(destination) && !fs::is_empty(destination))
    {
        std::ifstream in(destination / L".nova-hub-install"); std::string text((std::istreambuf_iterator<char>(in)), {});
        if (text != marker) throw std::runtime_error("기존 파일이 있는 폴더에는 설치하지 않습니다.");
    }
    fs::create_directories(destination);
    for (const auto& payload : hubPayload)
    {
    const fs::path output = destination / payload.path;
    for (auto parent = output.parent_path(); parent != destination && !parent.empty(); parent = parent.parent_path())
        if (fs::exists(parent) && (GetFileAttributesW(parent.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT)) throw std::runtime_error("설치 경로에 링크가 있습니다.");
    if (fs::exists(output) && (GetFileAttributesW(output.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT)) throw std::runtime_error("설치 파일에 링크가 있습니다.");
    fs::create_directories(output.parent_path());
    HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(payload.id), RT_RCDATA);
    if (!resource) throw std::runtime_error("Hub 설치 파일이 없습니다.");
    const DWORD size = SizeofResource(nullptr, resource); const void* bytes = LockResource(LoadResource(nullptr, resource));
    if (!bytes || !size) throw std::runtime_error("Hub 설치 파일을 읽지 못했습니다.");
    const auto temp = output.wstring() + L".install-" + std::to_wstring(GetCurrentProcessId()) + L".tmp";
    HANDLE out = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (out == INVALID_HANDLE_VALUE) throw std::runtime_error("설치 폴더에 쓸 수 없습니다.");
    DWORD written = 0; const bool ok = WriteFile(out, bytes, size, &written, nullptr) && written == size && FlushFileBuffers(out);
    CloseHandle(out);
    if (!ok || !MoveFileExW(temp.c_str(), output.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    { DeleteFileW(temp.c_str()); throw std::runtime_error("Hub를 설치하지 못했습니다. 실행 중인 Hub를 닫고 다시 시도하세요."); }
    }
    std::ofstream record(destination / L".nova-hub-install", std::ios::binary); record << marker; record.close();
    if (!record) throw std::runtime_error("설치 기록을 저장하지 못했습니다.");
    if (shortcuts)
    {
        auto menu = Known(FOLDERID_Programs) / L"NOVA"; fs::create_directories(menu);
        Shortcut(menu / L"NOVA Hub.lnk", destination / L"Binaries" / L"NovaHub.exe");
        Shortcut(Known(FOLDERID_Desktop) / L"NOVA Hub.lnk", destination / L"Binaries" / L"NovaHub.exe");
    }
}
static std::wstring Wide(const char* text)
{
    int size = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0); std::wstring out(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, -1, out.data(), size); return out;
}
static LRESULT CALLBACK Proc(HWND hwnd, UINT message, WPARAM w, LPARAM l)
{
    if (message == WM_COMMAND && LOWORD(w) == 1)
    {
        if (installed) { const auto exe = destination / L"Binaries" / L"NovaHub.exe"; ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr, exe.parent_path().c_str(), SW_SHOWNORMAL); DestroyWindow(hwnd); return 0; }
        EnableWindow(button, FALSE);
        try { Install(true); installed = true; SetWindowTextW(description, L"NOVA Hub 설치를 완료했습니다.\nHub에서 엔진 버전을 선택해 설치하세요."); SetWindowTextW(button, L"NOVA Hub 열기"); }
        catch (const std::exception& e) { MessageBoxW(hwnd, Wide(e.what()).c_str(), L"NOVA Hub 설치", MB_OK | MB_ICONERROR); }
        EnableWindow(button, TRUE); return 0;
    }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(hwnd, message, w, l);
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); SetProcessDPIAware();
    try
    {
        destination = Known(FOLDERID_LocalAppData) / L"NOVA" / L"HubApp";
        int count = 0; LPWSTR* args = CommandLineToArgvW(GetCommandLineW(), &count); bool test = false;
        for (int i = 1; i < count; ++i) if (std::wstring(args[i]) == L"--test-install" && i + 1 < count) { destination = args[++i]; test = true; }
        LocalFree(args);
        if (test) { Install(false); return 0; } // Isolated packaging test: no shortcuts, no launch, no user registry.
        WNDCLASSW cls = {}; cls.lpfnWndProc = Proc; cls.hInstance = instance; cls.lpszClassName = L"NovaHubSetup"; cls.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); cls.hCursor = LoadCursor(nullptr, IDC_ARROW); RegisterClassW(&cls);
        window = CreateWindowW(cls.lpszClassName, L"NOVA Hub 설치", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 560, 300, nullptr, nullptr, instance, nullptr);
        auto font = CreateFontW(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"Malgun Gothic");
        description = CreateWindowW(L"STATIC", L"NOVA Hub를 설치합니다.\n엔진은 Hub에서 버전을 선택해 별도로 설치할 수 있습니다.", WS_CHILD | WS_VISIBLE, 28, 30, 480, 65, window, nullptr, instance, nullptr);
        auto path = CreateWindowW(L"STATIC", destination.c_str(), WS_CHILD | WS_VISIBLE, 28, 115, 480, 55, window, nullptr, instance, nullptr);
        button = CreateWindowW(L"BUTTON", L"설치", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 328, 190, 170, 38, window, (HMENU)1, instance, nullptr);
        for (auto control : { description, path, button }) SendMessageW(control, WM_SETFONT, (WPARAM)font, TRUE);
        ShowWindow(window, show); MSG msg; while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        DeleteObject(font); CoUninitialize(); return 0;
    }
    catch (const std::exception& e) { OutputDebugStringW(Wide(e.what()).c_str()); return 1; }
}
