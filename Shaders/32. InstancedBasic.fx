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

float3 ShadeLit(LitSurface surf, float3 posW, float3 N, float3 V, float4 ssaoPosH)
{
    // Unity: kDielectricSpec = 0.04, oneMinusReflectivity = 0.96 * (1 - metallic)
    float oneMinusReflectivity = 0.96f * (1.0f - surf.Metallic);
    float3 diffuse = surf.Albedo * oneMinusReflectivity;
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
        [unroll]
        for (int i = 0; i < LIGHT_SIZE; i++)
            dirShadows[i] = DirShadow(gDirShadowMaps[i], i, posW, cascade, fade);
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
    gCubeMap.GetDimensions(0, w, h, mips);
    float3 ambient = ToLinear(gCubeMap.SampleLevel(samLinear, N, max((float)mips - 3.0f, 0.0f)).rgb);
    float NoV = saturate(dot(N, V));
    float ao = surf.Occlusion * ambientAccess;
    color += ambient * diffuse * ao;
    if (translucent)
        color += ToLinear(gCubeMap.SampleLevel(samLinear, -N, max((float)mips - 3.0f, 0.0f)).rgb) * surf.Transmission * 0.5f * ao;
    if (surf.Reflections)
    {
        float3 R = reflect(-V, N);
        // Unity 는 128 큐브의 6 밉 단계. 큐브가 더 크면 그만큼 밉을 더 내려가 거친 면이 충분히 흐려지게 한다
        float mip = perceptualRoughness * (1.7f - 0.7f * perceptualRoughness) * max((float)mips - 4.0f, 6.0f);
        float3 env = ToLinear(gCubeMap.SampleLevel(samLinear, R, min(mip, (float)(mips - 1))).rgb);
        float fresnel = pow(1.0f - NoV, 4.0f);
        float grazing = saturate(surf.Smoothness + (1.0f - oneMinusReflectivity));
        float surfaceReduction = 1.0f / (roughness * roughness + 1.0f);
        color += env * (surfaceReduction * lerp(specular, float3(grazing, grazing, grazing), fresnel)) * ao;
    }
    return color + surf.Emission;
}

// 선형 색 → 감마 + 안개
float4 FinishLit(float3 color, float alpha, float distToEye)
{
    float4 litColor = float4(ToGamma(color), alpha);
    if (gShaderSetting.gFogEnabled)
    {
        float fogLerp = saturate((distToEye - gFogStart) / gFogRange);
        litColor.rgb = lerp(litColor.rgb, gFogColor.rgb, fogLerp);
    }
    return litColor;
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
technique11 Tech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS()));
    }
}

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

technique11 BatchTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Batch()));
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

    float4 texColor = TerrainAlbedo(pin.UV, pin.PosW.xyz - gTerrainOrigin.xyz, normalW, distance(pin.PosW.xyz, gEyePosW));   // 절벽은 triplanar
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

DepthStencilState TreeBakeDepth
{
    DepthEnable = TRUE;
    DepthWriteMask = ALL;
    DepthFunc = LESS;
};

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
