#include "pch.h"
#include "AnimationClipSelectEditorDialog.h"
#include "EditorGUI.h"

AnimationClipSelectEditorDialog::AnimationClipSelectEditorDialog(
	shared_ptr<AnimationClip>& selectAnimationClip, 
	string& selectAnimationClipFilePath, 
	int& selectAnimationClipIndex) : EditorDialog("Select Animation Clip")
    , m_MeshFiles{}
    , m_SelectAnimationClip(selectAnimationClip)
    , m_SelectAnimationClipFilePath(selectAnimationClipFilePath)
    , m_SelectAnimationClipIndex(selectAnimationClipIndex)
{
}

AnimationClipSelectEditorDialog::~AnimationClipSelectEditorDialog()
{
}

void AnimationClipSelectEditorDialog::Open(
    shared_ptr<AnimationClip>& selectAnimationClip, 
    string& selectAnimationClipFilePath, 
    int& selectAnimationClipIndex)
{
    AnimationClipSelectEditorDialog* view = new AnimationClipSelectEditorDialog(
        selectAnimationClip, 
        selectAnimationClipFilePath,
        selectAnimationClipIndex);
    view->Show();
}

void AnimationClipSelectEditorDialog::Show()
{
    EditorDialog::Show();

    string path = wstring_to_string(PathManager::GetI()->GetContentPathW()) + "Assets\\";
    string extension = ".fbx";

    for (const auto& entry : fs::recursive_directory_iterator(path))
    {
        if (entry.is_regular_file())
        {
            string fileExtension = entry.path().extension().string();
            transform(fileExtension.begin(), fileExtension.end(), fileExtension.begin(), ::tolower);

            if (fileExtension == extension)
            {
                string filePath = entry.path().string();
                string assetPath = PathManager::GetI()->GetCutSolutionPath(filePath);

                auto meshFile = ResourceManager::GetI()->LoadMeshFile(assetPath);
                m_MeshFiles.push_back(meshFile);
            }
        }
    }
}

void AnimationClipSelectEditorDialog::Close()
{
    EditorDialog::Close();
}

void AnimationClipSelectEditorDialog::OnRender()
{
    ImGui::BeginChild("ScrollableRegion", ImVec2(0, -40), true, ImGuiWindowFlags_HorizontalScrollbar); 

    ImGui::Dummy(ImVec2(16, 16));
    ImGui::SameLine();
    ImGui::BeginGroup();

    ImGui::Dummy(ImVec2(22, 22));
    EditorGUI::RowSizedBox(8);
    if (ImGui::Selectable("None", m_SystemSelectAnimationClip == nullptr, NULL, ImVec2(0, 26)))
    {
        m_SystemSelectAnimationClip = nullptr; 
    }

    for (auto meshFile : m_MeshFiles)
    {
        for (int i = 0; i < meshFile->SkinnedData.AnimationClips.size(); ++i)
        {
            const auto& animation = meshFile->SkinnedData.AnimationClips[i];  
            if (RenderAnimationClipEntry(animation))
            {
                m_SystemSelectAnimationClipIndex = i;
                m_SystemSelectAnimationClipFilePath = wstring_to_string(meshFile->Path);
            }

            if (ImGui::IsMouseDoubleClicked(0))
            {
                m_SelectAnimationClip = m_SystemSelectAnimationClip;
                m_SelectAnimationClipFilePath = m_SystemSelectAnimationClipFilePath;
                m_SelectAnimationClipIndex = m_SystemSelectAnimationClipIndex;
                Close();
            }
            i++;
        }
    }
    ImGui::EndGroup();

    ImGui::EndChild(); // 스크롤 가능한 영역 끝
}

bool AnimationClipSelectEditorDialog::RenderAnimationClipEntry(const shared_ptr<AnimationClip>& selectAnimationClip)
{
    EditorGUI::Image(L"\\ProjectSetting\\icons\\icon_animation_clip.png", ImVec2(24, 24));
    EditorGUI::RowSizedBox(8);
    if (ImGui::Selectable(selectAnimationClip->Name.c_str(), m_SystemSelectAnimationClip == selectAnimationClip))
    {
        m_SystemSelectAnimationClip = selectAnimationClip;
        return true;
    }
    return false;
}
