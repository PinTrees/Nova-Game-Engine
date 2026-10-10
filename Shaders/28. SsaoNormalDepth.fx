
cbuffer cbPerObject
{
    // Instancing
    float4x4 gView;
    float4x4 gProj;
    
    float4x4 gWorldView;
    float4x4 gWorldInvTransposeView;
    float4x4 gWorldViewProj;
    float4x4 gTexTransform;
    float gAlphaCutoff;   // NOVA: 재질의 Alpha Clipping 기준 (본 패스와 같아야 깊이만 남는 구멍이 없다). 0 = 예전 고정값 0.1
    float4 gLodFade = float4(0, 0, 0, 0);   // NOVA: LOD Group 크로스페이드 (32 와 같은 무늬)
};

// LOD Group 크로스페이드 (gLodFade: x = 문턱, y = 1 이면 무늬 < x 인 픽셀만 / 0 이면 무늬 ≥ x, z = 1 켜짐)
//  화면 픽셀마다 고정 무늬 — 본 패스 (32) 와 같은 픽셀을 남겨 EQUAL 깊이 검사가 맞는다
float LodDither(float2 pixel) { return frac(52.9829189f * frac(dot(floor(pixel), float2(0.06711056f, 0.00583715f)))); }
void LodFadeClip(float2 pixel)
{
    if (gLodFade.z > 0.5f)
    {
        const float d = LodDither(pixel);
        clip(gLodFade.y > 0.5f ? gLodFade.x - d : d - gLodFade.x);   // 같은 값은 양쪽 다 남김 (구멍 없이 — 깊이 검사가 고름)
    }
}

cbuffer cbSkinned
{
    float4x4 gBoneTransforms[256];   // NOVA: 스킨 본 최대 256 개
};

// Nonnumeric values cannot be added to a cbuffer.
Texture2D gDiffuseMap;
 
SamplerState samLinear
{
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = WRAP;
    AddressV = WRAP;
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
    // 인스턴스 값 (BatchTech 만 읽는다): 입력 레이아웃 InstancedBasic (11 칸) 과 레지스터를 맞춘다 — 없으면 SV_InstanceID 가 v8 에 앉아
    //  레이아웃의 INSTCOLOR (v8) 와 겹친다 (D3D11 #343: 같은 레이아웃을 쓰는 셰이더끼리 서명이 맞지 않음)
    float4 InstBaseColor : INSTCOLOR;
    float4 InstSurface : INSTSURFACE;
    float4 InstEmission : INSTEMISSION;
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
    float3 PosV : POSITION;
    float3 NormalV : NORMAL;
    float2 Tex : TEXCOORD0;
};

VertexOut VS(VertexIn vin)
{
    VertexOut vout;
	
	// Transform to view space.
    vout.PosV = mul(float4(vin.PosL, 1.0f), gWorldView).xyz;
    vout.NormalV = mul(vin.NormalL, (float3x3) gWorldInvTransposeView);
		
	// Transform to homogeneous clip space.
    vout.PosH = mul(float4(vin.PosL, 1.0f), gWorldViewProj);
	
	// Output vertex attributes for interpolation across triangle.
    vout.Tex = mul(float4(vin.Tex, 0.0f, 1.0f), gTexTransform).xy;
 
    return vout;
}

VertexOut VS_Instancing(VertexIn_Instancing vin)
{
    VertexOut vout;
	
	// Transform to view space.
    //vout.PosV = mul(float4(vin.PosL, 1.0f), gWorldView).xyz;
    
    vout.PosH = mul(float4(vin.PosL, 1.0f), vin.World);

    vout.PosV = mul(vout.PosH, gView).xyz;
    
    //vout.NormalV = mul(vin.NormalL, (float3x3) gWorldInvTransposeView);
    vout.NormalV = mul(vin.NormalL, (float3x3) vin.World);
    vout.NormalV = mul(vout.NormalV, (float3x3) gView);
    
	// Transform to homogeneous clip space.
    vout.PosH = mul(vout.PosH, gView);
    vout.PosH = mul(vout.PosH, gProj);
	
	// Output vertex attributes for interpolation across triangle.
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
 
	// Transform to view space.
    vout.PosV = mul(float4(posL, 1.0f), gWorldView).xyz;
    vout.NormalV = mul(normalL, (float3x3) gWorldInvTransposeView);
		
	// Transform to homogeneous clip space.
    vout.PosH = mul(float4(posL, 1.0f), gWorldViewProj);
	
	// Output vertex attributes for interpolation across triangle.
    vout.Tex = mul(float4(vin.Tex, 0.0f, 1.0f), gTexTransform).xy;

    return vout;
}

float4 PS(VertexOut pin, uniform bool gAlphaClip) : SV_Target
{
    LodFadeClip(pin.PosH.xy);
	// Interpolating normal can unnormalize it, so normalize it.
    pin.NormalV = normalize(pin.NormalV);

    if (gAlphaClip)
    {
        float4 texColor = gDiffuseMap.Sample(samLinear, pin.Tex);
		 
        clip(texColor.a - (gAlphaCutoff > 0.0f ? gAlphaCutoff : 0.1f));
    }
	
    return float4(pin.NormalV, pin.PosV.z);
}

// MeshBatcher: 인스턴스 월드 행렬 → 월드 위치 × ViewProj (gWorldViewProj = ViewProj). 본 패스 BatchTech 와 같은 식
VertexOut VS_Batch(VertexIn_Instancing vin)
{
    VertexOut vout;
    const float4 posW = mul(float4(vin.PosL, 1.0f), vin.World);
    const float3 r0 = vin.World[0].xyz, r1 = vin.World[1].xyz, r2 = vin.World[2].xyz;
    const float3 normalW = mul(vin.NormalL, float3x3(cross(r1, r2), cross(r2, r0), cross(r0, r1)));
    vout.PosV = mul(posW, gView).xyz;
    vout.NormalV = mul(normalW, (float3x3) gView);
    vout.PosH = mul(posW, gWorldViewProj);
    vout.Tex = mul(float4(vin.Tex, 0.0f, 1.0f), gTexTransform).xy;
    return vout;
}

technique11 NormalDepthBatchTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Batch()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS(false)));
    }
}

// MeshBatcher: Alpha Clipping 재질 (재질마다 묶음 — 그림 알파로 잘라 본 패스 (EQUAL) 와 같은 구멍)
technique11 NormalDepthAlphaClipBatchTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Batch()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS(true)));
    }
}

// 바람 묶음 (PCG 나무 · 풀 — 67. BatchWind.fx): 본 패스 BatchWindTech (32 의 VS_BatchWind) 와 같은 식
#include "67. BatchWind.fx"
VertexOut VS_BatchWind(VertexIn_Instancing vin)
{
    VertexOut vout;
    precise float4 posW = mul(float4(vin.PosL, 1.0f), vin.World);
    posW.xyz += BatchWindOffset(posW.xyz, vin.World);
    const float3 r0 = vin.World[0].xyz, r1 = vin.World[1].xyz, r2 = vin.World[2].xyz;
    const float3 normalW = mul(vin.NormalL, float3x3(cross(r1, r2), cross(r2, r0), cross(r0, r1)));
    vout.PosV = mul(posW, gView).xyz;
    vout.NormalV = mul(normalW, (float3x3) gView);
    vout.PosH = mul(posW, gWorldViewProj);
    vout.Tex = mul(float4(vin.Tex, 0.0f, 1.0f), gTexTransform).xy;
    return vout;
}

technique11 NormalDepthWindBatchTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_BatchWind()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS(false)));
    }
}

technique11 NormalDepthAlphaClipWindBatchTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_BatchWind()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS(true)));
    }
}

// 재질 테셀레이션 (60. Tessellation.fx): 본 패스 (32 의 TessBatchTech) 와 같은 함수로 같은 깊이
#include "60. Tessellation.fx"

TessCP VS_TessBatchND(VertexIn_Instancing vin)
{
    return TessBatchCP(vin.PosL, vin.NormalL, vin.TangentL, mul(float4(vin.Tex, 0.0f, 1.0f), gTexTransform).xy, vin.World);
}

[domain("tri")]
VertexOut DS_TessBatchND(TessPatch pt, float3 w : SV_DomainLocation, const OutputPatch<TessCP, 3> tri)
{
    const TessCP v = TessEvaluate(tri[0], tri[1], tri[2], w);
    VertexOut o;
    precise const float4 posH = mul(float4(v.PosW, 1.0f), gTessViewProj);   // 본 패스와 같은 비트 (precise)
    o.PosH = posH;
    o.PosV = mul(float4(v.PosW, 1.0f), gTessView).xyz;
    o.NormalV = mul(v.NormalW, (float3x3) gTessView);
    o.Tex = v.Tex;
    return o;
}

technique11 TessNormalDepthBatchTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_TessBatchND()));
        SetHullShader(CompileShader(hs_5_0, TessHS()));
        SetDomainShader(CompileShader(ds_5_0, DS_TessBatchND()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS(false)));
    }
}

technique11 NormalDepth
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS(false)));
    }
}

technique11 NormalDepthAlphaClip
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS(true)));
    }
}

technique11 NormalDepthInstancing
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Instancing()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS(false)));
    }
}

technique11 NormalDepthAlphaClipInstancing
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Instancing()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS(true)));
    }
}

technique11 NormalDepthSkinned
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, SkinnedVS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS(false)));
    }
}

// 스킨드 인스턴싱 (엔진 공용 — 66): gWorldViewProj = ViewProj (MeshBatcher 의 VS_Batch 와 같이) — 본 패스와 같은 식
#include "66. SkinInstancing.fx"
VertexOut SkinnedInstancedVS(SkinnedVertexIn vin, uint iid : SV_InstanceID)
{
    const SkinInstance s = SkinInstanceLoad(gSkinInstanceBase + iid);
    float3 posW, normalW;
    float4 tangentW;
    SkinInstanceWorld(s, vin.PosL, vin.NormalL, vin.TangentL, vin.Weights, vin.BoneIndices, posW, normalW, tangentW);
    VertexOut vout;
    vout.PosV = mul(float4(posW, 1.0f), gView).xyz;
    vout.NormalV = mul(normalW, (float3x3) gView);
    precise float4 posH = mul(float4(posW, 1.0f), gWorldViewProj);   // 32 와 같은 식 (EQUAL)
    vout.PosH = posH;
    vout.Tex = mul(float4(vin.Tex, 0.0f, 1.0f), gTexTransform).xy;
    return vout;
}

technique11 NormalDepthSkinnedInstancedTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, SkinnedInstancedVS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS(false)));
    }
}

// Alpha Clipping 재질 (머리카락 · 레이스): 본 패스와 같은 기준으로 잘라 깊이를 남기지 않는다
technique11 NormalDepthAlphaClipSkinnedInstancedTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, SkinnedInstancedVS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS(true)));
    }
}

// 애니메이션 임포스터: 덮이지 않은 곳은 잘라 깊이를 남기지 않는다 (본 패스는 EQUAL)
struct SkinImpNDOut
{
    float4 PosH : SV_POSITION;
    float3 PosV : POSITION;
    float2 UV : TEXCOORD0;
    float3 AxisX : TEXCOORD1;
    float3 AxisZ : TEXCOORD2;
};

SkinImpNDOut SkinnedImpostorVS(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
    const SkinInstance s = SkinInstanceLoad(gSkinInstanceBase + iid);
    const SkinImpostorGeom g = SkinImpostorVertex(vid, s);
    SkinImpNDOut o;
    precise float4 posH = mul(float4(g.PosW, 1.0f), gWorldViewProj);
    o.PosH = posH;
    o.PosV = mul(float4(g.PosW, 1.0f), gView).xyz;
    o.UV = g.UV;
    o.AxisX = g.AxisX;
    o.AxisZ = g.AxisZ;
    return o;
}

float4 SkinnedImpostorPS(SkinImpNDOut pin) : SV_Target
{
    clip(gSkinImpAlbedo.Sample(samSkinImp, pin.UV).a - gSkinImpostor2.y);
    const float3 n = SkinImpostorNormalW(gSkinImpNormal.Sample(samSkinImp, pin.UV).rgb, pin.AxisX, pin.AxisZ);
    return float4(normalize(mul(n, (float3x3) gView)), pin.PosV.z);
}

RasterizerState SkinImpNDCullNone
{
    CullMode = NONE;
};

technique11 NormalDepthSkinnedImpostorTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, SkinnedImpostorVS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, SkinnedImpostorPS()));
        SetRasterizerState(SkinImpNDCullNone);
    }
}

technique11 NormalDepthAlphaClipSkinned
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, SkinnedVS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS(true)));
    }
}
//=============================================================================
// NOVA 지형 (Terrain 컴포넌트) - SSAO 노멀/깊이 패스 (gView, gProj 사용)
//=============================================================================
#include "40. TerrainCommon.fx"

struct TerrainNormalDepthOut
{
    float4 PosH : SV_POSITION;
    float3 PosV : POSITION;
    float2 UV : TEXCOORD0;
};

TerrainNormalDepthOut TerrainNormalDepthVS(uint vid : SV_VertexID)
{
    TerrainNormalDepthOut vout;
    float2 uv;
    float3 posW = TerrainVertexWorld(vid, uv);
    vout.PosV = mul(float4(posW, 1.0f), gView).xyz;
    // 본 패스(TerrainVS)와 똑같이 CPU 에서 곱한 ViewProj 한 번으로 → 깊이가 정확히 같아 EQUAL/LESS_EQUAL 검사가 흔들리지 않는다
    vout.PosH = mul(float4(posW, 1.0f), gWorldViewProj);
    vout.UV = uv;
    return vout;
}

float4 TerrainNormalDepthPS(TerrainNormalDepthOut pin) : SV_Target
{
    float3 normalV = normalize(mul(TerrainNormalUV(pin.UV), (float3x3) gView));
    return float4(normalV, pin.PosV.z);
}

technique11 TerrainNormalDepthTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, TerrainNormalDepthVS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, TerrainNormalDepthPS()));
    }
}

// 지형 테셀레이션 (Terrain Layer 의 Height Map — 61. TerrainTessellation.fx): 본 패스 (32 의 TerrainTessDS) 와 같은 자리
#include "61. TerrainTessellation.fx"

[domain("tri")]
TerrainNormalDepthOut TerrainTessNormalDepthDS(TessPatch pt, float3 w : SV_DomainLocation, const OutputPatch<TerrainCP, 3> tri)
{
    TerrainNormalDepthOut o;
    float2 uv;
    float3 n;
    const float3 p = TerrainTessPosition(tri[0], tri[1], tri[2], w, uv, n);
    o.PosV = mul(float4(p, 1.0f), gView).xyz;
    precise const float4 posH = mul(float4(p, 1.0f), gWorldViewProj);   // 본 패스와 같은 비트 (precise)
    o.PosH = posH;
    o.UV = uv;
    return o;
}

technique11 TerrainTessNormalDepthTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, TerrainTessVS()));
        SetHullShader(CompileShader(hs_5_0, TerrainHS()));
        SetDomainShader(CompileShader(ds_5_0, TerrainTessNormalDepthDS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, TerrainNormalDepthPS()));
    }
}

//=============================================================================
// NOVA 나무 (Tree / 지형 나무) - SSAO 노멀/깊이 패스 (gView, gWorldViewProj = ViewProj). 본 패스와 같은 LOD 디더·잎 자르기
//=============================================================================
#include "44. TreeCommon.fx"

RasterizerState TreeNormalDepthCullNone
{
    CullMode = None;
};

struct TreeNormalDepthOut
{
    float4 PosH : SV_POSITION;
    float3 PosV : POSITION;
    float3 NormalV : NORMAL;
    float2 UV : TEXCOORD0;
    float Seed : TEXCOORD1;
    float2 Fade : TEXCOORD2;
};

TreeNormalDepthOut TreeNormalDepthVS(TreeVertexIn vin, TreeInstanceIn inst)
{
    TreeNormalDepthOut vout;
    float3 normalW;
    // precise: 본 패스(TreeVS)가 이 깊이와 EQUAL 로 맞추므로 식 순서가 바뀌지 않게
    precise float3 posW = TreeWorldPos(vin, inst, normalW);
    vout.PosV = mul(float4(posW, 1.0f), gView).xyz;
    vout.NormalV = mul(normalW, (float3x3) gView);
    // 본 패스(TreeVS)와 똑같이 월드 위치 × CPU 에서 곱한 ViewProj
    precise float4 posH = mul(float4(posW, 1.0f), gWorldViewProj);
    vout.PosH = posH;
    vout.UV = vin.UV;
    vout.Seed = vin.Phase.z;
    vout.Fade = inst.Extra.zw;
    return vout;
}

float4 TreeNormalDepthBarkPS(TreeNormalDepthOut pin) : SV_Target
{
    TreeLodClip(pin.PosH.xy, pin.Fade);
    return float4(normalize(pin.NormalV), pin.PosV.z);
}

float4 TreeNormalDepthLeafPS(TreeNormalDepthOut pin) : SV_Target
{
    TreeLodClip(pin.PosH.xy, pin.Fade);
    TreeLeafClip(pin.UV, pin.Seed);
    return float4(normalize(pin.NormalV), pin.PosV.z);
}

struct TreeImpostorNDOut
{
    float4 PosH : SV_POSITION;
    float3 PosV : POSITION;
    float2 UV : TEXCOORD0;
    float3 AxisX : TEXCOORD1;
    float3 AxisZ : TEXCOORD2;
    float2 Fade : TEXCOORD3;
};

TreeImpostorNDOut TreeImpostorNormalDepthVS(uint vid : SV_VertexID, TreeInstanceIn inst)
{
    const TreeImpostorGeom g = TreeImpostorVertex(vid, inst);
    TreeImpostorNDOut vout;
    vout.PosV = mul(float4(g.PosW, 1.0f), gView).xyz;
    precise float4 posH = mul(float4(g.PosW, 1.0f), gWorldViewProj);
    vout.PosH = posH;
    vout.UV = g.UV;
    vout.AxisX = g.AxisX;
    vout.AxisZ = g.AxisZ;
    vout.Fade = inst.Extra.zw;
    return vout;
}

float4 TreeImpostorNormalDepthPS(TreeImpostorNDOut pin) : SV_Target
{
    TreeLodClip(pin.PosH.xy, pin.Fade);
    clip(gTreeImpostorAlbedo.Sample(samTreeImpostor, pin.UV).a - gTreeImpostor.w);
    const float3 n = TreeImpostorNormalW(gTreeImpostorNormal.Sample(samTreeImpostor, pin.UV).rgb, pin.AxisX, pin.AxisZ);
    return float4(normalize(mul(n, (float3x3) gView)), pin.PosV.z);
}

technique11 TreeNormalDepthBarkTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, TreeNormalDepthVS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, TreeNormalDepthBarkPS()));
    }
}

technique11 TreeNormalDepthLeafTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, TreeNormalDepthVS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, TreeNormalDepthLeafPS()));
        SetRasterizerState(TreeNormalDepthCullNone);
    }
}

technique11 TreeNormalDepthImpostorTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, TreeImpostorNormalDepthVS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, TreeImpostorNormalDepthPS()));
        SetRasterizerState(TreeNormalDepthCullNone);
    }
}


// ---- 바위 (47. RockCommon.fx)
#include "47. RockCommon.fx"

struct RockNormalDepthOut
{
    float4 PosH : SV_POSITION;
    float3 PosV : POSITION;
    float3 NormalV : NORMAL;
};

RockNormalDepthOut RockNormalDepthVS(RockVertexIn vin, RockInstanceIn inst)
{
    RockNormalDepthOut vout;
    float3 normalW;
    precise float3 posW = RockWorldPos(vin, inst, normalW);
    vout.PosV = mul(float4(posW, 1.0f), gView).xyz;
    vout.NormalV = mul(normalW, (float3x3) gView);
    precise float4 posH = mul(float4(posW, 1.0f), gWorldViewProj);
    vout.PosH = posH;
    return vout;
}

float4 RockNormalDepthPS(RockNormalDepthOut pin) : SV_Target
{
    return float4(normalize(pin.NormalV), pin.PosV.z);
}

technique11 RockNormalDepthTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, RockNormalDepthVS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, RockNormalDepthPS()));
    }
}


// ---- 지형 디테일 (48. DetailCommon.fx)
#include "48. DetailCommon.fx"

struct DetailNormalDepthOut
{
    float4 PosH : SV_POSITION;
    float3 PosV : POSITION;
    float3 NormalV : NORMAL;
};

DetailNormalDepthOut DetailNormalDepthVS(DetailVertexIn vin, DetailInstanceIn inst)
{
    DetailNormalDepthOut vout;
    float3 normalW;
    float fade;
    precise float3 posW = DetailWorldPos(vin, inst, normalW, fade);
    // 잎은 SSAO 가 잎 한 장 한 장을 어둡게 하지 않게 지면 쪽 법선으로
    if (vin.Data.y < 0.5f || vin.Data.y > 3.5f)
        normalW = normalize(lerp(normalW, inst.Ground.xyz, 0.7f));
    vout.PosV = mul(float4(posW, 1.0f), gView).xyz;
    vout.NormalV = mul(normalW, (float3x3) gView);
    precise float4 posH = mul(float4(posW, 1.0f), gWorldViewProj);
    vout.PosH = posH;
    return vout;
}

float4 DetailNormalDepthPS(DetailNormalDepthOut pin) : SV_Target
{
    const float3 n = normalize(pin.NormalV);
    return float4(dot(n, pin.PosV) > 0.0f ? -n : n, pin.PosV.z);   // 카메라 쪽을 보게 (양면)
}

RasterizerState DetailNormalDepthCullNone
{
    CullMode = None;
};

technique11 DetailNormalDepthTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, DetailNormalDepthVS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, DetailNormalDepthPS()));
        SetRasterizerState(DetailNormalDepthCullNone);
    }
}
