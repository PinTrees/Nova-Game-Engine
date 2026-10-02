#include "pch.h"
#include "CustomShaders.h"
#include "EditorApp.h"
#include "Volume.h"
#include "ScriptEngine.h"
#include "UISystem.h"
#include "ParticleRenderer.h"
#include "SpriteBatch.h"
#include "SpriteAnimator.h"
#include "RenderLayers.h"
#include "ParticleSystem.h"
#include "PlayerRuntime.h"
#include "SceneToolbar.h"
#include "EngineInfo.h"
#include "MathHelper.h"
#include "GeometryGenerator.h"
#include "Effects.h"
#include "ShaderCache.h"
#include "Vertex.h"
#include "RenderStates.h"
#include "Sky.h"
#include "ShadowMap.h"
#include "ShadowRenderer.h"
#include "SceneGrid.h"
#include "WaterRenderer.h"
#include "AtmospherePass.h"
#include "CliServer.h"
#include "CliCommands.h"
#include "SceneCulling.h"
#include "FrameProfiler.h"
#include "Profiler.h"
#include "MeshBatcher.h"
#include "DecalRenderer.h"
#include "TreeRenderer.h"
#include "Ssao.h"
#include "EditorCamera.h"
#include "LightManager.h"
#include "Light.h"

EditorApp::EditorApp(HINSTANCE hInstance)
	: App(hInstance)
{
	_mainWindowCaption = ENGINE_NAME_W L" Editor";
	if (!PathManager::GetProjectOverride().empty())
	{
		// "<프로젝트 이름> - NOVA Game Engine Editor"
		std::filesystem::path projectRoot(PathManager::GetProjectOverride());
		if (!projectRoot.has_filename() && projectRoot.has_parent_path())
			projectRoot = projectRoot.parent_path();
		_mainWindowCaption = projectRoot.filename().wstring() + L" - " + _mainWindowCaption;
	}

	_lastMousePos.x = 0;
	_lastMousePos.y = 0;

	//_camera.SetPosition(0.0f, 2.0f, -15.0f);

	_dirLights[0].Ambient = XMFLOAT4(0.6f, 0.6f, 0.6f, 1.0f);
	_dirLights[0].Diffuse = XMFLOAT4(0.8f, 0.7f, 0.7f, 1.0f);
	_dirLights[0].Specular = XMFLOAT4(0.6f, 0.6f, 0.7f, 1.0f);
	_dirLights[0].Direction = XMFLOAT3(-0.57735f, -0.57735f, 0.57735f);

	_dirLights[1].Ambient = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
	_dirLights[1].Diffuse = XMFLOAT4(0.4f, 0.4f, 0.4f, 1.0f);
	_dirLights[1].Specular = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
	_dirLights[1].Direction = XMFLOAT3(0.707f, -0.707f, 0.0f);

	_dirLights[2].Ambient = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
	_dirLights[2].Diffuse = XMFLOAT4(0.3f, 0.3f, 0.3f, 1.0f);
	_dirLights[2].Specular = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
	_dirLights[2].Direction = XMFLOAT3(0.0f, 0.0, -1.0f);

	_originalLightDir[0] = _dirLights[0].Direction;
	_originalLightDir[1] = _dirLights[1].Direction;
	_originalLightDir[2] = _dirLights[2].Direction;
}

EditorApp::~EditorApp()
{
	Effects::DestroyAll();
	InputLayouts::DestroyAll();
	RenderStates::DestroyAll();
}

bool EditorApp::Init()
{
	std::ofstream log("run_log.txt", std::ios::app);
	log << "EditorApp::Init -> App::Init..." << std::endl; log.flush();
	if (!Application::IsPlayer())   // 빌드된 게임에는 에디터 로딩 창을 띄우지 않는다
		LoadingScreen::Begin(L"Opening project...");
	if (!App::Init())
	{
		LoadingScreen::End();
		return false;
	}

	log << "EditorApp::Init -> Effects::InitAll..." << std::endl; log.flush();
	{
		// Effects::InitAll + Shaders::InitAll 이 쓰는 파일 (여기 없는 파일도 동작은 한다 - 그때 따로 컴파일)
		static const std::vector<std::wstring> kShaderFiles = {
			L"../Shaders/28. Basic.fx", L"../Shaders/12. TreeSprite.fx", L"../Shaders/13. VecAdd.fx", L"../Shaders/14. Blur.fx",
			L"../Shaders/15. Tessellation.fx", L"../Shaders/16. TriTessellation.fx", L"../Shaders/17. BezierTessellation.fx",
			L"../Shaders/32. InstancedBasic.fx", L"../Shaders/21. Sky.fx", L"../Shaders/23. NormalMap.fx", L"../Shaders/23. DisplacementMap.fx",
			L"../Shaders/24. Terrain.fx", L"../Shaders/25. Fire.fx", L"../Shaders/25. Rain.fx", L"../Shaders/26. BuildShadowMap.fx",
			L"../Shaders/26. DebugTexture.fx", L"../Shaders/27. AmbientOcclusion.fx", L"../Shaders/28. SsaoNormalDepth.fx",
			L"../Shaders/28. Ssao.fx", L"../Shaders/28. SsaoBlur.fx", L"../Shaders/31. NormalMapSkinned.fx",
			L"../Shaders/41. PostProcess.fx", L"../Shaders/42. UI.fx", L"../Shaders/43. Particle.fx", L"../Shaders/45. SceneGrid.fx", L"../Shaders/46. Water.fx", L"../Shaders/51. Sprite.fx" };
		LoadingScreen::BeginShaderPhase(0.22f, 0.85f, (int)kShaderFiles.size());
		ShaderCache::PrecompileParallel(kShaderFiles, ShaderCache::DefaultFlags());
	}
	LoadingScreen::BeginShaderPhase(0.85f, 0.90f, 22);   // Effects 20 + Shaders 2 (캐시에서 읽기)
	Effects::InitAll(_device, L"../Shaders/28. Basic.fx");
	log << "EditorApp::Init -> Shaders::InitAll..." << std::endl; log.flush();
	Shaders::InitAll(_device);
	log << "EditorApp::Init -> InputLayouts::InitAll..." << std::endl; log.flush();
	InputLayouts::InitAll(_device);
	log << "EditorApp::Init -> RenderStates::InitAll..." << std::endl; log.flush();
	RenderStates::InitAll(_device);

	log << "EditorApp::Init -> _texMgr.Init..." << std::endl; log.flush();
	LoadingScreen::SetProgress(0.92f, L"Loading textures");
	_texMgr.Init(_device);

	log << "EditorApp::Init -> _sky..." << std::endl; log.flush();
	// 기본 하늘: Poly Haven "Kloofendal 48d Partly Cloudy (Pure Sky)" (CC0) 을 Tools/hdri_to_cubemap.py 로 변환한 큐브맵.
	// 스카이박스 배경 + 반사(gCubeMap) + 환경광에 함께 쓴다.
	_sky = make_shared<Sky>(_device, L"../Resources/Textures/Skybox/KloofendalPureSky.dds", 5000.0f);
	//_smap = make_shared<ShadowMap>(_device, SMapSize, SMapSize);

	//_camera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
	//_ssao = make_shared<class Ssao>(_device, _deviceContext, _clientWidth, _clientHeight, _camera.GetFovY(), _camera.GetFarZ());

	log << "EditorApp::Init -> BuildScreenQuadGeometryBuffers..." << std::endl; log.flush();
	LoadingScreen::SetProgress(0.95f, L"Loading scripts");
	ScriptEngine::Init();   // .NET 런타임 + Assembly-CSharp (바뀌었으면 백그라운드 컴파일 시작)
	SpriteAnimClips::RegisterEditorAssetType();   // Project 창의 .spriteanim (프레임 애니메이션)
	LoadingScreen::SetProgress(0.96f, L"Preparing editor windows");
	BuildScreenQuadGeometryBuffers();

	//
	// Compute scene bounding box.
	//
	Scene* scene = SceneManager::GetI()->GetCurrentScene();

	XMFLOAT3 minPt(+MathHelper::Infinity, +MathHelper::Infinity, +MathHelper::Infinity);
	XMFLOAT3 maxPt(-MathHelper::Infinity, -MathHelper::Infinity, -MathHelper::Infinity);

	bool IsMesh = false;
	for (uint32 i = 0; i < scene->GetAllGameObjects().size(); ++i)
	{
		MeshRenderer* meshRenderer = scene->GetAllGameObjects()[i]->GetComponent<MeshRenderer>();
		if (meshRenderer == nullptr)
			continue;

		IsMesh = true; // CreateScene에서 Camera, Light를 추가하므로 MeshRenderer가 없을 시에는 인피니티에서 임의의 값으로 바꿔줘야함

		Transform* transform = meshRenderer->GetGameObject()->GetTransform();

		auto ssss = transform->GetWorldMatrix();
		XMMATRIX world = XMLoadFloat4x4(&ssss);

		if (meshRenderer->GetMesh() != nullptr)
		{
			for (uint32 j = 0; j < meshRenderer->GetMesh()->Vertices.size(); ++j)
			{
				XMFLOAT3 localPos = meshRenderer->GetMesh()->Vertices[j].pos;
				XMVECTOR localPosVec = XMLoadFloat3(&localPos);
				XMVECTOR worldPosVec = XMVector3TransformCoord(localPosVec, world);

				XMFLOAT3 worldPos;
				XMStoreFloat3(&worldPos, worldPosVec);

				minPt.x = MathHelper::Min(minPt.x, worldPos.x);
				minPt.y = MathHelper::Min(minPt.y, worldPos.y);
				minPt.z = MathHelper::Min(minPt.z, worldPos.z);

				maxPt.x = MathHelper::Max(maxPt.x, worldPos.x);
				maxPt.y = MathHelper::Max(maxPt.y, worldPos.y);
				maxPt.z = MathHelper::Max(maxPt.z, worldPos.z);
			}
		}
	}
	
	if ((!(scene->GetAllGameObjects().size() > 0)) || (!IsMesh))
	{
		minPt = XMFLOAT3(0, 0, 0);
		maxPt = XMFLOAT3(20, 20, 20);
	}

	//
	// Derive scene bounding sphere from bounding box.
	//
	_sceneBounds.Center = XMFLOAT3(
		0.5f * (minPt.x + maxPt.x),
		0.5f * (minPt.y + maxPt.y),
		0.5f * (minPt.z + maxPt.z));
	
	XMFLOAT3 extent(
		0.5f * (maxPt.x - minPt.x),
		0.5f * (maxPt.y - minPt.y),
		0.5f * (maxPt.z - minPt.z));
	
	_sceneBounds.Radius = sqrtf(extent.x * extent.x + extent.y * extent.y + extent.z * extent.z);

	if (Application::IsPlayer())
		PlayerRuntime::Start();   // 빌드된 게임: 첫 씬을 바로 Play
	else
	{
		// NOVA CLI: 터미널·AI 가 이 에디터를 다룰 수 있게 (nova.exe → 이름 있는 파이프)
		CliCommands::RegisterAll();
		CliServer::Start();
	}

	return true;
}

void EditorApp::OnResize()
{
	App::OnResize();

	//_camera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);

	//if (_ssao)
	//{
	//	_ssao->OnSize(_clientWidth, _clientHeight, _camera.GetFovY(), _camera.GetFarZ());
	//}
}

void EditorApp::UpdateScene(float dt)
{
	//_camera.UpdateViewMatrix();
}

void EditorApp::RenderApplication()
{

}

GfxDepthStencilView* EditorApp::ViewDepth(UINT width, UINT height)
{
	// 필요한 크기보다 작을 때만 다시 만든다 (커지기만 함). 창 백버퍼 크기 이상으로 유지
	width = (std::max)(width, (UINT)_clientWidth);
	height = (std::max)(height, (UINT)_clientHeight);
	if ((_viewDepthView == nullptr && _viewDepthRetry.Ready()) || width > _viewDepthW || height > _viewDepthH)
	{
		_viewDepthW = (std::max)(width, _viewDepthW);
		_viewDepthH = (std::max)(height, _viewDepthH);
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = _viewDepthW;
		desc.Height = _viewDepthH;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R24G8_TYPELESS;   // 깊이 + SRV (물이 장면 깊이를 읽는다)
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
		_viewDepthView.Reset();
		_viewDepthReadOnly.Reset();
		_viewDepthSRV.Reset();
		_viewDepthTex.Reset();
		if (SUCCEEDED(_device->CreateTexture2D(&desc, nullptr, _viewDepthTex.GetAddressOf())))
		{
			D3D11_DEPTH_STENCIL_VIEW_DESC dsv = {};
			dsv.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
			dsv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
			_device->CreateDepthStencilView(_viewDepthTex.Get(), &dsv, _viewDepthView.GetAddressOf());
			dsv.Flags = D3D11_DSV_READ_ONLY_DEPTH | D3D11_DSV_READ_ONLY_STENCIL;
			_device->CreateDepthStencilView(_viewDepthTex.Get(), &dsv, _viewDepthReadOnly.GetAddressOf());
			D3D11_SHADER_RESOURCE_VIEW_DESC srv = {};
			srv.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
			srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srv.Texture2D.MipLevels = 1;
			_device->CreateShaderResourceView(_viewDepthTex.Get(), &srv, _viewDepthSRV.GetAddressOf());
		}
		if (_viewDepthView) _viewDepthRetry.Succeeded(); else _viewDepthRetry.Failed();   // 실패 → 1 초 뒤 다시
		EditorLog::Write("View", "view depth buffer %u x %u%s", _viewDepthW, _viewDepthH, _viewDepthView ? "" : " (failed, retry in 1 s)");
	}
	return _viewDepthView ? _viewDepthView.Get() : _depthStencilView.Get();
}

// 물: 뷰 깊이 버퍼(읽기 전용 DSV + SRV)와 첫 방향광·하늘로 WaterRenderer 를 부른다
void EditorApp::DrawWater(CXMMATRIX view, CXMMATRIX proj, const XMFLOAT3& eye, GfxRenderTargetView* target, GfxDepthStencilView* dsv,
	const D3D11_VIEWPORT& viewport, const vector<DirectionalLight>& dirLights, bool skyVisible, ShadowMap* shadowMap, const void* shadowFrame,
	const void* atmosphere)
{
	if (dsv != _viewDepthView.Get() || !_viewDepthSRV || !_viewDepthReadOnly)
		return;
	WaterRenderer::View w;
	w.Context = _deviceContext.Get();
	w.ViewMatrix = view;
	w.Proj = proj;
	w.Eye = eye;
	w.Target = target;
	w.Depth = dsv;
	w.DepthReadOnly = _viewDepthReadOnly.Get();
	w.DepthSRV = _viewDepthSRV.Get();
	w.Viewport = viewport;
	w.Sun = dirLights.empty() ? nullptr : &dirLights[0];
	w.Sky = skyVisible && _sky ? _sky->CubeMapSRV().Get() : nullptr;
	w.Atmosphere = static_cast<const AtmospherePass::Params*>(atmosphere);
	// 해(방향광 0)의 그림자: 물이 지형·나무 그림자를 받는다
	const auto* frame = static_cast<const ShadowRenderer::FrameData*>(shadowFrame);
	if (shadowMap && frame && frame->DirCount > 0 && !dirLights.empty())
	{
		w.SunShadow = shadowMap->DepthMapSRVArray(LightType::Directional)[0];
		for (int c = 0; c < 4; ++c)
		{
			w.SunShadowTransforms[c] = frame->Dir[c];
			w.CascadeSpheres[c] = frame->Spheres[c];
		}
		w.ShadowParams = frame->Params;
		w.SunShadowData = frame->DirData[0];
	}
	WaterRenderer::Draw(w);
	_deviceContext->OMSetRenderTargets(1, &target, dsv);
	_deviceContext->RSSetViewports(1, &viewport);
}

void EditorApp::DrawAtmosphere(const void* params, CXMMATRIX viewProj, const XMFLOAT3& eye, GfxRenderTargetView* target, GfxDepthStencilView* dsv,
	const D3D11_VIEWPORT& viewport, bool skyVisible)
{
	const auto& p = *static_cast<const AtmospherePass::Params*>(params);
	if (!p.Active() || dsv != _viewDepthView.Get() || !_viewDepthSRV)
		return;
	AtmospherePass::Draw(_deviceContext.Get(), p, target, dsv, _viewDepthSRV.Get(), viewport, viewProj, eye, skyVisible && _sky ? _sky->CubeMapSRV().Get() : nullptr);
	_deviceContext->RSSetViewports(1, &viewport);
}

// Volume 의 Indirect Lighting → 하늘 환경광·반사 배율 (InstancedBasic 의 ShadeLit)
static XMFLOAT4 ApplyIndirectLighting(const VolumeStack& stack)
{
	XMFLOAT4 v(1.0f, 1.0f, 1.0f, 1.0f);
	if (const VolumeComponent* c = stack.Get("IndirectLighting"))
	{
		const float d = (std::max)(c->F("indirectDiffuse"), 0.0f);
		const float* t = c->V("ambientTint");
		v = XMFLOAT4(d * (std::max)(t[0], 0.0f), d * (std::max)(t[1], 0.0f), d * (std::max)(t[2], 0.0f), (std::max)(c->F("reflection"), 0.0f));
	}
	if (auto* var = Effects::InstancedBasicFX->GetFX()->GetVariableByName("gIndirect")->AsVector(); var && var->IsValid())
		var->SetFloatVector(&v.x);
	return v;
}

// 입자의 Lit · Soft Particles 가 쓰는 장면 정보 (뷰 깊이, 해, 하늘, 환경광 배율)
ParticleRenderer::Environment EditorApp::ParticleEnvironment(GfxDepthStencilView* dsv, const vector<DirectionalLight>& dirLights, const XMFLOAT4& indirect, bool skyVisible)
{
	ParticleRenderer::Environment env;
	if (dsv == _viewDepthView.Get() && _viewDepthReadOnly && _viewDepthSRV)
	{
		env.DepthReadOnly = _viewDepthReadOnly.Get();
		env.DepthSRV = _viewDepthSRV.Get();
	}
	env.Sky = skyVisible && _sky ? _sky->CubeMapSRV().Get() : nullptr;
	env.Indirect = indirect;
	if (!dirLights.empty())
	{
		env.HasSun = true;
		env.SunDirection = dirLights[0].Direction;
		env.SunColor = XMFLOAT3(dirLights[0].Diffuse.x, dirLights[0].Diffuse.y, dirLights[0].Diffuse.z);
	}
	return env;
}

// 화면별 그림자 결과 (그림자 패스 → 받는 쪽 셰이더)
static ShadowRenderer::FrameData s_GameShadow;
static ShadowRenderer::FrameData s_EditorShadow;

void EditorApp::OnSceneRender(GfxRenderTargetView* renderTargetView, Camera* camera)
{
	FRAME_PROFILE("GameView render");
	// Profiler 창: 이 화면의 GPU 시간 + 단계별 (CPU + GPU 타임스탬프)
	PROFILE_GPU("Game View");
	Profiler::Phases phase;
	MeshBatcher::BeginView();    // 렌더러·나무 목록은 화면마다 한 번 모아 모든 패스가 같이 쓴다
	TreeRenderer::BeginView();
	++RenderManager::GetI()->ViewSerial;
	vector<DirectionalLight> dirLights = LightManager::GetI()->GetDirLights();
	vector<PointLight> pointLights = LightManager::GetI()->GetPointLights();
	const int scenePointLights = (int)pointLights.size();      // 그림자는 장면의 Light 컴포넌트만 (입자 빛은 그림자 없음)
	ParticleSystem::CollectLights(pointLights, LIGHT_SIZE);   // Lights 모듈 (남은 점광 칸에)
	vector<SpotLight> spotLights = LightManager::GetI()->GetSpotLights();
	//BuildShadowTransform();

	RenderManager::GetI()->RenderingEditorView = false;
	RenderLayers::SetViewMask(camera->GetCullingMask());   // Camera.cullingMask: 이 화면에 그릴 레이어
	RenderLayers::SetPassMask(~0u);
	RenderManager::GetI()->CameraViewMatrix = camera->View();
	RenderManager::GetI()->CameraProjectionMatrix = camera->Proj();
	RenderManager::GetI()->CameraViewProjectionMatrix = XMMatrixMultiply(camera->View(), camera->Proj());

	// 와이어프레임 제어처럼 전체 그림자맵 제어도 가능하게
	auto shadowMap = RenderManager::GetI()->BaseShadowMap;
	auto viewport = RenderManager::GetI()->Viewport;
	// 뷰(렌더 타깃) 크기의 깊이 버퍼: 창 백버퍼보다 큰 해상도(예: 1080x1920)로 그릴 때도 깊이가 맞도록
	GfxDepthStencilView* viewDsv = ViewDepth((UINT)viewport.Width, (UINT)viewport.Height);
	Effects::BuildShadowMapFX->SetEyePosW(camera->GetPosition());

	// 그림자 맵: 카메라 위치의 Volume 값(Shadows)을 먼저 섞어 캐스케이드/해상도/바이어스를 정한다
	auto& stack = PostProcessingManager::GetI()->GameStack();
	{
		const XMFLOAT3 gameCamPos = camera->GetPosition();
		VolumeManager::Update(stack, Vec3(gameCamPos.x, gameCamPos.y, gameCamPos.z));
		const vector<shared_ptr<Light>> sortedLights = LightManager::GetI()->GetSortedLights();
		phase.Next("Shadows");
		ShadowRenderer::Render(_deviceContext.Get(), *shadowMap, sortedLights, (int)dirLights.size(), (int)spotLights.size(), scenePointLights,
			gameCamPos, camera->View(), camera->Proj(), ShadowRenderer::Settings::FromStack(stack), s_GameShadow,
			[]() {
				// 그림자 조각마다 빛의 절두체로 컬링
				SceneCulling::Cull(RenderManager::GetI()->LightViewProjection, true);
				SceneManager::GetI()->GetCurrentScene()->RenderSceneShadow();
			});
		Profiler::SetStat("Game View/Shadow Cascades Redrawn", s_GameShadow.CascadesDrawn);   // 먼 캐스케이드 캐시
		_deviceContext->RSSetState(0);
		_deviceContext->ClearDepthStencilView(viewDsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
		_deviceContext->RSSetViewports(1, &viewport);
	}

	// PostProcessing - SSAO
	phase.Next("Depth Prepass");
	auto ssao = PostProcessingManager::GetI()->GetSSAO();
	ssao->SetNormalDepthRenderTarget(viewDsv);

	if (RenderManager::GetI()->WireFrameMode)
		_deviceContext->RSSetState(RenderStates::WireframeRS.Get());

	// Draw Scene Objects
	// 카메라 절두체 컬링 (깊이 사전 패스와 본 패스가 같이 쓴다)
	SceneCulling::SetEditorView(false);
	SceneCulling::Cull(camera->View() * camera->Proj(), false);
	SceneManager::GetI()->GetCurrentScene()->RenderSceneShadowNormal();

	_deviceContext->RSSetState(0);

	// PostProcessing - SSAO
	phase.Next("SSAO");
	PostProcessingManager::GetI()->RenderSSAO(camera);

	// Volume 후처리: 필요하면 씬을 HDR 타깃에 그린 뒤 마지막에 뷰 타깃으로 합성한다
	auto& post = PostProcessingManager::GetI()->GamePost();   // stack 은 그림자 패스 앞에서 섞었다
	PostProcessPass::CameraOptions postOptions;
	postOptions.PostProcessing = camera->PostProcessingEnabled();
	postOptions.Fxaa = camera->AntiAliasingMode() != 0;   // SMAA 는 아직 없어 FXAA 로
	postOptions.Dithering = camera->DitheringEnabled();
	postOptions.StopNaNs = camera->StopNaNsEnabled();
	const bool usePost = PostProcessPass::IsNeeded(stack, postOptions);
	GfxRenderTargetView* sceneTarget = usePost ? post.Begin((UINT)viewport.Width, (UINT)viewport.Height) : renderTargetView;

	phase.Next("Opaque");
	GfxRenderTargetView* renderTargets[1] = { sceneTarget };
	_deviceContext->OMSetRenderTargets(1, renderTargets, viewDsv);
	_deviceContext->RSSetViewports(1, &viewport);
	{
		// Game 뷰: 카메라의 Background Type 이 Solid Color 이면 그 색, 아니면 Unity 기본 카메라 배경색(#314D79)
		float gameClear[4] = { 49.0f / 255.0f, 77.0f / 255.0f, 121.0f / 255.0f, 1.0f };
		if (camera && camera->UsesSolidBackground())
			memcpy(gameClear, camera->GetBackgroundColor(), sizeof(gameClear));
		_deviceContext->ClearRenderTargetView(sceneTarget, gameClear);
	}

	_deviceContext->OMSetDepthStencilState(RenderStates::EqualsDSS.Get(), 0);

	float blendFactor[] = { 0.0f, 0.0f, 0.0f, 0.0f };

	// cbPerFrame
	Effects::InstancedBasicFX->SetEyePosW(camera->GetPosition());
	Effects::InstancedBasicFX->SetCubeMap(_sky->CubeMapSRV().Get());
	Effects::InstancedBasicFX->SetSsaoMap(ssao->AmbientSRV().Get());
	const XMFLOAT4 indirect = ApplyIndirectLighting(stack);

	// lights
	Effects::InstancedBasicFX->SetDirLights(dirLights.data(), dirLights.size());
	Effects::InstancedBasicFX->SetSpotLights(spotLights.data(), spotLights.size());
	Effects::InstancedBasicFX->SetPointLights(pointLights.data(), pointLights.size());

	RenderLayers::SetLightMasks(Effects::InstancedBasicFX.get(), scenePointLights, false);   // Light.cullingMask

	// 그림자 맵 / 변환 / 캐스케이드 / 빛별 Strength·필터
	ShadowRenderer::Bind(Effects::InstancedBasicFX.get(), *shadowMap, s_GameShadow);
	// 패키지 셰이더 이펙트 (CustomShaders) 에도 같은 프레임 상수
	CustomShaders::ForEachEffect([&](InstancedBasicEffect* fx) {
		fx->SetEyePosW(camera->GetPosition());
		fx->SetCubeMap(_sky->CubeMapSRV().Get());
		fx->SetSsaoMap(ssao->AmbientSRV().Get());
		if (auto* var = fx->GetFX()->GetVariableByName("gIndirect")->AsVector(); var && var->IsValid())
			var->SetFloatVector(&indirect.x);
		fx->SetDirLights(dirLights.data(), dirLights.size());
		fx->SetSpotLights(spotLights.data(), spotLights.size());
		fx->SetPointLights(pointLights.data(), pointLights.size());
		ShadowRenderer::Bind(fx, *shadowMap, s_GameShadow);
		RenderLayers::SetLightMasks(fx, scenePointLights, false);
	});

	uint32 stride = sizeof(Vertex::PosNormalTexTan);
	uint32 offset = 0;

	_deviceContext->IASetInputLayout(InputLayouts::InstancedBasic.Get());

	SceneManager::GetI()->GetCurrentScene()->RenderScene();

	_deviceContext->RSSetState(0);
	_deviceContext->OMSetDepthStencilState(0, 0);

	// 데칼 (Decal Projector): 불투명 다음 — 깊이 프리패스의 노멀 · 깊이로 표면을 되살려 상자 안을 칠한다 (하늘 · 투명 · 물 · 입자에는 안 묻는다)
	phase.Next("Decals");
	DecalRenderer::Render(_deviceContext.Get(), sceneTarget, viewport, camera->View(), camera->Proj(), ssao->NormalDepthSRV().Get(), false);

	// 입자 (투명): 불투명 물체 다음, 후처리 전 → Bloom 이 Additive 불꽃을 빛나게 한다
	// Background Type = Skybox 면 불투명 물체 다음(빈 곳 깊이 = 1)에 하늘을 그린다. 입자(투명)보다는 먼저.
	if (camera->GetBackgroundType() == 0)
	{
		phase.Next("Sky");
		_sky->Draw(_deviceContext.Get(), camera->GetPosition(), camera->View() * camera->Proj());
		_deviceContext->RSSetState(0);
		_deviceContext->OMSetDepthStencilState(0, 0);
	}

	// 안개·대기 (Volume > Fog / Atmosphere): 불투명 + 하늘 다음, 물 전 (물은 같은 값으로 자기 표면에 입힌다)
	const AtmospherePass::Params atmosphere = AtmospherePass::FromStack(stack, dirLights.empty() ? nullptr : &dirLights[0]);
	if (atmosphere.Active())
	{
		phase.Next("Atmosphere");
		DrawAtmosphere(&atmosphere, camera->View() * camera->Proj(), camera->GetPosition(), sceneTarget, viewDsv, viewport, camera->GetBackgroundType() == 0);
	}

	// 투명 메시 (Shader Graph 의 Surface Type = Transparent): 불투명 · 하늘 · 대기 다음, 물 전 — 먼 것부터, 깊이는 읽기만
	phase.Next("Transparent");
	{
		// 깊이는 읽기만 (읽기 전용 DSV — 깊이 SRV 와 같이 묶여도 된다)
		GfxDepthStencilView* readDsv = (viewDsv == _viewDepthView.Get() && _viewDepthReadOnly) ? _viewDepthReadOnly.Get() : viewDsv;
		_deviceContext->OMSetRenderTargets(1, &sceneTarget, readDsv);
	}
	_deviceContext->RSSetViewports(1, &viewport);
	MeshBatcher::Draw(SceneManager::GetI()->GetCurrentScene(), MeshBatcher::Pass::Transparent, false);
	_deviceContext->RSSetState(0);

	// 물 (바다·호수·강): 불투명 + 하늘 다음 (굴절에 화면 색을 쓴다), 입자 전
	phase.Next("Water");
	DrawWater(camera->View(), camera->Proj(), camera->GetPosition(), sceneTarget, viewDsv, viewport, dirLights, camera->GetBackgroundType() == 0, shadowMap.get(), &s_GameShadow, &atmosphere);

	// 2D 스프라이트 (SpriteRenderer · 2D 뼈대): 투명 — 하늘 · 물 다음, 입자 전
	phase.Next("Sprites");
	_deviceContext->RSSetViewports(1, &viewport);
	SpriteBatch::Render(camera->View(), camera->Proj(), sceneTarget, viewDsv);

	phase.Next("Particles");
	{
		const ParticleRenderer::Environment env = ParticleEnvironment(viewDsv, dirLights, indirect, camera->GetBackgroundType() == 0);
		ParticleRenderer::Render(camera->View(), camera->Proj(), sceneTarget, viewDsv, &env);
	}
	_deviceContext->RSSetViewports(1, &viewport);

	_deviceContext->RSSetState(0);
	_deviceContext->OMSetDepthStencilState(0, 0);

	GfxShaderResourceView* nullSRV[128] = { 0 };
	_deviceContext->PSSetShaderResources(0, 128, nullSRV);

	if (usePost)
	{
		phase.Next("Post Processing");
		post.Execute(stack, postOptions, renderTargetView);
		// 이후 그리기(있다면)를 위해 원래 타깃과 뷰포트로 되돌린다
		GfxRenderTargetView* outTargets[1] = { renderTargetView };
		_deviceContext->OMSetRenderTargets(1, outTargets, viewDsv);
		_deviceContext->RSSetViewports(1, &viewport);
	}
}

void EditorApp::_Editor_OnSceneRender(GfxRenderTargetView* renderTargetView, EditorCamera* camera)
{
	FRAME_PROFILE("SceneView render");
	// Profiler 창: 이 화면의 GPU 시간 + 단계별 (CPU + GPU 타임스탬프)
	PROFILE_GPU("Scene View");
	Profiler::Phases phase;
	MeshBatcher::BeginView();
	TreeRenderer::BeginView();
	++RenderManager::GetI()->ViewSerial;
	vector<DirectionalLight> dirLights = LightManager::GetI()->GetEditorDirLights();
	vector<PointLight> pointLights = LightManager::GetI()->GetEditorPointLights();
	const int scenePointLights = (int)pointLights.size();      // 그림자는 장면의 Light 컴포넌트만 (입자 빛은 그림자 없음)
	ParticleSystem::CollectLights(pointLights, LIGHT_SIZE);   // Lights 모듈 (남은 점광 칸에)
	vector<SpotLight> spotLights = LightManager::GetI()->GetEditorSpotLights();

	RenderManager::GetI()->RenderingEditorView = true;
	RenderLayers::SetViewMask(~0u);   // Scene 뷰는 모든 레이어
	RenderLayers::SetPassMask(~0u);
	RenderManager::GetI()->EditorCameraViewMatrix = camera->View();
	RenderManager::GetI()->EditorCameraProjectionMatrix = camera->Proj();
	RenderManager::GetI()->EditorCameraViewProjectionMatrix = XMMatrixMultiply(camera->View(), camera->Proj());

	auto shadowMap = RenderManager::GetI()->EditorShadowMap;
	auto viewport = RenderManager::GetI()->EditorViewport;
	GfxDepthStencilView* viewDsv = ViewDepth((UINT)viewport.Width, (UINT)viewport.Height);
	Effects::BuildShadowMapFX->SetEyePosW(camera->GetPosition());

	// 그림자 맵 (Scene 뷰 카메라 기준 캐스케이드)
	auto& stack = PostProcessingManager::GetI()->EditorStack();
	const XMFLOAT3 camPos = camera->GetPosition();
	VolumeManager::Update(stack, Vec3(camPos.x, camPos.y, camPos.z));
	{
		const vector<shared_ptr<Light>> sortedLights = LightManager::GetI()->GetSortedEditorLights();
		phase.Next("Shadows");
		ShadowRenderer::Render(_deviceContext.Get(), *shadowMap, sortedLights, (int)dirLights.size(), (int)spotLights.size(), scenePointLights,
			camPos, camera->View(), camera->Proj(), ShadowRenderer::Settings::FromStack(stack), s_EditorShadow,
			[]() {
				SceneCulling::Cull(RenderManager::GetI()->LightViewProjection, true);
				SceneManager::GetI()->GetCurrentScene()->RenderSceneShadow();
			});
		Profiler::SetStat("Scene View/Shadow Cascades Redrawn", s_EditorShadow.CascadesDrawn);
		_deviceContext->RSSetState(0);
		_deviceContext->ClearDepthStencilView(viewDsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
		_deviceContext->RSSetViewports(1, &viewport);
	}

	// PostProcessing - SSAO
	phase.Next("Depth Prepass");
	auto ssao = PostProcessingManager::GetI()->_EditorGetSSAO();
	ssao->SetNormalDepthRenderTarget(viewDsv);

	if (RenderManager::GetI()->WireFrameMode)
		_deviceContext->RSSetState(RenderStates::WireframeRS.Get());

	// Draw Scene Objects
	SceneCulling::SetEditorView(true);
	SceneCulling::Cull(camera->View() * camera->Proj(), false);
	SceneManager::GetI()->GetCurrentScene()->_Editor_RenderSceneShadowNormal(); 

	_deviceContext->RSSetState(0);

	// PostProcessing - SSAO
	phase.Next("SSAO");
	PostProcessingManager::GetI()->_Editor_RenderSSAO(camera);

	// Volume 후처리 (Scene 뷰: 툴바 Effects > Post Processing 이 켜져 있을 때, 카메라 옵션은 기본)
	auto& post = PostProcessingManager::GetI()->EditorPost();   // stack 은 그림자 패스 앞에서 섞었다
	PostProcessPass::CameraOptions postOptions;
	postOptions.PostProcessing = SceneToolbar::PostProcessingVisible() && !RenderManager::GetI()->WireFrameMode;
	const bool usePost = PostProcessPass::IsNeeded(stack, postOptions);
	GfxRenderTargetView* sceneTarget = usePost ? post.Begin((UINT)viewport.Width, (UINT)viewport.Height) : renderTargetView;

	phase.Next("Opaque");
	GfxRenderTargetView* renderTargets[1] = { sceneTarget };
	_deviceContext->OMSetRenderTargets(1, renderTargets, viewDsv);
	_deviceContext->RSSetViewports(1, &viewport);
	{
		// Scene 뷰: 투명으로 지운 뒤 SceneViewOverlay 가 뒤에 그린 하늘 그라디언트가 비쳐 보이게 한다.
		const float sceneClear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		_deviceContext->ClearRenderTargetView(sceneTarget, sceneClear);
	}

	_deviceContext->OMSetDepthStencilState(RenderStates::EqualsDSS.Get(), 0);

	//float blendFactor[] = { 0.0f, 0.0f, 0.0f, 0.0f };

	// cbPerFrame
	Effects::InstancedBasicFX->SetEyePosW(camera->GetPosition());
	Effects::InstancedBasicFX->SetCubeMap(_sky->CubeMapSRV().Get());
	Effects::InstancedBasicFX->SetSsaoMap(ssao->AmbientSRV().Get());
	const XMFLOAT4 indirect = ApplyIndirectLighting(stack);

	// lights
	Effects::InstancedBasicFX->SetDirLights(dirLights.data(), dirLights.size());
	Effects::InstancedBasicFX->SetSpotLights(spotLights.data(), spotLights.size());
	Effects::InstancedBasicFX->SetPointLights(pointLights.data(), pointLights.size());

	RenderLayers::SetLightMasks(Effects::InstancedBasicFX.get(), scenePointLights, true);

	// 그림자 맵 / 변환 / 캐스케이드 / 빛별 Strength·필터
	ShadowRenderer::Bind(Effects::InstancedBasicFX.get(), *shadowMap, s_EditorShadow);
	// 패키지 셰이더 이펙트 (CustomShaders) 에도 같은 프레임 상수
	CustomShaders::ForEachEffect([&](InstancedBasicEffect* fx) {
		fx->SetEyePosW(camera->GetPosition());
		fx->SetCubeMap(_sky->CubeMapSRV().Get());
		fx->SetSsaoMap(ssao->AmbientSRV().Get());
		if (auto* var = fx->GetFX()->GetVariableByName("gIndirect")->AsVector(); var && var->IsValid())
			var->SetFloatVector(&indirect.x);
		fx->SetDirLights(dirLights.data(), dirLights.size());
		fx->SetSpotLights(spotLights.data(), spotLights.size());
		fx->SetPointLights(pointLights.data(), pointLights.size());
		ShadowRenderer::Bind(fx, *shadowMap, s_EditorShadow);
		RenderLayers::SetLightMasks(fx, scenePointLights, true);
	});

	uint32 stride = sizeof(Vertex::PosNormalTexTan);
	uint32 offset = 0;

	_deviceContext->IASetInputLayout(InputLayouts::InstancedBasic.Get());

	if (RenderManager::GetI()->WireFrameMode)
		_deviceContext->RSSetState(RenderStates::WireframeRS.Get());

	SceneManager::GetI()->GetCurrentScene()->_Editor_RenderScene();

	_deviceContext->RSSetState(0);
	_deviceContext->OMSetDepthStencilState(0, 0);

	// 데칼 (Game 뷰와 같은 자리)
	phase.Next("Decals");
	if (!RenderManager::GetI()->WireFrameMode)
		DecalRenderer::Render(_deviceContext.Get(), sceneTarget, viewport, camera->View(), camera->Proj(), ssao->NormalDepthSRV().Get(), true);

	// 입자 (Scene 뷰: 선택한 시스템의 미리보기 포함)
	// 스카이박스 (툴바 Effects > Skybox). 꺼져 있으면 SceneViewOverlay 의 그라디언트가 비친다.
	if (SceneToolbar::SkyboxVisible() && !RenderManager::GetI()->WireFrameMode)
	{
		phase.Next("Sky");
		_sky->Draw(_deviceContext.Get(), camera->GetPosition(), camera->View() * camera->Proj());
		_deviceContext->RSSetState(0);
		_deviceContext->OMSetDepthStencilState(0, 0);
	}

	// 안개·대기 (Scene 뷰: 툴바 Effects > Fog 가 켜져 있을 때)
	AtmospherePass::Params atmosphere;
	if (SceneToolbar::FogVisible() && !RenderManager::GetI()->WireFrameMode)
		atmosphere = AtmospherePass::FromStack(stack, dirLights.empty() ? nullptr : &dirLights[0]);
	if (atmosphere.Active())
	{
		phase.Next("Atmosphere");
		DrawAtmosphere(&atmosphere, camera->View() * camera->Proj(), camera->GetPosition(), sceneTarget, viewDsv, viewport, SceneToolbar::SkyboxVisible());
	}

	// 투명 메시 (Game 뷰와 같은 자리)
	phase.Next("Transparent");
	{
		// 깊이는 읽기만 (읽기 전용 DSV — 깊이 SRV 와 같이 묶여도 된다)
		GfxDepthStencilView* readDsv = (viewDsv == _viewDepthView.Get() && _viewDepthReadOnly) ? _viewDepthReadOnly.Get() : viewDsv;
		_deviceContext->OMSetRenderTargets(1, &sceneTarget, readDsv);
	}
	_deviceContext->RSSetViewports(1, &viewport);
	MeshBatcher::Draw(SceneManager::GetI()->GetCurrentScene(), MeshBatcher::Pass::Transparent, true);
	_deviceContext->RSSetState(0);

	// 물 (바다·호수·강)
	phase.Next("Water");
	if (!RenderManager::GetI()->WireFrameMode)
		DrawWater(camera->View(), camera->Proj(), camera->GetPosition(), sceneTarget, viewDsv, viewport, dirLights, SceneToolbar::SkyboxVisible(), shadowMap.get(), &s_EditorShadow, &atmosphere);

	// 바닥 격자 (툴바 Grid): 불투명 물체·하늘 다음에 깊이 검사하며 → 물체 뒤의 선은 가려진다
	phase.Next("Grid");
	if (SceneToolbar::GridVisible())
		SceneGrid::Draw(_deviceContext.Get(), camera->View() * camera->Proj(), camera->GetPosition());

	phase.Next("Sprites");
	_deviceContext->RSSetViewports(1, &viewport);
	SpriteBatch::Render(camera->View(), camera->Proj(), sceneTarget, viewDsv);

	phase.Next("Particles");
	if (SceneToolbar::ParticlesVisible())
	{
		const ParticleRenderer::Environment env = ParticleEnvironment(viewDsv, dirLights, indirect, SceneToolbar::SkyboxVisible());
		ParticleRenderer::Render(camera->View(), camera->Proj(), sceneTarget, viewDsv, &env);
	}
	_deviceContext->RSSetViewports(1, &viewport);

	// (디버그) 그림자 맵을 작은 화면으로 표시하던 DrawScreenQuad 는 제거함
	//DrawScreenQuad(ssao->AmbientSRV().Get());
	//DrawScreenQuad(PostProcessingManager::GetI()->_EditorGetSSAO()->GetRandomVectorSRV());

	_deviceContext->RSSetState(0);
	_deviceContext->OMSetDepthStencilState(0, 0);

	GfxShaderResourceView* nullSRV[128] = { 0 };
	_deviceContext->PSSetShaderResources(0, 128, nullSRV);

	if (usePost)
	{
		phase.Next("Post Processing");
		post.Execute(stack, postOptions, renderTargetView);
		GfxRenderTargetView* outTargets[1] = { renderTargetView };
		_deviceContext->OMSetRenderTargets(1, outTargets, viewDsv);
		_deviceContext->RSSetViewports(1, &viewport);
	}

	phase.Next("UI");
	// UI 캔버스: Unity 처럼 월드(1 픽셀 = 1 단위)에 놓인 사각형으로 (씬 깊이로 가려짐)
	{
		GfxRenderTargetView* uiTargets[1] = { renderTargetView };
		_deviceContext->OMSetRenderTargets(1, uiTargets, viewDsv);
		const XMFLOAT3 cp = camera->GetPosition();
		UISystem::RenderSceneView(renderTargetView, (UINT)viewport.Width, (UINT)viewport.Height, camera->View(), camera->Proj(), Vec3(cp.x, cp.y, cp.z));
		_deviceContext->RSSetViewports(1, &viewport);
	}
}

void EditorApp::OnMouseDown(WPARAM btnState, int32 x, int32 y)
{
	_lastMousePos.x = x;
	_lastMousePos.y = y;

	SetCapture(_hMainWnd);
}

void EditorApp::OnMouseUp(WPARAM btnState, int32 x, int32 y)
{
	ReleaseCapture();
}

void EditorApp::OnMouseMove(WPARAM btnState, int32 x, int32 y)
{
	if ((btnState & MK_LBUTTON) != 0)
	{
		// Make each pixel correspond to a quarter of a degree.
		float dx = XMConvertToRadians(0.25f * static_cast<float>(x - _lastMousePos.x));
		float dy = XMConvertToRadians(0.25f * static_cast<float>(y - _lastMousePos.y));

		//_camera.Pitch(dy);
		//_camera.RotateY(dx);
	}

	_lastMousePos.x = x;
	_lastMousePos.y = y;
}

void EditorApp::DrawSceneToSsaoNormalDepthMap()
{
	//XMMATRIX view = _camera.View();
	//XMMATRIX proj = _camera.Proj();
	//XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	//
	//RenderManager::GetI()->cameraViewMatrix = _camera.View();
	//RenderManager::GetI()->cameraProjectionMatrix = _camera.Proj();
	//RenderManager::GetI()->cameraViewProjectionMatrix = XMMatrixMultiply(view, proj);
	//
	//ComPtr<FxTechnique> tech = Effects::SsaoNormalDepthFX->NormalDepthTech;
	//ComPtr<FxTechnique> alphaClippedTech = Effects::SsaoNormalDepthFX->NormalDepthAlphaClipTech;
	//
	//XMMATRIX world;
	//XMMATRIX worldInvTranspose;
	//XMMATRIX worldView;
	//XMMATRIX worldInvTransposeView;
	//XMMATRIX worldViewProj;
	//
	//_deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	//_deviceContext->IASetInputLayout(InputLayouts::PosNormalTexTan.Get());
	//
	//// Draw Scene Objects
	//SceneManager::GetI()->GetCurrentScene()->RenderSceneShadowNormal();
	//
	//if (GetAsyncKeyState('1') & 0x8000)
	//	_deviceContext->RSSetState(RenderStates::WireframeRS.Get());
	//
	//D3DX11_TECHNIQUE_DESC techDesc;
	//tech->GetDesc(&techDesc);
	//for (uint32 p = 0; p < techDesc.Passes; ++p)
	//{
	//	for (uint32 modelIndex = 0; modelIndex < _modelInstances.size(); ++modelIndex)
	//	{
	//		world = XMLoadFloat4x4(&_modelInstances[modelIndex].World);
	//		worldInvTranspose = MathHelper::InverseTranspose(world);
	//		worldView = world * view;
	//		worldInvTransposeView = worldInvTranspose * view;
	//		worldViewProj = world * view * proj;
	//
	//		Effects::SsaoNormalDepthFX->SetWorldView(worldView);
	//		Effects::SsaoNormalDepthFX->SetWorldInvTransposeView(worldInvTransposeView);
	//		Effects::SsaoNormalDepthFX->SetWorldViewProj(worldViewProj);
	//		Effects::SsaoNormalDepthFX->SetTexTransform(XMMatrixScaling(1.0f, 1.0f, 1.0f));
	//
	//		tech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());
	//		for (uint32 subset = 0; subset < _modelInstances[modelIndex].Model->SubsetCount; ++subset)
	//		{
	//			_modelInstances[modelIndex].Model->ModelMesh.Draw(_deviceContext, subset);
	//		}
	//	}
	//}
	//
	//// The alpha tested triangles are leaves, so render them double sided.
	//_deviceContext->RSSetState(RenderStates::NoCullRS.Get());
	//alphaClippedTech->GetDesc(&techDesc);
	//
	//for (uint32 p = 0; p < techDesc.Passes; ++p)
	//{
	//	for (uint32 modelIndex = 0; modelIndex < _alphaClippedModelInstances.size(); ++modelIndex)
	//	{
	//		world = XMLoadFloat4x4(&_alphaClippedModelInstances[modelIndex].World);
	//		worldInvTranspose = MathHelper::InverseTranspose(world);
	//		worldView = world * view;
	//		worldInvTransposeView = worldInvTranspose * view;
	//		worldViewProj = world * view * proj;
	//
	//		Effects::SsaoNormalDepthFX->SetWorldView(worldView);
	//		Effects::SsaoNormalDepthFX->SetWorldInvTransposeView(worldInvTransposeView);
	//		Effects::SsaoNormalDepthFX->SetWorldViewProj(worldViewProj);
	//		Effects::SsaoNormalDepthFX->SetTexTransform(XMMatrixScaling(1.0f, 1.0f, 1.0f));
	//
	//		for (uint32 subset = 0; subset < _alphaClippedModelInstances[modelIndex].Model->SubsetCount; ++subset)
	//		{
	//			Effects::SsaoNormalDepthFX->SetDiffuseMap(_alphaClippedModelInstances[modelIndex].Model->DiffuseMapSRV[subset].Get());
	//			alphaClippedTech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());
	//			_alphaClippedModelInstances[modelIndex].Model->ModelMesh.Draw(_deviceContext, subset);
	//		}
	//	}
	//}
	//
	//_deviceContext->RSSetState(0);
}

void EditorApp::DrawSceneToShadowMap()
{
	//XMMATRIX view = XMLoadFloat4x4(&_lightView);
	//XMMATRIX proj = XMLoadFloat4x4(&_lightProj);
	//XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	//
	//RenderManager::GetI()->directinalLightViewProjection = XMMatrixMultiply(view, proj);
	//
	//Effects::BuildShadowMapFX->SetEyePosW(_camera.GetPosition());
	//Effects::BuildShadowMapFX->SetViewProj(viewProj);
	//
	//// Draw Scene Objects
	//SceneManager::GetI()->GetCurrentScene()->RenderSceneShadow();
	//
	//ComPtr<FxTechnique> tech = Effects::BuildShadowMapFX->BuildShadowMapTech;
	//ComPtr<FxTechnique> alphaClippedTech = Effects::BuildShadowMapFX->BuildShadowMapAlphaClipTech;
	//
	//_deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	//
	//XMMATRIX world;
	//XMMATRIX worldInvTranspose;
	//XMMATRIX worldViewProj;
	//
	//_deviceContext->IASetInputLayout(InputLayouts::PosNormalTexTan.Get());
	//
	//if (GetAsyncKeyState('1') & 0x8000)
	//	_deviceContext->RSSetState(RenderStates::WireframeRS.Get());
	//
	//D3DX11_TECHNIQUE_DESC techDesc;
	//tech->GetDesc(&techDesc);
	//
	//for (uint32 p = 0; p < techDesc.Passes; ++p)
	//{
	//	for (uint32 modelIndex = 0; modelIndex < _modelInstances.size(); ++modelIndex)
	//	{
	//		world = XMLoadFloat4x4(&_modelInstances[modelIndex].World);
	//		worldInvTranspose = MathHelper::InverseTranspose(world);
	//		worldViewProj = world * view * proj;
	//
	//		Effects::BuildShadowMapFX->SetWorld(world);
	//		Effects::BuildShadowMapFX->SetWorldInvTranspose(worldInvTranspose);
	//		Effects::BuildShadowMapFX->SetWorldViewProj(worldViewProj);
	//		Effects::BuildShadowMapFX->SetTexTransform(::XMMatrixScaling(1.0f, 1.0f, 1.0f));
	//
	//		tech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());
	//
	//		for (uint32 subset = 0; subset < _modelInstances[modelIndex].Model->SubsetCount; ++subset)
	//		{
	//			_modelInstances[modelIndex].Model->ModelMesh.Draw(_deviceContext, subset);
	//		}
	//	}
	//}
	//
	//alphaClippedTech->GetDesc(&techDesc);
	//for (uint32 p = 0; p < techDesc.Passes; ++p)
	//{
	//	for (uint32 modelIndex = 0; modelIndex < _alphaClippedModelInstances.size(); ++modelIndex)
	//	{
	//		world = XMLoadFloat4x4(&_alphaClippedModelInstances[modelIndex].World);
	//		worldInvTranspose = MathHelper::InverseTranspose(world);
	//		worldViewProj = world * view * proj;
	//
	//		Effects::BuildShadowMapFX->SetWorld(world);
	//		Effects::BuildShadowMapFX->SetWorldInvTranspose(worldInvTranspose);
	//		Effects::BuildShadowMapFX->SetWorldViewProj(worldViewProj);
	//		Effects::BuildShadowMapFX->SetTexTransform(XMMatrixScaling(1.0f, 1.0f, 1.0f));
	//
	//		for (uint32 subset = 0; subset < _alphaClippedModelInstances[modelIndex].Model->SubsetCount; ++subset)
	//		{
	//			Effects::BuildShadowMapFX->SetDiffuseMap(_alphaClippedModelInstances[modelIndex].Model->DiffuseMapSRV[subset].Get());
	//			alphaClippedTech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());
	//			_alphaClippedModelInstances[modelIndex].Model->ModelMesh.Draw(_deviceContext, subset);
	//		}
	//	}
	//}
	//
	//_deviceContext->RSSetState(0);
}

void EditorApp::DrawScreenQuad(ComPtr<GfxShaderResourceView> srv)
{
	uint32 stride = sizeof(Vertex::Basic32);
	uint32 offset = 0;

	_deviceContext->IASetInputLayout(InputLayouts::Basic32.Get());
	_deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	_deviceContext->IASetVertexBuffers(0, 1, _screenQuadVB.GetAddressOf(), &stride, &offset);
	_deviceContext->IASetIndexBuffer(_screenQuadIB.Get(), DXGI_FORMAT_R32_UINT, 0);

	// Scale and shift quad to lower-right corner.
	XMMATRIX world(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, -0.5f, 0.0f, 1.0f);

	ComPtr<FxTechnique> tech = Effects::DebugTexFX->ViewRedTech;
	D3DX11_TECHNIQUE_DESC techDesc;

	tech->GetDesc(&techDesc);
	for (uint32 p = 0; p < techDesc.Passes; ++p)
	{
		Effects::DebugTexFX->SetWorldViewProj(world);
		Effects::DebugTexFX->SetTexture(srv.Get());

		tech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());
		_deviceContext->DrawIndexed(6, 0, 0);
	}
}

void EditorApp::BuildShadowTransform()
{
	// Only the first "main" light casts a shadow.
	XMVECTOR lightDir = ::XMLoadFloat3(&_dirLights[0].Direction);
	XMVECTOR lightPos = -2.0f * _sceneBounds.Radius * lightDir;
	XMVECTOR targetPos = ::XMLoadFloat3(&_sceneBounds.Center);
	XMVECTOR up = ::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	XMMATRIX V = ::XMMatrixLookAtLH(lightPos, targetPos, up);

	// Transform bounding sphere to light space.
	XMFLOAT3 sphereCenterLS;
	::XMStoreFloat3(&sphereCenterLS, XMVector3TransformCoord(targetPos, V));

	// Ortho frustum in light space encloses scene.
	float l = sphereCenterLS.x - _sceneBounds.Radius;
	float b = sphereCenterLS.y - _sceneBounds.Radius;
	float n = sphereCenterLS.z - _sceneBounds.Radius;
	float r = sphereCenterLS.x + _sceneBounds.Radius;
	float t = sphereCenterLS.y + _sceneBounds.Radius;
	float f = sphereCenterLS.z + _sceneBounds.Radius;
	XMMATRIX P = ::XMMatrixOrthographicOffCenterLH(l, r, b, t, n, f);

	// Transform NDC space [-1,+1]^2 to texture space [0,1]^2
	XMMATRIX T(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f);

	XMMATRIX S = V * P * T;

	::XMStoreFloat4x4(&_lightView, V);
	::XMStoreFloat4x4(&_lightProj, P);
	::XMStoreFloat4x4(&_shadowTransform, S);
}

void EditorApp::BuildScreenQuadGeometryBuffers()
{
	GeometryGenerator::MeshData quad;

	GeometryGenerator geoGen;
	geoGen.CreateFullscreenQuad(quad);

	//
	// Extract the vertex elements we are interested in and pack the
	// vertices of all the meshes into one vertex buffer.
	//

	std::vector<Vertex::Basic32> vertices(quad.vertices.size());

	for (uint32 i = 0; i < quad.vertices.size(); ++i)
	{
		vertices[i].pos = quad.vertices[i].position;
		vertices[i].normal = quad.vertices[i].normal;
		vertices[i].tex = quad.vertices[i].texC;
	}

	D3D11_BUFFER_DESC vbd;
	vbd.Usage = D3D11_USAGE_IMMUTABLE;
	vbd.ByteWidth = sizeof(Vertex::Basic32) * quad.vertices.size();
	vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vbd.CPUAccessFlags = 0;
	vbd.MiscFlags = 0;
	D3D11_SUBRESOURCE_DATA vinitData;
	vinitData.pSysMem = &vertices[0];
	HR(_device->CreateBuffer(&vbd, &vinitData, _screenQuadVB.GetAddressOf()));

	//
	// Pack the indices of all the meshes into one index buffer.
	//

	D3D11_BUFFER_DESC ibd;
	ibd.Usage = D3D11_USAGE_IMMUTABLE;
	ibd.ByteWidth = sizeof(uint32) * quad.indices.size();
	ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
	ibd.CPUAccessFlags = 0;
	ibd.MiscFlags = 0;
	D3D11_SUBRESOURCE_DATA iinitData;
	iinitData.pSysMem = &quad.indices[0];
	HR(_device->CreateBuffer(&ibd, &iinitData, _screenQuadIB.GetAddressOf()));
}

