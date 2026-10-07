//=============================================================================
// 28. Ssao.fx — 화면 공간 앰비언트 오클루전 (Volume 의 Screen Space Ambient Occlusion)
//  1) Ssao     : AO (계산 크기 = 반 또는 전체 해상도) — 노멀 쪽 반구의 표본이 깊이에 막히는 만큼
//  2) Temporal : 지난 프레임 결과를 이번 자리로 되돌려 섞는다 (깊이가 맞을 때만, 이웃 범위로 자름) — 표본이 프레임마다 돈다
//  3) (28. SsaoBlur) 가장자리를 지키는 흐림
//  4) Upsample : 전체 해상도로 — 깊이 · 노멀이 비슷한 AO 텍셀만 섞는다 (윤곽 후광 없음)
//  노멀 · 깊이 (28. SsaoNormalDepth: xyz 뷰 노멀, w 뷰 깊이) 는 늘 점으로 읽는다 — 선형으로 읽으면 윤곽을 사이에 둔
//  깊이 · 노멀이 섞여 엉뚱한 위치가 되고, 그 자리가 가려진 것처럼 얇은 선이 남았다
//=============================================================================
cbuffer cbPerFrame
{
    float4x4 gViewToTexSpace;      // Proj × 텍스처 (NDC → [0,1])
    float4 gOffsetVectors[14];
    float4 gFrustumCorners[4];     // 화면 구석의 뷰 방향 (z = 1)

    // 뷰 공간 (m)
    float gOcclusionRadius = 0.5f;
    float gOcclusionFadeStart = 0.2f;
    float gOcclusionFadeEnd = 2.0f;
    float gSurfaceEpsilon = 0.05f;
    float4 gSsaoParams = float4(1.0f, 100.0f, 0.0f, 0.0f);   // x 세기, y Falloff Distance (m — 끝 20 % 에서 사라진다), zw 무작위 무늬 이동 (프레임마다)
    float4 gAoSize = float4(1.0f, 1.0f, 1.0f, 1.0f);          // xy 계산 크기 (텍셀), z 전체 해상도 픽셀 / 계산 텍셀 (1 · 2)

    // Temporal
    float4x4 gCurViewToPrevClip;   // 이번 뷰 공간 → 지난 프레임 클립 (그 프레임의 지터 투영)
    float4 gTemporal = float4(0.9f, 0.0f, 0.0f, 0.0f);       // x 히스토리 비중, y 히스토리 있음 (0 · 1)
    float4 gMotionInfo = float4(0.0f, 0.0f, 0.0f, 0.0f);     // x 1 = 모션 벡터 있음, yz = 이번 − 지난 프레임 지터 (uv)
};

Texture2D gNormalDepthMap;
Texture2D gRandomVecMap;
Texture2D gAoRaw;          // Temporal: 이번 AO / Upsample: 흐린 AO (계산 크기)
Texture2D gAoHistory;      // Temporal: 지난 결과 (r = AO, g = 뷰 깊이)
Texture2D gMotionVectors;  // Temporal: 모션 벡터 (63. MotionVectors — 지터 뺀 uv 의 이번 − 지난, 전체 해상도)

SamplerState samNormalDepth
{
    Filter = MIN_MAG_MIP_POINT;
    // 화면 밖 = 아주 먼 깊이 (가리지 않는다)
    AddressU = BORDER;
    AddressV = BORDER;
    BorderColor = float4(0.0f, 0.0f, 0.0f, 1e5f);
};

SamplerState samRandomVec
{
    Filter = MIN_MAG_MIP_POINT;   // 텍셀마다 다른 방향 (선형이면 이웃과 섞여 짧아진다)
    AddressU = WRAP;
    AddressV = WRAP;
};

SamplerState samPointClamp
{
    Filter = MIN_MAG_MIP_POINT;
    AddressU = CLAMP;
    AddressV = CLAMP;
};

struct VertexIn
{
    float3 PosL : POSITION;
    float3 ToFarPlaneIndex : NORMAL;
    float2 Tex : TEXCOORD;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float3 ToFarPlane : TEXCOORD0;
    float2 Tex : TEXCOORD1;
};

VertexOut VS(VertexIn vin)
{
    VertexOut vout;
    vout.PosH = float4(vin.PosL, 1.0f);   // 이미 NDC
    vout.ToFarPlane = gFrustumCorners[vin.ToFarPlaneIndex.x].xyz;   // 구석 번호는 노멀 x 에
    vout.Tex = vin.Tex;
    return vout;
}

// 계산 텍셀이 대표하는 전체 해상도 픽셀 (반 해상도 = 2 x 2 의 왼쪽 위)
float4 NormalDepthAt(int2 aoPixel)
{
    return gNormalDepthMap.Load(int3(aoPixel * (int)gAoSize.z, 0));
}

// 그 대표 픽셀 가운데의 뷰 방향 (z = 1) — 깊이를 읽은 픽셀과 같은 자리에서 위치를 되짚는다
//  (텍셀 가운데에서 보간한 방향을 쓰면 반 해상도에서 반 픽셀 어긋나, 비스듬한 먼 바닥이 옅게 스스로 가려졌다)
float3 ViewDirAt(int2 aoPixel)
{
    uint w, h;
    gNormalDepthMap.GetDimensions(w, h);
    float2 uv = (float2(aoPixel * (int)gAoSize.z) + 0.5f) / float2(w, h);
    // 구석: 0 왼쪽 아래 · 1 왼쪽 위 · 2 오른쪽 위 (uv 는 위가 0)
    return float3(lerp(gFrustumCorners[1].x, gFrustumCorners[2].x, uv.x), lerp(gFrustumCorners[1].y, gFrustumCorners[0].y, uv.y), 1.0f);
}

// distZ (p 와 막는 점 r 의 깊이 차) 에 따른 가림: Epsilon 안쪽은 같은 면, Fade Start ~ End 에서 줄어든다
float OcclusionFunction(float distZ)
{
    float occlusion = 0.0f;
    if (distZ > gSurfaceEpsilon)
    {
        float fadeLength = gOcclusionFadeEnd - gOcclusionFadeStart;
        occlusion = saturate((gOcclusionFadeEnd - distZ) / fadeLength);
    }
    return occlusion;
}

float4 PS(VertexOut pin, uniform int gSampleCount) : SV_Target
{
    // 이 텍셀의 뷰 노멀 · 깊이 → 뷰 위치 p = (깊이 / 방향 z) × 방향
    float4 normalDepth = NormalDepthAt(int2(pin.PosH.xy));
    float3 n = normalDepth.xyz;
    float pz = normalDepth.w;
    if (pz > 1e4f)
        return 1.0f;   // 하늘 · 빈 곳
    float3 p = pz * ViewDirAt(int2(pin.PosH.xy));

    // 픽셀마다 무작위 방향 (시간 누적이면 프레임마다 무늬를 옮긴다)
    float3 randVec = normalize(2.0f * gRandomVecMap.SampleLevel(samRandomVec, pin.PosH.xy / 256.0f + gSsaoParams.zw, 0.0f).rgb - 1.0f + 1e-4f);

    float occlusionSum = 0.0f;
    [unroll]
    for (int i = 0; i < gSampleCount; ++i)
    {
        // 고르게 퍼진 방향을 무작위 방향으로 반사 → 노멀 쪽 반구로
        float3 offset = reflect(gOffsetVectors[i].xyz, randVec);
        float flip = sign(dot(offset, n));
        float3 q = p + flip * gOcclusionRadius * offset;

        // q 를 화면에 → 그 자리의 가장 가까운 깊이 rz, 시선 위의 점 r
        float4 projQ = mul(float4(q, 1.0f), gViewToTexSpace);
        projQ /= projQ.w;
        float rz = gNormalDepthMap.SampleLevel(samNormalDepth, projQ.xy, 0.0f).a;
        float3 r = (rz / q.z) * q;

        // r 이 p 의 면 앞에 있을수록, 가까울수록 가린다
        float distZ = p.z - r.z;
        float dp = max(dot(n, normalize(r - p)), 0.0f);
        occlusionSum += dp * OcclusionFunction(distZ);
    }
    occlusionSum /= gSampleCount;

    // 세기 × 가림, 카메라에서 Falloff Distance 끝 20 % 에서 사라진다
    float fade = saturate((gSsaoParams.y - pz) / max(gSsaoParams.y * 0.2f, 1e-3f));
    return saturate(1.0f - occlusionSum * gSsaoParams.x * fade);
}

// 시간 누적: r = 섞인 AO, g = 이 텍셀의 뷰 깊이 (다음 프레임의 같은 면 판정)
float4 PS_Temporal(VertexOut pin) : SV_Target
{
    int2 pix = int2(pin.PosH.xy);
    float pz = NormalDepthAt(pix).w;
    float raw = gAoRaw.Load(int3(pix, 0)).r;
    if (pz > 1e4f)
        return float4(1.0f, pz, 0.0f, 0.0f);

    // 이웃 3 x 3 의 범위 — 움직인 물체가 남긴 옛 AO (같은 깊이의 바닥) 를 자른다
    float lo = raw, hi = raw;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float v = gAoRaw.Load(int3(clamp(pix + int2(x, y), int2(0, 0), int2(gAoSize.xy) - 1), 0)).r;
            lo = min(lo, v);
            hi = max(hi, v);
        }
    }

    float result = raw;
    if (gTemporal.y > 0.5f)
    {
        float3 p = pz * ViewDirAt(pix);
        float4 prev = mul(float4(p, 1.0f), gCurViewToPrevClip);   // w = 지난 뷰의 깊이 (원근 투영 — 멈춰 있던 점이면)
        float2 uv = float2(prev.x / prev.w * 0.5f + 0.5f, -prev.y / prev.w * 0.5f + 0.5f);
        float expectZ = prev.w, tolerance = 0.03f * prev.w + 0.02f;
        if (gMotionInfo.x > 0.5f)
        {
            // 모션 벡터: 대표 픽셀의 속도로 지난 래스터 자리 (= 이번 래스터 uv − 속도 − 지터 차)
            uint w, h;
            gNormalDepthMap.GetDimensions(w, h);
            int2 rep = pix * (int)gAoSize.z;
            float2 uvRep = (float2(rep) + 0.5f) / float2(w, h);
            float2 uvObj = uvRep - gMotionVectors.Load(int3(rep, 0)).xy - gMotionInfo.yz;
            // 카메라만 되돌린 자리와 한 픽셀 넘게 다르면 움직이는 물체 — 지난 깊이를 모르니 이번 깊이로 느슨히
            if (any(abs(uvObj - uv) * float2(w, h) > 1.0f))
            {
                expectZ = pz;
                tolerance = 0.1f * pz + 0.05f;
            }
            uv = uvObj;
        }
        if (prev.w > 1e-3f && all(uv >= 0.0f) && all(uv <= 1.0f))
        {
            float2 h = gAoHistory.SampleLevel(samPointClamp, uv, 0.0f).rg;
            // 같은 면이었나 (가려졌다 드러난 곳 · 다른 물체면 버린다)
            if (abs(h.g - expectZ) < tolerance)
                result = lerp(raw, clamp(h.r, lo, hi), gTemporal.x);
        }
    }
    return float4(result, pz, 0.0f, 0.0f);
}

// 전체 해상도로: 둘레 AO 텍셀 4 개 (쌍선형 자리) 중 이 픽셀과 깊이 · 노멀이 비슷한 것만 — 앞 물체가 뒤의 AO 를 받지 않는다
float4 PS_Upsample(VertexOut pin) : SV_Target
{
    float4 nd = gNormalDepthMap.Load(int3(int2(pin.PosH.xy), 0));
    if (nd.w > 1e4f)
        return 1.0f;
    float2 f = pin.PosH.xy / gAoSize.z - 0.5f;   // 계산 텍셀 좌표 (가운데 = 정수)
    float2 base = floor(f);
    float2 t = f - base;
    float sum = 0.0f, wsum = 0.0f, best = 1.0f, bestDz = 1e9f;
    [unroll]
    for (int j = 0; j <= 1; ++j)
    {
        [unroll]
        for (int i = 0; i <= 1; ++i)
        {
            int2 lp = clamp(int2(base) + int2(i, j), int2(0, 0), int2(gAoSize.xy) - 1);
            float4 lnd = NormalDepthAt(lp);
            float ao = gAoRaw.Load(int3(lp, 0)).r;
            float bw = (i ? t.x : 1.0f - t.x) * (j ? t.y : 1.0f - t.y);
            float dz = abs(lnd.w - nd.w) / max(nd.w, 1e-3f);
            float w = bw * saturate(1.0f - dz * 20.0f) * saturate(dot(lnd.xyz, nd.xyz));
            sum += ao * w;
            wsum += w;
            if (dz < bestDz) { bestDz = dz; best = ao; }
        }
    }
    return wsum > 1e-4f ? sum / wsum : best;   // 모두 다른 면이면 가장 가까운 깊이의 것
}

// 표본 수 = Volume 의 Samples (Low 4 · Medium 8 · High 14)
technique11 Ssao
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS(8)));
    }
}

technique11 SsaoLow
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS(4)));
    }
}

technique11 SsaoHigh
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS(14)));
    }
}

technique11 Temporal
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS_Temporal()));
    }
}

technique11 Upsample
{
    pass P0
    {
        SetVertexShader(CompileShader(vs_5_0, VS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_5_0, PS_Upsample()));
    }
}
