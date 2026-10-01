#pragma once
#include "Component.h"
#include "WaterWaves.h"

// 물 (Unreal Water Body 처럼 바다·호수·강을 한 컴포넌트로).
//  - Ocean: Transform 높이의 끝없는 바다. 카메라를 따라가는 LOD 격자 + Gerstner 파도, 지형이 얕으면 파도가 줄어든다
//  - Lake : 점들로 그린 닫힌 윤곽(부드러운 곡선) 안의 수면, 높이 = Transform y
//  - River: 점들을 잇는 곡선을 따라가는 띠. 점마다 폭·깊이·유속, 높이는 점의 y (내려가며 흐른다)
//  모습은 물 프로파일(Resources/Packages/Water/Profiles)이 정한다. 지형 생성기가 켜진 지형은 호수·강 모양으로 파인다(Carve Terrain)
class WaterBody : public Component
{
public:
	enum class Type { Ocean, Lake, River, Count };

	struct Point
	{
		Vec3 Position;                // 로컬 (Transform 기준)
		float Width = 14.0f;          // 강 폭 (m)
		float Depth = 2.5f;           // 강 깊이 (m, 지형 파기)
		float Speed = 1.0f;           // 유속 배율
	};

	Type BodyType = Type::Lake;
	std::string Profile = "Calm Lake";
	std::vector<Point> Points;
	float WaveScale = 1.0f;           // 프로파일 파도 배율
	float FlowSpeed = 1.0f;           // 강 유속 배율
	bool CarveTerrain = true;         // 지형 생성기 지형을 판다 (호수·강)
	float CarveDepth = 6.0f;          // 호수 가운데 깊이 (m)
	float BankWidth = 10.0f;          // 물가 경사 폭 (m)
	int Revision = 0;                 // 점·모양이 바뀌면 증가 (메시 다시 만들기)

	WaterBody();
	virtual ~WaterBody();

	static const std::vector<WaterBody*>& All();
	bool IsActiveBody() const;
	static const char* TypeName(Type t);
	void ResetShape();                // 종류에 맞는 기본 점
	// 지형에 맞추기 (땅 = 생성기가 물로 파기 전 높이): 강 = 점을 땅 - 0.6 m 로, 하류로 갈수록 낮게 / 호수 = 수위를 윤곽 위 가장 낮은 땅 - 0.3 m 로
	void SnapToGround();

	// 곡선 (월드): 호수 = 닫힌 윤곽, 강 = 가운데 선. step 간격(m)으로 Catmull-Rom 을 따라 찍은 점 + 점마다 폭·깊이·유속
	struct CurveSample { Vec3 Position; Vec3 Tangent; float Width, Depth, Speed, Distance; };
	std::vector<CurveSample> Curve(float step) const;
	float SurfaceY() const;           // 바다·호수 수면 (월드)
	XMMATRIX WorldMatrix() const;     // Transform 월드 행렬 (없으면 단위)

	// 물 위 높이 (월드). 이 물 위가 아니면 false. normal / flow(월드 m/s) 도 돌려준다
	bool Sample(const Vec3& world, float& height, Vec3* normal = nullptr, Vec3* flow = nullptr) const;
	// 모든 물 중 가장 높은 수면
	static bool Query(const Vec3& world, float& height, Vec3* normal = nullptr, Vec3* flow = nullptr);
	static bool GroundAt(float x, float z, float& y);   // 땅 높이 (물로 파기 전, 지형이 없으면 false)

	const WaterWaves::Set& Waves() const;   // 프로파일 + WaveScale (바뀌면 다시 만든다)

	virtual void OnInspectorGUI() override;
	virtual void OnDrawGizmos() override;
	virtual bool UsesUnityInspector() const override { return true; }
	virtual const char* InspectorIconName() const override { return "terrain_paint"; }

	GENERATE_COMPONENT_BODY(WaterBody)

private:
	mutable WaterWaves::Set m_Waves;
	mutable uint64_t m_WavesKey = 0;
};

REGISTER_COMPONENT(WaterBody)
