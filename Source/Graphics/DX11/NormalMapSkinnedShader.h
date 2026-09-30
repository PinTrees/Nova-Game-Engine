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

	ComPtr<ID3DX11EffectTechnique> Light1Tech;
	ComPtr<ID3DX11EffectTechnique> Light2Tech;
	ComPtr<ID3DX11EffectTechnique> Light3Tech;

	ComPtr<ID3DX11EffectTechnique> Light0TexTech;
	ComPtr<ID3DX11EffectTechnique> Light1TexTech;
	ComPtr<ID3DX11EffectTechnique> Light2TexTech;
	ComPtr<ID3DX11EffectTechnique> Light3TexTech;

	ComPtr<ID3DX11EffectTechnique> Light0TexAlphaClipTech;
	ComPtr<ID3DX11EffectTechnique> Light1TexAlphaClipTech;
	ComPtr<ID3DX11EffectTechnique> Light2TexAlphaClipTech;
	ComPtr<ID3DX11EffectTechnique> Light3TexAlphaClipTech;

	ComPtr<ID3DX11EffectTechnique> Light1FogTech;
	ComPtr<ID3DX11EffectTechnique> Light2FogTech;
	ComPtr<ID3DX11EffectTechnique> Light3FogTech;

	ComPtr<ID3DX11EffectTechnique> Light0TexFogTech;
	ComPtr<ID3DX11EffectTechnique> Light1TexFogTech;
	ComPtr<ID3DX11EffectTechnique> Light2TexFogTech;
	ComPtr<ID3DX11EffectTechnique> Light3TexFogTech;

	ComPtr<ID3DX11EffectTechnique> Light0TexAlphaClipFogTech;
	ComPtr<ID3DX11EffectTechnique> Light1TexAlphaClipFogTech;
	ComPtr<ID3DX11EffectTechnique> Light2TexAlphaClipFogTech;
	ComPtr<ID3DX11EffectTechnique> Light3TexAlphaClipFogTech;

	ComPtr<ID3DX11EffectTechnique> Light1ReflectTech;
	ComPtr<ID3DX11EffectTechnique> Light2ReflectTech;
	ComPtr<ID3DX11EffectTechnique> Light3ReflectTech;

	ComPtr<ID3DX11EffectTechnique> Light0TexReflectTech;
	ComPtr<ID3DX11EffectTechnique> Light1TexReflectTech;
	ComPtr<ID3DX11EffectTechnique> Light2TexReflectTech;
	ComPtr<ID3DX11EffectTechnique> Light3TexReflectTech;

	ComPtr<ID3DX11EffectTechnique> Light0TexAlphaClipReflectTech;
	ComPtr<ID3DX11EffectTechnique> Light1TexAlphaClipReflectTech;
	ComPtr<ID3DX11EffectTechnique> Light2TexAlphaClipReflectTech;
	ComPtr<ID3DX11EffectTechnique> Light3TexAlphaClipReflectTech;

	ComPtr<ID3DX11EffectTechnique> Light1FogReflectTech;
	ComPtr<ID3DX11EffectTechnique> Light2FogReflectTech;
	ComPtr<ID3DX11EffectTechnique> Light3FogReflectTech;

	ComPtr<ID3DX11EffectTechnique> Light0TexFogReflectTech;
	ComPtr<ID3DX11EffectTechnique> Light1TexFogReflectTech;
	ComPtr<ID3DX11EffectTechnique> Light2TexFogReflectTech;
	ComPtr<ID3DX11EffectTechnique> Light3TexFogReflectTech;

	ComPtr<ID3DX11EffectTechnique> Light0TexAlphaClipFogReflectTech;
	ComPtr<ID3DX11EffectTechnique> Light1TexAlphaClipFogReflectTech;
	ComPtr<ID3DX11EffectTechnique> Light2TexAlphaClipFogReflectTech;
	ComPtr<ID3DX11EffectTechnique> Light3TexAlphaClipFogReflectTech;

	ComPtr<ID3DX11EffectTechnique> Light1SkinnedTech;
	ComPtr<ID3DX11EffectTechnique> Light2SkinnedTech;
	ComPtr<ID3DX11EffectTechnique> Light3SkinnedTech;

	ComPtr<ID3DX11EffectTechnique> Light0TexSkinnedTech;
	ComPtr<ID3DX11EffectTechnique> Light1TexSkinnedTech;
	ComPtr<ID3DX11EffectTechnique> Light2TexSkinnedTech;
	ComPtr<ID3DX11EffectTechnique> Light3TexSkinnedTech;

	ComPtr<ID3DX11EffectTechnique> Light0TexAlphaClipSkinnedTech;
	ComPtr<ID3DX11EffectTechnique> Light1TexAlphaClipSkinnedTech;
	ComPtr<ID3DX11EffectTechnique> Light2TexAlphaClipSkinnedTech;
	ComPtr<ID3DX11EffectTechnique> Light3TexAlphaClipSkinnedTech;

	ComPtr<ID3DX11EffectTechnique> Light1FogSkinnedTech;
	ComPtr<ID3DX11EffectTechnique> Light2FogSkinnedTech;
	ComPtr<ID3DX11EffectTechnique> Light3FogSkinnedTech;

	ComPtr<ID3DX11EffectTechnique> Light0TexFogSkinnedTech;
	ComPtr<ID3DX11EffectTechnique> Light1TexFogSkinnedTech;
	ComPtr<ID3DX11EffectTechnique> Light2TexFogSkinnedTech;
	ComPtr<ID3DX11EffectTechnique> Light3TexFogSkinnedTech;

	ComPtr<ID3DX11EffectTechnique> Light0TexAlphaClipFogSkinnedTech;
	ComPtr<ID3DX11EffectTechnique> Light1TexAlphaClipFogSkinnedTech;
	ComPtr<ID3DX11EffectTechnique> Light2TexAlphaClipFogSkinnedTech;
	ComPtr<ID3DX11EffectTechnique> Light3TexAlphaClipFogSkinnedTech;

	ComPtr<ID3DX11EffectTechnique> Light1ReflectSkinnedTech;
	ComPtr<ID3DX11EffectTechnique> Light2ReflectSkinnedTech;
	ComPtr<ID3DX11EffectTechnique> Light3ReflectSkinnedTech;

	ComPtr<ID3DX11EffectTechnique> Light0TexReflectSkinnedTech;
	ComPtr<ID3DX11EffectTechnique> Light1TexReflectSkinnedTech;
	ComPtr<ID3DX11EffectTechnique> Light2TexReflectSkinnedTech;
	ComPtr<ID3DX11EffectTechnique> Light3TexReflectSkinnedTech;

	ComPtr<ID3DX11EffectTechnique> Light0TexAlphaClipReflectSkinnedTech;
	ComPtr<ID3DX11EffectTechnique> Light1TexAlphaClipReflectSkinnedTech;
	ComPtr<ID3DX11EffectTechnique> Light2TexAlphaClipReflectSkinnedTech;
	ComPtr<ID3DX11EffectTechnique> Light3TexAlphaClipReflectSkinnedTech;

	ComPtr<ID3DX11EffectTechnique> Light1FogReflectSkinnedTech;
	ComPtr<ID3DX11EffectTechnique> Light2FogReflectSkinnedTech;
	ComPtr<ID3DX11EffectTechnique> Light3FogReflectSkinnedTech;

	ComPtr<ID3DX11EffectTechnique> Light0TexFogReflectSkinnedTech;
	ComPtr<ID3DX11EffectTechnique> Light1TexFogReflectSkinnedTech;
	ComPtr<ID3DX11EffectTechnique> Light2TexFogReflectSkinnedTech;
	ComPtr<ID3DX11EffectTechnique> Light3TexFogReflectSkinnedTech;

	ComPtr<ID3DX11EffectTechnique> Light0TexAlphaClipFogReflectSkinnedTech;
	ComPtr<ID3DX11EffectTechnique> Light1TexAlphaClipFogReflectSkinnedTech;
	ComPtr<ID3DX11EffectTechnique> Light2TexAlphaClipFogReflectSkinnedTech;
	ComPtr<ID3DX11EffectTechnique> Light3TexAlphaClipFogReflectSkinnedTech;

	ComPtr<ID3DX11EffectMatrixVariable> WorldViewProj;
	ComPtr<ID3DX11EffectMatrixVariable> WorldViewProjTex;
	ComPtr<ID3DX11EffectMatrixVariable> World;
	ComPtr<ID3DX11EffectMatrixVariable> WorldInvTranspose;
	ComPtr<ID3DX11EffectMatrixVariable> BoneTransforms;
	ComPtr<ID3DX11EffectMatrixVariable> ShadowTransform;
	ComPtr<ID3DX11EffectMatrixVariable> TexTransform;
	ComPtr<ID3DX11EffectVectorVariable> EyePosW;
	ComPtr<ID3DX11EffectVectorVariable> FogColor;
	ComPtr<ID3DX11EffectScalarVariable> FogStart;
	ComPtr<ID3DX11EffectScalarVariable> FogRange;
	ComPtr<ID3DX11EffectVariable> DirLights;
	ComPtr<ID3DX11EffectVariable> Mat;

	ComPtr<ID3DX11EffectShaderResourceVariable> DiffuseMap;
	ComPtr<ID3DX11EffectShaderResourceVariable> CubeMap;
	ComPtr<ID3DX11EffectShaderResourceVariable> NormalMap;
	ComPtr<ID3DX11EffectShaderResourceVariable> ShadowMap;
	ComPtr<ID3DX11EffectShaderResourceVariable> SsaoMap;
};

