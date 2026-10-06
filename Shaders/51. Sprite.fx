//=============================================================================
// 51. Sprite.fx
// SpriteRenderer · 2D 뼈대 렌더러의 그림 사각형 (SpriteBatch). Unity 의 Sprites-Default 와 같이 조명 없음:
//  색 = 텍스처 × 정점 색 (Color · 슬롯 색). 깊이 검사만 하고 쓰지 않는다 → 3D 물체 뒤에서는 가려지고,
//  스프라이트끼리는 그리는 순서 (Order in Layer → 카메라에서 먼 것부터) 로 겹친다.
//  Filter Mode = Point 인 그림은 SpritePointTech (도트 그림이 흐려지지 않게).
//  장면 목표는 감마 공간 (3D 셰이더도 마지막에 ToGamma) → sRGB 형식 텍스처 (PNG 에 sRGB 표시) 는 하드웨어가 선형으로 푼 값을
//  다시 감마로 (gSrgb = 1) — 화면 색 = PNG 픽셀 색 × Color (Unity 의 Sprites-Default 와 같은 결과).
//  2D 빛 (씬에 Light 2D 가 있을 때, Sprite-Lit-Default): 색 × 빛의 합 (Global = 그대로, Spot = 반지름 · 각도 · 노멀 맵 · 그림자)
//   그림자 = 빛마다 화면 크기 텍스처의 한 채널 (gShadow0 = 빛 0~3, gShadow1 = 4~7) — Shadow*Tech 이 가림 모양을 MAX 로 그린다
//=============================================================================

#define MAX_LIGHTS2D 32

cbuffer cbLights2D
{
    float4 gLight2DInfo;                    // x = 빛 수, y = 1 이면 이 그림에 노멀 맵, z = 노멀 맵이 sRGB 형식
    float4 gScreen;                         // xy = 1 / 화면 크기 (그림자 텍스처 좌표)
    float4 gLightPosRadius[MAX_LIGHTS2D];   // xy = 위치, z = 안 반지름, w = 바깥 반지름 (< 0 = Global)
    float4 gLightColor[MAX_LIGHTS2D];       // rgb = 색 × 세기, a = Falloff Strength
    float4 gLightDir[MAX_LIGHTS2D];         // xy = 비추는 방향, z = cos(안 각 / 2), w = cos(바깥 각 / 2) (원 = -2)
    float4 gLightExtra[MAX_LIGHTS2D];       // x = 노멀 맵 거리 (빛의 높이), y = 그림자 채널 (-1 = 없음, 0 ~ 7)
};

Texture2D gNormalMap;
Texture2D gShadow0;
Texture2D gShadow1;

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

struct LitOut
{
    float4 PosH : SV_POSITION;
    float2 Tex : TEXCOORD0;
    float4 Color : COLOR;
    float3 PosW : TEXCOORD1;
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

// ---------------------------------------------------------------- 2D 빛
LitOut VSLit(VertexIn vin)
{
    LitOut vout;
    vout.PosH = mul(float4(vin.PosW, 1.0f), gViewProj);
    vout.Tex = vin.Tex;
    vout.Color = vin.Color;
    vout.PosW = vin.PosW;
    return vout;
}

// 이 점의 법선 (카메라 쪽 = -Z). 노멀 맵이 있으면 화면 미분으로 접선 틀을 만든다 (정점에 접선이 없어도)
float3 SpriteNormal(float3 posW, float2 uv)
{
    float3 dp1 = ddx(posW), dp2 = ddy(posW);
    float2 duv1 = ddx(uv), duv2 = ddy(uv);
    float3 ng = cross(dp1, dp2);
    ng = dot(ng, ng) > 1e-20f ? normalize(ng) : float3(0.0f, 0.0f, -1.0f);
    if (ng.z > 0.0f)
        ng = -ng;
    if (gLight2DInfo.y < 0.5f)
        return ng;
    float3 n = gNormalMap.Sample(samLinear, uv).xyz;
    if (gLight2DInfo.z > 0.5f)
        n = pow(max(n, 0.0f), 1.0f / 2.2f);
    n = n * 2.0f - 1.0f;
    n.y = -n.y;   // 그림 위 = v 가 작아지는 쪽 (노멀 맵 초록 = 위)
    float3 dp2perp = cross(dp2, ng);
    float3 dp1perp = cross(ng, dp1);
    float3 t = dp2perp * duv1.x + dp1perp * duv2.x;
    float3 b = dp2perp * duv1.y + dp1perp * duv2.y;
    float m = max(dot(t, t), dot(b, b));
    if (m < 1e-20f)
        return ng;
    float s = rsqrt(m);
    float3 nw = t * s * n.x + b * s * n.y + ng * n.z;
    return normalize(nw);
}

float3 Lights2D(float3 posW, float2 uv, float2 screenUV)
{
    float3 n = SpriteNormal(posW, uv);
    bool normalMapped = gLight2DInfo.y > 0.5f;
    float4 shadow0 = gShadow0.SampleLevel(samPoint, screenUV, 0);
    float4 shadow1 = gShadow1.SampleLevel(samPoint, screenUV, 0);
    float3 sum = float3(0.0f, 0.0f, 0.0f);
    int count = (int)gLight2DInfo.x;
    [loop]
    for (int i = 0; i < count; ++i)
    {
        float4 pr = gLightPosRadius[i];
        float4 col = gLightColor[i];
        if (pr.w < 0.0f)
        {
            sum += col.rgb;   // Global
            continue;
        }
        float2 d = posW.xy - pr.xy;
        float dist = length(d);
        if (dist >= pr.w)
            continue;
        float t = saturate((dist - pr.z) / max(pr.w - pr.z, 1e-4f));
        float att = pow(1.0f - t, 1.0f + col.a * 2.0f);   // Falloff Strength 0 = 곧게, 1 = 제곱의 세제곱 (빨리 어두워짐)
        float4 dir = gLightDir[i];
        if (dir.w > -1.5f)
        {
            float c = dist > 1e-4f ? dot(d / dist, dir.xy) : 1.0f;
            att *= saturate((c - dir.w) / max(dir.z - dir.w, 1e-4f));
        }
        float4 extra = gLightExtra[i];
        if (normalMapped)
        {
            float3 l = normalize(float3(-d, -extra.x));
            att *= saturate(dot(n, l));
        }
        if (extra.y >= 0.0f)
        {
            int ch = (int)extra.y;
            float4 mask = float4(ch == 0 || ch == 4, ch == 1 || ch == 5, ch == 2 || ch == 6, ch == 3 || ch == 7);
            att *= 1.0f - dot(ch < 4 ? shadow0 : shadow1, mask);
        }
        sum += col.rgb * att;
    }
    return sum;
}

float4 ShadeLit(float4 t, LitOut pin)
{
    float4 c = Shade(t, pin.Color);
    c.rgb *= Lights2D(pin.PosW, pin.Tex, pin.PosH.xy * gScreen.xy);
    return c;
}

float4 PSLit(LitOut pin) : SV_Target
{
    return ShadeLit(gTexture.Sample(samLinear, pin.Tex), pin);
}

float4 PSLitPoint(LitOut pin) : SV_Target
{
    return ShadeLit(gTexture.Sample(samPoint, pin.Tex), pin);
}

// 그림자 모양 (가리는 정도 = 정점 색 a): 빛마다 한 채널에 MAX 로 쌓는다
float4 PSShadow(VertexOut pin) : SV_Target
{
    return pin.Color.aaaa;
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

technique11 SpriteLitTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VSLit()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PSLit()));
        SetBlendState(SpriteBlend, float4(0.0f, 0.0f, 0.0f, 0.0f), 0xFFFFFFFF);
        SetDepthStencilState(SpriteDepth, 0);
        SetRasterizerState(SpriteNoCull);
    }
}

technique11 SpriteLitPointTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VSLit()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PSLitPoint()));
        SetBlendState(SpriteBlend, float4(0.0f, 0.0f, 0.0f, 0.0f), 0xFFFFFFFF);
        SetDepthStencilState(SpriteDepth, 0);
        SetRasterizerState(SpriteNoCull);
    }
}

// ---------------------------------------------------------------- 그림자 (채널마다)
DepthStencilState ShadowNoDepth
{
    DepthEnable = FALSE;
    DepthWriteMask = ZERO;
};

BlendState ShadowMaxR
{
    BlendEnable[0] = TRUE;
    SrcBlend = ONE;
    DestBlend = ONE;
    BlendOp = MAX;
    SrcBlendAlpha = ONE;
    DestBlendAlpha = ONE;
    BlendOpAlpha = MAX;
    RenderTargetWriteMask[0] = 0x01;
};

technique11 ShadowRTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PSShadow()));
        SetBlendState(ShadowMaxR, float4(0.0f, 0.0f, 0.0f, 0.0f), 0xFFFFFFFF);
        SetDepthStencilState(ShadowNoDepth, 0);
        SetRasterizerState(SpriteNoCull);
    }
}

BlendState ShadowMaxG
{
    BlendEnable[0] = TRUE;
    SrcBlend = ONE;
    DestBlend = ONE;
    BlendOp = MAX;
    SrcBlendAlpha = ONE;
    DestBlendAlpha = ONE;
    BlendOpAlpha = MAX;
    RenderTargetWriteMask[0] = 0x02;
};

technique11 ShadowGTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PSShadow()));
        SetBlendState(ShadowMaxG, float4(0.0f, 0.0f, 0.0f, 0.0f), 0xFFFFFFFF);
        SetDepthStencilState(ShadowNoDepth, 0);
        SetRasterizerState(SpriteNoCull);
    }
}

BlendState ShadowMaxB
{
    BlendEnable[0] = TRUE;
    SrcBlend = ONE;
    DestBlend = ONE;
    BlendOp = MAX;
    SrcBlendAlpha = ONE;
    DestBlendAlpha = ONE;
    BlendOpAlpha = MAX;
    RenderTargetWriteMask[0] = 0x04;
};

technique11 ShadowBTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PSShadow()));
        SetBlendState(ShadowMaxB, float4(0.0f, 0.0f, 0.0f, 0.0f), 0xFFFFFFFF);
        SetDepthStencilState(ShadowNoDepth, 0);
        SetRasterizerState(SpriteNoCull);
    }
}

BlendState ShadowMaxA
{
    BlendEnable[0] = TRUE;
    SrcBlend = ONE;
    DestBlend = ONE;
    BlendOp = MAX;
    SrcBlendAlpha = ONE;
    DestBlendAlpha = ONE;
    BlendOpAlpha = MAX;
    RenderTargetWriteMask[0] = 0x08;
};

technique11 ShadowATech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PSShadow()));
        SetBlendState(ShadowMaxA, float4(0.0f, 0.0f, 0.0f, 0.0f), 0xFFFFFFFF);
        SetDepthStencilState(ShadowNoDepth, 0);
        SetRasterizerState(SpriteNoCull);
    }
}
