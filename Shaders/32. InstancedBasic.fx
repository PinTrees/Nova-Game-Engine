#include "07. LightHelper.fx"

#define LIGHT_SIZE 4 // Light Size´Â ÀÌ °÷¿¡¼­¸¸ ¼³Á¤°¡´É
#define LIGHT_MULTYPLE 6
#define LIGHT_MAX_SIZE (LIGHT_SIZE * LIGHT_MULTYPLE)
 
struct ShaderSetting
{
    int gUseTexture;
    int gAlphaClip;
    int gUseNormalMap;
    int gUseShadowMap;
    int gUseSsaoMap;
    int gReflectionEnabled;
    int gFogEnabled;
    
    // 28¹ÙÀÌÆ® -> 32¹ÙÀÌÆ® Á¤·ÄÀ» À§ÇØ
    int pad;
};

cbuffer cbPerFrame
{
    DirectionalLight gDirLights[LIGHT_SIZE];
    PointLight gPointLights[LIGHT_SIZE];
    SpotLight gSpotLights[LIGHT_SIZE];
   
    int gDirLightCount;
    int gPointLightCount;
    int gSpotLightCount;
    
    // Instancing -> Worldx, LightV * LightP * toTexSpace, Not Instancing -> World * LightV * LightP * toTexSpace
    float4x4 gDirShadowTransforms[LIGHT_SIZE];
    float4x4 gSpotShadowTransforms[LIGHT_SIZE];
    float4x4 gPointShadowTransforms[LIGHT_MAX_SIZE];
    
    float3 gEyePosW;

    float gFogStart;
    float gFogRange;
    float4 gFogColor;
};

cbuffer cbPerObject
{
    // Use when instancing is not used.
    float4x4 gWorld;
    float4x4 gWorldInvTranspose;
    float4x4 gWorldViewProj;
    float4x4 gWorldViewProjTex;
    
    float4x4 gView;
    float4x4 gProj;
    float4x4 gViewProj;
    float4x4 gViewProjTex;
    
    float4x4 gTexTransform;
    
    Material gMaterial;
    ShaderSetting gShaderSetting;
}; 

cbuffer cbSkinned
{
    float4x4 gBoneTransforms[256];   // NOVA: ìŠ¤í‚¨ ë³¸ ìµœëŒ€ 256 ê°œ
};

// Nonnumeric values cannot be added to a cbuffer.

// frame
Texture2D gDirShadowMaps[LIGHT_SIZE];
Texture2D gSpotShadowMaps[LIGHT_SIZE];
Texture2D gPointShadowMaps[LIGHT_MAX_SIZE];
Texture2D gSsaoMap;
TextureCube gCubeMap;

// object
Texture2D gDiffuseMap;
Texture2D gNormalMap;



SamplerState samLinear
{
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = WRAP;
    AddressV = WRAP;
};

SamplerComparisonState samShadow
{
    Filter = COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    AddressU = BORDER;
    AddressV = BORDER;
    AddressW = BORDER;
    BorderColor = float4(0.0f, 0.0f, 0.0f, 0.0f);

    ComparisonFunc = LESS_EQUAL;
};

struct VertexIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 Tex : TEXCOORD;
    float4 TangentL : TANGENT;
};
 
struct VertexIn_Instancing
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 Tex : TEXCOORD;
    float4 TangentL : TANGENT;
    // Instancing
    row_major float4x4 World : WORLD;
    uint InstanceId : SV_InstanceID;
};

struct SkinnedVertexIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 Tex : TEXCOORD;
    float4 TangentL : TANGENT;
    float3 Weights : WEIGHTS;
    uint4 BoneIndices : BONEINDICES;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float4 PosW : POSITION;
    float3 NormalW : NORMAL;
    float4 TangentW : TANGENT;
    float2 Tex : TEXCOORD0;
    //float4 ShadowPosH[LIGHT_MAX_SIZE] : TEXCOORD2;
    float4 SsaoPosH : TEXCOORD1;
};

VertexOut VS(VertexIn vin)
{
    VertexOut vout;
	
	// Transform to world space space.
    vout.PosW = mul(float4(vin.PosL, 1.0f), gWorld);
    vout.NormalW = mul(vin.NormalL, (float3x3) gWorldInvTranspose);
    vout.TangentW = mul(vin.TangentL, gWorld);
    
	// Transform to homogeneous clip space.
    vout.PosH = mul(float4(vin.PosL, 1.0f), gWorldViewProj);
	
	// Output vertex attributes for interpolation across triangle.
    vout.Tex = mul(float4(vin.Tex, 0.0f, 1.0f), gTexTransform).xy;

	// Generate projective tex-coords to project shadow map onto scene.
    //vout.ShadowPosH = mul(float4(vin.PosL, 1.0f), gShadowTransform);
    
    //int i = 0;
    //int j = gDirLightCount;
    //int l = gSpotLightCount;
   
    //for (int i = 0; i < gDirLightCount;i++)
    //{
    //    vout.ShadowPosH[i] = mul(float4(vout.PosW, 1.0f), gShadowTransform[i]);
    //}
    //int startIndex = gDirLightCount;
    //for (int j = 0; j < gSpotLightCount; j++)
    //{
    //    vout.ShadowPosH[j + startIndex] = mul(float4(vout.PosW, 1.0f), gShadowTransform[j + startIndex]);
    //}
    //startIndex = gDirLightCount + gSpotLightCount;
    //for (int l = 0; l < gPointLightCount; l += 6)
    //{

    //    for (int s = 0; s < 6;s++)
    //    {
    //        vout.ShadowPosH[l + s + startIndex] = mul(float4(vout.PosW, 1.0f), gShadowTransform[l + s + startIndex]);
    //    }
    //}

	// Generate projective tex-coords to project SSAO map onto scene.
    vout.SsaoPosH = mul(float4(vin.PosL, 1.0f), gWorldViewProjTex);

    return vout;
}

VertexOut VS_Instancing(VertexIn_Instancing vin)
{
    VertexOut vout;
    
    // Transform to homogeneous clip space.
    vout.PosH = mul(float4(vin.PosL, 1.0f), vin.World);
    
	// Transform to world space space.
    vout.PosW = vout.PosH; // World
    
    vout.NormalW = mul(vin.NormalL, (float3x3) gWorldInvTranspose);
    vout.TangentW = mul(vin.TangentL, vin.World);
    
	// Output vertex attributes for interpolation across triangle.
    vout.Tex = mul(float4(vin.Tex, 0.0f, 1.0f), gTexTransform).xy;

	// Generate projective tex-coords to project shadow map onto scene.
    //vout.ShadowPosH = mul(vout.PosH, gShadowTransform);

	// Generate projective tex-coords to project SSAO map onto scene.
    vout.SsaoPosH = mul(vout.PosH, gViewProjTex);

    float4x4 wvp = mul(gWorld, gViewProj);
    
    vout.PosH = mul(float4(vin.PosL, 1.0f), wvp);
    //vout.PosH = mul(float4(vout.PosW, 1.0f), gViewProj);
    
    return vout;
}

VertexOut VS_Skinned(SkinnedVertexIn vin)
{
    VertexOut vout;
    
	// Init array or else we get strange warnings about SV_POSITION.
    float weights[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    weights[0] = vin.Weights.x;
    weights[1] = vin.Weights.y;
    weights[2] = vin.Weights.z;
    weights[3] = 1.0f - weights[0] - weights[1] - weights[2];

    float3 posL = float3(0.0f, 0.0f, 0.0f);
    float3 normalL = float3(0.0f, 0.0f, 0.0f);
    float3 tangentL = float3(0.0f, 0.0f, 0.0f);
    for (int i = 0; i < 4; ++i)
    {
	    // Assume no nonuniform scaling when transforming normals, so 
		// that we do not have to use the inverse-transpose.

        posL += weights[i] * mul(float4(vin.PosL, 1.0f), gBoneTransforms[vin.BoneIndices[i]]).xyz;
        normalL += weights[i] * mul(vin.NormalL, (float3x3) gBoneTransforms[vin.BoneIndices[i]]);
        tangentL += weights[i] * mul(vin.TangentL.xyz, (float3x3) gBoneTransforms[vin.BoneIndices[i]]);
    }
 
	// Transform to world space space.
    vout.PosW = mul(float4(posL, 1.0f), gWorld);
    vout.NormalW = mul(normalL, (float3x3) gWorldInvTranspose);
    vout.TangentW = float4(mul(tangentL, (float3x3) gWorld), vin.TangentL.w);

	// Transform to homogeneous clip space.
    vout.PosH = mul(float4(posL, 1.0f), gWorldViewProj);
	
	// Output vertex attributes for interpolation across triangle.
    vout.Tex = mul(float4(vin.Tex, 0.0f, 1.0f), gTexTransform).xy;

	// Generate projective tex-coords to project shadow map onto scene.
    //vout.ShadowPosH = mul(float4(posL, 1.0f), gShadowTransform);
    
	// Generate projective tex-coords to project SSAO map onto scene.
    vout.SsaoPosH = mul(float4(posL, 1.0f), gWorldViewProjTex);

    return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
	// Interpolating normal can unnormalize it, so normalize it.
    pin.NormalW = normalize(pin.NormalW);

	// The toEye vector is used in lighting.
    float3 toEye = gEyePosW - pin.PosW.xyz;

	// Cache the distance to the eye from this surface point.
    float distToEye = length(toEye);

	// Normalize.
    toEye /= distToEye;
    
    // Default to multiplicative identity.
    float4 texColor = float4(1, 1, 1, 1);
    if (gShaderSetting.gUseTexture)
    {
		// Sample texture.
        texColor = gDiffuseMap.Sample(samLinear, pin.Tex);

        if (gShaderSetting.gAlphaClip)
        {
			// Discard pixel if texture alpha < 0.1.  Note that we do this
			// test as soon as possible so that we can potentially exit the shader 
			// early, thereby skipping the rest of the shader code.
            clip(texColor.a - 0.1f);
        }
    }

	//
	// Normal mapping
	//

    float3 bumpedNormalW = pin.NormalW;
    if (gShaderSetting.gUseNormalMap)
    {
        float3 normalMapSample = gNormalMap.Sample(samLinear, pin.Tex).rgb;
        bumpedNormalW = NormalSampleToWorldSpace(normalMapSample, pin.NormalW, pin.TangentW);
    }
 

	//
	// Lighting.
	//

    float4 litColor = texColor;
    if ((gDirLightCount + gPointLightCount + gSpotLightCount) > 0)
    {
		// Start with a sum of zero. 
        float4 ambient = float4(0.0f, 0.0f, 0.0f, 0.0f);
        float4 diffuse = float4(0.0f, 0.0f, 0.0f, 0.0f);
        float4 spec = float4(0.0f, 0.0f, 0.0f, 0.0f);
		  
        float dirShadows[LIGHT_SIZE];
        float spotShadows[LIGHT_SIZE];
        float pointShadows[LIGHT_SIZE];
        
        [unroll]
        for (int shadowSize = 0; shadowSize < LIGHT_SIZE; shadowSize++)
        {
            dirShadows[shadowSize] = 1.0f;
            spotShadows[shadowSize] = 1.0f;
            pointShadows[shadowSize] = 1.0f;
        }
        
        if (gShaderSetting.gUseShadowMap)
        {
            [unloll]
            for (int i = 0; i < LIGHT_SIZE; i++)
            {
                dirShadows[i] = CalcShadowFactor(samShadow, gDirShadowMaps[i], mul(pin.PosW, gDirShadowTransforms[i]));
            }
            
            [unloll]
            for (int j = 0; j < LIGHT_SIZE; j++)
            {
                spotShadows[j] = CalcShadowFactor(samShadow, gSpotShadowMaps[j], mul(pin.PosW, gSpotShadowTransforms[j]));
            }
            
            [unloll]
            for (int l = 0; l < LIGHT_SIZE; l++)
            {
                int startIndex = l * 6;
                [unloll]
                for (int s = 0; s < 6; s++)
                {
                    pointShadows[l] += CalcShadowFactor(samShadow, gPointShadowMaps[startIndex + s], mul(pin.PosW, gPointShadowTransforms[startIndex + s]));
                }
    
                pointShadows[l] /= 6.0f; // Æò±Õ ³»±â
            }
            
            //shadow[0] = CalcShadowFactor(samShadow, gShadowMap, pin.ShadowPosH);
        }
        
		// Finish texture projection and sample SSAO map.
        pin.SsaoPosH /= pin.SsaoPosH.w;
        float ambientAccess = 1.0f;
        if (gShaderSetting.gUseSsaoMap) // Ssao
        {
            ambientAccess = gSsaoMap.Sample(samLinear, pin.SsaoPosH.xy, 0.0f).r;
        }
	    
        float4 A, D, S;
		// Sum the light contribution from each light source.  
		[unloll]
        for (int i = 0; i < gDirLightCount; ++i)
        {
            ComputeDirectionalLight(gMaterial, gDirLights[i], bumpedNormalW, toEye,
				A, D, S);
            
            ambient += ambientAccess * A;
            diffuse += dirShadows[i] * D;
            spec += dirShadows[i] * S;
        }

        [unloll]
        for (int j = 0; j < gSpotLightCount; ++j)
        {
            ComputeSpotLight(gMaterial, gSpotLights[j], pin.PosW.xyz, bumpedNormalW, toEye,
				A, D, S);

            ambient += ambientAccess * A;
            diffuse += spotShadows[j] * D;
            spec += spotShadows[j] * S;
        }
        
        [unloll]
        for (int l = 0; l < gPointLightCount; ++l)
        {
            ComputePointLight(gMaterial, gPointLights[l], pin.PosW.xyz, bumpedNormalW, toEye,
				A, D, S);

            ambient += ambientAccess * A;
            diffuse += pointShadows[l] * D;
            spec += pointShadows[l] * S;
        }
		   
        litColor = texColor * (ambient + diffuse) + spec;
    }
    
    if (gShaderSetting.gReflectionEnabled)
    {
        float3 incident = -toEye;
        float3 reflectionVector = reflect(incident, bumpedNormalW);
        float4 reflectionColor = gCubeMap.Sample(samLinear, reflectionVector);

        litColor += gMaterial.Reflect * reflectionColor;
    }
    
	//
	// Fogging
	//

    if (gShaderSetting.gFogEnabled)
    {
        float fogLerp = saturate((distToEye - gFogStart) / gFogRange);

		// Blend the fog color and the lit color.
        litColor = lerp(litColor, gFogColor, fogLerp);
    }

	// Common to take alpha from diffuse material and texture.
    litColor.a = gMaterial.Diffuse.a * texColor.a;

    return litColor;
}

technique11 Tech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS()));
    }
}

technique11 InstancingTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Instancing()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS()));
    }
}

technique11 SkinnedTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Skinned()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS()));
    }
}
//=============================================================================
// NOVA ì§€í˜• (Terrain ì»´í¬ë„ŒíŠ¸) - ë³¸ íŒ¨ìŠ¤
//=============================================================================
#include "40. TerrainCommon.fx"

DepthStencilState TerrainDepthLessEqual
{
    DepthEnable = TRUE;
    DepthWriteMask = ALL;
    DepthFunc = LESS_EQUAL;
};

struct TerrainVertexOut
{
    float4 PosH : SV_POSITION;
    float4 PosW : POSITION;
    float2 UV : TEXCOORD0;
    float4 SsaoPosH : TEXCOORD1;
};

TerrainVertexOut TerrainVS(uint vid : SV_VertexID)
{
    TerrainVertexOut vout;
    float2 uv;
    float3 posW = TerrainVertexWorld(vid, uv);
    vout.PosW = float4(posW, 1.0f);
    vout.PosH = mul(vout.PosW, gViewProj);
    vout.UV = uv;
    vout.SsaoPosH = mul(vout.PosW, gViewProjTex);
    return vout;
}

float4 TerrainPS(TerrainVertexOut pin) : SV_Target
{
    float3 normalW = TerrainNormalUV(pin.UV);
    float3 toEye = gEyePosW - pin.PosW.xyz;
    float distToEye = length(toEye);
    toEye /= distToEye;

    float4 texColor = TerrainAlbedo(pin.UV);
    float4 litColor = texColor;
    if ((gDirLightCount + gPointLightCount + gSpotLightCount) > 0)
    {
        float4 ambient = 0, diffuse = 0, spec = 0;
        float dirShadows[LIGHT_SIZE];
        float spotShadows[LIGHT_SIZE];
        [unroll]
        for (int k = 0; k < LIGHT_SIZE; k++)
        {
            dirShadows[k] = 1.0f;
            spotShadows[k] = 1.0f;
        }
        if (gShaderSetting.gUseShadowMap)
        {
            [unroll]
            for (int i = 0; i < LIGHT_SIZE; i++)
                dirShadows[i] = CalcShadowFactor(samShadow, gDirShadowMaps[i], mul(pin.PosW, gDirShadowTransforms[i]));
            [unroll]
            for (int j = 0; j < LIGHT_SIZE; j++)
                spotShadows[j] = CalcShadowFactor(samShadow, gSpotShadowMaps[j], mul(pin.PosW, gSpotShadowTransforms[j]));
        }

        float ambientAccess = 1.0f;
        if (gShaderSetting.gUseSsaoMap)
        {
            float4 ssao = pin.SsaoPosH / pin.SsaoPosH.w;
            ambientAccess = gSsaoMap.SampleLevel(samLinear, ssao.xy, 0.0f).r;
        }

        float4 A, D, S;
        for (int i = 0; i < gDirLightCount; ++i)
        {
            ComputeDirectionalLight(gMaterial, gDirLights[i], normalW, toEye, A, D, S);
            ambient += ambientAccess * A;
            diffuse += dirShadows[i] * D;
            spec += dirShadows[i] * S;
        }
        for (int j = 0; j < gSpotLightCount; ++j)
        {
            ComputeSpotLight(gMaterial, gSpotLights[j], pin.PosW.xyz, normalW, toEye, A, D, S);
            ambient += ambientAccess * A;
            diffuse += spotShadows[j] * D;
            spec += spotShadows[j] * S;
        }
        for (int l = 0; l < gPointLightCount; ++l)
        {
            ComputePointLight(gMaterial, gPointLights[l], pin.PosW.xyz, normalW, toEye, A, D, S);
            ambient += ambientAccess * A;
            diffuse += D;
            spec += S;
        }
        litColor = texColor * (ambient + diffuse) + spec;
    }

    if (gShaderSetting.gFogEnabled)
        litColor = lerp(litColor, gFogColor, saturate((distToEye - gFogStart) / gFogRange));
    litColor.a = 1.0f;
    return litColor;
}

technique11 TerrainTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, TerrainVS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, TerrainPS()));
        SetDepthStencilState(TerrainDepthLessEqual, 0);
    }
}
