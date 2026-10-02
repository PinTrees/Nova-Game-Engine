#pragma once
#include "EditorWindow.h"
#include <set>

// Unity 6 의 Package Manager 창 (Window > Package Manager).
//  - 위 막대: + ▾ (Install package from disk), Sort ▾ (이름 · 날짜), Filters ▾ (상태 · 분류), Clear Filters
//  - 왼쪽: In Project / Updates / NOVA Registry
//  - 가운데: 검색 + Features (여러 패키지 묶음 — package.json "type": "feature") + Packages (버전 · ✓ 설치됨 · ↑ 업데이트) + 마지막 새로 고침
//  - 오른쪽: 이름 · 버전 · 날짜 · 출처, Documentation / Changelog / Licenses, Install · Update · Locate · Manage ▾,
//            탭 Details (Technical Name · ABI · 설명) / Version History / Dependencies / Components
//  - 아래: 다시 시작해야 빠지는 패키지가 있으면 안내
class PackageManagerWindow
	: public EditorWindow
{
public:
	PackageManagerWindow();

	static void Open(const char* select = nullptr);
	static void Close();
	static bool IsOpen();

protected:
	void BeforeBegin() override;
	void PushStyle() override;
	void PopStyle() override;
	void OnRender() override;

private:
	enum View { InProjectView = 0, UpdatesView = 1, RegistryView = 2 };

	static PackageManagerWindow* s_Instance;
	bool m_FocusNext = false;
	int m_View = RegistryView;   // 처음엔 고를 수 있는 전체 목록
	int m_Sort = 0;              // 0 이름 오름차순, 1 이름 내림차순, 2 날짜 (새 것 먼저)
	int m_Status = 0;            // Filters > Status: 0 전체, 1 설치됨, 2 설치 안 됨
	std::set<std::string> m_Categories;   // Filters > Categories (비면 전체)
	int m_Tab = 0;               // 0 Details, 1 Version History, 2 Dependencies, 3 Components
	bool m_FeaturesOpen = true;
	bool m_PackagesOpen = true;
	char m_Search[128] = {};
	std::string m_Selected;      // 패키지 이름
	std::string m_LastError;     // 마지막 Install/Remove 실패

	void DrawToolbar(ImDrawList* dl, ImVec2 origin, float width, float height);
	void DrawSidebar(ImDrawList* dl, ImVec2 min, ImVec2 max);
	void DrawList(ImDrawList* dl, ImVec2 min, ImVec2 max);
	void DrawDetail(ImDrawList* dl, ImVec2 min, ImVec2 max);
	std::vector<const struct PackageInfo*> Rows(bool features) const;
};
