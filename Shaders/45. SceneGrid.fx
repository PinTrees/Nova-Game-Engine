//=============================================================================
// 45. SceneGrid.fx  (에디터 Scene 뷰 바닥 격자)
//
// Unity 처럼 3D 패스에서 깊이 검사를 하며 그린다 → 물체 뒤의 격자선은 가려진다.
//  - 카메라 아래 y = 0 평면의 큰 사각형 하나 (정점 버퍼 없이 SV_VertexID)
//  - 선은 화면 미분(fwidth)으로 1 픽셀 두께, 10 칸마다 진한 선
//  - 카메라에서 멀어질수록 흐려지고, 칸이 몇 픽셀보다 작아지면(멀리/비스듬히) 가는 선을 지워 물결무늬를 막는다
//=============================================================================

cbuffer cbGrid
{
    float4x4 gViewProj;
    float4 gCameraPos;     // xyz = 카메라 위치, w = 사각형 반폭
    float4 gGridColor;     // rgb = 선 색 (감마)
    float4 gGridParams;    // x = 가는 선 알파, y = 진한 선 알파, z = 흐려지는 거리
};

struct GridOut
{
    float4 PosH : SV_POSITION;
    float3 PosW : POSITION;
};

GridOut VS(uint vid : SV_VertexID)
{
    const float2 corners[6] = { float2(-1, -1), float2(-1, 1), float2(1, 1), float2(-1, -1), float2(1, 1), float2(1, -1) };
    const float2 c = corners[vid];
    GridOut vout;
    vout.PosW = float3(floor(gCameraPos.x) + c.x * gCameraPos.w, 0.0f, floor(gCameraPos.z) + c.y * gCameraPos.w);
    vout.PosH = mul(float4(vout.PosW, 1.0f), gViewProj);
    return vout;
}

// coord 격자(한 칸 = 1)의 선 덮임 (0~1)
float GridLine(float2 coord)
{
    const float2 w = max(fwidth(coord), 1e-5f);
    const float2 g = abs(frac(coord - 0.5f) - 0.5f) / w;
    const float lineCover = 1.0f - saturate(min(g.x, g.y));
    // 한 칸이 3 픽셀보다 작아지면 서서히 지운다
    const float density = max(w.x, w.y);
    return lineCover * saturate(1.5f - density * 3.0f);
}

float4 PS(GridOut pin) : SV_Target
{
    const float d = distance(pin.PosW.xz, gCameraPos.xz);
    const float fade = saturate(1.0f - d / gGridParams.z);
    const float minor = GridLine(pin.PosW.xz) * gGridParams.x;
    const float major = GridLine(pin.PosW.xz * 0.1f) * gGridParams.y;
    const float a = max(minor, major) * fade;
    clip(a - 0.004f);
    return float4(gGridColor.rgb, a);
}

BlendState GridBlend
{
    BlendEnable[0] = TRUE;
    SrcBlend = SRC_ALPHA;
    DestBlend = INV_SRC_ALPHA;
    BlendOp = ADD;
    SrcBlendAlpha = ONE;          // Scene 뷰는 투명 배경 위에 합성되므로 알파도 쌓는다
    DestBlendAlpha = INV_SRC_ALPHA;
    BlendOpAlpha = ADD;
    RenderTargetWriteMask[0] = 0x0F;
};

DepthStencilState GridDepth
{
    DepthEnable = TRUE;
    DepthWriteMask = ZERO;
    DepthFunc = LESS_EQUAL;
};

RasterizerState GridRS
{
    CullMode = None;
    // y = 0 에 놓인 바닥 메시와 깊이가 겹쳐 깜박이지 않도록 카메라 쪽으로 살짝
    DepthBias = -16;
    SlopeScaledDepthBias = -2.0f;
};

technique11 GridTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS()));
        SetBlendState(GridBlend, float4(0.0f, 0.0f, 0.0f, 0.0f), 0xFFFFFFFF);
        SetDepthStencilState(GridDepth, 0);
        SetRasterizerState(GridRS);
    }
}
