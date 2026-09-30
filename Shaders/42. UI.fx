//=============================================================================
// 42. UI.fx
// Unity UGUI(Canvas) 그리기. 정점 = 월드(캔버스 픽셀) 위치 + UV + 색(감마 공간, 곱하기).
//  - Game 뷰: gViewProj = 화면 픽셀 → NDC 직교 투영 (왼쪽 아래 (0,0), 오른쪽 위 (W,H))
//  - Scene 뷰: gViewProj = 에디터 카메라 View * Proj (캔버스가 월드에 1 픽셀 = 1 단위로 놓인 것처럼)
// 글자는 폰트 아틀라스(흰색 + 알파), 색 없는 Image 는 1x1 흰 텍스처를 쓴다. 깊이 없음, 알파 블렌딩.
//=============================================================================

cbuffer cbUI
{
    float4x4 gViewProj;
};

Texture2D gTexture;

SamplerState samUI
{
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = CLAMP;
    AddressV = CLAMP;
};

struct VertexIn
{
    float3 PosW : POSITION;
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
    vout.PosH = mul(float4(vin.PosW, 1.0f), gViewProj);
    vout.Tex = vin.Tex;
    vout.Color = vin.Color;
    return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
    return gTexture.Sample(samUI, pin.Tex) * pin.Color;
}

BlendState UIBlend
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

DepthStencilState UINoDepth
{
    DepthEnable = FALSE;
    DepthWriteMask = ZERO;
};

// Scene 뷰: 씬 깊이로 가려지게 (깊이는 쓰지 않음)
DepthStencilState UIDepthTest
{
    DepthEnable = TRUE;
    DepthWriteMask = ZERO;
    DepthFunc = LESS_EQUAL;
};

RasterizerState UINoCull
{
    FillMode = SOLID;
    CullMode = NONE;
    ScissorEnable = FALSE;
};

technique11 UITech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS()));
        SetBlendState(UIBlend, float4(0.0f, 0.0f, 0.0f, 0.0f), 0xFFFFFFFF);
        SetDepthStencilState(UINoDepth, 0);
        SetRasterizerState(UINoCull);
    }
}

technique11 UISceneTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS()));
        SetBlendState(UIBlend, float4(0.0f, 0.0f, 0.0f, 0.0f), 0xFFFFFFFF);
        SetDepthStencilState(UIDepthTest, 0);
        SetRasterizerState(UINoCull);
    }
}
