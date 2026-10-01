#pragma once
#include "RockGenerator.h"

// 바위 한 종류의 모든 설정 (모양 + 재질 + LOD). Rock 컴포넌트 하나 = RockDesc 하나.
//  같은 설정(Hash)의 바위는 RockRenderer 가 메시를 같이 쓰고 인스턴싱으로 한 번에 그린다.
//  색은 감마 공간 0~1
struct RockDesc
{
	enum Preset { LimestoneCliff, LimestoneBlock, GraniteBoulder, SandstoneLedge, BasaltSpire, RiverPebble, PresetCount };

	RockParams Params;
	int PresetIndex = GraniteBoulder;

	// ---- 재질
	XMFLOAT4 BaseColor = { 0.55f, 0.53f, 0.50f, 1.0f };
	XMFLOAT4 StrataColor = { 0.48f, 0.45f, 0.41f, 1.0f };   // 지층 띠 색 (바탕과 번갈아)
	float StrataContrast = 0.5f;
	XMFLOAT4 MossColor = { 0.33f, 0.40f, 0.18f, 1.0f };
	float Moss = 0.35f;              // 위를 향한 면을 덮는 정도
	float DetailStrength = 1.0f;     // 디테일 노멀 세기
	float DetailScale = 2.5f;        // m (디테일 텍스처 한 장)
	float Smoothness = 0.18f;
	float ColorVariation = 0.15f;    // 바위마다 밝기·색 차이

	// ---- LOD: 화면 크기(바위 크기 / 거리)로. 클수록 먼 곳까지 고운 단계
	float LodBias = 1.0f;
	bool CastShadows = true;

	static const char* PresetName(int preset);
	void ApplyPreset(int preset);   // 모양 + 재질 (seed 는 그대로)
	nlohmann::json ToJson() const;
	void FromJson(const nlohmann::json& j);
	bool DrawInspector(const char* statsText = nullptr);   // 바뀌면 true

	const std::string& MeshKey() const;   // 모양만
	size_t Hash() const;                  // 모양 + 보이는 값
	void Invalidate() { m_KeyDirty = true; }

private:
	mutable std::string m_MeshKey;
	mutable size_t m_Hash = 0;
	mutable bool m_KeyDirty = true;
};
