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

// 레이어 텍스처 혼합 (컨트롤 맵 RGBA = 레이어 0~3 가중치)
float4 TerrainAlbedo(float2 uv)
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

    float2 pos = uv * gTerrainSize.xz;   // 지형 로컬 미터
    float4 c = 0;
    c += w.r * gTerrainLayer0.Sample(samTerrainWrap, pos * gTerrainLayerST[0].xy + gTerrainLayerST[0].zw) * gTerrainLayerTint[0];
    c += w.g * gTerrainLayer1.Sample(samTerrainWrap, pos * gTerrainLayerST[1].xy + gTerrainLayerST[1].zw) * gTerrainLayerTint[1];
    c += w.b * gTerrainLayer2.Sample(samTerrainWrap, pos * gTerrainLayerST[2].xy + gTerrainLayerST[2].zw) * gTerrainLayerTint[2];
    c += w.a * gTerrainLayer3.Sample(samTerrainWrap, pos * gTerrainLayerST[3].xy + gTerrainLayerST[3].zw) * gTerrainLayerTint[3];
    c.a = 1.0f;
    return c;
}
