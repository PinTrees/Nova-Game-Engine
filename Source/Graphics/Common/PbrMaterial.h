#pragma once

// Unity URP Lit 재질 값 (셰이더 32. InstancedBasic.fx 의 PbrMaterial 과 같은 배치, 128 바이트)
//  색은 감마 공간(Inspector 에 보이는 값), EmissionColor 는 선형 HDR (색 × Intensity).
struct PbrMaterial
{
	XMFLOAT4 BaseColor = XMFLOAT4(1, 1, 1, 1);
	XMFLOAT4 EmissionColor = XMFLOAT4(0, 0, 0, 1);

	float Metallic = 0.0f;
	float Smoothness = 0.5f;
	float NormalScale = 1.0f;
	float OcclusionStrength = 1.0f;

	XMFLOAT2 Tiling = XMFLOAT2(1, 1);
	XMFLOAT2 Offset = XMFLOAT2(0, 0);

	float Cutoff = 0.5f;
	int UseBaseMap = 0;
	int UseMetallicMap = 0;
	int UseNormalMap = 0;

	int UseOcclusionMap = 0;
	int UseEmissionMap = 0;
	int SmoothnessFromAlbedo = 0;   // Smoothness Source: 0 Metallic Alpha, 1 Albedo Alpha
	int AlphaClip = 0;

	int SpecularHighlights = 1;
	int EnvironmentReflections = 1;
	int ReceiveShadows = 1;
	int Unlit = 0;                  // Universal Render Pipeline/Unlit

	// UV: 0 = 메시 UV, 1 = 월드 좌표 (면 방향으로 투영, 1 m = Tiling 1 — 늘린 블록아웃 상자도 격자가 1 m. UE 의 World Aligned Texture)
	int UVMode = 0;
	int PadUV0 = 0, PadUV1 = 0, PadUV2 = 0;
};
static_assert(sizeof(PbrMaterial) == 128, "PbrMaterial must match the HLSL cbuffer layout");
