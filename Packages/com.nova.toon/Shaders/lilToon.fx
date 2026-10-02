//=============================================================================
// lilToon.fx — NOVA Toon Shader 패키지 (com.nova.toon)
//
// lilToon (https://github.com/lilxyzw/lilToon, MIT License, Copyright (c) 2020-2024 lilxyzw) 의 셰이딩을 NOVA 로 옮긴 것.
//  옮긴 부분 (lil_common_frag.hlsl · lil_common_functions.hlsl 의 식):
//   - lilGetShading      : N·L*0.5+0.5 → 그림자 1 · 2 단 (Border · Blur 계단화, 받는 그림자), 그림자 색 (× 그림자 색 텍스처),
//                          주 색 곱하기, 환경광으로 들어올리기, min(그림자, 밝은 면), 경계 색 (Border Color · Range)
//   - 빛 색             : 주 빛 + 하늘 (SH 대신 하늘 큐브의 흐린 밉), Min/Max Limit, Monochrome, As Unlit
//   - lilGetRim         : 프레넬 림 (Border · Blur · Power · 그림자 마스크 · 빛 받기), 더하기
//   - lilBacklight      : 역광 (빛이 뒤에서 올 때 가장자리, Directivity · View Strength)
//   - lilCalcOutline    : 법선 방향 외곽선 (cm), 가까울 때 가늘게 (Fix Width), Z Bias, 외곽선 색 × 빛
//  엔진의 빛 · 그림자 · 하늘 · 변수는 "32. InstancedBasic.fx" 를 그대로 포함해 쓴다 (엔진이 프레임마다 값을 넣는다).
//=============================================================================
#include "32. InstancedBasic.fx"   // 엔진 셰이더 (엔진 Shaders 폴더에서 찾는다)

cbuffer cbLilToon
{
    float4 gLilShadowColor;        // rgb 1 단 그림자 색
    float4 gLilShadow2ndColor;     // rgb 2 단 그림자 색, a = 2 단 세기
    float4 gLilShadowBorderColor;  // rgb 경계 색, a = 경계 폭 (Border Range)
    float4 gLilShadow;             // x Border, y Blur, z 2nd Border, w 2nd Blur
    float4 gLilShadow2;            // x Strength, y Main Strength, z Environment Strength, w Receive Shadow
    float4 gLilLight;              // x Min Limit, y Max Limit, z Monochrome, w As Unlit
    float4 gLilRimColor;           // rgb, a = 세기 (0 = 끔)
    float4 gLilRim;                // x Border, y Blur, z Fresnel Power, w Shadow Mask
    float4 gLilRim2;               // x Enable Lighting, y Main Strength
    float4 gLilBacklightColor;     // rgb, a = 세기 (0 = 끔)
    float4 gLilBacklight;          // x Border, y Blur, z Directivity, w View Strength
    float4 gLilBacklight2;         // x Main Strength
    float4 gLilOutlineColor;       // rgb, a = 두께 (cm)
    float4 gLilOutline;            // x Fix Width, y Z Bias (m), z Enable Lighting
    float4 gLilFlags;              // x 그림자 켬, y 그림자 색 텍스처, z 2 단 그림자 색 텍스처, w 그림자 (3 단계 중) 1 = 2 단 켬
};

Texture2D gLilShadowColorTex;
Texture2D gLilShadow2ndColorTex;

// lilTooningScale (경계 border 를 blur 폭으로 부드럽게 + 화면 미분으로 계단 깨짐 막기)
float LilTooning(float value, float border, float blur)
{
    const float borderMin = saturate(border - blur * 0.5f);
    const float borderMax = saturate(border + blur * 0.5f);
    return saturate((value - borderMin) / saturate(borderMax - borderMin + fwidth(value)));
}

float LilTooningRange(float value, float border, float blur, float borderRange)
{
    const float borderMin = saturate(border - blur * 0.5f - borderRange);
    const float borderMax = saturate(border + blur * 0.5f);
    return saturate((value - borderMin) / saturate(borderMax - borderMin + fwidth(value)));
}

float3 LilGray(float3 c) { return dot(c, float3(1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f)).xxx; }

// 빛: 첫 방향광 = 주 빛, 하늘 큐브 (흐린 밉) 위 · 아래 = SH 의 밝은 · 어두운 쪽 대신
void LilLight(out float3 L, out float3 lightColor, out float3 indLight)
{
    L = float3(0.0f, 1.0f, 0.0f);
    float3 mainLight = float3(0, 0, 0);
    if (gDirLightCount > 0 && LightHits(gDirLightMask[0]))
    {
        L = normalize(-gDirLights[0].Direction);
        mainLight = ToLinear(gDirLights[0].Diffuse.rgb);
    }
    uint w, h, mips;
    gCubeMap.GetDimensions(0, w, h, mips);
    const float lod = max((float)mips - 2.0f, 0.0f);
    const float3 skyUp = ToLinear(gCubeMap.SampleLevel(samLinear, float3(0.0f, 1.0f, 0.0f), lod).rgb) * gIndirect.rgb;
    const float3 skyDown = ToLinear(gCubeMap.SampleLevel(samLinear, float3(0.0f, -1.0f, 0.0f), lod).rgb) * gIndirect.rgb;
    lightColor = mainLight + max(skyUp, skyDown);
    lightColor = clamp(lightColor, gLilLight.x, gLilLight.y);
    lightColor = lerp(lightColor, LilGray(lightColor), gLilLight.z);
    lightColor = lerp(lightColor, float3(1.0f, 1.0f, 1.0f), gLilLight.w);
    indLight = saturate(min(skyUp, skyDown));
}

float4 PS_Lil(VertexOut pin, bool front : SV_IsFrontFace) : SV_Target
{
    float3 N = normalize(pin.NormalW);
    if (!front)
        N = -N;   // 양면 (뒷면은 법선을 뒤집어)
    const float3 toEye = gEyePosW - pin.PosW.xyz;
    const float distToEye = length(toEye);
    const float3 V = toEye / max(distToEye, 0.0001f);
    const float2 uv = pin.Tex * gPbr.Tiling + gPbr.Offset;

    const float4 baseSample = gPbr.UseBaseMap ? gDiffuseMap.Sample(samLinear, uv) : float4(1, 1, 1, 1);
    const float4 baseColor = baseSample * gPbr.BaseColor;
    if (gPbr.AlphaClip)
        clip(baseColor.a - gPbr.Cutoff);
    if (gPbr.UseNormalMap)
    {
        float3 ns = gNormalMap.Sample(samLinear, uv).rgb * 2.0f - 1.0f;
        ns.xy *= gPbr.NormalScale;
        N = NormalSampleToWorldSpace(normalize(ns) * 0.5f + 0.5f, N, pin.TangentW);
    }
    float3 emission = gPbr.EmissionColor.rgb;
    if (gPbr.UseEmissionMap)
        emission *= ToLinear(gEmissionMap.Sample(samLinear, uv).rgb);
    const float3 albedo = ToLinear(baseColor.rgb);

    float3 L, lightColor, indLight;
    LilLight(L, lightColor, indLight);

    // 받는 그림자 (주 방향광)
    float attenuation = 1.0f;
    if (gShaderSetting.gUseShadowMap && gPbr.ReceiveShadows)
    {
        const int cascade = SelectCascade(pin.PosW.xyz);
        attenuation = DirShadow(gDirShadowMaps[0], 0, pin.PosW.xyz, cascade, ShadowFade(pin.PosW.xyz), CascadeBlend(pin.PosW.xyz, cascade));
    }

    // ---- 그림자 (lilGetShading)
    float3 col;
    float shadowmix = 1.0f;
    if (gLilFlags.x > 0.5f)
    {
        const float lnRaw = saturate(dot(L, N) * 0.5f + 0.5f);
        const float received = lerp(1.0f, attenuation, gLilShadow2.w);
        float4 lns = float4(lnRaw * received, lnRaw * received, 1.0f, lnRaw * received);
        lns.x = LilTooning(lns.x, gLilShadow.x, gLilShadow.y);
        lns.y = LilTooning(lns.y, gLilShadow.z, gLilShadow.w);
        lns.w = LilTooningRange(lns.w, gLilShadow.x, gLilShadow.y, gLilShadowBorderColor.a);
        shadowmix = lns.x;
        lns.x = lerp(1.0f, lns.x, gLilShadow2.x);

        float4 shadowTex = gLilFlags.y > 0.5f ? gLilShadowColorTex.Sample(samLinear, uv) : float4(0, 0, 0, 0);
        float4 shadow2Tex = gLilFlags.z > 0.5f ? gLilShadow2ndColorTex.Sample(samLinear, uv) : float4(0, 0, 0, 0);
        shadowTex.rgb = ToLinear(shadowTex.rgb);
        shadow2Tex.rgb = ToLinear(shadow2Tex.rgb);
        float3 indirectCol = lerp(albedo, shadowTex.rgb, shadowTex.a) * ToLinear(gLilShadowColor.rgb);
        if (gLilFlags.w > 0.5f)
        {
            const float3 shadow2 = lerp(albedo, shadow2Tex.rgb, shadow2Tex.a) * ToLinear(gLilShadow2ndColor.rgb);
            indirectCol = lerp(indirectCol, shadow2, gLilShadow2ndColor.a - lns.y * gLilShadow2ndColor.a);
        }
        indirectCol = lerp(indirectCol, indirectCol * albedo, gLilShadow2.y);
        const float3 directCol = albedo * lightColor;
        indirectCol = indirectCol * lightColor;
        indirectCol = lerp(indirectCol, albedo, saturate(indLight * gLilShadow2.z));
        indirectCol = min(indirectCol, directCol);
        indirectCol = lerp(indirectCol, directCol, lns.w * ToLinear(gLilShadowBorderColor.rgb));
        col = lerp(indirectCol, directCol, lns.x);
    }
    else
        col = albedo * lightColor;

    // 점 · 스포트 빛 (lilToon ForwardAdd 처럼 같은 경계로)
    [loop]
    for (int pi = 0; pi < gPointLightCount; ++pi)
    {
        if (!LightHits(gPointLightMask[pi]))
            continue;
        const float3 toLight = gPointLights[pi].Position - pin.PosW.xyz;
        const float d = length(toLight);
        const float3 Lp = toLight / max(d, 0.0001f);
        const float ln = LilTooning(saturate(dot(Lp, N) * 0.5f + 0.5f), gLilShadow.x, gLilShadow.y);
        col += albedo * min(ToLinear(gPointLights[pi].Diffuse.rgb) * RangeAttenuation(d, gPointLights[pi].Range), gLilLight.y) * ln;
    }
    [loop]
    for (int si = 0; si < gSpotLightCount; ++si)
    {
        if (!LightHits(gSpotLightMask[si]))
            continue;
        const float3 toLight = gSpotLights[si].Position - pin.PosW.xyz;
        const float d = length(toLight);
        const float3 Ls = toLight / max(d, 0.0001f);
        const float cone = smoothstep(cos(radians(gSpotLights[si].Spot * 0.5f)), cos(radians(gSpotLights[si].Spot * 0.4f)), dot(-Ls, normalize(gSpotLights[si].Direction)));
        const float ln = LilTooning(saturate(dot(Ls, N) * 0.5f + 0.5f), gLilShadow.x, gLilShadow.y);
        col += albedo * min(ToLinear(gSpotLights[si].Diffuse.rgb) * RangeAttenuation(d, gSpotLights[si].Range) * cone, gLilLight.y) * ln;
    }

    // ---- 역광 (lilBacklight)
    if (gLilBacklightColor.a > 0.0f && front)
    {
        const float factor = pow(saturate(-dot(L, V) * 0.5f + 0.5f), gLilBacklight.z);
        float ln = dot(normalize(-V * gLilBacklight.w + L), N) * 0.5f + 0.5f;
        ln = LilTooning(ln, gLilBacklight.x, gLilBacklight.y);
        float3 bc = ToLinear(gLilBacklightColor.rgb);
        bc = lerp(bc, bc * albedo, gLilBacklight2.x);
        col += saturate(factor * ln) * gLilBacklightColor.a * bc * lightColor;
    }

    // ---- 림 (lilGetRim, 더하기)
    if (gLilRimColor.a > 0.0f)
    {
        float rim = pow(saturate(1.0f - abs(dot(N, V))), gLilRim.z);
        rim = LilTooning(rim, gLilRim.x, gLilRim.y);
        rim = lerp(rim, rim * shadowmix, gLilRim.w);
        float3 rc = ToLinear(gLilRimColor.rgb);
        rc = lerp(rc, rc * albedo, gLilRim2.y);
        rc = lerp(rc, rc * lightColor, gLilRim2.x);
        col += rc * rim * gLilRimColor.a;
    }

    return FinishLit(col + emission, baseColor.a, distToEye);
}

// ---------------------------------------------------------------------------
// 외곽선 (lilCalcOutlinePosition): 법선 쪽으로 두께(cm) 만큼 부풀린 뒷면. 본 패스는 깊이 EQUAL 이라 이 패스는 자체 깊이 상태
// ---------------------------------------------------------------------------
VertexOut VS_LilOutline(SkinnedVertexIn vin)
{
    VertexOut vout = VS_Skinned(vin);
    const float3 n = normalize(vout.NormalW);
    const float3 toEye = gEyePosW - vout.PosW.xyz;
    float width = gLilOutlineColor.a * 0.01f;
    width *= lerp(1.0f, saturate(length(toEye)), gLilOutline.x);   // 가까울수록 가늘게 (Fix Width)
    vout.PosW.xyz += n * width;
    vout.PosW.xyz -= normalize(toEye) * gLilOutline.y;              // Z Bias: 카메라 반대쪽으로
    vout.PosH = mul(float4(vout.PosW.xyz, 1.0f), gViewProj);
    return vout;
}

float4 PS_LilOutline(VertexOut pin) : SV_Target
{
    const float2 uv = pin.Tex * gPbr.Tiling + gPbr.Offset;
    const float4 baseSample = gPbr.UseBaseMap ? gDiffuseMap.Sample(samLinear, uv) : float4(1, 1, 1, 1);
    if (gPbr.AlphaClip)
        clip(baseSample.a * gPbr.BaseColor.a - gPbr.Cutoff);
    float3 L, lightColor, indLight;
    LilLight(L, lightColor, indLight);
    float3 c = ToLinear(gLilOutlineColor.rgb);
    c = lerp(c, c * lightColor, gLilOutline.z);
    return FinishLit(c, 1.0f, length(gEyePosW - pin.PosW.xyz));
}

RasterizerState LilOutlineCull
{
    FillMode = SOLID;
    CullMode = FRONT;
};

DepthStencilState LilOutlineDepth
{
    DepthEnable = TRUE;
    DepthWriteMask = ALL;
    DepthFunc = LESS_EQUAL;
};

technique11 LilSkinnedTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Skinned()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS_Lil()));
    }
}

technique11 LilSkinnedOutlineTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_LilOutline()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS_LilOutline()));
        SetRasterizerState(LilOutlineCull);
        SetDepthStencilState(LilOutlineDepth, 0);
    }
}
