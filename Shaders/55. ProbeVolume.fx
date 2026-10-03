//=============================================================================
// 55. ProbeVolume.fx — Adaptive Probe Volume 실시간 계산 (ProbeVolumes)
//  복셀 = 단계마다 64 x 32 x 64 (프로브 한 칸에 2 복셀), 프로브 = 32 x 16 x 32, 단계는 아틀라스에 Z 로 쌓는다
//  1) Inject  : 얇은 판 (slab) 을 직교로 찍은 알베도 · 노멀 · 깊이 → 그 판의 복셀 (뒤 버퍼)
//  2) Relight : 복셀 = 알베도 x (직접광 · 그림자 · 프로브 간접광) — 32 의 ShadeLit 그대로 (여러 번 튐)
//  3) Update  : 프로브마다 광선을 복셀 속으로 걸어 맞은 복셀의 빛 / 하늘 → L1 SH (섞기는 블렌드 계수로)
//  4) Resample: 단계가 카메라를 따라 옮겨질 때 예전 값을 새 자리로
//  컴퓨트 · UAV 없이 픽셀 셰이더와 렌더 타깃만 — DirectX 11 · OpenGL 같은 길
//=============================================================================
#define NOVA_NO_ENGINE_TECHNIQUES
#include "32. InstancedBasic.fx"

static const float3 kGIVox = float3(64.0f, 32.0f, 64.0f);

cbuffer cbGIPass
{
    float4x4 gGICapView;       // Inject: 판을 찍은 직교 카메라
    float4x4 gGICapViewProj;
    float4x4 gGICapInvView;
    float4 gGICap;             // Inject: x 찍은 그림 폭, y 높이, z 복셀 Z (이 조각), w -
    float4 gGIVox;             // xyz 복셀 볼륨 최소 모서리 (이 단계), w 복셀 크기
    float4 gGIPass;            // x 단계, y 광선 수, z 이 조각 (단계 안 Z), w 프레임 (광선 돌리기 · 바둑판)
    float4 gGIOld[4];          // Resample: 예전 프로브 원점 + 간격
    float4 gGISky;             // x 하늘 밉, y 유효 판정 (뒷면 비율)
};

Texture2D gGICapColor;         // Inject: 알베도 찍기 (감마, a = 덮임)
Texture2D gGICapNormalDepth;   // Inject: 뷰 노멀 + 뷰 깊이
Texture3D gGIVoxAlbedo;        // Relight: 이 단계의 복셀 (앞 버퍼)
Texture3D gGIVoxNormal;
Texture3D gGINormals;          // Update: 모든 단계의 노멀 아틀라스
Texture3D gGIOldSH0;           // Resample: 예전 SH (복사본)
Texture3D gGIOldSH1;
Texture3D gGIOldSH2;
Texture3D gGIOldValid;

SamplerState samGIPoint
{
    Filter = MIN_MAG_MIP_POINT;
    AddressU = CLAMP;
    AddressV = CLAMP;
    AddressW = CLAMP;
};

struct GIVOut
{
    float4 PosH : SV_POSITION;
};

GIVOut VS_GIFull(uint id : SV_VertexID)
{
    GIVOut o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.PosH = float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return o;
}

// ---------------------------------------------------------------- 1) Inject
struct VoxOut
{
    float4 Albedo;
    float4 Normal;
    float4 PlaneP;
    float4 PlaneN;
};

// 복셀 하나: 쓸 것이 없으면 알파 0 (블렌드 SrcAlpha / InvSrcAlpha 로 예전 값이 남는다 — MRT 라 discard 를 못 쓴다)
VoxOut InjectVoxel(float3 idx)
{
    VoxOut o;
    o.Albedo = float4(0, 0, 0, 0);
    o.Normal = float4(0, 0, 0, 0);
    o.PlaneP = float4(0, 0, 0, 0);
    o.PlaneN = float4(0, 0, 0, 0);
    float3 posW = gGIVox.xyz + (idx + 0.5f) * gGIVox.w;
    float4 clip = mul(float4(posW, 1.0f), gGICapViewProj);
    float2 ndc = clip.xy / clip.w;
    int2 texel = int2((ndc.x * 0.5f + 0.5f) * gGICap.x, (0.5f - ndc.y * 0.5f) * gGICap.y);
    if (any(texel < 0) || texel.x >= (int)gGICap.x || texel.y >= (int)gGICap.y)
        return o;
    float4 color = gGICapColor.Load(int3(texel, 0));
    if (color.a < 0.5f)
        return o;
    float4 nd = gGICapNormalDepth.Load(int3(texel, 0));
    float viewZ = mul(float4(posW, 1.0f), gGICapView).z;
    // 면은 그 뒤 (물체 속) 복셀에 — 찍는 쪽에서 보이는 면이므로 조금 더 깊은 곳. 경계에 걸친 면이 앞의 빈 공간 복셀을 막지 않는다
    float dd = (nd.w + gGIVox.w * 0.05f) - viewZ;
    if (dd <= -gGIVox.w * 0.5f || dd > gGIVox.w * 0.5f)
        return o;
    float3 n = normalize(mul(nd.xyz, (float3x3)gGICapInvView));
    o.Albedo = float4(saturate(color.rgb), 1.0f);
    o.Normal = float4(n * 0.5f + 0.5f, 1.0f);
    // 면 평면: 면 위 점 = 복셀 가운데에서 찍는 방향으로 (면 깊이 - 복셀 깊이)
    float3 forward = normalize(gGICapInvView[2].xyz);
    float3 surface = posW + forward * (nd.w - viewZ);
    float offset = dot(surface - posW, n) / gGIVox.w;   // -0.87 .. 0.87
    float4 plane = float4(GIOctEncode(n), saturate(offset / 1.8f + 0.5f), 1.0f);
    float3 an = abs(n);
    float major = an.x >= an.y && an.x >= an.z ? n.x : (an.y >= an.z ? n.y : n.z);
    if (major >= 0.0f) o.PlaneP = plane; else o.PlaneN = plane;
    return o;
}

// 2 조각 (gGICap.z, +1) 을 한 번에: 타깃 = 알베도 · 노멀 · 평면 + · 평면 - x 2
struct Vox2Out
{
    float4 A0 : SV_Target0; float4 N0 : SV_Target1; float4 P0 : SV_Target2; float4 Q0 : SV_Target3;
    float4 A1 : SV_Target4; float4 N1 : SV_Target5; float4 P1 : SV_Target6; float4 Q1 : SV_Target7;
};

Vox2Out PS_Inject(GIVOut pin)
{
    float2 xy = floor(pin.PosH.xy);
    VoxOut v0 = InjectVoxel(float3(xy, gGICap.z));
    VoxOut v1 = InjectVoxel(float3(xy, gGICap.z + 1.0f));
    Vox2Out o;
    o.A0 = v0.Albedo; o.N0 = v0.Normal; o.P0 = v0.PlaneP; o.Q0 = v0.PlaneN;
    o.A1 = v1.Albedo; o.N1 = v1.Normal; o.P1 = v1.PlaneP; o.Q1 = v1.PlaneN;
    return o;
}

// ---------------------------------------------------------------- 2) Relight
struct RelightOut
{
    float4 Radiance : SV_Target0;
    float4 Normal : SV_Target1;
};

float4 RelightVoxel(int3 idx)
{
    float4 a = gGIVoxAlbedo.Load(int4(idx, 0));
    if (a.a < 0.5f)
        return float4(0, 0, 0, 0);
    float3 N = normalize(gGIVoxNormal.Load(int4(idx, 0)).xyz * 2.0f - 1.0f);
    float3 posW = gGIVox.xyz + ((float3)idx + 0.5f) * gGIVox.w;
    LitSurface surf;
    surf.Albedo = ToLinear(a.rgb);
    surf.Metallic = 0.0f;
    surf.Smoothness = 0.0f;
    surf.Occlusion = 1.0f;
    surf.Emission = float3(0, 0, 0);
    surf.Transmission = float3(0, 0, 0);
    surf.Highlights = false;
    surf.Reflections = false;
    surf.ReceiveShadows = true;
    // 표면 바로 앞 (반 복셀) 에서 비춘다 — 그림자 맵 자기 그림자 방지
    float3 rad = ShadeLit(surf, posW + N * (gGIVox.w * 0.5f), N, N, float4(0.5f, 0.5f, 0.0f, 1.0f));
    return float4(rad, 1.0f);
}

// 8 조각 (gGIPass.z ~ +7) 을 한 번에 — 노멀 아틀라스는 복셀 노멀을 그대로 복사한다 (C++)
struct Relight8Out
{
    float4 R0 : SV_Target0; float4 R1 : SV_Target1; float4 R2 : SV_Target2; float4 R3 : SV_Target3;
    float4 R4 : SV_Target4; float4 R5 : SV_Target5; float4 R6 : SV_Target6; float4 R7 : SV_Target7;
};

Relight8Out PS_Relight(GIVOut pin)
{
    int2 xy = int2(floor(pin.PosH.xy));
    int z = (int)gGIPass.z;
    float4 r[8];
    [loop]
    for (int k = 0; k < 8; ++k)
        r[k] = RelightVoxel(int3(xy, z + k));
    Relight8Out o;
    o.R0 = r[0]; o.R1 = r[1]; o.R2 = r[2]; o.R3 = r[3];
    o.R4 = r[4]; o.R5 = r[5]; o.R6 = r[6]; o.R7 = r[7];
    return o;
}

// ---------------------------------------------------------------- 3) Update
struct SHOut
{
    float4 R : SV_Target0;
    float4 G : SV_Target1;
    float4 B : SV_Target2;
    float4 V : SV_Target3;
};

// 단계 cc 의 복셀 좌표 (실수) → 아틀라스 UVW
float3 GIAtlasUVW(int cc, float3 v)
{
    return float3(v.x / kGIVox.x, v.y / kGIVox.y, (cc * kGIVox.z + v.z) / (gGIBias.w * kGIVox.z));
}

// 구면 피보나치 + 프레임마다 돌리기
float3 GIRayDir(uint i, uint n, float frame)
{
    const float golden = 2.39996323f;
    float z = 1.0f - (2.0f * i + 1.0f) / n;
    float r = sqrt(saturate(1.0f - z * z));
    float phi = i * golden + frame * 1.61803f;
    float3 d = float3(r * cos(phi), z, r * sin(phi));
    // 축도 프레임마다 조금씩 기울인다 (고정 방향 줄무늬 방지)
    float a = frame * 0.7548f, b = frame * 0.5698f;
    float ca = cos(a), sa = sin(a), cb = cos(b), sb = sin(b);
    d = float3(d.x, ca * d.y - sa * d.z, sa * d.y + ca * d.z);
    d = float3(cb * d.x + sb * d.z, d.y, -sb * d.x + cb * d.z);
    return d;
}

SHOut PS_Update(GIVOut pin)
{
    int c = (int)gGIPass.x;
    int3 pidx = int3(floor(pin.PosH.xy), (int)gGIPass.z);
    // 바둑판: 프레임마다 절반만 (두 프레임에 한 번씩 모두)
    if (((pidx.x + pidx.y + pidx.z + (int)gGIPass.w) & 1) != 0)
        discard;
    float s = gGICascade[c].w;
    float3 origin = gGICascade[c].xyz + ((float3)pidx + 0.5f) * s;
    uint rays = (uint)gGIPass.y;
    int count = (int)gGIParams.x;
    float4 shR = 0, shG = 0, shB = 0;
    float backfaces = 0.0f;
    uint wi, hi, mipsSky;
    gCubeMap.GetDimensions(0, wi, hi, mipsSky);
    [loop]
    for (uint i = 0; i < rays; ++i)
    {
        float3 dir = GIRayDir(i, rays, gGIPass.w);
        float3 L = 0.0f;
        bool hit = false;
        int cc = c;
        float t = gGIVoxAll[c].w * 0.75f;
        int steps = 0;
        [loop]
        while (cc < count && steps < 96)
        {
            float4 vol = gGIVoxAll[cc];
            if (vol.w <= 0.0f) { ++cc; continue; }
            float3 p = origin + dir * t;
            float3 v = (p - vol.xyz) / vol.w;
            if (any(v < 0.0f) || any(v >= kGIVox))
            {
                ++cc;   // 이 단계 밖 → 더 큰 단계에서 계속
                continue;
            }
            ++steps;
            // 4 복셀 덩어리가 비었으면 그 덩어리를 나가는 곳까지 (더 멀리 가면 얇은 벽을 뚫는다)
            if (gGIRadiance.SampleLevel(samGIPoint, GIAtlasUVW(cc, v), 2).a <= 0.0f)
            {
                float3 safeDir = max(abs(dir), 1e-5f) * (step(0.0f, dir) * 2.0f - 1.0f);
                float3 blockMin = floor(v * 0.25f) * 4.0f;
                float3 exitPlane = blockMin + step(0.0f, dir) * 4.0f;
                float3 te = (exitPlane - v) / safeDir;
                t += (min(te.x, min(te.y, te.z)) + 0.05f) * vol.w;
                continue;
            }
            int3 iv = int3(floor(v));
            float4 r = gGIRadiance.Load(int4(iv.x, iv.y, cc * (int)kGIVox.z + iv.z, 0));
            if (r.a > 0.5f)
            {
                float3 n = gGINormals.Load(int4(iv.x, iv.y, cc * (int)kGIVox.z + iv.z, 0)).xyz * 2.0f - 1.0f;
                if (dot(n, dir) > 0.1f)
                {
                    backfaces += 1.0f;   // 면의 뒤 (벽 속 프로브, 또는 얇은 벽의 바깥 면) — 빛 없음 (DDGI 와 같음)
                    L = 0.0f;
                }
                else
                    L = r.rgb;
                hit = true;
                break;
            }
            t += vol.w * 0.8f;   // 한 걸음 < 한 복셀 — 한 복셀 두께의 벽을 건너뛰지 않는다
        }
        if (!hit)
            L = ToLinear(gCubeMap.SampleLevel(samLinear, dir, max((float)mipsSky - 3.0f, 0.0f)).rgb);
        shR += float4(0.282095f, 0.488603f * dir) * L.r;
        shG += float4(0.282095f, 0.488603f * dir) * L.g;
        shB += float4(0.282095f, 0.488603f * dir) * L.b;
    }
    float norm = 4.0f * 3.14159265f / max((float)rays, 1.0f);
    float valid = backfaces / max((float)rays, 1.0f) > gGISky.y ? 0.0f : 1.0f;
    SHOut o;
    o.R = shR * norm;
    o.G = shG * norm;
    o.B = shB * norm;
    o.V = float4(valid, 0.0f, 0.0f, 1.0f);
    return o;
}

// ---------------------------------------------------------------- 4) Resample
SHOut PS_Resample(GIVOut pin)
{
    int c = (int)gGIPass.x;
    int3 pidx = int3(floor(pin.PosH.xy), (int)gGIPass.z);
    float3 posW = gGICascade[c].xyz + ((float3)pidx + 0.5f) * gGICascade[c].w;
    SHOut o;
    o.R = 0; o.G = 0; o.B = 0; o.V = float4(0, 0, 0, 1);
    int count = (int)gGIParams.x;
    [loop]
    for (int oc = c; oc < count; ++oc)
    {
        float4 old = gGIOld[oc];
        if (old.w <= 0.0f)
            continue;
        float3 local = (posW - old.xyz) / (kGIProbes * old.w);
        if (any(local < 0.0f) || any(local > 1.0f))
            continue;
        float z = clamp(local.z, 0.5f / kGIProbes.z, 1.0f - 0.5f / kGIProbes.z);
        float3 uvw = float3(local.x, local.y, (oc + z) / gGIBias.w);
        o.R = gGIOldSH0.SampleLevel(samGI, uvw, 0);
        o.G = gGIOldSH1.SampleLevel(samGI, uvw, 0);
        o.B = gGIOldSH2.SampleLevel(samGI, uvw, 0);
        o.V = float4(gGIOldValid.SampleLevel(samGI, uvw, 0).x, 0, 0, 1);
        break;
    }
    return o;
}

// ---------------------------------------------------------------- 5) Dilate
// 물체 속 프로브 (유효도 < 0.5) = 유효한 이웃 (6 방향) 의 평균 — 둘레가 모두 물체 속인 면 (상자 밑동 등) 이 다음 단계로 새지 않게
struct SH3Out
{
    float4 R : SV_Target0;
    float4 G : SV_Target1;
    float4 B : SV_Target2;
};

SH3Out PS_Dilate(GIVOut pin)
{
    int c = (int)gGIPass.x;
    int3 pidx = int3(floor(pin.PosH.xy), (int)gGIPass.z);
    int3 dims = (int3)kGIProbes;
    int4 me = int4(pidx.x, pidx.y, c * dims.z + pidx.z, 0);
    if (gGIOldValid.Load(me).x >= 0.5f)
        discard;
    float4 r = 0, g = 0, b = 0;
    float n = 0.0f;
    static const int3 kN[6] = { int3(1, 0, 0), int3(-1, 0, 0), int3(0, 1, 0), int3(0, -1, 0), int3(0, 0, 1), int3(0, 0, -1) };
    [unroll]
    for (int k = 0; k < 6; ++k)
    {
        int3 q = pidx + kN[k];
        if (any(q < 0) || any(q >= dims))
            continue;
        int4 t = int4(q.x, q.y, c * dims.z + q.z, 0);
        if (gGIOldValid.Load(t).x < 0.5f)
            continue;
        // 벽 · 지붕 너머 이웃은 쓰지 않는다 (두 프로브 사이 가운데 복셀이 차 있으면) — 방 모서리 프로브가 바깥 빛을 받지 않게
        float4 vox = gGIVoxAll[c];
        if (vox.w > 0.0f)
        {
            float3 mid = gGICascade[c].xyz + ((float3)pidx + 0.5f + (float3)kN[k] * 0.5f) * gGICascade[c].w;
            float3 vv = (mid - vox.xyz) / vox.w;
            if (all(vv >= 0.0f) && all(vv < kGIVoxels) &&
                gGIRadiance.Load(int4((int)vv.x, (int)vv.y, c * (int)kGIVoxels.z + (int)vv.z, 0)).a > 0.5f)
                continue;
        }
        r += gGIOldSH0.Load(t); g += gGIOldSH1.Load(t); b += gGIOldSH2.Load(t);
        n += 1.0f;
    }
    if (n <= 0.0f)
        discard;
    SH3Out o;
    o.R = r / n; o.G = g / n; o.B = b / n;
    return o;
}

technique11 DilateTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_GIFull()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS_Dilate()));
    }
}

technique11 InjectTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_GIFull()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS_Inject()));
    }
}

technique11 RelightTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_GIFull()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS_Relight()));
    }
}

technique11 UpdateTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_GIFull()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS_Update()));
    }
}

technique11 ResampleTech
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_GIFull()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS_Resample()));
    }
}
