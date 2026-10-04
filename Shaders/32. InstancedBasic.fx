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
};

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
};

// 이 빛이 이 물체를 비추나 (Light.cullingMask & 물체 레이어)
bool LightHits(uint mask) { return (mask & gObjectLayer) != 0u; }

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
Texture2DArray gDirShadowMaps[LIGHT_SIZE];   // 캐스케이드 = 배열 조각
Texture2DArray gSpotShadowMaps[LIGHT_SIZE];    // 조각 1 개
Texture2DArray gPointShadowMaps[LIGHT_SIZE];   // 큐브 6 면 = 조각 6 개
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
Texture2D gDiffuseMap;     // Base Map
Texture2D gNormalMap;
Texture2D gMetallicMap;    // R = Metallic, A = Smoothness (Unity 와 같음)
Texture2D gOcclusionMap;   // G = Occlusion
Texture2D gEmissionMap;



SamplerState samLinear
{
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = WRAP;
    AddressV = WRAP;
};

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
        float s = ShadowPCF(map, coord, (float)cascade, (int)data.y, data.z);
        [branch]
        if (blend > 0.0f)   // 경계 구간만 다음 캐스케이드를 한 번 더 읽는다
        {
            const float3 coord2 = mul(float4(posW, 1.0f), gDirShadowTransforms[i * 4 + cascade + 1]).xyz;
            s = lerp(s, ShadowPCF(map, coord2, (float)(cascade + 1), (int)data.y, data.z), blend);
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
    return PerspectiveShadow(map, gSpotShadowTransforms[i], 0.0f, gSpotShadowData[i], posW);
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
    return PerspectiveShadow(map, gPointShadowTransforms[i * 6 + face], (float)face, gPointShadowData[i], posW);
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
            sum += (w * bmax.w) * ToLinear(gProbeCubes.SampleLevel(samLinear, float4(dir, slot), probeMip).rgb);
            total += w;
        }
    }
    if (total < 0.999f)
    {
        float skyMip = perceptualRoughness * (1.7f - 0.7f * perceptualRoughness) * max(skyMips - 4.0f, 6.0f);
        sum += (1.0f - total) * skyScale * ToLinear(gCubeMap.SampleLevel(samLinear, R, min(skyMip, skyMips - 1.0f)).rgb);
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

float3 ShadeLit(LitSurface surf, float3 posW, float3 N, float3 V, float4 ssaoPosH)
{
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
            dirShadows[i] = DirShadow(gDirShadowMaps[i], i, posW, cascade, fade, blend);
        [unroll]
        for (int j = 0; j < LIGHT_SIZE; j++)
            spotShadows[j] = SpotShadow(gSpotShadowMaps[j], j, posW);
        [unroll]
        for (int l = 0; l < LIGHT_SIZE; l++)
            pointShadows[l] = PointShadow(gPointShadowMaps[l], l, posW);
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
    float3 ambient = ToLinear(gCubeMap.SampleLevel(samLinear, N, max((float)mips - 3.0f, 0.0f)).rgb);
    // Adaptive Probe Volume 안이면 확산 환경광 = 프로브 (벽 · 지붕이 가린 하늘, 주변 색이 번진 빛)
    float3 giReflect;
    float4 giAmbient = ProbeVolumeAmbient(posW, N, V, reflect(-V, N), giReflect);
    // 하늘 반사 가림 (스페큘러 오클루전): 반사 방향에서 프로브가 받는 빛이 그쪽 하늘빛보다 어두운 만큼
    //  — 닫힌 방의 바닥 · 천장이 비스듬히 하늘을 비추지 않게
    const float3 kLum = float3(0.2126f, 0.7152f, 0.0722f);
    float3 skyR = ToLinear(gCubeMap.SampleLevel(samLinear, reflect(-V, N), max((float)mips - 3.0f, 0.0f)).rgb);
    float skyOcclusion = lerp(1.0f, saturate(dot(giReflect, kLum) / max(dot(skyR, kLum) * giAmbient.w, 1e-3f)), giAmbient.w);
    ambient = giAmbient.rgb + ambient * (1.0f - giAmbient.w);
    if (gGIBias.z > 0.5f && gGIParams.x > 0.0f)
        return gGIBias.z > 1.5f ? s_GIDebug : giAmbient.rgb * 6.0f;   // 진단 보기: 1 = 프로브 빛만 (6 배), 2 = 섞은 방법
    float NoV = saturate(dot(N, V));
    float ao = surf.Occlusion * ambientAccess;
    color += ambient * diffuse * ao * gIndirect.rgb;
    if (translucent)
    {
        float3 giUnused;
        float4 giBack = ProbeVolumeAmbient(posW, -N, V, -N, giUnused);
        float3 back = giBack.rgb + ToLinear(gCubeMap.SampleLevel(samLinear, -N, max((float)mips - 3.0f, 0.0f)).rgb) * (1.0f - giBack.w);
        color += back * surf.Transmission * 0.5f * ao * gIndirect.rgb;
    }
    if (surf.Reflections)
    {
        float3 R = reflect(-V, N);
        // Unity 는 128 큐브의 6 밉 단계. 큐브가 더 크면 그만큼 밉을 더 내려가 거친 면이 충분히 흐려지게 한다
        float3 env = ProbeReflection(R, posW, perceptualRoughness, (float)mips, skyOcclusion);
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
float4 FinishLit(float3 color, float alpha, float distToEye)
{
    float4 litColor = float4(ToGamma(color), alpha);
    if (gShaderSetting.gFogEnabled && gGIParams.z < 0.5f)
    {
        float fogLerp = saturate((distToEye - gFogStart) / gFogRange);
        litColor.rgb = lerp(litColor.rgb, gFogColor.rgb, fogLerp);
    }
    return litColor;
}

// 재질 값 또는 인스턴스 값 (PS_Batch): baseColorFactor = _BaseColor, metallicValue · smoothnessValue = _Metallic · _Smoothness, emissionColor = 선형 _EmissionColor
float4 LitPS(VertexOut pin, float4 baseColorFactor, float metallicValue, float smoothnessValue, float3 emissionColor)
{
    LodFadeClip(pin.PosH.xy);
    float3 N = normalize(pin.NormalW);
    float3 toEye = gEyePosW - pin.PosW.xyz;
    float distToEye = length(toEye);
    float3 V = toEye / max(distToEye, 0.0001f);
    float2 uv = pin.Tex * gPbr.Tiling + gPbr.Offset;

    // ---- 표면
    float4 baseSample = gPbr.UseBaseMap ? gDiffuseMap.Sample(samLinear, uv) : float4(1, 1, 1, 1);
    float4 baseColor = baseSample * baseColorFactor;
    if (gPbr.AlphaClip)
        clip(baseColor.a - gPbr.Cutoff);

    float3 emission = emissionColor;
    if (gPbr.UseEmissionMap)
        emission *= ToLinear(gEmissionMap.Sample(samLinear, uv).rgb);

    if (gPbr.Unlit)
        return float4(ToGamma(ToLinear(baseColor.rgb) + emission), baseColor.a);

    float3 albedo = ToLinear(baseColor.rgb);
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

    LitSurface surf;
    surf.Albedo = albedo;
    surf.Metallic = metallic;
    surf.Smoothness = smoothness;
    surf.Occlusion = occlusion;
    surf.Emission = emission;
    surf.Transmission = float3(0, 0, 0);
    surf.Highlights = gPbr.SpecularHighlights != 0;
    surf.Reflections = gPbr.EnvironmentReflections != 0;
    surf.ReceiveShadows = gPbr.ReceiveShadows != 0;
    return FinishLit(ShadeLit(surf, pin.PosW.xyz, N, V, pin.SsaoPosH), baseColor.a, distToEye);
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
//=============================================================================
// NOVA 지형 (Terrain 컴포넌트) - 본 패스
//=============================================================================
#include "40. TerrainCommon.fx"

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

float4 TerrainPS(TerrainVertexOut pin) : SV_Target
{
    float3 normalW = TerrainNormalUV(pin.UV);
    float3 toEye = gEyePosW - pin.PosW.xyz;
    float distToEye = length(toEye);
    toEye /= distToEye;

    float4 texColor = TerrainAlbedo(pin.UV, pin.PosW.xyz - gTerrainOrigin.xyz, normalW, distance(pin.PosW.xyz, gEyePosW));   // 절벽은 triplanar

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
    return FinishLit(ShadeLit(surf, pin.PosW.xyz, normalW, toEye, pin.SsaoPosH), 1.0f, distToEye);
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
