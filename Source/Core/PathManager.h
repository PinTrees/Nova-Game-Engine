#pragma once

class PathManager
{
	SINGLE_HEADER(PathManager)
private:
	wchar_t _szContentPath[MAX_PATH]; // 프로젝트 루트 (Assets, ProjectSettings 가 있는 폴더)
	wchar_t _szEnginePath[MAX_PATH];  // 엔진 루트 (Shaders, Resources, ProjectSetting 이 있는 폴더)

	static wstring s_ProjectOverride;
public:
	// Init 전에 호출하면 해당 폴더를 프로젝트 루트로 사용한다. (에디터를 --project 로 실행할 때)
	static void SetProjectOverride(const wstring& projectRoot) { s_ProjectOverride = projectRoot; }
	static const wstring& GetProjectOverride() { return s_ProjectOverride; }

	void Init();

	// 엔진 리소스(폰트, 아이콘, 셰이더 등) 루트
	wstring GetEnginePathW();
	string GetEnginePathS();

	const wchar_t* GetContentPath() { return _szContentPath; }
	wstring GetContentPathW();
	string GetContentPathS();

	wstring GetMovePathW(wstring movePath);
	string GetMovePathS(string movePath);

	string GetFileName(string fullpath);

	// Assets\... 형태의 프로젝트 상대 경로로 변환. 프로젝트/엔진 루트가 아니면 그대로 반환.
	wstring GetCutSolutionPath(wstring path);
	string GetCutSolutionPath(string path);
};

#define PATH_SOLUTION PathManager::GetI()->GetContentPath();