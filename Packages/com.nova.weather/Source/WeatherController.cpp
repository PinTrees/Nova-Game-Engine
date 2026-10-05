#include "pch.h"
#include "WeatherController.h"
#include "WeatherFx.h"
#include "WeatherState.h"
#include "VisualEffect.h"
#include "LineRenderer.h"
#include "AudioSource.h"
#include "AudioClip.h"
#include "PackageManager.h"
#include "UnityGUI.h"

namespace
{
	std::vector<WeatherController*> s_All;

	float Smooth01(float t) { t = std::clamp(t, 0.0f, 1.0f); return t * t * (3.0f - 2.0f * t); }
	float Mix(float a, float b, float t) { return a + (b - a) * t; }
	XMFLOAT3 Mix3(const XMFLOAT3& a, const XMFLOAT3& b, float t) { return { Mix(a.x, b.x, t), Mix(a.y, b.y, t), Mix(a.z, b.z, t) }; }

	// 번개 한 번의 깜빡임: 세 번 번쩍 (0 · 0.14 · 0.25 초), 하나마다 빠르게 사그라든다
	float Flicker(float age)
	{
		constexpr float times[3] = { 0.0f, 0.14f, 0.25f };
		constexpr float peaks[3] = { 1.0f, 0.65f, 0.85f };
		float v = 0.0f;
		for (int i = 0; i < 3; ++i)
			if (age >= times[i])
				v = (std::max)(v, peaks[i] * expf(-(age - times[i]) / 0.05f));
		return v;
	}

	// 돕는 오브젝트 지우기 (장면 밖 — 컴포넌트의 OnDestroy 로 GPU · 소리를 놓은 뒤)
	void DeleteHelper(GameObject*& go)
	{
		if (go == nullptr)
			return;
		go->OnDestroy();
		delete go;
		go = nullptr;
	}

	std::string AudioPath(const char* file)
	{
		// 편집기 · PC 빌드: 패키지 폴더. 안드로이드: 게임 데이터의 Packages\<이름>\Resources (BuildPipeline::CollectGameFiles)
		if (const PackageInfo* p = PackageManager::Find("com.nova.weather"))
			return wstring_to_string(p->Folder) + "\\Resources\\Audio\\" + file;
		return std::string("Packages\\com.nova.weather\\Resources\\Audio\\") + file;
	}
}

// 웅덩이 목표: 젖음 × 비 (가는 비는 조금만)
float WeatherController::PuddleTarget(const WeatherParams& p)
{
	return std::clamp(p.Wetness * std::clamp(p.Rain * 1.6f, 0.0f, 1.0f), 0.0f, 1.0f);
}

WeatherController::WeatherController()
{
	m_InspectorTitleName = "Weather Controller";
	m_Rng ^= (unsigned)(uintptr_t)this;
	s_All.push_back(this);
}

WeatherController::~WeatherController()
{
	ReleaseHelpers();
	s_All.erase(std::remove(s_All.begin(), s_All.end(), this), s_All.end());
	if (s_All.empty())
		WeatherState::Reset();
}

WeatherController* WeatherController::Active()
{
	// 장면에 든 것만 (Undo · 복사본처럼 장면 밖에 있는 인스턴스는 건너뛴다), 켜진 것 먼저
	Scene* scene = SceneManager::GetI()->GetCurrentScene();
	if (scene == nullptr)
		return nullptr;
	WeatherController* any = nullptr;
	for (WeatherController* w : s_All)
	{
		GameObject* go = w->GetGameObject();
		if (go == nullptr || scene->FindByFileID(go->GetFileID()) != go)
			continue;
		if (w->IsEnabled() && go->IsActive())
			return w;
		if (any == nullptr)
			any = w;
	}
	return any;
}

int WeatherController::InstanceCount()
{
	return (int)s_All.size();
}

void WeatherController::OnDestroy()
{
	ReleaseHelpers();
	WeatherState::Reset();
}

void WeatherController::ReleaseHelpers()
{
	m_Fx = nullptr;
	DeleteHelper(m_FxObject);
	for (Bolt& b : m_Bolts)
		DeleteHelper(b.Object);
	m_Bolts.clear();
	m_RainLight = m_RainHeavy = m_Wind = m_OneShot = nullptr;
	DeleteHelper(m_AudioObject);
	m_Thunder.clear();
	m_HaveViewer = false;
}

float WeatherController::Random01()
{
	m_Rng ^= m_Rng << 13;
	m_Rng ^= m_Rng >> 17;
	m_Rng ^= m_Rng << 5;
	return (m_Rng & 0xFFFFFF) / 16777216.0f;
}

// ------------------------------------------------------------------ 프로필
bool WeatherController::SetProfile(const std::string& nameOrPath, float seconds, std::string* error)
{
	WeatherParams p;
	std::string e;
	m_TargetName = nameOrPath;
	if (!WeatherProfiles::Find(nameOrPath, p, e))
	{
		m_Error = e;
		if (error) *error = e;
		return false;
	}
	m_Error.clear();
	Profile = nameOrPath;
	m_From = m_Current;
	m_To = p;
	m_Elapsed = 0.0f;
	m_Duration = seconds < 0.0f ? (std::max)(0.0f, TransitionTime) : seconds;
	if (!m_Started || m_Duration <= 0.0f)
	{
		m_Current = m_To;
		m_Duration = 0.0f;
		m_SurfaceWet = m_To.Rain > 0.01f ? m_To.Wetness : 0.0f;   // 바로 = 표면도 바로 (Advance 의 목표와 같게)
		m_Puddles = PuddleTarget(m_To);
		m_Snow = m_To.Snow > 0.01f ? m_To.SnowCover : 0.0f;
	}
	m_Started = true;
	return true;
}

// ------------------------------------------------------------------ 프레임
Vec3 WeatherController::ViewerPosition(Vec3* forward) const
{
	const WeatherState& s = WeatherState::Get();
	auto fresh = [&](uint64 frame) { return frame != 0 && s.Frame - frame <= 3; };
	auto pick = [forward](const XMFLOAT3& p, const XMFLOAT3& f) {
		if (forward) *forward = Vec3(f.x, f.y, f.z);
		return Vec3(p.x, p.y, p.z);
	};
	// Play 중이면 게임 카메라, 편집 중이면 Scene 화면 (안 보이면 게임 화면), 둘 다 없으면 이 오브젝트 자리
	if (Application::IsPlaying() && fresh(s.GameViewFrame))
		return pick(s.GameViewPosition, s.GameViewForward);
	if (fresh(s.SceneViewFrame))
		return pick(s.SceneViewPosition, s.SceneViewForward);
	if (fresh(s.GameViewFrame))
		return pick(s.GameViewPosition, s.GameViewForward);
	if (forward) *forward = Vec3(0, 0, 1);
	return m_pGameObject ? m_pGameObject->GetTransform()->GetPosition() : Vec3::Zero;
}

void WeatherController::LastUpdate()
{
	const bool on = IsEnabled() && m_pGameObject != nullptr && [this] {
		for (GameObject* g = m_pGameObject; g != nullptr; g = g->GetParent())
			if (!g->IsActive())
				return false;
		return true;
	}();
	if (!on)
	{
		if (m_WasOn)
		{
			ReleaseHelpers();
			WeatherState::Reset();
		}
		m_WasOn = false;
		return;
	}
	m_WasOn = true;
	const bool paused = Application::IsPlaying() && !Application::ShouldUpdateGame();
	const float dt = paused ? 0.0f : std::clamp((float)DT, 0.0f, 0.25f);
	Advance(dt, true);
}

void WeatherController::Advance(float dt, bool running)
{
	if (!m_Started || Profile != m_TargetName)
		SetProfile(Profile, m_Started ? -1.0f : 0.0f);

	// 전환 (부드럽게 시작하고 끝난다)
	if (m_Duration > 0.0f && m_Elapsed < m_Duration)
	{
		m_Elapsed = (std::min)(m_Elapsed + dt, m_Duration);
		m_Current = WeatherParams::Lerp(m_From, m_To, Smooth01(m_Elapsed / m_Duration));
	}
	else
		m_Current = m_To;
	m_Time += dt;

	// 표면: 비가 오면 젖고 (센 비면 7 초쯤), 웅덩이는 더 천천히 찬다. 그치면 천천히 마른다 (1 분 · 웅덩이 2 분)
	{
		const float rain = std::clamp(m_Current.Rain, 0.0f, 1.0f);
		const float wetTarget = rain > 0.01f ? m_Current.Wetness : 0.0f;
		if (m_SurfaceWet < wetTarget) m_SurfaceWet = (std::min)(wetTarget, m_SurfaceWet + dt * (0.02f + 0.13f * rain));
		else m_SurfaceWet = (std::max)(wetTarget, m_SurfaceWet - dt * 0.017f);
		const float puddleTarget = PuddleTarget(m_Current);
		if (m_Puddles < puddleTarget) m_Puddles = (std::min)(puddleTarget, m_Puddles + dt * 0.045f * rain);
		else m_Puddles = (std::max)(puddleTarget, m_Puddles - dt * 0.008f);
		// 눈: 내리는 만큼 쌓이고 (눈보라면 40 초쯤), 그치면 아주 천천히 녹는다 (비가 오면 빨리)
		const float snowing = std::clamp(m_Current.Snow, 0.0f, 1.0f);
		const float snowTarget = snowing > 0.01f ? m_Current.SnowCover : 0.0f;
		if (m_Snow < snowTarget) m_Snow = (std::min)(snowTarget, m_Snow + dt * (0.005f + 0.02f * snowing));
		else m_Snow = (std::max)(snowTarget, m_Snow - dt * (0.004f + 0.03f * rain));
	}

	// 돌풍: 느린 물결 셋을 겹친다
	const float t = (float)m_Time;
	const float gust = (std::max)(0.2f, 1.0f + m_Current.Gust * (0.55f * sinf(0.63f * t) + 0.3f * sinf(1.71f * t + 1.3f) + 0.15f * sinf(4.3f * t + 2.1f)));
	const float yaw = XMConvertToRadians(m_Current.WindDirection);
	const Vec3 wind = Vec3(sinf(yaw), 0.0f, cosf(yaw)) * (m_Current.Wind * gust);

	// 따라갈 카메라: 빠르게 움직이면 (비행 · 차) 그만큼 앞에서 태어나게 속도를 잰다 (순간 이동은 무시)
	const Vec3 viewer = ViewerPosition(&m_ViewerForward);
	if (m_HaveViewer && dt > 1e-4f)
	{
		Vec3 v = (viewer - m_LastViewer) / dt;
		if (v.Length() > 120.0f)
			v = Vec3::Zero;
		m_ViewerVelocity = Vec3::Lerp(m_ViewerVelocity, v, 1.0f - expf(-4.0f * dt));
	}
	else if (!m_HaveViewer)
		m_ViewerVelocity = Vec3::Zero;
	m_LastViewer = viewer;
	m_HaveViewer = true;

	UpdateParticles(viewer, wind, running);
	UpdateLightning(dt, viewer, running && Lightning);
	UpdateAudio(dt, gust, running && Sound && Application::IsPlaying());
	ApplyState(gust);
}

void WeatherController::UpdateParticles(const Vec3& viewer, const Vec3& wind, bool on)
{
	const float rain = std::clamp(m_Current.Rain, 0.0f, 1.0f) * on;
	const float snow = std::clamp(m_Current.Snow, 0.0f, 1.0f) * on;
	if (rain <= 0.0f && snow <= 0.0f && m_FxObject == nullptr)
		return;   // 맑으면 만들지도 않는다
	if (m_FxObject == nullptr)
	{
		WeatherFx::EnsureAsset();
		m_FxObject = new GameObject("Weather Precipitation");
		m_Fx = m_FxObject->AddComponent<VisualEffect>();
		m_Fx->AssetPath = WeatherFx::kAssetPath;
	}
	// 수평 이동만 앞질러 (높이는 그대로 — 땅을 따라 오르내리는 카메라)
	Vec3 lead = m_ViewerVelocity * 1.1f;
	lead.y = 0.0f;
	m_FxObject->GetTransform()->SetPosition(viewer + lead);

	const float density = std::clamp(Density, 0.0f, 4.0f);
	auto set1 = [this](const char* name, float v) { m_Fx->SetProperty(name, { v, 0.0f, 0.0f, 0.0f }); };
	auto set3 = [this](const char* name, const Vec3& v) { m_Fx->SetProperty(name, { v.x, v.y, v.z, 0.0f }); };
	set1("Rain Rate", powf(rain, 1.3f) * 16000.0f * density);
	set1("Far Rain Rate", rain * 4500.0f * density);
	set3("Wind", wind);
	// 빗방울이 눈높이에 닿기까지 (가까운 비 14 m ÷ 11.5 m/s, 먼 비 30 m ÷ 13.5 m/s) 바람에 밀리는 만큼 바람 위쪽에서
	set3("Rain Offset", Vec3(0.0f, 14.0f, 0.0f) - wind * (0.5f * 1.2f));   // 빗방울은 바람의 반만큼 밀린다 (WeatherFx)
	set3("Far Rain Offset", Vec3(0.0f, 30.0f, 0.0f) - wind * (0.5f * 2.2f));
	// 눈: 바람이 셀수록 짧게 살고 (40 m 상자를 지나는 시간), 수명의 반만큼 바람 위쪽에서 → 카메라 둘레에 같은 수
	const float windSpeed = wind.Length();
	const float life = std::clamp(40.0f / (windSpeed + 3.0f), 3.0f, 9.0f);
	set1("Snow Life", life * 0.85f);
	set1("Snow Life Max", life * 1.15f);
	set3("Snow Offset", Vec3(0.0f, 6.0f, 0.0f) - wind * (life * 0.5f));
	// 살아 있는 눈송이 수 = 비율 × 수명 → 눈 세기만 따르게
	const float alive = (std::min)(powf(snow, 1.1f) * 85000.0f * density, 95000.0f);
	set1("Snow Rate", alive / life);
}

// ------------------------------------------------------------------ 번개 · 천둥
float WeatherController::FlashEnvelope() const
{
	return m_FlashPeak * Flicker(m_FlashAge);
}

void WeatherController::Strike()
{
	Vec3 fwd = m_ViewerForward;
	const Vec3 viewer = m_HaveViewer ? m_LastViewer : ViewerPosition(&fwd);
	SpawnBolt(viewer, fwd, 1.0f);   // 연출: 늘 보는 쪽에
}

void WeatherController::SpawnBolt(const Vec3& viewer, const Vec3& forward, float inFront)
{
	// inFront 의 확률로 보는 쪽 ±35° 안 (뒤에 치면 하늘이 번쩍일 뿐 빛줄기는 안 보인다)
	const float yaw = atan2f(forward.x, forward.z);
	const bool front = Random01() < inFront;
	const float dist = front ? 170.0f + 230.0f * Random01() : 150.0f + 350.0f * Random01();
	const float ang = front ? yaw + (Random01() - 0.5f) * XMConvertToRadians(70.0f) : 6.2831853f * Random01();
	const Vec3 ground = viewer + Vec3(sinf(ang) * dist, -6.0f, cosf(ang) * dist);
	const Vec3 top = ground + Vec3((Random01() - 0.5f) * 80.0f, 160.0f + 80.0f * Random01(), (Random01() - 0.5f) * 80.0f);

	// 가운데 점 밀기 (midpoint displacement): 갈수록 작게 꺾인 지그재그
	auto jagged = [this](const Vec3& a, const Vec3& b, int levels, float rough) {
		std::vector<Vec3> pts = { a, b };
		float amount = (b - a).Length() * rough;
		for (int l = 0; l < levels; ++l)
		{
			std::vector<Vec3> next;
			next.reserve(pts.size() * 2);
			for (size_t i = 0; i + 1 < pts.size(); ++i)
			{
				next.push_back(pts[i]);
				const Vec3 m = (pts[i] + pts[i + 1]) * 0.5f;
				next.push_back(m + Vec3((Random01() - 0.5f) * 2.0f, (Random01() - 0.5f) * 0.6f, (Random01() - 0.5f) * 2.0f) * amount);
			}
			next.push_back(pts.back());
			pts.swap(next);
			amount *= 0.52f;
		}
		return pts;
	};

	auto acquire = [this]() -> Bolt& {
		for (Bolt& b : m_Bolts)
			if (b.Age >= b.Life)
				return b;
		Bolt b;
		b.Object = new GameObject("Weather Lightning");
		b.Line = b.Object->AddComponent<LineRenderer>();
		b.Line->Blend = 1;           // Additive
		b.Line->CornerVertices = 0;
		b.Line->CapVertices = 2;
		m_Bolts.push_back(b);
		return m_Bolts.back();
	};

	const float width = dist * 0.0065f;
	std::vector<Vec3> main = jagged(top, ground, 6, 0.16f);
	{
		Bolt& b = acquire();
		b.Line->Positions = main;
		b.Width = width;
		b.Main = true;
		b.Age = 0.0f;
		b.Life = 0.55f;
	}
	// 가지 2 ~ 4 개: 줄기 위쪽 70 % 어딘가에서 아래 · 옆으로
	const int branches = 2 + (int)(Random01() * 2.99f);
	for (int i = 0; i < branches; ++i)
	{
		const size_t at = (size_t)(Random01() * 0.7f * (main.size() - 1));
		const Vec3 from = main[at];
		const float len = 30.0f + 60.0f * Random01();
		const float a = 6.2831853f * Random01();
		const Vec3 to = from + Vec3(sinf(a) * len * 0.8f, -len * (0.5f + 0.4f * Random01()), cosf(a) * len * 0.8f);
		Bolt& b = acquire();
		b.Line->Positions = jagged(from, to, 4, 0.22f);
		b.Width = width * 0.45f;
		b.Main = false;
		b.Age = 0.0f;
		b.Life = 0.3f + 0.15f * Random01();
	}

	m_FlashAge = 0.0f;
	m_FlashPeak = std::clamp(260.0f / dist, 0.35f, 1.3f);
	++m_Strikes;

	// 천둥: 소리 빠르기만큼 늦게. 가까우면 찢어지는 소리 (1 · 2), 멀면 우르릉 (3)
	Thunder th;
	th.Delay = dist / 343.0f;
	th.Clip = dist < 260.0f ? (Random01() < 0.5f ? 0 : 1) : 2;
	th.Volume = std::clamp(300.0f / dist, 0.35f, 1.0f);
	m_Thunder.push_back(th);
}

void WeatherController::UpdateLightning(float dt, const Vec3& viewer, bool on)
{
	m_FlashAge += dt;
	const float rate = on ? m_Current.Lightning : 0.0f;
	if (rate > 0.05f)
	{
		if (m_NextStrike < 0.0f)
			m_NextStrike = (std::max)(1.5f, -logf(1.0f - (std::min)(Random01(), 0.999f)) * 60.0f / rate);   // 포아송: 평균 60 / 분당 수 초
		m_NextStrike -= dt;
		if (m_NextStrike <= 0.0f)
		{
			SpawnBolt(viewer, m_ViewerForward, 0.6f);
			m_NextStrike = -1.0f;
		}
	}
	else
		m_NextStrike = -1.0f;

	for (Bolt& b : m_Bolts)
	{
		b.Age += dt;
		const bool visible = b.Age < b.Life;
		b.Line->SetEnabled(visible);
		if (!visible)
			continue;
		const float k = Flicker(b.Age) * (b.Main ? 1.0f : 0.8f);
		const float hdr = 6.0f * k;
		b.Line->WidthMultiplier = 1.0f;
		b.Line->SetEndWidth(false, b.Width * (0.6f + 0.4f * k));
		b.Line->SetEndWidth(true, b.Width * (b.Main ? 0.7f : 0.15f) * (0.6f + 0.4f * k));
		b.Line->SetEndColor(false, Vec4(0.78f * hdr, 0.84f * hdr, 1.0f * hdr, 1.0f));
		b.Line->SetEndColor(true, Vec4(0.78f * hdr, 0.84f * hdr, 1.0f * hdr, 1.0f));
	}
}

void WeatherController::UpdateAudio(float dt, float gust, bool on)
{
	if (!on)
	{
		m_RainLight = m_RainHeavy = m_Wind = m_OneShot = nullptr;
		DeleteHelper(m_AudioObject);
		m_Thunder.clear();
		return;
	}
	if (m_AudioObject == nullptr)
	{
		m_AudioObject = new GameObject("Weather Audio");
		auto loop = [this](const char* file) {
			AudioSource* a = m_AudioObject->AddComponent<AudioSource>();
			a->SetPlayOnAwake(false);
			a->SetSpatialBlend(0.0f);
			a->SetLoop(true);
			a->SetVolume(0.0f);
			const std::string path = AudioPath(file);
			if (!path.empty())
			{
				a->SetClip(path);
				a->Play();
			}
			return a;
		};
		m_RainLight = loop("rain_light.wav");
		m_RainHeavy = loop("rain_heavy.wav");
		m_Wind = loop("wind.wav");
		m_OneShot = m_AudioObject->AddComponent<AudioSource>();
		m_OneShot->SetPlayOnAwake(false);
		m_OneShot->SetSpatialBlend(0.0f);
		static const char* thunder[3] = { "thunder_1.wav", "thunder_2.wav", "thunder_3.wav" };
		for (int i = 0; i < 3; ++i)
		{
			const std::string path = AudioPath(thunder[i]);
			m_ThunderClips[i] = path.empty() ? nullptr : AudioClip::Load(path);
		}
	}
	if (m_HaveViewer)
		m_AudioObject->GetTransform()->SetPosition(m_LastViewer);

	const float vol = std::clamp(Volume, 0.0f, 1.0f);
	const float rain = std::clamp(m_Current.Rain, 0.0f, 1.0f);
	const float heavy = Smooth01((rain - 0.3f) / 0.6f);
	const float light = std::clamp(rain * 2.5f, 0.0f, 1.0f) * (1.0f - 0.55f * heavy);
	const float windSpeed = m_Current.Wind * gust;
	const float wind = powf(std::clamp((windSpeed - 2.5f) / 13.0f, 0.0f, 1.0f), 0.8f);
	// 목표 소리 크기로 천천히 (돌풍이 바람 소리를 오르내린다)
	const float k = 1.0f - expf(-3.0f * dt);
	auto approach = [k](AudioSource* a, float target) {
		if (a == nullptr) return;
		a->SetVolume(a->GetVolume() + (target - a->GetVolume()) * k);
		a->Update();
	};
	approach(m_RainLight, light * 0.8f * vol);
	approach(m_RainHeavy, heavy * 0.9f * vol);
	approach(m_Wind, wind * 0.75f * vol);

	for (size_t i = 0; i < m_Thunder.size();)
	{
		m_Thunder[i].Delay -= dt;
		if (m_Thunder[i].Delay > 0.0f)
		{
			++i;
			continue;
		}
		if (m_OneShot && m_ThunderClips[m_Thunder[i].Clip])
			m_OneShot->PlayOneShot(m_ThunderClips[m_Thunder[i].Clip], m_Thunder[i].Volume * vol);
		m_Thunder.erase(m_Thunder.begin() + i);
	}
	if (m_OneShot)
		m_OneShot->Update();
}

bool WeatherController::SoundLevels(float out[3]) const
{
	AudioSource* src[3] = { m_RainLight, m_RainHeavy, m_Wind };
	bool playing = false;
	for (int i = 0; i < 3; ++i)
	{
		out[i] = src[i] ? src[i]->GetVolume() : 0.0f;
		playing |= src[i] && src[i]->IsPlaying();
	}
	return playing;
}

// ------------------------------------------------------------------ 엔진에 건네기 (그릴 때 적용 — 장면은 그대로)
void WeatherController::ApplyState(float gust)
{
	WeatherState& s = WeatherState::Get();
	const WeatherParams& p = m_Current;
	const float c = std::clamp(p.Clouds, 0.0f, 1.0f);
	// 해: 먹구름 + 짙은 안개 (눈보라 · 폭풍) 에는 그림자가 거의 없을 만큼
	s.SunIntensity = (1.0f - 0.85f * c) * (1.0f - 0.6f * std::clamp(p.Fog, 0.0f, 1.0f));
	s.SunTint = Mix3({ 1, 1, 1 }, { 0.82f, 0.88f, 1.0f }, c);
	s.AmbientIntensity = 1.0f - 0.5f * c;
	s.AmbientTint = Mix3({ 1, 1, 1 }, { 0.86f, 0.9f, 1.0f }, c);
	s.SkyBrightness = 1.0f - 0.62f * c;
	s.SkyDesaturate = 0.85f * c;
	s.SkyTint = Mix3({ 1, 1, 1 }, { 0.88f, 0.92f, 1.0f }, c);

	// 안개: 비는 푸른 잿빛 (먹구름일수록 어둡게), 눈은 밝은 흰 잿빛
	const float fall = p.Rain + p.Snow;
	const float snowShare = fall > 1e-3f ? p.Snow / fall : 0.0f;
	const float grey = Mix(Mix(0.62f, 0.30f, c), Mix(0.86f, 0.64f, c), snowShare);
	const float flash = FlashEnvelope();
	s.FogAmount = std::clamp(p.Fog, 0.0f, 1.0f);
	s.FogDistance = (std::max)(5.0f, p.FogDistance);
	s.FogColor = { grey * 0.95f + flash * 0.25f, grey * 0.98f + flash * 0.27f, grey * 1.04f + flash * 0.32f };
	s.FogHeight = 120.0f;

	// 바람: 2 m/s (맑은 날 산들바람) = 각 에셋의 바람 그대로
	s.WindStrength = std::clamp(0.5f + p.Wind * gust / 4.0f, 0.0f, 4.5f);
	s.Wetness = std::clamp(m_SurfaceWet, 0.0f, 1.0f);
	s.PuddleLevel = std::clamp(m_Puddles, 0.0f, 1.0f);
	s.RainIntensity = std::clamp(p.Rain, 0.0f, 1.0f);
	s.Time = (float)fmod(m_Time, 3600.0);
	s.SnowCover = std::clamp(m_Snow, 0.0f, 1.0f);
	s.SnowFall = std::clamp(p.Snow, 0.0f, 1.0f);
	s.SnowDepth = (std::max)(0.02f, SnowDepth);
	s.Flash = flash;
}

// ------------------------------------------------------------------ Inspector
void WeatherController::OnInspectorGUI()
{
	const auto& presets = WeatherProfiles::Presets();
	// 기본 프로필 단추 (3 개씩 두 줄)
	UnityGUI::Label("Presets", 0, true);
	const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;
	for (size_t i = 0; i < presets.size(); ++i)
	{
		if (i % 3 != 0)
			ImGui::SameLine();
		const bool current = Profile == presets[i].first;
		if (current)
			ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
		if (ImGui::Button(presets[i].first.c_str(), ImVec2(w, 0.0f)))
			Profile = presets[i].first;
		if (current)
			ImGui::PopStyleColor();
	}
	UnityGUI::Spacing(4.0f);

	// 프로필 (기본 + 프로젝트의 .weather) — 파일 목록은 가끔만 다시 읽는다
	static std::vector<std::string> s_Assets;
	static double s_AssetsAt = -10.0;
	if (ImGui::GetTime() - s_AssetsAt > 2.0)
	{
		s_Assets = WeatherProfiles::FindAssets();
		s_AssetsAt = ImGui::GetTime();
	}
	std::vector<std::string> names;
	for (const auto& [n, _] : presets) names.push_back(n);
	for (const std::string& a : s_Assets) names.push_back(a);
	int index = -1;
	for (size_t i = 0; i < names.size(); ++i)
		if (names[i] == Profile) index = (int)i;
	if (index < 0)
	{
		names.push_back(Profile);
		index = (int)names.size() - 1;
	}
	std::vector<const char*> items;
	for (const std::string& n : names) items.push_back(n.c_str());
	if (UnityGUI::Dropdown("Profile", &index, items.data(), (int)items.size()))
		Profile = names[index];
	if (UnityGUI::Float("Transition Time", &TransitionTime)) TransitionTime = (std::max)(0.0f, TransitionTime);
	UnityGUI::Slider("Density", &Density, 0.0f, 2.0f);
	if (UnityGUI::Float("Snow Depth", &SnowDepth)) SnowDepth = std::clamp(SnowDepth, 0.02f, 1.0f);
	UnityGUI::Toggle("Lightning", &Lightning);
	UnityGUI::Toggle("Sound", &Sound);
	if (Sound)
		UnityGUI::Slider("Volume", &Volume, 0.0f, 1.0f, 1);

	if (!m_Error.empty())
		UnityGUI::HelpBox(m_Error.c_str(), true);

	// 지금 상태
	UnityGUI::Spacing(4.0f);
	char buf[160];
	const float prog = TransitionProgress();
	snprintf(buf, sizeof(buf), prog < 1.0f ? "%s (%.0f %%)" : "%s", m_TargetName.c_str(), prog * 100.0f);
	UnityGUI::ValueLabel("Now", buf);
	snprintf(buf, sizeof(buf), "Rain %.2f  Snow %.2f  Clouds %.2f", m_Current.Rain, m_Current.Snow, m_Current.Clouds);
	UnityGUI::ValueLabel("Precipitation", buf);
	snprintf(buf, sizeof(buf), "%.1f m/s  %.0f deg  Fog %.2f", m_Current.Wind, m_Current.WindDirection, m_Current.Fog);
	UnityGUI::ValueLabel("Wind", buf);
	snprintf(buf, sizeof(buf), "Wet %.2f  Puddles %.2f  Snow %.2f", m_SurfaceWet, m_Puddles, m_Snow);
	UnityGUI::ValueLabel("Surface", buf);
	if (Lightning && UnityGUI::CenterButton("Strike Lightning"))
		Strike();
	if (Sound && !Application::IsPlaying())
		UnityGUI::HelpBox("Rain, wind and thunder sounds play in Play mode.", false);
}

// ------------------------------------------------------------------ 저장
GENERATE_COMPONENT_FUNC_TOJSON(WeatherController)
{
	json j;
	j["type"] = "WeatherController";
	j["enabled"] = m_Enabled;
	j["profile"] = Profile;
	j["transitionTime"] = TransitionTime;
	j["density"] = Density;
	j["lightning"] = Lightning;
	j["sound"] = Sound;
	j["volume"] = Volume;
	j["snowDepth"] = SnowDepth;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(WeatherController)
{
	m_Enabled = j.value("enabled", true);
	Profile = j.value("profile", std::string("Clear"));
	TransitionTime = j.value("transitionTime", 8.0f);
	Density = j.value("density", 1.0f);
	Lightning = j.value("lightning", true);
	Sound = j.value("sound", true);
	Volume = j.value("volume", 1.0f);
	SnowDepth = j.value("snowDepth", 0.25f);
}
