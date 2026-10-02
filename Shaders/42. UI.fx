//=============================================================================
// 42. UI.fx
// Unity UGUI(Canvas) 그리기. 정점 = 월드(캔버스 픽셀) 위치 + UV + 색(감마 공간, 곱하기).
//  - Game 뷰: gViewProj = 화면 픽셀 → NDC 직교 투영 (왼쪽 아래 (0,0), 오른쪽 위 (W,H))
//  - Scene 뷰: gViewProj = 에디터 카메라 View * Proj (캔버스가 월드에 1 픽셀 = 1 단위로 놓인 것처럼)
// 색 없는 Image 는 1x1 흰 텍스처. 깊이 없음, 알파 블렌딩.
// 글자 (gSdfParams.x = 1): TextMeshPro 와 같은 SDF — 아틀라스 R = 거리장 (0.5 = 가장자리, 클수록 안쪽).
//  화면 미분(fwidth)으로 1 픽셀 폭 경계를 다시 만들어 어떤 크기 · 회전에서도 선명. Outline · Underlay · Face Dilate 는 같은 거리장에서.
//=============================================================================

cbuffer cbUI
{
    float4x4 gViewProj;
};

// SDF 글자 재질 (UIRenderer::TextMaterial)
cbuffer cbText
{
    float4 gSdfParams;       // x 켜짐, y Face Dilate, z Softness, w Outline Width (모두 거리장 단위)
    float4 gOutlineColor;
    float4 gUnderlayColor;   // a = 0 이면 없음
    float4 gUnderlayParams;  // xy UV 오프셋, z Softness, w Dilate
    float4 gUnderlayParams2; // (예약)
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
    float4 tex = gTexture.Sample(samUI, pin.Tex);
    if (gSdfParams.x < 0.5f)
        return tex * pin.Color;

    // ---- SDF 글자 ----
    float d = tex.r;
    float aa = max(fwidth(d), 1e-4f) * 0.75f + gSdfParams.z;   // 화면 1 픽셀 + Softness
    float faceEdge = 0.5f - gSdfParams.y;
    float faceA = smoothstep(faceEdge - aa, faceEdge + aa, d);
    float4 col = pin.Color;
    if (gSdfParams.w > 0.0f)
    {
        // Outline: 가장자리 바깥쪽 띠 (글자 색 → 외곽선 색)
        float outEdge = max(faceEdge - gSdfParams.w, 0.002f);
        float outA = smoothstep(outEdge - aa, outEdge + aa, d);
        float4 oc = float4(gOutlineColor.rgb, gOutlineColor.a * pin.Color.a);
        col = lerp(oc, pin.Color, faceA);
        col.a *= outA;
    }
    else
    {
        col.a *= faceA;
    }
    if (gUnderlayColor.a > 0.0f)
    {
        // Underlay (그림자): 밀린 자리의 거리장 → 글자 아래에 깐다
        float du = gTexture.Sample(samUI, pin.Tex - gUnderlayParams.xy).r;
        float uEdge = max(0.5f - gUnderlayParams.w - gSdfParams.w, 0.002f);
        float us = aa + gUnderlayParams.z;
        float ua = smoothstep(uEdge - us, uEdge + us, du) * gUnderlayColor.a * pin.Color.a;
        float a = col.a + ua * (1.0f - col.a);
        float3 rgb = (col.rgb * col.a + gUnderlayColor.rgb * ua * (1.0f - col.a)) / max(a, 1e-4f);
        col = float4(rgb, a);
    }
    return col;
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
    ScissorEnable = TRUE;   // Mask / RectMask2D (잘라내지 않을 때는 화면 전체)
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
