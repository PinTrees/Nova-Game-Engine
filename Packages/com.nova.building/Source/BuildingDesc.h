#pragma once
#include <string>
#include <vector>
#include <map>
#include <nlohmann/json.hpp>

// 건축물 생성기의 설정 한 벌 (Building 컴포넌트 = BuildingDesc 하나). 양식 (프리셋) 에서 시작해 형태 · 벽 · 창 · 문 · 지붕 · 장식 · 색을
//  모두 값으로 바꾼다 — 메시 · 텍스처 파일 없이 BuildingGenerator 가 식으로 짓고 BuildingTextures 가 재질을 식으로 굽는다.
//  중세 판타지 (오두막 · 튜더 · 여관 · 성 · 탑 · 스타브 교회) 와 유럽 양식 (로마 · 고딕 · 바로크 · 알프스 · 지중해 · 북유럽)
struct BuildingDesc
{
	// ---- 종류
	enum class Plan { Rectangle, LShape, TShape, UShape, Courtyard, Cross, Round, Bridge, Count };  // 평면 (Bridge = 돌 아치 다리)
	enum class Roof { Gable, Hip, Mansard, Gambrel, Flat, Shed, Pyramid, Cone, Dome, Onion, Tiered, Count };
	enum class WallMat { Rubble, Ashlar, Brick, Plaster, Stucco, Planks, Logs, Marble, Count };       // 벽 재질
	enum class RoofMat { ClayTiles, Slate, Thatch, Shingles, Copper, Lead, Turf, Count };           // 지붕 재질
	enum class Window { None, Rectangular, Arched, Gothic, Round, Mullioned, Baroque, Slit, Count };  // 창 모양
	enum class Door { Plank, Arched, Double, Grand, Count };
	enum class Timber { None, Grid, Cross, Chevron, Tudor, CloseStud, Count };                       // 반목조 무늬 (CloseStud = 촘촘한 샛기둥)
	enum class TowerShape { Round, Octagon, Square, Count };

	int Style = 0;          // 마지막으로 고른 양식 (프리셋 번호 — 표시 · 무작위 범위)
	int Seed = 1;           // 작은 변화 (덧창 열림 · 굴뚝 자리 · 색 얼룩)

	// ---- 형태
	Plan Shape = Plan::Rectangle;
	float Width = 10.0f;        // X (m) — 앞면 (-Z 쪽) 너비
	float Depth = 7.0f;         // Z (m)
	float WingWidth = 0.55f;    // 날개 폭 (깊이에 대한 비율 — L · T · U · 십자)
	float WingLength = 0.7f;    // 날개 길이 (너비에 대한 비율)
	int Floors = 2;
	float FloorHeight = 3.0f;
	float Plinth = 0.5f;        // 밑돌 높이 (m)
	float Jetty = 0.0f;         // 위층마다 밖으로 내민 길이 (m — 튜더 집)
	float Taper = 0.0f;         // 둥근 평면 (탑) 의 위로 좁아짐 (0 ~ 0.5)

	// ---- 탑 (모서리)
	int Towers = 0;             // 0 · 1 (앞 왼쪽) · 2 (앞 두 모서리) · 4
	TowerShape TowerKind = TowerShape::Round;
	float TowerRadius = 2.2f;
	float TowerExtra = 4.0f;    // 몸채보다 더 높은 만큼 (m)
	Roof TowerRoof = Roof::Cone;

	// ---- 벽
	WallMat GroundWall = WallMat::Rubble;     // 1 층
	WallMat UpperWall = WallMat::Plaster;     // 2 층 위
	Timber Frame = Timber::None;              // 반목조 (회벽 위층에)
	float TimberWidth = 0.2f;
	bool Quoins = false;          // 모서리 돌
	bool StringCourse = false;    // 층 사이 띠
	bool Cornice = false;         // 처마 돌림띠
	bool Pilasters = false;       // 벽기둥 (창 사이)
	bool Buttresses = false;      // 버팀벽 (고딕)
	bool Crenellations = false;   // 총안 흉벽 (평지붕 · 탑)
	bool Portico = false;         // 앞 주랑 (기둥 + 박공 — 로마 · 바로크)
	int PorticoColumns = 6;
	bool Peristyle = false;       // 둘레 기둥 (로마 신전)
	bool Arcade = false;          // 1 층이 열린 아치 (시장)
	bool Balcony = false;         // 2 층 앞 나무 발코니 (알프스)

	// ---- 창 · 문
	Window WindowStyle = Window::Rectangular;
	Window GroundWindowStyle = Window::Rectangular;
	float WindowWidth = 0.9f;
	float WindowHeight = 1.3f;
	float WindowSpacing = 2.6f;   // 창 사이 (중심 간격, m)
	float SillHeight = 0.95f;     // 층 바닥에서 창턱까지
	bool Shutters = false;
	bool Lattice = true;          // 납틀 마름모 유리
	bool WindowPediments = false; // 창 위 작은 박공 (바로크)
	bool RoseWindow = false;      // 앞 박공의 둥근 창 (고딕)
	Door DoorStyle = Door::Plank;
	float DoorWidth = 1.2f;
	float DoorHeight = 2.2f;

	// ---- 지붕
	Roof RoofStyle = Roof::Gable;
	RoofMat RoofMaterial = RoofMat::ClayTiles;
	float RoofPitch = 45.0f;      // 도
	float Overhang = 0.4f;        // 처마 (m)
	bool CrowSteps = false;       // 계단 박공 (북유럽 · 네덜란드)
	int Dormers = 0;              // 앞쪽 지붕창 수
	int Chimneys = 1;
	bool DragonHeads = false;     // 박공 끝 용머리 (북유럽 · 스타브)
	float SpireHeight = 0.0f;     // 첨탑 (가운데 · 탑 지붕 위, m)
	bool CentralDome = false;     // 가운데 돔 (바로크)
	bool Sails = false;           // 풍차 날개

	// ---- 세부 (중세 판타지 — 굵은 목재 · 석조 · 소품)
	bool Brackets = false;        // 처마 까치발 + 서까래 끝 + 벽 위 도리 + 박공의 도리 끝
	int Bargeboards = 0;          // 박공 판: 0 없음, 1 곧은 판, 2 조각 (물결 아래 끝, 꼭대기에서 엇갈려 솟음)
	bool Finials = false;         // 박공 꼭대기 장식 기둥
	bool PaintedGables = false;   // 박공 면에 칠한 무늬 판 (북유럽)
	bool CornerPosts = false;     // 굵은 모서리 기둥 (목조 벽)
	bool Porch = false;           // 문 앞 지붕 (기둥 둘 + 작은 박공 지붕)
	bool Lanterns = false;        // 문 양옆 등
	int Banners = 0;              // 앞면 깃발 수 (창 사이)
	float Weathering = 0.4f;      // 1 층 벽 아래의 때 · 물 얼룩 · 이끼 (0 ~ 1)
	float Irregularity = 0.3f;    // 손으로 지은 불규칙 (0 ~ 1): 위층이 기울고 용마루가 처지고 벽이 조금 휜다, 창 · 문 자리도 조금씩
	bool Annex = false;           // 옆에 붙인 낮은 헛간 (외쪽 지붕) — 비대칭
	bool Terrace = false;         // 앞의 지붕 덮인 테라스 (마루 · 난간 · 기둥)
	bool FlowerBoxes = false;     // 창 아래 꽃 상자
	bool ShopSign = false;        // 문 옆에 내건 간판
	int Props = 0;                // 소품: 0 없음, 1 장작 · 통, 2 + 상자 · 통 더미
	float RoofCurve = 0.0f;       // 종 모양 지붕 (0 ~ 0.6): 용마루 쪽이 가파르고 처마가 들린다 — 박공 · 박공 판 · 원뿔 탑 지붕 (마녀 모자)
	int Turrets = 0;              // 앞 모서리의 둥근 내민 탑 (굽은 받침 + 마녀 모자 지붕): 0 · 1 · 2
	bool StairTower = false;      // 뒤쪽 둥근 계단 탑
	int Oriels = 0;               // 위층 앞의 내민 창 (세 면 · 굽은 까치발 · 작은 지붕): 0 ~ 3
	float Rustic = 0.6f;          // 손 다듬은 나무 · 돌 (0 ~ 1): 들보 · 기둥이 휘고 굵기가 고르지 않다, 모서리를 깎는다, 돌은 모서리 · 크기가 고르지 않다 (0 = 반듯한 상자)
	float Canal = 0.0f;           // 물 위의 집 · 다리: 길 (y 0) 아래 물 높이 (m). 0 = 땅 위. 기초 돌벽이 물 밑까지 내려가고 앞에 선착장 (물 계단 · 말뚝)
	bool ShopFront = false;       // 1 층 앞 넓은 가게 창 (낮은 창턱 · 납틀 유리)
	int Arches = 3;               // 다리 아치 수 (Plan::Bridge — 길이 = Width, 폭 = Depth)

	// ---- 색 (감마) — 재질 텍스처 (밝은 중성 무늬) 에 곱한다
	float WallColor[3] = { 0.62f, 0.58f, 0.52f };
	float UpperColor[3] = { 0.9f, 0.86f, 0.76f };
	float TrimColor[3] = { 0.72f, 0.68f, 0.6f };      // 밑돌 · 띠 · 기둥 · 창턱
	float TimberColor[3] = { 0.24f, 0.17f, 0.12f };
	float RoofColor[3] = { 0.62f, 0.32f, 0.22f };
	float FrameColor[3] = { 0.3f, 0.22f, 0.16f };     // 창틀 · 문틀
	float ShutterColor[3] = { 0.2f, 0.36f, 0.24f };
	float DoorColor[3] = { 0.36f, 0.24f, 0.15f };
	float GlassColor[3] = { 0.34f, 0.42f, 0.44f };
	float MetalColor[3] = { 0.35f, 0.58f, 0.5f };     // 구리 녹 · 납
	float OrnamentColor[3] = { 1.0f, 1.0f, 1.0f };    // 칠한 무늬 판 (흰색 = 무늬 본래의 빨강 · 청록 · 황토)
	float BannerColor[3] = { 0.56f, 0.12f, 0.1f };    // 깃발

	// ---- 재질 (칸 → 텍스처 팩 id, 예 {"roof": "roof/thatch-mossy"}). 없으면 벽 · 지붕 재질 종류의 기본 id, "procedural" = 식으로 굽는 것
	//  칸 이름: wallLower · wallUpper · trim · timber · roof · glass · frame · shutter · door · metal · fabric · ornament · banner · plants
	std::map<std::string, std::string> Materials;

	void ApplyStyle(int style);          // 양식 (seed 는 그대로)
	void Randomize(int seed);            // 같은 양식 안에서 크기 · 층 · 색 · 장식을 조금씩
	static const char* const* StyleNames(int& count);
	static const char* StyleDescription(int style);
	static int FindStyle(const std::string& name);   // 대소문자 무시, 없으면 -1

	nlohmann::json ToJson() const;
	void FromJson(const nlohmann::json& j);
	size_t Hash() const;
	float WallHeight() const { return Plinth + Floors * FloorHeight; }
};
