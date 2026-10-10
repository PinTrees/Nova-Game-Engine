//=============================================================================
// 63. MotionVectors.fx — 모션 벡터 (화면 속도): 지터 뺀 화면 좌표 (uv) 의 이번 − 지난 프레임
//  1) CameraMotionTech  : 전체 화면 — 깊이 프리패스의 뷰 깊이로 위치를 되짚어 지난 카메라로 (하늘 = 방향만, 회전)
//  2) ObjectMotionTech  : 움직인 Mesh Renderer — 지난 월드 행렬로
//  3) SkinnedMotionTech : Skinned Mesh Renderer — 지난 월드 행렬 · 지난 본 팔레트로
//  물체 패스는 깊이 버퍼 대신 픽셀 셰이더에서 프리패스 깊이와 비교한다 (다른 면이면 버림 — 두 셰이더의 계산이 조금 달라도 구멍이 없다)
//  쓰는 곳: TAA (히스토리 자리), Motion Blur (Camera And Objects), SSAO 시간 누적
//=============================================================================
cbuffer cbMotion
{
    float4x4 gViewProjJ;      // 이번 뷰 × 투영 (지터 — 깊이 프리패스와 같은 래스터)
    float4x4 gViewProj;       // 이번 (지터 없음)
    float4x4 gPrevViewProj;   // 지난 프레임 (지터 없음)
    float4x4 gView;
    float4x4 gInvView;
    float4 gProjInfo;         // 지터한 투영의 P11, P22, (원근) P31 · P32 / (직교) P41 · P42
    float4 gMotionFlags;      // x 1 = 직교 투영, y 1 = 이 물체는 움직임 없음 (Force No Motion)
    float4x4 gWorld;
    float4x4 gPrevWorld;
};

cbuffer cbBones
{
    float4x4 gBones[256];
    float4x4 gPrevBones[256];
};

Texture2D gNormalDepth;   // 깊이 프리패스: xyz 뷰 노멀, w 뷰 깊이 (빈 곳 1e5)

float2 ToUV(float4 clip)
{
    return float2(clip.x / clip.w * 0.5f + 0.5f, 0.5f - clip.y / clip.w * 0.5f);
}

// 화면 uv (지터한 래스터의 픽셀 가운데) + 뷰 깊이 → 뷰 위치
float3 ViewPos(float2 uv, float z)
{
    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    float scale = gMotionFlags.x > 0.5f ? 1.0f : z;   // 직교 = 깊이와 상관없이
    return float3((ndc - gProjInfo.zw) * scale / gProjInfo.xy, z);
}

// ---------------------------------------------------------------- 카메라 (전체 화면)
struct FullOut
{
    float4 PosH : SV_POSITION;
    float2 Tex : TEXCOORD0;
};

FullOut VS_Full(uint id : SV_VertexID)
{
    FullOut o;
    float2 t = float2((id << 1) & 2, id & 2);
    o.PosH = float4(t * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    o.Tex = t;
    return o;
}

float4 PS_Camera(FullOut pin) : SV_Target
{
    float z = gNormalDepth.Load(int3(int2(pin.PosH.xy), 0)).w;
    if (z > 1e4f)
    {
        // 하늘: 무한히 먼 방향 — 회전만 (직교 투영은 움직임 없음)
        if (gMotionFlags.x > 0.5f)
            return 0.0f;
        float4 dirW = float4(mul(ViewPos(pin.Tex, 1.0f), (float3x3)gInvView), 0.0f);
        float4 c = mul(dirW, gViewProj), p = mul(dirW, gPrevViewProj);
        if (c.w <= 1e-4f || p.w <= 1e-4f)
            return 0.0f;
        return float4(ToUV(c) - ToUV(p), 0.0f, 0.0f);
    }
    float4 posW = mul(float4(ViewPos(pin.Tex, z), 1.0f), gInvView);
    float4 c = mul(posW, gViewProj), p = mul(posW, gPrevViewProj);
    if (p.w <= 1e-4f)
        return 0.0f;
    return float4(ToUV(c) - ToUV(p), 0.0f, 0.0f);
}

// ---------------------------------------------------------------- 물체
struct ObjectIn
{
    float3 PosL : POSITION;
};

struct SkinnedIn
{
    float3 PosL : POSITION;
    float3 Weights : WEIGHTS;
    uint4 BoneIndices : BONEINDICES;
};

struct ObjectOut
{
    float4 PosH : SV_POSITION;
    float4 Cur : TEXCOORD0;    // 이번 클립 (지터 없음)
    float4 Prev : TEXCOORD1;   // 지난 클립 (지터 없음)
    float ViewZ : TEXCOORD2;
    float Still : TEXCOORD3;   // 1 = Force No Motion (인스턴싱 — 렌더러마다). 인스턴싱 아니면 gMotionFlags.y
};

ObjectOut Emit(float3 posL, float3 prevPosL)
{
    ObjectOut o;
    float4 posW = mul(float4(posL, 1.0f), gWorld);
    float4 prevW = mul(float4(prevPosL, 1.0f), gPrevWorld);
    o.PosH = mul(posW, gViewProjJ);
    o.Cur = mul(posW, gViewProj);
    o.Prev = mul(prevW, gPrevViewProj);
    o.ViewZ = mul(posW, gView).z;
    o.Still = gMotionFlags.y;
    return o;
}

ObjectOut VS_Object(ObjectIn vin)
{
    return Emit(vin.PosL, vin.PosL);
}

// 28. SsaoNormalDepth 의 SkinnedVS 와 같은 섞기 (네 번째 가중치 = 1 − 나머지)
ObjectOut VS_Skinned(SkinnedIn vin)
{
    float weights[4] = { vin.Weights.x, vin.Weights.y, vin.Weights.z, 1.0f - vin.Weights.x - vin.Weights.y - vin.Weights.z };
    float3 posL = 0.0f, prevL = 0.0f;
    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        posL += weights[i] * mul(float4(vin.PosL, 1.0f), gBones[vin.BoneIndices[i]]).xyz;
        prevL += weights[i] * mul(float4(vin.PosL, 1.0f), gPrevBones[vin.BoneIndices[i]]).xyz;
    }
    return Emit(posL, prevL);
}

// 스킨드 인스턴싱 (엔진 공용 — 66): 월드 · 지난 월드 · 팔레트 · 지난 팔레트를 구조 버퍼에서
#include "66. SkinInstancing.fx"
ObjectOut VS_SkinnedInstanced(SkinnedIn vin, uint iid : SV_InstanceID)
{
    const SkinInstance s = SkinInstanceLoad(gSkinInstanceBase + iid);
    float3 posL, unusedN, unusedT;
    SkinInstanceBlend(s.PalA, s.PalB, s.Lerp, vin.PosL, float3(0, 1, 0), float3(1, 0, 0), vin.Weights, vin.BoneIndices, posL, unusedN, unusedT);
    const float3 prevL = SkinInstancePrevPosition(s, vin.PosL, vin.Weights, vin.BoneIndices);
    const float4 posW = float4(SkinXform(s.C0, s.C1, s.C2, posL), 1.0f);
    const float4 prevW = float4(SkinXform(s.P0, s.P1, s.P2, prevL), 1.0f);
    ObjectOut o;
    o.PosH = mul(posW, gViewProjJ);
    o.Cur = mul(posW, gViewProj);
    o.Prev = mul(prevW, gPrevViewProj);
    o.ViewZ = mul(posW, gView).z;
    o.Still = (s.Flags & kSkinStill) != 0u ? 1.0f : 0.0f;
    return o;
}

float4 PS_Object(ObjectOut pin) : SV_Target
{
    // 프리패스에 보인 면만 (앞을 가린 물체 · 잘라낸 투명 부분은 그 자리 깊이가 다르다)
    float zPre = gNormalDepth.Load(int3(int2(pin.PosH.xy), 0)).w;
    if (abs(pin.ViewZ - zPre) > 0.02f + 0.01f * zPre)
        discard;
    if (pin.Still > 0.5f)
        return 0.0f;   // Force No Motion
    return float4(ToUV(pin.Cur) - ToUV(pin.Prev), 0.0f, 0.0f);
}

technique11 CameraMotionTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Full()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS_Camera()));
    }
}

technique11 ObjectMotionTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Object()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS_Object()));
    }
}

technique11 SkinnedMotionTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Skinned()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS_Object()));
    }
}

technique11 SkinnedInstancedMotionTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_SkinnedInstanced()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS_Object()));
    }
}
