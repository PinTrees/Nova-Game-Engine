//=============================================================================
// 65. VirtualTexture.fx — Streaming Virtual Texturing 피드백 (VirtualTexturing.cpp, docs/VIRTUAL_TEXTURING.md)
//  가상 텍스처를 쓰는 Mesh Renderer 를 화면의 1/8 크기로 그려, 픽셀마다 필요한 페이지 (텍스처 번호, 밉, 페이지 x, y) 를 쓴다.
//  밉 = 32. InstancedBasic.fx 의 SampleVirtual 과 같은 식 (화면 미분) — 1/8 크기라 미분이 8 배 → gFeedback.x 로 되돌린다.
//  깊이 버퍼 대신 깊이 프리패스의 뷰 깊이와 비교한다 (가려진 면은 요청하지 않는다 — 63. MotionVectors.fx 와 같은 방법)
//=============================================================================
cbuffer cbVT
{
    float4x4 gWorld;
    float4x4 gViewProj;
    float4x4 gView;
    float4 gTexST;       // xy 타일링, zw 오프셋 (재질의 Tiling · Offset)
    float4 gVTInfo;      // x 가상 폭, y 높이, z 밉 수, w 텍스처 번호 (1 ~ 255)
    float4 gFeedback;    // x 밉 치우침 (−log2 크기 비율), y 1 = 깊이 비교, zw 지터 (클립 공간 — 프레임마다 다른 칸)
    float4 gDepthSize;   // xy 깊이 프리패스 크기, zw 피드백 크기
};

Texture2D gNormalDepth;   // 깊이 프리패스: w = 뷰 깊이 (빈 곳 1e5)

struct VIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 Tex : TEXCOORD;
    float4 TangentL : TANGENT;
};

struct VOut
{
    float4 PosH : SV_POSITION;
    float2 Tex : TEXCOORD0;
    float ViewZ : TEXCOORD1;
};

VOut VS(VIn vin)
{
    VOut o;
    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
    o.PosH = mul(posW, gViewProj);
    o.PosH.xy += gFeedback.zw * o.PosH.w;   // 프레임마다 1/8 칸 안의 다른 자리를 본다 (빠진 페이지가 없게)
    o.Tex = vin.Tex * gTexST.xy + gTexST.zw;
    o.ViewZ = mul(posW, gView).z;
    return o;
}

float4 PS(VOut pin) : SV_Target
{
    if (gFeedback.y > 0.5f)
    {
        // 피드백 픽셀 → 프리패스 픽셀 (같은 화면 자리)
        int2 p = int2(pin.PosH.xy * gDepthSize.xy / gDepthSize.zw);
        float zPre = gNormalDepth.Load(int3(p, 0)).w;
        if (abs(pin.ViewZ - zPre) > 0.05f + 0.02f * zPre)
            discard;
    }
    float2 texel = pin.Tex * gVTInfo.xy;
    float2 dx = ddx(texel), dy = ddy(texel);
    float mip = 0.5f * log2(max(max(dot(dx, dx), dot(dy, dy)), 1e-8f)) + gFeedback.x;
    float m = clamp(floor(mip), 0.0f, gVTInfo.z - 1.0f);
    float2 pages = max(floor(gVTInfo.xy / 128.0f / exp2(m)), 1.0f);
    float2 page = floor(frac(pin.Tex) * pages);
    return float4(gVTInfo.w, m, page.x, page.y) / 255.0f;
}

technique11 FeedbackTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS()));
    }
}
