#include "pch.h"
#include "EditorApp.h"
#include "EngineInfo.h"
#include "MathHelper.h"
#include "GeometryGenerator.h"
#include "Effects.h"
#include "ShaderCache.h"
#include "Vertex.h"
#include "RenderStates.h"
#include "Sky.h"
#include "ShadowMap.h"
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
			L"../Shaders/28. Ssao.fx", L"../Shaders/28. SsaoBlur.fx", L"../Shaders/31. NormalMapSkinned.fx" };
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
	_sky = make_shared<Sky>(_device, L"../Resources/Textures/desertcube1024.dds", 5000.0f);
	//_smap = make_shared<ShadowMap>(_device, SMapSize, SMapSize);

	//_camera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
	//_ssao = make_shared<class Ssao>(_device, _deviceContext, _clientWidth, _clientHeight, _camera.GetFovY(), _camera.GetFarZ());

	log << "EditorApp::Init -> BuildScreenQuadGeometryBuffers..." << std::endl; log.flush();
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

void EditorApp::OnSceneRender(ID3D11RenderTargetView* renderTargetView, Camera* camera)
{
	vector<DirectionalLight> dirLights = LightManager::GetI()->GetDirLights();
	vector<PointLight> pointLights = LightManager::GetI()->GetPointLights();
	vector<SpotLight> spotLights = LightManager::GetI()->GetSpotLights();
	//BuildShadowTransform();

	RenderManager::GetI()->RenderingEditorView = false;
	RenderManager::GetI()->CameraViewMatrix = camera->View();
	RenderManager::GetI()->CameraProjectionMatrix = camera->Proj();
	RenderManager::GetI()->CameraViewProjectionMatrix = XMMatrixMultiply(camera->View(), camera->Proj());

	// 와이어프레임 제어처럼 전체 그림자맵 제어도 가능하게
	auto shadowMap = RenderManager::GetI()->BaseShadowMap;
	auto viewport = RenderManager::GetI()->Viewport;
	Effects::BuildShadowMapFX->SetEyePosW(camera->GetPosition());

	// shadowMaps
	vector<shared_ptr<Light>> sortedLights = LightManager::GetI()->GetSortedLights();
	// 정렬된 빛의 수 만큼
	{
		XMMATRIX toTexSpace(
			0.5f, 0.0f, 0.0f, 0.0f,
			0.0f, -0.5f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.5f, 0.5f, 0.0f, 1.0f);

		// Dir
		RenderManager::GetI()->ShadowTransformArray[(uint32)LightType::Directional].clear();
		for (int i = 0; i < dirLights.size(); i++)
		{
			shadowMap->BindDsvAndSetNullRenderTarget(_deviceContext, LightType::Directional, i);

			XMMATRIX VP = sortedLights[i]->GetLightViewProjection(0);

			RenderManager::GetI()->LightViewProjection = VP;
			RenderManager::GetI()->ShadowTransformArray[(uint32)LightType::Directional].push_back(VP * toTexSpace);

			if (RenderManager::GetI()->WireFrameMode)
				_deviceContext->RSSetState(RenderStates::WireframeRS.Get());

			// Draw Scene Objects
			if (sortedLights[i]->CastsShadows())   // Shadow Type = No Shadows 면 비워 둔 깊이맵(=그림자 없음)
				SceneManager::GetI()->GetCurrentScene()->RenderSceneShadow();

			_deviceContext->RSSetState(0);

			_deviceContext->ClearDepthStencilView(_depthStencilView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
			_deviceContext->RSSetViewports(1, &viewport);
		}

		// Spot
		RenderManager::GetI()->ShadowTransformArray[(uint32)LightType::Spot].clear();
		int startIndex = dirLights.size();
		for (int i = 0; i < spotLights.size(); i++)
		{
			shadowMap->BindDsvAndSetNullRenderTarget(_deviceContext, LightType::Spot, i);

			XMMATRIX VP = sortedLights[startIndex + i]->GetLightViewProjection(0);

			RenderManager::GetI()->LightViewProjection = VP;
			RenderManager::GetI()->ShadowTransformArray[(uint32)LightType::Spot].push_back(VP * toTexSpace);

			if (RenderManager::GetI()->WireFrameMode)
				_deviceContext->RSSetState(RenderStates::WireframeRS.Get());

			// Draw Scene Objects
			if (sortedLights[startIndex + i]->CastsShadows())   // Shadow Type = No Shadows 면 비워 둔 깊이맵(=그림자 없음)
				SceneManager::GetI()->GetCurrentScene()->RenderSceneShadow();

			_deviceContext->RSSetState(0);

			_deviceContext->ClearDepthStencilView(_depthStencilView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
			_deviceContext->RSSetViewports(1, &viewport);
		}

		// Point
		RenderManager::GetI()->ShadowTransformArray[(uint32)LightType::Point].clear();
		startIndex = dirLights.size() + spotLights.size();
		for (int i = 0; i < pointLights.size(); i++)
		{
			int shadowIndex = i * 6;
			for (int j = 0; j < 6; j++)
			{
				shadowMap->BindDsvAndSetNullRenderTarget(_deviceContext, LightType::Point, shadowIndex + j);

				XMMATRIX VP = sortedLights[startIndex + i]->GetLightViewProjection(j);

				RenderManager::GetI()->LightViewProjection = VP;
				RenderManager::GetI()->ShadowTransformArray[(uint32)LightType::Point].push_back(VP * toTexSpace);

				if (RenderManager::GetI()->WireFrameMode)
					_deviceContext->RSSetState(RenderStates::WireframeRS.Get());

				// Draw Scene Objects
				if (sortedLights[startIndex + i]->CastsShadows())   // Shadow Type = No Shadows 면 비워 둔 깊이맵(=그림자 없음)
					SceneManager::GetI()->GetCurrentScene()->RenderSceneShadow();

				_deviceContext->RSSetState(0);

				_deviceContext->ClearDepthStencilView(_depthStencilView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
				_deviceContext->RSSetViewports(1, &viewport);
			}
		}
	}

	// PostProcessing - SSAO
	auto ssao = PostProcessingManager::GetI()->GetSSAO();
	ssao->SetNormalDepthRenderTarget(_depthStencilView.Get());

	if (RenderManager::GetI()->WireFrameMode)
		_deviceContext->RSSetState(RenderStates::WireframeRS.Get());

	// Draw Scene Objects
	SceneManager::GetI()->GetCurrentScene()->RenderSceneShadowNormal();

	_deviceContext->RSSetState(0);

	// PostProcessing - SSAO
	PostProcessingManager::GetI()->RenderSSAO(camera);

	ID3D11RenderTargetView* renderTargets[1] = { renderTargetView };
	_deviceContext->OMSetRenderTargets(1, renderTargets, _depthStencilView.Get());
	_deviceContext->RSSetViewports(1, &viewport);
	{
		// Game 뷰: 카메라의 Background Type 이 Solid Color 이면 그 색, 아니면 Unity 기본 카메라 배경색(#314D79)
		float gameClear[4] = { 49.0f / 255.0f, 77.0f / 255.0f, 121.0f / 255.0f, 1.0f };
		if (camera && camera->UsesSolidBackground())
			memcpy(gameClear, camera->GetBackgroundColor(), sizeof(gameClear));
		_deviceContext->ClearRenderTargetView(renderTargetView, gameClear);
	}

	_deviceContext->OMSetDepthStencilState(RenderStates::EqualsDSS.Get(), 0);

	float blendFactor[] = { 0.0f, 0.0f, 0.0f, 0.0f };

	// cbPerFrame
	Effects::InstancedBasicFX->SetEyePosW(camera->GetPosition());
	Effects::InstancedBasicFX->SetCubeMap(_sky->CubeMapSRV().Get());
	Effects::InstancedBasicFX->SetSsaoMap(ssao->AmbientSRV().Get());

	// lights
	Effects::InstancedBasicFX->SetDirLights(dirLights.data(), dirLights.size());
	Effects::InstancedBasicFX->SetSpotLights(spotLights.data(), spotLights.size());
	Effects::InstancedBasicFX->SetPointLights(pointLights.data(), pointLights.size());

	// shadowMaps
	Effects::InstancedBasicFX->SetDirShadowMaps(shadowMap->DepthMapSRVArray(LightType::Directional).data(), dirLights.size());
	Effects::InstancedBasicFX->SetSpotShadowMaps(shadowMap->DepthMapSRVArray(LightType::Spot).data(), spotLights.size());
	Effects::InstancedBasicFX->SetPointShadowMaps(shadowMap->DepthMapSRVArray(LightType::Point).data(), pointLights.size());
	
	// shadowTransforms
	auto& dirShadowTransforms = RenderManager::GetI()->ShadowTransformArray[(uint32)LightType::Directional];
	auto& spotShadowTransforms = RenderManager::GetI()->ShadowTransformArray[(uint32)LightType::Spot];
	auto& pointShadowTransforms = RenderManager::GetI()->ShadowTransformArray[(uint32)LightType::Point];
	Effects::InstancedBasicFX->SetDirShadowTransforms(dirShadowTransforms.data(), dirLights.size());
	Effects::InstancedBasicFX->SetSpotShadowTransforms(spotShadowTransforms.data(), spotLights.size());
	Effects::InstancedBasicFX->SetPointShadowTransforms(pointShadowTransforms.data(), pointLights.size());

	uint32 stride = sizeof(Vertex::PosNormalTexTan);
	uint32 offset = 0;

	_deviceContext->IASetInputLayout(InputLayouts::InstancedBasic.Get());

	SceneManager::GetI()->GetCurrentScene()->RenderScene();

	_deviceContext->RSSetState(0);
	_deviceContext->OMSetDepthStencilState(0, 0);

	//_sky->Draw(_deviceContext, _camera);

	_deviceContext->RSSetState(0);
	_deviceContext->OMSetDepthStencilState(0, 0);

	ID3D11ShaderResourceView* nullSRV[128] = { 0 };
	_deviceContext->PSSetShaderResources(0, 128, nullSRV);
}

void EditorApp::_Editor_OnSceneRender(ID3D11RenderTargetView* renderTargetView, EditorCamera* camera)
{
	vector<DirectionalLight> dirLights = LightManager::GetI()->GetEditorDirLights();
	vector<PointLight> pointLights = LightManager::GetI()->GetEditorPointLights();
	vector<SpotLight> spotLights = LightManager::GetI()->GetEditorSpotLights();

	RenderManager::GetI()->RenderingEditorView = true;
	RenderManager::GetI()->EditorCameraViewMatrix = camera->View();
	RenderManager::GetI()->EditorCameraProjectionMatrix = camera->Proj();
	RenderManager::GetI()->EditorCameraViewProjectionMatrix = XMMatrixMultiply(camera->View(), camera->Proj());

	auto shadowMap = RenderManager::GetI()->EditorShadowMap;
	auto viewport = RenderManager::GetI()->EditorViewport;
	Effects::BuildShadowMapFX->SetEyePosW(camera->GetPosition());

	// shadowMaps
	vector<shared_ptr<Light>> sortedLights = LightManager::GetI()->GetSortedEditorLights();
	// 정렬된 빛의 수 만큼
	{
		XMMATRIX toTexSpace(
			0.5f, 0.0f, 0.0f, 0.0f,
			0.0f, -0.5f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.5f, 0.5f, 0.0f, 1.0f);

		// Dir
		RenderManager::GetI()->EditorShadowTransformArray[(uint32)LightType::Directional].clear();
		for (int i = 0; i < dirLights.size(); i++)
		{
			shadowMap->BindDsvAndSetNullRenderTarget(_deviceContext, LightType::Directional, i);
			XMMATRIX V = sortedLights[i]->GetEditorLightView();
			XMMATRIX VP = sortedLights[i]->GetEditorLightViewProjection(0);

			RenderManager::GetI()->LightViewProjection = VP;
			RenderManager::GetI()->EditorShadowTransformArray[(uint32)LightType::Directional].push_back(VP * toTexSpace);

			if (RenderManager::GetI()->WireFrameMode)
				_deviceContext->RSSetState(RenderStates::WireframeRS.Get());

			// Draw Scene Objects
			if (sortedLights[i]->CastsShadows())   // Shadow Type = No Shadows 면 비워 둔 깊이맵(=그림자 없음)
				SceneManager::GetI()->GetCurrentScene()->RenderSceneShadow();

			_deviceContext->RSSetState(0);

			_deviceContext->ClearDepthStencilView(_depthStencilView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
			_deviceContext->RSSetViewports(1, &viewport);
		}

		// Spot
		RenderManager::GetI()->EditorShadowTransformArray[(uint32)LightType::Spot].clear();
		int startIndex = dirLights.size();
		for (int i = 0; i < spotLights.size(); i++)
		{
			shadowMap->BindDsvAndSetNullRenderTarget(_deviceContext, LightType::Spot, i);

			XMMATRIX VP = sortedLights[startIndex + i]->GetEditorLightViewProjection(0);

			RenderManager::GetI()->LightViewProjection = VP;
			RenderManager::GetI()->EditorShadowTransformArray[(uint32)LightType::Spot].push_back(VP * toTexSpace);

			if (RenderManager::GetI()->WireFrameMode)
				_deviceContext->RSSetState(RenderStates::WireframeRS.Get());

			// Draw Scene Objects
			if (sortedLights[startIndex + i]->CastsShadows())   // Shadow Type = No Shadows 면 비워 둔 깊이맵(=그림자 없음)
				SceneManager::GetI()->GetCurrentScene()->RenderSceneShadow();

			_deviceContext->RSSetState(0);

			_deviceContext->ClearDepthStencilView(_depthStencilView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
			_deviceContext->RSSetViewports(1, &viewport);
		}

		// Point
		RenderManager::GetI()->EditorShadowTransformArray[(uint32)LightType::Point].clear();
		startIndex = dirLights.size() + spotLights.size();
		for (int i = 0; i < pointLights.size(); i++)
		{
			int shadowIndex = i * 6;
			for (int j = 0; j < 6; j++)
			{
				shadowMap->BindDsvAndSetNullRenderTarget(_deviceContext, LightType::Point, shadowIndex + j);

				XMMATRIX VP = sortedLights[startIndex + i]->GetEditorLightViewProjection(j);

				RenderManager::GetI()->LightViewProjection = VP;
				RenderManager::GetI()->EditorShadowTransformArray[(uint32)LightType::Point].push_back(VP * toTexSpace);

				if (RenderManager::GetI()->WireFrameMode)
					_deviceContext->RSSetState(RenderStates::WireframeRS.Get());

				// Draw Scene Objects
				if (sortedLights[startIndex + i]->CastsShadows())   // Shadow Type = No Shadows 면 비워 둔 깊이맵(=그림자 없음)
					SceneManager::GetI()->GetCurrentScene()->RenderSceneShadow();

				_deviceContext->RSSetState(0);

				_deviceContext->ClearDepthStencilView(_depthStencilView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
				_deviceContext->RSSetViewports(1, &viewport);
			}
		}
	}
	
	// PostProcessing - SSAO
	auto ssao = PostProcessingManager::GetI()->_EditorGetSSAO();
	ssao->SetNormalDepthRenderTarget(_depthStencilView.Get());

	if (RenderManager::GetI()->WireFrameMode)
		_deviceContext->RSSetState(RenderStates::WireframeRS.Get());

	// Draw Scene Objects
	SceneManager::GetI()->GetCurrentScene()->_Editor_RenderSceneShadowNormal(); 

	_deviceContext->RSSetState(0);

	// PostProcessing - SSAO
	PostProcessingManager::GetI()->_Editor_RenderSSAO(camera);

	ID3D11RenderTargetView* renderTargets[1] = { renderTargetView };
	_deviceContext->OMSetRenderTargets(1, renderTargets, _depthStencilView.Get());
	_deviceContext->RSSetViewports(1, &viewport);
	{
		// Scene 뷰: 투명으로 지운 뒤 SceneViewOverlay 가 뒤에 그린 하늘 그라디언트가 비쳐 보이게 한다.
		const float sceneClear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		_deviceContext->ClearRenderTargetView(renderTargetView, sceneClear);
	}

	_deviceContext->OMSetDepthStencilState(RenderStates::EqualsDSS.Get(), 0);

	//float blendFactor[] = { 0.0f, 0.0f, 0.0f, 0.0f };

	// cbPerFrame
	Effects::InstancedBasicFX->SetEyePosW(camera->GetPosition());
	Effects::InstancedBasicFX->SetCubeMap(_sky->CubeMapSRV().Get());
	Effects::InstancedBasicFX->SetSsaoMap(ssao->AmbientSRV().Get());

	// lights
	Effects::InstancedBasicFX->SetDirLights(dirLights.data(), dirLights.size());
	Effects::InstancedBasicFX->SetSpotLights(spotLights.data(), spotLights.size());
	Effects::InstancedBasicFX->SetPointLights(pointLights.data(), pointLights.size());

	// shadowMaps
	Effects::InstancedBasicFX->SetDirShadowMaps(shadowMap->DepthMapSRVArray(LightType::Directional).data(), dirLights.size());
	Effects::InstancedBasicFX->SetSpotShadowMaps(shadowMap->DepthMapSRVArray(LightType::Spot).data(), spotLights.size());
	Effects::InstancedBasicFX->SetPointShadowMaps(shadowMap->DepthMapSRVArray(LightType::Point).data(), pointLights.size());
	
	// shadowTransforms
	auto& dirShadowTransforms = RenderManager::GetI()->EditorShadowTransformArray[(uint32)LightType::Directional];
	auto& spotShadowTransforms = RenderManager::GetI()->EditorShadowTransformArray[(uint32)LightType::Spot];
	auto& pointShadowTransforms = RenderManager::GetI()->EditorShadowTransformArray[(uint32)LightType::Point];
	Effects::InstancedBasicFX->SetDirShadowTransforms(dirShadowTransforms.data(), dirLights.size());
	Effects::InstancedBasicFX->SetSpotShadowTransforms(spotShadowTransforms.data(), spotLights.size());
	Effects::InstancedBasicFX->SetPointShadowTransforms(pointShadowTransforms.data(), pointLights.size());

	uint32 stride = sizeof(Vertex::PosNormalTexTan);
	uint32 offset = 0;

	_deviceContext->IASetInputLayout(InputLayouts::InstancedBasic.Get());

	if (RenderManager::GetI()->WireFrameMode)
		_deviceContext->RSSetState(RenderStates::WireframeRS.Get());

	SceneManager::GetI()->GetCurrentScene()->_Editor_RenderScene();

	_deviceContext->RSSetState(0);
	_deviceContext->OMSetDepthStencilState(0, 0);

	//_sky->Draw(_deviceContext, _camera);

	// (디버그) 그림자 맵을 작은 화면으로 표시하던 DrawScreenQuad 는 제거함
	//DrawScreenQuad(ssao->AmbientSRV().Get());
	//DrawScreenQuad(PostProcessingManager::GetI()->_EditorGetSSAO()->GetRandomVectorSRV());

	_deviceContext->RSSetState(0);
	_deviceContext->OMSetDepthStencilState(0, 0);

	ID3D11ShaderResourceView* nullSRV[128] = { 0 };
	_deviceContext->PSSetShaderResources(0, 128, nullSRV);
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
	//ComPtr<ID3DX11EffectTechnique> tech = Effects::SsaoNormalDepthFX->NormalDepthTech;
	//ComPtr<ID3DX11EffectTechnique> alphaClippedTech = Effects::SsaoNormalDepthFX->NormalDepthAlphaClipTech;
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
	//ComPtr<ID3DX11EffectTechnique> tech = Effects::BuildShadowMapFX->BuildShadowMapTech;
	//ComPtr<ID3DX11EffectTechnique> alphaClippedTech = Effects::BuildShadowMapFX->BuildShadowMapAlphaClipTech;
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

void EditorApp::DrawScreenQuad(ComPtr<ID3D11ShaderResourceView> srv)
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

	ComPtr<ID3DX11EffectTechnique> tech = Effects::DebugTexFX->ViewRedTech;
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

