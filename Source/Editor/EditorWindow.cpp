#include "pch.h"
#include "EditorWindow.h"
#include "EditorGUI.h"
#include "imgui_internal.h" 


EditorWindow::EditorWindow(const string& name, const string& icon)
    : m_WindowTitleName(name),
    m_Icon(icon),
    m_IsDocked(false)
{
}

EditorWindow::~EditorWindow()
{
}

string EditorWindow::GetImGuiName() const
{
    if (m_Icon != "") return "   " + m_Icon + "  " + m_WindowTitleName + "   " + "  ";
    return "   " + m_WindowTitleName + "   " + "  ";
}

void EditorWindow::Render()
{
    if (!m_IsOpened)
        return;
    // 화면의 너비와 높이를 가져옵니다.
    ImGuiIO& io = ImGui::GetIO();

    float window_width = 400.0f;  // 창의 너비
    float window_height = 300.0f; // 창의 높이

    // 창의 크기를 설정
    ImGui::SetNextWindowSize(ImVec2(window_width, window_height), ImGuiCond_FirstUseEver);
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoCollapse;

    EditorGUI::EditorWindowStylePush();

    // 스타일 설정 시작
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    EDITOR_GUI_STYLE_TAB_ROUNDING; 
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_TabBarBorderSize, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(5, 5));      // 아이템 간 패딩 설정

    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Tab, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TabHovered, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TabActive, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));

    ImGui::PushStyleColor(ImGuiCol_TabUnfocused, ImVec4(0.07f, 0.07f, 0.07f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TabUnfocusedActive, ImVec4(0.4f, 0.4f, 0.4f, 0.75f));

    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.07f, 0.07f, 0.07f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.07f, 0.07f, 0.07f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.07f, 0.07f, 0.07f, 1.0f));
   
    ImGui::PushStyleColor(ImGuiCol_TabSelectedOverline, ImVec4(0.f, 0.f, 0.f, 0.f)); 
    ImGui::PushStyleColor(ImGuiCol_TabDimmedSelectedOverline, ImVec4(0.f, 0.f, 0.f, 0.f)); 

    EDITOR_GUI_COLOR_TAB_HEADER_BG;   
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f)); 

    // 윈도우를 시작합니다
    string windowTitleName = GetImGuiName();

    if (ImGui::Begin(windowTitleName.c_str(), &m_IsOpened, windowFlags)) 
    {
        if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
        {
            ImGui::OpenPopup("ContextMenu");
        }

        // 컨텍스트 메뉴 렌더링
        if (ImGui::BeginPopup("ContextMenu"))
        {
            if (ImGui::MenuItem("Add Tab"))
            {
            }
            ImGui::EndPopup();
        }

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

        OnRender(); // 주요 렌더링 함수 호출
        
        ImGui::PopStyleVar();
    }
    ImGui::End();

    // 스타일 복구
    ImGui::PopStyleVar(5);    // WindowRounding, TabRounding, PopupRounding 복구 
    ImGui::PopStyleColor(15); // PushStyleColor로 지정한 모든 색상 복구 

    EditorGUI::EditorWindowStylePop();
}

