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
};

Texture2D<float> gTerrainHeightMap;
Texture2D gTerrainControl;
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
    int2 cell = int2(gTerrainPatch.xy) + int2(vid % side, vid / side) * step;
    uv = (float2) cell / (gTerrainSize.w - 1.0f);
    return gTerrainOrigin.xyz + float3(uv.x * gTerrainSize.x, TerrainHeightAt(cell), uv.y * gTerrainSize.z);
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

float4 TerrainLayerSample(Texture2D tex, float4 st, TerrainTriplanar t)
{
    float4 c = 0;
    [branch] if (t.W.y > 0.0f)
        c += t.W.y * tex.SampleGrad(samTerrainWrap, t.Top * st.xy + st.zw, t.TopDx * st.xy, t.TopDy * st.xy);
    [branch] if (t.W.x > 0.0f)
        c += t.W.x * tex.SampleGrad(samTerrainWrap, t.SideX * st.xy + st.zw, t.XDx * st.xy, t.XDy * st.xy);
    [branch] if (t.W.z > 0.0f)
        c += t.W.z * tex.SampleGrad(samTerrainWrap, t.SideZ * st.xy + st.zw, t.ZDx * st.xy, t.ZDy * st.xy);
    return c;
}

// 레이어 텍스처 혼합 (컨트롤 맵 RGBA = 레이어 0~3 가중치). localPos = 지형 로컬 위치(m), n = 월드 법선
float4 TerrainAlbedo(float2 uv, float3 localPos, float3 n)
{
    if (gTerrainLayerCount <= 0)
        return float4(0.72f, 0.72f, 0.72f, 1.0f);   // 레이어가 없으면 Unity 처럼 밝은 회색

    float4 w = gTerrainControl.Sample(samTerrainClamp, uv);
    if (gTerrainLayerCount < 4) w.a = 0.0f;
    if (gTerrainLayerCount < 3) w.b = 0.0f;
    if (gTerrainLayerCount < 2) w.g = 0.0f;
    float sum = w.r + w.g + w.b + w.a;
    if (sum < 1e-4f)
    {
        w = float4(1, 0, 0, 0);
        sum = 1.0f;
    }
    w /= sum;

    const TerrainTriplanar t = TerrainTriplanarSetup(localPos, n);
    float4 c = 0;
    [branch] if (w.r > 0.0f) c += w.r * TerrainLayerSample(gTerrainLayer0, gTerrainLayerST[0], t) * gTerrainLayerTint[0];
    [branch] if (w.g > 0.0f) c += w.g * TerrainLayerSample(gTerrainLayer1, gTerrainLayerST[1], t) * gTerrainLayerTint[1];
    [branch] if (w.b > 0.0f) c += w.b * TerrainLayerSample(gTerrainLayer2, gTerrainLayerST[2], t) * gTerrainLayerTint[2];
    [branch] if (w.a > 0.0f) c += w.a * TerrainLayerSample(gTerrainLayer3, gTerrainLayerST[3], t) * gTerrainLayerTint[3];
    c.a = 1.0f;
    return c;
}
