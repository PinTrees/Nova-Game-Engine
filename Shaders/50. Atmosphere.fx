//=============================================================================
// 50. Atmosphere.fx  (NOVA 대기·안개 화면 전체 패스 - AtmospherePass.cpp)
//  불투명 물체·하늘을 그린 뒤, 장면 색 사본 + 깊이에서 월드 위치를 되살려 대기 원근·높이 안개를 입힌다 (49. AtmosphereCommon.fx).
//  물은 자기 셰이더에서 같은 함수로 안개를 입힌다.
//=============================================================================
#include "49. AtmosphereCommon.fx"

cbuffer cbAtmospherePass
{
    float4x4 gAtmInvViewProj;
    float4 gAtmViewport;    // x, y = 왼쪽 위, z, w = 폭, 높이
};

Texture2D gAtmSceneColor;          // 장면 색 사본 (감마)
Texture2D<float> gAtmDepth;        // 장면 깊이 (0~1)

// ---- 볼륨 안개 (HDRP Fog 의 Volumetric Fog): 카메라 ~ 점 사이를 걸으며 해 그림자 맵 (캐스케이드) 으로 빛이 닿는 곳만 산란 —
//  나무 · 산 사이로 빛줄기 (갓 레이). 걸음마다 지터 (프레임마다 바뀌어 TAA 가 매끈하게), 밀도 = 높이 안개와 같은 식 × Multiplier
cbuffer cbAtmVolumetric
{
    float4x4 gAtmShadowT[8];   // 해 (방향광 0) 캐스케이드 변환 (월드 → 맵 uv · 깊이)
    float4 gAtmCascades[8];    // 캐스케이드 구 (xyz 중심, w 반지름²)
    float4 gAtmShadowInfo;     // x 캐스케이드 수 (0 = 그림자 없음 → 모두 빛), y 그림자 세기
    float4 gAtmVol;            // x 거리 (m), y Multiplier, z 걸음 수, w 프레임 (지터)
    float4 gAtmVolAlbedo;      // rgb 알베도 (선형), w 1 = 켜짐
};
Texture2DArray gAtmSunShadow;
SamplerComparisonState samAtmShadow
{
    Filter = COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    AddressU = BORDER;
    AddressV = BORDER;
    AddressW = BORDER;
    BorderColor = float4(1.0f, 1.0f, 1.0f, 1.0f);
    ComparisonFunc = LESS_EQUAL;
};

// 이 점에 해가 닿나 (1 = 빛). 가장 가까운 캐스케이드 하나 (32 의 SelectCascade 와 같은 고르기, 경계 섞기 없음 — 지터 · TAA 가 가린다)
float AtmSunVisible(float3 p)
{
    const int count = (int)gAtmShadowInfo.x;
    int cascade = -1;
    [unroll]
    for (int c = 7; c >= 0; --c)
    {
        const float3 d = p - gAtmCascades[c].xyz;
        if (c < count && dot(d, d) < gAtmCascades[c].w)
            cascade = c;
    }
    if (cascade < 0)
        return 1.0f;
    const float3 coord = mul(float4(p, 1.0f), gAtmShadowT[cascade]).xyz;
    if (coord.z > 1.0f)
        return 1.0f;
    const float s = gAtmSunShadow.SampleCmpLevelZero(samAtmShadow, float3(coord.xy, (float)cascade), coord.z).r;
    return lerp(1.0f, s, gAtmShadowInfo.y);
}

// 화면 픽셀 · 프레임마다 다른 0 ~ 1 (Interleaved Gradient Noise — 걸음 시작을 흔든다)
float AtmNoise(float2 pix, float frame)
{
    pix += 5.588238f * frame;
    return frac(52.9829189f * frac(dot(pix, float2(0.06711056f, 0.00583715f))));
}

// 카메라 → posW 방향 V, 거리 dist 까지의 해 빛 산란 (선형)
float3 AtmVolumetric(float3 V, float dist, float2 pix)
{
    const float len = min(dist, gAtmVol.x);
    const int steps = (int)gAtmVol.z;
    if (len <= 0.0f || steps <= 0)
        return 0.0f;
    const float ds = len / steps;
    const float jitter = AtmNoise(pix, gAtmVol.w);
    const float3 eye = gAtmEye.xyz;
    float transmittance = 1.0f;
    float scatter = 0.0f;
    [loop]
    for (int i = 0; i < steps; ++i)
    {
        const float t = (i + jitter) * ds;
        const float3 p = eye + V * t;
        const float sigma = gAtmFog.x * exp(-clamp((p.y - gAtmFog.y) * gAtmFog.z, -80.0f, 80.0f)) * gAtmVol.y;
        scatter += transmittance * sigma * AtmSunVisible(p) * ds;
        transmittance *= exp(-sigma * ds);
    }
    const float phase = AtmPhaseHG(dot(V, gAtmSunDir.xyz), gAtmFog2.w);
    return gAtmSunColor.rgb * gAtmVolAlbedo.rgb * phase * scatter * 0.25f;
}

struct AtmVSOut
{
    float4 PosH : SV_POSITION;
};

AtmVSOut AtmVS(uint vid : SV_VertexID)
{
    AtmVSOut o;
    const float2 uv = float2((vid << 1) & 2, vid & 2);
    o.PosH = float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return o;
}

float4 AtmPS(AtmVSOut pin) : SV_Target
{
    const int2 pix = int2(pin.PosH.xy);
    const float4 src = gAtmSceneColor.Load(int3(pix, 0));
    const float z = gAtmDepth.Load(int3(pix, 0));
    const bool sky = z >= 0.99999f;
    if (sky && src.a < 0.5f)
        return src;   // Scene 뷰의 빈 배경 (하늘 끔): 뒤의 그라디언트가 비치게 그대로
    const float2 ndc = float2((pin.PosH.x - gAtmViewport.x) / gAtmViewport.z * 2.0f - 1.0f, 1.0f - (pin.PosH.y - gAtmViewport.y) / gAtmViewport.w * 2.0f);
    float4 wp = mul(float4(ndc, sky ? 0.5f : z, 1.0f), gAtmInvViewProj);
    float3 posW = wp.xyz / wp.w;
    if (sky)
        posW = gAtmEye.xyz + normalize(posW - gAtmEye.xyz) * gAtmFog2.x;
    float3 c = AtmosphereApply(AtmToLinear(src.rgb), posW, sky);
    if (gAtmVolAlbedo.w > 0.5f)
    {
        const float3 d = posW - gAtmEye.xyz;
        const float dist = length(d);
        if (dist > 1e-3f)
            c += AtmVolumetric(d / dist, dist, pin.PosH.xy);
    }
    return float4(AtmToGamma(c), src.a);
}

DepthStencilState AtmNoDepth
{
    DepthEnable = FALSE;
    DepthWriteMask = ZERO;
};

RasterizerState AtmNoCull
{
    CullMode = None;
};

BlendState AtmNoBlend
{
    BlendEnable[0] = FALSE;
};

technique11 AtmosphereTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, AtmVS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, AtmPS()));
        SetDepthStencilState(AtmNoDepth, 0);
        SetRasterizerState(AtmNoCull);
        SetBlendState(AtmNoBlend, float4(0, 0, 0, 0), 0xFFFFFFFF);
    }
}
