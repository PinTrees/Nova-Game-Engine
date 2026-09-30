//=============================================================================
// 44. TreeCommon.fx  (NOVA 나무 공통 - InstancedBasic / BuildShadowMap / SsaoNormalDepth 에서 include)
//
// 텍스처 없이 수학식만으로 그리는 나무 (SpeedTree 처럼 절차적으로 만든 메시 + 절차적 셰이더)
//  - 수피: 가지 방향으로 길게 늘인 3D 값 노이즈 → 세로 골(능선), 노이즈 기울기로 법선, 위를 향한 면에 이끼
//  - 잎: 카드(사각형) 하나에 잎 여러 장을 거리 함수(SDF)로 그린다. 밖은 clip → 잎 모양 텍스처가 필요 없다
//  - 바람: Unreal/SpeedTree 처럼 계층 흔들림 (줄기 → 1차 가지 → 2차 가지 → 잎 떨림).
//          가지는 붙은 지점에서 부모의 흔들림 값을 물려받아 관절이 떨어지지 않는다
// 세 효과가 같은 TreeWorldPos 를 쓰므로 깊이 사전 패스·그림자·본 패스의 위치가 정확히 같다.
//=============================================================================

cbuffer cbTree
{
    float4x4 gTreeWorld;
    float4x4 gTreeWorldInvTranspose;
    float4 gTreeWind;          // xyz = 바람 방향(월드) * 세기(0~1), w = 시간(초)
    float4 gTreeWindParams;    // x = 줄기 흔들림(m), y = 가지 흔들림(m), z = 잎 떨림(m), w = 나무마다 다른 위상
    float4 gTreeBarkColor;     // rgb = 수피 색(감마), a = 이끼 양
    float4 gTreeBarkParams;    // x = 골 깊이, y = 골 주파수(1/m), z = smoothness, w = 밝은 반점 양
    float4 gTreeMossColor;     // rgb = 이끼 색(감마)
    float4 gTreeLeafColor;     // rgb = 잎 색(감마), a = 색 변화
    float4 gTreeLeafColor2;    // rgb = 두 번째 잎 색(감마), a = 모양 (0 Broad, 1 Oval, 2 Needle)
    float4 gTreeLeafParams;    // x = 투과(뒤에서 비치는 빛), y = smoothness, z = 카드당 잎 수, w = 잎 길이(카드 비율)
};

struct TreeVertexIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 UV : TEXCOORD;      // 수피: (둘레 0~1, 길이 m) / 잎: 카드 (0~1, 0~1), y = 0 이 잔가지 쪽
    float4 Wind : WIND;        // x = 줄기 가중(높이²), y = 1차 가지 가중, z = 2차 가지 가중, w = 잎 떨림 가중
    float4 Axis : AXIS;        // xyz = 가지 방향(수피) / 카드 위쪽(잎), w = AO
    float4 Phase : PHASE;      // x, y = 1차 · 2차 가지 위상 (0~1), z = 잎 무늬 시드, w = 가지 밑동 반지름(수피)
};

static const float kTreeTau = 6.2831853f;

// ---------------------------------------------------------------- 바람
float3 TreeWindOffset(TreeVertexIn v, float3 normalW)
{
    const float t = gTreeWind.w;
    const float strength = length(gTreeWind.xyz);
    const float3 dir = strength > 0.0001f ? gTreeWind.xyz / strength : float3(1.0f, 0.0f, 0.0f);
    const float3 side = float3(-dir.z, 0.0f, dir.x);
    const float ph = gTreeWindParams.w;

    // 줄기: 바람 쪽으로 기운 채 돌풍(두 사인의 곱)에 따라 천천히 흔들린다
    const float gust = 0.65f + 0.35f * sin(t * 0.73f + ph) * sin(t * 1.37f + ph * 2.1f);
    float3 offset = dir * (strength * gTreeWindParams.x * v.Wind.x * (gust + 0.25f * sin(t * 1.9f + ph)));

    // 1차 가지: 가지마다 위상이 다른 흔들림 (바람 방향 + 옆 + 위아래)
    const float p1 = v.Phase.x * kTreeTau;
    const float b1 = sin(t * 2.1f + p1) * 0.6f + sin(t * 3.7f + p1 * 1.7f) * 0.4f;
    offset += (dir * (0.6f + 0.4f * b1) + side * (b1 * 0.5f) + float3(0.0f, b1 * 0.35f, 0.0f)) * (strength * gTreeWindParams.y * v.Wind.y);

    // 2차 가지 (잔가지): 더 빠르게
    const float p2 = v.Phase.y * kTreeTau;
    const float b2 = sin(t * 3.9f + p2) * 0.7f + sin(t * 6.3f + p2 * 1.3f) * 0.3f;
    offset += (side * b2 + float3(0.0f, b2 * 0.5f, 0.0f) + dir * 0.4f) * (strength * gTreeWindParams.y * 0.6f * v.Wind.z);

    // 잎 떨림: 법선 방향 고주파
    offset += normalW * (sin(t * 11.0f + p2 * 13.0f + v.PosL.y * 2.0f) * strength * gTreeWindParams.z * v.Wind.w);
    return offset;
}

// 바람이 적용된 월드 위치 (세 효과가 모두 이 함수를 쓴다)
float3 TreeWorldPos(TreeVertexIn v, out float3 normalW)
{
    normalW = normalize(mul(v.NormalL, (float3x3) gTreeWorldInvTranspose));
    const float3 posW = mul(float4(v.PosL, 1.0f), gTreeWorld).xyz;
    return posW + TreeWindOffset(v, normalW);
}

// ---------------------------------------------------------------- 노이즈
float TreeHash11(float n)
{
    return frac(sin(n * 12.9898f) * 43758.5453f);
}

float TreeHash13(float3 p)
{
    p = frac(p * 0.1031f);
    p += dot(p, p.zyx + 31.32f);
    return frac((p.x + p.y) * p.z);
}

float TreeValueNoise(float3 p)
{
    const float3 i = floor(p);
    const float3 f = frac(p);
    const float3 u = f * f * (3.0f - 2.0f * f);
    const float a = TreeHash13(i + float3(0, 0, 0)), b = TreeHash13(i + float3(1, 0, 0));
    const float c = TreeHash13(i + float3(0, 1, 0)), d = TreeHash13(i + float3(1, 1, 0));
    const float e = TreeHash13(i + float3(0, 0, 1)), g = TreeHash13(i + float3(1, 0, 1));
    const float h = TreeHash13(i + float3(0, 1, 1)), k = TreeHash13(i + float3(1, 1, 1));
    return lerp(lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y), lerp(lerp(e, g, u.x), lerp(h, k, u.x), u.y), u.z);
}

// ---------------------------------------------------------------- 수피
// 둘레 방향으로 주기가 period 칸인 2D 값 노이즈 (u 가 0 → 1 로 한 바퀴 돌아도 이음매가 없다)
float TreeNoisePeriodic(float2 p, float period)
{
    const float2 i = floor(p);
    const float2 f = frac(p);
    const float2 u = f * f * (3.0f - 2.0f * f);
    const float x0 = i.x - period * floor(i.x / period);
    const float x1 = (x0 + 1.0f) - period * floor((x0 + 1.0f) / period);
    const float a = TreeHash13(float3(x0, i.y, period)), b = TreeHash13(float3(x1, i.y, period));
    const float c = TreeHash13(float3(x0, i.y + 1.0f, period)), d = TreeHash13(float3(x1, i.y + 1.0f, period));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

// 수피 높이 (0 = 틈, 1 = 껍질 판). uv = (둘레 0~1, 길이 m), r0 = 가지 밑동 반지름
//  세로로 6 배 늘인 노이즈의 0.5 등고선 = 위아래로 구불구불 이어지는 틈 (참나무 껍질처럼)
float TreeBarkHeightUV(float2 uv, float r0)
{
    const float freq = gTreeBarkParams.y;
    const float cells = max(3.0f, round(6.2831853f * r0 * freq));
    const float2 p = float2(uv.x * cells, uv.y * freq * 0.16f);
    const float n = TreeNoisePeriodic(p, cells) * 0.65f + TreeNoisePeriodic(float2(p.x * 2.0f, p.y * 2.0f + 5.7f), cells * 2.0f) * 0.35f;
    const float crack = smoothstep(0.012f, 0.08f, abs(n - 0.5f));
    const float plate = TreeNoisePeriodic(float2(p.x * 3.0f, p.y * 5.0f + 3.1f), cells * 3.0f);
    return crack * lerp(0.7f, 1.0f, plate);
}

// ---------------------------------------------------------------- 잎 (SDF)
// 잎 한 장의 거리 (< 0 = 안). p, base = 카드 uv, dir = 잎 방향, len/width = uv 단위
float TreeLeafSDF(float2 p, float2 base, float2 dir, float len, float width, float shape, out float along, out float across)
{
    const float2 d = p - base;
    along = dot(d, dir) / len;
    across = dot(d, float2(-dir.y, dir.x));
    const float a = saturate(along);
    float profile;
    if (shape < 0.5f)
        profile = pow(sin(3.14159f * pow(a, 0.8f)), 0.65f) * (1.0f - 0.15f * a);   // Broad: 넓고 끝이 뾰족
    else if (shape < 1.5f)
        profile = pow(sin(3.14159f * a), 0.9f);                                   // Oval: 버들잎처럼 길쭉
    else
        profile = 1.0f - a * 0.6f;                                                // Needle: 가는 바늘
    const float dAcross = abs(across) - width * profile;
    const float dAlong = max(-along, along - 1.0f) * len;
    return max(dAcross, dAlong);
}

struct TreeLeafHit
{
    float Dist;     // < 0 = 잎 또는 잔가지 위
    float Along;    // 잎 안에서 0(꼭지) ~ 1(끝)
    float Across;   // 가운데 잎맥에서 떨어진 거리 (uv)
    float Id;       // 카드 안 잎 번호 (색 변화용)
    bool Twig;      // 가운데 잔가지
};

// 카드 하나 = 가운데 잔가지 + 양옆으로 번갈아 난 잎 N 장 + 끝 잎 한 장
TreeLeafHit TreeLeafCluster(float2 uv, float seed)
{
    const float shape = gTreeLeafColor2.a;
    const int count = clamp((int) gTreeLeafParams.z, 1, 16);
    const float size = gTreeLeafParams.w;
    const bool needle = shape > 1.5f;

    TreeLeafHit hit;
    hit.Dist = 1000.0f;
    hit.Along = 0.0f;
    hit.Across = 0.0f;
    hit.Id = 0.0f;
    hit.Twig = false;

    // 잔가지: 아래(0)에서 0.85 까지 가는 선
    const float twigTop = needle ? 0.95f : 0.85f;
    const float twigD = max(abs(uv.x - 0.5f) - lerp(0.014f, 0.006f, saturate(uv.y / twigTop)), max(-uv.y, uv.y - twigTop));
    if (twigD < 0.0f)
    {
        hit.Dist = twigD;
        hit.Twig = true;
    }

    [loop]
    for (int i = 0; i <= count; ++i)
    {
        const float r = TreeHash11(seed * 91.7f + i * 7.13f);
        const bool tip = i == count;   // 마지막 = 끝 잎 (위로)
        const float t = tip ? 1.0f : (i + 0.5f) / count;
        const float side = (i & 1) ? 1.0f : -1.0f;
        float angle = tip ? 0.0f : side * radians(lerp(needle ? 62.0f : 58.0f, needle ? 40.0f : 32.0f, t) + (r - 0.5f) * 14.0f);
        const float2 dir = float2(sin(angle), cos(angle));
        const float len = size * (tip ? 0.9f : lerp(1.0f, 0.78f, t)) * (0.88f + 0.24f * r) * (needle ? 0.8f : 1.0f);
        const float width = needle ? 0.012f : size * (shape < 0.5f ? 0.34f : 0.2f);
        const float2 base = float2(0.5f, (tip ? twigTop : lerp(0.1f, twigTop - 0.06f, t)));
        float along, across;
        const float d = TreeLeafSDF(uv, base, dir, len, width, shape, along, across);
        if (d < hit.Dist)
        {
            hit.Dist = d;
            hit.Along = along;
            hit.Across = across / max(width, 0.001f);
            hit.Id = (float) i + r;
            hit.Twig = false;
        }
    }
    return hit;
}
