//=============================================================================
// 41. PostProcess.fx
// Unity URP 의 후처리(Volume) 패스.
//  - BloomPrefilter / BloomDown / BloomUp : 밝은 부분을 뽑아 절반 해상도부터 밉 체인으로 흐리게 (URP Bloom 과 같은 구조)
//  - Uber : 색수차 → Bloom 합성 → 비네트 → 색 보정(노출, 화이트 밸런스, 대비, 컬러 필터, 색조, 채도) → 톤매핑 → 감마 → 필름 그레인 → 디더링
//  - Fxaa : 카메라 Anti-aliasing = FXAA
// 씬은 감마 공간 값으로 그려지므로 Uber 는 선형으로 바꿔 처리한 뒤 다시 감마로 돌린다.
// 화면 전체를 덮는 삼각형 하나를 SV_VertexID 로 만든다 (정점 버퍼 없음, Draw(3)).
//=============================================================================

cbuffer cbPost
{
    float4 gBloomParams;      // x = 임계값(선형), y = knee, z = clamp, w = scatter
    float4 gBloomTint;        // rgb = tint * intensity, w = 1 이면 Bloom 사용
    float4 gGrading;          // x = 2^노출, y = 대비(1 + c/100), z = 색조 이동(-0.5..0.5), w = 채도(1 + s/100)
    float4 gColorFilter;      // rgb (선형)
    float4 gWhiteBalance;     // rgb = LMS 계수, w = 1 이면 적용
    float4 gVignetteColor;    // rgb = 색, w = 세기 * 3
    float4 gVignetteParams;   // xy = 중심, z = 부드러움 * 5, w = 가로 보정(둥글게 = 화면 비율, 아니면 1)
    float4 gGrain;            // x = 세기, y = 응답, z = 시드, w = 색수차 세기 * 0.05
    float4 gFlags;            // x = 톤매핑(0 없음, 1 Neutral, 2 ACES), y = 디더링, z = Stop NaNs, w = 색 보정 사용
    float4 gExposure;         // x = 고정 배율 (2^Compensation), y = 1 이면 자동 노출 (gExposureTex 를 곱한다)
    float4 gAutoExposure;     // x = Middle Gray, y = 2^Limit Min, z = 2^Limit Max, w = 0 이면 바로 목표로 (첫 프레임)
    float4 gAdapt;            // x = dt × 밝아질 때 속도, y = dt × 어두워질 때 속도
};

Texture2D gSource;     // 입력 (씬 HDR 또는 이전 단계)
Texture2D gBloomLow;   // Bloom 올리기: 한 단계 작은 밉 (이미 합성된 것)
Texture2D gBloomTex;   // Uber: 최종 Bloom (절반 해상도)
Texture2D gLumTex;        // 자동 노출: 로그 휘도 (밉 끝 = 평균)
Texture2D gPrevExposure;  // 자동 노출: 지난 프레임 배율 (1x1)
Texture2D gExposureTex;   // 자동 노출: 이번 프레임 배율 (1x1)

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

// ---------------------------------------------------------------- 공통
float2 TexelSize(Texture2D tex)
{
    float w, h;
    tex.GetDimensions(w, h);
    return float2(1.0f / w, 1.0f / h);
}

float3 GammaToLinear(float3 c) { return pow(max(c, 0.0f), 2.2f); }
float3 LinearToGamma(float3 c) { return pow(max(c, 0.0f), 1.0f / 2.2f); }
float Max3(float3 c) { return max(c.r, max(c.g, c.b)); }
float Luminance(float3 c) { return dot(c, float3(0.2126729f, 0.7151522f, 0.0721750f)); }

float3 KillNaN(float3 c)
{
    // NaN 이나 무한대는 0 으로 (한 픽셀의 NaN 이 Bloom 으로 화면 전체에 번지는 것을 막는다)
    return (any(isnan(c)) || any(isinf(c))) ? float3(0.0f, 0.0f, 0.0f) : c;
}

float Hash(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031f);
    p3 += dot(p3, p3.yzx + 33.33f);
    return frac((p3.x + p3.y) * p3.z);
}

// 9-tap 텐트 필터 (Bloom 올리기)
float3 SampleTent(Texture2D tex, float2 uv, float2 texel)
{
    float4 d = texel.xyxy * float4(1.0f, 1.0f, -1.0f, 0.0f);
    float3 s;
    s  = tex.SampleLevel(samLinear, uv - d.xy, 0).rgb;
    s += tex.SampleLevel(samLinear, uv - d.wy, 0).rgb * 2.0f;
    s += tex.SampleLevel(samLinear, uv - d.zy, 0).rgb;
    s += tex.SampleLevel(samLinear, uv + d.zw, 0).rgb * 2.0f;
    s += tex.SampleLevel(samLinear, uv, 0).rgb * 4.0f;
    s += tex.SampleLevel(samLinear, uv + d.xw, 0).rgb * 2.0f;
    s += tex.SampleLevel(samLinear, uv + d.zy, 0).rgb;
    s += tex.SampleLevel(samLinear, uv + d.wy, 0).rgb * 2.0f;
    s += tex.SampleLevel(samLinear, uv + d.xy, 0).rgb;
    return s * (1.0f / 16.0f);
}

// ---------------------------------------------------------------- Bloom
float4 PS_BloomPrefilter(VertexOut pin) : SV_Target
{
    // 전체 해상도 → 절반: 4 탭 박스 (4x4 텍셀 평균)
    float2 t = TexelSize(gSource);
    float3 c = 0;
    c += KillNaN(gSource.SampleLevel(samLinear, pin.Tex + t * float2(-0.5f, -0.5f), 0).rgb);
    c += KillNaN(gSource.SampleLevel(samLinear, pin.Tex + t * float2(0.5f, -0.5f), 0).rgb);
    c += KillNaN(gSource.SampleLevel(samLinear, pin.Tex + t * float2(-0.5f, 0.5f), 0).rgb);
    c += KillNaN(gSource.SampleLevel(samLinear, pin.Tex + t * float2(0.5f, 0.5f), 0).rgb);
    c = GammaToLinear(c * 0.25f);
    c = min(c, gBloomParams.z);

    // URP 와 같은 부드러운 임계값 (knee)
    float threshold = gBloomParams.x;
    float knee = gBloomParams.y;
    float brightness = Max3(c);
    float softness = clamp(brightness - threshold + knee, 0.0f, 2.0f * knee);
    softness = (softness * softness) / (4.0f * knee + 1e-4f);
    float multiplier = max(brightness - threshold, softness) / max(brightness, 1e-4f);
    return float4(max(c * multiplier, 0.0f), 1.0f);
}

float4 PS_BloomDown(VertexOut pin) : SV_Target
{
    // 13 탭 다운샘플 (가장자리 깜빡임이 적다)
    float2 t = TexelSize(gSource);
    float2 uv = pin.Tex;
    float3 A = gSource.SampleLevel(samLinear, uv + t * float2(-1.0f, -1.0f), 0).rgb;
    float3 B = gSource.SampleLevel(samLinear, uv + t * float2(0.0f, -1.0f), 0).rgb;
    float3 C = gSource.SampleLevel(samLinear, uv + t * float2(1.0f, -1.0f), 0).rgb;
    float3 D = gSource.SampleLevel(samLinear, uv + t * float2(-0.5f, -0.5f), 0).rgb;
    float3 E = gSource.SampleLevel(samLinear, uv + t * float2(0.5f, -0.5f), 0).rgb;
    float3 F = gSource.SampleLevel(samLinear, uv + t * float2(-1.0f, 0.0f), 0).rgb;
    float3 G = gSource.SampleLevel(samLinear, uv, 0).rgb;
    float3 H = gSource.SampleLevel(samLinear, uv + t * float2(1.0f, 0.0f), 0).rgb;
    float3 I = gSource.SampleLevel(samLinear, uv + t * float2(-0.5f, 0.5f), 0).rgb;
    float3 J = gSource.SampleLevel(samLinear, uv + t * float2(0.5f, 0.5f), 0).rgb;
    float3 K = gSource.SampleLevel(samLinear, uv + t * float2(-1.0f, 1.0f), 0).rgb;
    float3 L = gSource.SampleLevel(samLinear, uv + t * float2(0.0f, 1.0f), 0).rgb;
    float3 M = gSource.SampleLevel(samLinear, uv + t * float2(1.0f, 1.0f), 0).rgb;
    float3 c = (D + E + I + J) * 0.125f;
    c += (A + B + G + F) * 0.03125f;
    c += (B + C + H + G) * 0.03125f;
    c += (F + G + L + K) * 0.03125f;
    c += (G + H + M + L) * 0.03125f;
    return float4(c, 1.0f);
}

float4 PS_BloomUp(VertexOut pin) : SV_Target
{
    // 같은 크기의 내린 밉(gSource) 과 한 단계 작은 합성 결과(gBloomLow)를 scatter 비율로 섞는다 (URP)
    float3 high = gSource.SampleLevel(samLinear, pin.Tex, 0).rgb;
    float3 low = SampleTent(gBloomLow, pin.Tex, TexelSize(gBloomLow));
    return float4(lerp(high, low, gBloomParams.w), 1.0f);
}

// ---------------------------------------------------------------- 색 보정
float3 RgbToHsv(float3 c)
{
    float4 K = float4(0.0f, -1.0f / 3.0f, 2.0f / 3.0f, -1.0f);
    float4 p = lerp(float4(c.bg, K.wz), float4(c.gb, K.xy), step(c.b, c.g));
    float4 q = lerp(float4(p.xyw, c.r), float4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-4f;
    return float3(abs(q.z + (q.w - q.y) / (6.0f * d + e)), d / (q.x + e), q.x);
}

float3 HsvToRgb(float3 c)
{
    float4 K = float4(1.0f, 2.0f / 3.0f, 1.0f / 3.0f, 3.0f);
    float3 p = abs(frac(c.xxx + K.xyz) * 6.0f - K.www);
    return c.z * lerp(K.xxx, saturate(p - K.xxx), c.y);
}

// 선형 sRGB ↔ LMS (CAT02, Unity 의 White Balance 와 같은 공간)
static const float3x3 LIN_2_LMS = {
    3.90405e-1f, 5.49941e-1f, 8.92632e-3f,
    7.08416e-2f, 9.63172e-1f, 1.35775e-3f,
    2.31082e-2f, 1.28021e-1f, 9.36245e-1f };
static const float3x3 LMS_2_LIN = {
    2.85847e+0f, -1.62879e+0f, -2.48910e-2f,
    -2.10182e-1f, 1.15820e+0f, 3.24281e-4f,
    -4.18120e-2f, -1.18169e-1f, 1.06867e+0f };

// Unity 의 Neutral 톤매핑
float3 NeutralCurve(float3 x, float a, float b, float c, float d, float e, float f)
{
    return ((x * (a * x + c * b) + d * e) / (x * (a * x + b) + d * f)) - e / f;
}
float3 NeutralTonemap(float3 x)
{
    const float a = 0.2f, b = 0.29f, c = 0.24f, d = 0.272f, e = 0.02f, f = 0.3f;
    const float whiteLevel = 5.3f;
    float3 whiteScale = 1.0f / NeutralCurve(whiteLevel, a, b, c, d, e, f);
    return NeutralCurve(x * whiteScale, a, b, c, d, e, f) * whiteScale;
}

// ACES (RRT + ODT 근사, Stephen Hill)
static const float3x3 ACESInputMat = {
    0.59719f, 0.35458f, 0.04823f,
    0.07600f, 0.90834f, 0.01566f,
    0.02840f, 0.13383f, 0.83777f };
static const float3x3 ACESOutputMat = {
    1.60475f, -0.53108f, -0.07367f,
    -0.10208f, 1.10813f, -0.00605f,
    -0.00327f, -0.07276f, 1.07602f };
float3 RRTAndODTFit(float3 v)
{
    float3 a = v * (v + 0.0245786f) - 0.000090537f;
    float3 b = v * (0.983729f * v + 0.4329510f) + 0.238081f;
    return a / b;
}
float3 AcesTonemap(float3 c)
{
    c = mul(ACESInputMat, c * 1.8f);   // 1.8 = 중간 회색(0.18)이 톤매핑 전후 비슷한 밝기가 되게
    c = RRTAndODTFit(c);
    return mul(ACESOutputMat, c);
}

float4 PS_Uber(VertexOut pin) : SV_Target
{
    float2 uv = pin.Tex;
    float4 src = gSource.SampleLevel(samLinear, uv, 0);

    // 색수차: 가장자리로 갈수록 R/G/B 를 다른 위치에서 읽는다
    if (gGrain.w > 0.0f)
    {
        float2 coords = 2.0f * uv - 1.0f;
        float2 end = uv - coords * dot(coords, coords) * gGrain.w;
        float2 delta = (end - uv) / 3.0f;
        src.r = gSource.SampleLevel(samLinear, uv, 0).r;
        src.g = gSource.SampleLevel(samLinear, uv + delta, 0).g;
        src.b = gSource.SampleLevel(samLinear, uv + delta * 2.0f, 0).b;
    }

    float3 c = src.rgb;
    if (gFlags.z > 0.5f)
        c = KillNaN(c);
    c = GammaToLinear(c);

    float bloomAlpha = 0.0f;
    if (gBloomTint.w > 0.5f)
    {
        float3 bloom = SampleTent(gBloomTex, uv, TexelSize(gBloomTex)) * gBloomTint.rgb;
        c += bloom;
        bloomAlpha = saturate(Max3(bloom));
    }

    // 노출 (Volume > Exposure): 고정 보정 × 자동 노출 배율
    c *= gExposure.x * (gExposure.y > 0.5f ? gExposureTex.Load(int3(0, 0, 0)).r : 1.0f);

    // 비네트
    if (gVignetteColor.w > 0.0f)
    {
        float2 dist = abs(uv - gVignetteParams.xy) * gVignetteColor.w;
        dist.x *= gVignetteParams.w;
        float vfactor = pow(saturate(1.0f - dot(dist, dist)), gVignetteParams.z);
        c *= lerp(gVignetteColor.rgb, 1.0f, vfactor);
    }

    if (gFlags.w > 0.5f)
    {
        c *= gGrading.x;                                   // Post Exposure (EV)
        if (gWhiteBalance.w > 0.5f)
        {
            float3 lms = mul(LIN_2_LMS, c);
            c = mul(LMS_2_LIN, lms * gWhiteBalance.rgb);
        }
        c = 0.18f * pow(max(c, 0.0f) / 0.18f, gGrading.y); // 대비 (중간 회색 기준, 로그 공간)
        c *= gColorFilter.rgb;
        if (gGrading.z != 0.0f)
        {
            float3 hsv = RgbToHsv(max(c, 0.0f));
            hsv.x = frac(hsv.x + gGrading.z + 1.0f);
            c = HsvToRgb(hsv);
        }
        float luma = Luminance(c);
        c = luma + gGrading.w * (c - luma);                // 채도
        c = max(c, 0.0f);
    }

    if (gFlags.x > 1.5f)
        c = AcesTonemap(c);
    else if (gFlags.x > 0.5f)
        c = NeutralTonemap(c);
    c = LinearToGamma(saturate(c));

    // 필름 그레인 (밝은 곳일수록 약하게 = Response)
    if (gGrain.x > 0.0f)
    {
        float2 p = pin.PosH.xy + gGrain.z * 97.0f;
        float grain = (Hash(p) + Hash(p * 1.37f + 11.0f) - 1.0f);
        float lum = 1.0f - sqrt(Luminance(saturate(c)));
        lum = lerp(1.0f, lum, gGrain.y);
        c += c * grain * gGrain.x * lum;
    }

    // 디더링: 8비트 출력의 띠(banding)를 줄인다
    if (gFlags.y > 0.5f)
        c += (Hash(pin.PosH.xy + gGrain.z) - 0.5f) / 255.0f;

    // Scene 뷰는 투명으로 지운 배경 위에 그리므로, Bloom 이 번진 곳도 보이게 알파를 올린다
    return float4(saturate(c), max(src.a, bloomAlpha));
}

// ---------------------------------------------------------------- FXAA
float FxaaLuma(float3 c) { return dot(c, float3(0.299f, 0.587f, 0.114f)); }

float4 PS_Fxaa(VertexOut pin) : SV_Target
{
    const float kReduceMin = 1.0f / 128.0f;
    const float kReduceMul = 1.0f / 8.0f;
    const float kSpanMax = 8.0f;

    float2 rcp = TexelSize(gSource);
    float2 uv = pin.Tex;
    float4 center = gSource.SampleLevel(samLinear, uv, 0);
    float3 rgbNW = gSource.SampleLevel(samLinear, uv + float2(-1.0f, -1.0f) * rcp, 0).rgb;
    float3 rgbNE = gSource.SampleLevel(samLinear, uv + float2(1.0f, -1.0f) * rcp, 0).rgb;
    float3 rgbSW = gSource.SampleLevel(samLinear, uv + float2(-1.0f, 1.0f) * rcp, 0).rgb;
    float3 rgbSE = gSource.SampleLevel(samLinear, uv + float2(1.0f, 1.0f) * rcp, 0).rgb;

    float lumaNW = FxaaLuma(rgbNW), lumaNE = FxaaLuma(rgbNE), lumaSW = FxaaLuma(rgbSW), lumaSE = FxaaLuma(rgbSE);
    float lumaM = FxaaLuma(center.rgb);
    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    float2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y = ((lumaNW + lumaSW) - (lumaNE + lumaSE));
    float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * (0.25f * kReduceMul), kReduceMin);
    float rcpDirMin = 1.0f / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = clamp(dir * rcpDirMin, -kSpanMax, kSpanMax) * rcp;

    float3 rgbA = 0.5f * (gSource.SampleLevel(samLinear, uv + dir * (1.0f / 3.0f - 0.5f), 0).rgb +
                          gSource.SampleLevel(samLinear, uv + dir * (2.0f / 3.0f - 0.5f), 0).rgb);
    float3 rgbB = rgbA * 0.5f + 0.25f * (gSource.SampleLevel(samLinear, uv + dir * -0.5f, 0).rgb +
                                         gSource.SampleLevel(samLinear, uv + dir * 0.5f, 0).rgb);
    float lumaB = FxaaLuma(rgbB);
    float3 result = (lumaB < lumaMin || lumaB > lumaMax) ? rgbA : rgbB;
    return float4(result, center.a);
}

// ---------------------------------------------------------------- 자동 노출
// 장면 → 작은 타깃(256²)의 로그 휘도. 밉을 만들면 마지막 밉 = 화면 평균 로그 휘도 (기하 평균 → 밝은 해 몇 픽셀에 휘둘리지 않음)
float4 PS_Luminance(VertexOut pin) : SV_Target
{
    const float2 t = TexelSize(gSource) * 0.5f;
    float3 c = GammaToLinear(gSource.SampleLevel(samLinear, pin.Tex + float2(-t.x, -t.y), 0).rgb)
             + GammaToLinear(gSource.SampleLevel(samLinear, pin.Tex + float2(t.x, -t.y), 0).rgb)
             + GammaToLinear(gSource.SampleLevel(samLinear, pin.Tex + float2(-t.x, t.y), 0).rgb)
             + GammaToLinear(gSource.SampleLevel(samLinear, pin.Tex + float2(t.x, t.y), 0).rgb);
    // 가운데를 조금 더 (중앙 가중 측광)
    const float2 d = pin.Tex - 0.5f;
    const float w = 1.0f - dot(d, d) * 1.2f;
    return float4(log2(max(Luminance(c * 0.25f), 1e-4f)) * w, w, 0.0f, 1.0f);
}

// 1x1: 목표 배율 = Middle Gray / 평균 휘도 (한계 안), 지난 배율에서 로그 공간으로 천천히 따라간다
float4 PS_Adapt(VertexOut pin) : SV_Target
{
    uint w, h, mips;
    gLumTex.GetDimensions(0, w, h, mips);
    const float2 avg = gLumTex.SampleLevel(samLinear, float2(0.5f, 0.5f), (float)mips - 1.0f).rg;
    const float avgLum = exp2(avg.x / max(avg.y, 1e-3f));
    const float target = clamp(gAutoExposure.x / max(avgLum, 1e-4f), gAutoExposure.y, gAutoExposure.z);
    const float prev = gPrevExposure.Load(int3(0, 0, 0)).r;
    if (gAutoExposure.w < 0.5f || !(prev > 0.0f))
        return float4(target, 0, 0, 1);
    const float lt = log2(target), lp = log2(prev);
    // 장면이 밝아짐 = 목표 배율이 작아짐 → Speed Dark To Light
    const float rate = lt < lp ? gAdapt.x : gAdapt.y;
    return float4(exp2(lerp(lp, lt, 1.0f - exp(-rate))), 0, 0, 1);
}

// 단순 복사 (후처리 없이 해상도만 맞출 때)
float4 PS_Copy(VertexOut pin) : SV_Target
{
    return gSource.SampleLevel(samLinear, pin.Tex, 0);
}

#define POST_TECH(NAME, PSFUNC) \
technique11 NAME \
{ \
    pass P0 \
    { \
        SetVertexShader(CompileShader(vs_5_0, VS())); \
        SetGeometryShader(NULL); \
        SetPixelShader(CompileShader(ps_5_0, PSFUNC())); \
    } \
}

POST_TECH(BloomPrefilterTech, PS_BloomPrefilter)
POST_TECH(BloomDownTech, PS_BloomDown)
POST_TECH(BloomUpTech, PS_BloomUp)
POST_TECH(UberTech, PS_Uber)
POST_TECH(FxaaTech, PS_Fxaa)
POST_TECH(CopyTech, PS_Copy)
POST_TECH(LuminanceTech, PS_Luminance)
POST_TECH(AdaptTech, PS_Adapt)
