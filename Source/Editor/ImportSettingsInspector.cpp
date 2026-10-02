#include "pch.h"
#include "ImportSettingsInspector.h"
#include "AssetImportSettings.h"
#include "UnityGUI.h"
#include "SelectionManager.h"
#include "SkinnedMesh.h"
#include "SkinnedData.h"
#include "HumanoidAvatar.h"
#include "AudioClip.h"
#include <filesystem>

namespace fs = std::filesystem;
using nlohmann::json;

namespace
{
	// 고르고 있는 에셋의 설정: 저장된 값과 편집 중인 값 (다르면 Apply / Revert 가 켜진다)
	struct State
	{
		std::wstring Path;
		long long Stamp = -1;
		json Saved;
		json Edit;
		int Tab = 0;
	};
	State s_State;

	void Sync(const std::wstring& path)
	{
		const long long stamp = AssetImport::MetaStamp(path);
		if (s_State.Path == path && s_State.Stamp == stamp)
			return;
		const bool samePath = s_State.Path == path;
		const bool untouched = s_State.Edit == s_State.Saved;
		s_State.Path = path;
		s_State.Stamp = stamp;
		s_State.Saved = AssetImport::LoadJson(path);
		// 다른 곳(CLI 등)에서 .meta 가 바뀌었어도 고치던 값은 그대로 둔다
		if (!samePath || untouched)
			s_State.Edit = s_State.Saved;
		if (!samePath)
			s_State.Tab = 0;
	}

	std::string Rel(const std::wstring& full)
	{
		return wstring_to_string(PathManager::GetI()->GetCutSolutionPath(full));
	}

	// 오른쪽에 Revert · Apply (Unity 와 같은 자리)
	void ApplyRevertRow(const std::wstring& path)
	{
		const bool changed = s_State.Edit != s_State.Saved;
		const bool playing = Application::IsPlaying();
		UnityGUI::Spacing(4.0f);
		const float w = 72.0f;
		ImGui::SetCursorPosX((std::max)(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - w * 2 - 6.0f));
		ImGui::BeginDisabled(!changed || playing);
		if (ImGui::Button("Revert##import", ImVec2(w, 0)))
			s_State.Edit = s_State.Saved;
		ImGui::SameLine(0, 6.0f);
		if (ImGui::Button("Apply##import", ImVec2(w, 0)))
		{
			std::string error;
			if (!ImportSettingsInspector::Apply(path, s_State.Edit, error))
				EditorLog::Write("Import", "apply failed: %s", error.c_str());
		}
		ImGui::EndDisabled();
		if (changed && playing)
			UnityGUI::HelpBox("Exit Play mode to apply import settings.", true);
		UnityGUI::Spacing(6.0f);
	}

	std::string Bytes(size_t b)
	{
		char buf[32];
		if (b >= 1024 * 1024) snprintf(buf, sizeof(buf), "%.1f MB", b / (1024.0 * 1024.0));
		else snprintf(buf, sizeof(buf), "%.1f KB", b / 1024.0);
		return buf;
	}

	// ------------------------------------------------------------------ 텍스처
	void DrawTexture(const std::wstring& path)
	{
		AssetImport::TextureSettings t;
		t.FromJson(s_State.Edit);
		static const char* sizes[] = { "32", "64", "128", "256", "512", "1024", "2048", "4096", "8192", "16384" };
		int si = 0;
		while (si < 9 && (32 << si) < t.MaxSize)
			++si;
		if (UnityGUI::Dropdown("Max Size", &si, sizes, 10))
			t.MaxSize = 32 << si;
		static const char* comps[] = { "None", "Normal Quality", "High Quality" };
		UnityGUI::Dropdown("Compression", &t.Compression, comps, 3);
		UnityGUI::Toggle("Generate Mip Maps", &t.MipMaps);
		s_State.Edit = t.ToJson();
		ApplyRevertRow(path);

		// 가져온 결과 + 미리보기
		const std::string rel = Rel(path);
		ComPtr<GfxShaderResourceView> srv = ResourceManager::GetI()->LoadTexture(string_to_wstring(rel));
		AssetImport::TextureInfo info;
		if (AssetImport::GetTextureInfo(path, info))
		{
			char buf[160];
			if (info.SourceWidth > 0)
			{
				snprintf(buf, sizeof(buf), "%d x %d", info.SourceWidth, info.SourceHeight);
				UnityGUI::ValueLabel("Source Size", buf);
			}
			snprintf(buf, sizeof(buf), "%d x %d  %s  %d mips  %s", info.Width, info.Height, info.Format.c_str(), info.Mips, Bytes(info.Bytes).c_str());
			UnityGUI::ValueLabel("Imported", buf);
		}
		if (srv && info.Width > 0 && info.Height > 0)
		{
			UnityGUI::Spacing(6.0f);
			const float avail = ImGui::GetContentRegionAvail().x - 8.0f;
			const float side = (std::min)(avail, 256.0f);
			const float k = side / (float)(std::max)(info.Width, info.Height);
			const ImVec2 size(info.Width * k, info.Height * k);
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - size.x) * 0.5f);
			const ImVec2 p = ImGui::GetCursorScreenPos();
			// 투명한 곳이 보이게 체크무늬
			ImDrawList* dl = ImGui::GetWindowDrawList();
			const float cell = 8.0f;
			for (float y = 0; y < size.y; y += cell)
				for (float x = 0; x < size.x; x += cell)
					dl->AddRectFilled(ImVec2(p.x + x, p.y + y), ImVec2(p.x + (std::min)(x + cell, size.x), p.y + (std::min)(y + cell, size.y)),
						(((int)(x / cell) + (int)(y / cell)) & 1) ? IM_COL32(90, 90, 90, 255) : IM_COL32(70, 70, 70, 255));
			ImGui::Image((ImTextureID)srv.Get(), size);
		}
	}

	// ------------------------------------------------------------------ 모델
	void DrawModel(const std::wstring& path)
	{
		AssetImport::ModelSettings m;
		m.FromJson(s_State.Edit);
		const std::string rel = Rel(path);
		std::shared_ptr<MeshFile> file = ResourceManager::GetI()->LoadMeshFile(rel);
		const SkeletonAvataData* skeleton = file && !file->Avatas.empty() ? file->Avatas[0].get() : nullptr;

		if (ImGui::BeginTabBar("##importTabs"))
		{
			if (ImGui::BeginTabItem("Model"))
			{
				s_State.Tab = 0;
				UnityGUI::Spacing(4.0f);
				if (UnityGUI::Float("Scale Factor", &m.ScaleFactor))
					m.ScaleFactor = (std::max)(1e-4f, m.ScaleFactor);
				if (file)
				{
					char buf[64];
					if (skeleton)
					{
						snprintf(buf, sizeof(buf), "%g (file units to meters)", file->ScaleFactor > 0 ? skeleton->UnitScale / file->ScaleFactor : skeleton->UnitScale);
						UnityGUI::ValueLabel("File Scale", buf);
					}
					snprintf(buf, sizeof(buf), "%d", (int)file->Meshs.size());
					UnityGUI::ValueLabel("Meshes", buf);
					snprintf(buf, sizeof(buf), "%d", (int)file->SkinnedMeshs.size());
					UnityGUI::ValueLabel("Skinned Meshes", buf);
					if (skeleton)
					{
						snprintf(buf, sizeof(buf), "%d", (int)skeleton->NodeNames.size());
						UnityGUI::ValueLabel("Nodes", buf);
					}
				}
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Rig"))
			{
				s_State.Tab = 1;
				UnityGUI::Spacing(4.0f);
				const Humanoid::Avatar* av = skeleton ? &Humanoid::Get(*skeleton) : nullptr;
				// 지정하지 않았으면 (Auto) 사람 본을 찾았는지로 정한다 — 보여 주는 값
				int type = m.AnimationType;
				if (type == AssetImport::ModelSettings::Auto)
					type = av && av->Valid ? AssetImport::ModelSettings::Humanoid : AssetImport::ModelSettings::Generic;
				static const char* types[] = { "Generic", "Humanoid" };
				if (UnityGUI::Dropdown("Animation Type", &type, types, 2))
					m.AnimationType = type;
				if (m.AnimationType == AssetImport::ModelSettings::Auto)
					UnityGUI::HelpBox("Not set: Humanoid when the humanoid bones are found automatically, otherwise Generic.", false);
				if (skeleton == nullptr)
					UnityGUI::HelpBox("This model has no skeleton.", false);
				else if (type == AssetImport::ModelSettings::Humanoid && av)
				{
					char title[96];
					if (av->Generic)
						snprintf(title, sizeof(title), "Avatar: apply to build the humanoid mapping");
					else
						snprintf(title, sizeof(title), "Avatar: %d/%d humanoid bones%s", av->Found, (int)Humanoid::BoneCount, av->Valid ? "" : " (missing required bones)");
					UnityGUI::ValueLabel("Avatar", av->Valid ? "Humanoid" : "Invalid");
					if (UnityGUI::FoldoutPlain(title, 0, true))
					{
						// 본마다: 자동으로 찾은 노드 또는 직접 고른 노드
						const std::vector<std::string>& nodes = skeleton->NodeNames;
						for (int b = 0; b < Humanoid::BoneCount; ++b)
						{
							const char* boneName = Humanoid::BoneName(b);
							auto it = m.HumanBones.find(boneName);
							const bool overridden = it != m.HumanBones.end();
							std::string current;
							if (overridden)
								current = it->second;
							else if (!av->Generic && av->Node[b] >= 0 && av->Node[b] < (int)nodes.size())
								current = nodes[av->Node[b]];
							std::string shown = current.empty() ? std::string("None") : current;
							if (!overridden)
								shown += "  (auto)";
							ImGui::PushID(b);
							ImGui::AlignTextToFramePadding();
							ImGui::TextUnformatted(boneName);
							ImGui::SameLine(ImGui::GetWindowContentRegionMax().x * 0.42f);
							ImGui::SetNextItemWidth(-1);
							if (ImGui::BeginCombo("##node", shown.c_str(), ImGuiComboFlags_HeightLarge))
							{
								if (ImGui::Selectable("Auto", !overridden))
									m.HumanBones.erase(boneName);
								if (ImGui::Selectable("None", overridden && _stricmp(current.c_str(), "None") == 0))
									m.HumanBones[boneName] = "None";
								ImGui::Separator();
								for (const std::string& n : nodes)
									if (ImGui::Selectable(n.c_str(), overridden && n == current))
										m.HumanBones[boneName] = n;
								ImGui::EndCombo();
							}
							ImGui::PopID();
						}
					}
				}
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Animation"))
			{
				s_State.Tab = 2;
				UnityGUI::Spacing(4.0f);
				UnityGUI::Toggle("Import Animation", &m.ImportAnimation);
				if (file && m.ImportAnimation)
				{
					if (file->SkinnedData.AnimationClips.empty())
						UnityGUI::HelpBox("No animation clips in this file.", false);
					else
					{
						UnityGUI::Label("Clips", 0, true);
						for (const auto& clip : file->SkinnedData.AnimationClips)
							if (clip)
							{
								char buf[64];
								snprintf(buf, sizeof(buf), "%.2f s", clip->Duration);
								UnityGUI::ValueLabel(clip->Name.empty() ? "(unnamed)" : clip->Name.c_str(), buf, 1);
							}
					}
				}
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
		s_State.Edit = m.ToJson();
		ApplyRevertRow(path);
	}

	// ------------------------------------------------------------------ 오디오
	void DrawAudio(const std::wstring& path)
	{
		AssetImport::AudioSettings a;
		a.FromJson(s_State.Edit);
		UnityGUI::Toggle("Force To Mono", &a.ForceToMono);
		static const char* loads[] = { "Decompress On Load", "Compressed In Memory", "Streaming" };
		UnityGUI::Dropdown("Load Type", &a.LoadType, loads, 3);
		std::string ext = fs::path(path).extension().string();
		std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
		if (ext == ".wav" && a.LoadType != AssetImport::AudioSettings::DecompressOnLoad)
			UnityGUI::HelpBox("WAV files are always loaded uncompressed.", false);
		s_State.Edit = a.ToJson();
		ApplyRevertRow(path);
	}
}

namespace ImportSettingsInspector
{
	bool Draw(const std::wstring& fullPath)
	{
		const AssetImport::Kind kind = AssetImport::KindOf(fullPath);
		if (kind == AssetImport::Kind::None)
			return false;
		Sync(fullPath);
		const std::string name = fs::path(fullPath).stem().string();
		// 제목 (Unity: "<이름> Import Settings") — 이름이 길어도 왼쪽 칸에서 자르지 않게 한 줄 전체에
		ImGui::PushFont(UnityGUI::BoldFont());
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4.0f);
		ImGui::TextUnformatted((name + " Import Settings").c_str());
		ImGui::PopFont();
		UnityGUI::Spacing(4.0f);
		if (kind == AssetImport::Kind::Texture) DrawTexture(fullPath);
		else if (kind == AssetImport::Kind::Model) DrawModel(fullPath);
		else DrawAudio(fullPath);
		return true;
	}

	void Reimport(const std::wstring& fullPath)
	{
		const std::string rel = Rel(fullPath);
		switch (AssetImport::KindOf(fullPath))
		{
		case AssetImport::Kind::Texture:
			ResourceManager::GetI()->ForgetAsset(rel);
			ResourceManager::GetI()->ReloadMaterialTextures();
			break;
		case AssetImport::Kind::Model:
			ResourceManager::GetI()->ForgetAsset(rel);
			ResourceManager::GetI()->LoadMeshFile(rel);   // .meta 가 캐시보다 새로우니 지금 다시 가져온다
			break;
		case AssetImport::Kind::Audio:
			AudioClip::Forget(rel);
			break;
		default:
			return;
		}
		// 씬을 그대로 다시 만들어 컴포넌트가 새로 불러오게 (Undo · 저장 표시는 그대로)
		if (!Application::IsPlaying())
			if (Scene* scene = SceneManager::GetI()->GetCurrentScene())
			{
				json j = *scene;
				SceneManager::GetI()->RestoreSceneState(j.dump());
			}
		// 이 모델을 고르고 있으면 Inspector 미리보기도 새로
		if (SelectionManager::GetSelectedObjectType() == SelectionType::FILE && SelectionManager::GetSelectedFile() == fullPath)
			SelectionManager::SetSelectedFile(fullPath);
		EditorLog::Write("Import", "reimported %s with %s", rel.c_str(), AssetImport::LoadJson(fullPath).dump().c_str());
	}

	bool Apply(const std::wstring& fullPath, const json& settings, std::string& error)
	{
		if (AssetImport::KindOf(fullPath) == AssetImport::Kind::None)
		{
			error = "no import settings for this file type (textures, .fbx models, .wav/.ogg/.mp3 audio)";
			return false;
		}
		std::error_code ec;
		if (!fs::exists(fullPath, ec))
		{
			error = "file not found";
			return false;
		}
		if (Application::IsPlaying())
		{
			error = "not allowed in Play mode";
			return false;
		}
		if (!AssetImport::Save(fullPath, settings))
		{
			error = "cannot write " + wstring_to_string(AssetImport::MetaPath(fullPath));
			return false;
		}
		Reimport(fullPath);
		if (s_State.Path == fullPath)
		{
			s_State.Stamp = -1;   // 다음 Draw 에서 저장본을 다시 읽는다
			s_State.Saved = s_State.Edit = AssetImport::LoadJson(fullPath);
		}
		return true;
	}
}
