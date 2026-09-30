#include "pch.h"
#include "SceneEditorWindow.h"
#include "App.h"
#include "EditorCamera.h"
#include "MathHelper.h"
#include "SceneViewOverlay.h"

SceneEditorWindow::SceneEditorWindow()
    : EditorWindow("Scene", ICON_FA_BORDER_ALL),
    windowWidth(800),
    windowHeight(600)
{
    m_Camera = new EditorCamera;
    // Unity 기본 씬 뷰처럼 원점 근처를 비스듬히 내려다보는 초기 시점
    m_Camera->LookAt(XMFLOAT3(7.0f, 5.0f, -19.0f), XMFLOAT3(0.0f, 1.0f, -5.0f), XMFLOAT3(0.0f, 1.0f, 0.0f));
    InitRenderTarget(windowWidth, windowHeight);
    SceneViewManager::GetI()->m_LastActiveSceneEditorWindow = this;
}

SceneEditorWindow::~SceneEditorWindow()
{
    delete m_Camera;
}

void SceneEditorWindow::InitRenderTarget(UINT width, UINT height)
{
    CleanUpRenderTarget(); // ���� ���� Ÿ���� ������ ����

    // ���� Ÿ�� �ؽ�ó ����
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
    if (FAILED(hr)) { /* ���� ó�� */ }

    // ���� Ÿ�� �� ����
    hr = device->CreateRenderTargetView(renderTargetTexture, nullptr, &renderTargetView);
    if (FAILED(hr)) { /* ���� ó�� */ }

    // ���̴� ���ҽ� �� ����
    hr = device->CreateShaderResourceView(renderTargetTexture, nullptr, &shaderResourceView);
    if (FAILED(hr)) { /* ���� ó�� */ }

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
    
    // ���� ���� Ÿ�� ���
    context->OMGetRenderTargets(1, &oldRenderTarget, nullptr);
    
    // �� ���� Ÿ�� ����
    context->OMSetRenderTargets(1, &renderTargetView, nullptr);

    // �� ����
    Application::GetI()->GetApp()->_Editor_OnSceneRender(renderTargetView, m_Camera);

    if (oldRenderTarget == nullptr)
        return;
    
    // ���� ���� Ÿ�� ����
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

    // ���ʿ� ���η� �� �޴� ��ġ
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0)); // â ���� �е� ����
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));   // �׸� �� ���� ����
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));  // ��ư ���� �е� ����
    ImGui::BeginChild("LeftPanel", ImVec2(24, windowSize.y), false, ImGuiWindowFlags_NoDecoration); // �׵θ� ����

    bool isWireframeMode = RenderManager::GetI()->WireFrameMode;
    bool isInstancingMode = RenderManager::GetI()->InstancingMode;

    if (isWireframeMode)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered)); // ���õ� ��ư ����
    }

    if (ImGui::Button("W", ImVec2(24, 24)))
    {
        RenderManager::GetI()->WireFrameMode = !RenderManager::GetI()->WireFrameMode;
    }

    if (isWireframeMode)
    {
        ImGui::PopStyleColor(); // ����� ���� ����
    }

    if (isInstancingMode)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered)); // ���õ� ��ư ����
    }

    if (ImGui::Button("I", ImVec2(24, 25)))
    {
        RenderManager::GetI()->InstancingMode = !RenderManager::GetI()->InstancingMode;
    }

    if (isInstancingMode)
    {
        ImGui::PopStyleColor(); // ����� ���� ����
    }

    // �ٸ� ��ư�� �߰� ����
    ImGui::EndChild();
    ImGui::PopStyleVar(3); // �� ���� ��Ÿ�� ���� ����

    ImGui::SameLine();

    // ������ ���� ����
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

    // ImGui â�� ���� Ÿ�� �ؽ�ó�� ǥ��
    // 하늘 그라디언트(이미지 뒤) → 씬 렌더 이미지 → 그리드/기즈모 오버레이
    ImVec2 imageMin = ImGui::GetCursorScreenPos();
    ImVec2 imageMax(imageMin.x + windowSize.x, imageMin.y + windowSize.y);
    SceneViewOverlay::DrawBackground(imageMin, imageMax, m_Camera);

    ImGui::Image(reinterpret_cast<void*>(shaderResourceView), windowSize);

    SceneViewOverlay::Begin(imageMin, imageMax, m_Camera);
    SceneViewOverlay::DrawGrid();
    SceneManager::GetI()->GetCurrentScene()->RenderSceneGizmos();
    SceneViewOverlay::End();

    ImGui::EndChild(); // MainArea ����
}
