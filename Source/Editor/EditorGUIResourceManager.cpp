#include "pch.h"
#include "EditorGUIResourceManager.h"
#include "ImGuiGL.h"
#include "ImGuiGfx.h"
#include "App.h"
#include "TaskSystem.h"
#include "EditorTheme.h"

SINGLE_BODY(EditorGUIResourceManager);

EditorGUIResourceManager::EditorGUIResourceManager()
{

}

EditorGUIResourceManager::~EditorGUIResourceManager()
{
    for (auto& entry : m_FontMap)
    {
        ImFont* font = entry.second;
        if (font != nullptr)
        {
        }
    }

    m_FontMap.clear();
}

void EditorGUIResourceManager::Init()
{
}


ImFont* EditorGUIResourceManager::LoadFont(EditorTextStyle style)
{
    std::string fontKey = std::to_string(style.FontSize) + (style.Bold ? "_bold" : "_regular");

    // 기본 크기는 초기화 때 만든 Fonts[0](일반) / Fonts[1](굵게)를 그대로 사용
    if (style.FontSize == EditorTheme::FontSize)
    {
        ImGuiIO& io = ImGui::GetIO();
        int idx = style.Bold ? 1 : 0;
        if (io.Fonts->Fonts.Size > idx)
            return io.Fonts->Fonts[idx];
    }

    if (m_FontMap.find(fontKey) != m_FontMap.end())
    {
        return m_FontMap[fontKey];
    }

    if (m_LoadFontTask.find(fontKey) != m_LoadFontTask.end())
    {
        if (!m_LoadFontTask[fontKey])
        {
            return nullptr;
        }
        return nullptr;
    }

    m_LoadFontTask[fontKey] = true;
    FontLoadContainer fontContainer;
    fontContainer.FontKey = fontKey;
    fontContainer.FontStyle = style;

    LoadFontAsync(fontContainer);  
    //ImFont* font = fontFuture.get();  // ��Ʈ �ε尡 �Ϸ�Ǹ� ��Ʈ�� ���� 
    
    return nullptr;  // �۾��� �Ϸ�Ǳ� �������� nullptr ��ȯ
}

void EditorGUIResourceManager::LoadFontAsync(FontLoadContainer container)
{
    bool isPretendardFont = false;
    const string fontPath = EditorTheme::FontFile(container.FontStyle.Bold, isPretendardFont);
    const EditorTextStyle& style = container.FontStyle;

    string fontKey = to_string(style.FontSize) + (style.Bold ? "_bold" : "_regular");

    // �񵿱� �۾��� ����� ������ promise ��ü
    auto fontPromise = make_shared<promise<ImFont*>>(); 
    future<ImFont*> fontFuture = fontPromise->get_future();

    TaskSystem::mainThreadTasks.push([fontKey, fontPath, style, fontPromise, isPretendardFont, this]()
    {
        ImFontConfig config;
        config.MergeMode = true; // ���� ��Ʈ�� ���� ��� 
        config.PixelSnapH = true;
        static const ImWchar icons_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 }; // FontAwesome ����   

        static const ImWchar textRanges[] = { 0x0020, 0x00FF, 0x2010, 0x2027, 0x2190, 0x21FF, 0x1100, 0x11FF, 0x3000, 0x303F, 0x3130, 0x318F, 0xAC00, 0xD7A3, 0xFF00, 0xFFEF, 0 };
        ImGuiIO& io = ImGui::GetIO(); 
        ImFont* font = io.Fonts->AddFontFromFileTTF( 
            fontPath.c_str(), 
            style.FontSize, 
            NULL, 
            isPretendardFont ? textRanges : io.Fonts->GetGlyphRangesDefault() 
        );
        static const ImWchar textRanges_dummy[] = { 0 };
        (void)textRanges_dummy;
        // 한글은 맑은 고딕으로 병합 (Pretendard 는 한글을 포함하므로 생략)
        static const ImWchar hangulRanges[] = { 0x1100, 0x11FF, 0x3000, 0x303F, 0x3130, 0x318F, 0xAC00, 0xD7A3, 0xFF00, 0xFFEF, 0 };
        ImFontConfig hangulCfg;
        hangulCfg.MergeMode = true;
        hangulCfg.PixelSnapH = true;
        if (!isPretendardFont)
            io.Fonts->AddFontFromFileTTF(style.Bold ? "C:\\Windows\\Fonts\\malgunbd.ttf" : "C:\\Windows\\Fonts\\malgun.ttf", style.FontSize, &hangulCfg, hangulRanges);
        // Font Awesome ��Ʈ �߰�
        string fa_path = PathManager::GetI()->GetEnginePathS() + "ProjectSetting\\fonts\\fa-solid-900.ttf";
        io.Fonts->AddFontFromFileTTF( 
            fa_path.c_str(),
            style.FontSize - 2,
            &config, icons_ranges);  
         
        if (font)
        { 
            io.Fonts->Build(); 
            if (Application::GetI()->GetApp() && Application::GetI()->GetApp()->IsOpenGL())
            {
                ImGuiGL::InvalidateDeviceObjects();
                ImGuiGL::CreateDeviceObjects();
            }
            else if (Application::GetI()->GetApp() && Application::GetI()->GetApp()->IsVulkan())
            {
                ImGuiGfx::InvalidateDeviceObjects();
                ImGuiGfx::CreateDeviceObjects();
            }
            else
            {
                ImGui_ImplDX11_InvalidateDeviceObjects();
                ImGui_ImplDX11_CreateDeviceObjects();
            }

            m_LoadFontTask[fontKey] = false;
            this->m_FontMap[fontKey] = font; 
            // �۾� �Ϸ� �� promise�� ��� ����
            fontPromise->set_value(font);
        }
        else
        {
            fontPromise->set_value(nullptr);
        }
    });

    // �񵿱������� ��Ʈ�� ��ȯ (�۾��� ���� ������ ��ٸ���)
    return; 
}
