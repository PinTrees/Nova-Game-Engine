#pragma once
#include "TreeGenerator.h"

// 나무 한 종류의 모든 설정 (모양 + 수피·잎 색 + 바람 + LOD).
//  Tree 컴포넌트 하나 = TreeDesc 하나, 지형의 나무 프로토타입 하나 = TreeDesc 하나.
//  같은 설정(Hash)의 나무는 TreeRenderer 가 메시·임포스터를 같이 쓰고 인스턴싱으로 한 번에 그린다.
struct TreeDesc
{
	// ---- 모양 (바꾸면 메시를 다시 만든다)
	TreeParams Params;
	int Preset = 0;

	// ---- 수피 (감마 색)
	XMFLOAT4 BarkColor = { 0.36f, 0.29f, 0.23f, 1.0f };
	XMFLOAT4 MossColor = { 0.33f, 0.42f, 0.16f, 1.0f };
	float Moss = 0.35f;
	float RidgeDepth = 0.6f;
	float RidgeFrequency = 16.0f;
	float BarkSmoothness = 0.15f;
	float BarkFleck = 0.2f;

	// ---- 잎 (감마 색)
	XMFLOAT4 LeafColor = { 0.24f, 0.42f, 0.11f, 1.0f };
	XMFLOAT4 LeafColor2 = { 0.45f, 0.53f, 0.13f, 1.0f };
	float LeafVariation = 0.6f;
	float LeafTransmission = 0.6f;
	float LeafSmoothness = 0.35f;
	float LeafLength = 0.34f;   // 카드 안 잎 길이 (카드 비율)

	// ---- 바람
	float WindStrength = 0.5f;
	float WindDirection = 30.0f;   // 도 (Y 축 회전, 0 = +Z)
	float TrunkSway = 0.25f;       // m (높이 9 m 나무 기준)
	float BranchSway = 0.2f;
	float LeafFlutter = 0.03f;

	// ---- LOD (카메라 거리, m. 나무 크기 배율로 나눈 거리로 비교)
	float LodDistance = 35.0f;         // 전체 메시 → 중간 메시
	float BillboardDistance = 90.0f;   // 중간 메시 → 임포스터
	float CullDistance = 800.0f;       // 이 거리부터 그리지 않음

	bool CastShadows = true;

	void ApplyPreset(int preset);   // 모양 + 색 (seed 는 그대로)
	nlohmann::json ToJson() const;
	void FromJson(const nlohmann::json& j);
	// Inspector (Preset / Seed / Trunk / Branches / Leaves / Bark / Wind / LOD). 바뀌면 true
	bool DrawInspector(const char* statsText = nullptr);

	// 메시 키 = 모양만, Hash = 모양 + 보이는 값 전체 (같으면 한 번에 그린다)
	const std::string& MeshKey() const;
	size_t Hash() const;
	void Invalidate() { m_KeyDirty = true; }

private:
	mutable std::string m_MeshKey;
	mutable size_t m_Hash = 0;
	mutable bool m_KeyDirty = true;
};
