#pragma once
#include "Component.h"
#include "ParticleCurves.h"
#include "LightHelper.h"

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

	enum class SubEmitterType { Birth = 0, Collision, Death };
	enum SubEmitterInherit { InheritNothing = 0, InheritColor = 1, InheritSize = 2, InheritRotation = 4 };
	enum class CollisionType { Planes = 0, World };

	// 하위 이미터: 대상 = 자식 GameObject 의 Particle System (fileID). 대상은 스스로 방출하지 않고 이 시스템의 사건 때만 뿜는다
	struct SubEmitter
	{
		SubEmitterType Type = SubEmitterType::Death;
		uint64 Target = 0;
		int Inherit = InheritNothing;   // 비트 조합
		float Probability = 1.0f;
	};

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

	// Collision (Planes: 목록의 GameObject 위치·위쪽(+Y)이 평면, World: 물리 콜라이더 — Play 모드에서만)
	bool CollisionEnabled = false;
	CollisionType CollisionMode = CollisionType::Planes;
	std::vector<uint64> CollisionPlanes;   // GameObject fileID
	MinMaxCurve CollisionDampen = MinMaxCurve(0.0f);
	MinMaxCurve CollisionBounce = MinMaxCurve(1.0f);
	MinMaxCurve CollisionLifetimeLoss = MinMaxCurve(0.0f);
	float CollisionMinKillSpeed = 0.0f;
	float CollisionMaxKillSpeed = 10000.0f;
	float CollisionRadiusScale = 1.0f;
	float CollisionPlaneGizmoSize = 1.0f;   // Scene 뷰에 보이는 평면 격자 크기

	// Sub Emitters
	bool SubEmittersEnabled = false;
	std::vector<SubEmitter> SubEmitters;

	// Texture Sheet Animation
	bool SheetEnabled = false;
	int SheetTilesX = 1, SheetTilesY = 1;
	int SheetAnimation = 0;              // 0 Whole Sheet, 1 Single Row
	bool SheetRandomRow = true;
	int SheetRowIndex = 0;
	MinMaxCurve SheetFrameOverTime = MinMaxCurve::Curve(ParticleCurve::Linear(0.0f, 1.0f));
	MinMaxCurve SheetStartFrame = MinMaxCurve(0.0f);
	float SheetCycles = 1.0f;

	// Trails (입자마다 꼬리: 지나온 위치를 이어 카메라를 향한 띠로)
	bool TrailsEnabled = false;
	float TrailRatio = 1.0f;                     // 꼬리를 가질 입자 비율
	MinMaxCurve TrailLifetime = MinMaxCurve(1.0f);   // 입자 수명에 대한 비율
	float TrailMinVertexDistance = 0.2f;
	bool TrailWorldSpace = false;
	bool TrailDieWithParticles = true;
	bool TrailSizeAffectsWidth = true;
	bool TrailInheritParticleColor = true;
	MinMaxGradient TrailColorOverLifetime;
	MinMaxCurve TrailWidthOverTrail = MinMaxCurve(1.0f);
	MinMaxGradient TrailColorOverTrail;
	std::string TrailTexture = "builtin:Trail";

	// Lights (Unity 의 Lights 모듈): 입자 일부에 점광을 달아 주변을 비춘다 (모닥불·불똥·마법 구슬).
	// Unity 는 Light 프리팹을 받지만 여기서는 색·범위·세기를 직접. 장면의 점광과 합쳐 셰이더 한도(LIGHT_SIZE = 4) 안에서, 그림자는 없음
	bool LightsEnabled = false;
	float LightRatio = 0.2f;              // 0~1: 입자 중 몇 분의 1 에 빛을 단다
	bool LightUseParticleColor = true;    // 빛 색 × 입자 색
	bool LightSizeAffectsRange = true;    // 범위 × 입자 크기
	bool LightAlphaAffectsIntensity = true;
	float LightRange = 3.0f;              // m
	float LightIntensity = 1.0f;
	Vec4 LightColor = Vec4(1.0f, 0.62f, 0.32f, 1.0f);
	int LightMaxLights = 2;
	// 켜진 시스템의 입자 빛을 out 끝에 더한다 (out 이 maxTotal 이 될 때까지)
	static void CollectLights(std::vector<PointLight>& out, int maxTotal);

	// Renderer
	bool RendererEnabled = true;
	RenderMode Render = RenderMode::Billboard;
	float SpeedScale = 0.0f;
	float LengthScale = 2.0f;
	std::string Texture = "builtin:Default-Particle";
	BlendMode Blend = BlendMode::AlphaBlended;
	SortMode Sort = SortMode::None;
	float SortingFudge = 0.0f;
	bool Lit = false;              // Unity 의 Particles/Lit 재질: 해 + 하늘 환경광 (밤에 연기가 빛나 보이지 않게)
	bool SoftParticles = false;    // 장면 표면과 가까울수록 투명하게 (바닥·벽과의 딱딱한 경계 없앰)
	float SoftDistance = 1.0f;     // m: 이 거리 안에서 사라진다

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
		int Trail = -1;              // m_Trails 칸 (-1 = 꼬리 없음)
		float BirthAccumulator = 0.0f;   // Birth 하위 이미터 방출 누적
	};

	// 꼬리: 점은 저장 공간(TrailWorld 면 월드, 아니면 시뮬레이션 공간), 머리는 입자의 현재 위치
	struct TrailPoint { Vec3 Position; float Time; };
	struct Trail
	{
		std::vector<TrailPoint> Points;   // 오래된 것부터
		bool Used = false;
		bool Orphan = false;              // 입자가 죽은 뒤 남은 꼬리 (Die with Particles 꺼짐)
		float Lifetime = 1.0f;            // 초
		float Width = 1.0f;
		Vec4 Color = Vec4(1, 1, 1, 1);
		Vec3 Head;                        // 마지막 머리 위치 (Orphan 일 때 사용)
	};

	ParticleSystem();
	virtual ~ParticleSystem();

	virtual void Start() override;
	virtual void OnDestroy() override;
	// Unity: 오브젝트를 끄면 입자를 지우고 멈춘다, 다시 켜면 Play On Awake 로 처음부터
	virtual void OnHierarchyActiveChanged(bool active) override;

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
	bool IsAlive() const { return m_Playing || !m_Particles.empty() || m_OrphanTrails > 0; }
	int ParticleCount() const { return (int)m_Particles.size(); }
	float GetTime() const { return m_Time; }
	void SetTime(float t) { m_Time = std::clamp(t, 0.0f, Duration); }
	const std::vector<Particle>& Particles() const { return m_Particles; }
	const std::vector<Trail>& Trails() const { return m_Trails; }
	bool TrailsInWorld() const { return TrailWorldSpace || SimulationSpace == 1; }
	float TotalTime() const { return m_TotalTime; }
	// 하위 이미터로 쓰이는지 (부모 쪽 Particle System 의 Sub Emitters 목록에 있음) — 그러면 스스로 방출하지 않는다
	bool IsSubEmitterTarget();
	// 부모의 사건(탄생/충돌/소멸)으로 월드 위치에서 방출
	void EmitFromParent(const Vec3& worldPosition, int count, const Vec4* color, float sizeScale, float rotation);
	// Death / Collision 사건 때 뿜는 개수: Bursts 합 (없으면 Rate over Time 1초 분량)
	int SubEmitterBurstCount();
	// 기즈모·Inspector 용: 충돌 평면의 GameObject
	GameObject* FindSceneObject(uint64 fileID);

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
	virtual void RemapFileIDs(const std::unordered_map<uint64, uint64>& map) override;

private:
	std::vector<Particle> m_Particles;
	std::vector<Trail> m_Trails;
	std::vector<int> m_FreeTrails;
	int m_OrphanTrails = 0;
	// Advance 때 한 번 찾아 둔다 (fileID → 오브젝트)
	std::vector<std::pair<const SubEmitter*, ParticleSystem*>> m_SubTargets;
	std::vector<GameObject*> m_PlaneObjects;
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
	void SpawnParticle(const Vec3& simPosition, Vec3 simDirection, float systemT, const Vec4* colorMul, float sizeScale, float rotationAdd);
	void Advance(float dt);
	void RunStopAction();
	int AllocTrail(const Particle& p);
	void ReleaseTrail(int index, bool keepAsOrphan);
	void UpdateTrail(Particle& p, float t);
	void UpdateOrphanTrails();
	bool Collide(Particle& p, const Vec3& oldSimPos, const Matrix& toWorld, const Matrix& toSim, float t, Vec3& hitWorld);
	void FireSubEmitters(SubEmitterType type, const Particle& p, const Vec3& worldPosition);
	void ClearTrails();
	Matrix ShapeMatrix() const;
	void ShapeSample(Vec3& position, Vec3& direction);

	void DrawMainModule();
	void DrawModules();

	GENERATE_COMPONENT_BODY(ParticleSystem)
};

REGISTER_COMPONENT(ParticleSystem)
