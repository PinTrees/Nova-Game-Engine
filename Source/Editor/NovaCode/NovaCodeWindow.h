#pragma once
#include "EditorWindow.h"
#include "CodeEditor.h"
#include <filesystem>
#include <memory>
#include <set>

// NOVA Code: 에디터에 내장된 C# 스크립트 편집기 창 (External Script Editor 의 기본값).
//  - 왼쪽: Explorer (Assets 아래 폴더와 .cs 파일, + 새 스크립트)
//  - 위: 도구 모음 (저장/모두 저장/실행 취소/다시 실행/찾기) + 컴파일 상태
//  - 가운데: 열린 파일 탭 (저장 안 한 파일은 ● 표시) + 코드 편집기
//  - 아래: 상태 표시줄 (오류/경고 개수, 줄/열, 들여쓰기, 인코딩, 줄 끝)
//  - Ctrl+S 저장 → 스크립트가 바로 다시 컴파일된다. 컴파일 오류는 그 줄에 빨간 밑줄
//  - 다른 프로그램이 파일을 바꾸면 (저장 안 한 변경이 없을 때) 다시 읽는다
class NovaCodeWindow
	: public EditorWindow
{
public:
	NovaCodeWindow();
	~NovaCodeWindow();

	// 파일을 열고 줄로 이동한 뒤 창을 앞으로 (line 1부터, 0 = 그대로)
	static void Open(const std::wstring& file, int line = 0);
	// 창(또는 안의 편집기)에 포커스가 있는지: 씬 저장 Ctrl+S 등 전역 단축키가 양보한다
	static bool IsFocused();

	virtual void Update() override;

protected:
	virtual void BeforeBegin() override;
	virtual ImGuiWindowFlags ExtraWindowFlags() const override;
	virtual ImVec2 WindowPaddingOverride() const override { return ImVec2(0.0f, 0.0f); }
	virtual void OnRender() override;

private:
	struct Document
	{
		std::filesystem::path Path;
		std::string Name;
		CodeEditor Editor;
		std::filesystem::file_time_type LastWrite{};
		bool ChangedOnDisk = false;   // 저장 안 한 변경이 있는데 밖에서 파일이 바뀜
		bool Missing = false;         // 파일이 지워짐
		bool SelectTab = false;       // 다음 프레임에 이 탭을 고른다
		bool Bom = false;             // UTF-8 BOM 이 있던 파일 (저장할 때 유지)
	};

	Document* OpenDocument(const std::filesystem::path& path);
	Document* Active();
	bool Save(Document& doc);
	void SaveAll();
	void RequestClose(int index);
	void CloseNow(int index);
	void Reload(Document& doc);

	void DrawToolbar();
	void DrawExplorer(float width, float height);
	void DrawExplorerFolder(const std::filesystem::path& dir, int depth);
	void DrawTabs();
	void DrawStatusBar();
	void DrawClosePrompt();
	void HandleShortcuts();

	void CheckFilesOnDisk();
	void UpdateMarkers();
	void UpdateApi();
	void ScanAssets();

	std::vector<std::unique_ptr<Document>> m_Docs;
	int m_ActiveIndex = -1;
	int m_PendingClose = -1;          // "저장할까요?" 를 묻는 중인 탭
	bool m_FocusRequest = false;
	bool m_FocusEditor = false;
	bool m_ShowExplorer = true;
	float m_ExplorerWidth = 210.0f;
	float m_FontSize = 16.0f;

	// Explorer (Assets 폴더 트리)
	struct Node
	{
		std::filesystem::path Path;
		bool IsDir = false;
		std::vector<Node> Children;
	};
	Node m_Root;
	double m_NextScan = 0.0;
	double m_NextDiskCheck = 0.0;
	std::set<std::wstring> m_Expanded;  // 펼친 폴더 (소문자 경로)
	char m_NewScriptName[64] = {};
	bool m_OpenNewScript = false;
	std::filesystem::path m_NewScriptDir;

	uint64_t m_MarkerVersion = ~0ull;
	size_t m_ApiClassCount = ~(size_t)0;
	bool m_ApiLoaded = false;
};
