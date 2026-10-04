// 오클루전 컬링 (Hi-Z, 굽기 없음) — Source/Graphics/DX11/OcclusionCulling.cpp 가 기법마다 Dispatch 한다.
// .fx 효과라 DirectX 11 = Effects11, OpenGL = ShaderCross (DXC → SPIR-V → GLSL) 로 같은 소스를 쓴다.
//  PrepareTech  : 렌더러마다 지난 프레임에 보였는가 (gHistory 의 프레임 번호) → gFlags 비트 1
//  CompactTech  : 고른 렌더러의 월드 행렬을 묶음마다 이어 쓰고 간접 그리기 인자의 InstanceCount 를 센다
//                 gMode 0 = 지난 프레임에 보임 (깊이 프리패스 1 단계), 1 = 새로 보임 (2 단계), 2 = 지금 보임 (본 패스 · 그림자)
//  ReduceTech   : 깊이 버퍼 → Hi-Z 0 번 (반 해상도 — 칸 하나 = 깊이 2x2), 그 위로 밉마다 아래 2x2 의 가장 먼 깊이
//  CullTech     : 렌더러 상자를 화면에 투영해 덮는 Hi-Z 칸의 가장 먼 깊이보다 상자의 가장 가까운 깊이가 앞이면 보임 → gFlags 비트 0, gHistory
//  CullListTech : CPU 가 만든 인스턴스 목록 (나무) — 경계 구로 같은 검사, 보이는 것만 이어 쓰고 목록의 그리기 인자를 센다
//  ShadowCullTech : 그림자 캐스터 — 빛 방향으로 쓸어 늘린 상자 (CPU 가 만든다) 를 카메라 Hi-Z 로
//  BoxTech      : Skinned Mesh Renderer 의 상자를 오클루전 예측 쿼리로 그린다 (색 · 깊이 쓰기 없음)
//  BoxCullTech  : 같은 상자를 Hi-Z 로 (OpenGL ES — 조건부 렌더링이 없어 상자마다 0 / 1 을 쓰고, 그리기의 InstanceCount 로 복사한다)

cbuffer cbOcclusion
{
    row_major float4x4 gViewProj;
    float2 gViewSize;   // 뷰포트 (픽셀)
    uint2 gViewOrigin;  // ReduceCS 가 읽는 자리의 시작 (깊이 텍스처 = 뷰포트 왼쪽 위, 밉 = 0)
    uint2 gSrcSize;     // ReduceCS: 아래 밉 크기
    uint2 gDstSize;     // ReduceCS: 만드는 밉 크기
    uint gCount;        // 렌더러 · 항목 수
    uint gFrame;        // 이 뷰의 프레임 번호 (1 부터)
    uint gMode;         // CompactCS 의 고르기 · CullListCS 의 그리기 수
    uint gLevels;       // Hi-Z 밉 수 (0 = 깊이 없음 → 모두 보임)
    uint gStride;       // CullListCS: 인스턴스 크기 (uint 수)
    uint gSrcLevel;     // ReduceCS: gInput 의 밉 (OpenGL ES 는 텍스처 뷰가 없어 Hi-Z 전체에서 앞 밉을 읽는다, 그 밖에는 0)
    float4 gBoxMin;     // BoxVS (xyz) — float3 이면 D3D 는 앞 레지스터 끝 (116) 에 넣지만 GL std140 은 16 의 배수여야 해서 변환이 실패한다
    float4 gBoxMax;
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

StructuredBuffer<Caster> gCasters;
StructuredBuffer<Item> gItems;
StructuredBuffer<World> gWorlds;
StructuredBuffer<uint> gSrc;            // 인스턴스 목록 (gStride uint 씩)
StructuredBuffer<float4> gSpheres;      // xyz = 중심, w = 반지름
StructuredBuffer<float4> gBoxes;        // BoxCullCS: 상자마다 최소 · 최대 (xyz)
ByteAddressBuffer gFlagsIn;
Texture2D<float> gInput;                // ReduceCS: 깊이 버퍼 또는 아래 밉 하나
Texture2D<float> gHiZ;                  // 모든 밉
RWByteAddressBuffer gHistory;
RWByteAddressBuffer gFlags;
RWByteAddressBuffer gCounters;          // CullCS: 0 = 검사, 4 = 보임. 목록 · 그림자도 각자의 카운터를 여기에
RWByteAddressBuffer gArgs;              // 묶음 · 그리기마다 20 바이트 (InstanceCount = +4)
RWByteAddressBuffer gOut;               // 인스턴스 정점 버퍼 (Compact = 월드 행렬 64 바이트씩, List = gStride uint 씩)
RWTexture2D<float> gDst;

bool HiZVisible(float3 mn, float3 mx)
{
    float2 lo = float2(1e30, 1e30), hi = float2(-1e30, -1e30);
    float zNear = 1.0;
    bool behind = false;
    [unroll]
    for (uint i = 0; i < 8; ++i)
    {
        const float3 p = float3((i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 4) ? mx.z : mn.z);
        const float4 c = mul(float4(p, 1.0), gViewProj);
        behind = behind || c.w <= 1e-4;
        const float3 n = c.xyz / max(c.w, 1e-4);
        lo = min(lo, n.xy);
        hi = max(hi, n.xy);
        zNear = min(zNear, n.z);
    }
    if (behind)
        return true;                // 상자가 카메라 앞면을 넘는다 → 그린다
    if (zNear <= 0.0)
        return true;                // 가까운 면 앞 (깊이 0 이하) — 보수적으로 그린다
    if (hi.x < -1.0 || lo.x > 1.0 || hi.y < -1.0 || lo.y > 1.0)
        return false;               // 투영이 화면 밖 (절두체 검사는 CPU 가 이미 했다 — 걸친 상자의 모서리만 밖)
    // 화면 사각형 (픽셀, 위 = 0)
    float2 p0 = float2(lo.x * 0.5 + 0.5, 0.5 - hi.y * 0.5) * gViewSize;
    float2 p1 = float2(hi.x * 0.5 + 0.5, 0.5 - lo.y * 0.5) * gViewSize;
    p0 = clamp(p0, 0.0, gViewSize - 1.0);
    p1 = clamp(p1, 0.0, gViewSize - 1.0);
    // 칸 크기 = 사각형의 1/4 ~ 1/2 인 밉 (Hi-Z 밉 lv 의 칸 = 픽셀 2^(lv+1)) → 덮는 칸은 많아야 5x5.
    //  한 칸 = 사각형 크기로 고르면 (2x2) 덮는 영역이 사각형의 4 배까지 넓어져 벽 위 하늘까지 들어간다
    //  (밉의 마지막 칸은 남는 줄까지 덮으므로 넘으면 거기로)
    const float size = max(p1.x - p0.x, p1.y - p0.y);
    const uint L = max((uint)ceil(log2(max(size, 1.0))), 1u);
    const uint level = min(L > 3 ? L - 3 : 0, gLevels - 1);
    const uint shift = level + 1;
    const uint2 last = max(uint2(gViewSize) >> shift, uint2(1, 1)) - 1;
    const uint2 t0 = min((uint2)p0 >> shift, last), t1 = min((uint2)p1 >> shift, last);
    float d = 0.0;
    for (uint y = t0.y; y <= t1.y; ++y)
        for (uint x = t0.x; x <= t1.x; ++x)
            d = max(d, gHiZ.Load(int3(x, y, level)));
    return zNear <= d;
}

[numthreads(64, 1, 1)]
void PrepareCS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gCount)
        return;
    const uint last = gHistory.Load(gCasters[id.x].Slot * 4);
    gFlags.Store(id.x * 4, (gFrame > 1 && last == gFrame - 1) ? 2u : 0u);
}

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
            d = max(d, gInput.Load(int3(gViewOrigin + uint2(x, y), gSrcLevel)));
    gDst[id.xy] = d;
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

[numthreads(64, 1, 1)]
void CullListCS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gCount)
        return;
    gCounters.InterlockedAdd(0, 1);
    const float4 sp = gSpheres[id.x];
    if (gLevels > 0 && !HiZVisible(sp.xyz - sp.w, sp.xyz + sp.w))
        return;
    gCounters.InterlockedAdd(4, 1);
    uint n;
    gArgs.InterlockedAdd(4, 1, n);
    for (uint d = 1; d < gMode; ++d)
    {
        uint t;
        gArgs.InterlockedAdd(d * 20 + 4, 1, t);
    }
    for (uint k = 0; k < gStride; ++k)
        gOut.Store((n * gStride + k) * 4, gSrc[id.x * gStride + k]);
}

[numthreads(64, 1, 1)]
void ShadowCullCS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gCount)
        return;
    const Caster c = gCasters[id.x];
    const bool visible = gLevels == 0 || HiZVisible(c.Min, c.Max);
    gFlags.Store(id.x * 4, visible ? 1u : 0u);
    gCounters.InterlockedAdd(0, 1);
    if (visible)
        gCounters.InterlockedAdd(4, 1);
}

// Skinned Mesh Renderer 상자 (OpenGL ES): 보이면 1 → gFlags (상자마다 4 바이트), 가려진 수 · 검사 수 → gCounters 0 · 4
[numthreads(64, 1, 1)]
void BoxCullCS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gCount)
        return;
    const bool visible = gLevels == 0 || HiZVisible(gBoxes[id.x * 2].xyz, gBoxes[id.x * 2 + 1].xyz);
    gFlags.Store(id.x * 4, visible ? 1u : 0u);
    gCounters.InterlockedAdd(4, 1);
    if (!visible)
        gCounters.InterlockedAdd(0, 1);
}

// 상자 36 정점을 SV_VertexID 로 (면 6 개 × 삼각형 2 개 — 모서리 번호: 비트 0 = x, 1 = y, 2 = z)
float4 BoxVS(uint id : SV_VertexID) : SV_Position
{
    static const uint kCorner[36] = {
        0, 2, 3, 0, 3, 1,   4, 5, 7, 4, 7, 6,   0, 1, 5, 0, 5, 4,
        2, 6, 7, 2, 7, 3,   0, 4, 6, 0, 6, 2,   1, 3, 7, 1, 7, 5 };
    const uint c = kCorner[id % 36];
    const float3 p = float3((c & 1) ? gBoxMax.x : gBoxMin.x, (c & 2) ? gBoxMax.y : gBoxMin.y, (c & 4) ? gBoxMax.z : gBoxMin.z);
    return mul(float4(p, 1.0), gViewProj);
}

DepthStencilState BoxDepth
{
    DepthEnable = TRUE;
    DepthWriteMask = ZERO;
    DepthFunc = LESS_EQUAL;
};

BlendState BoxNoColor
{
    BlendEnable[0] = FALSE;
    RenderTargetWriteMask[0] = 0;
};

RasterizerState BoxNoCull
{
    FillMode = SOLID;
    CullMode = NONE;
};

technique11 PrepareTech { pass P0 { SetComputeShader(CompileShader(cs_5_0, PrepareCS())); } }
technique11 CompactTech { pass P0 { SetComputeShader(CompileShader(cs_5_0, CompactCS())); } }
technique11 ReduceTech { pass P0 { SetComputeShader(CompileShader(cs_5_0, ReduceCS())); } }
technique11 CullTech { pass P0 { SetComputeShader(CompileShader(cs_5_0, CullCS())); } }
technique11 CullListTech { pass P0 { SetComputeShader(CompileShader(cs_5_0, CullListCS())); } }
technique11 ShadowCullTech { pass P0 { SetComputeShader(CompileShader(cs_5_0, ShadowCullCS())); } }
technique11 BoxCullTech { pass P0 { SetComputeShader(CompileShader(cs_5_0, BoxCullCS())); } }

technique11 BoxTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, BoxVS()));
        SetGeometryShader(NULL);
        SetPixelShader(NULL);
        SetDepthStencilState(BoxDepth, 0);
        SetBlendState(BoxNoColor, float4(0.0f, 0.0f, 0.0f, 0.0f), 0xFFFFFFFF);
        SetRasterizerState(BoxNoCull);
    }
}
