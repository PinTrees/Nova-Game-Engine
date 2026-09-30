#include "pch.h"
#include "InspectorEditorWindow.h"
#include <filesystem>
#include "SkinnedMesh.h"

namespace fs = std::filesystem;

InspectorEditorWindow::InspectorEditorWindow()
	: EditorWindow("Inspector", ICON_FA_CIRCLE_INFO) 
{
}

InspectorEditorWindow::~InspectorEditorWindow()
{
}

void InspectorEditorWindow::OnRender()
{
	if (SelectionManager::GetSelectedObjectType() == SelectionType::NONE)
		return;

	if (SelectionManager::GetSelectedObjectType() == SelectionType::GAMEOBJECT)
	{
		GameObject* curSelectGameObject = SelectionManager::GetSelectedGameObject();

		if (curSelectGameObject == nullptr)
			return;

		curSelectGameObject->OnInspectorGUI();
	}
	else if (SelectionManager::GetSelectedObjectType() == SelectionType::FILE)
	{
		if (SelectionManager::GetSelectedSubType() == SelectionSubType::NONE)
			return;

		if (SelectionManager::GetSelectedSubType() == SelectionSubType::MATERIAL)
		{
			auto material = SelectionManager::GetSelectMaterial();
			if (material == nullptr)
				return;

			material->OnInspectorGUI();
		}
		else if (SelectionManager::GetSelectedSubType() == SelectionSubType::FBX)
		{
			auto fbxObject = SelectionManager::GetSelectFbxModel();
			if (fbxObject == nullptr)
				return;

			fbxObject->OnInspectorGUI();
		}
	}
}
