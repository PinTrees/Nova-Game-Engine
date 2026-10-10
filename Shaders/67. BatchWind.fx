// MeshBatcher 묶음의 바람 (PCG 의 나무 · 풀 · 덤불 — 바깥 묶음이 gBatchWindShape.w = 1 로 켠다, 나머지 묶음은 0 = 그대로)
//  본 패스 (32) · 깊이 프리패스 (28) · 그림자 (26) 가 같은 함수 → 깊이 프리패스와 본 패스의 위치가 같아 EQUAL 깊이 검사가 맞는다.
//  정점 바람 가중치가 없는 모델 (FBX — Unity SpeedTree 의 바람 데이터는 가져오지 않는다) 도 되게 모델 높이로:
//   - 흔들림: 바람 방향으로 (밑동에서 높이 / 모델 높이)² — 밑동은 그대로, 꼭대기가 가장 크게. 돌풍 = 바람을 따라 지나가는 느린 물결
//   - 잎 떨림: Alpha Clipping 재질 (잎 · 풀잎) 만, 정점 자리마다 다른 빠른 떨림
#ifndef NOVA_BATCH_WIND
#define NOVA_BATCH_WIND

cbuffer cbBatchWind
{
    float4 gBatchWind;        // xyz = 바람 방향 (월드, 단위) × 세기, w = 시간 (초)
    float4 gBatchWindShape;   // x = 꼭대기 흔들림 (m, 세기 1 일 때), y = 잎 떨림 (m), z = 모델 높이 (메시 단위 — 인스턴스 Y 축 길이를 곱해 m), w = 1 켜짐
};

float3 BatchWindOffset(float3 posW, float4x4 world)
{
    const float strength = length(gBatchWind.xyz);
    [branch]
    if (gBatchWindShape.w < 0.5f || strength < 1e-4f)
        return float3(0.0f, 0.0f, 0.0f);
    const float3 dir = gBatchWind.xyz / strength;
    const float t = gBatchWind.w;
    const float3 base = world[3].xyz;   // 모델 밑동 (PCG 점)
    const float3 local = posW - base;   // 32 km 월드에서도 정밀도가 버티게 밑동 기준
    const float height = max(gBatchWindShape.z * length(world[1].xyz), 0.05f);
    const float hn = saturate(local.y / height);
    // 모델마다 다른 위상 (자리 해시)
    const float ph = frac(dot(base.xz, float2(0.0137f, 0.0071f))) * 6.2831853f;
    // 돌풍: 바람 방향으로 180 m 물결이 지나간다 (이웃 나무가 차례로 숙인다) — sin 이 2π 주기라 frac 로 잘라도 이어진다
    const float wave = frac(dot(base.xz, dir.xz) * (1.0f / 180.0f)) * 6.2831853f;
    const float gust = 0.6f + 0.4f * sin(t * 0.8f - wave);
    const float sway = gust * (0.75f + 0.25f * sin(t * 1.7f + ph));
    const float3 side = float3(-dir.z, 0.0f, dir.x);
    float3 offset = (dir * sway + side * (0.2f * sin(t * 1.3f + ph * 1.7f))) * (gBatchWindShape.x * strength * hn * hn);
    // 숙인 만큼 조금 내려 길이를 지킨다 (꼭대기가 늘어나 보이지 않게)
    offset.y -= 0.5f * dot(offset.xz, offset.xz) / max(local.y, 0.1f);
    // 잎 떨림 (잎 · 풀잎 재질만)
    const float p2 = dot(local, float3(2.1f, 1.7f, 2.6f)) + ph;
    offset += float3(sin(t * 6.1f + p2), 0.6f * sin(t * 7.7f + p2 * 1.3f), cos(t * 5.3f + p2 * 0.8f)) * (gBatchWindShape.y * strength * hn);
    return offset;
}

#endif
