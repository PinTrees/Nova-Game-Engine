//=============================================================================
// 61. TerrainTessellation.fx — 지형의 레이어 높이: 테셀레이션 변위 · 높이 기반 섞기 · 픽셀 범프 · POM (+ 본 패스는 날씨의 쌓인 눈, 32)
//
// 40 (지형) · 60 (나눔) 뒤에 include. 본 패스 (32) · 깊이 프리패스 (28) · 그림자 (26) 가 이 파일의 같은 함수로 같은 자리를 민다
//  (자리 계산은 precise + 덧셈 · 곱셈만 — 60 과 같은 까닭, OpenGL 은 TES 의 invariant gl_Position)
//  - 높이 맵 넷은 배열 하나 (gTerrainHeights — TerrainRenderer 가 62. TerrainHeightBlit.fx 로 모은다): 샘플러 하나라
//    OpenGL · GLES 의 픽셀 단계 샘플러 32 개 한도 안에서 지형 PS 도 높이를 읽는다
//  - 높이 기반 섞기 (HDRP TerrainLit 의 Height-based Blend): 레이어 경계에서 (높이 + 가중치) 가 큰 쪽이 먼저 — 돌이 흙 위로 드러난다.
//    색 · 변위 · 범프가 같은 가중치
//  - 변위 = Σ 가중치 × (높이 − Base) × Amplitude, 지형 법선 쪽. 위 (xz) 투영은 색과 같은 타일 없애기 무늬, 옆은 삼평면. 높이 밉 = 정점 간격
//  - 픽셀: 높이의 화면 미분으로 범프 (모든 거리), 테셀레이션이 끝나는 거리 너머 · 테셀레이션 없는 기기는 POM (평평한 곳)
//=============================================================================
#ifndef NOVA_TERRAIN_TESSELLATION_FX
#define NOVA_TERRAIN_TESSELLATION_FX

cbuffer cbTerrainHeight
{
    float4 gTerrainLayerHeight[4];   // 레이어마다 x 높이 (m, 0 = 높이 맵 없음), y Base (0..1), z Normal Scale (0 = Normal Map 없음)
    float4 gTerrainHeightParams;     // x 1 = 높이 맵이 있는 레이어가 있다, y 높이 기반 섞기 전환 폭 (0 = 끔), z POM 이 끝나는 거리 (m), w 나눔 거리 (m, 0 = 테셀레이션 아님)
};
Texture2DArray gTerrainHeights;      // 슬라이스 = 레이어 (R10G10B10A2: R 높이 — 없으면 0.5, G · B Normal Map xy — 없으면 0.5)

// 컨트롤 맵 가중치 (TerrainAlbedo 와 같은 정규화). 밉 0 — Domain 에서도
float4 TerrainControlWeightsLevel(float2 uv)
{
    float4 w = gTerrainControl.SampleLevel(samTerrainClamp, uv, 0);
    if (gTerrainLayerCount < 4) w.a = 0.0f;
    if (gTerrainLayerCount < 3) w.b = 0.0f;
    if (gTerrainLayerCount < 2) w.g = 0.0f;
    const float sum = w.r + w.g + w.b + w.a;
    return sum < 1e-4f ? float4(1, 0, 0, 0) : w / sum;
}

// 높이 기반 섞기: c = 컨트롤 가중치, h = 레이어 높이 (0..1). 전환 폭 안에서 (h + c) 가 큰 레이어부터 (HDRP 와 같은 식)
float4 TerrainHeightBlend(float4 c, float4 h)
{
    const float T = gTerrainHeightParams.y;
    if (T <= 0.0f)
        return c;
    precise const float4 s = h + c;
    precise const float top = max(max(c.r > 0.0f ? s.r : -1.0f, c.g > 0.0f ? s.g : -1.0f), max(c.b > 0.0f ? s.b : -1.0f, c.a > 0.0f ? s.a : -1.0f));
    precise float4 b = max(s - (top - T), 0.0f);
    b = float4(c.r > 0.0f ? b.r : 0.0f, c.g > 0.0f ? b.g : 0.0f, c.b > 0.0f ? b.b : 0.0f, c.a > 0.0f ? b.a : 0.0f);
    precise const float sum = b.r + b.g + b.b + b.a;
    return sum > 1e-6f ? b / sum : c;
}

// 슬라이스 하나의 타일 없애기 무늬 (TerrainSampleNoTile 과 같은 격자 · 해시 · 섞기 — 색과 같은 무늬). 밉 지정 / 미분 지정
float TerrainNoTileHeight(float slice, float2 uv, bool level, float mip, float2 dx, float2 dy)
{
    precise const float2 skewed = mul(float2x2(1.0f, 0.0f, -0.57735027f, 1.15470054f), uv * 1.8f);
    const float2 base = floor(skewed);
    precise float3 t = float3(frac(skewed), 0.0f);
    t.z = 1.0f - t.x - t.y;
    float3 w;
    float2 v1, v2, v3;
    if (t.z > 0.0f)
    {
        w = float3(t.z, t.y, t.x);
        v1 = base; v2 = base + float2(0, 1); v3 = base + float2(1, 0);
    }
    else
    {
        w = float3(-t.z, 1.0f - t.y, 1.0f - t.x);
        v1 = base + float2(1, 1); v2 = base + float2(1, 0); v3 = base + float2(0, 1);
    }
    const float2 u1 = uv + TerrainHash2(v1) * 7.0f, u2 = uv + TerrainHash2(v2) * 7.0f, u3 = uv + TerrainHash2(v3) * 7.0f;
    float a, b, c;
    if (level)
    {
        a = gTerrainHeights.SampleLevel(samTerrainWrap, float3(u1, slice), mip).r;
        b = gTerrainHeights.SampleLevel(samTerrainWrap, float3(u2, slice), mip).r;
        c = gTerrainHeights.SampleLevel(samTerrainWrap, float3(u3, slice), mip).r;
    }
    else
    {
        a = gTerrainHeights.SampleGrad(samTerrainWrap, float3(u1, slice), dx, dy).r;
        b = gTerrainHeights.SampleGrad(samTerrainWrap, float3(u2, slice), dx, dy).r;
        c = gTerrainHeights.SampleGrad(samTerrainWrap, float3(u3, slice), dx, dy).r;
    }
    precise float3 wp = w * w;
    wp = wp * wp;
    wp = wp / (wp.x + wp.y + wp.z);
    const float mean = gTerrainHeights.SampleLevel(samTerrainWrap, float3(0.5f, 0.5f, slice), 16).r;
    precise const float mixed = a * wp.x + b * wp.y + c * wp.z;
    precise const float k = 1.0f / sqrt(max(TESS_DOT3(wp, wp), 1e-4f));
    precise const float h = saturate(mean + (mixed - mean) * (1.0f + (k - 1.0f) * 0.6f));
    return h;
}

// 레이어 하나의 높이 (삼평면, 밉 지정 — Domain). w = 삼평면 가중치 (x = zy 면, y = 위, z = xy 면)
float TerrainLayerHeightLevel(float slice, float4 st, float3 lp, float3 w, float mip)
{
    precise float h = 0.0f;
    [branch] if (w.y > 0.0f)
        h += w.y * TerrainNoTileHeight(slice, lp.xz * st.xy + st.zw, true, mip, 0, 0);
    [branch] if (w.x > 0.0f)
        h += w.x * gTerrainHeights.SampleLevel(samTerrainWrap, float3(float2(lp.z, -lp.y) * st.xy + st.zw, slice), mip).r;
    [branch] if (w.z > 0.0f)
        h += w.z * gTerrainHeights.SampleLevel(samTerrainWrap, float3(float2(lp.x, -lp.y) * st.xy + st.zw, slice), mip).r;
    return h;
}

// 높이 배열 한 칸의 월드 길이 → 정점 간격에 맞는 밉
float TerrainHeightMip(float4 st, float spacing)
{
    float tw, th, slices;
    gTerrainHeights.GetDimensions(tw, th, slices);
    precise const float texel = 1.0f / max(abs(st.x) * tw, 1e-6f);
    precise const float mip = clamp(log2(max(spacing, 1e-6f) / texel), 0.0f, 10.0f);
    return mip;
}

// 높이 → 섞은 가중치 · 변위 (m). h = 레이어 높이 (높이 맵 없는 레이어 = Base 자리 → 변위 0)
float TerrainBlendDisplacement(float4 c, float4 h, out float4 blended)
{
    blended = TerrainHeightBlend(c, h);
    precise const float d = blended.r * (h.r - gTerrainLayerHeight[0].y) * gTerrainLayerHeight[0].x
                          + blended.g * (h.g - gTerrainLayerHeight[1].y) * gTerrainLayerHeight[1].x
                          + blended.b * (h.b - gTerrainLayerHeight[2].y) * gTerrainLayerHeight[2].x
                          + blended.a * (h.a - gTerrainLayerHeight[3].y) * gTerrainLayerHeight[3].x;
    return d;
}

// 그 자리의 변위 (m, 지형 법선 쪽 — Domain). lp = 지형 로컬 위치, n = 지형 법선, spacing = 정점 간격 (m)
float TerrainDisplacement(float3 lp, float2 uv, float3 n, float spacing)
{
    if (gTerrainHeightParams.x < 0.5f)
        return 0.0f;
    const float4 c = TerrainControlWeightsLevel(uv);
    // 삼평면 가중치 (TerrainTriplanarSetup 과 같은 식 — pow 대신 곱셈)
    precise float3 n2 = n * n;
    precise float3 w = n2 * n2;
    w = w / max(w.x + w.y + w.z, 1e-5f);
    w.x = w.x < 0.02f ? 0.0f : w.x;
    w.z = w.z < 0.02f ? 0.0f : w.z;
    w = w / max(w.x + w.y + w.z, 1e-5f);
    // 높이 맵 없는 레이어는 Base 자리 (변위 0, 섞기에서는 가운데 높이)
    precise float4 h = float4(gTerrainLayerHeight[0].y, gTerrainLayerHeight[1].y, gTerrainLayerHeight[2].y, gTerrainLayerHeight[3].y);
    [branch] if (c.r > 0.0f && gTerrainLayerHeight[0].x > 0.0f) h.r = TerrainLayerHeightLevel(0, gTerrainLayerST[0], lp, w, TerrainHeightMip(gTerrainLayerST[0], spacing));
    [branch] if (c.g > 0.0f && gTerrainLayerHeight[1].x > 0.0f) h.g = TerrainLayerHeightLevel(1, gTerrainLayerST[1], lp, w, TerrainHeightMip(gTerrainLayerST[1], spacing));
    [branch] if (c.b > 0.0f && gTerrainLayerHeight[2].x > 0.0f) h.b = TerrainLayerHeightLevel(2, gTerrainLayerST[2], lp, w, TerrainHeightMip(gTerrainLayerST[2], spacing));
    [branch] if (c.a > 0.0f && gTerrainLayerHeight[3].x > 0.0f) h.a = TerrainLayerHeightLevel(3, gTerrainLayerST[3], lp, w, TerrainHeightMip(gTerrainLayerST[3], spacing));
    float4 blended;
    precise const float d = TerrainBlendDisplacement(c, h, blended);
    return d;
}

// 높이맵 중앙 차분 법선 (TerrainNormalUV 와 같은 값 — 덧셈 · 곱셈만)
float3 TerrainNormalPrecise(float2 uv)
{
    const float res = gTerrainSize.w;
    const float d = 1.0f / (res - 1.0f);
    const float hl = TerrainHeightUV(uv - float2(d, 0));
    const float hr = TerrainHeightUV(uv + float2(d, 0));
    const float hd = TerrainHeightUV(uv - float2(0, d));
    const float hu = TerrainHeightUV(uv + float2(0, d));
    precise const float3 g = float3(-(hr - hl) / (2.0f * gTerrainSize.x * d), 1.0f, -(hu - hd) / (2.0f * gTerrainSize.z * d));
    precise const float3 n = g * (1.0f / sqrt(TESS_DOT3(g, g)));
    return n;
}

// ---- 조절점 · Hull (나눔은 60 의 TessFactors — 카메라 · 거리 · 최대 나눔은 C++ 이 패스마다 같은 값으로)
struct TerrainCP
{
    float3 PosW : POSITION;
    float2 UV : TEXCOORD0;
};

TerrainCP TerrainTessVS(uint vid : SV_VertexID)
{
    TerrainCP o;
    o.PosW = TerrainVertexWorld(vid, o.UV);
    return o;
}

TessPatch TerrainPatchHS(InputPatch<TerrainCP, 3> p)
{
    return TessFactors(p[0].PosW, p[1].PosW, p[2].PosW);
}

[domain("tri")]
[partitioning("fractional_odd")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("TerrainPatchHS")]
[maxtessfactor(64.0f)]
TerrainCP TerrainHS(InputPatch<TerrainCP, 3> p, uint i : SV_OutputControlPointID)
{
    return p[i];
}

// 나눈 점 하나: 무게중심 → 지형 법선 쪽으로 레이어 높이만큼. uv · n 도 돌려준다
float3 TerrainTessPosition(TerrainCP a, TerrainCP b, TerrainCP c, float3 w, out float2 uv, out float3 n)
{
    precise const float3 p = a.PosW * w.x + b.PosW * w.y + c.PosW * w.z;
    precise const float2 t = a.UV * w.x + b.UV * w.y + c.UV * w.z;
    uv = t;
    n = TerrainNormalPrecise(t);
    if (gTerrainHeightParams.x < 0.5f)
        return p;
    precise const float3 toEye = p - gTessEye.xyz;
    precise const float dist = sqrt(TESS_DOT3(toEye, toEye));
    precise const float3 e1 = b.PosW - a.PosW;
    precise const float3 e2 = c.PosW - a.PosW;
    precise const float3 e3 = c.PosW - b.PosW;
    precise const float edge = sqrt(max(max(TESS_DOT3(e1, e1), TESS_DOT3(e2, e2)), TESS_DOT3(e3, e3)));
    precise const float spacing = max(max(gTessEye.w, 0.005f) * (1.0f + dist), edge / clamp(gTessParams.z, 1.0f, 64.0f));
    precise const float d = TerrainDisplacement(p - gTerrainOrigin.xyz, t, n, spacing);
    precise const float3 moved = p + n * d;
    return moved;
}

// ---- 픽셀 (본 패스 PS): 섞은 가중치 · 높이 (m) · 노멀 — 색 · 범프가 쓴다
//  배열의 G · B = 레이어 Normal Map 의 xy (0.5 = 평평). 삼평면 노멀은 투영마다 화이트아웃으로 지형 법선에 얹는다 (Golus 2017)

// 타일 없애기 무늬 (TerrainNoTileHeight 와 같은 격자 · 해시) 의 높이 + 노멀 xy (미분 지정). 높이만 분산 보존
float3 TerrainNoTileHN(float slice, float2 uv, float2 dx, float2 dy)
{
    const float2 skewed = mul(float2x2(1.0f, 0.0f, -0.57735027f, 1.15470054f), uv * 1.8f);
    const float2 base = floor(skewed);
    float3 t = float3(frac(skewed), 0.0f);
    t.z = 1.0f - t.x - t.y;
    float3 w;
    float2 v1, v2, v3;
    if (t.z > 0.0f)
    {
        w = float3(t.z, t.y, t.x);
        v1 = base; v2 = base + float2(0, 1); v3 = base + float2(1, 0);
    }
    else
    {
        w = float3(-t.z, 1.0f - t.y, 1.0f - t.x);
        v1 = base + float2(1, 1); v2 = base + float2(1, 0); v3 = base + float2(0, 1);
    }
    const float3 a = gTerrainHeights.SampleGrad(samTerrainWrap, float3(uv + TerrainHash2(v1) * 7.0f, slice), dx, dy).rgb;
    const float3 b = gTerrainHeights.SampleGrad(samTerrainWrap, float3(uv + TerrainHash2(v2) * 7.0f, slice), dx, dy).rgb;
    const float3 c = gTerrainHeights.SampleGrad(samTerrainWrap, float3(uv + TerrainHash2(v3) * 7.0f, slice), dx, dy).rgb;
    float3 wp = w * w;
    wp *= wp;
    wp /= wp.x + wp.y + wp.z;
    const float mean = gTerrainHeights.SampleLevel(samTerrainWrap, float3(0.5f, 0.5f, slice), 16).r;
    const float3 mixed = a * wp.x + b * wp.y + c * wp.z;
    const float k = rsqrt(max(dot(wp, wp), 1e-4f));
    return float3(saturate(mean + (mixed.x - mean) * lerp(1.0f, k, 0.6f)), mixed.yz);
}

// 노멀 xy (0..1) → 접공간 노멀 (Normal Scale 만큼)
float3 TerrainUnpackNormal(float2 xy, float scale)
{
    float2 v = (xy * 2.0f - 1.0f) * scale;
    return float3(v, sqrt(saturate(1.0f - dot(v, v))));
}

// 레이어 하나: 높이 (0..1) 와 그 레이어의 월드 노멀 (n = 지형 법선, scale 0 = Normal Map 없음 → n)
float TerrainLayerHN(float slice, float4 st, TerrainTriplanar t, float3 n, float scale, out float3 nW)
{
    float h = 0.0f;
    float3 acc = 0.0f;
    [branch] if (t.W.y > 0.0f)
    {
        const float3 s = TerrainNoTileHN(slice, t.Top * st.xy + st.zw, t.TopDx * st.xy, t.TopDy * st.xy);
        h += t.W.y * s.x;
        const float3 tn = TerrainUnpackNormal(s.yz, scale);
        acc += t.W.y * float3(tn.x + n.x, abs(tn.z) * n.y, tn.y + n.z);   // 위: u = +x, v = +z
    }
    [branch] if (t.W.x > 0.0f)
    {
        const float3 s = gTerrainHeights.SampleGrad(samTerrainWrap, float3(t.SideX * st.xy + st.zw, slice), t.XDx * st.xy, t.XDy * st.xy).rgb;
        h += t.W.x * s.x;
        float3 tn = TerrainUnpackNormal(s.yz, scale);
        tn.x *= n.x < 0.0f ? -1.0f : 1.0f;
        acc += t.W.x * float3(abs(tn.z) * n.x, n.y - tn.y, tn.x + n.z);   // x 면: u = +z, v = -y
    }
    [branch] if (t.W.z > 0.0f)
    {
        const float3 s = gTerrainHeights.SampleGrad(samTerrainWrap, float3(t.SideZ * st.xy + st.zw, slice), t.ZDx * st.xy, t.ZDy * st.xy).rgb;
        h += t.W.z * s.x;
        float3 tn = TerrainUnpackNormal(s.yz, scale);
        tn.x *= n.z < 0.0f ? 1.0f : -1.0f;
        acc += t.W.z * float3(tn.x + n.x, n.y - tn.y, abs(tn.z) * n.z);   // z 면: u = +x, v = -y
    }
    nW = scale != 0.0f ? normalize(acc) : n;
    return h;
}

struct TerrainPixelHeight
{
    float4 Weights;   // 높이 기반으로 섞은 레이어 가중치
    float D;          // 변위 (m)
    float3 Normal;    // 레이어 Normal Map 을 섞은 월드 노멀 (없으면 지형 법선)
};

TerrainPixelHeight TerrainHeightsAt(float4 c, TerrainTriplanar t, float3 n)
{
    float4 h = float4(gTerrainLayerHeight[0].y, gTerrainLayerHeight[1].y, gTerrainLayerHeight[2].y, gTerrainLayerHeight[3].y);
    float3 n0 = n, n1 = n, n2 = n, n3 = n;
    // 높이 (x) 또는 Normal Map (z) 이 있는 레이어만 배열을 읽는다
    [branch] if (c.r > 0.0f && (gTerrainLayerHeight[0].x > 0.0f || gTerrainLayerHeight[0].z != 0.0f))
    {
        const float v = TerrainLayerHN(0, gTerrainLayerST[0], t, n, gTerrainLayerHeight[0].z, n0);
        if (gTerrainLayerHeight[0].x > 0.0f) h.r = v;
    }
    [branch] if (c.g > 0.0f && (gTerrainLayerHeight[1].x > 0.0f || gTerrainLayerHeight[1].z != 0.0f))
    {
        const float v = TerrainLayerHN(1, gTerrainLayerST[1], t, n, gTerrainLayerHeight[1].z, n1);
        if (gTerrainLayerHeight[1].x > 0.0f) h.g = v;
    }
    [branch] if (c.b > 0.0f && (gTerrainLayerHeight[2].x > 0.0f || gTerrainLayerHeight[2].z != 0.0f))
    {
        const float v = TerrainLayerHN(2, gTerrainLayerST[2], t, n, gTerrainLayerHeight[2].z, n2);
        if (gTerrainLayerHeight[2].x > 0.0f) h.b = v;
    }
    [branch] if (c.a > 0.0f && (gTerrainLayerHeight[3].x > 0.0f || gTerrainLayerHeight[3].z != 0.0f))
    {
        const float v = TerrainLayerHN(3, gTerrainLayerST[3], t, n, gTerrainLayerHeight[3].z, n3);
        if (gTerrainLayerHeight[3].x > 0.0f) h.a = v;
    }
    TerrainPixelHeight o;
    o.D = TerrainBlendDisplacement(c, h, o.Weights);
    o.Normal = normalize(n0 * o.Weights.r + n1 * o.Weights.g + n2 * o.Weights.b + n3 * o.Weights.a);
    return o;
}

// 범프: 높이의 화면 미분 (Mikkelsen 2010 의 surface gradient) 을 노멀 n 에 얹는다. fade = 멀어지면 0 (반짝이지 않게)
float3 TerrainBumpNormal(float3 n, float3 posW, float d, float fade)
{
    const float3 sx = ddx(posW), sy = ddy(posW);
    const float dd = d * fade;
    const float3 r1 = cross(sy, n), r2 = cross(n, sx);
    const float det = dot(sx, r1);
    const float3 grad = sign(det) * (ddx(dd) * r1 + ddy(dd) * r2);
    const float3 bumped = abs(det) * n - grad;
    return dot(bumped, bumped) > 1e-20f ? normalize(bumped) : n;
}

// 투영 하나의 좌표 (지형 로컬 위치 → 그 투영의 uv, 미터): 0 = 위 (xz), 1 = x 면 (z, -y), 2 = z 면 (x, -y) — TerrainTriplanarSetup 과 같은 축
float2 TerrainProjUV(int proj, float3 p)
{
    return proj == 0 ? p.xz : (proj == 1 ? float2(p.z, -p.y) : float2(p.x, -p.y));
}

// 그 투영으로 본 섞은 높이 (m) — POM 걸음마다 (위는 타일 없애기 무늬, 옆은 그대로)
float TerrainProjDisplacement(int proj, float3 p, float4 c, float2 gx, float2 gy)
{
    const float2 uv = TerrainProjUV(proj, p);
    float4 h = float4(gTerrainLayerHeight[0].y, gTerrainLayerHeight[1].y, gTerrainLayerHeight[2].y, gTerrainLayerHeight[3].y);
    [unroll] for (int i = 0; i < 4; ++i)
    {
        [branch] if (c[i] > 0.01f && gTerrainLayerHeight[i].x > 0.0f)
        {
            const float4 st = gTerrainLayerST[i];
            h[i] = proj == 0 ? TerrainNoTileHeight(i, uv * st.xy + st.zw, false, 0, gx * st.xy, gy * st.xy)
                             : gTerrainHeights.SampleGrad(samTerrainWrap, float3(uv * st.xy + st.zw, i), gx * st.xy, gy * st.xy).r;
        }
    }
    float4 blended;
    return TerrainBlendDisplacement(c, h, blended);
}

// POM: 지형 법선 쪽 높이 맨 위에서 맨 아래로 시선을 걸어 내려가며 처음 닿는 자리로 옮긴 지형 로컬 위치 (3D).
//  가장 큰 삼평면 투영으로 (평평한 땅 = 위, 절벽 = 옆). 투영이 섞이는 곳 (가중치가 고르게 나뉜 곳) 은 줄인다. 깊이는 바꾸지 않는다
//  dLx · dLy = 지형 로컬 위치의 화면 미분 (분기 밖에서)
float3 TerrainParallax(float3 lp, float3 posW, float3 eye, float3 n, float4 c, float3 dLx, float3 dLy, float weight)
{
    if (weight <= 0.001f)
        return lp;
    float3 w = n * n;
    w *= w;
    w /= max(w.x + w.y + w.z, 1e-5f);
    const int proj = w.y >= max(w.x, w.z) ? 0 : (w.x >= w.z ? 1 : 2);
    weight *= saturate((max(w.y, max(w.x, w.z)) - 0.55f) * 4.0f);
    const float3 D = normalize(posW - eye);
    const float dz = dot(D, n);
    if (weight <= 0.001f || dz > -0.05f)
        return lp;
    float top = 0.0f, bottom = 0.0f;
    [unroll] for (int i = 0; i < 4; ++i)
    {
        top = max(top, (1.0f - gTerrainLayerHeight[i].y) * gTerrainLayerHeight[i].x);
        bottom = max(bottom, gTerrainLayerHeight[i].y * gTerrainLayerHeight[i].x);
    }
    const float range = top + bottom;
    if (range <= 1e-4f)
        return lp;
    const float2 gx = TerrainProjUV(proj, dLx), gy = TerrainProjUV(proj, dLy);
    const float3 step = D / -dz;   // 법선 쪽으로 1 m 내려갈 때 옮겨 가는 자리
    const float3 pTop = lp - step * top;
    const int steps = (int) lerp(10.0f, 5.0f, saturate(-dz));
    float prevGap = 0.0f, prevS = 0.0f;
    float3 hit = lp + step * bottom;
    [loop]
    for (int k = 0; k <= steps; ++k)
    {
        const float s = range * k / steps;
        const float3 p = pTop + step * s;
        const float gap = (top - s) - TerrainProjDisplacement(proj, p, c, gx, gy);   // > 0 = 아직 표면 위
        if (gap <= 0.0f)
        {
            const float f = k == 0 ? 0.0f : prevGap / max(prevGap - gap, 1e-6f);
            hit = pTop + step * lerp(prevS, s, f);
            break;
        }
        prevGap = gap;
        prevS = s;
    }
    return lerp(lp, hit, weight);
}

// POM 가중치: 테셀레이션 (나눔 거리 끝 1/4) 을 이어 받고 POM 거리에서 사라진다
float TerrainParallaxWeight(float dist, bool tess)
{
    const float far = gTerrainHeightParams.w;
    const float fadeIn = tess && far > 0.0f ? saturate((dist - far * 0.75f) / (far * 0.25f)) : 1.0f;
    const float pomEnd = max(gTerrainHeightParams.z, 1.0f);
    return fadeIn * saturate((pomEnd - dist) / (pomEnd * 0.2f));
}

#endif
