#pragma once
#include "Component.h"
#include "RockDesc.h"

// 바위 흩뿌리기 (GameObject 하나 = 바위 수백~수천 개, 모두 인스턴싱).
//  - Transform 위치 = 영역 가운데, 크기 x·z = 영역 넓이(m). 영역 안에 Count 개를 씨앗으로 무작위로 놓는다
//  - 모양 변형 Variants 개(템플릿 Desc 의 씨앗만 다름)를 회전·크기만 바꿔 다시 쓴다 → 메시는 변형 수 × LOD 만큼만
//  - 지형 위면 그 높이에 Sink 만큼 묻고, Align To Ground 면 경사를 따라 기울인다. 경사 범위로 거르면 비탈·절벽에만 놓을 수 있다
//  - 지형·값·위치가 바뀌면 다시 놓는다 (CPU 에서 행렬만 계산)
class RockScatter : public Component
{
public:
	RockDesc Desc;                 // 템플릿 (재질·모양, 씨앗은 변형마다 다름)
	int Count = 200;
	int Variants = 4;
	int Seed = 1;
	float ScaleMin = 0.5f, ScaleMax = 1.4f;
	float Sink = 0.12f;            // 높이 비율만큼 땅에 묻는다
	bool AlignToGround = true;
	float MinSlope = 0.0f, MaxSlope = 90.0f;   // 도
	float Clustering = 0.6f;       // 0 = 고르게, 1 = 무리 지어 (저주파 노이즈 밀도)
	float ClusterSize = 40.0f;     // m (무리 크기)

	struct Instance { int Variant; XMFLOAT4X4 World; };

	RockScatter();
	virtual ~RockScatter();

	static const std::vector<RockScatter*>& All();
	bool IsDrawable() const;

	// 변형 설정 + 인스턴스 (바뀌었으면 다시 만든다)
	const std::vector<RockDesc>& VariantDescs();
	const std::vector<Instance>& Instances();

	virtual void OnInspectorGUI() override;
	virtual void OnDrawGizmos() override;
	virtual bool UsesUnityInspector() const override { return true; }
	virtual const char* InspectorIconName() const override { return "terrain_paint"; }

	GENERATE_COMPONENT_BODY(RockScatter)

private:
	std::vector<RockDesc> m_Variants;
	std::vector<Instance> m_Instances;
	uint64_t m_Key = ~0ull;
	size_t m_VariantKey = 0;
};

REGISTER_COMPONENT(RockScatter)
