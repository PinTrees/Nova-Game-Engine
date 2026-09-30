//=============================================================================
// 43. Particle.fx
// Unity Particle System 그리기. 입자 하나 = 인스턴스 하나 (정점 버퍼 없이 SV_VertexID 로 사각형 모서리 4개).
//  - Render Mode: 0 Billboard(카메라를 향함), 1 Stretched Billboard(속도 방향으로 늘림),
//                 2 Horizontal Billboard(바닥에 눕힘), 3 Vertical Billboard(세로로 서서 카메라를 향함)
//  - Blend: AlphaTech(알파 블렌딩), AdditiveTech(더하기). 깊이는 비교만 하고 쓰지 않는다.
//=============================================================================

cbuffer cbParticles
{
    float4x4 gViewProj;
    float3 gCamRight;
    float gSpeedScale;
    float3 gCamUp;
    float gLengthScale;
    float3 gCamPos;
    int gRenderMode;
};

Texture2D gTexture;

SamplerState samParticle
{
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = CLAMP;
    AddressV = CLAMP;
};

struct InstanceIn
{
    float3 Pos : POSITION;       // 월드
    float Rot : ROTATION;        // 라디안
    float2 Size : SIZE;
    float3 Vel : VELOCITY;       // 월드 (Stretched Billboard)
    float4 Color : COLOR;
    float4 UV : TEXCOORD0;       // u0 v0 u1 v1 (Texture Sheet Animation 칸)
    uint VertexId : SV_VertexID;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float2 Tex : TEXCOORD;
    float4 Color : COLOR;
};

VertexOut VS(InstanceIn vin)
{
    // 삼각형 띠: 0 왼쪽 아래, 1 오른쪽 아래, 2 왼쪽 위, 3 오른쪽 위
    float2 corner = float2((vin.VertexId & 1) ? 0.5f : -0.5f, (vin.VertexId & 2) ? 0.5f : -0.5f);
    float2 uv = float2(corner.x + 0.5f, 0.5f - corner.y);

    float3 right = gCamRight;
    float3 up = gCamUp;
    float2 size = vin.Size;
    bool rotate = true;
    if (gRenderMode == 1)
    {
        // 속도 방향 = 사각형의 위쪽, 옆 = 속도와 시선에 수직
        float speed = length(vin.Vel);
        float3 dir = speed > 1e-5f ? vin.Vel / speed : gCamUp;
        float3 toCam = normalize(gCamPos - vin.Pos);
        float3 side = cross(dir, toCam);
        right = length(side) > 1e-5f ? normalize(side) : gCamRight;
        up = dir;
        size.y = size.y * gLengthScale + speed * gSpeedScale;
        rotate = false;
    }
    else if (gRenderMode == 2)
    {
        right = float3(1.0f, 0.0f, 0.0f);
        up = float3(0.0f, 0.0f, 1.0f);
    }
    else if (gRenderMode == 3)
    {
        float3 flat = float3(gCamRight.x, 0.0f, gCamRight.z);
        right = length(flat) > 1e-5f ? normalize(flat) : float3(1.0f, 0.0f, 0.0f);
        up = float3(0.0f, 1.0f, 0.0f);
    }

    float2 c = corner * size;
    if (rotate)
    {
        float s, co;
        sincos(-vin.Rot, s, co);   // Unity: 양수 = 시계 방향
        c = float2(c.x * co - c.y * s, c.x * s + c.y * co);
    }
    float3 world = vin.Pos + right * c.x + up * c.y;

    VertexOut vout;
    vout.PosH = mul(float4(world, 1.0f), gViewProj);
    vout.Tex = lerp(vin.UV.xy, vin.UV.zw, uv);
    vout.Color = vin.Color;
    return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
    return gTexture.Sample(samParticle, pin.Tex) * pin.Color;
}

// 알파 채널: 아래 값을 덮어 쓰지 않고 쌓는다 (Scene 뷰는 투명 배경 위에 합성되고, Game 뷰는 1 로 유지)
BlendState AlphaBlend
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

BlendState AdditiveBlend
{
    BlendEnable[0] = TRUE;
    SrcBlend = SRC_ALPHA;
    DestBlend = ONE;
    BlendOp = ADD;
    SrcBlendAlpha = ONE;
    DestBlendAlpha = INV_SRC_ALPHA;
    BlendOpAlpha = ADD;
    RenderTargetWriteMask[0] = 0x0F;
};

DepthStencilState DepthTestNoWrite
{
    DepthEnable = TRUE;
    DepthWriteMask = ZERO;
    DepthFunc = LESS_EQUAL;
};

RasterizerState NoCull
{
    FillMode = SOLID;
    CullMode = NONE;
};

technique11 AlphaTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS()));
        SetBlendState(AlphaBlend, float4(0.0f, 0.0f, 0.0f, 0.0f), 0xFFFFFFFF);
        SetDepthStencilState(DepthTestNoWrite, 0);
        SetRasterizerState(NoCull);
    }
}

technique11 AdditiveTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS()));
        SetBlendState(AdditiveBlend, float4(0.0f, 0.0f, 0.0f, 0.0f), 0xFFFFFFFF);
        SetDepthStencilState(DepthTestNoWrite, 0);
        SetRasterizerState(NoCull);
    }
}
