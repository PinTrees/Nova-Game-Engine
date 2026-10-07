#pragma once
#include "CinemachineCore.h"

// 가상 카메라 (Unity Cinemachine 3 의 CinemachineCamera): 실제로 그리지 않고, Brain 이 고르면 그 자세 · 렌즈가 실제 Camera 에 쓰인다.
//  Priority 가 가장 높은 (같으면 가장 늦게 켜진) 켜진 가상 카메라가 Live. 끄거나 Priority 를 바꾸면 Brain 이 섞어서 옮긴다.
//  위치 · 회전은 같은 GameObject 의 단계 컴포넌트 (Position Control · Rotation Control · Noise) 가 정한다 — 없으면 Transform 그대로.
class CinemachineCamera : public Component, public ICmProperties
{
public:
	int Priority = 0;
	uint64 TrackingTarget = 0;      // Follow (fileID)
	uint64 LookAtTarget = 0;        // CustomLookAtTarget 일 때만
	bool CustomLookAtTarget = false;
	CmLens Lens;

	CinemachineCamera();
	~CinemachineCamera() override;

	void OnInspectorGUI() override;
	void OnDrawGizmos() override;
	void RemapFileIDs(const std::unordered_map<uint64, uint64>& map) override;
	bool UsesUnityInspector() const override { return true; }
	const char* InspectorIconName() const override { return "camera"; }

	GameObject* Follow() const;
	GameObject* LookAt() const;           // Custom 이 아니면 Tracking Target
	bool IsEligible() const;              // 켜짐 (컴포넌트 · GameObject)
	bool IsLive() const;                  // 어느 Brain 의 Live (섞는 중의 대상 포함)
	void Prioritize();                    // 같은 Priority 중 맨 앞으로 (Unity Prioritize)
	void SnapNext() { m_PreviousValid = false; }   // 다음 갱신은 따라가기 없이 (PreviousStateIsValid = false)

	// Brain 이 프레임마다 (dt < 0 = 따라가기 없이)
	void UpdateCameraState(float dt);
	const CmState& State() const { return m_State; }
	CinemachineComponentBase* Pipeline(CmStage stage) const;
	json Info() const;

	// Brain 이 쓰는 켜진 순서
	uint32 ActivationStamp = 0;
	bool WasEligible = false;

	// ICmProperties (번호: Runtime/Cinemachine.cs)
	float* FloatProp(int i) override;
	int* IntProp(int i) override { return i == 0 ? &Priority : nullptr; }
	bool* BoolProp(int i) override { return i == 0 ? &CustomLookAtTarget : nullptr; }
	uint64* ObjectProp(int i) override;
	void Validate() override;

	GENERATE_COMPONENT_BODY(CinemachineCamera)

private:
	CmState m_State;
	bool m_PreviousValid = false;
	bool m_HasState = false;
	void PipelineInspector();
};

REGISTER_PACKAGE_COMPONENT(CinemachineCamera)
