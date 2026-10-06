#include "pch.h"
#include "Sky.h"
#include "WeatherState.h"
#include "DayNightState.h"
#include "MathHelper.h"
#include "GeometryGenerator.h"
#include "Effects.h"
#include "Vertex.h"
#include "Utils.h"

Sky::Sky(ComPtr<GfxDevice> device, const std::wstring& cubemapFilename, float skySphereRadius)
{
	_cubeMapSRV = Utils::LoadTexture(device, cubemapFilename);

	GeometryGenerator::MeshData sphere;
	GeometryGenerator geoGen;
	geoGen.CreateSphere(skySphereRadius, 30, 30, sphere);

	std::vector<XMFLOAT3> vertices(sphere.vertices.size());

	for (size_t i = 0; i < sphere.vertices.size(); ++i)
	{
		vertices[i] = sphere.vertices[i].position;
	}

	D3D11_BUFFER_DESC vbd;
	vbd.Usage = D3D11_USAGE_IMMUTABLE;
	vbd.ByteWidth = sizeof(XMFLOAT3) * vertices.size();
	vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vbd.CPUAccessFlags = 0;
	vbd.MiscFlags = 0;
	vbd.StructureByteStride = 0;

	D3D11_SUBRESOURCE_DATA vinitData;
	vinitData.pSysMem = &vertices[0];

	HR(device->CreateBuffer(&vbd, &vinitData, _vb.GetAddressOf()));

	_indexCount = sphere.indices.size();

	D3D11_BUFFER_DESC ibd;
	ibd.Usage = D3D11_USAGE_IMMUTABLE;
	ibd.ByteWidth = sizeof(USHORT) * _indexCount;
	ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
	ibd.CPUAccessFlags = 0;
	ibd.StructureByteStride = 0;
	ibd.MiscFlags = 0;

	std::vector<USHORT> indices16;
	indices16.assign(sphere.indices.begin(), sphere.indices.end());

	D3D11_SUBRESOURCE_DATA iinitData;
	iinitData.pSysMem = &indices16[0];

	HR(device->CreateBuffer(&ibd, &iinitData, _ib.GetAddressOf()));
}

Sky::~Sky()
{
}

ComPtr<GfxShaderResourceView> Sky::CubeMapSRV()
{
	return _cubeMapSRV;
}

void Sky::Draw(ComPtr<GfxContext> dc, const Camera& camera)
{
	Draw(dc.Get(), camera.GetPosition(), camera.ViewProj());
}

void Sky::Draw(GfxContext* dc, const XMFLOAT3& eyePos, CXMMATRIX viewProj)
{
	// 하늘 구를 눈 위치에 두고, VS 에서 z = w 로 항상 가장 먼 깊이(1)에 그린다
	XMMATRIX T = ::XMMatrixTranslation(eyePos.x, eyePos.y, eyePos.z);

	XMMATRIX WVP = ::XMMatrixMultiply(T, viewProj);

	Effects::SkyFX->SetWorldViewProj(WVP);
	Effects::SkyFX->SetCubeMap(_cubeMapSRV.Get());
	{
		// 날씨 (먹구름 · 번개) — 기본값이면 그대로
		const WeatherState& w = WeatherState::Get();
		const XMFLOAT3 scale = w.SkyScale();
		const float sky[4] = { scale.x, scale.y, scale.z, w.SkyDesaturate };
		FxEffect* fx = Effects::SkyFX->GetFX();
		if (FxVar* v = fx->GetVariableByName("gSkyWeather"); v && v->IsValid()) v->SetRawValue(sky, 0, 16);
		if (FxVar* v = fx->GetVariableByName("gSkyFlash"); v && v->IsValid()) v->SetRawValue(&w.Flash, 0, 4);
		// 낮 · 밤 순환 (DayNightState): 그라데이션 · 해 쪽 빛 · 해 · 달 · 별 · 은하수. 꺼져 있으면 (w = 0) 예전 그대로
		const DayNightState& d = DayNightState::Get();
		const float cycleSun[4] = { d.SunDirection.x, d.SunDirection.y, d.SunDirection.z, d.Enabled ? 1.0f : 0.0f };
		const float zenith[4] = { d.Zenith.x, d.Zenith.y, d.Zenith.z, d.Enabled ? d.Zenith.w : 0.0f };
		const float horizon[4] = { d.Horizon.x, d.Horizon.y, d.Horizon.z, 0.0f };
		const float glow[4] = { d.Glow.x, d.Glow.y, d.Glow.z, d.Enabled ? d.Glow.w : 0.0f };
		const float disk[4] = { d.SunDisk.x, d.SunDisk.y, d.SunDisk.z, d.Enabled ? d.SunDisk.w : 0.0f };
		const float night[4] = { d.Enabled ? d.Stars : 0.0f, d.Enabled ? d.MilkyWay : 0.0f, d.Time, d.Enabled ? d.Moon : 0.0f };
		auto set = [fx](const char* name, const float* v4) { if (FxVar* v = fx->GetVariableByName(name); v && v->IsValid()) v->SetRawValue(v4, 0, 16); };
		set("gCycleSun", cycleSun);
		set("gCycleZenith", zenith);
		set("gCycleHorizon", horizon);
		set("gCycleGlow", glow);
		set("gCycleSunDisk", disk);
		set("gCycleNight", night);
	}

	uint32 stride = sizeof(XMFLOAT3);
	uint32 offset = 0;
	dc->IASetVertexBuffers(0, 1, _vb.GetAddressOf(), &stride, &offset);
	dc->IASetIndexBuffer(_ib.Get(), DXGI_FORMAT_R16_UINT, 0);
	dc->IASetInputLayout(InputLayouts::Pos.Get());
	dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	D3DX11_TECHNIQUE_DESC techDesc;
	Effects::SkyFX->SkyTech->GetDesc(&techDesc);

	for (uint32 p = 0; p < techDesc.Passes; ++p)
	{
		ComPtr<FxPass> pass = Effects::SkyFX->SkyTech->GetPassByIndex(p);

		pass->Apply(0, dc);

		dc->DrawIndexed(_indexCount, 0, 0);
	}
}