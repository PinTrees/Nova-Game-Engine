#include "pch.h"
#include "AnimationClipLibrary.h"
#include "SkinnedMesh.h"

namespace AnimationClipLibrary
{
	namespace
	{
		std::vector<Entry> s_Entries;
		bool s_Ready = false;

		void ScanFolder(const std::wstring& root, const std::wstring& prefix)
		{
			std::error_code ec;
			if (!std::filesystem::exists(root, ec))
				return;
			for (const auto& entry : std::filesystem::recursive_directory_iterator(root, ec))
			{
				if (!entry.is_regular_file())
					continue;
				std::wstring ext = entry.path().extension().wstring();
				std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
				if (ext != L".fbx")
					continue;
				const std::wstring rel = prefix + std::filesystem::relative(entry.path(), root, ec).wstring();
				auto file = ResourceManager::GetI()->LoadMeshFile(wstring_to_string(rel));
				if (file == nullptr)
					continue;
				for (int i = 0; i < (int)file->SkinnedData.AnimationClips.size(); ++i)
					s_Entries.push_back({ wstring_to_string(rel), i, file->SkinnedData.AnimationClips[i]->Name });
			}
		}
	}

	void Refresh()
	{
		s_Entries.clear();
		ScanFolder(PathManager::GetI()->GetMovePathW(L"Resources\\Packages\\"), L"Resources\\Packages\\");
		ScanFolder(PathManager::GetI()->GetMovePathW(L"Assets\\"), L"Assets\\");
		s_Ready = true;
	}

	const std::vector<Entry>& Entries()
	{
		if (!s_Ready)
			Refresh();
		return s_Entries;
	}

	bool DrawPickerPopup(const char* popupId, std::string& path, int& index, bool allowNone)
	{
		bool picked = false;
		ImGui::SetNextWindowSizeConstraints(ImVec2(320, 0), ImVec2(560, 420));
		if (ImGui::BeginPopup(popupId))
		{
			if (allowNone && ImGui::Selectable("None"))
			{
				path.clear();
				index = 0;
				picked = true;
			}
			for (const Entry& e : Entries())
			{
				const std::string text = e.Name + "   (" + std::filesystem::path(e.Path).filename().string() + ")";
				if (ImGui::Selectable(text.c_str(), e.Path == path && e.Index == index))
				{
					path = e.Path;
					index = e.Index;
					picked = true;
				}
			}
			ImGui::Separator();
			if (ImGui::Selectable("Refresh", false, ImGuiSelectableFlags_DontClosePopups))
				Refresh();
			ImGui::EndPopup();
		}
		return picked;
	}
}
