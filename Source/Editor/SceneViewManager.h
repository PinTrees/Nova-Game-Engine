#pragma once

class SceneEditorWindow;

class SceneViewManager
{
	SINGLE_HEADER(SceneViewManager)

public:
	SceneEditorWindow* m_LastActiveSceneEditorWindow = nullptr;   // 빌드된 게임에서는 계속 null

private:
	Vec2 m_LastMousePos;

public:
	void Update();
};

