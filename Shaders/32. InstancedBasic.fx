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
    float4 gDirShadowData[LIGHT_SIZE];   // x Strength (0 = 그림자 없음), y 필터 (0 Hard, 1 Low, 2 Medium, 3 High)
    float4 gSpotShadowData[LIGHT_SIZE];
    float4 gPointShadowData[LIGHT_SIZE];

    float gFogStart;
    float gFogRange;
    float4 gFogColor;
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
};

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
float ShadowPCF(Texture2DArray map, float3 coord, float slice, int filter)
{
    float lit = 1.0f;   // 빛의 먼 평면 밖이면 빛
    if (coord.z <= 1.0f)
    {
        uint w, h, n;
        map.GetDimensions(w, h, n);
        const float2 texel = 1.0f / float2(w, h);
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

// Max Distance 끝(Last Border 구간)에서 그림자가 사라지는 정도 (0 = 그대로, 1 = 없음)
float ShadowFade(float3 posW)
{
    return saturate((distance(posW, gEyePosW) - gShadowParams.z) * gShadowParams.w);
}

// 방향광 i 의 그림자 (1 = 빛, 0 = 그림자). 캐스케이드마다 배열 조각 하나
float DirShadow(Texture2DArray map, int i, float3 posW, int cascade, float fade)
{
    const float4 data = gDirShadowData[i];
    float lit = 1.0f;
    if (data.x > 0.0f && cascade >= 0)
    {
        const float3 coord = mul(float4(posW, 1.0f), gDirShadowTransforms[i * 4 + cascade]).xyz;
        const float s = ShadowPCF(map, coord, (float)cascade, (int)data.y);
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
        lit = lerp(1.0f, ShadowPCF(map, p.xyz / p.w, slice, (int)data.y), data.x);
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

float4 PS(VertexOut pin) : SV_Target
{
    float3 N = normalize(pin.NormalW);
    float3 toEye = gEyePosW - pin.PosW.xyz;
    float distToEye = length(toEye);
    float3 V = toEye / max(distToEye, 0.0001f);
    float2 uv = pin.Tex * gPbr.Tiling + gPbr.Offset;

    // ---- 표면
    float4 baseSample = gPbr.UseBaseMap ? gDiffuseMap.Sample(samLinear, uv) : float4(1, 1, 1, 1);
    float4 baseColor = baseSample * gPbr.BaseColor;
    if (gPbr.AlphaClip)
        clip(baseColor.a - gPbr.Cutoff);

    float3 emission = gPbr.EmissionColor.rgb;
    if (gPbr.UseEmissionMap)
        emission *= ToLinear(gEmissionMap.Sample(samLinear, uv).rgb);

    if (gPbr.Unlit)
        return float4(ToGamma(ToLinear(baseColor.rgb) + emission), baseColor.a);

    float3 albedo = ToLinear(baseColor.rgb);
    float metallic = gPbr.Metallic;
    float smoothness = gPbr.Smoothness;
    if (gPbr.UseMetallicMap)
    {
        float4 m = gMetallicMap.Sample(samLinear, uv);
        metallic = m.r;
        smoothness = m.a * gPbr.Smoothness;
    }
    if (gPbr.SmoothnessFromAlbedo)
        smoothness = baseSample.a * gPbr.Smoothness;

    if (gPbr.UseNormalMap)
    {
        float3 ns = gNormalMap.Sample(samLinear, uv).rgb * 2.0f - 1.0f;
        ns.xy *= gPbr.NormalScale;
        N = NormalSampleToWorldSpace(normalize(ns) * 0.5f + 0.5f, N, pin.TangentW);
    }
    float occlusion = gPbr.UseOcclusionMap ? lerp(1.0f, gOcclusionMap.Sample(samLinear, uv).g, gPbr.OcclusionStrength) : 1.0f;

    // Unity: kDielectricSpec = 0.04, oneMinusReflectivity = 0.96 * (1 - metallic)
    float oneMinusReflectivity = 0.96f * (1.0f - metallic);
    float3 diffuse = albedo * oneMinusReflectivity;
    float3 specular = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);
    float perceptualRoughness = 1.0f - saturate(smoothness);
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
    if (gShaderSetting.gUseShadowMap && gPbr.ReceiveShadows)
    {
        const int cascade = SelectCascade(pin.PosW.xyz);
        const float fade = ShadowFade(pin.PosW.xyz);
        [unroll]
        for (int i = 0; i < LIGHT_SIZE; i++)
            dirShadows[i] = DirShadow(gDirShadowMaps[i], i, pin.PosW.xyz, cascade, fade);
        [unroll]
        for (int j = 0; j < LIGHT_SIZE; j++)
            spotShadows[j] = SpotShadow(gSpotShadowMaps[j], j, pin.PosW.xyz);
        [unroll]
        for (int l = 0; l < LIGHT_SIZE; l++)
            pointShadows[l] = PointShadow(gPointShadowMaps[l], l, pin.PosW.xyz);
    }

    pin.SsaoPosH /= pin.SsaoPosH.w;
    float ambientAccess = gShaderSetting.gUseSsaoMap ? gSsaoMap.Sample(samLinear, pin.SsaoPosH.xy, 0.0f).r : 1.0f;

    // ---- 직접광
    bool highlights = gPbr.SpecularHighlights != 0;
    float3 color = float3(0, 0, 0);
    [loop]
    for (int di = 0; di < gDirLightCount; ++di)
    {
        float3 L = normalize(-gDirLights[di].Direction);
        float NoL = saturate(dot(N, L));
        color += DirectBRDF(diffuse, specular, roughness, N, L, V, highlights) * ToLinear(gDirLights[di].Diffuse.rgb) * (NoL * dirShadows[di]);
    }
    [loop]
    for (int si = 0; si < gSpotLightCount; ++si)
    {
        float3 toLight = gSpotLights[si].Position - pin.PosW.xyz;
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
        float3 toLight = gPointLights[pi].Position - pin.PosW.xyz;
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
    gCubeMap.GetDimensions(0, w, h, mips);
    float3 ambient = ToLinear(gCubeMap.SampleLevel(samLinear, N, max((float)mips - 3.0f, 0.0f)).rgb);
    float NoV = saturate(dot(N, V));
    float ao = occlusion * ambientAccess;
    color += ambient * diffuse * ao;
    if (gPbr.EnvironmentReflections)
    {
        float3 R = reflect(-V, N);
        // Unity 는 128 큐브의 6 밉 단계. 큐브가 더 크면 그만큼 밉을 더 내려가 거친 면이 충분히 흐려지게 한다
        float mip = perceptualRoughness * (1.7f - 0.7f * perceptualRoughness) * max((float)mips - 4.0f, 6.0f);
        float3 env = ToLinear(gCubeMap.SampleLevel(samLinear, R, min(mip, (float)(mips - 1))).rgb);
        float fresnel = pow(1.0f - NoV, 4.0f);
        float grazing = saturate(smoothness + (1.0f - oneMinusReflectivity));
        float surfaceReduction = 1.0f / (roughness * roughness + 1.0f);
        color += env * (surfaceReduction * lerp(specular, float3(grazing, grazing, grazing), fresnel)) * ao;
    }
    color += emission;

    float4 litColor = float4(ToGamma(color), baseColor.a);
    if (gShaderSetting.gFogEnabled)
    {
        float fogLerp = saturate((distToEye - gFogStart) / gFogRange);
        litColor.rgb = lerp(litColor.rgb, gFogColor.rgb, fogLerp);
    }
    return litColor;
}
technique11 Tech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS()));
    }
}

technique11 InstancingTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Instancing()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS()));
    }
}

technique11 SkinnedTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Skinned()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS()));
    }
}
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

    float4 texColor = TerrainAlbedo(pin.UV);
    float4 litColor = texColor;
    if ((gDirLightCount + gPointLightCount + gSpotLightCount) > 0)
    {
        float4 ambient = 0, diffuse = 0, spec = 0;
        float dirShadows[LIGHT_SIZE];
        float spotShadows[LIGHT_SIZE];
        [unroll]
        for (int k = 0; k < LIGHT_SIZE; k++)
        {
            dirShadows[k] = 1.0f;
            spotShadows[k] = 1.0f;
        }
        if (gShaderSetting.gUseShadowMap)
        {
            const int cascade = SelectCascade(pin.PosW.xyz);
            const float fade = ShadowFade(pin.PosW.xyz);
            [unroll]
            for (int i = 0; i < LIGHT_SIZE; i++)
                dirShadows[i] = DirShadow(gDirShadowMaps[i], i, pin.PosW.xyz, cascade, fade);
            [unroll]
            for (int j = 0; j < LIGHT_SIZE; j++)
                spotShadows[j] = SpotShadow(gSpotShadowMaps[j], j, pin.PosW.xyz);
        }

        float ambientAccess = 1.0f;
        if (gShaderSetting.gUseSsaoMap)
        {
            float4 ssao = pin.SsaoPosH / pin.SsaoPosH.w;
            ambientAccess = gSsaoMap.SampleLevel(samLinear, ssao.xy, 0.0f).r;
        }

        float4 A, D, S;
        for (int i = 0; i < gDirLightCount; ++i)
        {
            ComputeDirectionalLight(gMaterial, gDirLights[i], normalW, toEye, A, D, S);
            ambient += ambientAccess * A;
            diffuse += dirShadows[i] * D;
            spec += dirShadows[i] * S;
        }
        for (int j = 0; j < gSpotLightCount; ++j)
        {
            ComputeSpotLight(gMaterial, gSpotLights[j], pin.PosW.xyz, normalW, toEye, A, D, S);
            ambient += ambientAccess * A;
            diffuse += spotShadows[j] * D;
            spec += spotShadows[j] * S;
        }
        for (int l = 0; l < gPointLightCount; ++l)
        {
            ComputePointLight(gMaterial, gPointLights[l], pin.PosW.xyz, normalW, toEye, A, D, S);
            ambient += ambientAccess * A;
            diffuse += D;
            spec += S;
        }
        litColor = texColor * (ambient + diffuse) + spec;
    }

    if (gShaderSetting.gFogEnabled)
        litColor = lerp(litColor, gFogColor, saturate((distToEye - gFogStart) / gFogRange));
    litColor.a = 1.0f;
    return litColor;
}

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
