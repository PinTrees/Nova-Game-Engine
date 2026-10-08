#pragma once
#include "LightHelper.h"
#include "ShaderSetting.h"
#include "PbrMaterial.h"

class Effect
{
public:
	Effect(ComPtr<GfxDevice> device, const std::wstring& filename);
	virtual ~Effect();

private:
	Effect(const Effect& rhs);
	Effect& operator=(const Effect& rhs);

public:
	// 이름으로 변수/기법을 직접 찾을 때 (지형 렌더러 등)
	FxEffect* GetFX() const { return _fx.Get(); }

protected:
	ComPtr<FxEffect> _fx;
};

class BasicEffect : public Effect
{
public:
	BasicEffect(ComPtr<GfxDevice> device, const std::wstring& filename);
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
	
	void SetDiffuseMap(GfxShaderResourceView* tex) { DiffuseMap->SetResource(tex); }
	void SetShadowMap(GfxShaderResourceView* tex) { ShadowMap->SetResource(tex); }
	void SetSsaoMap(GfxShaderResourceView* tex) { SsaoMap->SetResource(tex); }
	void SetCubeMap(GfxShaderResourceView* tex) { CubeMap->SetResource(tex); }

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

	// NEW
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

	ComPtr<FxVar> WorldViewProj;
	ComPtr<FxVar> WorldViewProjTex;
	ComPtr<FxVar> World;
	ComPtr<FxVar> WorldInvTranspose;
	ComPtr<FxVar> ShadowTransform;
	ComPtr<FxVar> TexTransform;
	ComPtr<FxVar> EyePosW;
	ComPtr<FxVar> FogColor;
	ComPtr<FxVar> FogStart;
	ComPtr<FxVar> FogRange;
	ComPtr<FxVar> DirLights;
	ComPtr<FxVar> Mat;

	ComPtr<FxVar> DiffuseMap;
	ComPtr<FxVar> ShadowMap;
	ComPtr<FxVar> SsaoMap;
	ComPtr<FxVar> CubeMap;
};

class TreeSpriteEffect : public Effect
{
public:
	TreeSpriteEffect(ComPtr<GfxDevice> device, const std::wstring& filename);
	~TreeSpriteEffect();

	void SetViewProj(CXMMATRIX M) { ViewProj->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetEyePosW(const XMFLOAT3& v) { EyePosW->SetRawValue(&v, 0, sizeof(XMFLOAT3)); }
	void SetFogColor(const FXMVECTOR v) { FogColor->SetFloatVector(reinterpret_cast<const float*>(&v)); }
	void SetFogStart(float f) { FogStart->SetFloat(f); }
	void SetFogRange(float f) { FogRange->SetFloat(f); }
	void SetDirLights(const DirectionalLight* lights) { DirLights->SetRawValue(lights, 0, 3 * sizeof(DirectionalLight)); }
	void SetMaterial(const Material& mat) { Mat->SetRawValue(&mat, 0, sizeof(Material)); }
	void SetTreeTextureMapArray(GfxShaderResourceView* tex) { TreeTextureMapArray->SetResource(tex); }

	ComPtr<FxTechnique> Light3Tech;
	ComPtr<FxTechnique> Light3TexAlphaClipTech;
	ComPtr<FxTechnique> Light3TexAlphaClipFogTech;

	ComPtr<FxVar> ViewProj;
	ComPtr<FxVar> EyePosW;
	ComPtr<FxVar> FogColor;
	ComPtr<FxVar> FogStart;
	ComPtr<FxVar> FogRange;
	ComPtr<FxVar> DirLights;
	ComPtr<FxVar> Mat;

	ComPtr<FxVar> TreeTextureMapArray;
};

class VecAddEffect : public Effect
{
public:
	VecAddEffect(ComPtr<GfxDevice> device, const std::wstring& filename);
	~VecAddEffect();

	void SetInputA(ComPtr<GfxShaderResourceView> srv) { InputA->SetResource(srv.Get()); }
	void SetInputB(ComPtr<GfxShaderResourceView> srv) { InputB->SetResource(srv.Get()); }
	void SetOutput(ComPtr<GfxUnorderedAccessView> uav) { Output->SetUnorderedAccessView(uav.Get()); }

	ComPtr<FxTechnique> VecAddTech;

	ComPtr<FxVar> InputA;
	ComPtr<FxVar> InputB;
	ComPtr<FxVar> Output;
};

class BlurEffect : public Effect
{
public:
	BlurEffect(ComPtr<GfxDevice> device, const std::wstring& filename);
	~BlurEffect();

	void SetWeights(const float weights[9]) { Weights->SetFloatArray(weights, 0, 9); }
	void SetInputMap(ComPtr<GfxShaderResourceView> tex) { InputMap->SetResource(tex.Get()); }
	void SetOutputMap(ComPtr<GfxUnorderedAccessView> tex) { OutputMap->SetUnorderedAccessView(tex.Get()); }

	ComPtr<FxTechnique> HorzBlurTech;
	ComPtr<FxTechnique> VertBlurTech;

	ComPtr<FxVar> Weights;
	ComPtr<FxVar> InputMap;
	ComPtr<FxVar> OutputMap;
};

class TessellationEffect : public Effect
{
public:
	TessellationEffect(ComPtr<GfxDevice> device, const std::wstring& filename);
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
	void SetDiffuseMap(GfxShaderResourceView* tex) { DiffuseMap->SetResource(tex); }

	ComPtr<FxTechnique> TessTech;

	ComPtr<FxVar> WorldViewProj;
	ComPtr<FxVar> World;
	ComPtr<FxVar> WorldInvTranspose;
	ComPtr<FxVar> TexTransform;
	ComPtr<FxVar> EyePosW;
	ComPtr<FxVar> FogColor;
	ComPtr<FxVar> FogStart;
	ComPtr<FxVar> FogRange;
	ComPtr<FxVar> DirLights;
	ComPtr<FxVar> Mat;

	ComPtr<FxVar> DiffuseMap;
};

class BezierTessellationEffect : public Effect
{
public:
	BezierTessellationEffect(ComPtr<GfxDevice> device, const std::wstring& filename);
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
	void SetDiffuseMap(GfxShaderResourceView* tex) { DiffuseMap->SetResource(tex); }

	ComPtr<FxTechnique> TessTech;

	ComPtr<FxVar> WorldViewProj;
	ComPtr<FxVar> World;
	ComPtr<FxVar> WorldInvTranspose;
	ComPtr<FxVar> TexTransform;
	ComPtr<FxVar> EyePosW;
	ComPtr<FxVar> FogColor;
	ComPtr<FxVar> FogStart;
	ComPtr<FxVar> FogRange;
	ComPtr<FxVar> DirLights;
	ComPtr<FxVar> Mat;

	ComPtr<FxVar> DiffuseMap;
};

class InstancedBasicEffect : public Effect
{
public:
	InstancedBasicEffect(ComPtr<GfxDevice> device, const std::wstring& filename);
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
	// 캐스케이드 그림자 (구 4개: xyz 중심, w 반지름²), 공통 값, 빛마다 (Strength, 필터)
	void SetCascadeSpheres(const XMFLOAT4* v, int cnt) { CascadeSpheres->SetFloatVectorArray(reinterpret_cast<const float*>(v), 0, cnt); }
	void SetShadowParams(const XMFLOAT4& v) { ShadowParams->SetFloatVector(reinterpret_cast<const float*>(&v)); }
	void SetDirShadowData(const XMFLOAT4* v, int cnt) { DirShadowData->SetFloatVectorArray(reinterpret_cast<const float*>(v), 0, cnt); }
	void SetSpotShadowData(const XMFLOAT4* v, int cnt) { SpotShadowData->SetFloatVectorArray(reinterpret_cast<const float*>(v), 0, cnt); }
	void SetPointShadowData(const XMFLOAT4* v, int cnt) { PointShadowData->SetFloatVectorArray(reinterpret_cast<const float*>(v), 0, cnt); }

	void SetEyePosW(const XMFLOAT3& v) { EyePosW->SetRawValue(&v, 0, sizeof(XMFLOAT3)); }
	void SetFogColor(const FXMVECTOR v) { FogColor->SetFloatVector(reinterpret_cast<const float*>(&v)); }
	void SetFogStart(float f) { FogStart->SetFloat(f); }
	void SetFogRange(float f) { FogRange->SetFloat(f); }
	/*
	template <typename T, int maxSize>
	void SetLights(const T* lights, int cnt, ComPtr<FxVar> LightEffect, ComPtr<FxVar> LightCountEffect)
	{
		T lightArray[maxSize];

		// ���� ����Ʈ �����͸� �迭�� ����
		for (int i = 0; i < cnt; i++)
		{
			lightArray[i] = lights[i];
		}

		// �⺻������ �ʱ�ȭ
		T temp;
		temp.Init();
		for (int i = cnt; i < maxSize; i++)
		{
			lightArray[i] = temp;
		}

		LightEffect->SetRawValue(lightArray, 0, maxSize * sizeof(T));

		// ����Ʈ ���� ����
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
	
	void SetDiffuseMap(GfxShaderResourceView* tex) { DiffuseMap->SetResource(tex); }
	
	// 빛 종류마다 Texture2DArray 하나 (조각 = 빛 x 조각 수 + 조각 — ShadowMap)
	void SetDirShadowMaps(GfxShaderResourceView* tex) { DirShadowMaps->SetResource(tex); }
	void SetSpotShadowMaps(GfxShaderResourceView* tex) { SpotShadowMaps->SetResource(tex); }
	void SetPointShadowMaps(GfxShaderResourceView* tex) { PointShadowMaps->SetResource(tex); }

	void SetNormalMap(GfxShaderResourceView* tex) { NormalMap->SetResource(tex); }
	void SetSsaoMap(GfxShaderResourceView* tex) { SsaoMap->SetResource(tex); }
	void SetCubeMap(GfxShaderResourceView* tex) { CubeMap->SetResource(tex); }

	// URP Lit (PBR) 재질 값과 추가 텍스처
	void SetPbr(const PbrMaterial& m) { Pbr->SetRawValue(&m, 0, sizeof(PbrMaterial)); }
	// Virtual Texturing (UseBaseMap == 2): 페이지 표 · 캐시 · 정보 (VirtualTexturing::Binding). 보통 재질은 null
	void SetVirtualTexture(GfxShaderResourceView* pageTable, GfxShaderResourceView* cache, const float info0[4], const float info1[4])
	{
		static const float zero[4] = {};
		if (VTPageTable) VTPageTable->SetResource(pageTable);
		if (VTCache) VTCache->SetResource(cache);
		if (VTInfo0) VTInfo0->SetFloatVector(info0 ? info0 : zero);
		if (VTInfo1) VTInfo1->SetFloatVector(info1 ? info1 : zero);
	}
	void SetMetallicMap(GfxShaderResourceView* tex) { MetallicMap->SetResource(tex); }
	void SetOcclusionMap(GfxShaderResourceView* tex) { OcclusionMap->SetResource(tex); }
	void SetEmissionMap(GfxShaderResourceView* tex) { EmissionMap->SetResource(tex); }
	ComPtr<FxVar> Pbr;
	ComPtr<FxVar> VTPageTable, VTCache, VTInfo0, VTInfo1;
	ComPtr<FxVar> MetallicMap;
	ComPtr<FxVar> OcclusionMap;
	ComPtr<FxVar> EmissionMap;

	ComPtr<FxTechnique> Tech;
	ComPtr<FxTechnique> InstancingTech;
	ComPtr<FxTechnique> SkinnedTech;

	ComPtr<FxVar> World;
	ComPtr<FxVar> WorldInvTranspose;
	ComPtr<FxVar> WorldViewProj;
	ComPtr<FxVar> WorldViewProjTex;

	//ComPtr<FxVar> View;
	//ComPtr<FxVar> Proj;
	ComPtr<FxVar> ViewProj;
	ComPtr<FxVar> ViewProjTex;
	ComPtr<FxVar> BoneTransforms;
	ComPtr<FxVar> TexTransform;
	
	ComPtr<FxVar> DirShadowTransforms;
	ComPtr<FxVar> SpotShadowTransforms;
	ComPtr<FxVar> PointShadowTransforms;
	ComPtr<FxVar> CascadeSpheres;
	ComPtr<FxVar> ShadowParams;
	ComPtr<FxVar> DirShadowData;
	ComPtr<FxVar> SpotShadowData;
	ComPtr<FxVar> PointShadowData;

	ComPtr<FxVar> EyePosW;
	ComPtr<FxVar> FogColor;
	ComPtr<FxVar> FogStart;
	ComPtr<FxVar> FogRange;

	ComPtr<FxVar> DirLights;
	ComPtr<FxVar> PointLights;
	ComPtr<FxVar> SpotLights;
	ComPtr<FxVar> DirLightCount;
	ComPtr<FxVar> PointLightCount;
	ComPtr<FxVar> SpotLightCount;

	ComPtr<FxVar> Mat;
	ComPtr<FxVar> Setting;

	ComPtr<FxVar> DiffuseMap;
	
	ComPtr<FxVar> DirShadowMaps;
	ComPtr<FxVar> SpotShadowMaps;
	ComPtr<FxVar> PointShadowMaps;

	ComPtr<FxVar> NormalMap;
	ComPtr<FxVar> SsaoMap;
	ComPtr<FxVar> CubeMap;
};

class SkyEffect : public Effect
{
public:
	SkyEffect(ComPtr<GfxDevice> device, const std::wstring& filename);
	~SkyEffect();

	void SetWorldViewProj(CXMMATRIX M) { WorldViewProj->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetCubeMap(GfxShaderResourceView* cubemap) { CubeMap->SetResource(cubemap); }

	ComPtr<FxTechnique> SkyTech;
	ComPtr<FxVar> WorldViewProj;
	ComPtr<FxVar> CubeMap;
};

class NormalMapEffect : public Effect
{
public:
	NormalMapEffect(ComPtr<GfxDevice> device, const std::wstring& filename);
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
	void SetDiffuseMap(GfxShaderResourceView* tex) { DiffuseMap->SetResource(tex); }
	void SetCubeMap(GfxShaderResourceView* tex) { CubeMap->SetResource(tex); }
	void SetNormalMap(GfxShaderResourceView* tex) { NormalMap->SetResource(tex); }
	void SetSsaoMap(GfxShaderResourceView* tex) { SsaoMap->SetResource(tex); }
	void SetShadowMap(GfxShaderResourceView* tex) { ShadowMap->SetResource(tex); }

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

	ComPtr<FxVar> WorldViewProj;
	ComPtr<FxVar> WorldViewProjTex;
	ComPtr<FxVar> World;
	ComPtr<FxVar> WorldInvTranspose;
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

class DisplacementMapEffect : public Effect
{
public:
	DisplacementMapEffect(ComPtr<GfxDevice> device, const std::wstring& filename);
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

	void SetDiffuseMap(GfxShaderResourceView* tex) { DiffuseMap->SetResource(tex); }
	void SetCubeMap(GfxShaderResourceView* tex) { CubeMap->SetResource(tex); }
	void SetNormalMap(GfxShaderResourceView* tex) { NormalMap->SetResource(tex); }
	void SetShadowMap(GfxShaderResourceView* tex) { ShadowMap->SetResource(tex); }
	
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

	ComPtr<FxVar> ViewProj;
	ComPtr<FxVar> WorldViewProj;
	ComPtr<FxVar> World;
	ComPtr<FxVar> WorldInvTranspose;
	ComPtr<FxVar> ShadowTransform;
	ComPtr<FxVar> TexTransform;
	ComPtr<FxVar> EyePosW;
	ComPtr<FxVar> FogColor;
	ComPtr<FxVar> FogStart;
	ComPtr<FxVar> FogRange;
	ComPtr<FxVar> DirLights;
	ComPtr<FxVar> Mat;
	ComPtr<FxVar> HeightScale;
	ComPtr<FxVar> MaxTessDistance;
	ComPtr<FxVar> MinTessDistance;
	ComPtr<FxVar> MinTessFactor;
	ComPtr<FxVar> MaxTessFactor;

	ComPtr<FxVar> DiffuseMap;
	ComPtr<FxVar> CubeMap;
	ComPtr<FxVar> NormalMap;
	ComPtr<FxVar> ShadowMap;
};

class TerrainEffect : public Effect
{
public:
	TerrainEffect(ComPtr<GfxDevice> device, const std::wstring& filename);
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

	void SetLayerMapArray(GfxShaderResourceView* tex) { LayerMapArray->SetResource(tex); }
	void SetBlendMap(GfxShaderResourceView* tex) { BlendMap->SetResource(tex); }
	void SetHeightMap(GfxShaderResourceView* tex) { HeightMap->SetResource(tex); }


	ComPtr<FxTechnique> Light1Tech;
	ComPtr<FxTechnique> Light2Tech;
	ComPtr<FxTechnique> Light3Tech;
	ComPtr<FxTechnique> Light1FogTech;
	ComPtr<FxTechnique> Light2FogTech;
	ComPtr<FxTechnique> Light3FogTech;

	ComPtr<FxVar> ViewProj;
	ComPtr<FxVar> World;
	ComPtr<FxVar> WorldInvTranspose;
	ComPtr<FxVar> TexTransform;
	ComPtr<FxVar> EyePosW;
	ComPtr<FxVar> FogColor;
	ComPtr<FxVar> FogStart;
	ComPtr<FxVar> FogRange;
	ComPtr<FxVar> DirLights;
	ComPtr<FxVar> Mat;
	ComPtr<FxVar> MinDist;
	ComPtr<FxVar> MaxDist;
	ComPtr<FxVar> MinTess;
	ComPtr<FxVar> MaxTess;
	ComPtr<FxVar> TexelCellSpaceU;
	ComPtr<FxVar> TexelCellSpaceV;
	ComPtr<FxVar> WorldCellSpace;
	ComPtr<FxVar> WorldFrustumPlanes;

	ComPtr<FxVar> LayerMapArray;
	ComPtr<FxVar> BlendMap;
	ComPtr<FxVar> HeightMap;
};

class ParticleEffect : public Effect
{
public:
	ParticleEffect(ComPtr<GfxDevice> device, const std::wstring& filename);
	~ParticleEffect();

	void SetViewProj(CXMMATRIX M) { ViewProj->SetMatrix(reinterpret_cast<const float*>(&M)); }

	void SetGameTime(float f) { GameTime->SetFloat(f); }
	void SetTimeStep(float f) { TimeStep->SetFloat(f); }

	void SetEyePosW(const XMFLOAT3& v) { EyePosW->SetRawValue(&v, 0, sizeof(XMFLOAT3)); }
	void SetEmitPosW(const XMFLOAT3& v) { EmitPosW->SetRawValue(&v, 0, sizeof(XMFLOAT3)); }
	void SetEmitDirW(const XMFLOAT3& v) { EmitDirW->SetRawValue(&v, 0, sizeof(XMFLOAT3)); }

	void SetTexArray(GfxShaderResourceView* tex) { TexArray->SetResource(tex); }
	void SetRandomTex(GfxShaderResourceView* tex) { RandomTex->SetResource(tex); }

	ComPtr<FxTechnique> StreamOutTech;
	ComPtr<FxTechnique> DrawTech;

	ComPtr<FxVar> ViewProj;
	ComPtr<FxVar> GameTime;
	ComPtr<FxVar> TimeStep;
	ComPtr<FxVar> EyePosW;
	ComPtr<FxVar> EmitPosW;
	ComPtr<FxVar> EmitDirW;
	ComPtr<FxVar> TexArray;
	ComPtr<FxVar> RandomTex;
};

class BuildShadowMapEffect : public Effect
{
public:
	BuildShadowMapEffect(ComPtr<GfxDevice> device, const std::wstring& filename);
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
	// 그림자 바이어스: light.w = 0 이면 xyz = 빛 쪽 방향, 1 이면 광원 위치. bias = (깊이, 노멀)
	void SetShadowLight(const XMFLOAT4& v) { ShadowLight->SetFloatVector(reinterpret_cast<const float*>(&v)); }
	void SetShadowBias(float depth, float normal) { const float v[2] = { depth, normal }; ShadowBias->SetRawValue(v, 0, sizeof(v)); }

	void SetDiffuseMap(GfxShaderResourceView* tex) { DiffuseMap->SetResource(tex); }
	void SetAlphaCutoff(float f) { if (AlphaCutoff) AlphaCutoff->SetFloat(f); }
	void SetNormalMap(GfxShaderResourceView* tex) { NormalMap->SetResource(tex); }

	ComPtr<FxTechnique> BuildShadowMapTech;
	ComPtr<FxTechnique> BuildShadowMapAlphaClipTech;
	ComPtr<FxTechnique> BuildShadowMapSkinnedTech;
	ComPtr<FxTechnique> BuildShadowMapAlphaClipSkinnedTech;
	ComPtr<FxTechnique> TessBuildShadowMapTech;
	ComPtr<FxTechnique> TessBuildShadowMapAlphaClipTech;

	// NEW
	ComPtr<FxTechnique> BuildShadowMapInstancingTech;
	ComPtr<FxTechnique> BuildShadowMapAlphaClipInstancingTech;

	ComPtr<FxVar> ViewProj;
	ComPtr<FxVar> WorldViewProj;
	ComPtr<FxVar> World;
	ComPtr<FxVar> WorldInvTranspose;
	ComPtr<FxVar> BoneTransforms;
	ComPtr<FxVar> TexTransform;
	ComPtr<FxVar> EyePosW;
	ComPtr<FxVar> HeightScale;
	ComPtr<FxVar> MaxTessDistance;
	ComPtr<FxVar> MinTessDistance;
	ComPtr<FxVar> MinTessFactor;
	ComPtr<FxVar> MaxTessFactor;
	ComPtr<FxVar> ShadowLight;
	ComPtr<FxVar> ShadowBias;

	ComPtr<FxVar> DiffuseMap;
	ComPtr<FxVar> AlphaCutoff;
	ComPtr<FxVar> NormalMap;
};

class DebugTexEffect : public Effect
{
public:
	DebugTexEffect(ComPtr<GfxDevice> device, const std::wstring& filename);
	~DebugTexEffect();

	void SetWorldViewProj(CXMMATRIX M) { WorldViewProj->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetTexture(GfxShaderResourceView* tex) { Texture->SetResource(tex); }

	ComPtr<FxTechnique> ViewArgbTech;
	ComPtr<FxTechnique> ViewRedTech;
	ComPtr<FxTechnique> ViewGreenTech;
	ComPtr<FxTechnique> ViewBlueTech;
	ComPtr<FxTechnique> ViewAlphaTech;

	ComPtr<FxVar> WorldViewProj;
	ComPtr<FxVar> Texture;
};

class AmbientOcclusionEffect : public Effect
{
public:
	AmbientOcclusionEffect(ComPtr<GfxDevice> device, const std::wstring& filename);
	~AmbientOcclusionEffect();

	void SetWorldViewProj(CXMMATRIX M) { WorldViewProj->SetMatrix(reinterpret_cast<const float*>(&M)); }

	ComPtr<FxTechnique> AmbientOcclusionTech;
	ComPtr<FxVar> WorldViewProj;
};

class SsaoNormalDepthEffect : public Effect
{
public:
	SsaoNormalDepthEffect(ComPtr<GfxDevice> device, const std::wstring& filename);
	~SsaoNormalDepthEffect();

	// NEW
	void SetView(CXMMATRIX M) { View->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetProj(CXMMATRIX M) { Proj->SetMatrix(reinterpret_cast<const float*>(&M)); }

	void SetWorldView(CXMMATRIX M) { WorldView->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetWorldInvTransposeView(CXMMATRIX M) { WorldInvTransposeView->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetBoneTransforms(const XMFLOAT4X4* M, int cnt) { BoneTransforms->SetMatrixArray(reinterpret_cast<const float*>(M), 0, cnt); }
	void SetWorldViewProj(CXMMATRIX M) { WorldViewProj->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetTexTransform(CXMMATRIX M) { TexTransform->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetDiffuseMap(GfxShaderResourceView* tex) { DiffuseMap->SetResource(tex); }
	void SetAlphaCutoff(float f) { if (AlphaCutoff) AlphaCutoff->SetFloat(f); }

	ComPtr<FxTechnique> NormalDepthTech;
	ComPtr<FxTechnique> NormalDepthAlphaClipTech;
	ComPtr<FxTechnique> NormalDepthSkinnedTech;
	ComPtr<FxTechnique> NormalDepthAlphaClipSkinnedTech;

	// NEW
	ComPtr<FxTechnique> NormalDepthInstancingTech;
	ComPtr<FxTechnique> NormalDepthAlphaClipInstancingTech;

	ComPtr<FxVar> View;
	ComPtr<FxVar> Proj;
	ComPtr<FxVar> WorldView;
	ComPtr<FxVar> WorldInvTransposeView;
	ComPtr<FxVar> BoneTransforms;
	ComPtr<FxVar> WorldViewProj;
	ComPtr<FxVar> TexTransform;
	ComPtr<FxVar> DiffuseMap;
	ComPtr<FxVar> AlphaCutoff;
};

class SsaoEffect : public Effect
{
public:
	SsaoEffect(ComPtr<GfxDevice> device, const std::wstring& filename);
	~SsaoEffect();

	void SetSsaoPower(float f) { SsaoPower->SetFloat(f); }
	void SetViewToTexSpace(CXMMATRIX M) { ViewToTexSpace->SetMatrix(reinterpret_cast<const float*>(&M)); }
	void SetOffsetVectors(const XMFLOAT4 v[14]) { OffsetVectors->SetFloatVectorArray(reinterpret_cast<const float*>(v), 0, 14); }
	void SetFrustumCorners(const XMFLOAT4 v[4]) { FrustumCorners->SetFloatVectorArray(reinterpret_cast<const float*>(v), 0, 4); }
	void SetOcclusionRadius(float f) { OcclusionRadius->SetFloat(f); }
	void SetOcclusionFadeStart(float f) { OcclusionFadeStart->SetFloat(f); }
	void SetOcclusionFadeEnd(float f) { OcclusionFadeEnd->SetFloat(f); }
	void SetSurfaceEpsilon(float f) { SurfaceEpsilon->SetFloat(f); }
	// x 세기, y Falloff Distance (Volume 의 Screen Space Ambient Occlusion)
	void SetParams(float intensity, float falloff) { const float v[4] = { intensity, falloff, 0.0f, 0.0f }; if (Params && Params->IsValid()) Params->SetFloatVector(v); }

	void SetNormalDepthMap(GfxShaderResourceView* srv) { NormalDepthMap->SetResource(srv); }
	void SetRandomVecMap(GfxShaderResourceView* srv) { RandomVecMap->SetResource(srv); }

	ComPtr<FxTechnique> SsaoTech;       // Medium (표본 8)
	ComPtr<FxTechnique> SsaoLowTech;    // Low (4)
	ComPtr<FxTechnique> SsaoHighTech;   // High (14)
	ComPtr<FxVar> Params;
	ComPtr<FxVar> SsaoPower;
	ComPtr<FxVar> ViewToTexSpace;
	ComPtr<FxVar> OffsetVectors;
	ComPtr<FxVar> FrustumCorners;
	ComPtr<FxVar> OcclusionRadius;
	ComPtr<FxVar> OcclusionFadeStart;
	ComPtr<FxVar> OcclusionFadeEnd;
	ComPtr<FxVar> SurfaceEpsilon;
	ComPtr<FxVar> NormalDepthMap;
	ComPtr<FxVar> RandomVecMap;
};

class SsaoBlurEffect : public Effect
{
public:
	SsaoBlurEffect(ComPtr<GfxDevice> device, const std::wstring& filename);
	~SsaoBlurEffect();

	void SetTexelWidth(float f) { TexelWidth->SetFloat(f); }
	void SetTexelHeight(float f) { TexelHeight->SetFloat(f); }

	void SetNormalDepthMap(GfxShaderResourceView* srv) { NormalDepthMap->SetResource(srv); }
	void SetInputImage(GfxShaderResourceView* srv) { InputImage->SetResource(srv); }

	ComPtr<FxTechnique> HorzBlurTech;
	ComPtr<FxTechnique> VertBlurTech;
	ComPtr<FxVar> TexelWidth;
	ComPtr<FxVar> TexelHeight;
	ComPtr<FxVar> NormalDepthMap;
	ComPtr<FxVar> InputImage;
};

class Effects
{
public:
	static void InitAll(ComPtr<GfxDevice> device, const std::wstring& filename);
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