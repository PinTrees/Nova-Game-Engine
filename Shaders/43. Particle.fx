//=============================================================================
// 43. Particle.fx
// Unity Particle System 그리기. 입자 하나 = 인스턴스 하나 (정점 버퍼 없이 SV_VertexID 로 사각형 모서리 4개).
//  - Render Mode: 0 Billboard(카메라를 향함), 1 Stretched Billboard(속도 방향으로 늘림),
//                 2 Horizontal Billboard(바닥에 눕힘), 3 Vertical Billboard(세로로 서서 카메라를 향함)
//  - Blend: AlphaTech(알파 블렌딩), AdditiveTech(더하기). 깊이는 비교만 하고 쓰지 않는다.
//  - Lit(Renderer > Lighting): 해(방향광 0) 반 램버트 + 하늘 환경광 → 밤에 연기가 빛나 보이지 않게
//  - Soft Particles: 장면 깊이(읽기 전용 깊이 + SRV)와의 거리로 알파를 줄여 바닥·벽과 겹치는 딱딱한 경계를 없앤다
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
    float4 gDepthParams;    // x, y = 투영 _33, _43 (깊이 → 시야 거리), z = Soft 거리 (0 = 끔), w = 장면 깊이 있음 (1)
    float4 gSunDir;         // xyz = 해가 비추는 방향 (방향광 Direction), w = Lit (1)
    float4 gSunColor;       // rgb = 해 색·세기 (방향광 Diffuse)
    float4 gIndirect;       // rgb = 환경광 배율 (Volume 의 Indirect Lighting), w = 하늘 있음 (1)
};

Texture2D gTexture;
Texture2D gSceneDepth;      // 뷰 깊이 (Soft Particles)
TextureCube gSky;           // 하늘 (Lit 의 환경광 = 가장 작은 밉)

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
    float3 PosW : TEXCOORD1;
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
    vout.PosW = world;
    return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
    float4 c = gTexture.Sample(samParticle, pin.Tex) * pin.Color;
    // Lit: 카메라를 향한 면 기준 반 램버트(얇은 연기처럼 뒤에서 오는 빛도 조금) + 하늘 환경광
    if (gSunDir.w > 0.5f)
    {
        const float3 n = normalize(gCamPos - pin.PosW);
        const float hl = dot(n, -gSunDir.xyz) * 0.5f + 0.5f;
        float3 ambient = float3(0.35f, 0.38f, 0.42f);
        if (gIndirect.w > 0.5f)
            ambient = gSky.SampleLevel(samParticle, float3(0.0f, 1.0f, 0.0f), 16.0f).rgb;   // 하늘의 평균 색 (가장 작은 밉)
        const float3 light = ambient * gIndirect.rgb + gSunColor.rgb * hl;
        c.rgb *= min(light, 2.0f);
    }
    // Soft Particles: 장면 표면까지의 시야 거리 차이로 알파 (Unity 의 Soft Particles Far Fade 처럼)
    if (gDepthParams.z > 0.0f && gDepthParams.w > 0.5f)
    {
        const float d = gSceneDepth.Load(int3(pin.PosH.xy, 0)).r;
        // 깊이 → 시야 거리 (원근 투영, 행 벡터): d = _33 + _43 / z → z = _43 / (d - _33). _33 > 1 이고 d <= 1 이라 분모는 늘 음수
        const float sceneZ = gDepthParams.y / min(d - gDepthParams.x, -1e-7f);
        c.a *= saturate((sceneZ - pin.PosH.w) / gDepthParams.z);
    }
    return c;
}

// Trails: CPU 가 만든 월드 공간 띠 (정점 = 위치, UV, 색)
struct TrailIn
{
    float3 PosW : POSITION;
    float2 Tex : TEXCOORD;
    float4 Color : COLOR;
};

VertexOut TrailVS(TrailIn vin)
{
    VertexOut vout;
    vout.PosH = mul(float4(vin.PosW, 1.0f), gViewProj);
    vout.Tex = vin.Tex;
    vout.Color = vin.Color;
    vout.PosW = vin.PosW;
    return vout;
}

// Line · Trail Renderer: 길이 방향 (U) 을 반복 (Texture Mode = Tile · Repeat Per Segment), 빛 · Soft 없음 (Unity 의 Default-Line = Unlit)
SamplerState samLine
{
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = WRAP;
    AddressV = CLAMP;
};

float4 LinePS(VertexOut pin) : SV_Target
{
    return gTexture.Sample(samLine, pin.Tex) * pin.Color;
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

technique11 TrailAlphaTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, TrailVS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS()));
        SetBlendState(AlphaBlend, float4(0.0f, 0.0f, 0.0f, 0.0f), 0xFFFFFFFF);
        SetDepthStencilState(DepthTestNoWrite, 0);
        SetRasterizerState(NoCull);
    }
}

technique11 TrailAdditiveTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, TrailVS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS()));
        SetBlendState(AdditiveBlend, float4(0.0f, 0.0f, 0.0f, 0.0f), 0xFFFFFFFF);
        SetDepthStencilState(DepthTestNoWrite, 0);
        SetRasterizerState(NoCull);
    }
}

technique11 LineAlphaTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, TrailVS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, LinePS()));
        SetBlendState(AlphaBlend, float4(0.0f, 0.0f, 0.0f, 0.0f), 0xFFFFFFFF);
        SetDepthStencilState(DepthTestNoWrite, 0);
        SetRasterizerState(NoCull);
    }
}

technique11 LineAdditiveTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, TrailVS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, LinePS()));
        SetBlendState(AdditiveBlend, float4(0.0f, 0.0f, 0.0f, 0.0f), 0xFFFFFFFF);
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
