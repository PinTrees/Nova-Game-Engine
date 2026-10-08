//=============================================================================
// 64. RenderingDebug.fx — Rendering Debugger 의 전체 화면 보기 (Unity URP Rendering Debugger 의 Fullscreen Debug Mode)
//  깊이 프리패스 (노멀 · 깊이) · SSAO 맵 · 모션 벡터를 뷰 전체에 그린다 (Scene · Game 뷰, 후처리 뒤 — 장면 색을 덮는다)
//  1 Depth   : 뷰 깊이 / Depth Range (가까우면 검정 → 멀면 흰색, 하늘 = 흰색)
//  2 Normals : 월드 노멀 (n * 0.5 + 0.5 — 위를 보는 바닥 = 연두), 하늘 = 검정
//  3 Ambient Occlusion : SSAO 맵 (흰색 = 열림, 검정 = 가려짐)
//  4 Motion Vectors    : 방향 = 색상환 (오른쪽 빨강 · 아래 노랑-초록 · 왼쪽 청록 · 위 파랑-보라), 밝기 = 속도 (픽셀 x Scale / 16), 멈춤 = 검정
//  7 Additional Light Count : Forward+ 클러스터의 추가 빛 수 (0 검정, 1 파랑, 4 초록, 8 노랑, 16 이상 빨강) — 그 뷰의 클러스터 (ClusteredLighting)
//=============================================================================
cbuffer cbDebug
{
    float4 gDebugParams;   // x 모드, y Depth Range (m), z Motion Vector Scale, w -
    float4 gDebugSize;     // xy 뷰 크기 (픽셀)
    float4x4 gInvView;     // 뷰 → 월드 (노멀)
};

Texture2D gNormalDepth;    // 깊이 프리패스: xyz 뷰 노멀, w 뷰 깊이 (빈 곳 1e5)
Texture2D gAo;             // SSAO 맵 (뷰 크기, r)
Texture2D gMotion;         // 모션 벡터 (uv 의 이번 − 지난)

// Forward+ 클러스터 (32. InstancedBasic 과 같은 이름 · 배치)
cbuffer cbCluster
{
    float4 gClusterParams;   // x 타일 가로, y 타일 세로, z 깊이 조각, w 빛 수
    float4 gClusterDepth;    // 조각 = log(뷰 깊이) * x + y
    float4 gClusterView;
};
Texture2D gClusterData;   // 텍셀 i → (i % 1024, i / 1024), 클러스터 표 = 4096 부터 (x 시작, y 개수)

SamplerState samPoint
{
    Filter = MIN_MAG_MIP_POINT;
    AddressU = Clamp;
    AddressV = Clamp;
};

SamplerState samLinear
{
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = Clamp;
    AddressV = Clamp;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float2 Tex : TEXCOORD0;
};

VertexOut VS_Full(uint id : SV_VertexID)
{
    VertexOut o;
    float2 t = float2((id << 1) & 2, id & 2);
    o.PosH = float4(t * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    o.Tex = t;
    return o;
}

float3 Hue(float h)
{
    float3 c = saturate(abs(frac(h + float3(0.0f, 2.0f / 3.0f, 1.0f / 3.0f)) * 6.0f - 3.0f) - 1.0f);
    return c;
}

float4 PS_Debug(VertexOut pin) : SV_Target
{
    int mode = (int)gDebugParams.x;
    float4 nd = gNormalDepth.SampleLevel(samPoint, pin.Tex, 0);
    bool sky = nd.w > 1e4f;
    if (mode == 1)
    {
        float d = sky ? 1.0f : saturate(nd.w / max(gDebugParams.y, 1e-3f));
        return float4(d, d, d, 1.0f);
    }
    if (mode == 2)
    {
        if (sky)
            return float4(0, 0, 0, 1);
        float3 n = normalize(mul(nd.xyz, (float3x3)gInvView));
        return float4(n * 0.5f + 0.5f, 1.0f);
    }
    if (mode == 3)
    {
        float a = sky ? 1.0f : gAo.SampleLevel(samLinear, pin.Tex, 0).r;
        return float4(a, a, a, 1.0f);
    }
    if (mode == 4)
    {
        float2 px = gMotion.SampleLevel(samPoint, pin.Tex, 0).xy * gDebugSize.xy;   // 화면 속도 (픽셀)
        float len = length(px);
        if (len < 1e-4f)
            return float4(0, 0, 0, 1);
        float h = atan2(px.y, px.x) / 6.2831853f;   // 화면 y 아래 + → 오른쪽 0, 아래 1/4 …
        float v = saturate(len * gDebugParams.z / 16.0f);
        return float4(Hue(frac(h + 1.0f)) * v, 1.0f);
    }
    if (mode == 7)
    {
        if (sky || gClusterParams.w < 0.5f)
            return float4(0, 0, 0, 1);
        int slice = clamp((int)floor(log(max(nd.w, 1e-3f)) * gClusterDepth.x + gClusterDepth.y), 0, (int)gClusterParams.z - 1);
        int2 tile = min(int2(pin.Tex * gClusterParams.xy), int2(gClusterParams.xy) - 1);
        uint cluster = 4096u + (uint)(tile.x + tile.y * (int)gClusterParams.x + slice * (int)(gClusterParams.x * gClusterParams.y));
        uint n = (uint)gClusterData.Load(int3((int)(cluster & 1023u), (int)(cluster >> 10), 0)).y;
        if (n == 0)
            return float4(0, 0, 0, 1);
        float t = saturate(log2((float)n) / 4.0f);   // 1 → 0, 16 → 1
        float3 c = t < 0.5f ? lerp(float3(0.1f, 0.2f, 1.0f), float3(0.1f, 0.9f, 0.2f), t * 2.0f) : lerp(float3(0.1f, 0.9f, 0.2f), float3(1.0f, 0.1f, 0.05f), t * 2.0f - 1.0f);
        return float4(c, 1.0f);
    }
    return float4(1, 0, 1, 1);
}

technique11 DebugTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Full()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS_Debug()));
    }
}
