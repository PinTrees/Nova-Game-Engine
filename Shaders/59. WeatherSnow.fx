//=============================================================================
// 59. WeatherSnow.fx — 눈 발자국 맵 (WeatherCover.cpp, com.nova.weather 의 쌓이는 눈)
//
// 카메라 둘레 48 m 를 칸 1024 개로 (4.7 cm). 값 = 눌린 정도 (0 = 새 눈, 1 = 바닥까지 눌림)
//  - 고리 배치: 텍스처 칸 = 월드 칸 % 크기 → 카메라가 움직여도 옮겨 쓰지 않는다. 새로 들어온 칸만 0 으로
//  - StampCS: 아래에서 본 깊이 (스킨 메시 · RigidBody 메시) 가 눈 표면 (위에서 본 덮개 맵의 땅 + 눈 깊이) 보다 낮은 만큼
//  - DeformCS: 이번 도장을 비탈 (gSlope) 로 넓혀 (발자국 벽이 깎아지르지 않게) 남은 발자국과 합친다. 눈이 내리면 조금씩 다시 덮인다
//  - 눌림 맵은 RGBA16F 두 장을 번갈아 (지난 것 읽기 → 새것 쓰기): 어느 기기에서나 거르기 · compute 쓰기가 된다 (R32F 는 GLES 에서 거르기 X, R16F 는 쓰기 X)
//=============================================================================

cbuffer cbSnow
{
    int2 gOrigin;                    // 창의 첫 칸 (월드 칸 번호)
    int2 gOldOrigin;                 // 지난 프레임 창 (여기 없던 칸은 새 눈)
    int gSize;                       // 한 변 칸 수
    float gTexel;                    // 칸 크기 (m)
    float gSnowDepth;                // 눈 깊이 (m)
    float gRefill;                   // 이번 프레임에 다시 덮이는 양
    row_major float4x4 gCoverToTex;  // 월드 → 위에서 본 덮개 맵 (u, v, 깊이)
    row_major float4x4 gBottomToTex; // 월드 → 아래에서 본 맵
    float4 gCoverInfo;               // x 있음, y 깊이 0 의 높이, z 깊이 범위 (m), w 맵 크기
    float4 gBottomInfo;              // x 있음, y 깊이 0 의 높이 (아래), z 깊이 범위 (m), w 맵 크기
    float gSlope;                    // 발자국 벽: 한 칸 멀어질 때 줄어드는 눌림
};

Texture2D<float4> gPrev;          // 지난 눌림 (r)
RWTexture2D<float4> gDeform;      // 새 눌림 (r)
RWTexture2D<float> gStamp;
Texture2D gCover;
Texture2D gBottom;

// 이 텍스처 칸이 맡은 월드 칸 (창 안에서 t 와 나머지가 같은 칸)
int2 WorldCell(int2 t) { return gOrigin + (((t - gOrigin) % gSize) + gSize) % gSize; }
int2 TexCell(int2 w) { return ((w % gSize) + gSize) % gSize; }

[numthreads(8, 8, 1)]
void StampCS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= (uint)gSize || id.y >= (uint)gSize)
        return;
    const int2 t = int2(id.xy);
    const int2 w = WorldCell(t);
    float v = 0.0f;
    if (gCoverInfo.x > 0.5f && gBottomInfo.x > 0.5f)
    {
        const float4 p = float4((float(w.x) + 0.5f) * gTexel, 0.0f, (float(w.y) + 0.5f) * gTexel, 1.0f);
        const float2 cu = mul(p, gCoverToTex).xy;
        const float2 bu = mul(p, gBottomToTex).xy;
        if (all(cu > 0.0f) && all(cu < 1.0f) && all(bu > 0.0f) && all(bu < 1.0f))
        {
            const float cd = gCover.Load(int3(int2(cu * gCoverInfo.w), 0)).r;
            const float bd = gBottom.Load(int3(int2(bu * gBottomInfo.w), 0)).r;
            if (cd < 0.999999f && bd < 0.999999f)
            {
                const float ground = gCoverInfo.y - cd * gCoverInfo.z;
                const float bottom = gBottomInfo.y + bd * gBottomInfo.z;
                const float top = ground + gSnowDepth;
                if (bottom < top && bottom > ground - 0.5f)
                    v = saturate((top - bottom) / max(gSnowDepth, 1e-3f));
            }
        }
    }
    gStamp[t] = v;
}

[numthreads(8, 8, 1)]
void DeformCS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= (uint)gSize || id.y >= (uint)gSize)
        return;
    const int2 t = int2(id.xy);
    const int2 w = WorldCell(t);
    float v = 0.0f;
    if (all(w >= gOldOrigin) && all(w < gOldOrigin + gSize))
        v = gPrev.Load(int3(t, 0)).r;
    v = max(0.0f, v - gRefill);
    // 이번 도장을 비탈로 넓힌다 (7 x 7 — 거리만큼 덜 눌림)
    float s = 0.0f;
    [loop]
    for (int y = -3; y <= 3; ++y)
    {
        [loop]
        for (int x = -3; x <= 3; ++x)
        {
            const int2 n = w + int2(x, y);
            if (any(n < gOrigin) || any(n >= gOrigin + gSize))
                continue;
            s = max(s, gStamp[TexCell(n)] - length(float2(x, y)) * gSlope);
        }
    }
    gDeform[t] = float4(max(v, s), 0.0f, 0.0f, 0.0f);
}

technique11 StampTech { pass P0 { SetComputeShader(CompileShader(cs_5_0, StampCS())); } }
technique11 DeformTech { pass P0 { SetComputeShader(CompileShader(cs_5_0, DeformCS())); } }
