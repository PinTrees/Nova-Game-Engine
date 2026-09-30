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
	//m_GameSSAO = std::make_unique<Ssao>(m_Device, m_Context);
}

void PostProcessingManager::_Editor_RenderSSAO(EditorCamera* camera)
{
	m_EditorSSAO->ComputeSsao(*camera);
	//m_EditorSSAO->BlurAmbientMap(4); 
}

void PostProcessingManager::RenderSSAO(Camera* camera)
{
	m_GameSSAO->ComputeSsao(*camera);
	m_GameSSAO->BlurAmbientMap(4);
}

void PostProcessingManager::SetSSAO(int32 screenWidth, int32 screenHeight, Camera* camera)
{
	m_GameSSAO = std::make_unique<Ssao>(); 
	m_GameSSAO->Init(screenWidth, screenHeight, camera->GetFovY(), camera->GetFarZ()); 
}

void PostProcessingManager::_EditorSetSSAO(int32 screenWidth, int32 screenHeight, EditorCamera* camera)
{
	m_EditorSSAO = std::make_unique<Ssao>();
	m_EditorSSAO->Init(screenWidth, screenHeight, camera->GetFovY(), camera->GetFarZ());
}
