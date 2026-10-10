#include "07. LightHelper.fx"

#define LIGHT_SIZE 4 // Light Size�� �� �������� ��������
#define LIGHT_MULTYPLE 6
#define LIGHT_MAX_SIZE (LIGHT_SIZE * LIGHT_MULTYPLE)
 
struct ShaderSetting
{
    int gUseTexture;
    int gAlphaClip;
    int gUseNormalMap;
    int gUseShadowMap;
    int gUseSsaoMap;
    int gReflectionEnabled;
    int gFogEnabled;
    
    // 28����Ʈ -> 32����Ʈ ������ ����
    int pad;
};

// Unity URP Lit 재질 (C++ PbrMaterial 과 같은 배치). 색은 감마 공간, EmissionColor 는 선형 HDR
struct PbrMaterial
{
    float4 BaseColor;
    float4 EmissionColor;
    float Metallic;
    float Smoothness;
    float NormalScale;
    float OcclusionStrength;
    float2 Tiling;
    float2 Offset;
    float Cutoff;
    int UseBaseMap;
    int UseMetallicMap;
    int UseNormalMap;
    int UseOcclusionMap;
    int UseEmissionMap;
    int SmoothnessFromAlbedo;
    int AlphaClip;
    int SpecularHighlights;
    int EnvironmentReflections;
    int ReceiveShadows;
    int Unlit;
    int UVMode;        // 0 메시 UV, 1 월드 좌표 (면 방향 투영 — 블록아웃 격자가 크기와 상관없이 1 m)
    int PadUV0;        // (int3 은 GLSL std140 에서 16 바이트 경계라 HLSL 배치 (오프셋 116) 와 맞지 않는다 — 정수 셋으로)
    int PadUV1;
    int PadUV2;
};

// 월드 좌표 UV: 법선의 가장 큰 축 방향으로 투영 (위 · 아래 = XZ, 옆 = ZY · XY). 1 m = 1. 글자가 거울처럼 뒤집히지 않게 면 방향으로 부호
float2 WorldBoxUV(float3 p, float3 n)
{
    float3 a = abs(n);
    if (a.y >= a.x && a.y >= a.z)
        return float2(p.x, n.y >= 0.0f ? -p.z : p.z);
    if (a.x >= a.z)
        return float2(n.x >= 0.0f ? -p.z : p.z, -p.y);
    return float2(n.z >= 0.0f ? p.x : -p.x, -p.y);
}

// Forward+ (클러스터 조명 — ClusteredLighting.cpp): 그림자 있는 앞의 빛 (위 배열) 밖의 점광 · 스포트광 · 입자 빛.
//  화면 16 x 9 타일 x 24 깊이 조각 (로그). 클러스터 표 (시작 | 개수 << 16) → 번호 목록 → 빛 텍스처 (빛마다 4 텍셀)
cbuffer cbCluster
{
    float4 gClusterParams;   // x 타일 가로, y 타일 세로, z 깊이 조각, w 빛 수 (0 = 없음)
    float4 gClusterDepth;    // 조각 = log(뷰 깊이) * x + y
    float4 gClusterView;     // xyz 카메라 앞 (월드) — 뷰 깊이 = dot(posW - 눈, 앞)
};
// 텍스처 하나 (OpenGL 샘플러 32 개 한도 — 무거운 셰이더가 넘지 않게): 텍셀 i → (i % 1024, i / 1024)
//  빛 = 0 부터 (빛마다 4: (0) 자리 · 범위 (1) 색 (감마) · 종류 (2) 방향 · cos 바깥 (3) cos 안 · 마스크 아래 16 · 위 16 비트)
//  클러스터 표 = 4096 부터 (x 시작, y 개수 — 클러스터 = 타일 x + 타일 y * 가로 + 조각 * 가로 * 세로), 번호 목록 = 8192 부터 (텍셀마다 4 개)
Texture2D gClusterData;
float4 ClusterTexel(uint i) { return gClusterData.Load(int3((int)(i & 1023u), (int)(i >> 10), 0)); }

cbuffer cbPerFrame
{
    DirectionalLight gDirLights[LIGHT_SIZE];
    PointLight gPointLights[LIGHT_SIZE];
    SpotLight gSpotLights[LIGHT_SIZE];
   
    int gDirLightCount;
    int gPointLightCount;
    int gSpotLightCount;
    
    // 그림자 변환 = LightV * LightP * toTexSpace. 방향광은 빛마다 캐스케이드 4개 (i * 4 + cascade)
    float4x4 gDirShadowTransforms[LIGHT_SIZE * 4];
    float4x4 gSpotShadowTransforms[LIGHT_SIZE];
    float4x4 gPointShadowTransforms[LIGHT_MAX_SIZE];
    
    float3 gEyePosW;

    // 캐스케이드 그림자 (Volume > Shadows): 구 = xyz 중심, w = 반지름²
    float4 gCascadeSpheres[4];
    float4 gShadowParams;                // x 캐스케이드 수, y Max Distance, z 흐려지기 시작 거리, w 1 / 흐려지는 폭
    float4 gDirShadowData[LIGHT_SIZE];   // x Strength (0 = 그림자 없음), y 필터 (0 Hard, 1 Low, 2 Medium, 3 High), z 1 / 맵 크기
    float4 gSpotShadowData[LIGHT_SIZE];
    float4 gPointShadowData[LIGHT_SIZE];

    float gFogStart;
    float gFogRange;
    float4 gFogColor;

    // 하늘 환경광 (Volume > Indirect Lighting): rgb = 확산 환경광 배율 × 틴트, w = 반사 배율
    float4 gIndirect = float4(1.0f, 1.0f, 1.0f, 1.0f);
    float4 gIndirectGI = float4(1.0f, 1.0f, 1.0f, 1.0f);   // Adaptive Probe Volume 빛의 배율 = Volume Indirect Lighting 만 (날씨 · 낮밤은 프로브가 모은 하늘에 이미)
    float4 gSsaoParams = float4(0.0f, 0.0f, 0.0f, 0.0f);   // Screen Space Ambient Occlusion: x = Direct Lighting Strength (직접광에 곱하는 몫)

    // Light.cullingMask: 빛마다 비추는 레이어 비트 (배열 순서 = gDirLights · gSpotLights · gPointLights). RenderLayers::SetLightMasks
    uint4 gDirLightMask = uint4(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF);
    uint4 gSpotLightMask = uint4(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF);
    uint4 gPointLightMask = uint4(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF);
};

cbuffer cbPerObject
{
    // Use when instancing is not used.
    float4x4 gWorld;
    float4x4 gWorldInvTranspose;
    float4x4 gWorldViewProj;
    float4x4 gWorldViewProjTex;
    
    float4x4 gView;
    float4x4 gProj;
    float4x4 gViewProj;
    float4x4 gViewProjTex;
    
    float4x4 gTexTransform;
    
    Material gMaterial;
    ShaderSetting gShaderSetting;
    PbrMaterial gPbr;   // URP Lit (메시 PS 가 쓰는 재질 값)
    uint gObjectLayer = 0xFFFFFFFF;   // 그리는 물체의 레이어 비트 (1 << layer). 정하지 않으면 모든 빛을 받는다
    float4 gLodFade = float4(0, 0, 0, 0);   // LOD Group 크로스페이드 (LodFadeClip)
    float4 gVTInfo0 = float4(0, 0, 0, 0);   // Virtual Texturing (UseBaseMap == 2): x 가상 폭, y 높이, z 밉 수, w 텍스처 번호
    float4 gVTInfo1 = float4(0, 0, 0, 0);   // x 캐시 폭 px, y 높이 px, z 타일 px (테두리 포함), w 테두리 px
};

// 이 빛이 이 물체를 비추나 (Light.cullingMask & 물체 레이어)
//  디퍼드 조명 패스 (PS_DeferredLight) 는 픽셀마다 G-버퍼의 레이어를 sDeferredLayer 에 둔다 (0 = 포워드 — 그리는 물체의 gObjectLayer)
static uint sDeferredLayer = 0u;
bool LightHits(uint mask) { return (mask & (sDeferredLayer != 0u ? sDeferredLayer : gObjectLayer)) != 0u; }

// LOD Group 크로스페이드 (gLodFade: x = 문턱, y = 1 이면 무늬 < x 인 픽셀만 / 0 이면 무늬 ≥ x, z = 1 켜짐)
//  화면 픽셀마다 고정 무늬 — 깊이 프리패스 (28) 와 본 패스 (32) 가 같은 픽셀을 남긴다
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

// frame
// 빛 종류마다 Texture2DArray 하나 (ShadowMap): 방향광 조각 = 빛 x 4 + 캐스케이드, 스포트광 = 빛, 점광 = 빛 x 6 + 큐브 면
//  (예전에는 빛마다 하나 = 12 장 — OpenGL 샘플러 32 개 한도에 걸려 지형 셰이더가 깨졌다)
Texture2DArray gDirShadowMaps;
Texture2DArray gSpotShadowMaps;
Texture2DArray gPointShadowMaps;
Texture2D gSsaoMap;
TextureCube gCubeMap;
// Adaptive Probe Volume (ProbeVolumes — 실시간): 확산 간접광 = 카메라 둘레 단계 (32 x 16 x 32 프로브) 의 L1 SH
//  아틀라스는 단계를 Z 로 쌓은 3D 텍스처. 물체 속 (유효하지 않은) 프로브는 이웃 값으로 채워 두고 (Dilate) 가중치를 낮춘다
Texture3D gGISH0;     // 빨강 (L0, L1x, L1y, L1z)
Texture3D gGISH1;     // 초록
Texture3D gGISH2;     // 파랑
Texture3D gGIValid;   // x 유효도
Texture3D gGIRadiance; // 복셀 빛 아틀라스 (a = 차 있음) — 프로브 갱신 (55) 이 쓴다
// 복셀 속 면 평면 6 칸 (노멀의 가장 큰 축 x 부호: 0 +X, 1 -X, 2 +Y, 3 -Y, 4 +Z, 5 -Z): rg 노멀 (팔면체), b 복셀 가운데에서 면까지 (노멀 방향), a 있음
//  — 방 모서리 (두 벽) · 얇은 벽의 두 면 · 바닥과 벽이 한 복셀에 있어도 따로 둔다. 칸은 Z 로 쌓는다: 칸 s · 단계 c 의 z = (s × 단계 수 + c) × 64 + z
Texture3D gGIPlanes;
cbuffer cbProbeVolume
{
    float4 gGIParams;      // x 단계 수 (0 = 없음 → 하늘), y Intensity, z 1 = 알베도 찍기 (빛 없이 표면 색만), w 1 = Local 상자 안만
    float4 gGIBias;        // x Normal Bias, y View Bias (미터), z -, w 아틀라스의 단계 수
    float4 gGICascade[4];  // xyz 볼륨 최소 모서리, w 프로브 간격
    float4 gGIVoxAll[4];   // 단계마다 복셀 볼륨 (최소 모서리, 복셀 크기) — w 0 = 아직 없음
    float4 gGILocalMin;
    float4 gGILocalMax;
};
SamplerState samGI
{
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = CLAMP;
    AddressV = CLAMP;
    AddressW = CLAMP;
};
TextureCubeArray gProbeCubes;   // Reflection Probe — 칸마다 GGX 로 필터한 밉 (ReflectionProbes)
cbuffer cbReflectionProbes
{
    float4 gProbeParams;       // x 이 뷰의 프로브 수 (0 = 하늘만), y 배열의 밉 수
    float4 gProbeData[24];     // 프로브마다 3 개: 상자 최소 + Blend Distance, 상자 최대 + Intensity, 찍은 점 + (칸 × 2 + Box Projection)
};
// Screen Space Reflection (ScreenSpaceReflection::Prepare · Bind): 깊이 프리패스 + 지난 프레임 장면 색
Texture2D gSsrNormalDepth;   // 뷰 노멀 xyz + 뷰 깊이 w (하늘 1e5)
Texture2D gSsrHistory;       // 지난 프레임 장면 색 (감마, 밉)
cbuffer cbScreenSpaceReflection
{
    float4x4 gSsrView;
    float4x4 gSsrProj;
    float4x4 gSsrPrevViewProj;   // 지난 프레임 장면 색을 그린 카메라
    float4 gSsrParams = float4(0, 0, 0, 0);    // x 켜짐, y 걸음 수, z 최대 거리 (m), w Object Thickness (깊이 비율)
    float4 gSsrParams2;   // x Minimum Smoothness, y Smoothness Fade Start, z Screen Edge Fade Distance, w 장면 색 밉 수
    float4 gSsrSize;      // 깊이 프리패스 (w, h, 1/w, 1/h)
};
SamplerState samSsr
{
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = CLAMP;
    AddressV = CLAMP;
};

// object
Texture2D gDiffuseMap;     // Base Map (가상 텍스처면 작은 대체 — 그림자 · 깊이 패스가 쓴다)
Texture2D gVTPageTable;    // Virtual Texturing: 페이지 표 (밉마다 — 칸 x, y, 올라 있는 밉), VirtualTexturing.cpp
Texture2D gVTCache;        // 모든 가상 텍스처가 함께 쓰는 물리 캐시 (136 x 136 타일)
Texture2D gNormalMap;
Texture2D gMetallicMap;    // R = Metallic, A = Smoothness (Unity 와 같음)
Texture2D gOcclusionMap;   // G = Occlusion
Texture2D gEmissionMap;

// ---- 날씨 (com.nova.weather — WeatherCover.cpp 가 넣는다. 넣지 않은 이펙트는 0 = 끔)
cbuffer cbWeather
{
    float4 gWeatherSurface;       // x 젖음, y 웅덩이, z 빗방울 물결, w 시간 (초)
    float4 gWeatherCoverParams;   // x 덮개 맵 있음, y 1 / 맵 크기, z 깊이 바이어스, w 미사용
    float4x4 gWeatherCoverVP;     // 월드 → 덮개 맵 (u, v, 깊이)
    float4 gWeatherSky;           // 먹구름 하늘 (하늘에서 온 환경광 · 반사): rgb = 1 - 색 배율, w = 채도 빼기 (0 = 그대로)
    float4 gWeatherSnow;          // x 눈 덮임, y 눈 깊이 (m), z 발자국 맵 있음, w 미사용
    float4 gWeatherSnowWin;       // 발자국 맵 창: xy 첫 칸의 월드 xz, z 한 변 (m), w 칸 수
};
Texture2D gWeatherCover;          // 위에서 본 깊이 (R24)
Texture2D gSnowDeform;            // 눈 발자국 (59. WeatherSnow.fx — 고리 배치, 1 = 바닥까지 눌림)
static float s_WeatherPuddles = 1.0f;   // 웅덩이 양 배율: 메시 1, 지형 0.45, 나무 · 풀 · 바위 0 (잎 · 바위 위에 웅덩이가 생기지 않게)
static bool s_SnowDisplaced = false;    // 쌓인 눈을 정점이 실제로 올렸다 (지형 테셀레이션) — 발자국 시차를 건너뛴다 (모양이 이미 파였다)

SamplerState samLinear
{
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = WRAP;
    AddressV = WRAP;
};

SamplerState samVTCache
{
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = CLAMP;
    AddressV = CLAMP;
};

// Streaming Virtual Texturing (docs/VIRTUAL_TEXTURING.md): 원하는 밉의 페이지 → 페이지 표 (올라 있는 가장 가까운 밉의 캐시 칸) → 캐시에서 쌍선형.
//  밉 = 화면 미분 (65. VirtualTexture.fx 의 피드백과 같은 식). 밉 사이를 섞지 않는다 (밉이 바뀌는 곳에 경계가 보일 수 있다)
float4 SampleVirtual(float2 uv)
{
    float2 texel = uv * gVTInfo0.xy;
    float2 dx = ddx(texel), dy = ddy(texel);
    float mip = 0.5f * log2(max(max(dot(dx, dx), dot(dy, dy)), 1e-8f));
    float m = clamp(floor(mip), 0.0f, gVTInfo0.z - 1.0f);
    float2 pages = max(floor(gVTInfo0.xy / 128.0f / exp2(m)), 1.0f);
    float2 wrapped = frac(uv);
    int2 page = int2(min(floor(wrapped * pages), pages - 1.0f));
    float4 e = gVTPageTable.Load(int3(page, (int)m)) * 255.0f;
    float2 rpages = max(floor(gVTInfo0.xy / 128.0f / exp2(round(e.z))), 1.0f);   // 실제로 올라 있는 밉의 페이지 수
    float2 inPage = frac(wrapped * rpages);
    float2 px = round(e.xy) * gVTInfo1.z + gVTInfo1.w + inPage * 128.0f;
    float4 cached = gVTCache.SampleLevel(samVTCache, px / gVTInfo1.xy, 0);
    float4 fallback = gDiffuseMap.Sample(samLinear, uv);   // 아직 아무 페이지도 없다 (w = 0) — 대체
    return e.w < 0.5f ? fallback : cached;
}

SamplerComparisonState samShadow
{
    Filter = COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    AddressU = BORDER;
    AddressV = BORDER;
    AddressW = BORDER;
    BorderColor = float4(1.0f, 1.0f, 1.0f, 1.0f);   // 그림자 맵 밖 = 가장 먼 깊이 = 빛을 받음

    ComparisonFunc = LESS_EQUAL;
};

// ---------------------------------------------------------------------------
// 그림자 (Unity URP 방식)
//  - 방향광: 카메라 절두체를 거리별 구(캐스케이드)로 나눠 가까운 곳일수록 촘촘한 맵. 픽셀이 들어 있는 첫 구를 쓴다
//  - 필터: Hard = 비교 샘플 1 번(2x2 선형), Soft = Low 4 / Medium 9 / High 16 번 (텐트 모양으로 부드럽게)
//  - Strength 로 그림자 농도, Max Distance 끝의 Last Border 구간에서 서서히 사라진다
// ---------------------------------------------------------------------------
// 비교 샘플 PCF. filter 0 = Hard(1 번, 2x2 선형), 1 Low(2x2), 2 Medium(3x3), 3 High(4x4) — 동적 루프라 코드는 한 벌
//  texelSize = 1 / 맵 크기 (C++ 가 gXxxShadowData.z 로 넘긴다). GetDimensions 를 쓰지 않는다:
//  OpenGL 에서는 크기 묻기용 샘플러가 따로 생겨 그림자 맵 12 장 × 2 가 샘플러 32 개 한도를 넘었다 (지형 PS)
float ShadowPCF(Texture2DArray map, float3 coord, float slice, int filter, float texelSize)
{
    float lit = 1.0f;   // 빛의 먼 평면 밖이면 빛
    if (coord.z <= 1.0f)
    {
        const float2 texel = float2(texelSize, texelSize);
        const int size = filter <= 0 ? 1 : filter + 1;
        const float center = (size - 1) * 0.5f;
        float sum = 0.0f;
        [loop]
        for (int y = 0; y < size; ++y)
        {
            [loop]
            for (int x = 0; x < size; ++x)
                sum += map.SampleCmpLevelZero(samShadow, float3(coord.xy + (float2(x, y) - center) * texel, slice), coord.z).r;
        }
        lit = sum / (float)(size * size);
    }
    return lit;
}

// 이 위치가 속한 캐스케이드 (없으면 -1 = Max Distance 밖)
int SelectCascade(float3 posW)
{
    const int count = (int)gShadowParams.x;
    int cascade = -1;
    [unroll]
    for (int c = 3; c >= 0; --c)
    {
        const float3 d = posW - gCascadeSpheres[c].xyz;
        if (c < count && dot(d, d) < gCascadeSpheres[c].w)
            cascade = c;   // 뒤에서부터 → 마지막으로 남는 값 = 가장 가까운 캐스케이드
    }
    return cascade;
}

// 캐스케이드 경계 섞기: 캐스케이드 구의 바깥 kCascadeBlend(반지름 비율) 구간에서 다음 캐스케이드와 부드럽게 섞는다.
// 예전에는 경계에서 그림자 선명도(텍셀 크기)가 한 번에 바뀌어 줄이 보였다 (숲·지형). 0 = 이 캐스케이드만, 1 = 다음 것만
static const float kCascadeBlend = 0.12f;
float CascadeBlend(float3 posW, int cascade)
{
    const int count = (int)gShadowParams.x;
    if (cascade < 0 || cascade + 1 >= count)
        return 0.0f;
    const float3 d = posW - gCascadeSpheres[cascade].xyz;
    const float t = sqrt(dot(d, d) / gCascadeSpheres[cascade].w);   // 0 = 가운데, 1 = 구 끝
    const float3 dn = posW - gCascadeSpheres[cascade + 1].xyz;
    if (dot(dn, dn) >= gCascadeSpheres[cascade + 1].w)
        return 0.0f;   // 다음 캐스케이드 구 밖 (그 맵에 이 위치가 없다)
    return saturate((t - (1.0f - kCascadeBlend)) / kCascadeBlend);
}

// Max Distance 끝(Last Border 구간)에서 그림자가 사라지는 정도 (0 = 그대로, 1 = 없음)
float ShadowFade(float3 posW)
{
    return saturate((distance(posW, gEyePosW) - gShadowParams.z) * gShadowParams.w);
}

// 방향광 i 의 그림자 (1 = 빛, 0 = 그림자). 캐스케이드마다 배열 조각 하나
float DirShadow(Texture2DArray map, int i, float3 posW, int cascade, float fade, float blend)
{
    const float4 data = gDirShadowData[i];
    float lit = 1.0f;
    if (data.x > 0.0f && cascade >= 0)
    {
        const float3 coord = mul(float4(posW, 1.0f), gDirShadowTransforms[i * 4 + cascade]).xyz;
        float s = ShadowPCF(map, coord, (float)(i * 4 + cascade), (int)data.y, data.z);
        [branch]
        if (blend > 0.0f)   // 경계 구간만 다음 캐스케이드를 한 번 더 읽는다
        {
            const float3 coord2 = mul(float4(posW, 1.0f), gDirShadowTransforms[i * 4 + cascade + 1]).xyz;
            s = lerp(s, ShadowPCF(map, coord2, (float)(i * 4 + cascade + 1), (int)data.y, data.z), blend);
        }
        lit = lerp(1.0f, lerp(s, 1.0f, fade), data.x);
    }
    return lit;
}

// 원근 맵 하나를 읽는 공통 부분 (스포트광 / 점광의 한 면)
float PerspectiveShadow(Texture2DArray map, float4x4 transform, float slice, float4 data, float3 posW)
{
    float lit = 1.0f;
    float4 p = mul(float4(posW, 1.0f), transform);
    if (data.x > 0.0f && p.w > 0.0f)
        lit = lerp(1.0f, ShadowPCF(map, p.xyz / p.w, slice, (int)data.y, data.z), data.x);
    return lit;
}

float SpotShadow(Texture2DArray map, int i, float3 posW)
{
    return PerspectiveShadow(map, gSpotShadowTransforms[i], (float)i, gSpotShadowData[i], posW);
}

// 점광 = 큐브 6 면 (C++ 순서: +X, -X, +Y, -Y, +Z, -Z = 배열 조각). 광원에서 본 방향의 가장 큰 축으로 면을 고른다
int PointFace(float3 v)
{
    const float3 a = abs(v);
    int face = v.z >= 0.0f ? 4 : 5;
    if (a.x >= a.y && a.x >= a.z)
        face = v.x >= 0.0f ? 0 : 1;
    else if (a.y >= a.z)
        face = v.y >= 0.0f ? 2 : 3;
    return face;
}

float PointShadow(Texture2DArray map, int i, float3 posW)
{
    const int face = PointFace(posW - gPointLights[i].Position);
    return PerspectiveShadow(map, gPointShadowTransforms[i * 6 + face], (float)(i * 6 + face), gPointShadowData[i], posW);
}

struct VertexIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 Tex : TEXCOORD;
    float4 TangentL : TANGENT;
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

// MeshBatcher 의 본 패스 (BatchTech): 인스턴스 = 월드 행렬 + MaterialPropertyBlock 의 값 (GPU 인스턴싱 속성 — MaterialBlock::Instanced).
//  BaseColor (_BaseColor): w < 0 = 그 색 (알파 = -1 - w), w >= 0 = 재질 값
//  Surface: x = _Metallic, y = _Smoothness, w = -(덮어쓸 것: 1 Metallic + 2 Smoothness) — w >= 0 = 재질 값
//  Emission (_EmissionColor, 선형 HDR): w < 0 = 그 값, w >= 0 = 재질 값
//  모두 입력이 꺼진 배치 (OpenGL 의 기본 0,0,0,1) 면 재질 값이 되게. 인스턴스 하나 112 바이트, WORLD 뒤에 붙인다
//  (location 8 · 9 · 10 — 같은 입력 배치를 쓰는 다른 셰이더와 앞 location 이 같게)
struct VertexIn_Batch
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 Tex : TEXCOORD;
    float4 TangentL : TANGENT;
    row_major float4x4 World : WORLD;
    float4 BaseColor : INSTCOLOR;
    float4 Surface : INSTSURFACE;
    float4 Emission : INSTEMISSION;
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
    float4 PosW : POSITION;
    float3 NormalW : NORMAL;
    float4 TangentW : TANGENT;
    float2 Tex : TEXCOORD0;
    //float4 ShadowPosH[LIGHT_MAX_SIZE] : TEXCOORD2;
    float4 SsaoPosH : TEXCOORD1;
};

VertexOut VS(VertexIn vin)
{
    VertexOut vout;
	
	// Transform to world space space.
    vout.PosW = mul(float4(vin.PosL, 1.0f), gWorld);
    vout.NormalW = mul(vin.NormalL, (float3x3) gWorldInvTranspose);
    vout.TangentW = mul(vin.TangentL, gWorld);
    
	// Transform to homogeneous clip space.
    vout.PosH = mul(float4(vin.PosL, 1.0f), gWorldViewProj);
	
	// Output vertex attributes for interpolation across triangle.
    vout.Tex = mul(float4(vin.Tex, 0.0f, 1.0f), gTexTransform).xy;

	// Generate projective tex-coords to project shadow map onto scene.
    //vout.ShadowPosH = mul(float4(vin.PosL, 1.0f), gShadowTransform);
    
    //int i = 0;
    //int j = gDirLightCount;
    //int l = gSpotLightCount;
   
    //for (int i = 0; i < gDirLightCount;i++)
    //{
    //    vout.ShadowPosH[i] = mul(float4(vout.PosW, 1.0f), gShadowTransform[i]);
    //}
    //int startIndex = gDirLightCount;
    //for (int j = 0; j < gSpotLightCount; j++)
    //{
    //    vout.ShadowPosH[j + startIndex] = mul(float4(vout.PosW, 1.0f), gShadowTransform[j + startIndex]);
    //}
    //startIndex = gDirLightCount + gSpotLightCount;
    //for (int l = 0; l < gPointLightCount; l += 6)
    //{

    //    for (int s = 0; s < 6;s++)
    //    {
    //        vout.ShadowPosH[l + s + startIndex] = mul(float4(vout.PosW, 1.0f), gShadowTransform[l + s + startIndex]);
    //    }
    //}

	// Generate projective tex-coords to project SSAO map onto scene.
    vout.SsaoPosH = mul(float4(vin.PosL, 1.0f), gWorldViewProjTex);

    return vout;
}

VertexOut VS_Instancing(VertexIn_Instancing vin)
{
    VertexOut vout;
    
    // Transform to homogeneous clip space.
    vout.PosH = mul(float4(vin.PosL, 1.0f), vin.World);
    
	// Transform to world space space.
    vout.PosW = vout.PosH; // World
    
    vout.NormalW = mul(vin.NormalL, (float3x3) gWorldInvTranspose);
    vout.TangentW = mul(vin.TangentL, vin.World);
    
	// Output vertex attributes for interpolation across triangle.
    vout.Tex = mul(float4(vin.Tex, 0.0f, 1.0f), gTexTransform).xy;

	// Generate projective tex-coords to project shadow map onto scene.
    //vout.ShadowPosH = mul(vout.PosH, gShadowTransform);

	// Generate projective tex-coords to project SSAO map onto scene.
    vout.SsaoPosH = mul(vout.PosH, gViewProjTex);

    float4x4 wvp = mul(gWorld, gViewProj);
    
    vout.PosH = mul(float4(vin.PosL, 1.0f), wvp);
    //vout.PosH = mul(float4(vout.PosW, 1.0f), gViewProj);
    
    return vout;
}

VertexOut VS_Skinned(SkinnedVertexIn vin)
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
    float3 tangentL = float3(0.0f, 0.0f, 0.0f);
    for (int i = 0; i < 4; ++i)
    {
	    // Assume no nonuniform scaling when transforming normals, so 
		// that we do not have to use the inverse-transpose.

        posL += weights[i] * mul(float4(vin.PosL, 1.0f), gBoneTransforms[vin.BoneIndices[i]]).xyz;
        normalL += weights[i] * mul(vin.NormalL, (float3x3) gBoneTransforms[vin.BoneIndices[i]]);
        tangentL += weights[i] * mul(vin.TangentL.xyz, (float3x3) gBoneTransforms[vin.BoneIndices[i]]);
    }
 
	// Transform to world space space.
    vout.PosW = mul(float4(posL, 1.0f), gWorld);
    vout.NormalW = mul(normalL, (float3x3) gWorldInvTranspose);
    vout.TangentW = float4(mul(tangentL, (float3x3) gWorld), vin.TangentL.w);

	// Transform to homogeneous clip space.
    vout.PosH = mul(float4(posL, 1.0f), gWorldViewProj);
	
	// Output vertex attributes for interpolation across triangle.
    vout.Tex = mul(float4(vin.Tex, 0.0f, 1.0f), gTexTransform).xy;

	// Generate projective tex-coords to project shadow map onto scene.
    //vout.ShadowPosH = mul(float4(posL, 1.0f), gShadowTransform);
    
	// Generate projective tex-coords to project SSAO map onto scene.
    vout.SsaoPosH = mul(float4(posL, 1.0f), gWorldViewProjTex);

    return vout;
}

// ---------------------------------------------------------------------------
// URP Lit (PBR, Metallic 워크플로). Unity URP 의 BRDF 를 따른다:
//  직접광 = (diffuse + specular * 정규화된 GGX) * 빛 * NdotL * 그림자
//  간접광 = 하늘 큐브맵 확산 조도 * diffuse * AO + 하늘 큐브맵(거칠기만큼 흐린 밉) * EnvironmentBRDF
//  감마 → 선형으로 계산한 뒤 다시 감마로 (나머지 파이프라인이 감마 공간)
// ---------------------------------------------------------------------------
float3 ToLinear(float3 c) { return pow(max(c, 0.0f), 2.2f); }
float3 ToGamma(float3 c) { return pow(max(c, 0.0f), 1.0f / 2.2f); }

float3 DirectBRDF(float3 diffuse, float3 specular, float roughness, float3 N, float3 L, float3 V, bool highlights)
{
    float3 H = normalize(L + V);
    float NoH = saturate(dot(N, H));
    float LoH = saturate(dot(L, H));
    float r2 = roughness * roughness;
    float d = NoH * NoH * (r2 - 1.0f) + 1.00001f;
    float specTerm = r2 / ((d * d) * max(0.1f, LoH * LoH) * (roughness * 4.0f + 2.0f));
    return diffuse + (highlights ? specular * specTerm : 0.0f);
}

// 점광 / 스포트광 거리 감쇠 (범위 끝에서 부드럽게 0)
float RangeAttenuation(float d, float range)
{
    float r = saturate(1.0f - pow(d / max(range, 0.0001f), 4.0f));
    return r * r / (d * d + 1.0f);
}

// ---------------------------------------------------------------------------
// URP Lit 조명 (메시 PS, 나무 PS 공용). 결과는 선형 색 (감마 변환·안개는 호출자)
//  Transmission: 잎처럼 얇은 면의 투과광 (Unreal 의 Two Sided Foliage 처럼 뒤에서 오는 빛이 비친다)
// ---------------------------------------------------------------------------
struct LitSurface
{
    float3 Albedo;        // 선형
    float Metallic;
    float Smoothness;
    float Occlusion;
    float3 Emission;      // 선형 HDR
    float3 Transmission;  // 선형 (0 = 없음)
    bool Highlights;
    bool Reflections;
    bool ReceiveShadows;
};

// ---------------------------------------------------------------------------
// Adaptive Probe Volume: 확산 환경광 (조도 / π — 하늘 큐브의 흐린 밉과 같은 단위)
//  xyz = 값, w = 덮은 정도 (0 = 볼륨 밖 → 하늘, 단계 가장자리 두 칸에서 다음 단계 · 하늘로 섞임)
// ---------------------------------------------------------------------------
static const float3 kGIProbes = float3(32.0f, 16.0f, 32.0f);
static const float3 kGIVoxels = float3(64.0f, 32.0f, 64.0f);
// 팔면체 노멀 (0..1 두 값)
float2 GIOctEncode(float3 n)
{
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    float2 o = n.xy;
    if (n.z < 0.0f)
        o = (1.0f - abs(n.yx)) * float2(n.x >= 0.0f ? 1.0f : -1.0f, n.y >= 0.0f ? 1.0f : -1.0f);
    return o * 0.5f + 0.5f;
}
float3 GIOctDecode(float2 e)
{
    float2 f = e * 2.0f - 1.0f;
    float3 n = float3(f.x, f.y, 1.0f - abs(f.x) - abs(f.y));
    float t = saturate(-n.z);
    n.xy += float2(n.x >= 0.0f ? -t : t, n.y >= 0.0f ? -t : t);
    return normalize(n);
}
// x 가 단계 c 의 복셀 속 면 뒤 (물체 속) 인지 — 복셀 크기보다 정확하게 (면이 복셀 어디에 있는지)
// 한 축의 두 칸 (+ 면, - 면): 있는 면들의 뒤인가 — 둘 다 있으면 둘 다 뒤 (얇은 벽 = 두 면 사이), 없으면 false
bool GIBehindAxis(float4 pp, float4 pn, float3 x, float3 vc, float vs)
{
    if (pp.a < 0.5f && pn.a < 0.5f)
        return false;
    bool behind = true;
    if (pp.a > 0.5f)
    {
        float3 n = GIOctDecode(pp.xy);
        behind = dot(x - (vc + n * ((pp.z - 0.5f) * 1.8f * vs)), n) < 0.0f;
    }
    if (pn.a > 0.5f)
    {
        float3 n = GIOctDecode(pn.xy);
        behind = behind && dot(x - (vc + n * ((pn.z - 0.5f) * 1.8f * vs)), n) < 0.0f;
    }
    return behind;
}

// 점 x 가 물체 속인가 (복셀 속 면 평면). 같은 축의 두 면은 둘 다 뒤 (얇은 벽), 다른 축끼리는 어느 하나라도 (방 안쪽 모서리 = 두 벽 중 하나의 속)
//  — 예전엔 면 칸이 + / - 두 개뿐이라 방 모서리의 두 벽 (-X · -Z) 이나 바닥과 벽 (+Y · +X) 이 한 칸을 다투고, 다른 축도 둘 다 뒤여야 막혀
//    모서리 복셀로 벽 너머 프로브가 새어 가는 빛 줄이 났다
bool GIBlocked(float3 x, int c, float4 vox)
{
    float3 vv = (x - vox.xyz) / vox.w;
    if (any(vv < 0.0f) || any(vv >= kGIVoxels))
        return false;
    int3 iv = int3(floor(vv));
    int4 t = int4(iv.x, iv.y, c * (int)kGIVoxels.z + iv.z, 0);
    float3 vc = vox.xyz + ((float3)iv + 0.5f) * vox.w;
    int stride = (int)gGIBias.w * (int)kGIVoxels.z;   // 칸 하나 = 모든 단계
    [loop]
    for (int axis = 0; axis < 3; ++axis)
    {
        float4 pp = gGIPlanes.Load(int4(t.xy, t.z + axis * 2 * stride, 0));
        float4 pn = gGIPlanes.Load(int4(t.xy, t.z + (axis * 2 + 1) * stride, 0));
        if (GIBehindAxis(pp, pn, x, vc, vox.w))
            return true;
    }
    return false;
}
static float3 s_GIDebug = float3(0, 0, 0);   // 진단 보기 (gGIBias.z = 2): 섞은 방법 색 — 초록 벽 검사 + 노멀, 노랑 노멀만, 빨강 삼선형만, 파랑 큰 단계
//  irrR = 반사 방향 R 쪽에서 오는 빛 (같은 SH — 하늘 반사를 가리는 데 쓴다)
float4 ProbeVolumeAmbient(float3 posW, float3 N, float3 V, float3 R, out float3 irrR)
{
    irrR = 0.0f;
    int count = (int)gGIParams.x;
    if (count <= 0)
        return float4(0, 0, 0, 0);
    if (gGIParams.w > 0.5f && (any(posW < gGILocalMin.xyz) || any(posW > gGILocalMax.xyz)))
        return float4(0, 0, 0, 0);
    float3 acc = 0.0f;
    float remaining = 1.0f;
    [loop]
    for (int c = 0; c < count; ++c)
    {
        float s = gGICascade[c].w;
        float3 p = posW + N * gGIBias.x + V * gGIBias.y;
        float3 local = (p - gGICascade[c].xyz) / (kGIProbes * s);
        float3 edge = min(local, 1.0f - local) * kGIProbes;   // 가장자리까지 프로브 칸 수
        float e = min(edge.x, min(edge.y, edge.z));
        if (e < 0.5f)
            continue;
        float w = saturate((e - 0.5f) / 2.0f) * remaining;
        // 둘레 8 프로브를 직접 섞는다 (Unity APV 의 Leak Reduction: Validity and Normal Based)
        //  - 유효도: 물체 속 프로브 (이웃 값으로 채운 것) 는 가중치 0.05 — 둘레가 모두 그럴 때만 쓰인다
        //  - 노멀: 면 뒤쪽 프로브는 빼고, 벽 너머 (가운데 복셀이 참) 도 뺀다. 다 빠지면 노멀만 → 삼선형만
        float3 gp = local * kGIProbes - 0.5f;
        int3 base = (int3)floor(gp);
        float3 f = gp - (float3)base;
        float4 r = 0, g = 0, b = 0;
        float v = 0.0f;
        float4 r2 = 0, g2 = 0, b2 = 0;   // 노멀만 (벽 검사 없이)
        float v2 = 0.0f;
        float4 r3 = 0, g3 = 0, b3 = 0;   // 삼선형만
        float v3 = 0.0f;
        [loop]   // 펼치면 셰이더가 커져 컴파일이 수십 초 (Debug)
        for (int k = 0; k < 8; ++k)
        {
            int3 o = int3(k & 1, (k >> 1) & 1, (k >> 2) & 1);
            int3 idx = clamp(base + o, int3(0, 0, 0), (int3)kGIProbes - 1);
            float3 tw = lerp(1.0f - f, f, (float3)o);
            float tri = tw.x * tw.y * tw.z;
            float3 toProbe = gGICascade[c].xyz + ((float3)idx + 0.5f) * s - p;
            float facing = saturate(dot(toProbe, N) / max(length(toProbe), 1e-4f) + 0.3f);
            int4 texel = int4(idx.x, idx.y, c * (int)kGIProbes.z + idx.z, 0);
            float4 sr = gGISH0.Load(texel), sg = gGISH1.Load(texel), sb = gGISH2.Load(texel);
            float sv = gGIValid.Load(texel).x;
            float visible = 1.0f;
            float4 vox = gGIVoxAll[c];
            if (vox.w > 0.0f)
            {
                // 면 앞 (p) 에서 프로브까지 네 점이 물체 속 (복셀 속 면 평면의 뒤) 이면 벽 너머 — 얇은 지붕 · 벽, 모서리 모두
                [loop]
                for (int q = 1; q <= 4 && visible > 0.0f; ++q)
                    if (GIBlocked(p + toProbe * (q * 0.2f), c, vox))
                        visible = 0.0f;
            }
            const float vw = sv > 0.5f ? 1.0f : 0.05f;
            const float w1 = tri * facing * visible * vw, w2 = tri * facing * vw, w3 = tri * vw;
            r += sr * w1; g += sg * w1; b += sb * w1; v += w1;
            r2 += sr * w2; g2 += sg * w2; b2 += sb * w2; v2 += w2;
            r3 += sr * w3; g3 += sg * w3; b3 += sb * w3; v3 += w3;
        }
        float3 dbg = c > 0 ? float3(0, 0, 1) : float3(0, 1, 0);
        if (v < 1e-5f) { r = r2; g = g2; b = b2; v = v2; dbg = float3(1, 1, 0); }   // 채운 프로브 (0.05) 라도 있으면 그것 — 문턱이 크면 벽 너머 프로브로 넘어가 모서리에 초승달 무늬
        if (v < 1e-5f) { r = r3; g = g3; b = b3; v = v3; dbg = float3(1, 0, 0); }
        s_GIDebug += dbg * w;
        if (v < 1e-4f)
            continue;
        r /= v; g /= v; b /= v;
        // E / π = Y00 L00 + (2/3) Y1 (L1 · n)   (Ramamoorthi — A0 = π, A1 = 2π/3)
        const float c0 = 0.282095f;
        const float c1 = 0.488603f * (2.0f / 3.0f);
        float3 irr = float3(c0 * r.x + c1 * dot(r.yzw, N), c0 * g.x + c1 * dot(g.yzw, N), c0 * b.x + c1 * dot(b.yzw, N));
        acc += max(irr, 0.0f) * w;
        irrR += max(float3(c0 * r.x + c1 * dot(r.yzw, R), c0 * g.x + c1 * dot(g.yzw, R), c0 * b.x + c1 * dot(b.yzw, R)), 0.0f) * w;
        remaining -= w;
        if (remaining <= 0.001f)
            break;
    }
    irrR *= gGIParams.y;
    return float4(acc * gGIParams.y, 1.0f - remaining);
}

// ---------------------------------------------------------------------------
// Reflection Probe (ReflectionProbes::Select · Bind): 반사 = 프로브들 (Importance 순, 상자 안쪽 Blend Distance 로 가중)
//  + 남는 몫은 하늘 — Unity URP Forward+ 의 프로브 블렌드. 확산 환경광은 하늘 그대로 (Unity 도 확산은 Light Probe / 하늘)
// ---------------------------------------------------------------------------
// 먹구름 · 낮밤 하늘 보정 (하늘에서 온 반사 · 환경광): 채도 빼기 + 색 배율
float3 WeatherSkyGrade(float3 c, bool tint)
{
    c = lerp(c, dot(c, float3(0.2126f, 0.7152f, 0.0722f)).xxx, gWeatherSky.w);
    return tint ? c * (1.0f - gWeatherSky.rgb) : c;
}

// 반사 = 프로브 (상자 안 가중치) + 남는 몫은 하늘. 하늘 · 구운 프로브에는 날씨 · 낮밤 하늘 보정 (WeatherSkyGrade),
//  실행 중에 찍은 프로브 (Intensity 가 음수로 온다 — 실시간 · 낮밤이 다시 찍은 것) 는 지금의 하늘이 이미 들어 있어 그대로
float3 ProbeReflection(float3 R, float3 posW, float perceptualRoughness, float skyMips, float skyScale)
{
    float3 sum = 0.0f;
    float total = 0.0f;
    int count = (int)gProbeParams.x;
    if (count > 0)
    {
        float probeMips = gProbeParams.y;
        float probeMip = min(perceptualRoughness * (1.7f - 0.7f * perceptualRoughness) * max(probeMips - 4.0f, 6.0f), probeMips - 1.0f);
        [loop]
        for (int i = 0; i < count; ++i)
        {
            if (total >= 0.999f)
                break;
            float4 bmin = gProbeData[i * 3 + 0];
            float4 bmax = gProbeData[i * 3 + 1];
            float4 cap = gProbeData[i * 3 + 2];
            float3 inside = min(posW - bmin.xyz, bmax.xyz - posW);
            float edge = min(inside.x, min(inside.y, inside.z));
            if (edge < 0.0f)
                continue;
            float w = bmin.w > 0.0f ? saturate(edge / bmin.w) : 1.0f;
            w = min(w, 1.0f - total);
            if (w <= 0.0f)
                continue;
            float slot = floor(cap.w * 0.5f + 0.01f);
            float3 dir = R;
            if (cap.w - slot * 2.0f > 0.5f)
            {
                // Box Projection: 반사 광선이 상자 벽에 닿는 점을 찍은 점에서 본 방향
                float3 safeR = max(abs(R), 1e-5f) * (step(0.0f, R) * 2.0f - 1.0f);
                float3 boxMinMax = lerp(bmin.xyz, bmax.xyz, step(0.0f, R));
                float3 rb = (boxMinMax - posW) / safeR;
                float fa = min(min(rb.x, rb.y), rb.z);
                dir = (posW - cap.xyz) + R * fa;
            }
            float3 c = ToLinear(gProbeCubes.SampleLevel(samLinear, float4(dir, slot), probeMip).rgb);
            if (bmax.w >= 0.0f)
                c = WeatherSkyGrade(c, true);
            sum += (w * abs(bmax.w)) * c;
            total += w;
        }
    }
    if (total < 0.999f)
    {
        float skyMip = perceptualRoughness * (1.7f - 0.7f * perceptualRoughness) * max(skyMips - 4.0f, 6.0f);
        sum += (1.0f - total) * skyScale * WeatherSkyGrade(ToLinear(gCubeMap.SampleLevel(samLinear, R, min(skyMip, skyMips - 1.0f)).rgb), true);
    }
    return sum;
}

// ---------------------------------------------------------------------------
// Screen Space Reflection (HDRP): 반사 방향으로 깊이 프리패스를 걸어 (뷰 공간, 가까울수록 촘촘 + 픽셀마다 어긋남) 처음 면 뒤로 들어간
//  곳을 이분 탐색으로 좁힌다 → 그 점의 지난 프레임 장면 색 (지난 ViewProj 로 되돌려 찾음). a = 믿음 (0 = 프로브 · 하늘 그대로)
// ---------------------------------------------------------------------------
bool SsrProject(float3 pV, out float2 uv)
{
    float4 c = mul(float4(pV, 1.0f), gSsrProj);
    uv = c.xy / max(c.w, 1e-5f) * float2(0.5f, -0.5f) + 0.5f;
    return c.w > 1e-4f && uv.x > 0.0f && uv.y > 0.0f && uv.x < 1.0f && uv.y < 1.0f;
}

float4 SsrDepth(float2 uv)
{
    return gSsrNormalDepth.Load(int3(min(uv * gSsrSize.xy, gSsrSize.xy - 1.0f), 0));
}

float4 ScreenSpaceReflection(float3 posW, float3 N, float3 R, float smoothness, float perceptualRoughness)
{
    if (gSsrParams.x < 0.5f || smoothness < gSsrParams2.x)
        return float4(0, 0, 0, 0);
    // 매끈함: Minimum Smoothness → Smoothness Fade Start 사이에서 서서히
    float fadeS = gSsrParams2.y > gSsrParams2.x + 1e-4f ? saturate((smoothness - gSsrParams2.x) / (gSsrParams2.y - gSsrParams2.x)) : 1.0f;
    float3 startW = posW + N * 0.02f;
    float3 originV = mul(float4(startW, 1.0f), gSsrView).xyz;
    float3 dirV = normalize(mul(R, (float3x3)gSsrView));
    float maxDist = gSsrParams.z;
    if (dirV.z < -1e-4f)
        maxDist = min(maxDist, (originV.z - 0.05f) / -dirV.z);   // 카메라 쪽 광선은 가까운 면 앞까지
    float2 uv0;
    if (maxDist <= 0.0f || !SsrProject(originV, uv0))
        return float4(0, 0, 0, 0);
    float jitter = LodDither(uv0 * gSsrSize.xy);   // 화면 고정 무늬 — 걸음 띠를 흩뜨린다
    int steps = (int)gSsrParams.y;
    // 화면에서 고르게 걷는다: 화면 비율 f 의 광선 거리 = 원근 보정 (1/w 를 선형으로) — 가까운 곳이 촘촘하다
    float4 c1 = mul(float4(originV + dirV * maxDist, 1.0f), gSsrProj);
    float k0 = 1.0f / max(mul(float4(originV, 1.0f), gSsrProj).w, 1e-4f);
    float k1 = 1.0f / max(c1.w, 1e-4f);
    // 화면 밖으로 나가는 곳까지만 걸음을 쓴다 (하늘로 가는 광선이 걸음을 버리지 않게 — 같은 걸음 수로 더 촘촘)
    float2 uv1 = c1.xy * k1 * float2(0.5f, -0.5f) + 0.5f;
    float2 duv = uv1 - uv0;
    float2 exitF = (step(0.0f, duv) - uv0) / (abs(duv) > 1e-6f ? duv : 1e-6f);
    float fEnd = saturate(min(abs(duv.x) > 1e-6f ? exitF.x : 1.0f, abs(duv.y) > 1e-6f ? exitF.y : 1.0f));
    float prevT = 0.0f, hitT = -1.0f;
    float prevSceneZ = 1e5f;
    [loop]
    for (int i = 0; i < steps; ++i)
    {
        float f = fEnd * (i + jitter) / (float)steps;
        float t = maxDist * max(f * k1 / lerp(k0, k1, f), 0.0005f);
        float3 p = originV + dirV * t;
        float2 uv;
        if (!SsrProject(p, uv))
            break;
        float sceneZ = SsrDepth(uv).w;
        float dz = p.z - sceneZ;
        // 면 뒤로 들어갔고 (두께 안) — 두께 = 깊이 비율 + 이번 걸음의 깊이 변화 (성긴 걸음이 얇은 면을 건너뛰지 않게)
        if (dz > 0.0f && dz < gSsrParams.w * sceneZ + abs(dirV.z) * (t - prevT) + 0.02f)
        {
            hitT = t;
            break;
        }
        // 깊이가 크게 끊김 (앞 표본은 면 앞, 이번은 훨씬 먼 곳 · 하늘): 그 사이 면 가장자리에서 면 뒤로 짧게 들어갔을 수 있다
        //  — 걸음 사이가 그 구간보다 길면 놓친다 (물체 위 모서리 반사의 털 같은 띠). 사이를 좁혀 찾는다
        float edgeZ = prevSceneZ * 1.1f + 0.3f;
        if (prevSceneZ < 1e4f && sceneZ > edgeZ)
        {
            float a = prevT, b = t;
            [loop]
            for (int j = 0; j < 5; ++j)
            {
                float m = (a + b) * 0.5f;
                float2 um;
                SsrProject(originV + dirV * m, um);
                float zm = SsrDepth(um).w;
                if (zm > edgeZ)
                    b = m;   // 이미 가장자리 밖
                else if (originV.z + dirV.z * m > zm)
                {
                    hitT = m;   // 가장자리 안에서 면 뒤 → 맞음 (아래 이분 탐색이 [a, m] 을 좁힌다)
                    break;
                }
                else
                    a = m;
            }
            if (hitT >= 0.0f)
            {
                prevT = a;
                break;
            }
        }
        prevT = t;
        prevSceneZ = sceneZ;
    }
    if (hitT < 0.0f)
        return float4(0, 0, 0, 0);
    // 이분 탐색
    float a = prevT, b = hitT;
    float2 hitUV = uv0;
    [unroll]
    for (int k = 0; k < 5; ++k)
    {
        float m = (a + b) * 0.5f;
        float2 uv;
        SsrProject(originV + dirV * m, uv);
        if (originV.z + dirV.z * m > SsrDepth(uv).w)
            b = m;
        else
            a = m;
    }
    SsrProject(originV + dirV * b, hitUV);
    float4 hit = SsrDepth(hitUV);
    if (hit.w > 1e4f || dot(hit.xyz, dirV) > 0.2f)
        return float4(0, 0, 0, 0);   // 하늘 · 면 뒷면
    // 좁힌 점이 면에 붙어 있어야 맞음 — 물체 위를 살짝 넘어 그 뒤로 지나간 광선 (걸음 허용으로 잡힌 것) 은 버린다
    if (originV.z + dirV.z * b - hit.w > gSsrParams.w * hit.w + 0.03f)
        return float4(0, 0, 0, 0);
    // 지난 프레임 화면 위치
    float4 prev = mul(float4(startW + R * b, 1.0f), gSsrPrevViewProj);
    if (prev.w <= 1e-4f)
        return float4(0, 0, 0, 0);
    float2 prevUV = prev.xy / prev.w * float2(0.5f, -0.5f) + 0.5f;
    // 화면 가장자리 (Screen Edge Fade Distance) · 광선 끝 · 매끈함
    float2 e = min(min(hitUV, 1.0f - hitUV), min(prevUV, 1.0f - prevUV));
    float edge = gSsrParams2.z > 1e-4f ? saturate(min(e.x, e.y) / (gSsrParams2.z * 0.5f)) : (min(e.x, e.y) > 0.0f ? 1.0f : 0.0f);
    float distFade = 1.0f - smoothstep(0.75f, 1.0f, b / gSsrParams.z);
    float mip = min(perceptualRoughness * (gSsrParams2.w - 1.0f) * 1.5f, gSsrParams2.w - 1.0f);
    float3 color = ToLinear(gSsrHistory.SampleLevel(samSsr, prevUV, mip).rgb);
    return float4(color, fadeS * edge * distFade);
}

// ---------------------------------------------------------------------------
// 날씨: 젖은 표면 · 웅덩이 · 빗방울 물결 (gWeatherSurface 가 0 이면 아무것도 하지 않는다)
// ---------------------------------------------------------------------------
float WeatherHash(float2 p) { return frac(sin(dot(p, float2(127.1f, 311.7f))) * 43758.5453f); }
float WeatherNoise(float2 p)
{
    const float2 i = floor(p), f = frac(p);
    const float2 u = f * f * (3.0f - 2.0f * f);
    return lerp(lerp(WeatherHash(i), WeatherHash(i + float2(1, 0)), u.x), lerp(WeatherHash(i + float2(0, 1)), WeatherHash(i + float2(1, 1)), u.x), u.y);
}

// 하늘 큐브에서 온 빛을 먹구름 하늘처럼 (Sky.fx 의 gSkyWeather 와 같은 값). tint = 밝기 · 색까지, 아니면 채도만
// 하늘 아래인 정도 (1 = 비를 맞는다, 0 = 지붕 · 처마 · 나무 아래). 덮개 맵 밖은 하늘 아래
// 위에서 본 맨 위 표면 (덮개 맵 — 땅 · 지붕) 보다 k m 넘게 높은가 (1 = 높다: 캐릭터의 몸 · 머리). 그림자 비교 샘플러를 다시 쓴다
//  (덮개 맵 깊이 + k 의 깊이 ≤ 맵 ⇔ 표면이 맨 위보다 k 넘게 위 — gWeatherCoverParams.z = 0.25 m 의 깊이)
float WeatherAboveCover(float3 posW, float k)
{
    if (gWeatherCoverParams.x < 0.5f)
        return 0.0f;
    const float4 c = mul(float4(posW, 1.0f), gWeatherCoverVP);
    return gWeatherCover.SampleCmpLevelZero(samShadow, c.xy, c.z + gWeatherCoverParams.z * (k / 0.25f));
}

float WeatherSky(float3 posW)
{
    if (gWeatherCoverParams.x < 0.5f)
        return 1.0f;
    const float4 c = mul(float4(posW, 1.0f), gWeatherCoverVP);
    const float d = c.z - gWeatherCoverParams.z;
    const float o = gWeatherCoverParams.y * 1.5f;
    // 맵 깊이보다 위 (작거나 같음) = 맨 위 표면 → 젖는다. 네 번 (가장자리를 부드럽게)
    float s = gWeatherCover.SampleCmpLevelZero(samShadow, c.xy + float2(-o, -o), d);
    s += gWeatherCover.SampleCmpLevelZero(samShadow, c.xy + float2(o, -o), d);
    s += gWeatherCover.SampleCmpLevelZero(samShadow, c.xy + float2(-o, o), d);
    s += gWeatherCover.SampleCmpLevelZero(samShadow, c.xy + float2(o, o), d);
    return s * 0.25f;
}

// 빗방울 물결 한 겹: 칸마다 정한 자리 · 때에 고리가 퍼지며 사라진다 → 높이의 기울기 (xz)
float2 WeatherRipple(float2 p, float t, float seed)
{
    const float2 cell = floor(p);
    const float2 f = frac(p);
    const float2 center = 0.25f + 0.5f * float2(WeatherHash(cell + seed), WeatherHash(cell + seed + 7.31f));
    const float life = frac(t * 1.1f + WeatherHash(cell + seed + 3.7f));
    const float2 d = f - center;
    const float r = length(d);
    const float x = (r - life * 0.48f) * 40.0f;   // 고리 둘레 물결 (반 칸까지 퍼진다)
    if (abs(x) > 3.14159f || r < 1e-4f)
        return float2(0, 0);
    const float amp = (1.0f - life) * (1.0f - life) * (0.5f + 0.5f * cos(x));
    return (d / r) * (cos(x * 2.0f) * amp);
}

// 눈 발자국: 눌린 정도 (창 밖 = 0). 고리 배치 = WRAP 샘플러로 uv = 월드 xz / 창 크기 (칸 번호 % 칸 수와 같다)
//  분기 없이 (조기 return 이 있으면 fxc 가 이 이펙트 전체를 컴파일하다 스택이 넘친다)
float SnowPressAt(float2 xz)
{
    const float2 rel = xz - gWeatherSnowWin.xy;
    const float inside = (gWeatherSnow.z > 0.5f && all(rel >= 0.0f) && all(rel < gWeatherSnowWin.z)) ? 1.0f : 0.0f;
    return inside * gSnowDeform.SampleLevel(samLinear, xz / max(gWeatherSnowWin.z, 1e-3f), 0.0f).r;
}

// 표면을 날씨에 맞게: 젖으면 어둡고 반들 (다공질일수록 더 어둡다), 위를 향한 면의 낮은 무늬에 웅덩이 (물 = 거울, 빗방울 고리)
void ApplyWeather(inout LitSurface surf, float3 posW, inout float3 N, float3 V)
{
    const float wet0 = gWeatherSurface.x, puddle0 = gWeatherSurface.y, snow0 = gWeatherSnow.x;
    if (wet0 <= 0.001f && puddle0 <= 0.001f && snow0 <= 0.001f)
        return;
    const float sky = WeatherSky(posW);
    if (sky <= 0.001f)
        return;
    const float up = saturate(N.y);
    // 옆면은 덜 (빗물이 흘러내린다), 아래를 향한 면은 마른다
    const float wet = wet0 * sky * lerp(0.6f, 1.0f, up) * saturate(N.y * 2.0f + 1.3f);
    const float porosity = saturate((1.0f - surf.Smoothness) * 1.2f) * (1.0f - surf.Metallic);
    surf.Albedo *= lerp(1.0f, lerp(0.85f, 0.45f, porosity), wet);
    surf.Smoothness = lerp(surf.Smoothness, max(surf.Smoothness, lerp(0.75f, 0.5f, porosity)), wet);   // 흙 · 풀은 덜 반들

    if (s_WeatherPuddles > 0.01f && puddle0 > 0.001f && up > 0.8f)
    {
        // 큰 무늬 + 작은 무늬: 웅덩이 수위가 오를수록 낮은 곳부터 찬다
        const float n = WeatherNoise(posW.xz * 0.3f) * 0.6f + WeatherNoise(posW.xz * 0.97f + 17.3f) * 0.4f;   // 2 ~ 3 m 웅덩이
        const float p = saturate((n - (1.0f - 0.36f * puddle0 * s_WeatherPuddles)) * 10.0f) * smoothstep(0.85f, 0.97f, up) * sky;   // 다 차면 바닥의 2 할쯤
        if (p > 0.001f)
        {
            float3 waterN = float3(0, 1, 0);
            const float rippleFade = gWeatherSurface.z * saturate(1.0f - distance(gEyePosW, posW) / 30.0f);   // 멀면 물결이 반짝이는 점으로 깨진다
            if (rippleFade > 0.001f)
            {
                const float t = gWeatherSurface.w;
                const float2 g = WeatherRipple(posW.xz / 0.32f, t, 0.0f) + WeatherRipple(posW.xz / 0.32f + 0.5f, t + 0.37f, 11.0f) * 0.8f;
                waterN = normalize(float3(-g.x * 0.9f * rippleFade, 1.0f, -g.y * 0.9f * rippleFade));
            }
            surf.Albedo *= lerp(1.0f, 0.3f, p);
            surf.Smoothness = lerp(surf.Smoothness, 0.97f, p);
            surf.Metallic = lerp(surf.Metallic, 0.0f, p);
            N = normalize(lerp(N, waterN, p));
        }
    }

    // ---- 쌓인 눈: 위를 향한 면부터 (덜 쌓였으면 잡음 무늬로 군데군데), 하늘 아래만
    if (snow0 > 0.001f)
    {
        const float n2 = WeatherNoise(posW.xz * 1.7f) * 0.6f + WeatherNoise(posW.xz * 0.23f + 5.1f) * 0.4f;
        const float snow = saturate((snow0 * 1.25f - (1.0f - up) * 1.5f - n2 * 0.45f * (1.0f - snow0)) * 5.0f) * sky;
        if (snow > 0.001f)
        {
            const float depth = gWeatherSnow.y;
            float2 xz = posW.xz;
            // 발자국 깊이 (시차): 보는 방향으로 내려가며 눌린 바닥을 만나는 자리
            // 발자국은 땅 · 지붕 같은 맨 위 표면에만 (캐릭터의 머리 · 어깨는 발밑 발자국 맵과 같은 xz 라도 눌리지 않는다)
            const float onGround = 1.0f - WeatherAboveCover(posW, depth + 0.3f);
            float press = SnowPressAt(xz) * onGround;
            const float3 dir = -V;
            if (!s_SnowDisplaced && dir.y < -0.05f && gWeatherSnow.z > 0.5f && onGround > 0.5f)
            {
                const float2 slide = dir.xz / -dir.y;
                // 8 걸음, 지나친 걸음과 앞 걸음 사이를 직선으로 (계단 무늬 없이)
                float prevGap = press * depth;   // 눌린 깊이 - 내려간 높이 (> 0 = 아직 바닥 위)
                float prevH = 0.0f;
                [loop]
                for (int k = 1; k <= 8; ++k)
                {
                    const float dh = depth * k * 0.125f;
                    const float pq = SnowPressAt(posW.xz + slide * dh);
                    const float gap = pq * depth - dh;
                    if (gap < 0.0f)
                    {
                        const float t = prevGap / max(prevGap - gap, 1e-5f);
                        const float h = lerp(prevH, dh, t);
                        xz = posW.xz + slide * h;
                        press = SnowPressAt(xz);
                        break;
                    }
                    prevGap = gap;
                    prevH = dh;
                    xz = posW.xz + slide * dh;
                    press = pq;
                }
            }
            // 발자국 가장자리의 기울기 → 법선 (눌린 곳 = 아래)
            const float e = gWeatherSnowWin.z / max(gWeatherSnowWin.w, 1.0f);
            float3 snowN = float3(0, 1, 0);
            [branch]
            if (gWeatherSnow.z > 0.5f && onGround > 0.5f)
            {
                // 가운데 차분 (2 칸)
                const float dx = (SnowPressAt(xz + float2(e * 2.0f, 0)) - SnowPressAt(xz - float2(e * 2.0f, 0))) / (4.0f * e);
                const float dz = (SnowPressAt(xz + float2(0, e * 2.0f)) - SnowPressAt(xz - float2(0, e * 2.0f))) / (4.0f * e);
                snowN = normalize(float3(dx * depth, 1.0f, dz * depth));
            }
            // 고운 눈 결 (작은 기복)
            const float b = WeatherNoise(xz * 9.0f) - 0.5f, b2 = WeatherNoise(xz * 9.0f + 3.3f) - 0.5f;
            snowN = normalize(snowN + float3(b, 0.0f, b2) * 0.12f);
            // 다져진 눈은 조금 어둡고 푸르다
            const float3 fresh = float3(0.9f, 0.92f, 0.96f);
            const float3 packed = float3(0.58f, 0.63f, 0.72f);
            surf.Albedo = lerp(surf.Albedo, lerp(fresh, packed, press * 0.8f), snow);
            surf.Smoothness = lerp(surf.Smoothness, lerp(0.28f, 0.45f, press), snow);
            surf.Metallic = lerp(surf.Metallic, 0.0f, snow);
            surf.Occlusion = lerp(surf.Occlusion, surf.Occlusion * (1.0f - 0.3f * press), snow);
            N = normalize(lerp(N, snowN, snow * 0.9f));
        }
    }
}

float3 ShadeLit(LitSurface surf, float3 posW, float3 N, float3 V, float4 ssaoPosH)
{
    if (gGIParams.z < 0.5f)
        ApplyWeather(surf, posW, N, V);
    // Unity: kDielectricSpec = 0.04, oneMinusReflectivity = 0.96 * (1 - metallic)
    float oneMinusReflectivity = 0.96f * (1.0f - surf.Metallic);
    float3 diffuse = surf.Albedo * oneMinusReflectivity;
    // Adaptive Probe Volume 의 장면 찍기: 빛 · 그림자 없이 확산 색만 (복셀이 매 프레임 다시 비춘다), 2 = 발광만 (발광 복셀)
    if (gGIParams.z > 1.5f)
        return surf.Emission;
    if (gGIParams.z > 0.5f)
        return diffuse;
    float3 specular = lerp(float3(0.04f, 0.04f, 0.04f), surf.Albedo, surf.Metallic);
    float perceptualRoughness = 1.0f - saturate(surf.Smoothness);
    float roughness = max(perceptualRoughness * perceptualRoughness, 0.0078125f);

    // ---- 그림자
    float dirShadows[LIGHT_SIZE];
    float spotShadows[LIGHT_SIZE];
    float pointShadows[LIGHT_SIZE];
    [unroll]
    for (int s0 = 0; s0 < LIGHT_SIZE; s0++)
    {
        dirShadows[s0] = 1.0f;
        spotShadows[s0] = 1.0f;
        pointShadows[s0] = 1.0f;
    }
    if (gShaderSetting.gUseShadowMap && surf.ReceiveShadows)
    {
        const int cascade = SelectCascade(posW);
        const float fade = ShadowFade(posW);
        const float blend = CascadeBlend(posW, cascade);
        [unroll]
        for (int i = 0; i < LIGHT_SIZE; i++)
            dirShadows[i] = DirShadow(gDirShadowMaps, i, posW, cascade, fade, blend);
        [unroll]
        for (int j = 0; j < LIGHT_SIZE; j++)
            spotShadows[j] = SpotShadow(gSpotShadowMaps, j, posW);
        [unroll]
        for (int l = 0; l < LIGHT_SIZE; l++)
            pointShadows[l] = PointShadow(gPointShadowMaps, l, posW);
    }

    ssaoPosH /= ssaoPosH.w;
    float ambientAccess = gShaderSetting.gUseSsaoMap ? gSsaoMap.SampleLevel(samLinear, ssaoPosH.xy, 0.0f).r : 1.0f;

    // ---- 직접광
    const bool highlights = surf.Highlights;
    const bool translucent = dot(surf.Transmission, surf.Transmission) > 0.0f;
    float3 color = float3(0, 0, 0);
    [loop]
    for (int di = 0; di < gDirLightCount; ++di)
    {
        if (!LightHits(gDirLightMask[di]))
            continue;
        float3 L = normalize(-gDirLights[di].Direction);
        float NoL = saturate(dot(N, L));
        float3 lightColor = ToLinear(gDirLights[di].Diffuse.rgb) * dirShadows[di];
        color += DirectBRDF(diffuse, specular, roughness, N, L, V, highlights) * lightColor * NoL;
        if (translucent)
        {
            // 뒷면으로 들어온 빛(감싸기) + 해를 마주 볼 때의 역광
            const float back = saturate(dot(-N, L)) * 0.6f + pow(saturate(dot(V, -L)), 6.0f) * 0.9f;
            color += surf.Transmission * lightColor * back;
        }
    }
    [loop]
    for (int si = 0; si < gSpotLightCount; ++si)
    {
        if (!LightHits(gSpotLightMask[si]))
            continue;
        float3 toLight = gSpotLights[si].Position - posW;
        float d = length(toLight);
        float3 L = toLight / max(d, 0.0001f);
        // Spot = 원뿔 전체 각도(도): 가장자리 20% 에서 부드럽게
        float cosOuter = cos(radians(gSpotLights[si].Spot * 0.5f));
        float cosInner = cos(radians(gSpotLights[si].Spot * 0.4f));
        float cone = smoothstep(cosOuter, cosInner, dot(-L, normalize(gSpotLights[si].Direction)));
        float atten = RangeAttenuation(d, gSpotLights[si].Range) * cone;
        float NoL = saturate(dot(N, L));
        color += DirectBRDF(diffuse, specular, roughness, N, L, V, highlights) * ToLinear(gSpotLights[si].Diffuse.rgb) * (NoL * atten * spotShadows[si]);
    }
    [loop]
    for (int pi = 0; pi < gPointLightCount; ++pi)
    {
        if (!LightHits(gPointLightMask[pi]))
            continue;
        float3 toLight = gPointLights[pi].Position - posW;
        float d = length(toLight);
        float3 L = toLight / max(d, 0.0001f);
        float atten = RangeAttenuation(d, gPointLights[pi].Range);
        float NoL = saturate(dot(N, L));
        color += DirectBRDF(diffuse, specular, roughness, N, L, V, highlights) * ToLinear(gPointLights[pi].Diffuse.rgb) * (NoL * atten * pointShadows[pi]);
    }
    // Forward+: 이 픽셀 클러스터의 추가 빛 (그림자 없음)
    if (gClusterParams.w > 0.5f)
    {
        float2 uvC = saturate(ssaoPosH.xy / ssaoPosH.w);
        float viewZ = max(dot(posW - gEyePosW, gClusterView.xyz), 1e-3f);
        int slice = clamp((int)floor(log(viewZ) * gClusterDepth.x + gClusterDepth.y), 0, (int)gClusterParams.z - 1);
        int2 tile = min(int2(uvC * gClusterParams.xy), int2(gClusterParams.xy) - 1);
        uint cluster = (uint)(tile.x + tile.y * (int)gClusterParams.x + slice * (int)(gClusterParams.x * gClusterParams.y));
        float4 cell = ClusterTexel(4096u + cluster);
        uint first = (uint)cell.x;
        uint count = (uint)cell.y;
        [loop]
        for (uint ci = 0; ci < count; ++ci)
        {
            uint at = first + ci;
            float4 packed = ClusterTexel(8192u + (at >> 2));
            uint q = at & 3u;
            uint li = (uint)(q == 0u ? packed.x : (q == 1u ? packed.y : (q == 2u ? packed.z : packed.w)));
            float4 c0 = ClusterTexel(li * 4u);
            float4 c1 = ClusterTexel(li * 4u + 1u);
            float4 c3 = ClusterTexel(li * 4u + 3u);
            if (!LightHits((uint)c3.y | ((uint)c3.z << 16)))
                continue;
            float3 toLight = c0.xyz - posW;
            float d = length(toLight);
            float3 L = toLight / max(d, 0.0001f);
            float atten = RangeAttenuation(d, c0.w);
            if (c1.w > 0.5f)
            {
                float4 c2 = ClusterTexel(li * 4u + 2u);
                atten *= smoothstep(c2.w, c3.x, dot(-L, c2.xyz));   // 스포트광 원뿔 (바깥 → 안)
            }
            if (atten <= 0.0f)
                continue;
            float NoL = saturate(dot(N, L));
            color += DirectBRDF(diffuse, specular, roughness, N, L, V, highlights) * ToLinear(c1.rgb) * (NoL * atten);
        }
    }

    // SSAO 의 Direct Lighting Strength: 직접광에도 그 몫만큼 (URP 와 같음 — 0 = 환경광만)
    color *= lerp(1.0f, ambientAccess, gSsaoParams.x);

    // ---- 간접광 (Unity EnvironmentBRDF)
    //  Environment Lighting = Skybox: 법선 방향 하늘을 아주 흐린 밉(면당 4x4)으로 읽어 확산 조도로 쓴다.
    //  빛의 Ambient 값은 예전 Blinn-Phong 용이라 Lit 에서는 쓰지 않는다 (Unity 에도 빛별 Ambient 는 없다).
    uint w, h, mips;
#ifdef NOVA_GLES   // OpenGL ES 에는 밉 개수 조회(textureQueryLevels)가 없다 → 크기로 (전체 밉 사슬)
    gCubeMap.GetDimensions(w, h);
    mips = firstbithigh(max(w, h)) + 1;
#else
    gCubeMap.GetDimensions(0, w, h, mips);
#endif
    float3 ambient = WeatherSkyGrade(ToLinear(gCubeMap.SampleLevel(samLinear, N, max((float)mips - 3.0f, 0.0f)).rgb), false);   // 밝기는 gIndirect 가
    // Adaptive Probe Volume 안이면 확산 환경광 = 프로브 (벽 · 지붕이 가린 하늘, 주변 색이 번진 빛)
    float3 giReflect;
    float4 giAmbient = ProbeVolumeAmbient(posW, N, V, reflect(-V, N), giReflect);
    // 하늘 반사 가림 (스페큘러 오클루전): 반사 방향에서 프로브가 받는 빛이 그쪽 하늘빛보다 어두운 만큼
    //  — 닫힌 방의 바닥 · 천장이 비스듬히 하늘을 비추지 않게
    const float3 kLum = float3(0.2126f, 0.7152f, 0.0722f);
    float3 skyR = ToLinear(gCubeMap.SampleLevel(samLinear, reflect(-V, N), max((float)mips - 3.0f, 0.0f)).rgb);
    //  (프로브 빛은 날씨 · 낮밤 하늘을 모았으니 하늘도 같은 배율로 — 밤에 바깥 반사가 다 가려지지 않게)
    float skyOcclusion = lerp(1.0f, saturate(dot(giReflect * gIndirectGI.rgb, kLum) / max(dot(skyR * gIndirect.rgb, kLum) * giAmbient.w, 1e-3f)), giAmbient.w);
    // 하늘 환경광만 gIndirect (Volume × 날씨 · 낮밤), 프로브 빛은 gIndirectGI — 예전엔 프로브 빛 전체에 밤 환경광 배율을 곱해 가로등이 번진 빛까지 어두웠다
    ambient = giAmbient.rgb * gIndirectGI.rgb + ambient * (1.0f - giAmbient.w) * gIndirect.rgb;
    if (gGIBias.z > 0.5f && gGIParams.x > 0.0f)
        return gGIBias.z > 1.5f ? s_GIDebug : giAmbient.rgb * 6.0f;   // 진단 보기: 1 = 프로브 빛만 (6 배), 2 = 섞은 방법
    float NoV = saturate(dot(N, V));
    float ao = surf.Occlusion * ambientAccess;
    color += ambient * diffuse * ao;
    if (translucent)
    {
        float3 giUnused;
        float4 giBack = ProbeVolumeAmbient(posW, -N, V, -N, giUnused);
        float3 back = giBack.rgb * gIndirectGI.rgb + ToLinear(gCubeMap.SampleLevel(samLinear, -N, max((float)mips - 3.0f, 0.0f)).rgb) * (1.0f - giBack.w) * gIndirect.rgb;
        color += back * surf.Transmission * 0.5f * ao;
    }
    if (surf.Reflections)
    {
        float3 R = reflect(-V, N);
        // Unity 는 128 큐브의 6 밉 단계. 큐브가 더 크면 그만큼 밉을 더 내려가 거친 면이 충분히 흐려지게 한다
        float3 env = ProbeReflection(R, posW, perceptualRoughness, (float)mips, skyOcclusion);   // 먹구름이면 파란 하늘 · 흰 구름을 비추지 않게 (보정은 안에서)
        float4 ssr = ScreenSpaceReflection(posW, N, R, surf.Smoothness, perceptualRoughness);
        env = lerp(env, ssr.rgb, ssr.a);   // 화면에서 맞은 만큼 프로브 · 하늘 대신
        float fresnel = pow(1.0f - NoV, 4.0f);
        float grazing = saturate(surf.Smoothness + (1.0f - oneMinusReflectivity));
        float surfaceReduction = 1.0f / (roughness * roughness + 1.0f);
        color += env * (surfaceReduction * lerp(specular, float3(grazing, grazing, grazing), fresnel)) * ao * gIndirect.w;
    }
    return color + surf.Emission;
}

// 선형 색 → 감마 + 안개
static int sDeferredFog = -1;   // 디퍼드 조명 패스: G-버퍼의 재질 안개 켜기 (-1 = 포워드 — 재질의 gFogEnabled)
float4 FinishLit(float3 color, float alpha, float distToEye)
{
    float4 litColor = float4(ToGamma(color), alpha);
    const bool fog = sDeferredFog >= 0 ? sDeferredFog != 0 : gShaderSetting.gFogEnabled != 0;
    if (fog && gGIParams.z < 0.5f)
    {
        float fogLerp = saturate((distToEye - gFogStart) / gFogRange);
        litColor.rgb = lerp(litColor.rgb, gFogColor.rgb, fogLerp);
    }
    return litColor;
}

// 표면 (LitPS · G-버퍼 공용): 재질 값 또는 인스턴스 값 (PS_Batch) — baseColorFactor = _BaseColor, metallicValue · smoothnessValue = _Metallic · _Smoothness,
//  emissionColor = 선형 _EmissionColor. 알파 자르기 · LOD 크로스페이드는 여기서 (픽셀을 버린다). alpha = Base Map × Base Color 의 알파
void LitSurfaceOf(VertexOut pin, float4 baseColorFactor, float metallicValue, float smoothnessValue, float3 emissionColor,
    out LitSurface surf, out float3 N, out float alpha)
{
    LodFadeClip(pin.PosH.xy);
    N = normalize(pin.NormalW);
    float2 uv = (gPbr.UVMode == 1 ? WorldBoxUV(pin.PosW.xyz, N) : pin.Tex) * gPbr.Tiling + gPbr.Offset;

    float4 baseSample = gPbr.UseBaseMap == 2 ? SampleVirtual(uv) : gPbr.UseBaseMap ? gDiffuseMap.Sample(samLinear, uv) : float4(1, 1, 1, 1);
    float4 baseColor = baseSample * baseColorFactor;
    if (gPbr.AlphaClip)
        clip(baseColor.a - gPbr.Cutoff);
    alpha = baseColor.a;

    float3 emission = emissionColor;
    if (gPbr.UseEmissionMap)
        emission *= ToLinear(gEmissionMap.Sample(samLinear, uv).rgb);

    float metallic = metallicValue;
    float smoothness = smoothnessValue;
    if (gPbr.UseMetallicMap)
    {
        float4 m = gMetallicMap.Sample(samLinear, uv);
        metallic = m.r;
        smoothness = m.a * smoothnessValue;
    }
    if (gPbr.SmoothnessFromAlbedo)
        smoothness = baseSample.a * smoothnessValue;

    if (gPbr.UseNormalMap)
    {
        float3 ns = gNormalMap.Sample(samLinear, uv).rgb * 2.0f - 1.0f;
        ns.xy *= gPbr.NormalScale;
        N = NormalSampleToWorldSpace(normalize(ns) * 0.5f + 0.5f, N, pin.TangentW);
    }
    float occlusion = gPbr.UseOcclusionMap ? lerp(1.0f, gOcclusionMap.Sample(samLinear, uv).g, gPbr.OcclusionStrength) : 1.0f;

    surf.Albedo = ToLinear(baseColor.rgb);
    surf.Metallic = metallic;
    surf.Smoothness = smoothness;
    surf.Occlusion = occlusion;
    surf.Emission = emission;
    surf.Transmission = float3(0, 0, 0);
    surf.Highlights = gPbr.SpecularHighlights != 0;
    surf.Reflections = gPbr.EnvironmentReflections != 0;
    surf.ReceiveShadows = gPbr.ReceiveShadows != 0;
}

float4 LitPS(VertexOut pin, float4 baseColorFactor, float metallicValue, float smoothnessValue, float3 emissionColor)
{
    LitSurface surf;
    float3 N;
    float alpha;
    LitSurfaceOf(pin, baseColorFactor, metallicValue, smoothnessValue, emissionColor, surf, N, alpha);
    if (gPbr.Unlit)
        return float4(ToGamma(surf.Albedo + surf.Emission), alpha);
    float3 toEye = gEyePosW - pin.PosW.xyz;
    float distToEye = length(toEye);
    float3 V = toEye / max(distToEye, 0.0001f);
    return FinishLit(ShadeLit(surf, pin.PosW.xyz, N, V, pin.SsaoPosH), alpha, distToEye);
}

float4 PS(VertexOut pin) : SV_Target
{
    return LitPS(pin, gPbr.BaseColor, gPbr.Metallic, gPbr.Smoothness, gPbr.EmissionColor.rgb);
}
#ifndef NOVA_NO_ENGINE_TECHNIQUES   // 데칼 등 함수만 쓰는 파일은 기법을 뺀다
technique11 Tech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS()));
    }
}
#endif

// ---------------------------------------------------------------------------
// MeshBatcher: 같은 메시·재질의 Mesh Renderer 를 인스턴싱으로 한 번에 (월드 행렬 = 인스턴스).
//  깊이 사전 패스(SsaoNormalDepth 의 NormalDepthBatchTech)와 똑같이 posW × ViewProj 로 계산해 EQUAL 깊이 검사가 맞는다.
//  법선은 월드 3x3 의 여인수 행렬(= 역전치 × 행렬식)로 → 크기가 축마다 달라도 맞다
// ---------------------------------------------------------------------------
float3 BatchNormal(float3 n, float4x4 world)
{
    const float3 r0 = world[0].xyz, r1 = world[1].xyz, r2 = world[2].xyz;
    const float3x3 cof = float3x3(cross(r1, r2), cross(r2, r0), cross(r0, r1));
    return mul(n, cof);
}

VertexOut VS_Batch(VertexIn_Instancing vin)
{
    VertexOut vout;
    const float4 posW = mul(float4(vin.PosL, 1.0f), vin.World);
    vout.PosW = posW;
    vout.NormalW = BatchNormal(vin.NormalL, vin.World);
    vout.TangentW = float4(mul(vin.TangentL.xyz, (float3x3) vin.World), vin.TangentL.w);
    vout.Tex = mul(float4(vin.Tex, 0.0f, 1.0f), gTexTransform).xy;
    vout.SsaoPosH = mul(posW, gViewProjTex);
    vout.PosH = mul(posW, gViewProj);
    return vout;
}

// 본 패스: VS_Batch 에 인스턴스 기본색을 더한다 (VS_Batch 는 Shader Graph 도 부른다 — 입력 그대로 둔다)
struct BatchVertexOut
{
    float4 PosH : SV_POSITION;
    float4 PosW : POSITION;
    float3 NormalW : NORMAL;
    float4 TangentW : TANGENT;
    float2 Tex : TEXCOORD0;
    float4 SsaoPosH : TEXCOORD1;
    nointerpolation float4 BaseColor : TEXCOORD2;
    nointerpolation float4 Surface : TEXCOORD3;
    nointerpolation float4 Emission : TEXCOORD4;
};

// 인스턴스 값을 뺀 정점 입력 (VS_Batch 에 넘긴다 — Shader Graph 의 VS_GraphBatch 도)
VertexIn_Instancing BatchToInstancing(VertexIn_Batch vin)
{
    VertexIn_Instancing v;
    v.PosL = vin.PosL;
    v.NormalL = vin.NormalL;
    v.Tex = vin.Tex;
    v.TangentL = vin.TangentL;
    v.World = vin.World;
    v.InstBaseColor = vin.BaseColor;
    v.InstSurface = vin.Surface;
    v.InstEmission = vin.Emission;
    v.InstanceId = vin.InstanceId;
    return v;
}

BatchVertexOut VS_BatchColor(VertexIn_Batch vin)
{
    const VertexOut o = VS_Batch(BatchToInstancing(vin));
    BatchVertexOut b;
    b.PosH = o.PosH;
    b.PosW = o.PosW;
    b.NormalW = o.NormalW;
    b.TangentW = o.TangentW;
    b.Tex = o.Tex;
    b.SsaoPosH = o.SsaoPosH;
    b.BaseColor = vin.BaseColor;
    b.Surface = vin.Surface;
    b.Emission = vin.Emission;
    return b;
}

float4 PS_Batch(BatchVertexOut pin) : SV_Target
{
    VertexOut v;
    v.PosH = pin.PosH;
    v.PosW = pin.PosW;
    v.NormalW = pin.NormalW;
    v.TangentW = pin.TangentW;
    v.Tex = pin.Tex;
    v.SsaoPosH = pin.SsaoPosH;
    const float4 baseColor = pin.BaseColor.w < 0.0f ? float4(pin.BaseColor.rgb, -1.0f - pin.BaseColor.w) : gPbr.BaseColor;
    const int mask = pin.Surface.w < -0.5f ? (int)(-pin.Surface.w + 0.5f) : 0;
    const float metallic = (mask & 1) ? pin.Surface.x : gPbr.Metallic;
    const float smoothness = (mask & 2) ? pin.Surface.y : gPbr.Smoothness;
    const float3 emission = pin.Emission.w < 0.0f ? pin.Emission.rgb : gPbr.EmissionColor.rgb;
    return LitPS(v, baseColor, metallic, smoothness, emission);
}

#ifndef NOVA_NO_ENGINE_TECHNIQUES   // 데칼 등 함수만 쓰는 파일은 기법을 뺀다
technique11 BatchTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_BatchColor()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS_Batch()));
    }
}

// ---------------------------------------------------------------------------
// Rendering Path = Deferred (URP 와 같은 G-버퍼 — docs/DEFERRED_RENDERING.md): MeshBatcher 의 엔진 Lit 재질은 표면만 G-버퍼에 쓰고
//  (PS_BatchGBuffer), 전체 화면 PS_DeferredLight 가 픽셀마다 같은 ShadeLit (그림자 · Forward+ 클러스터 · APV · 반사 · SSR · 날씨) + FinishLit (안개)
//   G0 (sRGB RGBA8)  : 알베도 (선형 → sRGB 저장), a = Occlusion
//   G1 (RGBA8)       : Metallic, Smoothness, 표시 (1 Highlights · 2 Reflections · 4 Receive Shadows · 8 있음 · 16 안개), 레이어 + 1 (33 = 모든 레이어)
//   G2 (RGBA16F)     : 월드 노멀
//   G3 (RGBA16F)     : 발광 (선형 HDR)
// ---------------------------------------------------------------------------
struct GBufferOut
{
    float4 G0 : SV_Target0;
    float4 G1 : SV_Target1;
    float4 G2 : SV_Target2;
    float4 G3 : SV_Target3;
};

GBufferOut PS_BatchGBuffer(BatchVertexOut pin)
{
    VertexOut v;
    v.PosH = pin.PosH;
    v.PosW = pin.PosW;
    v.NormalW = pin.NormalW;
    v.TangentW = pin.TangentW;
    v.Tex = pin.Tex;
    v.SsaoPosH = pin.SsaoPosH;
    const float4 baseColor = pin.BaseColor.w < 0.0f ? float4(pin.BaseColor.rgb, -1.0f - pin.BaseColor.w) : gPbr.BaseColor;
    const int mask = pin.Surface.w < -0.5f ? (int)(-pin.Surface.w + 0.5f) : 0;
    const float metallic = (mask & 1) ? pin.Surface.x : gPbr.Metallic;
    const float smoothness = (mask & 2) ? pin.Surface.y : gPbr.Smoothness;
    const float3 emission = pin.Emission.w < 0.0f ? pin.Emission.rgb : gPbr.EmissionColor.rgb;
    LitSurface surf;
    float3 N;
    float alpha;
    LitSurfaceOf(v, baseColor, metallic, smoothness, emission, surf, N, alpha);
    const uint layer = gObjectLayer == 0xFFFFFFFFu ? 33u : (uint)firstbitlow(gObjectLayer) + 1u;
    const uint flags = (surf.Highlights ? 1u : 0u) | (surf.Reflections ? 2u : 0u) | (surf.ReceiveShadows ? 4u : 0u) | 8u | (gShaderSetting.gFogEnabled ? 16u : 0u);
    GBufferOut o;
    o.G0 = float4(saturate(surf.Albedo), surf.Occlusion);
    o.G1 = float4(surf.Metallic, saturate(surf.Smoothness), flags / 255.0f, layer / 255.0f);
    o.G2 = float4(N, 1.0f);
    o.G3 = float4(surf.Emission, 1.0f);
    return o;
}

technique11 BatchGBufferTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_BatchColor()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS_BatchGBuffer()));
    }
}

Texture2D gGBuffer0;
Texture2D gGBuffer1;
Texture2D gGBuffer2;
Texture2D gGBuffer3;
Texture2D gDeferredDepth;    // 하드웨어 깊이 (R24 · R32 — G-버퍼와 같은 깊이 프리패스)
cbuffer cbDeferred
{
    float4x4 gDeferredInvViewProj;   // G-버퍼를 그린 뷰 × 투영 (지터 포함) 의 역
};

struct DeferredOut
{
    float4 PosH : SV_POSITION;
};

DeferredOut VS_DeferredFull(uint id : SV_VertexID)
{
    DeferredOut o;
    float2 t = float2((id << 1) & 2, id & 2);
    o.PosH = float4(t * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return o;
}

float4 PS_DeferredLight(DeferredOut pin) : SV_Target
{
    const int3 p = int3(int2(pin.PosH.xy), 0);
    const float4 g1 = gGBuffer1.Load(p);
    const uint flags = (uint)round(g1.z * 255.0f);
    if ((flags & 8u) == 0u)
        discard;   // G-버퍼 물체가 없는 픽셀 (하늘 · 포워드 물체 — 포워드 패스가 그린다)
    const float4 g0 = gGBuffer0.Load(p);
    const float4 g2 = gGBuffer2.Load(p);
    const float4 g3 = gGBuffer3.Load(p);
    uint dw, dh;
    gGBuffer1.GetDimensions(dw, dh);
    const float2 ndc = float2((pin.PosH.x / dw) * 2.0f - 1.0f, 1.0f - (pin.PosH.y / dh) * 2.0f);
    const float z = gDeferredDepth.Load(p).r;
    float4 w = mul(float4(ndc, z, 1.0f), gDeferredInvViewProj);
    const float3 posW = w.xyz / w.w;

    const uint layer = (uint)round(g1.w * 255.0f);
    sDeferredLayer = layer >= 33u || layer == 0u ? 0xFFFFFFFFu : (1u << (layer - 1u));
    sDeferredFog = (flags & 16u) != 0u ? 1 : 0;
    LitSurface surf;
    surf.Albedo = g0.rgb;
    surf.Occlusion = g0.a;
    surf.Metallic = g1.x;
    surf.Smoothness = g1.y;
    surf.Emission = g3.rgb;
    surf.Transmission = float3(0, 0, 0);
    surf.Highlights = (flags & 1u) != 0u;
    surf.Reflections = (flags & 2u) != 0u;
    surf.ReceiveShadows = (flags & 4u) != 0u;
    const float3 N = normalize(g2.xyz);
    const float3 toEye = gEyePosW - posW;
    const float distToEye = length(toEye);
    const float3 V = toEye / max(distToEye, 0.0001f);
    const float4 ssaoPosH = mul(float4(posW, 1.0f), gViewProjTex);
    return FinishLit(ShadeLit(surf, posW, N, V, ssaoPosH), 1.0f, distToEye);
}

technique11 DeferredLightTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_DeferredFull()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS_DeferredLight()));
    }
}
#endif

// ---------------------------------------------------------------------------
// 재질 테셀레이션 (Lit 의 Height Map — 60. Tessellation.fx): MeshBatcher 의 본 패스. 인스턴스 값은 첫 조절점의 것을 그대로
//  깊이 프리패스 (28 의 TessNormalDepthBatchTech) 와 같은 함수 → 같은 깊이 (EQUAL)
// ---------------------------------------------------------------------------
#include "60. Tessellation.fx"

struct TessCPB
{
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
    float4 TangentW : TANGENT;
    float2 Tex : TEXCOORD0;
    float4 BaseColor : TEXCOORD2;
    float4 Surface : TEXCOORD3;
    float4 Emission : TEXCOORD4;
};

TessCP TessCPOf(TessCPB b)
{
    TessCP c;
    c.PosW = b.PosW;
    c.NormalW = b.NormalW;
    c.TangentW = b.TangentW;
    c.Tex = b.Tex;
    return c;
}

TessCPB VS_TessBatch(VertexIn_Batch vin)
{
    const TessCP c = TessBatchCP(vin.PosL, vin.NormalL, vin.TangentL, mul(float4(vin.Tex, 0.0f, 1.0f), gTexTransform).xy, vin.World);
    TessCPB o;
    o.PosW = c.PosW;
    o.NormalW = c.NormalW;
    o.TangentW = c.TangentW;
    o.Tex = c.Tex;
    o.BaseColor = vin.BaseColor;
    o.Surface = vin.Surface;
    o.Emission = vin.Emission;
    return o;
}

TessPatch TessPatchHSB(InputPatch<TessCPB, 3> p)
{
    return TessFactors(p[0].PosW, p[1].PosW, p[2].PosW);
}

[domain("tri")]
[partitioning("fractional_odd")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("TessPatchHSB")]
[maxtessfactor(64.0f)]
TessCPB TessHSB(InputPatch<TessCPB, 3> p, uint i : SV_OutputControlPointID)
{
    return p[i];
}

// 본 패스의 Domain 출력: BatchVertexOut + 픽셀 법선용 (원래 법선 · uv 축)
struct TessBatchOut
{
    float4 PosH : SV_POSITION;
    float4 PosW : POSITION;
    float3 NormalW : NORMAL;
    float4 TangentW : TANGENT;
    float2 Tex : TEXCOORD0;
    float4 SsaoPosH : TEXCOORD1;
    nointerpolation float4 BaseColor : TEXCOORD2;
    nointerpolation float4 Surface : TEXCOORD3;
    nointerpolation float4 Emission : TEXCOORD4;
    float3 BaseN : TEXCOORD5;
    float3 DPdu : TEXCOORD6;
    float3 DPdv : TEXCOORD7;
};

[domain("tri")]
TessBatchOut DS_TessBatch(TessPatch pt, float3 w : SV_DomainLocation, const OutputPatch<TessCPB, 3> tri)
{
    const TessCP a = TessCPOf(tri[0]), b = TessCPOf(tri[1]), c = TessCPOf(tri[2]);
    const TessCP v = TessEvaluate(a, b, c, w);
    TessBatchOut o;
    o.PosW = float4(v.PosW, 1.0f);
    precise const float4 posH = mul(o.PosW, gTessViewProj);   // 깊이 프리패스와 같은 비트 (precise)
    o.PosH = posH;
    o.NormalW = v.NormalW;
    o.TangentW = v.TangentW;
    o.Tex = v.Tex;
    // SSAO 맵 좌표 = 화면 좌표 (VS_Batch 의 gViewProjTex 와 같은 값)
    o.SsaoPosH = float4(o.PosH.x * 0.5f + o.PosH.w * 0.5f, -o.PosH.y * 0.5f + o.PosH.w * 0.5f, o.PosH.z, o.PosH.w);
    o.BaseColor = tri[0].BaseColor;
    o.Surface = tri[0].Surface;
    o.Emission = tri[0].Emission;
    o.BaseN = a.NormalW * w.x + b.NormalW * w.y + c.NormalW * w.z;
    TessFrame(a, b, c, o.DPdu, o.DPdv);
    return o;
}

float4 PS_TessBatch(TessBatchOut pin) : SV_Target
{
    // 나눔이 끝나는 거리 너머는 POM 이 깊이감을 이어 받는다
    const float3 baseN = normalize(pin.BaseN);
    const float2 uv = TessParallaxUV(pin.PosW.xyz, baseN, pin.DPdu, pin.DPdv, pin.Tex, TessParallaxWeight(pin.PosW.xyz, true));
    BatchVertexOut v;
    v.PosH = pin.PosH;
    v.PosW = pin.PosW;
    v.NormalW = TessPixelNormal(baseN, pin.DPdu, pin.DPdv, uv);
    v.TangentW = pin.TangentW;
    v.Tex = uv;
    v.SsaoPosH = pin.SsaoPosH;
    v.BaseColor = pin.BaseColor;
    v.Surface = pin.Surface;
    v.Emission = pin.Emission;
    return PS_Batch(v);
}

// 테셀레이션이 없는 기기 (일부 OpenGL ES): 보통 정점 + POM · 픽셀 높이 법선 (uv 축은 화면 미분으로)
float4 PS_PomBatch(BatchVertexOut pin) : SV_Target
{
    const float3 dpx = ddx(pin.PosW.xyz), dpy = ddy(pin.PosW.xyz);
    const float2 dux = ddx(pin.Tex), duy = ddy(pin.Tex);
    const float det = dux.x * duy.y - dux.y * duy.x;
    const float inv = abs(det) > 1e-14f ? 1.0f / det : 0.0f;
    const float3 dPdu = (dpx * duy.y - dpy * dux.y) * inv;
    const float3 dPdv = (dpy * dux.x - dpx * duy.x) * inv;
    const float3 baseN = normalize(pin.NormalW);
    const float2 uv = TessParallaxUV(pin.PosW.xyz, baseN, dPdu, dPdv, pin.Tex, TessParallaxWeight(pin.PosW.xyz, false));
    BatchVertexOut v = pin;
    v.NormalW = TessPixelNormal(baseN, dPdu, dPdv, uv);
    v.Tex = uv;
    return PS_Batch(v);
}

#ifndef NOVA_NO_ENGINE_TECHNIQUES
technique11 PomBatchTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_BatchColor()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS_PomBatch()));
    }
}

technique11 TessBatchTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_TessBatch()));
        SetHullShader(CompileShader(hs_5_0, TessHSB()));
        SetDomainShader(CompileShader(ds_5_0, DS_TessBatch()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS_TessBatch()));
    }
}
#endif

#ifndef NOVA_NO_ENGINE_TECHNIQUES   // 데칼 등 함수만 쓰는 파일은 기법을 뺀다
technique11 InstancingTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Instancing()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS()));
    }
}
#endif

#ifndef NOVA_NO_ENGINE_TECHNIQUES   // 데칼 등 함수만 쓰는 파일은 기법을 뺀다
technique11 SkinnedTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Skinned()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS()));
    }
}
#endif

// 스킨드 인스턴싱 (엔진 공용 — 66. SkinInstancing.fx): 같은 메시 · 재질의 스킨 메시를 한 번에.
//  깊이 프리패스 (28 NormalDepthSkinnedInstancedTech) 와 같은 식 (posW × ViewProj) 이라 EQUAL 깊이 검사가 맞는다. 색 (Tint) 은 알베도에 곱한다
#include "66. SkinInstancing.fx"
struct SkinInstOut
{
    float4 PosH : SV_POSITION;
    float4 PosW : POSITION;
    float3 NormalW : NORMAL;
    float4 TangentW : TANGENT;
    float2 Tex : TEXCOORD0;
    float4 SsaoPosH : TEXCOORD1;
    float4 Tint : TEXCOORD2;
};

SkinInstOut VS_SkinnedInstanced(SkinnedVertexIn vin, uint iid : SV_InstanceID)
{
    const SkinInstance s = SkinInstanceLoad(gSkinInstanceBase + iid);
    float3 posW, normalW;
    float4 tangentW;
    SkinInstanceWorld(s, vin.PosL, vin.NormalL, vin.TangentL, vin.Weights, vin.BoneIndices, posW, normalW, tangentW);
    SkinInstOut o;
    o.PosW = float4(posW, 1.0f);
    o.NormalW = normalW;
    o.TangentW = tangentW;
    precise float4 posH = mul(float4(posW, 1.0f), gViewProj);   // 28 과 같은 식 (EQUAL)
    o.PosH = posH;
    o.Tex = mul(float4(vin.Tex, 0.0f, 1.0f), gTexTransform).xy;
    o.SsaoPosH = mul(o.PosW, gViewProjTex);
    o.Tint = s.Tint;
    return o;
}

VertexOut SkinInstToVertexOut(SkinInstOut p)
{
    VertexOut v;
    v.PosH = p.PosH;
    v.PosW = p.PosW;
    v.NormalW = p.NormalW;
    v.TangentW = p.TangentW;
    v.Tex = p.Tex;
    v.SsaoPosH = p.SsaoPosH;
    return v;
}

float4 PS_SkinnedInstanced(SkinInstOut pin) : SV_Target
{
    return LitPS(SkinInstToVertexOut(pin), gPbr.BaseColor * pin.Tint, gPbr.Metallic, gPbr.Smoothness, gPbr.EmissionColor.rgb);
}

// 애니메이션 임포스터 굽기 (물체 공간 = 월드, 색 없이): 알베도 (감마) · 물체 공간 법선
struct SkinBakeOut
{
    float4 Albedo : SV_Target0;
    float4 Normal : SV_Target1;
};
SkinBakeOut PS_SkinnedBake(SkinInstOut pin)
{
    LitSurface s;
    float3 N;
    float alpha;
    LitSurfaceOf(SkinInstToVertexOut(pin), gPbr.BaseColor, gPbr.Metallic, gPbr.Smoothness, gPbr.EmissionColor.rgb, s, N, alpha);
    SkinBakeOut o;
    // 임포스터는 비금속으로 그린다 (금속성 · 매끄러움 맵을 굽지 않는다) — 금속 부분은 반사 대신 어둡게 (멀리서 보이는 정도)
    o.Albedo = float4(ToGamma(s.Albedo * (1.0f - 0.6f * saturate(s.Metallic))), 1.0f);
    o.Normal = float4(N * 0.5f + 0.5f, s.Occlusion);
    return o;
}

struct SkinImpOut
{
    float4 PosH : SV_POSITION;
    float3 PosW : POSITION;
    float2 UV : TEXCOORD0;
    float3 AxisX : TEXCOORD1;
    float3 AxisZ : TEXCOORD2;
    float4 SsaoPosH : TEXCOORD3;
    float4 Tint : TEXCOORD4;
};

SkinImpOut VS_SkinnedImpostor(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
    const SkinInstance s = SkinInstanceLoad(gSkinInstanceBase + iid);
    const SkinImpostorGeom g = SkinImpostorVertex(vid, s);
    SkinImpOut o;
    o.PosW = g.PosW;
    precise float4 posH = mul(float4(g.PosW, 1.0f), gViewProj);   // 깊이 프리패스 (28) 와 같은 식 — EQUAL
    o.PosH = posH;
    o.UV = g.UV;
    o.AxisX = g.AxisX;
    o.AxisZ = g.AxisZ;
    o.SsaoPosH = mul(float4(g.PosW, 1.0f), gViewProjTex);
    o.Tint = s.Tint;
    return o;
}

float4 PS_SkinnedImpostor(SkinImpOut pin) : SV_Target
{
    const float4 a = gSkinImpAlbedo.Sample(samSkinImp, pin.UV);   // 자르기는 깊이 프리패스가 (EQUAL)
    const float4 n = gSkinImpNormal.Sample(samSkinImp, pin.UV);
    LitSurface s;
    s.Albedo = ToLinear(a.rgb) * pin.Tint.rgb;
    s.Metallic = 0.0f;      // 금속성은 굽기에서 알베도에 반영 (재질 값 1 · 맵으로 정하는 HDRP 마스크 재질이 검게 나왔다)
    s.Smoothness = 0.3f;
    s.Occlusion = n.a;
    s.Emission = float3(0, 0, 0);
    s.Transmission = float3(0, 0, 0);
    s.Highlights = true;
    s.Reflections = false;
    s.ReceiveShadows = true;
    const float3 N = SkinImpostorNormalW(n.rgb, pin.AxisX, pin.AxisZ);
    const float3 toEye = gEyePosW - pin.PosW;
    const float dist = length(toEye);
    return FinishLit(ShadeLit(s, pin.PosW, N, toEye / max(dist, 0.0001f), pin.SsaoPosH), 1.0f, dist);
}

DepthStencilState SkinImpDepthEqual
{
    DepthEnable = TRUE;
    DepthWriteMask = ZERO;
    DepthFunc = EQUAL;
};
DepthStencilState SkinBakeDepth
{
    DepthEnable = TRUE;
    DepthWriteMask = ALL;
    DepthFunc = LESS;
};
RasterizerState SkinImpCullNone
{
    CullMode = NONE;
};

#ifndef NOVA_NO_ENGINE_TECHNIQUES
technique11 SkinnedInstancedTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_SkinnedInstanced()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS_SkinnedInstanced()));
    }
}

technique11 SkinnedBakeTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_SkinnedInstanced()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS_SkinnedBake()));
        SetDepthStencilState(SkinBakeDepth, 0);
    }
}

technique11 SkinnedImpostorTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_SkinnedImpostor()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS_SkinnedImpostor()));
        SetDepthStencilState(SkinImpDepthEqual, 0);
        SetRasterizerState(SkinImpCullNone);
    }
}
#endif
//=============================================================================
// NOVA 지형 (Terrain 컴포넌트) - 본 패스
//=============================================================================
#include "40. TerrainCommon.fx"
#include "61. TerrainTessellation.fx"   // 레이어 높이: 변위 · 높이 기반 섞기 · 범프 · POM

DepthStencilState TerrainDepthLessEqual
{
    DepthEnable = TRUE;
    DepthWriteMask = ALL;
    DepthFunc = LESS_EQUAL;
};

struct TerrainVertexOut
{
    float4 PosH : SV_POSITION;
    float4 PosW : POSITION;
    float2 UV : TEXCOORD0;
    float4 SsaoPosH : TEXCOORD1;
};

TerrainVertexOut TerrainVS(uint vid : SV_VertexID)
{
    TerrainVertexOut vout;
    float2 uv;
    float3 posW = TerrainVertexWorld(vid, uv);
    vout.PosW = float4(posW, 1.0f);
    vout.PosH = mul(vout.PosW, gViewProj);
    vout.UV = uv;
    vout.SsaoPosH = mul(vout.PosW, gViewProjTex);
    return vout;
}

// tess = 테셀레이션으로 그리는 중 (POM 은 나눔 거리 끝에서 이어 받는다)
float4 TerrainShade(TerrainVertexOut pin, bool tess)
{
    float3 normalW = TerrainNormalUV(pin.UV);
    float3 toEye = gEyePosW - pin.PosW.xyz;
    float distToEye = length(toEye);
    toEye /= distToEye;

    float4 texColor;
    [branch] if (gTerrainHeightParams.x > 0.5f && gTerrainLayerCount > 0 && distToEye < 250.0f)   // 250 m 너머 = 범프 0 → 높이 · 노멀을 읽지 않는다
    {
        // 레이어 높이 · Normal Map (61): POM 으로 옮긴 자리 (절벽도) 에서 높이 기반으로 섞은 색 · 노멀 + 높이 범프 (모든 거리)
        const float4 c = TerrainControlWeights(pin.UV);
        float3 lp = pin.PosW.xyz - gTerrainOrigin.xyz;
        const float3 dLx = ddx(lp), dLy = ddy(lp);
        lp = TerrainParallax(lp, pin.PosW.xyz, gEyePosW, normalW, c, dLx, dLy, TerrainParallaxWeight(distToEye, tess));
        const TerrainTriplanar tri = TerrainTriplanarSetup(lp, normalW);   // 절벽은 triplanar
        const TerrainPixelHeight ph = TerrainHeightsAt(c, tri, normalW, distToEye < 30.0f);   // 30 m 안만 타일 없애기 3 표본
        texColor = TerrainAlbedoW(ph.Weights, pin.UV, tri, distToEye);
        normalW = TerrainBumpNormal(ph.Normal, pin.PosW.xyz, ph.D, saturate((250.0f - distToEye) / 150.0f));
    }
    else
        texColor = TerrainAlbedo(pin.UV, pin.PosW.xyz - gTerrainOrigin.xyz, normalW, distToEye);   // 절벽은 triplanar

    // 나무·바위·풀과 같은 URP Lit 조명: 해(그림자) + 하늘 큐브맵 환경광(SSAO) + 거친 반사. 흙·풀·바위는 거의 무광
    LitSurface surf;
    surf.Albedo = ToLinear(texColor.rgb);
    surf.Metallic = 0.0f;
    surf.Smoothness = 0.12f;
    surf.Occlusion = 1.0f;
    surf.Emission = float3(0, 0, 0);
    surf.Transmission = float3(0, 0, 0);
    surf.Highlights = true;
    surf.Reflections = true;
    surf.ReceiveShadows = true;
    s_WeatherPuddles = 0.45f;   // 풀 · 흙 위 웅덩이는 적게
    return FinishLit(ShadeLit(surf, pin.PosW.xyz, normalW, toEye, pin.SsaoPosH), 1.0f, distToEye);
}

float4 TerrainPS(TerrainVertexOut pin) : SV_Target
{
    return TerrainShade(pin, false);
}

// ---- 지형 테셀레이션 (61. TerrainTessellation.fx): Terrain Layer 의 Height Map 만큼 + 날씨의 쌓인 눈만큼 지형을 실제로 올린다
//  레이어 높이는 깊이 프리패스 (28) · 그림자 (26) 도 같은 함수로 민다 (EQUAL · LESS_EQUAL 이 맞게).
//  눈은 본 패스만 — 위로만 쌓이니 본 패스가 늘 프리패스 앞이라 LESS_EQUAL 이 맞는다 (그 위의 물체 발은 눈에 묻힌다). 발자국 자리는 땅까지

// 그 자리의 눈 높이 (m): ApplyWeather 의 눈 덮임 (위를 향한 면부터, 덜 쌓였으면 군데군데, 하늘 아래만) × 눈 깊이, 발자국만큼 낮게
float TerrainSnowLift(float3 posW, float3 n)
{
    const float snow0 = gWeatherSnow.x;
    if (snow0 <= 0.001f)
        return 0.0f;
    const float up = saturate(n.y);
    const float n2 = WeatherNoise(posW.xz * 1.7f) * 0.6f + WeatherNoise(posW.xz * 0.23f + 5.1f) * 0.4f;
    const float snow = saturate((snow0 * 1.25f - (1.0f - up) * 1.5f - n2 * 0.45f * (1.0f - snow0)) * 5.0f) * WeatherSky(posW);
    return gWeatherSnow.y * snow * (1.0f - SnowPressAt(posW.xz));
}

[domain("tri")]
TerrainVertexOut TerrainTessDS(TessPatch pt, float3 w : SV_DomainLocation, const OutputPatch<TerrainCP, 3> tri)
{
    TerrainVertexOut o;
    float2 uv;
    float3 n;
    float3 p = TerrainTessPosition(tri[0], tri[1], tri[2], w, uv, n);
    p.y += TerrainSnowLift(p, n);
    o.UV = uv;
    o.PosW = float4(p, 1.0f);
    precise const float4 posH = mul(o.PosW, gViewProj);   // 깊이 프리패스와 같은 비트 (precise)
    o.PosH = posH;
    o.SsaoPosH = mul(o.PosW, gViewProjTex);
    return o;
}

float4 TerrainTessPS(TerrainVertexOut pin) : SV_Target
{
    s_SnowDisplaced = true;
    return TerrainShade(pin, true);   // 법선 = 높이맵 + 레이어 높이 범프 (픽셀마다)
}

#ifndef NOVA_NO_ENGINE_TECHNIQUES   // 데칼 등 함수만 쓰는 파일은 기법을 뺀다
technique11 TerrainTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, TerrainVS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, TerrainPS()));
        SetDepthStencilState(TerrainDepthLessEqual, 0);
    }
}

technique11 TerrainTessTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, TerrainTessVS()));
        SetHullShader(CompileShader(hs_5_0, TerrainHS()));
        SetDomainShader(CompileShader(ds_5_0, TerrainTessDS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, TerrainTessPS()));
        SetDepthStencilState(TerrainDepthLessEqual, 0);
    }
}
#endif

//=============================================================================
// NOVA 나무 (Tree / 지형 나무) - 본 패스. 수피·잎은 실행 중에 식으로 구운 텍스처 (44. TreeCommon.fx)
//  인스턴싱 (TreeRenderer.cpp): 정점 = 나무 메시, 인스턴스 = 월드 행렬 + 색 변화·바람 위상·LOD 섞기
//=============================================================================
#include "44. TreeCommon.fx"

RasterizerState TreeLeafCullNone
{
    CullMode = None;   // 잎 카드·빌보드는 양면
};

struct TreeVertexOut
{
    float4 PosH : SV_POSITION;
    float3 PosW : POSITION0;
    float3 PosL : POSITION1;     // 바람 전 물체 공간 (반점 무늬가 흔들림을 따라 미끄러지지 않게)
    float3 NormalW : NORMAL;
    float3 AxisW : TANGENT;
    float2 UV : TEXCOORD0;
    float4 SsaoPosH : TEXCOORD1;
    float AO : TEXCOORD2;
    float Seed : TEXCOORD3;
    float3 AxisL : TEXCOORD4;
    float BranchRadius : TEXCOORD5;
    float Tint : TEXCOORD6;
    float2 Fade : TEXCOORD7;
};

TreeVertexOut TreeVS(TreeVertexIn vin, TreeInstanceIn inst)
{
    TreeVertexOut vout;
    float3 normalW;
    // precise: 깊이 사전 패스와 비트까지 같은 깊이여야 EQUAL 검사가 맞는다 (최적화로 식 순서가 바뀌지 않게)
    precise float3 posW = TreeWorldPos(vin, inst, normalW);
    vout.PosW = posW;
    vout.PosL = vin.PosL;
    vout.NormalW = normalW;
    vout.AxisW = normalize(mul(vin.Axis.xyz, (float3x3) inst.World));
    vout.AxisL = vin.Axis.xyz;
    vout.BranchRadius = vin.Phase.w;
    vout.UV = vin.UV;
    vout.AO = vin.Axis.w;
    vout.Seed = vin.Phase.z;
    vout.Tint = inst.Extra.x;
    vout.Fade = inst.Extra.zw;
    // 깊이 사전 패스(SsaoNormalDepth)와 같은 식: 월드 위치 × CPU 에서 곱한 ViewProj
    precise float4 posH = mul(float4(posW, 1.0f), gViewProj);
    vout.PosH = posH;
    vout.SsaoPosH = mul(float4(posW, 1.0f), gViewProjTex);
    return vout;
}

// ---------------------------------------------------------------- 표면 (본 패스와 임포스터 굽기가 같이 쓴다)
struct TreeSurf
{
    float3 Albedo;        // 선형
    float3 N;             // 월드
    float Smoothness;
    float Occlusion;
    float3 Transmission;
    bool Reflections;
};

TreeSurf TreeBarkSurface(TreeVertexOut pin)
{
    // 월드 접선 틀: T = 둘레(u 증가), B = 가지 방향(v 증가). 구운 타일에서 높이·기울기를 한 번 읽는다
    const float3 N0 = normalize(pin.NormalW);
    const float3 T = normalize(cross(normalize(pin.AxisW), N0) + 1e-5f);
    const float3 B = cross(N0, T);
    const float3 bark = TreeBarkSample(pin.UV, max(pin.BranchRadius, 0.005f));
    const float h0 = bark.z;

    TreeSurf s;
    s.N = normalize(N0 - (T * bark.x + B * bark.y) * gTreeBarkParams.x);

    // 색: 골은 어둡고 판은 밝게, 반점(밝은 / 음수면 자작나무 줄무늬), 위를 향한 면에 이끼
    const float3 barkColor = ToLinear(gTreeBarkColor.rgb);
    float3 albedo = barkColor * lerp(0.38f, 1.08f, h0);
    const float3 axisL = normalize(pin.AxisL);
    const float fleckAmount = abs(gTreeBarkParams.w);
    const float alongL = dot(pin.PosL, axisL);
    const float3 fq = gTreeBarkParams.w >= 0.0f ? pin.PosL * 9.0f : (pin.PosL - axisL * alongL) * 3.0f + axisL * (alongL * 25.0f);
    const float fleck = smoothstep(0.74f, 0.86f, TreeValueNoise(fq + 5.1f));
    albedo = lerp(albedo, gTreeBarkParams.w >= 0.0f ? barkColor * 1.6f : barkColor * 0.12f, fleck * fleckAmount);
    const float mossNoise = TreeValueNoise(pin.PosL * 2.3f + 11.7f);
    const float moss = saturate((s.N.y + 0.15f) * 1.6f) * smoothstep(0.35f, 0.75f, mossNoise + gTreeBarkColor.a * 0.5f) * gTreeBarkColor.a;
    albedo = lerp(albedo, ToLinear(gTreeMossColor.rgb) * lerp(0.7f, 1.1f, h0), moss);

    s.Albedo = albedo * lerp(0.9f, 1.1f, pin.Tint);
    s.Smoothness = gTreeBarkParams.z * lerp(0.6f, 1.0f, h0);
    s.Occlusion = pin.AO * lerp(0.55f, 1.0f, h0);   // 골 안쪽은 어둡게
    s.Transmission = float3(0, 0, 0);
    s.Reflections = true;
    return s;
}

// alphaClip: 임포스터 굽기만 자른다. 본 패스는 깊이 사전 패스가 이미 잘라 둔 깊이와 EQUAL 로 맞춘다
TreeSurf TreeLeafSurface(TreeVertexOut pin, bool alphaClip)
{
    const float4 leaf = TreeLeafSample(pin.UV, pin.Seed);
    if (alphaClip)
        clip(leaf.a - 0.5f);
    const bool twig = leaf.b < 0.05f;
    const float along = leaf.g;
    const float across = leaf.r * 2.0f - 1.0f;

    TreeSurf s;
    // 잎 법선은 생성기가 수관 구 쪽으로 굽혀 둔 값 (양면 모두 같은 법선 → 덩어리가 부드럽게 빛을 받음)
    s.N = normalize(pin.NormalW);
    s.Smoothness = gTreeLeafParams.y;
    s.Occlusion = pin.AO;
    s.Reflections = false;
    if (twig)
    {
        s.Albedo = ToLinear(gTreeBarkColor.rgb) * 0.8f;
        s.Smoothness = 0.2f;
        s.Transmission = float3(0, 0, 0);
    }
    else
    {
        // 잎마다 두 색 사이에서 조금씩 다르게, 끝으로 갈수록 밝게, 잎맥은 연하게
        const float v = TreeHash11(pin.Seed * 37.3f + leaf.b * 31.0f);
        float3 c = lerp(ToLinear(gTreeLeafColor.rgb), ToLinear(gTreeLeafColor2.rgb), saturate(v * gTreeLeafColor.a * 1.4f));
        c *= lerp(0.8f, 1.1f, saturate(along));
        const float midrib = 1.0f - smoothstep(0.03f, 0.12f, abs(across));
        const float veins = (1.0f - smoothstep(0.0f, 0.08f, abs(frac(along * 7.0f - abs(across) * 1.6f) - 0.5f) - 0.4f)) * (1.0f - midrib) * 0.5f;
        c *= (1.0f + midrib * 0.35f + veins * 0.15f) * lerp(0.78f, 1.22f, pin.Tint);
        s.Albedo = c;
        s.Transmission = c * gTreeLeafParams.x * 1.6f;

        // 잎을 살짝 접힌 모양으로: 잎맥에서 멀수록 법선을 옆으로
        const float3 up = normalize(pin.AxisW);
        const float3 right = normalize(cross(up, s.N) + 1e-5f);
        s.N = normalize(s.N + right * (across * 0.35f));
    }
    return s;
}

float4 TreeLit(TreeSurf s, float3 posW, float4 ssaoPosH)
{
    const float3 toEye = gEyePosW - posW;
    const float distToEye = length(toEye);
    LitSurface surf;
    surf.Albedo = s.Albedo;
    surf.Metallic = 0.0f;
    surf.Smoothness = s.Smoothness;
    surf.Occlusion = s.Occlusion;
    surf.Emission = float3(0, 0, 0);
    surf.Transmission = s.Transmission;
    surf.Highlights = true;
    surf.Reflections = s.Reflections;
    surf.ReceiveShadows = true;
    s_WeatherPuddles = 0.0f;   // 젖기만 (웅덩이 없음)
    return FinishLit(ShadeLit(surf, posW, s.N, toEye / max(distToEye, 0.0001f), ssaoPosH), 1.0f, distToEye);
}

// 본 패스: 잎 알파·LOD 디더는 깊이 사전 패스(SsaoNormalDepth)가 이미 잘랐다. 여기서는 깊이 EQUAL + 쓰기 없음으로
// 그 깊이와 같은 조각만 남긴다 → clip 이 없어 GPU 가 셰이더 전에 깊이로 거른다(early-Z). 겹친 잎 뒤쪽은 조명을 계산하지 않는다
float4 TreeBarkPS(TreeVertexOut pin) : SV_Target
{
    return TreeLit(TreeBarkSurface(pin), pin.PosW, pin.SsaoPosH);
}

float4 TreeLeafPS(TreeVertexOut pin) : SV_Target
{
    return TreeLit(TreeLeafSurface(pin, false), pin.PosW, pin.SsaoPosH);
}

// ---------------------------------------------------------------- 임포스터 굽기 (물체 공간 = 월드, 바람 0)
struct TreeBakeOut
{
    float4 Albedo : SV_Target0;   // rgb = 알베도(감마), a = 덮임
    float4 Normal : SV_Target1;   // rgb = 법선 * 0.5 + 0.5, a = AO
};

TreeBakeOut TreeBake(TreeSurf s)
{
    TreeBakeOut o;
    o.Albedo = float4(ToGamma(s.Albedo), 1.0f);
    o.Normal = float4(s.N * 0.5f + 0.5f, s.Occlusion);
    return o;
}

TreeBakeOut TreeBarkBakePS(TreeVertexOut pin) { return TreeBake(TreeBarkSurface(pin)); }
TreeBakeOut TreeLeafBakePS(TreeVertexOut pin) { return TreeBake(TreeLeafSurface(pin, true)); }

// ---------------------------------------------------------------- 임포스터 (멀리 있는 나무 = 카메라를 보는 사각형 하나)
struct TreeImpostorOut
{
    float4 PosH : SV_POSITION;
    float3 PosW : POSITION;
    float2 UV : TEXCOORD0;
    float3 AxisX : TEXCOORD1;
    float3 AxisZ : TEXCOORD2;
    float4 SsaoPosH : TEXCOORD3;
    float Tint : TEXCOORD4;
    float2 Fade : TEXCOORD5;
};

TreeImpostorOut TreeImpostorVS(uint vid : SV_VertexID, TreeInstanceIn inst)
{
    const TreeImpostorGeom g = TreeImpostorVertex(vid, inst);
    TreeImpostorOut vout;
    vout.PosW = g.PosW;
    vout.UV = g.UV;
    vout.AxisX = g.AxisX;
    vout.AxisZ = g.AxisZ;
    vout.Tint = inst.Extra.x;
    vout.Fade = inst.Extra.zw;
    precise float4 posH = mul(float4(g.PosW, 1.0f), gViewProj);
    vout.PosH = posH;
    vout.SsaoPosH = mul(float4(g.PosW, 1.0f), gViewProjTex);
    return vout;
}

float4 TreeImpostorPS(TreeImpostorOut pin) : SV_Target
{
    const float4 a = gTreeImpostorAlbedo.Sample(samTreeImpostor, pin.UV);   // 자르기는 깊이 사전 패스가 (EQUAL)
    const float4 n = gTreeImpostorNormal.Sample(samTreeImpostor, pin.UV);
    TreeSurf s;
    s.Albedo = ToLinear(a.rgb) * lerp(0.8f, 1.2f, pin.Tint);
    s.N = TreeImpostorNormalW(n.rgb, pin.AxisX, pin.AxisZ);
    s.Smoothness = 0.2f;
    s.Occlusion = n.a;
    s.Transmission = s.Albedo * gTreeLeafParams.x * 1.2f;
    s.Reflections = false;
    return TreeLit(s, pin.PosW, pin.SsaoPosH);
}

// ---------------------------------------------------------------- 기법
// 깊이 사전 패스가 쓴 깊이와 같은 조각만, 깊이는 다시 쓰지 않는다
DepthStencilState TreeDepthEqual
{
    DepthEnable = TRUE;
    DepthWriteMask = ZERO;
    DepthFunc = EQUAL;
};

#ifndef NOVA_NO_ENGINE_TECHNIQUES   // 데칼 등 함수만 쓰는 파일은 기법을 뺀다
technique11 TreeBarkTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, TreeVS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, TreeBarkPS()));
        SetDepthStencilState(TreeDepthEqual, 0);
    }
}
#endif

#ifndef NOVA_NO_ENGINE_TECHNIQUES   // 데칼 등 함수만 쓰는 파일은 기법을 뺀다
technique11 TreeLeafTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, TreeVS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, TreeLeafPS()));
        SetDepthStencilState(TreeDepthEqual, 0);
        SetRasterizerState(TreeLeafCullNone);
    }
}
#endif

#ifndef NOVA_NO_ENGINE_TECHNIQUES   // 데칼 등 함수만 쓰는 파일은 기법을 뺀다
technique11 TreeImpostorTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, TreeImpostorVS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, TreeImpostorPS()));
        SetDepthStencilState(TreeDepthEqual, 0);
        SetRasterizerState(TreeLeafCullNone);
    }
}
#endif

DepthStencilState TreeBakeDepth
{
    DepthEnable = TRUE;
    DepthWriteMask = ALL;
    DepthFunc = LESS;
};

#ifndef NOVA_NO_ENGINE_TECHNIQUES   // 데칼 등 함수만 쓰는 파일은 기법을 뺀다
technique11 TreeBarkBakeTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, TreeVS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, TreeBarkBakePS()));
        SetDepthStencilState(TreeBakeDepth, 0);
    }
}
#endif

#ifndef NOVA_NO_ENGINE_TECHNIQUES   // 데칼 등 함수만 쓰는 파일은 기법을 뺀다
technique11 TreeLeafBakeTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, TreeVS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, TreeLeafBakePS()));
        SetDepthStencilState(TreeBakeDepth, 0);
        SetRasterizerState(TreeLeafCullNone);
    }
}
#endif


// =============================================================================
// 바위·절벽 (47. RockCommon.fx, RockRenderer 가 인스턴싱으로 그린다)
// =============================================================================
#include "47. RockCommon.fx"

struct RockVertexOut
{
    float4 PosH : SV_POSITION;
    float3 PosW : POSITION0;
    float3 PosL : POSITION1;
    float3 NormalW : NORMAL0;
    float3 NormalL : NORMAL1;
    float4 SsaoPosH : TEXCOORD0;
    float2 AoCavity : TEXCOORD1;
    float Tint : TEXCOORD2;
    float3 Axis0 : TEXCOORD3;    // 물체 X/Y/Z 축 (월드) → 물체 공간 법선을 월드로
    float3 Axis1 : TEXCOORD4;
    float3 Axis2 : TEXCOORD5;
};

RockVertexOut RockVS(RockVertexIn vin, RockInstanceIn inst)
{
    RockVertexOut vout;
    float3 normalW;
    // precise: 깊이 사전 패스(SsaoNormalDepth)와 비트까지 같은 깊이여야 EQUAL 검사가 맞는다
    precise float3 posW = RockWorldPos(vin, inst, normalW);
    vout.PosW = posW;
    vout.PosL = vin.PosL;
    vout.NormalW = normalW;
    vout.NormalL = vin.NormalL;
    vout.AoCavity = vin.AoCavity;
    vout.Tint = inst.Extra.x;
    vout.Axis0 = inst.World[0].xyz;
    vout.Axis1 = inst.World[1].xyz;
    vout.Axis2 = inst.World[2].xyz;
    precise float4 posH = mul(float4(posW, 1.0f), gViewProj);
    vout.PosH = posH;
    vout.SsaoPosH = mul(float4(posW, 1.0f), gViewProjTex);
    return vout;
}

float4 RockPS(RockVertexOut pin) : SV_Target
{
    const float3 nL = normalize(pin.NormalL);
    // ---- 디테일: 물체 공간 triplanar (UDN 섞기)
    float3 w = pow(abs(nL), 4.0f);
    w /= max(w.x + w.y + w.z, 1e-5f);
    const float scale = 1.0f / max(gRockParams.w, 0.05f);
    const float4 tx = gRockDetail.Sample(samRock, pin.PosL.zy * scale + 0.37f);
    const float4 ty = gRockDetail.Sample(samRock, pin.PosL.xz * scale);
    const float4 tz = gRockDetail.Sample(samRock, pin.PosL.xy * scale + 0.71f);
    const float2 dx = (tx.xy * 2.0f - 1.0f) * gRockParams.z, dy = (ty.xy * 2.0f - 1.0f) * gRockParams.z, dz = (tz.xy * 2.0f - 1.0f) * gRockParams.z;
    float3 nDetailL = normalize(float3(0.0f, dx.y, dx.x) * w.x + float3(dy.x, 0.0f, dy.y) * w.y + float3(dz.x, dz.y, 0.0f) * w.z + nL);
    const float height = tx.a * w.x + ty.a * w.y + tz.a * w.z;
    // 물체 공간 → 월드 (비균등 크기도 대략 맞게: 축을 크기로 나눔)
    const float3 a0 = pin.Axis0 / max(dot(pin.Axis0, pin.Axis0), 1e-6f);
    const float3 a1 = pin.Axis1 / max(dot(pin.Axis1, pin.Axis1), 1e-6f);
    const float3 a2 = pin.Axis2 / max(dot(pin.Axis2, pin.Axis2), 1e-6f);
    float3 N = normalize(nDetailL.x * a0 + nDetailL.y * a1 + nDetailL.z * a2);
    const float3 N0 = normalize(pin.NormalW);

    // ---- 색
    const float cavity = pin.AoCavity.y;
    const float layers = max(gRockParams2.z, 1.0f);
    const float rockH = max(gRockParams2.w, 0.1f);
    // 지층 띠: 물체 높이를 따라 (노이즈로 흔들고 굵기가 다르게)
    const float yy = pin.PosL.y / rockH * layers + RockValueNoise(pin.PosL * 0.35f) * 0.8f;
    const float band = smoothstep(0.2f, 0.8f, RockValueNoise(float3(yy * 1.7f, 0.5f, 3.1f))) * gRockParams.x;
    float3 albedo = lerp(ToLinear(gRockBaseColor.rgb), ToLinear(gRockStrataColor.rgb), band);
    // 큰 얼룩(풍화·물 자국) + 디테일 높이 밝기
    const float stain = RockValueNoise(pin.PosL * 0.12f + 11.0f) * 0.65f + RockValueNoise(pin.PosL * 0.5f + 3.0f) * 0.35f;
    albedo *= lerp(0.62f, 1.1f, stain) * lerp(0.6f, 1.12f, height);
    // 세로 물 자국 (빗물이 흘러내린 어두운 줄)
    const float streak = RockValueNoise(float3(pin.PosL.x * 1.3f, pin.PosL.y * 0.08f, pin.PosL.z * 1.3f));
    albedo *= lerp(1.0f, lerp(0.58f, 1.0f, smoothstep(0.2f, 0.8f, streak)), saturate(1.0f - abs(N0.y) * 1.5f));
    // 오목한 곳 어둡게(흙·그늘), 볼록한 모서리 밝게 (닳은 모서리)
    albedo *= lerp(1.0f, 0.48f, saturate(cavity)) * (1.0f + 0.22f * saturate(-cavity));
    // 바위마다 밝기·색온도 차이
    const float tint = (pin.Tint - 0.5f) * 2.0f * gRockParams2.y;
    albedo *= float3(1.0f + tint * 0.6f, 1.0f + tint * 0.5f, 1.0f + tint * 0.35f);
    // 땅에 닿는 곳 (흙·습기로 어둡게)
    const float ground = 1.0f - smoothstep(0.0f, rockH * 0.12f + 0.15f, pin.PosL.y);
    albedo *= lerp(1.0f, 0.7f, ground);
    // 이끼·풀: 위를 향한 면 + 오목한 곳에 더, 노이즈로 얼룩
    const float mossNoise = RockValueNoise(pin.PosW * 0.6f) * 0.6f + RockValueNoise(pin.PosW * 2.3f) * 0.4f;
    const float moss = saturate(smoothstep(0.5f, 0.85f, N.y + (mossNoise - 0.5f) * 0.5f + saturate(cavity) * 0.15f) * gRockParams.y * 1.6f);
    const float3 mossCol = ToLinear(gRockMossColor.rgb) * lerp(0.7f, 1.15f, mossNoise);
    albedo = lerp(albedo, mossCol, moss);
    N = normalize(lerp(N, N0, moss * 0.6f));   // 이끼 위는 덜 거칠게

    LitSurface surf;
    surf.Albedo = albedo;
    surf.Metallic = 0.0f;
    surf.Smoothness = gRockParams2.x * (1.0f - moss * 0.7f) * lerp(0.7f, 1.2f, height);
    surf.Occlusion = pin.AoCavity.x * lerp(0.55f, 1.0f, height);
    surf.Emission = float3(0, 0, 0);
    surf.Transmission = float3(0, 0, 0);
    surf.Highlights = true;
    surf.Reflections = true;
    surf.ReceiveShadows = true;
    const float3 toEye = gEyePosW - pin.PosW;
    const float distToEye = length(toEye);
    s_WeatherPuddles = 0.0f;   // 젖기만 (웅덩이 없음)
    return FinishLit(ShadeLit(surf, pin.PosW, N, toEye / max(distToEye, 0.0001f), pin.SsaoPosH), 1.0f, distToEye);
}

DepthStencilState RockDepthEqual
{
    DepthEnable = TRUE;
    DepthWriteMask = ZERO;
    DepthFunc = EQUAL;
};

#ifndef NOVA_NO_ENGINE_TECHNIQUES   // 데칼 등 함수만 쓰는 파일은 기법을 뺀다
technique11 RockTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, RockVS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, RockPS()));
        SetDepthStencilState(RockDepthEqual, 0);
    }
}
#endif


// =============================================================================
// 지형 디테일: 풀·꽃·작은 돌 (48. DetailCommon.fx, DetailRenderer 가 인스턴싱으로 그린다)
// =============================================================================
#include "48. DetailCommon.fx"

struct DetailVertexOut
{
    float4 PosH : SV_POSITION;
    float3 PosW : POSITION0;
    float3 PosL : POSITION1;
    float3 NormalW : NORMAL0;
    float3 GroundN : NORMAL1;
    float2 UV : TEXCOORD0;
    float4 SsaoPosH : TEXCOORD1;
    nointerpolation float4 Data : TEXCOORD2;   // y = 부위, z = 잎 난수
    float AO : TEXCOORD3;
    float Tint : TEXCOORD4;
};

DetailVertexOut DetailVS(DetailVertexIn vin, DetailInstanceIn inst)
{
    DetailVertexOut vout;
    float3 normalW;
    float fade;
    // precise: 깊이 사전 패스(SsaoNormalDepth)와 비트까지 같은 깊이여야 EQUAL 검사가 맞는다
    precise float3 posW = DetailWorldPos(vin, inst, normalW, fade);
    vout.PosW = posW;
    vout.PosL = vin.PosL;
    vout.NormalW = normalW;
    vout.GroundN = inst.Ground.xyz;
    vout.UV = vin.UV;
    vout.Data = vin.Data;
    vout.AO = vin.Data.w;
    vout.Tint = inst.Scale.z;
    precise float4 posH = mul(float4(posW, 1.0f), gViewProj);
    vout.PosH = posH;
    vout.SsaoPosH = mul(float4(posW, 1.0f), gViewProjTex);
    return vout;
}

float4 DetailPS(DetailVertexOut pin) : SV_Target
{
    const int part = (int)round(pin.Data.y);
    const float3 toEye = gEyePosW - pin.PosW;
    const float distToEye = length(toEye);
    const float3 V = toEye / max(distToEye, 0.0001f);
    const float variation = (pin.Tint - 0.5f) * 2.0f * gDetailFlower.a;

    // 넓은 얼룩: 건강한 색 ↔ 마른 색 (Unity 의 Healthy / Dry Color + Noise Spread)
    const float2 q = pin.PosW.xz * gDetailDry.a;
    const float patch = DetailNoise(q) * 0.65f + DetailNoise(q * 3.1f + 5.3f) * 0.35f;
    const float dry = smoothstep(0.35f, 0.8f, patch) * gDetailHealthy.a;

    LitSurface surf;
    surf.Metallic = 0.0f;
    surf.Smoothness = gDetailParams.y;
    surf.Emission = float3(0, 0, 0);
    surf.Transmission = float3(0, 0, 0);
    surf.Highlights = true;
    surf.Reflections = false;
    surf.ReceiveShadows = true;
    float3 N = normalize(pin.NormalW);
    const float3 G = normalize(pin.GroundN);
    // 잎·꽃잎은 양면: 지면 쪽(위) 반구를 보게 돌린 뒤 지면 법선과 섞는다
    //  (뒷면을 그냥 뒤집어 섞으면 아래를 보는 법선 + 위 = 0 벡터 → NaN 검은 점)
    if (part != 3 && dot(N, G) < 0.0f)
        N = -N;

    if (part == 3)
    {
        // 돌: 물체 공간 얼룩 + 변화 색, 땅에 닿는 곳 어둡게
        const float spot = DetailNoise(pin.PosL.xz * 23.0f + pin.Data.z * 17.0f) * 0.6f + DetailNoise(pin.PosL.xy * 51.0f) * 0.4f;
        float3 c = lerp(ToLinear(gDetailHealthy.rgb), ToLinear(gDetailDry.rgb), saturate(spot * 1.2f - 0.2f) * (0.4f + gDetailHealthy.a));
        c *= lerp(0.75f, 1.15f, pin.Data.z) * (1.0f + variation * 0.6f);
        surf.Albedo = c;
        surf.Occlusion = pin.AO;
        surf.Reflections = true;
    }
    else if (part == 1 || part == 2)
    {
        // 꽃잎 (끝이 밝게) / 꽃 가운데
        float3 c = part == 1 ? ToLinear(gDetailFlower.rgb) * lerp(0.75f, 1.08f, pin.UV.y) : ToLinear(gDetailCenter.rgb);
        c *= 1.0f + variation * 0.5f + (pin.Data.z - 0.5f) * 0.15f;
        surf.Albedo = c;
        surf.Occlusion = pin.AO;
        surf.Transmission = part == 1 ? c * gDetailParams.x * 1.2f : float3(0, 0, 0);
        N = normalize(lerp(N, G, 0.35f));
    }
    else
    {
        // 잎 / 줄기: 뿌리 쪽 어둡게(덩어리 속 그늘), 끝으로 갈수록 밝고 마른 색, 잎마다 조금씩 다르게
        float3 c = lerp(ToLinear(gDetailHealthy.rgb), ToLinear(gDetailDry.rgb), saturate(dry + (pin.Data.z - 0.5f) * 0.25f));
        c *= 1.0f + variation + (pin.Data.z - 0.5f) * 0.2f;
        c = lerp(c, ToLinear(gDetailDry.rgb) * 1.1f, pow(saturate(pin.UV.y), 3.0f) * gDetailCenter.a);
        if (part == 4)
            c *= 0.8f;
        surf.Albedo = c;
        // 덩어리 속은 하늘이 덜 보인다 (뿌리 쪽 AO)
        surf.Occlusion = pin.AO * lerp(0.22f, 1.0f, smoothstep(0.0f, 0.8f, pin.UV.y));
        surf.Transmission = c * gDetailParams.x * 0.7f;
        // 잎 법선을 지면 쪽으로 굽혀 들판이 한 덩어리로 부드럽게 빛을 받게 (멀수록 더)
        const float soften = lerp(0.45f, 0.85f, saturate(distToEye / max(gDetailCam.w, 1.0f) * 1.5f));
        N = normalize(lerp(N, G, soften));
    }
    s_WeatherPuddles = 0.0f;   // 젖기만 (웅덩이 없음)
    return FinishLit(ShadeLit(surf, pin.PosW, N, V, pin.SsaoPosH), 1.0f, distToEye);
}

RasterizerState DetailCullNone
{
    CullMode = None;
};

DepthStencilState DetailDepthEqual
{
    DepthEnable = TRUE;
    DepthWriteMask = ZERO;
    DepthFunc = EQUAL;
};

#ifndef NOVA_NO_ENGINE_TECHNIQUES   // 데칼 등 함수만 쓰는 파일은 기법을 뺀다
technique11 DetailTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, DetailVS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, DetailPS()));
        SetDepthStencilState(DetailDepthEqual, 0);
        SetRasterizerState(DetailCullNone);
    }
}
#endif
