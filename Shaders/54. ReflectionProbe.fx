//=============================================================================
// 54. ReflectionProbe.fx — 반사 프로브 필터 (ReflectionProbes::FilterSlot)
//  원본 큐브 (찍은 것 / 구운 DDS / Custom) → 큐브 배열 한 칸의 면 하나 · 밉 하나
//  밉 0 = 그대로, 그 위 = GGX 중요도 샘플링 (거칠기 = 32 의 밉 공식의 역 — 셰이더가 같은 밉을 읽는다)
//  값은 감마 (엔진이 그린 색 그대로) → 선형으로 섞고 다시 감마로
//=============================================================================

TextureCube gProbeSource;

cbuffer cbProbeFilter
{
    float4 gFilter;   // x 면 (0..5 = +X -X +Y -Y +Z -Z), y 지각 거칠기, z 원본 한 변, w 이 밉의 한 변
};

SamplerState samProbe
{
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = CLAMP;
    AddressV = CLAMP;
    AddressW = CLAMP;
};

struct FilterVOut
{
    float4 PosH : SV_POSITION;
    float2 UV : TEXCOORD0;
};

// 화면 전체 삼각형 (정점 버퍼 없음)
FilterVOut VS_Filter(uint id : SV_VertexID)
{
    FilterVOut o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.PosH = float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    o.UV = uv;
    return o;
}

// 면 + 화면 UV (왼쪽 위 0,0) → 방향 (D3D 큐브 약속)
float3 FaceDirection(int face, float2 uv)
{
    float2 p = uv * 2.0f - 1.0f;
    float3 d;
    if (face == 0)      d = float3(1.0f, -p.y, -p.x);
    else if (face == 1) d = float3(-1.0f, -p.y, p.x);
    else if (face == 2) d = float3(p.x, 1.0f, p.y);
    else if (face == 3) d = float3(p.x, -1.0f, -p.y);
    else if (face == 4) d = float3(p.x, -p.y, 1.0f);
    else                d = float3(-p.x, -p.y, -1.0f);
    return normalize(d);
}

float3 FilterToLinear(float3 c) { return pow(max(c, 0.0f), 2.2f); }
float3 FilterToGamma(float3 c) { return pow(max(c, 0.0f), 1.0f / 2.2f); }

float2 Hammersley(uint i, uint n)
{
    uint b = i;
    b = (b << 16u) | (b >> 16u);
    b = ((b & 0x55555555u) << 1u) | ((b & 0xAAAAAAAAu) >> 1u);
    b = ((b & 0x33333333u) << 2u) | ((b & 0xCCCCCCCCu) >> 2u);
    b = ((b & 0x0F0F0F0Fu) << 4u) | ((b & 0xF0F0F0F0u) >> 4u);
    b = ((b & 0x00FF00FFu) << 8u) | ((b & 0xFF00FF00u) >> 8u);
    return float2((float)i / (float)n, (float)b * 2.3283064365386963e-10f);
}

static const uint kFilterSamples = 64;

float4 PS_Filter(FilterVOut pin) : SV_Target
{
    const float PI = 3.14159265f;
    float3 N = FaceDirection((int)gFilter.x, pin.UV);
    float perceptual = gFilter.y;
    float srcSize = max(gFilter.z, 1.0f);
    // 원본이 이 밉보다 크면 그만큼 원본 밉을 내려 읽는다 (계단 · 깜빡임 방지)
    float baseLod = max(log2(srcSize / max(gFilter.w, 1.0f)), 0.0f);
    if (perceptual <= 0.0f)
        return float4(gProbeSource.SampleLevel(samProbe, N, baseLod).rgb, 1.0f);

    float a = perceptual * perceptual;
    float a2 = a * a;
    float3 up = abs(N.y) < 0.999f ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 T = normalize(cross(up, N));
    float3 B = cross(N, T);
    // 픽셀 하나가 덮는 입체각 (원본 밉 0) — 표본의 pdf 로 읽을 밉을 정한다 (filtered importance sampling)
    float texelSolid = 4.0f * PI / (6.0f * srcSize * srcSize);
    float3 sum = 0.0f;
    float weight = 0.0f;
    [loop]
    for (uint i = 0; i < kFilterSamples; ++i)
    {
        float2 xi = Hammersley(i, kFilterSamples);
        float phi = 2.0f * PI * xi.x;
        float cosTheta = sqrt((1.0f - xi.y) / (1.0f + (a2 - 1.0f) * xi.y));
        float sinTheta = sqrt(1.0f - cosTheta * cosTheta);
        float3 H = T * (sinTheta * cos(phi)) + B * (sinTheta * sin(phi)) + N * cosTheta;
        float3 L = 2.0f * dot(N, H) * H - N;   // V = N
        float NoL = dot(N, L);
        if (NoL <= 0.0f)
            continue;
        float NoH = saturate(dot(N, H));
        float d = (NoH * NoH * (a2 - 1.0f) + 1.0f);
        float D = a2 / (PI * d * d);
        float pdf = D * 0.25f;   // N = V 이면 D · NoH / (4 VoH) = D / 4
        float sampleSolid = 1.0f / (kFilterSamples * pdf + 1e-5f);
        float lod = max(0.5f * log2(sampleSolid / texelSolid) + 1.0f, baseLod);
        sum += FilterToLinear(gProbeSource.SampleLevel(samProbe, L, lod).rgb) * NoL;
        weight += NoL;
    }
    return float4(FilterToGamma(sum / max(weight, 1e-4f)), 1.0f);
}

technique11 FilterTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Filter()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS_Filter()));
    }
}
