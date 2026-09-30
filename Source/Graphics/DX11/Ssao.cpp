#include "pch.h"
#include "Ssao.h"
#include "Camera.h"
#include "Effects.h"
#include "Vertex.h"
#include "MathHelper.h"
#include "EditorCamera.h"
#include "random"

Ssao::Ssao()
	:_device(nullptr),
	_deviceContext(nullptr)
{
}

Ssao::Ssao(ComPtr<ID3D11Device> device, ComPtr<ID3D11DeviceContext> dc, int32 width, int32 height, float fovy, float farZ)
	: _device(device), _deviceContext(dc)
{
	OnSize(width, height, fovy, farZ);

	BuildFullScreenQuad();
	BuildOffsetVectors();
	BuildRandomVectorTexture();
}

Ssao::~Ssao()
{

}

ComPtr<ID3D11ShaderResourceView> Ssao::NormalDepthSRV()
{
	return _normalDepthSRV;
}

ComPtr<ID3D11ShaderResourceView> Ssao::AmbientSRV()
{
	return _ambientSRV0;
}

void Ssao::Init(int32 width, int32 height, float fovy, float farZ)
{
	_device = Application::GetI()->GetDevice();
	_deviceContext = Application::GetI()->GetDeviceContext();

	OnSize(width, height, fovy, farZ);

	BuildFullScreenQuad();
	BuildOffsetVectors();
	BuildRandomVectorTexture();
}

void Ssao::OnSize(int32 width, int32 height, float fovy, float farZ)
{
	_renderTargetWidth = width;
	_renderTargetHeight = height;

	// We render to ambient map at half the resolution.
	_ambientMapViewport.TopLeftX = 0.0f;
	_ambientMapViewport.TopLeftY = 0.0f;
	_ambientMapViewport.Width = width / 2.0f;
	_ambientMapViewport.Height = height / 2.0f;
	_ambientMapViewport.MinDepth = 0.0f;
	_ambientMapViewport.MaxDepth = 1.0f;

	BuildFrustumFarCorners(fovy, farZ);
	BuildTextureViews();
}

void Ssao::SetNormalDepthRenderTarget(ComPtr<ID3D11DepthStencilView> dsv)
{
	ID3D11RenderTargetView* renderTargets[1] = { _normalDepthRTV.Get()};
	_deviceContext->OMSetRenderTargets(1, renderTargets, dsv.Get());

	// Clear view space normal to (0,0,-1) and clear depth to be very far away.  
	float clearColor[] = { 0.0f, 0.0f, -1.0f, 1e5f };
	_deviceContext->ClearRenderTargetView(_normalDepthRTV.Get(), clearColor);
}

void Ssao::ComputeSsao(const Camera& camera)
{
	// Bind the ambient map as the render target.  Observe that this pass does not bind 
	// a depth/stencil buffer--it does not need it, and without one, no depth test is
	// performed, which is what we want.
	ID3D11RenderTargetView* renderTargets[1] = { _ambientRTV0.Get() };
	_deviceContext->OMSetRenderTargets(1, renderTargets, 0);
	_deviceContext->ClearRenderTargetView(_ambientRTV0.Get(), reinterpret_cast<const float*>(&Colors::Black));
	_deviceContext->RSSetViewports(1, &_ambientMapViewport);

	// Transform NDC space [-1,+1]^2 to texture space [0,1]^2
	static const XMMATRIX T(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f);

	XMMATRIX P = camera.Proj();
	XMMATRIX PT = XMMatrixMultiply(P, T);

	Effects::SsaoFX->SetViewToTexSpace(PT);
	Effects::SsaoFX->SetOffsetVectors(_offsets);
	Effects::SsaoFX->SetFrustumCorners(_frustumFarCorner);
	Effects::SsaoFX->SetNormalDepthMap(_normalDepthSRV.Get());
	Effects::SsaoFX->SetRandomVecMap(_randomVectorSRV.Get());

	uint32 stride = sizeof(Vertex::Basic32);
	uint32 offset = 0;

	_deviceContext->IASetInputLayout(InputLayouts::Basic32.Get());
	_deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	_deviceContext->IASetVertexBuffers(0, 1, _screenQuadVB.GetAddressOf(), &stride, &offset);
	_deviceContext->IASetIndexBuffer(_screenQuadIB.Get(), DXGI_FORMAT_R16_UINT, 0);

	ComPtr<ID3DX11EffectTechnique> tech = Effects::SsaoFX->SsaoTech;
	D3DX11_TECHNIQUE_DESC techDesc;

	tech->GetDesc(&techDesc);
	for (uint32 p = 0; p < techDesc.Passes; ++p)
	{
		tech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());
		_deviceContext->DrawIndexed(6, 0, 0);
	}
}

void Ssao::ComputeSsao(const EditorCamera& camera)
{
	// Bind the ambient map as the render target.  Observe that this pass does not bind 
	// a depth/stencil buffer--it does not need it, and without one, no depth test is
	// performed, which is what we want.
	ID3D11RenderTargetView* renderTargets[1] = { _ambientRTV0.Get() };
	_deviceContext->OMSetRenderTargets(1, renderTargets, 0);
	_deviceContext->ClearRenderTargetView(_ambientRTV0.Get(), reinterpret_cast<const float*>(&Colors::Black));
	_deviceContext->RSSetViewports(1, &_ambientMapViewport);

	// Transform NDC space [-1,+1]^2 to texture space [0,1]^2
	static const XMMATRIX T(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f);

	XMMATRIX P = camera.Proj(); 
	XMMATRIX PT = XMMatrixMultiply(P, T);

	Effects::SsaoFX->SetViewToTexSpace(PT);
	Effects::SsaoFX->SetOffsetVectors(_offsets);
	Effects::SsaoFX->SetFrustumCorners(_frustumFarCorner);
	Effects::SsaoFX->SetNormalDepthMap(_normalDepthSRV.Get());
	Effects::SsaoFX->SetRandomVecMap(_randomVectorSRV.Get());

	uint32 stride = sizeof(Vertex::Basic32);
	uint32 offset = 0;

	_deviceContext->IASetInputLayout(InputLayouts::Basic32.Get());
	_deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	_deviceContext->IASetVertexBuffers(0, 1, _screenQuadVB.GetAddressOf(), &stride, &offset);
	_deviceContext->IASetIndexBuffer(_screenQuadIB.Get(), DXGI_FORMAT_R16_UINT, 0);

	ComPtr<ID3DX11EffectTechnique> tech = Effects::SsaoFX->SsaoTech;
	D3DX11_TECHNIQUE_DESC techDesc;

	tech->GetDesc(&techDesc);
	for (uint32 p = 0; p < techDesc.Passes; ++p)
	{
		tech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());
		_deviceContext->DrawIndexed(6, 0, 0);
	}
}

void Ssao::BlurAmbientMap(int32 blurCount)
{
	for (int32 i = 0; i < blurCount; ++i)
	{
		// Ping-pong the two ambient map textures as we apply
		// horizontal and vertical blur passes.
		BlurAmbientMap(_ambientSRV0, _ambientRTV1, true);
		BlurAmbientMap(_ambientSRV1, _ambientRTV0, false);
	}
}

void Ssao::BlurAmbientMap(ComPtr<ID3D11ShaderResourceView> inputSRV, ComPtr<ID3D11RenderTargetView> outputRTV, bool horzBlur)
{
	ID3D11RenderTargetView* renderTargets[1] = { outputRTV.Get()};
	_deviceContext->OMSetRenderTargets(1, renderTargets, 0);
	_deviceContext->ClearRenderTargetView(outputRTV.Get(), reinterpret_cast<const float*>(&Colors::Black));
	_deviceContext->RSSetViewports(1, &_ambientMapViewport);

	Effects::SsaoBlurFX->SetTexelWidth(1.0f / _ambientMapViewport.Width);
	Effects::SsaoBlurFX->SetTexelHeight(1.0f / _ambientMapViewport.Height);
	Effects::SsaoBlurFX->SetNormalDepthMap(_normalDepthSRV.Get());
	Effects::SsaoBlurFX->SetInputImage(inputSRV.Get());

	ComPtr<ID3DX11EffectTechnique> tech;
	if (horzBlur)
	{
		tech = Effects::SsaoBlurFX->HorzBlurTech;
	}
	else
	{
		tech = Effects::SsaoBlurFX->VertBlurTech;
	}

	uint32 stride = sizeof(Vertex::Basic32);
	uint32 offset = 0;

	_deviceContext->IASetInputLayout(InputLayouts::Basic32.Get());
	_deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	_deviceContext->IASetVertexBuffers(0, 1, _screenQuadVB.GetAddressOf(), &stride, &offset);
	_deviceContext->IASetIndexBuffer(_screenQuadIB.Get(), DXGI_FORMAT_R16_UINT, 0);

	D3DX11_TECHNIQUE_DESC techDesc;
	tech->GetDesc(&techDesc);

	for (uint32 p = 0; p < techDesc.Passes; ++p)
	{
		tech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());
		_deviceContext->DrawIndexed(6, 0, 0);

		// Unbind the input SRV as it is going to be an output in the next blur.
		Effects::SsaoBlurFX->SetInputImage(0);
		tech->GetPassByIndex(p)->Apply(0, _deviceContext.Get());
	}
}

void Ssao::BuildFrustumFarCorners(float fovy, float farZ)
{
	float aspect = (float)_renderTargetWidth / (float)_renderTargetHeight;

	float halfHeight = farZ * tanf(0.5f * fovy);
	float halfWidth = aspect * halfHeight;

	_frustumFarCorner[0] = XMFLOAT4(-halfWidth, -halfHeight, farZ, 0.0f);
	_frustumFarCorner[1] = XMFLOAT4(-halfWidth, +halfHeight, farZ, 0.0f);
	_frustumFarCorner[2] = XMFLOAT4(+halfWidth, +halfHeight, farZ, 0.0f);
	_frustumFarCorner[3] = XMFLOAT4(+halfWidth, -halfHeight, farZ, 0.0f);
}

void Ssao::BuildFullScreenQuad()
{
	Vertex::Basic32 v[4];

	v[0].pos = XMFLOAT3(-1.0f, -1.0f, 0.0f);
	v[1].pos = XMFLOAT3(-1.0f, +1.0f, 0.0f);
	v[2].pos = XMFLOAT3(+1.0f, +1.0f, 0.0f);
	v[3].pos = XMFLOAT3(+1.0f, -1.0f, 0.0f);

	// Stnre far plane frustum corner indices in Normal.x slot.
	v[0].normal = XMFLOAT3(0.0f, 0.0f, 0.0f);
	v[1].normal = XMFLOAT3(1.0f, 0.0f, 0.0f);
	v[2].normal = XMFLOAT3(2.0f, 0.0f, 0.0f);
	v[3].normal = XMFLOAT3(3.0f, 0.0f, 0.0f);

	v[0].tex = XMFLOAT2(0.0f, 1.0f);
	v[1].tex = XMFLOAT2(0.0f, 0.0f);
	v[2].tex = XMFLOAT2(1.0f, 0.0f);
	v[3].tex = XMFLOAT2(1.0f, 1.0f);

	D3D11_BUFFER_DESC vbd;
	vbd.Usage = D3D11_USAGE_IMMUTABLE;
	vbd.ByteWidth = sizeof(Vertex::Basic32) * 4;
	vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vbd.CPUAccessFlags = 0;
	vbd.MiscFlags = 0;
	vbd.StructureByteStride = 0;

	D3D11_SUBRESOURCE_DATA vinitData;
	vinitData.pSysMem = v;

	HR(_device->CreateBuffer(&vbd, &vinitData, _screenQuadVB.GetAddressOf()));

	USHORT indices[6] =
	{
		0, 1, 2,
		0, 2, 3
	};

	D3D11_BUFFER_DESC ibd;
	ibd.Usage = D3D11_USAGE_IMMUTABLE;
	ibd.ByteWidth = sizeof(USHORT) * 6;
	ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
	ibd.CPUAccessFlags = 0;
	ibd.StructureByteStride = 0;
	ibd.MiscFlags = 0;

	D3D11_SUBRESOURCE_DATA iinitData;
	iinitData.pSysMem = indices;

	HR(_device->CreateBuffer(&ibd, &iinitData, _screenQuadIB.GetAddressOf()));
}

void Ssao::BuildTextureViews()
{
	D3D11_TEXTURE2D_DESC texDesc;
	texDesc.Width = _renderTargetWidth;
	texDesc.Height = _renderTargetHeight;
	texDesc.MipLevels = 1;
	texDesc.ArraySize = 1;
	texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Usage = D3D11_USAGE_DEFAULT;
	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
	texDesc.CPUAccessFlags = 0;
	texDesc.MiscFlags = 0;

	ComPtr<ID3D11Texture2D> normalDepthTex;
	HR(_device->CreateTexture2D(&texDesc, 0, normalDepthTex.GetAddressOf()));
	HR(_device->CreateShaderResourceView(normalDepthTex.Get(), 0, _normalDepthSRV.GetAddressOf()));
	HR(_device->CreateRenderTargetView(normalDepthTex.Get(), 0, _normalDepthRTV.GetAddressOf()));

	// Render ambient map at half resolution.
	texDesc.Width = _renderTargetWidth / 2;
	texDesc.Height = _renderTargetHeight / 2;
	texDesc.Format = DXGI_FORMAT_R16_FLOAT;

	ComPtr<ID3D11Texture2D> ambientTex0;
	HR(_device->CreateTexture2D(&texDesc, 0, ambientTex0.GetAddressOf()));
	HR(_device->CreateShaderResourceView(ambientTex0.Get(), 0, _ambientSRV0.GetAddressOf()));
	HR(_device->CreateRenderTargetView(ambientTex0.Get(), 0, _ambientRTV0.GetAddressOf()));

	ComPtr<ID3D11Texture2D> ambientTex1;
	HR(_device->CreateTexture2D(&texDesc, 0, ambientTex1.GetAddressOf()));
	HR(_device->CreateShaderResourceView(ambientTex1.Get(), 0, _ambientSRV1.GetAddressOf()));
	HR(_device->CreateRenderTargetView(ambientTex1.Get(), 0, _ambientRTV1.GetAddressOf()));
}

void Ssao::BuildRandomVectorTexture()
{
	D3D11_TEXTURE2D_DESC texDesc;
	texDesc.Width = 256;
	texDesc.Height = 256;
	texDesc.MipLevels = 1;
	texDesc.ArraySize = 1;
	texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Usage = D3D11_USAGE_IMMUTABLE;
	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	texDesc.CPUAccessFlags = 0;
	texDesc.MiscFlags = 0;

	D3D11_SUBRESOURCE_DATA initData = { 0 };
	initData.SysMemPitch = 256 * sizeof(Color);

	vector<Color> color(256 * 256);
	std::random_device rd;    // 시스템의 하드웨어 난수 생성기 사용
	std::uniform_real_distribution<> dis(0.0, 1.0);

	for (int32 i = 0; i < 256; ++i)
	{
		for (int32 j = 0; j < 256; ++j)
		{
			std::mt19937 gen(rd());   // Mersenne Twister 엔진을 사용하여 난수 생성기 초기화

			XMFLOAT3 v(dis(gen), dis(gen), dis(gen));

			color[i * 256 + j] = Color(v.x, v.y, v.z, 0.0f);
		}
	}

	initData.pSysMem = color.data();

	ComPtr<ID3D11Texture2D> tex;
	HR(_device->CreateTexture2D(&texDesc, &initData, tex.GetAddressOf()));

	HR(_device->CreateShaderResourceView(tex.Get(), 0, _randomVectorSRV.GetAddressOf()));
}

void Ssao::BuildOffsetVectors()
{
	// Start with 14 uniformly distributed vectors.  We choose the 8 corners of the cube
	// and the 6 center points along each cube face.  We always alternate the points on 
	// opposites sides of the cubes.  This way we still get the vectors spread out even
	// if we choose to use less than 14 samples.

	// 8 cube corners
	_offsets[0] = XMFLOAT4(+1.0f, +1.0f, +1.0f, 0.0f);
	_offsets[1] = XMFLOAT4(-1.0f, -1.0f, -1.0f, 0.0f);

	_offsets[2] = XMFLOAT4(-1.0f, +1.0f, +1.0f, 0.0f);
	_offsets[3] = XMFLOAT4(+1.0f, -1.0f, -1.0f, 0.0f);

	_offsets[4] = XMFLOAT4(+1.0f, +1.0f, -1.0f, 0.0f);
	_offsets[5] = XMFLOAT4(-1.0f, -1.0f, +1.0f, 0.0f);

	_offsets[6] = XMFLOAT4(-1.0f, +1.0f, -1.0f, 0.0f);
	_offsets[7] = XMFLOAT4(+1.0f, -1.0f, +1.0f, 0.0f);

	// 6 centers of cube faces
	_offsets[8] = XMFLOAT4(-1.0f, 0.0f, 0.0f, 0.0f);
	_offsets[9] = XMFLOAT4(+1.0f, 0.0f, 0.0f, 0.0f);

	_offsets[10] = XMFLOAT4(0.0f, -1.0f, 0.0f, 0.0f);
	_offsets[11] = XMFLOAT4(0.0f, +1.0f, 0.0f, 0.0f);

	_offsets[12] = XMFLOAT4(0.0f, 0.0f, -1.0f, 0.0f);
	_offsets[13] = XMFLOAT4(0.0f, 0.0f, +1.0f, 0.0f);

	for (int32 i = 0; i < 14; ++i)
	{
		// Create random lengths in [0.25, 1.0].
		float s = MathHelper::RandF(0.25f, 1.0f);

		XMVECTOR v = s * ::XMVector4Normalize(::XMLoadFloat4(&_offsets[i]));

		::XMStoreFloat4(&_offsets[i], v);
	}
}