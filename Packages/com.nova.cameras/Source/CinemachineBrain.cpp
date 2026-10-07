#include "pch.h"
#include "CinemachineBrain.h"
#include "CinemachineCamera.h"
#include "UnityGUI.h"

namespace
{
	const char* kAnyCamera = "**ANY CAMERA**";

	std::string NameOf(const CinemachineCamera* vcam)
	{
		GameObject* go = vcam ? const_cast<CinemachineCamera*>(vcam)->GetGameObject() : nullptr;
		return go ? go->GetName() : std::string();
	}
}

CinemachineBrain::CinemachineBrain()
{
	m_InspectorTitleName = "Cinemachine Brain";
	CmCore::RegisterBrain(this);
}

CinemachineBrain::~CinemachineBrain()
{
	CmCore::UnregisterBrain(this);
}

bool CinemachineBrain::IsPrimary() const
{
	// Brain 이 여럿이어도 가상 카메라는 한 번만 갱신한다 (켜진 첫 Brain 이)
	for (CinemachineBrain* b : CmCore::Brains())
	{
		GameObject* go = b->GetGameObject();
		if (go && b->IsEnabled() && GameObject::IsAlive(go) && go->IsActiveInHierarchy() && CmCore::FindObject(go->GetFileID()) == go)
			return b == this;
	}
	return false;
}

bool CinemachineBrain::IsLiveChild(const CinemachineCamera* vcam) const
{
	return vcam != nullptr && (vcam == m_Live || (m_Blend.Active && vcam == m_Blend.From));
}

void CinemachineBrain::FindBlend(const std::string& from, const std::string& to, int& style, float& time) const
{
	// Unity 와 같은 순서: 정확히 맞는 것 → From 이 아무 카메라 → To 가 아무 카메라 → 기본
	int best = -1, bestScore = 0;
	for (size_t i = 0; i < CustomBlends.size(); ++i)
	{
		const CustomBlend& b = CustomBlends[i];
		const bool fromExact = b.From == from, toExact = b.To == to;
		const bool fromAny = b.From == kAnyCamera, toAny = b.To == kAnyCamera;
		int score = 0;
		if (fromExact && toExact) score = 3;
		else if (fromAny && toExact) score = 2;
		else if (fromExact && toAny) score = 1;
		if (score > bestScore)
		{
			bestScore = score;
			best = (int)i;
		}
	}
	if (best >= 0)
	{
		style = CustomBlends[best].Style;
		time = CustomBlends[best].Time;
		return;
	}
	style = DefaultBlendStyle;
	time = DefaultBlendTime;
}

void CinemachineBrain::LateUpdate()
{
	if (m_pGameObject == nullptr || !m_Enabled)
		return;
	const bool editing = !Application::IsPlaying();
	const float dt = editing ? -1.0f : (Application::IsPaused() ? 0.0f : (float)DT);
	Camera* camera = m_pGameObject->GetComponent<Camera>();
	if (camera)
		CmCore::SetAspect(camera->GetAspect());

	if (m_Live && !CmCore::IsRegistered(m_Live))
		m_Live = nullptr;
	if (m_Blend.From && !CmCore::IsRegistered(m_Blend.From))
		m_Blend.From = nullptr;

	// 가상 카메라 갱신 + Live 고르기
	const bool primary = IsPrimary();
	CinemachineCamera* best = nullptr;
	for (CinemachineCamera* vcam : CmCore::Cameras())
	{
		const bool eligible = vcam->IsEligible();
		if (primary)
		{
			if (eligible && !vcam->WasEligible)
			{
				vcam->ActivationStamp = CmCore::NextActivationStamp();   // 켜진 순서 (같은 Priority 면 늦게 켜진 것)
				vcam->SnapNext();
			}
			vcam->WasEligible = eligible;
		}
		if (!eligible)
			continue;
		if (primary)
			vcam->UpdateCameraState(dt);
		if (best == nullptr || vcam->Priority > best->Priority || (vcam->Priority == best->Priority && vcam->ActivationStamp > best->ActivationStamp))
			best = vcam;
	}
	// 섞는 중의 출발 카메라가 꺼졌어도 마지막 모습에서 섞는다
	if (primary && m_Blend.Active && m_Blend.From && !m_Blend.From->IsEligible())
		m_Blend.From = nullptr;

	if (best != m_Live)
	{
		int style = CmCore::Cut;
		float time = 0.0f;
		if (m_Live && best && !editing && m_HasOutput)
			FindBlend(NameOf(m_Live), NameOf(best), style, time);
		if (style == CmCore::Cut || time <= 0.0f)
			m_Blend = Blend();
		else
		{
			Blend b;
			b.Active = true;
			// 섞는 중에 또 바뀌면 지금 화면에서 새로 섞는다
			b.From = m_Blend.Active ? nullptr : m_Live;
			b.FromState = m_Blend.Active ? m_Output : m_Live->State();
			b.FromName = m_Blend.Active ? "Mid-Blend" : NameOf(m_Live);
			b.Style = style;
			b.Duration = time;
			m_Blend = b;
		}
		m_Live = best;
		++m_Switches;
	}
	if (m_Live == nullptr)
		return;   // 가상 카메라가 없다: Camera 는 그대로

	CmState out = m_Live->State();
	if (m_Blend.Active)
	{
		if (m_Blend.From)
			m_Blend.FromState = m_Blend.From->State();
		m_Blend.Elapsed += (std::max)(0.0f, dt);
		const float t = m_Blend.Elapsed / (std::max)(1e-4f, m_Blend.Duration);
		if (t >= 1.0f)
			m_Blend = Blend();
		else
			out = CmCore::Lerp(m_Blend.FromState, out, CmCore::BlendCurve(m_Blend.Style, t));
	}
	m_Output = out;
	m_HasOutput = true;
	Apply(out, editing);
}

void CinemachineBrain::Apply(const CmState& s, bool editing)
{
	Transform* tr = m_pGameObject->GetTransform();
	const Vec3 p = s.FinalPosition();
	const Quaternion q = s.FinalOrientation();
	// 편집 중에는 바뀔 때만 (같은 값을 매 프레임 쓰지 않는다)
	if (!editing || (p - tr->GetPosition()).LengthSquared() > 1e-10f)
		tr->SetPosition(p);
	if (!editing || 1.0f - fabsf(q.Dot(tr->GetRotation())) > 1e-9f)
		tr->SetRotation(q);
	Camera* camera = m_pGameObject->GetComponent<Camera>();
	if (camera == nullptr)
		return;
	const float fov = XMConvertToRadians(std::clamp(s.Lens.FieldOfView, 1.0f, 179.0f));
	if (fabsf(camera->GetFovY() - fov) > 1e-6f)
		camera->SetFovY(fov);
	if (fabsf(camera->GetNearZ() - s.Lens.NearClipPlane) > 1e-6f)
		camera->SetNearZ(s.Lens.NearClipPlane);
	if (fabsf(camera->GetFarZ() - s.Lens.FarClipPlane) > 1e-4f)
		camera->SetFarZ(s.Lens.FarClipPlane);
	if (camera->IsOrthographic() && fabsf(camera->GetOrthoSize() - s.Lens.OrthographicSize) > 1e-6f)
		camera->SetOrthoSize(s.Lens.OrthographicSize);
}

void CinemachineBrain::Validate()
{
	DefaultBlendStyle = std::clamp(DefaultBlendStyle, 0, (int)CmCore::BlendStyleCount - 1);
	DefaultBlendTime = (std::max)(0.0f, DefaultBlendTime);
}

json CinemachineBrain::Info() const
{
	json j;
	j["camera"] = m_pGameObject ? m_pGameObject->GetName() : std::string();
	j["live"] = NameOf(m_Live);
	j["blending"] = m_Blend.Active;
	j["switches"] = m_Switches;
	j["defaultBlend"] = { { "style", CmCore::BlendStyleNames()[DefaultBlendStyle] }, { "time", DefaultBlendTime } };
	if (m_Blend.Active)
	{
		const float t = m_Blend.Elapsed / (std::max)(1e-4f, m_Blend.Duration);
		j["blend"] = { { "from", m_Blend.FromName }, { "to", NameOf(m_Live) }, { "style", CmCore::BlendStyleNames()[m_Blend.Style] },
			{ "duration", m_Blend.Duration }, { "elapsed", m_Blend.Elapsed }, { "t", t }, { "weight", CmCore::BlendCurve(m_Blend.Style, t) } };
	}
	if (m_HasOutput)
	{
		const Vec3 p = m_Output.FinalPosition();
		const Vec3 e = CmCore::EulerDegrees(m_Output.FinalOrientation());
		j["output"] = { { "position", { p.x, p.y, p.z } }, { "rotation", { e.x, e.y, e.z } }, { "fov", m_Output.Lens.FieldOfView } };
	}
	if (m_pGameObject)
	{
		Transform* tr = m_pGameObject->GetTransform();
		const Vec3 p = tr->GetPosition();
		const Vec3 e = CmCore::EulerDegrees(tr->GetRotation());
		j["transform"] = { { "position", { p.x, p.y, p.z } }, { "rotation", { e.x, e.y, e.z } } };
		if (Camera* c = m_pGameObject->GetComponent<Camera>())
			j["cameraFov"] = XMConvertToDegrees(c->GetFovY());
	}
	return j;
}

void CinemachineBrain::OnInspectorGUI()
{
	const std::string live = NameOf(m_Live);
	UnityGUI::ValueLabel("Live Camera", live.empty() ? "(none)" : live.c_str());
	if (m_Blend.Active)
	{
		char buf[256];
		snprintf(buf, sizeof(buf), "%s -> %s (%.0f%%)", m_Blend.FromName.c_str(), live.c_str(),
			100.0f * m_Blend.Elapsed / (std::max)(1e-4f, m_Blend.Duration));
		UnityGUI::ValueLabel("Live Blend", buf);
	}

	UnityGUI::Label("Default Blend", 0, true);
	UnityGUI::Dropdown("Style", &DefaultBlendStyle, CmCore::BlendStyleNames(), CmCore::BlendStyleCount, 1);
	if (DefaultBlendStyle != CmCore::Cut && UnityGUI::Float("Time", &DefaultBlendTime, 1))
		Validate();

	UnityGUI::Label("Custom Blends", 0, true);
	int remove = -1;
	for (size_t i = 0; i < CustomBlends.size(); ++i)
	{
		CustomBlend& b = CustomBlends[i];
		ImGui::PushID((int)i);
		UnityGUI::TextField("From", &b.From, 1);
		UnityGUI::TextField("To", &b.To, 1);
		UnityGUI::Dropdown("Style", &b.Style, CmCore::BlendStyleNames(), CmCore::BlendStyleCount, 1);
		if (b.Style != CmCore::Cut && UnityGUI::Float("Time", &b.Time, 1))
			b.Time = (std::max)(0.0f, b.Time);
		if (UnityGUI::CenterButton("Remove Blend", 140.0f))
			remove = (int)i;
		ImGui::PopID();
	}
	if (remove >= 0)
		CustomBlends.erase(CustomBlends.begin() + remove);
	if (UnityGUI::CenterButton("Add Custom Blend", 180.0f))
		CustomBlends.push_back(CustomBlend());
	if (!CustomBlends.empty())
		UnityGUI::HelpBox("From / To are camera GameObject names. **ANY CAMERA** matches every camera.", false);
	if (m_pGameObject && m_pGameObject->GetComponent<Camera>() == nullptr)
		UnityGUI::HelpBox("Cinemachine Brain needs a Camera on the same GameObject.", true);
}

GENERATE_COMPONENT_FUNC_TOJSON(CinemachineBrain)
{
	json j;
	j["type"] = "CinemachineBrain";
	j["enabled"] = m_Enabled;
	j["defaultBlendStyle"] = DefaultBlendStyle;
	j["defaultBlendTime"] = DefaultBlendTime;
	json blends = json::array();
	for (const CustomBlend& b : CustomBlends)
		blends.push_back({ { "from", b.From }, { "to", b.To }, { "style", b.Style }, { "time", b.Time } });
	j["customBlends"] = blends;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(CinemachineBrain)
{
	m_Enabled = j.value("enabled", true);
	DefaultBlendStyle = j.value("defaultBlendStyle", (int)CmCore::EaseInOut);
	DefaultBlendTime = j.value("defaultBlendTime", 2.0f);
	CustomBlends.clear();
	if (j.contains("customBlends") && j["customBlends"].is_array())
		for (const json& b : j["customBlends"])
		{
			CustomBlend c;
			c.From = b.value("from", std::string(kAnyCamera));
			c.To = b.value("to", std::string(kAnyCamera));
			c.Style = std::clamp(b.value("style", (int)CmCore::EaseInOut), 0, (int)CmCore::BlendStyleCount - 1);
			c.Time = (std::max)(0.0f, b.value("time", 2.0f));
			CustomBlends.push_back(c);
		}
	Validate();
}
