#include "pch.h"
#include "Effects.h"
#include <fstream>
#include "ShaderCache.h"
#include "Utils.h"




// 효과는 RHI 장치(Rhi::Main)로 불러온다 — DirectX 11 = Effects11, OpenGL = ShaderCross. device 인자는 예전 호출 모양 그대로 두려고 남김
Effect::Effect(ComPtr<GfxDevice> device, const std::wstring& filename)
{
	(void)device;
	std::string error;
	_fx = FxEffect::Load(filename, error);
	if (!_fx)
	{
		// 대화상자를 띄우지 않는다 (백그라운드로 띄운 에디터가 거기서 멈춘다). 빈 효과 = 변수·기법이 모두 없는 것처럼 (그리지 않음)
		EditorLog::Write("Effect", "%s: %s", wstring_to_string(filename).c_str(), error.c_str());
		_fx = FxEffect::Empty();
	}
}

Effect::~Effect()
{

}

BasicEffect::BasicEffect(ComPtr<GfxDevice> device, const std::wstring& filename)
	: Effect(device, filename)
{
	Light1Tech = _fx->GetTechniqueByName("Light1");
	Light2Tech = _fx->GetTechniqueByName("Light2");
	Light3Tech = _fx->GetTechniqueByName("Light3");

	Light0TexTech = _fx->GetTechniqueByName("Light0Tex");
	Light1TexTech = _fx->GetTechniqueByName("Light1Tex");
	Light2TexTech = _fx->GetTechniqueByName("Light2Tex");
	Light3TexTech = _fx->GetTechniqueByName("Light3Tex");

	Light0TexAlphaClipTech = _fx->GetTechniqueByName("Light0TexAlphaClip");
	Light1TexAlphaClipTech = _fx->GetTechniqueByName("Light1TexAlphaClip");
	Light2TexAlphaClipTech = _fx->GetTechniqueByName("Light2TexAlphaClip");
	Light3TexAlphaClipTech = _fx->GetTechniqueByName("Light3TexAlphaClip");

	Light1FogTech = _fx->GetTechniqueByName("Light1Fog");
	Light2FogTech = _fx->GetTechniqueByName("Light2Fog");
	Light3FogTech = _fx->GetTechniqueByName("Light3Fog");

	Light0TexFogTech = _fx->GetTechniqueByName("Light0TexFog");
	Light1TexFogTech = _fx->GetTechniqueByName("Light1TexFog");
	Light2TexFogTech = _fx->GetTechniqueByName("Light2TexFog");
	Light3TexFogTech = _fx->GetTechniqueByName("Light3TexFog");

	Light0TexAlphaClipFogTech = _fx->GetTechniqueByName("Light0TexAlphaClipFog");
	Light1TexAlphaClipFogTech = _fx->GetTechniqueByName("Light1TexAlphaClipFog");
	Light2TexAlphaClipFogTech = _fx->GetTechniqueByName("Light2TexAlphaClipFog");
	Light3TexAlphaClipFogTech = _fx->GetTechniqueByName("Light3TexAlphaClipFog");

	Light1ReflectTech = _fx->GetTechniqueByName("Light1Reflect");
	Light2ReflectTech = _fx->GetTechniqueByName("Light2Reflect");
	Light3ReflectTech = _fx->GetTechniqueByName("Light3Reflect");

	Light0TexReflectTech = _fx->GetTechniqueByName("Light0TexReflect");
	Light1TexReflectTech = _fx->GetTechniqueByName("Light1TexReflect");
	Light2TexReflectTech = _fx->GetTechniqueByName("Light2TexReflect");
	Light3TexReflectTech = _fx->GetTechniqueByName("Light3TexReflect");

	Light0TexAlphaClipReflectTech = _fx->GetTechniqueByName("Light0TexAlphaClipReflect");
	Light1TexAlphaClipReflectTech = _fx->GetTechniqueByName("Light1TexAlphaClipReflect");
	Light2TexAlphaClipReflectTech = _fx->GetTechniqueByName("Light2TexAlphaClipReflect");
	Light3TexAlphaClipReflectTech = _fx->GetTechniqueByName("Light3TexAlphaClipReflect");

	Light1FogReflectTech = _fx->GetTechniqueByName("Light1FogReflect");
	Light2FogReflectTech = _fx->GetTechniqueByName("Light2FogReflect");
	Light3FogReflectTech = _fx->GetTechniqueByName("Light3FogReflect");

	Light0TexFogReflectTech = _fx->GetTechniqueByName("Light0TexFogReflect");
	Light1TexFogReflectTech = _fx->GetTechniqueByName("Light1TexFogReflect");
	Light2TexFogReflectTech = _fx->GetTechniqueByName("Light2TexFogReflect");
	Light3TexFogReflectTech = _fx->GetTechniqueByName("Light3TexFogReflect");

	Light0TexAlphaClipFogReflectTech = _fx->GetTechniqueByName("Light0TexAlphaClipFogReflect");
	Light1TexAlphaClipFogReflectTech = _fx->GetTechniqueByName("Light1TexAlphaClipFogReflect");
	Light2TexAlphaClipFogReflectTech = _fx->GetTechniqueByName("Light2TexAlphaClipFogReflect");
	Light3TexAlphaClipFogReflectTech = _fx->GetTechniqueByName("Light3TexAlphaClipFogReflect");

	WorldViewProj = _fx->GetVariableByName("gWorldViewProj")->AsMatrix();
	WorldViewProjTex = _fx->GetVariableByName("gWorldViewProjTex")->AsMatrix(); 
	World = _fx->GetVariableByName("gWorld")->AsMatrix();
	WorldInvTranspose = _fx->GetVariableByName("gWorldInvTranspose")->AsMatrix();
	TexTransform = _fx->GetVariableByName("gTexTransform")->AsMatrix();
	ShadowTransform = _fx->GetVariableByName("gShadowTransform")->AsMatrix();
	EyePosW = _fx->GetVariableByName("gEyePosW")->AsVector();
	FogColor = _fx->GetVariableByName("gFogColor")->AsVector();
	FogStart = _fx->GetVariableByName("gFogStart")->AsScalar();
	FogRange = _fx->GetVariableByName("gFogRange")->AsScalar();
	DirLights = _fx->GetVariableByName("gDirLights");
	Mat = _fx->GetVariableByName("gMaterial");
	DiffuseMap = _fx->GetVariableByName("gDiffuseMap")->AsShaderResource();
	CubeMap = _fx->GetVariableByName("gCubeMap")->AsShaderResource();
	ShadowMap = _fx->GetVariableByName("gShadowMap")->AsShaderResource();
	SsaoMap = _fx->GetVariableByName("gSsaoMap")->AsShaderResource();
}

BasicEffect::~BasicEffect()
{
}

TreeSpriteEffect::TreeSpriteEffect(ComPtr<GfxDevice> device, const std::wstring& filename) : Effect(device, filename)
{
	Light3Tech = _fx->GetTechniqueByName("Light3");
	Light3TexAlphaClipTech = _fx->GetTechniqueByName("Light3TexAlphaClip");
	Light3TexAlphaClipFogTech = _fx->GetTechniqueByName("Light3TexAlphaClipFog");

	ViewProj = _fx->GetVariableByName("gViewProj")->AsMatrix();
	EyePosW = _fx->GetVariableByName("gEyePosW")->AsVector();
	FogColor = _fx->GetVariableByName("gFogColor")->AsVector();
	FogStart = _fx->GetVariableByName("gFogStart")->AsScalar();
	FogRange = _fx->GetVariableByName("gFogRange")->AsScalar();
	DirLights = _fx->GetVariableByName("gDirLights");
	Mat = _fx->GetVariableByName("gMaterial");
	TreeTextureMapArray = _fx->GetVariableByName("gTreeMapArray")->AsShaderResource();
}

TreeSpriteEffect::~TreeSpriteEffect()
{
}

VecAddEffect::VecAddEffect(ComPtr<GfxDevice> device, const std::wstring& filename)
	: Effect(device, filename)
{
	VecAddTech = _fx->GetTechniqueByName("VecAdd");

	InputA = _fx->GetVariableByName("gInputA")->AsShaderResource();
	InputB = _fx->GetVariableByName("gInputB")->AsShaderResource();
	Output = _fx->GetVariableByName("gOutput")->AsUnorderedAccessView();
}

VecAddEffect::~VecAddEffect()
{
}

BlurEffect::BlurEffect(ComPtr<GfxDevice> device, const std::wstring& filename)
	: Effect(device, filename)
{
	HorzBlurTech = _fx->GetTechniqueByName("HorzBlur");
	VertBlurTech = _fx->GetTechniqueByName("VertBlur");

	Weights = _fx->GetVariableByName("gWeights")->AsScalar();
	InputMap = _fx->GetVariableByName("gInput")->AsShaderResource();
	OutputMap = _fx->GetVariableByName("gOutput")->AsUnorderedAccessView();
}

BlurEffect::~BlurEffect()
{
}

TessellationEffect::TessellationEffect(ComPtr<GfxDevice> device, const std::wstring& filename)
	: Effect(device, filename)
{
	TessTech = _fx->GetTechniqueByName("Tess");

	WorldViewProj = _fx->GetVariableByName("gWorldViewProj")->AsMatrix();
	World = _fx->GetVariableByName("gWorld")->AsMatrix();
	WorldInvTranspose = _fx->GetVariableByName("gWorldInvTranspose")->AsMatrix();
	TexTransform = _fx->GetVariableByName("gTexTransform")->AsMatrix();
	EyePosW = _fx->GetVariableByName("gEyePosW")->AsVector();
	FogColor = _fx->GetVariableByName("gFogColor")->AsVector();
	FogStart = _fx->GetVariableByName("gFogStart")->AsScalar();
	FogRange = _fx->GetVariableByName("gFogRange")->AsScalar();
	DirLights = _fx->GetVariableByName("gDirLights");
	Mat = _fx->GetVariableByName("gMaterial");
	DiffuseMap = _fx->GetVariableByName("gDiffuseMap")->AsShaderResource();
}

TessellationEffect::~TessellationEffect()
{
}

BezierTessellationEffect::BezierTessellationEffect(ComPtr<GfxDevice> device, const std::wstring& filename)
	: Effect(device, filename)
{
	TessTech = _fx->GetTechniqueByName("Tess");

	WorldViewProj = _fx->GetVariableByName("gWorldViewProj")->AsMatrix();
	World = _fx->GetVariableByName("gWorld")->AsMatrix();
	WorldInvTranspose = _fx->GetVariableByName("gWorldInvTranspose")->AsMatrix();
	TexTransform = _fx->GetVariableByName("gTexTransform")->AsMatrix();
	EyePosW = _fx->GetVariableByName("gEyePosW")->AsVector();
	FogColor = _fx->GetVariableByName("gFogColor")->AsVector();
	FogStart = _fx->GetVariableByName("gFogStart")->AsScalar();
	FogRange = _fx->GetVariableByName("gFogRange")->AsScalar();
	DirLights = _fx->GetVariableByName("gDirLights");
	Mat = _fx->GetVariableByName("gMaterial");
	DiffuseMap = _fx->GetVariableByName("gDiffuseMap")->AsShaderResource();
}

BezierTessellationEffect::~BezierTessellationEffect()
{
}

InstancedBasicEffect::InstancedBasicEffect(ComPtr<GfxDevice> device, const std::wstring& filename)
	: Effect(device, filename)
{

	Tech = _fx->GetTechniqueByName("Tech");
	InstancingTech = _fx->GetTechniqueByName("InstancingTech");
	SkinnedTech = _fx->GetTechniqueByName("SkinnedTech");

	World = _fx->GetVariableByName("gWorld")->AsMatrix();
	WorldInvTranspose = _fx->GetVariableByName("gWorldInvTranspose")->AsMatrix();
	WorldViewProj = _fx->GetVariableByName("gWorldViewProj")->AsMatrix();
	WorldViewProjTex = _fx->GetVariableByName("gWorldViewProjTex")->AsMatrix();

	//View = _fx->GetVariableByName("gView")->AsMatrix();
	//Proj = _fx->GetVariableByName("gProj")->AsMatrix();
	ViewProj = _fx->GetVariableByName("gViewProj")->AsMatrix();
	ViewProjTex = _fx->GetVariableByName("gViewProjTex")->AsMatrix();
	BoneTransforms = _fx->GetVariableByName("gBoneTransforms")->AsMatrix();
	TexTransform = _fx->GetVariableByName("gTexTransform")->AsMatrix();
	
	DirShadowTransforms = _fx->GetVariableByName("gDirShadowTransforms")->AsMatrix();
	SpotShadowTransforms = _fx->GetVariableByName("gSpotShadowTransforms")->AsMatrix();
	PointShadowTransforms = _fx->GetVariableByName("gPointShadowTransforms")->AsMatrix();
	CascadeSpheres = _fx->GetVariableByName("gCascadeSpheres")->AsVector();
	ShadowParams = _fx->GetVariableByName("gShadowParams")->AsVector();
	DirShadowData = _fx->GetVariableByName("gDirShadowData")->AsVector();
	SpotShadowData = _fx->GetVariableByName("gSpotShadowData")->AsVector();
	PointShadowData = _fx->GetVariableByName("gPointShadowData")->AsVector();

	EyePosW = _fx->GetVariableByName("gEyePosW")->AsVector();
	FogColor = _fx->GetVariableByName("gFogColor")->AsVector();
	FogStart = _fx->GetVariableByName("gFogStart")->AsScalar();
	FogRange = _fx->GetVariableByName("gFogRange")->AsScalar();

	DirLights = _fx->GetVariableByName("gDirLights");
	PointLights = _fx->GetVariableByName("gPointLights");
	SpotLights = _fx->GetVariableByName("gSpotLights");
	DirLightCount = _fx->GetVariableByName("gDirLightCount")->AsScalar();
	PointLightCount = _fx->GetVariableByName("gPointLightCount")->AsScalar();
	SpotLightCount = _fx->GetVariableByName("gSpotLightCount")->AsScalar();

	Mat = _fx->GetVariableByName("gMaterial");
	Setting = _fx->GetVariableByName("gShaderSetting");
	DiffuseMap = _fx->GetVariableByName("gDiffuseMap")->AsShaderResource();
	
	DirShadowMaps = _fx->GetVariableByName("gDirShadowMaps")->AsShaderResource();
	SpotShadowMaps = _fx->GetVariableByName("gSpotShadowMaps")->AsShaderResource();
	PointShadowMaps = _fx->GetVariableByName("gPointShadowMaps")->AsShaderResource();
	
	NormalMap = _fx->GetVariableByName("gNormalMap")->AsShaderResource();
	SsaoMap = _fx->GetVariableByName("gSsaoMap")->AsShaderResource();
	CubeMap = _fx->GetVariableByName("gCubeMap")->AsShaderResource();

	// URP Lit (PBR)
	Pbr = _fx->GetVariableByName("gPbr");
	MetallicMap = _fx->GetVariableByName("gMetallicMap")->AsShaderResource();
	OcclusionMap = _fx->GetVariableByName("gOcclusionMap")->AsShaderResource();
	EmissionMap = _fx->GetVariableByName("gEmissionMap")->AsShaderResource();
}

InstancedBasicEffect::~InstancedBasicEffect()
{
}

SkyEffect::SkyEffect(ComPtr<GfxDevice> device, const std::wstring& filename)
	: Effect(device, filename)
{
	SkyTech = _fx->GetTechniqueByName("SkyTech");
	WorldViewProj = _fx->GetVariableByName("gWorldViewProj")->AsMatrix();
	CubeMap = _fx->GetVariableByName("gCubeMap")->AsShaderResource();
}

SkyEffect::~SkyEffect()
{
}

NormalMapEffect::NormalMapEffect(ComPtr<GfxDevice> device, const std::wstring& filename)
	: Effect(device, filename)
{
	Light1Tech = _fx->GetTechniqueByName("Light1");
	Light2Tech = _fx->GetTechniqueByName("Light2");
	Light3Tech = _fx->GetTechniqueByName("Light3");

	Light0TexTech = _fx->GetTechniqueByName("Light0Tex");
	Light1TexTech = _fx->GetTechniqueByName("Light1Tex");
	Light2TexTech = _fx->GetTechniqueByName("Light2Tex");
	Light3TexTech = _fx->GetTechniqueByName("Light3Tex");

	Light0TexAlphaClipTech = _fx->GetTechniqueByName("Light0TexAlphaClip");
	Light1TexAlphaClipTech = _fx->GetTechniqueByName("Light1TexAlphaClip");
	Light2TexAlphaClipTech = _fx->GetTechniqueByName("Light2TexAlphaClip");
	Light3TexAlphaClipTech = _fx->GetTechniqueByName("Light3TexAlphaClip");

	Light1FogTech = _fx->GetTechniqueByName("Light1Fog");
	Light2FogTech = _fx->GetTechniqueByName("Light2Fog");
	Light3FogTech = _fx->GetTechniqueByName("Light3Fog");

	Light0TexFogTech = _fx->GetTechniqueByName("Light0TexFog");
	Light1TexFogTech = _fx->GetTechniqueByName("Light1TexFog");
	Light2TexFogTech = _fx->GetTechniqueByName("Light2TexFog");
	Light3TexFogTech = _fx->GetTechniqueByName("Light3TexFog");

	Light0TexAlphaClipFogTech = _fx->GetTechniqueByName("Light0TexAlphaClipFog");
	Light1TexAlphaClipFogTech = _fx->GetTechniqueByName("Light1TexAlphaClipFog");
	Light2TexAlphaClipFogTech = _fx->GetTechniqueByName("Light2TexAlphaClipFog");
	Light3TexAlphaClipFogTech = _fx->GetTechniqueByName("Light3TexAlphaClipFog");

	Light1ReflectTech = _fx->GetTechniqueByName("Light1Reflect");
	Light2ReflectTech = _fx->GetTechniqueByName("Light2Reflect");
	Light3ReflectTech = _fx->GetTechniqueByName("Light3Reflect");

	Light0TexReflectTech = _fx->GetTechniqueByName("Light0TexReflect");
	Light1TexReflectTech = _fx->GetTechniqueByName("Light1TexReflect");
	Light2TexReflectTech = _fx->GetTechniqueByName("Light2TexReflect");
	Light3TexReflectTech = _fx->GetTechniqueByName("Light3TexReflect");

	Light0TexAlphaClipReflectTech = _fx->GetTechniqueByName("Light0TexAlphaClipReflect");
	Light1TexAlphaClipReflectTech = _fx->GetTechniqueByName("Light1TexAlphaClipReflect");
	Light2TexAlphaClipReflectTech = _fx->GetTechniqueByName("Light2TexAlphaClipReflect");
	Light3TexAlphaClipReflectTech = _fx->GetTechniqueByName("Light3TexAlphaClipReflect");

	Light1FogReflectTech = _fx->GetTechniqueByName("Light1FogReflect");
	Light2FogReflectTech = _fx->GetTechniqueByName("Light2FogReflect");
	Light3FogReflectTech = _fx->GetTechniqueByName("Light3FogReflect");

	Light0TexFogReflectTech = _fx->GetTechniqueByName("Light0TexFogReflect");
	Light1TexFogReflectTech = _fx->GetTechniqueByName("Light1TexFogReflect");
	Light2TexFogReflectTech = _fx->GetTechniqueByName("Light2TexFogReflect");
	Light3TexFogReflectTech = _fx->GetTechniqueByName("Light3TexFogReflect");

	Light0TexAlphaClipFogReflectTech = _fx->GetTechniqueByName("Light0TexAlphaClipFogReflect");
	Light1TexAlphaClipFogReflectTech = _fx->GetTechniqueByName("Light1TexAlphaClipFogReflect");
	Light2TexAlphaClipFogReflectTech = _fx->GetTechniqueByName("Light2TexAlphaClipFogReflect");
	Light3TexAlphaClipFogReflectTech = _fx->GetTechniqueByName("Light3TexAlphaClipFogReflect");

	WorldViewProj = _fx->GetVariableByName("gWorldViewProj")->AsMatrix();
	WorldViewProjTex = _fx->GetVariableByName("gWorldViewProjTex")->AsMatrix();
	World = _fx->GetVariableByName("gWorld")->AsMatrix();
	WorldInvTranspose = _fx->GetVariableByName("gWorldInvTranspose")->AsMatrix();
	ShadowTransform = _fx->GetVariableByName("gShadowTransform")->AsMatrix();
	TexTransform = _fx->GetVariableByName("gTexTransform")->AsMatrix();
	EyePosW = _fx->GetVariableByName("gEyePosW")->AsVector();
	FogColor = _fx->GetVariableByName("gFogColor")->AsVector();
	FogStart = _fx->GetVariableByName("gFogStart")->AsScalar();
	FogRange = _fx->GetVariableByName("gFogRange")->AsScalar();
	DirLights = _fx->GetVariableByName("gDirLights");
	Mat = _fx->GetVariableByName("gMaterial");
	DiffuseMap = _fx->GetVariableByName("gDiffuseMap")->AsShaderResource();
	CubeMap = _fx->GetVariableByName("gCubeMap")->AsShaderResource();
	NormalMap = _fx->GetVariableByName("gNormalMap")->AsShaderResource();
	ShadowMap = _fx->GetVariableByName("gShadowMap")->AsShaderResource();
	SsaoMap = _fx->GetVariableByName("gSsaoMap")->AsShaderResource();
}

NormalMapEffect::~NormalMapEffect()
{
}

DisplacementMapEffect::DisplacementMapEffect(ComPtr<GfxDevice> device, const std::wstring& filename)
	: Effect(device, filename)
{
	Light1Tech = _fx->GetTechniqueByName("Light1");
	Light2Tech = _fx->GetTechniqueByName("Light2");
	Light3Tech = _fx->GetTechniqueByName("Light3");

	Light0TexTech = _fx->GetTechniqueByName("Light0Tex");
	Light1TexTech = _fx->GetTechniqueByName("Light1Tex");
	Light2TexTech = _fx->GetTechniqueByName("Light2Tex");
	Light3TexTech = _fx->GetTechniqueByName("Light3Tex");

	Light0TexAlphaClipTech = _fx->GetTechniqueByName("Light0TexAlphaClip");
	Light1TexAlphaClipTech = _fx->GetTechniqueByName("Light1TexAlphaClip");
	Light2TexAlphaClipTech = _fx->GetTechniqueByName("Light2TexAlphaClip");
	Light3TexAlphaClipTech = _fx->GetTechniqueByName("Light3TexAlphaClip");

	Light1FogTech = _fx->GetTechniqueByName("Light1Fog");
	Light2FogTech = _fx->GetTechniqueByName("Light2Fog");
	Light3FogTech = _fx->GetTechniqueByName("Light3Fog");

	Light0TexFogTech = _fx->GetTechniqueByName("Light0TexFog");
	Light1TexFogTech = _fx->GetTechniqueByName("Light1TexFog");
	Light2TexFogTech = _fx->GetTechniqueByName("Light2TexFog");
	Light3TexFogTech = _fx->GetTechniqueByName("Light3TexFog");

	Light0TexAlphaClipFogTech = _fx->GetTechniqueByName("Light0TexAlphaClipFog");
	Light1TexAlphaClipFogTech = _fx->GetTechniqueByName("Light1TexAlphaClipFog");
	Light2TexAlphaClipFogTech = _fx->GetTechniqueByName("Light2TexAlphaClipFog");
	Light3TexAlphaClipFogTech = _fx->GetTechniqueByName("Light3TexAlphaClipFog");

	Light1ReflectTech = _fx->GetTechniqueByName("Light1Reflect");
	Light2ReflectTech = _fx->GetTechniqueByName("Light2Reflect");
	Light3ReflectTech = _fx->GetTechniqueByName("Light3Reflect");

	Light0TexReflectTech = _fx->GetTechniqueByName("Light0TexReflect");
	Light1TexReflectTech = _fx->GetTechniqueByName("Light1TexReflect");
	Light2TexReflectTech = _fx->GetTechniqueByName("Light2TexReflect");
	Light3TexReflectTech = _fx->GetTechniqueByName("Light3TexReflect");

	Light0TexAlphaClipReflectTech = _fx->GetTechniqueByName("Light0TexAlphaClipReflect");
	Light1TexAlphaClipReflectTech = _fx->GetTechniqueByName("Light1TexAlphaClipReflect");
	Light2TexAlphaClipReflectTech = _fx->GetTechniqueByName("Light2TexAlphaClipReflect");
	Light3TexAlphaClipReflectTech = _fx->GetTechniqueByName("Light3TexAlphaClipReflect");

	Light1FogReflectTech = _fx->GetTechniqueByName("Light1FogReflect");
	Light2FogReflectTech = _fx->GetTechniqueByName("Light2FogReflect");
	Light3FogReflectTech = _fx->GetTechniqueByName("Light3FogReflect");

	Light0TexFogReflectTech = _fx->GetTechniqueByName("Light0TexFogReflect");
	Light1TexFogReflectTech = _fx->GetTechniqueByName("Light1TexFogReflect");
	Light2TexFogReflectTech = _fx->GetTechniqueByName("Light2TexFogReflect");
	Light3TexFogReflectTech = _fx->GetTechniqueByName("Light3TexFogReflect");

	Light0TexAlphaClipFogReflectTech = _fx->GetTechniqueByName("Light0TexAlphaClipFogReflect");
	Light1TexAlphaClipFogReflectTech = _fx->GetTechniqueByName("Light1TexAlphaClipFogReflect");
	Light2TexAlphaClipFogReflectTech = _fx->GetTechniqueByName("Light2TexAlphaClipFogReflect");
	Light3TexAlphaClipFogReflectTech = _fx->GetTechniqueByName("Light3TexAlphaClipFogReflect");

	ViewProj = _fx->GetVariableByName("gViewProj")->AsMatrix();
	WorldViewProj = _fx->GetVariableByName("gWorldViewProj")->AsMatrix();
	World = _fx->GetVariableByName("gWorld")->AsMatrix();
	WorldInvTranspose = _fx->GetVariableByName("gWorldInvTranspose")->AsMatrix();
	ShadowTransform = _fx->GetVariableByName("gShadowTransform")->AsMatrix();
	TexTransform = _fx->GetVariableByName("gTexTransform")->AsMatrix();
	EyePosW = _fx->GetVariableByName("gEyePosW")->AsVector();
	FogColor = _fx->GetVariableByName("gFogColor")->AsVector();
	FogStart = _fx->GetVariableByName("gFogStart")->AsScalar();
	FogRange = _fx->GetVariableByName("gFogRange")->AsScalar();
	DirLights = _fx->GetVariableByName("gDirLights");
	Mat = _fx->GetVariableByName("gMaterial");
	HeightScale = _fx->GetVariableByName("gHeightScale")->AsScalar();
	MaxTessDistance = _fx->GetVariableByName("gMaxTessDistance")->AsScalar();
	MinTessDistance = _fx->GetVariableByName("gMinTessDistance")->AsScalar();
	MinTessFactor = _fx->GetVariableByName("gMinTessFactor")->AsScalar();
	MaxTessFactor = _fx->GetVariableByName("gMaxTessFactor")->AsScalar();
	DiffuseMap = _fx->GetVariableByName("gDiffuseMap")->AsShaderResource();
	CubeMap = _fx->GetVariableByName("gCubeMap")->AsShaderResource();
	NormalMap = _fx->GetVariableByName("gNormalMap")->AsShaderResource();
	ShadowMap = _fx->GetVariableByName("gShadowMap")->AsShaderResource();
}

DisplacementMapEffect::~DisplacementMapEffect()
{
}

TerrainEffect::TerrainEffect(ComPtr<GfxDevice> device, const std::wstring& filename)
	: Effect(device, filename)
{
	Light1Tech = _fx->GetTechniqueByName("Light1");
	Light2Tech = _fx->GetTechniqueByName("Light2");
	Light3Tech = _fx->GetTechniqueByName("Light3");
	Light1FogTech = _fx->GetTechniqueByName("Light1Fog");
	Light2FogTech = _fx->GetTechniqueByName("Light2Fog");
	Light3FogTech = _fx->GetTechniqueByName("Light3Fog");

	ViewProj = _fx->GetVariableByName("gViewProj")->AsMatrix();
	EyePosW = _fx->GetVariableByName("gEyePosW")->AsVector();
	FogColor = _fx->GetVariableByName("gFogColor")->AsVector();
	FogStart = _fx->GetVariableByName("gFogStart")->AsScalar();
	FogRange = _fx->GetVariableByName("gFogRange")->AsScalar();
	DirLights = _fx->GetVariableByName("gDirLights");
	Mat = _fx->GetVariableByName("gMaterial");

	MinDist = _fx->GetVariableByName("gMinDist")->AsScalar();
	MaxDist = _fx->GetVariableByName("gMaxDist")->AsScalar();
	MinTess = _fx->GetVariableByName("gMinTess")->AsScalar();
	MaxTess = _fx->GetVariableByName("gMaxTess")->AsScalar();
	TexelCellSpaceU = _fx->GetVariableByName("gTexelCellSpaceU")->AsScalar();
	TexelCellSpaceV = _fx->GetVariableByName("gTexelCellSpaceV")->AsScalar();
	WorldCellSpace = _fx->GetVariableByName("gWorldCellSpace")->AsScalar();
	WorldFrustumPlanes = _fx->GetVariableByName("gWorldFrustumPlanes")->AsVector();

	LayerMapArray = _fx->GetVariableByName("gLayerMapArray")->AsShaderResource();
	BlendMap = _fx->GetVariableByName("gBlendMap")->AsShaderResource();
	HeightMap = _fx->GetVariableByName("gHeightMap")->AsShaderResource();
}

TerrainEffect::~TerrainEffect()
{
}

ParticleEffect::ParticleEffect(ComPtr<GfxDevice> device, const std::wstring& filename)
	: Effect(device, filename)
{
	StreamOutTech = _fx->GetTechniqueByName("StreamOutTech");
	DrawTech = _fx->GetTechniqueByName("DrawTech");

	ViewProj = _fx->GetVariableByName("gViewProj")->AsMatrix();
	GameTime = _fx->GetVariableByName("gGameTime")->AsScalar();
	TimeStep = _fx->GetVariableByName("gTimeStep")->AsScalar();
	EyePosW = _fx->GetVariableByName("gEyePosW")->AsVector();
	EmitPosW = _fx->GetVariableByName("gEmitPosW")->AsVector();
	EmitDirW = _fx->GetVariableByName("gEmitDirW")->AsVector();
	TexArray = _fx->GetVariableByName("gTexArray")->AsShaderResource();
	RandomTex = _fx->GetVariableByName("gRandomTex")->AsShaderResource();
}

ParticleEffect::~ParticleEffect()
{
}


BuildShadowMapEffect::BuildShadowMapEffect(ComPtr<GfxDevice> device, const std::wstring& filename)
	: Effect(device, filename)
{
	BuildShadowMapTech = _fx->GetTechniqueByName("BuildShadowMapTech");
	BuildShadowMapAlphaClipTech = _fx->GetTechniqueByName("BuildShadowMapAlphaClipTech");
	
	BuildShadowMapSkinnedTech = _fx->GetTechniqueByName("BuildShadowMapSkinnedTech");
	BuildShadowMapAlphaClipSkinnedTech = _fx->GetTechniqueByName("BuildShadowMapAlphaClipSkinnedTech");

	TessBuildShadowMapTech = _fx->GetTechniqueByName("TessBuildShadowMapTech");
	TessBuildShadowMapAlphaClipTech = _fx->GetTechniqueByName("TessBuildShadowMapAlphaClipTech");

	TessBuildShadowMapTech = _fx->GetTechniqueByName("TessBuildShadowMapTech");
	TessBuildShadowMapAlphaClipTech = _fx->GetTechniqueByName("TessBuildShadowMapAlphaClipTech");

	// NEW
	BuildShadowMapInstancingTech = _fx->GetTechniqueByName("BuildShadowMapInstancingTech");
	BuildShadowMapAlphaClipInstancingTech = _fx->GetTechniqueByName("BuildShadowMapAlphaClipInstancingTech");

	ViewProj = _fx->GetVariableByName("gViewProj")->AsMatrix();
	WorldViewProj = _fx->GetVariableByName("gWorldViewProj")->AsMatrix();
	World = _fx->GetVariableByName("gWorld")->AsMatrix();
	WorldInvTranspose = _fx->GetVariableByName("gWorldInvTranspose")->AsMatrix();
	BoneTransforms = _fx->GetVariableByName("gBoneTransforms")->AsMatrix();
	TexTransform = _fx->GetVariableByName("gTexTransform")->AsMatrix();
	EyePosW = _fx->GetVariableByName("gEyePosW")->AsVector();
	HeightScale = _fx->GetVariableByName("gHeightScale")->AsScalar();
	MaxTessDistance = _fx->GetVariableByName("gMaxTessDistance")->AsScalar();
	MinTessDistance = _fx->GetVariableByName("gMinTessDistance")->AsScalar();
	MinTessFactor = _fx->GetVariableByName("gMinTessFactor")->AsScalar();
	MaxTessFactor = _fx->GetVariableByName("gMaxTessFactor")->AsScalar();
	ShadowLight = _fx->GetVariableByName("gShadowLight")->AsVector();
	ShadowBias = _fx->GetVariableByName("gShadowBias");
	DiffuseMap = _fx->GetVariableByName("gDiffuseMap")->AsShaderResource();
	NormalMap = _fx->GetVariableByName("gNormalMap")->AsShaderResource();
}

BuildShadowMapEffect::~BuildShadowMapEffect()
{
}

DebugTexEffect::DebugTexEffect(ComPtr<GfxDevice> device, const std::wstring& filename)
	: Effect(device, filename)
{
	ViewArgbTech = _fx->GetTechniqueByName("ViewArgbTech");
	ViewRedTech = _fx->GetTechniqueByName("ViewRedTech");
	ViewGreenTech = _fx->GetTechniqueByName("ViewGreenTech");
	ViewBlueTech = _fx->GetTechniqueByName("ViewBlueTech");
	ViewAlphaTech = _fx->GetTechniqueByName("ViewAlphaTech");

	WorldViewProj = _fx->GetVariableByName("gWorldViewProj")->AsMatrix();
	Texture = _fx->GetVariableByName("gTexture")->AsShaderResource();
}

DebugTexEffect::~DebugTexEffect()
{

}

AmbientOcclusionEffect::AmbientOcclusionEffect(ComPtr<GfxDevice> device, const std::wstring& filename)
	: Effect(device, filename)
{
	AmbientOcclusionTech = _fx->GetTechniqueByName("AmbientOcclusion");
	WorldViewProj = _fx->GetVariableByName("gWorldViewProj")->AsMatrix();
}

AmbientOcclusionEffect::~AmbientOcclusionEffect()
{
}

SsaoNormalDepthEffect::SsaoNormalDepthEffect(ComPtr<GfxDevice> device, const std::wstring& filename)
	: Effect(device, filename)
{
	NormalDepthTech = _fx->GetTechniqueByName("NormalDepth");
	NormalDepthAlphaClipTech = _fx->GetTechniqueByName("NormalDepthAlphaClip");
	
	NormalDepthSkinnedTech = _fx->GetTechniqueByName("NormalDepthSkinned");
	NormalDepthAlphaClipSkinnedTech = _fx->GetTechniqueByName("NormalDepthAlphaClipSkinned");

	// NEW
	NormalDepthInstancingTech = _fx->GetTechniqueByName("NormalDepthInstancing");
	NormalDepthAlphaClipInstancingTech = _fx->GetTechniqueByName("NormalDepthAlphaClipInstancing");
	View = _fx->GetVariableByName("gView")->AsMatrix();
	Proj = _fx->GetVariableByName("gProj")->AsMatrix();
	WorldView = _fx->GetVariableByName("gWorldView")->AsMatrix();
	WorldInvTransposeView = _fx->GetVariableByName("gWorldInvTransposeView")->AsMatrix();
	BoneTransforms = _fx->GetVariableByName("gBoneTransforms")->AsMatrix();
	WorldViewProj = _fx->GetVariableByName("gWorldViewProj")->AsMatrix();
	TexTransform = _fx->GetVariableByName("gTexTransform")->AsMatrix();
	DiffuseMap = _fx->GetVariableByName("gDiffuseMap")->AsShaderResource();
}

SsaoNormalDepthEffect::~SsaoNormalDepthEffect()
{
}

SsaoEffect::SsaoEffect(ComPtr<GfxDevice> device, const std::wstring& filename)
	: Effect(device, filename)
{
	SsaoTech = _fx->GetTechniqueByName("Ssao");

	SsaoPower = _fx->GetVariableByName("gSsaoPower")->AsScalar();
	ViewToTexSpace = _fx->GetVariableByName("gViewToTexSpace")->AsMatrix();
	OffsetVectors = _fx->GetVariableByName("gOffsetVectors")->AsVector();
	FrustumCorners = _fx->GetVariableByName("gFrustumCorners")->AsVector();
	OcclusionRadius = _fx->GetVariableByName("gOcclusionRadius")->AsScalar();
	OcclusionFadeStart = _fx->GetVariableByName("gOcclusionFadeStart")->AsScalar();
	OcclusionFadeEnd = _fx->GetVariableByName("gOcclusionFadeEnd")->AsScalar();
	SurfaceEpsilon = _fx->GetVariableByName("gSurfaceEpsilon")->AsScalar();
	
	NormalDepthMap = _fx->GetVariableByName("gNormalDepthMap")->AsShaderResource();
	RandomVecMap = _fx->GetVariableByName("gRandomVecMap")->AsShaderResource();
}

SsaoEffect::~SsaoEffect()
{
}

SsaoBlurEffect::SsaoBlurEffect(ComPtr<GfxDevice> device, const std::wstring& filename)
	: Effect(device, filename)
{
	HorzBlurTech = _fx->GetTechniqueByName("HorzBlur");
	VertBlurTech = _fx->GetTechniqueByName("VertBlur");
	
	TexelWidth = _fx->GetVariableByName("gTexelWidth")->AsScalar();
	TexelHeight = _fx->GetVariableByName("gTexelHeight")->AsScalar();
	
	NormalDepthMap = _fx->GetVariableByName("gNormalDepthMap")->AsShaderResource();
	InputImage = _fx->GetVariableByName("gInputImage")->AsShaderResource();
}

SsaoBlurEffect::~SsaoBlurEffect()
{
}

shared_ptr<BasicEffect> Effects::BasicFX;
shared_ptr<TreeSpriteEffect> Effects::TreeSpriteFX;
shared_ptr<VecAddEffect> Effects::VecAddFX;
shared_ptr<BlurEffect> Effects::BlurFX;
shared_ptr<TessellationEffect> Effects::TessellationFX;
shared_ptr<BezierTessellationEffect> Effects::BezierTessellationFX; 
shared_ptr<TessellationEffect> Effects::TriangleTessellationFX;

shared_ptr<InstancedBasicEffect> Effects::InstancedBasicFX;

shared_ptr<SkyEffect> Effects::SkyFX;
shared_ptr<NormalMapEffect> Effects::NormalMapFX;
shared_ptr<DisplacementMapEffect> Effects::DisplacementMapFX;
shared_ptr<TerrainEffect> Effects::TerrainFX;
shared_ptr<ParticleEffect> Effects::FireFX;
shared_ptr<ParticleEffect> Effects::RainFX;
shared_ptr<BuildShadowMapEffect> Effects::BuildShadowMapFX;
shared_ptr<DebugTexEffect> Effects::DebugTexFX;
shared_ptr<AmbientOcclusionEffect> Effects::AmbientOcclusionFX;
shared_ptr<SsaoNormalDepthEffect> Effects::SsaoNormalDepthFX;
shared_ptr<SsaoEffect> Effects::SsaoFX;
shared_ptr<SsaoBlurEffect> Effects::SsaoBlurFX;

void Effects::InitAll(ComPtr<GfxDevice> device, const std::wstring& filename)
{
	BasicFX = make_shared<BasicEffect>(device, filename);
	TreeSpriteFX = make_shared<TreeSpriteEffect>(device, L"../Shaders/12. TreeSprite.fx");
	VecAddFX = make_shared<VecAddEffect>(device, L"../Shaders/13. VecAdd.fx");
	BlurFX = make_shared<BlurEffect>(device, L"../Shaders/14. Blur.fx");
	TessellationFX = make_shared<TessellationEffect>(device, L"../Shaders/15. Tessellation.fx");
	TriangleTessellationFX = make_shared<TessellationEffect>(device, L"../Shaders/16. TriTessellation.fx");
	BezierTessellationFX = make_shared<BezierTessellationEffect>(device, L"../Shaders/17. BezierTessellation.fx");

	//InstancedBasicFX = make_shared<InstancedBasicEffect>(device, L"../Shaders/19. InstancedBasic.fx");
	InstancedBasicFX = make_shared<InstancedBasicEffect>(device, L"../Shaders/32. InstancedBasic.fx");

	SkyFX = make_shared<SkyEffect>(device, L"../Shaders/21. Sky.fx");
	NormalMapFX = make_shared<NormalMapEffect>(device, L"../Shaders/23. NormalMap.fx");
	DisplacementMapFX = make_shared<DisplacementMapEffect>(device, L"../Shaders/23. DisplacementMap.fx");
	TerrainFX = make_shared<TerrainEffect>(device, L"../Shaders/24. Terrain.fx");
	FireFX = make_shared<ParticleEffect>(device, L"../Shaders/25. Fire.fx");
	RainFX = make_shared<ParticleEffect>(device, L"../Shaders/25. Rain.fx");
	BuildShadowMapFX = make_shared<BuildShadowMapEffect>(device, L"../Shaders/26. BuildShadowMap.fx");
	DebugTexFX = make_shared<DebugTexEffect>(device, L"../Shaders/26. DebugTexture.fx");
	AmbientOcclusionFX = make_shared<AmbientOcclusionEffect>(device, L"../Shaders/27. AmbientOcclusion.fx");
	SsaoNormalDepthFX = make_shared<SsaoNormalDepthEffect>(device, L"../Shaders/28. SsaoNormalDepth.fx");
	SsaoFX = make_shared<SsaoEffect>(device, L"../Shaders/28. Ssao.fx");
	SsaoBlurFX = make_shared<SsaoBlurEffect>(device, L"../Shaders/28. SsaoBlur.fx");
}

void Effects::DestroyAll()
{

}