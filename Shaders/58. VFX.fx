//=============================================================================
// 58. VFX.fx
// Unity VFX Graph 의 GPU 파티클 (Visual Effect 컴포넌트 · .vfx 에셋 — Source/Effects/VisualEffect.*).
//  - 시스템마다 파티클 버퍼 (96 바이트씩, GPU 만 쓴다) 를 compute 가 만들고 (Spawn) 움직인다 (Update).
//    블록 (Initialize · Update) 은 그래프가 만든 목록 (gProgram) 을 셰이더가 차례로 읽어 실행 — 그래프를 바꿔도 다시 컴파일하지 않는다
//  - 칸 고르기: 상태 버퍼의 카운터를 원자적으로 올려 (용량으로 나눈 나머지) — 가장 오래된 칸부터 다시 쓴다
//  - GPU Event (Trigger Event On Die): 죽은 파티클의 위치 · 속도 · 색을 이벤트 버퍼에, 하위 시스템이 그 자리에서 태어난다
//  - 그리기: 파티클 버퍼를 그대로 인스턴스 정점 버퍼로 (정점 셰이더가 GPU 버퍼를 읽지 않는다 — 휴대폰 GLES 에서도 같은 길)
//    모양은 그림 없이 셰이더가 만든다 (빛 · 별 · 고리 · 불꽃 줄기 · 연기) 또는 텍스처 + 플립북
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
    uint gSpawnCount;               // Spawn: 이번에 태어날 수 (이벤트면 이벤트당 수)
    uint gFromEvents;               // Spawn: 1 = 상위 시스템의 이벤트 자리에서
    uint gEventCapacity;            // 이벤트 버퍼 칸 수
    uint gEmitOnDie;                // Update: 1 = 죽을 때 이벤트를 쓴다
    uint gInitStart, gInitCount;    // gProgram 의 Initialize 블록 (시작 칸 · 블록 수)
    uint gUpdateStart, gUpdateCount;
    uint gSeed;                     // 이번 프레임 Spawn 의 씨앗
    uint gLocalSpace;               // 1 = Local (그릴 때 gWorld)
    float2 gPad0;
    float4 gOutput0;                // x 모양, y 방향 (0 카메라, 1 속도로 늘림, 2 수평), z 늘림 배율, w Soft 거리
    float4 gOutput1;                // x 세기 (HDR), y 플립북 열, z 행, w 플립북 (0 = 수명 동안 한 번, >0 = 초당 칸)
    float4 gDepthParams;            // x, y = 투영 _33, _43, z = 미사용, w = 장면 깊이 있음
};

StructuredBuffer<float4> gProgram;      // 블록 목록 (VfxRuntime 이 만든다)
RWByteAddressBuffer gParticles;         // 96 바이트씩
RWByteAddressBuffer gState;             // 0 = 칸 카운터, 4 = 이번 Update 의 살아 있는 수 (CPU 가 0 으로 두고 몇 프레임 뒤에 읽는다)
RWByteAddressBuffer gEventsOut;         // 이벤트 48 바이트씩 (위치 · 속도 · 색)
RWByteAddressBuffer gEventCountOut;     // 0 = 이벤트 수
ByteAddressBuffer gEventsIn;            // 상위 시스템의 이벤트
ByteAddressBuffer gEventCountIn;

Texture2D gTexture;
Texture2D gSceneDepth;

SamplerState samVfx
{
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = CLAMP;
    AddressV = CLAMP;
};

static const uint kStride = 96;

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
    const int n = clamp((int)octaves, 1, 4);
    for (int o = 0; o < n; ++o)
    {
        const float3 q = p * freq;
        sum += amp * float3(Noise3(q), Noise3(q + float3(31.4f, 7.7f, 2.1f)), Noise3(q + float3(-13.1f, 19.3f, 47.9f)));
        norm += amp;
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
}

// ---------------------------------------------------------------- 블록 (gProgram: 블록마다 머리 칸 (종류, 값 칸 수, 0, 0) + 값 칸들)
//  번호는 Source/Effects/VfxAsset.h 의 VfxBlockType 과 같다
float4 P(uint at, uint k) { return gProgram[at + 1 + k]; }

// 칸 i (0..3) 를 동적 색인 없이 (동적 색인은 블록 반복문을 펼치게 만든다)
float Lane(float4 v, uint i) { return dot(v, float4(i == 0, i == 1, i == 2, i == 3)); }

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

// GPU Event 로 태어날 때 부모 파티클의 속도 · 색 (Inherit Source 블록이 쓴다 — 없으면 이어받지 않는다)
static float3 sSrcVel = 0.0f;
static float4 sSrcColor = 1.0f;

void RunInitialize(inout Particle p, inout uint s)
{
    float3 shapeDir = float3(0, 1, 0);
    uint at = gInitStart;
    for (uint n = 0; n < gInitCount; ++n)
    {
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
        {
            const float4 a = P(at, 0), b = P(at, 1), m = P(at, 2);   // m: x 모드 (0 고정, 1 a..b 사이, 2 무지개), y 채도, z 밝기, w 세기
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
            p.BaseColor = col;
        }
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
    const float t = p.Life > 0.0f ? saturate(p.Age / p.Life) : 1.0f;
    uint at = gUpdateStart;
    for (uint n = 0; n < gUpdateCount; ++n)
    {
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
        const uint count = min(gEventCountIn.Load(0), gEventCapacity);
        if (ev >= count)
            return;
        const uint a = ev * 48;
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

groupshared uint gsAlive;

[numthreads(64, 1, 1)]
void UpdateCS(uint3 id : SV_DispatchThreadID, uint local : SV_GroupIndex)
{
    if (local == 0)
        gsAlive = 0;
    GroupMemoryBarrierWithGroupSync();
    if (id.x < gCapacity)
    {
        Particle p = Load(id.x);
        if (p.Life > 0.0f)
        {
            RunUpdate(p, p.Seed);
            p.Pos += p.Vel * gDt;
            p.Rot += p.AngVel * gDt;
            p.Age += gDt;
            if (p.Age >= p.Life)
            {
                if (gEmitOnDie != 0)
                {
                    uint e;
                    gEventCountOut.InterlockedAdd(0, 1, e);
                    if (e < gEventCapacity)
                    {
                        const float3 wp = gLocalSpace != 0 ? mul(float4(p.Pos, 1.0f), gWorld).xyz : p.Pos;
                        const float3 wv = gLocalSpace != 0 ? mul(float4(p.Vel, 0.0f), gWorld).xyz : p.Vel;
                        gEventsOut.Store4(e * 48, asuint(float4(wp, 0.0f)));
                        gEventsOut.Store4(e * 48 + 16, asuint(float4(wv, 0.0f)));
                        gEventsOut.Store4(e * 48 + 32, asuint(p.Color));
                    }
                }
                p.Life = 0.0f;
            }
            Store(id.x, p);
            if (p.Life > 0.0f)
                InterlockedAdd(gsAlive, 1u);
        }
    }
    // 살아 있는 수: 그룹 안에서 먼저 더하고 그룹마다 한 번만 전역 원자 연산 (백만 개에서도 한 주소에 몰리지 않게)
    GroupMemoryBarrierWithGroupSync();
    if (local == 0 && gsAlive > 0)
        gState.InterlockedAdd(4, gsAlive);
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

technique11 ResetTech { pass P0 { SetComputeShader(CompileShader(cs_5_0, ResetCS())); } }
technique11 SpawnTech { pass P0 { SetComputeShader(CompileShader(cs_5_0, SpawnCS())); } }
technique11 UpdateTech { pass P0 { SetComputeShader(CompileShader(cs_5_0, UpdateCS())); } }

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
