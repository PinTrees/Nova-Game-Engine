#include "pch.h"
#include "PostProcessingManager.h"
#include "EditorCamera.h"

SINGLE_BODY(PostProcessingManager)

PostProcessingManager::PostProcessingManager()
{

}

PostProcessingManager::~PostProcessingManager()
{

}

void PostProcessingManager::Init()
{
}

// Scene 뷰도 Game 뷰와 같이 흐린다 (예전엔 Scene 뷰만 흐림 없이 노이즈 그대로)
void PostProcessingManager::_Editor_RenderSSAO(EditorCamera* camera, const Ssao::Settings& settings)
{
	m_EditorSSAO->Render(camera->Proj(), settings);
}

void PostProcessingManager::RenderSSAO(Camera* camera, const Ssao::Settings& settings)
{
	m_GameSSAO->Render(camera->Proj(), settings);
}

// 시야각 · 화면비는 그릴 때마다 투영에서 읽는다 (여기서는 타깃 크기만)
void PostProcessingManager::SetSSAO(int32 screenWidth, int32 screenHeight, Camera* camera)
{
	m_GameSSAO = std::make_unique<Ssao>();
	m_GameSSAO->Init(screenWidth, screenHeight);
}

void PostProcessingManager::_EditorSetSSAO(int32 screenWidth, int32 screenHeight, EditorCamera* camera)
{
	m_EditorSSAO = std::make_unique<Ssao>();
	m_EditorSSAO->Init(screenWidth, screenHeight);
}
