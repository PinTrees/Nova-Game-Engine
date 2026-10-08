#include "pch.h"
#include "CustomShaders.h"
#include "EditorApp.h"
#include "Volume.h"
#include "ScriptEngine.h"
#include "UISystem.h"
#include "ParticleRenderer.h"
#include "VfxRuntime.h"
#include "VirtualTexturing.h"
#include "RenderPipelineSettings.h"
#include "JobSystem.h"
#include "DeferredRenderer.h"
#include "WeatherState.h"
#include "WeatherCover.h"
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
#include "ReflectionProbes.h"
#include "ProbeVolumes.h"
#include "RenderingDebug.h"
#include "ClusteredLighting.h"
#include "RenderGraph.h"
#include "LODGroup.h"
#include "ScreenSpaceReflection.h"
#include "ModelPlacement.h"
#include "VulkanTools.h"
#include "D3D12Tools.h"
#include "AndroidTools.h"
#include "WebTools.h"
#include "TreeRenderer.h"
#include "Ssao.h"
#include "MotionVectors.h"
#include "OcclusionCulling.h"
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
			L"../Shaders/41. PostProcess.fx", L"../Shaders/42. UI.fx", L"../Shaders/43. Particle.fx", L"../Shaders/45. SceneGrid.fx", L"../Shaders/46. Water.fx", L"../Shaders/51. Sprite.fx",
			L"../Shaders/58. VFX.fx" };   // Visual Effect: 커서 (블록 · 연산 노드 해석) 처음 컴파일이 몇 초 — 로딩 창에서 함께 (그리기 중에 멈추지 않게)
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

	// Reflection Probe 의 한 면 = Game 뷰 그리기
	ReflectionProbes::SetCapture([this](const ReflectionProbes::CaptureView& v) { CaptureProbeFace(v); });
	ProbeVolumes::SetCapture([this](const ProbeVolumes::CaptureView& v) { return CaptureGIView(v); });
	if (Application::IsPlayer())
		PlayerRuntime::Start();   // 빌드된 게임: 첫 씬을 바로 Play
	else
	{
		ReflectionProbes::RegisterEditor();   // nova probe
		ProbeVolumes::RegisterEditor();       // nova probevolume
		RenderingDebug::RegisterEditor();     // nova debugview (Window > Analysis > Rendering Debugger)
		ClusteredLighting::RegisterEditor();  // nova forwardplus
		RenderGraph::RegisterEditor();        // nova rendergraph (Window > Analysis > Render Graph Viewer)
		VirtualTexturing::RegisterEditor();   // nova vt
		RenderPipelineSettings::RegisterEditor();   // nova renderpath (Rendering Path — Forward · Forward+ · Deferred)
		Jobs::RegisterEditor();   // nova jobs (Job System — info · test · bench · set)
		LODGroup::RegisterEditor();           // nova lod
		OcclusionCulling::RegisterEditor();   // nova occlusion
		ModelPlacement::RegisterEditor();     // nova modelfile
		VulkanTools::RegisterEditor();        // nova vulkan
		D3D12Tools::RegisterEditor();         // nova d3d12
		AndroidTools::RegisterEditor();       // nova android
		WebTools::RegisterEditor();           // nova web
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
	// Adaptive Probe Volume (실시간 간접광) → Reflection Probe (굽기 요청 · 실시간 찍기): Game · Scene 뷰를 그리기 전, 프레임마다 한 번
	//  간접광이 먼저 — 다시 비추기가 마지막 뷰의 그림자 맵을 쓰는데, 프로브 찍기가 그 맵을 덮어쓴다
	ProbeVolumes::Update();
	RenderGraph::TrimPool();   // 오래 쓰이지 않은 임시 텍스처를 놓는다 (프레임마다 한 번)
	VirtualTexturing::Update();   // 가상 텍스처: 피드백 읽기 → 페이지 요청 · 올리기 · 페이지 표 (프레임마다 한 번)
	ReflectionProbes::Update();
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
		w.SunShadow = shadowMap->DepthMapSRV(LightType::Directional);   // 방향광 0 = 조각 0..3
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
//  Adaptive Probe Volume 빛은 Volume 배율만 (gIndirectGI) — 날씨 · 낮밤은 프로브가 모은 하늘에 이미 들어 있다
static XMFLOAT4 s_IndirectGI(1.0f, 1.0f, 1.0f, 1.0f);
static void SetIndirectGI(FxEffect* fx)
{
	if (auto* var = fx->GetVariableByName("gIndirectGI")->AsVector(); var && var->IsValid())
		var->SetFloatVector(&s_IndirectGI.x);
}
// Screen Space Ambient Occlusion 의 Direct Lighting Strength (꺼져 있으면 0) → ShadeLit
static XMFLOAT4 s_SsaoParams(0.0f, 0.0f, 0.0f, 0.0f);
static void SetSsaoParams(FxEffect* fx)
{
	if (auto* var = fx->GetVariableByName("gSsaoParams")->AsVector(); var && var->IsValid())
		var->SetFloatVector(&s_SsaoParams.x);
}
static void UseSsaoSettings(const Ssao::Settings& s)
{
	s_SsaoParams = XMFLOAT4(s.Active() ? s.DirectLightingStrength : 0.0f, 0.0f, 0.0f, 0.0f);
	SetSsaoParams(Effects::InstancedBasicFX->GetFX());
}
static XMFLOAT4 ApplyIndirectLighting(const VolumeStack& stack)
{
	XMFLOAT4 v(1.0f, 1.0f, 1.0f, 1.0f);
	if (const VolumeComponent* c = stack.Get("IndirectLighting"))
	{
		const float d = (std::max)(c->F("indirectDiffuse"), 0.0f);
		const float* t = c->V("ambientTint");
		v = XMFLOAT4(d * (std::max)(t[0], 0.0f), d * (std::max)(t[1], 0.0f), d * (std::max)(t[2], 0.0f), (std::max)(c->F("reflection"), 0.0f));
	}
	s_IndirectGI = v;
	SetIndirectGI(Effects::InstancedBasicFX->GetFX());
	WeatherState::Get().ApplyAmbient(v);   // 날씨 (흐림 · 번개) — 기본값이면 그대로
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
// 입자 빛 (Particle System 의 Lights 모듈): 남은 점광 칸 (LIGHT_SIZE) 에, 다 차면 Forward+ 클러스터로 (그림자 없음)
static void AddParticleLights(vector<PointLight>& pointLights, std::vector<AdditionalLight>& additional)
{
	std::vector<PointLight> particles;
	ParticleSystem::CollectLights(particles, kMaxAdditionalLights);
	for (const PointLight& p : particles)
	{
		if ((int)pointLights.size() < LIGHT_SIZE)
		{
			pointLights.push_back(p);
			continue;
		}
		if ((int)additional.size() >= kMaxAdditionalLights)
			break;
		AdditionalLight a;
		a.Position = Vec3(p.Position.x, p.Position.y, p.Position.z);
		a.Range = p.Range;
		a.Color = Vec3(p.Diffuse.x, p.Diffuse.y, p.Diffuse.z);
		additional.push_back(a);
	}
}

static ShadowRenderer::FrameData s_GameShadow;
static ShadowRenderer::FrameData s_ProbeShadow;   // Reflection Probe 찍기 (캐시 없음)
static ShadowRenderer::FrameData s_EditorShadow;

void EditorApp::OnSceneRender(GfxRenderTargetView* renderTargetView, Camera* camera)
{
	FRAME_PROFILE("GameView render");
	// Profiler 창: 이 화면의 GPU 시간 + 단계별 (CPU + GPU 타임스탬프)
	PROFILE_GPU("Game View");
	GameViewDesc d;
	d.Cam = camera;
	d.View = camera->View();
	d.Proj = camera->Proj();
	d.Position = camera->GetPosition();
	d.CullingMask = camera->GetCullingMask();
	d.BackgroundType = camera->GetBackgroundType();
	if (camera->UsesSolidBackground())
		memcpy(d.Background, camera->GetBackgroundColor(), sizeof(d.Background));
	d.Viewport = RenderManager::GetI()->Viewport;
	d.Shadow = &s_GameShadow;
	// TAA: 프레임마다 투영을 서브픽셀만큼 흔든다 (Halton 2 · 3, 8 개) — 후처리가 히스토리와 섞어 계단을 지운다
	if (camera->AntiAliasingMode() == 3 && d.Viewport.Width > 0 && d.Viewport.Height > 0)
	{
		auto halton = [](int i, int b) { float f = 1.0f, r = 0.0f; for (; i > 0; i /= b) { f /= b; r += f * (i % b); } return r; };
		const int k = (int)(SceneCulling::FrameIndex() % 8) + 1;
		const float s = camera->TaaJitterScale();
		const float jx = (halton(k, 2) - 0.5f) * s, jy = (halton(k, 3) - 0.5f) * s;   // 픽셀
		d.Jittered = true;
		d.UnjitteredProj = d.Proj;
		d.Proj = d.Proj * XMMatrixTranslation(2.0f * jx / d.Viewport.Width, -2.0f * jy / d.Viewport.Height, 0.0f);
		d.JitterUV = XMFLOAT2(jx / d.Viewport.Width, jy / d.Viewport.Height);
	}
	ProbeVolumes::SetFocus(d.Position, false);
	RenderGameView(renderTargetView, d);
}

// Adaptive Probe Volume 의 판 하나: 빛 없이 알베도 (32 의 gGIParams.z) — 그림자 · 하늘 · 투명 · 물 · 입자 없음. 노멀 · 깊이를 돌려준다
GfxShaderResourceView* EditorApp::CaptureGIView(const ProbeVolumes::CaptureView& v)
{
	GameViewDesc d;
	d.Mode = 2;
	d.View = v.View;
	d.Proj = v.Proj;
	d.Position = v.Position;
	d.BackgroundType = 1;
	for (float& f : d.Background) f = 0.0f;   // 알파 0 = 빈 곳
	d.Viewport = v.Viewport;
	d.Shadow = &s_ProbeShadow;
	RenderGameView(v.Target, d);
	return _probeNormalDepthSRV.Get();
}

// Reflection Probe 한 면: Game 뷰와 같은 길 (후처리 · SSAO 없음, 프로브 반사 끔 — ReflectionProbes::Capturing)
void EditorApp::CaptureProbeFace(const ReflectionProbes::CaptureView& v)
{
	GameViewDesc d;
	d.View = v.View;
	d.Proj = v.Proj;
	d.Position = v.Position;
	d.CullingMask = v.CullingMask;
	d.BackgroundType = v.SolidColor ? 1 : 0;
	memcpy(d.Background, v.Background, sizeof(d.Background));
	d.Viewport = v.Viewport;
	d.ShadowDistance = v.ShadowDistance;
	// 프로브는 캐시 없이 그림자를 다시 그리고, Game 뷰가 같은 그림자 맵에 캐시해 둔 먼 캐스케이드도 무효로
	for (auto& c : s_ProbeShadow.Cache) c.Valid = false;
	d.Shadow = &s_ProbeShadow;
	RenderGameView(v.Target, d);
	for (auto& c : s_GameShadow.Cache) c.Valid = false;
}

// Reflection Probe 찍기의 깊이 프리패스 타깃 (뷰 노멀 + 뷰 깊이, 커지기만 함)
bool EditorApp::ProbeNormalDepth(UINT width, UINT height)
{
	if (_probeNormalDepthRTV && width <= _probeNormalDepthSize && height <= _probeNormalDepthSize)
		return true;
	_probeNormalDepthRTV.Reset();
	_probeNormalDepthSRV.Reset();
	const UINT size = (std::max)((std::max)(width, height), _probeNormalDepthSize);
	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = desc.Height = size;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	ComPtr<GfxTexture2D> tex;
	if (FAILED(_device->CreateTexture2D(&desc, nullptr, tex.GetAddressOf())))
		return false;
	_device->CreateRenderTargetView(tex.Get(), nullptr, _probeNormalDepthRTV.GetAddressOf());
	_device->CreateShaderResourceView(tex.Get(), nullptr, _probeNormalDepthSRV.GetAddressOf());
	_probeNormalDepthSize = size;
	return _probeNormalDepthRTV && _probeNormalDepthSRV;
}

void EditorApp::RenderGameView(GfxRenderTargetView* renderTargetView, const GameViewDesc& d)
{
	const bool probe = d.Cam == nullptr;
	const bool giCapture = d.Mode == 2;   // Adaptive Probe Volume 판 찍기 (알베도만)
	// 렌더러·나무 목록은 화면마다 한 번 모아 모든 패스가 같이 쓴다 (찍기 = LOD 바로 고름).
	//  오클루전 컬링은 카메라마다 (Camera 의 Occlusion Culling, 찍기는 끔)
	MeshBatcher::BeginView(probe, (!probe && d.Cam && d.Cam->UsesOcclusionCulling()) ? static_cast<const void*>(d.Cam) : nullptr);
	TreeRenderer::BeginView();
	++RenderManager::GetI()->ViewSerial;
	vector<DirectionalLight> dirLights = LightManager::GetI()->GetDirLights();
	{
		// 날씨: 해 (방향광 0) 를 흐리게 · 번개, 그리고 날씨가 따라갈 카메라 자리
		WeatherState& ws = WeatherState::Get();
		if (!dirLights.empty()) ws.ApplySun(dirLights[0].Diffuse, dirLights[0].Specular);
		if (!probe)
		{
			ws.GameViewPosition = XMFLOAT3(d.Position.x, d.Position.y, d.Position.z);
			ws.GameViewForward = d.Cam->GetLook();
			ws.GameViewFrame = ws.Frame;
		}
	}
	vector<PointLight> pointLights = LightManager::GetI()->GetPointLights();
	const int scenePointLights = (int)pointLights.size();      // 그림자는 장면의 Light 컴포넌트만 (입자 빛은 그림자 없음)
	std::vector<AdditionalLight> additionalLights = LightManager::GetI()->GetAdditionalLights();   // Forward+ (클러스터)
	AddParticleLights(pointLights, additionalLights);   // Lights 모듈 (남은 점광 칸에, 다 차면 클러스터로)
	vector<SpotLight> spotLights = LightManager::GetI()->GetSpotLights();

	RenderManager::GetI()->RenderingEditorView = false;
	RenderLayers::SetViewMask(d.CullingMask);   // Camera.cullingMask: 이 화면에 그릴 레이어
	RenderLayers::SetPassMask(~0u);
	RenderManager::GetI()->CameraViewMatrix = d.View;
	RenderManager::GetI()->CameraProjectionMatrix = d.Proj;
	RenderManager::GetI()->CameraViewProjectionMatrix = XMMatrixMultiply(d.View, d.Proj);
	// 이 뷰에 쓸 Reflection Probe (큐브 배열 칸 필터 — 렌더 타깃을 묶기 전에)
	if (!giCapture)
		ReflectionProbes::Select(d.Position, XMMatrixMultiply(d.View, d.Proj));

	auto shadowMap = RenderManager::GetI()->BaseShadowMap;
	const D3D11_VIEWPORT viewport = d.Viewport;
	// 뷰(렌더 타깃) 크기의 깊이 버퍼: 창 백버퍼보다 큰 해상도(예: 1080x1920)로 그릴 때도 깊이가 맞도록
	GfxDepthStencilView* viewDsv = ViewDepth((UINT)viewport.Width, (UINT)viewport.Height);
	Effects::BuildShadowMapFX->SetEyePosW(d.Position);

	// Volume 값 (Shadows · 후처리) 을 카메라 위치로 섞는다
	auto& stack = PostProcessingManager::GetI()->GameStack();
	VolumeManager::Update(stack, Vec3(d.Position.x, d.Position.y, d.Position.z));

	// 깊이 프리패스의 노멀 · 깊이 타깃
	auto ssao = PostProcessingManager::GetI()->GetSSAO();
	GfxShaderResourceView* normalDepthSRV = nullptr;
	if (probe)
	{
		// 프로브: 자기 크기의 노멀 · 깊이 타깃 (SSAO 의 것은 Game 뷰 크기 — 큰 프로브 면이 잘린다)
		if (!ProbeNormalDepth((UINT)viewport.Width, (UINT)viewport.Height))
			return;
		normalDepthSRV = _probeNormalDepthSRV.Get();
	}
	else
	{
		if (!ssao)
			return;
		normalDepthSRV = ssao->NormalDepthSRV().Get();
	}

	// ---- 이 뷰의 설정 (패스를 쌓기 전에 정한다: 누가 무엇을 읽는지)
	const Ssao::Settings ssaoSettings = Ssao::Settings::FromStack(stack);
	auto& post = PostProcessingManager::GetI()->GamePost();   // stack 은 그림자 패스 앞에서 섞었다
	PostProcessPass::CameraOptions postOptions;
	postOptions.PostProcessing = !probe && d.Cam->PostProcessingEnabled();
	const int aa = probe ? 0 : d.Cam->AntiAliasingMode();   // URP: 0 없음, 1 FXAA, 2 SMAA, 3 TAA
	postOptions.Fxaa = aa == 1;
	postOptions.Smaa = aa == 2;
	postOptions.Taa = aa == 3 && d.Jittered;
	if (!probe)
	{
		postOptions.SmaaQuality = d.Cam->SmaaQuality();
		postOptions.TaaQuality = d.Cam->TaaQuality();
		postOptions.TaaBaseBlend = d.Cam->TaaBaseBlend();
		postOptions.TaaVarianceClamp = d.Cam->TaaVarianceClamp();
		postOptions.TaaSharpening = d.Cam->TaaSharpening();
		postOptions.TaaJitterUV = d.JitterUV;
	}
	postOptions.Dithering = !probe && d.Cam->DitheringEnabled();
	postOptions.StopNaNs = !probe && d.Cam->StopNaNsEnabled();
	postOptions.Depth = normalDepthSRV;   // Depth Of Field · Motion Blur
	XMStoreFloat4x4(&postOptions.View, d.View);
	XMStoreFloat4x4(&postOptions.Proj, d.Jittered ? d.UnjitteredProj : d.Proj);   // 지난 프레임 위치는 지터 없이
	const bool usePost = !probe && PostProcessPass::IsNeeded(stack, postOptions);
	GfxRenderTargetView* sceneTarget = usePost ? post.Begin((UINT)viewport.Width, (UINT)viewport.Height) : renderTargetView;
	// 모션 벡터를 읽는 쪽: SSAO 시간 누적 · TAA · Motion Blur (Camera And Objects) · Rendering Debugger — 아무도 읽지 않으면 그래프가 패스를 뺀다
	const VolumeComponent* motionBlur = stack.Get("MotionBlur");
	const bool postReadsMotion = usePost && (postOptions.Taa || (motionBlur && stack.IsActive("MotionBlur") && motionBlur->I("mode") == 1));
	const bool ssaoReadsMotion = ssaoSettings.Enabled && ssaoSettings.TemporalAccumulation;
	const int debugMode = probe ? RenderingDebug::None : RenderingDebug::Get().Mode;
	const bool debugReadsMotion = debugMode == RenderingDebug::MotionVectors;
	const AtmospherePass::Params atmosphere = AtmospherePass::FromStack(stack, dirLights.empty() ? nullptr : &dirLights[0]);
	XMFLOAT4 indirect = XMFLOAT4(1, 1, 1, 1);
	GfxShaderResourceView* motionVectors = nullptr;
	GfxShaderResourceView* ssaoMap = probe ? SpriteBatch::WhiteTexture() : ssao->AmbientSRV().Get();

	// ---- Render Graph: 패스마다 읽기 · 쓰기를 적고, 결과 (뷰 타깃 · 다음 프레임 히스토리) 에 닿는 패스만 차례로 실행
	RenderGraph::Graph g(giCapture ? "GI Capture" : probe ? "Reflection Probe" : "Game");
	RenderGraph::Texture tDepth = g.Import("View Depth", nullptr, nullptr, viewDsv);
	RenderGraph::Texture tNormalDepth = g.Import("Normal Depth", normalDepthSRV);
	RenderGraph::Texture tShadows = g.Import("Shadow Maps", nullptr);
	RenderGraph::Texture tMotion = g.Import("Motion Vectors", nullptr);
	RenderGraph::Texture tAO = g.Import("SSAO", ssaoMap);
	RenderGraph::Texture tScene = g.Import(usePost ? "Scene Color (HDR)" : "Scene Color", nullptr, sceneTarget);
	RenderGraph::Texture tOut = usePost ? g.Import("View Target", nullptr, renderTargetView) : RenderGraph::Texture();
	RenderGraph::Texture* out = usePost ? &tOut : &tScene;   // 뷰 타깃 (후처리가 없으면 장면 색이 곧 뷰 타깃 — 그 마지막 판)
	RenderGraph::Texture tSsrHistory = g.Import("SSR History", nullptr);

	g.AddPass("Depth Prepass", [&](RenderGraph::Builder& b) { tNormalDepth = b.Write(tNormalDepth); tDepth = b.Write(tDepth); },
		[&](const RenderGraph::Resources&) {
			_deviceContext->RSSetState(0);
			_deviceContext->ClearDepthStencilView(viewDsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
			_deviceContext->RSSetViewports(1, &viewport);
			if (probe)
			{
				GfxRenderTargetView* nd[1] = { _probeNormalDepthRTV.Get() };
				_deviceContext->OMSetRenderTargets(1, nd, viewDsv);
				const float clearND[4] = { 0.0f, 0.0f, -1.0f, 1e5f };
				_deviceContext->ClearRenderTargetView(_probeNormalDepthRTV.Get(), clearND);
			}
			else
				ssao->SetNormalDepthRenderTarget(viewDsv);
			if (RenderManager::GetI()->WireFrameMode)
				_deviceContext->RSSetState(RenderStates::WireframeRS.Get());
			// 카메라 절두체 컬링 (깊이 사전 패스와 본 패스가 같이 쓴다)
			SceneCulling::SetEditorView(false);
			SceneCulling::Cull(d.View * d.Proj, false);
			SceneManager::GetI()->GetCurrentScene()->RenderSceneShadowNormal();
			MeshBatcher::FinishDepthPrepass(SceneManager::GetI()->GetCurrentScene(), false);   // 오클루전 컬링: 깊이 → Hi-Z → 새로 보인 렌더러
			_deviceContext->RSSetState(0);
		});

	// Virtual Texturing 피드백: 가상 텍스처 물체를 1/8 크기로 그려 필요한 페이지를 적는다 (CPU 가 2 프레임 뒤에 읽는다 — Side Effect)
	if (!probe && !giCapture && VirtualTexturing::HasWork())
	{
		RenderGraph::Texture tVtFeedback = g.Import("VT Feedback", nullptr);
		g.AddPass("VT Feedback", [&](RenderGraph::Builder& b) { b.Read(tNormalDepth); tVtFeedback = b.Write(tVtFeedback); b.SideEffect(); },
			[&](const RenderGraph::Resources&) {
				VirtualTexturing::RenderFeedback(_deviceContext.Get(), d.View, d.Proj, (UINT)viewport.Width, (UINT)viewport.Height, normalDepthSRV, false);
				_deviceContext->RSSetViewports(1, &viewport);
			});
	}

	// 그림자 맵 (깊이 프리패스 뒤: 오클루전 컬링의 Hi-Z 로 그림자가 보이지 않는 캐스터를 뺀다). 알베도 찍기는 빛 · 그림자가 없다
	if (!giCapture)
		g.AddPass("Shadows", [&](RenderGraph::Builder& b) { b.Read(tDepth); tShadows = b.Write(tShadows); },
			[&](const RenderGraph::Resources&) {
				const XMFLOAT3 gameCamPos = d.Position;
				const vector<shared_ptr<Light>> sortedLights = LightManager::GetI()->GetSortedLights();
				ShadowRenderer::Settings shadowSettings = ShadowRenderer::Settings::FromStack(stack);
				if (probe)
				{
					shadowSettings.MaxDistance = (std::max)(d.ShadowDistance, 0.01f);   // 프로브의 Shadow Distance
					shadowSettings.FarCascadeUpdate = 0;
				}
				ShadowRenderer::Render(_deviceContext.Get(), *shadowMap, sortedLights, (int)dirLights.size(), (int)spotLights.size(), scenePointLights,
					gameCamPos, d.View, d.Proj, shadowSettings, *d.Shadow,
					[]() {
						// 그림자 조각마다 빛의 절두체로 컬링
						SceneCulling::Cull(RenderManager::GetI()->LightViewProjection, true);
						SceneManager::GetI()->GetCurrentScene()->RenderSceneShadow();
					});
				if (!probe)
				{
					Profiler::SetStat("Game View/Shadow Cascades Redrawn", s_GameShadow.CascadesDrawn);   // 먼 캐스케이드 캐시
					WeatherCover::Render(_deviceContext.Get(), 0, gameCamPos);   // 날씨: 위에서 본 덮개 (지붕 아래는 젖지 않는다)
				}
				_deviceContext->RSSetState(0);
				_deviceContext->RSSetViewports(1, &viewport);
				// 그림자 조각마다 빛으로 컬링했다 → 카메라 컬링을 되돌린다 (본 패스 · 투명 · 스킨 메시가 따른다)
				SceneCulling::SetEditorView(false);
				SceneCulling::Cull(d.View * d.Proj, false);
			});

	// Visual Effect 시뮬레이션 (GPU 파티클): 비동기 컴퓨트 — 큐가 둘인 백엔드 (DirectX 12) 에서는 모션 벡터 · SSAO · 불투명과 겹쳐 돈다.
	//  깊이 충돌이 이 뷰의 깊이를 읽는다 (프리패스 뒤). 입자 패스가 결과를 읽는다 (그 앞에서 기다린다). 프레임의 첫 뷰에서만
	RenderGraph::Texture tVfx;
	if (!probe && !giCapture && VfxRuntime::NeedsSimulation())
	{
		tVfx = g.Import("VFX Particles", nullptr);
		g.AddPass("VFX Simulation", [&](RenderGraph::Builder& b) { b.Read(tDepth); tVfx = b.Write(tVfx); if (!VfxRuntime::ReadsSceneTextures()) b.AsyncCompute(); },
			[&](const RenderGraph::Resources&) {
				_deviceContext->RSSetViewports(1, &viewport);
				const ParticleRenderer::Environment env = ParticleEnvironment(viewDsv, dirLights, indirect, d.BackgroundType == 0);
				VfxRuntime::SimulateEffects(d.View, d.Proj, &env);
			});
	}

	// 모션 벡터 (Volume 의 Motion Vectors): 깊이 프리패스 뒤. 읽는 패스가 없으면 그래프가 뺀다. 프로브 · APV 찍기에는 없음
	if (!probe)
		g.AddPass("Motion Vectors", [&](RenderGraph::Builder& b) { b.Read(tNormalDepth); tMotion = b.Write(tMotion); },
			[&](const RenderGraph::Resources&) {
				MotionVectors::Frame mf;
				XMStoreFloat4x4(&mf.View, d.View);
				XMStoreFloat4x4(&mf.Proj, d.Jittered ? d.UnjitteredProj : d.Proj);
				XMStoreFloat4x4(&mf.ProjJittered, d.Proj);
				mf.Width = (UINT)viewport.Width;
				mf.Height = (UINT)viewport.Height;
				mf.NormalDepth = normalDepthSRV;
				MotionVectors::Render(mf, MotionVectors::Settings::FromStack(stack));
				motionVectors = MotionVectors::SRV();
				_deviceContext->RSSetViewports(1, &viewport);
			});

	// SSAO (프로브: 없음 — 노멀 · 깊이 타깃이 화면 크기라 프로브 화면과 맞지 않는다, 흰 그림으로)
	if (!probe)
		g.AddPass("SSAO", [&](RenderGraph::Builder& b) {
				b.Read(tNormalDepth);
				if (ssaoReadsMotion)
					b.Read(tMotion);
				tAO = b.Write(tAO);
			},
			[&](const RenderGraph::Resources&) {
				PostProcessingManager::GetI()->RenderSSAO(d.View, d.Proj, ssaoSettings, ssaoReadsMotion ? motionVectors : nullptr);   // TAA 지터 그대로 (깊이 프리패스와 같은 투영)
				UseSsaoSettings(ssaoSettings);
			});

	// 이 뷰의 빛 · 그림자 · 프로브 값을 Lit 효과 (+ 패키지 셰이더) 에 — 포워드 본 패스 · 디퍼드 G-버퍼 · 조명이 같이 쓴다 (cbPerFrame)
	auto bindFrame = [&]() {
		indirect = ApplyIndirectLighting(stack);
		if (giCapture)
		{
			// 알베도 찍기: 32 의 ShadeLit 이 빛 없이 확산 색만 돌려준다. 다른 프레임 값 (빛 · 그림자 · 눈 위치) 은 건드리지 않는다
			//  — Adaptive Probe Volume 의 다시 비추기가 마지막 뷰의 값을 쓴다
			ProbeVolumes::Bind(Effects::InstancedBasicFX.get());
			CustomShaders::ForEachEffect([&](InstancedBasicEffect* fx) { ProbeVolumes::Bind(fx); });
		}
		else
		{
			Effects::InstancedBasicFX->SetEyePosW(d.Position);
			Effects::InstancedBasicFX->SetCubeMap(_sky->CubeMapSRV().Get());
			Effects::InstancedBasicFX->SetSsaoMap(ssaoMap);
			ReflectionProbes::Bind(Effects::InstancedBasicFX.get());
			ProbeVolumes::Bind(Effects::InstancedBasicFX.get());
			// Screen Space Reflection: 깊이 프리패스 + 지난 프레임 장면 색 (찍기에는 없음)
			ScreenSpaceReflection::Prepare(stack, false, probe, normalDepthSRV, d.View, d.Proj);
			ScreenSpaceReflection::Bind(Effects::InstancedBasicFX.get());

			// lights
			Effects::InstancedBasicFX->SetDirLights(dirLights.data(), dirLights.size());
			Effects::InstancedBasicFX->SetSpotLights(spotLights.data(), spotLights.size());
			Effects::InstancedBasicFX->SetPointLights(pointLights.data(), pointLights.size());

			RenderLayers::SetLightMasks(Effects::InstancedBasicFX.get(), scenePointLights, false);   // Light.cullingMask
			{
				XMFLOAT4X4 v, p;
				XMStoreFloat4x4(&v, d.View);
				XMStoreFloat4x4(&p, d.Proj);
				ClusteredLighting::Build(additionalLights, v, p);   // 이 뷰 (Game · 반사 프로브 면) 의 클러스터
			}
			ClusteredLighting::Bind(Effects::InstancedBasicFX.get());

			// 그림자 맵 / 변환 / 캐스케이드 / 빛별 Strength·필터
			ShadowRenderer::Bind(Effects::InstancedBasicFX.get(), *shadowMap, *d.Shadow);
			WeatherCover::Bind(Effects::InstancedBasicFX.get(), 0);   // 젖은 표면 · 웅덩이
			// 패키지 셰이더 이펙트 (CustomShaders) 에도 같은 프레임 상수
			CustomShaders::ForEachEffect([&](InstancedBasicEffect* fx) {
				WeatherCover::Bind(fx, 0);
				fx->SetEyePosW(d.Position);
				fx->SetCubeMap(_sky->CubeMapSRV().Get());
				fx->SetSsaoMap(ssaoMap);
				ReflectionProbes::Bind(fx);
				ProbeVolumes::Bind(fx);
				ScreenSpaceReflection::Bind(fx);
				if (auto* var = fx->GetFX()->GetVariableByName("gIndirect")->AsVector(); var && var->IsValid())
					var->SetFloatVector(&indirect.x);
				SetIndirectGI(fx->GetFX());
				SetSsaoParams(fx->GetFX());
				fx->SetDirLights(dirLights.data(), dirLights.size());
				fx->SetSpotLights(spotLights.data(), spotLights.size());
				fx->SetPointLights(pointLights.data(), pointLights.size());
				ShadowRenderer::Bind(fx, *shadowMap, *d.Shadow);
				RenderLayers::SetLightMasks(fx, scenePointLights, false);
				ClusteredLighting::Bind(fx);
			});
		}
	};
	auto clearScene = [&]() {
		// Game 뷰: 카메라의 Background Type 이 Solid Color 이면 그 색, 아니면 Unity 기본 카메라 배경색(#314D79)
		float gameClear[4] = { 49.0f / 255.0f, 77.0f / 255.0f, 121.0f / 255.0f, 1.0f };
		if (d.BackgroundType == 1)
			memcpy(gameClear, d.Background, sizeof(gameClear));
		_deviceContext->ClearRenderTargetView(sceneTarget, gameClear);
	};
	// Rendering Path = Deferred (Project Settings > Graphics): 엔진 Lit 재질 묶음 → G-버퍼 → 전체 화면 조명, 나머지는 아래 Opaque 가 포워드로
	const bool deferred = !probe && !giCapture && RenderPipelineSettings::GetRenderingPath() == RenderPipelineSettings::RenderingPath::Deferred &&
		viewDsv == _viewDepthView.Get() && _viewDepthSRV && DeferredRenderer::Available();
	if (deferred)
	{
		RenderGraph::Texture tGBuffer = g.Import("G-Buffer", DeferredRenderer::GBufferSRV(0, 0));
		g.AddPass("GBuffer", [&](RenderGraph::Builder& b) { b.Read(tDepth); b.Read(tNormalDepth); tGBuffer = b.Write(tGBuffer); },
			[&](const RenderGraph::Resources&) {
				GfxRenderTargetView* renderTargets[1] = { sceneTarget };
				_deviceContext->OMSetRenderTargets(1, renderTargets, viewDsv);
				clearScene();
				bindFrame();
				if (DeferredRenderer::BeginGBuffer(_deviceContext.Get(), (UINT)viewport.Width, (UINT)viewport.Height, viewDsv, viewport, 0))
				{
					_deviceContext->OMSetDepthStencilState(RenderStates::EqualsDSS.Get(), 0);
					_deviceContext->IASetInputLayout(InputLayouts::InstancedBasic.Get());
					MeshBatcher::SetDeferredSplit(MeshBatcher::DeferredSplit::GBuffer);
					MeshBatcher::Draw(SceneManager::GetI()->GetCurrentScene(), MeshBatcher::Pass::Main, false);
					MeshBatcher::SetDeferredSplit(MeshBatcher::DeferredSplit::None);
				}
				_deviceContext->RSSetState(0);
				_deviceContext->OMSetDepthStencilState(0, 0);
			});
		g.AddPass("Deferred Lighting", [&](RenderGraph::Builder& b) { b.Read(tGBuffer); b.Read(tDepth); b.Read(tShadows); b.Read(tAO); tScene = b.Write(tScene); },
			[&](const RenderGraph::Resources&) {
				DeferredRenderer::Light(_deviceContext.Get(), sceneTarget, _viewDepthSRV.Get(), d.View * d.Proj, viewport, 0);
			});
	}

	g.AddPass("Opaque", [&](RenderGraph::Builder& b) {
			b.Read(tDepth);
			b.Read(tNormalDepth);
			if (!giCapture)
				b.Read(tShadows);
			if (!probe)
				b.Read(tAO);
			if (deferred)
				b.Read(tScene);   // 디퍼드 조명이 그린 장면 위에 그린다 (안 읽으면 G-버퍼 · 조명 패스가 빠진다)
			tScene = b.Write(tScene);
		},
		[&](const RenderGraph::Resources&) {
			GfxRenderTargetView* renderTargets[1] = { sceneTarget };
			_deviceContext->OMSetRenderTargets(1, renderTargets, viewDsv);
			_deviceContext->RSSetViewports(1, &viewport);
			if (!deferred)
			{
				clearScene();
				bindFrame();
			}
			_deviceContext->OMSetDepthStencilState(RenderStates::EqualsDSS.Get(), 0);
			_deviceContext->IASetInputLayout(InputLayouts::InstancedBasic.Get());
			// 디퍼드: G-버퍼로 간 묶음은 빼고 나머지만 포워드로 (URP 의 Forward Only)
			MeshBatcher::SetDeferredSplit(deferred ? MeshBatcher::DeferredSplit::ForwardOnly : MeshBatcher::DeferredSplit::None);
			SceneManager::GetI()->GetCurrentScene()->RenderScene();
			MeshBatcher::SetDeferredSplit(MeshBatcher::DeferredSplit::None);
			_deviceContext->RSSetState(0);
			_deviceContext->OMSetDepthStencilState(0, 0);
		});

	// 데칼 (Decal Projector): 불투명 다음 — 깊이 프리패스의 노멀 · 깊이로 표면을 되살려 상자 안을 칠한다 (하늘 · 투명 · 물 · 입자에는 안 묻는다)
	g.AddPass("Decals", [&](RenderGraph::Builder& b) { b.Read(tNormalDepth); b.Read(tScene); tScene = b.Write(tScene); },
		[&](const RenderGraph::Resources&) {
			DecalRenderer::Render(_deviceContext.Get(), sceneTarget, viewport, d.View, d.Proj, normalDepthSRV, false);
		});

	// Background Type = Skybox 면 불투명 물체 다음(빈 곳 깊이 = 1)에 하늘을 그린다. 입자(투명)보다는 먼저.
	if (d.BackgroundType == 0)
		g.AddPass("Sky", [&](RenderGraph::Builder& b) { b.Read(tDepth); b.Read(tScene); tScene = b.Write(tScene); },
			[&](const RenderGraph::Resources&) {
				_sky->Draw(_deviceContext.Get(), d.Position, d.View * d.Proj);
				_deviceContext->RSSetState(0);
				_deviceContext->OMSetDepthStencilState(0, 0);
			});

	if (!giCapture)   // 알베도 찍기: 불투명 (+ 데칼 · 하늘) 까지만
	{
		// 안개·대기 (Volume > Fog / Atmosphere): 불투명 + 하늘 다음, 물 전 (물은 같은 값으로 자기 표면에 입힌다)
		if (atmosphere.Active())
			g.AddPass("Atmosphere", [&](RenderGraph::Builder& b) { b.Read(tDepth); b.Read(tScene); tScene = b.Write(tScene); },
				[&](const RenderGraph::Resources&) {
					DrawAtmosphere(&atmosphere, d.View * d.Proj, d.Position, sceneTarget, viewDsv, viewport, d.BackgroundType == 0);
				});

		// 투명 메시 (Shader Graph 의 Surface Type = Transparent): 먼 것부터, 깊이는 읽기만
		g.AddPass("Transparent", [&](RenderGraph::Builder& b) { b.Read(tDepth); b.Read(tScene); tScene = b.Write(tScene); },
			[&](const RenderGraph::Resources&) {
				// 깊이는 읽기만 (읽기 전용 DSV — 깊이 SRV 와 같이 묶여도 된다)
				GfxDepthStencilView* readDsv = (viewDsv == _viewDepthView.Get() && _viewDepthReadOnly) ? _viewDepthReadOnly.Get() : viewDsv;
				_deviceContext->OMSetRenderTargets(1, &sceneTarget, readDsv);
				_deviceContext->RSSetViewports(1, &viewport);
				MeshBatcher::Draw(SceneManager::GetI()->GetCurrentScene(), MeshBatcher::Pass::Transparent, false);
				_deviceContext->RSSetState(0);
			});

		// 물 (바다·호수·강): 굴절에 화면 색을 쓴다, 입자 전
		g.AddPass("Water", [&](RenderGraph::Builder& b) { b.Read(tDepth); b.Read(tShadows); b.Read(tScene); tScene = b.Write(tScene); },
			[&](const RenderGraph::Resources&) {
				DrawWater(d.View, d.Proj, d.Position, sceneTarget, viewDsv, viewport, dirLights, d.BackgroundType == 0, shadowMap.get(), d.Shadow, &atmosphere);
			});

		// 2D 스프라이트 (SpriteRenderer · 2D 뼈대): 투명 — 하늘 · 물 다음, 입자 전
		g.AddPass("Sprites", [&](RenderGraph::Builder& b) { b.Read(tDepth); b.Read(tScene); tScene = b.Write(tScene); },
			[&](const RenderGraph::Resources&) {
				_deviceContext->RSSetViewports(1, &viewport);
				SpriteBatch::Render(d.View, d.Proj, sceneTarget, viewDsv);
			});

		g.AddPass("Particles", [&](RenderGraph::Builder& b) { b.Read(tDepth); b.Read(tVfx); b.Read(tScene); tScene = b.Write(tScene); },
			[&](const RenderGraph::Resources&) {
				const ParticleRenderer::Environment env = ParticleEnvironment(viewDsv, dirLights, indirect, d.BackgroundType == 0);
				ParticleRenderer::Render(d.View, d.Proj, sceneTarget, viewDsv, &env);
				VfxRuntime::Render(d.View, d.Proj, sceneTarget, viewDsv, &env);   // Visual Effect (GPU 파티클 — 프레임의 첫 뷰에서 시뮬레이션)
				_deviceContext->RSSetViewports(1, &viewport);
				_deviceContext->RSSetState(0);
				_deviceContext->OMSetDepthStencilState(0, 0);
				GfxShaderResourceView* nullSRV[128] = { 0 };
				_deviceContext->PSSetShaderResources(0, 128, nullSRV);
			});

		// 다음 프레임 Screen Space Reflection 이 쓸 장면 색 (후처리 전 — 톤매핑 · 블룸이 두 번 들지 않게). 그래프 밖으로 나가는 결과
		if (!probe)
			g.AddPass("SSR History", [&](RenderGraph::Builder& b) { b.Read(tScene); tSsrHistory = b.Write(tSsrHistory); b.SideEffect(); },
				[&](const RenderGraph::Resources&) {
					ScreenSpaceReflection::StoreHistory(_deviceContext.Get(), sceneTarget, false);
				});

		if (usePost)
			g.AddPass("Post Processing", [&](RenderGraph::Builder& b) {
					b.Read(tScene);
					b.Read(tNormalDepth);
					if (postReadsMotion)
						b.Read(tMotion);
					tOut = b.Write(tOut);
				},
				[&](const RenderGraph::Resources&) {
					postOptions.MotionVectors = postReadsMotion ? motionVectors : nullptr;   // TAA · Motion Blur (Camera And Objects)
					post.Execute(stack, postOptions, renderTargetView);
					// 이후 그리기(있다면)를 위해 원래 타깃과 뷰포트로 되돌린다
					GfxRenderTargetView* outTargets[1] = { renderTargetView };
					_deviceContext->OMSetRenderTargets(1, outTargets, viewDsv);
					_deviceContext->RSSetViewports(1, &viewport);
				});

		// Rendering Debugger: 깊이 · 노멀 · SSAO · 모션 벡터 · 클러스터 전체 화면 보기
		if (debugMode != RenderingDebug::None)
			g.AddPass("Rendering Debugger", [&](RenderGraph::Builder& b) {
					b.Read(tNormalDepth);
					b.Read(tAO);
					if (debugReadsMotion)
						b.Read(tMotion);
					b.Read(*out);
					*out = b.Write(*out);
				},
				[&](const RenderGraph::Resources&) {
					RenderingDebug::Inputs dbg;
					dbg.NormalDepth = normalDepthSRV;
					dbg.Ao = ssaoMap;
					dbg.Motion = debugReadsMotion ? motionVectors : nullptr;
					XMStoreFloat4x4(&dbg.View, d.View);
					dbg.Width = (UINT)viewport.Width;
					dbg.Height = (UINT)viewport.Height;
					if (RenderingDebug::Draw(dbg, renderTargetView, viewport))
					{
						GfxRenderTargetView* outTargets[1] = { renderTargetView };
						_deviceContext->OMSetRenderTargets(1, outTargets, viewDsv);
					}
				});
	}
	g.MarkOutput(*out);
	g.Compile();
	g.Execute();
}

void EditorApp::_Editor_OnSceneRender(GfxRenderTargetView* renderTargetView, EditorCamera* camera)
{
	FRAME_PROFILE("SceneView render");
	// Profiler 창: 이 화면의 GPU 시간 + 단계별 (CPU + GPU 타임스탬프 — Render Graph 가 패스마다)
	PROFILE_GPU("Scene View");
	MeshBatcher::BeginView(false, camera);   // Scene 뷰도 오클루전 컬링 (카메라 = 지난 프레임 기록 · Hi-Z 의 키)
	TreeRenderer::BeginView();
	++RenderManager::GetI()->ViewSerial;
	vector<DirectionalLight> dirLights = LightManager::GetI()->GetEditorDirLights();
	{
		WeatherState& ws = WeatherState::Get();
		if (!dirLights.empty()) ws.ApplySun(dirLights[0].Diffuse, dirLights[0].Specular);
		const Vec3 cp = camera->GetPosition();
		ws.SceneViewPosition = XMFLOAT3(cp.x, cp.y, cp.z);
		ws.SceneViewForward = camera->GetLook();
		ws.SceneViewFrame = ws.Frame;
	}
	vector<PointLight> pointLights = LightManager::GetI()->GetEditorPointLights();
	const int scenePointLights = (int)pointLights.size();      // 그림자는 장면의 Light 컴포넌트만 (입자 빛은 그림자 없음)
	std::vector<AdditionalLight> additionalLights = LightManager::GetI()->GetEditorAdditionalLights();
	AddParticleLights(pointLights, additionalLights);
	vector<SpotLight> spotLights = LightManager::GetI()->GetEditorSpotLights();

	RenderManager::GetI()->RenderingEditorView = true;
	RenderLayers::SetViewMask(~0u);   // Scene 뷰는 모든 레이어
	RenderLayers::SetPassMask(~0u);
	RenderManager::GetI()->EditorCameraViewMatrix = camera->View();
	RenderManager::GetI()->EditorCameraProjectionMatrix = camera->Proj();
	RenderManager::GetI()->EditorCameraViewProjectionMatrix = XMMatrixMultiply(camera->View(), camera->Proj());
	ReflectionProbes::Select(camera->GetPosition(), XMMatrixMultiply(camera->View(), camera->Proj()));
	ProbeVolumes::SetFocus(camera->GetPosition(), true);

	auto shadowMap = RenderManager::GetI()->EditorShadowMap;
	auto viewport = RenderManager::GetI()->EditorViewport;
	GfxDepthStencilView* viewDsv = ViewDepth((UINT)viewport.Width, (UINT)viewport.Height);
	Effects::BuildShadowMapFX->SetEyePosW(camera->GetPosition());

	auto& stack = PostProcessingManager::GetI()->EditorStack();
	const XMFLOAT3 camPos = camera->GetPosition();
	VolumeManager::Update(stack, Vec3(camPos.x, camPos.y, camPos.z));
	auto ssao = PostProcessingManager::GetI()->_EditorGetSSAO();
	GfxShaderResourceView* normalDepthSRV = ssao->NormalDepthSRV().Get();

	// ---- 이 뷰의 설정 (패스를 쌓기 전에 정한다)
	const Ssao::Settings ssaoSettings = Ssao::Settings::FromStack(stack);
	// Volume 후처리 (Scene 뷰: 툴바 Effects > Post Processing 이 켜져 있을 때, 카메라 옵션은 기본)
	auto& post = PostProcessingManager::GetI()->EditorPost();
	PostProcessPass::CameraOptions postOptions;
	postOptions.PostProcessing = SceneToolbar::PostProcessingVisible() && !RenderManager::GetI()->WireFrameMode;
	postOptions.Depth = normalDepthSRV;   // Depth Of Field (Motion Blur 는 Scene 뷰에 없음 — Unity 와 같음)
	XMStoreFloat4x4(&postOptions.View, camera->View());
	XMStoreFloat4x4(&postOptions.Proj, camera->Proj());
	postOptions.SceneView = true;
	const bool usePost = PostProcessPass::IsNeeded(stack, postOptions);
	GfxRenderTargetView* sceneTarget = usePost ? post.Begin((UINT)viewport.Width, (UINT)viewport.Height) : renderTargetView;
	const int debugMode = RenderingDebug::Get().Mode;
	const bool wire = RenderManager::GetI()->WireFrameMode;
	AtmospherePass::Params atmosphere;
	if (SceneToolbar::FogVisible() && !wire)
		atmosphere = AtmospherePass::FromStack(stack, dirLights.empty() ? nullptr : &dirLights[0]);
	XMFLOAT4 indirect = XMFLOAT4(1, 1, 1, 1);
	GfxShaderResourceView* sceneMotion = nullptr;

	// ---- Render Graph (Game 뷰와 같은 방법 — RenderGameView)
	RenderGraph::Graph g("Scene");
	RenderGraph::Texture tDepth = g.Import("View Depth", nullptr, nullptr, viewDsv);
	RenderGraph::Texture tNormalDepth = g.Import("Normal Depth", normalDepthSRV);
	RenderGraph::Texture tShadows = g.Import("Shadow Maps", nullptr);
	RenderGraph::Texture tMotion = g.Import("Motion Vectors", nullptr);
	RenderGraph::Texture tAO = g.Import("SSAO", ssao->AmbientSRV().Get());
	RenderGraph::Texture tScene = g.Import(usePost ? "Scene Color (HDR)" : "Scene Color", nullptr, sceneTarget);
	RenderGraph::Texture tOut = usePost ? g.Import("View Target", nullptr, renderTargetView) : RenderGraph::Texture();
	RenderGraph::Texture* out = usePost ? &tOut : &tScene;
	RenderGraph::Texture tSsrHistory = g.Import("SSR History", nullptr);

	g.AddPass("Depth Prepass", [&](RenderGraph::Builder& b) { tNormalDepth = b.Write(tNormalDepth); tDepth = b.Write(tDepth); },
		[&](const RenderGraph::Resources&) {
			_deviceContext->RSSetState(0);
			_deviceContext->ClearDepthStencilView(viewDsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
			_deviceContext->RSSetViewports(1, &viewport);
			ssao->SetNormalDepthRenderTarget(viewDsv);
			if (wire)
				_deviceContext->RSSetState(RenderStates::WireframeRS.Get());
			SceneCulling::SetEditorView(true);
			SceneCulling::Cull(camera->View() * camera->Proj(), false);
			SceneManager::GetI()->GetCurrentScene()->_Editor_RenderSceneShadowNormal();
			MeshBatcher::FinishDepthPrepass(SceneManager::GetI()->GetCurrentScene(), true);
			_deviceContext->RSSetState(0);
		});

	if (VirtualTexturing::HasWork())
	{
		RenderGraph::Texture tVtFeedback = g.Import("VT Feedback", nullptr);
		g.AddPass("VT Feedback", [&](RenderGraph::Builder& b) { b.Read(tNormalDepth); tVtFeedback = b.Write(tVtFeedback); b.SideEffect(); },
			[&](const RenderGraph::Resources&) {
				VirtualTexturing::RenderFeedback(_deviceContext.Get(), camera->View(), camera->Proj(), (UINT)viewport.Width, (UINT)viewport.Height, normalDepthSRV, true);
				_deviceContext->RSSetViewports(1, &viewport);
			});
	}

	// 그림자 맵 (Scene 뷰 카메라 기준 캐스케이드 — 깊이 프리패스 뒤: 오클루전 컬링의 Hi-Z 로 캐스터를 거른다)
	g.AddPass("Shadows", [&](RenderGraph::Builder& b) { b.Read(tDepth); tShadows = b.Write(tShadows); },
		[&](const RenderGraph::Resources&) {
			const vector<shared_ptr<Light>> sortedLights = LightManager::GetI()->GetSortedEditorLights();
			ShadowRenderer::Render(_deviceContext.Get(), *shadowMap, sortedLights, (int)dirLights.size(), (int)spotLights.size(), scenePointLights,
				camPos, camera->View(), camera->Proj(), ShadowRenderer::Settings::FromStack(stack), s_EditorShadow,
				[]() {
					SceneCulling::Cull(RenderManager::GetI()->LightViewProjection, true);
					SceneManager::GetI()->GetCurrentScene()->RenderSceneShadow();
				});
			Profiler::SetStat("Scene View/Shadow Cascades Redrawn", s_EditorShadow.CascadesDrawn);
			WeatherCover::Render(_deviceContext.Get(), 1, XMFLOAT3(camPos.x, camPos.y, camPos.z));
			_deviceContext->RSSetState(0);
			_deviceContext->RSSetViewports(1, &viewport);
			SceneCulling::SetEditorView(true);
			SceneCulling::Cull(camera->View() * camera->Proj(), false);   // 빛 컬링 뒤 카메라 컬링을 되돌린다
		});

	// 모션 벡터: Scene 뷰는 Rendering Debugger 의 Motion Vectors 보기만 읽는다 → 아니면 그래프가 뺀다 (Game 뷰와 따로 기록)
	RenderGraph::Texture tVfx;
	if (SceneToolbar::ParticlesVisible() && VfxRuntime::NeedsSimulation())
	{
		tVfx = g.Import("VFX Particles", nullptr);
		g.AddPass("VFX Simulation", [&](RenderGraph::Builder& b) { b.Read(tDepth); tVfx = b.Write(tVfx); if (!VfxRuntime::ReadsSceneTextures()) b.AsyncCompute(); },
			[&](const RenderGraph::Resources&) {
				_deviceContext->RSSetViewports(1, &viewport);
				const ParticleRenderer::Environment env = ParticleEnvironment(viewDsv, dirLights, indirect, SceneToolbar::SkyboxVisible());
				VfxRuntime::SimulateEffects(camera->View(), camera->Proj(), &env);
			});
	}

	g.AddPass("Motion Vectors", [&](RenderGraph::Builder& b) { b.Read(tNormalDepth); tMotion = b.Write(tMotion); },
		[&](const RenderGraph::Resources&) {
			MotionVectors::Frame mf;
			XMStoreFloat4x4(&mf.View, camera->View());
			XMStoreFloat4x4(&mf.Proj, camera->Proj());
			mf.ProjJittered = mf.Proj;
			mf.Width = (UINT)viewport.Width;
			mf.Height = (UINT)viewport.Height;
			mf.NormalDepth = normalDepthSRV;
			mf.SceneView = true;
			MotionVectors::Settings ms = MotionVectors::Settings::FromStack(stack);
			ms.Enabled = true;   // 보려고 켠 것 (Volume 이 꺼도)
			MotionVectors::Render(mf, ms);
			sceneMotion = MotionVectors::SRV(true);
			_deviceContext->RSSetViewports(1, &viewport);
		});

	g.AddPass("SSAO", [&](RenderGraph::Builder& b) { b.Read(tNormalDepth); tAO = b.Write(tAO); },
		[&](const RenderGraph::Resources&) {
			PostProcessingManager::GetI()->_Editor_RenderSSAO(camera, ssaoSettings);
			UseSsaoSettings(ssaoSettings);
		});

	// 이 뷰의 빛 · 그림자 · 프로브 값 (포워드 본 패스 · 디퍼드가 같이 쓴다)
	auto bindFrame = [&]() {
		Effects::InstancedBasicFX->SetEyePosW(camera->GetPosition());
		Effects::InstancedBasicFX->SetCubeMap(_sky->CubeMapSRV().Get());
		Effects::InstancedBasicFX->SetSsaoMap(ssao->AmbientSRV().Get());
		ReflectionProbes::Bind(Effects::InstancedBasicFX.get());
		ProbeVolumes::Bind(Effects::InstancedBasicFX.get());
		ScreenSpaceReflection::Prepare(stack, true, false, normalDepthSRV, camera->View(), camera->Proj());
		ScreenSpaceReflection::Bind(Effects::InstancedBasicFX.get());
		indirect = ApplyIndirectLighting(stack);

		// lights
		Effects::InstancedBasicFX->SetDirLights(dirLights.data(), dirLights.size());
		Effects::InstancedBasicFX->SetSpotLights(spotLights.data(), spotLights.size());
		Effects::InstancedBasicFX->SetPointLights(pointLights.data(), pointLights.size());

		RenderLayers::SetLightMasks(Effects::InstancedBasicFX.get(), scenePointLights, true);
		{
			XMFLOAT4X4 v, p;
			XMStoreFloat4x4(&v, camera->View());
			XMStoreFloat4x4(&p, camera->Proj());
			ClusteredLighting::Build(additionalLights, v, p);
		}
		ClusteredLighting::Bind(Effects::InstancedBasicFX.get());

		// 그림자 맵 / 변환 / 캐스케이드 / 빛별 Strength·필터
		ShadowRenderer::Bind(Effects::InstancedBasicFX.get(), *shadowMap, s_EditorShadow);
		WeatherCover::Bind(Effects::InstancedBasicFX.get(), 1);
		// 패키지 셰이더 이펙트 (CustomShaders) 에도 같은 프레임 상수
		CustomShaders::ForEachEffect([&](InstancedBasicEffect* fx) {
			WeatherCover::Bind(fx, 1);
			fx->SetEyePosW(camera->GetPosition());
			fx->SetCubeMap(_sky->CubeMapSRV().Get());
			fx->SetSsaoMap(ssao->AmbientSRV().Get());
			ReflectionProbes::Bind(fx);
			ProbeVolumes::Bind(fx);
			ScreenSpaceReflection::Bind(fx);
			if (auto* var = fx->GetFX()->GetVariableByName("gIndirect")->AsVector(); var && var->IsValid())
				var->SetFloatVector(&indirect.x);
			SetIndirectGI(fx->GetFX());
			SetSsaoParams(fx->GetFX());
			fx->SetDirLights(dirLights.data(), dirLights.size());
			fx->SetSpotLights(spotLights.data(), spotLights.size());
			fx->SetPointLights(pointLights.data(), pointLights.size());
			ShadowRenderer::Bind(fx, *shadowMap, s_EditorShadow);
			RenderLayers::SetLightMasks(fx, scenePointLights, true);
			ClusteredLighting::Bind(fx);
		});

	};
	auto clearScene = [&]() {
		// Scene 뷰: 투명으로 지운 뒤 SceneViewOverlay 가 뒤에 그린 하늘 그라디언트가 비쳐 보이게 한다.
		const float sceneClear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		_deviceContext->ClearRenderTargetView(sceneTarget, sceneClear);
	};
	const bool deferred = !wire && RenderPipelineSettings::GetRenderingPath() == RenderPipelineSettings::RenderingPath::Deferred &&
		viewDsv == _viewDepthView.Get() && _viewDepthSRV && DeferredRenderer::Available();
	if (deferred)
	{
		RenderGraph::Texture tGBuffer = g.Import("G-Buffer", DeferredRenderer::GBufferSRV(0, 1));
		g.AddPass("GBuffer", [&](RenderGraph::Builder& b) { b.Read(tDepth); b.Read(tNormalDepth); tGBuffer = b.Write(tGBuffer); },
			[&](const RenderGraph::Resources&) {
				GfxRenderTargetView* renderTargets[1] = { sceneTarget };
				_deviceContext->OMSetRenderTargets(1, renderTargets, viewDsv);
				clearScene();
				bindFrame();
				if (DeferredRenderer::BeginGBuffer(_deviceContext.Get(), (UINT)viewport.Width, (UINT)viewport.Height, viewDsv, viewport, 1))
				{
					_deviceContext->OMSetDepthStencilState(RenderStates::EqualsDSS.Get(), 0);
					_deviceContext->IASetInputLayout(InputLayouts::InstancedBasic.Get());
					MeshBatcher::SetDeferredSplit(MeshBatcher::DeferredSplit::GBuffer);
					MeshBatcher::Draw(SceneManager::GetI()->GetCurrentScene(), MeshBatcher::Pass::Main, true);
					MeshBatcher::SetDeferredSplit(MeshBatcher::DeferredSplit::None);
				}
				_deviceContext->RSSetState(0);
				_deviceContext->OMSetDepthStencilState(0, 0);
			});
		g.AddPass("Deferred Lighting", [&](RenderGraph::Builder& b) { b.Read(tGBuffer); b.Read(tDepth); b.Read(tShadows); b.Read(tAO); tScene = b.Write(tScene); },
			[&](const RenderGraph::Resources&) {
				DeferredRenderer::Light(_deviceContext.Get(), sceneTarget, _viewDepthSRV.Get(), camera->View() * camera->Proj(), viewport, 1);
			});
	}

	g.AddPass("Opaque", [&](RenderGraph::Builder& b) { b.Read(tDepth); b.Read(tNormalDepth); b.Read(tShadows); b.Read(tAO); if (deferred) b.Read(tScene); tScene = b.Write(tScene); },
		[&](const RenderGraph::Resources&) {
			GfxRenderTargetView* renderTargets[1] = { sceneTarget };
			_deviceContext->OMSetRenderTargets(1, renderTargets, viewDsv);
			_deviceContext->RSSetViewports(1, &viewport);
			if (!deferred)
			{
				clearScene();
				bindFrame();
			}
			_deviceContext->OMSetDepthStencilState(RenderStates::EqualsDSS.Get(), 0);
			_deviceContext->IASetInputLayout(InputLayouts::InstancedBasic.Get());
			if (wire)
				_deviceContext->RSSetState(RenderStates::WireframeRS.Get());
			MeshBatcher::SetDeferredSplit(deferred ? MeshBatcher::DeferredSplit::ForwardOnly : MeshBatcher::DeferredSplit::None);
			SceneManager::GetI()->GetCurrentScene()->_Editor_RenderScene();
			MeshBatcher::SetDeferredSplit(MeshBatcher::DeferredSplit::None);
			_deviceContext->RSSetState(0);
			_deviceContext->OMSetDepthStencilState(0, 0);
		});

	// 데칼 (Game 뷰와 같은 자리)
	if (!wire)
		g.AddPass("Decals", [&](RenderGraph::Builder& b) { b.Read(tNormalDepth); b.Read(tScene); tScene = b.Write(tScene); },
			[&](const RenderGraph::Resources&) {
				DecalRenderer::Render(_deviceContext.Get(), sceneTarget, viewport, camera->View(), camera->Proj(), normalDepthSRV, true);
			});

	// 스카이박스 (툴바 Effects > Skybox). 꺼져 있으면 SceneViewOverlay 의 그라디언트가 비친다.
	if (SceneToolbar::SkyboxVisible() && !wire)
		g.AddPass("Sky", [&](RenderGraph::Builder& b) { b.Read(tDepth); b.Read(tScene); tScene = b.Write(tScene); },
			[&](const RenderGraph::Resources&) {
				_sky->Draw(_deviceContext.Get(), camera->GetPosition(), camera->View() * camera->Proj());
				_deviceContext->RSSetState(0);
				_deviceContext->OMSetDepthStencilState(0, 0);
			});

	// 안개·대기 (Scene 뷰: 툴바 Effects > Fog 가 켜져 있을 때)
	if (atmosphere.Active())
		g.AddPass("Atmosphere", [&](RenderGraph::Builder& b) { b.Read(tDepth); b.Read(tScene); tScene = b.Write(tScene); },
			[&](const RenderGraph::Resources&) {
				DrawAtmosphere(&atmosphere, camera->View() * camera->Proj(), camera->GetPosition(), sceneTarget, viewDsv, viewport, SceneToolbar::SkyboxVisible());
			});

	// 투명 메시 (Game 뷰와 같은 자리)
	g.AddPass("Transparent", [&](RenderGraph::Builder& b) { b.Read(tDepth); b.Read(tScene); tScene = b.Write(tScene); },
		[&](const RenderGraph::Resources&) {
			// 깊이는 읽기만 (읽기 전용 DSV — 깊이 SRV 와 같이 묶여도 된다)
			GfxDepthStencilView* readDsv = (viewDsv == _viewDepthView.Get() && _viewDepthReadOnly) ? _viewDepthReadOnly.Get() : viewDsv;
			_deviceContext->OMSetRenderTargets(1, &sceneTarget, readDsv);
			_deviceContext->RSSetViewports(1, &viewport);
			MeshBatcher::Draw(SceneManager::GetI()->GetCurrentScene(), MeshBatcher::Pass::Transparent, true);
			_deviceContext->RSSetState(0);
		});

	// 물 (바다·호수·강)
	if (!wire)
		g.AddPass("Water", [&](RenderGraph::Builder& b) { b.Read(tDepth); b.Read(tShadows); b.Read(tScene); tScene = b.Write(tScene); },
			[&](const RenderGraph::Resources&) {
				DrawWater(camera->View(), camera->Proj(), camera->GetPosition(), sceneTarget, viewDsv, viewport, dirLights, SceneToolbar::SkyboxVisible(), shadowMap.get(), &s_EditorShadow, &atmosphere);
			});

	// 다음 프레임 Screen Space Reflection 이 쓸 장면 색 (격자 · 스프라이트 · 입자 전 — 격자 선이 반사에 비치지 않게)
	g.AddPass("SSR History", [&](RenderGraph::Builder& b) { b.Read(tScene); tSsrHistory = b.Write(tSsrHistory); b.SideEffect(); },
		[&](const RenderGraph::Resources&) {
			GfxShaderResourceView* none[128] = { 0 };
			_deviceContext->PSSetShaderResources(0, 128, none);
			ScreenSpaceReflection::StoreHistory(_deviceContext.Get(), sceneTarget, true);
		});

	// 바닥 격자 (툴바 Grid): 불투명 물체·하늘 다음에 깊이 검사하며 → 물체 뒤의 선은 가려진다
	if (SceneToolbar::GridVisible())
		g.AddPass("Grid", [&](RenderGraph::Builder& b) { b.Read(tDepth); b.Read(tScene); tScene = b.Write(tScene); },
			[&](const RenderGraph::Resources&) {
				SceneGrid::Draw(_deviceContext.Get(), camera->View() * camera->Proj(), camera->GetPosition());
			});

	g.AddPass("Sprites", [&](RenderGraph::Builder& b) { b.Read(tDepth); b.Read(tScene); tScene = b.Write(tScene); },
		[&](const RenderGraph::Resources&) {
			_deviceContext->RSSetViewports(1, &viewport);
			SpriteBatch::Render(camera->View(), camera->Proj(), sceneTarget, viewDsv);
		});

	if (SceneToolbar::ParticlesVisible())
		g.AddPass("Particles", [&](RenderGraph::Builder& b) { b.Read(tDepth); b.Read(tVfx); b.Read(tScene); tScene = b.Write(tScene); },
			[&](const RenderGraph::Resources&) {
				const ParticleRenderer::Environment env = ParticleEnvironment(viewDsv, dirLights, indirect, SceneToolbar::SkyboxVisible());
				ParticleRenderer::Render(camera->View(), camera->Proj(), sceneTarget, viewDsv, &env);
				VfxRuntime::Render(camera->View(), camera->Proj(), sceneTarget, viewDsv, &env);
				_deviceContext->RSSetViewports(1, &viewport);
			});

	if (usePost)
		g.AddPass("Post Processing", [&](RenderGraph::Builder& b) { b.Read(tScene); b.Read(tNormalDepth); tOut = b.Write(tOut); },
			[&](const RenderGraph::Resources&) {
				_deviceContext->RSSetState(0);
				_deviceContext->OMSetDepthStencilState(0, 0);
				GfxShaderResourceView* nullSRV[128] = { 0 };
				_deviceContext->PSSetShaderResources(0, 128, nullSRV);
				post.Execute(stack, postOptions, renderTargetView);
				GfxRenderTargetView* outTargets[1] = { renderTargetView };
				_deviceContext->OMSetRenderTargets(1, outTargets, viewDsv);
				_deviceContext->RSSetViewports(1, &viewport);
			});

	// Rendering Debugger: 전체 화면 보기 (후처리 뒤, UI 앞)
	if (debugMode != RenderingDebug::None)
		g.AddPass("Rendering Debugger", [&](RenderGraph::Builder& b) {
				b.Read(tNormalDepth);
				b.Read(tAO);
				if (debugMode == RenderingDebug::MotionVectors)
					b.Read(tMotion);
				b.Read(*out);
				*out = b.Write(*out);
			},
			[&](const RenderGraph::Resources&) {
				RenderingDebug::Inputs dbg;
				dbg.NormalDepth = normalDepthSRV;
				dbg.Ao = ssao->AmbientSRV().Get();
				dbg.Motion = sceneMotion;
				XMStoreFloat4x4(&dbg.View, camera->View());
				dbg.Width = (UINT)viewport.Width;
				dbg.Height = (UINT)viewport.Height;
				dbg.SceneView = true;
				RenderingDebug::Draw(dbg, renderTargetView, viewport);
			});

	// UI 캔버스: Unity 처럼 월드(1 픽셀 = 1 단위)에 놓인 사각형으로 (씬 깊이로 가려짐)
	g.AddPass("UI", [&](RenderGraph::Builder& b) { b.Read(tDepth); b.Read(*out); *out = b.Write(*out); },
		[&](const RenderGraph::Resources&) {
			_deviceContext->RSSetState(0);
			_deviceContext->OMSetDepthStencilState(0, 0);
			GfxShaderResourceView* nullSRV[128] = { 0 };
			_deviceContext->PSSetShaderResources(0, 128, nullSRV);
			GfxRenderTargetView* uiTargets[1] = { renderTargetView };
			_deviceContext->OMSetRenderTargets(1, uiTargets, viewDsv);
			const XMFLOAT3 cp = camera->GetPosition();
			UISystem::RenderSceneView(renderTargetView, (UINT)viewport.Width, (UINT)viewport.Height, camera->View(), camera->Proj(), Vec3(cp.x, cp.y, cp.z));
			_deviceContext->RSSetViewports(1, &viewport);
		});

	g.MarkOutput(*out);
	g.Compile();
	g.Execute();
	if (sceneMotion == nullptr)
		MotionVectors::Invalidate(true);   // 이번 프레임에 그리지 않았다 (지난 결과를 쓰지 않게)
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

