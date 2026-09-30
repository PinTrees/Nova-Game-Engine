#include "pch.h"
#include "04. ShapesDemo.h"
#include "MathHelper.h"
#include "GeometryGenerator.h"


ShapesDemo::ShapesDemo(HINSTANCE hInstance) : App(hInstance)
{
	_mainWindowCaption = L"Shapes Demo";

	_lastMousePos.x = 0;
	_lastMousePos.y = 0;

	XMMATRIX I = ::XMMatrixIdentity();
	::XMStoreFloat4x4(&_gridWorld, I);
	::XMStoreFloat4x4(&_view, I);
	::XMStoreFloat4x4(&_proj, I);

	XMMATRIX boxScale = ::XMMatrixScaling(2.0f, 1.0f, 2.0f);
	XMMATRIX boxOffset = ::XMMatrixTranslation(0.0f, 0.5f, 0.0f);
	::XMStoreFloat4x4(&_boxWorld, ::XMMatrixMultiply(boxScale, boxOffset));

	XMMATRIX centerSphereScale = ::XMMatrixScaling(2.0f, 2.0f, 2.0f);
	XMMATRIX centerSphereOffset = ::XMMatrixTranslation(0.0f, 2.0f, 0.0f);
	::XMStoreFloat4x4(&_centerSphere, ::XMMatrixMultiply(centerSphereScale, centerSphereOffset));

	for (int32 i = 0; i < 5; ++i)
	{
		::XMStoreFloat4x4(&_cylWorld[i * 2 + 0], ::XMMatrixTranslation(-5.0f, 1.5f, -10.0f + i * 5.0f));
		::XMStoreFloat4x4(&_cylWorld[i * 2 + 1], ::XMMatrixTranslation(+5.0f, 1.5f, -10.0f + i * 5.0f));

		::XMStoreFloat4x4(&_sphereWorld[i * 2 + 0], ::XMMatrixTranslation(-5.0f, 3.5f, -10.0f + i * 5.0f));
		::XMStoreFloat4x4(&_sphereWorld[i * 2 + 1], ::XMMatrixTranslation(+5.0f, 3.5f, -10.0f + i * 5.0f));
	}
}

ShapesDemo::~ShapesDemo()
{

}

bool ShapesDemo::Init()
{
	if (!App::Init())
		return false;

	BuildGeometryBuffers();
	BuildFX();
	BuildVertexLayout();

	// Wireframe
	D3D11_RASTERIZER_DESC wireframeDesc;
	ZeroMemory(&wireframeDesc, sizeof(D3D11_RASTERIZER_DESC));
	wireframeDesc.FillMode = D3D11_FILL_WIREFRAME;
	wireframeDesc.CullMode = D3D11_CULL_BACK;
	wireframeDesc.FrontCounterClockwise = false;
	wireframeDesc.DepthClipEnable = true;

	HR(_device->CreateRasterizerState(&wireframeDesc, _wireframeRS.GetAddressOf()));

	return true;
}

void ShapesDemo::OnResize()
{
	App::OnResize();

	XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * XM_PI, AspectRatio(), 1.0f, 1000.0f);
	XMStoreFloat4x4(&_proj, P);
}

void ShapesDemo::UpdateScene(float dt)
{
	// Convert Spherical to Cartesian coordinates.
	float x = _radius * sinf(_phi) * cosf(_theta);
	float z = _radius * sinf(_phi) * sinf(_theta);
	float y = _radius * cosf(_phi);

	// Build the view matrix.
	XMVECTOR pos = XMVectorSet(x, y, z, 1.0f);
	XMVECTOR target = XMVectorZero();
	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	XMMATRIX V = XMMatrixLookAtLH(pos, target, up);
	XMStoreFloat4x4(&_view, V);
}

void ShapesDemo::RenderApplication()
{
	_deviceContext->ClearRenderTargetView(_renderTargetView.Get(), reinterpret_cast<const float*>(&Colors::LightSteelBlue));
	_deviceContext->ClearDepthStencilView(_depthStencilView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

	_deviceContext->IASetInputLayout(_inputLayout.Get());
	_deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	_deviceContext->RSSetState(_wireframeRS.Get());

	uint32 stride = sizeof(Vertex);
	uint32 offset = 0;
	_deviceContext->IASetVertexBuffers(0, 1, _vertedBuffer.GetAddressOf(), &stride, &offset);
	_deviceContext->IASetIndexBuffer(_indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);

	// Set constants
	XMMATRIX view = ::XMLoadFloat4x4(&_view);
	XMMATRIX proj = ::XMLoadFloat4x4(&_proj);
	XMMATRIX viewProj =  view * proj;

	D3DX11_TECHNIQUE_DESC techDesc;
	_tech->GetDesc(&techDesc);

	for (uint32 p = 0; p < techDesc.Passes; ++p)
	{
		// Draw the grid.
		XMMATRIX world = XMLoadFloat4x4(&_gridWorld);
		XMMATRIX wvp = world * viewProj;
		_fxWorldViewProj->SetMatrix(reinterpret_cast<float*>(&(wvp)));
		_tech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());
		_deviceContext->DrawIndexed(_gridIndexCount, _gridIndexOffset, _gridVertexOffset);

		// Draw the box.
		world = XMLoadFloat4x4(&_boxWorld);
		wvp = world * viewProj;
		_fxWorldViewProj->SetMatrix(reinterpret_cast<float*>(&(wvp)));
		_tech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());
		_deviceContext->DrawIndexed(_boxIndexCount, _boxIndexOffset, _boxVertexOffset);

		// Draw center sphere.
		world = XMLoadFloat4x4(&_centerSphere);
		wvp = world * viewProj;
		_fxWorldViewProj->SetMatrix(reinterpret_cast<float*>(&(wvp)));
		_tech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());
		_deviceContext->DrawIndexed(_sphereIndexCount, _sphereIndexOffset, _sphereVertexOffset);

		// Draw the cylinders.
		for (int32 i = 0; i < 10; ++i)
		{
			world = XMLoadFloat4x4(&_cylWorld[i]);
			wvp = world * viewProj;
			_fxWorldViewProj->SetMatrix(reinterpret_cast<float*>(&(wvp)));
			_tech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());
			_deviceContext->DrawIndexed(_cylinderIndexCount, _cylinderIndexOffset, _cylinderVertexOffset);
		}

		// Draw the spheres.
		for (int32 i = 0; i < 10; ++i)
		{
			world = XMLoadFloat4x4(&_sphereWorld[i]);
			wvp = world * viewProj;
			_fxWorldViewProj->SetMatrix(reinterpret_cast<float*>(&(wvp )));
			_tech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());
			_deviceContext->DrawIndexed(_sphereIndexCount, _sphereIndexOffset, _sphereVertexOffset);
		}
	}

	HR(_swapChain->Present(0, 0));
}

void ShapesDemo::OnMouseDown(WPARAM btnState, int x, int y)
{
	_lastMousePos.x = x;
	_lastMousePos.y = y;

	::SetCapture(_hMainWnd);
}

void ShapesDemo::OnMouseUp(WPARAM btnState, int x, int y)
{
	::ReleaseCapture();
}

void ShapesDemo::OnMouseMove(WPARAM btnState, int x, int y)
{
	if ((btnState & MK_LBUTTON) != 0)
	{
		// Make each pixel correspond to a quarter of a degree.
		float dx = XMConvertToRadians(0.25f * static_cast<float>(x - _lastMousePos.x));
		float dy = XMConvertToRadians(0.25f * static_cast<float>(y - _lastMousePos.y));

		// Update angles based on input to orbit camera around box.
		_theta += dx;
		_phi += dy;

		// Restrict the angle mPhi.
		_phi = MathHelper::Clamp(_phi, 0.1f, MathHelper::Pi - 0.1f);
	}
	else if ((btnState & MK_RBUTTON) != 0)
	{
		// Make each pixel correspond to 0.005 unit in the scene.
		float dx = 0.005f * static_cast<float>(x - _lastMousePos.x);
		float dy = 0.005f * static_cast<float>(y - _lastMousePos.y);

		// Update the camera radius based on input.
		_radius += dx - dy;

		// Restrict the radius.
		_radius = MathHelper::Clamp(_radius, 3.0f, 15.0f);
	}

	_lastMousePos.x = x;
	_lastMousePos.y = y;
}

void ShapesDemo::BuildGeometryBuffers()
{
	GeometryGenerator::MeshData box;
	GeometryGenerator::MeshData grid;
	GeometryGenerator::MeshData sphere;
	GeometryGenerator::MeshData cylinder;

	GeometryGenerator geoGen;
	geoGen.CreateBox(1.0f, 1.0f, 1.0f, box);
	geoGen.CreateGrid(20.0f, 30.0f, 60, 40, grid);
	geoGen.CreateSphere(0.5f, 20, 20, sphere);
	//geoGen.CreateGeosphere(0.5f, 2, sphere);
	geoGen.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20, cylinder);

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

	std::vector<Vertex> vertices(totalVertexCount);

	XMFLOAT4 black(0.0f, 0.0f, 0.0f, 1.0f);

	uint32 k = 0;

	for (size_t i = 0; i < box.vertices.size(); ++i, ++k)
	{
		vertices[k].pos = box.vertices[i].position;
		vertices[k].color = black;
	}

	for (size_t i = 0; i < grid.vertices.size(); ++i, ++k)
	{
		vertices[k].pos = grid.vertices[i].position;
		vertices[k].color = black;
	}

	for (size_t i = 0; i < sphere.vertices.size(); ++i, ++k)
	{
		vertices[k].pos = sphere.vertices[i].position;
		vertices[k].color = black;
	}

	for (size_t i = 0; i < cylinder.vertices.size(); ++i, ++k)
	{
		vertices[k].pos = cylinder.vertices[i].position;
		vertices[k].color = black;
	}

	D3D11_BUFFER_DESC vbd;
	vbd.Usage = D3D11_USAGE_IMMUTABLE;
	vbd.ByteWidth = sizeof(Vertex) * totalVertexCount;
	vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vbd.CPUAccessFlags = 0;
	vbd.MiscFlags = 0;
	D3D11_SUBRESOURCE_DATA vinitData;
	vinitData.pSysMem = &vertices[0];
	HR(_device->CreateBuffer(&vbd, &vinitData, _vertedBuffer.GetAddressOf()));

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
	HR(_device->CreateBuffer(&ibd, &iinitData, _indexBuffer.GetAddressOf()));
}

void ShapesDemo::BuildFX()
{
	DWORD shaderFlags = 0;
#if defined( DEBUG ) || defined( _DEBUG )
	shaderFlags |= D3D10_SHADER_DEBUG;
	shaderFlags |= D3D10_SHADER_SKIP_OPTIMIZATION;
	//shaderFlags |= D3D10_SHADER_ENABLE_BACKWARDS_COMPATIBILITY;
	//shaderFlags |= D3D10_SHADER_PACK_MATRIX_ROW_MAJOR;
#endif

	ComPtr<ID3D10Blob> compiledShader = 0;
	ComPtr<ID3D10Blob> compilationMsgs = 0;
	
	HRESULT hr = ::D3DCompileFromFile(L"../Shaders/02. color.fx", 0, D3D_COMPILE_STANDARD_FILE_INCLUDE, 0, "fx_5_0", shaderFlags, 0, compiledShader.GetAddressOf(), compilationMsgs.GetAddressOf());

	// compilationMsgs can store errors or warnings.
	if (FAILED(hr))
	{
		if (compilationMsgs != 0)
			::MessageBoxA(0, (char*)compilationMsgs->GetBufferPointer(), 0, 0);

		assert(false);
	}

	HR(D3DX11CreateEffectFromMemory(compiledShader->GetBufferPointer(), compiledShader->GetBufferSize(), 0, _device.Get(), _fx.GetAddressOf()));

	_tech = _fx->GetTechniqueByName("T0");
	_fxWorldViewProj = _fx->GetVariableByName("gWorldViewProj")->AsMatrix();
}

void ShapesDemo::BuildVertexLayout()
{
	// Create the vertex input layout.
	D3D11_INPUT_ELEMENT_DESC vertexDesc[] =
	{
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}
	};

	// Create the input layout
	D3DX11_PASS_DESC passDesc;
	_tech->GetPassByIndex(0)->GetDesc(&passDesc);

	HR(_device->CreateInputLayout(vertexDesc, 2, passDesc.pIAInputSignature, passDesc.IAInputSignatureSize, _inputLayout.GetAddressOf()));
}