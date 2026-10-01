//=============================================================================
// 50. Atmosphere.fx  (NOVA 대기·안개 화면 전체 패스 - AtmospherePass.cpp)
//  불투명 물체·하늘을 그린 뒤, 장면 색 사본 + 깊이에서 월드 위치를 되살려 대기 원근·높이 안개를 입힌다 (49. AtmosphereCommon.fx).
//  물은 자기 셰이더에서 같은 함수로 안개를 입힌다.
//=============================================================================
#include "49. AtmosphereCommon.fx"

cbuffer cbAtmospherePass
{
    float4x4 gAtmInvViewProj;
    float4 gAtmViewport;    // x, y = 왼쪽 위, z, w = 폭, 높이
};

Texture2D gAtmSceneColor;          // 장면 색 사본 (감마)
Texture2D<float> gAtmDepth;        // 장면 깊이 (0~1)

struct AtmVSOut
{
    float4 PosH : SV_POSITION;
};

AtmVSOut AtmVS(uint vid : SV_VertexID)
{
    AtmVSOut o;
    const float2 uv = float2((vid << 1) & 2, vid & 2);
    o.PosH = float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return o;
}

float4 AtmPS(AtmVSOut pin) : SV_Target
{
    const int2 pix = int2(pin.PosH.xy);
    const float4 src = gAtmSceneColor.Load(int3(pix, 0));
    const float z = gAtmDepth.Load(int3(pix, 0));
    const bool sky = z >= 0.99999f;
    if (sky && src.a < 0.5f)
        return src;   // Scene 뷰의 빈 배경 (하늘 끔): 뒤의 그라디언트가 비치게 그대로
    const float2 ndc = float2((pin.PosH.x - gAtmViewport.x) / gAtmViewport.z * 2.0f - 1.0f, 1.0f - (pin.PosH.y - gAtmViewport.y) / gAtmViewport.w * 2.0f);
    float4 wp = mul(float4(ndc, sky ? 0.5f : z, 1.0f), gAtmInvViewProj);
    float3 posW = wp.xyz / wp.w;
    if (sky)
        posW = gAtmEye.xyz + normalize(posW - gAtmEye.xyz) * gAtmFog2.x;
    const float3 c = AtmosphereApply(AtmToLinear(src.rgb), posW, sky);
    return float4(AtmToGamma(c), src.a);
}

DepthStencilState AtmNoDepth
{
    DepthEnable = FALSE;
    DepthWriteMask = ZERO;
};

RasterizerState AtmNoCull
{
    CullMode = None;
};

BlendState AtmNoBlend
{
    BlendEnable[0] = FALSE;
};

technique11 AtmosphereTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, AtmVS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, AtmPS()));
        SetDepthStencilState(AtmNoDepth, 0);
        SetRasterizerState(AtmNoCull);
        SetBlendState(AtmNoBlend, float4(0, 0, 0, 0), 0xFFFFFFFF);
    }
}
