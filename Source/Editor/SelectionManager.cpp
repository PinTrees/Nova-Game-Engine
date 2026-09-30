#include "pch.h"
#include "SelectionManager.h"
#include <filesystem>
#include "AnimatorController.h"

namespace fs = std::filesystem;

SINGLE_BODY(SelectionManager)

SelectionType SelectionManager::m_SelectedType = SelectionType::NONE;
SelectionSubType SelectionManager::m_SelectedSubType = SelectionSubType::NONE;

wstring SelectionManager::m_SelectedFilePath = L"";
GameObject* SelectionManager::m_SelectedGameObject = nullptr;

shared_ptr<UMaterial> SelectionManager::m_SelectedFile_Material = nullptr;
shared_ptr<MeshFile> SelectionManager::m_SelectFile_FbxModel = nullptr;
shared_ptr<AnimatorController> SelectionManager::m_SelectFile_Controller = nullptr;
AnimatorSelection SelectionManager::m_AnimatorSelection;

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
	m_SelectFile_Controller = nullptr;
	m_AnimatorSelection = AnimatorSelection();
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
	else if (path.extension() == ".volumeprofile")
	{
		m_SelectedSubType = SelectionSubType::VOLUME_PROFILE;
	}
	else if (path.extension() == ".controller")
	{
		string cutPath = PathManager::GetI()->GetCutSolutionPath(wstring_to_string(filePath));
		std::replace(cutPath.begin(), cutPath.end(), '/', '\\');

		m_SelectFile_Controller = AnimatorController::Load(cutPath);
		m_SelectedSubType = SelectionSubType::ANIMATOR_CONTROLLER;
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

void SelectionManager::SetSelectedAnimatorItem(shared_ptr<AnimatorController> controller, int layer, int state, int transition)
{
	m_AnimatorSelection.Controller = controller;
	m_AnimatorSelection.Layer = layer;
	m_AnimatorSelection.State = state;
	m_AnimatorSelection.Transition = transition;
	m_SelectedType = SelectionType::ANIMATOR;
	m_SelectedSubType = SelectionSubType::NONE;
	m_SelectedGameObject = nullptr;
}
