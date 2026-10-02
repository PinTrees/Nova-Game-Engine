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
	NOVA_API ImTextureID Icon(const char* name);
	NOVA_API void DrawIcon(ImDrawList* dl, const char* name, ImVec2 pos, float size, ImU32 tint = IM_COL32_WHITE);
	NOVA_API ImFont* BoldFont();
	NOVA_API ImFont* TitleFont();    // 굵게 22px (큰 제목)
	NOVA_API ImFont* HeaderFont();   // 굵게 19px (창 안의 분류 제목)

	// ---- 레이아웃 ----
	NOVA_API void Spacing(float height);
	// 직접 그리는 필드용 행: 레이블을 그리고 필드 열 위치를 돌려준다. 다 그린 뒤 EndFieldRow (height = 행 높이)
	struct FieldRow
	{
		ImVec2 p;        // 행 시작 (화면 좌표)
		float w;         // 행 폭
		float fieldX;    // 필드 열 시작 x
		float fieldW;    // 필드 열 폭
	};
	NOVA_API FieldRow BeginFieldRow(const char* label, int indent = 0);
	NOVA_API void EndFieldRow(const FieldRow& row, float height = kRowStep);
	// 필드 스타일(어두운 상자)을 입힌 숫자 칸. 행 안의 원하는 위치에
	NOVA_API bool FloatBox(const char* id, float* value, ImVec2 pos, float width);

	// ---- 행 위젯 (레이블 + 필드) ----
	NOVA_API bool Dropdown(const char* label, int* index, const char* const* items, int count, int indent = 0, bool disabled = false);
	NOVA_API bool Toggle(const char* label, bool* value, int indent = 0);
	// "Freeze Position  [ ]X [ ]Y [ ]Z" 처럼 축별 체크박스 3개
	NOVA_API bool Toggle3(const char* label, bool* xyz, int indent = 0);
	// 레이어 마스크 드롭다운 (Nothing / Everything / 레이어별 체크). 비트 i = LayerNames() 의 i 번째 레이어
	NOVA_API bool MaskField(const char* label, uint32* mask, int indent = 0);
	// 프로젝트 레이어 이름 목록 (GameObject 헤더의 Layer 드롭다운과 같은 순서)
	NOVA_API const char* const* LayerNames(int* count);
	// 읽기 전용 값 표시 (Rigidbody 의 Info 등)
	NOVA_API void ValueLabel(const char* label, const char* value, int indent = 0);
	NOVA_API bool Float(const char* label, float* value, int indent = 0, const char* innerLabel = nullptr);
	NOVA_API bool Int(const char* label, int* value, int indent = 0);
	NOVA_API bool Slider(const char* label, float* value, float minV, float maxV, int indent = 0);   // 슬라이더 + 숫자 입력
	NOVA_API bool TextField(const char* label, std::string* value, int indent = 0);   // 한 줄 문자열
	// 여러 줄 문자열 (레이블 행 아래 폭 전체 상자, 예: Text 컴포넌트의 Text)
	NOVA_API bool TextArea(const char* label, std::string* value, float height = 60.0f, int indent = 0);
	// 행 구조 없이 원하는 위치에 숫자 입력 칸 하나 (RectTransform 의 Pos X / Width 칸 등). 커서는 호출자가 관리
	NOVA_API bool FloatCell(const char* id, float* value, ImVec2 pos, float width);
	// 바로 위 Slider 트랙 양 끝 아래의 작은 설명 글자 (예: High / Low, Left / Right, 2D / 3D)
	NOVA_API void SliderCaptions(const char* left, const char* right);
	NOVA_API bool Vector3(const char* label, float* xyz, bool showLinkIcon = false, int indent = 0);
	NOVA_API bool Vector2Pair(const char* label, const char* n0, float* a, const char* n1, float* b, int indent = 0);   // "X 0  Y 0" 형태
	NOVA_API bool Color(const char* label, float* rgba, int indent = 0);
	// 오브젝트 필드. 오른쪽 ⊙ 버튼을 누르면 true. iconName 이 있으면 필드 안쪽 왼쪽에 아이콘을 표시한다.
	NOVA_API bool ObjectField(const char* label, const char* text, int indent = 0, const char* iconName = nullptr);
	// GameObject 참조 필드 (Hierarchy 에서 끌어다 놓기, x 로 비우기). 값 = 씬 GameObject 의 fileID (0 = None)
	NOVA_API bool GameObjectField(const char* label, uint64* fileID, int indent = 0);
	NOVA_API void Label(const char* label, int indent = 0, bool bold = false);
	NOVA_API void EmptyListBox(const char* header, const char* emptyText);
	// 체크박스 + 텍스트가 왼쪽에 붙는 행 (예: Light 의 "Cookie"). withTargetIcon 이면 텍스트 앞에 ⊙ 아이콘.
	NOVA_API bool ToggleLeft(const char* text, bool* value, int indent = 1, bool disabled = false, bool withTargetIcon = false);
	// 경고/정보 박스 (아이콘 + 자동 줄바꿈 텍스트)
	NOVA_API void HelpBox(const char* text, bool warning = true, int indent = 0);
	// 색온도 그라디언트 바 + Kelvin 숫자 입력. 값이 바뀌면 true.
	NOVA_API bool TemperatureBar(const char* label, float* kelvin, float minK, float maxK, int indent = 0);   // Unity 의 비어 있는 리스트 박스 (예: Camera Stack)

	// ---- 섹션 ----
	// SRP 스타일 접이식 서브 섹션 (예: Projection, Rendering). 열려 있으면 true.
bool Foldout(const char* label, int indent = 0, bool defaultOpen = true, bool help = true);
	// 배경 밴드 없이 화살표 + 굵은 글자만 있는 폴드아웃 (MeshRenderer 의 Lighting / Probes 등)
	NOVA_API bool FoldoutPlain(const char* label, int indent = 0, bool defaultOpen = true);
	// "Materials" 폴드아웃 + 오른쪽 크기 입력 필드
	NOVA_API bool MaterialsHeader(const char* label, int* count);
	// [= Element N ] [ 오브젝트 필드 ⊙ ]
	NOVA_API bool ElementRow(const char* label, const char* text, const char* iconName);
	// 리스트 하단 + / - 버튼
	NOVA_API void PlusMinus(bool* plus, bool* minus);
	// 레이블 + 아이콘 버튼 (예: Edit Collider). 눌리면 true
	NOVA_API bool IconButtonRow(const char* label, const char* iconName, int indent = 0);
	// 재질 Inspector (컴포넌트 아래에 표시): 헤더 + Shader 행
	NOVA_API void MaterialPanel(const char* name, const char* shaderName);

	// 컴포넌트 헤더 (Transform, Camera 등)
	enum class HeaderAction { None, Reset, Remove };
	struct HeaderResult
	{
		bool open = true;
		HeaderAction action = HeaderAction::None;
	};
	NOVA_API HeaderResult ComponentHeader(const char* id, const char* title, const char* iconName, bool* open, bool* enabled, bool canRemove);

	// ---- Volume ----
	// 행 앞(레이블 왼쪽)의 체크박스 (Volume 파라미터별 Override). 커서는 움직이지 않으니 바로 이어서 행 위젯을 그린다.
	NOVA_API bool LeadingCheckbox(const char* id, bool* value, int indent = 1);
	// 오브젝트 필드 + 오른쪽 버튼들 (예: Profile [필드 ⊙][New][Clone]).
	// 반환: -2 = 아무것도 안 누름, -1 = ⊙, 0.. = 버튼 순번. fieldMin/Max = 끌어 놓기 대상 영역
	NOVA_API int ObjectFieldButtons(const char* label, const char* text, const char* iconName, const char* const* buttons, int buttonCount,
		ImVec2* fieldMin = nullptr, ImVec2* fieldMax = nullptr, int indent = 0);
	// Volume 효과 헤더: [▼][✓] 이름 ........ ALL NONE ⋮ (⋮ 메뉴 = Reset / Remove)
	NOVA_API HeaderAction VolumeEffectHeader(const char* id, const char* title, bool* open, bool* active, bool* all, bool* none);
	// 가운데 정렬 버튼 (예: Add Override)
	NOVA_API bool CenterButton(const char* label, float width = 230.0f);

	// 프리팹 인스턴스 루트의 행: Prefab  [Open] [Select] [Overrides ▾] (Apply All / Revert All)
	NOVA_API void PrefabInstanceRow(class GameObject* instanceRoot);

	// GameObject 헤더 (아이콘, 활성 체크, 이름, Static, Tag, Layer)
	NOVA_API bool GameObjectHeader(bool* active, std::string* name, bool* isStatic, std::string* tag, int* layer);
}
