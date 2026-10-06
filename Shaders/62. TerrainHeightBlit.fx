//=============================================================================
// 62. TerrainHeightBlit.fx — Terrain Layer 의 높이 맵 · Normal Map 을 지형 레이어 배열 (Texture2DArray 의 한 슬라이스,
//  R10G10B10A2 1024²: R = 높이, G · B = 노멀 xy) 로 옮긴다
//  TerrainRenderer 가 레이어 높이 · 노멀이 바뀔 때만 (슬라이스마다 화면 덮는 삼각형 하나, 그 뒤 GenerateMips).
//  배열 하나라 지형 셰이더는 높이 · 노멀 여덟 장을 샘플러 하나로 읽는다 (OpenGL · GLES 의 픽셀 단계 샘플러 32 개 한도)
//=============================================================================

cbuffer cbHeightBlit
{
    float4 gBlitParams;   // x 배열 한 변 (원본이 더 크면 그만큼 흐린 밉에서 — 반짝이지 않게), y 1 = 높이 맵 있음, z 1 = Normal Map 있음
};
Texture2D gBlitSource;    // 높이 맵 (R)
Texture2D gBlitNormal;    // Normal Map (RG = xy, 0..1)

SamplerState samBlit
{
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = WRAP;
    AddressV = WRAP;
};

struct BlitOut
{
    float4 PosH : SV_POSITION;
    float2 UV : TEXCOORD0;
};

BlitOut VS_Blit(uint id : SV_VertexID)
{
    BlitOut o;
    o.UV = float2((id << 1) & 2, id & 2);
    o.PosH = float4(o.UV * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return o;
}

float MipFor(Texture2D tex)
{
    float w, h;
    tex.GetDimensions(w, h);
    return max(log2(max(w, h) / max(gBlitParams.x, 1.0f)), 0.0f);
}

float4 PS_Blit(BlitOut pin) : SV_Target
{
    const float h = gBlitParams.y > 0.5f ? gBlitSource.SampleLevel(samBlit, pin.UV, MipFor(gBlitSource)).r : 0.5f;
    const float2 n = gBlitParams.z > 0.5f ? gBlitNormal.SampleLevel(samBlit, pin.UV, MipFor(gBlitNormal)).rg : float2(0.5f, 0.5f);
    return float4(h, n, 1.0f);
}

technique11 BlitTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Blit()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS_Blit()));
    }
}
