//=============================================================================
// 28. SsaoBlur.fx — SSAO 의 가장자리를 지키는 흐림 (가로 · 세로)
//  AO 계산 크기의 정수 좌표로 (반 해상도면 노멀 · 깊이는 2 x 2 의 왼쪽 위 — 28. Ssao 와 같은 대표 픽셀).
//  노멀이 다르거나 깊이가 깊이의 5 % 넘게 다르면 섞지 않는다 (예전: 선형으로 읽은 노멀 · 깊이 + 고정 0.2 m —
//  윤곽에서 섞이고, 멀리 비스듬한 면은 흐려지지 않았다)
//=============================================================================
cbuffer cbPerFrame
{
    float gTexelWidth;
    float gTexelHeight;
    float4 gBlurSize = float4(1.0f, 1.0f, 1.0f, 0.0f);   // xy AO 계산 크기 (텍셀), z 전체 해상도 픽셀 / 계산 텍셀 (1 · 2)
};

cbuffer cbSettings
{
    float gWeights[11] =
    {
        0.05f, 0.05f, 0.1f, 0.1f, 0.1f, 0.2f, 0.1f, 0.1f, 0.1f, 0.05f, 0.05f
    };
};

static const int gBlurRadius = 5;

Texture2D gNormalDepthMap;
Texture2D gInputImage;

struct VertexIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 Tex : TEXCOORD;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float2 Tex : TEXCOORD;
};

VertexOut VS(VertexIn vin)
{
    VertexOut vout;
    vout.PosH = float4(vin.PosL, 1.0f);   // 이미 NDC
    vout.Tex = vin.Tex;
    return vout;
}

float4 NormalDepthAt(int2 aoPixel)
{
    return gNormalDepthMap.Load(int3(aoPixel * (int)gBlurSize.z, 0));
}

float4 PS(VertexOut pin, uniform bool gHorizontalBlur) : SV_Target
{
    int2 pix = int2(pin.PosH.xy);
    int2 dir = gHorizontalBlur ? int2(1, 0) : int2(0, 1);
    int2 last = int2(gBlurSize.xy) - 1;
    float4 center = NormalDepthAt(pix);

    float sum = gWeights[gBlurRadius] * gInputImage.Load(int3(pix, 0)).r;
    float totalWeight = gWeights[gBlurRadius];
    [unroll]
    for (int i = -gBlurRadius; i <= gBlurRadius; ++i)
    {
        if (i == 0)
            continue;
        int2 q = clamp(pix + dir * i, int2(0, 0), last);
        float4 nd = NormalDepthAt(q);
        // 다른 면 (노멀 · 깊이가 다름) 은 섞지 않는다
        if (dot(nd.xyz, center.xyz) >= 0.8f && abs(nd.w - center.w) <= 0.05f * center.w + 0.02f)
        {
            float w = gWeights[i + gBlurRadius];
            sum += w * gInputImage.Load(int3(q, 0)).r;
            totalWeight += w;
        }
    }
    return sum / totalWeight;
}

technique11 HorzBlur
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS(true)));
    }
}

technique11 VertBlur
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS(false)));
    }
}
