// 오클루전 컬링 (Hi-Z, 굽기 없음) — Source/Graphics/DX11/OcclusionCulling.cpp 가 커널마다 cs_5_0 으로 컴파일한다 (KERNEL_* 정의)
//  1. PrepareCS : 렌더러마다 지난 프레임에 보였는가 (gHistory 의 프레임 번호) → gFlags 비트 1
//  2. CompactCS : 고른 렌더러의 월드 행렬을 묶음마다 이어 쓰고 간접 그리기 인자의 InstanceCount 를 센다
//                 gMode 0 = 지난 프레임에 보임 (깊이 프리패스 1 단계), 1 = 새로 보임 (2 단계), 2 = 지금 보임 (본 패스)
//  3. CopyDepthCS · ReduceCS : 깊이 버퍼 → Hi-Z (밉마다 아래 2x2 의 가장 먼 깊이)
//  4. CullCS : 렌더러 상자를 화면에 투영해 덮는 Hi-Z 4 칸의 가장 먼 깊이보다 상자의 가장 가까운 깊이가 앞이면 보임 → gFlags 비트 0, gHistory

cbuffer cbOcclusion : register(b0)
{
    row_major float4x4 gViewProj;
    float2 gViewSize;   // 뷰포트 (픽셀)
    uint2 gViewOrigin;  // 깊이 텍스처 안의 뷰포트 왼쪽 위
    uint2 gSrcSize;     // ReduceCS: 아래 밉 크기
    uint2 gDstSize;     // CopyDepthCS · ReduceCS: 만드는 밉 크기
    uint gCount;        // 렌더러 · 항목 수
    uint gFrame;        // 이 뷰의 프레임 번호 (1 부터)
    uint gMode;         // CompactCS 의 고르기
    uint gLevels;       // Hi-Z 밉 수 (0 = 깊이 없음 → 모두 보임)
};

struct Caster
{
    float3 Min;         // 월드 AABB
    uint Slot;          // SceneCulling 의 렌더러 번호 (지난 프레임 기록 자리)
    float3 Max;
    uint Pad;
};

struct Item             // 렌더러 × 서브셋 하나 → 묶음 하나
{
    uint Caster;
    uint Batch;
    uint Base;          // 묶음의 첫 인스턴스 자리 (= 간접 인자의 StartInstanceLocation)
};

struct World
{
    float4 R0, R1, R2, R3;
};

#if defined(KERNEL_PREPARE)

StructuredBuffer<Caster> gCasters : register(t0);
RWByteAddressBuffer gHistory : register(u0);
RWByteAddressBuffer gFlags : register(u1);

[numthreads(64, 1, 1)]
void PrepareCS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gCount)
        return;
    const uint last = gHistory.Load(gCasters[id.x].Slot * 4);
    gFlags.Store(id.x * 4, (gFrame > 1 && last == gFrame - 1) ? 2u : 0u);
}

#elif defined(KERNEL_COMPACT)

StructuredBuffer<Item> gItems : register(t0);
StructuredBuffer<World> gWorlds : register(t1);
ByteAddressBuffer gFlagsIn : register(t2);
RWByteAddressBuffer gArgs : register(u0);   // 묶음마다 IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation, StartInstanceLocation
RWByteAddressBuffer gOut : register(u1);    // 인스턴스 정점 버퍼 (월드 행렬 64 바이트씩)

[numthreads(64, 1, 1)]
void CompactCS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gCount)
        return;
    const Item it = gItems[id.x];
    const uint f = gFlagsIn.Load(it.Caster * 4);
    const bool take = gMode == 0 ? (f & 2u) != 0 : gMode == 1 ? (f & 3u) == 1u : (f & 1u) != 0;
    if (!take)
        return;
    uint n;
    gArgs.InterlockedAdd(it.Batch * 20 + 4, 1, n);
    const World w = gWorlds[it.Caster];
    const uint o = (it.Base + n) * 64;
    gOut.Store4(o, asuint(w.R0));
    gOut.Store4(o + 16, asuint(w.R1));
    gOut.Store4(o + 32, asuint(w.R2));
    gOut.Store4(o + 48, asuint(w.R3));
}

#elif defined(KERNEL_COPYDEPTH)

Texture2D<float> gInput : register(t0);
RWTexture2D<float> gDst : register(u0);

[numthreads(8, 8, 1)]
void CopyDepthCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= gDstSize))
        return;
    gDst[id.xy] = gInput.Load(int3(gViewOrigin + id.xy, 0));
}

#elif defined(KERNEL_REDUCE)

Texture2D<float> gInput : register(t0);     // 아래 밉 하나
RWTexture2D<float> gDst : register(u0);

[numthreads(8, 8, 1)]
void ReduceCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= gDstSize))
        return;
    // 밉 크기는 D3D 처럼 내림 (홀수면 아래 밉의 마지막 줄이 남는다) → 마지막 칸이 남는 줄까지 덮는다 (최대 3x3)
    const uint2 p0 = min(id.xy * 2, gSrcSize - 1);
    const uint2 p1 = uint2(id.x == gDstSize.x - 1 ? gSrcSize.x - 1 : p0.x + 1, id.y == gDstSize.y - 1 ? gSrcSize.y - 1 : p0.y + 1);
    float d = 0.0;
    for (uint y = p0.y; y <= p1.y; ++y)
        for (uint x = p0.x; x <= p1.x; ++x)
            d = max(d, gInput.Load(int3(x, y, 0)));
    gDst[id.xy] = d;
}

#elif defined(KERNEL_CULL)

StructuredBuffer<Caster> gCasters : register(t0);
Texture2D<float> gHiZ : register(t1);       // 모든 밉
RWByteAddressBuffer gHistory : register(u0);
RWByteAddressBuffer gFlags : register(u1);
RWByteAddressBuffer gCounters : register(u2);   // 0 = 검사, 4 = 보임

bool HiZVisible(float3 mn, float3 mx)
{
    float2 lo = float2(1e30, 1e30), hi = float2(-1e30, -1e30);
    float zNear = 1.0;
    [unroll]
    for (uint i = 0; i < 8; ++i)
    {
        const float3 p = float3((i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 4) ? mx.z : mn.z);
        const float4 c = mul(float4(p, 1.0), gViewProj);
        if (c.w <= 1e-4)
            return true;            // 상자가 카메라 앞면을 넘는다 → 그린다
        const float3 n = c.xyz / c.w;
        lo = min(lo, n.xy);
        hi = max(hi, n.xy);
        zNear = min(zNear, n.z);
    }
    if (zNear <= 0.0)
        return true;                // 가까운 면 앞 (깊이 0 이하) — 보수적으로 그린다
    if (hi.x < -1.0 || lo.x > 1.0 || hi.y < -1.0 || lo.y > 1.0)
        return false;               // 투영이 화면 밖 (절두체 검사는 CPU 가 이미 했다 — 걸친 상자의 모서리만 밖)
    // 화면 사각형 (픽셀, 위 = 0)
    float2 p0 = float2(lo.x * 0.5 + 0.5, 0.5 - hi.y * 0.5) * gViewSize;
    float2 p1 = float2(hi.x * 0.5 + 0.5, 0.5 - lo.y * 0.5) * gViewSize;
    p0 = clamp(p0, 0.0, gViewSize - 1.0);
    p1 = clamp(p1, 0.0, gViewSize - 1.0);
    // 사각형이 한 칸 이하가 되는 밉 → 덮는 칸은 많아야 2x2 (밉의 마지막 칸은 남는 줄까지 덮으므로 넘으면 거기로)
    const float size = max(p1.x - p0.x, p1.y - p0.y);
    const uint level = min((uint)ceil(log2(max(size, 1.0))), gLevels - 1);
    const uint2 last = max(uint2(gViewSize) >> level, uint2(1, 1)) - 1;
    const uint2 t0 = min((uint2)p0 >> level, last), t1 = min((uint2)p1 >> level, last);
    const float d = max(max(gHiZ.Load(int3(t0, level)), gHiZ.Load(int3(t1.x, t0.y, level))),
                        max(gHiZ.Load(int3(t0.x, t1.y, level)), gHiZ.Load(int3(t1, level))));
    return zNear <= d;
}

[numthreads(64, 1, 1)]
void CullCS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gCount)
        return;
    const Caster c = gCasters[id.x];
    const bool visible = gLevels == 0 || HiZVisible(c.Min, c.Max);
    uint f = gFlags.Load(id.x * 4);
    if (visible)
    {
        f |= 1u;
        gHistory.Store(c.Slot * 4, gFrame);
        gCounters.InterlockedAdd(4, 1);
    }
    gFlags.Store(id.x * 4, f);
    gCounters.InterlockedAdd(0, 1);
}

#endif
