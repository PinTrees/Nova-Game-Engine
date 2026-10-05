
cbuffer cbPerFrame
{
    float3 gEyePosW;
	
    float gHeightScale;
    float gMaxTessDistance;
    float gMinTessDistance;
    float gMinTessFactor;
    float gMaxTessFactor;

    // 그림자 바이어스 (Unity URP 의 ApplyShadowBias)
    //  gShadowLight: w = 0 이면 xyz = 빛 쪽 방향(방향광), w = 1 이면 xyz = 광원 위치(스포트/점광)
    //  gShadowBias: x = 깊이 바이어스, y = 노멀 바이어스 (방향광은 월드 단위, 원근 광원은 거리 1 당 값)
    float4 gShadowLight;
    float2 gShadowBias;
};

cbuffer cbPerObject
{
    float4x4 gWorld;
    float4x4 gWorldInvTranspose;
    float4x4 gViewProj;
    float4x4 gWorldViewProj;
    float4x4 gTexTransform;
    float gAlphaCutoff;   // NOVA: 재질의 Alpha Clipping 기준. 0 = 예전 고정값 0.15
}; 

cbuffer cbSkinned
{
    float4x4 gBoneTransforms[256];   // NOVA: 스킨 본 최대 256 개
};

// Nonnumeric values cannot be added to a cbuffer.
Texture2D gDiffuseMap;
Texture2D gNormalMap;
 
SamplerState samLinear
{
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = Wrap;
    AddressV = Wrap;
};

struct VertexIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 Tex : TEXCOORD;
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
    float2 Tex : TEXCOORD;
};

// 빛에서 멀어지는 쪽으로 깊이 바이어스, 표면 안쪽으로 노멀 바이어스 (빛과 비스듬할수록 크게)
float3 ApplyShadowBias(float3 posW, float3 normalW)
{
    float3 L = gShadowLight.xyz;
    float scale = 1.0f;
    if (gShadowLight.w > 0.5f)
    {
        const float3 v = gShadowLight.xyz - posW;
        scale = length(v);
        L = v / max(scale, 0.0001f);
    }
    const float invNdotL = 1.0f - saturate(dot(L, normalW));
    posW -= L * (gShadowBias.x * scale);
    posW -= normalW * (invNdotL * gShadowBias.y * scale);
    return posW;
}
 
VertexOut VS(VertexIn vin)
{
    VertexOut vout;

    float3 posW = mul(float4(vin.PosL, 1.0f), gWorld).xyz;
    const float3 normalW = normalize(mul(vin.NormalL, (float3x3) gWorldInvTranspose));
    vout.PosH = mul(float4(ApplyShadowBias(posW, normalW), 1.0f), gViewProj);
    vout.Tex = mul(float4(vin.Tex, 0.0f, 1.0f), gTexTransform).xy;

    return vout;
}

VertexOut VS_Instancing(VertexIn_Instancing vin)
{
    VertexOut vout;

    const float3 posW = mul(float4(vin.PosL, 1.0f), vin.World).xyz;
    const float3 normalW = normalize(mul(vin.NormalL, (float3x3) vin.World));   // 균일 크기 가정
    vout.PosH = mul(float4(ApplyShadowBias(posW, normalW), 1.0f), gViewProj);
    
    vout.Tex = mul(float4(vin.Tex, 0.0f, 1.0f), gTexTransform).xy;

    return vout;
}

VertexOut SkinnedVS(SkinnedVertexIn vin)
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
    for (int i = 0; i < 4; ++i)
    {
	    // Assume no nonuniform scaling when transforming normals, so 
		// that we do not have to use the inverse-transpose.
        posL += weights[i] * mul(float4(vin.PosL, 1.0f), gBoneTransforms[vin.BoneIndices[i]]).xyz;
        normalL += weights[i] * mul(vin.NormalL, (float3x3) gBoneTransforms[vin.BoneIndices[i]]);
    }
 
	// Transform to homogeneous clip space.
    // 월드 → 바이어스 → 광원 클립 공간
    const float3 posW = mul(float4(posL, 1.0f), gWorld).xyz;
    const float3 normalW = normalize(mul(normalL, (float3x3) gWorldInvTranspose));
    vout.PosH = mul(float4(ApplyShadowBias(posW, normalW), 1.0f), gViewProj);
	
	// Output vertex attributes for interpolation across triangle.
    vout.Tex = mul(float4(vin.Tex, 0.0f, 1.0f), gTexTransform).xy;

    return vout;
}

struct TessVertexOut
{
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
    float2 Tex : TEXCOORD;
    float TessFactor : TESS;
};

TessVertexOut TessVS(VertexIn vin)
{
    TessVertexOut vout;

    vout.PosW = mul(float4(vin.PosL, 1.0f), gWorld).xyz;
    vout.NormalW = mul(vin.NormalL, (float3x3) gWorldInvTranspose);
    vout.Tex = mul(float4(vin.Tex, 0.0f, 1.0f), gTexTransform).xy;

    float d = distance(vout.PosW, gEyePosW);

	// Normalized tessellation factor. 
	// The tessellation is 
	//   0 if d >= gMinTessDistance and
	//   1 if d <= gMaxTessDistance.  
    float tess = saturate((gMinTessDistance - d) / (gMinTessDistance - gMaxTessDistance));
	
	// Rescale [0,1] --> [gMinTessFactor, gMaxTessFactor].
    vout.TessFactor = gMinTessFactor + tess * (gMaxTessFactor - gMinTessFactor);

    return vout;
}

struct PatchTess
{
    float EdgeTess[3] : SV_TessFactor;
    float InsideTess : SV_InsideTessFactor;
};

PatchTess PatchHS(InputPatch<TessVertexOut, 3> patch,
                  uint patchID : SV_PrimitiveID)
{
    PatchTess pt;
	
	// Average tess factors along edges, and pick an edge tess factor for 
	// the interior tessellation.  It is important to do the tess factor
	// calculation based on the edge properties so that edges shared by 
	// more than one triangle will have the same tessellation factor.  
	// Otherwise, gaps can appear.
    pt.EdgeTess[0] = 0.5f * (patch[1].TessFactor + patch[2].TessFactor);
    pt.EdgeTess[1] = 0.5f * (patch[2].TessFactor + patch[0].TessFactor);
    pt.EdgeTess[2] = 0.5f * (patch[0].TessFactor + patch[1].TessFactor);
    pt.InsideTess = pt.EdgeTess[0];
	
    return pt;
}

struct HullOut
{
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
    float2 Tex : TEXCOORD;
};

[domain("tri")]
[partitioning("fractional_odd")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("PatchHS")]
HullOut HS(InputPatch<TessVertexOut, 3> p,
           uint i : SV_OutputControlPointID,
           uint patchId : SV_PrimitiveID)
{
    HullOut hout;
	
	// Pass through shader.
    hout.PosW = p[i].PosW;
    hout.NormalW = p[i].NormalW;
    hout.Tex = p[i].Tex;
	
    return hout;
}

struct DomainOut
{
    float4 PosH : SV_POSITION;
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
    float2 Tex : TEXCOORD;
};

// The domain shader is called for every vertex created by the tessellator.  
// It is like the vertex shader after tessellation.
[domain("tri")]
DomainOut DS(PatchTess patchTess,
             float3 bary : SV_DomainLocation,
             const OutputPatch<HullOut, 3> tri)
{
    DomainOut dout;
	
	// Interpolate patch attributes to generated vertices.
    dout.PosW = bary.x * tri[0].PosW + bary.y * tri[1].PosW + bary.z * tri[2].PosW;
    dout.NormalW = bary.x * tri[0].NormalW + bary.y * tri[1].NormalW + bary.z * tri[2].NormalW;
    dout.Tex = bary.x * tri[0].Tex + bary.y * tri[1].Tex + bary.z * tri[2].Tex;
	
	// Interpolating normal can unnormalize it, so normalize it.
    dout.NormalW = normalize(dout.NormalW);
	
	//
	// Displacement mapping.
	//
	
	// Choose the mipmap level based on distance to the eye; specifically, choose
	// the next miplevel every MipInterval units, and clamp the miplevel in [0,6].
    const float MipInterval = 20.0f;
    float mipLevel = clamp((distance(dout.PosW, gEyePosW) - MipInterval) / MipInterval, 0.0f, 6.0f);
	
	// Sample height map (stored in alpha channel).
    float h = gNormalMap.SampleLevel(samLinear, dout.Tex, mipLevel).a;
	
	// Offset vertex along normal.
    dout.PosW += (gHeightScale * (h - 1.0)) * dout.NormalW;
	
	// Project to homogeneous clip space.
    dout.PosH = mul(float4(dout.PosW, 1.0f), gViewProj);
	
    return dout;
}

// This is only used for alpha cut out geometry, so that shadows 
// show up correctly.  Geometry that does not need to sample a
// texture can use a NULL pixel shader for depth pass.
void PS(VertexOut pin)
{
    float4 diffuse = gDiffuseMap.Sample(samLinear, pin.Tex);

	// Don't write transparent pixels to the shadow map.
    clip(diffuse.a - (gAlphaCutoff > 0.0f ? gAlphaCutoff : 0.15f));
}

// This is only used for alpha cut out geometry, so that shadows 
// show up correctly.  Geometry that does not need to sample a
// texture can use a NULL pixel shader for depth pass.
void TessPS(DomainOut pin)
{
    float4 diffuse = gDiffuseMap.Sample(samLinear, pin.Tex);

	// Don't write transparent pixels to the shadow map.
    clip(diffuse.a - (gAlphaCutoff > 0.0f ? gAlphaCutoff : 0.15f));
}

RasterizerState Depth
{
	// [From MSDN]
	// If the depth buffer currently bound to the output-merger stage has a UNORM format or
	// no depth buffer is bound the bias value is calculated like this: 
	//
	// Bias = (float)DepthBias * r + SlopeScaledDepthBias * MaxDepthSlope;
	//
	// where r is the minimum representable value > 0 in the depth-buffer format converted to float32.
	// [/End MSDN]
	// 
	// For a 24-bit depth buffer, r = 1 / 2^24.
	//
	// Example: DepthBias = 100000 ==> Actual DepthBias = 100000/2^24 = .006

	// You need to experiment with these values for your scene.
    // 바이어스는 VS 의 ApplyShadowBias(월드 공간, 텍셀 크기 비례)가 맡는다 → 하드웨어 바이어스는 쓰지 않음
    DepthBias = 0;
    DepthBiasClamp = 0.0f;
    SlopeScaledDepthBias = 0.0f;
};

technique11 BuildShadowMapTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS()));
        SetGeometryShader(NULL);
        SetPixelShader(NULL);

        SetRasterizerState(Depth);
    }
}

technique11 BuildShadowMapAlphaClipTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS()));
    }
}

technique11 BuildShadowMapInstancingTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Instancing()));
        SetGeometryShader(NULL);
        SetPixelShader(NULL);

        SetRasterizerState(Depth);
    }
}

// 재질 테셀레이션 (60. Tessellation.fx): 높이 맵만큼 민 모양으로 그림자. 나눔은 화면 카메라 (gTessEye) 기준
#include "60. Tessellation.fx"

TessCP VS_TessInstancing(VertexIn_Instancing vin)
{
    return TessBatchCP(vin.PosL, vin.NormalL, vin.TangentL, mul(float4(vin.Tex, 0.0f, 1.0f), gTexTransform).xy, vin.World);
}

[domain("tri")]
VertexOut DS_TessShadow(TessPatch pt, float3 w : SV_DomainLocation, const OutputPatch<TessCP, 3> tri)
{
    const TessCP v = TessEvaluate(tri[0], tri[1], tri[2], w);
    VertexOut o;
    o.PosH = mul(float4(ApplyShadowBias(v.PosW, normalize(v.NormalW)), 1.0f), gViewProj);
    o.Tex = v.Tex;
    return o;
}

technique11 TessBuildShadowMapInstancingTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_TessInstancing()));
        SetHullShader(CompileShader(hs_5_0, TessHS()));
        SetDomainShader(CompileShader(ds_5_0, DS_TessShadow()));
        SetGeometryShader(NULL);
        SetPixelShader(NULL);

        SetRasterizerState(Depth);
    }
}

technique11 BuildShadowMapAlphaClipInstancingTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Instancing()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS()));
    }
}

technique11 BuildShadowMapSkinnedTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, SkinnedVS()));
        SetGeometryShader(NULL);
        SetPixelShader(NULL);

        SetRasterizerState(Depth);
    }
}

technique11 BuildShadowMapAlphaClipSkinnedTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, SkinnedVS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS()));
    }
}

technique11 TessBuildShadowMapTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, TessVS()));
        SetHullShader(CompileShader(hs_5_0, HS()));
        SetDomainShader(CompileShader(ds_5_0, DS()));
        SetGeometryShader(NULL);
        SetPixelShader(NULL);

        SetRasterizerState(Depth);
    }
}

technique11 TessBuildShadowMapAlphaClipTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, TessVS()));
        SetHullShader(CompileShader(hs_5_0, HS()));
        SetDomainShader(CompileShader(ds_5_0, DS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, TessPS()));
    }
}
//=============================================================================
// NOVA 지형 (Terrain 컴포넌트) - 그림자맵 패스 (gViewProj = 광원 ViewProj)
//=============================================================================
#include "40. TerrainCommon.fx"

float4 TerrainShadowVS(uint vid : SV_VertexID) : SV_POSITION
{
    float2 uv;
    float3 posW = TerrainVertexWorld(vid, uv);
    return mul(float4(ApplyShadowBias(posW, TerrainNormalUV(uv)), 1.0f), gViewProj);
}

technique11 TerrainShadowTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, TerrainShadowVS()));
        SetGeometryShader(NULL);
        SetPixelShader(NULL);
        SetRasterizerState(Depth);
    }
}

// 지형 테셀레이션 (Terrain Layer 의 Height Map — 61. TerrainTessellation.fx): 본 패스와 같은 모양으로 그림자
#include "61. TerrainTessellation.fx"

[domain("tri")]
float4 TerrainTessShadowDS(TessPatch pt, float3 w : SV_DomainLocation, const OutputPatch<TerrainCP, 3> tri) : SV_POSITION
{
    float2 uv;
    float3 n;
    float spacing, d;
    const float3 p = TerrainTessPosition(tri[0], tri[1], tri[2], w, uv, n, spacing, d);
    return mul(float4(ApplyShadowBias(p, n), 1.0f), gViewProj);
}

technique11 TerrainTessShadowTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, TerrainTessVS()));
        SetHullShader(CompileShader(hs_5_0, TerrainHS()));
        SetDomainShader(CompileShader(ds_5_0, TerrainTessShadowDS()));
        SetGeometryShader(NULL);
        SetPixelShader(NULL);
        SetRasterizerState(Depth);
    }
}

//=============================================================================
// NOVA 나무 (Tree / 지형 나무) - 그림자맵 패스 (인스턴싱). 잎은 구운 잎 텍스처로, 임포스터는 빛을 보는 사각형으로
//=============================================================================
#include "44. TreeCommon.fx"

RasterizerState TreeShadowCullNone
{
    CullMode = None;
};

struct TreeShadowOut
{
    float4 PosH : SV_POSITION;
    float2 UV : TEXCOORD0;
    float Seed : TEXCOORD1;
};

TreeShadowOut TreeShadowVS(TreeVertexIn vin, TreeInstanceIn inst)
{
    TreeShadowOut vout;
    float3 normalW;
    const float3 posW = TreeWorldPos(vin, inst, normalW);
    vout.PosH = mul(float4(ApplyShadowBias(posW, normalW), 1.0f), gViewProj);
    vout.UV = vin.UV;
    vout.Seed = vin.Phase.z;
    return vout;
}

void TreeShadowLeafPS(TreeShadowOut pin)
{
    TreeLeafClip(pin.UV, pin.Seed);
}

// 임포스터: 빛을 바라보는 사각형 (gTreeViewPos = 빛 쪽). 깊이 바이어스만 (법선 = 빛 방향)
float4 TreeImpostorShadowVS(uint vid : SV_VertexID, TreeInstanceIn inst, out float2 uv : TEXCOORD0) : SV_POSITION
{
    const TreeImpostorGeom g = TreeImpostorVertex(vid, inst);
    uv = g.UV;
    const float3 L = gShadowLight.w > 0.5f ? normalize(gShadowLight.xyz - g.PosW) : gShadowLight.xyz;
    return mul(float4(ApplyShadowBias(g.PosW, L), 1.0f), gViewProj);
}

void TreeImpostorShadowPS(float4 posH : SV_POSITION, float2 uv : TEXCOORD0)
{
    clip(gTreeImpostorAlbedo.Sample(samTreeImpostor, uv).a - gTreeImpostor.w);
}

technique11 TreeShadowBarkTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, TreeShadowVS()));
        SetGeometryShader(NULL);
        SetPixelShader(NULL);
        SetRasterizerState(Depth);
    }
}

technique11 TreeShadowLeafTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, TreeShadowVS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, TreeShadowLeafPS()));
        SetRasterizerState(TreeShadowCullNone);
    }
}

technique11 TreeShadowImpostorTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, TreeImpostorShadowVS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, TreeImpostorShadowPS()));
        SetRasterizerState(TreeShadowCullNone);
    }
}


// ---- 바위 (47. RockCommon.fx)
#include "47. RockCommon.fx"

float4 RockShadowVS(RockVertexIn vin, RockInstanceIn inst) : SV_POSITION
{
    float3 normalW;
    const float3 posW = RockWorldPos(vin, inst, normalW);
    return mul(float4(ApplyShadowBias(posW, normalW), 1.0f), gViewProj);
}

technique11 RockShadowTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, RockShadowVS()));
        SetGeometryShader(NULL);
        SetPixelShader(NULL);
        SetRasterizerState(Depth);
    }
}


// ---- 지형 디테일 (48. DetailCommon.fx): 잎·돌 모두 불투명 삼각형 → 깊이만
#include "48. DetailCommon.fx"

float4 DetailShadowVS(DetailVertexIn vin, DetailInstanceIn inst) : SV_POSITION
{
    float3 normalW;
    float fade;
    const float3 posW = DetailWorldPos(vin, inst, normalW, fade);
    return mul(float4(ApplyShadowBias(posW, normalW), 1.0f), gViewProj);
}

RasterizerState DetailShadowCullNone
{
    CullMode = None;
};

technique11 DetailShadowTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, DetailShadowVS()));
        SetGeometryShader(NULL);
        SetPixelShader(NULL);
        SetRasterizerState(DetailShadowCullNone);
    }
}
