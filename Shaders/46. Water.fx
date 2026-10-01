//=============================================================================
// 46. Water.fx  (바다 · 호수 · 강)
//
// 참고: Crest(카메라 LOD 격자 + 얕은 물 파도 감쇠 + 산란/SSS), KWS(흐름 노멀·코스틱), Unreal Water(Gerstner 생성기)
//  - 바다: 카메라를 따라가는 중첩 격자(레벨마다 칸 2 배). 경계 띠는 거친 격자 위치로 모핑. Gerstner 를 정점에서 변위,
//          픽셀에서 같은 파도의 법선·접힘(거품)을 화면 크기보다 작은 파도는 빼고 계산
//  - 호수/강: CPU 가 만든 월드 메시. 강은 흐름 방향으로 2 단계(위상) 노멀을 흘린다
//  - 색: 하드웨어 깊이로 물속 거리 → 흡수(빨강부터) + 산란, 굴절(흔들린 화면), 바닥 코스틱, 하늘 반사(프레넬), 해 반사, SSS
//  - 결과는 불투명으로 쓴다 (굴절 = 미리 복사한 화면 색). 깊이는 따로 한 번 더 그려 쓴다 (입자·격자가 물에 가려지게)
//  색 입출력은 엔진의 다른 셰이더처럼 감마 공간, 계산은 선형
//=============================================================================

#define MAX_WAVES 32
static const float PI = 3.14159265f;

cbuffer cbWaterFrame
{
    float4x4 gViewProj;
    float3 gEyePos;
    float gTime;
    float4 gProjParams;      // x = proj._33, y = proj._43 (뷰 z = y / (d - x))
    float4 gViewport;        // x, y, w, h (픽셀)
    float3 gSunDir;          // 해 쪽 (단위)
    float gSunIntensity;
    float3 gSunColor;        // 선형
    float gCubeMips;
    float3 gAmbient;         // 선형 (하늘 위쪽 평균)
    float gHasSky;
    float4x4 gInvViewProj;
    float gEyeUnder;         // 1 = 카메라가 이 물 아래 (수중)
    float gHasSunShadow;
    float2 gPad0;
    // 해(방향광 0) 캐스케이드 그림자 (32. InstancedBasic.fx 와 같은 방식)
    float4x4 gSunShadowTransforms[4];
    float4 gCascadeSpheres[4];
    float4 gShadowParams;    // x 캐스케이드 수, z 흐려지기 시작 거리, w 1/폭
    float4 gSunShadowData;   // x Strength, y 필터
};

cbuffer cbWaterBody
{
    int gBodyType;           // 0 바다, 1 호수, 2 강
    int gWaveCount;
    float gSurfaceY;
    float gBaseCell;         // 바다 격자 레벨 0 칸 (m)
    float4 gWaveA[MAX_WAVES];   // dirX, dirZ, k, 진폭
    float4 gWaveB[MAX_WAVES];   // omega, phase, Q, 0
    float3 gSigma;           // 흡수 계수 (1/m)
    float gTurbidity;
    float3 gScatter;         // 선형
    float gClarity;
    float3 gSSSColor;        // 선형
    float gSSS;
    float4 gNormalParams;    // 세기, 타일(m), 속도(m/s), 0
    float2 gWindDir;
    float gMaxAmp;
    float gHasShoreMap;
    float4 gFoamParams;      // 마루 거품, 물가 거품 폭(m), 타일(m), 0
    float4 gLightParams;     // 매끈함, 반사, 굴절, 0
    float4 gCausticParams;   // 세기, 깊이(m), 타일(m), 0
    float4 gShoreRect;       // 지형 높이 지도 범위: minX, minZ, 1/폭, 1/깊이
    float gHasOceanMask;
    float3 gPad1;
    float4 gShoreWave;       // 물가 파도: 높이(m), 마루 사이 수심(m), 위상 속도, 0
};

Texture2D gSceneColor;
Texture2D<float> gSceneDepth;
TextureCube gSky;
Texture2D gNormalA;
Texture2D gNormalB;
Texture2D gFoamTex;
Texture2D gCausticsTex;
Texture2D<float> gShoreMap;
Texture2D<float> gOceanMask;   // 바다: 1 = 열린 바다 쪽, 0 = 내륙 웅덩이 (바닷물 없음)
Texture2DArray gSunShadow;

SamplerComparisonState samShadow
{
    Filter = COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    AddressU = BORDER;
    AddressV = BORDER;
    AddressW = BORDER;
    BorderColor = float4(1.0f, 1.0f, 1.0f, 1.0f);
    ComparisonFunc = LESS_EQUAL;
};

SamplerState samWrap
{
    Filter = ANISOTROPIC;
    MaxAnisotropy = 8;
    AddressU = WRAP;
    AddressV = WRAP;
};

SamplerState samClamp
{
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = CLAMP;
    AddressV = CLAMP;
};

float3 ToLinear(float3 c) { return pow(max(c, 0.0f), 2.2f); }
float3 ToGamma(float3 c) { return pow(max(c, 0.0f), 1.0f / 2.2f); }

// 주변광: 하늘 큐브맵 위쪽의 흐린 밉 (없으면 CPU 값)
float3 SkyAmbient()
{
    float3 a = gAmbient;
    if (gHasSky > 0.5f)
        a = ToLinear(gSky.SampleLevel(samClamp, float3(0.0f, 1.0f, 0.0f), max(gCubeMips - 2.0f, 0.0f)).rgb) * 0.9f;
    return a;
}

// ---------------------------------------------------------------- 해 그림자 (1 = 빛, 0 = 그림자)
float SunShadowAt(float3 posW)
{
    if (gHasSunShadow < 0.5f)
        return 1.0f;
    const int count = (int)gShadowParams.x;
    int cascade = -1;
    [unroll] for (int c = 3; c >= 0; --c)
    {
        const float3 d = posW - gCascadeSpheres[c].xyz;
        if (c < count && dot(d, d) < gCascadeSpheres[c].w)
            cascade = c;
    }
    if (cascade < 0)
        return 1.0f;
    const float3 coord = mul(float4(posW, 1.0f), gSunShadowTransforms[cascade]).xyz;
    if (coord.z > 1.0f)
        return 1.0f;
    uint w, h, n;
    gSunShadow.GetDimensions(w, h, n);
    const float2 texel = 1.0f / float2(w, h);
    // 물은 늘 2x2 PCF + 주변 4 곳 (부드럽게, 필터 설정과 무관하게 싸게)
    float s = gSunShadow.SampleCmpLevelZero(samShadow, float3(coord.xy, cascade), coord.z).r * 2.0f;
    s += gSunShadow.SampleCmpLevelZero(samShadow, float3(coord.xy + float2(1.5f, 0.5f) * texel, cascade), coord.z).r;
    s += gSunShadow.SampleCmpLevelZero(samShadow, float3(coord.xy + float2(-1.5f, -0.5f) * texel, cascade), coord.z).r;
    s += gSunShadow.SampleCmpLevelZero(samShadow, float3(coord.xy + float2(0.5f, -1.5f) * texel, cascade), coord.z).r;
    s += gSunShadow.SampleCmpLevelZero(samShadow, float3(coord.xy + float2(-0.5f, 1.5f) * texel, cascade), coord.z).r;
    s /= 6.0f;
    const float fade = saturate((distance(posW, gEyePos) - gShadowParams.z) * gShadowParams.w);
    return lerp(1.0f, lerp(s, 1.0f, fade), gSunShadowData.x);
}

// ---------------------------------------------------------------- 파도
// 얕은 물: 파장의 1/4 보다 얕으면 줄어든다 (최소 15%)
float ShoreDepthAt(float2 xz)
{
    if (gHasShoreMap < 0.5f)
        return 1e4f;
    const float2 uv = (xz - gShoreRect.xy) * gShoreRect.zw;
    if (any(uv < 0.0f) || any(uv > 1.0f))
        return 1e4f;
    return gSurfaceY - gShoreMap.SampleLevel(samClamp, uv, 0);
}

// 물가 파도: 수심을 위상으로 → 마루가 수심 등고선과 나란히, 시간이 갈수록 얕은 쪽(해안)으로 밀려온다.
//  수심 8 m 부터 커지고 1 m 아래에서 부서져 사라진다. 노이즈로 위상을 흔들어 줄이 끊기게
float ShoreWaveHeight(float2 xz, float depth, out float crest)
{
    crest = 0.0f;
    if (gShoreWave.x <= 0.0f || depth >= 8.0f || depth <= 0.0f)
        return 0.0f;
    const float n = gFoamTex.SampleLevel(samWrap, xz / 90.0f, 0).r;
    const float phase = depth * (2.0f * PI / gShoreWave.y) + gTime * gShoreWave.z + n * 4.0f;
    const float s = 0.5f + 0.5f * sin(phase);
    crest = s * s * s * s * s * s;
    const float env = smoothstep(8.0f, 3.0f, depth) * smoothstep(0.0f, 1.2f, depth);
    crest *= env;
    return gShoreWave.x * crest;
}

float WaveAtten(float k, float depth)
{
    return lerp(0.15f, 1.0f, saturate(depth * k / 1.5f));
}

// 변위 (가로 + 높이). filterLen: 이보다 짧은 파장은 뺀다 (격자 칸보다 작은 파도 = 계단 무늬)
float3 GerstnerDisplace(float2 xz, float filterLen, float depth)
{
    float3 d = 0;
    [loop] for (int i = 0; i < gWaveCount; ++i)
    {
        const float4 a = gWaveA[i];
        const float4 b = gWaveB[i];
        const float len = 2.0f * PI / a.z;
        const float fade = saturate(len / filterLen - 1.0f) * WaveAtten(a.z, depth);
        if (fade <= 0.0f)
            continue;
        const float th = a.z * dot(a.xy, xz) - b.x * gTime + b.y;
        float s, c;
        sincos(th, s, c);
        const float amp = a.w * fade;
        d.xz += a.xy * (b.z * amp * c);
        d.y += amp * s;
    }
    return d;
}

// 법선 + 접힘(야코비안, 1 보다 작으면 마루가 겹쳐 하얗게 부서짐)
float3 GerstnerNormal(float2 xz, float filterLen, float depth, out float jacobian)
{
    float nx = 0, nz = 0, ny = 1;
    float jxx = 1, jzz = 1, jxz = 0;
    [loop] for (int i = 0; i < gWaveCount; ++i)
    {
        const float4 a = gWaveA[i];
        const float4 b = gWaveB[i];
        const float len = 2.0f * PI / a.z;
        const float fade = saturate(len / filterLen - 1.0f) * WaveAtten(a.z, depth);
        if (fade <= 0.0f)
            continue;
        const float th = a.z * dot(a.xy, xz) - b.x * gTime + b.y;
        float s, c;
        sincos(th, s, c);
        const float wa = a.z * a.w * fade;
        nx -= a.x * wa * c;
        nz -= a.y * wa * c;
        ny -= b.z * wa * s;
        const float qs = b.z * wa * s;
        jxx -= a.x * a.x * qs;
        jzz -= a.y * a.y * qs;
        jxz -= a.x * a.y * qs;
    }
    jacobian = jxx * jzz - jxz * jxz;
    return normalize(float3(nx, ny, nz));
}

// 바다 마스크: 내륙 웅덩이면 그리지 않는다
void ClipOcean(float2 xz)
{
    if (gHasOceanMask < 0.5f)
        return;
    const float2 uv = (xz - gShoreRect.xy) * gShoreRect.zw;
    if (any(uv < 0.0f) || any(uv > 1.0f))
        return;
    clip(gOceanMask.SampleLevel(samClamp, uv, 0) - 0.5f);
}

// ---------------------------------------------------------------- 정점
struct VSOut
{
    float4 PosH : SV_POSITION;
    float3 PosW : POSITION;
    float2 BaseXZ : TEXCOORD0;    // 변위 전 위치 (픽셀 파도 계산)
    float2 Flow : TEXCOORD1;      // 강: 흐름 (월드 m/s)
    float2 UV : TEXCOORD2;        // 강: x = 가로 0~1, y = 따라간 거리(m)
    float Edge : TEXCOORD3;       // 강: 가장자리 1
    float Height : TEXCOORD4;     // 파도 높이 / 최대 진폭 (-1~1)
    float Depth : TEXCOORD5;      // 바다: 지형까지 수심 (지도에서)
};

// 바다 격자 정점: x, z = 레벨 안 칸 좌표, w = 레벨
VSOut OceanVS(float3 v : POSITION)
{
    const float level = v.z;
    const float cell = gBaseCell * exp2(level);
    const float snap = cell * 2.0f;
    const float2 origin = floor(gEyePos.xz / snap) * snap;
    float2 coord = v.xy;
    // 바깥 띠(|좌표| 48~64)는 다음 레벨(칸 2 배) 위치로 모핑 → 레벨 경계에서 틈·계단이 줄어든다
    const float edge = max(abs(coord.x), abs(coord.y));
    const float morph = saturate((edge - 48.0f) / 14.0f);
    coord = lerp(coord, floor(coord * 0.5f + 0.5f) * 2.0f, morph);
    const float2 xz = origin + coord * cell;
    const float depth = ShoreDepthAt(xz);
    float3 d = GerstnerDisplace(xz, cell * 4.0f, depth);
    float crest;
    d.y += ShoreWaveHeight(xz, depth, crest);
    VSOut o;
    o.PosW = float3(xz.x + d.x, gSurfaceY + d.y, xz.y + d.z);
    o.PosH = mul(float4(o.PosW, 1.0f), gViewProj);
    o.BaseXZ = xz;
    o.Flow = 0;
    o.UV = 0;
    o.Edge = 0;
    o.Height = d.y / max(gMaxAmp, 0.01f);
    o.Depth = depth;
    return o;
}

struct SurfaceIn
{
    float3 Pos : POSITION;
    float2 Flow : TEXCOORD0;
    float2 UV : TEXCOORD1;
    float Edge : TEXCOORD2;
    float Rapids : TEXCOORD3;
};

// 호수·강: 월드 메시 그대로 (호수는 작은 파도라 변위 없이 픽셀 법선만)
VSOut SurfaceVS(SurfaceIn v)
{
    VSOut o;
    o.PosW = v.Pos;
    o.PosH = mul(float4(v.Pos, 1.0f), gViewProj);
    o.BaseXZ = v.Pos.xz;
    o.Flow = v.Flow;
    o.UV = v.UV;
    o.Edge = v.Edge;
    o.Height = v.Rapids;   // 강: Height 칸에 급류 정도
    o.Depth = 1e4f;
    return o;
}

// ---------------------------------------------------------------- 픽셀
float ViewZ(float d) { return gProjParams.y / (d - gProjParams.x); }

float3 DetailNormal(float2 xz, float dist)
{
    const float tiling = max(gNormalParams.y, 0.1f);
    const float2 w = gWindDir;
    const float2 perp = float2(-w.y, w.x);
    // 바람 방향으로 늘어놓은 두 장 (다른 크기·속도·방향)
    const float2 uvA = float2(dot(xz, w), dot(xz, perp)) / tiling - float2(gTime * gNormalParams.z / tiling, 0.0f);
    const float2 r = float2(w.x * 0.8f - w.y * 0.6f, w.x * 0.6f + w.y * 0.8f);
    const float2 uvB = float2(dot(xz, r), dot(xz, float2(-r.y, r.x))) / (tiling * 0.37f) - float2(gTime * gNormalParams.z * 1.3f / (tiling * 0.37f), 0.0f);
    float3 a = gNormalA.Sample(samWrap, uvA).xyz * 2.0f - 1.0f;
    float3 b = gNormalB.Sample(samWrap, uvB).xyz * 2.0f - 1.0f;
    // 텍스처 x 축 = 바람 방향 → 월드로
    a.xy = a.x * w + a.y * perp;
    b.xy = b.x * r + b.y * float2(-r.y, r.x);
    const float2 slope = (a.xy / max(a.z, 0.2f) + b.xy / max(b.z, 0.2f) * 0.6f);
    return float3(slope.x, 0.0f, slope.y) * gNormalParams.x * lerp(1.0f, 0.35f, saturate(dist / 400.0f));
}

// 강: 흐름을 따라 흐르는 노멀 (두 위상을 엇갈려 섞어 늘어나는 무늬를 숨긴다)
float3 FlowNormal(float2 xz, float2 flow, float dist, out float flowFoam, float2 foamUV)
{
    const float tiling = max(gNormalParams.y, 0.1f) * 0.6f;
    const float period = 2.0f;
    const float t = gTime / period;
    const float p0 = frac(t), p1 = frac(t + 0.5f);
    const float w0 = 1.0f - abs(1.0f - 2.0f * p0);
    const float2 off0 = flow * (p0 * period), off1 = flow * (p1 * period);
    const float3 a0 = gNormalA.Sample(samWrap, (xz - off0) / tiling).xyz * 2 - 1;
    const float3 a1 = gNormalA.Sample(samWrap, (xz - off1) / tiling + 0.5f).xyz * 2 - 1;
    const float3 b0 = gNormalB.Sample(samWrap, (xz - off0 * 1.2f) / (tiling * 0.4f)).xyz * 2 - 1;
    const float3 b1 = gNormalB.Sample(samWrap, (xz - off1 * 1.2f) / (tiling * 0.4f) + 0.5f).xyz * 2 - 1;
    const float3 a = lerp(a1, a0, w0), b = lerp(b1, b0, w0);
    const float speed = length(flow);
    const float2 slope = (a.xy / max(a.z, 0.2f) + b.xy / max(b.z, 0.2f) * 0.7f) * (0.6f + 0.5f * saturate(speed / 2.0f));
    const float f0 = gFoamTex.Sample(samWrap, (foamUV - off0) / gFoamParams.z).r;
    const float f1 = gFoamTex.Sample(samWrap, (foamUV - off1) / gFoamParams.z + 0.37f).r;
    flowFoam = lerp(f1, f0, w0);
    return float3(slope.x, 0.0f, slope.y) * gNormalParams.x * lerp(1.0f, 0.4f, saturate(dist / 300.0f));
}

// 아래에서 본 수면: 임계각(약 48.6°) 안쪽은 하늘·물 위가 굴절되어 보이고(스넬의 창), 바깥은 전반사로 물속 색
float4 UnderSurface(VSOut pin, float3 N, float3 V, float dist)
{
    N = -N;
    const float3 L = gSunDir;
    const float3 sun = gSunColor * gSunIntensity;
    const float3 ambient = SkyAmbient();
    const float cosI = saturate(dot(N, V));
    const float eta = 1.33f;
    const float sinT2 = eta * eta * (1.0f - cosI * cosI);
    const float3 waterCol = gScatter * (sun * saturate(L.y) * 0.6f + ambient);
    float3 c;
    if (sinT2 >= 1.0f)
        c = waterCol;   // 전반사
    else
    {
        const int2 pix = int2(clamp(pin.PosH.xy + N.xz * 30.0f, gViewport.xy, gViewport.xy + gViewport.zw - 1.0f));
        const float3 above = ToLinear(gSceneColor.Load(int3(pix, 0)).rgb);
        const float edge = smoothstep(0.75f, 1.0f, sinT2);   // 창 가장자리는 반사가 늘어난다
        c = lerp(above, waterCol, edge);
    }
    // 눈까지 물속 거리만큼 흡수·산란
    const float3 T = exp(-gSigma * dist);
    c = c * T + waterCol * (1.0f - T);
    return float4(ToGamma(c), 1.0f);
}

// 화면 공간 반사: 반사 방향으로 월드에서 점점 큰 걸음(1.15 배) → 화면에 투영해 장면 깊이 앞에서 뒤로 넘어가는 순간을 맞음으로,
// 이분 탐색 5 번으로 다듬는다. (두께로 거르면 낮은 각도에서 언덕 속으로 들어간 광선을 놓친다)
// 넘어간 곳의 깊이 차이가 걸음보다 훨씬 크면(앞 물체 뒤로 숨은 것) 그 앞 물체의 색은 쓰지 않는다. 화면 가장자리·먼 곳은 하늘로
bool TraceReflection(float3 P, float3 R, out float3 color, out float fade)
{
    color = 0;
    fade = 0;
    float stepLen = 0.5f;
    float t = 0.3f;
    float prevT = 0.0f;
    bool prevFront = true;
    [loop] for (int i = 0; i < 36; ++i)
    {
        const float3 q = P + R * t;
        const float4 c = mul(float4(q, 1.0f), gViewProj);
        if (c.w <= 0.05f)
            return false;
        const float2 ndc = c.xy / c.w;
        if (any(abs(ndc) > 1.0f))
            return false;
        const float2 pix = gViewport.xy + float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f) * gViewport.zw;
        const float d = gSceneDepth.Load(int3(pix, 0));
        const float diff = d < 0.99999f ? c.w - ViewZ(d) : -1.0f;
        const bool front = diff <= 0.0f;
        if (!front && prevFront)
        {
            {
                // 이분 탐색
                float a = prevT, b = t;
                float2 hitPix = pix;
                [unroll] for (int k = 0; k < 5; ++k)
                {
                    const float m = (a + b) * 0.5f;
                    const float4 cm = mul(float4(P + R * m, 1.0f), gViewProj);
                    const float2 nm = cm.xy / cm.w;
                    const float2 pm = gViewport.xy + float2(nm.x * 0.5f + 0.5f, 0.5f - nm.y * 0.5f) * gViewport.zw;
                    const float dm = gSceneDepth.Load(int3(pm, 0));
                    if (dm < 0.99999f && cm.w > ViewZ(dm))
                    {
                        b = m;
                        hitPix = pm;
                    }
                    else
                        a = m;
                }
                color = ToLinear(gSceneColor.Load(int3(hitPix, 0)).rgb);
                const float edge = saturate((1.0f - max(abs(ndc.x), abs(ndc.y))) * 8.0f);
                // 넘어간 깊이 차이가 걸음의 몇 배를 넘으면 가려진 뒤쪽 → 믿음을 줄인다
                const float trust = saturate(1.5f - diff / max(stepLen * 6.0f, 3.0f));
                fade = edge * trust * saturate(1.0f - t / 400.0f);
                return fade > 0.01f;
            }
        }
        prevFront = front;
        prevT = t;
        stepLen *= 1.15f;
        t += stepLen;
    }
    return false;
}

// 깊이만 쓰는 패스 (바다 마스크로 웅덩이는 빼고)
void DepthPS(VSOut pin)
{
    ClipOcean(pin.BaseXZ);
}

float4 WaterPS(VSOut pin) : SV_Target
{
    ClipOcean(pin.BaseXZ);
    const float3 toEye = gEyePos - pin.PosW;
    const float dist = length(toEye);
    const float3 V = toEye / max(dist, 1e-4f);
    const float footprint = max(length(fwidth(pin.BaseXZ)), 0.01f);

    // ---- 법선
    float jac = 1.0f;
    float3 N = float3(0, 1, 0);
    float flowFoam = 0.0f;
    float3 detail;
    const float rapids = gBodyType == 2 ? saturate(pin.Height) : 0.0f;
    if (gBodyType == 2)
        detail = FlowNormal(pin.BaseXZ, pin.Flow, dist, flowFoam, pin.BaseXZ) * (1.0f + 1.2f * rapids);   // 급류는 거칠게
    else
    {
        N = GerstnerNormal(pin.BaseXZ, footprint * 3.0f, pin.Depth, jac);
        detail = DetailNormal(pin.BaseXZ, dist);
    }
    // 물가 파도의 기울기 (바다)
    float shoreCrest = 0.0f;
    if (gBodyType == 0 && gShoreWave.x > 0.0f && pin.Depth < 8.0f)
    {
        const float h0 = ShoreWaveHeight(pin.BaseXZ, pin.Depth, shoreCrest);
        float cx, cz;
        const float hx = ShoreWaveHeight(pin.BaseXZ + float2(0.6f, 0.0f), ShoreDepthAt(pin.BaseXZ + float2(0.6f, 0.0f)), cx);
        const float hz = ShoreWaveHeight(pin.BaseXZ + float2(0.0f, 0.6f), ShoreDepthAt(pin.BaseXZ + float2(0.0f, 0.6f)), cz);
        N = normalize(N + float3(-(hx - h0) / 0.6f, 0.0f, -(hz - h0) / 0.6f));
    }
    N = normalize(N + detail);
    if (gEyeUnder > 0.5f && V.y < 0.0f)
        return UnderSurface(pin, N, V, dist);
    if (dot(N, V) < 0.02f)   // 비스듬히 볼 때 뒤집힌 법선 (검은 점) 막기
        N = normalize(N + V * (0.02f - dot(N, V)));

    // ---- 화면 깊이 → 물속 거리
    const int2 pix = int2(pin.PosH.xy);
    const float waterZ = pin.PosH.w;
    const float sceneZ0 = ViewZ(gSceneDepth.Load(int3(pix, 0)));
    // 굴절: 법선으로 흔든 위치 (물 앞에 있는 물체면 흔들지 않는다)
    const float2 maxPix = gViewport.xy + gViewport.zw - 1.0f;
    const float2 refrOff = N.xz * gLightParams.z * 60.0f * saturate((sceneZ0 - waterZ) / 4.0f) / (1.0f + waterZ * 0.02f);
    int2 rpix = int2(clamp(pin.PosH.xy + refrOff, gViewport.xy, maxPix));
    float sceneZ = ViewZ(gSceneDepth.Load(int3(rpix, 0)));
    if (sceneZ < waterZ + 0.05f)
    {
        rpix = pix;
        sceneZ = sceneZ0;
    }
    const bool sky = gSceneDepth.Load(int3(rpix, 0)) >= 0.99999f;
    const float3 rayDir = -V;
    const float viewThick = sky ? 1e4f : max(0.0f, (sceneZ - waterZ) * dist / waterZ);   // 시선을 따라 물속을 지난 거리
    const float3 floorW = pin.PosW + rayDir * viewThick;
    const float vDepth = sky ? 1e4f : max(0.0f, gSurfaceY + (pin.PosW.y - gSurfaceY) - floorW.y);   // 물 아래 깊이
    const float thick0 = sky ? 1e4f : max(0.0f, (sceneZ0 - waterZ) * dist / waterZ);

    // ---- 빛
    const float3 L = gSunDir;
    const float shadow = SunShadowAt(pin.PosW);
    const float3 sunFull = gSunColor * gSunIntensity;
    const float3 sun = sunFull * shadow;
    const float3 ambient = SkyAmbient();
    const float NdotL = saturate(dot(N, L));

    // ---- 물속 (굴절 + 흡수 + 산란 + 코스틱)
    float3 refr = ToLinear(gSceneColor.Load(int3(rpix, 0)).rgb);
    const float path = viewThick + min(vDepth, 50.0f);   // 빛이 들어와 바닥에 닿고 눈으로 오는 길 (근사)
    if (!sky && gCausticParams.x > 0.0f)
    {
        const float fadeC = saturate(1.0f - vDepth / max(gCausticParams.y, 0.1f)) * saturate(vDepth * 2.0f);
        if (fadeC > 0.0f)
        {
            const float2 cuv = (floorW.xz + L.xz * vDepth) / max(gCausticParams.z, 0.1f);
            const float c1 = gCausticsTex.Sample(samWrap, cuv + gTime * float2(0.031f, 0.017f)).r;
            const float c2 = gCausticsTex.Sample(samWrap, cuv * 1.27f + float2(0.43f, 0.11f) - gTime * float2(0.021f, 0.029f)).r;
            refr *= 1.0f + min(c1, c2) * 3.0f * gCausticParams.x * fadeC * saturate(L.y * 2.0f) * SunShadowAt(floorW);
        }
    }
    const float3 T = exp(-gSigma * path);
    // 물속으로 흩어지는 빛: 그늘이 진 물도 주변에서 흩어져 온 빛이 조금 있다
    const float3 lightIn = sunFull * saturate(L.y) * 0.6f * (0.35f + 0.65f * shadow) + ambient;
    const float3 scatter = gScatter * lightIn;
    float3 under = refr * T + scatter * (1.0f - T);
    // 탁도: 산란이 얕은 곳부터 바닥을 가린다
    const float murk = 1.0f - exp(-path * gTurbidity * 4.0f / max(gClarity, 0.1f));
    under = lerp(under, scatter, murk);
    if (sky)
        under = scatter;

    // SSS: 해를 등지고 볼 때 파도 마루로 비쳐 나오는 빛 (Crest 식)
    const float sssView = pow(saturate(dot(V, -L) * 0.5f + 0.5f), 4.0f);
    const float crest = saturate(pin.Height * 0.5f + 0.5f);
    under += gSSSColor * sun * gSSS * sssView * (0.2f + crest * crest) * saturate(1.0f - abs(N.y - 0.8f));

    // ---- 반사 (하늘 + 해)
    const float fresnel = 0.02f + 0.98f * pow(1.0f - saturate(dot(N, V)), 5.0f);
    const float3 R = reflect(-V, N);
    float3 refl = gHasSky > 0.5f ? ToLinear(gSky.SampleLevel(samClamp, float3(R.x, max(R.y, 0.02f), R.z), 0.0f).rgb) : ambient * 1.5f;
    {
        float3 ssr;
        float ssrFade;
        // 아래로 꺾인 반사(물결 법선)는 물속 바닥을 맞히므로 수평 위로
        const float3 Rt = normalize(float3(R.x, max(R.y, 0.03f), R.z));
        if (TraceReflection(pin.PosW + N * 0.05f, Rt, ssr, ssrFade))
            refl = lerp(refl, ssr, ssrFade);
    }
    refl *= gLightParams.y;
    const float smooth = gLightParams.x;
    const float specPow = exp2(4.0f + smooth * 9.0f);
    const float3 H = normalize(L + V);
    const float spec = pow(saturate(dot(N, H)), specPow) * (specPow + 8.0f) / (8.0f * PI) * smooth;
    float3 color = lerp(under, refl, fresnel) + sun * spec * fresnel * 4.0f * saturate(L.y * 4.0f);

    // ---- 거품: 파도 마루(접힘) + 물가 + 강 가장자리·물살
    const float foamTex = gFoamTex.Sample(samWrap, pin.BaseXZ / max(gFoamParams.z, 0.1f) + gWindDir * gTime * 0.02f).r;
    // 흰 물결: 마루가 많이 접힌 곳만 (야코비안 < 0.6), 거품 무늬로 잘게
    float foam = saturate((0.6f - jac) * 2.5f) * gFoamParams.x * 2.0f * smoothstep(0.2f, 0.7f, foamTex);
    if (!sky && gFoamParams.y > 0.0f)
    {
        const float band = saturate(1.0f - thick0 / gFoamParams.y);
        const float waves = 0.6f + 0.4f * sin(thick0 * 3.0f - gTime * 1.6f + foamTex * 3.0f);
        foam = max(foam, band * band * waves * smoothstep(0.25f, 0.6f, foamTex + band * 0.4f));
    }
    // 물가 파도가 부서지는 마루 (얕을수록 하얗게)
    foam = max(foam, shoreCrest * smoothstep(0.35f, 0.8f, foamTex + 0.2f) * smoothstep(2.5f, 0.9f, pin.Depth));
    if (gBodyType == 2)
    {
        const float rough = saturate(length(pin.Flow) / 3.0f);
        foam = max(foam, smoothstep(0.55f, 0.95f, flowFoam) * (0.25f + 0.6f * rough) * (0.4f + pin.Edge));
        // 급류: 흰 물살 (거품 무늬의 넓은 부분까지 하얗게)
        foam = max(foam, smoothstep(0.8f - 0.3f * rapids, 1.0f, flowFoam + rapids * 0.1f) * rapids);
    }
    const float3 foamCol = (sun * (NdotL * 0.8f + 0.2f) + ambient * 1.2f) * 0.9f;
    color = lerp(color, foamCol, saturate(foam));

    // ---- 물가: 아주 얕은 곳은 화면 색으로 부드럽게
    const float shore = sky ? 1.0f : saturate(thick0 / 0.35f);
    color = lerp(ToLinear(gSceneColor.Load(int3(pix, 0)).rgb), color, shore);
    return float4(ToGamma(color), 1.0f);
}

// ---------------------------------------------------------------- 수중 (카메라가 물 아래일 때 전체 화면)
struct FullOut
{
    float4 PosH : SV_POSITION;
};

FullOut FullVS(uint vid : SV_VertexID)
{
    FullOut o;
    const float2 uv = float2((vid << 1) & 2, vid & 2);
    o.PosH = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}

float4 UnderwaterPS(FullOut pin) : SV_Target
{
    const int2 pix = int2(pin.PosH.xy);
    const float d = gSceneDepth.Load(int3(pix, 0));
    const float2 ndc = float2((pin.PosH.x - gViewport.x) / gViewport.z * 2.0f - 1.0f, 1.0f - (pin.PosH.y - gViewport.y) / gViewport.w * 2.0f);
    const float4 farP = mul(float4(ndc, 1.0f, 1.0f), gInvViewProj);
    const float3 dir = normalize(farP.xyz / farP.w - gEyePos);
    float sceneDist = 1e4f;
    if (d < 0.99999f)
    {
        const float4 sp = mul(float4(ndc, d, 1.0f), gInvViewProj);
        sceneDist = distance(sp.xyz / sp.w, gEyePos);
    }
    // 수면까지 (위로 향할 때)
    const float toSurface = dir.y > 1e-4f ? (gSurfaceY - gEyePos.y) / dir.y : 1e4f;
    const float path = min(sceneDist, toSurface);
    const float3 L = gSunDir;
    const float3 sun = gSunColor * gSunIntensity;
    const float depthBelow = max(0.0f, gSurfaceY - gEyePos.y);
    // 깊을수록 어두워진다 (햇빛이 위에서 흡수됨)
    const float3 lightDown = exp(-gSigma * depthBelow * 0.7f);
    const float3 waterCol = gScatter * (sun * saturate(L.y) * 0.6f + SkyAmbient()) * lightDown;
    // 물속에서는 흡수에 산란 소광이 더해진다 (맑은 물도 수십 m 면 흐려진다)
    const float3 T = exp(-(gSigma * 1.6f + 2.0f / max(gClarity * 6.0f, 1.0f)) * path);
    const float murk = 1.0f - exp(-path * gTurbidity * 4.0f / max(gClarity, 0.1f));
    float3 c = ToLinear(gSceneColor.Load(int3(pix, 0)).rgb) * lightDown;
    // 코스틱이 물속 바닥에 일렁인다
    if (sceneDist < toSurface && gCausticParams.x > 0.0f)
    {
        const float3 floorW = gEyePos + dir * sceneDist;
        const float fd = gSurfaceY - floorW.y;
        const float2 cuv = (floorW.xz + L.xz * fd) / max(gCausticParams.z, 0.1f);
        const float c1 = gCausticsTex.Sample(samWrap, cuv + gTime * float2(0.031f, 0.017f)).r;
        const float c2 = gCausticsTex.Sample(samWrap, cuv * 1.27f + float2(0.43f, 0.11f) - gTime * float2(0.021f, 0.029f)).r;
        c *= 1.0f + min(c1, c2) * 3.0f * gCausticParams.x * saturate(1.0f - fd / max(gCausticParams.y * 2.0f, 0.1f)) * saturate(L.y * 2.0f) * SunShadowAt(floorW);
    }
    c = lerp(c * T + waterCol * (1.0f - T), waterCol, murk);
    return float4(ToGamma(c), 1.0f);
}

// ---------------------------------------------------------------- 물보라 (강 급류)
struct SprayIn
{
    float3 Pos : POSITION;
    float2 Corner : TEXCOORD0;
    float Seed : TEXCOORD1;
    float Size : TEXCOORD2;
};

struct SprayOut
{
    float4 PosH : SV_POSITION;
    float3 PosW : POSITION;
    float2 UV : TEXCOORD0;
    float Alpha : TEXCOORD1;
};

// 씨앗마다 다른 주기로: 물 위에서 솟아 커지며 흩어지고 사라진다 (카메라를 보는 사각형)
SprayOut SprayVS(SprayIn v)
{
    const float phase = frac(gTime * (0.45f + v.Seed * 0.35f) + v.Seed * 7.3f);
    float3 center = v.Pos;
    center.y += phase * v.Size * 1.1f + 0.15f;
    const float3 toEye = normalize(gEyePos - center);
    const float3 right = normalize(cross(float3(0, 1, 0), toEye));
    const float3 up = cross(toEye, right);
    const float size = v.Size * (0.5f + phase * 0.9f);
    SprayOut o;
    o.PosW = center + (right * v.Corner.x + up * v.Corner.y) * size;
    o.PosH = mul(float4(o.PosW, 1.0f), gViewProj);
    o.UV = v.Corner * 0.5f + 0.5f + v.Seed * 3.1f;
    o.Alpha = sin(phase * PI) * 0.55f;
    return o;
}

float4 SprayPS(SprayOut pin) : SV_Target
{
    const float2 local = (pin.UV - floor(pin.UV)) * 2.0f - 1.0f;
    const float r = length(local);
    const float soft = saturate(1.0f - r);
    const float tex = gFoamTex.Sample(samWrap, pin.UV * 0.5f).r;
    const float a = pin.Alpha * soft * soft * (0.4f + 0.6f * tex);
    clip(a - 0.01f);
    const float3 light = gSunColor * gSunIntensity * (0.5f + 0.5f * SunShadowAt(pin.PosW)) * 0.8f + SkyAmbient() * 1.3f;
    return float4(ToGamma(light), a);
}

// ---------------------------------------------------------------- 상태 / 기법
DepthStencilState WaterDepthTest
{
    DepthEnable = TRUE;
    DepthWriteMask = ZERO;
    DepthFunc = LESS_EQUAL;
};

DepthStencilState WaterDepthWrite
{
    DepthEnable = TRUE;
    DepthWriteMask = ALL;
    DepthFunc = LESS_EQUAL;
};

BlendState WaterOpaque
{
    BlendEnable[0] = FALSE;
    RenderTargetWriteMask[0] = 0x0F;
};

BlendState NoColor
{
    BlendEnable[0] = FALSE;
    RenderTargetWriteMask[0] = 0;
};

RasterizerState WaterRS
{
    CullMode = None;
};

BlendState SprayBlend
{
    BlendEnable[0] = TRUE;
    SrcBlend = SRC_ALPHA;
    DestBlend = INV_SRC_ALPHA;
    BlendOp = ADD;
    SrcBlendAlpha = ONE;
    DestBlendAlpha = INV_SRC_ALPHA;
    BlendOpAlpha = ADD;
    RenderTargetWriteMask[0] = 0x0F;
};

technique11 SprayTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, SprayVS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, SprayPS()));
        SetDepthStencilState(WaterDepthTest, 0);
        SetBlendState(SprayBlend, float4(0, 0, 0, 0), 0xFFFFFFFF);
        SetRasterizerState(WaterRS);
    }
}

technique11 OceanTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, OceanVS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, WaterPS()));
        SetDepthStencilState(WaterDepthTest, 0);
        SetBlendState(WaterOpaque, float4(0, 0, 0, 0), 0xFFFFFFFF);
        SetRasterizerState(WaterRS);
    }
}

technique11 SurfaceTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, SurfaceVS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, WaterPS()));
        SetDepthStencilState(WaterDepthTest, 0);
        SetBlendState(WaterOpaque, float4(0, 0, 0, 0), 0xFFFFFFFF);
        SetRasterizerState(WaterRS);
    }
}

DepthStencilState NoDepth
{
    DepthEnable = FALSE;
    DepthWriteMask = ZERO;
};

technique11 UnderwaterTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, FullVS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, UnderwaterPS()));
        SetDepthStencilState(NoDepth, 0);
        SetBlendState(WaterOpaque, float4(0, 0, 0, 0), 0xFFFFFFFF);
        SetRasterizerState(WaterRS);
    }
}

technique11 OceanDepthTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, OceanVS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, DepthPS()));
        SetDepthStencilState(WaterDepthWrite, 0);
        SetBlendState(NoColor, float4(0, 0, 0, 0), 0xFFFFFFFF);
        SetRasterizerState(WaterRS);
    }
}

technique11 SurfaceDepthTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, SurfaceVS()));
        SetGeometryShader(NULL);
        SetPixelShader(NULL);
        SetDepthStencilState(WaterDepthWrite, 0);
        SetBlendState(NoColor, float4(0, 0, 0, 0), 0xFFFFFFFF);
        SetRasterizerState(WaterRS);
    }
}
