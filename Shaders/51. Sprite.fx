//=============================================================================
// 51. Sprite.fx
// SpriteRenderer · 2D 뼈대 렌더러의 그림 사각형 (SpriteBatch). Unity 의 Sprites-Default 와 같이 조명 없음:
//  색 = 텍스처 × 정점 색 (Color · 슬롯 색). 깊이 검사만 하고 쓰지 않는다 → 3D 물체 뒤에서는 가려지고,
//  스프라이트끼리는 그리는 순서 (Order in Layer → 카메라에서 먼 것부터) 로 겹친다.
//  Filter Mode = Point 인 그림은 SpritePointTech (도트 그림이 흐려지지 않게).
//  장면 목표는 감마 공간 (3D 셰이더도 마지막에 ToGamma) → sRGB 형식 텍스처 (PNG 에 sRGB 표시) 는 하드웨어가 선형으로 푼 값을
//  다시 감마로 (gSrgb = 1) — 화면 색 = PNG 픽셀 색 × Color (Unity 의 Sprites-Default 와 같은 결과).
//=============================================================================

cbuffer cbSprite
{
    float4x4 gViewProj;
    float4 gSrgb;   // x = 1 이면 텍스처가 sRGB 형식 (샘플 = 선형 → 감마로)
};

Texture2D gTexture;

SamplerState samLinear
{
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = CLAMP;
    AddressV = CLAMP;
};

SamplerState samPoint
{
    Filter = MIN_MAG_MIP_POINT;
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

float4 Shade(float4 t, float4 color)
{
    if (gSrgb.x > 0.5f)
        t.rgb = pow(max(t.rgb, 0.0f), 1.0f / 2.2f);
    return t * color;
}

float4 PS(VertexOut pin) : SV_Target
{
    return Shade(gTexture.Sample(samLinear, pin.Tex), pin.Color);
}

float4 PSPoint(VertexOut pin) : SV_Target
{
    return Shade(gTexture.Sample(samPoint, pin.Tex), pin.Color);
}

// 알파: 아래 값을 쌓는다 (Scene 뷰는 투명 배경 위에 합성된다 — 43. Particle.fx 와 같음)
BlendState SpriteBlend
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

DepthStencilState SpriteDepth
{
    DepthEnable = TRUE;
    DepthWriteMask = ZERO;
    DepthFunc = LESS_EQUAL;
};

RasterizerState SpriteNoCull
{
    FillMode = SOLID;
    CullMode = NONE;
};

technique11 SpriteTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS()));
        SetBlendState(SpriteBlend, float4(0.0f, 0.0f, 0.0f, 0.0f), 0xFFFFFFFF);
        SetDepthStencilState(SpriteDepth, 0);
        SetRasterizerState(SpriteNoCull);
    }
}

technique11 SpritePointTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PSPoint()));
        SetBlendState(SpriteBlend, float4(0.0f, 0.0f, 0.0f, 0.0f), 0xFFFFFFFF);
        SetDepthStencilState(SpriteDepth, 0);
        SetRasterizerState(SpriteNoCull);
    }
}
