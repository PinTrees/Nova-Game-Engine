#pragma once
#include "Component.h"

class Mesh;
class MeshRenderer;

// Unity Splines 의 Spline Container: 로컬 점 (매듭) 을 잇는 곡선. Smooth = Catmull-Rom (centripetal — 고리 · 뾰족함 없이),
//  Linear = 직선 (성벽 모서리처럼 꺾인 길). Closed 면 끝이 처음으로 돌아온다.
//  거리로 묻는다 (Evaluate): 곡선을 잘게 나눈 표 (누적 길이) 를 Revision 이 바뀔 때만 다시 만든다. 좌표는 이 오브젝트의 로컬
class SplineContainer : public Component
{
public:
	enum class Interpolation { Smooth, Linear, Count };

	std::vector<Vec3> Knots;
	bool Closed = false;
	Interpolation Mode = Interpolation::Smooth;
	int Revision = 1;   // 점 · 모양이 바뀔 때 +1 (Spline Instantiate 가 다시 만든다)

	SplineContainer();
	virtual ~SplineContainer();

	float Length();
	// 거리 d (0 ~ Length, Closed 면 감긴다) 의 로컬 위치 · 진행 방향 (정규화)
	void Evaluate(float d, Vec3& position, Vec3& tangent);
	XMMATRIX WorldMatrix() const;
	void ResetShape();

	virtual void OnInspectorGUI() override;
	virtual void OnDrawGizmos() override;
	virtual bool UsesUnityInspector() const override { return true; }
	virtual const char* InspectorIconName() const override { return "terrain_paint"; }
	virtual std::string InspectorTitle() const override { return "Spline Container"; }

	GENERATE_COMPONENT_BODY(SplineContainer)

private:
	struct Sample { Vec3 P; float D; };
	std::vector<Sample> m_Samples;
	int m_SampledRevision = 0;
	void Resample();
};

REGISTER_COMPONENT(SplineContainer)

// Unity Splines 의 Spline Instantiate (+ 휘게 하기): 같은 오브젝트의 Spline Container 를 따라 프리팹을 놓는다.
//  - Repeat: 프리팹 인스턴스를 그대로 (휘지 않음) 곡선을 따라 반복 — 판자 길 · 울타리 · 가로등
//  - Deform: 프리팹의 메시를 곡선을 따라 휘게 — 성벽 · 길 · 다리. 진행 방향으로 잘게 나눠 (Deform Resolution) 상자도 휜다
//  만든 조각은 저장하지 않고 계층 창에서도 숨긴다 (스플라인 · 설정이 바뀌면 다시 만든다). Bake 로 보통 오브젝트로 바꾼다 (Repeat)
class SplineInstantiate : public Component
{
public:
	enum class Method { Repeat, Deform, Count };
	enum class SpacingMode { ItemLength, Distance, Count, ModeCount };
	enum class Axis { X, Z, NegX, NegZ, Count };

	struct Item
	{
		std::string Prefab;   // .prefab 경로 (Assets/... 또는 Resources/Packages/...)
		float Weight = 1.0f;  // 여럿이면 무작위로 고를 비율
	};

	std::vector<Item> Items;
	Method Placement = Method::Repeat;
	SpacingMode Spacing = SpacingMode::ItemLength;
	float Gap = 0.0f;               // ItemLength: 조각 사이 틈 (m)
	float Distance = 2.0f;          // Distance: 조각 사이 거리 (m)
	int Count = 10;                 // Count: 조각 수
	bool FitToLength = true;        // 곡선 길이에 꼭 맞게 (Deform 은 늘이고 줄인다, Repeat 은 간격을 맞춘다)
	Axis ForwardAxis = Axis::X;     // 프리팹의 앞 (곡선을 따라갈) 축 — 프로토타입 벽은 X, 판자처럼 가로로 놓을 것은 Z
	bool KeepUpright = true;        // 경사에서도 세로를 그대로 (성벽 · 울타리). 끄면 경사를 따라 기운다 (길 · 판자)
	float DeformResolution = 0.5f;  // Deform: 진행 방향으로 나눌 길이 (m)
	bool GenerateColliders = true;  // Deform: 휜 조각마다 Mesh Collider (캐릭터가 걷고 부딪힌다 — Repeat 은 프리팹의 콜라이더 그대로)
	Vec3 Offset = Vec3::Zero;       // 곡선 기준 (옆 X · 위 Y · 앞 Z) 으로 옮기기
	float RotationY = 0.0f;         // 조각마다 더 돌리기 (도)
	float RandomYaw = 0.0f;         // Repeat: 무작위 회전 (± 도)
	float RandomScale[2] = { 1.0f, 1.0f };   // Repeat: 무작위 크기 (최소 · 최대)
	int Seed = 1;
	int Revision = 1;               // 설정이 바뀔 때 +1

	SplineInstantiate();
	virtual ~SplineInstantiate();

	void Rebuild();                 // 지금 바로 다시 만든다
	int Bake();                     // 만든 조각을 보통 (저장되는) 오브젝트로 — 돌려주는 값 = 바꾼 수. Deform 은 메시 파일이 없어 하지 않는다 (-1)
	nlohmann::json Info();          // 길이 · 조각 수 · 정점 수 (CLI)

	virtual void Start() override { EnsureBuilt(); }
	virtual void Update() override { EnsureBuilt(); }
	virtual void _Editor_Update() override { EnsureBuilt(); }
	virtual void OnDestroy() override;
	virtual void OnInspectorGUI() override;
	virtual bool UsesUnityInspector() const override { return true; }
	virtual const char* InspectorIconName() const override { return "terrain_trees"; }
	virtual std::string InspectorTitle() const override { return "Spline Instantiate"; }

	GENERATE_COMPONENT_BODY(SplineInstantiate)

private:
	struct SourcePart
	{
		std::shared_ptr<::Mesh> Source;   // 원본 메시 (공유 — 고치지 않는다)
		nlohmann::json Renderer;       // Mesh Renderer 설정 (재질 · 그림자)
		Matrix ToItem;                 // 그 노드 → 프리팹 루트
	};
	struct SourceItem
	{
		std::string Prefab;
		std::vector<SourcePart> Parts;
		Vec3 Min = Vec3::Zero, Max = Vec3::Zero;   // 프리팹 루트 기준 범위 (앞 축 길이 · 가운데)
		bool Valid = false;
	};
	std::vector<SourceItem> m_Sources;
	std::vector<GameObject*> m_Generated;
	uint64_t m_BuiltKey = 0;
	double m_LastBuild = -1.0;
	int m_Vertices = 0;
	float m_Length = 0.0f;

	void EnsureBuilt();
	void Clear();
	bool LoadSources();
	SplineContainer* Container();
};

REGISTER_COMPONENT(SplineInstantiate)
