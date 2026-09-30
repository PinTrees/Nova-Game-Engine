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
	void DrawContextMenu(class Scene* scene, GameObject* target);
	void HandleShortcuts(class Scene* scene);

	// 편집 (Unity 컨텍스트 메뉴)
	void CopyObject(GameObject* target, bool cut);
	GameObject* PasteObject(class Scene* scene, GameObject* parent);
	GameObject* DuplicateObject(class Scene* scene, GameObject* target);
	void BeginRename(GameObject* target);

	// Handle Drag an Drop
	void HandleFbxFileDrop(const std::string& filePath, GameObject* parent);

private:
	char m_Search[64] = {};
	bool m_PendingDiscard = false;   // 씬 메뉴의 Discard changes (다음 프레임에 처리)
	bool m_SceneOpen = true;
	bool m_WindowFocused = false;
	GameObject* m_PendingDelete = nullptr;

	// 이름 바꾸기 (F2 / 우클릭 Rename)
	GameObject* m_RenameTarget = nullptr;
	char m_RenameBuffer[128] = {};
	int m_RenameFrames = 0;
};
