#pragma once
#include <string>

// Unity Inspector 스타일 위젯 모음.
// 모든 행은 "레이블 열 | 필드 열" 구조이며 (Unity: 필드 열 시작 ≈ 폭의 41%), 행 높이 18px / 간격 20px 이다.
// 아이콘은 ProjectSetting/icons/svg/*.svg 를 래스터화한 PNG(ProjectSetting/icons/svg/png)를 사용한다.
namespace UnityGUI
{
	constexpr float kRowHeight = 18.0f;
	constexpr float kRowStep = 20.0f;
	constexpr float kBaseIndent = 22.0f;   // 컴포넌트 본문 레이블 시작 x
	constexpr float kNestIndent = 19.0f;   // 폴드아웃 안쪽 들여쓰기

	// ---- 리소스 ----
	ImTextureID Icon(const char* name);
	void DrawIcon(ImDrawList* dl, const char* name, ImVec2 pos, float size, ImU32 tint = IM_COL32_WHITE);
	ImFont* BoldFont();

	// ---- 레이아웃 ----
	void Spacing(float height);

	// ---- 행 위젯 (레이블 + 필드) ----
	bool Dropdown(const char* label, int* index, const char* const* items, int count, int indent = 0);
	bool Toggle(const char* label, bool* value, int indent = 0);
	bool Float(const char* label, float* value, int indent = 0, const char* innerLabel = nullptr);
	bool Int(const char* label, int* value, int indent = 0);
	bool Slider(const char* label, float* value, float minV, float maxV, int indent = 0);   // 슬라이더 + 숫자 입력
	bool Vector3(const char* label, float* xyz, bool showLinkIcon = false, int indent = 0);
	bool Vector2Pair(const char* label, const char* n0, float* a, const char* n1, float* b, int indent = 0);   // "X 0  Y 0" 형태
	bool Color(const char* label, float* rgba, int indent = 0);
	void ObjectField(const char* label, const char* text, int indent = 0);
	void Label(const char* label, int indent = 0, bool bold = false);
	void EmptyListBox(const char* header, const char* emptyText);   // Unity 의 비어 있는 리스트 박스 (예: Camera Stack)

	// ---- 섹션 ----
	// SRP 스타일 접이식 서브 섹션 (예: Projection, Rendering). 열려 있으면 true.
	bool Foldout(const char* label, int indent = 0, bool defaultOpen = true, bool help = true);

	// 컴포넌트 헤더 (Transform, Camera 등)
	enum class HeaderAction { None, Reset, Remove };
	struct HeaderResult
	{
		bool open = true;
		HeaderAction action = HeaderAction::None;
	};
	HeaderResult ComponentHeader(const char* id, const char* title, const char* iconName, bool* open, bool* enabled, bool canRemove);

	// GameObject 헤더 (아이콘, 활성 체크, 이름, Static, Tag, Layer)
	bool GameObjectHeader(bool* active, std::string* name, bool* isStatic, std::string* tag, int* layer);
}
