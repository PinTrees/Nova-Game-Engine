#include "pch.h"
#include "PathManager.h"

SINGLE_BODY(PathManager)

PathManager::PathManager()
{

}

PathManager::~PathManager()
{

}


void PathManager::Init()
{
	std::filesystem::path currentPath = std::filesystem::current_path();

	if (std::filesystem::exists(currentPath / "Assets"))
	{
		std::wstring rootStr = currentPath.wstring();
		if (rootStr.back() != L'\\' && rootStr.back() != L'/')
			rootStr += L"\\";
		wcscpy_s(_szContentPath, MAX_PATH, rootStr.c_str());
		return;
	}

	if (currentPath.has_parent_path() && std::filesystem::exists(currentPath.parent_path() / "Assets"))
	{
		std::wstring rootStr = currentPath.parent_path().wstring();
		if (rootStr.back() != L'\\' && rootStr.back() != L'/')
			rootStr += L"\\";
		wcscpy_s(_szContentPath, MAX_PATH, rootStr.c_str());
		return;
	}

	wchar_t exePathBuf[MAX_PATH] = { 0 };
	GetModuleFileNameW(NULL, exePathBuf, MAX_PATH);
	std::filesystem::path checkDir = std::filesystem::path(exePathBuf).parent_path();
	for (int i = 0; i < 3; ++i)
	{
		if (std::filesystem::exists(checkDir / "Assets"))
		{
			std::wstring rootStr = checkDir.wstring();
			if (rootStr.back() != L'\\' && rootStr.back() != L'/')
				rootStr += L"\\";
			wcscpy_s(_szContentPath, MAX_PATH, rootStr.c_str());
			return;
		}
		if (checkDir.has_parent_path())
			checkDir = checkDir.parent_path();
	}

	GetCurrentDirectory(MAX_PATH, _szContentPath);
	int ilen = wcslen(_szContentPath);
	for (int i = ilen - 1; i >= 0; i--)
	{
		if ('\\' == _szContentPath[i])
		{
			_szContentPath[i] = '\0';
			break;
		}
	}
	wcscat_s(_szContentPath, MAX_PATH, L"\\");
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
	wstring path = _szContentPath;
	path += movePath;

	// �ִ� ���� ��� ���� �˻�
	if (path.size() > MAX_PATH)
		assert(false);

	return path;
}

string PathManager::GetMovePathS(string movePath)
{
	wstring path = _szContentPath;
	path += string_to_wstring(movePath);

	string result = wstring_to_string(path);

	// �ִ� ���� ��� ���� �˻�
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
	wstring result = path;
	wstring soluPath = GetContentPathW();
	size_t pos = result.find(soluPath);
	result.erase(pos, soluPath.length());

	return result;
}

string PathManager::GetCutSolutionPath(string path)
{
	string result = path;
	string soluPath = GetContentPathS();
	size_t pos = result.find(soluPath);
	result.erase(pos, soluPath.length());

	return result;
}

