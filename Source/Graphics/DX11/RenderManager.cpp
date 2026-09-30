#include "pch.h"
#include "RenderManager.h"
#include "ShadowMap.h"

SINGLE_BODY(RenderManager)

RenderManager::RenderManager()
{

}

RenderManager::~RenderManager()
{

}

void RenderManager::Init()
{
	auto device = Application::GetI()->GetDevice();
	BaseShadowMap = make_shared<ShadowMap>(device, SMapSize, SMapSize);   
    EditorShadowMap = make_shared<ShadowMap>(device, SMapSize, SMapSize);

    SetEditorViewport(100, 100);
    SetViewport(100, 100);
}

void RenderManager::SetEditorViewport(UINT width, UINT height)
{
    EditorViewport.TopLeftX = 0.0f;
    EditorViewport.TopLeftY = 0.0f;
    EditorViewport.Width = static_cast<float>(width); 
    EditorViewport.Height = static_cast<float>(height); 
    EditorViewport.MinDepth = 0.0f;
    EditorViewport.MaxDepth = 1.0f;
}

void RenderManager::SetViewport(UINT width, UINT height)
{
    Viewport.TopLeftX = 0.0f;
    Viewport.TopLeftY = 0.0f;
    Viewport.Width = static_cast<float>(width);
    Viewport.Height = static_cast<float>(height);
    Viewport.MinDepth = 0.0f;
    Viewport.MaxDepth = 1.0f;
}
