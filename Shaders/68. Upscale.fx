//=============================================================================
// 68. Upscale.fx
// URP 의 Render Scale · Upscaling Filter: 배율 해상도로 그린 Game 카메라 화면 (후처리 끝난 LDR) 을 출력 크기로.
//  - BilinearTech  : 선형 보간 (줄일 때도)
//  - NearestTech   : 가장 가까운 픽셀 (정수 배율이면 Automatic 이 고른다)
//  - EasuTech      : AMD FidelityFX Super Resolution 1.0 의 EASU (경계 방향을 따르는 12 탭 Lanczos 근사, 링잉 막기)
//  - RcasTech      : FSR 1.0 의 RCAS (잘리지 않는 만큼만 날카롭게 — EASU 결과를 출력 크기 그대로)
// FSR 1.0 의 식은 ffx_fsr1.h (MIT, Copyright (c) 2021 Advanced Micro Devices) 를 32 비트 경로 그대로 옮겼다 —
//  gather 대신 Load 12 번 (OpenGL · Vulkan 변환에서도 같게), 근사 역수는 보통 나눗셈 (0 나누기 막음).
//=============================================================================

cbuffer cbUpscale
{
    float4 gSize;      // xy = 입력 (배율 해상도) 크기, zw = 출력 크기 (px)
    float4 gEasuCon;   // xy = 입력 / 출력 (출력 픽셀 → 입력 픽셀), zw = 0.5 × 입력 / 출력 − 0.5
    float4 gRcasCon;   // x = 날카로움 (선형, exp2(−stops))
};

Texture2D gSource;

SamplerState samLinear
{
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = CLAMP;
    AddressV = CLAMP;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float2 Tex : TEXCOORD;
};

VertexOut VS(uint id : SV_VertexID)
{
    VertexOut vout;
    vout.Tex = float2((id << 1) & 2, id & 2);
    vout.PosH = float4(vout.Tex * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return vout;
}

float4 PS_Bilinear(VertexOut pin) : SV_Target
{
    return float4(gSource.SampleLevel(samLinear, pin.PosH.xy / gSize.zw, 0).rgb, 1.0f);
}

float4 PS_Nearest(VertexOut pin) : SV_Target
{
    int2 p = int2(floor(pin.PosH.xy * gSize.xy / gSize.zw));
    p = clamp(p, int2(0, 0), int2(gSize.xy) - 1);
    return float4(gSource.Load(int3(p, 0)).rgb, 1.0f);
}

// ---------------------------------------------------------------- EASU
float3 Tap(int2 p)
{
    p = clamp(p, int2(0, 0), int2(gSize.xy) - 1);
    return gSource.Load(int3(p, 0)).rgb;
}

// 밝기 × 2 (FSR 의 가장 간단한 근사)
float Luma2(float3 c) { return c.b * 0.5f + (c.r * 0.5f + c.g); }

float SafeRcp(float x) { return 1.0f / max(x, 1e-30f); }

void EasuTap(inout float3 aC, inout float aW, float2 off, float2 dir, float2 len, float lob, float clp, float3 c)
{
    float2 v;
    v.x = off.x * dir.x + off.y * dir.y;
    v.y = off.x * (-dir.y) + off.y * dir.x;
    v *= len;
    float d2 = min(v.x * v.x + v.y * v.y, clp);
    // lanczos2 근사: (25/16 (2/5 x² − 1)² − (25/16 − 1)) × (lob x² − 1)²
    float wB = 0.4f * d2 - 1.0f;
    float wA = lob * d2 - 1.0f;
    wB *= wB;
    wA *= wA;
    wB = (25.0f / 16.0f) * wB - (25.0f / 16.0f - 1.0f);
    float w = wB * wA;
    aC += c * w;
    aW += w;
}

// 방향 · 길이 누적 (가운데 c, 위 a, 왼쪽 b, 오른쪽 d, 아래 e) — 쌍선형 무게 w
void EasuSet(inout float2 dir, inout float len, float w, float lA, float lB, float lC, float lD, float lE)
{
    float dc = lD - lC;
    float cb = lC - lB;
    float lenX = SafeRcp(max(abs(dc), abs(cb)));
    float dirX = lD - lB;
    dir.x += dirX * w;
    lenX = saturate(abs(dirX) * lenX);
    lenX *= lenX;
    len += lenX * w;
    float ec = lE - lC;
    float ca = lC - lA;
    float lenY = SafeRcp(max(abs(ec), abs(ca)));
    float dirY = lE - lA;
    dir.y += dirY * w;
    lenY = saturate(abs(dirY) * lenY);
    lenY *= lenY;
    len += lenY * w;
}

float4 PS_Easu(VertexOut pin) : SV_Target
{
    float2 ip = floor(pin.PosH.xy);
    float2 pp = ip * gEasuCon.xy + gEasuCon.zw;
    float2 fp = floor(pp);
    pp -= fp;
    int2 f0 = int2(fp);
    //    b c
    //  e f g h
    //  i j k l
    //    n o
    float3 b = Tap(f0 + int2(0, -1)), c = Tap(f0 + int2(1, -1));
    float3 e = Tap(f0 + int2(-1, 0)), f = Tap(f0), g = Tap(f0 + int2(1, 0)), h = Tap(f0 + int2(2, 0));
    float3 i = Tap(f0 + int2(-1, 1)), j = Tap(f0 + int2(0, 1)), k = Tap(f0 + int2(1, 1)), l = Tap(f0 + int2(2, 1));
    float3 n = Tap(f0 + int2(0, 2)), o = Tap(f0 + int2(1, 2));
    float bL = Luma2(b), cL = Luma2(c), eL = Luma2(e), fL = Luma2(f), gL = Luma2(g), hL = Luma2(h);
    float iL = Luma2(i), jL = Luma2(j), kL = Luma2(k), lL = Luma2(l), nL = Luma2(n), oL = Luma2(o);

    float2 dir = float2(0.0f, 0.0f);
    float len = 0.0f;
    EasuSet(dir, len, (1.0f - pp.x) * (1.0f - pp.y), bL, eL, fL, gL, jL);
    EasuSet(dir, len, pp.x * (1.0f - pp.y), cL, fL, gL, hL, kL);
    EasuSet(dir, len, (1.0f - pp.x) * pp.y, fL, iL, jL, kL, nL);
    EasuSet(dir, len, pp.x * pp.y, gL, jL, kL, lL, oL);

    float2 dir2 = dir * dir;
    float dirR = dir2.x + dir2.y;
    bool zro = dirR < (1.0f / 32768.0f);
    dirR = zro ? 1.0f : rsqrt(dirR);
    dir.x = zro ? 1.0f : dir.x;
    dir *= dirR;
    len = len * 0.5f;
    len *= len;
    float stretch = (dir.x * dir.x + dir.y * dir.y) * SafeRcp(max(abs(dir.x), abs(dir.y)));
    float2 len2 = float2(1.0f + (stretch - 1.0f) * len, 1.0f - 0.5f * len);
    float lob = 0.5f + ((1.0f / 4.0f - 0.04f) - 0.5f) * len;
    float clp = SafeRcp(lob);

    float3 min4 = min(min(f, g), min(j, k));
    float3 max4 = max(max(f, g), max(j, k));
    float3 aC = float3(0.0f, 0.0f, 0.0f);
    float aW = 0.0f;
    EasuTap(aC, aW, float2(0.0f, -1.0f) - pp, dir, len2, lob, clp, b);
    EasuTap(aC, aW, float2(1.0f, -1.0f) - pp, dir, len2, lob, clp, c);
    EasuTap(aC, aW, float2(-1.0f, 1.0f) - pp, dir, len2, lob, clp, i);
    EasuTap(aC, aW, float2(0.0f, 1.0f) - pp, dir, len2, lob, clp, j);
    EasuTap(aC, aW, float2(0.0f, 0.0f) - pp, dir, len2, lob, clp, f);
    EasuTap(aC, aW, float2(-1.0f, 0.0f) - pp, dir, len2, lob, clp, e);
    EasuTap(aC, aW, float2(1.0f, 1.0f) - pp, dir, len2, lob, clp, k);
    EasuTap(aC, aW, float2(2.0f, 1.0f) - pp, dir, len2, lob, clp, l);
    EasuTap(aC, aW, float2(2.0f, 0.0f) - pp, dir, len2, lob, clp, h);
    EasuTap(aC, aW, float2(1.0f, 0.0f) - pp, dir, len2, lob, clp, g);
    EasuTap(aC, aW, float2(1.0f, 2.0f) - pp, dir, len2, lob, clp, o);
    EasuTap(aC, aW, float2(0.0f, 2.0f) - pp, dir, len2, lob, clp, n);
    // 정규화 + 링잉 막기 (가까운 넷의 범위)
    float3 pix = min(max4, max(min4, aC / (abs(aW) > 1e-8f ? aW : 1.0f)));
    return float4(pix, 1.0f);
}

// ---------------------------------------------------------------- RCAS
#define FSR_RCAS_LIMIT (0.25f - (1.0f / 16.0f))

float3 TapOut(int2 p)
{
    p = clamp(p, int2(0, 0), int2(gSize.zw) - 1);
    return gSource.Load(int3(p, 0)).rgb;
}

float4 PS_Rcas(VertexOut pin) : SV_Target
{
    //    b
    //  d e f
    //    h
    int2 sp = int2(pin.PosH.xy);
    float3 b = TapOut(sp + int2(0, -1));
    float3 d = TapOut(sp + int2(-1, 0));
    float3 e = TapOut(sp);
    float3 f = TapOut(sp + int2(1, 0));
    float3 h = TapOut(sp + int2(0, 1));
    float3 mn4 = min(min(b, d), min(f, h));
    float3 mx4 = max(max(b, d), max(f, h));
    // 잘리지 않는 최대 음의 엽
    float3 hitMin = min(mn4, e) / max(4.0f * mx4, 1e-5f);
    float3 hitMax = (1.0f - max(mx4, e)) / min(4.0f * mn4 - 4.0f, -1e-5f);
    float3 lobeRGB = max(-hitMin, hitMax);
    float lobe = max(-FSR_RCAS_LIMIT, min(max(lobeRGB.r, max(lobeRGB.g, lobeRGB.b)), 0.0f)) * gRcasCon.x;
    float3 pix = (lobe * (b + d + h + f) + e) / (4.0f * lobe + 1.0f);
    return float4(pix, 1.0f);
}

#define UPSCALE_TECH(NAME, PSFUNC) \
technique11 NAME \
{ \
    pass P0 \
    { \
        SetVertexShader(CompileShader(vs_5_0, VS())); \
        SetGeometryShader(NULL); \
        SetPixelShader(CompileShader(ps_5_0, PSFUNC())); \
    } \
}

UPSCALE_TECH(BilinearTech, PS_Bilinear)
UPSCALE_TECH(NearestTech, PS_Nearest)
UPSCALE_TECH(EasuTech, PS_Easu)
UPSCALE_TECH(RcasTech, PS_Rcas)
