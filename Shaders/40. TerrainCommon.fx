//=============================================================================
// 40. TerrainCommon.fx  (NOVA 지형 공통 - 다른 .fx 에서 include)
//
// 정점 버퍼 없이 SV_VertexID(= 인덱스 버퍼 값) 로 쿼드트리 노드의 격자점을 만든다.
//  - 모든 노드는 같은 격자(한 변 gTerrainPatch.w 정점)이고, 격자 한 칸 = 높이맵 gTerrainPatch.z 칸
//    → 큰(먼) 노드는 높이맵을 성기게, 작은(가까운) 노드는 촘촘하게 샘플링한다
//  - 높이는 높이맵 텍스처(R32_FLOAT, 0~1)에서 읽어 편집 즉시 반영된다
//=============================================================================

cbuffer cbTerrain
{
    float4 gTerrainPatch;          // xy = 노드 시작 격자점(높이맵), z = 격자 간격(높이맵 칸), w = 한 변 정점 수
    float4 gTerrainSize;           // xyz = 지형 크기(m), w = 높이맵 해상도
    float4 gTerrainOrigin;         // xyz = 지형 월드 위치
    float4 gTerrainLayerST[4];     // xy = 1 / 타일 크기, zw = 오프셋 / 타일 크기
    float4 gTerrainLayerTint[4];
    int gTerrainLayerCount;
    int gTerrainUseColorMap;       // 1 = 컬러 맵(생성기의 색 재질)을 쓴다
};

Texture2D<float> gTerrainHeightMap;
Texture2D gTerrainControl;
Texture2D gTerrainColorMap;        // rgb = 색(sRGB), a = 색이 레이어 텍스처 색을 대신하는 정도
Texture2D gTerrainLayer0;
Texture2D gTerrainLayer1;
Texture2D gTerrainLayer2;
Texture2D gTerrainLayer3;

SamplerState samTerrainClamp
{
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = CLAMP;
    AddressV = CLAMP;
};

SamplerState samTerrainWrap
{
    Filter = ANISOTROPIC;
    MaxAnisotropy = 8;
    AddressU = WRAP;
    AddressV = WRAP;
};

float TerrainHeightAt(int2 cell)
{
    int res = (int) gTerrainSize.w;
    cell = clamp(cell, int2(0, 0), int2(res - 1, res - 1));
    return gTerrainHeightMap.Load(int3(cell, 0)) * gTerrainSize.y;
}

// uv(0~1) 위치의 높이 (쌍선형, 미터)
float TerrainHeightUV(float2 uv)
{
    float res = gTerrainSize.w;
    float2 tc = (uv * (res - 1.0f) + 0.5f) / res;
    return gTerrainHeightMap.SampleLevel(samTerrainClamp, tc, 0) * gTerrainSize.y;
}

// 격자점의 월드 위치. uv = 지형 전체 기준 0~1
float3 TerrainVertexWorld(uint vid, out float2 uv)
{
    uint side = (uint) gTerrainPatch.w;
    int step = (int) gTerrainPatch.z;
    uint grid = side * side;
    int2 local;
    float drop = 0.0f;
    if (vid >= grid)
    {
        // 스커트 (월드 타일 가장자리, gTerrainOrigin.w = 깊이 m): 가장자리 격자점을 아래로 내린 정점 — 해상도가 다른 이웃 타일과의 틈을 가린다.
        //  번호 = grid + 변 (0 -X, 1 +X, 2 -Z, 3 +Z) * side + 변 위 차례
        uint k = vid - grid;
        uint s = k / side;
        int i = (int) (k % side);
        int last = (int) side - 1;
        local = s == 0 ? int2(0, i) : (s == 1 ? int2(last, i) : (s == 2 ? int2(i, 0) : int2(i, last)));
        drop = gTerrainOrigin.w;
    }
    else
        local = int2(vid % side, vid / side);
    int2 cell = int2(gTerrainPatch.xy) + local * step;
    uv = (float2) cell / (gTerrainSize.w - 1.0f);
    return gTerrainOrigin.xyz + float3(uv.x * gTerrainSize.x, TerrainHeightAt(cell) - drop, uv.y * gTerrainSize.z);
}

// 높이맵 중앙 차분 법선 (픽셀 단위로 계산해 LOD 가 낮아도 조명은 원래 해상도)
float3 TerrainNormalUV(float2 uv)
{
    float res = gTerrainSize.w;
    float d = 1.0f / (res - 1.0f);
    float hl = TerrainHeightUV(uv - float2(d, 0));
    float hr = TerrainHeightUV(uv + float2(d, 0));
    float hd = TerrainHeightUV(uv - float2(0, d));
    float hu = TerrainHeightUV(uv + float2(0, d));
    float dx = 2.0f * gTerrainSize.x * d;
    float dz = 2.0f * gTerrainSize.z * d;
    return normalize(float3(-(hr - hl) / dx, 1.0f, -(hu - hd) / dz));
}

// ---- Triplanar: 위(xz) + 옆(zy, xy) 세 방향 투영을 법선으로 섞는다 (절벽에서 텍스처가 세로로 늘어나지 않게)
//  가중치 = |법선|^4 (정규화). 완만한 곳은 옆 가중치가 거의 0 이라 그 샘플을 건너뛴다(분기).
//  분기 안에서도 밉이 맞도록 좌표 미분은 분기 밖에서 구해 SampleGrad 로 넘긴다
struct TerrainTriplanar
{
    float2 Top, SideX, SideZ;           // 투영 좌표 (미터)
    float2 TopDx, TopDy, XDx, XDy, ZDx, ZDy;
    float3 W;                           // x = zy 면, y = 위, z = xy 면
};

TerrainTriplanar TerrainTriplanarSetup(float3 localPos, float3 n)
{
    TerrainTriplanar t;
    t.Top = localPos.xz;
    t.SideX = float2(localPos.z, -localPos.y);   // x 를 향한 면: 가로 = z, 세로 = 높이 (텍스처 위가 위쪽)
    t.SideZ = float2(localPos.x, -localPos.y);   // z 를 향한 면
    t.TopDx = ddx(t.Top); t.TopDy = ddy(t.Top);
    t.XDx = ddx(t.SideX); t.XDy = ddy(t.SideX);
    t.ZDx = ddx(t.SideZ); t.ZDy = ddy(t.SideZ);
    float3 w = pow(abs(n), 4.0f);
    w /= max(w.x + w.y + w.z, 1e-5f);
    // 아주 작은 옆 가중치는 버리고 다시 정규화 (평지에서 샘플 1 번)
    w.x = w.x < 0.02f ? 0.0f : w.x;
    w.z = w.z < 0.02f ? 0.0f : w.z;
    t.W = w / max(w.x + w.y + w.z, 1e-5f);
    return t;
}

// ---- 타일 반복 없애기: 확률적 텍스처링 (Heitz & Neyret 2018 의 삼각 격자, Mikkelsen hex tiling 과 같은 생각)
//  텍스처 좌표를 삼각 격자(한 변 약 0.6 장)로 나누고 꼭짓점마다 해시로 무작위 오프셋을 준 세 샘플을 무게중심 가중치로 섞는다.
//  가중치를 거듭제곱해 경계를 좁히고, 섞은 색을 평균(가장 작은 밉) 둘레로 분산이 줄지 않게 되돌린다 (흐려지지 않게).
//  노이즈 텍스처 없이 해시. 위(xz) 투영에만 쓴다 (옆 면은 짧아 반복이 덜 보인다)
float2 TerrainHash2(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * float3(0.1031f, 0.1030f, 0.0973f));
    p3 += dot(p3, p3.yzx + 33.33f);
    return frac((p3.xx + p3.yz) * p3.zy);
}

float4 TerrainSampleNoTile(Texture2D tex, float2 uv, float2 dx, float2 dy)
{
    // 삼각 격자로 비틀기 (정삼각형 → 직각 격자)
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
    const float4 a = tex.SampleGrad(samTerrainWrap, uv + TerrainHash2(v1) * 7.0f, dx, dy);
    const float4 b = tex.SampleGrad(samTerrainWrap, uv + TerrainHash2(v2) * 7.0f, dx, dy);
    const float4 c = tex.SampleGrad(samTerrainWrap, uv + TerrainHash2(v3) * 7.0f, dx, dy);
    // 경계를 좁게 (가중치^4) → 분산 보존 섞기
    float3 wp = w * w;
    wp *= wp;
    wp /= wp.x + wp.y + wp.z;
    const float4 mean = tex.SampleLevel(samTerrainWrap, float2(0.5f, 0.5f), 16);
    const float4 mixed = a * wp.x + b * wp.y + c * wp.z;
    const float k = rsqrt(max(dot(wp, wp), 1e-4f));
    return saturate(mean + (mixed - mean) * lerp(1.0f, k, 0.6f));
}

float4 TerrainLayerSample(Texture2D tex, float4 st, TerrainTriplanar t)
{
    float4 c = 0;
    [branch] if (t.W.y > 0.0f)
        c += t.W.y * TerrainSampleNoTile(tex, t.Top * st.xy + st.zw, t.TopDx * st.xy, t.TopDy * st.xy);
    [branch] if (t.W.x > 0.0f)
        c += t.W.x * tex.SampleGrad(samTerrainWrap, t.SideX * st.xy + st.zw, t.XDx * st.xy, t.XDy * st.xy);
    [branch] if (t.W.z > 0.0f)
        c += t.W.z * tex.SampleGrad(samTerrainWrap, t.SideZ * st.xy + st.zw, t.ZDx * st.xy, t.ZDy * st.xy);
    return c;
}

// 컨트롤 맵 가중치 (RGBA = 레이어 0~3, 레이어 수만큼, 합 1)
float4 TerrainControlWeights(float2 uv)
{
    float4 w = gTerrainControl.Sample(samTerrainClamp, uv);
    if (gTerrainLayerCount < 4) w.a = 0.0f;
    if (gTerrainLayerCount < 3) w.b = 0.0f;
    if (gTerrainLayerCount < 2) w.g = 0.0f;
    const float sum = w.r + w.g + w.b + w.a;
    return sum < 1e-4f ? float4(1, 0, 0, 0) : w / sum;
}

float4 TerrainAlbedoW(float4 w, float2 uv, TerrainTriplanar t, float viewDist);

// 레이어 텍스처 혼합 (컨트롤 맵 RGBA = 레이어 0~3 가중치). localPos = 지형 로컬 위치(m), n = 월드 법선, viewDist = 카메라 거리(m)
float4 TerrainAlbedo(float2 uv, float3 localPos, float3 n, float viewDist)
{
    if (gTerrainLayerCount <= 0)
        return float4(0.72f, 0.72f, 0.72f, 1.0f);   // 레이어가 없으면 Unity 처럼 밝은 회색
    return TerrainAlbedoW(TerrainControlWeights(uv), uv, TerrainTriplanarSetup(localPos, n), viewDist);
}

// 가중치 w (높이 기반으로 섞은 것도) · 삼평면 t (POM 으로 옮긴 위치도) 로 색
float4 TerrainAlbedoW(float4 w, float2 uv, TerrainTriplanar t, float viewDist)
{
    if (gTerrainLayerCount <= 0)
        return float4(0.72f, 0.72f, 0.72f, 1.0f);
    float4 c = 0;
    [branch] if (w.r > 0.0f) c += w.r * TerrainLayerSample(gTerrainLayer0, gTerrainLayerST[0], t) * gTerrainLayerTint[0];
    [branch] if (w.g > 0.0f) c += w.g * TerrainLayerSample(gTerrainLayer1, gTerrainLayerST[1], t) * gTerrainLayerTint[1];
    [branch] if (w.b > 0.0f) c += w.b * TerrainLayerSample(gTerrainLayer2, gTerrainLayerST[2], t) * gTerrainLayerTint[2];
    [branch] if (w.a > 0.0f) c += w.a * TerrainLayerSample(gTerrainLayer3, gTerrainLayerST[3], t) * gTerrainLayerTint[3];
    c.a = 1.0f;

    // World Creator 식 색 재질: 컬러 맵 색 × 텍스처 명암 디테일 (텍스처 밝기 / 그 텍스처의 평균 밝기)
    [branch] if (gTerrainUseColorMap != 0)
    {
        const float4 cm = gTerrainColorMap.Sample(samTerrainClamp, uv);
        [branch] if (cm.a > 0.003f)
        {
            float3 avg = 0;   // 레이어 평균 색 = 가장 작은 밉
            [branch] if (w.r > 0.0f) avg += w.r * gTerrainLayer0.SampleLevel(samTerrainWrap, float2(0.5f, 0.5f), 16).rgb * gTerrainLayerTint[0].rgb;
            [branch] if (w.g > 0.0f) avg += w.g * gTerrainLayer1.SampleLevel(samTerrainWrap, float2(0.5f, 0.5f), 16).rgb * gTerrainLayerTint[1].rgb;
            [branch] if (w.b > 0.0f) avg += w.b * gTerrainLayer2.SampleLevel(samTerrainWrap, float2(0.5f, 0.5f), 16).rgb * gTerrainLayerTint[2].rgb;
            [branch] if (w.a > 0.0f) avg += w.a * gTerrainLayer3.SampleLevel(samTerrainWrap, float2(0.5f, 0.5f), 16).rgb * gTerrainLayerTint[3].rgb;
            const float3 lumW = float3(0.299f, 0.587f, 0.114f);
            float detail = clamp(dot(c.rgb, lumW) / max(dot(avg, lumW), 0.03f), 0.35f, 1.9f);
            // 멀수록 텍스처 명암을 줄인다: 타일 반복 무늬가 넓은 줄무늬로 보이지 않게 (먼 곳은 컬러 맵이 주인공)
            detail = lerp(detail, 1.0f, 0.75f * saturate((viewDist - 60.0f) / 400.0f));
            c.rgb = lerp(c.rgb, cm.rgb * detail, cm.a);
        }
    }
    return c;
}
