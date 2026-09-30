#include "pch.h"
#include "SkeletonAvataSelectEditorDailog.h"
#include "EditorGUI.h"

SkeletonAvataSelectEditorDailog::SkeletonAvataSelectEditorDailog(
	shared_ptr<SkeletonAvataData>&	selectSkeletonAvata,
	string&							selectSkeletonAvataFilePath, 
	int&							selectSkeletonAvataIndex) 
	: EditorDialog("Select Skeleton Avata")
	, m_MeshFiles{}
	, m_SelectSkeletonAvata(selectSkeletonAvata)
	, m_SelectSkeletonAvataFilePath(selectSkeletonAvataFilePath)
	, m_SelectSkeletonAvataIndex(selectSkeletonAvataIndex)
{
}

SkeletonAvataSelectEditorDailog::~SkeletonAvataSelectEditorDailog()
{
}

void SkeletonAvataSelectEditorDailog::Open(
	  shared_ptr<SkeletonAvataData>&	selectSkeletonAvata
	, string&							selectSkeletonAvataFilePath
	, int&								selectSkeletonAvataIndex)
{
	SkeletonAvataSelectEditorDailog* view = new SkeletonAvataSelectEditorDailog(
		selectSkeletonAvata,
		selectSkeletonAvataFilePath,
		selectSkeletonAvataIndex);
	view->Show();
}

void SkeletonAvataSelectEditorDailog::Show()
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
				if (meshFile != nullptr)
					m_MeshFiles.push_back(meshFile);
			}
		}
	}
}

void SkeletonAvataSelectEditorDailog::Close()
{
	EditorDialog::Close();
}

void SkeletonAvataSelectEditorDailog::OnRender()
{
	ImGui::BeginChild("ScrollableRegion", ImVec2(0, -40), true, ImGuiWindowFlags_HorizontalScrollbar);

	ImGui::Dummy(ImVec2(16, 16));
	ImGui::SameLine();
	ImGui::BeginGroup();

	ImGui::Dummy(ImVec2(22, 22));
	EditorGUI::RowSizedBox(8);
	if (ImGui::Selectable("None", m_SystemSelectSkeletonAvata == nullptr, NULL, ImVec2(0, 26)))
	{
		m_SystemSelectSkeletonAvata = nullptr;
	}

	for (auto meshFile : m_MeshFiles)
	{
		for (int i = 0; i < meshFile->Avatas.size(); ++i)
		{
			const auto& skeletone = meshFile->Avatas[i];
			if (RenderSkeletonAvataEntry(skeletone))
			{
				m_SystemSelectSkeletonAvataIndex = i;
				m_SystemSelectSkeletonAvataFilePath = wstring_to_string(meshFile->Path);
			}

			if (ImGui::IsMouseDoubleClicked(0))
			{
				m_SelectSkeletonAvata = m_SystemSelectSkeletonAvata;
				m_SelectSkeletonAvataFilePath = m_SystemSelectSkeletonAvataFilePath; 
				m_SelectSkeletonAvataIndex = m_SystemSelectSkeletonAvataIndex; 
				Close();
			}
			i++;
		}
	}
	ImGui::EndGroup();

	ImGui::EndChild(); 
}

bool SkeletonAvataSelectEditorDailog::RenderSkeletonAvataEntry(const shared_ptr<SkeletonAvataData>& skeletonAvata)
{
	if (skeletonAvata->Name.empty())
		return false;

	EditorGUI::Image(L"\\ProjectSetting\\icons\\File\\skeleton_avata.png", ImVec2(24, 24));
	EditorGUI::RowSizedBox(8);
	if (ImGui::Selectable(skeletonAvata->Name.c_str(), m_SystemSelectSkeletonAvata == skeletonAvata))
	{
		m_SystemSelectSkeletonAvata = skeletonAvata;
		return true;
	}

	return false;
}
