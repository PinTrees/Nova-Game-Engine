//=============================================================================
// 49. GrassField.fx  (NOVA 월드 풀밭 — 32. InstancedBasic 에서 include, 40 (지형) 뒤)
//
// World Terrain 의 바이옴 지형 위를 카메라 둘레 수백 m 까지 빽빽한 풀로 덮는다. 정점 · 인스턴스 버퍼 없이 정점 셰이더가 만든다:
//  - 점: 월드에 고정된 기본 격자 (간격 gGrassLevel.y) 의 점마다 덩어리 하나. 점의 "단계" = 격자 번호의 끝 0 비트 수
//    (단계 k 점은 2^k 칸마다 — 단계가 높을수록 성기다). 거리 d 에서 남기는 단계 L(d) = y · log2(d / d0):
//    단계가 L 보다 낮은 점은 줄어들며 사라지고 남은 점은 넓어진다 (덮임 유지) — 패치 경계 없이 밀도가 이어진다
//  - 패치 (C++ GrassField — 지형 타일 안 쿼드트리): 패치마다 그 단계 이상의 점만 (간격 2^Lp) 인스턴스로
//  - 지형: 높이 (타일 높이맵) · 풀 역할 레이어의 스플랫 (길 · 강 · 바위 · 눈은 0 — 풀이 없다) · 기후 → 바이옴 둘 (밀도 · 키 · 마름 · 너비)
//  - 색: 그 자리 바이옴의 풀 사진 재질 (40 의 배열) 을 흐린 밉으로 — 먼 풀과 땅이 같은 색, 뿌리는 어둡고 끝은 밝게 (마른 바이옴은 노랗게)
//  - 잎: SV_VertexID → 잎 · 마디 (끝으로 좁아지는 띠), 휜 모양 + 바람 (흘러가는 돌풍 + 떨림)
//=============================================================================

cbuffer cbGrass
{
    float4 gGrassPatch;      // xy = 패치 시작 (월드 xz, m), z = 패치 한 변 (m), w = 한 변 점 수
    float4 gGrassLevel;      // x = 패치 단계 Lp, y = 기본 간격 (m), z = 잎 수, w = 마디 수
    float4 gGrassCam;        // xyz = 카메라, w = 그리는 거리 (m)
    float4 gGrassFalloff;    // x = d0 (m — 이 안은 가장 촘촘), y = 단계 기울기, z = 풀 레이어 채널 (0..3), w = 시간 (초)
    float4 gGrassWind;       // xy = 바람 방향 (xz), z = 세기, w = 1 / 돌풍 물결 크기 (m)
    float4 gGrassBiome[8];   // 바이옴마다 x = 밀도 배율, y = 키 (m), z = 마름 (0..1), w = 잎 너비 배율
    float4 gGrassBiomeSlice; // x = 풀 역할 번호 (gTerrainBiomeSlices 의 성분)
};

struct GrassVertexOut
{
    float4 PosH : SV_POSITION;
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
    float3 Color : COLOR0;       // 선형 (뿌리 ~ 끝 밝기 반영 전)
    float2 Blade : TEXCOORD0;    // x = 마름, y = 잎 높이 비율 (0 뿌리 ~ 1 끝)
    float4 SsaoPosH : TEXCOORD1;
};

float GrassHash(int2 p, int k)
{
    uint h = (uint) p.x * 73856093u ^ (uint) p.y * 19349663u ^ (uint) k * 83492791u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return (h & 0xffffffu) / 16777215.0f;
}

// 격자 점의 단계 (끝 0 비트 수, 0 ~ 8)
float GrassLevelOf(int2 I)
{
    const uint m = (uint) (I.x | I.y);
    return m == 0u ? 8.0f : (float) min(firstbitlow(m), 8u);
}

GrassVertexOut GrassDead()
{
    GrassVertexOut o = (GrassVertexOut) 0;
    o.PosH = float4(0, 0, -1, 1);   // 모든 정점이 같은 자리 → 넓이 0 (그리지 않는다)
    return o;
}

GrassVertexOut GrassVS(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
    const int side = (int) gGrassPatch.w;
    const int2 local = int2((int) (iid % (uint) side), (int) (iid / (uint) side));
    const int step = 1 << (int) gGrassLevel.x;
    const float base = gGrassLevel.y;
    // 패치가 맡는 점: 패치 안 (시작 ≤ 점 < 끝) 의 step 배수 번호만 — 이웃 패치와 겹치지도 비지도 않는다
    const float stepM = base * step;
    const int2 I = int2(ceil(gGrassPatch.xy / stepM)) * step + local * step;
    if (any((float2) I * base >= gGrassPatch.xy + gGrassPatch.z))
        return GrassDead();
    const float lvl = GrassLevelOf(I);
    // 점 자리: 자기 단계 칸 안에서 흔든다 (어느 패치 · 단계로 그려도 같은 자리)
    const float cell = base * exp2(min(lvl, 6.0f));
    const float2 xz = (float2) I * base + (float2(GrassHash(I, 1), GrassHash(I, 2)) - 0.5f) * cell * 0.9f;
    const float2 toCam = xz - gGrassCam.xz;
    const float dist = length(toCam);
    const float D = max(gGrassCam.w, 1.0f);
    const float L = max(0.0f, gGrassFalloff.y * log2(max(dist, 1.0f) / max(gGrassFalloff.x, 1.0f)));
    float fade = saturate(lvl + 1.0f - L) * saturate((D - dist) / (D * 0.18f));
    if (fade <= 0.001f)
        return GrassDead();
    const float2 uv = (xz - gTerrainOrigin.xz) / gTerrainSize.xz;
    if (any(uv < 0.0f) || any(uv > 1.0f))
        return GrassDead();
    // 풀 레이어 무게 (컨트롤 맵 정규화) × 바이옴 밀도
    float4 w = gTerrainControl.SampleLevel(samTerrainClamp, uv, 0);
    const float sum = w.r + w.g + w.b + w.a;
    const int ch = (int) gGrassFalloff.z;
    const float grass = sum > 1e-4f ? w[ch] / sum : 0.0f;
    int ba, bb;
    float bt;
    TerrainBiomeIndex(uv, xz, ba, bb, bt);
    const float4 bp = lerp(gGrassBiome[ba], gGrassBiome[bb], bt);
    const float keep = saturate((grass - 0.15f) / 0.6f) * bp.x;
    if (GrassHash(I, 3) >= keep)
        return GrassDead();
    const float y0 = TerrainHeightUV(uv) + gTerrainOrigin.y;

    // 잎 · 마디: 잎마다 6 × 마디 정점 (띠 — 끝 마디는 세모가 된다)
    const int segs = max((int) gGrassLevel.w, 1);
    const int vpb = 6 * segs;
    const int blade = (int) vid / vpb;
    const int k = (int) vid % vpb;
    const int q = k / 6, c = k % 6;
    const int row = q + ((c == 2 || c == 3 || c == 5) ? 1 : 0);
    const float sideSign = (c == 1 || c == 4 || c == 5) ? 1.0f : -1.0f;
    const float t = (float) row / segs;

    // 성기게 된 만큼 덩어리를 넓힌다 (단계 하나 = 점이 1/4 → 반지름 2 배 — 덮임 유지)
    const float leff = min(L, lvl);
    const float widen = exp2(leff);
    const float clumpH = bp.y * (0.65f + 0.7f * GrassHash(I, 4)) * lerp(0.35f, 1.0f, fade) * (1.0f + leff * 0.08f);
    const float clumpR = base * 0.6f * widen;
    const float ang = GrassHash(I, 10 + blade) * 6.2831853f;
    const float rr = clumpR * sqrt(GrassHash(I, 30 + blade));
    const float face = GrassHash(I, 50 + blade) * 6.2831853f;
    // 짧은 잎 · 긴 잎이 섞인다 (짧은 것이 더 많이)
    const float hr = GrassHash(I, 70 + blade);
    const float bladeH = clumpH * (0.3f + 0.85f * hr * hr + 0.15f * hr);
    const float width = 0.017f * bp.w * exp2(leff * 0.85f) * (0.7f + 0.6f * GrassHash(I, 90 + blade)) * lerp(0.5f, 1.0f, fade);
    // 덩어리 가운데에서 바깥으로 휘어 넘어간다 (분수 꼴) + 잎마다 조금 다른 방향
    const float2 outDir = float2(cos(ang), sin(ang));
    const float2 jitDir = float2(cos(face + 1.3f), sin(face + 1.3f));
    float2 lean = normalize(outDir * 0.8f + jitDir * 0.5f + 1e-4f) * (0.35f + 0.75f * GrassHash(I, 110 + blade)) * lerp(1.0f, 0.55f, saturate(leff * 0.3f));
    // 바람: 흘러가는 돌풍 + 잎 떨림 (끝일수록)
    {
        const float2 dir = gGrassWind.xy;
        const float2 qn = xz * gGrassWind.w - dir * gGrassFalloff.w * 1.4f;
        const float gust = TerrainValueNoise(qn) * 0.7f + TerrainValueNoise(qn * 2.3f + 7.1f) * 0.3f;
        const float flutter = sin(gGrassFalloff.w * (3.3f + GrassHash(I, 130 + blade) * 2.0f) + GrassHash(I, 150 + blade) * 6.28f);
        lean += (dir * (0.1f + gust * gust * 1.3f) + float2(-dir.y, dir.x) * flutter * 0.08f) * gGrassWind.z;
    }
    const float2 bladeBase = xz + float2(cos(ang), sin(ang)) * rr;
    const float2 bend = lean * bladeH * t * t;
    const float3 sideV = float3(cos(face), 0.0f, sin(face));
    const float wt = width * pow(saturate(1.0f - t), 0.75f);
    float3 posW = float3(bladeBase.x + bend.x, y0 + bladeH * t - dot(bend, bend) / max(2.0f * bladeH, 0.05f), bladeBase.y + bend.y) + sideV * (sideSign * wt);
    // 잎 법선: 띠의 가로 × 위로 가는 방향
    const float3 up = normalize(float3(lean.x * 2.0f * t, 1.0f, lean.y * 2.0f * t));
    const float3 n = normalize(cross(sideV, up));

    // 색: 바이옴 풀 재질 (사진) 의 흐린 밉 — 땅과 같은 색
    const float sa = gTerrainBiomeSlices[ba][(int) gGrassBiomeSlice.x], sb = gTerrainBiomeSlices[bb][(int) gGrassBiomeSlice.x];
    const float2 tuv = xz * gTerrainSliceParams[(int) sa].x;
    float3 col = ToLinear(gTerrainAlbedos.SampleLevel(samTerrainWrap, float3(tuv, sa), 5.0f).rgb);
    if (bt > 0.01f)
        col = lerp(col, ToLinear(gTerrainAlbedos.SampleLevel(samTerrainWrap, float3(xz * gTerrainSliceParams[(int) sb].x, sb), 5.0f).rgb), bt);
    // 덩어리 · 잎마다 밝기 · 색 (초록 ↔ 누런) 차이 — 한 색 판처럼 보이지 않게
    col *= 0.8f + 0.35f * GrassHash(I, 5);
    const float hueJ = GrassHash(I, 170 + blade);
    col *= lerp(float3(0.85f, 1.05f, 0.8f), float3(1.12f, 1.0f, 0.75f), hueJ) * (0.8f + 0.4f * GrassHash(I, 190 + blade));
    // 마른 잎 몇 장 (바이옴 마름만큼 더)
    if (GrassHash(I, 210 + blade) < 0.08f + bp.z * 0.35f)
        col = lerp(col, float3(0.32f, 0.26f, 0.13f), 0.65f);

    GrassVertexOut o;
    o.PosW = posW;
    o.NormalW = n;
    o.Color = col;
    o.Blade = float2(saturate(bp.z + (GrassHash(I, 6) - 0.5f) * 0.3f), t);
    o.PosH = mul(float4(posW, 1.0f), gViewProj);
    o.SsaoPosH = mul(float4(posW, 1.0f), gViewProjTex);
    return o;
}

float4 GrassPS(GrassVertexOut pin, bool front : SV_IsFrontFace) : SV_Target
{
    const float3 toEye = gEyePosW - pin.PosW;
    const float distToEye = length(toEye);
    const float t = pin.Blade.y;
    // 뿌리 어둡게 (덩어리 속 그늘) · 끝 밝게, 마른 바이옴은 끝이 짚 색
    float3 c = pin.Color * lerp(0.62f, 1.22f, t);
    c = lerp(c, float3(0.42f, 0.36f, 0.2f) * (0.7f + 0.6f * t), pin.Blade.x * t * 0.6f);
    float3 N = normalize(pin.NormalW);
    if (!front)
        N = -N;
    N = normalize(lerp(N, float3(0, 1, 0), lerp(0.5f, 0.85f, saturate(distToEye / max(gGrassCam.w, 1.0f) * 1.5f))));
    LitSurface surf;
    surf.Albedo = c;
    surf.Metallic = 0.0f;
    surf.Smoothness = 0.22f;
    surf.Occlusion = lerp(0.3f, 1.0f, smoothstep(0.0f, 0.8f, t));
    surf.Emission = float3(0, 0, 0);
    surf.Transmission = c * 0.55f;
    surf.Highlights = true;
    surf.Reflections = false;
    surf.ReceiveShadows = true;
    s_WeatherPuddles = 0.0f;
    return FinishLit(ShadeLit(surf, pin.PosW, N, toEye / max(distToEye, 1e-4f), pin.SsaoPosH), 1.0f, distToEye);
}

RasterizerState GrassCullNone
{
    CullMode = None;
};

DepthStencilState GrassDepthLessEqual
{
    DepthEnable = TRUE;
    DepthWriteMask = ALL;
    DepthFunc = LESS_EQUAL;
};

#ifndef NOVA_NO_ENGINE_TECHNIQUES
technique11 GrassFieldTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, GrassVS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, GrassPS()));
        SetDepthStencilState(GrassDepthLessEqual, 0);
        SetRasterizerState(GrassCullNone);
    }
}
#endif
