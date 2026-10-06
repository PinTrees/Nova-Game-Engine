//=============================================================================
// 62. TerrainHeightBlit.fx — Terrain Layer 의 높이 맵을 지형 높이 배열 (Texture2DArray 의 한 슬라이스, R16F 1024²) 로 옮긴다
//  TerrainRenderer 가 레이어 높이가 바뀔 때만 (슬라이스마다 화면 덮는 삼각형 하나, 그 뒤 GenerateMips).
//  배열 하나라 지형 셰이더는 높이 맵 넷을 샘플러 하나로 읽는다 (OpenGL · GLES 의 픽셀 단계 샘플러 32 개 한도)
//=============================================================================

cbuffer cbHeightBlit
{
    float4 gBlitParams;   // x 배열 한 변 (원본이 더 크면 그만큼 흐린 밉에서 — 반짝이지 않게)
};
Texture2D gBlitSource;

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

float4 PS_Blit(BlitOut pin) : SV_Target
{
    float w, h;
    gBlitSource.GetDimensions(w, h);
    const float mip = max(log2(max(w, h) / max(gBlitParams.x, 1.0f)), 0.0f);
    return gBlitSource.SampleLevel(samBlit, pin.UV, mip).rrrr;
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
