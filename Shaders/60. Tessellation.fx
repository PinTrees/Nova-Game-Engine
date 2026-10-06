//=============================================================================
// 60. Tessellation.fx — 재질 테셀레이션 (Lit 의 Height Map 변위, HDRP Lit 의 Tessellation 처럼)
//
// 카메라에 가까울수록 삼각형을 잘게 나누고 (변 길이 ÷ 원하는 길이, 거리만큼 길게), 높이 맵만큼 법선 쪽으로 민다.
// 본 패스 (32) · 깊이 프리패스 (28) · 그림자 (26) 가 이 파일의 같은 함수로 → 같은 자리 (EQUAL 깊이 검사 · 그림자가 맞는다)
//  자리 · 나눔 계산은 precise: 셰이더마다 컴파일러가 순서를 바꾸면 (mad 합치기) 비트가 달라 EQUAL 이 깨진다
//  그 계산에는 distance · normalize · lerp · cross 대신 아래 매크로 (덧셈 · 곱셈만): GLSL 로 바꾸면 그 내장 함수는 precise 가 붙지 않아
//  드라이버가 셰이더마다 다르게 펼친다 (OpenGL 에서 나눔이 어긋나 깊이가 얼룩졌다). 지역 값도 모두 precise (함수 out 은 이어지지 않는다)
//  - 나눔: 변마다 가운데 점의 거리로 (두 삼각형이 나누는 변은 같은 값 — 틈이 없다), 나눔 거리 끝 1/4 에서 1 로 줄어든다
//  - 변위: (높이 - 가운데) × 높이 (m). 높이 밉은 정점 간격에 맞춰 (톱니 · 반짝임 없이)
//  - 법선: 높이의 기울기 (uv) 와 삼각형의 uv → 월드 축으로 다시 — 빛이 튀어나온 모양을 따른다
//=============================================================================
#ifndef NOVA_TESSELLATION_FX
#define NOVA_TESSELLATION_FX

cbuffer cbTessellation
{
    float4x4 gTessViewProj;   // 이 패스의 ViewProj (본 · 깊이 프리패스 = 카메라, 그림자는 26 의 gViewProj)
    float4x4 gTessView;       // 깊이 프리패스 (28): 뷰 공간 법선 · 깊이
    float4 gTessParams;       // x 높이 (m), y 가운데 (0..1), z 최대 나눔 (1..64), w 나눔이 끝나는 거리 (m)
    float4 gTessUV;           // xy 타일링, zw 오프셋 (재질과 같은 값)
    float4 gTessEye;          // xyz 나눔을 정하는 카메라 (그림자 패스도 화면 카메라), w 1 m 거리에서 원하는 변 길이 (m)
    float4 gTessCull;         // x 1 = 화면 밖 패치 버리기 (본 · 깊이 — 그림자는 0: 화면 밖 물체도 그림자를 드리운다), y 여유 (m — 밀 수 있는 높이)
};
Texture2D gHeightMap;
SamplerState samTessHeight
{
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = WRAP;
    AddressV = WRAP;
};

// 조절점 (월드 공간 — 일괄 그리기의 인스턴스 행렬을 곱한 뒤)
struct TessCP
{
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
    float4 TangentW : TANGENT;
    float2 Tex : TEXCOORD0;
};

struct TessPatch
{
    float Edge[3] : SV_TessFactor;
    float Inside : SV_InsideTessFactor;
};

#define TESS_DOT3(a, b) ((a).x * (b).x + (a).y * (b).y + (a).z * (b).z)
#define TESS_CROSS(a, b) float3((a).y * (b).z - (a).z * (b).y, (a).z * (b).x - (a).x * (b).z, (a).x * (b).y - (a).y * (b).x)

float TessEdge(float3 a, float3 b)
{
    precise const float3 toEye = (a + b) * 0.5f - gTessEye.xyz;
    precise const float3 ab = a - b;
    precise const float d = sqrt(TESS_DOT3(toEye, toEye));
    precise const float len = sqrt(TESS_DOT3(ab, ab));
    precise const float far = max(gTessParams.w, 0.01f);
    precise const float want = max(gTessEye.w, 0.005f) * (1.0f + d);   // 원하는 변 길이: 멀수록 길게
    precise const float fade = saturate((far - d) / (far * 0.25f));     // 나눔 거리 끝 1/4 에서 1 로 (넘으면 0)
    precise const float f = clamp(1.0f + (len / want - 1.0f) * fade, 1.0f, clamp(gTessParams.z, 1.0f, 64.0f));
    return f;
}

// 패치가 화면 (gTessViewProj) 밖인가: 세 점이 모두 한 평면 바깥 (여유 = 밀 수 있는 높이만큼). 본 · 깊이 프리패스가 같은 값 → 같은 패치
bool TessPatchOutside(float3 p0, float3 p1, float3 p2)
{
    if (gTessCull.x < 0.5f)
        return false;
    const float4 a = mul(float4(p0, 1.0f), gTessViewProj);
    const float4 b = mul(float4(p1, 1.0f), gTessViewProj);
    const float4 c = mul(float4(p2, 1.0f), gTessViewProj);
    const float m = gTessCull.y;
    const float3 x = float3(a.x, b.x, c.x), y = float3(a.y, b.y, c.y), w = float3(a.w, b.w, c.w) + m;
    return all(x < -w) || all(x > w) || all(y < -w) || all(y > w) || all(w < 0.0f);
}

TessPatch TessFactors(float3 p0, float3 p1, float3 p2)
{
    TessPatch t;
    if (TessPatchOutside(p0, p1, p2))
    {
        // 나눔 0 = 그 패치를 그리지 않는다 (카메라 뒤 · 옆 — 지형 노드는 카메라 아래에서 뒤로도 넓다)
        t.Edge[0] = t.Edge[1] = t.Edge[2] = 0.0f;
        t.Inside = 0.0f;
        return t;
    }
    t.Edge[0] = TessEdge(p1, p2);
    t.Edge[1] = TessEdge(p2, p0);
    t.Edge[2] = TessEdge(p0, p1);
    t.Inside = (t.Edge[0] + t.Edge[1] + t.Edge[2]) * (1.0f / 3.0f);
    return t;
}

float TessHeight(float2 uv, float mip)
{
    return gHeightMap.SampleLevel(samTessHeight, uv * gTessUV.xy + gTessUV.zw, mip).r;
}

// 삼각형의 uv → 월드 축 (dP/du, dP/dv). uv 가 겹친 삼각형이면 false
bool TessFrame(TessCP a, TessCP b, TessCP c, out float3 dPdu, out float3 dPdv)
{
    const float3 e1 = b.PosW - a.PosW, e2 = c.PosW - a.PosW;
    const float2 t1 = b.Tex - a.Tex, t2 = c.Tex - a.Tex;
    const float det = t1.x * t2.y - t2.x * t1.y;
    const float inv = abs(det) > 1e-10f ? 1.0f / det : 0.0f;
    dPdu = (e1 * t2.y - e2 * t1.y) * inv;
    dPdv = (e2 * t1.x - e1 * t2.x) * inv;
    return inv != 0.0f;
}

// 높이의 uv 기울기 (hu, hv) 로 민 면의 법선: 원래 법선 n · 축 (dPdu, dPdv). 삼각형 감김과 상관없이 n 쪽
float3 TessBentNormal(float3 n, float3 dPdu, float3 dPdv, float hu, float hv)
{
    const float3 du = dPdu + n * (hu * gTessParams.x);
    const float3 dv = dPdv + n * (hv * gTessParams.x);
    float3 nn = cross(du, dv);
    if (dot(nn, nn) <= 1e-12f)
        return n;
    nn = normalize(nn);
    return dot(nn, n) < 0.0f ? -nn : nn;
}

// 픽셀 법선 (본 패스 PS): 높이 맵의 기울기를 픽셀마다 — 나눈 정점 사이의 작은 무늬까지 빛이 따른다 (정점 법선만 쓰면 줄눈이 오톨도톨)
//  기울기 간격 = 높이 맵 한 칸과 화면 한 픽셀 중 큰 것 (멀면 흐린 밉과 같이 — 반짝이지 않게)
float3 TessPixelNormal(float3 baseN, float3 dPdu, float3 dPdv, float2 uv)
{
    float w, h;
    gHeightMap.GetDimensions(w, h);
    const float2 tuv = uv * gTessUV.xy + gTessUV.zw;
    const float2 gx = ddx(tuv), gy = ddy(tuv);
    const float2 step = max(1.0f / float2(w, h), max(abs(gx), abs(gy)));
    const float hR = gHeightMap.SampleGrad(samTessHeight, tuv + float2(step.x, 0.0f), gx, gy).r;
    const float hL = gHeightMap.SampleGrad(samTessHeight, tuv - float2(step.x, 0.0f), gx, gy).r;
    const float hU = gHeightMap.SampleGrad(samTessHeight, tuv + float2(0.0f, step.y), gx, gy).r;
    const float hD = gHeightMap.SampleGrad(samTessHeight, tuv - float2(0.0f, step.y), gx, gy).r;
    // 타일 uv 기울기 × 타일링 = 원래 uv 기울기
    const float hu = (hR - hL) / (2.0f * step.x) * gTessUV.x;
    const float hv = (hU - hD) / (2.0f * step.y) * gTessUV.y;
    return TessBentNormal(normalize(baseN), dPdu, dPdv, hu, hv);
}

// ---- 시차 가림 (POM — HDRP Lit 의 Pixel Displacement): 테셀레이션이 끝나는 거리 너머 · 테셀레이션이 없는 기기에서 깊이감
//  보이는 면 = 가운데 (Base) 평면이라고 보고, 시선을 높이 맵의 맨 위 ((1 - Base) × 높이) 에서 맨 아래까지 걸어 내려가며 처음 닿는 곳의 uv.
//  weight 0 = 원래 uv. 깊이는 바꾸지 않는다 (깊이 프리패스와 EQUAL 그대로)
float2 TessParallaxUV(float3 posW, float3 n, float3 dPdu, float3 dPdv, float2 uv, float weight)
{
    const float2 gx = ddx(uv) * gTessUV.xy, gy = ddy(uv) * gTessUV.xy;   // 분기 전에 (화면 미분)
    const float amp = gTessParams.x;
    if (weight <= 0.001f || amp <= 0.0f)
        return uv;
    const float du2 = dot(dPdu, dPdu), dv2 = dot(dPdv, dPdv);
    const float3 D = normalize(posW - gTessEye.xyz);   // 눈 → 그 점
    const float dz = dot(D, n);
    if (du2 < 1e-12f || dv2 < 1e-12f || dz > -0.05f)   // uv 축이 없거나 거의 옆에서 본다
        return uv;
    // 1 m 내려갈 때 uv 가 움직이는 양 (uv 축으로 투영)
    const float2 duv = float2(dot(D, dPdu) / du2, dot(D, dPdv) / dv2) / -dz;
    const float top = (1.0f - gTessParams.y) * amp;
    const float2 uvTop = uv - duv * top;
    const int steps = (int) lerp(16.0f, 6.0f, saturate(-dz));   // 비스듬할수록 촘촘히
    float prevGap = top - ((gHeightMap.SampleGrad(samTessHeight, uvTop * gTessUV.xy + gTessUV.zw, gx, gy).r - gTessParams.y) * amp);
    float prevS = 0.0f;
    float2 hit = uv + duv * (gTessParams.y * amp);   // 끝까지 안 닿으면 맨 아래
    [loop]
    for (int i = 1; i <= steps; ++i)
    {
        const float s = amp * i / steps;   // 맨 위에서 내려간 깊이 (m)
        const float2 u = uvTop + duv * s;
        const float surf = (gHeightMap.SampleGrad(samTessHeight, u * gTessUV.xy + gTessUV.zw, gx, gy).r - gTessParams.y) * amp;
        const float gap = (top - s) - surf;   // > 0 = 아직 표면 위
        if (gap <= 0.0f)
        {
            const float t = prevGap / max(prevGap - gap, 1e-6f);
            hit = uvTop + duv * lerp(prevS, s, t);
            break;
        }
        prevGap = gap;
        prevS = s;
    }
    return lerp(uv, hit, weight);
}

// POM 가중치: 테셀레이션 (나눔 거리 끝 1/4 에서 줄어든다) 을 이어 받고, 나눔 거리의 2.5 배에서 사라진다. tess = 테셀레이션으로 그리는 중
float TessParallaxWeight(float3 posW, bool tess)
{
    const float dist = distance(posW, gTessEye.xyz);
    const float far = max(gTessParams.w, 0.01f);
    const float fadeIn = tess ? saturate((dist - far * 0.75f) / (far * 0.25f)) : 1.0f;
    return fadeIn * saturate((far * 2.5f - dist) / (far * 0.5f));
}

// 나눈 점 하나: 무게중심 보간 → 높이만큼 밀기 → 법선 다시
TessCP TessEvaluate(TessCP a, TessCP b, TessCP c, float3 w)
{
    TessCP o;
    precise const float3 p = a.PosW * w.x + b.PosW * w.y + c.PosW * w.z;
    precise const float3 nSum = a.NormalW * w.x + b.NormalW * w.y + c.NormalW * w.z;
    precise const float3 n = nSum * (1.0f / sqrt(max(TESS_DOT3(nSum, nSum), 1e-20f)));
    precise const float2 uv = a.Tex * w.x + b.Tex * w.y + c.Tex * w.z;
    o.TangentW = a.TangentW * w.x + b.TangentW * w.y + c.TangentW * w.z;
    o.Tex = uv;
    o.NormalW = n;

    // 높이 밉 = 정점 간격에 맞춰 (나이퀴스트): 정점보다 잘게 바뀌는 높이는 흐린 밉으로 — 날카로운 줄눈이 톱니가 되지 않는다
    //  정점 간격 = 원하는 변 길이와 (가장 긴 변 ÷ 최대 나눔) 중 큰 것. 잔무늬는 본 패스의 픽셀 법선 (TessPixelNormal) 이 그린다
    precise const float3 e1 = b.PosW - a.PosW;
    precise const float3 e2 = c.PosW - a.PosW;
    precise const float3 e3 = c.PosW - b.PosW;
    precise const float2 t1 = b.Tex - a.Tex;
    precise const float2 t2 = c.Tex - a.Tex;
    precise const float det = t1.x * t2.y - t2.x * t1.y;
    const bool frame = abs(det) > 1e-10f;
    precise const float inv = frame ? 1.0f / det : 0.0f;
    precise const float3 dPdu = (e1 * t2.y - e2 * t1.y) * inv;
    precise const float3 dPdv = (e2 * t1.x - e1 * t2.x) * inv;
    float tw, th;
    gHeightMap.GetDimensions(tw, th);
    precise const float3 toEye = p - gTessEye.xyz;
    precise const float dist = sqrt(TESS_DOT3(toEye, toEye));
    precise const float texel = max(sqrt(TESS_DOT3(dPdu, dPdu)) / max(tw * abs(gTessUV.x), 1.0f),
                                    sqrt(TESS_DOT3(dPdv, dPdv)) / max(th * abs(gTessUV.y), 1.0f));   // 높이 맵 한 칸의 월드 길이
    precise const float edge = sqrt(max(max(TESS_DOT3(e1, e1), TESS_DOT3(e2, e2)), TESS_DOT3(e3, e3)));
    precise const float spacing = max(max(gTessEye.w, 0.005f) * (1.0f + dist), edge / clamp(gTessParams.z, 1.0f, 64.0f));
    precise const float mip = clamp(log2(spacing / max(texel, 1e-6f)), 0.0f, 10.0f);
    precise const float h = TessHeight(uv, mip);
    precise const float3 moved = p + n * ((h - gTessParams.y) * gTessParams.x);
    o.PosW = moved;

    // 정점 법선 (깊이 프리패스의 SSAO 법선 · 그림자 바이어스): 높이의 기울기로 다시. 본 패스는 픽셀마다 (TessPixelNormal)
    if (frame)
    {
        // 기울기 간격 = 그 밉의 한 칸 (원래 uv)
        const float2 s = exp2(mip) / max(float2(tw, th) * abs(gTessUV.xy), 1.0f);
        const float hu = (TessHeight(uv + float2(s.x, 0.0f), mip) - TessHeight(uv - float2(s.x, 0.0f), mip)) / (2.0f * s.x);
        const float hv = (TessHeight(uv + float2(0.0f, s.y), mip) - TessHeight(uv - float2(0.0f, s.y), mip)) / (2.0f * s.y);
        o.NormalW = TessBentNormal(n, dPdu, dPdv, hu, hv);
    }
    return o;
}

// 조절점 그대로 넘기는 Hull (나눔은 TessPatchHS)
TessPatch TessPatchHS(InputPatch<TessCP, 3> p)
{
    return TessFactors(p[0].PosW, p[1].PosW, p[2].PosW);
}

[domain("tri")]
[partitioning("fractional_odd")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("TessPatchHS")]
[maxtessfactor(64.0f)]
TessCP TessHS(InputPatch<TessCP, 3> p, uint i : SV_OutputControlPointID)
{
    return p[i];
}

// 일괄 그리기 (MeshBatcher) 의 인스턴스 월드 행렬 → 조절점 (법선 = 3x3 의 여인수 — 축마다 크기가 달라도 맞다)
TessCP TessBatchCP(float3 posL, float3 normalL, float4 tangentL, float2 tex, float4x4 world)
{
    TessCP o;
    precise const float3 p = mul(float4(posL, 1.0f), world).xyz;
    precise const float3 r0 = world[0].xyz;
    precise const float3 r1 = world[1].xyz;
    precise const float3 r2 = world[2].xyz;
    precise const float3 c0 = TESS_CROSS(r1, r2);
    precise const float3 c1 = TESS_CROSS(r2, r0);
    precise const float3 c2 = TESS_CROSS(r0, r1);
    precise const float3 n = c0 * normalL.x + c1 * normalL.y + c2 * normalL.z;
    o.PosW = p;
    o.NormalW = n;
    o.TangentW = float4(mul(tangentL.xyz, (float3x3) world), tangentL.w);
    o.Tex = tex;
    return o;
}

#endif
