#pragma once
#include "EditorGUIStyle.h"

#define FIELD_DEFAULT_HEIGHT	28.f

#define EDITOR_GUI_FIELD_WIDTH (ImGui::GetContentRegionAvail().x * 0.6f)
#define EDITOR_GUI_LARGE_FIELD_WIDTH (ImGui::GetContentRegionAvail().x * 0.7f) 
#define EDITOR_GUI_BUTTON_WIDTH  20

#define EDITOR_GUI_FIELD_PADDING    ImGui::Dummy(ImVec2(12, 0));	\
									ImGui::SameLine()				\

#define EDITOR_GUI_FIELD_SPACING(s)	ImGui::SameLine();													\
									ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x - s - 4, 0));	\
									ImGui::SameLine()													\

// Style
#define EDITOR_GUI_STYLE_TAB_ROUNDING   ImGui::PushStyleVar(ImGuiStyleVar_TabRounding, 24.0f)
// Color 
#define EDITOR_GUI_COLOR_TRANSPARENT	ImVec4(0.f, 0.f, 0.f, 0.f)
#define EDITOR_GUI_COLOR_BG				ImVec4(0.07f, 0.07f, 0.07f, 1.0f)
#define EDITOR_GUI_COLOR_TAB_HEADER_BG	ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, ImVec4(0.07f, 0.07f, 0.07f, 1.0f))  
#define EDITOR_GUI_COLOR_LABEL			ImVec4(0.8f, 0.8f, 0.8f, 1.0f)
#define EDITOR_GUI_COLOR_LABEL_A		ImVec4(0.95f, 0.95f, 0.95f, 1.0f)
#define EDITOR_GUI_COLOR_LABEL_D		ImVec4(0.75f, 0.75f, 0.75f, 1.0f)
#define EDITOR_GUI_COLOR_BUTTON_BG		ImVec4(0.25f, 0.25f, 0.25f, 1.0f)
#define EDITOR_GUI_COLOR_BUTTON_HOBER	ImVec4(0.35f, 0.35f, 0.35f, 1.0f)

// Sise
#define EDITOR_GUI_SIZE_ICON			ImVec2(24, 24)
#define EDITOR_GUI_SIZE_CHECKBOX		ImVec2(22, 22)

class Mesh;
class SkinnedMesh;

class EditorGUI
{
public:
	static EditorTextStyle DefaultTextStyle;

public:
	static void RowSizedBox(float size);

	static void Image(wstring path, ImVec2 size=EDITOR_GUI_SIZE_ICON); 
	static bool Checkbox(bool& active);
	static bool InputField(string& inputText);
	static void Label(string text, EditorTextStyle guiStyle = DefaultTextStyle, ImVec4 color = EDITOR_GUI_COLOR_LABEL);
	static void LabelHeader(string text);
	static bool Button(string text, Vec2 size = Vec2(0, 20), Color color = Color(0.31f, 0.31f, 0.31f, 1.0f));
	static bool IconButton_FA(string text, EditorTextStyle guiStyle = DefaultTextStyle, ImVec2 size = ImVec2(0, 20), int iconSize=24, ImVec4 backgroundColor=EDITOR_GUI_COLOR_BUTTON_BG);
	static bool ImageButton(wstring path, ImVec2 size, ImVec2 padding=ImVec2(2,2));

	static bool ComponentHeader(string title, wstring icon, bool& isOpened);
	static void ComponentDivider();
	static void Divider(Color color, float height = 2.8f);
	static void Spacing(Vec2 size);


public:
	static bool BoolField(string title, bool& active);
	static bool FloatField(string title, float& v, ImVec2 size);
	static bool FloatField(string title, float& v);
	static bool Vector3Field(string title, Vec3& vec3);
	static bool MaterialField(string title, shared_ptr<UMaterial>& material, wstring& materialPath);
	static bool MeshField(string title, shared_ptr<Mesh>& mesh, wstring& meshFilePath, int& subsetIndex);
	static bool SkinnedMeshField(string title, shared_ptr<SkinnedMesh>& mesh, wstring& meshFilePath, int& subsetIndex);
	static bool AnimationClipField(string title, shared_ptr<AnimationClip>& animationClip, string& animationClipPath, int index);
	static bool SkeletonAvataField(string title, shared_ptr<SkeletonAvataData>& skeletonAvata, string& skeletonAvataPath, int index);

	//static bool EnumField(string title);

public:
	static void ContainerStylePush();
	static void ContainerStylePop();

	static void DropFieldStylePush();
	static void DropFieldStylePop();

	static void FieldStylePush();
	static void FieldStylePop();

	static void ComponentBlockStylePush();
	static void ComponentBlockStylePop();

	static void EditorWindowStylePush();
	static void EditorWindowStylePop();
};

