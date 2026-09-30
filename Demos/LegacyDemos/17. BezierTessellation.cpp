#include "pch.h"
#include "17. BezierTessellation.h"
#include "MathHelper.h"
#include "GeometryGenerator.h"
#include "Effects.h"
#include "Vertex.h"
#include "RenderStates.h"


BezierTessellation::BezierTessellation(HINSTANCE hInstance)
	: App(hInstance)
{
	_mainWindowCaption = L"Bezier Surface Demo";
	_enable4xMsaa = false;

	_lastMousePos.x = 0;
	_lastMousePos.y = 0;

	XMMATRIX I = XMMatrixIdentity();
	XMStoreFloat4x4(&_view, I);
	XMStoreFloat4x4(&_proj, I);
}

BezierTessellation::~BezierTessellation()
{
	_deviceContext->ClearState();

	Effects::DestroyAll();
	InputLayouts::DestroyAll();
	RenderStates::DestroyAll();
}

bool BezierTessellation::Init()
{
	if (!App::Init())
		return false;

	// Must init Effects first since InputLayouts depend on shader signatures.
	Effects::InitAll(_device, L"../Shaders/10. Basic.fx");
	InputLayouts::InitAll(_device);
	RenderStates::InitAll(_device);

	BuildQuadPatchBuffer();

	return true;
}

void BezierTessellation::OnResize()
{
	App::OnResize();

	XMMATRIX P = ::XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
	::XMStoreFloat4x4(&_proj, P);
}

void BezierTessellation::UpdateScene(float dt)
{
	// Convert Spherical to Cartesian coordinates.
	float x = _radius * sinf(_phi) * cosf(_theta);
	float z = _radius * sinf(_phi) * sinf(_theta);
	float y = _radius * cosf(_phi);

	_eyePosW = XMFLOAT3(x, y, z);

	// Build the view matrix.
	XMVECTOR pos = ::XMVectorSet(x, y, z, 1.0f);
	XMVECTOR target = ::XMVectorZero();
	XMVECTOR up = ::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	XMMATRIX V = ::XMMatrixLookAtLH(pos, target, up);
	::XMStoreFloat4x4(&_view, V);
}

void BezierTessellation::RenderApplication()
{
	_deviceContext->ClearRenderTargetView(_renderTargetView.Get(), reinterpret_cast<const float*>(&Colors::Silver));
	_deviceContext->ClearDepthStencilView(_depthStencilView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

	_deviceContext->IASetInputLayout(InputLayouts::Basic32.Get());
	_deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	float blendFactor[] = { 0.0f, 0.0f, 0.0f, 0.0f };

	XMMATRIX view = ::XMLoadFloat4x4(&_view);
	XMMATRIX proj = ::XMLoadFloat4x4(&_proj);
	XMMATRIX viewProj = view * proj;

	_deviceContext->IASetInputLayout(InputLayouts::Pos.Get());
	_deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST);

	uint32 stride = sizeof(Vertex::Pos);
	uint32 offset = 0;

	// Set per frame constants.
	Effects::BezierTessellationFX->SetEyePosW(_eyePosW);
	Effects::BezierTessellationFX->SetFogColor(Colors::Silver);
	Effects::BezierTessellationFX->SetFogStart(15.0f);
	Effects::BezierTessellationFX->SetFogRange(175.0f);

	D3DX11_TECHNIQUE_DESC techDesc;
	Effects::BezierTessellationFX->TessTech->GetDesc(&techDesc);

	for (uint32 p = 0; p < techDesc.Passes; ++p)
	{
		_deviceContext->IASetVertexBuffers(0, 1, _quadPatchVB.GetAddressOf(), &stride, &offset);

		// Set per object constants.
		XMMATRIX world = XMMatrixIdentity();
		XMMATRIX worldInvTranspose = MathHelper::InverseTranspose(world);
		XMMATRIX worldViewProj = world * view * proj;

		Effects::BezierTessellationFX->SetWorld(world);
		Effects::BezierTessellationFX->SetWorldInvTranspose(worldInvTranspose);
		Effects::BezierTessellationFX->SetWorldViewProj(worldViewProj);
		Effects::BezierTessellationFX->SetTexTransform(XMMatrixIdentity());
		//Effects::BezierTessellationFX->SetMaterial(0);
		Effects::BezierTessellationFX->SetDiffuseMap(0);

		Effects::BezierTessellationFX->TessTech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());

		_deviceContext->RSSetState(RenderStates::WireframeRS.Get());
		_deviceContext->Draw(4, 0);
	}

	HR(_swapChain->Present(0, 0));
}

void BezierTessellation::OnMouseDown(WPARAM btnState, int32 x, int32 y)
{
	_lastMousePos.x = x;
	_lastMousePos.y = y;

	SetCapture(_hMainWnd);
}

void BezierTessellation::OnMouseUp(WPARAM btnState, int32 x, int32 y)
{
	ReleaseCapture();
}

void BezierTessellation::OnMouseMove(WPARAM btnState, int32 x, int32 y)
{
	if ((btnState & MK_LBUTTON) != 0)
	{
		// Make each pixel correspond to a quarter of a degree.
		float dx = ::XMConvertToRadians(0.25f * static_cast<float>(x - _lastMousePos.x));
		float dy = ::XMConvertToRadians(0.25f * static_cast<float>(y - _lastMousePos.y));

		// Update angles based on input to orbit camera around box.
		_theta += dx;
		_phi += dy;

		// Restrict the angle mPhi.
		_phi = MathHelper::Clamp(_phi, 0.1f, MathHelper::Pi - 0.1f);
	}
	else if ((btnState & MK_RBUTTON) != 0)
	{
		float dx = 0.2f * static_cast<float>(x - _lastMousePos.x);
		float dy = 0.2f * static_cast<float>(y - _lastMousePos.y);

		// Update the camera radius based on input.
		_radius += dx - dy;

		// Restrict the radius.
		_radius = MathHelper::Clamp(_radius, 5.0f, 300.0f);
	}

	_lastMousePos.x = x;
	_lastMousePos.y = y;
}

void BezierTessellation::BuildQuadPatchBuffer()
{
	D3D11_BUFFER_DESC vbd;
	vbd.Usage = D3D11_USAGE_IMMUTABLE;
	vbd.ByteWidth = sizeof(XMFLOAT3) * 4;
	vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vbd.CPUAccessFlags = 0;
	vbd.MiscFlags = 0;

	XMFLOAT3 vertices[4] =
	{
		XMFLOAT3(-10.0f, 0.0f, +10.0f),
		XMFLOAT3(+10.0f, 0.0f, +10.0f),
		XMFLOAT3(-10.0f, 0.0f, -10.0f),
		XMFLOAT3(+10.0f, 0.0f, -10.0f)
	};

	D3D11_SUBRESOURCE_DATA vinitData;
	vinitData.pSysMem = vertices;
	HR(_device->CreateBuffer(&vbd, &vinitData, _quadPatchVB.GetAddressOf()));
}