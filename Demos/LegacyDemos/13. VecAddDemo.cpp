#include "pch.h"
#include "13. VecAddDemo.h"
#include "MathHelper.h"
#include "GeometryGenerator.h"
#include "Effects.h"
#include "Vertex.h"
#include "RenderStates.h"

VecAddDemo::VecAddDemo(HINSTANCE hInstance) : App(hInstance)
{
	_mainWindowCaption = L"Compute Shader Vec Add Demo";
}

VecAddDemo::~VecAddDemo()
{
	_deviceContext->ClearState();

	Effects::DestroyAll();
	InputLayouts::DestroyAll();
	RenderStates::DestroyAll();
}

bool VecAddDemo::Init()
{
	if (!App::Init())
		return false;

	// Must init Effects first since InputLayouts depend on shader signatures.
	Effects::InitAll(_device, L"../Shaders/10. Basic.fx");
	InputLayouts::InitAll(_device);
	RenderStates::InitAll(_device);

	BuildBuffersAndViews();

	DoComputeWork();

	return true;
}

void VecAddDemo::OnResize()
{
	App::OnResize();
}

void VecAddDemo::UpdateScene(float dt)
{

}

void VecAddDemo::RenderApplication()
{
	_deviceContext->ClearRenderTargetView(_renderTargetView.Get(), reinterpret_cast<const float*>(&Colors::Silver));
	_deviceContext->ClearDepthStencilView(_depthStencilView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

	HR(_swapChain->Present(0, 0));
}

void VecAddDemo::DoComputeWork()
{
	D3DX11_TECHNIQUE_DESC techDesc;

	Effects::VecAddFX->SetInputA(_inputASRV);
	Effects::VecAddFX->SetInputB(_inputBSRV);
	Effects::VecAddFX->SetOutput(_outputUAV);

	Effects::VecAddFX->VecAddTech->GetDesc(&techDesc);
	for (UINT p = 0; p < techDesc.Passes; ++p)
	{
		ID3DX11EffectPass* pass = Effects::VecAddFX->VecAddTech->GetPassByIndex(p);
		pass->Apply(0, _deviceContext.Get());

		_deviceContext->Dispatch(1, 1, 1);
	}

	// Unbind the input textures from the CS for good housekeeping.
	ID3D11ShaderResourceView* nullSRV[1] = { 0 };
	_deviceContext->CSSetShaderResources(0, 1, nullSRV);

	// Unbind output from compute shader (we are going to use this output as an input in the next pass, 
	// and a resource cannot be both an output and input at the same time.
	ID3D11UnorderedAccessView* nullUAV[1] = { 0 };
	_deviceContext->CSSetUnorderedAccessViews(0, 1, nullUAV, 0);

	// Disable compute shader.
	_deviceContext->CSSetShader(0, 0, 0);

	std::ofstream fout("results.txt");

	// Copy the output buffer to system memory.
	_deviceContext->CopyResource(_outputDebugBuffer.Get(), _outputBuffer.Get());

	// Map the data for reading.
	D3D11_MAPPED_SUBRESOURCE mappedData;
	_deviceContext->Map(_outputDebugBuffer.Get(), 0, D3D11_MAP_READ, 0, &mappedData);

	Data* dataView = reinterpret_cast<Data*>(mappedData.pData);

	for (int i = 0; i < _numElements; ++i)
	{
		fout << "(" << dataView[i].v1.x << ", " << dataView[i].v1.y << ", " << dataView[i].v1.z <<
			", " << dataView[i].v2.x << ", " << dataView[i].v2.y << ")" << std::endl;
	}

	_deviceContext->Unmap(_outputDebugBuffer.Get(), 0);

	fout.close();
}

void VecAddDemo::BuildBuffersAndViews()
{
	std::vector<Data> dataA(_numElements);
	std::vector<Data> dataB(_numElements);
	for (int i = 0; i < _numElements; ++i)
	{
		dataA[i].v1 = XMFLOAT3(i, i, i);
		dataA[i].v2 = XMFLOAT2(i, 0);

		dataB[i].v1 = XMFLOAT3(-i, i, 0.0f);
		dataB[i].v2 = XMFLOAT2(0, -i);
	}

	// Create a buffer to be bound as a shader input (D3D11_BIND_SHADER_RESOURCE).
	D3D11_BUFFER_DESC inputDesc;
	inputDesc.Usage = D3D11_USAGE_DEFAULT;
	inputDesc.ByteWidth = sizeof(Data) * _numElements;
	inputDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	inputDesc.CPUAccessFlags = 0;
	inputDesc.StructureByteStride = sizeof(Data);
	inputDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

	D3D11_SUBRESOURCE_DATA vinitDataA;
	vinitDataA.pSysMem = &dataA[0];

	ComPtr<ID3D11Buffer> bufferA;
	HR(_device->CreateBuffer(&inputDesc, &vinitDataA, bufferA.GetAddressOf()));

	D3D11_SUBRESOURCE_DATA vinitDataB;
	vinitDataB.pSysMem = &dataB[0];

	ComPtr<ID3D11Buffer> bufferB;
	HR(_device->CreateBuffer(&inputDesc, &vinitDataB, bufferB.GetAddressOf()));

	// Create a read-write buffer the compute shader can write to (D3D11_BIND_UNORDERED_ACCESS).
	D3D11_BUFFER_DESC outputDesc;
	outputDesc.Usage = D3D11_USAGE_DEFAULT;
	outputDesc.ByteWidth = sizeof(Data) * _numElements;
	outputDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
	outputDesc.CPUAccessFlags = 0;
	outputDesc.StructureByteStride = sizeof(Data);
	outputDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

	HR(_device->CreateBuffer(&outputDesc, 0, _outputBuffer.GetAddressOf()));

	// Create a system memory version of the buffer to read the results back from.
	outputDesc.Usage = D3D11_USAGE_STAGING;
	outputDesc.BindFlags = 0;
	outputDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	HR(_device->CreateBuffer(&outputDesc, 0, _outputDebugBuffer.GetAddressOf()));


	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
	srvDesc.BufferEx.FirstElement = 0;
	srvDesc.BufferEx.Flags = 0;
	srvDesc.BufferEx.NumElements = _numElements;

	_device->CreateShaderResourceView(bufferA.Get(), &srvDesc, _inputASRV.GetAddressOf());
	_device->CreateShaderResourceView(bufferB.Get(), &srvDesc, _inputBSRV.GetAddressOf());


	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc;
	uavDesc.Format = DXGI_FORMAT_UNKNOWN;
	uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	uavDesc.Buffer.FirstElement = 0;
	uavDesc.Buffer.Flags = 0;
	uavDesc.Buffer.NumElements = _numElements;

	_device->CreateUnorderedAccessView(_outputBuffer.Get(), &uavDesc, _outputUAV.GetAddressOf());
}
