#pragma once

class NormalMapSkinnedShader : public Shader
{

public:
	NormalMapSkinnedShader(ComPtr<ID3D11Device> device, const std::wstring& filename);
	~NormalMapSkinnedShader();

	void SetWorldViewProj(CXMMATRIX M) { WorldViewProj->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetWorldViewProjTex(CXMMATRIX M) { WorldViewProjTex->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetWorld(CXMMATRIX M) { World->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetWorldInvTranspose(CXMMATRIX M) { WorldInvTranspose->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetBoneTransforms(const XMFLOAT4X4* M, int cnt) { BoneTransforms->SetMatrixArray(reinterpret_cast<const float*>(M), 0, cnt); }
	void SetShadowTransform(CXMMATRIX M) { ShadowTransform->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetTexTransform(CXMMATRIX M) { TexTransform->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetEyePosW(const XMFLOAT3& v) { EyePosW->SetRawValue(&v, 0, sizeof(XMFLOAT3)); }
	void SetFogColor(const FXMVECTOR v) { FogColor->SetFloatVector(reinterpret_cast<const float*>(&v)); }
	void SetFogStart(float f) { FogStart->SetFloat(f); }
	void SetFogRange(float f) { FogRange->SetFloat(f); }
	void SetDirLights(const DirectionalLight* lights) { DirLights->SetRawValue(lights, 0, 3 * sizeof(DirectionalLight)); }
	void SetMaterial(const Material& mat) { Mat->SetRawValue(&mat, 0, sizeof(Material)); }
	void SetDiffuseMap(ID3D11ShaderResourceView* tex) { DiffuseMap->SetResource(tex); }
	void SetCubeMap(ID3D11ShaderResourceView* tex) { CubeMap->SetResource(tex); }
	void SetNormalMap(ID3D11ShaderResourceView* tex) { NormalMap->SetResource(tex); }
	void SetSsaoMap(ID3D11ShaderResourceView* tex) { SsaoMap->SetResource(tex); }
	void SetShadowMap(ID3D11ShaderResourceView* tex) { ShadowMap->SetResource(tex); }

	ComPtr<FxTechnique> Light1Tech;
	ComPtr<FxTechnique> Light2Tech;
	ComPtr<FxTechnique> Light3Tech;

	ComPtr<FxTechnique> Light0TexTech;
	ComPtr<FxTechnique> Light1TexTech;
	ComPtr<FxTechnique> Light2TexTech;
	ComPtr<FxTechnique> Light3TexTech;

	ComPtr<FxTechnique> Light0TexAlphaClipTech;
	ComPtr<FxTechnique> Light1TexAlphaClipTech;
	ComPtr<FxTechnique> Light2TexAlphaClipTech;
	ComPtr<FxTechnique> Light3TexAlphaClipTech;

	ComPtr<FxTechnique> Light1FogTech;
	ComPtr<FxTechnique> Light2FogTech;
	ComPtr<FxTechnique> Light3FogTech;

	ComPtr<FxTechnique> Light0TexFogTech;
	ComPtr<FxTechnique> Light1TexFogTech;
	ComPtr<FxTechnique> Light2TexFogTech;
	ComPtr<FxTechnique> Light3TexFogTech;

	ComPtr<FxTechnique> Light0TexAlphaClipFogTech;
	ComPtr<FxTechnique> Light1TexAlphaClipFogTech;
	ComPtr<FxTechnique> Light2TexAlphaClipFogTech;
	ComPtr<FxTechnique> Light3TexAlphaClipFogTech;

	ComPtr<FxTechnique> Light1ReflectTech;
	ComPtr<FxTechnique> Light2ReflectTech;
	ComPtr<FxTechnique> Light3ReflectTech;

	ComPtr<FxTechnique> Light0TexReflectTech;
	ComPtr<FxTechnique> Light1TexReflectTech;
	ComPtr<FxTechnique> Light2TexReflectTech;
	ComPtr<FxTechnique> Light3TexReflectTech;

	ComPtr<FxTechnique> Light0TexAlphaClipReflectTech;
	ComPtr<FxTechnique> Light1TexAlphaClipReflectTech;
	ComPtr<FxTechnique> Light2TexAlphaClipReflectTech;
	ComPtr<FxTechnique> Light3TexAlphaClipReflectTech;

	ComPtr<FxTechnique> Light1FogReflectTech;
	ComPtr<FxTechnique> Light2FogReflectTech;
	ComPtr<FxTechnique> Light3FogReflectTech;

	ComPtr<FxTechnique> Light0TexFogReflectTech;
	ComPtr<FxTechnique> Light1TexFogReflectTech;
	ComPtr<FxTechnique> Light2TexFogReflectTech;
	ComPtr<FxTechnique> Light3TexFogReflectTech;

	ComPtr<FxTechnique> Light0TexAlphaClipFogReflectTech;
	ComPtr<FxTechnique> Light1TexAlphaClipFogReflectTech;
	ComPtr<FxTechnique> Light2TexAlphaClipFogReflectTech;
	ComPtr<FxTechnique> Light3TexAlphaClipFogReflectTech;

	ComPtr<FxTechnique> Light1SkinnedTech;
	ComPtr<FxTechnique> Light2SkinnedTech;
	ComPtr<FxTechnique> Light3SkinnedTech;

	ComPtr<FxTechnique> Light0TexSkinnedTech;
	ComPtr<FxTechnique> Light1TexSkinnedTech;
	ComPtr<FxTechnique> Light2TexSkinnedTech;
	ComPtr<FxTechnique> Light3TexSkinnedTech;

	ComPtr<FxTechnique> Light0TexAlphaClipSkinnedTech;
	ComPtr<FxTechnique> Light1TexAlphaClipSkinnedTech;
	ComPtr<FxTechnique> Light2TexAlphaClipSkinnedTech;
	ComPtr<FxTechnique> Light3TexAlphaClipSkinnedTech;

	ComPtr<FxTechnique> Light1FogSkinnedTech;
	ComPtr<FxTechnique> Light2FogSkinnedTech;
	ComPtr<FxTechnique> Light3FogSkinnedTech;

	ComPtr<FxTechnique> Light0TexFogSkinnedTech;
	ComPtr<FxTechnique> Light1TexFogSkinnedTech;
	ComPtr<FxTechnique> Light2TexFogSkinnedTech;
	ComPtr<FxTechnique> Light3TexFogSkinnedTech;

	ComPtr<FxTechnique> Light0TexAlphaClipFogSkinnedTech;
	ComPtr<FxTechnique> Light1TexAlphaClipFogSkinnedTech;
	ComPtr<FxTechnique> Light2TexAlphaClipFogSkinnedTech;
	ComPtr<FxTechnique> Light3TexAlphaClipFogSkinnedTech;

	ComPtr<FxTechnique> Light1ReflectSkinnedTech;
	ComPtr<FxTechnique> Light2ReflectSkinnedTech;
	ComPtr<FxTechnique> Light3ReflectSkinnedTech;

	ComPtr<FxTechnique> Light0TexReflectSkinnedTech;
	ComPtr<FxTechnique> Light1TexReflectSkinnedTech;
	ComPtr<FxTechnique> Light2TexReflectSkinnedTech;
	ComPtr<FxTechnique> Light3TexReflectSkinnedTech;

	ComPtr<FxTechnique> Light0TexAlphaClipReflectSkinnedTech;
	ComPtr<FxTechnique> Light1TexAlphaClipReflectSkinnedTech;
	ComPtr<FxTechnique> Light2TexAlphaClipReflectSkinnedTech;
	ComPtr<FxTechnique> Light3TexAlphaClipReflectSkinnedTech;

	ComPtr<FxTechnique> Light1FogReflectSkinnedTech;
	ComPtr<FxTechnique> Light2FogReflectSkinnedTech;
	ComPtr<FxTechnique> Light3FogReflectSkinnedTech;

	ComPtr<FxTechnique> Light0TexFogReflectSkinnedTech;
	ComPtr<FxTechnique> Light1TexFogReflectSkinnedTech;
	ComPtr<FxTechnique> Light2TexFogReflectSkinnedTech;
	ComPtr<FxTechnique> Light3TexFogReflectSkinnedTech;

	ComPtr<FxTechnique> Light0TexAlphaClipFogReflectSkinnedTech;
	ComPtr<FxTechnique> Light1TexAlphaClipFogReflectSkinnedTech;
	ComPtr<FxTechnique> Light2TexAlphaClipFogReflectSkinnedTech;
	ComPtr<FxTechnique> Light3TexAlphaClipFogReflectSkinnedTech;

	ComPtr<FxVar> WorldViewProj;
	ComPtr<FxVar> WorldViewProjTex;
	ComPtr<FxVar> World;
	ComPtr<FxVar> WorldInvTranspose;
	ComPtr<FxVar> BoneTransforms;
	ComPtr<FxVar> ShadowTransform;
	ComPtr<FxVar> TexTransform;
	ComPtr<FxVar> EyePosW;
	ComPtr<FxVar> FogColor;
	ComPtr<FxVar> FogStart;
	ComPtr<FxVar> FogRange;
	ComPtr<FxVar> DirLights;
	ComPtr<FxVar> Mat;

	ComPtr<FxVar> DiffuseMap;
	ComPtr<FxVar> CubeMap;
	ComPtr<FxVar> NormalMap;
	ComPtr<FxVar> ShadowMap;
	ComPtr<FxVar> SsaoMap;
};

