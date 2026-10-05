#pragma once
#include "Component.h"
#include "VfxAsset.h"
#include <map>

struct VfxGpu;   // GPU 버퍼 (VfxRuntime.cpp)
struct VfxGpuDeleter { void operator()(VfxGpu* gpu) const; };
using VfxGpuPtr = std::unique_ptr<VfxGpu, VfxGpuDeleter>;

// Unity 의 Visual Effect (VFX Graph 를 재생하는 컴포넌트).
//  - .vfx 에셋 (Vfx::Asset) 의 시스템을 GPU 에서 시뮬레이션 · 그린다 (58. VFX.fx — VfxRuntime)
//  - 이벤트: Initial Event (기본 OnPlay) 로 시작, SendEvent 로 시스템의 Start · Stop 이벤트, GPU Event 는 GPU 안에서
//  - Exposed Property 덮어쓰기 (Inspector · SetFloat …) — 블록의 bind 로 값이 들어간다
//  - Unity 처럼 Play 모드가 아니어도 장면에서 재생한다 (에셋을 고치면 바로 다시 시작)
class NOVA_API VisualEffect : public Component
{
public:
	struct OverrideValue
	{
		std::array<float, 4> Value = { 0, 0, 0, 1 };
		bool Enabled = true;
	};

	// ---------------------------------------------------------------- 설정 (저장됨)
	std::string AssetPath;
	std::string InitialEvent = "OnPlay";   // 비우면 스스로 시작하지 않는다 (SendEvent 를 기다림)
	float PlayRate = 1.0f;
	uint32 StartSeed = 0;
	bool ResetSeedOnPlay = true;
	bool Paused = false;
	std::map<std::string, OverrideValue> Overrides;

	VisualEffect();
	virtual ~VisualEffect();

	virtual void Start() override;
	virtual void OnDestroy() override;

	// ---- Unity API ----
	void Play() { SendEvent("OnPlay"); }
	void Stop() { SendEvent("OnStop"); }
	void Reinit();                                   // 모든 파티클을 지우고 Initial Event 부터
	void SendEvent(const std::string& name);
	bool HasProperty(const std::string& name) const;
	bool GetProperty(const std::string& name, std::array<float, 4>& out) const;   // 덮어쓴 값, 없으면 에셋 기본값
	bool SetProperty(const std::string& name, const std::array<float, 4>& value);  // 덮어쓰기 (없는 이름이면 false)
	void ResetOverride(const std::string& name);
	int AliveParticleCount() const { return m_Alive; }   // 몇 프레임 늦은 GPU 값
	int SystemAliveCount(int system) const;
	bool HasAnySystemAwake() const;                  // Spawn 이 도는 시스템이 있다
	std::shared_ptr<const Vfx::Asset> GetAsset() const { return m_Asset; }
	uint64 AssetRevision() const { return m_Revision; }   // 에셋이 바뀔 때마다 오른다 (Vfx::Load)
	const std::string& AssetError() const { return m_AssetError; }
	float TotalTime() const { return m_TotalTime; }
	bool ActiveInHierarchy();

	// 모든 Visual Effect: 이벤트 · Spawn 수 (CPU). GPU 일은 그리기 전에 VfxRuntime 이
	static void UpdateAll();
	static const std::vector<VisualEffect*>& All();
	// 에디터가 채운다 (그래프 창 열기 — 안드로이드 플레이어에는 없음)
	static inline void (*OpenGraph)(const std::string& assetPath) = nullptr;

	virtual void OnInspectorGUI() override;
	virtual bool UsesUnityInspector() const override { return true; }
	virtual const char* InspectorIconName() const override { return "particle_system"; }
	virtual void OnDrawGizmos() override;

	// ---- VfxRuntime 이 쓰는 상태
	struct SystemState
	{
		bool Active = false;        // Spawn 이 도는 중
		float Time = 0.0f;          // 이번 바퀴 안의 시간
		float DelayLeft = 0.0f;
		float RateAccumulator = 0.0f;
		std::vector<int> BurstFired;
		uint32 PendingSpawn = 0;    // 다음 GPU 단계에서 태어날 수
		int Alive = 0;              // GPU 가 센 수 (몇 프레임 늦음)
	};
	std::vector<SystemState>& Systems() { return m_Systems; }
	float TakePendingDt() { const float dt = m_PendingDt; m_PendingDt = 0.0f; return dt; }
	// 화면 밖 (VfxRuntime 이 정한다 — 지난 프레임에 어느 뷰에도 그리지 않았다): Spawn 수 · 시간을 쌓지 않는다 (Unity 의 Culling)
	bool IsCulled() const { return m_Culled; }
	void SetCulled(bool culled) { m_Culled = culled; }
	// 월드 경계 상자 (GPU 가 모은 파티클 자리, 몇 프레임 늦음). 모르면 false
	bool GetWorldBounds(Vec3& mn, Vec3& mx);
	bool TakeResetRequest() { const bool r = m_ResetGpu; m_ResetGpu = false; return r; }
	uint32 NextSeed() { return m_Seed = m_Seed * 1664525u + 1013904223u; }
	VfxGpu* Gpu() { return m_Gpu.get(); }
	void SetGpu(VfxGpuPtr gpu) { m_Gpu = std::move(gpu); }
	void SetAliveCounts(const std::vector<int>& perSystem);
	// 속성 값 (덮어쓰기 → 에셋 기본) — Vfx::Encode 가 묻는다
	const Vfx::PropertySource& Properties() const { return m_Props; }

private:
	struct Props : Vfx::PropertySource
	{
		const VisualEffect* Owner = nullptr;
		bool Get(const std::string& name, std::array<float, 4>& out) const override { return Owner->GetProperty(name, out); }
	} m_Props;

	std::shared_ptr<const Vfx::Asset> m_Asset;
	std::string m_AssetError;
	std::string m_LoadedPath;
	uint64 m_Revision = 0;
	std::vector<SystemState> m_Systems;
	std::vector<std::string> m_Events;
	bool m_Initialized = false;
	bool m_Culled = false;
	bool m_ResetGpu = true;
	float m_PendingDt = 0.0f;
	float m_TotalTime = 0.0f;
	uint32 m_Seed = 1;
	int m_Alive = 0;
	VfxGpuPtr m_Gpu;

	void RefreshAsset();
	void Advance(float dt);
	void StartSystem(int i);

	GENERATE_COMPONENT_BODY(VisualEffect)
};

REGISTER_COMPONENT(VisualEffect)
