#include "pch.h"
#include "InspectorEditorWindow.h"
#include <filesystem>
#include "SkinnedMesh.h"
#include "AnimatorInspector.h"
#include "AnimatorController.h"
#include "UndoSystem.h"
#include "VolumeProfile.h"
#include "VolumeEditor.h"
#include "RenderPipelineSettings.h"
#include "UnityGUI.h"

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

			std::weak_ptr<UMaterial> weak = material;
			Undo::WatchAsset("material:" + std::to_string((uintptr_t)material.get()), "Material",
				[weak]() { auto m = weak.lock(); if (!m) return std::string(); json j = *m; return j.dump(); },
				[weak](const std::string& text) { if (auto m = weak.lock()) { from_json(json::parse(text), *m); m->ReloadTextures(); UMaterial::Save(m.get()); } });
			material->OnInspectorGUI();
		}
		else if (SelectionManager::GetSelectedSubType() == SelectionSubType::FBX)
		{
			auto fbxObject = SelectionManager::GetSelectFbxModel();
			if (fbxObject == nullptr)
				return;

			fbxObject->OnInspectorGUI();
		}
		else if (SelectionManager::GetSelectedSubType() == SelectionSubType::ANIMATOR_CONTROLLER)
		{
			AnimatorInspector::DrawController(SelectionManager::GetSelectAnimatorController());
		}
		else if (SelectionManager::GetSelectedSubType() == SelectionSubType::VOLUME_PROFILE)
		{
			// .volumeprofile 에셋: 제목 + 효과 목록 (기본 프로파일이면 Override 체크 없이)
			const std::string rel = wstring_to_string(PathManager::GetI()->GetCutSolutionPath(SelectionManager::GetSelectedFile()));
			if (auto profile = VolumeProfile::Load(rel))
			{
				UnityGUI::Label((profile->Name() + " (Volume Profile)").c_str(), 0, true);
				const bool isDefault = _stricmp(RenderPipelineSettings::DefaultVolumeProfilePath().c_str(), profile->Path.c_str()) == 0;
				if (isDefault)
					UnityGUI::HelpBox("This is the Default Volume Profile (Project Settings > Graphics). Its values apply to every scene and can be overridden by Volumes.", false);
				VolumeEditor::DrawProfile(profile, isDefault);
			}
		}
	}
	else if (SelectionManager::GetSelectedObjectType() == SelectionType::ANIMATOR)
	{
		const AnimatorSelection& sel = SelectionManager::GetAnimatorSelection();
		if (sel.Controller)
		{
			std::weak_ptr<AnimatorController> weak = sel.Controller;
			Undo::WatchAsset("controller:" + sel.Controller->Path, sel.Controller->Name(),
				[weak]() { auto c = weak.lock(); return c ? c->ToJsonString() : std::string(); },
				[weak](const std::string& text) { if (auto c = weak.lock()) { c->ApplyJson(text); c->Commit(); } });
		}
		AnimatorInspector::DrawSelection(sel);
	}
}
