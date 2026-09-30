#pragma once
#include "EditorWindow.h"

class SceneHierachyEditorWindow
	: public EditorWindow
{
public:
	SceneHierachyEditorWindow();
	~SceneHierachyEditorWindow();

protected:
	virtual void OnRender() override;

private:
	void DrawGameObject(GameObject* gameObject);
	void PopupContextMenu();

	// Handle Drag an Drop
	void HandleFbxFileDrop(const std::string& filePath, GameObject* parent);
};

