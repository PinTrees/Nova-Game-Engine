#pragma once
#include "SSAO.h"

class Camera;
class EditorCamera;

class PostProcessingManager
{
	SINGLE_HEADER(PostProcessingManager)

private:
	std::unique_ptr<Ssao> m_EditorSSAO;
	std::unique_ptr<Ssao> m_GameSSAO;

public:
	void Init();

	void RenderSSAO(Camera* camera);
	void SetSSAO(int32 screenWidth, int32 screenHeight, Camera* camera);
	Ssao* GetSSAO() { return m_GameSSAO.get(); }

	// Editor
	void _Editor_RenderSSAO(EditorCamera* camera);
	void _EditorSetSSAO(int32 screenWidth, int32 screenHeight, EditorCamera* camera);
	Ssao* _EditorGetSSAO() { return m_EditorSSAO.get(); }
};

