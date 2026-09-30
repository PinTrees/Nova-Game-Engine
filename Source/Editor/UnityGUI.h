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
	bool Dropdown(const char* label, int* index, const char* const* items, int count, int indent = 0, bool disabled = false);
	bool Toggle(const char* label, bool* value, int indent = 0);
	// "Freeze Position  [ ]X [ ]Y [ ]Z" 처럼 축별 체크박스 3개
	bool Toggle3(const char* label, bool* xyz, int indent = 0);
	// 레이어 마스크 드롭다운 (Nothing / Everything / 레이어별 체크). 비트 i = LayerNames() 의 i 번째 레이어
	bool MaskField(const char* label, uint32* mask, int indent = 0);
	// 프로젝트 레이어 이름 목록 (GameObject 헤더의 Layer 드롭다운과 같은 순서)
	const char* const* LayerNames(int* count);
	// 읽기 전용 값 표시 (Rigidbody 의 Info 등)
	void ValueLabel(const char* label, const char* value, int indent = 0);
	bool Float(const char* label, float* value, int indent = 0, const char* innerLabel = nullptr);
	bool Int(const char* label, int* value, int indent = 0);
	bool Slider(const char* label, float* value, float minV, float maxV, int indent = 0);   // 슬라이더 + 숫자 입력
	bool Vector3(const char* label, float* xyz, bool showLinkIcon = false, int indent = 0);
	bool Vector2Pair(const char* label, const char* n0, float* a, const char* n1, float* b, int indent = 0);   // "X 0  Y 0" 형태
	bool Color(const char* label, float* rgba, int indent = 0);
	// 오브젝트 필드. 오른쪽 ⊙ 버튼을 누르면 true. iconName 이 있으면 필드 안쪽 왼쪽에 아이콘을 표시한다.
	bool ObjectField(const char* label, const char* text, int indent = 0, const char* iconName = nullptr);
	void Label(const char* label, int indent = 0, bool bold = false);
	void EmptyListBox(const char* header, const char* emptyText);
	// 체크박스 + 텍스트가 왼쪽에 붙는 행 (예: Light 의 "Cookie"). withTargetIcon 이면 텍스트 앞에 ⊙ 아이콘.
	bool ToggleLeft(const char* text, bool* value, int indent = 1, bool disabled = false, bool withTargetIcon = false);
	// 경고/정보 박스 (아이콘 + 자동 줄바꿈 텍스트)
	void HelpBox(const char* text, bool warning = true, int indent = 0);
	// 색온도 그라디언트 바 + Kelvin 숫자 입력. 값이 바뀌면 true.
	bool TemperatureBar(const char* label, float* kelvin, float minK, float maxK, int indent = 0);   // Unity 의 비어 있는 리스트 박스 (예: Camera Stack)

	// ---- 섹션 ----
	// SRP 스타일 접이식 서브 섹션 (예: Projection, Rendering). 열려 있으면 true.
bool Foldout(const char* label, int indent = 0, bool defaultOpen = true, bool help = true);
	// 배경 밴드 없이 화살표 + 굵은 글자만 있는 폴드아웃 (MeshRenderer 의 Lighting / Probes 등)
	bool FoldoutPlain(const char* label, int indent = 0, bool defaultOpen = true);
	// "Materials" 폴드아웃 + 오른쪽 크기 입력 필드
	bool MaterialsHeader(const char* label, int* count);
	// [= Element N ] [ 오브젝트 필드 ⊙ ]
	bool ElementRow(const char* label, const char* text, const char* iconName);
	// 리스트 하단 + / - 버튼
	void PlusMinus(bool* plus, bool* minus);
	// 레이블 + 아이콘 버튼 (예: Edit Collider). 눌리면 true
	bool IconButtonRow(const char* label, const char* iconName, int indent = 0);
	// 재질 Inspector (컴포넌트 아래에 표시): 헤더 + Shader 행
	void MaterialPanel(const char* name, const char* shaderName);

	// 컴포넌트 헤더 (Transform, Camera 등)
	enum class HeaderAction { None, Reset, Remove };
	struct HeaderResult
	{
		bool open = true;
		HeaderAction action = HeaderAction::None;
	};
	HeaderResult ComponentHeader(const char* id, const char* title, const char* iconName, bool* open, bool* enabled, bool canRemove);

	// 프리팹 인스턴스 루트의 행: Prefab  [Open] [Select] [Overrides ▾] (Apply All / Revert All)
	void PrefabInstanceRow(class GameObject* instanceRoot);

	// GameObject 헤더 (아이콘, 활성 체크, 이름, Static, Tag, Layer)
	bool GameObjectHeader(bool* active, std::string* name, bool* isStatic, std::string* tag, int* layer);
}
