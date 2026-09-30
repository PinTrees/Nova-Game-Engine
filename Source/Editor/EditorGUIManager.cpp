#include "pch.h"
#include "EditorGUIManager.h"
#include "EngineInfo.h"
#include "GraphicsSettings.h"
#include "GraphicsBackendFactory.h"

#include "EditorWindow.h"
#include "imgui_internal.h"
#include "EditorGUI.h"
#include "App.h"

#include "GameObjectFactory.h"
#include "EditorUtility.h"
#include "Scene.h"
#include "PathManager.h"
#include "SelectionManager.h"
SINGLE_BODY(EditorGUIManager)

EditorGUIManager::EditorGUIManager()
    : m_IsInit(false)
{

}

EditorGUIManager::~EditorGUIManager()
{
    Safe_Delete_Vec(m_pEditorWindows);
    Safe_Delete_Vec(m_pEditorDialogs); 
}

void EditorGUIManager::Init()
{
    if (m_IsInit)
        return;

    m_IsInit = true; 

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    //io.ConfigFlags |= ImGuiConfigFlags_DpiEnableScaleViewports; 
    //io.ConfigFlags |= ImGuiConfigFlags_DpiEnableScaleFonts; 

    io.Fonts->Flags |= ImFontAtlasFlags_NoBakedLines;
    io.FontGlobalScale = 1.0f;
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f); 

    ImGui::StyleColorsDark(); 
    SetStyle_Base();  

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_EnableDpiAwareness();  
    ImGui_ImplWin32_Init(Application::GetI()->GetMainHwnd());
    ImGui_ImplDX11_Init(Application::GetI()->GetDevice(), Application::GetI()->GetDeviceContext());

    float fontSize = 24.0f;

    ImFontConfig config;
    config.MergeMode = true; // 기존 폰트와 합쳐 사용 
    config.PixelSnapH = true;
    static const ImWchar icons_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 }; // FontAwesome 범위   
    string fa_path = PathManager::GetI()->GetContentPathS() + "ProjectSetting\\fonts\\fa-solid-900.ttf";
    
    // Load Fonts
    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\malgun.ttf", fontSize, NULL, io.Fonts->GetGlyphRangesKorean());
    io.Fonts->AddFontFromFileTTF(fa_path.c_str(), fontSize - 4, &config, icons_ranges); 
    io.Fonts->Build();
}

void EditorGUIManager::Destroy() 
{
    ImGui_ImplDX11_Shutdown();  
    ImGui_ImplWin32_Shutdown();  
    ImGui::DestroyContext();  
}

void EditorGUIManager::Update()
{
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    for (auto& window : m_pEditorWindows)
    {
        window->Update();
    }
}

// imgui.ini가 없을 때 Unity 기본 레이아웃(Hierarchy | Scene/Game | Inspector, 하단 Project/Console)으로 도킹
void EditorGUIManager::BuildDefaultLayout(ImGuiID dockspaceId, ImVec2 size)
{
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, size);

    ImGuiID center = dockspaceId;
    ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.16f, nullptr, &center);
    ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.22f, nullptr, &center);
    ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.28f, nullptr, &center);

    for (auto& w : m_pEditorWindows)
    {
        const std::string& t = w->GetTitle();
        ImGuiID target = center;
        if (t == "Hierachy") target = left;
        else if (t == "Inspector") target = right;
        else if (t == "Project" || t == "Console" || t == "Animator") target = bottom;
        else if (t == "Unity Hub") continue;
        ImGui::DockBuilderDockWindow(w->GetImGuiName().c_str(), target);
    }
    ImGui::DockBuilderFinish(dockspaceId);
}

void EditorGUIManager::RenderEditorWindows()
{
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(18.0f, 18.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ImVec4(0.07f, 0.07f, 0.07f, 1.0f));

    if (ImGui::BeginMainMenuBar())
    {
        const float menuBarHeight = ImGui::GetFrameHeight();
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(16.0f, 4.0f));

        // 1. File Menu
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("New Scene", "Ctrl+N"))
            {
                SceneManager::GetI()->CreateScene();
            }
            if (ImGui::MenuItem("Open Scene...", "Ctrl+O"))
            {
                std::wstring filePath = EditorUtility::OpenFileDialog(PathManager::GetI()->GetMovePathW(L"Assets\\"), L"Open Scene", std::vector<std::wstring>{ L"scene" });
                if (!filePath.empty())
                {
                    std::wstring relPath = PathManager::GetI()->GetCutSolutionPath(filePath);
                    SceneManager::GetI()->LoadScene(relPath);
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Project Hub...", "Ctrl+H"))
            {
                for (auto& w : m_pEditorWindows)
                {
                    if (w->GetTitle() == "Unity Hub")
                        w->SetIsOpened(true);
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Save Scene", "Ctrl+S"))
            {
                Scene* curr = SceneManager::GetI()->GetCurrentScene();
                if (curr)
                {
                    if (curr->GetScenePath().empty())
                        Scene::SaveNewScene(curr);
                    else
                        Scene::Save(curr);
                }
            }
            if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S"))
            {
                Scene* curr = SceneManager::GetI()->GetCurrentScene();
                if (curr)
                    Scene::SaveNewScene(curr);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4"))
            {
                ::PostQuitMessage(0);
            }
            ImGui::EndMenu();
        }

        // 2. Edit Menu
        if (ImGui::BeginMenu("Edit"))
        {
            if (ImGui::MenuItem("Play / Stop", "Ctrl+P"))
            {
                bool isPlaying = Application::IsPlaying();
                Application::SetPlaying(!isPlaying);
                if (!isPlaying)
                    SceneManager::GetI()->HandlePlay();
                else
                {
                    SelectionManager::ClearSelection();
                    SceneManager::GetI()->HandleStop();
                }
            }
            if (ImGui::MenuItem("Clear Selection"))
            {
                SelectionManager::ClearSelection();
            }
            ImGui::Separator();
            // 렌더링 API 선택 (다음 실행부터 적용)
            if (ImGui::BeginMenu("Graphics API"))
            {
                for (int i = 0; i < static_cast<int>(GraphicsAPI::Count); ++i)
                {
                    GraphicsAPI api = static_cast<GraphicsAPI>(i);
                    bool supported = GraphicsBackendFactory::Create(api)->IsSupported();
                    bool selected = (GraphicsSettings::GetRequestedAPI() == api);
                    std::string label = GraphicsAPIToString(api);
                    if (!supported) label += " (not implemented)";
                    if (ImGui::MenuItem(label.c_str(), nullptr, selected, supported))
                        GraphicsSettings::SetRequestedAPI(api);
                }
                ImGui::Separator();
                ImGui::TextDisabled("Active: %s (restart to apply)", GraphicsAPIToString(GraphicsSettings::GetActiveAPI()));
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }

        // 3. GameObject Menu
        if (ImGui::BeginMenu("GameObject"))
        {
            Scene* currentScene = SceneManager::GetI()->GetCurrentScene();
            auto AddObj = [&](GameObject* obj) {
                if (currentScene && obj) {
                    GameObject* selected = SelectionManager::GetSelectedGameObject();
                    if (selected)
                        obj->SetParent(selected);
                    else
                        currentScene->AddRootGameObject(obj);
                    SelectionManager::SetSelectedGameObject(obj);
                }
            };

            if (ImGui::MenuItem("Create Empty"))
            {
                AddObj(GameObjectFactory::CreateEmpty());
            }

            if (ImGui::BeginMenu("3D Object"))
            {
                if (ImGui::MenuItem("Cube")) AddObj(GameObjectFactory::CreateCube());
                if (ImGui::MenuItem("Sphere")) AddObj(GameObjectFactory::CreateSphere());
                if (ImGui::MenuItem("Cylinder")) AddObj(GameObjectFactory::CreateCylinder());
                if (ImGui::MenuItem("Plane")) AddObj(GameObjectFactory::CreatePlane());
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Light"))
            {
                if (ImGui::MenuItem("Directional Light")) AddObj(GameObjectFactory::CreateDirectionalLight());
                if (ImGui::MenuItem("Point Light")) AddObj(GameObjectFactory::CreatePointLight());
                if (ImGui::MenuItem("Spot Light")) AddObj(GameObjectFactory::CreateSpotLight());
                ImGui::EndMenu();
            }

            if (ImGui::MenuItem("Camera"))
            {
                AddObj(GameObjectFactory::CreateCamera());
            }
            ImGui::EndMenu();
        }

        // 4. Window Menu
        if (ImGui::BeginMenu("Window"))
        {
            for (auto& window : m_pEditorWindows)
            {
                bool opened = window->GetIsOpened();
                if (ImGui::MenuItem(window->GetTitle().c_str(), nullptr, &opened))
                {
                    window->SetIsOpened(opened);
                }
            }
            ImGui::EndMenu();
        }

        // 5. Help Menu
        if (ImGui::BeginMenu("Help"))
        {
            if (ImGui::MenuItem("About " ENGINE_NAME_A))
            {
            }
            ImGui::EndMenu();
        }

        ImGui::PopStyleVar();

        // Center Toolbar (Play / Stop)
        float imageHeight = 14;
        float buttonPaddingX = 16;
        float buttonPaddingY = 8;
        float buttonHeight = imageHeight + buttonPaddingY * 2;
        float centerOffset = (menuBarHeight - buttonHeight) * 0.5f;
        float centerPos = ImGui::GetWindowWidth() * 0.5f - 60.0f;
        if (centerPos > ImGui::GetCursorPosX())
            ImGui::SetCursorPosX(centerPos);

        ImGui::SetCursorPosY(centerOffset);
        if (EditorGUI::ImageButton(L"\\ProjectSetting\\icons\\icon_editor_play.png", ImVec2(imageHeight, imageHeight), ImVec2(buttonPaddingX, buttonPaddingY)))
        {
            Application::SetPlaying(true);
            SceneManager::GetI()->HandlePlay();
        }
        ImGui::Dummy(ImVec2(4, 0));
        ImGui::SetCursorPosY(centerOffset);
        if (EditorGUI::ImageButton(L"\\ProjectSetting\\icons\\icon_editor_stop.png", ImVec2(imageHeight, imageHeight), ImVec2(buttonPaddingX, buttonPaddingY)))
        {
            Application::SetPlaying(false);
            SelectionManager::ClearSelection();
            SceneManager::GetI()->HandleStop();
        }
        ImGui::EndMainMenuBar();
    }
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);

    // 창의 전체 크기와 위치 설정
    // 프레임과 배경을 제거하는 플래그 설정
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoBackground;

    // 전체 화면 크기로 DockSpace 창 설정    
    Vec2 screenSize = Application::GetI()->GetApp()->GetScreenSize();
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + 48)); 
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, viewport->Size.y - 48)); 
    ImGui::SetNextWindowViewport(viewport->ID);  

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f); 
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);

    if (ImGui::Begin("##DockSpace", NULL, window_flags))
    {
        ImGuiID dockspace_id = ImGui::GetID("RootDockspace");  
        if (ImGui::DockBuilderGetNode(dockspace_id) == nullptr)
            BuildDefaultLayout(dockspace_id, ImVec2(viewport->Size.x, viewport->Size.y - 48));
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None); 
    }

    ImGui::End();

    ImGui::PopStyleVar(5);

    for (auto& window : m_pEditorWindows)
    {
        window->Render();
    }
    
    for (auto& dialog : m_pEditorDialogs) 
    { 
        dialog->Render(); 
    } 
}

void EditorGUIManager::RenderAfter()
{
    ImGuiIO& io = ImGui::GetIO(); 
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }
}

void EditorGUIManager::OnResize(Vec2 size)
{
    if (!m_IsInit)
        return;

    //ImGuiIO& io = ImGui::GetIO(); 
    //io.DisplaySize = ImVec2(size.x, size.y);
}

void EditorGUIManager::RegisterWindow(EditorWindow* window)
{
    m_pEditorWindows.push_back(window);
}

void EditorGUIManager::RegisterEditorDialog(EditorDialog* dialog)
{
    m_pEditorDialogs.push_back(dialog); 
}

void EditorGUIManager::RemoveEditorDialog(EditorDialog* dialog)
{
    auto it = std::remove(m_pEditorDialogs.begin(), m_pEditorDialogs.end(), dialog);

    if (it != m_pEditorDialogs.end())
    {
        m_pEditorDialogs.erase(it, m_pEditorDialogs.end());
    }
}


void EditorGUIManager::SetStyle_Base()
{
    ImGuiStyle* style = &ImGui::GetStyle(); 
    
    style->WindowPadding = ImVec2(8.0f, 8.0f);
    style->FramePadding = ImVec2(6.0f, 4.0f);
    style->ItemSpacing = ImVec2(6.0f, 4.0f);
    style->ItemInnerSpacing = ImVec2(4.0f, 4.0f);
    style->IndentSpacing = 16.0f;
    style->ScrollbarSize = 14.0f;
    style->GrabMinSize = 10.0f;

    style->WindowRounding = 0.0f;
    style->ChildRounding = 0.0f;
    style->FrameRounding = 3.0f;
    style->PopupRounding = 4.0f;
    style->ScrollbarRounding = 9.0f;
    style->GrabRounding = 3.0f;
    style->TabRounding = 4.0f;
    style->DockingSeparatorSize = 3.0f;
    
    ImVec4* colors = style->Colors;

    // Unity Dark Theme Color Palette
    const ImVec4 unityDark       = ImVec4(0.15f, 0.15f, 0.15f, 1.00f); // #262626 Panel Background
    const ImVec4 unityMid        = ImVec4(0.22f, 0.22f, 0.22f, 1.00f); // #383838 Window/Tab Background
    const ImVec4 unityLight      = ImVec4(0.33f, 0.33f, 0.33f, 1.00f); // #545454 Buttons/Frames
    const ImVec4 unityHighlight  = ImVec4(0.40f, 0.40f, 0.40f, 1.00f); // #666666 Hover
    const ImVec4 unityActive     = ImVec4(0.28f, 0.28f, 0.28f, 1.00f); // #484848 Active
    const ImVec4 unityBlue       = ImVec4(0.18f, 0.44f, 0.75f, 1.00f); // #2E70BF Unity Selection Blue
    const ImVec4 unityAccentBlue = ImVec4(0.18f, 0.55f, 0.95f, 1.00f); // #2E8CF2 Active tab underline

    colors[ImGuiCol_Text]                  = ImVec4(0.85f, 0.85f, 0.85f, 1.00f);
    colors[ImGuiCol_TextDisabled]          = ImVec4(0.45f, 0.45f, 0.45f, 1.00f);
    colors[ImGuiCol_WindowBg]              = unityDark;
    colors[ImGuiCol_ChildBg]               = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_PopupBg]               = ImVec4(0.18f, 0.18f, 0.18f, 0.98f);
    colors[ImGuiCol_Border]                = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
    colors[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    colors[ImGuiCol_FrameBg]               = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
    colors[ImGuiCol_FrameBgActive]         = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);

    colors[ImGuiCol_TitleBg]               = unityMid;
    colors[ImGuiCol_TitleBgActive]         = unityMid;
    colors[ImGuiCol_TitleBgCollapsed]      = unityMid;

    colors[ImGuiCol_MenuBarBg]             = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.12f, 0.12f, 0.12f, 0.60f);
    colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.35f, 0.35f, 0.35f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.45f, 0.45f, 0.45f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.55f, 0.55f, 0.55f, 1.00f);

    colors[ImGuiCol_CheckMark]             = unityAccentBlue;
    colors[ImGuiCol_SliderGrab]            = unityLight;
    colors[ImGuiCol_SliderGrabActive]      = unityBlue;

    colors[ImGuiCol_Button]                = unityLight;
    colors[ImGuiCol_ButtonHovered]         = unityHighlight;
    colors[ImGuiCol_ButtonActive]          = unityActive;

    colors[ImGuiCol_Header]                = unityBlue;
    colors[ImGuiCol_HeaderHovered]         = ImVec4(0.22f, 0.50f, 0.82f, 0.85f);
    colors[ImGuiCol_HeaderActive]          = ImVec4(0.16f, 0.40f, 0.70f, 1.00f);

    colors[ImGuiCol_Separator]             = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
    colors[ImGuiCol_SeparatorHovered]      = unityAccentBlue;
    colors[ImGuiCol_SeparatorActive]       = unityBlue;

    colors[ImGuiCol_ResizeGrip]            = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_ResizeGripHovered]     = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_ResizeGripActive]      = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    // Unity Tab Styling
    colors[ImGuiCol_Tab]                   = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
    colors[ImGuiCol_TabHovered]            = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);
    colors[ImGuiCol_TabSelected]           = unityMid;
    colors[ImGuiCol_TabSelectedOverline]   = unityAccentBlue;
    colors[ImGuiCol_TabDimmed]             = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    colors[ImGuiCol_TabDimmedSelected]     = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0.35f, 0.35f, 0.35f, 1.00f);

    colors[ImGuiCol_DockingPreview]        = ImVec4(0.18f, 0.44f, 0.75f, 0.70f);
    colors[ImGuiCol_DockingEmptyBg]        = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);

    colors[ImGuiCol_TableHeaderBg]         = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
    colors[ImGuiCol_TableBorderStrong]     = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
    colors[ImGuiCol_TableBorderLight]      = ImVec4(0.16f, 0.16f, 0.16f, 0.50f);
    colors[ImGuiCol_TableRowBg]            = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt]         = ImVec4(1.00f, 1.00f, 1.00f, 0.03f);

    colors[ImGuiCol_TextSelectedBg]        = ImVec4(0.18f, 0.44f, 0.75f, 0.50f);
    colors[ImGuiCol_DragDropTarget]        = unityAccentBlue;
    colors[ImGuiCol_NavHighlight]          = unityAccentBlue;
}
