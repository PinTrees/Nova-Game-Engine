#pragma once
#include <string>
#include <vector>

// 패키지가 Add Component 메뉴에 내놓는 컴포넌트 (package.json 의 "components")
struct PackageComponentInfo
{
	std::string Type;       // ComponentFactory 이름
	std::string Display;    // 메뉴 표시 이름
	std::string Category;   // 메뉴 카테고리 (Physics, Rendering, Navigation ...)
	std::string Icon;       // ProjectSetting/icons/svg/png/<이름>.png
	bool Single = true;     // 한 GameObject 에 하나만
};

// 패키지 한 개 (폴더 + package.json)
//  <폴더>/package.json        이름·버전·설명·컴포넌트·네이티브 DLL
//  <폴더>/Plugins/<x>.dll     C++ 컴포넌트 (NovaCore.dll 을 링크) + <x>.dll.abi (엔진 ABI 문자열, 빌드가 만든다)
//  <폴더>/Runtime/**/*.cs     C# API — 프로젝트 스크립트(Assembly-CSharp)와 함께 컴파일
struct PackageInfo
{
	std::string Name;            // com.nova.cameras
	std::string DisplayName;     // Cameras
	std::string Version;
	std::string Description;
	std::string Author;
	std::string Category;
	std::string Documentation;   // 문서 주소 (선택)
	std::vector<std::string> Keywords;
	std::vector<std::string> Native;              // Plugins 안 DLL 파일 이름
	std::vector<std::pair<std::string, std::string>> Dependencies;   // 다른 패키지 (이름, 버전) — Add 할 때 같이 넣는다
	std::vector<PackageComponentInfo> Components;
	std::string Type;            // "" = 패키지, "feature" = Unity 의 Feature (여러 패키지 묶음 — dependencies 만)
	std::string Date;            // 출시 날짜 yyyy-mm-dd ("date", 없으면 package.json 수정 날짜)
	std::string MinEditor;       // 필요한 엔진 버전 ("nova", 없으면 빈 문자열)
	std::wstring Changelog;      // CHANGELOG.md (있으면)
	std::wstring License;        // LICENSE.md / LICENSE (있으면)
	std::wstring Folder;         // 패키지 폴더 (끝 \ 없음)
	bool Embedded = false;       // 프로젝트 Packages/<이름>/ 에 들어 있는 패키지 (manifest 없이 항상 포함)
	bool Local = false;          // manifest 의 "file:<경로>" (Add package from disk — 레지스트리 밖 폴더)
};

// Unity 의 Package Manager.
//  - 레지스트리 = 엔진 폴더의 Packages/ (NOVA 공식 패키지, 엔진과 같이 빌드·배포). 나중에 온라인 스토어도 같은 형식으로.
//  - 프로젝트는 <프로젝트>/Packages/manifest.json 의 "dependencies" 에 이름·버전만 적는다 → 프로젝트 크기는 그대로.
//  - 프로젝트에 들어간 패키지만 불러오고(DLL), 게임 빌드에도 그것만 들어간다 (BuildPipeline → <제품>_Data/Packages/).
namespace PackageManager
{
	NOVA_API void Init();       // 프로젝트 경로가 정해진 뒤, 씬을 읽기 전 (패키지 컴포넌트가 먼저 등록돼야 한다)
	NOVA_API void Shutdown();
	NOVA_API void Refresh();    // 레지스트리 · manifest 다시 읽기 (창의 새로 고침)

	NOVA_API const std::vector<PackageInfo>& Registry();          // 엔진 레지스트리
	NOVA_API std::vector<const PackageInfo*> InProject();         // manifest + embedded
	NOVA_API const PackageInfo* Find(const std::string& name);    // 프로젝트 embedded 먼저, 그다음 레지스트리
	NOVA_API bool IsInProject(const std::string& name);
	NOVA_API bool IsLoaded(const std::string& name);
	NOVA_API std::string LoadError(const std::string& name);      // 불러오지 못한 이유 (없으면 빈 문자열)
	NOVA_API bool RestartRequired();                              // 쓰는 중이라 바로 내리지 못한 패키지가 있다
	NOVA_API std::string ManifestVersion(const std::string& name); // manifest 에 적힌 버전 (없으면 빈 문자열)
	NOVA_API bool HasUpdate(const std::string& name);             // 프로젝트 버전보다 레지스트리 버전이 새롭다 (Add = 업데이트)
	NOVA_API std::string LastRefresh();                           // 마지막 Refresh 시각 (표시용)

	// 프로젝트에 넣기 (manifest + 바로 불러오기 + 스크립트 다시 컴파일) / 빼기 (쓰는 컴포넌트가 없으면 바로 내린다)
	NOVA_API bool Add(const std::string& name, std::string& error);
	NOVA_API bool Remove(const std::string& name, std::string& error);
	// Unity 의 Add package from disk: package.json(또는 그 폴더)을 manifest 에 "file:<경로>" 로 (프로젝트 Packages 폴더 기준 상대 경로)
	NOVA_API bool AddFromDisk(const std::wstring& packageJsonOrFolder, std::string& error, std::string* addedName = nullptr);

	// 패키지 컴포넌트 타입 → 패키지 이름 (MissingComponent 안내용, 레지스트리에서 찾음)
	NOVA_API std::string PackageForComponent(const std::string& componentType);
	// 편집기에서 씬을 읽다가 모르는 타입을 만나면: 그 타입을 가진 레지스트리 패키지를 프로젝트에 넣고 불러온다
	// (예: Animation 패키지가 나오기 전 프로젝트의 Animator). 넣었으면 true
	NOVA_API bool AddForComponent(const std::string& componentType);
	// 프로젝트에 들어간 패키지의 C# 폴더 (Runtime) — Assembly-CSharp.csproj 에 포함
	NOVA_API std::vector<std::wstring> ScriptFolders();
	NOVA_API std::wstring ManifestPath();
	NOVA_API std::wstring RegistryFolder();
}
