#include "pch.h"
#include "SceneEditorWindow.h"
#include "App.h"
#include "EditorCamera.h"
#include "MathHelper.h"

SceneEditorWindow::SceneEditorWindow()
    : EditorWindow("Scene", ICON_FA_BORDER_ALL),
    windowWidth(800),
    windowHeight(600)
{
    m_Camera = new EditorCamera;
    InitRenderTarget(windowWidth, windowHeight);
    SceneViewManager::GetI()->m_LastActiveSceneEditorWindow = this;
}

SceneEditorWindow::~SceneEditorWindow()
{
    delete m_Camera;
}

void SceneEditorWindow::InitRenderTarget(UINT width, UINT height)
{
    CleanUpRenderTarget(); // 이전 렌더 타겟이 있으면 정리

    // 렌더 타겟 텍스처 생성
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
    if (FAILED(hr)) { /* 에러 처리 */ }

    // 렌더 타겟 뷰 생성
    hr = device->CreateRenderTargetView(renderTargetTexture, nullptr, &renderTargetView);
    if (FAILED(hr)) { /* 에러 처리 */ }

    // 셰이더 리소스 뷰 생성
    hr = device->CreateShaderResourceView(renderTargetTexture, nullptr, &shaderResourceView);
    if (FAILED(hr)) { /* 에러 처리 */ }

    float aspectRatio = static_cast<float>(width) / height;
    m_Camera->SetLens(0.25f * MathHelper::Pi, aspectRatio, 1.0f, 1000.0f);
    PostProcessingManager::GetI()->_EditorSetSSAO(width, height, m_Camera);
    RenderManager::GetI()->SetEditorViewport(width, height);
}

void SceneEditorWindow::CleanUpRenderTarget()
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

void SceneEditorWindow::RenderScene()
{
    XMMATRIX view = m_Camera->View();
    XMMATRIX proj = m_Camera->Proj();
    XMMATRIX viewProj = m_Camera->ViewProj();
    RenderManager::GetI()->EditorCameraViewProjectionMatrix = view * proj;
    
    auto context = Application::GetI()->GetDeviceContext();
    
    // 기존 렌더 타겟 백업
    context->OMGetRenderTargets(1, &oldRenderTarget, nullptr);
    
    // 새 렌더 타겟 설정
    context->OMSetRenderTargets(1, &renderTargetView, nullptr);

    // 씬 렌더
    Application::GetI()->GetApp()->_Editor_OnSceneRender(renderTargetView, m_Camera);

    if (oldRenderTarget == nullptr)
        return;
    
    // 기존 렌더 타겟 복원
    context->OMSetRenderTargets(1, &oldRenderTarget, nullptr);
    if (oldRenderTarget) oldRenderTarget->Release();
    
    oldRenderTarget = nullptr;
}


void SceneEditorWindow::Update()
{
}

void SceneEditorWindow::PushStyle()
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
}

void SceneEditorWindow::PopStyle()
{
    ImGui::PopStyleVar();
}

void SceneEditorWindow::OnRender()
{
    ImVec2 windowSize = ImGui::GetContentRegionAvail();

    // 왼쪽에 세로로 탭 메뉴 배치
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0)); // 창 내부 패딩 제거
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));   // 항목 간 간격 제거
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));  // 버튼 내부 패딩 제거
    ImGui::BeginChild("LeftPanel", ImVec2(24, windowSize.y), false, ImGuiWindowFlags_NoDecoration); // 테두리 제거

    bool isWireframeMode = RenderManager::GetI()->WireFrameMode;
    bool isInstancingMode = RenderManager::GetI()->InstancingMode;

    if (isWireframeMode)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered)); // 선택된 버튼 색상
    }

    if (ImGui::Button("W", ImVec2(24, 24)))
    {
        RenderManager::GetI()->WireFrameMode = !RenderManager::GetI()->WireFrameMode;
    }

    if (isWireframeMode)
    {
        ImGui::PopStyleColor(); // 변경된 색상 복원
    }

    if (isInstancingMode)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered)); // 선택된 버튼 색상
    }

    if (ImGui::Button("I", ImVec2(24, 25)))
    {
        RenderManager::GetI()->InstancingMode = !RenderManager::GetI()->InstancingMode;
    }

    if (isInstancingMode)
    {
        ImGui::PopStyleColor(); // 변경된 색상 복원
    }

    // 다른 버튼들 추가 가능
    ImGui::EndChild();
    ImGui::PopStyleVar(3); // 세 개의 스타일 변수 복원

    ImGui::SameLine();

    // 가상의 영역 설정
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("MainArea", ImVec2(windowSize.x - 24, windowSize.y), false, ImGuiWindowFlags_NoDecoration);
    ImGui::PopStyleVar();

    windowSize = ImGui::GetContentRegionAvail();
    if (windowWidth != windowSize.x || windowHeight != windowSize.y)
    {
        windowWidth = static_cast<UINT>(windowSize.x);
        windowHeight = static_cast<UINT>(windowSize.y);
        InitRenderTarget(windowWidth, windowHeight);
    }

    RenderScene();

    // ImGui 창에 렌더 타겟 텍스처를 표시
    ImGui::Image(reinterpret_cast<void*>(shaderResourceView), windowSize);

    SceneManager::GetI()->GetCurrentScene()->RenderSceneGizmos();

    ImGui::EndChild(); // MainArea 종료
}
