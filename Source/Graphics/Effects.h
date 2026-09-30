#pragma once
#include "LightHelper.h"
#include "ShaderSetting.h"

class Effect
{
public:
	Effect(ComPtr<ID3D11Device> device, const std::wstring& filename);
	virtual ~Effect();

private:
	Effect(const Effect& rhs);
	Effect& operator=(const Effect& rhs);

protected:
	ComPtr<ID3DX11Effect> _fx;
};

class BasicEffect : public Effect
{
public:
	BasicEffect(ComPtr<ID3D11Device> device, const std::wstring& filename);
	~BasicEffect();

	void SetWorldViewProj(CXMMATRIX M) { WorldViewProj->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetWorldViewProjTex(CXMMATRIX M) { WorldViewProjTex->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetWorld(CXMMATRIX M) { World->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetWorldInvTranspose(CXMMATRIX M) { WorldInvTranspose->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetShadowTransform(CXMMATRIX M) { ShadowTransform->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetTexTransform(CXMMATRIX M) { TexTransform->SetMatrix(reinterpret_cast<const float*>(&M)); }	
	void SetEyePosW(const XMFLOAT3& v) { EyePosW->SetRawValue(&v, 0, sizeof(XMFLOAT3)); }
	void SetFogColor(const FXMVECTOR v) { FogColor->SetFloatVector(reinterpret_cast<const float*>(&v)); }
	void SetFogStart(float f) { FogStart->SetFloat(f); }
	void SetFogRange(float f) { FogRange->SetFloat(f); }
	void SetDirLights(const DirectionalLight* lights) { DirLights->SetRawValue(lights, 0, 3 * sizeof(DirectionalLight)); }
	void SetMaterial(const Material& mat) { Mat->SetRawValue(&mat, 0, sizeof(Material)); }
	
	void SetDiffuseMap(ID3D11ShaderResourceView* tex) { DiffuseMap->SetResource(tex); }
	void SetShadowMap(ID3D11ShaderResourceView* tex) { ShadowMap->SetResource(tex); }
	void SetSsaoMap(ID3D11ShaderResourceView* tex) { SsaoMap->SetResource(tex); }
	void SetCubeMap(ID3D11ShaderResourceView* tex) { CubeMap->SetResource(tex); }

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

	// NEW
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

	ComPtr<ID3DX11EffectMatrixVariable> WorldViewProj;
	ComPtr<ID3DX11EffectMatrixVariable> WorldViewProjTex;
	ComPtr<ID3DX11EffectMatrixVariable> World;
	ComPtr<ID3DX11EffectMatrixVariable> WorldInvTranspose;
	ComPtr<ID3DX11EffectMatrixVariable> ShadowTransform;
	ComPtr<ID3DX11EffectMatrixVariable> TexTransform;
	ComPtr<ID3DX11EffectVectorVariable> EyePosW;
	ComPtr<ID3DX11EffectVectorVariable> FogColor;
	ComPtr<ID3DX11EffectScalarVariable> FogStart;
	ComPtr<ID3DX11EffectScalarVariable> FogRange;
	ComPtr<ID3DX11EffectVariable> DirLights;
	ComPtr<ID3DX11EffectVariable> Mat;

	ComPtr<ID3DX11EffectShaderResourceVariable> DiffuseMap;
	ComPtr<ID3DX11EffectShaderResourceVariable> ShadowMap;
	ComPtr<ID3DX11EffectShaderResourceVariable> SsaoMap;
	ComPtr<ID3DX11EffectShaderResourceVariable> CubeMap;
};

class TreeSpriteEffect : public Effect
{
public:
	TreeSpriteEffect(ComPtr<ID3D11Device> device, const std::wstring& filename);
	~TreeSpriteEffect();

	void SetViewProj(CXMMATRIX M) { ViewProj->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetEyePosW(const XMFLOAT3& v) { EyePosW->SetRawValue(&v, 0, sizeof(XMFLOAT3)); }
	void SetFogColor(const FXMVECTOR v) { FogColor->SetFloatVector(reinterpret_cast<const float*>(&v)); }
	void SetFogStart(float f) { FogStart->SetFloat(f); }
	void SetFogRange(float f) { FogRange->SetFloat(f); }
	void SetDirLights(const DirectionalLight* lights) { DirLights->SetRawValue(lights, 0, 3 * sizeof(DirectionalLight)); }
	void SetMaterial(const Material& mat) { Mat->SetRawValue(&mat, 0, sizeof(Material)); }
	void SetTreeTextureMapArray(ID3D11ShaderResourceView* tex) { TreeTextureMapArray->SetResource(tex); }

	ComPtr<ID3DX11EffectTechnique> Light3Tech;
	ComPtr<ID3DX11EffectTechnique> Light3TexAlphaClipTech;
	ComPtr<ID3DX11EffectTechnique> Light3TexAlphaClipFogTech;

	ComPtr<ID3DX11EffectMatrixVariable> ViewProj;
	ComPtr<ID3DX11EffectVectorVariable> EyePosW;
	ComPtr<ID3DX11EffectVectorVariable> FogColor;
	ComPtr<ID3DX11EffectScalarVariable> FogStart;
	ComPtr<ID3DX11EffectScalarVariable> FogRange;
	ComPtr<ID3DX11EffectVariable> DirLights;
	ComPtr<ID3DX11EffectVariable> Mat;

	ComPtr<ID3DX11EffectShaderResourceVariable> TreeTextureMapArray;
};

class VecAddEffect : public Effect
{
public:
	VecAddEffect(ComPtr<ID3D11Device> device, const std::wstring& filename);
	~VecAddEffect();

	void SetInputA(ComPtr<ID3D11ShaderResourceView> srv) { InputA->SetResource(srv.Get()); }
	void SetInputB(ComPtr<ID3D11ShaderResourceView> srv) { InputB->SetResource(srv.Get()); }
	void SetOutput(ComPtr<ID3D11UnorderedAccessView> uav) { Output->SetUnorderedAccessView(uav.Get()); }

	ComPtr<ID3DX11EffectTechnique> VecAddTech;

	ComPtr<ID3DX11EffectShaderResourceVariable> InputA;
	ComPtr<ID3DX11EffectShaderResourceVariable> InputB;
	ComPtr<ID3DX11EffectUnorderedAccessViewVariable> Output;
};

class BlurEffect : public Effect
{
public:
	BlurEffect(ComPtr<ID3D11Device> device, const std::wstring& filename);
	~BlurEffect();

	void SetWeights(const float weights[9]) { Weights->SetFloatArray(weights, 0, 9); }
	void SetInputMap(ComPtr<ID3D11ShaderResourceView> tex) { InputMap->SetResource(tex.Get()); }
	void SetOutputMap(ComPtr<ID3D11UnorderedAccessView> tex) { OutputMap->SetUnorderedAccessView(tex.Get()); }

	ComPtr<ID3DX11EffectTechnique> HorzBlurTech;
	ComPtr<ID3DX11EffectTechnique> VertBlurTech;

	ComPtr<ID3DX11EffectScalarVariable> Weights;
	ComPtr<ID3DX11EffectShaderResourceVariable> InputMap;
	ComPtr<ID3DX11EffectUnorderedAccessViewVariable> OutputMap;
};

class TessellationEffect : public Effect
{
public:
	TessellationEffect(ComPtr<ID3D11Device> device, const std::wstring& filename);
	~TessellationEffect();

	void SetWorldViewProj(CXMMATRIX M) { WorldViewProj->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetWorld(CXMMATRIX M) { World->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetWorldInvTranspose(CXMMATRIX M) { WorldInvTranspose->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetTexTransform(CXMMATRIX M) { TexTransform->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetEyePosW(const XMFLOAT3& v) { EyePosW->SetRawValue(&v, 0, sizeof(XMFLOAT3)); }
	void SetFogColor(const FXMVECTOR v) { FogColor->SetFloatVector(reinterpret_cast<const float*>(&v)); }
	void SetFogStart(float f) { FogStart->SetFloat(f); }
	void SetFogRange(float f) { FogRange->SetFloat(f); }
	void SetDirLights(const DirectionalLight* lights) { DirLights->SetRawValue(lights, 0, 3 * sizeof(DirectionalLight)); }
	void SetMaterial(const Material& mat) { Mat->SetRawValue(&mat, 0, sizeof(Material)); }
	void SetDiffuseMap(ID3D11ShaderResourceView* tex) { DiffuseMap->SetResource(tex); }

	ComPtr<ID3DX11EffectTechnique> TessTech;

	ComPtr<ID3DX11EffectMatrixVariable> WorldViewProj;
	ComPtr<ID3DX11EffectMatrixVariable> World;
	ComPtr<ID3DX11EffectMatrixVariable> WorldInvTranspose;
	ComPtr<ID3DX11EffectMatrixVariable> TexTransform;
	ComPtr<ID3DX11EffectVectorVariable> EyePosW;
	ComPtr<ID3DX11EffectVectorVariable> FogColor;
	ComPtr<ID3DX11EffectScalarVariable> FogStart;
	ComPtr<ID3DX11EffectScalarVariable> FogRange;
	ComPtr<ID3DX11EffectVariable> DirLights;
	ComPtr<ID3DX11EffectVariable> Mat;

	ComPtr<ID3DX11EffectShaderResourceVariable> DiffuseMap;
};

class BezierTessellationEffect : public Effect
{
public:
	BezierTessellationEffect(ComPtr<ID3D11Device> device, const std::wstring& filename);
	~BezierTessellationEffect();

	void SetWorldViewProj(CXMMATRIX M) { WorldViewProj->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetWorld(CXMMATRIX M) { World->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetWorldInvTranspose(CXMMATRIX M) { WorldInvTranspose->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetTexTransform(CXMMATRIX M) { TexTransform->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetEyePosW(const XMFLOAT3& v) { EyePosW->SetRawValue(&v, 0, sizeof(XMFLOAT3)); }
	void SetFogColor(const FXMVECTOR v) { FogColor->SetFloatVector(reinterpret_cast<const float*>(&v)); }
	void SetFogStart(float f) { FogStart->SetFloat(f); }
	void SetFogRange(float f) { FogRange->SetFloat(f); }
	void SetDirLights(const DirectionalLight* lights) { DirLights->SetRawValue(lights, 0, 3 * sizeof(DirectionalLight)); }
	void SetMaterial(const Material& mat) { Mat->SetRawValue(&mat, 0, sizeof(Material)); }
	void SetDiffuseMap(ID3D11ShaderResourceView* tex) { DiffuseMap->SetResource(tex); }

	ComPtr<ID3DX11EffectTechnique> TessTech;

	ComPtr<ID3DX11EffectMatrixVariable> WorldViewProj;
	ComPtr<ID3DX11EffectMatrixVariable> World;
	ComPtr<ID3DX11EffectMatrixVariable> WorldInvTranspose;
	ComPtr<ID3DX11EffectMatrixVariable> TexTransform;
	ComPtr<ID3DX11EffectVectorVariable> EyePosW;
	ComPtr<ID3DX11EffectVectorVariable> FogColor;
	ComPtr<ID3DX11EffectScalarVariable> FogStart;
	ComPtr<ID3DX11EffectScalarVariable> FogRange;
	ComPtr<ID3DX11EffectVariable> DirLights;
	ComPtr<ID3DX11EffectVariable> Mat;

	ComPtr<ID3DX11EffectShaderResourceVariable> DiffuseMap;
};

class InstancedBasicEffect : public Effect
{
public:
	InstancedBasicEffect(ComPtr<ID3D11Device> device, const std::wstring& filename);
	~InstancedBasicEffect();
	
	void SetWorldViewProj(CXMMATRIX M) { WorldViewProj->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetWorldViewProjTex(CXMMATRIX M) { WorldViewProjTex->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetWorld(CXMMATRIX M) { World->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetWorldInvTranspose(CXMMATRIX M) { WorldInvTranspose->SetMatrix(reinterpret_cast<const float*>(&M)); }
	//void SetView(CXMMATRIX M) { View->SetMatrix(reinterpret_cast<const float*>(&M)); }
	//void SetProj(CXMMATRIX M) { Proj->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetViewProj(CXMMATRIX M) { ViewProj->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetViewProjTex(CXMMATRIX M) { ViewProjTex->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetBoneTransforms(const XMFLOAT4X4* M, int cnt) { BoneTransforms->SetMatrixArray(reinterpret_cast<const float*>(M), 0, cnt); }
	void SetTexTransform(CXMMATRIX M) { TexTransform->SetMatrix(reinterpret_cast<const float*>(&M)); }
	
	void SetDirShadowTransforms(const XMMATRIX* M, int cnt) { DirShadowTransforms->SetMatrixArray(reinterpret_cast<const float*>(M), 0, cnt); }
	void SetSpotShadowTransforms(const XMMATRIX* M, int cnt) { SpotShadowTransforms->SetMatrixArray(reinterpret_cast<const float*>(M), 0, cnt); }
	void SetPointShadowTransforms(const XMMATRIX* M, int cnt) { PointShadowTransforms->SetMatrixArray(reinterpret_cast<const float*>(M), 0, cnt); }

	void SetEyePosW(const XMFLOAT3& v) { EyePosW->SetRawValue(&v, 0, sizeof(XMFLOAT3)); }
	void SetFogColor(const FXMVECTOR v) { FogColor->SetFloatVector(reinterpret_cast<const float*>(&v)); }
	void SetFogStart(float f) { FogStart->SetFloat(f); }
	void SetFogRange(float f) { FogRange->SetFloat(f); }
	/*
	template <typename T, int maxSize>
	void SetLights(const T* lights, int cnt, ComPtr<ID3DX11EffectVariable> LightEffect, ComPtr<ID3DX11EffectScalarVariable> LightCountEffect)
	{
		T lightArray[maxSize];

		// 실제 라이트 데이터를 배열에 복사
		for (int i = 0; i < cnt; i++)
		{
			lightArray[i] = lights[i];
		}

		// 기본값으로 초기화
		T temp;
		temp.Init();
		for (int i = cnt; i < maxSize; i++)
		{
			lightArray[i] = temp;
		}

		LightEffect->SetRawValue(lightArray, 0, maxSize * sizeof(T));

		// 라이트 개수 설정
		LightCountEffect->SetInt(cnt);
	}
	*/
	void SetDirLights(const DirectionalLight* lights, int cnt)
	{ 
		DirLights->SetRawValue(lights, 0, cnt * sizeof(DirectionalLight));
		DirLightCount->SetInt(cnt);
	}
	void SetPointLights(const PointLight* lights, int cnt) 
	{
		PointLights->SetRawValue(lights, 0, cnt * sizeof(PointLight));
		PointLightCount->SetInt(cnt);
	}
	void SetSpotLights(const SpotLight* lights, int cnt) 
	{ 
		SpotLights->SetRawValue(lights, 0, cnt * sizeof(SpotLight));
		SpotLightCount->SetInt(cnt);
	}

	void SetMaterial(const Material& mat) { Mat->SetRawValue(&mat, 0, sizeof(Material)); }
	void SetShaderSetting(const ShaderSetting& setting) { Setting->SetRawValue(&setting, 0, sizeof(ShaderSetting)); }
	
	void SetDiffuseMap(ID3D11ShaderResourceView* tex) { DiffuseMap->SetResource(tex); }
	
	void SetDirShadowMaps(ID3D11ShaderResourceView** tex, int cnt) { DirShadowMaps->SetResourceArray(tex, 0, cnt); }
	void SetSpotShadowMaps(ID3D11ShaderResourceView** tex, int cnt) { SpotShadowMaps->SetResourceArray(tex, 0, cnt); }
	void SetPointShadowMaps(ID3D11ShaderResourceView** tex, int cnt) { PointShadowMaps->SetResourceArray(tex, 0, cnt); }

	void SetNormalMap(ID3D11ShaderResourceView* tex) { NormalMap->SetResource(tex); }
	void SetSsaoMap(ID3D11ShaderResourceView* tex) { SsaoMap->SetResource(tex); }
	void SetCubeMap(ID3D11ShaderResourceView* tex) { CubeMap->SetResource(tex); }

	ComPtr<ID3DX11EffectTechnique> Tech;
	ComPtr<ID3DX11EffectTechnique> InstancingTech;
	ComPtr<ID3DX11EffectTechnique> SkinnedTech;

	ComPtr<ID3DX11EffectMatrixVariable> World;
	ComPtr<ID3DX11EffectMatrixVariable> WorldInvTranspose;
	ComPtr<ID3DX11EffectMatrixVariable> WorldViewProj;
	ComPtr<ID3DX11EffectMatrixVariable> WorldViewProjTex;

	//ComPtr<ID3DX11EffectMatrixVariable> View;
	//ComPtr<ID3DX11EffectMatrixVariable> Proj;
	ComPtr<ID3DX11EffectMatrixVariable> ViewProj;
	ComPtr<ID3DX11EffectMatrixVariable> ViewProjTex;
	ComPtr<ID3DX11EffectMatrixVariable> BoneTransforms;
	ComPtr<ID3DX11EffectMatrixVariable> TexTransform;
	
	ComPtr<ID3DX11EffectMatrixVariable> DirShadowTransforms;
	ComPtr<ID3DX11EffectMatrixVariable> SpotShadowTransforms;
	ComPtr<ID3DX11EffectMatrixVariable> PointShadowTransforms;

	ComPtr<ID3DX11EffectVectorVariable> EyePosW;
	ComPtr<ID3DX11EffectVectorVariable> FogColor;
	ComPtr<ID3DX11EffectScalarVariable> FogStart;
	ComPtr<ID3DX11EffectScalarVariable> FogRange;

	ComPtr<ID3DX11EffectVariable> DirLights;
	ComPtr<ID3DX11EffectVariable> PointLights;
	ComPtr<ID3DX11EffectVariable> SpotLights;
	ComPtr<ID3DX11EffectScalarVariable> DirLightCount;
	ComPtr<ID3DX11EffectScalarVariable> PointLightCount;
	ComPtr<ID3DX11EffectScalarVariable> SpotLightCount;

	ComPtr<ID3DX11EffectVariable> Mat;
	ComPtr<ID3DX11EffectVariable> Setting;

	ComPtr<ID3DX11EffectShaderResourceVariable> DiffuseMap;
	
	ComPtr<ID3DX11EffectShaderResourceVariable> DirShadowMaps;
	ComPtr<ID3DX11EffectShaderResourceVariable> SpotShadowMaps;
	ComPtr<ID3DX11EffectShaderResourceVariable> PointShadowMaps;

	ComPtr<ID3DX11EffectShaderResourceVariable> NormalMap;
	ComPtr<ID3DX11EffectShaderResourceVariable> SsaoMap;
	ComPtr<ID3DX11EffectShaderResourceVariable> CubeMap;
};

class SkyEffect : public Effect
{
public:
	SkyEffect(ComPtr<ID3D11Device> device, const std::wstring& filename);
	~SkyEffect();

	void SetWorldViewProj(CXMMATRIX M) { WorldViewProj->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetCubeMap(ID3D11ShaderResourceView* cubemap) { CubeMap->SetResource(cubemap); }

	ComPtr<ID3DX11EffectTechnique> SkyTech;
	ComPtr<ID3DX11EffectMatrixVariable> WorldViewProj;
	ComPtr<ID3DX11EffectShaderResourceVariable> CubeMap;
};

class NormalMapEffect : public Effect
{
public:
	NormalMapEffect(ComPtr<ID3D11Device> device, const std::wstring& filename);
	~NormalMapEffect();

	void SetWorldViewProj(CXMMATRIX M) { WorldViewProj->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetWorldViewProjTex(CXMMATRIX M) { WorldViewProjTex->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetWorld(CXMMATRIX M) { World->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetWorldInvTranspose(CXMMATRIX M) { WorldInvTranspose->SetMatrix(reinterpret_cast<const float*>(&M)); }
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

	ComPtr<ID3DX11EffectMatrixVariable> WorldViewProj;
	ComPtr<ID3DX11EffectMatrixVariable> WorldViewProjTex;
	ComPtr<ID3DX11EffectMatrixVariable> World;
	ComPtr<ID3DX11EffectMatrixVariable> WorldInvTranspose;
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

class DisplacementMapEffect : public Effect
{
public:
	DisplacementMapEffect(ComPtr<ID3D11Device> device, const std::wstring& filename);
	~DisplacementMapEffect();

	void SetViewProj(CXMMATRIX M) { ViewProj->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetWorldViewProj(CXMMATRIX M) { WorldViewProj->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetWorld(CXMMATRIX M) { World->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetWorldInvTranspose(CXMMATRIX M) { WorldInvTranspose->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetShadowTransform(CXMMATRIX M) { ShadowTransform->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetTexTransform(CXMMATRIX M) { TexTransform->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetEyePosW(const XMFLOAT3& v) { EyePosW->SetRawValue(&v, 0, sizeof(XMFLOAT3)); }
	void SetFogColor(const FXMVECTOR v) { FogColor->SetFloatVector(reinterpret_cast<const float*>(&v)); }
	void SetFogStart(float f) { FogStart->SetFloat(f); }
	void SetFogRange(float f) { FogRange->SetFloat(f); }
	void SetDirLights(const DirectionalLight* lights) { DirLights->SetRawValue(lights, 0, 3 * sizeof(DirectionalLight)); }
	void SetMaterial(const Material& mat) { Mat->SetRawValue(&mat, 0, sizeof(Material)); }
	void SetHeightScale(float f) { HeightScale->SetFloat(f); }
	void SetMaxTessDistance(float f) { MaxTessDistance->SetFloat(f); }
	void SetMinTessDistance(float f) { MinTessDistance->SetFloat(f); }
	void SetMinTessFactor(float f) { MinTessFactor->SetFloat(f); }
	void SetMaxTessFactor(float f) { MaxTessFactor->SetFloat(f); }

	void SetDiffuseMap(ID3D11ShaderResourceView* tex) { DiffuseMap->SetResource(tex); }
	void SetCubeMap(ID3D11ShaderResourceView* tex) { CubeMap->SetResource(tex); }
	void SetNormalMap(ID3D11ShaderResourceView* tex) { NormalMap->SetResource(tex); }
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

	ComPtr<ID3DX11EffectMatrixVariable> ViewProj;
	ComPtr<ID3DX11EffectMatrixVariable> WorldViewProj;
	ComPtr<ID3DX11EffectMatrixVariable> World;
	ComPtr<ID3DX11EffectMatrixVariable> WorldInvTranspose;
	ComPtr<ID3DX11EffectMatrixVariable> ShadowTransform;
	ComPtr<ID3DX11EffectMatrixVariable> TexTransform;
	ComPtr<ID3DX11EffectVectorVariable> EyePosW;
	ComPtr<ID3DX11EffectVectorVariable> FogColor;
	ComPtr<ID3DX11EffectScalarVariable> FogStart;
	ComPtr<ID3DX11EffectScalarVariable> FogRange;
	ComPtr<ID3DX11EffectVariable> DirLights;
	ComPtr<ID3DX11EffectVariable> Mat;
	ComPtr<ID3DX11EffectScalarVariable> HeightScale;
	ComPtr<ID3DX11EffectScalarVariable> MaxTessDistance;
	ComPtr<ID3DX11EffectScalarVariable> MinTessDistance;
	ComPtr<ID3DX11EffectScalarVariable> MinTessFactor;
	ComPtr<ID3DX11EffectScalarVariable> MaxTessFactor;

	ComPtr<ID3DX11EffectShaderResourceVariable> DiffuseMap;
	ComPtr<ID3DX11EffectShaderResourceVariable> CubeMap;
	ComPtr<ID3DX11EffectShaderResourceVariable> NormalMap;
	ComPtr<ID3DX11EffectShaderResourceVariable> ShadowMap;
};

class TerrainEffect : public Effect
{
public:
	TerrainEffect(ComPtr<ID3D11Device> device, const std::wstring& filename);
	~TerrainEffect();

	void SetViewProj(CXMMATRIX M) { ViewProj->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetEyePosW(const XMFLOAT3& v) { EyePosW->SetRawValue(&v, 0, sizeof(XMFLOAT3)); }
	void SetFogColor(const FXMVECTOR v) { FogColor->SetFloatVector(reinterpret_cast<const float*>(&v)); }
	void SetFogStart(float f) { FogStart->SetFloat(f); }
	void SetFogRange(float f) { FogRange->SetFloat(f); }
	void SetDirLights(const DirectionalLight* lights) { DirLights->SetRawValue(lights, 0, 3 * sizeof(DirectionalLight)); }
	void SetMaterial(const Material& mat) { Mat->SetRawValue(&mat, 0, sizeof(Material)); }

	void SetMinDist(float f) { MinDist->SetFloat(f); }
	void SetMaxDist(float f) { MaxDist->SetFloat(f); }
	void SetMinTess(float f) { MinTess->SetFloat(f); }
	void SetMaxTess(float f) { MaxTess->SetFloat(f); }
	void SetTexelCellSpaceU(float f) { TexelCellSpaceU->SetFloat(f); }
	void SetTexelCellSpaceV(float f) { TexelCellSpaceV->SetFloat(f); }
	void SetWorldCellSpace(float f) { WorldCellSpace->SetFloat(f); }
	void SetWorldFrustumPlanes(XMFLOAT4 planes[6]) { WorldFrustumPlanes->SetFloatVectorArray(reinterpret_cast<float*>(planes), 0, 6); }

	void SetLayerMapArray(ID3D11ShaderResourceView* tex) { LayerMapArray->SetResource(tex); }
	void SetBlendMap(ID3D11ShaderResourceView* tex) { BlendMap->SetResource(tex); }
	void SetHeightMap(ID3D11ShaderResourceView* tex) { HeightMap->SetResource(tex); }


	ComPtr<ID3DX11EffectTechnique> Light1Tech;
	ComPtr<ID3DX11EffectTechnique> Light2Tech;
	ComPtr<ID3DX11EffectTechnique> Light3Tech;
	ComPtr<ID3DX11EffectTechnique> Light1FogTech;
	ComPtr<ID3DX11EffectTechnique> Light2FogTech;
	ComPtr<ID3DX11EffectTechnique> Light3FogTech;

	ComPtr<ID3DX11EffectMatrixVariable> ViewProj;
	ComPtr<ID3DX11EffectMatrixVariable> World;
	ComPtr<ID3DX11EffectMatrixVariable> WorldInvTranspose;
	ComPtr<ID3DX11EffectMatrixVariable> TexTransform;
	ComPtr<ID3DX11EffectVectorVariable> EyePosW;
	ComPtr<ID3DX11EffectVectorVariable> FogColor;
	ComPtr<ID3DX11EffectScalarVariable> FogStart;
	ComPtr<ID3DX11EffectScalarVariable> FogRange;
	ComPtr<ID3DX11EffectVariable> DirLights;
	ComPtr<ID3DX11EffectVariable> Mat;
	ComPtr<ID3DX11EffectScalarVariable> MinDist;
	ComPtr<ID3DX11EffectScalarVariable> MaxDist;
	ComPtr<ID3DX11EffectScalarVariable> MinTess;
	ComPtr<ID3DX11EffectScalarVariable> MaxTess;
	ComPtr<ID3DX11EffectScalarVariable> TexelCellSpaceU;
	ComPtr<ID3DX11EffectScalarVariable> TexelCellSpaceV;
	ComPtr<ID3DX11EffectScalarVariable> WorldCellSpace;
	ComPtr<ID3DX11EffectVectorVariable> WorldFrustumPlanes;

	ComPtr<ID3DX11EffectShaderResourceVariable> LayerMapArray;
	ComPtr<ID3DX11EffectShaderResourceVariable> BlendMap;
	ComPtr<ID3DX11EffectShaderResourceVariable> HeightMap;
};

class ParticleEffect : public Effect
{
public:
	ParticleEffect(ComPtr<ID3D11Device> device, const std::wstring& filename);
	~ParticleEffect();

	void SetViewProj(CXMMATRIX M) { ViewProj->SetMatrix(reinterpret_cast<const float*>(&M)); }

	void SetGameTime(float f) { GameTime->SetFloat(f); }
	void SetTimeStep(float f) { TimeStep->SetFloat(f); }

	void SetEyePosW(const XMFLOAT3& v) { EyePosW->SetRawValue(&v, 0, sizeof(XMFLOAT3)); }
	void SetEmitPosW(const XMFLOAT3& v) { EmitPosW->SetRawValue(&v, 0, sizeof(XMFLOAT3)); }
	void SetEmitDirW(const XMFLOAT3& v) { EmitDirW->SetRawValue(&v, 0, sizeof(XMFLOAT3)); }

	void SetTexArray(ID3D11ShaderResourceView* tex) { TexArray->SetResource(tex); }
	void SetRandomTex(ID3D11ShaderResourceView* tex) { RandomTex->SetResource(tex); }

	ComPtr<ID3DX11EffectTechnique> StreamOutTech;
	ComPtr<ID3DX11EffectTechnique> DrawTech;

	ComPtr<ID3DX11EffectMatrixVariable> ViewProj;
	ComPtr<ID3DX11EffectScalarVariable> GameTime;
	ComPtr<ID3DX11EffectScalarVariable> TimeStep;
	ComPtr<ID3DX11EffectVectorVariable> EyePosW;
	ComPtr<ID3DX11EffectVectorVariable> EmitPosW;
	ComPtr<ID3DX11EffectVectorVariable> EmitDirW;
	ComPtr<ID3DX11EffectShaderResourceVariable> TexArray;
	ComPtr<ID3DX11EffectShaderResourceVariable> RandomTex;
};

class BuildShadowMapEffect : public Effect
{
public:
	BuildShadowMapEffect(ComPtr<ID3D11Device> device, const std::wstring& filename);
	~BuildShadowMapEffect();

	void SetViewProj(CXMMATRIX M) { ViewProj->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetWorldViewProj(CXMMATRIX M) { WorldViewProj->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetWorld(CXMMATRIX M) { World->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetWorldInvTranspose(CXMMATRIX M) { WorldInvTranspose->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetBoneTransforms(const XMFLOAT4X4* M, int32 cnt) { BoneTransforms->SetMatrixArray(reinterpret_cast<const float*>(M), 0, cnt); }
	void SetTexTransform(CXMMATRIX M) { TexTransform->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetEyePosW(const XMFLOAT3& v) { EyePosW->SetRawValue(&v, 0, sizeof(XMFLOAT3)); }

	void SetHeightScale(float f) { HeightScale->SetFloat(f); }
	void SetMaxTessDistance(float f) { MaxTessDistance->SetFloat(f); }
	void SetMinTessDistance(float f) { MinTessDistance->SetFloat(f); }
	void SetMinTessFactor(float f) { MinTessFactor->SetFloat(f); }
	void SetMaxTessFactor(float f) { MaxTessFactor->SetFloat(f); }

	void SetDiffuseMap(ID3D11ShaderResourceView* tex) { DiffuseMap->SetResource(tex); }
	void SetNormalMap(ID3D11ShaderResourceView* tex) { NormalMap->SetResource(tex); }

	ComPtr<ID3DX11EffectTechnique> BuildShadowMapTech;
	ComPtr<ID3DX11EffectTechnique> BuildShadowMapAlphaClipTech;
	ComPtr<ID3DX11EffectTechnique> BuildShadowMapSkinnedTech;
	ComPtr<ID3DX11EffectTechnique> BuildShadowMapAlphaClipSkinnedTech;
	ComPtr<ID3DX11EffectTechnique> TessBuildShadowMapTech;
	ComPtr<ID3DX11EffectTechnique> TessBuildShadowMapAlphaClipTech;

	// NEW
	ComPtr<ID3DX11EffectTechnique> BuildShadowMapInstancingTech;
	ComPtr<ID3DX11EffectTechnique> BuildShadowMapAlphaClipInstancingTech;

	ComPtr<ID3DX11EffectMatrixVariable> ViewProj;
	ComPtr<ID3DX11EffectMatrixVariable> WorldViewProj;
	ComPtr<ID3DX11EffectMatrixVariable> World;
	ComPtr<ID3DX11EffectMatrixVariable> WorldInvTranspose;
	ComPtr<ID3DX11EffectMatrixVariable> BoneTransforms;
	ComPtr<ID3DX11EffectMatrixVariable> TexTransform;
	ComPtr<ID3DX11EffectVectorVariable> EyePosW;
	ComPtr<ID3DX11EffectScalarVariable> HeightScale;
	ComPtr<ID3DX11EffectScalarVariable> MaxTessDistance;
	ComPtr<ID3DX11EffectScalarVariable> MinTessDistance;
	ComPtr<ID3DX11EffectScalarVariable> MinTessFactor;
	ComPtr<ID3DX11EffectScalarVariable> MaxTessFactor;

	ComPtr<ID3DX11EffectShaderResourceVariable> DiffuseMap;
	ComPtr<ID3DX11EffectShaderResourceVariable> NormalMap;
};

class DebugTexEffect : public Effect
{
public:
	DebugTexEffect(ComPtr<ID3D11Device> device, const std::wstring& filename);
	~DebugTexEffect();

	void SetWorldViewProj(CXMMATRIX M) { WorldViewProj->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetTexture(ID3D11ShaderResourceView* tex) { Texture->SetResource(tex); }

	ComPtr<ID3DX11EffectTechnique> ViewArgbTech;
	ComPtr<ID3DX11EffectTechnique> ViewRedTech;
	ComPtr<ID3DX11EffectTechnique> ViewGreenTech;
	ComPtr<ID3DX11EffectTechnique> ViewBlueTech;
	ComPtr<ID3DX11EffectTechnique> ViewAlphaTech;

	ComPtr<ID3DX11EffectMatrixVariable> WorldViewProj;
	ComPtr<ID3DX11EffectShaderResourceVariable> Texture;
};

class AmbientOcclusionEffect : public Effect
{
public:
	AmbientOcclusionEffect(ComPtr<ID3D11Device> device, const std::wstring& filename);
	~AmbientOcclusionEffect();

	void SetWorldViewProj(CXMMATRIX M) { WorldViewProj->SetMatrix(reinterpret_cast<const float*>(&M)); }

	ComPtr<ID3DX11EffectTechnique> AmbientOcclusionTech;
	ComPtr<ID3DX11EffectMatrixVariable> WorldViewProj;
};

class SsaoNormalDepthEffect : public Effect
{
public:
	SsaoNormalDepthEffect(ComPtr<ID3D11Device> device, const std::wstring& filename);
	~SsaoNormalDepthEffect();

	// NEW
	void SetView(CXMMATRIX M) { View->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetProj(CXMMATRIX M) { Proj->SetMatrix(reinterpret_cast<const float*>(&M)); }

	void SetWorldView(CXMMATRIX M) { WorldView->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetWorldInvTransposeView(CXMMATRIX M) { WorldInvTransposeView->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetBoneTransforms(const XMFLOAT4X4* M, int cnt) { BoneTransforms->SetMatrixArray(reinterpret_cast<const float*>(M), 0, cnt); }
	void SetWorldViewProj(CXMMATRIX M) { WorldViewProj->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetTexTransform(CXMMATRIX M) { TexTransform->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetDiffuseMap(ID3D11ShaderResourceView* tex) { DiffuseMap->SetResource(tex); }

	ComPtr<ID3DX11EffectTechnique> NormalDepthTech;
	ComPtr<ID3DX11EffectTechnique> NormalDepthAlphaClipTech;
	ComPtr<ID3DX11EffectTechnique> NormalDepthSkinnedTech;
	ComPtr<ID3DX11EffectTechnique> NormalDepthAlphaClipSkinnedTech;

	// NEW
	ComPtr<ID3DX11EffectTechnique> NormalDepthInstancingTech;
	ComPtr<ID3DX11EffectTechnique> NormalDepthAlphaClipInstancingTech;

	ComPtr<ID3DX11EffectMatrixVariable> View;
	ComPtr<ID3DX11EffectMatrixVariable> Proj;
	ComPtr<ID3DX11EffectMatrixVariable> WorldView;
	ComPtr<ID3DX11EffectMatrixVariable> WorldInvTransposeView;
	ComPtr<ID3DX11EffectMatrixVariable> BoneTransforms;
	ComPtr<ID3DX11EffectMatrixVariable> WorldViewProj;
	ComPtr<ID3DX11EffectMatrixVariable> TexTransform;
	ComPtr<ID3DX11EffectShaderResourceVariable> DiffuseMap;
};

class SsaoEffect : public Effect
{
public:
	SsaoEffect(ComPtr<ID3D11Device> device, const std::wstring& filename);
	~SsaoEffect();

	void SetSsaoPower(float f) { SsaoPower->SetFloat(f); }
	void SetViewToTexSpace(CXMMATRIX M) { ViewToTexSpace->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetOffsetVectors(const XMFLOAT4 v[14]) { OffsetVectors->SetFloatVectorArray(reinterpret_cast<const float*>(v), 0, 14); }
	void SetFrustumCorners(const XMFLOAT4 v[4]) { FrustumCorners->SetFloatVectorArray(reinterpret_cast<const float*>(v), 0, 4); }
	void SetOcclusionRadius(float f) { OcclusionRadius->SetFloat(f); }
	void SetOcclusionFadeStart(float f) { OcclusionFadeStart->SetFloat(f); }
	void SetOcclusionFadeEnd(float f) { OcclusionFadeEnd->SetFloat(f); }
	void SetSurfaceEpsilon(float f) { SurfaceEpsilon->SetFloat(f); }

	void SetNormalDepthMap(ID3D11ShaderResourceView* srv) { NormalDepthMap->SetResource(srv); }
	void SetRandomVecMap(ID3D11ShaderResourceView* srv) { RandomVecMap->SetResource(srv); }

	ComPtr<ID3DX11EffectTechnique> SsaoTech;
	ComPtr<ID3DX11EffectScalarVariable> SsaoPower;
	ComPtr<ID3DX11EffectMatrixVariable> ViewToTexSpace;
	ComPtr<ID3DX11EffectVectorVariable> OffsetVectors;
	ComPtr<ID3DX11EffectVectorVariable> FrustumCorners;
	ComPtr<ID3DX11EffectScalarVariable> OcclusionRadius;
	ComPtr<ID3DX11EffectScalarVariable> OcclusionFadeStart;
	ComPtr<ID3DX11EffectScalarVariable> OcclusionFadeEnd;
	ComPtr<ID3DX11EffectScalarVariable> SurfaceEpsilon;
	ComPtr<ID3DX11EffectShaderResourceVariable> NormalDepthMap;
	ComPtr<ID3DX11EffectShaderResourceVariable> RandomVecMap;
};

class SsaoBlurEffect : public Effect
{
public:
	SsaoBlurEffect(ComPtr<ID3D11Device> device, const std::wstring& filename);
	~SsaoBlurEffect();

	void SetTexelWidth(float f) { TexelWidth->SetFloat(f); }
	void SetTexelHeight(float f) { TexelHeight->SetFloat(f); }

	void SetNormalDepthMap(ID3D11ShaderResourceView* srv) { NormalDepthMap->SetResource(srv); }
	void SetInputImage(ID3D11ShaderResourceView* srv) { InputImage->SetResource(srv); }

	ComPtr<ID3DX11EffectTechnique> HorzBlurTech;
	ComPtr<ID3DX11EffectTechnique> VertBlurTech;
	ComPtr<ID3DX11EffectScalarVariable> TexelWidth;
	ComPtr<ID3DX11EffectScalarVariable> TexelHeight;
	ComPtr<ID3DX11EffectShaderResourceVariable> NormalDepthMap;
	ComPtr<ID3DX11EffectShaderResourceVariable> InputImage;
};

class Effects
{
public:
	static void InitAll(ComPtr<ID3D11Device> device, const std::wstring& filename);
	static void DestroyAll();

	static shared_ptr<BasicEffect> BasicFX;
	static shared_ptr<TreeSpriteEffect> TreeSpriteFX;
	static shared_ptr<VecAddEffect> VecAddFX;
	static shared_ptr<BlurEffect> BlurFX;
	static shared_ptr<TessellationEffect> TessellationFX;
	static shared_ptr<BezierTessellationEffect> BezierTessellationFX;
	static shared_ptr<TessellationEffect> TriangleTessellationFX;

	static shared_ptr<InstancedBasicEffect> InstancedBasicFX;

	static shared_ptr<SkyEffect> SkyFX;
	static shared_ptr<NormalMapEffect> NormalMapFX;
	static shared_ptr<DisplacementMapEffect> DisplacementMapFX;
	static shared_ptr<TerrainEffect> TerrainFX;
	static shared_ptr<ParticleEffect> FireFX;
	static shared_ptr<ParticleEffect> RainFX;
	static shared_ptr<BuildShadowMapEffect> BuildShadowMapFX;
	static shared_ptr<DebugTexEffect> DebugTexFX;
	static shared_ptr<AmbientOcclusionEffect> AmbientOcclusionFX;
	static shared_ptr<SsaoNormalDepthEffect> SsaoNormalDepthFX;
	static shared_ptr<SsaoEffect> SsaoFX;
	static shared_ptr<SsaoBlurEffect> SsaoBlurFX;
};