//=============================================================================
// 44. TreeCommon.fx  (NOVA 나무 공통 - InstancedBasic / BuildShadowMap / SsaoNormalDepth 에서 include)
//
// 텍스처 없이 수학식만으로 그리는 나무 (SpeedTree 처럼 절차적으로 만든 메시 + 절차적 셰이더)
//  - 수피: 세로 균열 높이 타일(실행 중에 굽는다) → 기울기로 법선, 위를 향한 면에 이끼 (값 노이즈)
//  - 잎: 카드 하나에 잎 여러 장을 거리 함수(SDF)로 실행 중에 텍스처로 한 번 구워 두고, 픽셀은 한 번 읽어 알파 컷
//    (픽셀마다 잎을 반복 계산하면 바늘잎처럼 잎이 많을 때 그림자 캐스케이드·깊이 패스까지 곱해져 매우 무겁다)
//  - 바람: Unreal/SpeedTree 처럼 계층 흔들림 (줄기 → 1차 가지 → 2차 가지 → 잎 떨림).
//          가지는 붙은 지점에서 부모의 흔들림 값을 물려받아 관절이 떨어지지 않는다
// 세 효과가 같은 TreeWorldPos 를 쓰므로 깊이 사전 패스·그림자·본 패스의 위치가 정확히 같다.
//
// 숲 (TreeRenderer.cpp): 같은 모양·색의 나무는 인스턴싱으로 한 번에 그린다 (정점 = 메시, 인스턴스 = 월드 행렬 + 값).
//  LOD: 가까이 = 전체 메시(LOD0), 중간 = 가지·잎 카드를 줄인 메시(LOD1), 멀리 = 8 방향에서 구운 빌보드(임포스터).
//  단계가 바뀌는 구간은 화면 디더로 두 단계를 섞는다 (Unity 의 LOD Cross Fade).
//=============================================================================

cbuffer cbTree
{
    float4 gTreeWind;          // xyz = 바람 방향(월드) * 세기(0~1), w = 시간(초)
    float4 gTreeWindParams;    // x = 줄기 흔들림(m, 높이 1 배 나무 기준), y = 가지 흔들림(m), z = 잎 떨림(m)
    float4 gTreeBarkColor;     // rgb = 수피 색(감마), a = 이끼 양
    float4 gTreeBarkParams;    // x = 골 깊이, y = 골 주파수(1/m), z = smoothness, w = 밝은 반점 양
    float4 gTreeMossColor;     // rgb = 이끼 색(감마)
    float4 gTreeLeafColor;     // rgb = 잎 색(감마), a = 색 변화
    float4 gTreeLeafColor2;    // rgb = 두 번째 잎 색(감마), a = 모양 (0 Broad, 1 Oval, 2 Needle)
    float4 gTreeLeafParams;    // x = 투과(뒤에서 비치는 빛), y = smoothness, z = 카드당 잎 수, w = 잎 길이(카드 비율)
    float4 gTreeImpostor;      // x = 프레임 수, y = 프레임 한 변(m, 물체 공간), z = 프레임 중심 높이(m), w = 알파 문턱
    float4 gTreeViewPos;       // 빌보드가 바라볼 곳: w = 0 이면 xyz = 위치(카메라/광원), w = 1 이면 xyz = 그쪽 방향(방향광)
};

// 인스턴스 (나무 한 그루): 월드 행렬 + x = 색 변화(0.5 = 그대로), y = 바람 위상, z = LOD 섞기 문턱, w = 섞기 쪽(0 나가는 단계, 1 들어오는 단계)
struct TreeInstanceIn
{
    row_major float4x4 World : WORLD;
    float4 Extra : INSTANCE;
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
float3 TreeWindOffset(TreeVertexIn v, float3 normalW, float ph, float heightScale)
{
    const float t = gTreeWind.w;
    const float strength = length(gTreeWind.xyz);
    const float3 dir = strength > 0.0001f ? gTreeWind.xyz / strength : float3(1.0f, 0.0f, 0.0f);
    const float3 side = float3(-dir.z, 0.0f, dir.x);

    // 줄기: 바람 쪽으로 기운 채 돌풍(두 사인의 곱)에 따라 천천히 흔들린다
    const float gust = 0.65f + 0.35f * sin(t * 0.73f + ph) * sin(t * 1.37f + ph * 2.1f);
    float3 offset = dir * (strength * gTreeWindParams.x * heightScale * v.Wind.x * (gust + 0.25f * sin(t * 1.9f + ph)));

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

// 바람이 적용된 월드 위치 (세 효과가 모두 이 함수를 쓴다). 나무는 Y 축 회전 + 크기라 법선은 월드 3x3 로 충분
float3 TreeWorldPos(TreeVertexIn v, TreeInstanceIn inst, out float3 normalW)
{
    normalW = normalize(mul(v.NormalL, (float3x3) inst.World));
    const float3 posW = mul(float4(v.PosL, 1.0f), inst.World).xyz;
    return posW + TreeWindOffset(v, normalW, inst.Extra.y * kTreeTau, length(inst.World[1].xyz));
}

// ---------------------------------------------------------------- LOD 섞기 (화면 디더)
float TreeDither(float2 pixel)
{
    return frac(52.9829189f * frac(dot(floor(pixel), float2(0.06711056f, 0.00583715f))));
}

// fade = (문턱, 쪽). 나가는 단계는 디더 >= 문턱, 들어오는 단계는 디더 < 문턱 인 픽셀만 → 두 단계가 겹치지 않고 나뉜다
void TreeLodClip(float2 pixel, float2 fade)
{
    const float d = TreeDither(pixel);
    clip(fade.y < 0.5f ? d - fade.x : fade.x - d - 0.0001f);
}

// ---------------------------------------------------------------- 임포스터 (빌보드)
// 구운 아틀라스: 가로로 프레임 N 개. gTreeImpostorAlbedo rgb = 알베도(감마), a = 덮임 / gTreeImpostorNormal rgb = 물체 공간 법선, a = AO
Texture2D gTreeImpostorAlbedo;
Texture2D gTreeImpostorNormal;

SamplerState samTreeImpostor
{
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = CLAMP;
    AddressV = CLAMP;
};

struct TreeImpostorGeom
{
    float3 PosW;
    float2 UV;      // 아틀라스
    float3 AxisX;   // 물체 X, Z 축 (월드, 정규화) → 구운 법선을 월드로
    float3 AxisZ;
};

// vid 0~5 (삼각형 둘) → 빌보드 꼭짓점. 바라볼 방향(gTreeViewPos)에 가장 가까운 프레임을 고르고, 세로축(Y) 기준으로 돌린다
TreeImpostorGeom TreeImpostorVertex(uint vid, TreeInstanceIn inst)
{
    const float2 corners[6] = { float2(-1, -1), float2(-1, 1), float2(1, 1), float2(-1, -1), float2(1, 1), float2(1, -1) };
    const float2 c = corners[vid];
    const float3 center = inst.World[3].xyz;
    const float scaleXZ = length(inst.World[0].xyz);
    const float scaleY = length(inst.World[1].xyz);
    const float3 ax = inst.World[0].xyz / max(scaleXZ, 1e-5f);
    const float3 az = inst.World[2].xyz / max(length(inst.World[2].xyz), 1e-5f);

    float3 toView = gTreeViewPos.w > 0.5f ? gTreeViewPos.xyz : gTreeViewPos.xyz - center;
    toView.y = 0.0f;
    toView = dot(toView, toView) > 1e-8f ? normalize(toView) : az;

    // 물체 공간 방위 (0 = 물체 +Z 쪽에서 봄) → 가까운 프레임
    const float frames = gTreeImpostor.x;
    const float azimuth = atan2(dot(toView, ax), dot(toView, az));
    float frame = round(azimuth / (kTreeTau / frames));
    frame = frame - frames * floor(frame / frames);

    // 화면 오른쪽 = cross(위, 앞) (앞 = 보는 쪽 → 나무)
    const float3 right = normalize(cross(float3(0, 1, 0), -toView));
    const float halfSize = gTreeImpostor.y * 0.5f;

    TreeImpostorGeom g;
    g.PosW = center + right * (c.x * halfSize * scaleXZ) + float3(0, 1, 0) * ((gTreeImpostor.z + c.y * halfSize) * scaleY);
    g.UV = float2((frame + c.x * 0.5f + 0.5f) / frames, 0.5f - c.y * 0.5f);
    g.AxisX = ax;
    g.AxisZ = az;
    return g;
}

float3 TreeImpostorNormalW(float3 packed, float3 ax, float3 az)
{
    const float3 n = packed * 2.0f - 1.0f;
    return normalize(ax * n.x + float3(0, 1, 0) * n.y + az * n.z);
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
// ---------------------------------------------------------------- 구운 텍스처 (TreeTextures.cpp 가 실행 중에 식으로 만든다)
// 잎 아틀라스 (2x2 변형): r = 잎맥에서 거리(±, 0.5 = 가운데), g = 잎 안 위치(0 꼭지 → 1 끝), b = 잎 번호(0 = 잔가지), a = 덮임
Texture2D gTreeLeafTex;
// 수피 타일 (가로·세로 kBarkCells = 8 칸): rg = 기울기 / 16 + 0.5 (칸 단위), b = 높이
Texture2D gTreeBarkTex;
static const float kTreeBarkCells = 8.0f;

SamplerState samTreeLeaf
{
    Filter = ANISOTROPIC;
    MaxAnisotropy = 8;
    AddressU = CLAMP;
    AddressV = CLAMP;
};

SamplerState samTreeBark
{
    Filter = ANISOTROPIC;
    MaxAnisotropy = 8;
    AddressU = WRAP;
    AddressV = WRAP;
};

// 카드 uv(v = 0 이 잔가지 쪽) → 아틀라스 칸 하나. 시드로 네 변형 중 하나 + 좌우 뒤집기
float4 TreeLeafSample(float2 uv, float seed)
{
    const float cell = floor(frac(seed) * 4.0f);
    const float2 offset = float2(fmod(cell, 2.0f), floor(cell * 0.5f)) * 0.5f;
    float2 c = float2(uv.x, 1.0f - uv.y);
    if (frac(seed * 7.31f) > 0.5f)
        c.x = 1.0f - c.x;
    return gTreeLeafTex.Sample(samTreeLeaf, offset + (0.004f + saturate(c) * 0.992f) * 0.5f);
}

// 잎 자르기 (본 패스·그림자·깊이 패스 공용)
void TreeLeafClip(float2 uv, float seed)
{
    clip(TreeLeafSample(uv, seed).a - 0.5f);
}

// 수피: uv = (둘레 0~1, 길이 m), r0 = 가지 밑동 반지름. 둘레에 타일을 정수 번 두르고 세로로 늘인다
//  반환: xy = 기울기 (1 m 당, 둘레 / 가지 방향), z = 높이
float3 TreeBarkSample(float2 uv, float r0)
{
    const float freq = gTreeBarkParams.y;
    const float circumference = 6.2831853f * r0;
    const float tiles = max(1.0f, round(circumference * freq / kTreeBarkCells));
    const float alongScale = freq * 0.16f;   // 세로 칸 = 가로 칸의 약 6 배 길이
    const float4 t = gTreeBarkTex.Sample(samTreeBark, float2(uv.x * tiles, uv.y * alongScale / kTreeBarkCells));
    const float2 g = (t.rg - 0.5f) * 16.0f;  // 칸 당 높이 변화
    return float3(g.x * (kTreeBarkCells * tiles / circumference), g.y * alongScale, t.b);
}
