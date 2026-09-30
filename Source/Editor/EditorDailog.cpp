#include "pch.h"
#include "EditorDailog.h"
#include "EditorGUI.h"
#include "EditorGUIManager.h"

EditorDialog::EditorDialog(const std::string& name)
    : m_WindowName(name)
{
}

EditorDialog::~EditorDialog()
{
}

void EditorDialog::Render()
{
    // 창이 닫혔을 때의 상태를 감지
    if (m_PreviousWindowOpen && !m_WindowOpen)
    {
        Close(); 
        return;
    }

    // 창이 닫혀 있으면 렌더링하지 않음
    if (!m_WindowOpen)
        return;

    ImGui::PushID(m_WindowName.c_str());

    ImGui::SetNextWindowSize(ImVec2(400.0f, 300.0f), ImGuiCond_FirstUseEver);
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDocking 
                                 | ImGuiWindowFlags_NoCollapse
                                 | ImGuiWindowFlags_NoTitleBar;
        
    PushStyle();

    // 창을 최상위로 설정
    ImGui::SetNextWindowFocus();  
    string window_id = "##" + m_WindowName;
    ImGui::Begin(window_id.c_str(), &m_WindowOpen, windowFlags); 

    // Imgui render title bar -----------------------
    PushStyle_TitleBar();
    ImGui::BeginChild(m_WindowName.c_str(), ImVec2(ImGui::GetWindowWidth(), 42), false); // 30px 높이의 커스텀 타이틀바
    ImGui::Dummy(ImVec2(12, 0));
    ImGui::SameLine();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6);
    EditorGUI::Label(m_WindowName); 
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 42);
    if (EditorGUI::IconButton_FA(ICON_FA_XMARK, EditorGUI::DefaultTextStyle, ImVec2(42, 42), 24, ImVec4(0.78f, 0.31f, 0.31f, 1.0f)))
    {
        m_WindowOpen = false;
    }
    ImGui::EndChild();
    PopStyle_TitleBar();
    // ------------------------------------------

    EditorGUI::Divider(Color(0.1f, 0.1f, 0.1f, 1.0f), 2.0f);
   
    // Imgui render body ------------------------
    PushStyle_Body();
    OnRender(); 
    PopStyle_Body();
    // ------------------------------------------

    ImGui::End();
  
    PopStyle();

    ImGui::PopID();
}

void EditorDialog::Show()
{
    m_WindowOpen = true;
    m_PreviousWindowOpen = true;

    EditorGUIManager::GetI()->RegisterEditorDialog(this);
}

void EditorDialog::Close()
{
    EditorGUIManager::GetI()->RemoveEditorDialog(this);  
}





void EditorDialog::PushStyle()
{
    // 스타일 설정 시작
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0)); 
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0)); 

    // 타이틀바 색상 변경
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.156f, 0.156f, 0.156f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.156f, 0.156f, 0.156f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, ImVec4(0.156f, 0.156f, 0.156f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.35f, 0.35f, 0.35f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));

}
void EditorDialog::PopStyle()
{
    ImGui::PopStyleVar(4);      
    ImGui::PopStyleColor(5);   
}

void EditorDialog::PushStyle_TitleBar()
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.135f, 0.135f, 0.135f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
}
void EditorDialog::PopStyle_TitleBar()
{
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}




void EditorDialog::PushStyle_Body()
{
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 12)); 
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12, 12)); 
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.f);
}
void EditorDialog::PopStyle_Body()
{
    ImGui::PopStyleVar(4);
}
