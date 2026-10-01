//=============================================================================
// 47. RockCommon.fx  (NOVA 바위·절벽 공통 - InstancedBasic / BuildShadowMap / SsaoNormalDepth 에서 include)
//
// 절차적 바위 (RockGenerator 의 SDF 메시) + 절차적 재질:
//  - 디테일: 물체 공간 triplanar 로 균열 노멀·높이 텍스처(Resources/Packages/Rock/Textures/RockDetail.png)를 세 방향에서 읽어 섞는다
//  - 색: 바탕 ↔ 지층 색 띠(물체 높이), 디테일 높이로 밝기, 오목한 곳(정점 Cavity) 어둡게·모서리 밝게, 위를 향한 면에 이끼, 땅에 닿는 곳 어둡게
// 세 효과가 같은 RockWorldPos 를 쓰므로 깊이 사전 패스·그림자·본 패스의 위치가 정확히 같다 (본 패스는 깊이 EQUAL).
//=============================================================================

cbuffer cbRock
{
    float4 gRockBaseColor;     // rgb 감마
    float4 gRockStrataColor;   // rgb 감마
    float4 gRockMossColor;     // rgb 감마
    float4 gRockParams;        // x 지층 대비, y 이끼 양, z 디테일 노멀 세기, w 디테일 한 장 크기(m)
    float4 gRockParams2;       // x smoothness, y 바위마다 색 차이, z 지층 수, w 바위 높이(m)
};

Texture2D gRockDetail;          // rgb = 탄젠트 노멀, a = 높이

SamplerState samRock
{
    Filter = ANISOTROPIC;
    MaxAnisotropy = 8;
    AddressU = WRAP;
    AddressV = WRAP;
};

// 인스턴스 (바위 하나): 월드 행렬 + x = 색 차이 씨앗(0~1)
struct RockInstanceIn
{
    row_major float4x4 World : WORLD;
    float4 Extra : INSTANCE;
};

struct RockVertexIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 AoCavity : TEXCOORD;   // x = AO (1 트임), y = 오목함 (-1 볼록 ~ +1 오목)
};

float3 RockWorldPos(RockVertexIn v, RockInstanceIn inst, out float3 normalW)
{
    normalW = normalize(mul(v.NormalL, (float3x3) inst.World));
    return mul(float4(v.PosL, 1.0f), inst.World).xyz;
}

float RockHash(float3 p)
{
    p = frac(p * 0.3183099f + 0.1f);
    p *= 17.0f;
    return frac(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float RockValueNoise(float3 x)
{
    const float3 i = floor(x), f = frac(x);
    const float3 u = f * f * (3.0f - 2.0f * f);
    return lerp(lerp(lerp(RockHash(i + float3(0, 0, 0)), RockHash(i + float3(1, 0, 0)), u.x),
                     lerp(RockHash(i + float3(0, 1, 0)), RockHash(i + float3(1, 1, 0)), u.x), u.y),
                lerp(lerp(RockHash(i + float3(0, 0, 1)), RockHash(i + float3(1, 0, 1)), u.x),
                     lerp(RockHash(i + float3(0, 1, 1)), RockHash(i + float3(1, 1, 1)), u.x), u.y), u.z);
}
