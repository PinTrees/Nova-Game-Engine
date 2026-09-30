#include "pch.h"
#include "PathManager.h"

SINGLE_BODY(PathManager)

PathManager::PathManager()
{

}

PathManager::~PathManager()
{

}


wstring PathManager::s_ProjectOverride;

namespace
{
	std::wstring WithTrailingSlash(std::wstring p)
	{
		if (!p.empty() && p.back() != L'\\' && p.back() != L'/')
			p += L"\\";
		return p;
	}

	// 시작 경로에서 위로 올라가며 marker 폴더가 있는 첫 상위 폴더를 찾는다.
	bool FindRootWithMarker(std::filesystem::path start, const wchar_t* marker, std::filesystem::path& out)
	{
		std::error_code ec;
		for (int i = 0; i < 5 && !start.empty(); ++i)
		{
			if (std::filesystem::exists(start / marker, ec))
			{
				out = start;
				return true;
			}
			if (!start.has_parent_path() || start.parent_path() == start)
				break;
			start = start.parent_path();
		}
		return false;
	}

	bool StartsWithNoCase(const std::wstring& text, const std::wstring& prefix)
	{
		if (text.size() < prefix.size()) return false;
		return _wcsnicmp(text.c_str(), prefix.c_str(), prefix.size()) == 0;
	}

	// 엔진 폴더에 있는 리소스인지(프로젝트 폴더가 아닌) 판별
	bool IsEngineRelative(const std::wstring& movePath)
	{
		size_t pos = movePath.find_first_not_of(L"\\/");
		if (pos == std::wstring::npos) return false;
		std::wstring rel = movePath.substr(pos);
		return StartsWithNoCase(rel, L"ProjectSetting") || StartsWithNoCase(rel, L"Resources") || StartsWithNoCase(rel, L"Shaders");
	}
}

void PathManager::Init()
{
	wchar_t exePathBuf[MAX_PATH] = { 0 };
	GetModuleFileNameW(NULL, exePathBuf, MAX_PATH);
	std::filesystem::path exeDir = std::filesystem::path(exePathBuf).parent_path();

	// 엔진 루트: Shaders 폴더가 있는 상위 폴더 (실행 파일은 Binaries 안에 있음)
	std::filesystem::path engineRoot;
	if (!FindRootWithMarker(exeDir, L"Shaders", engineRoot) &&
		!FindRootWithMarker(std::filesystem::current_path(), L"Shaders", engineRoot) &&
		!FindRootWithMarker(exeDir, L"Assets", engineRoot))
	{
		engineRoot = exeDir.has_parent_path() ? exeDir.parent_path() : exeDir;
	}
	wcscpy_s(_szEnginePath, MAX_PATH, WithTrailingSlash(engineRoot.wstring()).c_str());

	// 프로젝트 루트: --project 로 지정되면 그 폴더, 아니면 엔진 루트(개발용 샘플 프로젝트)
	if (!s_ProjectOverride.empty())
		wcscpy_s(_szContentPath, MAX_PATH, WithTrailingSlash(s_ProjectOverride).c_str());
	else
		wcscpy_s(_szContentPath, MAX_PATH, _szEnginePath);
}

wstring PathManager::GetEnginePathW()
{
	return _szEnginePath;
}

string PathManager::GetEnginePathS()
{
	return wstring_to_string(_szEnginePath);
}

wstring PathManager::GetContentPathW()
{
	wstring path = _szContentPath;

	return path;
}

string PathManager::GetContentPathS()
{
	string path = wstring_to_string(_szContentPath);

	return path;
}

wstring PathManager::GetMovePathW(wstring movePath)
{
	wstring path = IsEngineRelative(movePath) ? _szEnginePath : _szContentPath;
	path += movePath;

	// 최대 폴더 경로 제한 검사
	if (path.size() > MAX_PATH)
		assert(false);

	return path;
}

string PathManager::GetMovePathS(string movePath)
{
	wstring wide = string_to_wstring(movePath);
	wstring path = IsEngineRelative(wide) ? _szEnginePath : _szContentPath;
	path += wide;

	string result = wstring_to_string(path);

	// 최대 폴더 경로 제한 검사
	if (result.size() > MAX_PATH)
		assert(false);

	return result;
}

string PathManager::GetFileName(string fullpath)
{
	string filename = filesystem::path(fullpath).stem().string();
	return filename;
}

wstring PathManager::GetCutSolutionPath(wstring path)
{
	// 프로젝트 루트 → 엔진 루트 순으로 접두 경로를 제거 (둘 다 아니면 원본 유지)
	for (const wchar_t* root : { _szContentPath, _szEnginePath })
	{
		size_t len = wcslen(root);
		if (len > 0 && StartsWithNoCase(path, root))
			return path.substr(len);
	}
	return path;
}

string PathManager::GetCutSolutionPath(string path)
{
	return wstring_to_string(GetCutSolutionPath(string_to_wstring(path)));
}
