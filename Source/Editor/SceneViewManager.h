#pragma once

class SceneEditorWindow;

class SceneViewManager
{
	SINGLE_HEADER(SceneViewManager)

public:
	SceneEditorWindow* m_LastActiveSceneEditorWindow;

private:
	Vec2 m_LastMousePos;

public:
	void Update();
};

