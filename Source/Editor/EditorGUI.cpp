#include "pch.h"
#include "EditorGUI.h"
#include "EditorTheme.h"
#include "EditorGUIResourceManager.h"
#include "SkinnedMesh.h"

// EditorDialog
#include "MeshSelectEditorDialog.h"
#include "AnimationClipSelectEditorDialog.h"
#include "SkeletonAvataSelectEditorDailog.h"

EditorTextStyle EditorGUI::DefaultTextStyle = EditorTextStyle();

void EditorGUI::RowSizedBox(float size)
{
    ImGui::SameLine();
    ImGui::Dummy(ImVec2(size, 0));
    ImGui::SameLine();
}

void EditorGUI::Image(wstring path, ImVec2 size)
{
    ComPtr<ID3D11ShaderResourceView> icon = ResourceManager::GetI()->LoadTexture(path);
    if (icon)
    {
        ImGui::Image((void*)icon.Get(), size); 
    }
}

bool EditorGUI::Checkbox(bool& active)
{
    bool isDirty = false;
    // 체크박스 크기 설정

    // 스타일 적용
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));         // 기본 배경 색상
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.25f, 0.25f, 1.0f));  // 호버 시 배경 색상
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.30f, 0.30f, 0.30f, 1.0f));   // 클릭 시 배경 색상
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));            // 테두리 색상

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);  // 라운딩 적용
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f); // 테두리 두께 적용

    // 20x20 크기의 버튼을 체크박스로 사용
    if (ImGui::Button("##Checkbox", EDITOR_GUI_SIZE_CHECKBOX))
    {
        active = !active; 
        isDirty = true;
    }

    // 체크 상태에 따라 체크 표시 그리기
    // 체크 표시 (V 표시를 그리는 부분)
    if (active)
    {
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        ImVec2 p_min = ImGui::GetItemRectMin();
        ImVec2 p_max = ImGui::GetItemRectMax();
  
        ImVec2 center = ImVec2((p_min.x + p_max.x) * 0.5f, (p_min.y + p_max.y) * 0.5f);
        draw_list->AddLine(ImVec2(p_min.x + 4, center.y), ImVec2(center.x - 2, p_max.y - 4), ImColor(255, 255, 255, 255), 2.0f); // 왼쪽 아래로
        draw_list->AddLine(ImVec2(center.x - 2, p_max.y - 4), ImVec2(p_max.x - 4, p_min.y + 4), ImColor(255, 255, 255, 255), 2.0f); // 오른쪽 위로
    }

    // 스타일 복원
    ImGui::PopStyleVar(2);  // 라운딩과 테두리 복원
    ImGui::PopStyleColor(4); // 색상 복원

    return isDirty;
}

bool EditorGUI::InputField(string& inputText)
{
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);

    bool changed = ImGui::InputText("##Name", &inputText[0], inputText.capacity() + 1);

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);

    return changed;
}

void EditorGUI::Label(string text, EditorTextStyle guiStyle, ImVec4 color) 
{
    ImFont* font = EditorGUIResourceManager::GetI()->LoadFont(guiStyle);

    if (font == nullptr)
    {
        ImGui::Text(text.c_str());
        return;
    }

    ImGui::PushFont(font);
    ImGui::PushStyleColor(ImGuiCol_Text, color); 
    ImGui::Text(text.c_str()); 
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void EditorGUI::LabelHeader(string text)
{
    EditorTextStyle style;
    style.FontSize = 14;
    style.Bold = true;
    ImFont* font = EditorGUIResourceManager::GetI()->LoadFont(style);

    EDITOR_GUI_FIELD_PADDING;

    if (font == nullptr)
    {
        ImGui::Text(text.c_str());
        return;
    }

    ImGui::PushFont(font);
    ImGui::Text(text.c_str());
    ImGui::PopFont();
}

bool EditorGUI::Button(string text, Vec2 size, Color color)
{
    // Unity 버튼 색상에 맞는 ImGui 색상 설정
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(color.x, color.y, color.z, color.w));  // (기본 버튼 배경)
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.37f, 0.37f, 0.37f, 1.0f));    // (마우스 올렸을 때)
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.43f, 0.43f, 0.43f, 1.0f));     // (클릭했을 때)
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));                // (텍스트)
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 0));  // 패딩 (좌우, 상하) 2px 설정
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));  // 패딩 (좌우, 상하) 2px 설정
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);         // 라운드 값 2 적용
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.4f);       // 테두리 두께 1 적용

    bool clicked = ImGui::Button(text.c_str(), ImVec2(size.x, size.y));

    ImGui::PopStyleVar(4);   // 라운드와 테두리 두께 스타일 복원
    ImGui::PopStyleColor(5); // 테두리와 버튼 색상 스타일 복원 (총 5개의 스타일 팝)

    return clicked;
}

bool EditorGUI::IconButton_FA(string text, EditorTextStyle guiStyle, ImVec2 size, int iconSize, ImVec4 backgroundColor)
{
    // Unity 버튼 색상에 맞는 ImGui 색상 설정
    ImGui::PushStyleColor(ImGuiCol_Button, backgroundColor);                 // (기본 버튼 배경)
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.37f, 0.37f, 0.37f, 1.0f));   // (마우스 올렸을 때)
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.43f, 0.43f, 0.43f, 1.0f));    // (클릭했을 때)
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.9f, 1.0f));               // (텍스트)
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 0));  // 패딩 (좌우, 상하) 2px 설정
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));  // 패딩 (좌우, 상하) 2px 설정
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);         // 라운드 값 2 적용
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.4f);       // 테두리 두께 1 적용

    EditorTextStyle style; 
    style.FontSize = iconSize;
    style.Bold = true; 
    ImFont* font = EditorGUIResourceManager::GetI()->LoadFont(style); 

    bool clicked = false;
    if (font) 
    { 
        ImGui::PushFont(font);
        clicked = ImGui::Button(text.c_str(), ImVec2(size.x, size.y));
        ImGui::PopFont();
    }

    ImGui::PopStyleVar(4);   // 라운드와 테두리 두께 스타일 복원
    ImGui::PopStyleColor(5); // 테두리와 버튼 색상 스타일 복원 (총 5개의 스타일 팝)

    return clicked;
}

bool EditorGUI::ImageButton(wstring path, ImVec2 size, ImVec2 padding)
{
    // Texture 로드
    ComPtr<ID3D11ShaderResourceView> icon = ResourceManager::GetI()->LoadTexture(path);
    if (!icon)
        return false;

    // Unity 버튼 색상에 맞는 ImGui 색상 설정
    Color color = Color(0.31f, 0.31f, 0.31f, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(color.x, color.y, color.z, color.w));  // (기본 버튼 배경)
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.37f, 0.37f, 0.37f, 1.0f));    // (마우스 올렸을 때)
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.43f, 0.43f, 0.43f, 1.0f));     // (클릭했을 때)
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, padding);       // 패딩 
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);         // 라운드 값 2 적용
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);       // 테두리 두께 1 적용

    bool clicked = ImGui::ImageButton((void*)icon.Get(), size);

    ImGui::PopStyleVar(3);   // 라운드와 테두리 두께 스타일 복원
    ImGui::PopStyleColor(4); // 테두리와 버튼 색상 스타일 복원 (총 5개의 스타일 팝)

    return clicked;
}

bool EditorGUI::ComponentHeader(string title, wstring icon, bool& isOpened)
{
    // 창의 너비를 가져와 버튼 크기로 설정
    ImVec2 availableSize = ImGui::GetContentRegionAvail();

    // 회색 배경색 설정
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.24f, 0.24f, 0.24f, 1.0f));            // 기본 상태
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.37f, 0.37f, 0.37f, 1.0f));     // 마우스 오버 시
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.43f, 0.43f, 0.43f, 1.0f));      // 클릭 상태

    // 패딩과 둥글기 설정
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));   // 패딩을 수직으로 조정하여 높이 제어
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);

    ImGui::PushStyleColor(ImGuiCol_Button, EDITOR_GUI_COLOR_BUTTON_BG);             // 기본 버튼 색상 
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, EDITOR_GUI_COLOR_BUTTON_HOBER);   // 호버 시 색상 
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, EDITOR_GUI_COLOR_BUTTON_BG);       // 클릭 시 색상 
    string component_inspector_id = "##ComponentInspectorHeaderButton" + title;
    if (ImGui::Button(component_inspector_id.c_str(), ImVec2(availableSize.x, 32)))
    {
        isOpened = !isOpened; 
    }
    ImGui::PopStyleColor(3);

    ImGui::SameLine(); 
    ImGui::SetCursorPosX(-2);
    EditorGUI::LabelHeader(ICON_FA_SORT_DOWN); 
    
    ImGui::SameLine();
    ImGui::SetCursorPosX(30); 
    EditorGUI::Image(icon, ImVec2(32, 32));

    ImGui::SameLine();
    ImGui::SetCursorPosX(70);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4);
    EditorGUI::Checkbox(isOpened);

    ImGui::SameLine();
    ImGui::SetCursorPosX(88); 
    EditorGUI::LabelHeader(title);  

    // 스타일 복원
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(3);    // 색상 복원 (3개의 색상)

    return isOpened;
}

void EditorGUI::ComponentDivider()
{
    Divider(Color(0, 0, 0, 1.0f));
}

void EditorGUI::Divider(Color color, float height)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    float lineHeight = height;
    ImU32 imColor = ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, color.w));

    draw_list->AddRectFilled(ImVec2(p.x, p.y), ImVec2(p.x + ImGui::GetContentRegionAvail().x, p.y + lineHeight), imColor);

    ImGui::Dummy(ImVec2(0.0f, lineHeight));

    ImGui::PopStyleVar(3);
}

void EditorGUI::Spacing(Vec2 size)
{
    ImGui::Dummy(ImVec2(size.x, size.y));
}

bool EditorGUI::BoolField(string title, bool& active)
{
    bool isDirty = false;
    float fieldWidth = EDITOR_GUI_FIELD_WIDTH;

    EDITOR_GUI_FIELD_PADDING;
    EditorGUI::Label(title);
    EDITOR_GUI_FIELD_SPACING(fieldWidth - 8);

    EditorGUI::FieldStylePush();

    string id = title + "FloatField";
    if (ImGui::BeginChild(id.c_str(), ImVec2(fieldWidth, FIELD_DEFAULT_HEIGHT), true,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        ImGui::Dummy(ImVec2(4, 0));
        ImGui::SameLine();
        if (Checkbox(active)) {  
            isDirty = true; 
        } 
    }
    ImGui::EndChild();

    EditorGUI::FieldStylePop();

    return isDirty;
}

bool EditorGUI::FloatField(string title, float& v, ImVec2 size)
{
    bool isDirty = false;

    EditorGUI::ContainerStylePush();

    if (ImGui::BeginChild((title + "Float Field Group").c_str(), ImVec2(size.x, FIELD_DEFAULT_HEIGHT), true,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        EditorGUI::Label(title);
        ImGui::SameLine();
        ImGui::Dummy(ImVec2(6, 0));
        ImGui::SameLine();
        
        EditorGUI::DropFieldStylePush();

        string float_dropfield_id = title + "Float Container";
        float float_dropfield_width = ImGui::GetContentRegionAvail().x;
        if (ImGui::BeginChild(float_dropfield_id.c_str(), ImVec2(float_dropfield_width, FIELD_DEFAULT_HEIGHT), true, 
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
        {
            string float_id = "##" + title + "Float";
            ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);

            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));            // 드래그 필드 배경을 투명하게 설정
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));     // 호버 시 배경색도 투명하게 설정
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));      // 클릭 시 배경색도 투명하게 설정
            ImGui::PushStyleColor(ImGuiCol_Text, EDITOR_GUI_COLOR_LABEL_A);       

            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 4);
            isDirty = ImGui::DragFloat(float_id.c_str(), &v, 0.1f, -FLT_MAX, FLT_MAX, "%.3f");

            ImGui::PopStyleColor(4);

            ImGui::PopItemWidth();
        }
        ImGui::EndChild();  

        EditorGUI::DropFieldStylePop();
    }
    ImGui::EndChild(); 

    EditorGUI::ContainerStylePop();

    return isDirty;
}
bool EditorGUI::FloatField(string title, float& v)
{
    bool isDirty = false;
    float fieldWidth = EDITOR_GUI_FIELD_WIDTH;

    EDITOR_GUI_FIELD_PADDING;
    EditorGUI::Label(title);
    EDITOR_GUI_FIELD_SPACING(fieldWidth - 8);

    EditorGUI::DropFieldStylePush();

    string id = title + "FloatField";
    float fieldWidth_container = fieldWidth;
    if (ImGui::BeginChild(id.c_str(), ImVec2(fieldWidth_container, FIELD_DEFAULT_HEIGHT), true,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        string field_id = "##" + title; 
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x); 

        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));            // 드래그 필드 배경을 투명하게 설정 
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));     // 호버 시 배경색도 투명하게 설정 
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));      // 클릭 시 배경색도 투명하게 설정 

        if (ImGui::DragFloat(id.c_str(), &v, 0.1f, -FLT_MAX, FLT_MAX, "%.3f"))
        {
            isDirty = true;
        }

        ImGui::PopStyleColor(3);
        ImGui::PopItemWidth();
    }
    ImGui::EndChild();

    EditorGUI::DropFieldStylePop();

    return isDirty;
}

bool EditorGUI::Vector3Field(string title, Vec3& vec3)
{
    bool isDirty = false;
    float fieldWidth = EDITOR_GUI_LARGE_FIELD_WIDTH;

    EDITOR_GUI_FIELD_PADDING;
    EditorGUI::Label(title);
    EDITOR_GUI_FIELD_SPACING(fieldWidth);

    ImGui::PushID(title.c_str());

    float itemSpacing = 6.0f;
    float totalItemWidth = (fieldWidth - itemSpacing * 2.0f) / 3.0f;
    float btnWidth = 18.0f;
    float inputWidth = totalItemWidth - btnWidth;

    float resetVal = (title == "Scale") ? 1.0f : 0.0f;

    auto DrawAxis = [&](const char* axisLabel, float& val, const ImVec4& colorNormal, const ImVec4& colorHover, const ImVec4& colorActive)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 0));

        // Unity Colored Axis Button/Badge
        ImGui::PushStyleColor(ImGuiCol_Button, colorNormal);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colorHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, colorActive);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

        if (ImGui::Button(axisLabel, ImVec2(btnWidth, 22.0f)))
        {
            val = resetVal;
            isDirty = true;
        }
        ImGui::PopStyleColor(4);

        ImGui::SameLine();

        // Dark Input Drag Box
        ImGui::PushItemWidth(inputWidth);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.12f, 0.12f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.18f, 0.18f, 0.18f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.24f, 0.24f, 0.24f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, EDITOR_GUI_COLOR_LABEL_A);

        std::string dragId = std::string("##") + axisLabel;
        if (ImGui::DragFloat(dragId.c_str(), &val, 0.1f, -FLT_MAX, FLT_MAX, "%.2f"))
        {
            isDirty = true;
        }

        ImGui::PopStyleColor(4);
        ImGui::PopItemWidth();
        ImGui::PopStyleVar(2);
    };

    // X: Unity Red #E04343
    DrawAxis("X", vec3.x,
        ImVec4(0.85f, 0.24f, 0.24f, 1.0f),
        ImVec4(0.95f, 0.32f, 0.32f, 1.0f),
        ImVec4(0.75f, 0.18f, 0.18f, 1.0f));

    ImGui::SameLine(0, itemSpacing);

    // Y: Unity Green #66BB6A
    DrawAxis("Y", vec3.y,
        ImVec4(0.35f, 0.72f, 0.36f, 1.0f),
        ImVec4(0.42f, 0.82f, 0.44f, 1.0f),
        ImVec4(0.28f, 0.62f, 0.28f, 1.0f));

    ImGui::SameLine(0, itemSpacing);

    // Z: Unity Blue #42A5F5
    DrawAxis("Z", vec3.z,
        ImVec4(0.26f, 0.58f, 0.96f, 1.0f),
        ImVec4(0.35f, 0.68f, 1.0f, 1.0f),
        ImVec4(0.20f, 0.48f, 0.82f, 1.0f));

    ImGui::PopID();

    return isDirty;
}
bool EditorGUI::MaterialField(string title, shared_ptr<UMaterial>& material, wstring& materialPath)
{
    bool isDirty = false;
    float fieldWidth = EDITOR_GUI_FIELD_WIDTH;

    EDITOR_GUI_FIELD_PADDING;
    EditorGUI::Label(title);
    EDITOR_GUI_FIELD_SPACING(fieldWidth);

    EditorGUI::DropFieldStylePush();

    if (ImGui::BeginChild("Meterial Field", ImVec2(fieldWidth, FIELD_DEFAULT_HEIGHT), true,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse));
    {
        // Material 이름 또는 "None" 출력
        if (material == nullptr)
        {
            EditorGUI::Label("None");
        }
        else
        {
            EditorGUI::Label(material->GetName());
        }

        ImGui::SameLine();
        ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x - EDITOR_GUI_BUTTON_WIDTH, 0));
        ImGui::SameLine();

        if (EditorGUI::Button("+", Vec2(20, 20), Color(0.20f, 0.20f, 0.20f, 1.0f)))
        {
            wstring filePath = EditorUtility::OpenFileDialog(L"", L"Material", { L"mat" });
            filePath = PathManager::GetI()->GetCutSolutionPath(filePath);

            if (!filePath.empty())
            {
                material = ResourceManager::GetI()->LoadMaterial(wstring_to_string(filePath));
                materialPath = filePath;
                isDirty = true;
            }
        }
    }
    ImGui::EndChild();

    EditorGUI::DropFieldStylePop();

    return isDirty;
}
bool EditorGUI::MeshField(string title, shared_ptr<Mesh>& mesh, wstring& meshFilePath, int& subsetIndex) 
{
    bool isDirty = false;
    float fieldWidth = EDITOR_GUI_FIELD_WIDTH;

    EDITOR_GUI_FIELD_PADDING;
    EditorGUI::Label(title);
    EDITOR_GUI_FIELD_SPACING(fieldWidth - 8);

    EditorGUI::DropFieldStylePush();

    if (ImGui::BeginChild("Mesh Field", ImVec2(fieldWidth, FIELD_DEFAULT_HEIGHT), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        if (mesh == nullptr)
        {
            EditorGUI::Label(" None (Mesh)");
        }
        else
        {
            ImGui::PushItemWidth(fieldWidth - (EDITOR_GUI_BUTTON_WIDTH + 4)); 
            ImGui::Dummy(ImVec2(1, 0));
            ImGui::SameLine();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4);
            EditorGUI::Image(L"\\ProjectSetting\\icons\\icon_mesh.png", ImVec2(20, 20));
            ImGui::SameLine(); 
            ImGui::Dummy(ImVec2(4, 0)); 
            ImGui::SameLine(); 
            EditorGUI::Label(mesh->Name); 
        }

        ImGui::SameLine();
        ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x - 28, 0)); 
        ImGui::SameLine();

        if (EditorGUI::Button(ICON_FA_CIRCLE_DOT, Vec2(28, 28))) 
        {
            shared_ptr<SkinnedMesh> skinned_mesh_dummy;
            MeshSelectEditorDialog::Open(mesh, skinned_mesh_dummy, meshFilePath, subsetIndex, MESH_SELECT_DIALOG_TYPE::MESH_STATIC);
        }
    }
    ImGui::EndChild();

    EditorGUI::DropFieldStylePop();

    return isDirty;
}

bool EditorGUI::SkinnedMeshField(string title, shared_ptr<SkinnedMesh>& mesh, wstring& meshFilePath, int& subsetIndex)
{
    bool isDirty = false;
    float fieldWidth = EDITOR_GUI_FIELD_WIDTH;

    EDITOR_GUI_FIELD_PADDING;
    EditorGUI::Label(title);
    EDITOR_GUI_FIELD_SPACING(fieldWidth - 8);

    EditorGUI::DropFieldStylePush();

    if (ImGui::BeginChild("Mesh Field", ImVec2(fieldWidth, FIELD_DEFAULT_HEIGHT), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        if (mesh == nullptr)
        {
            EditorGUI::Label(" None (Skinned Mesh)");
        }
        else
        {
            ImGui::PushItemWidth(fieldWidth - (EDITOR_GUI_BUTTON_WIDTH + 4)); 
            ImGui::Dummy(ImVec2(1, 0));
            ImGui::SameLine(); 
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4);
            EditorGUI::Image(L"\\ProjectSetting\\icons\\icon_mesh.png", ImVec2(20, 20));
            ImGui::SameLine();
            ImGui::Dummy(ImVec2(4, 0));
            ImGui::SameLine(); 
            EditorGUI::Label(mesh->Name); 
        }

        ImGui::SameLine();
        ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x - 28, 0));
        ImGui::SameLine();

        if (EditorGUI::Button(ICON_FA_CIRCLE_DOT, Vec2(28, 28)))
        {
            shared_ptr<Mesh> mesh_dummy = nullptr; 
            MeshSelectEditorDialog::Open(mesh_dummy, mesh, meshFilePath, subsetIndex, MESH_SELECT_DIALOG_TYPE::MESH_SKINNED); 
        }
    }
    ImGui::EndChild();

    EditorGUI::DropFieldStylePop();

    return isDirty;
}

bool EditorGUI::AnimationClipField(string title, shared_ptr<AnimationClip>& animationClip, string& animationClipPath, int index)
{
    bool isDirty = false;
    float fieldWidth = EDITOR_GUI_FIELD_WIDTH;

    EDITOR_GUI_FIELD_PADDING;
    EditorGUI::Label(title);
    EDITOR_GUI_FIELD_SPACING(fieldWidth - 8);

    EditorGUI::DropFieldStylePush();

    if (ImGui::BeginChild("AnimationClip Field", ImVec2(fieldWidth, FIELD_DEFAULT_HEIGHT), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        if (animationClip == nullptr)
        {
            EditorGUI::Label(" None (Animation Clip)");
        }
        else
        {
            ImGui::PushItemWidth(fieldWidth - (EDITOR_GUI_BUTTON_WIDTH + 4));
            ImGui::Dummy(ImVec2(1, 0));
            ImGui::SameLine();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4);
            EditorGUI::Image(L"\\ProjectSetting\\icons\\icon_animation_clip.png", ImVec2(20, 20));
            ImGui::SameLine();
            ImGui::Dummy(ImVec2(4, 0));
            ImGui::SameLine();
            EditorGUI::Label(animationClip->Name);
        }

        ImGui::SameLine();
        ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x - 28, 0));
        ImGui::SameLine();

        if (EditorGUI::Button(ICON_FA_CIRCLE_DOT, Vec2(28, 28)))
        {
            AnimationClipSelectEditorDialog::Open(animationClip, animationClipPath, index);
        }
    }
    ImGui::EndChild();

    EditorGUI::DropFieldStylePop();

    return isDirty;
}

bool EditorGUI::SkeletonAvataField(string title, shared_ptr<SkeletonAvataData>& skeletonAvata, string& skeletonAvataPath, int index)
{
    bool isDirty = false;
    float fieldWidth = EDITOR_GUI_FIELD_WIDTH;

    EDITOR_GUI_FIELD_PADDING;
    EditorGUI::Label(title);
    EDITOR_GUI_FIELD_SPACING(fieldWidth - 8);

    EditorGUI::DropFieldStylePush();

    if (ImGui::BeginChild("SkeletonAvata Field", ImVec2(fieldWidth, FIELD_DEFAULT_HEIGHT), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        if (skeletonAvata == nullptr)
        {
            EditorGUI::Label(" None (Skeleton Avata)");
        }
        else
        {
            ImGui::PushItemWidth(fieldWidth - (EDITOR_GUI_BUTTON_WIDTH + 4));
            ImGui::Dummy(ImVec2(1, 0));
            ImGui::SameLine();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4);
            EditorGUI::Image(L"\\ProjectSetting\\icons\\File\\skeleton_avata.png", ImVec2(24, 24));
            ImGui::SameLine();
            ImGui::Dummy(ImVec2(4, 0));
            ImGui::SameLine();
            EditorGUI::Label(skeletonAvata->Name);
        }

        ImGui::SameLine();
        ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x - 28, 0));
        ImGui::SameLine();

        if (EditorGUI::Button(ICON_FA_CIRCLE_DOT, Vec2(28, 28)))
        {
            SkeletonAvataSelectEditorDailog::Open(skeletonAvata, skeletonAvataPath, index);
        }
    }
    ImGui::EndChild();

    EditorGUI::DropFieldStylePop();

    return isDirty;
}

void EditorGUI::ContainerStylePush()
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.f, 0.f, 0.f, 0.f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.f, 0.f, 0.f, 0.f));

    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0)); 
}
void EditorGUI::ContainerStylePop()
{
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(2);
}

void EditorGUI::DropFieldStylePush()
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.07f, 0.07f, 0.07f, 1.0f));

    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);          // 차일드 창 라운드 
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.4f);        // 차일드 창 라운드 
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 4.f);        // 차일드 창 테두리 두께
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 0));  // 차일드 창 내부 패딩
}
void EditorGUI::DropFieldStylePop()
{
    // 적용한 스타일 변수 및 색상 복원
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(2);
}

void EditorGUI::FieldStylePush()
{
    // 차일드 창의 배경 색상과 테두리 색상 적용
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.f, 0.f, 0.f, 0.f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.f, 0.f, 0.f, 0.f));

    // 차일드 창의 라운딩과 내부 패딩 적용
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 0)); 
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

}
void EditorGUI::FieldStylePop()
{
    ImGui::PopStyleVar(5);
    ImGui::PopStyleColor(2);
}

void EditorGUI::ComponentBlockStylePush()
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.219f, 0.219f, 0.219f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));

    // 테두리 두께 설정
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f); 
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);  
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f); 

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4, 4));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 4));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));
}
void EditorGUI::ComponentBlockStylePop()
{
    ImGui::PopStyleVar(7);
    ImGui::PopStyleColor(2);
}

void EditorGUI::EditorWindowStylePush()
{
    // 진한 회색 배경 색상 (#333333 in hex) 설정
    ImGui::PushStyleColor(ImGuiCol_WindowBg, EditorTheme::Panel());  // Unity 패널 배경 #383838
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);

    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);                   // 테두리 두께 설정
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));     // 테두리 색상 (어두운 회색)

    // 창 패딩 설정 (0으로 설정하여 내부 여백 제거)
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
}
void EditorGUI::EditorWindowStylePop()
{
    // 스타일 복원
    ImGui::PopStyleVar(3);  // 테두리 두께 및 라운딩 복원
    ImGui::PopStyleColor(2);  // 배경 색상과 테두리 색상 복원
}
