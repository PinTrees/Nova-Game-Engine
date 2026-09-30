#include "pch.h"
#include "SelectionManager.h"
#include <filesystem>

namespace fs = std::filesystem;

SINGLE_BODY(SelectionManager)

SelectionType SelectionManager::m_SelectedType = SelectionType::NONE;
SelectionSubType SelectionManager::m_SelectedSubType = SelectionSubType::NONE;

wstring SelectionManager::m_SelectedFilePath = L"";
GameObject* SelectionManager::m_SelectedGameObject = nullptr;

shared_ptr<UMaterial> SelectionManager::m_SelectedFile_Material = nullptr;
shared_ptr<MeshFile> SelectionManager::m_SelectFile_FbxModel = nullptr;

SelectionManager::SelectionManager()
{

}

SelectionManager::~SelectionManager()
{

}

void SelectionManager::ClearSelection()
{
	m_SelectedType = SelectionType::NONE; 
	m_SelectedSubType = SelectionSubType::NONE; 
	m_SelectedGameObject = nullptr; 
	m_SelectedFile_Material = nullptr;
	m_SelectFile_FbxModel = nullptr;
}

void SelectionManager::SetSelectedFile(const std::wstring& filePath)
{
	m_SelectedFilePath = filePath;
	m_SelectedType = SelectionType::FILE;
	m_SelectedSubType = SelectionSubType::NONE;

	fs::path path(filePath);

	if (path.extension() == ".mat")
	{
		string cutPath = PathManager::GetI()->GetCutSolutionPath(wstring_to_string(filePath));

		m_SelectedFile_Material = ResourceManager::GetI()->LoadMaterial(cutPath);
		m_SelectedSubType = SelectionSubType::MATERIAL;
	}
	else if (path.extension() == ".fbx" || path.extension() == ".FBX")
	{
		string cutPath = PathManager::GetI()->GetCutSolutionPath(wstring_to_string(filePath));

		m_SelectFile_FbxModel = ResourceManager::GetI()->LoadFbxModel(cutPath);
		m_SelectedSubType = SelectionSubType::FBX;
	}
	else
	{
		m_SelectedFile_Material = nullptr;
		m_SelectedSubType = SelectionSubType::NONE;
	}
}

void SelectionManager::SetSelectedGameObject(GameObject* gameObject)
{
	m_SelectedGameObject = gameObject;
	m_SelectedType = SelectionType::GAMEOBJECT;
	m_SelectedSubType = SelectionSubType::NONE;
}
