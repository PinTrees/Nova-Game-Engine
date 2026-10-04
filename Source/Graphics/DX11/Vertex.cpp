#include "pch.h"
#include "Vertex.h"
#include "Effects.h"

const D3D11_INPUT_ELEMENT_DESC InputLayoutDesc::Pos[1] =
{
	{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0}
};

const D3D11_INPUT_ELEMENT_DESC InputLayoutDesc::Basic32[3] =
{
	{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
	{"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
	{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0}
};

const D3D11_INPUT_ELEMENT_DESC InputLayoutDesc::TreePointSprite[2] =
{
	{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
	{"SIZE", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}
};

const D3D11_INPUT_ELEMENT_DESC InputLayoutDesc::InstancedBasic32[8] =
{
	{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
	{"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
	{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
	{ "WORLD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
	{ "WORLD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
	{ "WORLD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
	{ "WORLD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 48, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
	{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 64,  D3D11_INPUT_PER_INSTANCE_DATA, 1 }
};

const D3D11_INPUT_ELEMENT_DESC InputLayoutDesc::PosNormalTexTan[4] =
{
	{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0},
	{"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
	{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
	{"TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0}
};

const D3D11_INPUT_ELEMENT_DESC InputLayoutDesc::PosNormalTexTan2[4] =
{
	{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0},
	{"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
	{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
	{"TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0}
};
const D3D11_INPUT_ELEMENT_DESC InputLayoutDesc::Terrain[3] =
{
	{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
	{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
	{"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0}
};

const D3D11_INPUT_ELEMENT_DESC InputLayoutDesc::Particle[5] =
{
	{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0},
	{"VELOCITY", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
	{"SIZE",     0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
	{"AGE",      0, DXGI_FORMAT_R32_FLOAT,       0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
	{"TYPE",     0, DXGI_FORMAT_R32_UINT,        0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0},
};

const D3D11_INPUT_ELEMENT_DESC InputLayoutDesc::AmbientOcclusion[4] =
{
	{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
	{"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
	{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
	{"AMBIENT",  0, DXGI_FORMAT_R32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0}
};

const D3D11_INPUT_ELEMENT_DESC InputLayoutDesc::PosNormalTexTanSkinned[6] =
{
	{"POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0},
	{"NORMAL",       0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
	{"TEXCOORD",     0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
	{"TANGENT",      0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
	{"WEIGHTS",      0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0},
	{"BONEINDICES",  0, DXGI_FORMAT_R8G8B8A8_UINT,   0, 60, D3D11_INPUT_PER_VERTEX_DATA, 0}
};

// 인스턴스 (슬롯 1) = 월드 행렬 + 기본색 80 바이트 (MeshBatcher · OcclusionCulling::InstanceBytes). INSTCOLOR 는 BatchTech 만 읽는다
const D3D11_INPUT_ELEMENT_DESC InputLayoutDesc::InstancedBasic[9] =
{
	{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0},
	{"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
	{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
	{"TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
	{ "WORLD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
	{ "WORLD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
	{ "WORLD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
	{ "WORLD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 48, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
	{ "INSTCOLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 64, D3D11_INPUT_PER_INSTANCE_DATA, 1 }
};

const D3D11_INPUT_ELEMENT_DESC InputLayoutDesc::TerrainVertex[7] =
{
	{"POSITION",	0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0},
	{"TEXCOORD",	0, DXGI_FORMAT_R32G32_FLOAT,       0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
	{"NORMAL",		0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
	{"TANGENT",		0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0},
	{"BINORMAL",	0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0},
	{"COLOR",		0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 60, D3D11_INPUT_PER_VERTEX_DATA, 0},
	{"TEXCOORD",	1, DXGI_FORMAT_R32G32_FLOAT,       0, 72, D3D11_INPUT_PER_VERTEX_DATA, 0}
};

ComPtr<GfxInputLayout> InputLayouts::Pos;
ComPtr<GfxInputLayout> InputLayouts::Basic32;
ComPtr<GfxInputLayout> InputLayouts::TreePointSprite;
ComPtr<GfxInputLayout> InputLayouts::InstancedBasic32;
ComPtr<GfxInputLayout> InputLayouts::PosNormalTexTan;
ComPtr<GfxInputLayout> InputLayouts::PosNormalTexTan2;
ComPtr<GfxInputLayout> InputLayouts::Terrain;
ComPtr<GfxInputLayout> InputLayouts::Particle;
ComPtr<GfxInputLayout> InputLayouts::AmbientOcclusion;
ComPtr<GfxInputLayout> InputLayouts::PosNormalTexTanSkinned;

// Instancing
ComPtr<GfxInputLayout> InputLayouts::InstancedBasic;

void InputLayouts::InitAll(ComPtr<GfxDevice> device)
{
	D3DX11_PASS_DESC passDesc;

	//
	// Pos
	//

	Effects::TessellationFX->TessTech->GetPassByIndex(0)->GetDesc(&passDesc);
	HR(device->CreateInputLayout(InputLayoutDesc::Pos, 1, passDesc.pIAInputSignature, passDesc.IAInputSignatureSize, &Pos));

	//
	// Basic32
	//

	Effects::BasicFX->Light1Tech->GetPassByIndex(0)->GetDesc(&passDesc);
	HR(device->CreateInputLayout(InputLayoutDesc::Basic32, 3, passDesc.pIAInputSignature, passDesc.IAInputSignatureSize, &Basic32));

	//
	// TreePointSprite
	//

	Effects::TreeSpriteFX->Light3Tech->GetPassByIndex(0)->GetDesc(&passDesc);
	HR(device->CreateInputLayout(InputLayoutDesc::TreePointSprite, 2, passDesc.pIAInputSignature, passDesc.IAInputSignatureSize, &TreePointSprite));

	//
	// InstancedBasic32
	//

	//Effects::InstancedBasicFX->Light1Tech->GetPassByIndex(0)->GetDesc(&passDesc);
	//HR(device->CreateInputLayout(InputLayoutDesc::InstancedBasic32, 8, passDesc.pIAInputSignature, passDesc.IAInputSignatureSize, &InstancedBasic32));

	//
	// NormalMap
	//

	Effects::NormalMapFX->Light1Tech->GetPassByIndex(0)->GetDesc(&passDesc);
	HR(device->CreateInputLayout(InputLayoutDesc::PosNormalTexTan, 4, passDesc.pIAInputSignature,
		passDesc.IAInputSignatureSize, &PosNormalTexTan));

	Effects::NormalMapFX->Light1Tech->GetPassByIndex(0)->GetDesc(&passDesc);
	HR(device->CreateInputLayout(InputLayoutDesc::PosNormalTexTan2, 4, passDesc.pIAInputSignature,
		passDesc.IAInputSignatureSize, &PosNormalTexTan2));

	//
	// Terrain
	//

	Effects::TerrainFX->Light1Tech->GetPassByIndex(0)->GetDesc(&passDesc);
	HR(device->CreateInputLayout(InputLayoutDesc::Terrain, 3, passDesc.pIAInputSignature,
		passDesc.IAInputSignatureSize, &Terrain));

	//
	// Particle
	//

	Effects::FireFX->StreamOutTech->GetPassByIndex(0)->GetDesc(&passDesc);
	HR(device->CreateInputLayout(InputLayoutDesc::Particle, 5, passDesc.pIAInputSignature,
		passDesc.IAInputSignatureSize, &Particle));

	//
	// AmbientOcclusion
	//

	Effects::AmbientOcclusionFX->AmbientOcclusionTech->GetPassByIndex(0)->GetDesc(&passDesc);
	HR(device->CreateInputLayout(InputLayoutDesc::AmbientOcclusion, 4, passDesc.pIAInputSignature,
		passDesc.IAInputSignatureSize, &AmbientOcclusion));

	//
	// NormalMapSkinned
	//

	Shaders::GetShader<NormalMapSkinnedShader>()->Light1SkinnedTech->GetPassByIndex(0)->GetDesc(&passDesc);
	HR(device->CreateInputLayout(InputLayoutDesc::PosNormalTexTanSkinned, 6, passDesc.pIAInputSignature,
		passDesc.IAInputSignatureSize, &PosNormalTexTanSkinned));

	//
	// Instancing
	//

	// Basic: 입력이 가장 많은 BatchTech 로 (OpenGL 은 이 서명의 location 으로 정점 배열을 만든다 — 다른 기법은 앞 8 칸이 같다)
	Effects::InstancedBasicFX->GetFX()->GetTechniqueByName("BatchTech")->GetPassByIndex(0)->GetDesc(&passDesc);
	HR(device->CreateInputLayout(InputLayoutDesc::InstancedBasic, 9, passDesc.pIAInputSignature,
		passDesc.IAInputSignatureSize, &InstancedBasic));

	//
	// TerrainVertex
	//

	// Effects::TerrainFX
	//HR(device->CreateInputLayout(InputLayoutDesc::TerrainVertex, 7, passDesc.pIAInputSignature,
	//	passDesc.IAInputSignatureSize, &TerrainVertex));
}

void InputLayouts::DestroyAll()
{
}
