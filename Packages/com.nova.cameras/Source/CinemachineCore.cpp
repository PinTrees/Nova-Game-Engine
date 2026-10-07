#include "pch.h"
#include "CinemachineCore.h"
#include "UnityGUI.h"
#include <chrono>

Quaternion CmState::FinalOrientation() const
{
	const Quaternion dutch = Quaternion::CreateFromYawPitchRoll(0.0f, 0.0f, XMConvertToRadians(Lens.Dutch));
	return dutch * OrientationCorrection * RawOrientation;
}

namespace CmCore
{
	namespace
	{
		std::vector<CinemachineCamera*> s_Cameras;
		std::vector<CinemachineBrain*> s_Brains;
		uint32 s_Stamp = 0;
		float s_Aspect = 16.0f / 9.0f;
		std::vector<ImpulseEvent> s_Impulses;

		// 1 차원 그레이디언트 노이즈 (−1..1 근처)
		float Hash(int i, float seed)
		{
			uint32 h = (uint32)i * 374761393u + (uint32)(seed * 1000.0f) * 668265263u;
			h = (h ^ (h >> 13)) * 1274126177u;
			h ^= h >> 16;
			return (float)(h & 0xffff) / 32767.5f - 1.0f;
		}
		float Noise1(float x, float seed)
		{
			const float fl = floorf(x);
			const int i = (int)fl;
			const float f = x - fl;
			const float u = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);
			const float a = Hash(i, seed) * f;
			const float b = Hash(i + 1, seed) * (f - 1.0f);
			return (a + (b - a) * u) * 2.0f;
		}

		// 채널 하나 = 두 옥타브 (주파수 Hz, 진폭)
		struct Octave { float Frequency, Amplitude; };
		struct Channel { Octave A, B; };
		struct Profile { Channel Position[3]; Channel Rotation[3]; };

		// 손에 든 카메라 (회전만, 도) — 프로필마다 진폭 · 주파수 배율
		const Channel kHandheld[3] = {
			{ { 0.25f, 0.6f }, { 0.9f, 0.2f } },    // 피치
			{ { 0.2f, 0.7f }, { 0.8f, 0.2f } },     // 요
			{ { 0.3f, 0.25f }, { 1.1f, 0.1f } },    // 롤
		};
		Profile Handheld(float amplitude, float frequency)
		{
			Profile p{};
			for (int c = 0; c < 3; ++c)
			{
				p.Rotation[c].A = { kHandheld[c].A.Frequency * frequency, kHandheld[c].A.Amplitude * amplitude };
				p.Rotation[c].B = { kHandheld[c].B.Frequency * frequency, kHandheld[c].B.Amplitude * amplitude };
			}
			return p;
		}
		Profile Shake6D()
		{
			Profile p{};
			for (int c = 0; c < 3; ++c)
			{
				p.Position[c] = { { 2.1f + 0.4f * c, 0.06f }, { 5.3f + 0.7f * c, 0.03f } };
				p.Rotation[c] = { { 3.1f + 0.5f * c, 1.2f }, { 7.7f + 0.9f * c, 0.5f } };
			}
			return p;
		}
		const char* kNoiseNames[] = { "None", "6D Shake", "Handheld_normal_mild", "Handheld_normal_strong", "Handheld_normal_extreme",
			"Handheld_tele_mild", "Handheld_tele_strong", "Handheld_wideangle_mild", "Handheld_wideangle_strong" };
		const Profile& GetProfile(int index)
		{
			static const Profile profiles[] = {
				Profile{},
				Shake6D(),
				Handheld(1.0f, 1.0f),
				Handheld(2.5f, 1.3f),
				Handheld(5.0f, 1.8f),
				Handheld(0.4f, 1.0f),    // 망원: 같은 손 떨림이 화면에선 크다 → 각도는 작게
				Handheld(1.0f, 1.3f),
				Handheld(1.4f, 1.0f),
				Handheld(3.5f, 1.3f),
			};
			return profiles[std::clamp(index, 0, (int)(sizeof(profiles) / sizeof(profiles[0])) - 1)];
		}
		float SampleChannel(const Channel& c, float time, float seed)
		{
			return Noise1(time * c.A.Frequency, seed) * c.A.Amplitude + Noise1(time * c.B.Frequency, seed + 7.31f) * c.B.Amplitude;
		}

		const char* kBlendNames[] = { "Cut", "Ease In Out", "Ease In", "Ease Out", "Hard In", "Hard Out", "Linear" };
	}

	float Damp(float initial, float dampTime, float dt)
	{
		if (dt < 0.0f || dampTime < 1e-4f || fabsf(initial) < 1e-6f)
			return initial;
		const float k = 4.605170186f / dampTime;   // −ln(0.01)
		return initial * (1.0f - expf(-k * dt));
	}

	Vec3 Damp(const Vec3& initial, const Vec3& dampTime, float dt)
	{
		return Vec3(Damp(initial.x, dampTime.x, dt), Damp(initial.y, dampTime.y, dt), Damp(initial.z, dampTime.z, dt));
	}

	const char* const* BlendStyleNames() { return kBlendNames; }

	float BlendCurve(int style, float t)
	{
		t = std::clamp(t, 0.0f, 1.0f);
		switch (style)
		{
		case Cut: return 1.0f;
		case EaseInOut: return t * t * (3.0f - 2.0f * t);
		case EaseIn: return t * t * (2.0f - t);                                  // 천천히 출발, 끝은 일정
		case EaseOut: { const float u = 1.0f - t; return 1.0f - u * u * (2.0f - u); }
		case HardIn: return t * t;                                               // 천천히 출발, 갑자기 도착
		case HardOut: { const float u = 1.0f - t; return 1.0f - u * u; }
		default: return t;
		}
	}

	void ToYawPitchRoll(const Quaternion& q, float& yaw, float& pitch, float& roll)
	{
		const Vec3 f = Vec3::Transform(Vec3(0.0f, 0.0f, 1.0f), q);
		yaw = atan2f(f.x, f.z);
		pitch = asinf(std::clamp(-f.y, -1.0f, 1.0f));
		const Quaternion yp = Quaternion::CreateFromYawPitchRoll(yaw, pitch, 0.0f);
		const Vec3 r = Vec3::Transform(Vec3(1.0f, 0.0f, 0.0f), yp);
		const Vec3 u = Vec3::Transform(Vec3(0.0f, 1.0f, 0.0f), yp);
		const Vec3 up = Vec3::Transform(Vec3(0.0f, 1.0f, 0.0f), q);
		roll = atan2f(-up.Dot(r), up.Dot(u));
	}

	Quaternion LookRotation(const Vec3& dir)
	{
		const float len = dir.Length();
		if (len < 1e-6f)
			return Quaternion::Identity;
		const Vec3 d = dir / len;
		return Quaternion::CreateFromYawPitchRoll(atan2f(d.x, d.z), asinf(std::clamp(-d.y, -1.0f, 1.0f)), 0.0f);
	}

	float WrapAngle(float a)
	{
		a = fmodf(a + XM_PI, XM_2PI);
		if (a < 0.0f)
			a += XM_2PI;
		return a - XM_PI;
	}

	Vec3 EulerDegrees(const Quaternion& q)
	{
		float y, p, r;
		ToYawPitchRoll(q, y, p, r);
		return Vec3(XMConvertToDegrees(p), XMConvertToDegrees(y), XMConvertToDegrees(r));
	}

	CmState Lerp(const CmState& a, const CmState& b, float t)
	{
		CmState s = b;
		s.RawPosition = Vec3::Lerp(a.RawPosition, b.RawPosition, t);
		s.PositionCorrection = Vec3::Lerp(a.PositionCorrection, b.PositionCorrection, t);
		s.RawOrientation = Quaternion::Slerp(a.RawOrientation, b.RawOrientation, t);
		s.OrientationCorrection = Quaternion::Slerp(a.OrientationCorrection, b.OrientationCorrection, t);
		auto L = [t](float x, float y) { return x + (y - x) * t; };
		s.Lens.FieldOfView = L(a.Lens.FieldOfView, b.Lens.FieldOfView);
		s.Lens.OrthographicSize = L(a.Lens.OrthographicSize, b.Lens.OrthographicSize);
		s.Lens.NearClipPlane = L(a.Lens.NearClipPlane, b.Lens.NearClipPlane);
		s.Lens.FarClipPlane = L(a.Lens.FarClipPlane, b.Lens.FarClipPlane);
		s.Lens.Dutch = L(a.Lens.Dutch, b.Lens.Dutch);
		return s;
	}

	void Register(CinemachineCamera* vcam) { s_Cameras.push_back(vcam); }
	void Unregister(CinemachineCamera* vcam) { s_Cameras.erase(std::remove(s_Cameras.begin(), s_Cameras.end(), vcam), s_Cameras.end()); }
	const std::vector<CinemachineCamera*>& Cameras() { return s_Cameras; }
	bool IsRegistered(const CinemachineCamera* vcam) { return std::find(s_Cameras.begin(), s_Cameras.end(), vcam) != s_Cameras.end(); }
	void RegisterBrain(CinemachineBrain* brain) { s_Brains.push_back(brain); }
	void UnregisterBrain(CinemachineBrain* brain) { s_Brains.erase(std::remove(s_Brains.begin(), s_Brains.end(), brain), s_Brains.end()); }
	const std::vector<CinemachineBrain*>& Brains() { return s_Brains; }
	uint32 NextActivationStamp() { return ++s_Stamp; }

	GameObject* FindObject(uint64 fileID)
	{
		// Scene::FindByFileID 는 모든 오브젝트를 훑는다 → 찾은 것을 기억 (살아 있고 fileID 가 같으면 그대로)
		static Scene* s_Scene = nullptr;
		static std::unordered_map<uint64, GameObject*> s_Cache;
		Scene* scene = SceneManager::GetI()->GetCurrentScene();
		if (fileID == 0 || scene == nullptr)
			return nullptr;
		if (scene != s_Scene)
		{
			s_Scene = scene;
			s_Cache.clear();
		}
		auto it = s_Cache.find(fileID);
		if (it != s_Cache.end() && GameObject::IsAlive(it->second) && it->second->GetFileID() == fileID)
			return it->second;
		GameObject* go = scene->FindByFileID(fileID);
		if (go)
			s_Cache[fileID] = go;
		else if (it != s_Cache.end())
			s_Cache.erase(it);
		return go;
	}

	float Aspect() { return s_Aspect; }
	void SetAspect(float aspect)
	{
		if (aspect > 1e-3f && std::isfinite(aspect))
			s_Aspect = aspect;
	}

	int NoiseProfileCount() { return (int)(sizeof(kNoiseNames) / sizeof(kNoiseNames[0])); }
	const char* const* NoiseProfileNames() { return kNoiseNames; }

	void SampleNoise(int profile, float time, const Vec3& seed, Vec3& position, Vec3& rotationDegrees)
	{
		const Profile& p = GetProfile(profile);
		const float s[3] = { seed.x, seed.y, seed.z };
		for (int c = 0; c < 3; ++c)
		{
			(&position.x)[c] = SampleChannel(p.Position[c], time, s[c]);
			(&rotationDegrees.x)[c] = SampleChannel(p.Rotation[c], time, s[c] + 31.7f);
		}
	}

	float ImpulseCurve(int shape, float t)
	{
		if (t < 0.0f || t > 1.0f)
			return 0.0f;
		switch (shape)
		{
		case Recoil:
		{
			if (t < 0.1f)
				return sinf(t / 0.1f * XM_PIDIV2);
			const float u = (t - 0.1f) / 0.9f;
			return (1.0f - u) * (1.0f - u) * cosf(u * XM_PI * 1.5f);   // 뒤로 차고 조금 넘었다가 돌아온다
		}
		case Explosion:
			return (std::min)(1.0f, t / 0.03f) * (1.0f - t) * (1.0f - t) * cosf(t * XM_PI * 7.0f);
		case Rumble:
		{
			const float env = (std::max)(0.0f, (std::min)({ 1.0f, t / 0.15f, (1.0f - t) / 0.25f }));
			return env * (0.6f * sinf(XM_2PI * 9.0f * t) + 0.4f * sinf(XM_2PI * 13.0f * t + 1.0f));
		}
		default:   // Bump
			return sinf(XM_PI * t);
		}
	}

	double Now()
	{
		static const auto start = std::chrono::steady_clock::now();
		return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
	}

	void AddImpulse(const ImpulseEvent& e)
	{
		ImpulseEvent ev = e;
		ev.StartTime = Now();
		ev.Duration = (std::max)(0.01f, ev.Duration);
		s_Impulses.push_back(ev);
	}

	Vec3 SampleImpulse(const Vec3& listener, int channelMask, bool use2DDistance)
	{
		const double now = Now();
		// 끝난 충격 빼기 (전파는 가장 먼 곳까지 걸리는 시간만큼 더)
		s_Impulses.erase(std::remove_if(s_Impulses.begin(), s_Impulses.end(), [now](const ImpulseEvent& e) {
			const double travel = e.Type == Propagating ? e.DissipationDistance / (std::max)(1.0f, e.PropagationSpeed) : 0.0;
			return now - e.StartTime > e.Duration + travel + 0.1;
		}), s_Impulses.end());

		Vec3 sum;
		for (const ImpulseEvent& e : s_Impulses)
		{
			if ((e.Channel & channelMask) == 0)
				continue;
			Vec3 d = listener - e.Position;
			if (use2DDistance)
				d.y = 0.0f;
			const float distance = d.Length();
			float attenuation = 1.0f;
			double delay = 0.0;
			if (e.Type != Uniform)
			{
				// 거리로 줄어든다: DissipationDistance 에서 0, Rate 0.25 = 직선 (클수록 가까이서 빨리)
				const float x = std::clamp(1.0f - distance / (std::max)(0.01f, e.DissipationDistance), 0.0f, 1.0f);
				attenuation = powf(x, (std::max)(0.05f, e.DissipationRate * 4.0f));
				if (e.Type == Propagating)
					delay = distance / (std::max)(1.0f, e.PropagationSpeed);
			}
			if (attenuation <= 0.0f)
				continue;
			const float t = (float)((now - e.StartTime - delay) / e.Duration);
			sum += e.Velocity * (ImpulseCurve(e.Shape, t) * attenuation);
		}
		return sum;
	}

	int ActiveImpulseCount()
	{
		const double now = Now();
		int n = 0;
		for (const ImpulseEvent& e : s_Impulses)
			if (now - e.StartTime <= e.Duration)
				++n;
		return n;
	}

	void ClearImpulses() { s_Impulses.clear(); }
}

namespace CmCore
{
	json VecJson(const Vec3& v) { return json::array({ v.x, v.y, v.z }); }

	Vec3 VecFromJson(const json& j, const char* key, const Vec3& fallback)
	{
		auto it = j.find(key);
		if (it == j.end() || !it->is_array() || it->size() < 2)
			return fallback;
		return Vec3((*it)[0].get<float>(), (*it)[1].get<float>(), it->size() > 2 ? (*it)[2].get<float>() : 0.0f);
	}
}

// ---------------------------------------------------------------- CmTracker
Quaternion CmTracker::Orientation(GameObject* target, const Vec3& cameraPosition, float dt)
{
	if (BindingMode == WorldSpace || target == nullptr)
	{
		m_HasPrevious = false;
		return Quaternion::Identity;
	}
	Transform* t = target->GetTransform();
	if (BindingMode == LazyFollow)
	{
		// 카메라 자신의 수평 방향 (대상을 향해) — 대상이 돌아도 카메라는 돌지 않는다
		Vec3 d = t->GetPosition() - cameraPosition;
		d.y = 0.0f;
		m_HasPrevious = false;
		return d.LengthSquared() > 1e-8f ? Quaternion::CreateFromYawPitchRoll(atan2f(d.x, d.z), 0.0f, 0.0f) : Quaternion::Identity;
	}
	Quaternion goal = t->GetRotation();
	if (BindingMode == LockToTargetOnAssign)
	{
		if (m_AssignTarget != target->GetFileID())
		{
			m_AssignTarget = target->GetFileID();
			m_Assign = goal;
		}
		m_HasPrevious = false;
		return m_Assign;
	}
	float yaw, pitch, roll;
	CmCore::ToYawPitchRoll(goal, yaw, pitch, roll);
	if (BindingMode == LockToTargetWithWorldUp)
		pitch = roll = 0.0f;
	else if (BindingMode == LockToTargetNoRoll)
		roll = 0.0f;
	goal = Quaternion::CreateFromYawPitchRoll(yaw, pitch, roll);
	if (dt < 0.0f || !m_HasPrevious)
	{
		m_Yaw = yaw; m_Pitch = pitch; m_Roll = roll;
		m_Previous = goal;
		m_HasPrevious = true;
		return goal;
	}
	if (AngularDampingMode == 1)
	{
		m_Previous = Quaternion::Slerp(m_Previous, goal, CmCore::Damp(1.0f, QuaternionDamping, dt));
		CmCore::ToYawPitchRoll(m_Previous, m_Yaw, m_Pitch, m_Roll);
		return m_Previous;
	}
	m_Yaw += CmCore::Damp(CmCore::WrapAngle(yaw - m_Yaw), RotationDamping.y, dt);
	m_Pitch += CmCore::Damp(CmCore::WrapAngle(pitch - m_Pitch), RotationDamping.x, dt);
	m_Roll += CmCore::Damp(CmCore::WrapAngle(roll - m_Roll), RotationDamping.z, dt);
	m_Previous = Quaternion::CreateFromYawPitchRoll(m_Yaw, m_Pitch, m_Roll);
	return m_Previous;
}

Vec3 CmTracker::TrackPosition(const Vec3& targetPosition, const Quaternion& binding, float dt)
{
	if (dt < 0.0f || !m_HasPosition)
	{
		m_Position = targetPosition;
		m_HasPosition = true;
		return m_Position;
	}
	Quaternion inv;
	binding.Inverse(inv);
	const Vec3 local = Vec3::Transform(targetPosition - m_Position, inv);
	m_Position += Vec3::Transform(CmCore::Damp(local, PositionDamping, dt), binding);
	return m_Position;
}

void CmTracker::Inspector()
{
	static const char* bindings[] = { "Lock To Target On Assign", "Lock To Target With World Up", "Lock To Target No Roll", "Lock To Target", "World Space", "Lazy Follow" };
	UnityGUI::Dropdown("Binding Mode", &BindingMode, bindings, BindingCount);
	if (UnityGUI::Vector3("Position Damping", &PositionDamping.x))
		PositionDamping = Vec3::Max(PositionDamping, Vec3::Zero);
	if (BindingMode != WorldSpace && BindingMode != LazyFollow && BindingMode != LockToTargetOnAssign)
	{
		static const char* modes[] = { "Euler", "Quaternion" };
		UnityGUI::Dropdown("Angular Damping Mode", &AngularDampingMode, modes, 2);
		if (AngularDampingMode == 0)
		{
			if (UnityGUI::Vector3("Rotation Damping", &RotationDamping.x))
				RotationDamping = Vec3::Max(RotationDamping, Vec3::Zero);
		}
		else if (UnityGUI::Float("Quaternion Damping", &QuaternionDamping))
			QuaternionDamping = (std::max)(0.0f, QuaternionDamping);
	}
}

void CmTracker::ToJson(json& j) const
{
	j["bindingMode"] = BindingMode;
	j["positionDamping"] = CmCore::VecJson(PositionDamping);
	j["angularDampingMode"] = AngularDampingMode;
	j["rotationDamping"] = CmCore::VecJson(RotationDamping);
	j["quaternionDamping"] = QuaternionDamping;
}

void CmTracker::FromJson(const json& j)
{
	BindingMode = std::clamp(j.value("bindingMode", (int)LockToTargetWithWorldUp), 0, BindingCount - 1);
	PositionDamping = CmCore::VecFromJson(j, "positionDamping", Vec3(1.0f, 1.0f, 1.0f));
	AngularDampingMode = j.value("angularDampingMode", 0);
	RotationDamping = CmCore::VecFromJson(j, "rotationDamping", Vec3(1.0f, 1.0f, 1.0f));
	QuaternionDamping = j.value("quaternionDamping", 1.0f);
}
