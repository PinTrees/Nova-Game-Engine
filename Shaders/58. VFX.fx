//=============================================================================
// 58. VFX.fx
// Unity VFX Graph 의 GPU 파티클 (Visual Effect 컴포넌트 · .vfx 에셋 — Source/Effects/VisualEffect.*).
//  - 시스템마다 파티클 버퍼 (96 바이트씩, GPU 만 쓴다) 를 compute 가 만들고 (Spawn) 움직인다 (Update).
//    블록 (Initialize · Update) 은 그래프가 만든 목록 (gProgram) 을 셰이더가 차례로 읽어 실행 — 그래프를 바꿔도 다시 컴파일하지 않는다
//  - 칸 고르기: 상태 버퍼의 카운터를 원자적으로 올려 (용량으로 나눈 나머지) — 가장 오래된 칸부터 다시 쓴다
//  - GPU Event: 죽을 때 (Trigger Event On Die) · 살아 있는 동안 초당 N 번 (Trigger Event Rate) 위치 · 속도 · 색을 이벤트 버퍼에, 하위 시스템이 그 자리에서 태어난다
//  - 그리기: 파티클 버퍼를 그대로 인스턴스 정점 버퍼로 (정점 셰이더가 GPU 버퍼를 읽지 않는다 — 휴대폰 GLES 에서도 같은 길)
//    모양은 그림 없이 셰이더가 만든다 (빛 · 별 · 고리 · 불꽃 줄기 · 연기) 또는 텍스처 + 플립북
//  - 꼬리 (Unity 의 Output Particle Strip): 파티클마다 지난 자리 몇 개를 기록 → compute 가 띠 조각 (인스턴스 정점) 을 만들어 그린다
//  - 정렬 (Alpha): 카메라 거리 키를 bitonic 정렬 (512 개는 그룹 메모리에서) → 정렬된 순서로 파티클을 복사해 그린다
//  - 경계 상자: Update 가 살아 있는 파티클의 최소 · 최대 자리를 원자적으로 모은다 (CPU 가 몇 프레임 늦게 읽어 화면 밖이면 건너뜀)
//  - 사용자 속성 (Custom Attribute): 파티클마다 float 4 칸 (Float 1 칸 · Vector3 3 칸), Set Attribute 블록 · Get Attribute 연산 노드
//  - 깊이 버퍼 충돌 (Collide with Depth Buffer): 프레임의 첫 뷰 장면 깊이에서 표면 자리 · 법선을 되살려 튕긴다
//  - Collide with SDF: 메시를 구운 거리장 (gSdf — 칸마다 float) 을 세 방향 보간해 거리 · 기울기 (법선) 로 튕긴다
//  - Output Mesh: 파티클마다 메시 하나 (인스턴스 그리기 — 메시 정점 + 파티클 버퍼), 빛 (해 · 환경광)
//=============================================================================

cbuffer cbVfx
{
    row_major float4x4 gViewProj;
    row_major float4x4 gWorld;      // Visual Effect 의 월드 행렬 (Spawn 위치 · Local 공간)
    float3 gCamRight;
    float gTime;
    float3 gCamUp;
    float gDt;
    float3 gCamPos;
    uint gCapacity;
    float3 gCamFwd;                 // 정렬 키 (카메라 앞 방향 거리)
    uint gSpawnCount;               // Spawn: 이번에 태어날 수 (이벤트면 이벤트당 수)
    uint gFromEvents;               // Spawn: 0 스스로, 1 부모의 죽음 이벤트, 2 부모의 Rate 이벤트
    uint gEventCapacity;            // 이벤트 칸 수 (죽음 · Rate 따로)
    uint gEmitOnDie;                // Update: 1 = 죽을 때 이벤트를 쓴다
    float gEmitRate;                // Update: 살아 있는 파티클 하나가 초당 쓰는 이벤트 (0 = 끔)
    uint gInitStart, gInitCount;    // gProgram 의 Initialize 블록 (시작 칸 · 블록 수)
    uint gUpdateStart, gUpdateCount;
    uint gSeed;                     // 이번 프레임 Spawn 의 씨앗
    uint gLocalSpace;               // 1 = Local (그릴 때 gWorld)
    uint gTrailPoints;              // 꼬리: 파티클마다 기록하는 자리 수 (0 = 꼬리 없음)
    float gTrailInterval;           // 꼬리: 기록 사이 (초)
    float gTrailWidth;              // 꼬리: 너비 = 파티클 크기 × 이 값
    uint gSortCount;                // 정렬: 키 수 (2 의 거듭제곱)
    uint gSortK, gSortJ;            // 정렬: bitonic 단계 (J = 0 이면 512 개 안에서 처음부터)
    float4 gOutput0;                // x 모양, y 방향 (0 카메라, 1 속도로 늘림, 2 수평), z 늘림 배율, w Soft 거리
    float4 gOutput1;                // x 세기 (HDR), y 플립북 열, z 행, w 플립북 (0 = 수명 동안 한 번, >0 = 초당 칸)
    float4 gDepthParams;            // x, y = 투영 _33, _43, z = 미사용, w = 장면 깊이 있음
    row_major float4x4 gWorldInv;   // gWorld 의 역 (Local 시스템의 깊이 충돌 결과를 되돌린다)
    row_major float4x4 gCollViewProj;     // 깊이 충돌: 깊이를 그린 뷰
    row_major float4x4 gCollInvViewProj;
    float4 gCollParams;             // x 있음, y, z = 투영 _33, _43
    float4 gCollCam;                // xyz 그 뷰의 카메라 자리
    float4 gCollViewport;           // 그 뷰의 뷰포트 (왼쪽 위 x, y, 너비, 높이 — 깊이 텍스처는 뷰보다 클 수 있다)
    float4 gSunDir;                 // Output Mesh 빛: xyz 해가 비추는 방향, w 빛 (1 = Lit)
    float4 gSunColor;               // rgb 해 색 × 세기
    float4 gAmbient;                // rgb 환경광
    row_major float4x4 gCoverVP;    // 날씨 덮개 맵: 월드 → (u, v, 깊이)
    float4 gCoverParams;            // x 있음, y 깊이 0 의 높이, z 깊이 범위 (m), w 맵 크기 (텍셀)
};

StructuredBuffer<float4> gProgram;      // 블록 목록 (VfxRuntime 이 만든다)
RWByteAddressBuffer gParticles;         // 112 바이트씩
RWByteAddressBuffer gState;             // 0 칸 카운터, 4 살아 있는 수, 8..20 최소 · 20..32 최대 자리 (순서 키), 32 최대 크기 — 4 부터는 Update 마다 CPU 가 처음 값으로
RWByteAddressBuffer gEvents;            // 0 죽음 이벤트 수, 4 Rate 이벤트 수, 16.. 죽음 이벤트 48 바이트씩 (위치 · 속도 · 색), 그 뒤 Rate 이벤트
ByteAddressBuffer gEventsIn;            // 부모 시스템의 이벤트 버퍼 (같은 배치)
RWByteAddressBuffer gTrail;             // 꼬리 기록: 파티클마다 gTrailPoints 칸 (위치, 기록한 나이)
RWByteAddressBuffer gTrailVerts;        // 꼬리 띠 조각: 64 바이트씩 (A 위치 · 너비, B 위치 · 너비, 색, u0 · u1 · 있음) — 인스턴스 정점 버퍼
RWByteAddressBuffer gSortKeys;          // 정렬: 8 바이트씩 (키 float, 칸 uint)
RWByteAddressBuffer gSorted;            // 정렬된 순서의 파티클 사본 (인스턴스 정점 버퍼)
ByteAddressBuffer gSdf;                 // Collide with SDF: 거리장 칸 (x 가 가장 빠르게, float)

Texture2D gTexture;
Texture2D gSceneDepth;
Texture2D gCollDepth;                   // 깊이 충돌 (compute 에서 Load)
Texture2D gCover;                       // Collide with Weather Cover: 위에서 본 깊이 (com.nova.weather 가 켠다)

SamplerState samVfx
{
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = CLAMP;
    AddressV = CLAMP;
};

static const uint kStride = 112;

// ---------------------------------------------------------------- 난수 · 잡음
uint Hash(uint x)
{
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
float Rand(inout uint s) { s = Hash(s); return (s & 0x00FFFFFFu) / 16777216.0f; }
float3 RandDir(inout uint s)
{
    const float z = Rand(s) * 2.0f - 1.0f;
    const float a = Rand(s) * 6.2831853f;
    const float r = sqrt(saturate(1.0f - z * z));
    return float3(r * cos(a), z, r * sin(a));
}

float Noise3(float3 p)
{
    const float3 i = floor(p);
    float3 f = p - i;
    f = f * f * (3.0f - 2.0f * f);
    const uint3 c = (uint3)(int3)i;
    float v = 0.0f;
    [unroll] for (uint k = 0; k < 8; ++k)
    {
        const uint3 o = uint3(k & 1u, (k >> 1) & 1u, (k >> 2) & 1u);
        const uint h = Hash((c.x + o.x) * 73856093u ^ (c.y + o.y) * 19349663u ^ (c.z + o.z) * 83492791u);
        const float r = (h & 0xFFFFu) / 65535.0f * 2.0f - 1.0f;
        const float3 w = lerp(1.0f - f, f, (float3)o);
        v += r * w.x * w.y * w.z;
    }
    return v;
}

float3 NoiseVec(float3 p, float octaves)
{
    float3 sum = 0.0f;
    float amp = 1.0f, freq = 1.0f, norm = 0.0f;
    const float n = clamp(round(octaves), 1.0f, 4.0f);
    // 횟수가 정해진 고리 (4) + 안 쓰는 옥타브는 건너뛴다: 횟수가 값에 따라 바뀌는 고리는 MuMu GLES 3.2 에서
    //  Turbulence 의 파티클을 통째로 망가뜨렸다 (자리가 NaN — 그려지지 않음)
    [unroll] for (int o = 0; o < 4; ++o)
    {
        [branch] if ((float)o < n)
        {
            const float3 q = p * freq;
            sum += amp * float3(Noise3(q), Noise3(q + float3(31.4f, 7.7f, 2.1f)), Noise3(q + float3(-13.1f, 19.3f, 47.9f)));
            norm += amp;
        }
        amp *= 0.5f;
        freq *= 2.0f;
    }
    return sum / max(norm, 1e-4f);
}

// ---------------------------------------------------------------- 파티클
struct Particle
{
    float3 Pos; float Age;
    float3 Vel; float Life;
    float4 Color;
    float Size; float Rot; float AngVel; uint Seed;
    float4 BaseColor;
    float BaseSize; float Spare0; float Spare1; float Spare2;
    float4 Custom;                  // 사용자 속성 4 칸
};

Particle Load(uint i)
{
    const uint a = i * kStride;
    Particle p;
    float4 v;
    v = asfloat(gParticles.Load4(a));      p.Pos = v.xyz; p.Age = v.w;
    v = asfloat(gParticles.Load4(a + 16)); p.Vel = v.xyz; p.Life = v.w;
    p.Color = asfloat(gParticles.Load4(a + 32));
    v = asfloat(gParticles.Load4(a + 48)); p.Size = v.x; p.Rot = v.y; p.AngVel = v.z; p.Seed = asuint(v.w);
    p.BaseColor = asfloat(gParticles.Load4(a + 64));
    v = asfloat(gParticles.Load4(a + 80)); p.BaseSize = v.x; p.Spare0 = v.y; p.Spare1 = v.z; p.Spare2 = v.w;
    p.Custom = asfloat(gParticles.Load4(a + 96));
    return p;
}

void Store(uint i, Particle p)
{
    const uint a = i * kStride;
    gParticles.Store4(a, asuint(float4(p.Pos, p.Age)));
    gParticles.Store4(a + 16, asuint(float4(p.Vel, p.Life)));
    gParticles.Store4(a + 32, asuint(p.Color));
    gParticles.Store4(a + 48, uint4(asuint(p.Size), asuint(p.Rot), asuint(p.AngVel), p.Seed));
    gParticles.Store4(a + 64, asuint(p.BaseColor));
    gParticles.Store4(a + 80, asuint(float4(p.BaseSize, p.Spare0, p.Spare1, p.Spare2)));
    gParticles.Store4(a + 96, asuint(p.Custom));
}

// 칸 i (0..3) 를 동적 색인 없이 (동적 색인은 블록 반복문을 펼치게 만든다)
float Lane(float4 v, uint i) { return dot(v, float4(i == 0, i == 1, i == 2, i == 3)); }

// 사용자 속성: lane 부터 width 칸 (1 = Float → 네 칸에 같은 값, 3 = Vector3)
float4 GetCustom(float4 c, uint lane, uint width)
{
    if (width <= 1u)
        return Lane(c, lane).xxxx;
    return float4(Lane(c, lane), Lane(c, lane + 1u), Lane(c, lane + 2u), 0.0f);
}

// ---------------------------------------------------------------- 블록 (gProgram: 블록마다 머리 칸 (종류, 칸 + 식 수, 값 칸 수, 식 있는 칸 비트) + 값 칸들 + 식들)
//  번호는 Source/Effects/VfxAsset.cpp 의 블록 정의표와 같다. 연산 노드 (Operator) 에 이은 값 칸은 식으로 — 파티클마다 계산 (Eval)
static Particle sP;   // 지금 블록이 보는 파티클 (식의 Age · Position · Seed …)

float4 HsvToRgb(float3 c)
{
    const float3 k = saturate(abs(frac(c.x + float3(1.0f, 2.0f / 3.0f, 1.0f / 3.0f)) * 6.0f - 3.0f) - 1.0f);
    return float4(c.z * lerp(1.0f.xxx, k, c.y), 1.0f);
}

// 스택 명령 (Source/Effects/VfxOperators.cpp 의 Op): 값은 float4, 스칼라는 네 칸에 같은 값.
//  스택은 고정 레지스터 열 개 (r0 = 맨 위) — 배열을 동적 색인으로 쓰면 fxc (DirectX 11) 가 쓰기 · 읽기를 잘못 옮겨 값이 사라졌다
#define VFX_PUSH(v) { const float4 pv_ = (v); r9 = r8; r8 = r7; r7 = r6; r6 = r5; r5 = r4; r4 = r3; r3 = r2; r2 = r1; r1 = r0; r0 = pv_; }
#define VFX_POP { r0 = r1; r1 = r2; r2 = r3; r3 = r4; r4 = r5; r5 = r6; r6 = r7; r7 = r8; r8 = r9; }

float4 Eval(uint pc, uint end)
{
    float4 r0 = 0.0f, r1 = 0.0f, r2 = 0.0f, r3 = 0.0f, r4 = 0.0f, r5 = 0.0f, r6 = 0.0f, r7 = 0.0f, r8 = 0.0f, r9 = 0.0f;
    [loop] for (uint i = pc; i < end; ++i)
    {
        const float4 ins = gProgram[i];
        const uint op = (uint)ins.x;
        if (op == 1)   // 상수 (다음 칸)
        {
            VFX_PUSH(gProgram[i + 1]);
            ++i;
        }
        else if (op >= 2 && op <= 13)
        {
            float4 v = 0.0f;
            const float t = sP.Life > 0.0f ? saturate(sP.Age / sP.Life) : 0.0f;
            if (op == 2) v = gTime.xxxx;
            else if (op == 3) v = gDt.xxxx;
            else if (op == 4) v = sP.Age.xxxx;
            else if (op == 5) v = sP.Life.xxxx;
            else if (op == 6) v = t.xxxx;
            else if (op == 7) v = float4(sP.Pos, 0.0f);
            else if (op == 8) v = float4(sP.Vel, 0.0f);
            else if (op == 9) v = sP.Color;
            else if (op == 10) v = sP.Size.xxxx;
            else if (op == 11) v = length(sP.Vel).xxxx;
            else if (op == 12) v = ((Hash(sP.Seed ^ ((uint)ins.y * 0x9E3779B9u)) & 0x00FFFFFFu) / 16777216.0f).xxxx;   // 파티클마다 고정
            else v = ((Hash(sP.Seed ^ ((uint)ins.y * 0x9E3779B9u) ^ (asuint(gTime) * 0x85EBCA6Bu)) & 0x00FFFFFFu) / 16777216.0f).xxxx;   // 프레임마다
            VFX_PUSH(v);
        }
        else if (op == 14)   // Get Attribute (사용자 속성): ins.y 첫 칸, ins.z 칸 수
            VFX_PUSH(GetCustom(sP.Custom, (uint)ins.y, (uint)ins.z))
        else if (op >= 20 && op <= 34)
        {
            // 둘: A (r1) B (r0) → 결과
            const float4 a = r1, b = r0;
            float4 r = 0.0f;
            if (op == 20) r = a + b;
            else if (op == 21) r = a - b;
            else if (op == 22) r = a * b;
            else if (op == 23) r = a / (abs(b) > 1e-12f ? b : 1e-12f);
            else if (op == 24) r = min(a, b);
            else if (op == 25) r = max(a, b);
            else if (op == 26) r = pow(abs(a), b) * sign(a);
            else if (op == 27) r = step(a, b);
            else if (op == 28) r = dot(a.xyz, b.xyz).xxxx;
            else if (op == 29) r = float4(cross(a.xyz, b.xyz), 0.0f);
            else if (op == 30) r = distance(a.xyz, b.xyz).xxxx;
            else if (op == 31) r = a - b * floor(a / (abs(b) > 1e-12f ? b : 1e-12f));
            else if (op == 32)   // Compare (x 성분): ins.y = 0 같음 · 1 다름 · 2 작음 · 3 작거나 같음 · 4 큼 · 5 크거나 같음
            {
                const uint c = (uint)ins.y;
                const bool t = c == 0 ? a.x == b.x : c == 1 ? a.x != b.x : c == 2 ? a.x < b.x : c == 3 ? a.x <= b.x : c == 4 ? a.x > b.x : a.x >= b.x;
                r = t ? 1.0f : 0.0f;
            }
            else if (op == 33) r = (a.x > 0.5f && b.x > 0.5f) ? 1.0f : 0.0f;   // And
            else r = (a.x > 0.5f || b.x > 0.5f) ? 1.0f : 0.0f;                  // Or
            VFX_POP;
            r0 = r;
        }
        else if (op >= 40 && op <= 42)
        {
            // 셋: A (r2) B (r1) C (r0)
            const float4 a = r2, b = r1, c = r0;
            float4 r = 0.0f;
            if (op == 40) r = lerp(a, b, c);
            else if (op == 41) r = clamp(a, b, c);
            else r = smoothstep(a, b, c);
            VFX_POP;
            VFX_POP;
            r0 = r;
        }
        else if (op == 44)   // Branch: Predicate (r2) True (r1) False (r0)
        {
            const float4 r = r2.x > 0.5f ? r1 : r0;
            VFX_POP;
            VFX_POP;
            r0 = r;
        }
        else if (op == 43)
        {
            // Remap: X (r4) InMin InMax OutMin OutMax (r0)
            const float4 range = abs(r2 - r3) > 1e-12f ? r2 - r3 : 1e-12f;
            const float4 r = r1 + (r4 - r3) / range * (r0 - r1);
            VFX_POP;
            VFX_POP;
            VFX_POP;
            VFX_POP;
            r0 = r;
        }
        else if (op >= 50 && op <= 63)
        {
            const float4 x = r0;
            float4 r = x;
            if (op == 50) r = abs(x);
            else if (op == 51) r = sin(x);
            else if (op == 52) r = cos(x);
            else if (op == 53) r = frac(x);
            else if (op == 54) r = saturate(x);
            else if (op == 55) r = 1.0f - x;
            else if (op == 56) r = -x;
            else if (op == 57) r = length(x.xyz).xxxx;
            else if (op == 58) r = float4(dot(x.xyz, x.xyz) > 1e-12f ? normalize(x.xyz) : float3(0, 1, 0), 0.0f);
            else if (op == 59) r = floor(x);
            else if (op == 60) r = sqrt(abs(x));
            else if (op == 61) r = round(x);
            else if (op == 62) r = HsvToRgb(x.xyz);
            else r = x.x > 0.5f ? 0.0f : 1.0f;   // Not
            r0 = r;
        }
        else if (op == 70)   // Split: 성분 ins.y
            r0 = dot(r0, float4(ins.y == 0.0f, ins.y == 1.0f, ins.y == 2.0f, ins.y == 3.0f)).xxxx;
        else if (op == 71)   // Combine: X (r3) Y Z W (r0) 의 x 성분 넷
        {
            const float4 r = float4(r3.x, r2.x, r1.x, r0.x);
            VFX_POP;
            VFX_POP;
            VFX_POP;
            r0 = r;
        }
        else if (op == 80)   // 곡선 16 칸 (다음 4 칸)
        {
            const float x = saturate(r0.x) * 15.0f;
            const uint i0 = min((uint)x, 14u);
            const float a = Lane(gProgram[i + 1 + i0 / 4], i0 % 4);
            const float b = Lane(gProgram[i + 1 + (i0 + 1) / 4], (i0 + 1) % 4);
            r0 = lerp(a, b, x - i0).xxxx;
            i += 4;
        }
        else if (op == 81)   // 그라디언트 8 칸 (다음 8 칸)
        {
            const float x = saturate(r0.x) * 7.0f;
            const uint i0 = min((uint)x, 6u);
            r0 = lerp(gProgram[i + 1 + i0], gProgram[i + 2 + i0], x - i0);
            i += 8;
        }
        else if (op == 82 || op == 83)   // 잡음: Position (r1) Frequency (r0)
        {
            const float3 q = r1.xyz * r0.x;
            const float4 r = op == 82 ? Noise3(q).xxxx : float4(NoiseVec(q, 2.0f), 0.0f);
            VFX_POP;
            r0 = r;
        }
        else if (op == 90 || op == 91)   // World 시스템의 위치 · 방향 값: Visual Effect 변환
            r0 = float4(mul(float4(r0.xyz, op == 90 ? 1.0f : 0.0f), gWorld).xyz, r0.w);
        else if (op == 95)   // 성분 바꾸기: r1[ins.y] = r0[ins.z], ins.w = 1 이면 r0 을 내린다
        {
            const float x = dot(r0, float4(ins.z == 0.0f, ins.z == 1.0f, ins.z == 2.0f, ins.z == 3.0f));
            const float4 m = float4(ins.y == 0.0f, ins.y == 1.0f, ins.y == 2.0f, ins.y == 3.0f);
            r1 = lerp(r1, x.xxxx, m);
            if (ins.w > 0.5f)
                VFX_POP;
        }
    }
    return r0;
}

// 블록이 시작할 때 식 있는 칸을 한 번 계산해 둔다 (P 마다 Eval 을 펼치면 셰이더가 너무 커진다)
static float4 sSlot[8];
static uint sMask = 0u;

void PrepareSlots(uint at)
{
    const float4 h = gProgram[at];
    sMask = (uint)h.w;
    if (sMask == 0u)
        return;
    uint e = at + 1 + (uint)h.z;
    const uint end = at + 1 + (uint)h.y;
    [loop] while (e < end)
    {
        const float4 eh = gProgram[e];
        const uint len = (uint)eh.y;
        sSlot[min((uint)eh.x, 7u)] = Eval(e + 1, e + 1 + len);
        e += 1 + len;
    }
}

float4 P(uint at, uint k)
{
    return ((sMask >> k) & 1u) != 0u ? sSlot[min(k, 7u)] : gProgram[at + 1 + k];
}

// 곡선 16 칸 (값 칸 4 개) · 그라디언트 8 칸 (값 칸 8 개) 을 t 로
float Curve16(uint at, uint first, float t)
{
    const float x = saturate(t) * 15.0f;
    const uint i0 = min((uint)x, 14u);
    const float f = x - i0;
    const float a = Lane(P(at, first + i0 / 4), i0 % 4);
    const float b = Lane(P(at, first + (i0 + 1) / 4), (i0 + 1) % 4);
    return lerp(a, b, f);
}
float4 Gradient8(uint at, uint first, float t)
{
    const float x = saturate(t) * 7.0f;
    const uint i0 = min((uint)x, 6u);
    return lerp(P(at, first + i0), P(at, first + i0 + 1), x - i0);
}

float3 ShapePosition(uint at, inout uint s, out float3 dir)
{
    const float4 a = P(at, 0);   // x 모양, y 반지름 (상자: x 크기), z y 크기, w z 크기
    const float4 b = P(at, 1);   // xyz 가운데, w 표면 (1 = 표면만, 0 = 부피)
    const float4 c = P(at, 2);   // x 호 (0..1), y 높이 (원뿔 · 선), z 원뿔 각 (도), w 토러스 안쪽 반지름
    const int shape = (int)a.x;
    const float surface = b.w;
    float3 p = 0.0f;
    dir = float3(0, 1, 0);
    if (shape == 1)   // 구
    {
        dir = RandDir(s);
        const float r = surface > 0.5f ? a.y : a.y * pow(Rand(s), 1.0f / 3.0f);
        p = dir * r;
    }
    else if (shape == 2)   // 원 (XZ)
    {
        const float ang = Rand(s) * 6.2831853f * max(c.x, 1e-3f);
        dir = float3(cos(ang), 0.0f, sin(ang));
        const float r = surface > 0.5f ? a.y : a.y * sqrt(Rand(s));
        p = dir * r;
    }
    else if (shape == 3)   // 상자
    {
        float3 u = float3(Rand(s), Rand(s), Rand(s)) - 0.5f;
        if (surface > 0.5f)
        {
            const uint axis = min((uint)(Rand(s) * 3.0f), 2u);
            const float side = Rand(s) < 0.5f ? -0.5f : 0.5f;
            u = axis == 0 ? float3(side, u.y, u.z) : (axis == 1 ? float3(u.x, side, u.z) : float3(u.x, u.y, side));
        }
        p = u * float3(a.y, a.z, a.w);
        dir = normalize(p + 1e-5f);
    }
    else if (shape == 4)   // 원뿔 (Y 위): 바닥 원 반지름 a.y, 각 c.z — 방향이 위로 벌어진다
    {
        const float ang = Rand(s) * 6.2831853f * max(c.x, 1e-3f);
        const float rr = surface > 0.5f ? 1.0f : sqrt(Rand(s));
        const float2 xz = float2(cos(ang), sin(ang)) * rr;
        const float spread = radians(c.z);
        dir = normalize(float3(xz.x * sin(spread), cos(spread), xz.y * sin(spread)));
        const float h = Rand(s) * c.y;
        p = float3(xz.x * a.y, 0.0f, xz.y * a.y) + dir * h;
    }
    else if (shape == 5)   // 선 (가운데에서 Y 로 높이만큼)
    {
        p = float3(0.0f, Rand(s) * c.y, 0.0f);
        dir = float3(0, 1, 0);
    }
    else if (shape == 6)   // 토러스 (XZ): 큰 반지름 a.y, 안쪽 c.w
    {
        const float u = Rand(s) * 6.2831853f * max(c.x, 1e-3f);
        const float v = Rand(s) * 6.2831853f;
        const float r = surface > 0.5f ? c.w : c.w * sqrt(Rand(s));
        const float3 ring = float3(cos(u), 0.0f, sin(u));
        dir = normalize(ring * cos(v) + float3(0, sin(v), 0));
        p = ring * a.y + dir * r;
    }
    // 평면: 모양은 Y 위 (XZ 바닥) 로 만든 뒤 XY (Z 를 향해 선 고리 — 포털) · YZ 로 돌린다
    const int plane = (int)P(at, 3).x;
    if (plane == 1)
    {
        p = float3(p.x, -p.z, p.y);
        dir = float3(dir.x, -dir.z, dir.y);
    }
    else if (plane == 2)
    {
        p = float3(p.y, -p.x, p.z);
        dir = float3(dir.y, -dir.x, dir.z);
    }
    return p + b.xyz;
}

// 나선 팔 (은하): 팔 수 · 감긴 정도 · 흩어짐 · 두께
float3 SpiralPosition(uint at, inout uint s, out float3 dir)
{
    const float4 a = P(at, 0);   // x 팔 수, y 반지름, z 감김 (바퀴), w 흩어짐 (라디안)
    const float4 b = P(at, 1);   // xyz 가운데, w 두께
    const float arms = max(1.0f, a.x);
    const float t = pow(Rand(s), 0.6f);                     // 가운데가 조금 더 빽빽
    const float r = a.y * t;
    const float arm = floor(Rand(s) * arms);
    const float jitter = (Rand(s) + Rand(s) + Rand(s) - 1.5f) * a.w * (0.4f + t);   // 대략 정규 분포
    const float ang = arm * 6.2831853f / arms + a.z * 6.2831853f * t + jitter;
    const float h = (Rand(s) - 0.5f) * b.w * (1.0f - t * 0.8f);
    const float3 p = float3(cos(ang) * r, h, sin(ang) * r);
    dir = float3(-sin(ang), 0.0f, cos(ang));                // 도는 방향 (접선)
    return p + b.xyz;
}

// Set Attribute: a = (첫 칸, 칸 수, 모드 0 바꾸기 · 1 더하기 · 2 곱하기), v = 값
void SetCustom(inout Particle p, uint at)
{
    const float4 a = P(at, 0), v = P(at, 1);
    const uint lane = (uint)a.x, width = (uint)a.y, mode = (uint)a.z;
    [unroll] for (uint k = 0; k < 3; ++k)
    {
        if (k >= width || lane + k > 3u)
            continue;
        const float4 m = float4(lane + k == 0u, lane + k == 1u, lane + k == 2u, lane + k == 3u);
        const float cur = Lane(p.Custom, lane + k), x = Lane(v, k);
        const float n = mode == 1u ? cur + x : (mode == 2u ? cur * x : x);
        p.Custom = lerp(p.Custom, n.xxxx, m);
    }
}

// Set Color (Initialize · Update 둘 다): m = (모드, 채도, 밝기, 세기)
float4 BlockColor(uint at, inout uint s)
{
    const float4 a = P(at, 0), b = P(at, 1), m = P(at, 2);
    float4 col = a;
    const int mode = (int)m.x;
    if (mode == 1) col = lerp(a, b, Rand(s));
    else if (mode == 2)
    {
        const float hue = Rand(s) * 6.0f;
        const float3 rgb = saturate(float3(abs(hue - 3.0f) - 1.0f, 2.0f - abs(hue - 2.0f), 2.0f - abs(hue - 4.0f)));
        col = float4(lerp(1.0f.xxx, rgb, m.y) * m.z, a.w);
    }
    col.rgb *= max(m.w, 0.0f);
    return col;
}

// 깊이 버퍼 충돌: 그 뷰의 장면 깊이 화소 → 월드 자리
float3 DepthWorld(int2 q)
{
    const float d = gCollDepth.Load(int3(q, 0)).r;
    const float2 uv = (float2(q) + 0.5f - gCollViewport.xy) / gCollViewport.zw;
    const float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    const float4 v = mul(float4(ndc, d, 1.0f), gCollInvViewProj);
    return v.xyz / v.w;
}

// a = (튕김, 마찰, 수명 줄이기, 두께 m). 날씨 덮개 맵 (위에서 본 맨 위 표면 — 지붕 · 나무 · 땅) 아래로 들어가면 그 높이에서 위로 튕긴다
//  화면에 보이지 않는 곳도 (집 안에 비가 오지 않게, 지붕 위의 튀는 물)
void CollideCover(inout Particle p, uint at)
{
    if (gCoverParams.x < 0.5f)
        return;
    const float4 a = P(at, 0);
    float3 wp = gLocalSpace != 0 ? mul(float4(p.Pos, 1.0f), gWorld).xyz : p.Pos;
    float3 wv = gLocalSpace != 0 ? mul(float4(p.Vel, 0.0f), gWorld).xyz : p.Vel;
    const float3 next = wp + wv * gDt;
    const float4 c = mul(float4(next, 1.0f), gCoverVP);
    // 맞는 쪽으로 묻는다 (NaN 이면 거짓 → 지나간다)
    if (!(all(c.xy > 0.0f) && all(c.xy < 1.0f)))
        return;
    const float d = gCover.Load(int3(int2(c.xy * gCoverParams.w), 0)).r;
    if (!(d < 0.999999f))
        return;   // 아무것도 없다
    const float topY = gCoverParams.y - d * gCoverParams.z;
    if (!(next.y <= topY && next.y >= topY - max(a.w, 0.01f)))
        return;   // 표면 위 · 두께 아래 (처마 밑으로 들어온 것은 지나간다)
    wp.y = max(wp.y, topY + 0.002f);
    if (wv.y < 0.0f)
    {
        wv.xz *= 1.0f - a.y;
        wv.y = -wv.y * a.x;
        p.Age += a.z * p.Life;
    }
    if (!(all(abs(wp) < 1e9f) && all(abs(wv) < 1e9f)))
        return;   // NaN · 무한이면 그대로 둔다 (한 번 망가진 자리는 영영 그려지지 않는다)
    if (gLocalSpace != 0)
    {
        p.Pos = mul(float4(wp, 1.0f), gWorldInv).xyz;
        p.Vel = mul(float4(wv, 0.0f), gWorldInv).xyz;
    }
    else
    {
        p.Pos = wp;
        p.Vel = wv;
    }
}

// a = (튕김, 마찰, 수명 줄이기, 두께 m). 다음 자리 (자리 + 속도 × dt) 가 보이는 표면 뒤 두께 안이면 표면 법선으로 튕긴다
void CollideDepth(inout Particle p, uint at)
{
    if (gCollParams.x < 0.5f)
        return;
    const float4 a = P(at, 0);
    float3 wp = gLocalSpace != 0 ? mul(float4(p.Pos, 1.0f), gWorld).xyz : p.Pos;
    float3 wv = gLocalSpace != 0 ? mul(float4(p.Vel, 0.0f), gWorld).xyz : p.Vel;
    const float3 next = wp + wv * gDt;
    const float4 clip = mul(float4(next, 1.0f), gCollViewProj);
    if (clip.w <= 1e-4f)
        return;
    const float2 ndc = clip.xy / clip.w;
    if (abs(ndc.x) >= 1.0f || abs(ndc.y) >= 1.0f)
        return;
    if (gCollViewport.z < 3.0f || gCollViewport.w < 3.0f)
        return;
    const int2 lo = int2(gCollViewport.xy) + 1, hi = int2(gCollViewport.xy + gCollViewport.zw) - 2;
    const int2 q = clamp(int2(gCollViewport.xy + float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f) * gCollViewport.zw), lo, hi);
    const float d = gCollDepth.Load(int3(q, 0)).r;
    if (d >= 0.999999f)
        return;   // 하늘
    const float sceneZ = gCollParams.z / min(d - gCollParams.y, -1e-7f);
    if (clip.w < sceneZ || clip.w > sceneZ + max(a.w, 0.01f))
        return;   // 표면 앞 · 두께 너머 (물체 뒤로 지나간다)
    // 표면 자리 · 법선: 이웃 화소 중 깊이가 가까운 쪽 (모서리를 넘지 않게)
    const float3 c = DepthWorld(q);
    if (!all(abs(c) < 1e9f))
        return;   // 되살린 자리가 NaN · 무한 (깊이 · 행렬이 어긋난 뷰)
    const float3 l = DepthWorld(q - int2(1, 0)), r = DepthWorld(q + int2(1, 0));
    const float3 u = DepthWorld(q - int2(0, 1)), b = DepthWorld(q + int2(0, 1));
    const float3 dx = dot(r - c, r - c) < dot(c - l, c - l) ? r - c : c - l;
    const float3 dy = dot(b - c, b - c) < dot(c - u, c - u) ? b - c : c - u;
    float3 n = cross(dy, dx);
    n = dot(n, n) > 1e-12f ? normalize(n) : normalize(gCollCam.xyz - c + 1e-6f);
    if (dot(n, gCollCam.xyz - c) < 0.0f)
        n = -n;
    const float dist = dot(wp - c, n);
    if (dist < 0.0f)
        wp -= n * (dist - 0.002f);
    const float vn = dot(wv, n);
    if (vn < 0.0f)
    {
        const float3 vt = wv - n * vn;
        wv = vt * (1.0f - a.y) - n * vn * a.x;
        p.Age += a.z * p.Life;
    }
    if (!(all(abs(wp) < 1e9f) && all(abs(wv) < 1e9f)))
        return;   // NaN · 무한이면 그대로 둔다 (한 번 망가진 자리는 영영 그려지지 않는다)
    if (gLocalSpace != 0)
    {
        p.Pos = mul(float4(wp, 1.0f), gWorldInv).xyz;
        p.Vel = mul(float4(wv, 0.0f), gWorldInv).xyz;
    }
    else
    {
        p.Pos = wp;
        p.Vel = wv;
    }
}

// Collide with SDF: 칸 값 (범위 밖은 가장자리)
float SdfCell(int3 c, int3 n)
{
    c = clamp(c, int3(0, 0, 0), n - 1);
    return asfloat(gSdf.Load(((c.z * n.y + c.y) * n.x + c.x) * 4));
}

// 메시 공간 자리 q 의 거리 (칸 가운데 기준 세 방향 보간)
float SdfSample(float3 q, float4 grid, int3 n)
{
    const float3 g = (q - grid.xyz) / grid.w - 0.5f;
    const int3 i = (int3)floor(g);
    const float3 f = g - i;
    const float c00 = lerp(SdfCell(i, n), SdfCell(i + int3(1, 0, 0), n), f.x);
    const float c10 = lerp(SdfCell(i + int3(0, 1, 0), n), SdfCell(i + int3(1, 1, 0), n), f.x);
    const float c01 = lerp(SdfCell(i + int3(0, 0, 1), n), SdfCell(i + int3(1, 0, 1), n), f.x);
    const float c11 = lerp(SdfCell(i + int3(0, 1, 1), n), SdfCell(i + int3(1, 1, 1), n), f.x);
    return lerp(lerp(c00, c10, f.y), lerp(c01, c11, f.y), f.z);
}

// 칸: 0..2 시뮬레이션 → 메시 (열), 3..5 메시 → 시뮬레이션 (열), 6 (모서리 xyz, 칸 한 변), 7 (칸 수 xyz, 거리 배율), 8 (튕김, 마찰, 수명 줄이기, 반지름)
void CollideSdf(inout Particle p, uint at)
{
    const float4 i0 = P(at, 0), i1 = P(at, 1), i2 = P(at, 2);
    const float4 f0 = P(at, 3), f1 = P(at, 4), f2 = P(at, 5);
    const float4 grid = P(at, 6), dims = P(at, 7), a = P(at, 8);
    const int3 n = int3(dims.xyz);
    if (n.x < 2 || n.y < 2 || n.z < 2)
        return;
    // 다음 자리 (자리 + 속도 × dt) 로 — 빠른 파티클이 얇은 면을 건너뛰지 않게
    const float3 next = p.Pos + p.Vel * gDt;
    const float4 h = float4(next, 1.0f);
    const float3 q = float3(dot(h, i0), dot(h, i1), dot(h, i2));
    const float3 lo = grid.xyz, hi = grid.xyz + grid.w * float3(n);
    if (any(q < lo) || any(q > hi))
        return;   // 거리장 상자 밖
    const float d = SdfSample(q, grid, n) * dims.w - a.w;   // 시뮬레이션 단위 (반지름만큼 띄운다)
    if (d >= 0.0f)
        return;
    // 법선 = 기울기 (메시 공간) → 시뮬레이션 공간
    const float e = grid.w * 0.5f;
    const float3 gl = float3(SdfSample(q + float3(e, 0, 0), grid, n) - SdfSample(q - float3(e, 0, 0), grid, n),
                             SdfSample(q + float3(0, e, 0), grid, n) - SdfSample(q - float3(0, e, 0), grid, n),
                             SdfSample(q + float3(0, 0, e), grid, n) - SdfSample(q - float3(0, 0, e), grid, n));
    float3 nrm = float3(dot(float4(gl, 0.0f), f0), dot(float4(gl, 0.0f), f1), dot(float4(gl, 0.0f), f2));
    if (dot(nrm, nrm) < 1e-12f)
        return;
    nrm = normalize(nrm);
    p.Pos = next - nrm * d - p.Vel * gDt;   // 다음 자리가 표면 위에 오도록 (적분이 속도만큼 옮긴다)
    const float vn = dot(p.Vel, nrm);
    if (vn < 0.0f)
    {
        const float3 vt = p.Vel - nrm * vn;
        p.Vel = vt * (1.0f - a.y) - nrm * vn * a.x;
        p.Age += a.z * p.Life;
    }
}

// GPU Event 로 태어날 때 부모 파티클의 속도 · 색 (Inherit Source 블록이 쓴다 — 없으면 이어받지 않는다)
static float3 sSrcVel = 0.0f;
static float4 sSrcColor = 1.0f;

void RunInitialize(inout Particle p, inout uint s)
{
    float3 shapeDir = float3(0, 1, 0);
    uint at = gInitStart;
    for (uint n = 0; n < gInitCount; ++n)
    {
        sP = p;
        PrepareSlots(at);
        const float4 h = gProgram[at];
        const int type = (int)h.x;
        if (type == 1)   // Set Position (Shape)
            p.Pos += ShapePosition(at, s, shapeDir);
        else if (type == 8)   // Set Position (Spiral Arms)
            p.Pos += SpiralPosition(at, s, shapeDir);
        else if (type == 2)   // Set Velocity
        {
            const float4 a = P(at, 0);   // x 모드, y 최소 속력, z 최대 속력, w 퍼짐 (도)
            const float3 d = P(at, 1).xyz;
            const float speed = lerp(a.y, a.z, Rand(s));
            float3 v = 0.0f;
            const int mode = (int)a.x;
            if (mode == 0) v = d * speed;
            else if (mode == 1) v = RandDir(s) * speed;
            else if (mode == 2) v = shapeDir * speed;
            else
            {
                // 원뿔: d 둘레로 a.w 도
                const float3 axis = normalize(d + 1e-6f);
                const float3 t1 = normalize(abs(axis.y) < 0.99f ? cross(axis, float3(0, 1, 0)) : cross(axis, float3(1, 0, 0)));
                const float3 t2 = cross(axis, t1);
                const float ca = cos(radians(a.w));
                const float z = lerp(ca, 1.0f, Rand(s));
                const float r = sqrt(saturate(1.0f - z * z));
                const float ph = Rand(s) * 6.2831853f;
                v = (axis * z + (t1 * cos(ph) + t2 * sin(ph)) * r) * speed;
            }
            p.Vel += v;
        }
        else if (type == 3)   // Set Lifetime Random
            p.Life = lerp(P(at, 0).x, P(at, 0).y, Rand(s));
        else if (type == 4)   // Set Size Random
            p.BaseSize = lerp(P(at, 0).x, P(at, 0).y, Rand(s));
        else if (type == 5)   // Set Color
            p.BaseColor = BlockColor(at, s);
        else if (type == 9)   // Set Attribute (사용자 속성)
            SetCustom(p, at);
        else if (type == 6)   // Set Angle · Angular Velocity (도)
        {
            const float4 a = P(at, 0);
            p.Rot = radians(lerp(a.x, a.y, Rand(s)));
            p.AngVel = radians(lerp(a.z, a.w, Rand(s)));
        }
        else if (type == 7)   // Inherit Source (GPU Event): 부모 파티클의 속도 (배율) · 색
        {
            const float4 a = P(at, 0);
            p.Vel += sSrcVel * a.x;
            if (a.y > 0.5f) p.BaseColor = sSrcColor;
        }
        at += 1 + (uint)h.y;
    }
    p.Color = p.BaseColor;
    p.Size = p.BaseSize;
}

void RunUpdate(inout Particle p, uint s)
{
    s = Hash(s ^ asuint(gTime));   // Update 의 무작위 (Set Color Random) 는 프레임마다
    const float t = p.Life > 0.0f ? saturate(p.Age / p.Life) : 1.0f;
    uint at = gUpdateStart;
    for (uint n = 0; n < gUpdateCount; ++n)
    {
        sP = p;
        PrepareSlots(at);
        const float4 h = gProgram[at];
        const int type = (int)h.x;
        if (type == 20)   // Gravity
            p.Vel += P(at, 0).xyz * gDt;
        else if (type == 21)   // Linear Drag
            p.Vel *= saturate(1.0f - P(at, 0).x * gDt);
        else if (type == 22)   // Turbulence: x 세기, y 주파수, z 옥타브, w 끌림 (0 = 힘 더하기, 1 = 그 속도로 끌림)
        {
            const float4 a = P(at, 0);
            const float3 scroll = P(at, 1).xyz;
            const float3 f = NoiseVec(p.Pos * a.y + scroll * gTime, a.z) * a.x;
            p.Vel = lerp(p.Vel + f * gDt, f, saturate(a.w * gDt));
        }
        else if (type == 23)   // Vortex: 축 둘레로 돌리고 안으로 당긴다
        {
            const float4 a = P(at, 0), b = P(at, 1);   // a: 축 · 세기, b: 가운데 · 당김
            const float3 axis = normalize(a.xyz + 1e-6f);
            float3 r = p.Pos - b.xyz;
            r -= axis * dot(r, axis);
            const float d = max(length(r), 1e-3f);
            p.Vel += (cross(axis, r / d) * a.w - r / d * b.w) * gDt;
        }
        else if (type == 24)   // Attractor: 가운데로 (반지름 안에서는 그 거리에 맞춰 — Conform to Sphere)
        {
            const float4 a = P(at, 0), b = P(at, 1);   // a: 가운데 · 세기, b: x 반지름, y 끌림
            const float3 to = a.xyz - p.Pos;
            const float d = length(to);
            const float3 dir = to / max(d, 1e-4f);
            const float pull = b.x > 0.0f ? (d - b.x) : d;
            p.Vel += dir * pull * a.w * gDt;
            p.Vel *= saturate(1.0f - b.y * gDt);
        }
        else if (type == 25)   // Collide with Plane: 법선 · 거리, 튕김 · 마찰 · 수명 줄이기
        {
            const float4 a = P(at, 0), b = P(at, 1);
            const float3 nrm = normalize(a.xyz + 1e-6f);
            const float dist = dot(p.Pos, nrm) - a.w;
            if (dist < 0.0f)
            {
                p.Pos -= nrm * dist;
                const float vn = dot(p.Vel, nrm);
                if (vn < 0.0f)
                {
                    const float3 vt = p.Vel - nrm * vn;
                    p.Vel = vt * (1.0f - b.y) - nrm * vn * b.x;
                    p.Age += b.z * p.Life;
                }
            }
        }
        else if (type == 26)   // Color over Life: x 모드 (0 곱하기, 1 바꾸기)
        {
            const float4 g = Gradient8(at, 1, t);
            p.Color = P(at, 0).x > 0.5f ? g : p.BaseColor * g;
        }
        else if (type == 27)   // Size over Life: 곡선 × 처음 크기
            p.Size = p.BaseSize * Curve16(at, 1, t);
        else if (type == 28)   // Speed Limit
        {
            const float sp = length(p.Vel);
            const float lim = P(at, 0).x;
            if (sp > lim) p.Vel *= lim / sp;
        }
        else if (type == 29)   // Orbit: 축 둘레로 각속도 (도/초), Falloff > 0 이면 먼 것이 느리다 (은하의 차등 회전)
        {
            const float4 a = P(at, 0);   // xyz 가운데, w 각속도
            const float4 b = P(at, 1);   // xyz 축, w 감쇠 (각속도 ∝ 1 / 거리^w)
            const float3 k = normalize(b.xyz + float3(0.0f, 1e-6f, 0.0f));
            const float3 r = p.Pos - a.xyz;
            const float dist = length(r - k * dot(r, k));
            const float ang = radians(a.w) * gDt * (b.w > 0.0f ? pow(max(dist, 0.25f), -b.w) : 1.0f);
            const float c = cos(ang), sn = sin(ang);
            p.Pos = a.xyz + r * c + cross(k, r) * sn + k * dot(k, r) * (1.0f - c);   // 로드리게스 회전
            p.Vel = p.Vel * c + cross(k, p.Vel) * sn + k * dot(k, p.Vel) * (1.0f - c);   // 속도도 함께 (바깥으로 가던 것은 계속 바깥으로 — 토네이도 깔때기)
        }
        else if (type == 5)   // Set Color (Update): 이 프레임의 색 — 뒤의 Color over Life (곱하기) 가 이 색에 곱한다
        {
            p.BaseColor = BlockColor(at, s);
            p.Color = p.BaseColor;
        }
        else if (type == 9)   // Set Attribute
            SetCustom(p, at);
        else if (type == 30)   // Collide with Depth Buffer
            CollideDepth(p, at);
        else if (type == 31)   // Collide with Signed Distance Field
            CollideSdf(p, at);
        else if (type == 32)   // Collide with Weather Cover
            CollideCover(p, at);
        at += 1 + (uint)h.y;
    }
}

// ---------------------------------------------------------------- compute
[numthreads(64, 1, 1)]
void ResetCS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gCapacity)
        return;
    gParticles.Store4(id.x * kStride + 16, asuint(float4(0, 0, 0, 0)));   // 수명 0 = 빈 칸
    gParticles.Store4(id.x * kStride, asuint(float4(0, 0, 0, 1)));
}

[numthreads(64, 1, 1)]
void SpawnCS(uint3 id : SV_DispatchThreadID)
{
    Particle p = (Particle)0;
    uint s = Hash(gSeed ^ (id.x * 0x9E3779B9u));
    if (gFromEvents != 0)
    {
        const uint ev = id.x / max(gSpawnCount, 1u);
        const uint count = min(gEventsIn.Load(gFromEvents == 2 ? 4 : 0), gEventCapacity);
        if (ev >= count)
            return;
        const uint a = 16 + (gFromEvents == 2 ? gEventCapacity * 48 : 0) + ev * 48;
        p.Pos = asfloat(gEventsIn.Load3(a));
        sSrcVel = asfloat(gEventsIn.Load3(a + 16));
        sSrcColor = asfloat(gEventsIn.Load4(a + 32));
    }
    else if (id.x >= gSpawnCount)
        return;
    p.BaseColor = float4(1, 1, 1, 1);
    p.Life = 1.0f;
    p.BaseSize = 0.1f;
    p.Seed = s;
    float3 eventPos = p.Pos;
    p.Pos = 0.0f;
    RunInitialize(p, s);
    if (gFromEvents != 0)
        p.Pos += eventPos;   // 이벤트 자리 (월드) 를 기준으로
    else if (gLocalSpace == 0)
    {
        // World: 지금 Visual Effect 자리에서 태어나 그 뒤로는 월드에 남는다 (Local 은 그릴 때 gWorld)
        p.Pos = mul(float4(p.Pos, 1.0f), gWorld).xyz;
        p.Vel = mul(float4(p.Vel, 0.0f), gWorld).xyz;
    }
    uint slot;
    gState.InterlockedAdd(0, 1, slot);
    Store(slot % gCapacity, p);
}

// 이벤트 하나 (위치 · 속도는 월드)
void EmitEvent(uint countOffset, uint baseOffset, Particle p)
{
    uint e;
    gEvents.InterlockedAdd(countOffset, 1, e);
    if (e >= gEventCapacity)
        return;
    const float3 wp = gLocalSpace != 0 ? mul(float4(p.Pos, 1.0f), gWorld).xyz : p.Pos;
    const float3 wv = gLocalSpace != 0 ? mul(float4(p.Vel, 0.0f), gWorld).xyz : p.Vel;
    const uint a = baseOffset + e * 48;
    gEvents.Store4(a, asuint(float4(wp, 0.0f)));
    gEvents.Store4(a + 16, asuint(float4(wv, 0.0f)));
    gEvents.Store4(a + 32, asuint(p.Color));
}

// float 를 크기 순서가 같은 uint 로 (원자적 최소 · 최대에 쓴다 — 음수도)
uint OrderKey(float f)
{
    const uint u = asuint(f);
    return (u & 0x80000000u) != 0 ? ~u : (u | 0x80000000u);
}

groupshared uint gsAlive;
groupshared uint gsBounds[7];   // 최소 xyz, 최대 xyz, 최대 크기 (순서 키)

[numthreads(64, 1, 1)]
void UpdateCS(uint3 id : SV_DispatchThreadID, uint local : SV_GroupIndex)
{
    if (local == 0)
    {
        gsAlive = 0;
        gsBounds[0] = 0xFFFFFFFFu; gsBounds[1] = 0xFFFFFFFFu; gsBounds[2] = 0xFFFFFFFFu;
        gsBounds[3] = 0u; gsBounds[4] = 0u; gsBounds[5] = 0u; gsBounds[6] = 0u;
    }
    GroupMemoryBarrierWithGroupSync();
    if (id.x < gCapacity)
    {
        Particle p = Load(id.x);
        if (p.Life > 0.0f)
        {
            const float ageBefore = p.Age;
            RunUpdate(p, p.Seed);
            p.Pos += p.Vel * gDt;
            p.Rot += p.AngVel * gDt;
            p.Age += gDt;
            // Trigger Event Rate: 살아 있는 동안 초당 gEmitRate 번 (한 프레임에 많아야 4)
            if (gEmitRate > 0.0f)
            {
                const uint n = (uint)clamp(floor(min(p.Age, p.Life) * gEmitRate) - floor(ageBefore * gEmitRate), 0.0f, 4.0f);
                for (uint k = 0; k < n; ++k)
                    EmitEvent(4, 16 + gEventCapacity * 48, p);
            }
            if (p.Age >= p.Life)
            {
                if (gEmitOnDie != 0)
                    EmitEvent(0, 16, p);
                p.Life = 0.0f;
            }
            else if (gTrailPoints > 0)
            {
                // 꼬리: 처음, 또는 gTrailInterval 이 지났으면 지금 자리를 다음 칸에 (Spare0 기록한 나이, Spare1 마지막 칸, Spare2 기록 수)
                const uint n = gTrailPoints;
                const float count = p.Spare2;
                if (count < 0.5f || p.Age - p.Spare0 >= gTrailInterval)
                {
                    const uint head = count < 0.5f ? 0u : ((uint)p.Spare1 + 1u) % n;
                    gTrail.Store4((id.x * n + head) * 16, asuint(float4(p.Pos, p.Age)));
                    p.Spare0 = p.Age;
                    p.Spare1 = (float)head;
                    p.Spare2 = min(count + 1.0f, (float)n);
                }
            }
            Store(id.x, p);
            if (p.Life > 0.0f)
            {
                InterlockedAdd(gsAlive, 1u);
                InterlockedMin(gsBounds[0], OrderKey(p.Pos.x));
                InterlockedMin(gsBounds[1], OrderKey(p.Pos.y));
                InterlockedMin(gsBounds[2], OrderKey(p.Pos.z));
                InterlockedMax(gsBounds[3], OrderKey(p.Pos.x));
                InterlockedMax(gsBounds[4], OrderKey(p.Pos.y));
                InterlockedMax(gsBounds[5], OrderKey(p.Pos.z));
                InterlockedMax(gsBounds[6], OrderKey(p.Size));
            }
        }
    }
    // 살아 있는 수 · 경계: 그룹 안에서 먼저 모으고 그룹마다 한 번만 전역 원자 연산 (백만 개에서도 한 주소에 몰리지 않게)
    GroupMemoryBarrierWithGroupSync();
    if (local == 0 && gsAlive > 0)
    {
        gState.InterlockedAdd(4, gsAlive);
        gState.InterlockedMin(8, gsBounds[0]);
        gState.InterlockedMin(12, gsBounds[1]);
        gState.InterlockedMin(16, gsBounds[2]);
        gState.InterlockedMax(20, gsBounds[3]);
        gState.InterlockedMax(24, gsBounds[4]);
        gState.InterlockedMax(28, gsBounds[5]);
        gState.InterlockedMax(32, gsBounds[6]);
    }
}

// ---------------------------------------------------------------- 꼬리 띠 조각 (파티클마다 gTrailPoints 조각: 0 = 지금 자리 → 마지막 기록, k = 기록 k-1 → k)
float4 TrailPoint(uint particle, uint slot) { return asfloat(gTrail.Load4((particle * gTrailPoints + slot) * 16)); }

[numthreads(64, 1, 1)]
void TrailCS(uint3 id : SV_DispatchThreadID)
{
    const uint n = gTrailPoints;
    if (n == 0 || id.x >= gCapacity * n)
        return;
    const uint pi = id.x / n, seg = id.x % n;
    const Particle p = Load(pi);
    float4 A = 0.0f, B = 0.0f, C = 0.0f, D = 0.0f;
    const uint count = (uint)p.Spare2;
    if (p.Life > 0.0f && p.Age < p.Life && seg + 1 <= count)
    {
        const uint head = (uint)p.Spare1;
        const float4 q0 = seg == 0 ? float4(p.Pos, p.Age) : TrailPoint(pi, (head + n - (seg - 1)) % n);
        const float4 q1 = TrailPoint(pi, (head + n - seg) % n);
        // u = 꼬리 길이 (기록 수 × 간격) 에서 지난 시간의 비율 → 끝으로 갈수록 가늘고 흐려진다
        const float len = max(n * gTrailInterval, 1e-4f);
        const float u0 = saturate((p.Age - q0.w) / len), u1 = saturate((p.Age - q1.w) / len);
        const float w = p.Size * gTrailWidth;
        A = float4(q0.xyz, w * (1.0f - u0));
        B = float4(q1.xyz, w * (1.0f - u1));
        C = p.Color;
        D = float4(u0, u1, 1.0f, 0.0f);
    }
    const uint a = id.x * 64;
    gTrailVerts.Store4(a, asuint(A));
    gTrailVerts.Store4(a + 16, asuint(B));
    gTrailVerts.Store4(a + 32, asuint(C));
    gTrailVerts.Store4(a + 48, asuint(D));
}

// ---------------------------------------------------------------- 정렬 (Alpha 시스템을 먼 것부터): 키 = -카메라 앞 거리 → 오름차순
[numthreads(64, 1, 1)]
void SortKeysCS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gSortCount)
        return;
    float key = 3.0e38f;   // 빈 칸 · 남는 칸 = 맨 뒤
    if (id.x < gCapacity)
    {
        const float4 v0 = asfloat(gParticles.Load4(id.x * kStride));
        const float4 v1 = asfloat(gParticles.Load4(id.x * kStride + 16));
        if (v1.w > 0.0f && v0.w < v1.w)
        {
            const float3 pos = gLocalSpace != 0 ? mul(float4(v0.xyz, 1.0f), gWorld).xyz : v0.xyz;
            key = -dot(pos - gCamPos, gCamFwd);
        }
    }
    gSortKeys.Store2(id.x * 8, uint2(asuint(key), id.x));
}

groupshared float gsKey[512];
groupshared uint gsIdx[512];

void SortStep(uint base, uint t, uint k, uint j)
{
    GroupMemoryBarrierWithGroupSync();
    const uint i = 2 * t - (t & (j - 1));
    const uint ixj = i + j;
    const bool ascending = ((base + i) & k) == 0;
    const float ka = gsKey[i], kb = gsKey[ixj];
    if (ascending ? ka > kb : ka < kb)
    {
        gsKey[i] = kb;
        gsKey[ixj] = ka;
        const uint ia = gsIdx[i];
        gsIdx[i] = gsIdx[ixj];
        gsIdx[ixj] = ia;
    }
}

// 512 개 안에서: gSortJ = 0 이면 처음부터 정렬 (k = 2..512), 아니면 큰 k (gSortK) 의 j = 256..1 단계 (전역 단계 뒤 마무리)
[numthreads(256, 1, 1)]
void SortLocalCS(uint3 gid : SV_GroupID, uint t : SV_GroupIndex)
{
    const uint base = gid.x * 512;
    uint2 e = gSortKeys.Load2((base + t) * 8);
    gsKey[t] = asfloat(e.x);
    gsIdx[t] = e.y;
    e = gSortKeys.Load2((base + t + 256) * 8);
    gsKey[t + 256] = asfloat(e.x);
    gsIdx[t + 256] = e.y;
    if (gSortJ == 0)
    {
        for (uint k = 2; k <= 512; k <<= 1)
            for (uint j = k >> 1; j > 0; j >>= 1)
                SortStep(base, t, k, j);
    }
    else
    {
        for (uint j = 256; j > 0; j >>= 1)
            SortStep(base, t, gSortK, j);
    }
    GroupMemoryBarrierWithGroupSync();
    gSortKeys.Store2((base + t) * 8, uint2(asuint(gsKey[t]), gsIdx[t]));
    gSortKeys.Store2((base + t + 256) * 8, uint2(asuint(gsKey[t + 256]), gsIdx[t + 256]));
}

// 512 보다 먼 짝 (j >= 512): 짝마다 스레드 하나
[numthreads(64, 1, 1)]
void SortGlobalCS(uint3 id : SV_DispatchThreadID)
{
    const uint t = id.x;
    if (t >= gSortCount / 2)
        return;
    const uint i = 2 * t - (t & (gSortJ - 1));
    const uint ixj = i + gSortJ;
    const uint2 a = gSortKeys.Load2(i * 8), b = gSortKeys.Load2(ixj * 8);
    const bool ascending = (i & gSortK) == 0;
    if (ascending ? asfloat(a.x) > asfloat(b.x) : asfloat(a.x) < asfloat(b.x))
    {
        gSortKeys.Store2(i * 8, b);
        gSortKeys.Store2(ixj * 8, a);
    }
}

// 정렬된 순서로 파티클을 복사 (그리기는 이 사본을 정점 버퍼로)
[numthreads(64, 1, 1)]
void SortGatherCS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gCapacity)
        return;
    const uint src = gSortKeys.Load2(id.x * 8).y;
    const uint dst = id.x * kStride;
    if (src >= gCapacity)
    {
        gSorted.Store4(dst + 16, uint4(0, 0, 0, 0));   // 남는 칸: 빈 파티클
        return;
    }
    const uint a = src * kStride;
    [unroll] for (uint k = 0; k < kStride; k += 16)
        gSorted.Store4(dst + k, gParticles.Load4(a + k));
}

// ---------------------------------------------------------------- 그리기 (파티클 버퍼 = 인스턴스 정점 버퍼)
struct ParticleIn
{
    float4 PosAge : POSITION;     // xyz 위치, w 나이
    float4 VelLife : VELOCITY;    // xyz 속도, w 수명
    float4 Color : COLOR;
    float4 Params : TEXCOORD0;    // x 크기, y 회전, z 각속도, w 씨앗 (비트)
    uint VertexId : SV_VertexID;
};

struct VfxOut
{
    float4 PosH : SV_POSITION;
    float2 Tex : TEXCOORD0;       // -1..1
    float4 Color : COLOR;
    float4 Extra : TEXCOORD1;     // x 씨앗 (0..1), y 늘림 비율, z 나이 비율, w 플립북 칸
};

VfxOut VS(ParticleIn vin)
{
    VfxOut o;
    o.Tex = 0.0f;
    o.Color = 0.0f;
    o.Extra = 0.0f;
    const float life = vin.VelLife.w;
    if (life <= 0.0f || vin.PosAge.w >= life)
    {
        o.PosH = float4(2.0f, 2.0f, 2.0f, 1.0f);   // 빈 칸: 화면 밖 (잘린다)
        return o;
    }
    float3 pos = vin.PosAge.xyz;
    float3 vel = vin.VelLife.xyz;
    if (gLocalSpace != 0)
    {
        pos = mul(float4(pos, 1.0f), gWorld).xyz;
        vel = mul(float4(vel, 0.0f), gWorld).xyz;
    }
    const float2 corner = float2((vin.VertexId & 1) ? 1.0f : -1.0f, (vin.VertexId & 2) ? 1.0f : -1.0f);
    float3 right = gCamRight, up = gCamUp;
    float2 halfSize = vin.Params.x * 0.5f;
    float stretch = 1.0f;
    const int orient = (int)gOutput0.y;
    if (orient == 1)
    {
        const float speed = length(vel);
        const float3 dir = speed > 1e-5f ? vel / speed : gCamUp;
        const float3 toCam = normalize(gCamPos - pos);
        const float3 side = cross(dir, toCam);
        right = length(side) > 1e-5f ? normalize(side) : gCamRight;
        up = dir;
        stretch = 1.0f + speed * gOutput0.z;
        halfSize.y *= stretch;
    }
    else if (orient == 2)
    {
        right = float3(1, 0, 0);
        up = float3(0, 0, 1);
    }
    // 화면에서 1.4 픽셀보다 가는 것은 1.4 픽셀로 넓히고 그만큼 옅게 — 먼 빗줄기 · 눈송이가 점선 · 깜빡임으로 끊겨 보이지 않게
    //  (gDepthParams.z = 깊이 1 에서 1 m 의 픽셀 수, 0 = 모름)
    float thin = 1.0f;
    if (gDepthParams.z > 0.0f && orient != 2)
    {
        // Spark 는 보이는 심이 사각형 너비의 1/4 쯤 (ShapeAlpha 의 exp(-x² · 30)) → 사각형을 그만큼 더 넓게
        const float core = (int)gOutput0.x == 5 ? 4.0f : 1.0f;
        const float minHalf = 0.7f * core * max(mul(float4(pos, 1.0f), gViewProj).w, 1e-3f) / gDepthParams.z;
        if (halfSize.x < minHalf)
        {
            thin = halfSize.x / minHalf;
            if (orient == 0)
            {
                thin *= min(halfSize.y / minHalf, 1.0f);
                halfSize.y = max(halfSize.y, minHalf);
            }
            halfSize.x = minHalf;
        }
    }
    float2 c = corner * halfSize;
    if (orient != 1)
    {
        float s, co;
        sincos(vin.Params.y, s, co);
        c = float2(c.x * co - c.y * s, c.x * s + c.y * co);
    }
    const float3 world = pos + right * c.x + up * c.y;
    o.PosH = mul(float4(world, 1.0f), gViewProj);
    o.Tex = corner;
    o.Color = vin.Color;
    o.Color.a *= thin;
    const float t = saturate(vin.PosAge.w / life);
    // 플립북 칸: 수명 동안 한 번 (w = 0) 또는 초당 w 칸
    const float frames = max(gOutput1.y * gOutput1.z, 1.0f);
    const float frame = gOutput1.w > 0.0f ? floor(vin.PosAge.w * gOutput1.w) % frames : floor(t * frames * 0.999f);
    o.Extra = float4((asuint(vin.Params.w) & 0xFFFFu) / 65535.0f, stretch, t, frame);
    return o;
}

// 그림 없이 만드는 모양 (uv -1..1, 가운데 0)
float ShapeAlpha(int shape, float2 uv, float seed, float stretch)
{
    const float r = length(uv);
    const float soft = saturate(1.0f - r);
    float a = soft;
    if (shape == 0)   // Soft Dot
        a = soft * soft;
    else if (shape == 1)   // Glow: 밝은 가운데 + 넓은 번짐
        a = saturate(exp(-r * r * 9.0f) + 0.35f * soft * soft);
    else if (shape == 2)   // Star (4 갈래 빛줄기)
    {
        const float rays = exp(-abs(uv.x) * 18.0f) * saturate(1.0f - abs(uv.y)) + exp(-abs(uv.y) * 18.0f) * saturate(1.0f - abs(uv.x));
        a = saturate(rays + exp(-r * r * 20.0f));
    }
    else if (shape == 3)   // Sparkle (6 갈래)
    {
        const float ang = atan2(uv.y, uv.x);
        a = saturate(pow(abs(cos(ang * 3.0f)), 24.0f) * soft + exp(-r * r * 30.0f));
    }
    else if (shape == 4)   // Ring
        a = exp(-pow((r - 0.7f) * 9.0f, 2.0f)) * saturate((1.0f - r) * 6.0f);
    else if (shape == 5)   // Spark: 가늘고 긴 줄기 (속도로 늘린 방향과 함께)
        a = exp(-uv.x * uv.x * 30.0f) * saturate(1.0f - abs(uv.y));
    else if (shape == 6)   // Smoke: 잡음으로 흩어진 덩어리
    {
        const float n = Noise3(float3(uv * 2.2f, seed * 37.0f)) * 0.5f + Noise3(float3(uv * 4.7f, seed * 91.0f)) * 0.25f;
        a = saturate((1.0f - r * r) * (0.55f + n)) * soft;
    }
    else if (shape == 7)   // Square
        a = 1.0f;
    else if (shape == 9)   // Heart
    {
        float2 q = uv * 1.25f;
        q.y = -q.y + 0.25f;
        const float k = q.x * q.x + q.y * q.y - 0.35f;
        a = saturate(-(k * k * k - q.x * q.x * q.y * q.y * q.y) * 40.0f);
    }
    return a;
}

float4 PS(VfxOut pin) : SV_Target
{
    const int shape = (int)gOutput0.x;
    float4 c = pin.Color;
    if (shape == 8)
    {
        // 텍스처 + 플립북 (열 × 행)
        const float cols = max(gOutput1.y, 1.0f), rows = max(gOutput1.z, 1.0f);
        const float frame = pin.Extra.w;
        const float2 cell = float2(fmod(frame, cols), floor(frame / cols));
        const float2 uv = (pin.Tex * float2(0.5f, -0.5f) + 0.5f + cell) / float2(cols, rows);
        c *= gTexture.Sample(samVfx, uv);
    }
    else
        c.a *= ShapeAlpha(shape, pin.Tex, pin.Extra.x, pin.Extra.y);
    c.rgb *= gOutput1.x;
    // Soft Particles
    if (gOutput0.w > 0.0f && gDepthParams.w > 0.5f)
    {
        const float d = gSceneDepth.Load(int3(pin.PosH.xy, 0)).r;
        const float sceneZ = gDepthParams.y / min(d - gDepthParams.x, -1e-7f);
        c.a *= saturate((sceneZ - pin.PosH.w) / gOutput0.w);
    }
    return c;
}

// 더하기: 알파를 밝기에 곱해 더한다 (HDR — 1 을 넘으면 Bloom)
float4 PS_Additive(VfxOut pin) : SV_Target
{
    const float4 c = PS(pin);
    return float4(c.rgb * c.a, 0.0f);
}

// 불투명 (Opaque): 모양의 알파 0.5 로 잘라 깊이를 쓴다
float4 PS_Cutout(VfxOut pin) : SV_Target
{
    const float4 c = PS(pin);
    clip(c.a - 0.5f);
    return float4(c.rgb, 1.0f);
}

// ---------------------------------------------------------------- Output Mesh (메시 정점 = 0 번 버퍼, 파티클 = 1 번 버퍼 인스턴스)
struct MeshIn
{
    float3 MPos : POSITION1;      // 메시 자리 (크기 1 기준)
    float3 MNormal : NORMAL;
    float4 PosAge : POSITION0;
    float4 VelLife : VELOCITY;
    float4 Color : COLOR;
    float4 Params : TEXCOORD0;    // x 크기, y 회전, z 각속도, w 씨앗
};

struct MeshOut
{
    float4 PosH : SV_POSITION;
    float4 Color : COLOR;
    float3 Normal : NORMAL;
};

// 축 k (단위) 둘레로 a 라디안 (로드리게스)
float3 RotateAxis(float3 v, float3 k, float a)
{
    float s, c;
    sincos(a, s, c);
    return v * c + cross(k, v) * s + k * dot(k, v) * (1.0f - c);
}

MeshOut MeshVS(MeshIn vin)
{
    MeshOut o;
    o.Color = 0.0f;
    o.Normal = float3(0, 1, 0);
    const float life = vin.VelLife.w;
    if (life <= 0.0f || vin.PosAge.w >= life)
    {
        o.PosH = float4(2.0f, 2.0f, 2.0f, 1.0f);
        return o;
    }
    float3 pos = vin.PosAge.xyz;
    float3 vel = vin.VelLife.xyz;
    if (gLocalSpace != 0)
    {
        pos = mul(float4(pos, 1.0f), gWorld).xyz;
        vel = mul(float4(vel, 0.0f), gWorld).xyz;
    }
    float3 v = vin.MPos * vin.Params.x;
    float3 n = vin.MNormal;
    const int orient = (int)gOutput0.y;
    if (orient == 1)
    {
        // 메시의 +Y 를 속도 방향으로, 그 둘레로 회전 각
        v = RotateAxis(v, float3(0, 1, 0), vin.Params.y);
        n = RotateAxis(n, float3(0, 1, 0), vin.Params.y);
        const float speed = length(vel);
        const float3 dir = speed > 1e-5f ? vel / speed : float3(0, 1, 0);
        const float3 axis = cross(float3(0, 1, 0), dir);
        const float sa = length(axis);
        if (sa > 1e-5f)
        {
            const float ang = atan2(sa, dir.y);
            v = RotateAxis(v, axis / sa, ang);
            n = RotateAxis(n, axis / sa, ang);
        }
        else if (dir.y < 0.0f)
        {
            v = float3(v.x, -v.y, -v.z);
            n = float3(n.x, -n.y, -n.z);
        }
    }
    else if (orient == 2)
    {
        v = RotateAxis(v, float3(0, 1, 0), vin.Params.y);
        n = RotateAxis(n, float3(0, 1, 0), vin.Params.y);
    }
    else
    {
        // 파티클마다 무작위 축 둘레로 회전 각 (구르는 파편)
        uint sd = asuint(vin.Params.w);
        const float3 axis = RandDir(sd);
        v = RotateAxis(v, axis, vin.Params.y);
        n = RotateAxis(n, axis, vin.Params.y);
    }
    o.PosH = mul(float4(pos + v, 1.0f), gViewProj);
    o.Color = vin.Color;
    o.Normal = n;
    return o;
}

float4 MeshColor(MeshOut pin)
{
    float4 c = pin.Color;
    if (gSunDir.w > 0.5f)
    {
        const float3 n = normalize(pin.Normal);
        const float ndl = saturate(dot(n, -gSunDir.xyz));
        const float sky = 0.6f + 0.4f * n.y;   // 위를 보는 면이 하늘빛을 더 받는다
        c.rgb *= gAmbient.rgb * sky + gSunColor.rgb * ndl;
    }
    c.rgb *= gOutput1.x;
    return c;
}

float4 MeshPS(MeshOut pin) : SV_Target { return MeshColor(pin); }
float4 MeshPS_Additive(MeshOut pin) : SV_Target { const float4 c = MeshColor(pin); return float4(c.rgb * c.a, 0.0f); }
float4 MeshPS_Opaque(MeshOut pin) : SV_Target { return float4(MeshColor(pin).rgb, 1.0f); }

// ---------------------------------------------------------------- 꼬리 그리기 (띠 조각 = 인스턴스 하나, 정점 4 개)
struct TrailIn
{
    float4 A : POSITION0;         // xyz 앞 자리, w 너비
    float4 B : POSITION1;         // xyz 뒤 자리, w 너비
    float4 Color : COLOR;
    float4 D : TEXCOORD0;         // x u0, y u1, z 있음
    uint VertexId : SV_VertexID;
};

VfxOut TrailVS(TrailIn vin)
{
    VfxOut o;
    o.Tex = 0.0f;
    o.Color = 0.0f;
    o.Extra = 0.0f;
    if (vin.D.z < 0.5f)
    {
        o.PosH = float4(2.0f, 2.0f, 2.0f, 1.0f);
        return o;
    }
    float3 a = vin.A.xyz, b = vin.B.xyz;
    if (gLocalSpace != 0)
    {
        a = mul(float4(a, 1.0f), gWorld).xyz;
        b = mul(float4(b, 1.0f), gWorld).xyz;
    }
    const float along = (vin.VertexId & 2) ? 1.0f : 0.0f;
    const float side = (vin.VertexId & 1) ? 1.0f : -1.0f;
    const float3 p = lerp(a, b, along);
    const float w = lerp(vin.A.w, vin.B.w, along);
    float3 dir = b - a;
    dir = dot(dir, dir) > 1e-12f ? normalize(dir) : gCamUp;
    float3 across = cross(dir, gCamPos - p);
    across = dot(across, across) > 1e-12f ? normalize(across) : gCamRight;
    o.PosH = mul(float4(p + across * side * w * 0.5f, 1.0f), gViewProj);
    o.Tex = float2(side, lerp(vin.D.x, vin.D.y, along));
    o.Color = vin.Color;
    return o;
}

float4 TrailPS(VfxOut pin) : SV_Target
{
    float4 c = pin.Color;
    const float x = saturate(1.0f - abs(pin.Tex.x));
    c.a *= x * x * (3.0f - 2.0f * x) * (1.0f - pin.Tex.y);   // 가운데가 밝은 띠, 끝으로 흐려진다
    c.rgb *= gOutput1.x;
    return c;
}

float4 TrailPS_Additive(VfxOut pin) : SV_Target
{
    const float4 c = TrailPS(pin);
    return float4(c.rgb * c.a, 0.0f);
}

BlendState VfxAlpha
{
    BlendEnable[0] = TRUE;
    SrcBlend = SRC_ALPHA;
    DestBlend = INV_SRC_ALPHA;
    BlendOp = ADD;
    SrcBlendAlpha = ZERO;
    DestBlendAlpha = ONE;
    BlendOpAlpha = ADD;
};

BlendState VfxAdditive
{
    BlendEnable[0] = TRUE;
    SrcBlend = ONE;
    DestBlend = ONE;
    BlendOp = ADD;
    SrcBlendAlpha = ZERO;
    DestBlendAlpha = ONE;
    BlendOpAlpha = ADD;
};

BlendState VfxOpaque
{
    BlendEnable[0] = FALSE;
};

DepthStencilState VfxDepthTestNoWrite
{
    DepthEnable = TRUE;
    DepthWriteMask = ZERO;
    DepthFunc = LESS_EQUAL;
};

RasterizerState VfxNoCull
{
    CullMode = NONE;
};

RasterizerState VfxCullBack
{
    CullMode = BACK;
};

DepthStencilState VfxDepthWrite
{
    DepthEnable = TRUE;
    DepthWriteMask = ALL;
    DepthFunc = LESS_EQUAL;
};

technique11 ResetTech { pass P0 { SetComputeShader(CompileShader(cs_5_0, ResetCS())); } }
technique11 SpawnTech { pass P0 { SetComputeShader(CompileShader(cs_5_0, SpawnCS())); } }
technique11 UpdateTech { pass P0 { SetComputeShader(CompileShader(cs_5_0, UpdateCS())); } }
technique11 TrailTech { pass P0 { SetComputeShader(CompileShader(cs_5_0, TrailCS())); } }
technique11 SortKeysTech { pass P0 { SetComputeShader(CompileShader(cs_5_0, SortKeysCS())); } }
technique11 SortLocalTech { pass P0 { SetComputeShader(CompileShader(cs_5_0, SortLocalCS())); } }
technique11 SortGlobalTech { pass P0 { SetComputeShader(CompileShader(cs_5_0, SortGlobalCS())); } }
technique11 SortGatherTech { pass P0 { SetComputeShader(CompileShader(cs_5_0, SortGatherCS())); } }

technique11 TrailAlphaTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, TrailVS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, TrailPS()));
        SetBlendState(VfxAlpha, float4(0, 0, 0, 0), 0xFFFFFFFF);
        SetDepthStencilState(VfxDepthTestNoWrite, 0);
        SetRasterizerState(VfxNoCull);
    }
}

technique11 TrailAdditiveTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, TrailVS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, TrailPS_Additive()));
        SetBlendState(VfxAdditive, float4(0, 0, 0, 0), 0xFFFFFFFF);
        SetDepthStencilState(VfxDepthTestNoWrite, 0);
        SetRasterizerState(VfxNoCull);
    }
}


technique11 AlphaTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS()));
        SetBlendState(VfxAlpha, float4(0, 0, 0, 0), 0xFFFFFFFF);
        SetDepthStencilState(VfxDepthTestNoWrite, 0);
        SetRasterizerState(VfxNoCull);
    }
}

technique11 AdditiveTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS_Additive()));
        SetBlendState(VfxAdditive, float4(0, 0, 0, 0), 0xFFFFFFFF);
        SetDepthStencilState(VfxDepthTestNoWrite, 0);
        SetRasterizerState(VfxNoCull);
    }
}

technique11 OpaqueTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS_Cutout()));
        SetBlendState(VfxOpaque, float4(0, 0, 0, 0), 0xFFFFFFFF);
        SetDepthStencilState(VfxDepthWrite, 0);
        SetRasterizerState(VfxNoCull);
    }
}

technique11 MeshAlphaTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, MeshVS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, MeshPS()));
        SetBlendState(VfxAlpha, float4(0, 0, 0, 0), 0xFFFFFFFF);
        SetDepthStencilState(VfxDepthTestNoWrite, 0);
        SetRasterizerState(VfxCullBack);
    }
}

technique11 MeshAdditiveTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, MeshVS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, MeshPS_Additive()));
        SetBlendState(VfxAdditive, float4(0, 0, 0, 0), 0xFFFFFFFF);
        SetDepthStencilState(VfxDepthTestNoWrite, 0);
        SetRasterizerState(VfxCullBack);
    }
}

technique11 MeshOpaqueTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, MeshVS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, MeshPS_Opaque()));
        SetBlendState(VfxOpaque, float4(0, 0, 0, 0), 0xFFFFFFFF);
        SetDepthStencilState(VfxDepthWrite, 0);
        SetRasterizerState(VfxCullBack);
    }
}
