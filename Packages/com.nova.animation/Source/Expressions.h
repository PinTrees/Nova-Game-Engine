#pragma once
#include "Component.h"

class SkinnedMeshRenderer;

// Expressions (VRM 의 Expression · UniVRM 의 Vrm10Instance.Runtime.Expression): 이름 있는 표정 = 자식 SkinnedMeshRenderer 의
// BlendShape 묶음. 표정 값 0..1 × 묶음 가중치 → 그 BlendShape (0..100, 여러 표정이 같은 셰이프면 더한다).
//  - 이름 = VRM 1.0 프리셋 (happy angry sad relaxed surprised aa ih ou ee oh blink blinkLeft blinkRight neutral …), VRM 0.x 는 바꿔 읽는다
//  - Auto Blink: Play 중 몇 초마다 blink 를 0 → 1 → 0. overrideBlink = block 인 표정 (웃음 등) 이 켜지면 눈 깜빡임을 끈다 (VRM 규칙)
//  - VRM 을 캐릭터로 만들면 파일의 표정으로 자동으로 붙는다 (VrmImport::ExpressionsJson)
class Expressions : public Component
{
public:
	struct Bind
	{
		std::string Renderer;   // 자식 오브젝트 이름 (SkinnedMeshRenderer)
		std::string Shape;      // BlendShape 이름 (비어 있거나 못 찾으면 Index)
		int Index = -1;
		float Weight = 100.0f;  // 표정 1 일 때 이 셰이프 값 (0..100)
	};
	struct Expression
	{
		std::string Name;
		std::vector<Bind> Binds;
		float Value = 0.0f;     // 0..1
		bool Binary = false;    // 0.5 넘으면 1
		std::string OverrideBlink = "none", OverrideMouth = "none";   // none | block | blend
	};

	std::vector<Expression> List;
	bool AutoBlink = true;
	float BlinkInterval = 4.0f;    // 평균 (초), 실제로는 ±50 % 무작위
	float BlinkDuration = 0.15f;

	Expressions();
	int Find(const std::string& name) const;
	bool SetWeight(const std::string& name, float value);
	float GetWeight(const std::string& name) const;
	float AutoBlinkValue() const { return m_Blink; }

	void Update() override;
	void OnInspectorGUI() override;
	bool UsesUnityInspector() const override { return true; }

	GENERATE_COMPONENT_BODY(Expressions)

private:
	float m_Blink = 0.0f;           // 자동 깜빡임 값 (0..1)
	float m_BlinkTimer = 0.0f, m_NextBlink = -1.0f, m_BlinkTime = -1.0f;
	uint32 m_Seed = 12345;
	std::vector<std::pair<std::string, int>> m_Touched;   // 지난번에 쓴 (렌더러 이름, 셰이프) — 안 쓰게 되면 0 으로
	void Apply();
	SkinnedMeshRenderer* FindRenderer(const std::string& name) const;
};

REGISTER_PACKAGE_COMPONENT(Expressions)
