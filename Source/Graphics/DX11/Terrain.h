#pragma once

class Camera;
struct DirectionalLight;
#include "LightHelper.h"

class Terrain
{
public:
	struct InitInfo
	{
		std::wstring heightMapFilename;
		std::wstring layerMapFilename0;
		std::wstring layerMapFilename1;
		std::wstring layerMapFilename2;
		std::wstring layerMapFilename3;
		std::wstring layerMapFilename4;
		std::wstring blendMapFilename;
		float heightScale;
		uint32 heightmapWidth;
		uint32 heightmapHeight;
		float cellSpacing;
	};

public:
	Terrain();
	~Terrain();

	float GetWidth() const;
	float GetDepth() const;
	float GetHeight(float x, float z) const;

	XMMATRIX GetWorld() const;
	void SetWorld(CXMMATRIX M);

	void Init(ComPtr<ID3D11Device> device, ComPtr<ID3D11DeviceContext> dc, const InitInfo& initInfo);
	void Draw(ComPtr<ID3D11DeviceContext> dc, const Camera& cam, DirectionalLight lights[3]);

private:
	void LoadHeightmap();
	void Smooth();
	bool InBounds(int32 i, int32 j);
	float Average(int32 i, int32 j);
	void CalcAllPatchBoundsY();
	void CalcPatchBoundsY(uint32 i, uint32 j);
	void BuildQuadPatchVB(ComPtr<ID3D11Device> device);
	void BuildQuadPatchIB(ComPtr<ID3D11Device> device);
	void BuildHeightmapSRV(ComPtr<ID3D11Device> device);

private:

	// Divide heightmap into patches such that each patch has CellsPerPatch cells
	// and CellsPerPatch+1 vertices.  Use 64 so that if we tessellate all the way 
	// to 64, we use all the data from the heightmap.  
	static const int CellsPerPatch = 64;

	ComPtr<ID3D11Buffer> _quadPatchVB;
	ComPtr<ID3D11Buffer> _quadPatchIB;

	ComPtr<ID3D11ShaderResourceView> _layerMapArraySRV;
	ComPtr<ID3D11ShaderResourceView> _blendMapSRV;
	ComPtr<ID3D11ShaderResourceView> _heightMapSRV;

	InitInfo _info;

	uint32 _numPatchVertices = 0;
	uint32 _numPatchQuadFaces = 0;
	uint32 _numPatchVertRows = 0;
	uint32 _numPatchVertCols = 0;

	XMFLOAT4X4 _world;

	Material _mat;

	std::vector<XMFLOAT2> _patchBoundsY;
	std::vector<float> _heightmap;
};