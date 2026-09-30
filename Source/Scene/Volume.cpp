#include "pch.h"
#include "Volume.h"
#include "BoxCollider.h"
#include "SphereCollider.h"
#include "UnityGUI.h"
#include "VolumeEditor.h"
#include "RenderPipelineSettings.h"

std::vector<Volume*> Volume::s_All;

Volume::Volume()
{
	m_InspectorTitleName = "Volume";
	s_All.push_back(this);
}

Volume::~Volume()
{
	s_All.erase(std::remove(s_All.begin(), s_All.end(), this), s_All.end());
}

void Volume::SetProfile(const std::string& path)
{
	m_ProfilePath = path;
	m_Profile = path.empty() ? nullptr : VolumeProfile::Load(path);
}

std::shared_ptr<VolumeProfile> Volume::GetProfile()
{
	if (m_ProfilePath.empty())
		return nullptr;
	m_Profile = VolumeProfile::Load(m_ProfilePath);   // 캐시: 파일이 바뀌었을 때만 다시 읽음
	return m_Profile;
}

bool Volume::HasCollider() const
{
	if (m_pGameObject == nullptr)
		return false;
	auto* box = m_pGameObject->GetComponent<BoxCollider>();
	auto* sphere = m_pGameObject->GetComponent<SphereCollider>();
	return (box && box->IsEnabled()) || (sphere && sphere->IsEnabled());
}

float Volume::DistanceTo(const Vec3& point) const
{
	if (m_pGameObject == nullptr)
		return -1.0f;
	float best = -1.0f;
	auto consider = [&](float d) { if (best < 0.0f || d < best) best = d; };

	Transform* tr = m_pGameObject->GetTransform();
	if (auto* box = m_pGameObject->GetComponent<BoxCollider>(); box && box->IsEnabled())
	{
		// 콜라이더 로컬 공간(크기 포함)에서 가장 가까운 점을 찾고 월드로 되돌려 거리를 잰다
		const Matrix world = tr->GetWorldMatrix();
		const Matrix inv = world.Invert();
		const Vec3 local = Vec3::Transform(point, inv);
		const Vec3 half = box->GetSize() * 0.5f;
		const Vec3 c = box->GetCenter();
		const Vec3 closest(std::clamp(local.x, c.x - half.x, c.x + half.x),
			std::clamp(local.y, c.y - half.y, c.y + half.y),
			std::clamp(local.z, c.z - half.z, c.z + half.z));
		consider((Vec3::Transform(closest, world) - point).Length());
	}
	if (auto* sphere = m_pGameObject->GetComponent<SphereCollider>(); sphere && sphere->IsEnabled())
	{
		const Vec3 s = tr->GetScale();
		const float radius = sphere->GetRadius() * (std::max)(fabsf(s.x), (std::max)(fabsf(s.y), fabsf(s.z)));
		consider((std::max)(0.0f, (sphere->GetWorldCenter() - point).Length() - radius));
	}
	return best;
}

// ------------------------------------------------------------------ Inspector
void Volume::OnInspectorGUI()
{
	static const char* kModes[] = { "Global", "Local" };
	int mode = (int)m_Mode;
	if (UnityGUI::Dropdown("Mode", &mode, kModes, 2))
		m_Mode = (Mode)mode;
	if (m_Mode == Mode::Local)
	{
		if (UnityGUI::Float("Blend Distance", &m_BlendDistance))
			m_BlendDistance = (std::max)(0.0f, m_BlendDistance);
		if (!HasCollider())
			UnityGUI::HelpBox("Add a Box Collider or Sphere Collider to this GameObject to set boundaries for the local Volume.", true);
	}
	UnityGUI::Slider("Weight", &m_Weight, 0.0f, 1.0f);
	UnityGUI::Float("Priority", &m_Priority);

	const std::string baseName = (m_pGameObject ? m_pGameObject->GetName() : std::string("Volume")) + " Profile";
	std::string path = m_ProfilePath;
	if (VolumeEditor::ProfileField("Profile", path, baseName))
		SetProfile(path);

	auto profile = GetProfile();
	if (profile == nullptr)
	{
		UnityGUI::HelpBox("Please select or create a new Volume profile to begin applying effects to the scene.", false);
		return;
	}
	VolumeEditor::DrawProfile(profile, false);
}

// ------------------------------------------------------------------ 저장
GENERATE_COMPONENT_FUNC_TOJSON(Volume)
{
	json j;
	SERIALIZE_TYPE(j, Volume);
	j["enabled"] = m_Enabled;
	j["mode"] = (int)m_Mode;
	j["weight"] = m_Weight;
	j["priority"] = m_Priority;
	j["blendDistance"] = m_BlendDistance;
	j["profile"] = m_ProfilePath;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(Volume)
{
	m_Enabled = j.value("enabled", true);
	m_Mode = (Mode)j.value("mode", 0);
	m_Weight = j.value("weight", 1.0f);
	m_Priority = j.value("priority", 0.0f);
	m_BlendDistance = j.value("blendDistance", 0.0f);
	SetProfile(j.value("profile", std::string()));
}

// ================================================================== VolumeManager
namespace
{
	bool ActiveInHierarchy(GameObject* go)
	{
		for (GameObject* g = go; g != nullptr; g = g->GetParent())
			if (!g->IsActive())
				return false;
		return go != nullptr;
	}

	void BlendParam(VolumeParameter& dst, const VolumeParameter& src, float t)
	{
		switch (src.Kind)
		{
		case VolumeParamKind::Bool:
		case VolumeParamKind::Enum:
			if (t > 0.0f)
				dst.Value[0] = src.Value[0];
			break;
		case VolumeParamKind::Int:
			dst.Value[0] = (float)(int)(dst.Value[0] + (src.Value[0] - dst.Value[0]) * t + 0.5f);
			break;
		default:
			for (int i = 0; i < 4; ++i)
				dst.Value[i] += (src.Value[i] - dst.Value[i]) * t;
			break;
		}
		dst.Override = true;
	}

	void ApplyProfile(VolumeStack& stack, const VolumeProfile& profile, float t, bool allParams)
	{
		for (const auto& comp : profile.Components)
		{
			if (!comp->Active)
				continue;
			VolumeComponent* dst = stack.Get(comp->Type);
			if (dst == nullptr)
				continue;
			for (const VolumeParameter& p : comp->Params)
				if (allParams || p.Override)
					if (VolumeParameter* d = dst->Find(p.Key))
						BlendParam(*d, p, t);
		}
	}
}

namespace VolumeManager
{
	bool AnyActiveVolume()
	{
		for (Volume* v : Volume::All())
			if (v->IsEnabled() && ActiveInHierarchy(v->GetGameObject()) && !v->GetProfilePath().empty())
				return true;
		return false;
	}

	void Update(VolumeStack& stack, const Vec3& cameraPosition)
	{
		stack.Reset();

		// 1) Project Settings > Graphics 의 기본 프로파일: 모든 값을 기준값으로 (씬에 Volume 이 없어도 적용)
		if (auto def = RenderPipelineSettings::DefaultVolumeProfile())
			ApplyProfile(stack, *def, 1.0f, true);

		// 2) 씬의 Volume: 우선순위가 낮은 것부터 섞는다 (높은 것이 마지막에 덮음)
		std::vector<Volume*> volumes;
		for (Volume* v : Volume::All())
			if (v->IsEnabled() && v->GetWeight() > 0.0f && ActiveInHierarchy(v->GetGameObject()))
				volumes.push_back(v);
		std::stable_sort(volumes.begin(), volumes.end(), [](Volume* a, Volume* b) { return a->GetPriority() < b->GetPriority(); });

		for (Volume* v : volumes)
		{
			auto profile = v->GetProfile();
			if (profile == nullptr)
				continue;
			float interp = 1.0f;
			if (v->GetMode() == Volume::Mode::Local)
			{
				const float d = v->DistanceTo(cameraPosition);
				if (d < 0.0f)
					continue;   // 콜라이더 없음
				const float blend = v->GetBlendDistance();
				if (d > 0.0f)
				{
					if (d > blend || blend <= 0.0f)
						continue;
					interp = 1.0f - (d * d) / (blend * blend);   // Unity 와 같은 거리 제곱 기준
				}
			}
			const float t = std::clamp(v->GetWeight() * interp, 0.0f, 1.0f);
			if (t > 0.0f)
				ApplyProfile(stack, *profile, t, false);
		}
	}
}
