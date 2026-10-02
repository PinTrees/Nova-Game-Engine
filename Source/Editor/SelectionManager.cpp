#include "pch.h"
#include "SelectionManager.h"
#include <filesystem>
#include "EditorExtensions.h"
#include "AudioClip.h"
#include "AssetImportSettings.h"

namespace fs = std::filesystem;

SINGLE_BODY(SelectionManager)

SelectionType SelectionManager::m_SelectedType = SelectionType::NONE;
SelectionSubType SelectionManager::m_SelectedSubType = SelectionSubType::NONE;

wstring SelectionManager::m_SelectedFilePath = L"";
GameObject* SelectionManager::m_SelectedGameObject = nullptr;

shared_ptr<UMaterial> SelectionManager::m_SelectedFile_Material = nullptr;
shared_ptr<MeshFile> SelectionManager::m_SelectFile_FbxModel = nullptr;
CustomSelection SelectionManager::m_Custom;

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
	m_Custom = CustomSelection();
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
	else if (path.extension() == ".cs")
	{
		m_SelectedSubType = SelectionSubType::SCRIPT;
	}
	else if (AudioClip::IsAudioPath(path.string()))   // .wav .ogg .mp3
	{
		m_SelectedSubType = SelectionSubType::AUDIO_CLIP;
	}
	else if (path.extension() == ".volumeprofile")
	{
		m_SelectedSubType = SelectionSubType::VOLUME_PROFILE;
	}
	else if (AssetImport::KindOf(filePath) == AssetImport::Kind::Texture)
	{
		m_SelectedSubType = SelectionSubType::TEXTURE;
	}
	else if (EditorExtensions::FindAssetType(path.extension().string()))
	{
		m_SelectedSubType = SelectionSubType::CUSTOM_ASSET;   // 패키지가 등록한 에셋 (Inspector 가 그 함수로 그린다)
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

void SelectionManager::SetCustomSelection(const std::string& owner, std::shared_ptr<void> data, std::function<void()> drawInspector)
{
	m_Custom.Owner = owner;
	m_Custom.Data = std::move(data);
	m_Custom.DrawInspector = std::move(drawInspector);
	m_SelectedType = SelectionType::CUSTOM;
	m_SelectedSubType = SelectionSubType::NONE;
	m_SelectedGameObject = nullptr;
}
