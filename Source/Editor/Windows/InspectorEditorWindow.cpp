#include "pch.h"
#include "InspectorEditorWindow.h"
#include <filesystem>
#include "SkinnedMesh.h"
#include "AnimatorInspector.h"
#include "AnimatorController.h"
#include "UndoSystem.h"
#include "VolumeProfile.h"
#include "VolumeEditor.h"
#include "AudioClip.h"
#include "AudioManager.h"
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
		else if (SelectionManager::GetSelectedSubType() == SelectionSubType::AUDIO_CLIP)
		{
			// AudioClip Inspector: 정보 + 미리 듣기 (Unity 의 오디오 임포트 설정 하단 미리보기에 해당)
			const std::string rel = wstring_to_string(PathManager::GetI()->GetCutSolutionPath(SelectionManager::GetSelectedFile()));
			if (auto clip = AudioClip::Load(rel))
			{
				UnityGUI::Label((clip->Name() + " (Audio Clip)").c_str(), 0, true);
				char buf[64];
				UnityGUI::ValueLabel("Channels", clip->Channels == 1 ? "Mono" : (clip->Channels == 2 ? "Stereo" : std::to_string(clip->Channels).c_str()));
				snprintf(buf, sizeof(buf), "%d Hz", clip->Frequency);
				UnityGUI::ValueLabel("Sample Rate", buf);
				snprintf(buf, sizeof(buf), "%u bit %s", clip->Format.wBitsPerSample, clip->Format.wFormatTag == 3 ? "float" : "PCM");
				UnityGUI::ValueLabel("Format", buf);
				snprintf(buf, sizeof(buf), "%.3f s", clip->Length);
				UnityGUI::ValueLabel("Length", buf);
				snprintf(buf, sizeof(buf), "%.1f KB", clip->Data.size() / 1024.0);
				UnityGUI::ValueLabel("Size", buf);
				UnityGUI::Spacing(6.0f);
				const bool playing = AudioManager::IsPreviewPlaying();
				if (UnityGUI::CenterButton(playing ? "Stop##clipPreview" : "Play##clipPreview", 140.0f))
				{
					if (playing) AudioManager::StopPreview();
					else AudioManager::PlayPreview(clip);
				}
			}
			else
				UnityGUI::HelpBox("This WAV file could not be loaded (supported: PCM 8/16/24/32-bit, 32-bit float).", true);
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
