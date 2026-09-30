#pragma once

class PathManager
{
	SINGLE_HEADER(PathManager)
private:
	wchar_t _szContentPath[MAX_PATH]; // Project Path
public:
	void Init();

	const wchar_t* GetContentPath() { return _szContentPath; }
	wstring GetContentPathW();
	string GetContentPathS();

	wstring GetMovePathW(wstring movePath);
	string GetMovePathS(string movePath);

	string GetFileName(string fullpath);

	wstring GetCutSolutionPath(wstring path);
	string GetCutSolutionPath(string path);
};

#define PATH_SOLUTION PathManager::GetI()->GetContentPath();