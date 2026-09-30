#include "pch.h"
#include "MeshSelectEditorDialog.h"
#include "EditorGUIManager.h"
#include "EditorGUI.h"

MeshSelectEditorDialog::MeshSelectEditorDialog(
    shared_ptr<Mesh>& selectMesh, 
    shared_ptr<SkinnedMesh>& selectSkinnedMesh, 
    wstring&      selectMeshFilePath,
    int& selectIndex,
    MESH_SELECT_DIALOG_TYPE openType) : EditorDialog("Select Mesh")
    , m_MeshFiles{}
    , m_SelectMesh(selectMesh)
    , m_SelectSkinnedMesh(selectSkinnedMesh)
    , m_SelectMeshFilePath(selectMeshFilePath)
    , m_SelectSubsetIndex(selectIndex)
    , m_OpenMeshType(openType)
{
}

MeshSelectEditorDialog::~MeshSelectEditorDialog()
{
}

void MeshSelectEditorDialog::Open(
    shared_ptr<Mesh>& selectMesh,
    shared_ptr<SkinnedMesh>& selectSkinnedMesh,
    wstring&      meshFilePath,
    int&         selectSubsetIndex,
    MESH_SELECT_DIALOG_TYPE openType)
{
	MeshSelectEditorDialog* view = new MeshSelectEditorDialog( 
        selectMesh, 
        selectSkinnedMesh, 
        meshFilePath,
        selectSubsetIndex, 
        openType);
	view->Show();   
}

void MeshSelectEditorDialog::Show()
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

                const auto& meshFile = ResourceManager::GetI()->LoadMeshFile(assetPath);
                if (meshFile == nullptr)
                    continue;

                m_MeshFiles.push_back(meshFile);
            }
        }
    }
}

void MeshSelectEditorDialog::Close()
{
    EditorDialog::Close(); 
    m_MeshFiles.clear();
}

void MeshSelectEditorDialog::OnRender()
{
    if (ImGui::BeginChild("ScrollableRegion", ImVec2(0, -40), true, ImGuiWindowFlags_HorizontalScrollbar))
    {
        ImGui::Dummy(ImVec2(16, 16));
        ImGui::SameLine();
        ImGui::BeginGroup();

        if (m_OpenMeshType == MESH_SELECT_DIALOG_TYPE::MESH_STATIC)
        {
            ImGui::Dummy(ImVec2(22, 22));
            EditorGUI::RowSizedBox(8);
            if (ImGui::Selectable("None", m_SystemSelectMesh == nullptr, NULL, ImVec2(0, 26)))
            {
                m_SystemSelectMesh = nullptr; 
            }

            for (const auto& meshFile : m_MeshFiles)
            {
                for (int i = 0; i < meshFile->Meshs.size(); ++i)
                {
                    const auto& mesh = meshFile->Meshs[i];

                    string entry_id = mesh->Name + wstring_to_string(mesh->Path) + to_string(i);
                    ImGui::PushID(entry_id.c_str());

                    EditorGUI::Image(L"\\ProjectSetting\\icons\\icon_mesh.png", ImVec2(22, 22));
                    EditorGUI::RowSizedBox(8);
                    if (ImGui::Selectable(("Static - " + mesh->Name).c_str(), m_SystemSelectMesh == mesh, NULL, ImVec2(0, 26)))
                    {
                        m_SystemSelectMesh = mesh;
                        m_SystemSelectMeshIndex = i;
                        m_SystemSelectMeshFilePath = meshFile->Path;  
                    }
                    if (ImGui::IsMouseDoubleClicked(0))
                    {
                        m_SelectMesh = m_SystemSelectMesh;
                        m_SelectSubsetIndex = m_SystemSelectMeshIndex;
                        m_SelectMeshFilePath = m_SystemSelectMeshFilePath;
                        Close();
                    }

                    ImGui::PopID();
                }
            }
        }
        else if (m_OpenMeshType == MESH_SELECT_DIALOG_TYPE::MESH_SKINNED)
        {
            ImGui::Dummy(ImVec2(22, 22));
            EditorGUI::RowSizedBox(8);
            if (ImGui::Selectable("None", m_SystemSelectMesh == nullptr, NULL, ImVec2(0, 26)))
            {
                m_SystemSelectMesh = nullptr;
            }

            for (const auto& meshFile : m_MeshFiles)
            {
                for (int i = 0; i < meshFile->SkinnedMeshs.size(); ++i)
                {
                    const auto& skinnedMesh = meshFile->SkinnedMeshs[i];

                    string entry_id = skinnedMesh->Name + wstring_to_string(skinnedMesh->Path) + to_string(i); 
                    ImGui::PushID(entry_id.c_str());

                    EditorGUI::Image(L"\\ProjectSetting\\icons\\icon_mesh.png", ImVec2(22, 22));
                    EditorGUI::RowSizedBox(8);

                    if (ImGui::Selectable(("Skinned - " + skinnedMesh->Name).c_str(), m_SystemSelectSkinnedMesh == skinnedMesh))
                    {
                        m_SystemSelectSkinnedMesh = skinnedMesh;
                        m_SystemSelectMeshFilePath = meshFile->Path;
                        m_SystemSelectMeshIndex = i;
                    }
                    if (ImGui::IsMouseDoubleClicked(0))
                    {
                        m_SelectSkinnedMesh = m_SystemSelectSkinnedMesh;
                        m_SelectSubsetIndex = m_SystemSelectMeshIndex;
                        m_SelectMeshFilePath = m_SystemSelectMeshFilePath;
                        Close(); 
                    }

                    ImGui::PopID();
                }
            }
        }

        ImGui::EndGroup();
    }
    ImGui::EndChild();     
}
