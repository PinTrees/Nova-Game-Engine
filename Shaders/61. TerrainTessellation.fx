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
    float4 gTerrainLayerHeight[4];   // 레이어마다 x 높이 (m, 0 = 높이 맵 없음), y Base (0..1)
    float4 gTerrainHeightParams;     // x 1 = 높이 맵이 있는 레이어가 있다, y 높이 기반 섞기 전환 폭 (0 = 끔), z POM 이 끝나는 거리 (m), w 나눔 거리 (m, 0 = 테셀레이션 아님)
};
Texture2DArray gTerrainHeights;      // 슬라이스 = 레이어 (R16F, 높이 맵 없는 레이어 = 0.5)

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

// 레이어 하나의 높이 (픽셀 — TerrainLayerSample 과 같은 투영 · 미분)
float TerrainLayerHeightGrad(float slice, float4 st, TerrainTriplanar t)
{
    float h = 0.0f;
    [branch] if (t.W.y > 0.0f)
        h += t.W.y * TerrainNoTileHeight(slice, t.Top * st.xy + st.zw, false, 0, t.TopDx * st.xy, t.TopDy * st.xy);
    [branch] if (t.W.x > 0.0f)
        h += t.W.x * gTerrainHeights.SampleGrad(samTerrainWrap, float3(t.SideX * st.xy + st.zw, slice), t.XDx * st.xy, t.XDy * st.xy).r;
    [branch] if (t.W.z > 0.0f)
        h += t.W.z * gTerrainHeights.SampleGrad(samTerrainWrap, float3(t.SideZ * st.xy + st.zw, slice), t.ZDx * st.xy, t.ZDy * st.xy).r;
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

// ---- 픽셀 (본 패스 PS): 섞은 가중치 · 높이 (m) — 색 · 범프가 쓴다
struct TerrainPixelHeight
{
    float4 Weights;   // 높이 기반으로 섞은 레이어 가중치
    float D;          // 변위 (m)
};

TerrainPixelHeight TerrainHeightsAt(float4 c, TerrainTriplanar t)
{
    float4 h = float4(gTerrainLayerHeight[0].y, gTerrainLayerHeight[1].y, gTerrainLayerHeight[2].y, gTerrainLayerHeight[3].y);
    [branch] if (c.r > 0.0f && gTerrainLayerHeight[0].x > 0.0f) h.r = TerrainLayerHeightGrad(0, gTerrainLayerST[0], t);
    [branch] if (c.g > 0.0f && gTerrainLayerHeight[1].x > 0.0f) h.g = TerrainLayerHeightGrad(1, gTerrainLayerST[1], t);
    [branch] if (c.b > 0.0f && gTerrainLayerHeight[2].x > 0.0f) h.b = TerrainLayerHeightGrad(2, gTerrainLayerST[2], t);
    [branch] if (c.a > 0.0f && gTerrainLayerHeight[3].x > 0.0f) h.a = TerrainLayerHeightGrad(3, gTerrainLayerST[3], t);
    TerrainPixelHeight o;
    o.D = TerrainBlendDisplacement(c, h, o.Weights);
    return o;
}

// 범프: 높이의 화면 미분 (Mikkelsen 2010 의 surface gradient). fade = 멀어지면 0 (반짝이지 않게)
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

// POM (위 투영 — 평평한 땅): 시선을 높이 맨 위에서 맨 아래로 걸어 내려가며 처음 닿는 xz 로 옮긴 지형 로컬 위치.
//  c = 컨트롤 가중치, weight 0 = 그대로. 깊이는 바꾸지 않는다
float3 TerrainParallax(float3 lp, float3 posW, float3 eye, float3 n, float4 c, float2 dxTop, float2 dyTop, float weight)
{
    if (weight <= 0.001f)
        return lp;
    const float3 D = normalize(posW - eye);
    const float dz = dot(D, n);
    if (dz > -0.05f)
        return lp;
    // 높이 범위 (m): 레이어마다 (1 − Base) × 높이 위로, Base × 높이 아래로
    float top = 0.0f, bottom = 0.0f;
    [unroll] for (int i = 0; i < 4; ++i)
    {
        top = max(top, (1.0f - gTerrainLayerHeight[i].y) * gTerrainLayerHeight[i].x);
        bottom = max(bottom, gTerrainLayerHeight[i].y * gTerrainLayerHeight[i].x);
    }
    const float range = top + bottom;
    if (range <= 1e-4f)
        return lp;
    const float2 dxz = D.xz / -dz;   // 1 m 내려갈 때 xz 가 움직이는 양
    const float2 xzTop = lp.xz - dxz * top;
    const int steps = (int) lerp(10.0f, 5.0f, saturate(-dz));
    float prevGap = 0.0f, prevS = 0.0f;
    float2 hit = lp.xz + dxz * bottom;
    [loop]
    for (int k = 0; k <= steps; ++k)
    {
        const float s = range * k / steps;
        const float2 xz = xzTop + dxz * s;
        float4 h = float4(gTerrainLayerHeight[0].y, gTerrainLayerHeight[1].y, gTerrainLayerHeight[2].y, gTerrainLayerHeight[3].y);
        [branch] if (c.r > 0.01f && gTerrainLayerHeight[0].x > 0.0f) h.r = TerrainNoTileHeight(0, xz * gTerrainLayerST[0].xy + gTerrainLayerST[0].zw, false, 0, dxTop * gTerrainLayerST[0].xy, dyTop * gTerrainLayerST[0].xy);
        [branch] if (c.g > 0.01f && gTerrainLayerHeight[1].x > 0.0f) h.g = TerrainNoTileHeight(1, xz * gTerrainLayerST[1].xy + gTerrainLayerST[1].zw, false, 0, dxTop * gTerrainLayerST[1].xy, dyTop * gTerrainLayerST[1].xy);
        [branch] if (c.b > 0.01f && gTerrainLayerHeight[2].x > 0.0f) h.b = TerrainNoTileHeight(2, xz * gTerrainLayerST[2].xy + gTerrainLayerST[2].zw, false, 0, dxTop * gTerrainLayerST[2].xy, dyTop * gTerrainLayerST[2].xy);
        [branch] if (c.a > 0.01f && gTerrainLayerHeight[3].x > 0.0f) h.a = TerrainNoTileHeight(3, xz * gTerrainLayerST[3].xy + gTerrainLayerST[3].zw, false, 0, dxTop * gTerrainLayerST[3].xy, dyTop * gTerrainLayerST[3].xy);
        float4 blended;
        const float surf = TerrainBlendDisplacement(c, h, blended);
        const float gap = (top - s) - surf;   // > 0 = 아직 표면 위
        if (gap <= 0.0f)
        {
            const float f = k == 0 ? 0.0f : prevGap / max(prevGap - gap, 1e-6f);
            hit = xzTop + dxz * lerp(prevS, s, f);
            break;
        }
        prevGap = gap;
        prevS = s;
    }
    return float3(lerp(lp.xz, hit, weight), lp.y).xzy;
}

// POM 가중치: 테셀레이션 (나눔 거리 끝 1/4) 을 이어 받고 POM 거리에서 사라진다. 평평한 땅만 (위 투영)
float TerrainParallaxWeight(float dist, float3 n, bool tess)
{
    const float far = gTerrainHeightParams.w;
    const float fadeIn = tess && far > 0.0f ? saturate((dist - far * 0.75f) / (far * 0.25f)) : 1.0f;
    const float pomEnd = max(gTerrainHeightParams.z, 1.0f);
    return fadeIn * saturate((pomEnd - dist) / (pomEnd * 0.2f)) * saturate((n.y - 0.7f) * 5.0f);
}

#endif
