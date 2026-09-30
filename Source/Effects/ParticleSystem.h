#pragma once
#include "Component.h"
#include "ParticleCurves.h"

// Unity 의 Particle System (Shuriken).
//  모듈: Main, Emission(Rate over Time / Distance, Bursts), Shape(Sphere / Hemisphere / Cone / Box / Circle / Edge),
//        Velocity over Lifetime, Limit Velocity over Lifetime, Force over Lifetime, Color over Lifetime,
//        Size over Lifetime, Rotation over Lifetime, Noise, Texture Sheet Animation, Renderer
//  시뮬레이션은 CPU(ParticleSystem::UpdateAll, 매 프레임 App 루프), 그리기는 입자 하나 = 인스턴스 하나(ParticleRenderer).
//  에디터: 선택하면 Play 모드가 아니어도 미리 재생 (Scene 뷰의 Particle Effect 창, ParticleSystemEditor).
class ParticleSystem : public Component
{
public:
	enum class ShapeType { Sphere = 0, Hemisphere, Cone, Box, Circle, Edge };
	enum class RenderMode { Billboard = 0, StretchedBillboard, HorizontalBillboard, VerticalBillboard };
	enum class BlendMode { AlphaBlended = 0, Additive };
	enum class SortMode { None = 0, ByDistance, OldestInFront, YoungestInFront };
	enum class StopAction { None = 0, Disable, Destroy };

	struct Burst
	{
		float Time = 0.0f;
		MinMaxCurve Count = MinMaxCurve(30.0f);
		int Cycles = 1;          // 0 = Infinite
		float Interval = 0.01f;
		float Probability = 1.0f;
	};

	// ---------------------------------------------------------------- 설정 (저장됨)
	// Main
	float Duration = 5.0f;
	bool Looping = true;
	bool Prewarm = false;
	MinMaxCurve StartDelay = MinMaxCurve(0.0f);
	MinMaxCurve StartLifetime = MinMaxCurve(5.0f);
	MinMaxCurve StartSpeed = MinMaxCurve(5.0f);
	MinMaxCurve StartSize = MinMaxCurve(1.0f);
	MinMaxCurve StartRotation = MinMaxCurve(0.0f);   // 도
	float FlipRotation = 0.0f;
	MinMaxGradient StartColor;
	MinMaxCurve GravityModifier = MinMaxCurve(0.0f);
	int SimulationSpace = 0;       // 0 Local, 1 World
	float SimulationSpeed = 1.0f;
	bool PlayOnAwake = true;
	int MaxParticles = 1000;
	bool AutoRandomSeed = true;
	uint32 RandomSeed = 0;
	StopAction OnStop = StopAction::None;

	// Emission
	bool EmissionEnabled = true;
	MinMaxCurve RateOverTime = MinMaxCurve(10.0f);
	MinMaxCurve RateOverDistance = MinMaxCurve(0.0f);
	std::vector<Burst> Bursts;

	// Shape (도형 공간: +Z 로 방출. 기본 GameObject 는 X -90 도라 위로 나간다)
	bool ShapeEnabled = true;
	ShapeType Shape = ShapeType::Cone;
	float ShapeAngle = 25.0f;
	float ShapeRadius = 1.0f;
	float ShapeRadiusThickness = 1.0f;   // 0 = 표면, 1 = 부피 전체
	float ShapeArc = 360.0f;
	int ConeEmitFrom = 0;                // 0 Base, 1 Volume
	float ShapeLength = 5.0f;
	Vec3 ShapePosition = Vec3(0, 0, 0);
	Vec3 ShapeRotation = Vec3(0, 0, 0);  // 도
	Vec3 ShapeScale = Vec3(1, 1, 1);
	float RandomizeDirection = 0.0f;
	float SpherizeDirection = 0.0f;

	// Velocity over Lifetime
	bool VelocityEnabled = false;
	MinMaxCurve VelocityX, VelocityY, VelocityZ;
	int VelocitySpace = 0;               // 0 Local, 1 World
	MinMaxCurve SpeedModifier = MinMaxCurve(1.0f);

	// Limit Velocity over Lifetime
	bool LimitEnabled = false;
	MinMaxCurve LimitSpeed = MinMaxCurve(1.0f);
	float LimitDampen = 1.0f;
	MinMaxCurve LimitDrag = MinMaxCurve(0.0f);

	// Force over Lifetime
	bool ForceEnabled = false;
	MinMaxCurve ForceX, ForceY, ForceZ;
	int ForceSpace = 0;

	// Color over Lifetime
	bool ColorEnabled = false;
	MinMaxGradient ColorOverLifetime;

	// Size over Lifetime
	bool SizeEnabled = false;
	MinMaxCurve SizeOverLifetime = MinMaxCurve::Curve(ParticleCurve::Linear(0.0f, 1.0f));

	// Rotation over Lifetime
	bool RotationEnabled = false;
	MinMaxCurve AngularVelocity = MinMaxCurve(45.0f);   // 도/초

	// Noise
	bool NoiseEnabled = false;
	MinMaxCurve NoiseStrength = MinMaxCurve(1.0f);
	float NoiseFrequency = 0.5f;
	MinMaxCurve NoiseScrollSpeed = MinMaxCurve(0.0f);
	bool NoiseDamping = true;
	int NoiseOctaves = 1;
	float NoiseOctaveMultiplier = 0.5f;
	float NoiseOctaveScale = 2.0f;

	// Texture Sheet Animation
	bool SheetEnabled = false;
	int SheetTilesX = 1, SheetTilesY = 1;
	int SheetAnimation = 0;              // 0 Whole Sheet, 1 Single Row
	bool SheetRandomRow = true;
	int SheetRowIndex = 0;
	MinMaxCurve SheetFrameOverTime = MinMaxCurve::Curve(ParticleCurve::Linear(0.0f, 1.0f));
	MinMaxCurve SheetStartFrame = MinMaxCurve(0.0f);
	float SheetCycles = 1.0f;

	// Renderer
	bool RendererEnabled = true;
	RenderMode Render = RenderMode::Billboard;
	float SpeedScale = 0.0f;
	float LengthScale = 2.0f;
	std::string Texture = "builtin:Default-Particle";
	BlendMode Blend = BlendMode::AlphaBlended;
	SortMode Sort = SortMode::None;
	float SortingFudge = 0.0f;

	// ---------------------------------------------------------------- 실행 중 상태
	struct Particle
	{
		Vec3 Position;          // 시뮬레이션 공간 (Local 이면 오브젝트 기준)
		Vec3 Velocity;          // 적분되는 속도 (시작 속도 + 중력 + 힘)
		Vec3 AnimatedVelocity;  // Velocity over Lifetime (적분하지 않음)
		float Lifetime = 1.0f;
		float Age = 0.0f;
		float StartSize = 1.0f;
		float Size = 1.0f;
		float Rotation = 0.0f;  // 도
		float Direction = 1.0f; // Flip Rotation: -1 이면 반대로 돈다
		Vec4 StartColor = Vec4(1, 1, 1, 1);
		Vec4 Color = Vec4(1, 1, 1, 1);
		float Random[4] = {};   // 입자마다 고정된 난수 (Random Between Two ... 용)
		float SheetFrame = 0.0f;
		int SheetRow = 0;
	};

	ParticleSystem();
	virtual ~ParticleSystem();

	virtual void Start() override;
	virtual void OnDestroy() override;

	// ---- Unity API ----
	void Play(bool withChildren = true);
	void Stop(bool withChildren = true, bool clear = false);   // clear = StopEmittingAndClear
	void Pause(bool withChildren = true);
	void Clear(bool withChildren = true);
	void Emit(int count);
	void Restart();                 // Clear + Play (에디터 Restart 버튼)
	bool IsPlaying() const { return m_Playing && !m_Paused; }
	bool IsPaused() const { return m_Paused; }
	bool IsStopped() const { return !m_Playing && !m_Paused; }   // Unity: Stop 직후에도 true (남은 입자는 IsAlive)
	bool IsEmitting() const { return m_Playing && m_Emitting; }
	bool IsAlive() const { return m_Playing || !m_Particles.empty(); }
	int ParticleCount() const { return (int)m_Particles.size(); }
	float GetTime() const { return m_Time; }
	void SetTime(float t) { m_Time = std::clamp(t, 0.0f, Duration); }
	const std::vector<Particle>& Particles() const { return m_Particles; }

	// dt 초만큼 진행 (SimulationSpeed 적용 전)
	void Simulate(float dt);
	// 시뮬레이션 공간 → 월드
	Matrix SimulationToWorld();
	bool ActiveInHierarchy();
	// 자신 + 자식의 Particle System (Unity 의 withChildren)
	void CollectHierarchy(std::vector<ParticleSystem*>& out);

	// 모든 Particle System 진행: Play 모드면 게임 시간, 아니면 에디터 미리보기(ParticleSystemEditor)
	static void UpdateAll();
	static const std::vector<ParticleSystem*>& All();

	virtual void OnInspectorGUI() override;
	virtual bool UsesUnityInspector() const override { return true; }
	virtual const char* InspectorIconName() const override { return "particle_system"; }
	virtual void OnDrawGizmos() override;
	virtual bool HasEnabledToggle() const override;

private:
	std::vector<Particle> m_Particles;
	bool m_Playing = false;
	bool m_Paused = false;
	bool m_Emitting = false;
	float m_Time = 0.0f;            // 이번 반복 안의 시스템 시간 (0~Duration)
	float m_TotalTime = 0.0f;       // Play 이후 전체 시간 (Noise 스크롤)
	float m_Delay = 0.0f;           // 남은 Start Delay
	float m_RateAccumulator = 0.0f;
	float m_DistanceAccumulator = 0.0f;
	Vec3 m_LastEmitterPosition;
	bool m_HasLastPosition = false;
	uint32 m_Rng = 1;
	bool m_StopActionPending = false;

	float Random01();
	void EmitInternal(int count, float systemT);
	void Advance(float dt);
	void RunStopAction();
	Matrix ShapeMatrix() const;
	void ShapeSample(Vec3& position, Vec3& direction);

	void DrawMainModule();
	void DrawModules();

	GENERATE_COMPONENT_BODY(ParticleSystem)
};

REGISTER_COMPONENT(ParticleSystem)
