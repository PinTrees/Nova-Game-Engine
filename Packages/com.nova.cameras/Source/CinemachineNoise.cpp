#include "pch.h"
#include "CinemachineNoise.h"
#include "CinemachineCamera.h"
#include "UnityGUI.h"

// ---------------------------------------------------------------- Basic Multi Channel Perlin
CinemachineBasicMultiChannelPerlin::CinemachineBasicMultiChannelPerlin()
{
	m_InspectorTitleName = "Cinemachine Basic Multi Channel Perlin";
	ReSeed();
}

void CinemachineBasicMultiChannelPerlin::ReSeed()
{
	// 가상 카메라마다 다른 무늬 (같은 프로필이어도 같이 흔들리지 않게)
	static uint32 s_Counter = 0x9E3779B9u;
	auto next = []() {
		s_Counter = s_Counter * 1664525u + 1013904223u;
		return (float)((s_Counter >> 8) & 0xffff) / 65535.0f * 100.0f;
	};
	m_Seed = Vec3(next(), next(), next());
}

void CinemachineBasicMultiChannelPerlin::MutateCameraState(CinemachineCamera*, CmState& state, float dt)
{
	if (NoiseProfile <= 0 || AmplitudeGain == 0.0f)
		return;
	if (dt > 0.0f)
		m_Time += dt * FrequencyGain;
	Vec3 pos, rotDeg;
	CmCore::SampleNoise(NoiseProfile, m_Time, m_Seed, pos, rotDeg);
	pos *= AmplitudeGain;
	rotDeg *= AmplitudeGain;
	const Quaternion rot = Quaternion::CreateFromYawPitchRoll(XMConvertToRadians(rotDeg.y), XMConvertToRadians(rotDeg.x), XMConvertToRadians(rotDeg.z));
	// 회전 중심이 카메라가 아니면 그 점을 중심으로 돈 만큼 위치도 옮긴다
	if (PivotOffset.LengthSquared() > 1e-10f)
		pos += PivotOffset - Vec3::Transform(PivotOffset, rot);
	state.PositionCorrection += Vec3::Transform(pos, state.OrientationCorrection * state.RawOrientation);
	state.OrientationCorrection = rot * state.OrientationCorrection;
}

void CinemachineBasicMultiChannelPerlin::Validate()
{
	NoiseProfile = std::clamp(NoiseProfile, 0, CmCore::NoiseProfileCount() - 1);
}

void CinemachineBasicMultiChannelPerlin::OnInspectorGUI()
{
	if (UnityGUI::Dropdown("Noise Profile", &NoiseProfile, CmCore::NoiseProfileNames(), CmCore::NoiseProfileCount()))
		Validate();
	UnityGUI::Vector3("Pivot Offset", &PivotOffset.x);
	UnityGUI::Float("Amplitude Gain", &AmplitudeGain);
	UnityGUI::Float("Frequency Gain", &FrequencyGain);
	if (UnityGUI::CenterButton("New random seed", 180.0f))
		ReSeed();
	UnityGUI::HelpBox("Noise only moves the camera output in Play mode. Handheld profiles shake rotation only; 6D Shake also moves position.", false);
}

GENERATE_COMPONENT_FUNC_TOJSON(CinemachineBasicMultiChannelPerlin)
{
	json j;
	j["type"] = "CinemachineBasicMultiChannelPerlin";
	j["enabled"] = m_Enabled;
	j["noiseProfile"] = CmCore::NoiseProfileNames()[std::clamp(NoiseProfile, 0, CmCore::NoiseProfileCount() - 1)];
	j["pivotOffset"] = CmCore::VecJson(PivotOffset);
	j["amplitudeGain"] = AmplitudeGain;
	j["frequencyGain"] = FrequencyGain;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(CinemachineBasicMultiChannelPerlin)
{
	m_Enabled = j.value("enabled", true);
	// 프로필은 이름으로 (순서가 바뀌어도)
	NoiseProfile = 2;
	auto it = j.find("noiseProfile");
	if (it != j.end() && it->is_number())
		NoiseProfile = it->get<int>();   // 번호도 (CLI set)
	else if (it != j.end() && it->is_string())
		for (int i = 0; i < CmCore::NoiseProfileCount(); ++i)
			if (it->get<std::string>() == CmCore::NoiseProfileNames()[i])
				NoiseProfile = i;
	Validate();
	PivotOffset = CmCore::VecFromJson(j, "pivotOffset", Vec3());
	AmplitudeGain = j.value("amplitudeGain", 1.0f);
	FrequencyGain = j.value("frequencyGain", 1.0f);
}

// ---------------------------------------------------------------- Impulse Source
namespace
{
	const char* kShapes[] = { "Recoil", "Bump", "Explosion", "Rumble" };
	const char* kTypes[] = { "Uniform", "Dissipating", "Propagating" };
}

void CinemachineImpulseSource::GenerateImpulseAtPositionWithVelocity(const Vec3& position, const Vec3& velocity)
{
	CmCore::ImpulseEvent e;
	e.Channel = ImpulseChannel;
	e.Shape = ImpulseShape;
	e.Type = ImpulseType;
	e.Duration = ImpulseDuration;
	e.DissipationRate = DissipationRate;
	e.DissipationDistance = DissipationDistance;
	e.PropagationSpeed = PropagationSpeed;
	e.Position = position;
	e.Velocity = velocity;
	CmCore::AddImpulse(e);
}

void CinemachineImpulseSource::GenerateImpulseWithVelocity(const Vec3& velocity)
{
	GenerateImpulseAtPositionWithVelocity(m_pGameObject ? m_pGameObject->GetTransform()->GetPosition() : Vec3(), velocity);
}

int* CinemachineImpulseSource::IntProp(int i)
{
	return i == 0 ? &ImpulseChannel : i == 1 ? &ImpulseShape : i == 2 ? &ImpulseType : nullptr;
}

float* CinemachineImpulseSource::FloatProp(int i)
{
	switch (i)
	{
	case 0: return &ImpulseDuration;
	case 1: return &DissipationRate;
	case 2: return &DissipationDistance;
	case 3: return &PropagationSpeed;
	default: return nullptr;
	}
}

void CinemachineImpulseSource::Validate()
{
	ImpulseShape = std::clamp(ImpulseShape, 0, (int)CmCore::ImpulseShapeCount - 1);
	ImpulseType = std::clamp(ImpulseType, 0, (int)CmCore::ImpulseTypeCount - 1);
	ImpulseDuration = (std::max)(0.01f, ImpulseDuration);
	DissipationRate = std::clamp(DissipationRate, 0.0f, 1.0f);
	DissipationDistance = (std::max)(0.01f, DissipationDistance);
	PropagationSpeed = (std::max)(1.0f, PropagationSpeed);
}

void CinemachineImpulseSource::OnInspectorGUI()
{
	bool changed = false;
	UnityGUI::Label("Impulse Definition", 0, true);
	changed |= UnityGUI::Int("Impulse Channel", &ImpulseChannel, 1);
	changed |= UnityGUI::Dropdown("Impulse Shape", &ImpulseShape, kShapes, CmCore::ImpulseShapeCount, 1);
	changed |= UnityGUI::Float("Impulse Duration", &ImpulseDuration, 1);
	changed |= UnityGUI::Dropdown("Impulse Type", &ImpulseType, kTypes, CmCore::ImpulseTypeCount, 1);
	if (ImpulseType != CmCore::Uniform)
	{
		changed |= UnityGUI::Slider("Dissipation Rate", &DissipationRate, 0.0f, 1.0f, 1);
		changed |= UnityGUI::Float("Dissipation Distance", &DissipationDistance, 1);
		if (ImpulseType == CmCore::Propagating)
			changed |= UnityGUI::Float("Propagation Speed", &PropagationSpeed, 1);
	}
	changed |= UnityGUI::Vector3("Default Velocity", &DefaultVelocity.x);
	if (changed)
		Validate();
	UnityGUI::Float("Test Force", &m_TestForce);
	if (UnityGUI::CenterButton("Invoke (Play mode)", 200.0f) && Application::IsPlaying())
		GenerateImpulseWithForce(m_TestForce);
	UnityGUI::HelpBox("Call GenerateImpulse() from a script (hit, landing, explosion). Cinemachine Cameras with a Cinemachine Impulse Listener on the same channel shake.", false);
}

GENERATE_COMPONENT_FUNC_TOJSON(CinemachineImpulseSource)
{
	json j;
	j["type"] = "CinemachineImpulseSource";
	j["enabled"] = m_Enabled;
	j["impulseChannel"] = ImpulseChannel;
	j["impulseShape"] = ImpulseShape;
	j["impulseDuration"] = ImpulseDuration;
	j["impulseType"] = ImpulseType;
	j["dissipationRate"] = DissipationRate;
	j["dissipationDistance"] = DissipationDistance;
	j["propagationSpeed"] = PropagationSpeed;
	j["defaultVelocity"] = CmCore::VecJson(DefaultVelocity);
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(CinemachineImpulseSource)
{
	m_Enabled = j.value("enabled", true);
	ImpulseChannel = j.value("impulseChannel", 1);
	ImpulseShape = j.value("impulseShape", (int)CmCore::Bump);
	ImpulseDuration = j.value("impulseDuration", 0.2f);
	ImpulseType = j.value("impulseType", (int)CmCore::Uniform);
	DissipationRate = j.value("dissipationRate", 0.25f);
	DissipationDistance = j.value("dissipationDistance", 100.0f);
	PropagationSpeed = j.value("propagationSpeed", 343.0f);
	DefaultVelocity = CmCore::VecFromJson(j, "defaultVelocity", Vec3(0.0f, -1.0f, 0.0f));
	Validate();
}

// ---------------------------------------------------------------- Impulse Listener
void CinemachineImpulseListener::MutateCameraState(CinemachineCamera*, CmState& state, float)
{
	Vec3 impulse = CmCore::SampleImpulse(state.RawPosition, ChannelMask, Use2DDistance) * Gain;
	if (impulse.LengthSquared() < 1e-12f)
		return;
	if (UseCameraSpace)
		impulse = Vec3::Transform(impulse, state.RawOrientation);
	state.PositionCorrection += impulse;
}

void CinemachineImpulseListener::OnInspectorGUI()
{
	UnityGUI::Int("Channel Mask", &ChannelMask);
	UnityGUI::Float("Gain", &Gain);
	UnityGUI::Toggle("Use 2D Distance", &Use2DDistance);
	UnityGUI::Toggle("Use Camera Space", &UseCameraSpace);
}

GENERATE_COMPONENT_FUNC_TOJSON(CinemachineImpulseListener)
{
	json j;
	j["type"] = "CinemachineImpulseListener";
	j["enabled"] = m_Enabled;
	j["channelMask"] = ChannelMask;
	j["gain"] = Gain;
	j["use2DDistance"] = Use2DDistance;
	j["useCameraSpace"] = UseCameraSpace;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(CinemachineImpulseListener)
{
	m_Enabled = j.value("enabled", true);
	ChannelMask = j.value("channelMask", 1);
	Gain = j.value("gain", 1.0f);
	Use2DDistance = j.value("use2DDistance", false);
	UseCameraSpace = j.value("useCameraSpace", true);
}
