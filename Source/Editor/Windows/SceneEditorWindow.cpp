#include "pch.h"
#include "SceneEditorWindow.h"
#include "App.h"
#include "EditorCamera.h"
#include "MathHelper.h"
#include "SceneViewOverlay.h"
#include "SceneToolbar.h"

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

    // 상단 툴바: Pivot/Local, 스냅, 드로우 모드, 2D, 라이팅, 이펙트, 그리드, 카메라, 기즈모 (Unity Scene 뷰와 동일한 구성)
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    SceneToolbar::DrawTopBar(windowSize.x, m_Camera);
    ImGui::PopStyleVar();

    windowSize = ImGui::GetContentRegionAvail();
    if (windowSize.x < 1.0f || windowSize.y < 1.0f)
        return;

    if (windowWidth != windowSize.x || windowHeight != windowSize.y)
    {
        windowWidth = static_cast<UINT>(windowSize.x);
        windowHeight = static_cast<UINT>(windowSize.y);
        InitRenderTarget(windowWidth, windowHeight);
    }

    RenderScene();

    // 하늘 그라디언트(이미지 뒤) → 씬 렌더 이미지 → 그리드/기즈모 오버레이 → 도구 팔레트
    ImVec2 imageMin = ImGui::GetCursorScreenPos();
    ImVec2 imageMax(imageMin.x + windowSize.x, imageMin.y + windowSize.y);
    SceneViewOverlay::DrawBackground(imageMin, imageMax, m_Camera);

    ImGui::Image(reinterpret_cast<void*>(shaderResourceView), windowSize);
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 mouse = io.MousePos;
    const bool overPalette = mouse.x < imageMin.x + 40.0f && mouse.y < imageMin.y + 180.0f;
    const bool viewHovered = ImGui::IsItemHovered() && !overPalette;

    SceneViewOverlay::Begin(imageMin, imageMax, m_Camera);
    if (SceneToolbar::GridVisible())
        SceneViewOverlay::DrawGrid();
    if (SceneToolbar::GizmosVisible())
        SceneManager::GetI()->GetCurrentScene()->RenderSceneGizmos();
    SceneViewOverlay::End();

    // 도구 단축키(Q/W/E/R/T/Y), Hand 도구 이동(좌클릭 드래그), 마우스 휠 줌
    SceneToolbar::HandleShortcuts(viewHovered);
    if (viewHovered && !ImGui::IsMouseDown(ImGuiMouseButton_Right))
    {
        if (SceneToolbar::CurrentTool() == SceneToolbar::Tool::View && ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            m_Camera->Strafe(-io.MouseDelta.x * 0.03f);
            m_Camera->Pedestal(io.MouseDelta.y * 0.03f);
        }
        if (io.MouseWheel != 0.0f)
            m_Camera->Walk(io.MouseWheel * 2.0f);
    }

    SceneToolbar::DrawToolPalette(imageMin, imageMax);
}
