#include "pch.h"
#include "NormalMapSkinnedShader.h"

NormalMapSkinnedShader::NormalMapSkinnedShader(ComPtr<ID3D11Device> device, const std::wstring& filename)
	: Shader(device, filename)
{
	Light1Tech = m_pFx->GetTechniqueByName("Light1");
	Light2Tech = m_pFx->GetTechniqueByName("Light2");
	Light3Tech = m_pFx->GetTechniqueByName("Light3");

	Light0TexTech = m_pFx->GetTechniqueByName("Light0Tex");
	Light1TexTech = m_pFx->GetTechniqueByName("Light1Tex");
	Light2TexTech = m_pFx->GetTechniqueByName("Light2Tex");
	Light3TexTech = m_pFx->GetTechniqueByName("Light3Tex");

	Light0TexAlphaClipTech = m_pFx->GetTechniqueByName("Light0TexAlphaClip");
	Light1TexAlphaClipTech = m_pFx->GetTechniqueByName("Light1TexAlphaClip");
	Light2TexAlphaClipTech = m_pFx->GetTechniqueByName("Light2TexAlphaClip");
	Light3TexAlphaClipTech = m_pFx->GetTechniqueByName("Light3TexAlphaClip");

	Light1FogTech = m_pFx->GetTechniqueByName("Light1Fog");
	Light2FogTech = m_pFx->GetTechniqueByName("Light2Fog");
	Light3FogTech = m_pFx->GetTechniqueByName("Light3Fog");

	Light0TexFogTech = m_pFx->GetTechniqueByName("Light0TexFog");
	Light1TexFogTech = m_pFx->GetTechniqueByName("Light1TexFog");
	Light2TexFogTech = m_pFx->GetTechniqueByName("Light2TexFog");
	Light3TexFogTech = m_pFx->GetTechniqueByName("Light3TexFog");

	Light0TexAlphaClipFogTech = m_pFx->GetTechniqueByName("Light0TexAlphaClipFog");
	Light1TexAlphaClipFogTech = m_pFx->GetTechniqueByName("Light1TexAlphaClipFog");
	Light2TexAlphaClipFogTech = m_pFx->GetTechniqueByName("Light2TexAlphaClipFog");
	Light3TexAlphaClipFogTech = m_pFx->GetTechniqueByName("Light3TexAlphaClipFog");

	Light1ReflectTech = m_pFx->GetTechniqueByName("Light1Reflect");
	Light2ReflectTech = m_pFx->GetTechniqueByName("Light2Reflect");
	Light3ReflectTech = m_pFx->GetTechniqueByName("Light3Reflect");

	Light0TexReflectTech = m_pFx->GetTechniqueByName("Light0TexReflect");
	Light1TexReflectTech = m_pFx->GetTechniqueByName("Light1TexReflect");
	Light2TexReflectTech = m_pFx->GetTechniqueByName("Light2TexReflect");
	Light3TexReflectTech = m_pFx->GetTechniqueByName("Light3TexReflect");

	Light0TexAlphaClipReflectTech = m_pFx->GetTechniqueByName("Light0TexAlphaClipReflect");
	Light1TexAlphaClipReflectTech = m_pFx->GetTechniqueByName("Light1TexAlphaClipReflect");
	Light2TexAlphaClipReflectTech = m_pFx->GetTechniqueByName("Light2TexAlphaClipReflect");
	Light3TexAlphaClipReflectTech = m_pFx->GetTechniqueByName("Light3TexAlphaClipReflect");

	Light1FogReflectTech = m_pFx->GetTechniqueByName("Light1FogReflect");
	Light2FogReflectTech = m_pFx->GetTechniqueByName("Light2FogReflect");
	Light3FogReflectTech = m_pFx->GetTechniqueByName("Light3FogReflect");

	Light0TexFogReflectTech = m_pFx->GetTechniqueByName("Light0TexFogReflect");
	Light1TexFogReflectTech = m_pFx->GetTechniqueByName("Light1TexFogReflect");
	Light2TexFogReflectTech = m_pFx->GetTechniqueByName("Light2TexFogReflect");
	Light3TexFogReflectTech = m_pFx->GetTechniqueByName("Light3TexFogReflect");

	Light0TexAlphaClipFogReflectTech = m_pFx->GetTechniqueByName("Light0TexAlphaClipFogReflect");
	Light1TexAlphaClipFogReflectTech = m_pFx->GetTechniqueByName("Light1TexAlphaClipFogReflect");
	Light2TexAlphaClipFogReflectTech = m_pFx->GetTechniqueByName("Light2TexAlphaClipFogReflect");
	Light3TexAlphaClipFogReflectTech = m_pFx->GetTechniqueByName("Light3TexAlphaClipFogReflect");

	Light1SkinnedTech = m_pFx->GetTechniqueByName("Light1Skinned");
	Light2SkinnedTech = m_pFx->GetTechniqueByName("Light2Skinned");
	Light3SkinnedTech = m_pFx->GetTechniqueByName("Light3Skinned");

	Light0TexSkinnedTech = m_pFx->GetTechniqueByName("Light0TexSkinned");
	Light1TexSkinnedTech = m_pFx->GetTechniqueByName("Light1TexSkinned");
	Light2TexSkinnedTech = m_pFx->GetTechniqueByName("Light2TexSkinned");
	Light3TexSkinnedTech = m_pFx->GetTechniqueByName("Light3TexSkinned");

	Light0TexAlphaClipSkinnedTech = m_pFx->GetTechniqueByName("Light0TexAlphaClipSkinned");
	Light1TexAlphaClipSkinnedTech = m_pFx->GetTechniqueByName("Light1TexAlphaClipSkinned");
	Light2TexAlphaClipSkinnedTech = m_pFx->GetTechniqueByName("Light2TexAlphaClipSkinned");
	Light3TexAlphaClipSkinnedTech = m_pFx->GetTechniqueByName("Light3TexAlphaClipSkinned");

	Light1FogSkinnedTech = m_pFx->GetTechniqueByName("Light1FogSkinned");
	Light2FogSkinnedTech = m_pFx->GetTechniqueByName("Light2FogSkinned");
	Light3FogSkinnedTech = m_pFx->GetTechniqueByName("Light3FogSkinned");

	Light0TexFogSkinnedTech = m_pFx->GetTechniqueByName("Light0TexFogSkinned");
	Light1TexFogSkinnedTech = m_pFx->GetTechniqueByName("Light1TexFogSkinned");
	Light2TexFogSkinnedTech = m_pFx->GetTechniqueByName("Light2TexFogSkinned");
	Light3TexFogSkinnedTech = m_pFx->GetTechniqueByName("Light3TexFogSkinned");

	Light0TexAlphaClipFogSkinnedTech = m_pFx->GetTechniqueByName("Light0TexAlphaClipFogSkinned");
	Light1TexAlphaClipFogSkinnedTech = m_pFx->GetTechniqueByName("Light1TexAlphaClipFogSkinned");
	Light2TexAlphaClipFogSkinnedTech = m_pFx->GetTechniqueByName("Light2TexAlphaClipFogSkinned");
	Light3TexAlphaClipFogSkinnedTech = m_pFx->GetTechniqueByName("Light3TexAlphaClipFogSkinned");

	Light1ReflectSkinnedTech = m_pFx->GetTechniqueByName("Light1ReflectSkinned");
	Light2ReflectSkinnedTech = m_pFx->GetTechniqueByName("Light2ReflectSkinned");
	Light3ReflectSkinnedTech = m_pFx->GetTechniqueByName("Light3ReflectSkinned");

	Light0TexReflectSkinnedTech = m_pFx->GetTechniqueByName("Light0TexReflectSkinned");
	Light1TexReflectSkinnedTech = m_pFx->GetTechniqueByName("Light1TexReflectSkinned");
	Light2TexReflectSkinnedTech = m_pFx->GetTechniqueByName("Light2TexReflectSkinned");
	Light3TexReflectSkinnedTech = m_pFx->GetTechniqueByName("Light3TexReflectSkinned");

	Light0TexAlphaClipReflectSkinnedTech = m_pFx->GetTechniqueByName("Light0TexAlphaClipReflectSkinned");
	Light1TexAlphaClipReflectSkinnedTech = m_pFx->GetTechniqueByName("Light1TexAlphaClipReflectSkinned");
	Light2TexAlphaClipReflectSkinnedTech = m_pFx->GetTechniqueByName("Light2TexAlphaClipReflectSkinned");
	Light3TexAlphaClipReflectSkinnedTech = m_pFx->GetTechniqueByName("Light3TexAlphaClipReflectSkinned");

	Light1FogReflectSkinnedTech = m_pFx->GetTechniqueByName("Light1FogReflectSkinned");
	Light2FogReflectSkinnedTech = m_pFx->GetTechniqueByName("Light2FogReflectSkinned");
	Light3FogReflectSkinnedTech = m_pFx->GetTechniqueByName("Light3FogReflectSkinned");

	Light0TexFogReflectSkinnedTech = m_pFx->GetTechniqueByName("Light0TexFogReflectSkinned");
	Light1TexFogReflectSkinnedTech = m_pFx->GetTechniqueByName("Light1TexFogReflectSkinned");
	Light2TexFogReflectSkinnedTech = m_pFx->GetTechniqueByName("Light2TexFogReflectSkinned");
	Light3TexFogReflectSkinnedTech = m_pFx->GetTechniqueByName("Light3TexFogReflectSkinned");

	Light0TexAlphaClipFogReflectSkinnedTech = m_pFx->GetTechniqueByName("Light0TexAlphaClipFogReflectSkinned");
	Light1TexAlphaClipFogReflectSkinnedTech = m_pFx->GetTechniqueByName("Light1TexAlphaClipFogReflectSkinned");
	Light2TexAlphaClipFogReflectSkinnedTech = m_pFx->GetTechniqueByName("Light2TexAlphaClipFogReflectSkinned");
	Light3TexAlphaClipFogReflectSkinnedTech = m_pFx->GetTechniqueByName("Light3TexAlphaClipFogReflectSkinned");

	WorldViewProj = m_pFx->GetVariableByName("gWorldViewProj")->AsMatrix();
	WorldViewProjTex = m_pFx->GetVariableByName("gWorldViewProjTex")->AsMatrix();
	World = m_pFx->GetVariableByName("gWorld")->AsMatrix();
	WorldInvTranspose = m_pFx->GetVariableByName("gWorldInvTranspose")->AsMatrix();
	BoneTransforms = m_pFx->GetVariableByName("gBoneTransforms")->AsMatrix();
	ShadowTransform = m_pFx->GetVariableByName("gShadowTransform")->AsMatrix();
	TexTransform = m_pFx->GetVariableByName("gTexTransform")->AsMatrix();
	EyePosW = m_pFx->GetVariableByName("gEyePosW")->AsVector();
	FogColor = m_pFx->GetVariableByName("gFogColor")->AsVector();
	FogStart = m_pFx->GetVariableByName("gFogStart")->AsScalar();
	FogRange = m_pFx->GetVariableByName("gFogRange")->AsScalar();
	DirLights = m_pFx->GetVariableByName("gDirLights");
	Mat = m_pFx->GetVariableByName("gMaterial");
	DiffuseMap = m_pFx->GetVariableByName("gDiffuseMap")->AsShaderResource();
	CubeMap = m_pFx->GetVariableByName("gCubeMap")->AsShaderResource();
	NormalMap = m_pFx->GetVariableByName("gNormalMap")->AsShaderResource();
	ShadowMap = m_pFx->GetVariableByName("gShadowMap")->AsShaderResource();
	SsaoMap = m_pFx->GetVariableByName("gSsaoMap")->AsShaderResource();
}

NormalMapSkinnedShader::~NormalMapSkinnedShader()
{
}
