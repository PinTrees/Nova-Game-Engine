#pragma once
#include "CinemachineCore.h"

// Cinemachine Brain (Unity CinemachineBrain): 실제 Camera 가 있는 GameObject (보통 Main Camera) 에 붙인다.
//  LateUpdate 마다 모든 가상 카메라를 갱신하고, Live 를 골라 (Priority → 같으면 가장 늦게 켜진 것) 그 자세 · 렌즈를 Camera 에 쓴다.
//  Live 가 바뀌면 Default Blend (또는 Custom Blends 의 From → To) 모양 · 시간으로 섞는다. 섞는 중에 또 바뀌면 지금 모습에서 다시 섞는다.
//  편집 중에도 Live 의 모습을 바로 보여 준다 (섞기 · 흔들림 없이).
class CinemachineBrain : public Component, public ICmProperties
{
public:
	struct CustomBlend
	{
		std::string From = "**ANY CAMERA**";
		std::string To = "**ANY CAMERA**";
		int Style = CmCore::EaseInOut;
		float Time = 2.0f;
	};
	int DefaultBlendStyle = CmCore::EaseInOut;
	float DefaultBlendTime = 2.0f;
	std::vector<CustomBlend> CustomBlends;

	CinemachineBrain();
	~CinemachineBrain() override;

	void LateUpdate() override;
	void _Editor_Update() override { LateUpdate(); }   // 편집 중에도 Live 의 모습을 (Unity 와 같이)
	void OnInspectorGUI() override;
	bool UsesUnityInspector() const override { return true; }
	const char* InspectorIconName() const override { return "camera"; }

	CinemachineCamera* ActiveVirtualCamera() const { return m_Live; }
	bool IsBlending() const { return m_Blend.Active; }
	bool IsLiveChild(const CinemachineCamera* vcam) const;   // Live 또는 섞는 중의 출발
	const CmState& Output() const { return m_Output; }
	void FindBlend(const std::string& from, const std::string& to, int& style, float& time) const;
	json Info() const;

	// ICmProperties
	int* IntProp(int i) override { return i == 0 ? &DefaultBlendStyle : nullptr; }
	float* FloatProp(int i) override { return i == 0 ? &DefaultBlendTime : nullptr; }
	void Validate() override;

	GENERATE_COMPONENT_BODY(CinemachineBrain)

private:
	struct Blend
	{
		bool Active = false;
		CinemachineCamera* From = nullptr;   // 섞는 중에 바뀌었으면 nullptr (FromState 는 그때의 출력)
		CmState FromState;
		std::string FromName;
		int Style = CmCore::EaseInOut;
		float Duration = 0.0f;
		float Elapsed = 0.0f;
	};
	CinemachineCamera* m_Live = nullptr;
	Blend m_Blend;
	CmState m_Output;
	bool m_HasOutput = false;
	int m_Switches = 0;   // Live 가 바뀐 횟수 (CLI 정보)

	bool IsPrimary() const;
	void Apply(const CmState& s, bool editing);
};

REGISTER_PACKAGE_COMPONENT(CinemachineBrain)
