//=============================================================================
// 66. SkinInstancing.fx — 스킨드 인스턴싱 (엔진 공용): 같은 메시 · 재질의 스킨 메시를 한 번에 그린다 (SkinnedInstancing.cpp)
//  32 (본 · 임포스터 굽기 · 임포스터) · 26 (그림자) · 28 (깊이 프리패스) · 63 (모션 벡터) 가 include 한다
//  팔레트 = 본마다 float4 세 칸 (본 행렬 4x3 의 열 — 행 벡터 곱 v · M 의 x · y · z). gSkinPalettes 에 무엇을 넣든 같은 셰이더:
//   - Animator 가 이번 프레임 계산한 팔레트 (Auto LOD 군중) — 렌더러마다 한 벌, 섞는 비율 0
//   - 미리 구운 클립 프레임 (CrowdAnimation — 대규모 군중 · 전투) — 두 프레임을 섞는다 (GPU Gems 3 「Animated Crowd Rendering」)
//  인스턴스 = gSkinInstances 의 9 칸:
//   0 ~ 2: 월드 행렬의 열 0 · 1 · 2 (팔레트와 같은 꼴)
//   3:     팔레트 A · B (float4 칸, asuint), A → B 비율, 임포스터 아틀라스 줄
//   4 ~ 6: 지난 월드 열 (모션 벡터)
//   7:     지난 팔레트 A · B, 지난 비율, 표시 (asuint: 1 = 움직임 없음, 2 = 지난 팔레트는 gSkinPrevPalettes)
//   8:     색 (알베도에 곱한다, 선형 — 기본 1)
//  인스턴스 번호 = gSkinInstanceBase + SV_InstanceID (D3D11 의 SV_InstanceID 는 StartInstanceLocation 을 더하지 않는다)
//=============================================================================
#ifndef NOVA_SKIN_INSTANCING
#define NOVA_SKIN_INSTANCING

StructuredBuffer<float4> gSkinInstances;
StructuredBuffer<float4> gSkinPalettes;
StructuredBuffer<float4> gSkinPrevPalettes;   // 지난 프레임 팔레트 버퍼 (표시 2 일 때 — Animator 팔레트)
Texture2D gSkinImpAlbedo;                     // 애니메이션 임포스터: rgb = 알베도 (감마), a = 덮임
Texture2D gSkinImpNormal;                     //  rgb = 물체 공간 법선 * 0.5 + 0.5, a = AO

cbuffer cbSkinInstancing
{
    uint gSkinInstanceBase;
    uint gSkinPad0;   // int3 대신 낱개 (GLSL std140 정렬)
    uint gSkinPad1;
    uint gSkinPad2;
    float4 gSkinImpostor;    // x 방향 수, y 아틀라스 줄 수, z 칸 반폭 (m), w 칸 반높이 (m)
    float4 gSkinImpostor2;   // x 칸 중심 높이 (m), y 알파 문턱
    float4 gSkinView;        // xyz 카메라 위치 (임포스터가 보는 방향)
};

SamplerState samSkinImp
{
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = CLAMP;
    AddressV = CLAMP;
};

static const uint kSkinInstanceStride = 9;
static const uint kSkinStill = 1u;
static const uint kSkinPrevBuffer = 2u;

struct SkinInstance
{
    float4 C0, C1, C2;         // 월드 열
    uint PalA, PalB;
    float Lerp;
    float Row;
    float4 P0, P1, P2;         // 지난 월드 열
    uint PrevA, PrevB;
    float PrevLerp;
    uint Flags;
    float4 Tint;
};

SkinInstance SkinInstanceLoad(uint inst)
{
    const uint b = inst * kSkinInstanceStride;
    SkinInstance s;
    s.C0 = gSkinInstances[b];
    s.C1 = gSkinInstances[b + 1];
    s.C2 = gSkinInstances[b + 2];
    const float4 a = gSkinInstances[b + 3];
    s.PalA = asuint(a.x);
    s.PalB = asuint(a.y);
    s.Lerp = a.z;
    s.Row = a.w;
    s.P0 = gSkinInstances[b + 4];
    s.P1 = gSkinInstances[b + 5];
    s.P2 = gSkinInstances[b + 6];
    const float4 p = gSkinInstances[b + 7];
    s.PrevA = asuint(p.x);
    s.PrevB = asuint(p.y);
    s.PrevLerp = p.z;
    s.Flags = asuint(p.w);
    s.Tint = gSkinInstances[b + 8];
    return s;
}

// precise: 깊이 프리패스 (28) 와 본 패스 (32) 가 비트까지 같은 위치를 내야 EQUAL 깊이 검사가 맞는다
//  (셰이더마다 컴파일러가 곱 · 더하기를 다르게 합치면 — FMA — 몇 픽셀이 떨어져 뒤가 비쳤다)
float3 SkinXform(float4 c0, float4 c1, float4 c2, float3 p)
{
    const float4 q = float4(p, 1.0f);
    precise float3 r = float3(dot(q, c0), dot(q, c1), dot(q, c2));
    return r;
}

float3 SkinXformDir(float4 c0, float4 c1, float4 c2, float3 v)
{
    return float3(dot(v, c0.xyz), dot(v, c1.xyz), dot(v, c2.xyz));
}

// 월드 3x3 의 여인수 행렬 (= 역전치 × 행렬식) — 축마다 크기가 달라도 법선이 맞다 (MeshBatcher 와 같은 식)
float3 SkinInstanceNormal(float3 n, float4 c0, float4 c1, float4 c2)
{
    const float3 r0 = float3(c0.x, c1.x, c2.x), r1 = float3(c0.y, c1.y, c2.y), r2 = float3(c0.z, c1.z, c2.z);
    return mul(n, float3x3(cross(r1, r2), cross(r2, r0), cross(r0, r1)));
}

// 팔레트 A · B 를 t 로 섞어 스키닝 (네 번째 가중치 = 1 − 나머지 — 32 · 26 · 28 · 63 의 스킨과 같은 섞기)
void SkinInstanceBlend(uint a, uint b, float t, float3 pos, float3 nrm, float3 tan, float3 weights, uint4 bones,
    out float3 posL, out float3 nrmL, out float3 tanL)
{
    precise float w[4] = { weights.x, weights.y, weights.z, 1.0f - weights.x - weights.y - weights.z };
    const float4 p = float4(pos, 1.0f);
    precise float3 acc = 0.0f;
    nrmL = 0.0f;
    tanL = 0.0f;
    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        const uint o = bones[i] * 3;
        precise float4 c0 = lerp(gSkinPalettes[a + o], gSkinPalettes[b + o], t);
        precise float4 c1 = lerp(gSkinPalettes[a + o + 1], gSkinPalettes[b + o + 1], t);
        precise float4 c2 = lerp(gSkinPalettes[a + o + 2], gSkinPalettes[b + o + 2], t);
        acc += w[i] * float3(dot(p, c0), dot(p, c1), dot(p, c2));
        nrmL += w[i] * float3(dot(nrm, c0.xyz), dot(nrm, c1.xyz), dot(nrm, c2.xyz));
        tanL += w[i] * float3(dot(tan, c0.xyz), dot(tan, c1.xyz), dot(tan, c2.xyz));
    }
    posL = acc;
}

// 모션 벡터의 지난 위치: 지난 팔레트가 지난 버퍼 (Animator) 면 gSkinPrevPalettes, 아니면 같은 버퍼 (구운 클립)
//  (자원을 함수 인자로 넘기지 않는다 — 셰이더 변환기)
float3 SkinInstancePrevPosition(SkinInstance s, float3 pos, float3 weights, uint4 bones)
{
    const float w[4] = { weights.x, weights.y, weights.z, 1.0f - weights.x - weights.y - weights.z };
    const float4 p = float4(pos, 1.0f);
    const bool prevBuffer = (s.Flags & kSkinPrevBuffer) != 0u;
    float3 posL = 0.0f;
    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        const uint o = bones[i] * 3;
        float4 c0, c1, c2;
        if (prevBuffer)
        {
            c0 = lerp(gSkinPrevPalettes[s.PrevA + o], gSkinPrevPalettes[s.PrevB + o], s.PrevLerp);
            c1 = lerp(gSkinPrevPalettes[s.PrevA + o + 1], gSkinPrevPalettes[s.PrevB + o + 1], s.PrevLerp);
            c2 = lerp(gSkinPrevPalettes[s.PrevA + o + 2], gSkinPrevPalettes[s.PrevB + o + 2], s.PrevLerp);
        }
        else
        {
            c0 = lerp(gSkinPalettes[s.PrevA + o], gSkinPalettes[s.PrevB + o], s.PrevLerp);
            c1 = lerp(gSkinPalettes[s.PrevA + o + 1], gSkinPalettes[s.PrevB + o + 1], s.PrevLerp);
            c2 = lerp(gSkinPalettes[s.PrevA + o + 2], gSkinPalettes[s.PrevB + o + 2], s.PrevLerp);
        }
        posL += w[i] * float3(dot(p, c0), dot(p, c1), dot(p, c2));
    }
    return posL;
}

// 스키닝 + 월드: 위치 · 법선 · 접선 (월드)
void SkinInstanceWorld(SkinInstance s, float3 pos, float3 nrm, float4 tan, float3 weights, uint4 bones,
    out float3 posW, out float3 nrmW, out float4 tanW)
{
    float3 posL, nrmL, tanL;
    SkinInstanceBlend(s.PalA, s.PalB, s.Lerp, pos, nrm, tan.xyz, weights, bones, posL, nrmL, tanL);
    posW = SkinXform(s.C0, s.C1, s.C2, posL);
    nrmW = SkinInstanceNormal(nrmL, s.C0, s.C1, s.C2);
    tanW = float4(SkinXformDir(s.C0, s.C1, s.C2, tanL), tan.w);
}

// ---------------------------------------------------------------- 애니메이션 임포스터 (먼 캐릭터 = 카메라를 보는 사각형 하나)
//  아틀라스: 가로 = 방향 (0 = 물체 +Z 쪽에서 봄 — 나무 임포스터와 같다), 세로 = 클립 프레임 (CPU 가 줄을 고른다)
struct SkinImpostorGeom
{
    float3 PosW;
    float2 UV;
    float3 AxisX;   // 물체 X · Z 축 (월드, 정규화) → 구운 법선을 월드로
    float3 AxisZ;
};

SkinImpostorGeom SkinImpostorVertex(uint vid, SkinInstance s)
{
    const float2 corners[6] = { float2(-1, -1), float2(-1, 1), float2(1, 1), float2(-1, -1), float2(1, 1), float2(1, -1) };
    const float2 c = corners[vid];
    const float3 center = float3(s.C0.w, s.C1.w, s.C2.w);
    const float3 axRaw = SkinXformDir(s.C0, s.C1, s.C2, float3(1, 0, 0));
    const float3 ayRaw = SkinXformDir(s.C0, s.C1, s.C2, float3(0, 1, 0));
    const float3 azRaw = SkinXformDir(s.C0, s.C1, s.C2, float3(0, 0, 1));
    const float scaleXZ = length(axRaw), scaleY = length(ayRaw);
    const float3 ax = axRaw / max(scaleXZ, 1e-5f);
    const float3 az = azRaw / max(length(azRaw), 1e-5f);
    float3 toView = gSkinView.xyz - center;
    toView.y = 0.0f;
    toView = dot(toView, toView) > 1e-8f ? normalize(toView) : az;
    const float yaws = gSkinImpostor.x;
    const float azimuth = atan2(dot(toView, ax), dot(toView, az));
    float col = round(azimuth / (6.28318530718f / yaws));
    col = col - yaws * floor(col / yaws);
    const float3 right = normalize(cross(float3(0, 1, 0), -toView));
    SkinImpostorGeom g;
    g.PosW = center + right * (c.x * gSkinImpostor.z * scaleXZ) + float3(0, (gSkinImpostor2.x + c.y * gSkinImpostor.w) * scaleY, 0);
    g.UV = float2((col + c.x * 0.5f + 0.5f) / yaws, (s.Row + 0.5f - c.y * 0.5f) / gSkinImpostor.y);
    g.AxisX = ax;
    g.AxisZ = az;
    return g;
}

float3 SkinImpostorNormalW(float3 packed, float3 ax, float3 az)
{
    const float3 n = packed * 2.0f - 1.0f;
    return normalize(ax * n.x + float3(0, 1, 0) * n.y + az * n.z);
}

#endif
