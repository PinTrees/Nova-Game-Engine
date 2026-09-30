#include "pch.h"
#include "31. SkinnedMeshDemo.h"
#include "MathHelper.h"
#include "GeometryGenerator.h"
#include "Effects.h"
#include "Vertex.h"
#include "RenderStates.h"
#include "Sky.h"
#include "ShadowMap.h"
#include "Ssao.h"

SkinnedMeshDemo::SkinnedMeshDemo(HINSTANCE hInstance)
	: App(hInstance)
{
	GameObject* gameObject = new GameObject;

	_mainWindowCaption = L"Skinned Mesh Demo";

	_lastMousePos.x = 0;
	_lastMousePos.y = 0;

	//_camera.SetPosition(0.0f, 2.0f, -15.0f);

	// Estimate the scene bounding sphere manually since we know how the scene was constructed.
	// The grid is the "widest object" with a width of 20 and depth of 30.0f, and centered at
	// the world space origin.  In general, you need to loop over every world space vertex
	// position and compute the bounding sphere.
	_sceneBounds.Center = XMFLOAT3(0.0f, 0.0f, 0.0f);
	_sceneBounds.Radius = ::sqrtf(10.0f * 10.0f + 15.0f * 15.0f);

	XMMATRIX I = ::XMMatrixIdentity();
	::XMStoreFloat4x4(&_gridWorld, I);

	XMMATRIX boxScale = ::XMMatrixScaling(3.0f, 1.0f, 3.0f);
	XMMATRIX boxOffset = ::XMMatrixTranslation(0.0f, 0.5f, 0.0f);
	::XMStoreFloat4x4(&_boxWorld, ::XMMatrixMultiply(boxScale, boxOffset));

	XMMATRIX skullScale = ::XMMatrixScaling(0.5f, 0.5f, 0.5f);
	XMMATRIX skullOffset = ::XMMatrixTranslation(0.0f, 1.0f, 0.0f);
	::XMStoreFloat4x4(&_skullWorld, ::XMMatrixMultiply(skullScale, skullOffset));

	for (int32 i = 0; i < 5; ++i)
	{
		::XMStoreFloat4x4(&_cylWorld[i * 2 + 0], ::XMMatrixTranslation(-5.0f, 1.5f, -10.0f + i * 5.0f));
		::XMStoreFloat4x4(&_cylWorld[i * 2 + 1], ::XMMatrixTranslation(+5.0f, 1.5f, -10.0f + i * 5.0f));

		::XMStoreFloat4x4(&_sphereWorld[i * 2 + 0], ::XMMatrixTranslation(-5.0f, 3.5f, -10.0f + i * 5.0f));
		::XMStoreFloat4x4(&_sphereWorld[i * 2 + 1], ::XMMatrixTranslation(+5.0f, 3.5f, -10.0f + i * 5.0f));
	}

	_dirLights[0].Ambient = XMFLOAT4(0.5f, 0.5f, 0.5f, 1.0f);
	_dirLights[0].Diffuse = XMFLOAT4(1.0f, 0.9f, 0.9f, 1.0f);
	_dirLights[0].Specular = XMFLOAT4(0.8f, 0.8f, 0.7f, 1.0f);
	_dirLights[0].Direction = XMFLOAT3(-0.57735f, -0.57735f, 0.57735f);

	// Shadow acne gets worse as we increase the slope of the polygon (from the
	// perspective of the light).
	//mDirLights[0].Direction = XMFLOAT3(5.0f/sqrtf(50.0f), -5.0f/sqrtf(50.0f), 0.0f);
	//mDirLights[0].Direction = XMFLOAT3(10.0f/sqrtf(125.0f), -5.0f/sqrtf(125.0f), 0.0f);
	//mDirLights[0].Direction = XMFLOAT3(10.0f/sqrtf(116.0f), -4.0f/sqrtf(116.0f), 0.0f);
	//mDirLights[0].Direction = XMFLOAT3(10.0f/sqrtf(109.0f), -3.0f/sqrtf(109.0f), 0.0f);

	_dirLights[1].Ambient = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
	_dirLights[1].Diffuse = XMFLOAT4(0.40f, 0.40f, 0.40f, 1.0f);
	_dirLights[1].Specular = XMFLOAT4(0.2f, 0.2f, 0.2f, 1.0f);
	_dirLights[1].Direction = XMFLOAT3(0.707f, -0.707f, 0.0f);

	_dirLights[2].Ambient = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
	_dirLights[2].Diffuse = XMFLOAT4(0.4f, 0.4f, 0.4f, 1.0f);
	_dirLights[2].Specular = XMFLOAT4(0.2f, 0.2f, 0.2f, 1.0f);
	_dirLights[2].Direction = XMFLOAT3(0.0f, 0.0, -1.0f);

	_originalLightDir[0] = _dirLights[0].Direction;
	_originalLightDir[1] = _dirLights[1].Direction;
	_originalLightDir[2] = _dirLights[2].Direction;

	_gridMat.Ambient = XMFLOAT4(0.7f, 0.7f, 0.7f, 1.0f);
	_gridMat.Diffuse = XMFLOAT4(0.6f, 0.6f, 0.6f, 1.0f);
	_gridMat.Specular = XMFLOAT4(0.4f, 0.4f, 0.4f, 16.0f);
	_gridMat.Reflect = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);

	_cylinderMat.Ambient = XMFLOAT4(0.8f, 0.8f, 0.8f, 1.0f);
	_cylinderMat.Diffuse = XMFLOAT4(0.4f, 0.4f, 0.4f, 1.0f);
	_cylinderMat.Specular = XMFLOAT4(1.0f, 1.0f, 1.0f, 32.0f);
	_cylinderMat.Reflect = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);

	_sphereMat.Ambient = XMFLOAT4(0.3f, 0.4f, 0.5f, 1.0f);
	_sphereMat.Diffuse = XMFLOAT4(0.2f, 0.3f, 0.4f, 1.0f);
	_sphereMat.Specular = XMFLOAT4(0.9f, 0.9f, 0.9f, 16.0f);
	_sphereMat.Reflect = XMFLOAT4(0.3f, 0.3f, 0.3f, 1.0f);

	_boxMat.Ambient = XMFLOAT4(0.8f, 0.8f, 0.8f, 1.0f);
	_boxMat.Diffuse = XMFLOAT4(0.4f, 0.4f, 0.4f, 1.0f);
	_boxMat.Specular = XMFLOAT4(0.8f, 0.8f, 0.8f, 16.0f);
	_boxMat.Reflect = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);

	_skullMat.Ambient = XMFLOAT4(0.5f, 0.5f, 0.5f, 1.0f);
	_skullMat.Diffuse = XMFLOAT4(0.2f, 0.2f, 0.2f, 1.0f);
	_skullMat.Specular = XMFLOAT4(0.5f, 0.5f, 0.5f, 16.0f);
	_skullMat.Reflect = XMFLOAT4(0.3f, 0.3f, 0.3f, 1.0f);
}

SkinnedMeshDemo::~SkinnedMeshDemo()
{
	Effects::DestroyAll();
	InputLayouts::DestroyAll();
	RenderStates::DestroyAll();
}

bool SkinnedMeshDemo::Init()
{
	if (!App::Init())
		return false;

	// Must init Effects first since InputLayouts depend on shader signatures.
	Effects::InitAll(_device, L"../Shaders/28. Basic.fx");
	Shaders::InitAll(_device);

	InputLayouts::InitAll(_device);
	RenderStates::InitAll(_device);

	_texMgr.Init(_device);

	_sky = make_shared<Sky>(_device, L"../Resources/Textures/desertcube1024.dds", 5000.0f);
	smap = make_shared<ShadowMap>(_device, SMapSize, SMapSize);

	_camera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
	_ssao = make_shared<Ssao>(_device, _deviceContext, _clientWidth, _clientHeight, _camera.GetFovY(), _camera.GetFarZ());

	_stoneTexSRV = _texMgr.CreateTexture(L"../Resources/Textures/floor.dds");
	_brickTexSRV = _texMgr.CreateTexture(L"../Resources/Textures/bricks.dds");
	_stoneNormalTexSRV = _texMgr.CreateTexture(L"../Resources/Textures/floor_nmap.dds");
	_brickNormalTexSRV = _texMgr.CreateTexture(L"../Resources/Textures/bricks_nmap.dds");

	BuildShapeGeometryBuffers();
	BuildSkullGeometryBuffers();
	BuildScreenQuadGeometryBuffers();

	_characterModel = make_shared<SkinnedModel>
		(_device, _texMgr, "../Resources/Models/soldier.m3d", L"../Resources/Textures/");
	_characterInstance1.Model = _characterModel;
	_characterInstance2.Model = _characterModel;
	_characterInstance1.TimePos = 0.0f;
	_characterInstance2.TimePos = 0.0f;
	_characterInstance1.ClipName = "Take1";
	_characterInstance2.ClipName = "Take1";
	_characterInstance1.FinalTransforms.resize(_characterModel->SkinnedData.BoneCount());
	_characterInstance2.FinalTransforms.resize(_characterModel->SkinnedData.BoneCount());

	// Reflect to change coordinate system from the RHS the data was exported out as.
	XMMATRIX modelScale = ::XMMatrixScaling(0.05f, 0.05f, -0.05f);
	XMMATRIX modelRot = ::XMMatrixRotationY(MathHelper::Pi);
	XMMATRIX modelOffset = ::XMMatrixTranslation(-2.0f, 0.0f, -7.0f);
	::XMStoreFloat4x4(&_characterInstance1.World, modelScale * modelRot * modelOffset);

	modelOffset = ::XMMatrixTranslation(2.0f, 0.0f, -7.0f);
	::XMStoreFloat4x4(&_characterInstance2.World, modelScale * modelRot * modelOffset);

	//model = make_shared<Mesh>(_device, _texMgr, "../Resources/Meshs/Banana.fbx", L"../Resources/Textures/");
	//
	//for (int i = 0; i < 1; ++i)
	//{
	//	MeshInstance rockInstance1;
	//
	//	rockInstance1.Model = model;
	//
	//	modelScale = ::XMMatrixScaling(1.0f, 1.0f, 1.0f);
	//	modelRot = ::XMMatrixRotationY(MathHelper::Pi);
	//	modelOffset = ::XMMatrixTranslation(2.0f, 0.0f, -8.0f);
	//	::XMStoreFloat4x4(&rockInstance1.World, modelScale * modelRot * modelOffset);
	//
	//	_modelInstances.push_back(rockInstance1);
	//}

	return true;
}

void SkinnedMeshDemo::OnResize()
{
	App::OnResize();

	_camera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);

	if (_ssao)
	{
		_ssao->OnSize(_clientWidth, _clientHeight, _camera.GetFovY(), _camera.GetFarZ());
	}
}

void SkinnedMeshDemo::UpdateScene(float dt)
{
	//
	// Control the camera.
	//
	if (GetAsyncKeyState('W') & 0x8000)
		_camera.Walk(10.0f * dt);

	if (GetAsyncKeyState('S') & 0x8000)
		_camera.Walk(-10.0f * dt);

	if (GetAsyncKeyState('A') & 0x8000)
		_camera.Strafe(-10.0f * dt);

	if (GetAsyncKeyState('D') & 0x8000)
		_camera.Strafe(10.0f * dt);

	//
	// Animate the character.
	// 

	_characterInstance1.Update(dt);
	_characterInstance2.Update(dt);

	//
	// Animate the lights (and hence shadows).
	//

	BuildShadowTransform();

	_camera.UpdateViewMatrix();
}

void SkinnedMeshDemo::RenderApplication()
{
	//
	// Render the scene to the shadow map.
	//

	smap->BindDsvAndSetNullRenderTarget(_deviceContext);

	DrawSceneToShadowMap();

	_deviceContext->RSSetState(0);

	//
	// Render the view space normals and depths.  This render target has the
	// same dimensions as the back buffer, so we can use the screen viewport.
	// This render pass is needed to compute the ambient occlusion.
	// Notice that we use the main depth/stencil buffer in this pass.  
	//

	_deviceContext->ClearDepthStencilView(_depthStencilView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
	_deviceContext->RSSetViewports(1, &_viewport);
	_ssao->SetNormalDepthRenderTarget(_depthStencilView.Get());

	DrawSceneToSsaoNormalDepthMap();

	//
	// Now compute the ambient occlusion.
	//

	_ssao->ComputeSsao(_camera);
	_ssao->BlurAmbientMap(2);

	//
	// Restore the back and depth buffer and viewport to the OM stage.
	//
	ID3D11RenderTargetView* renderTargets[1] = { _renderTargetView.Get()};
	_deviceContext->OMSetRenderTargets(1, renderTargets, _depthStencilView.Get());
	_deviceContext->RSSetViewports(1, &_viewport);

	_deviceContext->ClearRenderTargetView(_renderTargetView.Get(), reinterpret_cast<const float*>(&Colors::Silver));

	// We already laid down scene depth to the depth buffer in the Normal/Depth map pass,
	// so we can set the depth comparison test to “EQUALS.? This prevents any overdraw
	// in this rendering pass, as only the nearest visible pixels will pass this depth
	// comparison test.

	_deviceContext->OMSetDepthStencilState(RenderStates::EqualsDSS.Get(), 0);

	XMMATRIX view = _camera.View();
	XMMATRIX proj = _camera.Proj();
	XMMATRIX viewProj = _camera.ViewProj();

	float blendFactor[] = { 0.0f, 0.0f, 0.0f, 0.0f };

	// Set per frame constants.
	Effects::BasicFX->SetDirLights(_dirLights);
	Effects::BasicFX->SetEyePosW(_camera.GetPosition());
	Effects::BasicFX->SetCubeMap(_sky->CubeMapSRV().Get());
	Effects::BasicFX->SetShadowMap(smap->DepthMapSRV().Get());
	Effects::BasicFX->SetSsaoMap(_ssao->AmbientSRV().Get());

	Effects::NormalMapFX->SetDirLights(_dirLights);
	Effects::NormalMapFX->SetEyePosW(_camera.GetPosition());
	Effects::NormalMapFX->SetCubeMap(_sky->CubeMapSRV().Get());
	Effects::NormalMapFX->SetShadowMap(smap->DepthMapSRV().Get());
	Effects::NormalMapFX->SetSsaoMap(_ssao->AmbientSRV().Get());

	Shaders::GetShader<NormalMapSkinnedShader>()->SetDirLights(_dirLights);
	Shaders::GetShader<NormalMapSkinnedShader>()->SetEyePosW(_camera.GetPosition());
	Shaders::GetShader<NormalMapSkinnedShader>()->SetCubeMap(_sky->CubeMapSRV().Get());
	Shaders::GetShader<NormalMapSkinnedShader>()->SetShadowMap(smap->DepthMapSRV().Get());
	Shaders::GetShader<NormalMapSkinnedShader>()->SetSsaoMap(_ssao->AmbientSRV().Get());

	// Figure out which technique to use for different geometry.
	ComPtr<ID3DX11EffectTechnique> activeTech = Shaders::GetShader<NormalMapSkinnedShader>()->Light3TexTech;
	ComPtr<ID3DX11EffectTechnique> activeSphereTech = Effects::BasicFX->Light3ReflectTech;
	ComPtr<ID3DX11EffectTechnique> activeSkullTech = Effects::BasicFX->Light3ReflectTech;
	ComPtr<ID3DX11EffectTechnique> activeMeshTech = Effects::BasicFX->Light3ReflectTech;
	ComPtr<ID3DX11EffectTechnique> activeSkinnedTech = Shaders::GetShader<NormalMapSkinnedShader>()->Light3TexSkinnedTech;

	_deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	XMMATRIX world;
	XMMATRIX worldInvTranspose;
	XMMATRIX worldViewProj;

	// Transform NDC space [-1,+1]^2 to texture space [0,1]^2
	XMMATRIX toTexSpace(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f);

	XMMATRIX shadowTransform = XMLoadFloat4x4(&_shadowTransform);

	//
	// Draw the grid, cylinders, and box without any cubemap reflection.
	// 

	uint32 stride = sizeof(Vertex::PosNormalTexTan2);
	uint32 offset = 0;

	_deviceContext->IASetInputLayout(InputLayouts::PosNormalTexTan2.Get());
	_deviceContext->IASetVertexBuffers(0, 1, _shapesVB.GetAddressOf(), &stride, &offset);
	_deviceContext->IASetIndexBuffer(_shapesIB.Get(), DXGI_FORMAT_R32_UINT, 0);

	if (GetAsyncKeyState('1') & 0x8000)
		_deviceContext->RSSetState(RenderStates::WireframeRS.Get());

	D3DX11_TECHNIQUE_DESC techDesc;
	activeTech->GetDesc(&techDesc);
	for (uint32 p = 0; p < techDesc.Passes; ++p)
	{
		// Draw the grid.
		world = XMLoadFloat4x4(&_gridWorld);
		worldInvTranspose = MathHelper::InverseTranspose(world);
		worldViewProj = world * view * proj;

		Shaders::GetShader<NormalMapSkinnedShader>()->SetWorld(world);
		Shaders::GetShader<NormalMapSkinnedShader>()->SetWorldInvTranspose(worldInvTranspose);
		Shaders::GetShader<NormalMapSkinnedShader>()->SetWorldViewProj(worldViewProj);
		Shaders::GetShader<NormalMapSkinnedShader>()->SetWorldViewProjTex(worldViewProj * toTexSpace);
		Shaders::GetShader<NormalMapSkinnedShader>()->SetShadowTransform(world * shadowTransform);
		Shaders::GetShader<NormalMapSkinnedShader>()->SetTexTransform(::XMMatrixScaling(8.0f, 10.0f, 1.0f));
		Shaders::GetShader<NormalMapSkinnedShader>()->SetMaterial(_gridMat);
		Shaders::GetShader<NormalMapSkinnedShader>()->SetDiffuseMap(_stoneTexSRV.Get());
		Shaders::GetShader<NormalMapSkinnedShader>()->SetNormalMap(_stoneNormalTexSRV.Get());

		activeTech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());
		_deviceContext->DrawIndexed(_gridIndexCount, _gridIndexOffset, _gridVertexOffset);

		// Draw the box.
		world = XMLoadFloat4x4(&_boxWorld);
		worldInvTranspose = MathHelper::InverseTranspose(world);
		worldViewProj = world * view * proj;

		Shaders::GetShader<NormalMapSkinnedShader>()->SetWorld(world);
		Shaders::GetShader<NormalMapSkinnedShader>()->SetWorldInvTranspose(worldInvTranspose);
		Shaders::GetShader<NormalMapSkinnedShader>()->SetWorldViewProj(worldViewProj);
		Shaders::GetShader<NormalMapSkinnedShader>()->SetWorldViewProjTex(worldViewProj * toTexSpace);
		Shaders::GetShader<NormalMapSkinnedShader>()->SetShadowTransform(world * shadowTransform);
		Shaders::GetShader<NormalMapSkinnedShader>()->SetTexTransform(::XMMatrixScaling(2.0f, 1.0f, 1.0f));
		Shaders::GetShader<NormalMapSkinnedShader>()->SetMaterial(_boxMat);
		Shaders::GetShader<NormalMapSkinnedShader>()->SetDiffuseMap(_brickTexSRV.Get());
		Shaders::GetShader<NormalMapSkinnedShader>()->SetNormalMap(_brickNormalTexSRV.Get());

		activeTech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());
		_deviceContext->DrawIndexed(_boxIndexCount, _boxIndexOffset, _boxVertexOffset);

		// Draw the cylinders.
		for (int32 i = 0; i < 10; ++i)
		{
			world = XMLoadFloat4x4(&_cylWorld[i]);
			worldInvTranspose = MathHelper::InverseTranspose(world);
			worldViewProj = world * view * proj;

			Shaders::GetShader<NormalMapSkinnedShader>()->SetWorld(world);
			Shaders::GetShader<NormalMapSkinnedShader>()->SetWorldInvTranspose(worldInvTranspose);
			Shaders::GetShader<NormalMapSkinnedShader>()->SetWorldViewProj(worldViewProj);
			Shaders::GetShader<NormalMapSkinnedShader>()->SetWorldViewProjTex(worldViewProj * toTexSpace);
			Shaders::GetShader<NormalMapSkinnedShader>()->SetShadowTransform(world * shadowTransform);
			Shaders::GetShader<NormalMapSkinnedShader>()->SetTexTransform(::XMMatrixScaling(1.0f, 2.0f, 1.0f));
			Shaders::GetShader<NormalMapSkinnedShader>()->SetMaterial(_cylinderMat);
			Shaders::GetShader<NormalMapSkinnedShader>()->SetDiffuseMap(_brickTexSRV.Get());
			Shaders::GetShader<NormalMapSkinnedShader>()->SetNormalMap(_brickNormalTexSRV.Get());

			activeTech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());
			_deviceContext->DrawIndexed(_cylinderIndexCount, _cylinderIndexOffset, _cylinderVertexOffset);
		}
	}


	//
	// Draw the spheres with cubemap reflection.
	//
	_deviceContext->IASetVertexBuffers(0, 1, _shapesVB.GetAddressOf(), &stride, &offset);
	_deviceContext->IASetIndexBuffer(_shapesIB.Get(), DXGI_FORMAT_R32_UINT, 0);

	activeSphereTech->GetDesc(&techDesc);
	for (uint32 p = 0; p < techDesc.Passes; ++p)
	{
		// Draw the spheres.
		for (int32 i = 0; i < 10; ++i)
		{
			world = XMLoadFloat4x4(&_sphereWorld[i]);
			worldInvTranspose = MathHelper::InverseTranspose(world);
			worldViewProj = world * view * proj;

			Effects::BasicFX->SetWorld(world);
			Effects::BasicFX->SetWorldInvTranspose(worldInvTranspose);
			Effects::BasicFX->SetWorldViewProj(worldViewProj);
			Effects::BasicFX->SetWorldViewProjTex(worldViewProj * toTexSpace);
			Effects::BasicFX->SetShadowTransform(world * shadowTransform);
			Effects::BasicFX->SetTexTransform(XMMatrixIdentity());
			Effects::BasicFX->SetMaterial(_sphereMat);

			activeSphereTech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());
			_deviceContext->DrawIndexed(_sphereIndexCount, _sphereIndexOffset, _sphereVertexOffset);
		}
	}

	stride = sizeof(Vertex::Basic32);
	offset = 0;

	_deviceContext->IASetInputLayout(InputLayouts::Basic32.Get());
	_deviceContext->IASetVertexBuffers(0, 1, _skullVB.GetAddressOf(), &stride, &offset);
	_deviceContext->IASetIndexBuffer(_skullIB.Get(), DXGI_FORMAT_R32_UINT, 0);

	activeSkullTech->GetDesc(&techDesc);
	for (uint32 p = 0; p < techDesc.Passes; ++p)
	{
		// Draw the skull.
		world = XMLoadFloat4x4(&_skullWorld);
		worldInvTranspose = MathHelper::InverseTranspose(world);
		worldViewProj = world * view * proj;

		Effects::BasicFX->SetWorld(world);
		Effects::BasicFX->SetWorldInvTranspose(worldInvTranspose);
		Effects::BasicFX->SetWorldViewProj(worldViewProj);
		Effects::BasicFX->SetWorldViewProjTex(worldViewProj * toTexSpace);
		Effects::BasicFX->SetMaterial(_skullMat);
		Effects::BasicFX->SetShadowTransform(world * shadowTransform);

		activeSkullTech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());
		_deviceContext->DrawIndexed(_skullIndexCount, 0, 0);
	}



	//
	// Draw opaque objects.
	//

	_deviceContext->IASetInputLayout(InputLayouts::PosNormalTexTan2.Get());
	_deviceContext->IASetVertexBuffers(0, 1, _shapesVB.GetAddressOf(), &stride, &offset);
	_deviceContext->IASetIndexBuffer(_shapesIB.Get(), DXGI_FORMAT_R32_UINT, 0);

	activeMeshTech->GetDesc(&techDesc);
	for (uint32 p = 0; p < techDesc.Passes; ++p)
	{
		for (uint32 modelIndex = 0; modelIndex < _modelInstances.size(); ++modelIndex)
		{
			world = XMLoadFloat4x4(&_modelInstances[modelIndex].World);
			worldInvTranspose = MathHelper::InverseTranspose(world);
			worldViewProj = world * view * proj;

			Effects::BasicFX->SetWorld(world);
			Effects::BasicFX->SetWorldInvTranspose(worldInvTranspose);
			Effects::BasicFX->SetWorldViewProj(worldViewProj);
			Effects::BasicFX->SetWorldViewProjTex(worldViewProj * toTexSpace);
			Effects::BasicFX->SetShadowTransform(world * shadowTransform);
			Effects::BasicFX->SetTexTransform(::XMMatrixScaling(1.0f, 1.0f, 1.0f));

			for (uint32 subset = 0; subset < _modelInstances[modelIndex].Model->SubsetCount; ++subset)
			{
				//Effects::BasicFX->SetDiffuseMap(_brickTexSRV.Get());
				//Effects::BasicFX->Set(_modelInstances[modelIndex].Model->NormalMapSRV[subset].Get());

				//_modelInstances[modelIndex].Model->ModelMesh.Draw(_deviceContext, subset);

				Effects::BasicFX->SetMaterial(_sphereMat);

				activeMeshTech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());
				_deviceContext->DrawIndexed(_sphereIndexCount, _sphereIndexOffset, _sphereVertexOffset);
			}
		}
	}





	//
	// Draw the animated characters.
	//

	_deviceContext->IASetInputLayout(InputLayouts::PosNormalTexTanSkinned.Get());

	activeSkinnedTech->GetDesc(&techDesc);
	for (uint32 p = 0; p < techDesc.Passes; ++p)
	{
		// Instance 1

		world = XMLoadFloat4x4(&_characterInstance1.World);
		worldInvTranspose = MathHelper::InverseTranspose(world);
		worldViewProj = world * view * proj;

		Shaders::GetShader<NormalMapSkinnedShader>()->SetWorld(world);
		Shaders::GetShader<NormalMapSkinnedShader>()->SetWorldInvTranspose(worldInvTranspose);
		Shaders::GetShader<NormalMapSkinnedShader>()->SetWorldViewProj(worldViewProj);
		Shaders::GetShader<NormalMapSkinnedShader>()->SetWorldViewProjTex(worldViewProj * toTexSpace);
		Shaders::GetShader<NormalMapSkinnedShader>()->SetShadowTransform(world * shadowTransform);
		Shaders::GetShader<NormalMapSkinnedShader>()->SetTexTransform(::XMMatrixScaling(1.0f, 1.0f, 1.0f));
		Shaders::GetShader<NormalMapSkinnedShader>()->SetBoneTransforms(
			&_characterInstance1.FinalTransforms[0],
			_characterInstance1.FinalTransforms.size());

		for (uint32 subset = 0; subset < _characterInstance1.Model->SubsetCount; ++subset)
		{
			Shaders::GetShader<NormalMapSkinnedShader>()->SetMaterial(_characterInstance1.Model->Mat[subset]);
			Shaders::GetShader<NormalMapSkinnedShader>()->SetDiffuseMap(_characterInstance1.Model->DiffuseMapSRV[subset].Get());
			Shaders::GetShader<NormalMapSkinnedShader>()->SetNormalMap(_characterInstance1.Model->NormalMapSRV[subset].Get());

			activeSkinnedTech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());
			_characterInstance1.Model->ModelMesh.Draw(_deviceContext, subset);
		}

		// Instance 2

		world = XMLoadFloat4x4(&_characterInstance2.World);
		worldInvTranspose = MathHelper::InverseTranspose(world);
		worldViewProj = world * view * proj;

		Shaders::GetShader<NormalMapSkinnedShader>()->SetWorld(world);
		Shaders::GetShader<NormalMapSkinnedShader>()->SetWorldInvTranspose(worldInvTranspose);
		Shaders::GetShader<NormalMapSkinnedShader>()->SetWorldViewProj(worldViewProj);
		Shaders::GetShader<NormalMapSkinnedShader>()->SetWorldViewProjTex(worldViewProj * toTexSpace);
		Shaders::GetShader<NormalMapSkinnedShader>()->SetShadowTransform(world * shadowTransform);
		Shaders::GetShader<NormalMapSkinnedShader>()->SetTexTransform(::XMMatrixScaling(1.0f, 1.0f, 1.0f));
		Shaders::GetShader<NormalMapSkinnedShader>()->SetBoneTransforms(
			&_characterInstance2.FinalTransforms[0],
			_characterInstance2.FinalTransforms.size());

		for (uint32 subset = 0; subset < _characterInstance1.Model->SubsetCount; ++subset)
		{
			Shaders::GetShader<NormalMapSkinnedShader>()->SetMaterial(_characterInstance2.Model->Mat[subset]);
			Shaders::GetShader<NormalMapSkinnedShader>()->SetDiffuseMap(_characterInstance2.Model->DiffuseMapSRV[subset].Get());
			Shaders::GetShader<NormalMapSkinnedShader>()->SetNormalMap(_characterInstance2.Model->NormalMapSRV[subset].Get());

			activeSkinnedTech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());
			_characterInstance2.Model->ModelMesh.Draw(_deviceContext, subset);
		}
	}

	// Turn off wireframe.
	_deviceContext->RSSetState(0);

	// Restore from RenderStates::EqualsDSS
	_deviceContext->OMSetDepthStencilState(0, 0);

	// Debug view SSAO map.
	//DrawScreenQuad(_ssao->AmbientSRV().Get());

	_sky->Draw(_deviceContext, _camera);

	// restore default states, as the SkyFX changes them in the effect file.
	_deviceContext->RSSetState(0);
	_deviceContext->OMSetDepthStencilState(0, 0);

	// Unbind shadow map and AmbientMap as a shader input because we are going to render
	// to it next frame.  These textures can be at any slot, so clear all slots.
	ID3D11ShaderResourceView* nullSRV[16] = { 0 };
	_deviceContext->PSSetShaderResources(0, 16, nullSRV);

	HR(_swapChain->Present(0, 0));
}

void SkinnedMeshDemo::OnMouseDown(WPARAM btnState, int32 x, int32 y)
{
	_lastMousePos.x = x;
	_lastMousePos.y = y;

	SetCapture(_hMainWnd);
}

void SkinnedMeshDemo::OnMouseUp(WPARAM btnState, int32 x, int32 y)
{
	ReleaseCapture();
}

void SkinnedMeshDemo::OnMouseMove(WPARAM btnState, int32 x, int32 y)
{
	if ((btnState & MK_LBUTTON) != 0)
	{
		// Make each pixel correspond to a quarter of a degree.
		float dx = XMConvertToRadians(0.25f * static_cast<float>(x - _lastMousePos.x));
		float dy = XMConvertToRadians(0.25f * static_cast<float>(y - _lastMousePos.y));

		_camera.Pitch(dy);
		_camera.RotateY(dx);
	}

	_lastMousePos.x = x;
	_lastMousePos.y = y;
}

void SkinnedMeshDemo::DrawSceneToSsaoNormalDepthMap()
{
	XMMATRIX view = _camera.View();
	XMMATRIX proj = _camera.Proj();
	XMMATRIX viewProj = XMMatrixMultiply(view, proj);

	ComPtr<ID3DX11EffectTechnique> tech = Effects::SsaoNormalDepthFX->NormalDepthTech;
	ComPtr<ID3DX11EffectTechnique> animatedTech = Effects::SsaoNormalDepthFX->NormalDepthSkinnedTech;

	XMMATRIX world;
	XMMATRIX worldInvTranspose;
	XMMATRIX worldView;
	XMMATRIX worldInvTransposeView;
	XMMATRIX worldViewProj;

	//
	// Draw the grid, cylinders, spheres and box.

	uint32 stride = sizeof(Vertex::PosNormalTexTan2);
	uint32 offset = 0;

	_deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	_deviceContext->IASetInputLayout(InputLayouts::PosNormalTexTan2.Get());
	_deviceContext->IASetVertexBuffers(0, 1, _shapesVB.GetAddressOf(), &stride, &offset);
	_deviceContext->IASetIndexBuffer(_shapesIB.Get(), DXGI_FORMAT_R32_UINT, 0);

	if (GetAsyncKeyState('1') & 0x8000)
		_deviceContext->RSSetState(RenderStates::WireframeRS.Get());

	D3DX11_TECHNIQUE_DESC techDesc;
	tech->GetDesc(&techDesc);

	for (uint32 p = 0; p < techDesc.Passes; ++p)
	{
		// Draw the grid.
		world = XMLoadFloat4x4(&_gridWorld);
		worldInvTranspose = MathHelper::InverseTranspose(world);
		worldView = world * view;
		worldInvTransposeView = worldInvTranspose * view;
		worldViewProj = world * view * proj;

		Effects::SsaoNormalDepthFX->SetWorldView(worldView);
		Effects::SsaoNormalDepthFX->SetWorldInvTransposeView(worldInvTransposeView);
		Effects::SsaoNormalDepthFX->SetWorldViewProj(worldViewProj);
		Effects::SsaoNormalDepthFX->SetTexTransform(::XMMatrixScaling(8.0f, 10.0f, 1.0f));

		tech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());
		_deviceContext->DrawIndexed(_gridIndexCount, _gridIndexOffset, _gridVertexOffset);

		// Draw the box.
		world = XMLoadFloat4x4(&_boxWorld);
		worldInvTranspose = MathHelper::InverseTranspose(world);
		worldView = world * view;
		worldInvTransposeView = worldInvTranspose * view;
		worldViewProj = world * view * proj;

		Effects::SsaoNormalDepthFX->SetWorldView(worldView);
		Effects::SsaoNormalDepthFX->SetWorldInvTransposeView(worldInvTransposeView);
		Effects::SsaoNormalDepthFX->SetWorldViewProj(worldViewProj);
		Effects::SsaoNormalDepthFX->SetTexTransform(::XMMatrixScaling(2.0f, 1.0f, 1.0f));

		tech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());
		_deviceContext->DrawIndexed(_boxIndexCount, _boxIndexOffset, _boxVertexOffset);

		// Draw the cylinders.
		for (int32 i = 0; i < 10; ++i)
		{
			world = XMLoadFloat4x4(&_cylWorld[i]);
			worldInvTranspose = MathHelper::InverseTranspose(world);
			worldView = world * view;
			worldInvTransposeView = worldInvTranspose * view;
			worldViewProj = world * view * proj;

			Effects::SsaoNormalDepthFX->SetWorldView(worldView);
			Effects::SsaoNormalDepthFX->SetWorldInvTransposeView(worldInvTransposeView);
			Effects::SsaoNormalDepthFX->SetWorldViewProj(worldViewProj);
			Effects::SsaoNormalDepthFX->SetTexTransform(::XMMatrixScaling(1.0f, 2.0f, 1.0f));

			tech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());
			_deviceContext->DrawIndexed(_cylinderIndexCount, _cylinderIndexOffset, _cylinderVertexOffset);
		}

		// Draw the spheres.
		for (int32 i = 0; i < 10; ++i)
		{
			world = XMLoadFloat4x4(&_sphereWorld[i]);
			worldInvTranspose = MathHelper::InverseTranspose(world);
			worldView = world * view;
			worldInvTransposeView = worldInvTranspose * view;
			worldViewProj = world * view * proj;

			Effects::SsaoNormalDepthFX->SetWorldView(worldView);
			Effects::SsaoNormalDepthFX->SetWorldInvTransposeView(worldInvTransposeView);
			Effects::SsaoNormalDepthFX->SetWorldViewProj(worldViewProj);
			Effects::SsaoNormalDepthFX->SetTexTransform(XMMatrixIdentity());

			tech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());
			_deviceContext->DrawIndexed(_sphereIndexCount, _sphereIndexOffset, _sphereVertexOffset);
		}
	}

	//
	// Draw the skull.
	//

	stride = sizeof(Vertex::Basic32);
	offset = 0;

	_deviceContext->IASetInputLayout(InputLayouts::Basic32.Get());
	_deviceContext->IASetVertexBuffers(0, 1, _skullVB.GetAddressOf(), &stride, &offset);
	_deviceContext->IASetIndexBuffer(_skullIB.Get(), DXGI_FORMAT_R32_UINT, 0);

	for (uint32 p = 0; p < techDesc.Passes; ++p)
	{
		world = XMLoadFloat4x4(&_skullWorld);
		worldInvTranspose = MathHelper::InverseTranspose(world);
		worldView = world * view;
		worldInvTransposeView = worldInvTranspose * view;
		worldViewProj = world * view * proj;

		Effects::SsaoNormalDepthFX->SetWorldView(worldView);
		Effects::SsaoNormalDepthFX->SetWorldInvTransposeView(worldInvTransposeView);
		Effects::SsaoNormalDepthFX->SetWorldViewProj(worldViewProj);
		Effects::SsaoNormalDepthFX->SetTexTransform(XMMatrixIdentity());

		tech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());
		_deviceContext->DrawIndexed(_skullIndexCount, 0, 0);
	}

	//
	// Draw the animated characters.
	//

	_deviceContext->IASetInputLayout(InputLayouts::PosNormalTexTanSkinned.Get());

	animatedTech->GetDesc(&techDesc);
	for (uint32 p = 0; p < techDesc.Passes; ++p)
	{
		// Instance 1

		world = XMLoadFloat4x4(&_characterInstance1.World);
		worldInvTranspose = MathHelper::InverseTranspose(world);
		worldView = world * view;
		worldInvTransposeView = worldInvTranspose * view;
		worldViewProj = world * view * proj;

		Effects::SsaoNormalDepthFX->SetWorldView(worldView);
		Effects::SsaoNormalDepthFX->SetWorldInvTransposeView(worldInvTransposeView);
		Effects::SsaoNormalDepthFX->SetWorldViewProj(worldViewProj);
		Effects::SsaoNormalDepthFX->SetTexTransform(XMMatrixIdentity());
		Effects::SsaoNormalDepthFX->SetBoneTransforms(
			&_characterInstance1.FinalTransforms[0],
			_characterInstance1.FinalTransforms.size());

		animatedTech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());

		for (uint32 subset = 0; subset < _characterInstance1.Model->SubsetCount; ++subset)
		{
			_characterInstance1.Model->ModelMesh.Draw(_deviceContext, subset);
		}

		// Instance 2

		world = XMLoadFloat4x4(&_characterInstance2.World);
		worldInvTranspose = MathHelper::InverseTranspose(world);
		worldView = world * view;
		worldInvTransposeView = worldInvTranspose * view;
		worldViewProj = world * view * proj;

		Effects::SsaoNormalDepthFX->SetWorldView(worldView);
		Effects::SsaoNormalDepthFX->SetWorldInvTransposeView(worldInvTransposeView);
		Effects::SsaoNormalDepthFX->SetWorldViewProj(worldViewProj);
		Effects::SsaoNormalDepthFX->SetTexTransform(XMMatrixIdentity());
		Effects::SsaoNormalDepthFX->SetBoneTransforms(
			&_characterInstance2.FinalTransforms[0],
			_characterInstance2.FinalTransforms.size());

		animatedTech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());

		for (uint32 subset = 0; subset < _characterInstance2.Model->SubsetCount; ++subset)
		{
			_characterInstance2.Model->ModelMesh.Draw(_deviceContext, subset);
		}
	}

	_deviceContext->RSSetState(0);
}

void SkinnedMeshDemo::DrawSceneToShadowMap()
{
	XMMATRIX view = ::XMLoadFloat4x4(&_lightView);
	XMMATRIX proj = ::XMLoadFloat4x4(&_lightProj);
	XMMATRIX viewProj = ::XMMatrixMultiply(view, proj);

	Effects::BuildShadowMapFX->SetEyePosW(_camera.GetPosition());
	Effects::BuildShadowMapFX->SetViewProj(viewProj);

	// These properties could be set per object if needed.
	Effects::BuildShadowMapFX->SetHeightScale(0.07f);
	Effects::BuildShadowMapFX->SetMaxTessDistance(1.0f);
	Effects::BuildShadowMapFX->SetMinTessDistance(25.0f);
	Effects::BuildShadowMapFX->SetMinTessFactor(1.0f);
	Effects::BuildShadowMapFX->SetMaxTessFactor(5.0f);

	ComPtr<ID3DX11EffectTechnique> smapTech = Effects::BuildShadowMapFX->BuildShadowMapTech;
	ComPtr<ID3DX11EffectTechnique> animatedSmapTech = Effects::BuildShadowMapFX->BuildShadowMapSkinnedTech;

	_deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	XMMATRIX world;
	XMMATRIX worldInvTranspose;
	XMMATRIX worldViewProj;

	//
	// Draw the grid, cylinders, spheres, and box.
	// 

	uint32 stride = sizeof(Vertex::PosNormalTexTan2);
	uint32 offset = 0;

	_deviceContext->IASetInputLayout(InputLayouts::PosNormalTexTan2.Get());
	_deviceContext->IASetVertexBuffers(0, 1, _shapesVB.GetAddressOf(), &stride, &offset);
	_deviceContext->IASetIndexBuffer(_shapesIB.Get(), DXGI_FORMAT_R32_UINT, 0);

	if (GetAsyncKeyState('1') & 0x8000)
		_deviceContext->RSSetState(RenderStates::WireframeRS.Get());

	D3DX11_TECHNIQUE_DESC techDesc;
	smapTech->GetDesc(&techDesc);

	for (uint32 p = 0; p < techDesc.Passes; ++p)
	{
		// Draw the grid.
		world = XMLoadFloat4x4(&_gridWorld);
		worldInvTranspose = MathHelper::InverseTranspose(world);
		worldViewProj = world * view * proj;

		Effects::BuildShadowMapFX->SetWorld(world);
		Effects::BuildShadowMapFX->SetWorldInvTranspose(worldInvTranspose);
		Effects::BuildShadowMapFX->SetWorldViewProj(worldViewProj);
		Effects::BuildShadowMapFX->SetTexTransform(::XMMatrixScaling(8.0f, 10.0f, 1.0f));

		smapTech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());
		_deviceContext->DrawIndexed(_gridIndexCount, _gridIndexOffset, _gridVertexOffset);

		// Draw the box.
		world = XMLoadFloat4x4(&_boxWorld);
		worldInvTranspose = MathHelper::InverseTranspose(world);
		worldViewProj = world * view * proj;

		Effects::BuildShadowMapFX->SetWorld(world);
		Effects::BuildShadowMapFX->SetWorldInvTranspose(worldInvTranspose);
		Effects::BuildShadowMapFX->SetWorldViewProj(worldViewProj);
		Effects::BuildShadowMapFX->SetTexTransform(::XMMatrixScaling(2.0f, 1.0f, 1.0f));

		smapTech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());
		_deviceContext->DrawIndexed(_boxIndexCount, _boxIndexOffset, _boxVertexOffset);

		// Draw the cylinders.
		for (int32 i = 0; i < 10; ++i)
		{
			world = XMLoadFloat4x4(&_cylWorld[i]);
			worldInvTranspose = MathHelper::InverseTranspose(world);
			worldViewProj = world * view * proj;

			Effects::BuildShadowMapFX->SetWorld(world);
			Effects::BuildShadowMapFX->SetWorldInvTranspose(worldInvTranspose);
			Effects::BuildShadowMapFX->SetWorldViewProj(worldViewProj);
			Effects::BuildShadowMapFX->SetTexTransform(::XMMatrixScaling(1.0f, 2.0f, 1.0f));

			smapTech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());
			_deviceContext->DrawIndexed(_cylinderIndexCount, _cylinderIndexOffset, _cylinderVertexOffset);
		}

		// Draw the spheres.
		for (int32 i = 0; i < 10; ++i)
		{
			world = XMLoadFloat4x4(&_sphereWorld[i]);
			worldInvTranspose = MathHelper::InverseTranspose(world);
			worldViewProj = world * view * proj;

			Effects::BuildShadowMapFX->SetWorld(world);
			Effects::BuildShadowMapFX->SetWorldInvTranspose(worldInvTranspose);
			Effects::BuildShadowMapFX->SetWorldViewProj(worldViewProj);
			Effects::BuildShadowMapFX->SetTexTransform(XMMatrixIdentity());

			smapTech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());
			_deviceContext->DrawIndexed(_sphereIndexCount, _sphereIndexOffset, _sphereVertexOffset);
		}
	}

	//
	// Draw the skull.
	//
	stride = sizeof(Vertex::Basic32);
	offset = 0;

	_deviceContext->IASetInputLayout(InputLayouts::Basic32.Get());
	_deviceContext->IASetVertexBuffers(0, 1, _skullVB.GetAddressOf(), &stride, &offset);
	_deviceContext->IASetIndexBuffer(_skullIB.Get(), DXGI_FORMAT_R32_UINT, 0);

	for (uint32 p = 0; p < techDesc.Passes; ++p)
	{
		world = XMLoadFloat4x4(&_skullWorld);
		worldInvTranspose = MathHelper::InverseTranspose(world);
		worldViewProj = world * view * proj;

		Effects::BuildShadowMapFX->SetWorld(world);
		Effects::BuildShadowMapFX->SetWorldInvTranspose(worldInvTranspose);
		Effects::BuildShadowMapFX->SetWorldViewProj(worldViewProj);
		Effects::BuildShadowMapFX->SetTexTransform(XMMatrixIdentity());

		smapTech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());
		_deviceContext->DrawIndexed(_skullIndexCount, 0, 0);
	}


	//
	// Draw opaque objects.
	//
	_deviceContext->IASetInputLayout(InputLayouts::PosNormalTexTan2.Get());

	smapTech->GetDesc(&techDesc);
	for (uint32 p = 0; p < techDesc.Passes; ++p)
	{
		for (uint32 modelIndex = 0; modelIndex < _modelInstances.size(); ++modelIndex)
		{
			world = XMLoadFloat4x4(&_skullWorld);
			worldInvTranspose = MathHelper::InverseTranspose(world);
			worldViewProj = world * view * proj;

			Effects::BuildShadowMapFX->SetWorld(world);
			Effects::BuildShadowMapFX->SetWorldInvTranspose(worldInvTranspose);
			Effects::BuildShadowMapFX->SetWorldViewProj(worldViewProj);
			Effects::BuildShadowMapFX->SetTexTransform(::XMMatrixScaling(1.0f, 1.0f, 1.0f));

			smapTech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());

			for (uint32 subset = 0; subset < _modelInstances[modelIndex].Model->SubsetCount; ++subset)
			{
				_modelInstances[modelIndex].Model->ModelMesh.Draw(_deviceContext, subset);
			}
		}
	} 


	//
	// Draw the animated characters.
	//

	_deviceContext->IASetInputLayout(InputLayouts::PosNormalTexTanSkinned.Get());

	animatedSmapTech->GetDesc(&techDesc);
	for (uint32 p = 0; p < techDesc.Passes; ++p)
	{
		// Instance 1

		world = XMLoadFloat4x4(&_characterInstance1.World);
		worldInvTranspose = MathHelper::InverseTranspose(world);
		worldViewProj = world * view * proj;

		Effects::BuildShadowMapFX->SetWorld(world);
		Effects::BuildShadowMapFX->SetWorldInvTranspose(worldInvTranspose);
		Effects::BuildShadowMapFX->SetWorldViewProj(worldViewProj);
		Effects::BuildShadowMapFX->SetTexTransform(XMMatrixIdentity());
		Effects::BuildShadowMapFX->SetBoneTransforms(
			&_characterInstance1.FinalTransforms[0],
			_characterInstance1.FinalTransforms.size());


		animatedSmapTech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());

		for (uint32 subset = 0; subset < _characterInstance1.Model->SubsetCount; ++subset)
		{
			_characterInstance1.Model->ModelMesh.Draw(_deviceContext, subset);
		}

		// Instance 2

		world = XMLoadFloat4x4(&_characterInstance2.World);
		worldInvTranspose = MathHelper::InverseTranspose(world);
		worldViewProj = world * view * proj;

		Effects::BuildShadowMapFX->SetWorld(world);
		Effects::BuildShadowMapFX->SetWorldInvTranspose(worldInvTranspose);
		Effects::BuildShadowMapFX->SetWorldViewProj(worldViewProj);
		Effects::BuildShadowMapFX->SetTexTransform(XMMatrixIdentity());
		Effects::BuildShadowMapFX->SetBoneTransforms(
			&_characterInstance2.FinalTransforms[0],
			_characterInstance2.FinalTransforms.size());

		animatedSmapTech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());

		for (uint32 subset = 0; subset < _characterInstance1.Model->SubsetCount; ++subset)
		{
			_characterInstance2.Model->ModelMesh.Draw(_deviceContext, subset);
		}
	}

	_deviceContext->RSSetState(0);
}

void SkinnedMeshDemo::DrawScreenQuad(ID3D11ShaderResourceView* srv)
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
		Effects::DebugTexFX->SetTexture(srv);

		tech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());
		_deviceContext->DrawIndexed(6, 0, 0);
	}
}

void SkinnedMeshDemo::BuildShadowTransform()
{
	// Only the first "main" light casts a shadow.
	XMVECTOR lightDir = XMLoadFloat3(&_dirLights[0].Direction);
	XMVECTOR lightPos = -2.0f * _sceneBounds.Radius * lightDir;
	XMVECTOR targetPos = XMLoadFloat3(&_sceneBounds.Center);
	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	XMMATRIX V = XMMatrixLookAtLH(lightPos, targetPos, up);

	// Transform bounding sphere to light space.
	XMFLOAT3 sphereCenterLS;
	XMStoreFloat3(&sphereCenterLS, XMVector3TransformCoord(targetPos, V));

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

void SkinnedMeshDemo::BuildShapeGeometryBuffers()
{
	GeometryGenerator::MeshData box;
	GeometryGenerator::MeshData grid;
	GeometryGenerator::MeshData sphere;
	GeometryGenerator::MeshData cylinder;

	GeometryGenerator geoGen;
	geoGen.CreateBox(1.0f, 1.0f, 1.0f, box);
	geoGen.CreateGrid(20.0f, 30.0f, 50, 40, grid);
	geoGen.CreateSphere(0.5f, 20, 20, sphere);
	geoGen.CreateCylinder(0.5f, 0.5f, 3.0f, 15, 15, cylinder);

	// Cache the vertex offsets to each object in the concatenated vertex buffer.
	_boxVertexOffset = 0;
	_gridVertexOffset = box.vertices.size();
	_sphereVertexOffset = _gridVertexOffset + grid.vertices.size();
	_cylinderVertexOffset = _sphereVertexOffset + sphere.vertices.size();

	// Cache the index count of each object.
	_boxIndexCount = box.indices.size();
	_gridIndexCount = grid.indices.size();
	_sphereIndexCount = sphere.indices.size();
	_cylinderIndexCount = cylinder.indices.size();

	// Cache the starting index for each object in the concatenated index buffer.
	_boxIndexOffset = 0;
	_gridIndexOffset = _boxIndexCount;
	_sphereIndexOffset = _gridIndexOffset + _gridIndexCount;
	_cylinderIndexOffset = _sphereIndexOffset + _sphereIndexCount;

	uint32 totalVertexCount =
		box.vertices.size() +
		grid.vertices.size() +
		sphere.vertices.size() +
		cylinder.vertices.size();

	uint32 totalIndexCount =
		_boxIndexCount +
		_gridIndexCount +
		_sphereIndexCount +
		_cylinderIndexCount;

	//
	// Extract the vertex elements we are interested in and pack the
	// vertices of all the meshes into one vertex buffer.
	//

	std::vector<Vertex::PosNormalTexTan2> vertices(totalVertexCount);

	uint32 k = 0;
	for (size_t i = 0; i < box.vertices.size(); ++i, ++k)
	{
		vertices[k].pos = box.vertices[i].position;
		vertices[k].normal = box.vertices[i].normal;
		vertices[k].tex = box.vertices[i].texC;
		vertices[k].tangentU = XMFLOAT4(
			box.vertices[i].tangentU.x,
			box.vertices[i].tangentU.y,
			box.vertices[i].tangentU.z,
			1.0f);
	}

	for (size_t i = 0; i < grid.vertices.size(); ++i, ++k)
	{
		vertices[k].pos = grid.vertices[i].position;
		vertices[k].normal = grid.vertices[i].normal;
		vertices[k].tex = grid.vertices[i].texC;
		vertices[k].tangentU = XMFLOAT4(
			grid.vertices[i].tangentU.x,
			grid.vertices[i].tangentU.y,
			grid.vertices[i].tangentU.z,
			1.0f);
	}

	for (size_t i = 0; i < sphere.vertices.size(); ++i, ++k)
	{
		vertices[k].pos = sphere.vertices[i].position;
		vertices[k].normal = sphere.vertices[i].normal;
		vertices[k].tex = sphere.vertices[i].texC;
		vertices[k].tangentU = XMFLOAT4(
			sphere.vertices[i].tangentU.x,
			sphere.vertices[i].tangentU.y,
			sphere.vertices[i].tangentU.z,
			1.0f);
	}

	for (size_t i = 0; i < cylinder.vertices.size(); ++i, ++k)
	{
		vertices[k].pos = cylinder.vertices[i].position;
		vertices[k].normal = cylinder.vertices[i].normal;
		vertices[k].tex = cylinder.vertices[i].texC;
		vertices[k].tangentU = XMFLOAT4(
			cylinder.vertices[i].tangentU.x,
			cylinder.vertices[i].tangentU.y,
			cylinder.vertices[i].tangentU.z,
			1.0f);
	}

	D3D11_BUFFER_DESC vbd;
	vbd.Usage = D3D11_USAGE_IMMUTABLE;
	vbd.ByteWidth = sizeof(Vertex::PosNormalTexTan2) * totalVertexCount;
	vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vbd.CPUAccessFlags = 0;
	vbd.MiscFlags = 0;
	D3D11_SUBRESOURCE_DATA vinitData;
	vinitData.pSysMem = &vertices[0];
	HR(_device->CreateBuffer(&vbd, &vinitData, _shapesVB.GetAddressOf()));

	//
	// Pack the indices of all the meshes into one index buffer.
	//

	std::vector<uint32> indices;
	indices.insert(indices.end(), box.indices.begin(), box.indices.end());
	indices.insert(indices.end(), grid.indices.begin(), grid.indices.end());
	indices.insert(indices.end(), sphere.indices.begin(), sphere.indices.end());
	indices.insert(indices.end(), cylinder.indices.begin(), cylinder.indices.end());

	D3D11_BUFFER_DESC ibd;
	ibd.Usage = D3D11_USAGE_IMMUTABLE;
	ibd.ByteWidth = sizeof(uint32) * totalIndexCount;
	ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
	ibd.CPUAccessFlags = 0;
	ibd.MiscFlags = 0;
	D3D11_SUBRESOURCE_DATA iinitData;
	iinitData.pSysMem = &indices[0];
	HR(_device->CreateBuffer(&ibd, &iinitData, _shapesIB.GetAddressOf()));
}

void SkinnedMeshDemo::BuildSkullGeometryBuffers()
{
	std::ifstream fin("../Resources/Models/skull.txt");

	if (!fin)
	{
		MessageBox(0, L"../Resources/Models/skull.txt not found.", 0, 0);
		return;
	}

	uint32 vcount = 0;
	uint32 tcount = 0;
	std::string ignore;

	fin >> ignore >> vcount;
	fin >> ignore >> tcount;
	fin >> ignore >> ignore >> ignore >> ignore;

	std::vector<Vertex::Basic32> vertices(vcount);
	for (uint32 i = 0; i < vcount; ++i)
	{
		fin >> vertices[i].pos.x >> vertices[i].pos.y >> vertices[i].pos.z;
		fin >> vertices[i].normal.x >> vertices[i].normal.y >> vertices[i].normal.z;
	}

	fin >> ignore;
	fin >> ignore;
	fin >> ignore;

	_skullIndexCount = 3 * tcount;
	std::vector<uint32> indices(_skullIndexCount);
	for (uint32 i = 0; i < tcount; ++i)
	{
		fin >> indices[i * 3 + 0] >> indices[i * 3 + 1] >> indices[i * 3 + 2];
	}

	fin.close();

	D3D11_BUFFER_DESC vbd;
	vbd.Usage = D3D11_USAGE_IMMUTABLE;
	vbd.ByteWidth = sizeof(Vertex::Basic32) * vcount;
	vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vbd.CPUAccessFlags = 0;
	vbd.MiscFlags = 0;
	D3D11_SUBRESOURCE_DATA vinitData;
	vinitData.pSysMem = &vertices[0];
	HR(_device->CreateBuffer(&vbd, &vinitData, _skullVB.GetAddressOf()));

	//
	// Pack the indices of all the meshes into one index buffer.
	//

	D3D11_BUFFER_DESC ibd;
	ibd.Usage = D3D11_USAGE_IMMUTABLE;
	ibd.ByteWidth = sizeof(UINT) * _skullIndexCount;
	ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
	ibd.CPUAccessFlags = 0;
	ibd.MiscFlags = 0;
	D3D11_SUBRESOURCE_DATA iinitData;
	iinitData.pSysMem = &indices[0];
	HR(_device->CreateBuffer(&ibd, &iinitData, _skullIB.GetAddressOf()));
}

void SkinnedMeshDemo::BuildScreenQuadGeometryBuffers()
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
