#include "pch.h"
#include "Light2D.h"
#include "SpriteRenderer.h"
#include "Physics2DComponents.h"
#include "UnityGUI.h"
#include "SceneViewOverlay.h"
#include "SelectionManager.h"

namespace
{
	std::vector<Light2D*> s_Lights;
	std::vector<ShadowCaster2D*> s_Casters;

	bool ActiveInHierarchy(GameObject* g)
	{
		if (g == nullptr)
			return false;
		for (; g != nullptr; g = g->GetParent())
			if (!g->IsActive())
				return false;
		return true;
	}

	bool IsSelected(GameObject* g)
	{
		GameObject* sel = SelectionManager::GetSelectedGameObject();
		for (; g != nullptr; g = g->GetParent())
			if (g == sel)
				return true;
		return false;
	}

	void Circle(const Vec3& c, float r, ImU32 color, float fromDeg = 0.0f, float toDeg = 360.0f, const Vec3& right = Vec3(1, 0, 0), const Vec3& up = Vec3(0, 1, 0))
	{
		if (r <= 0.0f)
			return;
		const int n = (std::max)(8, (int)((toDeg - fromDeg) / 6.0f));
		Vec3 prev;
		for (int i = 0; i <= n; ++i)
		{
			const float a = XMConvertToRadians(fromDeg + (toDeg - fromDeg) * i / n);
			const Vec3 p = c + right * (cosf(a) * r) + up * (sinf(a) * r);
			if (i > 0)
				SceneViewOverlay::DrawLine(XMFLOAT3(prev.x, prev.y, prev.z), XMFLOAT3(p.x, p.y, p.z), color, 1.5f);
			prev = p;
		}
	}

	// 고리를 반시계로 (넓이 부호)
	void MakeCounterClockwise(std::vector<Vec2>& loop)
	{
		float area = 0.0f;
		for (size_t i = 0; i < loop.size(); ++i)
		{
			const Vec2& a = loop[i];
			const Vec2& b = loop[(i + 1) % loop.size()];
			area += a.x * b.y - b.x * a.y;
		}
		if (area < 0.0f)
			std::reverse(loop.begin(), loop.end());
	}
}

// ------------------------------------------------------------------ Light 2D
Light2D::Light2D()
{
	m_InspectorTitleName = "Light 2D";
	s_Lights.push_back(this);
}

Light2D::~Light2D()
{
	s_Lights.erase(std::remove(s_Lights.begin(), s_Lights.end(), this), s_Lights.end());
}

const std::vector<Light2D*>& Light2D::All() { return s_Lights; }

bool Light2D::ActiveAndEnabled() const { return m_Enabled && ActiveInHierarchy(m_pGameObject); }

void Light2D::OnDrawGizmos()
{
	// Unity: 고른 빛의 안 · 바깥 반지름 (노랑), 원뿔이면 가장자리 선
	if (m_pGameObject == nullptr || !SceneViewOverlay::IsActive() || LightType != Type::Point || !IsSelected(m_pGameObject))
		return;
	const Matrix m = m_pGameObject->GetTransform()->GetWorldMatrix();
	const Vec3 c = m.Translation();
	Vec3 up = Vec3::TransformNormal(Vec3(0, 1, 0), m), right = Vec3::TransformNormal(Vec3(1, 0, 0), m);
	up.Normalize();
	right.Normalize();
	const ImU32 outer = IM_COL32(255, 230, 120, 230), inner = IM_COL32(255, 230, 120, 140);
	const float half = std::clamp(OuterAngle, 0.0f, 360.0f) * 0.5f;
	if (half >= 179.9f)
	{
		Circle(c, OuterRadius, outer, 0.0f, 360.0f, right, up);
		Circle(c, InnerRadius, inner, 0.0f, 360.0f, right, up);
		return;
	}
	// 위 (90 도) 를 가운데로
	Circle(c, OuterRadius, outer, 90.0f - half, 90.0f + half, right, up);
	Circle(c, InnerRadius, inner, 90.0f - half, 90.0f + half, right, up);
	for (float a : { 90.0f - half, 90.0f + half })
	{
		const float r = XMConvertToRadians(a);
		const Vec3 e = c + right * (cosf(r) * OuterRadius) + up * (sinf(r) * OuterRadius);
		SceneViewOverlay::DrawLine(XMFLOAT3(c.x, c.y, c.z), XMFLOAT3(e.x, e.y, e.z), outer, 1.5f);
	}
}

void Light2D::OnInspectorGUI()
{
	static const char* kTypes[] = { "Global", "Spot" };
	int type = (int)LightType;
	if (UnityGUI::Dropdown("Light Type", &type, kTypes, 2))
		LightType = (Type)type;
	UnityGUI::Color("Color", Color);
	UnityGUI::Float("Intensity", &Intensity);
	Intensity = (std::max)(0.0f, Intensity);
	if (LightType == Type::Point)
	{
		UnityGUI::MinMaxSlider("Radius", &InnerRadius, &OuterRadius, 0.0f, (std::max)(10.0f, OuterRadius * 1.5f));
		UnityGUI::Float("Inner Radius", &InnerRadius, 1);
		UnityGUI::Float("Outer Radius", &OuterRadius, 1);
		OuterRadius = (std::max)(0.01f, OuterRadius);
		InnerRadius = std::clamp(InnerRadius, 0.0f, OuterRadius);
		UnityGUI::MinMaxSlider("Inner / Outer Spot Angle", &InnerAngle, &OuterAngle, 0.0f, 360.0f);
		OuterAngle = std::clamp(OuterAngle, 0.0f, 360.0f);
		InnerAngle = std::clamp(InnerAngle, 0.0f, OuterAngle);
		UnityGUI::Slider("Falloff Strength", &Falloff, 0.0f, 1.0f);
		if (UnityGUI::Foldout("Shadows"))
		{
			UnityGUI::Toggle("Shadows", &Shadows, 1);
			if (Shadows)
				UnityGUI::Slider("Strength", &ShadowStrength, 0.0f, 1.0f, 1);
		}
		if (UnityGUI::Foldout("Normal Maps"))
		{
			UnityGUI::Float("Distance", &NormalMapDistance, 1);
			NormalMapDistance = (std::max)(0.01f, NormalMapDistance);
		}
	}
}

GENERATE_COMPONENT_FUNC_TOJSON(Light2D)
{
	json j;
	SERIALIZE_TYPE(j, Light2D);
	j["enabled"] = m_Enabled;
	j["lightType"] = (int)LightType;
	j["color"] = { Color[0], Color[1], Color[2] };
	j["intensity"] = Intensity;
	j["innerRadius"] = InnerRadius;
	j["outerRadius"] = OuterRadius;
	j["innerAngle"] = InnerAngle;
	j["outerAngle"] = OuterAngle;
	j["falloff"] = Falloff;
	j["shadows"] = Shadows;
	j["shadowStrength"] = ShadowStrength;
	j["normalMapDistance"] = NormalMapDistance;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(Light2D)
{
	m_Enabled = j.value("enabled", true);
	LightType = (Type)std::clamp(j.value("lightType", 1), 0, 1);
	if (j.contains("color") && j["color"].is_array() && j["color"].size() >= 3)
		for (int i = 0; i < 3; ++i) Color[i] = j["color"][i].get<float>();
	Intensity = j.value("intensity", 1.0f);
	InnerRadius = j.value("innerRadius", 0.0f);
	OuterRadius = j.value("outerRadius", 3.0f);
	InnerAngle = j.value("innerAngle", 360.0f);
	OuterAngle = j.value("outerAngle", 360.0f);
	Falloff = j.value("falloff", 0.5f);
	Shadows = j.value("shadows", false);
	ShadowStrength = j.value("shadowStrength", 0.75f);
	NormalMapDistance = j.value("normalMapDistance", 3.0f);
}

// ------------------------------------------------------------------ Shadow Caster 2D
ShadowCaster2D::ShadowCaster2D()
{
	m_InspectorTitleName = "Shadow Caster 2D";
	s_Casters.push_back(this);
}

ShadowCaster2D::~ShadowCaster2D()
{
	s_Casters.erase(std::remove(s_Casters.begin(), s_Casters.end(), this), s_Casters.end());
}

const std::vector<ShadowCaster2D*>& ShadowCaster2D::All() { return s_Casters; }

bool ShadowCaster2D::ActiveAndEnabled() const { return m_Enabled && CastsShadows && ActiveInHierarchy(m_pGameObject); }

bool ShadowCaster2D::WorldOutline(std::vector<std::vector<Vec2>>& loops, float& z) const
{
	loops.clear();
	if (m_pGameObject == nullptr)
		return false;
	std::vector<std::vector<Vec2>> local;
	// 2D 콜라이더 (켜진 것 하나 — Box · Circle · Capsule · Polygon · 타일맵 콜라이더 …)
	for (const auto& c : m_pGameObject->GetComponents())
		if (auto* col = dynamic_cast<Collider2D*>(c.get()); col && col->IsEnabled() && dynamic_cast<EdgeCollider2D*>(col) == nullptr)
		{
			col->Outline(local);
			if (!local.empty())
				break;
		}
	// 없으면 스프라이트 사각형
	if (local.empty())
		if (auto* sr = m_pGameObject->GetComponent<SpriteRenderer>())
		{
			Vec3 bmin, bmax;
			if (sr->SpriteLocalBounds(bmin, bmax))
				local.push_back({ Vec2(bmin.x, bmin.y), Vec2(bmax.x, bmin.y), Vec2(bmax.x, bmax.y), Vec2(bmin.x, bmax.y) });
		}
	if (local.empty())
		return false;
	const Matrix m = m_pGameObject->GetTransform()->GetWorldMatrix();
	z = m.Translation().z;
	for (auto& loop : local)
	{
		if (loop.size() < 3)
			continue;
		std::vector<Vec2> w;
		w.reserve(loop.size());
		for (const Vec2& p : loop)
		{
			const Vec3 v = Vec3::Transform(Vec3(p.x, p.y, 0.0f), m);
			w.push_back(Vec2(v.x, v.y));
		}
		MakeCounterClockwise(w);
		loops.push_back(std::move(w));
	}
	return !loops.empty();
}

void ShadowCaster2D::OnDrawGizmos()
{
	if (m_pGameObject == nullptr || !SceneViewOverlay::IsActive() || !IsSelected(m_pGameObject))
		return;
	std::vector<std::vector<Vec2>> loops;
	float z = 0.0f;
	if (!WorldOutline(loops, z))
		return;
	for (const auto& loop : loops)
		for (size_t i = 0; i < loop.size(); ++i)
		{
			const Vec2& a = loop[i];
			const Vec2& b = loop[(i + 1) % loop.size()];
			SceneViewOverlay::DrawLine(XMFLOAT3(a.x, a.y, z), XMFLOAT3(b.x, b.y, z), IM_COL32(255, 255, 255, 200), 1.5f);
		}
}

void ShadowCaster2D::OnInspectorGUI()
{
	UnityGUI::Toggle("Casts Shadows", &CastsShadows);
	UnityGUI::Toggle("Self Shadows", &SelfShadows);
	UnityGUI::ValueLabel("Shape", "2D Collider outline (or Sprite rectangle)");
}

GENERATE_COMPONENT_FUNC_TOJSON(ShadowCaster2D)
{
	json j;
	SERIALIZE_TYPE(j, ShadowCaster2D);
	j["enabled"] = m_Enabled;
	j["castsShadows"] = CastsShadows;
	j["selfShadows"] = SelfShadows;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(ShadowCaster2D)
{
	m_Enabled = j.value("enabled", true);
	CastsShadows = j.value("castsShadows", true);
	SelfShadows = j.value("selfShadows", false);
}
