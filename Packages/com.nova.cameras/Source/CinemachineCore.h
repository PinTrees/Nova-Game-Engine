#pragma once
#include "Component.h"

// Cinemachine (Unity Cinemachine 3 와 같은 이름 · 구조): 가상 카메라 여럿 → Brain 이 하나를 골라 (Priority) 실제 Camera 에 쓴다.
//  가상 카메라 (CinemachineCamera) 의 파이프라인: Body (위치) → Aim (회전) → Noise (흔들림) → 확장 (충격)
//  같은 GameObject 의 단계 컴포넌트 (CinemachineFollow · RotationComposer · BasicMultiChannelPerlin …) 가 상태를 바꾼다.
class CinemachineCamera;
class CinemachineBrain;

// 렌즈 (Unity LensSettings): 시야각 (도) · 직교 크기 · 가까운 · 먼 면 · 기울임 (도, 앞 방향 축)
struct CmLens
{
	float FieldOfView = 60.0f;
	float OrthographicSize = 5.0f;
	float NearClipPlane = 0.3f;
	float FarClipPlane = 1000.0f;
	float Dutch = 0.0f;
};

// 카메라 상태 (Unity CameraState): 원래 자세 (Raw — 가상 카메라 Transform 에 남는 것) + 보정 (흔들림 · 충격 — 출력에만)
struct CmState
{
	Vec3 RawPosition;
	Quaternion RawOrientation = Quaternion::Identity;
	Vec3 PositionCorrection;
	Quaternion OrientationCorrection = Quaternion::Identity;
	CmLens Lens;
	bool HasLookAt = false;
	Vec3 ReferenceLookAt;

	Vec3 FinalPosition() const { return RawPosition + PositionCorrection; }
	// 보정 · 기울임은 카메라 자기 공간에서 (SimpleMath: a * b = a 다음 b)
	Quaternion FinalOrientation() const;
};

// 단계 (Unity CinemachineCore.Stage)
enum class CmStage { Body = 0, Aim = 1, Noise = 2, Finalize = 3 };

// C# · CLI 가 값 하나를 이름 없이 읽고 쓰는 길 (번호는 Runtime/Cinemachine.cs 와 같아야 한다)
class ICmProperties
{
public:
	virtual ~ICmProperties() = default;
	virtual float* FloatProp(int) { return nullptr; }
	virtual int* IntProp(int) { return nullptr; }      // bool 도 int 로 (BoolProp)
	virtual bool* BoolProp(int) { return nullptr; }
	virtual Vec3* VecProp(int) { return nullptr; }     // Vector2 는 x · y 만
	virtual uint64* ObjectProp(int) { return nullptr; }
	virtual void Validate() {}                         // 값을 바꾼 뒤 (범위 맞추기)
};

// 단계 컴포넌트의 바탕 (Unity CinemachineComponentBase)
class CinemachineComponentBase : public Component, public ICmProperties
{
public:
	virtual CmStage Stage() const = 0;
	// dt < 0 = 따라가기 없이 바로 (처음 · 편집 중 · 순간 이동)
	virtual void MutateCameraState(CinemachineCamera* vcam, CmState& state, float dt) = 0;
	// 순간 이동 · 다시 시작: 지난 프레임 값 버리기
	virtual void OnSnap() {}

	bool UsesUnityInspector() const override { return true; }
	const char* InspectorIconName() const override { return "camera"; }
};

namespace CmCore
{
	// Unity Damper.Damp: dampTime 초에 99 % 따라간다 — 이번 프레임에 움직일 양
	float Damp(float initial, float dampTime, float dt);
	Vec3 Damp(const Vec3& initial, const Vec3& dampTime, float dt);

	// 섞기 모양 (Unity CinemachineBlendDefinition.Styles)
	enum BlendStyle { Cut = 0, EaseInOut, EaseIn, EaseOut, HardIn, HardOut, Linear, BlendStyleCount };
	const char* const* BlendStyleNames();
	float BlendCurve(int style, float t);

	// 회전 ↔ 요 · 피치 · 롤 (라디안, DirectX 왼손: 앞 +Z, 피치 + = 아래)
	void ToYawPitchRoll(const Quaternion& q, float& yaw, float& pitch, float& roll);
	Quaternion LookRotation(const Vec3& dir);   // 롤 없음 (세상 위 = +Y)
	float WrapAngle(float radians);             // −π..π
	Vec3 EulerDegrees(const Quaternion& q);     // (피치, 요, 롤) 도 — CLI · 정보

	// 섞기: 위치 lerp, 회전 slerp, 렌즈 lerp
	CmState Lerp(const CmState& a, const CmState& b, float t);

	// 가상 카메라 목록 (만들 때 넣고 지울 때 뺀다)
	void Register(CinemachineCamera* vcam);
	void Unregister(CinemachineCamera* vcam);
	const std::vector<CinemachineCamera*>& Cameras();
	bool IsRegistered(const CinemachineCamera* vcam);
	void RegisterBrain(CinemachineBrain* brain);
	void UnregisterBrain(CinemachineBrain* brain);
	const std::vector<CinemachineBrain*>& Brains();
	uint32 NextActivationStamp();

	// 지금 씬의 fileID → GameObject
	GameObject* FindObject(uint64 fileID);
	// Brain 이 쓰는 카메라의 화면 비율 (Rotation Composer 가 화면 자리를 각도로 바꿀 때)
	float Aspect();
	void SetAspect(float aspect);

	// 흔들림 (Unity NoiseSettings 의 미리 만든 프로필)
	int NoiseProfileCount();
	const char* const* NoiseProfileNames();   // 0 = None
	// 이 시간의 위치 (m, 카메라 공간) · 회전 (도: 피치 · 요 · 롤) 흔들림
	void SampleNoise(int profile, float time, const Vec3& seed, Vec3& position, Vec3& rotationDegrees);

	// 충격 (Unity CinemachineImpulseManager)
	enum ImpulseShape { Recoil = 0, Bump, Explosion, Rumble, ImpulseShapeCount };
	enum ImpulseType { Uniform = 0, Dissipating, Propagating, ImpulseTypeCount };
	struct ImpulseEvent
	{
		int Channel = 1;
		int Shape = Bump;
		int Type = Uniform;
		float Duration = 0.2f;
		float DissipationRate = 0.25f;
		float DissipationDistance = 100.0f;
		float PropagationSpeed = 343.0f;
		Vec3 Position;
		Vec3 Velocity;
		double StartTime = 0.0;
	};
	float ImpulseCurve(int shape, float t);   // t = 0..1
	void AddImpulse(const ImpulseEvent& e);
	// 듣는 자리의 충격 변위 (월드 축, m)
	Vec3 SampleImpulse(const Vec3& listener, int channelMask, bool use2DDistance);
	int ActiveImpulseCount();
	void ClearImpulses();
	double Now();   // 충격 시계 (초)

	// JSON: Vec3 = [x, y, z]
	json VecJson(const Vec3& v);
	Vec3 VecFromJson(const json& j, const char* key, const Vec3& fallback);
}

// 대상 따라가기 설정 (Unity TrackerSettings) — Follow · Orbital Follow 가 함께 쓴다
struct CmTracker
{
	enum Binding { LockToTargetOnAssign = 0, LockToTargetWithWorldUp, LockToTargetNoRoll, LockToTarget, WorldSpace, LazyFollow, BindingCount };
	int BindingMode = LockToTargetWithWorldUp;
	Vec3 PositionDamping = Vec3(1.0f, 1.0f, 1.0f);
	int AngularDampingMode = 0;   // 0 Euler, 1 Quaternion
	Vec3 RotationDamping = Vec3(1.0f, 1.0f, 1.0f);   // 피치 · 요 · 롤 (초)
	float QuaternionDamping = 1.0f;

	// 묶는 회전 (대상의 방향 중 모드가 고른 것, 따라가기 적용)
	Quaternion Orientation(GameObject* target, const Vec3& cameraPosition, float dt);
	// 따라가는 점: 대상 위치를 묶는 공간의 축마다 늦게
	Vec3 TrackPosition(const Vec3& targetPosition, const Quaternion& binding, float dt);
	void Reset() { m_HasPrevious = false; m_HasPosition = false; m_AssignTarget = 0; }

	void Inspector();
	void ToJson(json& j) const;
	void FromJson(const json& j);

private:
	bool m_HasPrevious = false, m_HasPosition = false;
	float m_Yaw = 0.0f, m_Pitch = 0.0f, m_Roll = 0.0f;
	Quaternion m_Previous = Quaternion::Identity;
	Vec3 m_Position;
	uint64 m_AssignTarget = 0;
	Quaternion m_Assign = Quaternion::Identity;
};
