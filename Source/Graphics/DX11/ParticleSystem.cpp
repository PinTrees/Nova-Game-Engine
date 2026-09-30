#include "pch.h"
#include "ParticleSystem.h"
#include "Camera.h"
#include "Effects.h"
#include "Vertex.h"

ParticleSystem::ParticleSystem()
{
	_firstRun = true;
	_gameTime = 0.0f;
	_timeStep = 0.0f;
	_age = 0.0f;

	_eyePosW = XMFLOAT3(0.0f, 0.0f, 0.0f);
	_emitPosW = XMFLOAT3(0.0f, 0.0f, 0.0f);
	_emitDirW = XMFLOAT3(0.0f, 1.0f, 0.0f);
}

ParticleSystem::~ParticleSystem()
{

}

float ParticleSystem::GetAge() const
{
	return _age;
}

void ParticleSystem::SetEyePos(const XMFLOAT3& eyePosW)
{
	_eyePosW = eyePosW;
}

void ParticleSystem::SetEmitPos(const XMFLOAT3& emitPosW)
{
	_emitPosW = emitPosW;
}

void ParticleSystem::SetEmitDir(const XMFLOAT3& emitDirW)
{
	_emitDirW = emitDirW;
}

void ParticleSystem::Init(ComPtr<ID3D11Device> device, shared_ptr<ParticleEffect> fx,
	ComPtr<ID3D11ShaderResourceView> texArraySRV,
	ComPtr<ID3D11ShaderResourceView> randomTexSRV,
	uint32 maxParticles)
{
	_maxParticles = maxParticles;

	_fx = fx;

	_texArraySRV = texArraySRV;
	_randomTexSRV = randomTexSRV;

	BuildVB(device);
}

void ParticleSystem::Reset()
{
	_firstRun = true;
	_age = 0.0f;
}

void ParticleSystem::Update(float dt, float gameTime)
{
	_gameTime = gameTime;
	_timeStep = dt;

	_age += dt;
}

void ParticleSystem::Draw(ComPtr<ID3D11DeviceContext> dc, const Camera& cam)
{
	XMMATRIX VP = cam.ViewProj();

	//
	// Set constants.
	//
	_fx->SetViewProj(VP);
	_fx->SetGameTime(_gameTime);
	_fx->SetTimeStep(_timeStep);
	_fx->SetEyePosW(_eyePosW);
	_fx->SetEmitPosW(_emitPosW);
	_fx->SetEmitDirW(_emitDirW);
	_fx->SetTexArray(_texArraySRV.Get());
	_fx->SetRandomTex(_randomTexSRV.Get());

	//
	// Set IA stage.
	//
	dc->IASetInputLayout(InputLayouts::Particle.Get());
	dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);

	uint32 stride = sizeof(Vertex::Particle);
	uint32 offset = 0;

	// On the first pass, use the initialization VB.  Otherwise, use
	// the VB that contains the current particle list.
	if (_firstRun)
		dc->IASetVertexBuffers(0, 1, _initVB.GetAddressOf(), &stride, &offset);
	else
		dc->IASetVertexBuffers(0, 1, _drawVB.GetAddressOf(), &stride, &offset);

	//
	// Draw the current particle list using stream-out only to update them.  
	// The updated vertices are streamed-out to the target VB. 
	//
	dc->SOSetTargets(1, _streamOutVB.GetAddressOf(), &offset);

	D3DX11_TECHNIQUE_DESC techDesc;
	_fx->StreamOutTech->GetDesc(&techDesc);
	for (UINT p = 0; p < techDesc.Passes; ++p)
	{
		_fx->StreamOutTech->GetPassByIndex(p)->Apply(0, dc.Get());

		if (_firstRun)
		{
			dc->Draw(1, 0);
			_firstRun = false;
		}
		else
		{
			dc->DrawAuto();
		}
	}

	// done streaming-out--unbind the vertex buffer
	ID3D11Buffer* bufferArray[1] = { 0 };
	dc->SOSetTargets(1, bufferArray, &offset);

	// ping-pong the vertex buffers
	std::swap(_drawVB, _streamOutVB);

	//
	// Draw the updated particle system we just streamed-out. 
	//
	dc->IASetVertexBuffers(0, 1, _drawVB.GetAddressOf(), &stride, &offset);

	_fx->DrawTech->GetDesc(&techDesc);
	for (uint32 p = 0; p < techDesc.Passes; ++p)
	{
		_fx->DrawTech->GetPassByIndex(p)->Apply(0, dc.Get());

		dc->DrawAuto();
	}
}

void ParticleSystem::BuildVB(ComPtr<ID3D11Device> device)
{
	//
	// Create the buffer to kick-off the particle system.
	//

	D3D11_BUFFER_DESC vbd;
	vbd.Usage = D3D11_USAGE_DEFAULT;
	vbd.ByteWidth = sizeof(Vertex::Particle) * 1;
	vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vbd.CPUAccessFlags = 0;
	vbd.MiscFlags = 0;
	vbd.StructureByteStride = 0;

	// The initial particle emitter has type 0 and age 0.  The rest
	// of the particle attributes do not apply to an emitter.
	Vertex::Particle p;
	ZeroMemory(&p, sizeof(Vertex::Particle));
	p.Age = 0.0f;
	p.Type = 0;

	D3D11_SUBRESOURCE_DATA vinitData;
	vinitData.pSysMem = &p;

	HR(device->CreateBuffer(&vbd, &vinitData, _initVB.GetAddressOf()));

	//
	// Create the ping-pong buffers for stream-out and drawing.
	//
	vbd.ByteWidth = sizeof(Vertex::Particle) * _maxParticles;
	vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_STREAM_OUTPUT;

	HR(device->CreateBuffer(&vbd, 0, _drawVB.GetAddressOf()));
	HR(device->CreateBuffer(&vbd, 0, _streamOutVB.GetAddressOf()));
}