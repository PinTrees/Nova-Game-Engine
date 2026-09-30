#include "pch.h"
#include "16. BasicTriangleTessellation.h"
#include "MathHelper.h"
#include "GeometryGenerator.h"
#include "Effects.h"
#include "Vertex.h"
#include "RenderStates.h"


BasicTriangleTessellation::BasicTriangleTessellation(HINSTANCE hInstance)
	: App(hInstance)
{
	_mainWindowCaption = L"Basic Triangle Tessellation Demo";
	_enable4xMsaa = false;

	mLastMousePos.x = 0;
	mLastMousePos.y = 0;

	XMMATRIX I = XMMatrixIdentity();
	XMStoreFloat4x4(&mView, I);
	XMStoreFloat4x4(&mProj, I);
}

BasicTriangleTessellation::~BasicTriangleTessellation()
{
	_deviceContext->ClearState();

	Effects::DestroyAll();
	InputLayouts::DestroyAll();
	RenderStates::DestroyAll();
}

bool BasicTriangleTessellation::Init()
{
	if (!App::Init())
		return false;

	// Must init Effects first since InputLayouts depend on shader signatures.
	Effects::InitAll(_device, L"../Shaders/10. Basic.fx");
	InputLayouts::InitAll(_device);
	RenderStates::InitAll(_device);

	BuildTriPatchBuffers();

	return true;
}

void BasicTriangleTessellation::OnResize()
{
	App::OnResize();

	XMMATRIX P = ::XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
	::XMStoreFloat4x4(&mProj, P);
}

void BasicTriangleTessellation::UpdateScene(float dt)
{
	// Convert Spherical to Cartesian coordinates.
	float x = mRadius * sinf(mPhi) * cosf(mTheta);
	float z = mRadius * sinf(mPhi) * sinf(mTheta);
	float y = mRadius * cosf(mPhi);

	mEyePosW = XMFLOAT3(x, y, z);

	// Build the view matrix.
	XMVECTOR pos = ::XMVectorSet(x, y, z, 1.0f);
	XMVECTOR target = ::XMVectorZero();
	XMVECTOR up = ::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	XMMATRIX V = ::XMMatrixLookAtLH(pos, target, up);
	::XMStoreFloat4x4(&mView, V);
}

void BasicTriangleTessellation::RenderApplication()
{
	_deviceContext->ClearRenderTargetView(_renderTargetView.Get(), reinterpret_cast<const float*>(&Colors::White));
	_deviceContext->ClearDepthStencilView(_depthStencilView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

	_deviceContext->IASetInputLayout(InputLayouts::Basic32.Get());
	_deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	float blendFactor[] = { 0.0f, 0.0f, 0.0f, 0.0f };

	XMMATRIX view = XMLoadFloat4x4(&mView);
	XMMATRIX proj = XMLoadFloat4x4(&mProj);
	XMMATRIX viewProj = view * proj;

	_deviceContext->IASetInputLayout(InputLayouts::Pos.Get());
	_deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);

	uint32 stride = sizeof(Vertex::Pos);
	uint32 offset = 0;

	// Set per frame constants.
	Effects::TriangleTessellationFX->SetEyePosW(mEyePosW);
	Effects::TriangleTessellationFX->SetFogColor(Colors::Silver);
	Effects::TriangleTessellationFX->SetFogStart(15.0f);
	Effects::TriangleTessellationFX->SetFogRange(175.0f);

	D3DX11_TECHNIQUE_DESC techDesc;
	Effects::TriangleTessellationFX->TessTech->GetDesc(&techDesc);

	for (uint32 p = 0; p < techDesc.Passes; ++p)
	{
		_deviceContext->IASetVertexBuffers(0, 1, mTriPatchVB.GetAddressOf(), &stride, &offset);

		// Set per object constants.
		XMMATRIX world = ::XMMatrixIdentity();
		XMMATRIX worldInvTranspose = MathHelper::InverseTranspose(world);
		XMMATRIX worldViewProj = world * view * proj;

		Effects::TriangleTessellationFX->SetWorld(world);
		Effects::TriangleTessellationFX->SetWorldInvTranspose(worldInvTranspose);
		Effects::TriangleTessellationFX->SetWorldViewProj(worldViewProj);
		Effects::TriangleTessellationFX->SetTexTransform(XMMatrixIdentity());
		//Effects::TriangleTessellationFX->SetMaterial(0);
		Effects::TriangleTessellationFX->SetDiffuseMap(0);

		Effects::TriangleTessellationFX->TessTech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());

		_deviceContext->RSSetState(RenderStates::WireframeRS.Get());
		_deviceContext->Draw(3, 0);
	}

	HR(_swapChain->Present(0, 0));
}

void BasicTriangleTessellation::OnMouseDown(WPARAM btnState, int x, int y)
{
	mLastMousePos.x = x;
	mLastMousePos.y = y;

	SetCapture(_hMainWnd);
}

void BasicTriangleTessellation::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}

void BasicTriangleTessellation::OnMouseMove(WPARAM btnState, int x, int y)
{
	if ((btnState & MK_LBUTTON) != 0)
	{
		// Make each pixel correspond to a quarter of a degree.
		float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
		float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

		// Update angles based on input to orbit camera around box.
		mTheta += dx;
		mPhi += dy;

		// Restrict the angle mPhi.
		mPhi = MathHelper::Clamp(mPhi, 0.1f, MathHelper::Pi - 0.1f);
	}
	else if ((btnState & MK_RBUTTON) != 0)
	{
		float dx = 0.2f * static_cast<float>(x - mLastMousePos.x);
		float dy = 0.2f * static_cast<float>(y - mLastMousePos.y);

		// Update the camera radius based on input.
		mRadius += dx - dy;

		// Restrict the radius.
		mRadius = MathHelper::Clamp(mRadius, 5.0f, 300.0f);
	}

	mLastMousePos.x = x;
	mLastMousePos.y = y;
}

void BasicTriangleTessellation::BuildTriPatchBuffers()
{
	D3D11_BUFFER_DESC vbd;
	vbd.Usage = D3D11_USAGE_IMMUTABLE;
	vbd.ByteWidth = sizeof(XMFLOAT3) * 3;
	vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vbd.CPUAccessFlags = 0;
	vbd.MiscFlags = 0;

	XMFLOAT3 vertices[3] =
	{
		XMFLOAT3(-10.0f, 0.0f, -10.0f),
		XMFLOAT3(0.0f, 0.0f, +10.0f),
		XMFLOAT3(+10.0f, 0.0f, -10.0f),
	};

	D3D11_SUBRESOURCE_DATA vinitData;
	vinitData.pSysMem = vertices;
	HR(_device->CreateBuffer(&vbd, &vinitData, &mTriPatchVB));
}

