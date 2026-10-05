
cbuffer cbPerFrame
{
	float4x4 gWorldViewProj;
	float4 gSkyWeather;   // 날씨 (WeatherState): rgb 색 × 밝기, w 회색으로 (먹구름)
	float gSkyFlash;      // 번개 (위쪽이 더 밝다)
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

float4 PS(VertexOut pin) : SV_Target
{
	float4 c = gCubeMap.Sample(samTriLinearSam, pin.PosL);
	const float gray = dot(c.rgb, float3(0.299f, 0.587f, 0.114f));
	c.rgb = lerp(c.rgb, gray.xxx, gSkyWeather.w) * gSkyWeather.rgb;
	const float up = saturate(normalize(pin.PosL).y * 0.7f + 0.5f);
	c.rgb += gSkyFlash * up * float3(0.75f, 0.8f, 1.0f);
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
