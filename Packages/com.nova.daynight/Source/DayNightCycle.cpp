#include "pch.h"
#include "DayNightCycle.h"
#include "DayNightState.h"
#include "Light.h"
#include "UnityGUI.h"

namespace
{
	std::vector<DayNightCycle*> s_Instances;
	DayNightCycle* s_Applied = nullptr;      // DayNightState 를 쓰는 인스턴스
	double s_Clock = 0.0;                    // 별 반짝임 (실제 초)

	const char* kNames[DayNightCycle::PhaseCount] = { "Dawn", "Morning", "Day", "Evening", "Sunset", "Night", "MilkyWay" };
	const char* kLabels[DayNightCycle::PhaseCount] = { "Dawn", "Morning", "Day", "Evening", "Sunset", "Night", "Milky Way" };
	// 새벽 4:30 · 아침 6:30 · 낮 10:00 · 저녁 16:00 · 노을 18:00 · 밤 19:30 · 은하수 23:00
	const float kStarts[DayNightCycle::PhaseCount] = { 4.5f, 6.5f, 10.0f, 16.0f, 18.0f, 19.5f, 23.0f };

	float Wrap24(float t) { t = fmodf(t, 24.0f); return t < 0.0f ? t + 24.0f : t; }
	float Smooth(float a, float b, float x) { const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f); return t * t * (3.0f - 2.0f * t); }
	XMFLOAT3 Lerp3(const XMFLOAT3& a, const XMFLOAT3& b, float t) { return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t }; }
	float Lerp1(float a, float b, float t) { return a + (b - a) * t; }

	// 단계 가운데 시각 (그 단계의 모습이 온전히 보이는 때)
	float Center(int i)
	{
		const float a = kStarts[i];
		float b = kStarts[(i + 1) % DayNightCycle::PhaseCount];
		if (b <= a) b += 24.0f;
		return Wrap24(0.5f * (a + b));
	}

	DayNightCycle::Look Mix(const DayNightCycle::Look& a, const DayNightCycle::Look& b, float t)
	{
		DayNightCycle::Look o;
		o.SunColor = Lerp3(a.SunColor, b.SunColor, t);       o.SunIntensity = Lerp1(a.SunIntensity, b.SunIntensity, t);
		o.Ambient = Lerp3(a.Ambient, b.Ambient, t);          o.AmbientIntensity = Lerp1(a.AmbientIntensity, b.AmbientIntensity, t);
		o.Zenith = Lerp3(a.Zenith, b.Zenith, t);             o.Horizon = Lerp3(a.Horizon, b.Horizon, t);
		o.SkyBrightness = Lerp1(a.SkyBrightness, b.SkyBrightness, t);
		o.GradientMix = Lerp1(a.GradientMix, b.GradientMix, t);
		o.Glow = Lerp3(a.Glow, b.Glow, t);                   o.GlowStrength = Lerp1(a.GlowStrength, b.GlowStrength, t);
		o.Stars = Lerp1(a.Stars, b.Stars, t);                o.MilkyWay = Lerp1(a.MilkyWay, b.MilkyWay, t);
		o.Fog = Lerp3(a.Fog, b.Fog, t);
		return o;
	}

	json V3(const XMFLOAT3& v) { return { v.x, v.y, v.z }; }
	void R3(const json& j, const char* k, XMFLOAT3& v)
	{
		if (j.contains(k) && j[k].is_array() && j[k].size() == 3)
			v = { j[k][0].get<float>(), j[k][1].get<float>(), j[k][2].get<float>() };
	}

	bool ColorField(const char* label, XMFLOAT3& c)
	{
		float rgba[4] = { c.x, c.y, c.z, 1.0f };
		if (!UnityGUI::Color(label, rgba, 1))
			return false;
		c = { rgba[0], rgba[1], rgba[2] };
		return true;
	}

	bool IsActiveInHierarchy(GameObject* go)
	{
		for (GameObject* g = go; g; g = g->GetParent())
			if (!g->IsActive())
				return false;
		return true;
	}

	// forward 를 보는 회전 (LH: right = up × forward)
	Quaternion LookAlong(Vec3 f)
	{
		f.Normalize();
		Vec3 up(0, 1, 0);
		if (fabsf(f.Dot(up)) > 0.999f)
			up = Vec3(0, 0, 1);
		Vec3 r = up.Cross(f);
		r.Normalize();
		const Vec3 u = f.Cross(r);
		return Quaternion::CreateFromRotationMatrix(Matrix(r, u, f));
	}
}

const char* DayNightCycle::PhaseName(int phase) { return phase >= 0 && phase < PhaseCount ? kNames[phase] : "?"; }
float DayNightCycle::PhaseStart(int phase) { return phase >= 0 && phase < PhaseCount ? kStarts[phase] : 0.0f; }

void DayNightCycle::ResetLooks(Look l[PhaseCount])
{
	// 새벽: 해가 뜨기 전 — 동쪽 지평이 붉고 별이 사라져 간다
	l[Dawn].SunColor = { 1.0f, 0.55f, 0.38f };   l[Dawn].SunIntensity = 0.25f;
	l[Dawn].Ambient = { 0.55f, 0.52f, 0.68f };    l[Dawn].AmbientIntensity = 0.45f;
	l[Dawn].Zenith = { 0.16f, 0.22f, 0.45f };     l[Dawn].Horizon = { 0.95f, 0.58f, 0.45f };
	l[Dawn].SkyBrightness = 0.4f;                 l[Dawn].GradientMix = 0.75f;
	l[Dawn].Glow = { 1.0f, 0.52f, 0.3f };         l[Dawn].GlowStrength = 0.9f;
	l[Dawn].Stars = 0.2f;                         l[Dawn].MilkyWay = 0.0f;
	l[Dawn].Fog = { 0.75f, 0.62f, 0.62f };
	// 아침: 따뜻하고 낮은 해
	l[Morning].SunColor = { 1.0f, 0.88f, 0.72f }; l[Morning].SunIntensity = 0.85f;
	l[Morning].Ambient = { 0.9f, 0.88f, 0.92f };  l[Morning].AmbientIntensity = 0.85f;
	l[Morning].Zenith = { 0.33f, 0.53f, 0.9f };   l[Morning].Horizon = { 0.85f, 0.85f, 0.88f };
	l[Morning].SkyBrightness = 0.9f;              l[Morning].GradientMix = 0.15f;
	l[Morning].Glow = { 1.0f, 0.8f, 0.6f };       l[Morning].GlowStrength = 0.15f;
	l[Morning].Fog = { 0.95f, 0.93f, 0.9f };
	// 낮: 스카이박스 · 빛 그대로
	l[Day].SunColor = { 1, 1, 1 };                l[Day].SunIntensity = 1.0f;
	l[Day].Ambient = { 1, 1, 1 };                 l[Day].AmbientIntensity = 1.0f;
	l[Day].SkyBrightness = 1.0f;                  l[Day].GradientMix = 0.0f;
	l[Day].GlowStrength = 0.0f;
	// 저녁: 해가 기울며 노랗게
	l[Evening].SunColor = { 1.0f, 0.84f, 0.62f }; l[Evening].SunIntensity = 0.8f;
	l[Evening].Ambient = { 0.92f, 0.86f, 0.8f };  l[Evening].AmbientIntensity = 0.8f;
	l[Evening].Zenith = { 0.3f, 0.45f, 0.8f };    l[Evening].Horizon = { 0.95f, 0.8f, 0.65f };
	l[Evening].SkyBrightness = 0.85f;             l[Evening].GradientMix = 0.2f;
	l[Evening].Glow = { 1.0f, 0.7f, 0.4f };       l[Evening].GlowStrength = 0.3f;
	l[Evening].Fog = { 0.98f, 0.9f, 0.8f };
	// 노을: 해가 진 서쪽 하늘이 주황 · 붉게, 위는 보라
	l[Sunset].SunColor = { 1.0f, 0.42f, 0.18f };  l[Sunset].SunIntensity = 0.35f;
	l[Sunset].Ambient = { 0.78f, 0.52f, 0.48f };  l[Sunset].AmbientIntensity = 0.45f;
	l[Sunset].Zenith = { 0.22f, 0.2f, 0.42f };    l[Sunset].Horizon = { 1.0f, 0.45f, 0.2f };
	l[Sunset].SkyBrightness = 0.45f;              l[Sunset].GradientMix = 0.8f;
	l[Sunset].Glow = { 1.0f, 0.38f, 0.12f };      l[Sunset].GlowStrength = 1.3f;
	l[Sunset].Stars = 0.08f;
	l[Sunset].Fog = { 0.9f, 0.55f, 0.42f };
	// 밤: 푸른 달빛, 별
	l[Night].SunColor = { 0.55f, 0.66f, 1.0f };   l[Night].SunIntensity = 0.14f;
	l[Night].Ambient = { 0.28f, 0.33f, 0.55f };   l[Night].AmbientIntensity = 0.18f;
	l[Night].Zenith = { 0.012f, 0.02f, 0.06f };   l[Night].Horizon = { 0.04f, 0.06f, 0.12f };
	l[Night].SkyBrightness = 0.06f;               l[Night].GradientMix = 0.95f;
	l[Night].GlowStrength = 0.0f;
	l[Night].Stars = 0.8f;                        l[Night].MilkyWay = 0.3f;
	l[Night].Fog = { 0.15f, 0.18f, 0.28f };
	// 은하수: 가장 어두운 밤 — 별과 은하수가 가장 밝다
	l[MilkyWay].SunColor = { 0.5f, 0.6f, 1.0f };  l[MilkyWay].SunIntensity = 0.09f;
	l[MilkyWay].Ambient = { 0.2f, 0.24f, 0.42f }; l[MilkyWay].AmbientIntensity = 0.13f;
	l[MilkyWay].Zenith = { 0.005f, 0.01f, 0.03f }; l[MilkyWay].Horizon = { 0.02f, 0.03f, 0.07f };
	l[MilkyWay].SkyBrightness = 0.03f;            l[MilkyWay].GradientMix = 1.0f;
	l[MilkyWay].GlowStrength = 0.0f;
	l[MilkyWay].Stars = 1.0f;                     l[MilkyWay].MilkyWay = 1.0f;
	l[MilkyWay].Fog = { 0.1f, 0.12f, 0.2f };
}

DayNightCycle::DayNightCycle()
{
	m_InspectorTitleName = "Day Night Cycle";
	ResetLooks(Looks);
	s_Instances.push_back(this);
}

DayNightCycle::~DayNightCycle()
{
	s_Instances.erase(std::remove(s_Instances.begin(), s_Instances.end(), this), s_Instances.end());
	if (s_Applied == this)
	{
		s_Applied = nullptr;
		DayNightState::Reset();
	}
}

void DayNightCycle::OnDestroy()
{
	if (s_Applied == this)
	{
		s_Applied = nullptr;
		DayNightState::Reset();
	}
}

DayNightCycle* DayNightCycle::Active()
{
	Scene* scene = SceneManager::GetI()->GetCurrentScene();
	for (DayNightCycle* d : s_Instances)
		if (d->m_pGameObject && d->IsEnabled() && IsActiveInHierarchy(d->m_pGameObject) && scene && scene->FindByFileID(d->m_pGameObject->GetFileID()) == d->m_pGameObject)
			return d;
	return nullptr;
}

int DayNightCycle::CurrentPhase() const
{
	const float t = Wrap24(TimeOfDay);
	for (int i = PhaseCount - 1; i >= 0; --i)
		if (t >= kStarts[i])
			return i;
	return MilkyWay;   // 0 시 ~ 4:30 = 은하수 (23 시에 시작)
}

Vec3 DayNightCycle::SunDirection() const
{
	// 시간각: 정오 0, 6 시 = -90° (동쪽 지평), 18 시 = +90° (서쪽 지평). 정오에는 남쪽 (-Z) 으로 MaxElevation 만큼
	const float h = (Wrap24(TimeOfDay) - 12.0f) / 24.0f * XM_2PI;
	const float el = XMConvertToRadians(std::clamp(MaxElevation, 1.0f, 90.0f));
	Vec3 d(-sinf(h), cosf(h) * sinf(el), -cosf(h) * cosf(el));
	const float az = XMConvertToRadians(SunAzimuth);
	return Vec3(d.x * cosf(az) + d.z * sinf(az), d.y, -d.x * sinf(az) + d.z * cosf(az));
}

float DayNightCycle::SunElevation() const
{
	return XMConvertToDegrees(asinf(std::clamp(SunDirection().y, -1.0f, 1.0f)));
}

GameObject* DayNightCycle::LightObject() const
{
	Scene* scene = SceneManager::GetI()->GetCurrentScene();
	if (scene == nullptr)
		return nullptr;
	if (SunLight != 0)
		if (GameObject* g = scene->FindByFileID(SunLight))
			return g;
	for (GameObject* g : scene->GetAllGameObjects())
		if (Light* l = g->GetComponent<Light>(); l && l->GetLightType() == LightType::Directional && IsActiveInHierarchy(g))
			return g;
	return nullptr;
}

void DayNightCycle::LastUpdate()
{
	if (m_pGameObject == nullptr)
		return;
	if (Active() != this)
	{
		if (s_Applied == this)
		{
			s_Applied = nullptr;
			DayNightState::Reset();
		}
		return;
	}
	// 시계: Play 중에만 흐른다 (편집 중에는 Time Of Day 를 그대로 보여 준다)
	const float dt = (std::min)((float)DT, 0.25f);
	s_Clock += dt;
	if (Application::IsPlaying() && !Paused && DayLengthMinutes > 0.0f && dt > 0.0f)
		TimeOfDay = Wrap24(TimeOfDay + dt * 24.0f / (DayLengthMinutes * 60.0f));
	Apply();
}

void DayNightCycle::Apply()
{
	// 단계 가운데 사이를 부드럽게 섞는다
	const float t = Wrap24(TimeOfDay);
	int a = MilkyWay;
	float best = -1.0f;
	for (int i = 0; i < PhaseCount; ++i)
	{
		float since = t - Center(i);
		if (since < 0.0f) since += 24.0f;
		if (best < 0.0f || since < best) { best = since; a = i; }
	}
	const int b = (a + 1) % PhaseCount;
	float span = Center(b) - Center(a);
	if (span <= 0.0f) span += 24.0f;
	m_Blended = Mix(Looks[a], Looks[b], Smooth(0.0f, 1.0f, best / span));
	const Look& L = m_Blended;

	// 해 · 달: 해가 지평 위면 해, 아래면 반대편 달 — 지평 가까이에서 세기가 0 이 되어 바뀔 때 튀지 않는다
	const Vec3 sun = SunDirection();
	const Vec3 lightFrom = sun.y >= 0.0f ? sun : -sun;
	const float vis = Smooth(0.0f, 0.1f, lightFrom.y);
	if (GameObject* lo = LightObject())
	{
		const Quaternion want = LookAlong(-lightFrom);
		const Quaternion have = lo->GetTransform()->GetRotation();
		if (fabsf(want.Dot(have)) < 0.9999999f)
			lo->GetTransform()->SetRotation(want);
	}

	DayNightState& s = DayNightState::Get();
	s.Enabled = true;
	s.SunIntensity = (std::max)(0.0f, L.SunIntensity) * vis;
	s.SunTint = L.SunColor;
	s.AmbientIntensity = (std::max)(0.0f, L.AmbientIntensity);
	s.AmbientTint = L.Ambient;
	s.SkyBrightness = (std::max)(0.0f, L.SkyBrightness);
	s.SkyTint = { 1, 1, 1 };
	s.FogTint = L.Fog;
	s.SunDirection = XMFLOAT3(sun.x, sun.y, sun.z);
	s.Zenith = XMFLOAT4(L.Zenith.x, L.Zenith.y, L.Zenith.z, std::clamp(L.GradientMix, 0.0f, 1.0f));
	s.Horizon = L.Horizon;
	s.Glow = XMFLOAT4(L.Glow.x, L.Glow.y, L.Glow.z, (std::max)(0.0f, L.GlowStrength));
	const float sunDisk = Smooth(-0.03f, 0.03f, sun.y);
	s.SunDisk = XMFLOAT4(L.SunColor.x, L.SunColor.y, L.SunColor.z, sunDisk);
	s.Stars = (std::max)(0.0f, L.Stars * StarBrightness);
	s.MilkyWay = (std::max)(0.0f, L.MilkyWay * MilkyWayBrightness);
	s.Moon = Smooth(-0.03f, 0.03f, -sun.y);
	s.Time = (float)fmod(s_Clock, 3600.0);
	s.TimeOfDay = t;
	s.ProbeRefreshMinutes = (std::max)(0.0f, ProbeRefreshMinutes);
	s_Applied = this;
}

void DayNightCycle::OnInspectorGUI()
{
	char buf[96];
	if (UnityGUI::Slider("Time Of Day", &TimeOfDay, 0.0f, 24.0f))
		TimeOfDay = Wrap24(TimeOfDay);
	const int phase = CurrentPhase();
	const int hh = (int)Wrap24(TimeOfDay), mm = (int)((Wrap24(TimeOfDay) - hh) * 60.0f);
	snprintf(buf, sizeof(buf), "%02d:%02d  %s  (sun %.0f deg)", hh, mm, kLabels[phase], SunElevation());
	UnityGUI::ValueLabel("Now", buf);
	// 단계로 바로 가기
	const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 3.0f) / 4.0f;
	for (int i = 0; i < PhaseCount; ++i)
	{
		if (i % 4 != 0)
			ImGui::SameLine();
		const bool on = i == phase;
		if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
		if (ImGui::Button(kLabels[i], ImVec2(w, 0)))
			TimeOfDay = Center(i);
		if (on) ImGui::PopStyleColor();
	}
	if (UnityGUI::Float("Day Length (min)", &DayLengthMinutes)) DayLengthMinutes = (std::max)(0.0f, DayLengthMinutes);
	UnityGUI::Toggle("Paused", &Paused);
	UnityGUI::Label("Sun", 0, true);
	UnityGUI::GameObjectField("Sun Light", &SunLight);
	if (SunLight == 0)
	{
		GameObject* lo = LightObject();
		UnityGUI::ValueLabel("Using", lo ? lo->GetName().c_str() : "(no Directional Light)");
	}
	UnityGUI::Slider("Sun Azimuth", &SunAzimuth, 0.0f, 360.0f);
	UnityGUI::Slider("Max Elevation", &MaxElevation, 5.0f, 90.0f);
	UnityGUI::Label("Night Sky", 0, true);
	UnityGUI::Slider("Stars", &StarBrightness, 0.0f, 3.0f);
	UnityGUI::Slider("Milky Way", &MilkyWayBrightness, 0.0f, 3.0f);
	UnityGUI::Label("Reflections", 0, true);
	if (UnityGUI::Float("Probe Refresh (game min)", &ProbeRefreshMinutes)) ProbeRefreshMinutes = (std::max)(0.0f, ProbeRefreshMinutes);
	if (ProbeRefreshMinutes > 0.0f)
		UnityGUI::HelpBox("Reflection Probes are captured again whenever the time of day moves this much (baked probes too, at run time - the baked file is kept), so reflections show the night sky and lit lamps.", false);
	UnityGUI::Label("Looks", 0, true);
	for (int i = 0; i < PhaseCount; ++i)
	{
		ImGui::PushID(i);
		snprintf(buf, sizeof(buf), "%s  (from %02d:%02d)", kLabels[i], (int)kStarts[i], (int)((kStarts[i] - (int)kStarts[i]) * 60.0f));
		if (UnityGUI::FoldoutPlain(buf, 0, false))
		{
			Look& l = Looks[i];
			ColorField("Sun Color", l.SunColor);
			UnityGUI::Slider("Sun Intensity", &l.SunIntensity, 0.0f, 2.0f, 1);
			ColorField("Ambient", l.Ambient);
			UnityGUI::Slider("Ambient Intensity", &l.AmbientIntensity, 0.0f, 2.0f, 1);
			UnityGUI::Slider("Skybox Brightness", &l.SkyBrightness, 0.0f, 1.5f, 1);
			UnityGUI::Slider("Gradient", &l.GradientMix, 0.0f, 1.0f, 1);
			ColorField("Zenith", l.Zenith);
			ColorField("Horizon", l.Horizon);
			ColorField("Glow", l.Glow);
			UnityGUI::Slider("Glow Strength", &l.GlowStrength, 0.0f, 3.0f, 1);
			UnityGUI::Slider("Stars", &l.Stars, 0.0f, 2.0f, 1);
			UnityGUI::Slider("Milky Way", &l.MilkyWay, 0.0f, 2.0f, 1);
			ColorField("Fog", l.Fog);
		}
		ImGui::PopID();
	}
	if (UnityGUI::CenterButton("Reset Looks"))
		ResetLooks(Looks);
	if (LightObject() == nullptr)
		UnityGUI::HelpBox("Add a Directional Light to the scene: it becomes the sun (and the moon at night).");
}

void DayNightCycle::RemapFileIDs(const std::unordered_map<uint64, uint64>& map)
{
	auto it = map.find(SunLight);
	if (it != map.end())
		SunLight = it->second;
}

GENERATE_COMPONENT_FUNC_TOJSON(DayNightCycle)
{
	json j;
	j["type"] = "DayNightCycle";
	j["enabled"] = m_Enabled;
	j["timeOfDay"] = TimeOfDay;
	j["dayLengthMinutes"] = DayLengthMinutes;
	j["paused"] = Paused;
	j["sunAzimuth"] = SunAzimuth;
	j["maxElevation"] = MaxElevation;
	j["sunLight"] = SunLight;
	j["starBrightness"] = StarBrightness;
	j["milkyWayBrightness"] = MilkyWayBrightness;
	j["probeRefreshMinutes"] = ProbeRefreshMinutes;
	json looks = json::object();
	for (int i = 0; i < PhaseCount; ++i)
	{
		const Look& l = Looks[i];
		looks[kNames[i]] = { { "sunColor", V3(l.SunColor) }, { "sunIntensity", l.SunIntensity }, { "ambient", V3(l.Ambient) },
			{ "ambientIntensity", l.AmbientIntensity }, { "zenith", V3(l.Zenith) }, { "horizon", V3(l.Horizon) }, { "skyBrightness", l.SkyBrightness },
			{ "gradient", l.GradientMix }, { "glow", V3(l.Glow) }, { "glowStrength", l.GlowStrength }, { "stars", l.Stars }, { "milkyWay", l.MilkyWay },
			{ "fog", V3(l.Fog) } };
	}
	j["looks"] = looks;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(DayNightCycle)
{
	m_Enabled = j.value("enabled", true);
	TimeOfDay = Wrap24(j.value("timeOfDay", 9.0f));
	DayLengthMinutes = (std::max)(0.0f, j.value("dayLengthMinutes", 24.0f));
	Paused = j.value("paused", false);
	SunAzimuth = j.value("sunAzimuth", 0.0f);
	MaxElevation = j.value("maxElevation", 60.0f);
	SunLight = j.value("sunLight", (uint64)0);
	StarBrightness = j.value("starBrightness", 1.0f);
	MilkyWayBrightness = j.value("milkyWayBrightness", 1.0f);
	ProbeRefreshMinutes = (std::max)(0.0f, j.value("probeRefreshMinutes", 30.0f));
	ResetLooks(Looks);
	if (j.contains("looks") && j["looks"].is_object())
		for (int i = 0; i < PhaseCount; ++i)
		{
			if (!j["looks"].contains(kNames[i]))
				continue;
			const json& o = j["looks"][kNames[i]];
			Look& l = Looks[i];
			R3(o, "sunColor", l.SunColor);   l.SunIntensity = o.value("sunIntensity", l.SunIntensity);
			R3(o, "ambient", l.Ambient);     l.AmbientIntensity = o.value("ambientIntensity", l.AmbientIntensity);
			R3(o, "zenith", l.Zenith);       R3(o, "horizon", l.Horizon);
			l.SkyBrightness = o.value("skyBrightness", l.SkyBrightness);
			l.GradientMix = o.value("gradient", l.GradientMix);
			R3(o, "glow", l.Glow);           l.GlowStrength = o.value("glowStrength", l.GlowStrength);
			l.Stars = o.value("stars", l.Stars);
			l.MilkyWay = o.value("milkyWay", l.MilkyWay);
			R3(o, "fog", l.Fog);
		}
}
