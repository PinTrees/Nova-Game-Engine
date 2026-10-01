//=============================================================================
// 49. AtmosphereCommon.fx  (NOVA 대기·안개 - 50. Atmosphere.fx(화면 전체 패스) / 46. Water.fx 에서 include)
//
// Volume 의 Fog / Atmosphere 오버라이드 (AtmospherePass.cpp 가 값을 채운다):
//  - 대기 원근 (Unreal Sky Atmosphere 의 Aerial Perspective): 카메라 ~ 점 사이 공기의 레일리(파장별)·미 산란.
//    밀도는 높이에 따라 지수로 줄고(척도 높이), 경로 평균 밀도는 해석적으로 적분한다.
//    투과 T = exp(-(βR·ρR + βM·ρM)·거리), 들어오는 빛 = (1 - T) × 지평선 하늘 색 + (1 - T_미) × 해 × 미 위상 (해 쪽이 밝게 번짐)
//  - 높이 안개 (Unreal Exponential Height Fog): 밀도 = d0 · exp(-(h - 기준 높이) / 척도 높이), 광선 적분도 해석적.
//    색 = 보는 방향의 흐린 하늘(Sky Color) 또는 상수 색 + 해 쪽 산란 (Henyey-Greenstein)
// 색은 선형 공간 (장면 버퍼는 감마라 호출자가 바꿔 넘긴다).
//=============================================================================

cbuffer cbAtmosphere
{
    float4 gAtmFog;        // x = 밀도(1/m, 기준 높이에서), y = 기준 높이(m), z = 1/척도 높이(1/m), w = 시작 거리(m)
    float4 gAtmFog2;       // x = 최대 거리(m, 하늘), y = 최대 불투명도, z = 해 산란 세기, w = 비등방 g
    float4 gAtmFogColor;   // rgb = 상수 색 또는 하늘 색 틴트 (선형), w = 1 이면 하늘 색
    float4 gAtmRayleigh;   // rgb = 레일리 산란 계수(1/m, 거리 배율 포함), w = 1/척도 높이
    float4 gAtmMie;        // x = 미 산란 계수(1/m, 거리 배율 포함), y = 1/척도 높이, z = 비등방 g, w = 해 세기
    float4 gAtmSunDir;     // xyz = 해 쪽 방향(빛이 오는 쪽, 단위)
    float4 gAtmSunColor;   // rgb = 해 색 (선형)
    float4 gAtmEye;        // xyz = 카메라 위치
    float4 gAtmFlags;      // x = 안개 사용, y = 대기 사용, z = 하늘 큐브맵 있음
};

TextureCube gAtmSky;

SamplerState samAtmSky
{
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = CLAMP;
    AddressV = CLAMP;
    AddressW = CLAMP;
};

static const float kAtmPi = 3.14159265f;

float3 AtmToLinear(float3 c) { return pow(max(c, 0.0f), 2.2f); }
float3 AtmToGamma(float3 c) { return pow(max(c, 0.0f), 1.0f / 2.2f); }

// Henyey-Greenstein (4π 로 정규화: 등방 = 1)
float AtmPhaseHG(float cosT, float g)
{
    const float g2 = g * g;
    return (1.0f - g2) / pow(max(1.0f + g2 - 2.0f * g * cosT, 1e-4f), 1.5f);
}

// 보는 방향의 흐린 하늘 색 (지평선 아래는 지평선 색). 큐브맵이 없으면 회청색
float3 AtmSkyColor(float3 V)
{
    if (gAtmFlags.z < 0.5f)
        return float3(0.45f, 0.55f, 0.7f);
    uint w, h, mips;
    gAtmSky.GetDimensions(0, w, h, mips);
    const float3 dir = normalize(float3(V.x, max(V.y, 0.03f), V.z));
    return AtmToLinear(gAtmSky.SampleLevel(samAtmSky, dir, max((float)mips - 3.0f, 0.0f)).rgb);
}

// 높이 h0 → h1 직선 경로의 exp(-h·k) 평균
float AtmAvgDensity(float h0, float h1, float k)
{
    const float dh = (h1 - h0) * k;
    const float e0 = exp(-clamp(h0 * k, -80.0f, 80.0f));
    return abs(dh) > 1e-3f ? e0 * (1.0f - exp(-clamp(dh, -80.0f, 80.0f))) / dh : e0;
}

// 선형 색에 대기 원근 + 높이 안개. sky = 깊이 없는 픽셀(하늘): 대기 원근은 하늘 그림에 이미 있으므로 높이 안개만
float3 AtmosphereApply(float3 color, float3 posW, bool sky)
{
    const float3 eye = gAtmEye.xyz;
    const float3 d = posW - eye;
    const float dist = length(d);
    if (dist < 1e-3f)
        return color;
    const float3 V = d / dist;
    const float cosT = dot(V, gAtmSunDir.xyz);
    const float3 skyCol = AtmSkyColor(V);

    // ---- 대기 원근
    if (gAtmFlags.y > 0.5f && !sky)
    {
        const float rhoR = AtmAvgDensity(eye.y, posW.y, gAtmRayleigh.w);
        const float rhoM = AtmAvgDensity(eye.y, posW.y, gAtmMie.y);
        const float3 tauR = gAtmRayleigh.rgb * rhoR * dist;
        const float tauM = gAtmMie.x * rhoM * dist;
        const float3 T = exp(-(tauR + tauM));
        const float TM = exp(-tauM);
        const float3 sunGlow = gAtmSunColor.rgb * gAtmMie.w * AtmPhaseHG(cosT, gAtmMie.z) * 0.08f;
        color = color * T + (1.0f - T) * skyCol * gAtmMie.w + (1.0f - TM) * sunGlow;
    }

    // ---- 높이 안개
    if (gAtmFlags.x > 0.5f)
    {
        const float start = gAtmFog.w;
        const float len = min(dist, gAtmFog2.x) - start;
        if (len > 0.0f)
        {
            const float3 p0 = eye + V * start;
            const float avg = AtmAvgDensity(p0.y - gAtmFog.y, p0.y - gAtmFog.y + V.y * len, gAtmFog.z);
            const float fog = min(1.0f - exp(-gAtmFog.x * avg * len), gAtmFog2.y);
            float3 fogCol = gAtmFogColor.w > 0.5f ? skyCol * gAtmFogColor.rgb : gAtmFogColor.rgb;
            fogCol += gAtmSunColor.rgb * gAtmFog2.z * AtmPhaseHG(cosT, gAtmFog2.w) * 0.06f;
            color = lerp(color, fogCol, fog);
        }
    }
    return color;
}
