#include "pch.h"
#include "UndoSystem.h"
#include "ProfilerEditorWindow.h"
#include "PackageManagerWindow.h"
#include "AutoSave.h"
#include "FrameProfiler.h"
#include "EditorGUIManager.h"
#include "ImGuiGL.h"
#include "ImGuiGfx.h"
#include "App.h"
#include "EngineInfo.h"
#include "GraphicsSettings.h"
#include "ProjectSettingsWindow.h"
#include "PreferencesWindow.h"
#include "BuildSettingsWindow.h"
#include "BuildPipeline.h"
#include "NovaCodeWindow.h"
#include "AddComponentMenu.h"
#include "GameViewEditorWindow.h"
#include "ScriptEngine.h"
#include "ObjectPicker.h"
#include "GraphicsBackendFactory.h"
#include "HubProject.h"
#include "EditorTheme.h"
#include "GameObjectMenu.h"
#include "ComponentFactory.h"
#include <shellapi.h>

#include "EditorWindow.h"
#include "imgui_internal.h"
#include "EditorGUI.h"
#include "ImportSettingsInspector.h"
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

void EditorGUIManager::Init(bool hubMode)
{
    if (m_IsInit)
        return;

    m_IsInit = true; 

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    const bool openGL = Application::GetI()->GetApp() && Application::GetI()->GetApp()->IsOpenGL();
    const bool vulkan = Application::GetI()->GetApp() && Application::GetI()->GetApp()->IsVulkan();
    if (!hubMode)
    {
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        if (!vulkan)   // Vulkan: 창 밖 OS 창(뷰포트)은 아직 없다 (창마다 스왑체인 — ImGuiGfx.h)
            io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;   // 창 밖으로 뺀 창 = OS 창 (DX11: 스왑 체인, OpenGL: ImGuiGL 이 창 DC 에 본 컨텍스트를 붙여 그림)
    }
    else
    {
        // Hub는 에디터의 imgui.ini(창 배치)를 덮어쓰지 않는다.
        io.IniFilename = nullptr;
    }
    if (!hubMode)
    {
        // 창 이름/레이아웃 구조가 바뀌면 파일 이름의 버전을 올려 저장된 배치를 한 번 초기화한다.
        io.IniFilename = "nova_layout_v2.ini";
    }
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
    // ImGui DX11 백엔드는 진짜 D3D11 객체로 (그림 텍스처 ImTextureID = GfxShaderResourceView* — 백엔드가 풀어 씀)
    if (openGL)   // OpenGL: ImGuiGL (ImTextureID = Gfx 뷰 → GL 텍스처)
        ImGuiGL::Init();
    else if (vulkan)   // Vulkan: Gfx 층 + 56. ImGui.fx
        ImGuiGfx::Init();
    else
        ImGui_ImplDX11_Init(static_cast<ID3D11Device*>(Application::GetI()->GetDevice()->Native()), static_cast<ID3D11DeviceContext*>(Application::GetI()->GetDeviceContext()->Native()));

    // Hub는 창 DPI 배율에 맞춰 폰트 크기를 정한다. (에디터는 기존 고정 크기 유지)
    float dpiScale = hubMode ? (float)GetDpiForWindow(Application::GetI()->GetMainHwnd()) / 96.0f : 1.0f;
    float fontSize = hubMode ? 17.0f * dpiScale : (float)EditorTheme::FontSize;

    ImFontConfig config;
    config.MergeMode = true; // 기존 폰트와 합쳐 사용 
    config.PixelSnapH = true;
    static const ImWchar icons_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 }; // FontAwesome 범위   
    string fa_path = PathManager::GetI()->GetEnginePathS() + "ProjectSetting\\fonts\\fa-solid-900.ttf";
    
    // Load Fonts
    if (!hubMode)
    {
        // 본문 폰트: Pretendard(있을 때) 또는 Segoe UI + 맑은 고딕(한글), 그리고 Font Awesome(아이콘)
        static const ImWchar textRanges[] = { 0x0020, 0x00FF, 0x2010, 0x2027, 0x2190, 0x21FF, 0x1100, 0x11FF, 0x3000, 0x303F, 0x3130, 0x318F, 0xAC00, 0xD7A3, 0xFF00, 0xFFEF, 0 };
        static const ImWchar hangulRanges[] = { 0x1100, 0x11FF, 0x3000, 0x303F, 0x3130, 0x318F, 0xAC00, 0xD7A3, 0xFF00, 0xFFEF, 0 };

        ImFontConfig textCfg;                 // 글자를 또렷하게: 가로 오버샘플링을 줄이고 픽셀에 맞춘다
        textCfg.OversampleH = 1;
        textCfg.OversampleV = 1;
        textCfg.PixelSnapH = true;
        textCfg.RasterizerMultiply = 1.2f;   // 힌팅 없는 래스터라이저에서 획이 가늘어 보이는 것을 보정
        ImFontConfig mergeCfg = textCfg;
        mergeCfg.MergeMode = true;

        auto loadTextFont = [&](bool bold)
        {
            bool pretendard = false;
            std::string file = EditorTheme::FontFile(bold, pretendard);
            io.Fonts->AddFontFromFileTTF(file.c_str(), fontSize, &textCfg, pretendard ? textRanges : io.Fonts->GetGlyphRangesDefault());
            if (!pretendard)
                io.Fonts->AddFontFromFileTTF(bold ? "C:\\Windows\\Fonts\\malgunbd.ttf" : "C:\\Windows\\Fonts\\malgun.ttf", fontSize, &mergeCfg, hangulRanges);
            io.Fonts->AddFontFromFileTTF(fa_path.c_str(), fontSize - 1, &config, icons_ranges);
        };
        loadTextFont(false);   // Fonts[0]: 일반
        loadTextFont(true);    // Fonts[1]: 굵게 (UnityGUI::BoldFont)

        // Fonts[2]: 코드 편집기(NOVA Code) 고정폭 — Consolas + 한글은 맑은 고딕 (주석/문자열의 한글)
        ImFontConfig codeCfg = textCfg;
        codeCfg.RasterizerMultiply = 1.1f;
        const char* codeFont = std::filesystem::exists("C:\\Windows\\Fonts\\consola.ttf") ? "C:\\Windows\\Fonts\\consola.ttf" : "C:\\Windows\\Fonts\\cour.ttf";
        io.Fonts->AddFontFromFileTTF(codeFont, 16.0f, &codeCfg, io.Fonts->GetGlyphRangesDefault());
        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\malgun.ttf", 15.0f, &mergeCfg, hangulRanges);

        // 큰 굵은 글꼴: 키운 비트맵(SetWindowFontScale)은 흐려지므로 그 크기로 따로 굽는다
        auto loadBold = [&](float size)
        {
            bool pretendard = false;
            std::string file = EditorTheme::FontFile(true, pretendard);
            ImFontConfig bigCfg = textCfg;
            bigCfg.OversampleH = 2;   // 큰 글자는 가로 오버샘플링이 있어야 획이 매끈하다
            io.Fonts->AddFontFromFileTTF(file.c_str(), size, &bigCfg, pretendard ? textRanges : io.Fonts->GetGlyphRangesDefault());
            ImFontConfig bigMerge = bigCfg;
            bigMerge.MergeMode = true;
            if (!pretendard)
                io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\malgunbd.ttf", size, &bigMerge, hangulRanges);
        };
        loadBold(22.0f);   // Fonts[3]: 제목 (UnityGUI::TitleFont — Package Manager 패키지 이름)
        loadBold(19.0f);   // Fonts[4]: 머리글 (UnityGUI::HeaderFont — Preferences · Project Settings 분류 제목)
    }
    else
    {
        // Hub: 에디터와 같은 글꼴(Pretendard, 없으면 Segoe UI + 맑은 고딕)을 더 크게. 순서 = HubApp 의 HubFont 번호
        //  [0] 본문 18  [1] 큰 제목(굵게) 28  [2] 강조(SemiBold) 18  [3] 작은 글자 15  [4] 앱 바 아이콘 19  [5] 앱 이름(SemiBold) 21
        static const ImWchar textRanges[] = { 0x0020, 0x00FF, 0x2010, 0x2027, 0x2190, 0x21FF, 0x2500, 0x257F, 0x1100, 0x11FF, 0x3000, 0x303F, 0x3130, 0x318F, 0xAC00, 0xD7A3, 0xFF00, 0xFFEF, 0 };
        static const ImWchar hangulRanges[] = { 0x1100, 0x11FF, 0x3000, 0x303F, 0x3130, 0x318F, 0xAC00, 0xD7A3, 0xFF00, 0xFFEF, 0 };
        ImFontConfig textCfg;
        textCfg.OversampleH = 2;
        textCfg.OversampleV = 1;
        textCfg.PixelSnapH = true;
        textCfg.RasterizerMultiply = 1.1f;
        ImFontConfig mergeCfg = textCfg;
        mergeCfg.MergeMode = true;
        auto loadText = [&](bool bold, float size, bool icons)
        {
            bool pretendard = false;
            const std::string file = EditorTheme::FontFile(bold, pretendard);
            io.Fonts->AddFontFromFileTTF(file.c_str(), size, &textCfg, pretendard ? textRanges : io.Fonts->GetGlyphRangesDefault());
            if (!pretendard)
                io.Fonts->AddFontFromFileTTF(bold ? "C:\\Windows\\Fonts\\malgunbd.ttf" : "C:\\Windows\\Fonts\\malgun.ttf", size, &mergeCfg, hangulRanges);
            if (icons)
                io.Fonts->AddFontFromFileTTF(fa_path.c_str(), size * 0.86f, &config, icons_ranges);
        };
        loadText(false, 18.0f * dpiScale, true);   // [0]
        loadText(true, 28.0f * dpiScale, true);    // [1]
        loadText(true, 18.0f * dpiScale, true);    // [2]
        loadText(false, 15.0f * dpiScale, true);   // [3]
        ImFontConfig iconCfg;
        iconCfg.PixelSnapH = true;
        iconCfg.OversampleH = 2;
        io.Fonts->AddFontFromFileTTF(fa_path.c_str(), 19.0f * dpiScale, &iconCfg, icons_ranges);   // [4]
        loadText(true, 21.0f * dpiScale, false);   // [5]
    }
    io.Fonts->Build();
}

void EditorGUIManager::Destroy() 
{
    if (Application::GetI()->GetApp() && Application::GetI()->GetApp()->IsOpenGL())
        ImGuiGL::Shutdown();
    else if (Application::GetI()->GetApp() && Application::GetI()->GetApp()->IsVulkan())
        ImGuiGfx::Shutdown();
    else
        ImGui_ImplDX11_Shutdown();  
    ImGui_ImplWin32_Shutdown();  
    ImGui::DestroyContext();  
}

void EditorGUIManager::Update()
{
    ImportSettingsInspector::Update();   // Inspector 에서 누른 Apply · Fix Now (그리기 밖에서 씬을 다시 만든다)
    if (Application::GetI()->GetApp() && Application::GetI()->GetApp()->IsOpenGL())
        ImGuiGL::NewFrame();
    else if (Application::GetI()->GetApp() && Application::GetI()->GetApp()->IsVulkan())
        ImGuiGfx::NewFrame();
    else
        ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    for (auto& window : m_pEditorWindows)
    {
        FRAME_PROFILE("Update " + window->GetTitle());
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
        if (t == "Hierarchy") target = left;
        else if (t == "Inspector") target = right;
        else if (t == "Project" || t == "Console" || t == "Animator") target = bottom;
        ImGui::DockBuilderDockWindow(w->GetImGuiName().c_str(), target);
    }
    ImGui::DockBuilderFinish(dockspaceId);
    m_FocusDefaultTabs = 8;
}

// Unity 툴바 행: 좌측 프로젝트 이름, 중앙 Play/Pause/Step, 우측 Layout 드롭다운
void EditorGUIManager::DrawToolbar(float y)
{
    ImGuiViewport* vp = ImGui::GetMainViewport();
    const float h = EditorTheme::ToolbarHeight;
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y + y));
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x, h));
    ImGui::SetNextWindowViewport(vp->ID);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, EditorTheme::Chrome());

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    if (ImGui::Begin("##NovaToolbar", nullptr, flags))
    {
        const float W = vp->Size.x;
        ImVec2 wp = ImGui::GetWindowPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddLine(ImVec2(wp.x, wp.y + h - 1), ImVec2(wp.x + W, wp.y + h - 1), ImGui::GetColorU32(EditorTheme::Separator()));

        const float bh = 24.0f;
        const float by = (h - bh) * 0.5f;

        // 좌측: 로고 + 프로젝트 이름
        {
            static ComPtr<GfxShaderResourceView> s_LogoSrv;
            if (!s_LogoSrv)
                s_LogoSrv = ResourceManager::GetI()->LoadTexture(L"\\ProjectSetting\\logo\\nova-logo-64.png");
            GfxShaderResourceView* logo = s_LogoSrv.Get();
            float x = 12.0f;
            if (logo)
            {
                ImGui::SetCursorPos(ImVec2(x, (h - 20.0f) * 0.5f));
                ImGui::Image((ImTextureID)logo, ImVec2(20.0f, 20.0f));
                x += 28.0f;
            }
            if (!PathManager::GetProjectOverride().empty())
            {
                std::filesystem::path root(PathManager::GetProjectOverride());
                if (!root.has_filename() && root.has_parent_path()) root = root.parent_path();
                ImGui::SetCursorPos(ImVec2(x, (h - ImGui::GetFontSize()) * 0.5f));
                ImGui::PushStyleColor(ImGuiCol_Text, EditorTheme::TextDim());
                ImGui::TextUnformatted(wstring_to_string(root.filename().wstring()).c_str());
                ImGui::PopStyleColor();
            }
        }

        // 중앙: Play / Pause / Step
        const bool playing = Application::IsPlaying();
        const bool paused = Application::IsPaused();
        const float bw = 34.0f, gap = 2.0f;
        float x = (W - (bw * 3 + gap * 2)) * 0.5f;

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        auto toolbarButton = [&](const char* label, bool active) -> bool
        {
            ImGui::PushStyleColor(ImGuiCol_Button, active ? EditorTheme::Accent() : EditorTheme::Button());
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, active ? EditorTheme::Accent() : EditorTheme::ButtonHover());
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, EditorTheme::ButtonHover());
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.92f, 0.92f, 0.92f, 1.0f));
            bool pressed = ImGui::Button(label, ImVec2(bw, bh));
            ImGui::PopStyleColor(4);
            return pressed;
        };

        ImGui::SetCursorPos(ImVec2(x, by));
        if (toolbarButton(ICON_FA_PLAY "##play", playing))
        {
            if (!playing)
            {
                // 스크립트 컴파일 중이면 기다리고, 컴파일 오류가 있으면 Play 에 들어가지 않는다 (Unity)
                if (ScriptEngine::CanEnterPlayMode())
                {
                    Application::SetPaused(false);
                    Application::SetPlaying(true);
                    SceneManager::GetI()->HandlePlay();
                }
            }
            else
            {
                Application::SetPlaying(false);
                Application::SetPaused(false);
                SelectionManager::ClearSelection();
                SceneManager::GetI()->HandleStop();
            }
        }
        ImGui::SetCursorPos(ImVec2(x + bw + gap, by));
        if (toolbarButton(ICON_FA_PAUSE "##pause", playing && paused) && playing)
            Application::SetPaused(!paused);
        ImGui::SetCursorPos(ImVec2(x + (bw + gap) * 2, by));
        if (toolbarButton(ICON_FA_FORWARD_STEP "##step", false) && playing)
        {
            Application::SetPaused(true);
            Application::RequestStep();
        }
        ImGui::PopStyleVar(2);

        // 우측: Layout 드롭다운
        const float lw = 84.0f;
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        ImGui::PushStyleColor(ImGuiCol_Button, EditorTheme::Chrome());
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, EditorTheme::Panel());
        ImGui::PushStyleColor(ImGuiCol_Text, EditorTheme::Text());
        ImGui::SetCursorPos(ImVec2(W - lw - 12.0f, by));
        if (ImGui::Button("Layout  " ICON_FA_CHEVRON_DOWN, ImVec2(lw, bh)))
            ImGui::OpenPopup("##layoutmenu");
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(2);
        if (ImGui::BeginPopup("##layoutmenu"))
        {
            if (ImGui::MenuItem("Default"))
                m_ResetLayout = true;
            ImGui::EndPopup();
        }
    }
    ImGui::End();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}

void EditorGUIManager::RenderEditorWindows()
{
    SceneManager::GetI()->DrawScenePrompt();
    AutoSave::DrawRecoveryPrompt();   // 지난 세션이 충돌로 끝났을 때 "Recover / Discard"   // "Scene Has Been Modified" 확인 창 (New/Open Scene)

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(EditorTheme::MenuBarPaddingX, EditorTheme::MenuBarPaddingY));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_MenuBarBg, EditorTheme::Chrome());

    if (ImGui::BeginMainMenuBar())
    {
        m_TopChromeHeight = ImGui::GetFrameHeight() + EditorTheme::ToolbarHeight;
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 4.0f));

        // 1. File Menu
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("New Scene", "Ctrl+N", false, !Application::IsPlaying()))
                SceneManager::GetI()->NewSceneFromEditor();
            if (ImGui::MenuItem("Open Scene...", "Ctrl+O", false, !Application::IsPlaying()))
                SceneManager::GetI()->OpenSceneFromEditor();
            ImGui::Separator();
            if (ImGui::MenuItem("Project Hub..."))
            {
                // Hub 는 별도 프로세스(같은 exe, 인자 없음)로 실행된다.
                HubLauncher::LaunchHub();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Save", "Ctrl+S", false, !Application::IsPlaying()))
                SceneManager::GetI()->SaveCurrentScene(false);
            if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S", false, !Application::IsPlaying()))
                SceneManager::GetI()->SaveCurrentScene(true);
            ImGui::Separator();
            if (ImGui::MenuItem("Build Settings...", "Ctrl+Shift+B"))
                BuildSettingsWindow::Open();
            if (ImGui::MenuItem("Build And Run", "Ctrl+B", false, !Application::IsPlaying() && !BuildPipeline::IsRunning()))
                BuildSettingsWindow::BuildAndRun();
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4"))
                ::PostMessageW(Application::GetI()->GetMainHwnd(), WM_CLOSE, 0, 0);   // 창 닫기와 같은 길 (저장 안 한 변경 확인)
            ImGui::EndMenu();
        }

        // 2. Edit Menu
        if (ImGui::BeginMenu("Edit"))
        {
            const std::string undoLabel = Undo::CanUndo() ? "Undo " + Undo::UndoName() : std::string("Undo");
            const std::string redoLabel = Undo::CanRedo() ? "Redo " + Undo::RedoName() : std::string("Redo");
            if (ImGui::MenuItem(undoLabel.c_str(), "Ctrl+Z", false, Undo::CanUndo() && !Application::IsPlaying()))
                Undo::PerformUndo();
            if (ImGui::MenuItem(redoLabel.c_str(), "Ctrl+Y", false, Undo::CanRedo() && !Application::IsPlaying()))
                Undo::PerformRedo();
            ImGui::Separator();
            if (ImGui::MenuItem("Play / Stop", "Ctrl+P"))
                SceneManager::GetI()->TogglePlayFromEditor();
            if (ImGui::MenuItem("Clear Selection"))
            {
                SelectionManager::ClearSelection();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Project Settings..."))
                ProjectSettingsWindow::Open("Graphics");
            if (ImGui::MenuItem("Preferences..."))
                PreferencesWindow::Open();
            // 렌더링 API 선택 (다음 실행부터 적용)
            if (ImGui::BeginMenu("Graphics API"))
            {
                for (int i = 0; i < static_cast<int>(GraphicsAPI::Count); ++i)
                {
                    GraphicsAPI api = static_cast<GraphicsAPI>(i);
                    bool supported = GraphicsBackendFactory::Create(api)->IsSupported();
                    bool selected = (GraphicsSettings::GetEditorAPI() == api);
                    std::string label = GraphicsAPIToString(api);
                    if (!supported) label += " (not implemented)";
                    if (ImGui::MenuItem(label.c_str(), nullptr, selected, supported))
                        GraphicsSettings::SetEditorAPI(api);
                }
                ImGui::Separator();
                ImGui::TextDisabled("Active: %s (restart to apply)", GraphicsAPIToString(GraphicsSettings::GetActiveAPI()));
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }

        // Assets Menu
        if (ImGui::BeginMenu("Assets"))
        {
            if (ImGui::BeginMenu("Create"))
            {
                if (ImGui::MenuItem("Folder"))
                {
                    std::error_code ec;
                    std::filesystem::path base = PathManager::GetI()->GetMovePathW(L"Assets\\");
                    std::filesystem::path dir = base / L"New Folder";
                    for (int i = 1; std::filesystem::exists(dir, ec); ++i)
                        dir = base / (L"New Folder " + std::to_wstring(i));
                    std::filesystem::create_directories(dir, ec);
                }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Show in Explorer"))
            {
                ::ShellExecuteW(nullptr, L"open", PathManager::GetI()->GetMovePathW(L"Assets\\").c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
            ImGui::EndMenu();
        }

        // 3. GameObject Menu
        if (ImGui::BeginMenu("GameObject"))
        {
            // Hierarchy 의 [+] / 우클릭 메뉴와 같은 생성 항목 (선택한 오브젝트의 자식으로 생성)
            GameObjectMenu::DrawCreateItems(SceneManager::GetI()->GetCurrentScene(), SelectionManager::GetSelectedGameObject());
            ImGui::EndMenu();
        }

        // Component Menu: 선택한 GameObject 에 컴포넌트 추가
        if (ImGui::BeginMenu("Component"))
        {
            GameObject* selected = SelectionManager::GetSelectedGameObject();
            for (const std::string& type : ComponentFactory::Instance().GetComponentTypes())
            {
                if (!AddComponentMenu::IsListed(type)) continue;   // C++ 예제 스크립트/기반 타입은 숨김 (스크립트는 Add Component > Scripts 의 C# 만)
                if (ImGui::MenuItem(AddComponentMenu::DisplayName(type).c_str(), nullptr, false, selected != nullptr))
                {
                    auto component = ComponentFactory::Instance().CreateComponent(type);
                    if (component)
                        selected->AddComponent(component);
                }
            }
            ImGui::EndMenu();
        }

        // 4. Window Menu
        if (ImGui::BeginMenu("Window"))
        {
            if (ImGui::MenuItem("Package Manager"))   // Unity: Window > Package Manager
                PackageManagerWindow::Open();
            ImGui::Separator();
            for (auto& window : m_pEditorWindows)
            {
                if (window->GetTitle() == "Profiler" || window->GetTitle() == "Package Manager")
                    continue;   // Analysis 아래 / 맨 위 Package Manager
                bool opened = window->GetIsOpened();
                if (ImGui::MenuItem(window->GetTitle().c_str(), nullptr, &opened))
                {
                    window->SetIsOpened(opened);
                }
            }
            ImGui::Separator();
            if (ImGui::BeginMenu("Analysis"))
            {
                if (EditorWindow* profiler = FindWindow("Profiler"))
                    if (ImGui::MenuItem("Profiler", "Ctrl+7", profiler->GetIsOpened()))
                        ProfilerEditorWindow::Toggle();
                ImGui::EndMenu();
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

        ImGui::EndMainMenuBar();
    }
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);

    DrawToolbar(m_TopChromeHeight - EditorTheme::ToolbarHeight);

    // 창의 전체 크기와 위치 설정
    // 프레임과 배경을 제거하는 플래그 설정
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoBackground |
        // 도킹된 창(Scene 등)을 눌러도 도킹 영역 전체가 앞으로 오지 않게 → 떠 있는 창(Profiler 등)이 뒤로 숨지 않는다
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    // 전체 화면 크기로 DockSpace 창 설정    
    Vec2 screenSize = Application::GetI()->GetApp()->GetScreenSize();
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + m_TopChromeHeight)); 
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, viewport->Size.y - m_TopChromeHeight)); 
    ImGui::SetNextWindowViewport(viewport->ID);  

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f); 
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);

    if (ImGui::Begin("##DockSpace", NULL, window_flags))
    {
        ImGuiID dockspace_id = ImGui::GetID("RootDockspace");  
        if (m_ResetLayout || ImGui::DockBuilderGetNode(dockspace_id) == nullptr)
        {
            m_ResetLayout = false;
            BuildDefaultLayout(dockspace_id, ImVec2(viewport->Size.x, viewport->Size.y - m_TopChromeHeight));
        }
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
        // Game 뷰 Play Maximized 가 덮을 영역 = 도킹 영역 전체
        if (ImGuiDockNode* root = ImGui::DockBuilderGetNode(dockspace_id))
            GameViewEditorWindow::SetDockRect(root->Pos, ImVec2(root->Pos.x + root->Size.x, root->Pos.y + root->Size.y));
    }

    ImGui::End();

    ImGui::PopStyleVar(5);

    // Play 시작/끝 순간: Game 뷰의 Play Focused / Maximized / Unfocused 처리
    {
        static bool s_WasPlaying = false;
        const bool playing = Application::IsPlaying();
        if (playing != s_WasPlaying)
        {
            s_WasPlaying = playing;
            GameViewEditorWindow::OnPlayModeChanged(playing);
            if (!playing)
                ScriptEngine::OnPlayModeChanged(false);   // Play 중에 바뀐 스크립트를 이제 다시 읽는다
        }
    }

    for (auto& window : m_pEditorWindows)
    {
        FRAME_PROFILE("Window " + window->GetTitle());
        window->Render();
    }

    if (m_PendingTabFrames > 0)
    {
        --m_PendingTabFrames;
        if (EditorWindow* w = FindWindow(m_PendingTab))
            if (ImGuiWindow* win = ImGui::FindWindowByName(w->GetImGuiName().c_str()); win && win->DockNode)
            {
                win->DockNode->SelectedTabId = win->TabId;
                if (win->DockNode->TabBar)
                    win->DockNode->TabBar->SelectedTabId = win->DockNode->TabBar->NextSelectedTabId = win->TabId;
                // 포커스가 있는 창(Play 를 누르면 Game 창)의 탭이 다시 앞으로 오므로 마지막 프레임에 포커스도 옮긴다
                // (예전에는 Play 중 nova window scene 이 몇 프레임 뒤 Game 탭으로 되돌아가 Scene 뷰가 그려지지 않았다)
                if (m_PendingTabFrames == 0)
                    ImGui::SetWindowFocus(w->GetImGuiName().c_str());
            }
    }

    if (m_FocusDefaultTabs > 0)
    {
        // 기본 레이아웃 직후 몇 프레임 동안 Scene / Project 탭을 선택 상태로 만든다. (도킹 노드의 선택 탭을 직접 지정)
        const bool lastFrame = (--m_FocusDefaultTabs == 0);
        for (auto& w : m_pEditorWindows)
        {
            if (w->GetTitle() != "Scene" && w->GetTitle() != "Project")
                continue;
            ImGuiWindow* win = ImGui::FindWindowByName(w->GetImGuiName().c_str());
            if (win && win->DockNode)
            {
                win->DockNode->SelectedTabId = win->TabId;
                if (win->DockNode->TabBar)
                    win->DockNode->TabBar->SelectedTabId = win->DockNode->TabBar->NextSelectedTabId = win->TabId;
            }
            // 내비게이션 포커스가 있는 창의 탭이 우선하므로, 마지막 프레임에 Project 창에 포커스를 준다.
            if (lastFrame && w->GetTitle() == "Project")
                ImGui::SetWindowFocus(w->GetImGuiName().c_str());
        }
    }
    
    for (auto& dialog : m_pEditorDialogs) 
    { 
        dialog->Render(); 
    } 

    GameViewEditorWindow::DrawMaximized();
    ObjectPicker::Draw();
    ProjectSettingsWindow::Draw();
    PreferencesWindow::Draw();
    BuildSettingsWindow::Draw();   // + 빌드 진행 창, 끝난 빌드 처리

    // Ctrl+Shift+B = Build Settings, Ctrl+B = Build And Run (글자 입력 중이 아닐 때)
    {
        ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl && !io.WantTextInput && !NovaCodeWindow::IsFocused() && ImGui::IsKeyPressed(ImGuiKey_B, false))
        {
            if (io.KeyShift) BuildSettingsWindow::Open();
            else if (!Application::IsPlaying() && !BuildPipeline::IsRunning()) BuildSettingsWindow::BuildAndRun();
        }
        // Ctrl+7 = Profiler (Unity 와 같은 단축키)
        if (io.KeyCtrl && !io.KeyShift && !io.WantTextInput && !NovaCodeWindow::IsFocused() && ImGui::IsKeyPressed(ImGuiKey_7, false))
            ProfilerEditorWindow::Toggle();
    }
}

void EditorGUIManager::RenderAfter()
{
    ImGuiIO& io = ImGui::GetIO(); 
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        if (Application::GetI()->GetApp() && Application::GetI()->GetApp()->IsOpenGL())
            ImGuiGL::EndPlatformWindows();   // 본 창 DC 로 되돌린다 (안 그러면 Present 가 마지막 뷰포트 창에 그린다)
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

void EditorGUIManager::UnregisterWindow(EditorWindow* window)
{
    m_pEditorWindows.erase(std::remove(m_pEditorWindows.begin(), m_pEditorWindows.end(), window), m_pEditorWindows.end());
}

EditorWindow* EditorGUIManager::FindWindow(const std::string& title) const
{
    for (EditorWindow* w : m_pEditorWindows)
        if (w->GetTitle() == title)
            return w;
    return nullptr;
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

    colors[ImGuiCol_Text]                  = EditorTheme::Text();
    colors[ImGuiCol_TextDisabled]          = ImVec4(0.45f, 0.45f, 0.45f, 1.00f);
    colors[ImGuiCol_WindowBg]              = EditorTheme::Panel();
    colors[ImGuiCol_ChildBg]               = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_PopupBg]               = ImVec4(0.18f, 0.18f, 0.18f, 0.98f);
    colors[ImGuiCol_Border]                = EditorTheme::Separator();
    colors[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    colors[ImGuiCol_FrameBg]               = EditorTheme::Rgb(42, 42, 42);
    colors[ImGuiCol_FrameBgHovered]        = EditorTheme::Rgb(52, 52, 52);
    colors[ImGuiCol_FrameBgActive]         = EditorTheme::Rgb(60, 60, 60);

    colors[ImGuiCol_TitleBg]               = EditorTheme::Chrome();
    colors[ImGuiCol_TitleBgActive]         = EditorTheme::Chrome();
    colors[ImGuiCol_TitleBgCollapsed]      = EditorTheme::Chrome();

    colors[ImGuiCol_MenuBarBg]             = EditorTheme::Chrome();
    colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.12f, 0.12f, 0.12f, 0.60f);
    colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.35f, 0.35f, 0.35f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.45f, 0.45f, 0.45f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.55f, 0.55f, 0.55f, 1.00f);

    colors[ImGuiCol_CheckMark]             = unityAccentBlue;
    colors[ImGuiCol_SliderGrab]            = unityLight;
    colors[ImGuiCol_SliderGrabActive]      = unityBlue;

    colors[ImGuiCol_Button]                = EditorTheme::Button();
    colors[ImGuiCol_ButtonHovered]         = EditorTheme::ButtonHover();
    colors[ImGuiCol_ButtonActive]          = EditorTheme::Rgb(75, 75, 75);

    colors[ImGuiCol_Header]                = EditorTheme::Selection();
    colors[ImGuiCol_HeaderHovered]         = EditorTheme::Selection();
    colors[ImGuiCol_HeaderActive]          = EditorTheme::Selection();

    colors[ImGuiCol_Separator]             = EditorTheme::Separator();
    colors[ImGuiCol_SeparatorHovered]      = unityAccentBlue;
    colors[ImGuiCol_SeparatorActive]       = unityBlue;

    colors[ImGuiCol_ResizeGrip]            = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_ResizeGripHovered]     = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_ResizeGripActive]      = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    // Unity Tab Styling
    colors[ImGuiCol_Tab]                   = EditorTheme::Chrome();
    colors[ImGuiCol_TabHovered]            = EditorTheme::Rgb(68, 68, 68);
    colors[ImGuiCol_TabSelected]           = EditorTheme::Panel();
    colors[ImGuiCol_TabSelectedOverline]   = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_TabDimmed]             = EditorTheme::Chrome();
    colors[ImGuiCol_TabDimmedSelected]     = EditorTheme::Panel();
    colors[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0, 0, 0, 0);

    colors[ImGuiCol_DockingPreview]        = ImVec4(0.18f, 0.44f, 0.75f, 0.70f);
    colors[ImGuiCol_DockingEmptyBg]        = EditorTheme::Chrome();

    colors[ImGuiCol_TableHeaderBg]         = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
    colors[ImGuiCol_TableBorderStrong]     = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
    colors[ImGuiCol_TableBorderLight]      = ImVec4(0.16f, 0.16f, 0.16f, 0.50f);
    colors[ImGuiCol_TableRowBg]            = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt]         = ImVec4(1.00f, 1.00f, 1.00f, 0.03f);

    colors[ImGuiCol_TextSelectedBg]        = ImVec4(0.18f, 0.44f, 0.75f, 0.50f);
    colors[ImGuiCol_DragDropTarget]        = unityAccentBlue;
    colors[ImGuiCol_NavHighlight]          = unityAccentBlue;
}
