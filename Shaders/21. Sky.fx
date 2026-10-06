
cbuffer cbPerFrame
{
	float4x4 gWorldViewProj;
	float4 gSkyWeather;   // 날씨 (WeatherState): rgb 색 × 밝기, w 회색으로 (먹구름)
	float gSkyFlash;      // 번개 (위쪽이 더 밝다)
	// 낮 · 밤 순환 (DayNightState) — gCycleSun.w = 0 이면 아무것도 하지 않는다
	float4 gCycleSun;      // xyz 해가 있는 쪽 (단위, 달 = 반대), w 켬
	float4 gCycleZenith;   // rgb 천정 색, a 그라데이션 섞기 (0 = 스카이박스 그대로)
	float4 gCycleHorizon;  // rgb 지평 색
	float4 gCycleGlow;     // rgb 해 쪽 지평 빛 (새벽 · 노을), a 세기
	float4 gCycleSunDisk;  // rgb 해 원반 색, a 세기
	float4 gCycleNight;    // x 별, y 은하수, z 시계 (초), w 달 원반
};

// Nonnumeric values cannot be added to a cbuffer.
TextureCube gCubeMap;

SamplerState samTriLinearSam
{
	Filter = MIN_MAG_MIP_LINEAR;
	AddressU = Wrap;
	AddressV = Wrap;
};

struct VertexIn
{
	float3 PosL : POSITION;
};

struct VertexOut
{
	float4 PosH : SV_POSITION;
	float3 PosL : POSITION;
};

VertexOut VS(VertexIn vin)
{
	VertexOut vout;

	// Set z = w so that z/w = 1 (i.e., skydome always on far plane).
	vout.PosH = mul(float4(vin.PosL, 1.0f), gWorldViewProj).xyww;

	// Use local vertex position as cubemap lookup vector.
	vout.PosL = vin.PosL;

	return vout;
}

// ---- 낮 · 밤: 별 · 은하수 (방향만으로 — 텍스처 없이)
float Hash13(float3 p)
{
	p = frac(p * 0.1031f);
	p += dot(p, p.zyx + 31.32f);
	return frac((p.x + p.y) * p.z);
}

float3 Hash33(float3 p)
{
	p = frac(p * float3(0.1031f, 0.1030f, 0.0973f));
	p += dot(p, p.yxz + 33.33f);
	return frac((p.xxy + p.yxx) * p.zyx);
}

float Noise3(float3 p)
{
	const float3 i = floor(p);
	float3 f = frac(p);
	f = f * f * (3.0f - 2.0f * f);
	const float a = lerp(Hash13(i), Hash13(i + float3(1, 0, 0)), f.x);
	const float b = lerp(Hash13(i + float3(0, 1, 0)), Hash13(i + float3(1, 1, 0)), f.x);
	const float c = lerp(Hash13(i + float3(0, 0, 1)), Hash13(i + float3(1, 0, 1)), f.x);
	const float d = lerp(Hash13(i + float3(0, 1, 1)), Hash13(i + float3(1, 1, 1)), f.x);
	return lerp(lerp(a, b, f.y), lerp(c, d, f.y), f.z);
}

float Fbm(float3 p)
{
	float s = 0.0f, a = 0.5f;
	[unroll] for (int k = 0; k < 4; ++k)
	{
		s += a * Noise3(p);
		p = p * 2.03f + 17.1f;
		a *= 0.5f;
	}
	return s;
}

// 별 한 겹: 방향 × scale 의 3D 칸마다 별 하나 (threshold 보다 운이 좋은 칸만). 반지름은 칸의 일부 (약 1 화소)
float3 StarLayer(float3 d, float scale, float threshold, float size, float gain, float time)
{
	const float3 p = d * scale;
	const float3 cell = floor(p);
	const float h = Hash13(cell + 7.7f);
	const float lucky = step(threshold, h);   // 별이 있는 칸 (분기 없이)
	const float3 r = Hash33(cell);
	const float dist = length(frac(p) - (0.25f + 0.5f * r));
	float b = saturate(1.0f - dist / (size * (0.75f + 0.5f * r.x)));
	b *= b;
	const float twinkle = 0.7f + 0.3f * sin(time * (1.5f + 4.0f * r.y) + r.z * 40.0f);
	const float3 tint = lerp(float3(0.72f, 0.82f, 1.0f), float3(1.0f, 0.86f, 0.7f), r.z);
	return tint * b * twinkle * gain * lucky * saturate((h - threshold) / (1.0f - threshold));
}

// 은하수: 기울어진 큰 원을 따라 구름 같은 빛 띠 + 가운데 어두운 먼지 + 밝은 중심. band = 띠 안의 정도 (별을 더 많이)
float3 MilkyWay(float3 d, out float band)
{
	const float3 n = normalize(float3(0.35f, 0.42f, 0.84f));    // 은하면의 법선
	const float3 core = normalize(float3(0.0f, n.z, -n.y));     // 띠 위의 중심 (하늘 높이)
	const float lat = dot(d, n);
	band = exp(-lat * lat / (2.0f * 0.13f * 0.13f));
	const float3 q = d * 5.0f;
	const float clouds = Fbm(q);
	const float fine = Fbm(q * 3.0f + 11.0f);
	const float dust = smoothstep(0.42f, 0.68f, Fbm(q * 2.2f + 5.0f)) * exp(-lat * lat / (2.0f * 0.045f * 0.045f));
	const float coreGlow = pow(saturate(dot(d, core)), 6.0f);
	const float b = band * (0.3f + 0.7f * clouds) * (0.6f + 0.4f * fine) * (1.0f - 0.75f * dust) * (0.55f + 1.6f * coreGlow);
	const float3 col = lerp(float3(0.55f, 0.62f, 0.88f), float3(1.0f, 0.86f, 0.66f), saturate(coreGlow * 1.5f + clouds * 0.3f));
	return col * b * 0.55f;
}

float3 DayNightSky(float3 c, float3 d)
{
	const float3 s = gCycleSun.xyz;
	// 1) 그라데이션: 지평 → 천정, 지평 아래는 어두워진다
	const float up = saturate(d.y);
	float3 grad = lerp(gCycleHorizon.rgb, gCycleZenith.rgb, pow(up, 0.5f));
	grad *= 0.15f + 0.85f * saturate(1.0f + d.y * 2.5f);
	c = lerp(c, grad, saturate(gCycleZenith.a));
	// 2) 해 쪽 지평 빛 (새벽 · 노을): 해의 방위에 가깝고 지평에 가까울수록
	const float2 dh = normalize(d.xz + 1e-5f), sh = normalize(s.xz + 1e-5f);
	const float toward = saturate(dot(dh, sh) * 0.5f + 0.5f);
	const float near = exp(-abs(d.y) * 5.0f) * saturate(1.0f + d.y * 4.0f);
	c += gCycleGlow.rgb * gCycleGlow.a * near * pow(toward, 4.0f);
	// 3) 별 · 은하수 (지평 위 — 먹구름이면 가린다)
	const float above = saturate(d.y * 8.0f) * saturate(1.0f - gSkyWeather.w * 1.15f);
	if (above > 0.0f && gCycleNight.x + gCycleNight.y > 0.001f)
	{
		float band;
		const float3 mw = MilkyWay(d, band);
		c += mw * gCycleNight.y * above;
		const float dense = 1.0f + band * 1.5f * saturate(gCycleNight.y);
		const float3 stars = StarLayer(d, 260.0f, 0.86f, 0.3f, 1.6f, gCycleNight.z) + StarLayer(d, 110.0f, 0.95f, 0.22f, 2.4f, gCycleNight.z * 0.7f);
		c += stars * gCycleNight.x * dense * above;
	}
	// 4) 해 · 달 원반 (지평 아래는 가린다)
	const float horizonCut = saturate(d.y * 20.0f + 1.0f);
	const float sd = dot(d, s);
	c += gCycleSunDisk.rgb * gCycleSunDisk.a * horizonCut *
		(smoothstep(0.99955f, 0.99975f, sd) * 6.0f + pow(saturate(sd), 400.0f) * 1.2f + pow(saturate(sd), 12.0f) * 0.12f);
	const float md = dot(d, -s);
	c += float3(0.85f, 0.9f, 1.0f) * gCycleNight.w * horizonCut * saturate(1.0f - gSkyWeather.w) *
		(smoothstep(0.99965f, 0.9998f, md) * 1.6f + pow(saturate(md), 300.0f) * 0.15f);
	return c;
}

float4 PS(VertexOut pin) : SV_Target
{
	float4 c = gCubeMap.Sample(samTriLinearSam, pin.PosL);
	const float gray = dot(c.rgb, float3(0.299f, 0.587f, 0.114f));
	c.rgb = lerp(c.rgb, gray.xxx, gSkyWeather.w) * gSkyWeather.rgb;
	const float up = saturate(normalize(pin.PosL).y * 0.7f + 0.5f);
	c.rgb += gSkyFlash * up * float3(0.75f, 0.8f, 1.0f);
	if (gCycleSun.w > 0.5f)
		c.rgb = DayNightSky(c.rgb, normalize(pin.PosL));
	return c;
}

RasterizerState NoCull
{
	CullMode = None;
};

DepthStencilState LessEqualDSS
{
	// Make sure the depth function is LESS_EQUAL and not just LESS.  
	// Otherwise, the normalized depth values at z = 1 (NDC) will 
	// fail the depth test if the depth buffer was cleared to 1.
	DepthFunc = LESS_EQUAL;
};

technique11 SkyTech
{
	pass P0
	{
		SetVertexShader(CompileShader(vs_5_0, VS()));
		SetGeometryShader(NULL);
		SetPixelShader(CompileShader(ps_5_0, PS()));

		SetRasterizerState(NoCull);
		SetDepthStencilState(LessEqualDSS, 0);
	}
}
