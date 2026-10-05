//=============================================================================
// 61. TerrainTessellation.fx — 지형 테셀레이션: Terrain Layer 의 Height Map 변위 (+ 본 패스는 날씨의 쌓인 눈, 32)
//
// 40 (지형) · 60 (나눔) 뒤에 include. 본 패스 (32) · 깊이 프리패스 (28) · 그림자 (26) 가 이 파일의 같은 함수로 같은 자리를 민다
//  (자리 계산은 precise + 덧셈 · 곱셈만 — 60 과 같은 까닭, OpenGL 은 TES 의 invariant gl_Position)
//  - 높이: 컨트롤 맵 가중치 × 레이어마다 (높이 - Base) × Amplitude, 지형 법선 쪽으로. 위 (xz) 투영은 색과 같은 타일 없애기 무늬
//    (TerrainSampleNoTile 과 같은 격자 · 해시 — 돌의 색과 모양이 맞는다), 옆은 삼평면
//  - 높이 밉 = 정점 간격에 맞춰 (나이퀴스트), 본 패스 법선은 Domain 에서 변위의 기울기로 (TerrainDisplacedNormal)
//=============================================================================
#ifndef NOVA_TERRAIN_TESSELLATION_FX
#define NOVA_TERRAIN_TESSELLATION_FX

cbuffer cbTerrainHeight
{
    float4 gTerrainLayerHeight[4];   // 레이어마다 x 높이 (m, 0 = 변위 없음), y Base (0..1)
    float4 gTerrainHeightParams;     // x 1 = 높이 변위가 있는 레이어가 있다
};
Texture2D gTerrainHeight0;
Texture2D gTerrainHeight1;
Texture2D gTerrainHeight2;
Texture2D gTerrainHeight3;

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

// TerrainSampleNoTile 의 밉 지정판 (같은 격자 · 해시 · 섞기 — 색과 같은 무늬). r 만
float TerrainNoTileLevel(Texture2D tex, float2 uv, float mip)
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
    const float a = tex.SampleLevel(samTerrainWrap, uv + TerrainHash2(v1) * 7.0f, mip).r;
    const float b = tex.SampleLevel(samTerrainWrap, uv + TerrainHash2(v2) * 7.0f, mip).r;
    const float c = tex.SampleLevel(samTerrainWrap, uv + TerrainHash2(v3) * 7.0f, mip).r;
    precise float3 wp = w * w;
    wp = wp * wp;
    wp = wp / (wp.x + wp.y + wp.z);
    const float mean = tex.SampleLevel(samTerrainWrap, float2(0.5f, 0.5f), 16).r;
    precise const float mixed = a * wp.x + b * wp.y + c * wp.z;
    precise const float k = 1.0f / sqrt(max(TESS_DOT3(wp, wp), 1e-4f));
    precise const float h = saturate(mean + (mixed - mean) * (1.0f + (k - 1.0f) * 0.6f));
    return h;
}

// 레이어 하나의 높이 (삼평면, 밉 지정). w = 삼평면 가중치 (x = zy 면, y = 위, z = xy 면)
float TerrainLayerHeightLevel(Texture2D tex, float4 st, float3 lp, float3 w, float mip)
{
    precise float h = 0.0f;
    [branch] if (w.y > 0.0f)
        h += w.y * TerrainNoTileLevel(tex, lp.xz * st.xy + st.zw, mip);
    [branch] if (w.x > 0.0f)
        h += w.x * tex.SampleLevel(samTerrainWrap, float2(lp.z, -lp.y) * st.xy + st.zw, mip).r;
    [branch] if (w.z > 0.0f)
        h += w.z * tex.SampleLevel(samTerrainWrap, float2(lp.x, -lp.y) * st.xy + st.zw, mip).r;
    return h;
}

// 높이 맵 한 칸의 월드 길이 → 정점 간격에 맞는 밉
float TerrainHeightMip(Texture2D tex, float4 st, float spacing)
{
    float tw, th;
    tex.GetDimensions(tw, th);
    precise const float texel = 1.0f / max(abs(st.x) * tw, 1e-6f);
    precise const float mip = clamp(log2(max(spacing, 1e-6f) / texel), 0.0f, 10.0f);
    return mip;
}

// 그 자리의 변위 (m, 지형 법선 쪽). lp = 지형 로컬 위치, n = 지형 법선, spacing = 정점 간격 (m)
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
    precise float d = 0.0f;
    [branch] if (c.r > 0.0f && gTerrainLayerHeight[0].x > 0.0f)
        d += c.r * (TerrainLayerHeightLevel(gTerrainHeight0, gTerrainLayerST[0], lp, w, TerrainHeightMip(gTerrainHeight0, gTerrainLayerST[0], spacing)) - gTerrainLayerHeight[0].y) * gTerrainLayerHeight[0].x;
    [branch] if (c.g > 0.0f && gTerrainLayerHeight[1].x > 0.0f)
        d += c.g * (TerrainLayerHeightLevel(gTerrainHeight1, gTerrainLayerST[1], lp, w, TerrainHeightMip(gTerrainHeight1, gTerrainLayerST[1], spacing)) - gTerrainLayerHeight[1].y) * gTerrainLayerHeight[1].x;
    [branch] if (c.b > 0.0f && gTerrainLayerHeight[2].x > 0.0f)
        d += c.b * (TerrainLayerHeightLevel(gTerrainHeight2, gTerrainLayerST[2], lp, w, TerrainHeightMip(gTerrainHeight2, gTerrainLayerST[2], spacing)) - gTerrainLayerHeight[2].y) * gTerrainLayerHeight[2].x;
    [branch] if (c.a > 0.0f && gTerrainLayerHeight[3].x > 0.0f)
        d += c.a * (TerrainLayerHeightLevel(gTerrainHeight3, gTerrainLayerST[3], lp, w, TerrainHeightMip(gTerrainHeight3, gTerrainLayerST[3], spacing)) - gTerrainLayerHeight[3].y) * gTerrainLayerHeight[3].x;
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
float3 TerrainTessPosition(TerrainCP a, TerrainCP b, TerrainCP c, float3 w, out float2 uv, out float3 n, out float spacingOut, out float dOut)
{
    precise const float3 p = a.PosW * w.x + b.PosW * w.y + c.PosW * w.z;
    precise const float2 t = a.UV * w.x + b.UV * w.y + c.UV * w.z;
    uv = t;
    n = TerrainNormalPrecise(t);
    spacingOut = 1.0f;
    dOut = 0.0f;
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
    spacingOut = spacing;
    dOut = d;
    return moved;
}

// ---- 민 면의 법선 (본 패스 Domain): 높이 변위의 기울기 — 지형 면을 따라 x · z 로 조금 옮긴 두 점도 밀어서.
//  픽셀 셰이더에 높이 맵을 더 묶지 않는다 (OpenGL · GLES 의 픽셀 단계 샘플러 32 개 한도 — 지형 PS 가 이미 가득)
float3 TerrainDisplacedNormal(float3 lp, float2 uv, float3 n, float spacing, float d0)
{
    if (gTerrainHeightParams.x < 0.5f)
        return n;
    const float e = max(spacing * 0.5f, 0.01f);
    const float hx = -n.x / max(n.y, 0.05f), hz = -n.z / max(n.y, 0.05f);   // 지형 면의 기울기 (높이맵 법선에서)
    const float3 ox = float3(e, hx * e, 0.0f), oz = float3(0.0f, hz * e, e);
    const float dx = TerrainDisplacement(lp + ox, uv + float2(e / gTerrainSize.x, 0.0f), n, spacing);
    const float dz = TerrainDisplacement(lp + oz, uv + float2(0.0f, e / gTerrainSize.z), n, spacing);
    const float3 px = ox + n * (dx - d0), pz = oz + n * (dz - d0);
    const float3 nn = cross(pz, px);
    return dot(nn, nn) > 1e-20f ? normalize(nn) : n;
}

#endif
