//=============================================================================
// 48. DetailCommon.fx  (NOVA 지형 디테일: 풀·꽃·작은 돌 - InstancedBasic / BuildShadowMap / SsaoNormalDepth 에서 include)
//
// DetailRenderer 가 지형 밀도 맵에서 흩뿌린 덩어리(풀 잎 여러 장 / 꽃 / 돌 몇 개)를 인스턴싱으로 그린다.
//  - 바람: 바람 방향으로 흘러가는 노이즈 물결(돌풍) + 잎마다 작은 떨림. 잎 끝일수록(휨 가중) 많이 휜다
//  - 거리: 멀수록 순위(rank)가 높은 덩어리부터 땅속으로 줄어들어 빠지고(성기게), 남은 덩어리는 넓혀 덮임을 유지한다
//  - 지면 맞춤: 덩어리 위쪽 = 위 ↔ 지면 법선 사이 (돌은 지면을 따라 눕는다)
// 세 효과가 같은 DetailWorldPos 를 쓰므로 깊이 사전 패스·그림자·본 패스의 위치가 정확히 같다 (본 패스는 깊이 EQUAL).
//=============================================================================

cbuffer cbDetail
{
    float4 gDetailWind;         // xy = 바람 방향(월드 xz), z = 시간 × 바람 속도, w = 휨 세기 (Wind Bending)
    float4 gDetailWindParams;   // x = 1 / 바람 물결 크기(m), y = 종류의 바람 반응, z = 종류의 높이(m), w = 시간(초)
    float4 gDetailCam;          // xyz = 카메라 위치 (거리 솎기 기준), w = 그리는 거리(m)
    float4 gDetailHealthy;      // rgb = 건강한 색 / 돌 색 (감마), a = 마른 색 섞임
    float4 gDetailDry;          // rgb = 마른 색 / 돌 변화 색 (감마), a = 얼룩 크기 (1/m)
    float4 gDetailFlower;       // rgb = 꽃잎 색 (감마), a = 덩어리마다 밝기 차이
    float4 gDetailCenter;       // rgb = 꽃 가운데 색 (감마), a = 잎 끝 색
    float4 gDetailParams;       // x = 투과, y = smoothness, z = 종류 (0 풀, 1 꽃, 2 돌), w = 지면 맞춤
};

struct DetailVertexIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 UV : TEXCOORD;       // x = 잎 가로 (-1 ~ 1), y = 잎 세로 (0 뿌리 ~ 1 끝)
    float4 Data : DETAIL;       // x = 휨 가중 (0 뿌리 ~ 1 끝), y = 부위 (0 잎, 1 꽃잎, 2 꽃 가운데, 3 돌, 4 줄기), z = 잎마다 난수, w = AO
};

// 인스턴스 (덩어리 하나)
struct DetailInstanceIn
{
    float4 PosYaw : INSTANCE0;  // xyz = 지면 위치(월드), w = 회전 (라디안, Y 축)
    float4 Scale : INSTANCE1;   // x = 폭 배율, y = 높이 배율, z = 색 난수 (0~1), w = 순위 (0~1)
    float4 Ground : INSTANCE2;  // xyz = 지면 법선, w = 바람 위상 (0~1)
};

float DetailHash(float2 p)
{
    p = frac(p * float2(0.1031f, 0.1030f));
    p += dot(p, p.yx + 33.33f);
    return frac((p.x + p.y) * p.x);
}

float DetailNoise(float2 x)
{
    const float2 i = floor(x), f = frac(x);
    const float2 u = f * f * (3.0f - 2.0f * f);
    return lerp(lerp(DetailHash(i), DetailHash(i + float2(1, 0)), u.x), lerp(DetailHash(i + float2(0, 1)), DetailHash(i + float2(1, 1)), u.x), u.y);
}

// 거리 비율(0~1) → 남기는 비율. CPU (DetailRenderer::Keep) 와 같은 식
float DetailKeep(float t)
{
    return (1.0f - 0.9f * smoothstep(0.12f, 0.7f, t)) * (1.0f - smoothstep(0.85f, 1.0f, t));
}

// 덩어리 정점 → 월드. fade = 1 그대로 ~ 0 사라짐 (거리 솎기)
float3 DetailWorldPos(DetailVertexIn v, DetailInstanceIn inst, out float3 normalW, out float fade)
{
    const float3 base = inst.PosYaw.xyz;
    const float keep = DetailKeep(distance(base, gDetailCam.xyz) / max(gDetailCam.w, 1.0f));
    fade = saturate((keep - inst.Scale.w) / 0.08f);
    const float widen = min(rsqrt(max(keep, 0.05f)), 2.5f);   // 성기게 된 만큼 남은 덩어리를 넓힌다
    const bool pebble = gDetailParams.z > 1.5f;

    float3 p = v.PosL;
    p.xz *= inst.Scale.x * (pebble ? fade : widen);
    p.y *= inst.Scale.y * fade;                               // 줄어들며 땅속으로
    float s, c;
    sincos(inst.PosYaw.w, s, c);
    const float3 r = float3(p.x * c - p.z * s, p.y, p.x * s + p.z * c);
    const float3 n = float3(v.NormalL.x * c - v.NormalL.z * s, v.NormalL.y, v.NormalL.x * s + v.NormalL.z * c);

    // 지면 맞춤: 덩어리 위쪽 축을 위 ↔ 지면 법선 사이로
    const float3 up = normalize(lerp(float3(0, 1, 0), inst.Ground.xyz, gDetailParams.w));
    const float3 ax = normalize(cross(up, float3(0, 0, 1)));
    const float3 az = cross(ax, up);
    float3 posW = base + ax * r.x + up * r.y + az * r.z;
    normalW = normalize(ax * n.x + up * n.y + az * n.z);

    // 바람: 흘러가는 노이즈 물결(돌풍) + 잎 떨림. 휜 만큼 높이를 낮춰 잎 길이를 대략 유지
    const float response = gDetailWindParams.y;
    if (response > 0.0f)
    {
        const float2 dir = gDetailWind.xy;
        const float2 side = float2(-dir.y, dir.x);
        const float2 q = base.xz * gDetailWindParams.x - dir * gDetailWind.z;
        const float gust = DetailNoise(q) * 0.7f + DetailNoise(q * 2.3f + 7.1f) * 0.3f;
        const float ph = inst.Ground.w * 6.2831853f + v.Data.z * 2.0f;
        const float t = gDetailWindParams.w;
        const float flutter = sin(t * (3.1f + v.Data.z * 1.7f) + ph) * 0.5f + sin(t * 5.3f + ph * 1.3f) * 0.25f;
        const float bend = (0.15f + gust * gust * 1.6f) * gDetailWind.w * response;
        const float h = gDetailWindParams.z * inst.Scale.y * fade;
        float2 off = (dir * bend + side * flutter * 0.12f * (0.3f + gust) * gDetailWind.w * response) * v.Data.x * h;
        const float len2 = dot(off, off);
        posW.xz += off;
        posW.y -= len2 / max(2.0f * h, 0.02f) * v.Data.x;
    }
    return posW;
}
