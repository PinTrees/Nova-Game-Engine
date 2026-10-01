#include "pch.h"
#include "SceneEditorWindow.h"
#include "App.h"
#include "EditorCamera.h"
#include "MathHelper.h"
#include "SceneViewOverlay.h"
#include "SceneToolbar.h"
#include "SceneGizmoTools.h"
#include "TerrainEditor.h"
#include "WaterEditor.h"
#include "TerrainSplineEditor.h"
#include "SceneViewState.h"
#include "ParticleSystemEditor.h"

SceneEditorWindow::SceneEditorWindow()
    : EditorWindow("Scene", ICON_FA_BORDER_ALL),
    windowWidth(800),
    windowHeight(600)
{
    m_Camera = new EditorCamera;
    // Unity 기본 씬 뷰처럼 원점 근처를 비스듬히 내려다보는 초기 시점
    m_Camera->LookAt(XMFLOAT3(7.0f, 5.0f, -19.0f), XMFLOAT3(0.0f, 1.0f, -5.0f), XMFLOAT3(0.0f, 1.0f, 0.0f));
    // (개발/검증용) NOVA_SCENE_CAM="x y z tx ty tz" 로 시작 시점 지정
    char cam[128] = {};
    float c[6];
    if (::GetEnvironmentVariableA("NOVA_SCENE_CAM", cam, sizeof(cam)) > 0 && sscanf_s(cam, "%f %f %f %f %f %f", &c[0], &c[1], &c[2], &c[3], &c[4], &c[5]) == 6)
        m_Camera->LookAt(XMFLOAT3(c[0], c[1], c[2]), XMFLOAT3(c[3], c[4], c[5]), XMFLOAT3(0.0f, 1.0f, 0.0f));
    InitRenderTarget(windowWidth, windowHeight);
    SceneViewManager::GetI()->m_LastActiveSceneEditorWindow = this;
}

SceneEditorWindow::~SceneEditorWindow()
{
    delete m_Camera;
}

void SceneEditorWindow::InitRenderTarget(UINT width, UINT height)
{
    // 창이 아직 배치되지 않았거나 숨겨진 프레임: 크기 0 이면 만들지 않는다 (종횡비 0 → 투영 행렬 assert)
    EditorLog::Write("View", "Scene view render target %u x %u%s", width, height, (width == 0 || height == 0) ? " (skipped: zero size)" : "");
    if (width == 0 || height == 0)
        return;
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
    // 시야각/클리핑은 툴바 카메라 드롭다운(Scene Camera 설정)을 따른다
    const SceneToolbar::SceneCameraSettings& cam = SceneToolbar::CameraSettings();
    m_Camera->SetLens(XMConvertToRadians(cam.FieldOfView), aspectRatio, cam.NearClip, cam.FarClip);
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

    // 뷰 전체를 덮는 입력 영역 (클릭이 창 이동/도킹으로 넘어가지 않도록). 팔레트 버튼이 위에 겹칠 수 있게 허용.
    ImGui::SetCursorScreenPos(imageMin);
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("##SceneViewInput", windowSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 mouse = io.MousePos;
    const bool overPalette = mouse.x < imageMin.x + 40.0f && mouse.y < imageMin.y + 170.0f;
    // 선택한 Particle System 의 "Particle Effect" 창 위 클릭은 선택/카메라 조작으로 넘기지 않는다
    ImVec2 effectMin, effectMax;
    const bool overEffect = ParticleSystemEditor::OverlayRect(imageMin, imageMax, effectMin, effectMax) &&
        mouse.x >= effectMin.x && mouse.x <= effectMax.x && mouse.y >= effectMin.y && mouse.y <= effectMax.y;
    const bool viewHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && !overPalette && !overEffect;

    SceneViewOverlay::Begin(imageMin, imageMax, m_Camera);
    // 바닥 격자는 3D 패스(EditorApp::_Editor_OnSceneRender → SceneGrid)에서 깊이 검사하며 그린다
    if (SceneToolbar::GizmosVisible())
        SceneManager::GetI()->GetCurrentScene()->RenderSceneGizmos();
    // Terrain 이 선택되고 Paint Terrain 도구가 켜져 있으면 브러시로 칠한다 (Unity 처럼 이동 핸들/클릭 선택은 쉰다)
    const bool terrainTool = TerrainEditor::SceneGUI(m_Camera, imageMin, imageMax, viewHovered);
    // 물 점 편집 (Edit Points 가 켜진 Water Body)
    const bool waterTool = WaterEditor::SceneGUI(m_Camera, imageMin, imageMax, viewHovered);
    const bool splineTool = TerrainSplineEditor::SceneGUI(m_Camera, imageMin, imageMax, viewHovered);
    SceneViewOverlay::End();
    SceneGizmoTools::SetSuppressed(terrainTool || waterTool || splineTool);

    // 도구 단축키(Q/W/E/R/T/Y) → Move/Rotate/Scale/Rect 핸들, 클릭 선택, Hand/휠/Alt 궤도/F 포커스
    SceneToolbar::HandleShortcuts(viewHovered);
    SceneGizmoTools::Update(m_Camera, imageMin, imageMax, viewHovered);
    // 씬마다 마지막 카메라 시점 저장·복원 (UserSettings/SceneView.json)
    SceneViewState::Update(m_Camera);

    SceneToolbar::DrawToolPalette(imageMin, imageMax);
    ParticleSystemEditor::DrawOverlay(imageMin, imageMax);
}
