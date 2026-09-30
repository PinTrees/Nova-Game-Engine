
cbuffer cbPerObject
{
    // Instancing
    float4x4 gView;
    float4x4 gProj;
    
    float4x4 gWorldView;
    float4x4 gWorldInvTransposeView;
    float4x4 gWorldViewProj;
    float4x4 gTexTransform;
}; 

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
	// Interpolating normal can unnormalize it, so normalize it.
    pin.NormalV = normalize(pin.NormalV);

    if (gAlphaClip)
    {
        float4 texColor = gDiffuseMap.Sample(samLinear, pin.Tex);
		 
        clip(texColor.a - 0.1f);
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
    const float3 posW = TreeWorldPos(vin, inst, normalW);
    vout.PosV = mul(float4(posW, 1.0f), gView).xyz;
    vout.NormalV = mul(normalW, (float3x3) gView);
    // 본 패스(TreeVS)와 똑같이 월드 위치 × CPU 에서 곱한 ViewProj
    vout.PosH = mul(float4(posW, 1.0f), gWorldViewProj);
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
    vout.PosH = mul(float4(g.PosW, 1.0f), gWorldViewProj);
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
