#include "pch.h"
#include "Terrain.h"
#include "Camera.h"
#include "LightHelper.h"
#include "Effects.h"
#include "Vertex.h"
#include <fstream>
#include <sstream>
#include "MathHelper.h"

Terrain::Terrain()
{
	::XMStoreFloat4x4(&_world, ::XMMatrixIdentity());

	_mat.Ambient = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	_mat.Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	_mat.Specular = XMFLOAT4(0.0f, 0.0f, 0.0f, 64.0f);
	_mat.Reflect = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
}

Terrain::~Terrain()
{
}

float Terrain::GetWidth()const
{
	// Total terrain width.
	return (_info.heightmapWidth - 1) * _info.cellSpacing;
}

float Terrain::GetDepth()const
{
	// Total terrain depth.
	return (_info.heightmapHeight - 1) * _info.cellSpacing;
}

float Terrain::GetHeight(float x, float z) const
{
	// Transform from terrain local space to "cell" space.
	float c = (x + 0.5f * GetWidth()) / _info.cellSpacing;
	float d = (z - 0.5f * GetDepth()) / -_info.cellSpacing;

	// Get the row and column we are in.
	int row = (int)floorf(d);
	int col = (int)floorf(c);

	// Grab the heights of the cell we are in.
	// A*--*B
	//  | /|
	//  |/ |
	// C*--*D
	float A = _heightmap[row * _info.heightmapWidth + col];
	float B = _heightmap[row * _info.heightmapWidth + col + 1];
	float C = _heightmap[(row + 1) * _info.heightmapWidth + col];
	float D = _heightmap[(row + 1) * _info.heightmapWidth + col + 1];

	// Where we are relative to the cell.
	float s = c - (float)col;
	float t = d - (float)row;

	// If upper triangle ABC.
	if (s + t <= 1.0f)
	{
		float uy = B - A;
		float vy = C - A;
		return A + s * uy + t * vy;
	}
	else // lower triangle DCB.
	{
		float uy = C - D;
		float vy = B - D;
		return D + (1.0f - s) * uy + (1.0f - t) * vy;
	}
}

XMMATRIX Terrain::GetWorld()const
{
	return XMLoadFloat4x4(&_world);
}

void Terrain::SetWorld(CXMMATRIX M)
{
	XMStoreFloat4x4(&_world, M);
}

void Terrain::Init(ComPtr<ID3D11Device> device, ComPtr<ID3D11DeviceContext> dc, const InitInfo& initInfo)
{
	_info = initInfo;

	// Divide heightmap into patches such that each patch has CellsPerPatch.
	_numPatchVertRows = ((_info.heightmapHeight - 1) / CellsPerPatch) + 1;
	_numPatchVertCols = ((_info.heightmapWidth - 1) / CellsPerPatch) + 1;

	_numPatchVertices = _numPatchVertRows * _numPatchVertCols;
	_numPatchQuadFaces = (_numPatchVertRows - 1) * (_numPatchVertCols - 1);

	LoadHeightmap();
	Smooth();
	CalcAllPatchBoundsY();

	BuildQuadPatchVB(device);
	BuildQuadPatchIB(device);
	BuildHeightmapSRV(device);

	std::vector<std::wstring> layerFilenames;
	layerFilenames.push_back(_info.layerMapFilename0);
	layerFilenames.push_back(_info.layerMapFilename1);
	layerFilenames.push_back(_info.layerMapFilename2);
	layerFilenames.push_back(_info.layerMapFilename3);
	layerFilenames.push_back(_info.layerMapFilename4);

	_layerMapArraySRV = Utils::CreateTexture2DArraySRV(device, dc, layerFilenames);

	_blendMapSRV = Utils::LoadTexture(device, _info.blendMapFilename.c_str());
}

void Terrain::Draw(ComPtr<ID3D11DeviceContext> dc, const Camera& cam, DirectionalLight lights[3])
{
	dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST);
	dc->IASetInputLayout(InputLayouts::Terrain.Get());

	uint32 stride = sizeof(Vertex::Terrain);
	uint32 offset = 0;
	dc->IASetVertexBuffers(0, 1, _quadPatchVB.GetAddressOf(), &stride, &offset);
	dc->IASetIndexBuffer(_quadPatchIB.Get(), DXGI_FORMAT_R16_UINT, 0);

	XMMATRIX viewProj = cam.ViewProj();
	XMMATRIX world = XMLoadFloat4x4(&_world);
	XMMATRIX worldInvTranspose = MathHelper::InverseTranspose(world);
	XMMATRIX worldViewProj = world * viewProj;

	XMFLOAT4 worldPlanes[6];

	BoundingBox;

	MathHelper::ExtractFrustumPlanes(worldPlanes, viewProj);

	// Set per frame constants.
	Effects::TerrainFX->SetViewProj(viewProj);
	Effects::TerrainFX->SetEyePosW(cam.GetPosition());
	Effects::TerrainFX->SetDirLights(lights);
	Effects::TerrainFX->SetFogColor(Colors::Silver);
	Effects::TerrainFX->SetFogStart(15.0f);
	Effects::TerrainFX->SetFogRange(175.0f);
	Effects::TerrainFX->SetMinDist(20.0f);
	Effects::TerrainFX->SetMaxDist(500.0f);
	Effects::TerrainFX->SetMinTess(0.0f);
	Effects::TerrainFX->SetMaxTess(6.0f);
	Effects::TerrainFX->SetTexelCellSpaceU(1.0f / _info.heightmapWidth);
	Effects::TerrainFX->SetTexelCellSpaceV(1.0f / _info.heightmapHeight);
	Effects::TerrainFX->SetWorldCellSpace(_info.cellSpacing);
	Effects::TerrainFX->SetWorldFrustumPlanes(worldPlanes);

	Effects::TerrainFX->SetLayerMapArray(_layerMapArraySRV.Get());
	Effects::TerrainFX->SetBlendMap(_blendMapSRV.Get());
	Effects::TerrainFX->SetHeightMap(_heightMapSRV.Get());

	Effects::TerrainFX->SetMaterial(_mat);

	ComPtr<ID3DX11EffectTechnique> tech = Effects::TerrainFX->Light1Tech;
	D3DX11_TECHNIQUE_DESC techDesc;
	tech->GetDesc(&techDesc);

	for (uint32 i = 0; i < techDesc.Passes; ++i)
	{
		ComPtr<ID3DX11EffectPass> pass = tech->GetPassByIndex(i);
		pass->Apply(0, dc.Get());

		dc->DrawIndexed(_numPatchQuadFaces * 4, 0, 0);
	}

	// FX sets tessellation stages, but it does not disable them.  So do that here
	// to turn off tessellation.
	dc->HSSetShader(0, 0, 0);
	dc->DSSetShader(0, 0, 0);
}

void Terrain::LoadHeightmap()
{
	// A height for each vertex
	std::vector<unsigned char> in(_info.heightmapWidth * _info.heightmapHeight);

	// Open the file.
	std::ifstream inFile;
	inFile.open(_info.heightMapFilename.c_str(), std::ios_base::binary);

	if (inFile)
	{
		// Read the RAW bytes.
		inFile.read((char*)&in[0], (std::streamsize)in.size());

		// Done with file.
		inFile.close();
	}

	// Copy the array data into a float array and scale it.
	_heightmap.resize(_info.heightmapHeight * _info.heightmapWidth, 0);

	for (uint32 i = 0; i < _info.heightmapHeight * _info.heightmapWidth; ++i)
	{
		_heightmap[i] = (in[i] / 255.0f) * _info.heightScale;
	}
}

void Terrain::Smooth()
{
	std::vector<float> dest(_heightmap.size());

	for (uint32 i = 0; i < _info.heightmapHeight; ++i)
	{
		for (uint32 j = 0; j < _info.heightmapWidth; ++j)
		{
			dest[i * _info.heightmapWidth + j] = Average(i, j);
		}
	}

	// Replace the old heightmap with the filtered one.
	_heightmap = dest;
}

bool Terrain::InBounds(int32 i, int32 j)
{
	// True if ij are valid indices; false otherwise.
	return
		i >= 0 && i < (int32)_info.heightmapHeight &&
		j >= 0 && j < (int32)_info.heightmapWidth;
}

float Terrain::Average(int32 i, int32 j)
{
	// Function computes the average height of the ij element.
	// It averages itself with its eight neighbor pixels.  Note
	// that if a pixel is missing neighbor, we just don't include it
	// in the average--that is, edge pixels don't have a neighbor pixel.
	//
	// ----------
	// | 1| 2| 3|
	// ----------
	// |4 |ij| 6|
	// ----------
	// | 7| 8| 9|
	// ----------

	float avg = 0.0f;
	float num = 0.0f;

	// Use int to allow negatives.  If we use UINT, @ i=0, m=i-1=UINT_MAX
	// and no iterations of the outer for loop occur.
	for (int32 m = i - 1; m <= i + 1; ++m)
	{
		for (int32 n = j - 1; n <= j + 1; ++n)
		{
			if (InBounds(m, n))
			{
				avg += _heightmap[m * _info.heightmapWidth + n];
				num += 1.0f;
			}
		}
	}

	return avg / num;
}

void Terrain::CalcAllPatchBoundsY()
{
	_patchBoundsY.resize(_numPatchQuadFaces);

	// For each patch
	for (uint32 i = 0; i < _numPatchVertRows - 1; ++i)
	{
		for (uint32 j = 0; j < _numPatchVertCols - 1; ++j)
		{
			CalcPatchBoundsY(i, j);
		}
	}
}

void Terrain::CalcPatchBoundsY(uint32 i, uint32 j)
{
	// Scan the heightmap values this patch covers and compute the min/max height.

	uint32 x0 = j * CellsPerPatch;
	uint32 x1 = (j + 1) * CellsPerPatch;

	uint32 y0 = i * CellsPerPatch;
	uint32 y1 = (i + 1) * CellsPerPatch;

	float minY = +MathHelper::Infinity;
	float maxY = -MathHelper::Infinity;

	for (uint32 y = y0; y <= y1; ++y)
	{
		for (uint32 x = x0; x <= x1; ++x)
		{
			uint32 k = y * _info.heightmapWidth + x;
			minY = MathHelper::Min(minY, _heightmap[k]);
			maxY = MathHelper::Max(maxY, _heightmap[k]);
		}
	}

	uint32 patchID = i * (_numPatchVertCols - 1) + j;
	_patchBoundsY[patchID] = XMFLOAT2(minY, maxY);
}

void Terrain::BuildQuadPatchVB(ComPtr<ID3D11Device> device)
{
	std::vector<Vertex::Terrain> patchVertices(_numPatchVertRows * _numPatchVertCols);

	float halfWidth = 0.5f * GetWidth();
	float halfDepth = 0.5f * GetDepth();

	float patchWidth = GetWidth() / (_numPatchVertCols - 1);
	float patchDepth = GetDepth() / (_numPatchVertRows - 1);
	float du = 1.0f / (_numPatchVertCols - 1);
	float dv = 1.0f / (_numPatchVertRows - 1);

	for (uint32 i = 0; i < _numPatchVertRows; ++i)
	{
		float z = halfDepth - i * patchDepth;
		for (uint32 j = 0; j < _numPatchVertCols; ++j)
		{
			float x = -halfWidth + j * patchWidth;

			patchVertices[i * _numPatchVertCols + j].Pos = XMFLOAT3(x, 0.0f, z);

			// Stretch texture over grid.
			patchVertices[i * _numPatchVertCols + j].Tex.x = j * du;
			patchVertices[i * _numPatchVertCols + j].Tex.y = i * dv;
		}
	}

	// Store axis-aligned bounding box y-bounds in upper-left patch corner.
	for (uint32 i = 0; i < _numPatchVertRows - 1; ++i)
	{
		for (uint32 j = 0; j < _numPatchVertCols - 1; ++j)
		{
			uint32 patchID = i * (_numPatchVertCols - 1) + j;
			patchVertices[i * _numPatchVertCols + j].BoundsY = _patchBoundsY[patchID];
		}
	}

	D3D11_BUFFER_DESC vbd;
	vbd.Usage = D3D11_USAGE_IMMUTABLE;
	vbd.ByteWidth = sizeof(Vertex::Terrain) * patchVertices.size();
	vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vbd.CPUAccessFlags = 0;
	vbd.MiscFlags = 0;
	vbd.StructureByteStride = 0;

	D3D11_SUBRESOURCE_DATA vinitData;
	vinitData.pSysMem = &patchVertices[0];
	HR(device->CreateBuffer(&vbd, &vinitData, _quadPatchVB.GetAddressOf()));
}

void Terrain::BuildQuadPatchIB(ComPtr<ID3D11Device> device)
{
	std::vector<USHORT> indices(_numPatchQuadFaces * 4); // 4 indices per quad face

	// Iterate over each quad and compute indices.
	int32 k = 0;
	for (uint32 i = 0; i < _numPatchVertRows - 1; ++i)
	{
		for (uint32 j = 0; j < _numPatchVertCols - 1; ++j)
		{
			// Top row of 2x2 quad patch
			indices[k] = i * _numPatchVertCols + j;
			indices[k + 1] = i * _numPatchVertCols + j + 1;

			// Bottom row of 2x2 quad patch
			indices[k + 2] = (i + 1) * _numPatchVertCols + j;
			indices[k + 3] = (i + 1) * _numPatchVertCols + j + 1;

			k += 4; // next quad
		}
	}

	D3D11_BUFFER_DESC ibd;
	ibd.Usage = D3D11_USAGE_IMMUTABLE;
	ibd.ByteWidth = sizeof(USHORT) * indices.size();
	ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
	ibd.CPUAccessFlags = 0;
	ibd.MiscFlags = 0;
	ibd.StructureByteStride = 0;

	D3D11_SUBRESOURCE_DATA iinitData;
	iinitData.pSysMem = &indices[0];
	HR(device->CreateBuffer(&ibd, &iinitData, _quadPatchIB.GetAddressOf()));
}

void Terrain::BuildHeightmapSRV(ComPtr<ID3D11Device> device)
{
	D3D11_TEXTURE2D_DESC texDesc;
	texDesc.Width = _info.heightmapWidth;
	texDesc.Height = _info.heightmapHeight;
	texDesc.MipLevels = 1;
	texDesc.ArraySize = 1;
	texDesc.Format = DXGI_FORMAT_R16_FLOAT;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Usage = D3D11_USAGE_DEFAULT;
	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	texDesc.CPUAccessFlags = 0;
	texDesc.MiscFlags = 0;

	// HALF is defined in xnamath.h, for storing 16-bit float.
	std::vector<uint16> hmap(_heightmap.size());
	std::transform(_heightmap.begin(), _heightmap.end(), hmap.begin(), MathHelper::ConvertFloatToHalf);
	
	D3D11_SUBRESOURCE_DATA data;
	data.pSysMem = &hmap[0];
	data.SysMemPitch = _info.heightmapWidth * sizeof(uint16);
	data.SysMemSlicePitch = 0;

	ID3D11Texture2D* hmapTex = 0;
	HR(device->CreateTexture2D(&texDesc, &data, &hmapTex));

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
	srvDesc.Format = texDesc.Format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = -1;
	HR(device->CreateShaderResourceView(hmapTex, &srvDesc, _heightMapSRV.GetAddressOf()));
}