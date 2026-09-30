#pragma once
#include <string>
#include <vector>

// Hub 에서 관리하는 프로젝트 한 건
struct HubProject
{
	std::string  Name;
	std::wstring Path;            // 프로젝트 루트 (Assets, ProjectSettings 가 있는 폴더)
	std::string  EngineVersion;
	std::string  Template;
	long long    LastOpened = 0;  // epoch seconds, 0 이면 열어본 적 없음
	bool         Favorite = false;
};

struct HubTemplate
{
	const char* Id;
	const char* Title;
	const char* Description;
};

// 프로젝트 목록(%LOCALAPPDATA%\NOVA\Hub\projects.json)과 프로젝트 생성 로직
class HubProjectRegistry
{
public:
	static const std::vector<HubTemplate>& Templates();

	static void Load();
	static void Save();
	static std::vector<HubProject>& Projects() { return s_Projects; }

	// 이미 있는 폴더를 목록에 추가. Assets 폴더가 없으면 생성해 프로젝트로 만든다.
	static bool AddExisting(const std::wstring& folder, std::string& error);

	// location\name 에 새 프로젝트 폴더를 만들고 목록 맨 위에 추가
	static bool Create(const std::string& name, const std::wstring& location, const std::string& templateId, std::string& error);

	static void Remove(size_t index);
	static void MarkOpened(size_t index);

	static bool Exists(const HubProject& project);
	static bool IsValidProjectName(const std::string& name, std::string& error);

	// 기본 프로젝트 생성 위치 (문서 폴더 아래 NOVA Projects)
	static std::wstring DefaultLocation();

private:
	static std::vector<HubProject> s_Projects;
};

// 프로세스 실행 도우미: Hub 와 에디터는 같은 exe 를 다른 인자로 실행한다.
namespace HubLauncher
{
	// NovaEngine.exe --project "<path>"
	bool LaunchEditor(const std::wstring& projectPath, std::string& error);
	// 인자 없이 실행 → Hub
	bool LaunchHub();
	// 이 프로젝트의 에디터 프로세스가 아직 실행 중인지
	bool IsEditorRunning(const std::wstring& projectPath);
}
