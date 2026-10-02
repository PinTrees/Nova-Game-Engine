//=============================================================================
// 52. Decal.fx — 엔진 데칼 (Decal Projector 의 재질이 Lit / Unlit 일 때)
//  재질 값: Base Map × Base Color (알파 = 불투명도), Normal Map (Scale), Metallic · Smoothness, Emission
//=============================================================================
#define NOVA_NO_ENGINE_TECHNIQUES   // 32 의 함수만 (기법까지 넣으면 OpenGL 변환이 몇 초 걸린다)
#include "32. InstancedBasic.fx"
#include "53. DecalCommon.fx"

cbuffer cbDecalMaterial
{
    float4 gDecalColor;      // Base Color (감마)
    float4 gDecalSurface;    // x Metallic, y Smoothness, z Normal Scale
    float4 gDecalEmission;   // 선형 HDR
    float4 gDecalMaps;       // x 1 = Base Map, y 1 = Normal Map, z 1 = Unlit
};
Texture2D gDecalBaseMap;
Texture2D gDecalNormalMap;

SamplerState samDecal
{
    Filter = ANISOTROPIC;
    MaxAnisotropy = 8;
    AddressU = WRAP;
    AddressV = WRAP;
};

float4 PS_Decal(DecalVOut pin) : SV_Target
{
    DecalSample d = DecalReconstruct(pin.PosH);
    float4 base = gDecalColor;
    if (gDecalMaps.x > 0.5)
        base *= gDecalBaseMap.Sample(samDecal, d.UV);
    const float alpha = base.a * d.Fade;
    clip(alpha - 0.002);
    if (gDecalMaps.z > 0.5)
        return float4(ToGamma(ToLinear(base.rgb) + gDecalEmission.rgb), alpha);
    float3 N = d.NormalW;
    if (gDecalMaps.y > 0.5)
    {
        float3 ns = gDecalNormalMap.Sample(samDecal, d.UV).rgb * 2.0 - 1.0;
        ns.xy *= gDecalSurface.z;
        N = DecalNormal(normalize(ns), d.NormalW);
    }
    LitSurface surf;
    surf.Albedo = ToLinear(saturate(base.rgb));
    surf.Metallic = saturate(gDecalSurface.x);
    surf.Smoothness = saturate(gDecalSurface.y);
    surf.Occlusion = 1.0;
    surf.Emission = max(gDecalEmission.rgb, 0);
    surf.Transmission = float3(0, 0, 0);
    surf.Highlights = true;
    surf.Reflections = true;
    surf.ReceiveShadows = true;
    return DecalFinish(surf, d, N, alpha);
}

technique11 DecalTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Decal()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS_Decal()));
    }
}
