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

void EditorWindow::Render()
{
    if (!m_IsOpened)
        return;
    // ȭ���� �ʺ�� ���̸� �����ɴϴ�.
    ImGuiIO& io = ImGui::GetIO();

    float window_width = 400.0f;  // â�� �ʺ�
    float window_height = 300.0f; // â�� ����

    // â�� ũ�⸦ ����
    ImGui::SetNextWindowSize(ImVec2(window_width, window_height), ImGuiCond_FirstUseEver);
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoCollapse;

    EditorGUI::EditorWindowStylePush();

    // ��Ÿ�� ���� ����
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    EDITOR_GUI_STYLE_TAB_ROUNDING; 
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_TabBarBorderSize, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(5, 5));      // ������ �� �е� ����

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

    // �����츦 �����մϴ�
    string windowTitleName = "";
    if (m_Icon != "") windowTitleName = "   " + m_Icon + "  " + m_WindowTitleName + "   " + "  ";
    else windowTitleName = "   " + m_WindowTitleName + "   " + "  ";

    if (ImGui::Begin(windowTitleName.c_str(), &m_IsOpened, windowFlags)) 
    {
        if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
        {
            ImGui::OpenPopup("ContextMenu");
        }

        // ���ؽ�Ʈ �޴� ������
        if (ImGui::BeginPopup("ContextMenu"))
        {
            if (ImGui::MenuItem("Add Tab"))
            {
            }
            ImGui::EndPopup();
        }

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

        OnRender(); // �ֿ� ������ �Լ� ȣ��
        
        ImGui::PopStyleVar();
    }
    ImGui::End();

    // ��Ÿ�� ����
    ImGui::PopStyleVar(5);    // WindowRounding, TabRounding, PopupRounding ���� 
    ImGui::PopStyleColor(15); // PushStyleColor�� ������ ��� ���� ���� 

    EditorGUI::EditorWindowStylePop();
}

