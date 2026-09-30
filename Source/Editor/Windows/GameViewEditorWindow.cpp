#include "pch.h"
#include "GameViewEditorWindow.h"
#include "MathHelper.h"
#include "App.h"

GameViewEditorWindow::GameViewEditorWindow()
	: EditorWindow("Game", ICON_FA_GAMEPAD),  
    windowWidth(800),
    windowHeight(600)
{
    InitRenderTarget(windowWidth, windowHeight); 
}

GameViewEditorWindow::~GameViewEditorWindow()
{
}


void GameViewEditorWindow::InitRenderTarget(UINT width, UINT height)
{
    CleanUpRenderTarget(); // ÀÌÀü ·»´õ Å¸°ÙÀÌ ÀÖÀ¸¸é Á¤¸® 

    // ·»´õ Å¸°Ù ÅØ½ºÃ³ »ý¼º
    D3D11_TEXTURE2D_DESC textureDesc = {};
    textureDesc.Width = width;
    textureDesc.Height = height;
    textureDesc.MipLevels = 1;
    textureDesc.ArraySize = 1;
    textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Usage = D3D11_USAGE_DEFAULT;
    textureDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    auto device = Application::GetI()->GetDevice();

    HRESULT hr = device->CreateTexture2D(&textureDesc, nullptr, &renderTargetTexture);
    if (FAILED(hr)) { /* ¿¡·¯ Ã³¸® */ }

    // ·»´õ Å¸°Ù ºä »ý¼º
    hr = device->CreateRenderTargetView(renderTargetTexture, nullptr, &renderTargetView);
    if (FAILED(hr)) { /* ¿¡·¯ Ã³¸® */ }

    // ¼ÎÀÌ´õ ¸®¼Ò½º ºä »ý¼º
    hr = device->CreateShaderResourceView(renderTargetTexture, nullptr, &shaderResourceView);
    if (FAILED(hr)) { /* ¿¡·¯ Ã³¸® */ }

    auto camera = DisplayManager::GetI()->GetActiveCamera();
    if (camera) 
    {
        float aspectRatio = static_cast<float>(width) / height;
        camera->SetAspect(aspectRatio); 
        PostProcessingManager::GetI()->SetSSAO(width, height, camera.get()); 
    }
    
    RenderManager::GetI()->SetViewport(width, height); 
}

void GameViewEditorWindow::CleanUpRenderTarget()
{
    if (renderTargetView)
    {
        renderTargetView->Release();
        renderTargetView = nullptr;
    }

    if (shaderResourceView)
    {
        shaderResourceView->Release();
        shaderResourceView = nullptr;
    }

    if (renderTargetTexture)
    {
        renderTargetTexture->Release();
        renderTargetTexture = nullptr;
    }
}

void GameViewEditorWindow::RenderScene()
{
    shared_ptr<Camera> camera = DisplayManager::GetI()->GetActiveCamera();   

    if (camera == nullptr)
        return;

    auto context = Application::GetI()->GetDeviceContext();

    // ±âÁ¸ ·»´õ Å¸°Ù ¹é¾÷
    context->OMGetRenderTargets(1, &oldRenderTarget, nullptr);

    // »õ ·»´õ Å¸°Ù ¼³Á¤
    context->OMSetRenderTargets(1, &renderTargetView, nullptr);

    // ¾À ·»´õ
    camera->LateUpdate();
    XMMATRIX view = camera->View(); 
    XMMATRIX proj = camera->Proj(); 
    XMMATRIX viewProj = camera->ViewProj(); 
    RenderManager::GetI()->CameraViewProjectionMatrix = view * proj; 

    Application::GetI()->GetApp()->OnSceneRender(renderTargetView, camera.get()); 

    if (oldRenderTarget == nullptr)
        return;

    // ±âÁ¸ ·»´õ Å¸°Ù º¹¿ø
    context->OMSetRenderTargets(1, &oldRenderTarget, nullptr);
    if (oldRenderTarget) oldRenderTarget->Release();

    oldRenderTarget = nullptr;
}

void GameViewEditorWindow::OnRender()
{
    ImVec2 windowSize = ImGui::GetContentRegionAvail();

    // °¡»óÀÇ ¿µ¿ª ¼³Á¤
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("MainArea", ImVec2(windowSize.x, windowSize.y), false, ImGuiWindowFlags_NoDecoration);
    ImGui::PopStyleVar();

    windowSize = ImGui::GetContentRegionAvail();
    if (windowWidth != windowSize.x || windowHeight != windowSize.y)
    {
        windowWidth = static_cast<UINT>(windowSize.x);
        windowHeight = static_cast<UINT>(windowSize.y);
        InitRenderTarget(windowWidth, windowHeight);
    }

    RenderScene();

    // ImGui Ã¢¿¡ ·»´õ Å¸°Ù ÅØ½ºÃ³¸¦ Ç¥½Ã
    ImGui::Image(reinterpret_cast<void*>(shaderResourceView), windowSize);
    ImGui::EndChild(); // MainArea Á¾·á
}
