#include "pch.h"
#include "ParticleSystem.h"
#include "ParticleSystemEditor.h"
#include "SceneViewOverlay.h"
#include "UnityGUI.h"

namespace
{
	std::vector<ParticleSystem*> s_All;
	uint32 s_SeedCounter = 0x9E3779B9u;

	constexpr float kGravity = -9.81f;
	constexpr float kDegToRad = 3.14159265f / 180.0f;

	// ---- 3D 값 노이즈 (Noise 모듈): 격자 해시 + 부드러운 보간, -1~1
	float Hash3(int x, int y, int z)
	{
		uint32 h = (uint32)x * 374761393u + (uint32)y * 668265263u + (uint32)z * 2147483647u;
		h = (h ^ (h >> 13)) * 1274126177u;
		h ^= h >> 16;
		return (h & 0xFFFFFF) / (float)0xFFFFFF * 2.0f - 1.0f;
	}

	float ValueNoise(float x, float y, float z)
	{
		const int ix = (int)floorf(x), iy = (int)floorf(y), iz = (int)floorf(z);
		const float fx = x - ix, fy = y - iy, fz = z - iz;
		auto s = [](float t) { return t * t * (3.0f - 2.0f * t); };
		const float ux = s(fx), uy = s(fy), uz = s(fz);
		auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
		const float c000 = Hash3(ix, iy, iz), c100 = Hash3(ix + 1, iy, iz), c010 = Hash3(ix, iy + 1, iz), c110 = Hash3(ix + 1, iy + 1, iz);
		const float c001 = Hash3(ix, iy, iz + 1), c101 = Hash3(ix + 1, iy, iz + 1), c011 = Hash3(ix, iy + 1, iz + 1), c111 = Hash3(ix + 1, iy + 1, iz + 1);
		return lerp(lerp(lerp(c000, c100, ux), lerp(c010, c110, ux), uy), lerp(lerp(c001, c101, ux), lerp(c011, c111, ux), uy), uz);
	}

	Vec3 Noise3(const Vec3& p, int octaves, float octaveMultiplier, float octaveScale)
	{
		Vec3 sum(0, 0, 0);
		float amp = 1.0f, freq = 1.0f, norm = 0.0f;
		for (int o = 0; o < (std::max)(1, octaves); ++o)
		{
			const Vec3 q = p * freq;
			// 축마다 다른 곳을 읽어 서로 다른 세 값
			sum.x += ValueNoise(q.x, q.y, q.z) * amp;
			sum.y += ValueNoise(q.x + 31.4f, q.y + 17.9f, q.z - 5.3f) * amp;
			sum.z += ValueNoise(q.x - 11.7f, q.y + 43.1f, q.z + 23.9f) * amp;
			norm += amp;
			amp *= octaveMultiplier;
			freq *= octaveScale;
		}
		return norm > 0.0f ? sum / norm : sum;
	}
}

ParticleSystem::ParticleSystem()
{
	m_InspectorTitleName = "Particle System";
	s_All.push_back(this);
	// Unity 기본 Color over Lifetime: 흰색, 끝으로 갈수록 투명해지지 않음 (켜면 사용자가 바꾼다)
	ColorOverLifetime.Mode = ParticleGradientMode::Gradient;
	ColorOverLifetime.GradientMax.Alphas = { { 0.0f, 1.0f }, { 1.0f, 0.0f } };
}

ParticleSystem::~ParticleSystem()
{
	s_All.erase(std::remove(s_All.begin(), s_All.end(), this), s_All.end());
}

const std::vector<ParticleSystem*>& ParticleSystem::All()
{
	return s_All;
}

bool ParticleSystem::HasEnabledToggle() const
{
	return false;   // Unity 의 Particle System 은 Behaviour 가 아니어서 헤더에 체크박스가 없다
}

void ParticleSystem::Start()
{
	// Play 시작: 에디터 미리보기로 남은 입자를 지우고 Play On Awake 면 재생 (자식은 각자의 Start 에서)
	m_Particles.clear();
	m_Playing = m_Paused = m_Emitting = false;
	m_StopActionPending = false;
	if (PlayOnAwake && Application::IsPlaying())
		Play(false);
}

void ParticleSystem::OnDestroy()
{
	m_Particles.clear();
	m_Playing = false;
}

// ------------------------------------------------------------------ 제어
float ParticleSystem::Random01()
{
	// xorshift32
	m_Rng ^= m_Rng << 13;
	m_Rng ^= m_Rng >> 17;
	m_Rng ^= m_Rng << 5;
	return (m_Rng & 0xFFFFFF) / (float)0x1000000;
}

bool ParticleSystem::ActiveInHierarchy()
{
	for (GameObject* g = m_pGameObject; g != nullptr; g = g->GetParent())
		if (!g->IsActive())
			return false;
	return m_pGameObject != nullptr;
}

void ParticleSystem::CollectHierarchy(std::vector<ParticleSystem*>& out)
{
	out.push_back(this);
	if (m_pGameObject == nullptr)
		return;
	std::function<void(GameObject*)> walk = [&](GameObject* g) {
		for (GameObject* c : g->GetChildren())
		{
			if (ParticleSystem* ps = c->GetComponent<ParticleSystem>())
				out.push_back(ps);
			walk(c);
		}
	};
	walk(m_pGameObject);
}

void ParticleSystem::Play(bool withChildren)
{
	std::vector<ParticleSystem*> group;
	if (withChildren)
		CollectHierarchy(group);
	else
		group.push_back(this);
	for (ParticleSystem* ps : group)
	{
		if (ps->m_Paused)
		{
			ps->m_Paused = false;   // 일시정지에서 이어서
			continue;
		}
		if (ps->m_Playing)
			continue;
		ps->m_Playing = true;
		ps->m_Emitting = true;
		ps->m_StopActionPending = false;
		ps->m_Time = 0.0f;
		if (ps->m_Particles.empty())
			ps->m_TotalTime = 0.0f;
		ps->m_RateAccumulator = 0.0f;
		ps->m_DistanceAccumulator = 0.0f;
		ps->m_HasLastPosition = false;
		ps->m_Rng = ps->AutoRandomSeed ? (s_SeedCounter = s_SeedCounter * 1664525u + 1013904223u + (uint32)::GetTickCount64()) : (ps->RandomSeed ? ps->RandomSeed : 1u);
		if (ps->m_Rng == 0)
			ps->m_Rng = 1;
		ps->m_Delay = (std::max)(0.0f, ps->StartDelay.Evaluate(0.0f, ps->Random01()));
		// Prewarm: 한 주기를 미리 돌려 두고 시작 (반복일 때만, Start Delay 무시 — Unity 와 같음)
		if (ps->Prewarm && ps->Looping && ps->Duration > 0.0f)
		{
			ps->m_Delay = 0.0f;
			const float step = 1.0f / 30.0f;
			for (float t = 0.0f; t < ps->Duration; t += step)
				ps->Advance(step);
		}
	}
}

void ParticleSystem::Stop(bool withChildren, bool clear)
{
	std::vector<ParticleSystem*> group;
	if (withChildren)
		CollectHierarchy(group);
	else
		group.push_back(this);
	for (ParticleSystem* ps : group)
	{
		if (ps->m_Playing)
			ps->m_StopActionPending = true;
		ps->m_Playing = false;
		ps->m_Emitting = false;
		ps->m_Paused = false;
		if (clear)
			ps->m_Particles.clear();
	}
}

void ParticleSystem::Pause(bool withChildren)
{
	std::vector<ParticleSystem*> group;
	if (withChildren)
		CollectHierarchy(group);
	else
		group.push_back(this);
	for (ParticleSystem* ps : group)
		if (ps->IsAlive())
			ps->m_Paused = true;
}

void ParticleSystem::Clear(bool withChildren)
{
	std::vector<ParticleSystem*> group;
	if (withChildren)
		CollectHierarchy(group);
	else
		group.push_back(this);
	for (ParticleSystem* ps : group)
		ps->m_Particles.clear();
}

void ParticleSystem::Restart()
{
	Stop(true, true);
	m_StopActionPending = false;
	std::vector<ParticleSystem*> group;
	CollectHierarchy(group);
	for (ParticleSystem* ps : group)
		ps->m_StopActionPending = false;
	Play(true);
}

void ParticleSystem::Emit(int count)
{
	EmitInternal(count, Duration > 0.0f ? m_Time / Duration : 0.0f);
}

// ------------------------------------------------------------------ 도형
Matrix ParticleSystem::ShapeMatrix() const
{
	return Matrix::CreateScale(ShapeScale) * Matrix::CreateFromQuaternion(Transform::EulerToQuaternion(ShapeRotation)) * Matrix::CreateTranslation(ShapePosition);
}

void ParticleSystem::ShapeSample(Vec3& position, Vec3& direction)
{
	const float arc = std::clamp(ShapeArc, 0.0f, 360.0f) * kDegToRad;
	const float thickness = std::clamp(ShapeRadiusThickness, 0.0f, 1.0f);
	auto randomUnit = [&]() {
		const float z = Random01() * 2.0f - 1.0f;
		const float a = Random01() * 6.2831853f;
		const float r = sqrtf((std::max)(0.0f, 1.0f - z * z));
		return Vec3(r * cosf(a), r * sinf(a), z);
	};
	// 원판 안의 반지름 비율 (0 = 가운데, 1 = 가장자리). Radius Thickness 0 이면 가장자리에서만
	auto discRadius = [&]() {
		const float inner = 1.0f - thickness;
		return sqrtf(inner * inner + (1.0f - inner * inner) * Random01());
	};
	switch (Shape)
	{
	case ShapeType::Sphere:
	case ShapeType::Hemisphere:
	{
		Vec3 d = randomUnit();
		if (Shape == ShapeType::Hemisphere)
			d.z = fabsf(d.z);
		const float inner = 1.0f - thickness;
		const float r = cbrtf(inner * inner * inner + (1.0f - inner * inner * inner) * Random01());
		position = d * (ShapeRadius * r);
		direction = d;
		break;
	}
	case ShapeType::Cone:
	{
		const float a = Random01() * arc;
		const float s = discRadius();
		const float angle = std::clamp(ShapeAngle, 0.0f, 90.0f) * kDegToRad;
		// 가장자리 입자일수록 원뿔 각도만큼 바깥으로 기운다 (Unity 와 같은 모양)
		direction = Vec3(cosf(a) * s * sinf(angle), sinf(a) * s * sinf(angle), cosf(angle));
		direction.Normalize();
		position = Vec3(cosf(a) * s * ShapeRadius, sinf(a) * s * ShapeRadius, 0.0f);
		if (ConeEmitFrom == 1)
		{
			// Volume: 원뿔 몸통 안 (길이 방향으로 퍼짐)
			const float along = Random01() * ShapeLength;
			position += direction * (along / (std::max)(0.05f, direction.z));
		}
		break;
	}
	case ShapeType::Box:
		position = Vec3(Random01() - 0.5f, Random01() - 0.5f, Random01() - 0.5f);
		direction = Vec3(0, 0, 1);
		break;
	case ShapeType::Circle:
	{
		const float a = Random01() * arc;
		const float s = discRadius();
		position = Vec3(cosf(a) * s * ShapeRadius, sinf(a) * s * ShapeRadius, 0.0f);
		direction = Vec3(cosf(a), sinf(a), 0.0f);
		break;
	}
	case ShapeType::Edge:
		position = Vec3((Random01() * 2.0f - 1.0f) * ShapeRadius, 0.0f, 0.0f);
		direction = Vec3(0, 1, 0);
		break;
	}
	if (!ShapeEnabled)
	{
		position = Vec3(0, 0, 0);
		direction = Vec3(0, 0, 1);
	}
	if (SpherizeDirection > 0.0f && position.LengthSquared() > 1e-10f)
	{
		Vec3 out = position;
		out.Normalize();
		direction = Vec3::Lerp(direction, out, std::clamp(SpherizeDirection, 0.0f, 1.0f));
	}
	if (RandomizeDirection > 0.0f)
		direction = Vec3::Lerp(direction, randomUnit(), std::clamp(RandomizeDirection, 0.0f, 1.0f));
	if (direction.LengthSquared() < 1e-10f)
		direction = Vec3(0, 0, 1);
	direction.Normalize();
}

Matrix ParticleSystem::SimulationToWorld()
{
	if (SimulationSpace == 1 || m_pGameObject == nullptr)
		return Matrix::Identity;
	return m_pGameObject->GetTransform()->GetWorldMatrix();
}

// ------------------------------------------------------------------ 방출
void ParticleSystem::EmitInternal(int count, float systemT)
{
	count = (std::min)(count, (std::max)(0, MaxParticles - (int)m_Particles.size()));
	if (count <= 0)
		return;
	// 도형 공간 → 시뮬레이션 공간 (World 면 오브젝트의 월드 변환까지)
	Matrix toSim = ShapeMatrix();
	if (SimulationSpace == 1 && m_pGameObject)
		toSim = toSim * m_pGameObject->GetTransform()->GetWorldMatrix();
	const int sheetFrames = SheetEnabled ? (SheetAnimation == 1 ? (std::max)(1, SheetTilesX) : (std::max)(1, SheetTilesX) * (std::max)(1, SheetTilesY)) : 1;
	for (int i = 0; i < count; ++i)
	{
		Particle p;
		for (float& r : p.Random)
			r = Random01();
		Vec3 pos, dir;
		ShapeSample(pos, dir);
		p.Position = Vec3::Transform(pos, toSim);
		dir = Vec3::TransformNormal(dir, toSim);
		if (dir.LengthSquared() > 1e-10f)
			dir.Normalize();
		p.Velocity = dir * StartSpeed.Evaluate(systemT, Random01());
		p.Lifetime = (std::max)(0.001f, StartLifetime.Evaluate(systemT, Random01()));
		p.StartSize = (std::max)(0.0f, StartSize.Evaluate(systemT, Random01()));
		p.Size = p.StartSize;
		p.Rotation = StartRotation.Evaluate(systemT, Random01());
		if (FlipRotation > 0.0f && Random01() < FlipRotation)
		{
			p.Rotation = -p.Rotation;
			p.Direction = -1.0f;
		}
		p.StartColor = StartColor.Evaluate(systemT, Random01());
		p.Color = p.StartColor;
		p.SheetFrame = SheetEnabled ? std::clamp(SheetStartFrame.Evaluate(0.0f, Random01()), 0.0f, (float)(sheetFrames - 1)) : 0.0f;
		p.SheetRow = SheetEnabled && SheetAnimation == 1 ? (SheetRandomRow ? (int)(Random01() * (std::max)(1, SheetTilesY)) : SheetRowIndex) : 0;
		m_Particles.push_back(p);
	}
}

// ------------------------------------------------------------------ 진행
void ParticleSystem::Advance(float dt)
{
	if (dt <= 0.0f)
		return;
	m_TotalTime += dt;
	const float duration = (std::max)(0.05f, Duration);

	// ---- 방출 (Start Delay → 시스템 시간 [이전, 지금) 구간의 Rate / Burst)
	if (m_Playing && m_Emitting)
	{
		float emitDt = dt;
		if (m_Delay > 0.0f)
		{
			m_Delay -= dt;
			emitDt = m_Delay < 0.0f ? -m_Delay : 0.0f;
			if (m_Delay < 0.0f)
				m_Delay = 0.0f;
		}
		auto emitRange = [&](float a, float b) {
			if (b <= a || !EmissionEnabled)
				return;
			const float t = a / duration;
			m_RateAccumulator += (std::max)(0.0f, RateOverTime.Evaluate(t, Random01())) * (b - a);
			const int n = (int)m_RateAccumulator;
			m_RateAccumulator -= n;
			EmitInternal(n, t);
			for (const Burst& burst : Bursts)
			{
				const int cycles = burst.Cycles <= 0 ? INT_MAX : burst.Cycles;
				const float interval = (std::max)(0.0001f, burst.Interval);
				for (int c = 0; c < cycles; ++c)
				{
					const float tb = burst.Time + c * interval;
					if (tb >= duration || tb >= b)
						break;
					if (tb >= a && Random01() <= burst.Probability)
						EmitInternal((int)(burst.Count.Evaluate(tb / duration, Random01()) + 0.5f), tb / duration);
				}
			}
		};
		if (emitDt > 0.0f)
		{
			float prev = m_Time;
			float cur = prev + emitDt;
			while (true)
			{
				emitRange(prev, (std::min)(cur, duration));
				if (cur < duration)
					break;
				if (!Looping)
				{
					cur = duration;
					m_Emitting = false;   // 한 번 재생 끝: 남은 입자가 모두 사라지면 멈춘다
					break;
				}
				cur -= duration;
				prev = 0.0f;
				m_RateAccumulator = 0.0f;
			}
			m_Time = cur;
		}

		// Rate over Distance: 오브젝트가 움직인 거리만큼
		if (m_pGameObject && EmissionEnabled && !RateOverDistance.IsConstantZero())
		{
			const Vec3 p = m_pGameObject->GetTransform()->GetPosition();
			if (m_HasLastPosition)
			{
				m_DistanceAccumulator += (std::max)(0.0f, RateOverDistance.Evaluate(m_Time / duration, Random01())) * (p - m_LastEmitterPosition).Length();
				const int n = (int)m_DistanceAccumulator;
				m_DistanceAccumulator -= n;
				EmitInternal(n, m_Time / duration);
			}
			m_LastEmitterPosition = p;
			m_HasLastPosition = true;
		}
	}

	// ---- 입자
	const Matrix toWorld = SimulationToWorld();
	const bool local = SimulationSpace == 0;
	Matrix toSim = Matrix::Identity;
	Matrix objectWorld = m_pGameObject ? m_pGameObject->GetTransform()->GetWorldMatrix() : Matrix::Identity;
	if (local)
		toWorld.Invert(toSim);
	// 월드 방향 → 시뮬레이션 공간, 오브젝트 로컬 방향 → 시뮬레이션 공간
	auto worldDir = [&](const Vec3& v) { return local ? Vec3::TransformNormal(v, toSim) : v; };
	auto localDir = [&](const Vec3& v) { return local ? v : Vec3::TransformNormal(v, objectWorld); };
	const float sysT = m_Time / duration;
	const Vec3 gravity = worldDir(Vec3(0.0f, kGravity, 0.0f));
	const int sheetTilesX = (std::max)(1, SheetTilesX), sheetTilesY = (std::max)(1, SheetTilesY);
	const int sheetFrames = SheetAnimation == 1 ? sheetTilesX : sheetTilesX * sheetTilesY;
	const float dampFrame = 1.0f - powf(1.0f - std::clamp(LimitDampen, 0.0f, 1.0f), dt * 30.0f);

	for (size_t i = 0; i < m_Particles.size();)
	{
		Particle& p = m_Particles[i];
		p.Age += dt;
		if (p.Age >= p.Lifetime)
		{
			p = m_Particles.back();
			m_Particles.pop_back();
			continue;
		}
		const float t = p.Age / p.Lifetime;

		const float g = GravityModifier.Evaluate(sysT, p.Random[0]);
		if (g != 0.0f)
			p.Velocity += gravity * (g * dt);
		if (ForceEnabled)
		{
			const Vec3 f(ForceX.Evaluate(t, p.Random[1]), ForceY.Evaluate(t, p.Random[2]), ForceZ.Evaluate(t, p.Random[3]));
			p.Velocity += (ForceSpace == 1 ? worldDir(f) : localDir(f)) * dt;
		}
		if (LimitEnabled)
		{
			const float limit = (std::max)(0.0f, LimitSpeed.Evaluate(t, p.Random[1]));
			const float speed = p.Velocity.Length();
			if (speed > limit && speed > 1e-6f)
				p.Velocity *= (speed - (speed - limit) * dampFrame) / speed;
			const float drag = LimitDrag.Evaluate(t, p.Random[2]);
			if (drag > 0.0f)
				p.Velocity *= (std::max)(0.0f, 1.0f - drag * dt);
		}
		float speedModifier = 1.0f;
		if (VelocityEnabled)
		{
			const Vec3 v(VelocityX.Evaluate(t, p.Random[1]), VelocityY.Evaluate(t, p.Random[2]), VelocityZ.Evaluate(t, p.Random[3]));
			p.AnimatedVelocity = VelocitySpace == 1 ? worldDir(v) : localDir(v);
			speedModifier = SpeedModifier.Evaluate(t, p.Random[0]);
		}
		else
			p.AnimatedVelocity = Vec3(0, 0, 0);
		Vec3 noise(0, 0, 0);
		if (NoiseEnabled)
		{
			const float scroll = NoiseScrollSpeed.Evaluate(sysT, 0.5f) * m_TotalTime;
			const float freq = (std::max)(0.0001f, NoiseFrequency);
			const Vec3 world = local ? Vec3::Transform(p.Position, toWorld) : p.Position;
			noise = Noise3(world * freq + Vec3(scroll, scroll, scroll), NoiseOctaves, NoiseOctaveMultiplier, NoiseOctaveScale);
			// Damping: 세기가 주파수에 비례 (주파수를 올려도 흔들림 폭이 커지지 않게)
			float strength = NoiseStrength.Evaluate(t, p.Random[3]);
			if (NoiseDamping)
				strength *= freq * 2.0f;
			noise = worldDir(noise * strength);
		}
		p.Position += ((p.Velocity + p.AnimatedVelocity) * speedModifier + noise) * dt;
		if (RotationEnabled)
			p.Rotation += AngularVelocity.Evaluate(t, p.Random[2]) * p.Direction * dt;
		p.Size = p.StartSize * (SizeEnabled ? (std::max)(0.0f, SizeOverLifetime.Evaluate(t, p.Random[1])) : 1.0f);
		if (ColorEnabled)
		{
			const Vec4 c = ColorOverLifetime.Evaluate(t, p.Random[2]);
			p.Color = Vec4(p.StartColor.x * c.x, p.StartColor.y * c.y, p.StartColor.z * c.z, p.StartColor.w * c.w);
		}
		else
			p.Color = p.StartColor;
		if (SheetEnabled)
		{
			const float cycle = fmodf(t * (std::max)(0.0001f, SheetCycles), 1.0f);
			const float start = SheetStartFrame.Evaluate(0.0f, p.Random[3]);
			p.SheetFrame = std::clamp(start + SheetFrameOverTime.Evaluate(cycle, p.Random[0]) * sheetFrames, 0.0f, (float)(sheetFrames - 1));
		}
		++i;
	}

	// 반복하지 않는 시스템: 방출이 끝나고 입자가 모두 사라지면 멈춤
	if (m_Playing && !m_Emitting && m_Particles.empty())
	{
		m_Playing = false;
		m_StopActionPending = true;
	}
}

void ParticleSystem::Simulate(float dt)
{
	if (dt <= 0.0f || m_Paused)
		return;
	if (!m_Playing && m_Particles.empty())
	{
		if (m_StopActionPending)
			RunStopAction();
		return;
	}
	dt *= (std::max)(0.0f, SimulationSpeed);
	dt = (std::min)(dt, 1.0f);   // 멈췄다 돌아온 프레임 (중단점, 창 끌기)
	const float maxStep = 1.0f / 20.0f;
	while (dt > 1e-6f)
	{
		const float step = (std::min)(dt, maxStep);
		Advance(step);
		dt -= step;
	}
	if (!m_Playing && m_Particles.empty() && m_StopActionPending)
		RunStopAction();
}

void ParticleSystem::RunStopAction()
{
	m_StopActionPending = false;
	if (!Application::IsPlaying() || m_pGameObject == nullptr)
		return;
	switch (OnStop)
	{
	case StopAction::Disable: m_pGameObject->SetActive(false); break;
	case StopAction::Destroy: GameObject::Destroy(m_pGameObject); break;
	default: break;
	}
}

void ParticleSystem::UpdateAll()
{
	const float dt = (std::min)((float)DT, 0.25f);
	if (Application::IsPlaying())
	{
		if (!Application::ShouldUpdateGame())
			return;
		// Stop Action(Destroy) 이 목록을 바꿀 수 있어 복사본을 돈다
		const std::vector<ParticleSystem*> systems = s_All;
		for (ParticleSystem* ps : systems)
			if (std::find(s_All.begin(), s_All.end(), ps) != s_All.end() && ps->IsAlive() && ps->ActiveInHierarchy())
				ps->Simulate(dt * ParticleSystemEditor::PlaybackSpeedFor(ps));
		return;
	}
	ParticleSystemEditor::UpdatePreview(dt);
}

// ------------------------------------------------------------------ 기즈모
void ParticleSystem::OnDrawGizmos()
{
	if (m_pGameObject == nullptr || !SceneViewOverlay::IsActive())
		return;
	Transform* tr = m_pGameObject->GetTransform();
	// 아이콘 (Unity 처럼 화면 크기 일정)
	ImVec2 sp;
	if (SceneViewOverlay::Project(XMFLOAT3(tr->GetPosition().x, tr->GetPosition().y, tr->GetPosition().z), sp))
		UnityGUI::DrawIcon(ImGui::GetWindowDrawList(), "particle_system", ImVec2(sp.x - 12.0f, sp.y - 12.0f), 24.0f, IM_COL32(255, 255, 255, 220));

	if (SelectionManager::GetSelectedGameObject() != m_pGameObject || !ShapeEnabled)
		return;
	const Matrix m = ShapeMatrix() * tr->GetWorldMatrix();
	const ImU32 col = IM_COL32(148, 205, 255, 255);
	auto line = [&](const Vec3& a, const Vec3& b) {
		const Vec3 wa = Vec3::Transform(a, m), wb = Vec3::Transform(b, m);
		SceneViewOverlay::DrawLine(XMFLOAT3(wa.x, wa.y, wa.z), XMFLOAT3(wb.x, wb.y, wb.z), col);
	};
	// 원: plane 0 = XY, 1 = XZ, 2 = YZ. 도 단위 호
	auto circle = [&](const Vec3& center, float r, int plane, float fromDeg = 0.0f, float toDeg = 360.0f) {
		const int seg = 48;
		Vec3 prev;
		for (int i = 0; i <= seg; ++i)
		{
			const float a = (fromDeg + (toDeg - fromDeg) * i / seg) * kDegToRad;
			const float c = cosf(a) * r, s = sinf(a) * r;
			const Vec3 p = center + (plane == 0 ? Vec3(c, s, 0) : plane == 1 ? Vec3(c, 0, s) : Vec3(0, c, s));
			if (i > 0)
				line(prev, p);
			prev = p;
		}
	};
	const float arc = std::clamp(ShapeArc, 0.0f, 360.0f);
	switch (Shape)
	{
	case ShapeType::Sphere:
		circle(Vec3(0, 0, 0), ShapeRadius, 0);
		circle(Vec3(0, 0, 0), ShapeRadius, 1);
		circle(Vec3(0, 0, 0), ShapeRadius, 2);
		break;
	case ShapeType::Hemisphere:
		circle(Vec3(0, 0, 0), ShapeRadius, 0);
		circle(Vec3(0, 0, 0), ShapeRadius, 1, 0.0f, 180.0f);
		circle(Vec3(0, 0, 0), ShapeRadius, 2, 0.0f, 180.0f);
		break;
	case ShapeType::Cone:
	{
		const float angle = std::clamp(ShapeAngle, 0.0f, 89.0f) * kDegToRad;
		const float top = ShapeRadius + ShapeLength * tanf(angle);
		circle(Vec3(0, 0, 0), ShapeRadius, 0, 0.0f, arc);
		circle(Vec3(0, 0, ShapeLength), top, 0, 0.0f, arc);
		// 옆면 선 4개 (호가 360 이 아니면 양 끝이 보이도록 호를 4 등분)
		const int sides = arc < 360.0f ? 5 : 4;
		for (int k = 0; k < sides; ++k)
		{
			const float a = (arc * k / 4.0f) * kDegToRad;
			line(Vec3(cosf(a) * ShapeRadius, sinf(a) * ShapeRadius, 0), Vec3(cosf(a) * top, sinf(a) * top, ShapeLength));
		}
		break;
	}
	case ShapeType::Box:
	{
		const float h = 0.5f;
		const Vec3 c[8] = { Vec3(-h, -h, -h), Vec3(h, -h, -h), Vec3(h, h, -h), Vec3(-h, h, -h), Vec3(-h, -h, h), Vec3(h, -h, h), Vec3(h, h, h), Vec3(-h, h, h) };
		for (int k = 0; k < 4; ++k)
		{
			line(c[k], c[(k + 1) % 4]);
			line(c[k + 4], c[(k + 1) % 4 + 4]);
			line(c[k], c[k + 4]);
		}
		break;
	}
	case ShapeType::Circle:
		circle(Vec3(0, 0, 0), ShapeRadius, 0, 0.0f, arc);
		break;
	case ShapeType::Edge:
		line(Vec3(-ShapeRadius, 0, 0), Vec3(ShapeRadius, 0, 0));
		break;
	}
}

// ------------------------------------------------------------------ 저장
namespace
{
	json Vec3Json(const Vec3& v) { return json::array({ v.x, v.y, v.z }); }
	Vec3 Vec3From(const json& j, const Vec3& def)
	{
		return j.is_array() && j.size() >= 3 ? Vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>()) : def;
	}
	template <typename T>
	void Read(const json& j, const char* key, T& out)
	{
		if (j.contains(key))
			out = j[key].get<T>();
	}
}

GENERATE_COMPONENT_FUNC_TOJSON(ParticleSystem)
{
	json j;
	SERIALIZE_TYPE(j, ParticleSystem);
	j["main"] = {
		{ "duration", Duration }, { "looping", Looping }, { "prewarm", Prewarm }, { "startDelay", StartDelay },
		{ "startLifetime", StartLifetime }, { "startSpeed", StartSpeed }, { "startSize", StartSize }, { "startRotation", StartRotation },
		{ "flipRotation", FlipRotation }, { "startColor", StartColor }, { "gravityModifier", GravityModifier },
		{ "simulationSpace", SimulationSpace }, { "simulationSpeed", SimulationSpeed }, { "playOnAwake", PlayOnAwake },
		{ "maxParticles", MaxParticles }, { "autoRandomSeed", AutoRandomSeed }, { "randomSeed", RandomSeed }, { "stopAction", (int)OnStop } };
	json bursts = json::array();
	for (const Burst& b : Bursts)
		bursts.push_back({ { "time", b.Time }, { "count", b.Count }, { "cycles", b.Cycles }, { "interval", b.Interval }, { "probability", b.Probability } });
	j["emission"] = { { "enabled", EmissionEnabled }, { "rateOverTime", RateOverTime }, { "rateOverDistance", RateOverDistance }, { "bursts", bursts } };
	j["shape"] = { { "enabled", ShapeEnabled }, { "type", (int)Shape }, { "angle", ShapeAngle }, { "radius", ShapeRadius },
		{ "radiusThickness", ShapeRadiusThickness }, { "arc", ShapeArc }, { "emitFrom", ConeEmitFrom }, { "length", ShapeLength },
		{ "position", Vec3Json(ShapePosition) }, { "rotation", Vec3Json(ShapeRotation) }, { "scale", Vec3Json(ShapeScale) },
		{ "randomizeDirection", RandomizeDirection }, { "spherizeDirection", SpherizeDirection } };
	j["velocityOverLifetime"] = { { "enabled", VelocityEnabled }, { "x", VelocityX }, { "y", VelocityY }, { "z", VelocityZ },
		{ "space", VelocitySpace }, { "speedModifier", SpeedModifier } };
	j["limitVelocityOverLifetime"] = { { "enabled", LimitEnabled }, { "speed", LimitSpeed }, { "dampen", LimitDampen }, { "drag", LimitDrag } };
	j["forceOverLifetime"] = { { "enabled", ForceEnabled }, { "x", ForceX }, { "y", ForceY }, { "z", ForceZ }, { "space", ForceSpace } };
	j["colorOverLifetime"] = { { "enabled", ColorEnabled }, { "color", ColorOverLifetime } };
	j["sizeOverLifetime"] = { { "enabled", SizeEnabled }, { "size", SizeOverLifetime } };
	j["rotationOverLifetime"] = { { "enabled", RotationEnabled }, { "angularVelocity", AngularVelocity } };
	j["noise"] = { { "enabled", NoiseEnabled }, { "strength", NoiseStrength }, { "frequency", NoiseFrequency }, { "scrollSpeed", NoiseScrollSpeed },
		{ "damping", NoiseDamping }, { "octaves", NoiseOctaves }, { "octaveMultiplier", NoiseOctaveMultiplier }, { "octaveScale", NoiseOctaveScale } };
	j["textureSheetAnimation"] = { { "enabled", SheetEnabled }, { "tilesX", SheetTilesX }, { "tilesY", SheetTilesY }, { "animation", SheetAnimation },
		{ "randomRow", SheetRandomRow }, { "rowIndex", SheetRowIndex }, { "frameOverTime", SheetFrameOverTime }, { "startFrame", SheetStartFrame },
		{ "cycles", SheetCycles } };
	j["renderer"] = { { "enabled", RendererEnabled }, { "renderMode", (int)Render }, { "speedScale", SpeedScale }, { "lengthScale", LengthScale },
		{ "texture", Texture }, { "blendMode", (int)Blend }, { "sortMode", (int)Sort }, { "sortingFudge", SortingFudge } };
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(ParticleSystem)
{
	if (j.contains("main"))
	{
		const json& m = j["main"];
		Read(m, "duration", Duration);
		Read(m, "looping", Looping);
		Read(m, "prewarm", Prewarm);
		Read(m, "startDelay", StartDelay);
		Read(m, "startLifetime", StartLifetime);
		Read(m, "startSpeed", StartSpeed);
		Read(m, "startSize", StartSize);
		Read(m, "startRotation", StartRotation);
		Read(m, "flipRotation", FlipRotation);
		Read(m, "startColor", StartColor);
		Read(m, "gravityModifier", GravityModifier);
		Read(m, "simulationSpace", SimulationSpace);
		Read(m, "simulationSpeed", SimulationSpeed);
		Read(m, "playOnAwake", PlayOnAwake);
		Read(m, "maxParticles", MaxParticles);
		Read(m, "autoRandomSeed", AutoRandomSeed);
		Read(m, "randomSeed", RandomSeed);
		OnStop = (StopAction)std::clamp(m.value("stopAction", 0), 0, 2);
	}
	if (j.contains("emission"))
	{
		const json& e = j["emission"];
		Read(e, "enabled", EmissionEnabled);
		Read(e, "rateOverTime", RateOverTime);
		Read(e, "rateOverDistance", RateOverDistance);
		Bursts.clear();
		if (e.contains("bursts") && e["bursts"].is_array())
			for (const json& b : e["bursts"])
			{
				Burst burst;
				Read(b, "time", burst.Time);
				Read(b, "count", burst.Count);
				Read(b, "cycles", burst.Cycles);
				Read(b, "interval", burst.Interval);
				Read(b, "probability", burst.Probability);
				Bursts.push_back(burst);
			}
	}
	if (j.contains("shape"))
	{
		const json& s = j["shape"];
		Read(s, "enabled", ShapeEnabled);
		Shape = (ShapeType)std::clamp(s.value("type", (int)Shape), 0, 5);
		Read(s, "angle", ShapeAngle);
		Read(s, "radius", ShapeRadius);
		Read(s, "radiusThickness", ShapeRadiusThickness);
		Read(s, "arc", ShapeArc);
		Read(s, "emitFrom", ConeEmitFrom);
		Read(s, "length", ShapeLength);
		ShapePosition = Vec3From(s.value("position", json()), ShapePosition);
		ShapeRotation = Vec3From(s.value("rotation", json()), ShapeRotation);
		ShapeScale = Vec3From(s.value("scale", json()), ShapeScale);
		Read(s, "randomizeDirection", RandomizeDirection);
		Read(s, "spherizeDirection", SpherizeDirection);
	}
	if (j.contains("velocityOverLifetime"))
	{
		const json& v = j["velocityOverLifetime"];
		Read(v, "enabled", VelocityEnabled);
		Read(v, "x", VelocityX);
		Read(v, "y", VelocityY);
		Read(v, "z", VelocityZ);
		Read(v, "space", VelocitySpace);
		Read(v, "speedModifier", SpeedModifier);
	}
	if (j.contains("limitVelocityOverLifetime"))
	{
		const json& l = j["limitVelocityOverLifetime"];
		Read(l, "enabled", LimitEnabled);
		Read(l, "speed", LimitSpeed);
		Read(l, "dampen", LimitDampen);
		Read(l, "drag", LimitDrag);
	}
	if (j.contains("forceOverLifetime"))
	{
		const json& f = j["forceOverLifetime"];
		Read(f, "enabled", ForceEnabled);
		Read(f, "x", ForceX);
		Read(f, "y", ForceY);
		Read(f, "z", ForceZ);
		Read(f, "space", ForceSpace);
	}
	if (j.contains("colorOverLifetime"))
	{
		Read(j["colorOverLifetime"], "enabled", ColorEnabled);
		Read(j["colorOverLifetime"], "color", ColorOverLifetime);
	}
	if (j.contains("sizeOverLifetime"))
	{
		Read(j["sizeOverLifetime"], "enabled", SizeEnabled);
		Read(j["sizeOverLifetime"], "size", SizeOverLifetime);
	}
	if (j.contains("rotationOverLifetime"))
	{
		Read(j["rotationOverLifetime"], "enabled", RotationEnabled);
		Read(j["rotationOverLifetime"], "angularVelocity", AngularVelocity);
	}
	if (j.contains("noise"))
	{
		const json& n = j["noise"];
		Read(n, "enabled", NoiseEnabled);
		Read(n, "strength", NoiseStrength);
		Read(n, "frequency", NoiseFrequency);
		Read(n, "scrollSpeed", NoiseScrollSpeed);
		Read(n, "damping", NoiseDamping);
		Read(n, "octaves", NoiseOctaves);
		Read(n, "octaveMultiplier", NoiseOctaveMultiplier);
		Read(n, "octaveScale", NoiseOctaveScale);
	}
	if (j.contains("textureSheetAnimation"))
	{
		const json& t = j["textureSheetAnimation"];
		Read(t, "enabled", SheetEnabled);
		Read(t, "tilesX", SheetTilesX);
		Read(t, "tilesY", SheetTilesY);
		Read(t, "animation", SheetAnimation);
		Read(t, "randomRow", SheetRandomRow);
		Read(t, "rowIndex", SheetRowIndex);
		Read(t, "frameOverTime", SheetFrameOverTime);
		Read(t, "startFrame", SheetStartFrame);
		Read(t, "cycles", SheetCycles);
	}
	if (j.contains("renderer"))
	{
		const json& r = j["renderer"];
		Read(r, "enabled", RendererEnabled);
		Render = (RenderMode)std::clamp(r.value("renderMode", 0), 0, 3);
		Read(r, "speedScale", SpeedScale);
		Read(r, "lengthScale", LengthScale);
		Read(r, "texture", Texture);
		Blend = (BlendMode)std::clamp(r.value("blendMode", 0), 0, 1);
		Sort = (SortMode)std::clamp(r.value("sortMode", 0), 0, 3);
		Read(r, "sortingFudge", SortingFudge);
	}
}
