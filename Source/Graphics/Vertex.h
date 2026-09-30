#pragma once

#include "Utils.h"

namespace Vertex
{
	struct Pos
	{
		float x;
		float y;
		float z;
	};

	// Basic 32-byte vertex structure.
	struct Basic32
	{
		Basic32() : pos(0.0f, 0.0f, 0.0f), normal(0.0f, 0.0f, 0.0f), tex(0.0f, 0.0f) {}
		Basic32(const XMFLOAT3& p, const XMFLOAT3& n, const XMFLOAT2& uv) : pos(p), normal(n), tex(uv) {}
		Basic32(float px, float py, float pz, float nx, float ny, float nz, float u, float v) : pos(px, py, pz), normal(nx, ny, nz), tex(u, v) {}

		XMFLOAT3 pos;
		XMFLOAT3 normal;
		XMFLOAT2 tex;
	};

	struct TreePointSprite
	{
		XMFLOAT3 pos;
		XMFLOAT2 size;
	};

	struct PosNormalTexTan
	{
		XMFLOAT3 pos;
		XMFLOAT3 normal;
		XMFLOAT2 tex;
		XMFLOAT3 tangentU;
	};

	struct PosNormalTexTan2
	{
		XMFLOAT3 pos;
		XMFLOAT3 normal;
		XMFLOAT2 tex;
		XMFLOAT4 tangentU;
	};

	struct Terrain
	{
		XMFLOAT3 Pos;
		XMFLOAT2 Tex;
		XMFLOAT2 BoundsY;
	};

	struct Particle
	{
		XMFLOAT3 InitialPos;
		XMFLOAT3 InitialVel;
		XMFLOAT2 Size;
		float Age;
		uint32 Type;
	};

	struct AmbientOcclusion
	{
		XMFLOAT3 Pos;
		XMFLOAT3 Normal;
		XMFLOAT2 Tex;
		float AmbientAccess;
	};

	struct PosNormalTexTanSkinned
	{
		XMFLOAT3 pos;
		XMFLOAT3 normal;
		XMFLOAT2 tex;
		XMFLOAT4 tangentU;
		XMFLOAT3 weights; 
		BYTE boneIndices[4];   
		//BYTE color[4];
	};

	struct TerrainVertex
	{
		XMFLOAT4 pos;
		XMFLOAT2 tex;
		XMFLOAT3 normal;
		XMFLOAT3 tangentU;
		XMFLOAT3 binormal;
		XMFLOAT3 color;
		XMFLOAT2 tex2;
	};
}
 

class InputLayoutDesc
{
public:
	// Init like const int A::a[4] = {0, 1, 2, 3}; in .cpp file.
	static const D3D11_INPUT_ELEMENT_DESC Pos[1];
	static const D3D11_INPUT_ELEMENT_DESC Basic32[3];
	static const D3D11_INPUT_ELEMENT_DESC TreePointSprite[2];
	static const D3D11_INPUT_ELEMENT_DESC InstancedBasic32[8];
	static const D3D11_INPUT_ELEMENT_DESC PosNormalTexTan[4];
	static const D3D11_INPUT_ELEMENT_DESC PosNormalTexTan2[4];
	static const D3D11_INPUT_ELEMENT_DESC Terrain[3];
	static const D3D11_INPUT_ELEMENT_DESC Particle[5];
	static const D3D11_INPUT_ELEMENT_DESC AmbientOcclusion[4];
	static const D3D11_INPUT_ELEMENT_DESC PosNormalTexTanSkinned[6];
	static const D3D11_INPUT_ELEMENT_DESC InstancedBasic[8];
	static const D3D11_INPUT_ELEMENT_DESC TerrainVertex[7];
};

class InputLayouts
{
public:
	static void InitAll(ComPtr<ID3D11Device> device);
	static void DestroyAll();

	static ComPtr<ID3D11InputLayout> Pos;
	static ComPtr<ID3D11InputLayout> Basic32;
	static ComPtr<ID3D11InputLayout> TreePointSprite;
	static ComPtr<ID3D11InputLayout> InstancedBasic32;
	static ComPtr<ID3D11InputLayout> PosNormalTexTan;
	static ComPtr<ID3D11InputLayout> PosNormalTexTan2;
	static ComPtr<ID3D11InputLayout> Terrain;
	static ComPtr<ID3D11InputLayout> Particle;
	static ComPtr<ID3D11InputLayout> AmbientOcclusion;
	static ComPtr<ID3D11InputLayout> PosNormalTexTanSkinned;
	static ComPtr<ID3D11InputLayout> InstancedBasic;
	static ComPtr<ID3D11InputLayout> TerrainVertex;
};
