#pragma once
// Win32 파일 열기 · 저장 대화 상자 (패키지의 편집 창) — 안드로이드에는 없다: 늘 "취소"
#include "WinCompat.h"
struct OPENFILENAMEW
{
	DWORD lStructSize; HWND hwndOwner; void* hInstance; LPCWSTR lpstrFilter; wchar_t* lpstrCustomFilter; DWORD nMaxCustFilter; DWORD nFilterIndex;
	wchar_t* lpstrFile; DWORD nMaxFile; wchar_t* lpstrFileTitle; DWORD nMaxFileTitle; LPCWSTR lpstrInitialDir; LPCWSTR lpstrTitle; DWORD Flags;
	unsigned short nFileOffset; unsigned short nFileExtension; LPCWSTR lpstrDefExt; intptr_t lCustData; void* lpfnHook; LPCWSTR lpTemplateName;
};
#define OFN_OVERWRITEPROMPT 0x00000002
#define OFN_NOCHANGEDIR 0x00000008
#define OFN_PATHMUSTEXIST 0x00000800
#define OFN_FILEMUSTEXIST 0x00001000
inline BOOL GetOpenFileNameW(OPENFILENAMEW*) { return FALSE; }
inline BOOL GetSaveFileNameW(OPENFILENAMEW*) { return FALSE; }
