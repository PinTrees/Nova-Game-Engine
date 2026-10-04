#pragma once
#include "Component.h"
#include "ParticleCurves.h"
#include <deque>

// Unity 의 Line Renderer · Trail Renderer (Component > Effects, GameObject > Effects > Line · Trail).
//  - 점들을 잇는 띠를 CPU 가 프레임마다 만든다 (카메라를 향하거나 Transform Z 를 향함), Particle System 과 같은 투명 패스에서
//    먼 것부터 함께 그린다 (43. Particle.fx 의 Trail 기법 — 정점 = 위치 · UV · 색)
//  - Width (곡선 × Width Multiplier) · Color (그라디언트) 는 선을 따라 길이 비율 0 → 1 로 (Trail = 물체 쪽이 0)
//  - Corner Vertices = 꺾인 곳을 둥글게 (0 = 뾰족한 이음), End Cap Vertices = 끝을 둥글게
//  - Texture Mode: Stretch (선 전체에 한 번) · Tile (월드 길이마다 반복) · Distribute Per Segment · Repeat Per Segment
//  - 재질 = 텍스처 + Blend (Particle System 의 Renderer 와 같은 방식). 텍스처가 비면 흰색 (Unity 의 Default-Line)
class NOVA_API LineRendererBase : public Component
{
public:
	enum Alignment { AlignView = 0, AlignTransformZ = 1 };
	enum TextureModes { Stretch = 0, Tile = 1, DistributePerSegment = 2, RepeatPerSegment = 3 };

	MinMaxCurve WidthCurve = MinMaxCurve::Curve(ParticleCurve::Constant(1.0f));
	float WidthMultiplier = 1.0f;
	MinMaxGradient ColorGradient;
	int CornerVertices = 0;
	int CapVertices = 0;
	int AlignmentMode = AlignView;
	int TextureMode = Stretch;
	float TextureScale[2] = { 1.0f, 1.0f };
	std::string Texture;           // 비면 흰색
	int Blend = 0;                 // 0 Alpha Blended, 1 Additive

	LineRendererBase();
	~LineRendererBase() override;
	static const std::vector<LineRendererBase*>& All();

	// 그릴 점 (월드). Trail 은 물체 쪽부터
	virtual void WorldPoints(std::vector<Vec3>& out) const = 0;
	virtual bool IsLoop() const { return false; }
	bool IsVisible() const;
	GameObject* Owner() const { return m_pGameObject; }

	// 선을 따라 t (0 ~ 1) 에서의 너비 · 색 — Unity 의 startWidth · endWidth · startColor · endColor 도 이것으로
	float WidthAt(float t) const;
	Vec4 ColorAt(float t) const;
	void SetEndWidth(bool end, float width);
	void SetEndColor(bool end, const Vec4& color);

	bool UsesUnityInspector() const override { return true; }

protected:
	void StyleToJson(json& j) const;
	void StyleFromJson(const json& j);
	void DrawStyleInspector();   // Width · Color · Corner / Cap Vertices · Alignment · Texture Mode · Materials
};

class NOVA_API LineRenderer : public LineRendererBase
{
public:
	std::vector<Vec3> Positions = { Vec3(0, 0, 0), Vec3(0, 0, 1) };
	bool UseWorldSpace = true;
	bool Loop = false;

	void WorldPoints(std::vector<Vec3>& out) const override;
	bool IsLoop() const override { return Loop; }
	void OnInspectorGUI() override;
	const char* InspectorIconName() const override { return "particle_system"; }

	GENERATE_COMPONENT_BODY(LineRenderer)
};
REGISTER_COMPONENT(LineRenderer)

class NOVA_API TrailRenderer : public LineRendererBase
{
public:
	float Time = 5.0f;              // 점이 사라지기까지 (초)
	float MinVertexDistance = 0.1f; // 이만큼 움직여야 새 점
	bool Autodestruct = false;      // Play 중 Time 동안 가만히 있으면 GameObject 를 지운다
	bool Emitting = true;

	struct Point { Vec3 Position; double Time; };

	void WorldPoints(std::vector<Vec3>& out) const override;
	void OnInspectorGUI() override;
	void Start() override;
	const char* InspectorIconName() const override { return "particle_system"; }

	// 점 (오래된 것부터 — Unity 의 GetPosition 0 = 가장 오래된 점)
	const std::deque<Point>& Points() const { return m_Points; }
	void Clear() { m_Points.clear(); }
	void AddPosition(const Vec3& p);
	void SetPosition(int index, const Vec3& p);

	// 프레임마다 (App 루프): Play 중이면 게임 시간, 아니면 에디터 시간으로 점을 더하고 오래된 점을 뺀다
	static void UpdateAll();

private:
	void Tick(double now);
	std::deque<Point> m_Points;
	bool m_HadPoints = false;

	GENERATE_COMPONENT_BODY(TrailRenderer)
};
REGISTER_COMPONENT(TrailRenderer)

// 띠 정점 (43. Particle.fx 의 TrailIn — ParticleRenderer 가 Particle System 과 함께 그린다)
namespace LineGeometry
{
	struct Vertex
	{
		XMFLOAT3 Pos;
		XMFLOAT2 UV;
		XMFLOAT4 Color;
	};
	// 렌더러 하나의 띠를 out 에 덧붙인다 (삼각형 목록)
	void Build(const LineRendererBase& line, const Vec3& cameraPosition, std::vector<Vertex>& out);
}
