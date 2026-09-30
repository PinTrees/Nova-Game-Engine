#pragma once
#include "EditorWindow.h"
#include <filesystem>

namespace fs = std::filesystem;

class ProjectEditorWindow
	: public EditorWindow
{
public:
	static wchar_t  newName[512];
	static bool renaming;
	static std::wstring renamingPath;
	static vector<char> renameBuffVec;

	static std::wstring solutionDirectory;
	static std::wstring currentDirectory;

public:
	ProjectEditorWindow();
	~ProjectEditorWindow();

protected:
	virtual void OnRender() override;

private:
	void DisplayDirectoryTree(const std::wstring& directory);

	static void DisplayDirectoryContents(const std::wstring& directory);
	static void RenderFileEntry(const std::filesystem::directory_entry& entry, bool isSelected);
	static void RenameSelectedFile(const std::wstring& oldPath, const std::wstring& newName);

	static void RenderFileEntry_Directory(const std::filesystem::directory_entry& entry, bool isSelected);
	static void RenderFileEntry_FBX(const std::filesystem::directory_entry& entry, bool isSelected);
	static void RenderFileEntry_PNG(const std::filesystem::directory_entry& entry, bool isSelected);
	static void RenderFileEntry_MAT(const std::filesystem::directory_entry& entry, bool isSelected);
	static void RenderFileEntry_Scene(const std::filesystem::directory_entry& entry, bool isSelected);
};

