//=============================================================================
// 56. ImGui.fx
// 에디터 UI (ImGui) 를 Gfx 층으로 그리는 셰이더 (ImGuiGfx — Vulkan 등 imgui_impl_dx11 · ImGuiGL 이 없는 API).
//  imgui_impl_dx11 과 같은 그림: 색 = 정점 색 × 텍스처, 알파 블렌드, 컬링 · 깊이 없음, 가위 (명령마다 잘라 그리기)
//=============================================================================

cbuffer cbImGui
{
    float4x4 gProjection;   // 화면 좌표 → 클립 (행 우선, mul(v, M))
};

Texture2D gTexture;

SamplerState samLinear
{
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = WRAP;
    AddressV = WRAP;
};

struct VertexIn
{
    float2 Pos : POSITION;
    float2 Tex : TEXCOORD;
    float4 Color : COLOR;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float2 Tex : TEXCOORD;
    float4 Color : COLOR;
};

VertexOut VS(VertexIn vin)
{
    VertexOut vout;
    vout.PosH = mul(float4(vin.Pos, 0.0f, 1.0f), gProjection);
    vout.Tex = vin.Tex;
    vout.Color = vin.Color;
    return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
    return pin.Color * gTexture.Sample(samLinear, pin.Tex);
}

BlendState ImGuiBlend
{
    BlendEnable[0] = TRUE;
    SrcBlend = SRC_ALPHA;
    DestBlend = INV_SRC_ALPHA;
    BlendOp = ADD;
    SrcBlendAlpha = ONE;
    DestBlendAlpha = INV_SRC_ALPHA;
    BlendOpAlpha = ADD;
    RenderTargetWriteMask[0] = 0x0F;
};

DepthStencilState ImGuiDepth
{
    DepthEnable = FALSE;
    DepthWriteMask = ZERO;
};

RasterizerState ImGuiRaster
{
    FillMode = SOLID;
    CullMode = NONE;
    ScissorEnable = TRUE;
    DepthClipEnable = TRUE;
};

technique11 ImGuiTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS()));
        SetBlendState(ImGuiBlend, float4(0.0f, 0.0f, 0.0f, 0.0f), 0xFFFFFFFF);
        SetDepthStencilState(ImGuiDepth, 0);
        SetRasterizerState(ImGuiRaster);
    }
}
