#include "pch.h"
#include "EditorWindow.h"
#include "EditorTheme.h"
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
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoCollapse | ExtraWindowFlags();
    BeforeBegin();

    EditorGUI::EditorWindowStylePush();

    // 스타일 설정 시작
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    EDITOR_GUI_STYLE_TAB_ROUNDING; 
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_TabBarBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(5, 5));      // 아이템 간 패딩 설정

    ImGui::PushStyleColor(ImGuiCol_TitleBg, EditorTheme::Chrome());
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, EditorTheme::Chrome());
    ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, EditorTheme::Chrome());
    ImGui::PushStyleColor(ImGuiCol_Tab, EditorTheme::Chrome());
    ImGui::PushStyleColor(ImGuiCol_TabHovered, EditorTheme::Rgb(68, 68, 68));
    ImGui::PushStyleColor(ImGuiCol_TabActive, EditorTheme::Panel());

    ImGui::PushStyleColor(ImGuiCol_TabUnfocused, EditorTheme::Chrome());
    ImGui::PushStyleColor(ImGuiCol_TabUnfocusedActive, EditorTheme::Panel());

    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.07f, 0.07f, 0.07f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.07f, 0.07f, 0.07f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.07f, 0.07f, 0.07f, 1.0f));
   
    ImGui::PushStyleColor(ImGuiCol_TabSelectedOverline, ImVec4(0.f, 0.f, 0.f, 0.f)); 
    ImGui::PushStyleColor(ImGuiCol_TabDimmedSelectedOverline, ImVec4(0.f, 0.f, 0.f, 0.f)); 

    EDITOR_GUI_COLOR_TAB_HEADER_BG;   
    ImGui::PushStyleColor(ImGuiCol_Text, EditorTheme::Text()); 

    // 윈도우를 시작합니다
    string windowTitleName = GetImGuiName();

    const ImVec2 padding = WindowPaddingOverride();
    if (padding.x >= 0.0f)
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
    if (m_FloatFrames > 0)
    {
        // 도킹에서 떼고(다음 NewFrame 에 처리) 몇 프레임 동안 위치·크기를 정한다
        --m_FloatFrames;
        if (ImGuiWindow* w = ImGui::FindWindowByName(windowTitleName.c_str()); w && w->DockNode)
            ImGui::DockContextQueueUndockWindow(GImGui, w);
        const ImVec2 origin = ImGui::GetMainViewport()->Pos;
        ImGui::SetNextWindowPos(ImVec2(origin.x + m_FloatPos.x, origin.y + m_FloatPos.y), ImGuiCond_Always);
        ImGui::SetNextWindowSize(m_FloatSize, ImGuiCond_Always);
    }
    if (m_OwnViewport)
    {
        ImGuiWindowClass wc;
        wc.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge;
        ImGui::SetNextWindowClass(&wc);
    }
    const bool visible = ImGui::Begin(windowTitleName.c_str(), &m_IsOpened, windowFlags);
    if (padding.x >= 0.0f)
        ImGui::PopStyleVar();   // Begin 이 이미 읽었다
    if (visible)
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

