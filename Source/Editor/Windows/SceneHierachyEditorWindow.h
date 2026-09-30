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
	void DrawToolbar(class Scene* scene);
	void DrawSceneHeader(class Scene* scene);
	void DrawGameObject(GameObject* gameObject, int depth);
	void DrawCreateMenu(class Scene* scene, GameObject* parent);
	void PopupContextMenu();

	// Handle Drag an Drop
	void HandleFbxFileDrop(const std::string& filePath, GameObject* parent);

private:
	char m_Search[64] = {};
	bool m_SceneOpen = true;
	bool m_WindowFocused = false;
	GameObject* m_PendingDelete = nullptr;
};
