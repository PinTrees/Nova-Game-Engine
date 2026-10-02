#include "pch.h"
#include "ImportSettingsInspector.h"
#include "AssetImportSettings.h"
#include "UnityGUI.h"
#include "SelectionManager.h"
#include "SkinnedMesh.h"
#include "SkinnedData.h"
#include "HumanoidAvatar.h"
#include "AudioClip.h"
#include "SpriteSlicer.h"
#include "SpriteAnimator.h"
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
	std::vector<std::pair<std::wstring, json>> s_Deferred;

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
			ImportSettingsInspector::ApplyDeferred(path, s_State.Edit);   // 그리는 중에 씬을 다시 만들지 않게
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

	// ------------------------------------------------------------------ Sprite Mode / Sprite Editor (Slice)
	// Unity 의 Sprite Editor 를 Inspector 안에: 자르는 방법을 고르고 Slice → 사각형 목록 (Apply 로 저장), 미리 보기에 사각형 표시
	struct SliceUi
	{
		int Type = 1;                 // 0 Automatic, 1 Grid By Cell Size, 2 Grid By Cell Count, 3 2D Animator Sheet
		int Cell[2] = { 32, 32 };
		int Count[2] = { 4, 1 };
		int Offset[2] = { 0, 0 };
		int Padding[2] = { 0, 0 };
		int MinSize = 4;
		int Pivot = 0;                // Center / Bottom / Top / Left / Right / Bottom Left
		bool KeepEmpty = false;
		float Fps = 12.0f;
		std::string Message;
	};
	SliceUi s_Slice;

	void DrawSpriteMode(const std::wstring& path, AssetImport::TextureSettings& t)
	{
		static const char* modes[] = { "Single", "Multiple" };
		UnityGUI::Dropdown("Sprite Mode", &t.SpriteMode, modes, 2);
		if (t.SpriteMode != AssetImport::TextureSettings::MultipleSprites)
			return;
		char buf[96];
		snprintf(buf, sizeof(buf), "%d sprites", (int)t.Sprites.size());
		UnityGUI::ValueLabel("Sprites", buf, 1);
		if (!UnityGUI::FoldoutPlain("Slice (Sprite Editor)", 1))
			return;
		const std::wstring sheet = SpriteSlicer::SheetJsonFor(path);
		static const char* types[] = { "Automatic", "Grid By Cell Size", "Grid By Cell Count", "2D Animator Sheet (.json)" };
		int typeCount = sheet.empty() ? 3 : 4;
		if (s_Slice.Type >= typeCount) s_Slice.Type = 1;
		UnityGUI::Dropdown("Type", &s_Slice.Type, types, typeCount, 2);
		if (s_Slice.Type == 0)
			UnityGUI::Int("Minimum Size", &s_Slice.MinSize, 2);
		if (s_Slice.Type == 1)
		{
			UnityGUI::Int("Cell Width", &s_Slice.Cell[0], 2);
			UnityGUI::Int("Cell Height", &s_Slice.Cell[1], 2);
			UnityGUI::Int("Offset X", &s_Slice.Offset[0], 2);
			UnityGUI::Int("Offset Y", &s_Slice.Offset[1], 2);
			UnityGUI::Int("Padding X", &s_Slice.Padding[0], 2);
			UnityGUI::Int("Padding Y", &s_Slice.Padding[1], 2);
		}
		if (s_Slice.Type == 2)
		{
			UnityGUI::Int("Columns", &s_Slice.Count[0], 2);
			UnityGUI::Int("Rows", &s_Slice.Count[1], 2);
		}
		static const char* pivots[] = { "Center", "Bottom", "Top", "Left", "Right", "Bottom Left" };
		static const float pv[6][2] = { { 0.5f, 0.5f }, { 0.5f, 0 }, { 0.5f, 1 }, { 0, 0.5f }, { 1, 0.5f }, { 0, 0 } };
		if (s_Slice.Type != 3)
			UnityGUI::Dropdown("Pivot", &s_Slice.Pivot, pivots, 6, 2);
		else
			UnityGUI::ValueLabel("Pivot", "from the sheet (skeleton origin = feet)", 2);
		if (s_Slice.Type == 1 || s_Slice.Type == 2)
			UnityGUI::Toggle("Keep Empty Rects", &s_Slice.KeepEmpty, 2);
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 34.0f);
		if (ImGui::Button("Slice", ImVec2(90, 0)))
		{
			SpriteSlicer::Pixels px;
			SpriteSlicer::Options o;
			o.BaseName = wstring_to_string(fs::path(path).stem().wstring());
			o.PivotX = pv[s_Slice.Pivot][0];
			o.PivotY = pv[s_Slice.Pivot][1];
			o.KeepEmpty = s_Slice.KeepEmpty;
			if (!px.Load(path))
				s_Slice.Message = "Cannot read the image";
			else
			{
				std::vector<AssetImport::SpriteRect> rects;
				if (s_Slice.Type == 0) rects = SpriteSlicer::Automatic(px, (std::max)(1, s_Slice.MinSize), o);
				else if (s_Slice.Type == 1) rects = SpriteSlicer::GridBySize(px, s_Slice.Cell[0], s_Slice.Cell[1], s_Slice.Offset[0], s_Slice.Offset[1], s_Slice.Padding[0], s_Slice.Padding[1], o);
				else if (s_Slice.Type == 2) rects = SpriteSlicer::GridByCount(px, s_Slice.Count[0], s_Slice.Count[1], o);
				else SpriteSlicer::FromSheetJson(sheet, px.W, px.H, o, rects, &s_Slice.Fps);
				t.Sprites = rects;
				s_Slice.Message = std::to_string(rects.size()) + " sprites - press Apply to save";
			}
		}
		ImGui::SameLine();
		ImGui::TextDisabled("%s", s_Slice.Message.c_str());
		// 프레임 애니메이션: 저장된 스프라이트들을 차례로 (.spriteanim 을 그림 옆에)
		if (!t.Sprites.empty())
		{
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 34.0f);
			const bool saved = s_State.Edit == s_State.Saved;
			ImGui::BeginDisabled(!saved);
			if (ImGui::Button(ICON_FA_FILM " Create Sprite Animation"))
			{
				SpriteAnimClip clip;
				clip.Fps = s_Slice.Fps;
				const std::string rel = Rel(path);
				for (const AssetImport::SpriteRect& r : t.Sprites)
					clip.Frames.push_back(rel + "#" + r.Name);
				fs::path anim = fs::path(string_to_wstring(rel)).replace_extension(L".spriteanim");
				SpriteAnimClips::Save(wstring_to_string(anim.wstring()), clip);
				s_Slice.Message = "made " + wstring_to_string(anim.filename().wstring());
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::SetNextItemWidth(60.0f);
			ImGui::DragFloat("fps", &s_Slice.Fps, 0.2f, 1.0f, 60.0f, "%.0f");
			if (!saved)
				ImGui::TextDisabled("      Apply first, then make the animation");
		}
	}

	// ------------------------------------------------------------------ 텍스처
	void DrawTexture(const std::wstring& path)
	{
		AssetImport::TextureSettings t;
		t.FromJson(s_State.Edit);
		static const char* types[] = { "Default", "Normal map", "Sprite (2D and UI)" };
		const int oldType = t.TextureType;
		if (UnityGUI::Dropdown("Texture Type", &t.TextureType, types, 3) && t.TextureType != oldType)
			t.MipMaps = t.TextureType != AssetImport::TextureSettings::Sprite;   // Unity: Sprite 는 밉 없이
		if (t.TextureType == AssetImport::TextureSettings::Sprite)
		{
			// Unity 의 Sprite Mode = Single: 그림 전체가 스프라이트 하나 (SpriteRenderer 크기 = 픽셀 / Pixels Per Unit)
			UnityGUI::Float("Pixels Per Unit", &t.PixelsPerUnit);
			t.PixelsPerUnit = (std::max)(0.01f, t.PixelsPerUnit);
			static const char* pivots[] = { "Center", "Top Left", "Top", "Top Right", "Left", "Right", "Bottom Left", "Bottom", "Bottom Right", "Custom" };
			static const float pv[9][2] = { { 0.5f, 0.5f }, { 0, 1 }, { 0.5f, 1 }, { 1, 1 }, { 0, 0.5f }, { 1, 0.5f }, { 0, 0 }, { 0.5f, 0 }, { 1, 0 } };
			int pi = 9;
			for (int i = 0; i < 9; ++i)
				if (fabsf(t.PivotX - pv[i][0]) < 1e-4f && fabsf(t.PivotY - pv[i][1]) < 1e-4f)
					pi = i;
			if (UnityGUI::Dropdown("Pivot", &pi, pivots, 10) && pi < 9)
			{
				t.PivotX = pv[pi][0];
				t.PivotY = pv[pi][1];
			}
			if (pi == 9)
				UnityGUI::Vector2Pair("Custom Pivot", "X", &t.PivotX, "Y", &t.PivotY, 1);
			DrawSpriteMode(path, t);
		}
		if (t.TextureType == AssetImport::TextureSettings::NormalMap)
			UnityGUI::ValueLabel("sRGB (Color Texture)", "Off (normal maps are linear)");
		else
			UnityGUI::Toggle("sRGB (Color Texture)", &t.SRGB);
		UnityGUI::Spacing(4.0f);
		static const char* sizes[] = { "32", "64", "128", "256", "512", "1024", "2048", "4096", "8192", "16384" };
		int si = 0;
		while (si < 9 && (32 << si) < t.MaxSize)
			++si;
		if (UnityGUI::Dropdown("Max Size", &si, sizes, 10))
			t.MaxSize = 32 << si;
		static const char* comps[] = { "None", "Normal Quality", "High Quality" };
		UnityGUI::Dropdown("Compression", &t.Compression, comps, 3);
		UnityGUI::Toggle("Generate Mip Maps", &t.MipMaps);
		static const char* filters[] = { "Point (no filter)", "Bilinear", "Trilinear" };
		UnityGUI::Dropdown("Filter Mode", &t.FilterMode, filters, 3);   // 지금은 스프라이트 그리기에만 쓴다 (도트 그림 = Point)
		s_State.Edit = t.ToJson();
		ApplyRevertRow(path);
		if (!AssetImport::AppliesTo(path))
			UnityGUI::HelpBox("This texture is outside Assets and the engine packages: import settings are not used for it.", false);

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
			// Sprite Mode = Multiple: 잘라 놓은 사각형 (원본 픽셀, 왼쪽 아래 원점)
			if (t.TextureType == AssetImport::TextureSettings::Sprite && t.SpriteMode == AssetImport::TextureSettings::MultipleSprites)
			{
				const float sw = info.SourceWidth > 0 ? (float)info.SourceWidth : (float)info.Width;
				const float sh = info.SourceHeight > 0 ? (float)info.SourceHeight : (float)info.Height;
				const float kx = size.x / sw, ky = size.y / sh;
				for (const AssetImport::SpriteRect& r : t.Sprites)
				{
					const ImVec2 a(p.x + r.X * kx, p.y + (sh - r.Y - r.H) * ky), b(a.x + r.W * kx, a.y + r.H * ky);
					dl->AddRect(a, b, IM_COL32(80, 200, 255, 230));
					dl->AddCircleFilled(ImVec2(a.x + r.PivotX * (b.x - a.x), b.y - r.PivotY * (b.y - a.y)), 2.0f, IM_COL32(255, 120, 60, 255));
				}
			}
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

	void ApplyDeferred(const std::wstring& fullPath, const json& settings)
	{
		s_Deferred.push_back({ fullPath, settings });
	}

	void Update()
	{
		if (s_Deferred.empty())
			return;
		auto work = std::move(s_Deferred);
		s_Deferred.clear();
		for (const auto& [path, settings] : work)
		{
			std::string error;
			if (!Apply(path, settings, error))
				EditorLog::Write("Import", "apply failed for %s: %s", wstring_to_string(path).c_str(), error.c_str());
		}
	}

	void MarkAsNormalMap(const std::wstring& fullPath)
	{
		json s = AssetImport::LoadJson(fullPath);
		s["textureType"] = "NormalMap";
		ApplyDeferred(fullPath, s);
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
