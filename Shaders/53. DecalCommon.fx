//=============================================================================
// 53. DecalCommon.fx — 화면 공간 데칼 공용 (엔진 데칼 52. Decal.fx · Shader Graph 의 Decal 그래프)
//  "32. InstancedBasic.fx" 다음에 포함한다 (ShadeLit · FinishLit · gEyePosW · gViewProjTex).
//  데칼 상자 (단위 상자 -0.5..0.5) 의 뒷면을 그리고, 픽셀마다 깊이 프리패스 (뷰 노멀 + 뷰 깊이) 에서
//  표면의 월드 위치 · 노멀을 되살려 상자 안이면 칠한다 (Unity URP 의 Screen Space 데칼과 같은 방식)
//=============================================================================

cbuffer cbDecal
{
    float4x4 gDecalBox;         // 단위 상자 → 월드
    float4x4 gDecalWorldToBox;  // 월드 → 상자 (-0.5..0.5)
    float4x4 gDecalViewProj;
    float4x4 gDecalInvView;     // 뷰 → 월드
    float4 gDecalProj;          // x P00, y P11, z 1 = 직교
    float4 gDecalProjOffset;    // x P20, y P21 (원근 중심 이동), z P30, w P31 (직교 이동)
    float4 gDecalViewport;      // x, y (뷰포트 왼쪽 위), z 1/너비, w 1/높이
    float4 gDecalAxisX;         // 투영기의 오른쪽 (월드, 단위 벡터)
    float4 gDecalAxisY;         // 위
    float4 gDecalAxisZ;         // 투영 방향 (앞)
    float4 gDecalUV;            // xy 크기, zw 이동
    float4 gDecalFade;          // x 불투명도 (Fade Factor), y 각도 페이드 시작 cos, z 끝 cos (y <= z 이면 각도 페이드 없음), w 거리 페이드
};
Texture2D gDecalNormalDepth;    // 깊이 프리패스: 뷰 노멀 xyz + 뷰 깊이 w (하늘 = 1e5)

struct DecalVIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 Tex : TEXCOORD;
    float4 TangentL : TANGENT;
};

struct DecalVOut
{
    float4 PosH : SV_POSITION;
};

DecalVOut VS_Decal(DecalVIn vin)
{
    DecalVOut o;
    o.PosH = mul(mul(float4(vin.PosL, 1.0), gDecalBox), gDecalViewProj);
    return o;
}

struct DecalSample
{
    float3 PosW;
    float3 NormalW;     // 표면 노멀 (월드)
    float3 Local;       // 상자 안 위치 (-0.5..0.5)
    float2 UV;
    float Fade;         // 불투명도 × 각도 × 거리
    float3 ViewW;
    float Dist;
    float4 SsaoPosH;
    float4 Screen;
};

DecalSample DecalReconstruct(float4 posH)
{
    DecalSample d;
    const float2 pixel = posH.xy - gDecalViewport.xy;
    const float4 nd = gDecalNormalDepth.Load(int3(int2(pixel), 0));
    clip(9.0e4 - nd.w);   // 하늘 (아무것도 없는 곳)
    const float2 suv = pixel * gDecalViewport.zw;
    const float2 ndc = float2(suv.x * 2.0 - 1.0, 1.0 - suv.y * 2.0);
    float3 posV;
    if (gDecalProj.z > 0.5)
        posV = float3((ndc.x - gDecalProjOffset.z) / gDecalProj.x, (ndc.y - gDecalProjOffset.w) / gDecalProj.y, nd.w);
    else
        posV = float3((ndc.x - gDecalProjOffset.x) * nd.w / gDecalProj.x, (ndc.y - gDecalProjOffset.y) * nd.w / gDecalProj.y, nd.w);
    d.PosW = mul(float4(posV, 1.0), gDecalInvView).xyz;
    d.Local = mul(float4(d.PosW, 1.0), gDecalWorldToBox).xyz;
    clip(0.5 - abs(d.Local));
    d.NormalW = normalize(mul(nd.xyz, (float3x3) gDecalInvView));
    // UV: 투영기에서 볼 때 그림이 바로 서게 (위 = 축 Y)
    d.UV = float2(d.Local.x + 0.5, 0.5 - d.Local.y) * gDecalUV.xy + gDecalUV.zw;
    float fade = gDecalFade.x * gDecalFade.w;
    if (gDecalFade.y > gDecalFade.z)
    {
        // 투영기를 마주보는 면 = 1, 비스듬할수록 흐리게 (Angle Fade)
        const float facing = dot(d.NormalW, -gDecalAxisZ.xyz);
        fade *= saturate((facing - gDecalFade.z) / max(gDecalFade.y - gDecalFade.z, 1e-4));
    }
    d.Fade = fade;
    const float3 toEye = gEyePosW - d.PosW;
    d.Dist = length(toEye);
    d.ViewW = toEye / max(d.Dist, 1e-4);
    d.SsaoPosH = mul(float4(d.PosW, 1.0), gViewProjTex);
    d.Screen = float4(suv, 0, 1);
    return d;
}

// 데칼 노멀 (탄젠트 공간) → 월드: 투영기의 X · Y 축을 표면에 눕힌 틀
float3 DecalNormal(float3 nTS, float3 surfN)
{
    float3 T = gDecalAxisX.xyz - surfN * dot(gDecalAxisX.xyz, surfN);
    T = dot(T, T) > 1e-6 ? normalize(T) : normalize(cross(surfN, float3(0, 1, 0.001)));
    const float3 B = cross(T, surfN);
    return normalize(T * nTS.x + B * nTS.y + surfN * nTS.z);
}

// 빛 (엔진 URP Lit 과 같다) → 감마 + 안개. 알파는 섞기 (데칼 BS 는 RGB 만 쓴다)
float4 DecalFinish(LitSurface surf, DecalSample d, float3 N, float alpha)
{
    const float3 c = ShadeLit(surf, d.PosW, N, d.ViewW, d.SsaoPosH);
    return FinishLit(c, saturate(alpha), d.Dist);
}
