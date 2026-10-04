#include "pch.h"
#include "PathManager.h"
#include "AndroidEngine.h"

// Source/Core/PathManager.cpp 의 안드로이드 판: 경로 구분자는 '/' (엔진 · 씬 파일의 '\' 경로를 바꿔서 돌려준다).
//  엔진 루트 = 프로젝트 루트 = 앱 파일 폴더의 game/ (PC 빌드한 게임의 <이름>_Data 와 같은 배치)
SINGLE_BODY(PathManager)

PathManager::PathManager() {}
PathManager::~PathManager() {}

wstring PathManager::s_ProjectOverride;

namespace
{
	std::wstring Slashes(std::wstring p)
	{
		for (wchar_t& c : p)
			if (c == L'\\') c = L'/';
		return p;
	}

	std::wstring WithTrailingSlash(std::wstring p)
	{
		p = Slashes(p);
		if (!p.empty() && p.back() != L'/')
			p += L"/";
		return p;
	}

	// 엔진 폴더에 있는 리소스인지 (프로젝트 폴더가 아닌) — Windows 판과 같은 규칙
	bool IsEngineRelative(const std::wstring& movePath)
	{
		size_t pos = movePath.find_first_not_of(L"\\/");
		if (pos == std::wstring::npos) return false;
		std::wstring rel = movePath.substr(pos);
		const size_t sep = rel.find_first_of(L"\\/");
		const std::wstring first = sep == std::wstring::npos ? rel : rel.substr(0, sep);
		return _wcsicmp(first.c_str(), L"ProjectSetting") == 0 || _wcsicmp(first.c_str(), L"Resources") == 0 || _wcsicmp(first.c_str(), L"Shaders") == 0;
	}

	void Copy(wchar_t* dst, const std::wstring& src)
	{
		wcsncpy(dst, src.c_str(), MAX_PATH - 1);
		dst[MAX_PATH - 1] = 0;
	}
}

void PathManager::Init()
{
	const std::wstring game = string_to_wstring(NovaAndroid::FilesDir() + "/game");
	Copy(_szEnginePath, WithTrailingSlash(s_EngineOverride.empty() ? game : s_EngineOverride));
	Copy(_szContentPath, s_ProjectOverride.empty() ? std::wstring(_szEnginePath) : WithTrailingSlash(s_ProjectOverride));
}

wstring PathManager::GetEnginePathW() { return _szEnginePath; }
string PathManager::GetEnginePathS() { return wstring_to_string(_szEnginePath); }
wstring PathManager::GetContentPathW() { return _szContentPath; }
string PathManager::GetContentPathS() { return wstring_to_string(_szContentPath); }

wstring PathManager::GetMovePathW(wstring movePath)
{
	wstring path = IsEngineRelative(movePath) ? _szEnginePath : _szContentPath;
	return Slashes(path + movePath);
}

string PathManager::GetMovePathS(string movePath)
{
	return wstring_to_string(GetMovePathW(string_to_wstring(movePath)));
}

string PathManager::GetFileName(string fullpath)
{
	for (char& c : fullpath)
		if (c == '\\') c = '/';
	return filesystem::path(fullpath).stem().string();
}

wstring PathManager::GetCutSolutionPath(wstring path)
{
	path = Slashes(path);
	for (const wchar_t* root : { _szContentPath, _szEnginePath })
	{
		const size_t len = wcslen(root);
		if (len > 0 && path.size() >= len && _wcsnicmp(path.c_str(), root, len) == 0)
			return path.substr(len);
	}
	return path;
}

string PathManager::GetCutSolutionPath(string path)
{
	return wstring_to_string(GetCutSolutionPath(string_to_wstring(path)));
}
