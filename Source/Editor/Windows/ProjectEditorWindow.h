#pragma once
#include "EditorWindow.h"
#include <filesystem>
#include <set>

namespace fs = std::filesystem;

// Unity 의 Project 창 (Two Column Layout).
//  - 상단 툴바: + (Create), 검색, 타입 필터, 새로고침
//  - 왼쪽: 폴더 트리 (Assets, Packages) — 화살표로 펼치기, 클릭하면 오른쪽에 그 폴더 내용
//  - 오른쪽: breadcrumb (Assets > A > B, 조각을 누르면 이동) + 목록/격자 (아래 슬라이더로 아이콘 크기, 맨 왼쪽 = 목록)
//  - 하단: 선택한 에셋 경로
//  - 더블클릭: 폴더 열기 / 씬 열기 / Animator Controller 는 Animator 창 / 그 외 기본 프로그램
//  - 끌기: 프리팹·FBX·재질·텍스처·컨트롤러 → Hierarchy 등, Hierarchy 의 GameObject 를 놓으면 프리팹 생성
//  - F2 이름 바꾸기, Delete = 휴지통으로 (확인 창)
class ProjectEditorWindow
	: public EditorWindow
{
public:
	static std::wstring solutionDirectory;   // Assets 폴더 (절대 경로)
	static std::wstring currentDirectory;    // 오른쪽에 보이는 폴더

public:
	ProjectEditorWindow();
	~ProjectEditorWindow();

protected:
	virtual void OnRender() override;

private:
	struct Entry
	{
		fs::path Path;
		std::string Name;       // 표시 이름 (확장자 포함, Unity 는 확장자를 숨기지만 같은 이름 구분을 위해 유지)
		std::wstring Ext;       // 소문자
		bool IsDir = false;
		bool HasSubDirs = false;
	};
	const std::vector<Entry>& List(const fs::path& dir);
	void InvalidateCache() { m_Cache.clear(); m_SearchKey.clear(); }

	void DrawToolbar(ImVec2 pos, float width);
	void DrawTree(ImVec2 pos, ImVec2 size);
	void DrawTreeNode(const fs::path& dir, const std::string& label, int depth, float& y, float x0, float width);
	void DrawContent(ImVec2 pos, ImVec2 size);
	void DrawBreadcrumb(ImVec2 pos, float width);
	void DrawBottomBar(ImVec2 pos, float width);
	void DrawEntryRow(const Entry& e, float x, float& y, float width, int depth);
	void DrawEntryTile(const Entry& e, ImVec2 p, float tile);
	void DrawCreateMenu(const fs::path& dir);
	void DrawItemContextMenu();
	void HandleEntryInteraction(const Entry& e, bool hovered);
	void BeginDragSource(const Entry& e);
	bool AcceptGameObjectDrop(const fs::path& dir);
	void Open(const Entry& e);
	void Navigate(const fs::path& dir);
	std::string RelativeDisplayPath(const fs::path& p) const;

	std::map<std::wstring, std::pair<double, std::vector<Entry>>> m_Cache;
	std::set<std::wstring> m_Expanded;       // 트리에서 펼친 폴더
	std::set<std::wstring> m_ExpandedAssets; // 목록에서 펼친 FBX (하위 에셋)
	fs::path m_PackagesRoot;
	float m_TreeWidth = 220.0f;
	float m_IconSize = 0.0f;                 // 0 = 목록, 1 = 가장 큰 격자
	char m_Search[128] = {};
	std::string m_SearchKey;                 // 검색 결과 캐시 (검색어|필터)
	double m_SearchTime = -10.0;
	std::vector<Entry> m_SearchResults;
	int m_TypeFilter = 0;
	bool m_Focused = false;

	// 이름 바꾸기 / 삭제
	fs::path m_RenamePath;
	char m_RenameBuffer[256] = {};
	int m_RenameFrames = 0;
	fs::path m_PendingDelete;
	fs::path m_ContextPath;
	bool m_ContextOnEmpty = false;
};
