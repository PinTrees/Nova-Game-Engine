#pragma once
#include "SSAO.h"
#include "PostProcessPass.h"

class Camera;
class EditorCamera;

class PostProcessingManager
{
	SINGLE_HEADER(PostProcessingManager)

private:
	std::unique_ptr<Ssao> m_EditorSSAO;
	std::unique_ptr<Ssao> m_GameSSAO;

	// Volume 후처리 (뷰마다 타깃과 섞인 값이 따로)
	PostProcessPass m_EditorPost, m_GamePost;
	VolumeStack m_EditorStack, m_GameStack;

public:
	void Init();

	void RenderSSAO(Camera* camera);
	void SetSSAO(int32 screenWidth, int32 screenHeight, Camera* camera);
	Ssao* GetSSAO() { return m_GameSSAO.get(); }

	// Editor
	void _Editor_RenderSSAO(EditorCamera* camera);
	void _EditorSetSSAO(int32 screenWidth, int32 screenHeight, EditorCamera* camera);
	Ssao* _EditorGetSSAO() { return m_EditorSSAO.get(); }

	// Volume
	PostProcessPass& GamePost() { return m_GamePost; }
	PostProcessPass& EditorPost() { return m_EditorPost; }
	VolumeStack& GameStack() { return m_GameStack; }
	VolumeStack& EditorStack() { return m_EditorStack; }
};

