#pragma once
#include "Component.h"

// 지형 스플라인 (Atlas / Gaia 의 스플라인 스탬프처럼 곡선을 따라 지형을 바꾼다).
//  - Road  : 곡선 높이(점의 y)로 평평하게 깎고 메워 길을 내고, 바깥 Falloff 폭으로 원래 지형과 이어 붙인다 (성토·절토 경사)
//  - Canyon: 곡선을 따라 Depth 만큼 판다 (바닥 폭 = 점의 Width, 벽 = Falloff)
//  - Ridge : 곡선을 따라 Depth 만큼 올린다 (제방, 능선)
//  지형 생성기가 켜진 지형에 Order 순으로 합친다. Canyon/Ridge 는 기본으로 침식 전(벽이 자연스럽게 깎인다), Road 는 침식 뒤(매끈하게).
//  Paint Layer / Color 로 길 표면을 칠한다. 점 편집은 Edit Points (물과 같은 Scene 뷰 편집기)
class TerrainSpline : public Component
{
public:
	enum class Mode { Road, Canyon, Ridge, Count };

	struct Point
	{
		Vec3 Position;          // 로컬 (Transform 기준)
		float Width = 8.0f;     // 길 폭 / 협곡 바닥 폭 / 능선 꼭대기 폭 (m)
	};

	Mode SplineMode = Mode::Road;
	std::vector<Point> Points;
	float Falloff = 12.0f;          // 바깥 경사 폭 (m)
	float Depth = 25.0f;            // Canyon 깊이 / Ridge 높이 (m)
	bool BeforeErosion = false;     // 침식 필터 전에 합친다
	int PaintLayer = 2;             // 칠할 지형 레이어 (-1 = 칠하지 않음)
	bool PaintColor = true;         // 컬러 맵에 색도 칠한다
	float Color[3] = { 0.42f, 0.36f, 0.29f };
	int Order = 0;
	int Revision = 0;

	TerrainSpline();
	virtual ~TerrainSpline();

	static const std::vector<TerrainSpline*>& All();
	bool IsActiveSpline() const;
	static const char* ModeName(Mode m);
	void ResetShape();
	void ApplyModeDefaults();       // 모드에 맞는 기본값 (침식 전/후, 칠하기, 깊이)
	void SnapToGround();            // 점 높이 = 지면 (Road: 앞뒤 점과 고르게)
	XMMATRIX WorldMatrix() const;

	struct CurveSample { Vec3 Position; Vec3 Tangent; float Width; };
	std::vector<CurveSample> Curve(float step) const;   // 월드, Catmull-Rom

	virtual void OnInspectorGUI() override;
	virtual void OnDrawGizmos() override;
	virtual bool UsesUnityInspector() const override { return true; }
	virtual const char* InspectorIconName() const override { return "terrain_paint"; }

	GENERATE_COMPONENT_BODY(TerrainSpline)
};

REGISTER_COMPONENT(TerrainSpline)
