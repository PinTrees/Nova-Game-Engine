#include "pch.h"
#include "EditorUtility.h"
#include <filesystem>

namespace fs = std::filesystem;

std::wstring EditorUtility::OpenTextureFileDialog()
{
    OPENFILENAME ofn;
    wchar_t szFile[260] = { 0 };   
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t);                    // 변경: wchar_t에 맞게 크기 조정
    ofn.lpstrFilter = L"Image Files\0*.png;*.jpg\0All Files\0*.*\0";    // 변경: L"..." 사용
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileNameW(&ofn) == TRUE) {               // 변경: GetOpenFileNameW 사용
        std::wstring ws(szFile);
        return ws;
    }

    return L"";
}

std::wstring EditorUtility::OpenTextureFileDialog(const std::string& initialPath)
{
    fs::path resourcePath(initialPath);
    fs::path directoryPath = resourcePath.parent_path();
    std::wstring directoryPathW = directoryPath.wstring();

    OPENFILENAME ofn;
    wchar_t szFile[260] = { 0 };
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t);
    ofn.lpstrFilter = L"Image Files\0*.png;*.jpg;*.tga;*.TGA\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrInitialDir = directoryPathW.c_str();  // 초기 경로 설정
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileNameW(&ofn) == TRUE) {
        return std::wstring(ofn.lpstrFile);
    }

    return L"";
}

std::wstring EditorUtility::OpenFileDialog(const std::wstring& initialPath, const string title, const vector<string> filters)
{
    fs::path resourcePath(initialPath);
    fs::path directoryPath = resourcePath.parent_path();
    std::wstring directoryPathW = directoryPath.wstring();

    std::wstring filterString = string_to_wstring(title) + L'\0';

    for (const auto& filter : filters) 
        filterString += L"*." + string_to_wstring(filter) + L';';

    if (!filters.empty()) 
        filterString.pop_back();

    // "All Files" 필터 추가
    filterString += L"\0All Files\0*.*\0";

    OPENFILENAME ofn;
    wchar_t szFile[260] = { 0 };
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t);
    ofn.lpstrFilter = filterString.c_str();
    ofn.nFilterIndex = 1;
    ofn.lpstrInitialDir = directoryPathW.c_str();  // 초기 경로 설정
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileNameW(&ofn) == TRUE) {
        return std::wstring(ofn.lpstrFile);
    }

    return L"";
}

std::wstring EditorUtility::OpenFileDialog(const std::wstring& initialPath, const wstring title, const vector<wstring> filters)
{
    fs::path resourcePath(initialPath);
    fs::path directoryPath = resourcePath.parent_path();
    std::wstring directoryPathW = directoryPath.wstring();

    std::wstring filterString = L"";

    for (const auto& filter : filters)
    {
        filterString += title + L'\0' + filterString + L"*." + filter + L';';
    }
      

    if (!filters.empty())
        filterString.pop_back();

    // "All Files" 필터 추가
    filterString += L"\0All Files\0*.*\0";

    OPENFILENAME ofn;
    wchar_t szFile[260] = { 0 };
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t);
    ofn.lpstrFilter = filterString.c_str();
    ofn.nFilterIndex = 1;
    ofn.lpstrInitialDir = directoryPathW.c_str();  // 초기 경로 설정
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileNameW(&ofn) == TRUE) {
        return std::wstring(ofn.lpstrFile);
    }

    return L"";
}

std::wstring EditorUtility::SaveFileDialog(const std::wstring& initialPath, const std::wstring& title, const std::wstring& extension)
{
    fs::path resourcePath(initialPath);
    fs::path directoryPath = resourcePath.parent_path();
    std::wstring directoryPathW = directoryPath.wstring();

    // 필터 이름\0필터\0필터 이름\0필터\0
    // \0 문자는 반드시 ''로 추가
    std::wstring filterString = title + L'\0';
    filterString += L"*." + extension + L'\0';
    filterString += L"All Files (*.*)\0" + L'\0';
    filterString += L"*.*" + L'\0';

    OPENFILENAME ofn;
    wchar_t fileName[MAX_PATH] = L"";

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(OPENFILENAME);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = filterString.c_str();
    ofn.nFilterIndex = 1;
    ofn.lpstrTitle = title.c_str();
    ofn.lpstrInitialDir = directoryPathW.c_str();
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_OVERWRITEPROMPT;

    if (GetSaveFileName(&ofn)) 
    {
        std::wstring filePath = fileName;
        if (filePath.find(L'.') == std::wstring::npos)
        {
            filePath += L"." + extension;
        }
        return filePath;
    }

    return L"";
}
