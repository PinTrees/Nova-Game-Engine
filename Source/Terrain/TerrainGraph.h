#pragma once
#include "WorldGen.h"
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

// 지형 그래프 (QuadSpinner Gaea 처럼): 노드를 이어 월드 전체 (예: 50 x 50 km) 의 높이를 만든다.
//  - 생성 (Mountain · Ridged · Perlin · Voronoi · Gradient · Radial · Constant) → 조합 · 변형 (Combine · Warp · Terrace · Curve · Clamp ·
//    Normalize · Invert · Blur) → 시뮬레이션 (Erosion = 수력 물방울 침식 — Flow · Deposits · Wear 출력, Thermal = 무너짐) → 마스크 (Slope ·
//    Height · Curvature) → Output (Height · Flow · Deposits)
//  - 빌드 = 월드 해상도 (예: 4097² — 50 km 면 12 m) 로 한 번 (작업 스레드, 진행 · 취소). 결과는 그래프 옆 .build 파일 (16 비트 높이 + 8 비트 맵)
//  - 타일 (예: 1 km · 2049²) 은 저장하지 않고 필요할 때 계산한다: 빌드 높이 (쌍삼차) + 해상도와 상관없는 침식 결 디테일 (위치의 순수 함수) —
//    이웃 타일 경계가 정확히 맞고, 디스크 · 메모리는 빌드 하나만. 스플랫 = 텍스처 규칙 (높이 · 경사 · 물길 · 퇴적 · 굴곡 · 노이즈)
//  - World Terrain (스트리밍) · PCG · 여러 Terrain 오브젝트 (Apply) 가 같은 함수를 쓴다
namespace TerrainGraph
{
	enum class NodeType
	{
		Constant, Gradient, Radial, Perlin, Ridged, Mountain, Voronoi,
		Combine, Warp, Terrace, Curve, Clamp, Normalize, Invert, Blur,
		Erosion, Thermal,
		Slope, HeightMask, Curvature,
		Output,
		// Gaea 식 더 (2026-10-12): 산맥 · 거친 비탈 · 지층 · 골 · 퇴적 · 침식 2 (여러 크기) · 강 · 길 · 색
		MountainRange, Rugged, Stratify, Gullies, Sediment, Erosion2, River, Road, SatMap, ColorBlend,
		Count
	};
	enum class ParamKind { Float, Int, Bool, Enum };
	struct ParamDesc
	{
		const char* Key;
		const char* Label;
		ParamKind Kind;
		float Default, Min, Max;
		const char* Options = nullptr;   // Enum: "A|B|C"
		const char* Tooltip = nullptr;
	};
	// 출력 핀 종류: 높이 (미리 보기 = 음영) · 마스크 (흑백) · 색 (RGB — SatMap)
	enum class PinKind { Height, Mask, Color };
	struct NodeDesc
	{
		NodeType Type;
		const char* Key;        // 저장 이름
		const char* Label;
		const char* Category;   // Primitive · Adjust · Simulate · Data · Output
		std::vector<const char*> Inputs;
		std::vector<std::pair<const char*, PinKind>> Outputs;
		std::vector<ParamDesc> Params;
		const char* Help;
	};
	const std::vector<NodeDesc>& NodeDescs();
	const NodeDesc* FindDesc(NodeType type);

	struct Node
	{
		int Id = 0;
		NodeType Type = NodeType::Perlin;
		float X = 0.0f, Y = 0.0f;
		bool Enabled = true;
		std::string Comment;
		std::vector<float> Values;   // Desc 의 Params 차례
		float Get(const char* key) const;
		void Set(const char* key, float value);
	};
	struct Edge { int From = 0, FromPin = 0, To = 0, ToPin = 0; };

	// 월드 · 빌드 · 타일 (그래프 파일에 같이)
	struct WorldSettings
	{
		float WorldSize = 50000.0f;      // 한 변 (m), 가운데가 원점
		float TileSize = 1000.0f;        // 타일 (Terrain) 한 변 (m) → 50 x 50
		int TileResolution = 2049;       // 가까운 타일 높이맵 (2k)
		float MaxHeight = 1600.0f;       // 높이 0 ~ 1 → 0 ~ MaxHeight (m)
		int BuildResolution = 4097;      // 월드 빌드 해상도 (침식) — 50 km 면 12 m
		int PreviewResolution = 513;     // 편집기 미리 보기
		int Seed = 1;
		// 타일 디테일 (빌드보다 잔 결 — 위치의 순수 함수, 타일 경계가 맞는다)
		float DetailAmplitude = 4.0f;    // m
		float DetailScale = 90.0f;       // 가장 큰 결 (m)
		float DetailErosion = 0.7f;      // 침식 결 (기울기를 따라 골이 패인다, 0 = 보통 fBm)
		float DetailSlope = 1.0f;        // 가파른 곳일수록 거칠게
		float DetailDeposits = 0.7f;     // 퇴적된 골 · 평지는 매끈하게
		float ColorStrength = 0.45f;      // 컬러 맵 (SatMap) 이 텍스처 색을 대신하는 정도 (텍스처는 명암 디테일)

		uint64_t Hash() const;           // 빌드에 드는 값 (해상도 · 크기 · Seed · 높이)
		uint64_t TileHash() const;       // 타일 계산에 드는 값 (디테일 · 타일)
		nlohmann::json ToJson() const;
		void FromJson(const nlohmann::json& j);
	};

	// 텍스처 규칙 (스플랫 4 칸, World Creator · Gaea 의 Texture 처럼): 0 번 = 바탕, 1 ~ 3 번이 차례로 위에 얹힌다
	struct LayerRule
	{
		bool Enabled = true;
		std::string Name;
		std::string Layer;               // .terrainlayer
		float HeightMin = 0.0f, HeightMax = 100000.0f, HeightFalloff = 20.0f;   // m
		float SlopeMin = 0.0f, SlopeMax = 90.0f, SlopeFalloff = 4.0f;          // 도
		float Flow = 0.0f;               // 물길을 얼마나 따를지 (0 = 무시)
		float Deposits = 0.0f;           // 퇴적된 곳 (골 · 선상지)
		float Curvature = 0.0f;          // > 0 오목한 곳, < 0 볼록한 곳 (능선)
		float Noise = 0.3f;              // 경계를 흐트러뜨린다
		float NoiseScale = 40.0f;        // m
		float Strength = 1.0f;
		nlohmann::json ToJson() const;
		void FromJson(const nlohmann::json& j);
	};
	// 한 점의 스플랫 (합 1): 높이 (m), 경사 (도), 물길 · 퇴적 (0..1), 굴곡 (-1..1, + 오목), 노이즈 seed 위치
	void SplatRules(const LayerRule rules[4], float heightM, float slopeDeg, float flow, float deposits, float curvature, float x, float z, int seed, float out[4]);

	struct Graph
	{
		WorldSettings World;
		std::vector<Node> Nodes;
		std::vector<Edge> Edges;
		LayerRule Layers[4];
		int NextId = 1;
		uint64_t Revision = 1;           // 바뀔 때마다 (미리 보기 · World Terrain)

		Node& Add(NodeType type, float x, float y);
		void Remove(int id);
		Node* Find(int id);
		const Node* Find(int id) const;
		bool Connect(int from, int fromPin, int to, int toPin);   // 입력 핀 하나에 선 하나 (바꾼다), 고리 거절
		const Edge* InputOf(int to, int toPin) const;
		int OutputNode() const;          // 첫 Output 노드 (없으면 0)
		void Touch() { ++Revision; }
		uint64_t ContentHash() const;    // 노드 · 선 · 빌드 값 (빌드가 낡았는지)
		uint64_t RulesHash() const;      // 텍스처 규칙 · 타일 값 (다시 빌드하지 않고 타일만)
		nlohmann::json ToJson() const;
		bool FromJson(const nlohmann::json& j);
	};
	Graph MakeDefault(int seed = 1);
	// 경로마다 하나 (편집기 · World Terrain · CLI 가 같은 객체)
	std::shared_ptr<Graph> Load(const std::string& path);
	bool Save(const std::string& path, const Graph& g);

	// ---- 맵 · 평가
	struct Map
	{
		int Res = 0;
		int Channels = 1;                // 3 = 색 (RGB 선형 0..1, 칸마다 3 개)
		std::vector<float> V;
		float At(int x, int z) const { return V[(size_t)std::clamp(z, 0, Res - 1) * Res + std::clamp(x, 0, Res - 1)]; }
		float Bilinear(float fx, float fz) const;   // 격자 좌표
	};
	using MapPtr = std::shared_ptr<const Map>;

	struct EvalContext
	{
		int Res = 513;
		float WorldSize = 50000.0f;
		float MaxHeight = 1600.0f;
		int Seed = 1;
		std::atomic<bool>* Cancel = nullptr;
		std::function<void(float, const char*)> Progress;   // 0..1, 단계
		bool KeepAll = false;            // 모든 노드 출력을 남긴다 (미리 보기 — 노드를 고르면 그 그림)
		float Cell() const { return WorldSize / (Res - 1); }
		float X0() const { return -WorldSize * 0.5f; }
	};
	// 강 · 길의 선 (월드 m): 점마다 위치 · 높이 (m) · 너비 (m)
	struct PathPoint { float X = 0.0f, Z = 0.0f, Y = 0.0f, Width = 0.0f; };
	struct Path { std::vector<PathPoint> Points; int Kind = 0; float Order = 0.0f; };   // Kind 0 = 강, 1 = 길. Order = 강 크기 (0..1)

	struct EvalResult
	{
		std::map<std::pair<int, int>, MapPtr> Outputs;   // (노드, 출력 핀) — KeepAll 일 때
		MapPtr Height, Flow, Deposits, Wear, Color;       // Output 노드의 입력 (Color = RGB)
		std::vector<Path> Rivers, Roads;                  // River · Road 노드가 만든 선
		std::string Error;
		bool Cancelled = false;
		double Ms = 0.0;
		std::map<int, double> NodeMs;
	};
	EvalResult Evaluate(const Graph& g, const EvalContext& ctx);

	// ---- 빌드한 월드 (WorldGen::Source — World Terrain · PCG · Apply)
	struct Built : public WorldGen::Source
	{
		WorldSettings World;
		LayerRule Layers[4];
		int Res = 0;
		std::shared_ptr<const std::vector<float>> Height;     // 0..1
		std::shared_ptr<const std::vector<uint8_t>> Flow, Deposits, Wear, Slope, Curvature;   // 0..255 (Slope = 경사 / 90°, Curvature 128 = 평평)
		std::shared_ptr<const std::vector<uint8_t>> ColorMap;   // RGB8 (sRGB) — 없으면 nullptr (SatMap 을 Output 의 Color 에 이으면)
		std::vector<Path> Rivers, Roads;
		// 선 찾기 격자 (칸 = GridCell m): 칸마다 가까운 선분 (강 · 길) 번호
		struct Segment { float X0, Z0, Y0, W0, X1, Z1, Y1, W1; int Kind; float Order; };
		std::vector<Segment> Segments;
		std::vector<std::vector<int>> Grid;
		int GridN = 0;
		float GridCell = 250.0f;
		void BuildPathIndex();
		// 가장 가까운 강 · 길: 거리 (m, 없으면 1e9), 그 자리 선 높이 (m) · 너비 (m)
		struct Near { float Dist = 1e9f, Y = 0.0f, Width = 0.0f, Order = 0.0f; };
		void NearestPath(float x, float z, Near& river, Near& road) const;
		void ApplyPaths(float x, float z, float footprint, float& heightM) const;   // 강바닥 · 길 노면 (m)
		uint64_t GraphHash = 0, RulesHash = 0, FileStamp = 0;

		float Height01(float x, float z, float footprint) const override;
		WorldGen::Biome BiomeAt(float x, float z, float height01, float slopeCos) const override;
		void Splat(float x, float z, float height01, float slopeCos, uint8_t out[4]) const override;
		bool Color(float x, float z, uint8_t out[4]) const override;
		uint64_t Hash() const override;

		float BaseHeight01(float x, float z) const;    // 빌드 높이만 (쌍삼차)
		float Sample8(const std::vector<uint8_t>& m, float x, float z) const;   // 0..1 (쌍선형)
		std::vector<std::string> LayerPaths() const;
		size_t Bytes() const;
	};
	// 그래프 경로의 빌드 (.build 파일 + 그래프의 지금 규칙). 없으면 nullptr. 규칙 · 타일 값이 바뀌면 같은 맵으로 새 객체
	//  wait = false: 파일을 아직 안 읽었으면 작업 스레드에서 읽기 시작하고 바로 돌아온다 (그동안 옛 빌드 또는 nullptr — World Terrain · 창)
	std::shared_ptr<const Built> GetBuilt(const std::string& graphPath, bool wait = true);
	std::string BuildFilePath(const std::string& graphPath);

	// 빌드 (작업 스레드 하나 — 안에서 ParallelFor). resolution 0 = 그래프 값
	struct BuildStatus
	{
		bool Running = false;
		float Progress = 0.0f;
		std::string Stage;
		double Ms = 0.0;
		std::string Error;
		int Resolution = 0;
		bool Done = false;               // 마지막 빌드가 끝났다 (성공)
	};
	bool StartBuild(const std::string& graphPath, int resolution = 0);
	void CancelBuild(const std::string& graphPath);
	BuildStatus GetBuildStatus(const std::string& graphPath);

	// 여러 Terrain 오브젝트에 (월드 위치 그대로 — 경계가 맞는다): 높이 · 스플랫 · 레이어. 고친 지형 수
	int ApplyToTerrains(const Built& b, const std::vector<class Terrain*>& terrains, bool heights = true, bool splat = true);

	// 미리 보기 그림 (RGBA8): 높이 = 음영 (해가 왼쪽 위) + 높이 색, 마스크 = 흑백, splat = 텍스처 규칙 색
	enum class PreviewMode { Shaded, Mask, Splat, Color };
	void PreviewImage(const Map& height, const Map* mask, PreviewMode mode, const Graph& g, const Map* flow, const Map* deposits, std::vector<uint8_t>& rgba,
		const Map* color = nullptr);
	// 3D 미리 보기 (CPU 광선 행진 — Gaea 의 뷰포트처럼): 높이 + 색 (rgba = PreviewImage 결과, Res²) → w x h 그림. yaw · pitch (도), zoom 1 = 전체
	void Render3D(const Map& height, const std::vector<uint8_t>& albedo, float maxHeight, float worldSize, float yaw, float pitch, float zoom, int w, int h,
		std::vector<uint8_t>& out, const std::atomic<bool>* cancel = nullptr);

	// SatMap 팔레트 (Gaea 의 SatMap 처럼: 위성 사진에서 뽑은 듯한 색 띠). 이름 · 분류 (Rock · Sand · Green · Blue · Color)
	struct Palette { const char* Name; const char* Category; std::vector<uint32_t> Stops; };   // 0xRRGGBB, 고르게 놓인 멈춤점
	const std::vector<Palette>& Palettes();
	const char* PaletteOptions();                        // "A|B|C" (SatMap 의 Palette 값)
	void PaletteColor(int index, float t, float out[3]);  // sRGB 0..1
}
