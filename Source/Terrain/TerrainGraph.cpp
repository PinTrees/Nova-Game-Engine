#include "pch.h"
#include "TerrainGraph.h"
#include "TerrainGraphNoise.h"
#include "JobSystem.h"
#include <chrono>
#include <fstream>
#include <mutex>
#include <set>
#include <queue>
#include <tuple>

using json = nlohmann::json;
namespace N = TerrainGraphNoise;

namespace TerrainGraph
{
	// ================================================================ 노드 설명
	const std::vector<NodeDesc>& NodeDescs()
	{
		using K = ParamKind;
		const auto H = PinKind::Height;
		const auto M = PinKind::Mask;
		const auto C = PinKind::Color;
		static const std::vector<NodeDesc> kDescs = {
			{ NodeType::Constant, "constant", "Constant", "Primitive", {}, { { "Out", H } },
				{ { "value", "Value", K::Float, 0.5f, 0.0f, 1.0f } }, "같은 높이 (0 ~ 1)." },
			{ NodeType::Gradient, "gradient", "Gradient", "Primitive", {}, { { "Out", H } },
				{ { "angle", "Angle", K::Float, 0.0f, 0.0f, 360.0f }, { "min", "Min", K::Float, 0.0f, 0.0f, 1.0f }, { "max", "Max", K::Float, 1.0f, 0.0f, 1.0f } },
				"월드를 가로지르는 비탈 (각도 방향으로 Min → Max)." },
			{ NodeType::Radial, "radial", "Radial", "Primitive", {}, { { "Out", H } },
				{ { "radius", "Radius (m)", K::Float, 20000.0f, 100.0f, 100000.0f }, { "falloff", "Falloff", K::Float, 2.0f, 0.2f, 8.0f },
				  { "centerX", "Center X (m)", K::Float, 0.0f, -50000.0f, 50000.0f }, { "centerZ", "Center Z (m)", K::Float, 0.0f, -50000.0f, 50000.0f },
				  { "invert", "Invert", K::Bool, 0.0f, 0.0f, 1.0f } },
				"가운데가 1, 반지름에서 0 (섬 · 월드 가장자리 마스크)." },
			{ NodeType::Perlin, "perlin", "Perlin", "Primitive", {}, { { "Out", H } },
				{ { "scale", "Scale (m)", K::Float, 6000.0f, 10.0f, 80000.0f }, { "octaves", "Octaves", K::Int, 6.0f, 1.0f, 12.0f },
				  { "gain", "Gain", K::Float, 0.5f, 0.1f, 0.9f }, { "lacunarity", "Lacunarity", K::Float, 2.0f, 1.5f, 3.0f },
				  { "amplitude", "Amplitude", K::Float, 1.0f, 0.0f, 2.0f }, { "seed", "Seed", K::Int, 1.0f, 0.0f, 100000.0f } },
				"부드러운 언덕 (fBm). 0.5 를 가운데로 ± Amplitude/2." },
			{ NodeType::Ridged, "ridged", "Ridged", "Primitive", {}, { { "Out", H } },
				{ { "scale", "Scale (m)", K::Float, 6000.0f, 10.0f, 80000.0f }, { "octaves", "Octaves", K::Int, 6.0f, 1.0f, 12.0f },
				  { "gain", "Gain", K::Float, 0.5f, 0.1f, 0.9f }, { "sharpness", "Sharpness", K::Float, 2.0f, 0.5f, 4.0f }, { "seed", "Seed", K::Int, 2.0f, 0.0f, 100000.0f } },
				"날카로운 산등성이 (리지 멀티프랙탈)." },
			{ NodeType::Mountain, "mountain", "Mountain", "Primitive", {}, { { "Out", H } },
				{ { "scale", "Scale (m)", K::Float, 14000.0f, 200.0f, 80000.0f }, { "height", "Height", K::Float, 1.0f, 0.0f, 1.0f },
				  { "style", "Style", K::Enum, 1.0f, 0.0f, 2.0f, "Basic|Eroded|Old" }, { "bulk", "Bulk", K::Float, 0.5f, 0.0f, 1.0f },
				  { "seed", "Seed", K::Int, 3.0f, 0.0f, 100000.0f } },
				"산맥 (Gaea 의 Mountain): 휜 리지 + 침식 결. Bulk = 산자락 덩어리, Style = Eroded 는 골이 패이고 Old 는 둥글다." },
			{ NodeType::Voronoi, "voronoi", "Voronoi", "Primitive", {}, { { "Out", H } },
				{ { "scale", "Scale (m)", K::Float, 3000.0f, 10.0f, 50000.0f }, { "jitter", "Jitter", K::Float, 1.0f, 0.0f, 1.0f },
				  { "mode", "Mode", K::Enum, 0.0f, 0.0f, 2.0f, "F1|F2-F1|Cells" }, { "seed", "Seed", K::Int, 4.0f, 0.0f, 100000.0f } },
				"세포 무늬 (F1 = 원뿔 봉우리, F2-F1 = 갈라진 금, Cells = 칸마다 높이)." },
			{ NodeType::Combine, "combine", "Combine", "Adjust", { "A", "B", "Mask" }, { { "Out", H } },
				{ { "mode", "Mode", K::Enum, 0.0f, 0.0f, 7.0f, "Add|Subtract|Multiply|Max|Min|Blend|Difference|Screen" },
				  { "ratio", "Ratio", K::Float, 0.5f, 0.0f, 1.0f } },
				"두 입력을 합친다. Ratio = B 의 비중, Mask 가 있으면 Mask 만큼만." },
			{ NodeType::Warp, "warp", "Warp", "Adjust", { "In" }, { { "Out", H } },
				{ { "strength", "Strength (m)", K::Float, 1500.0f, 0.0f, 20000.0f }, { "scale", "Scale (m)", K::Float, 6000.0f, 50.0f, 50000.0f },
				  { "octaves", "Octaves", K::Int, 4.0f, 1.0f, 8.0f }, { "seed", "Seed", K::Int, 5.0f, 0.0f, 100000.0f } },
				"노이즈만큼 자리를 밀어 휜다 (곧은 산맥 · 해안을 자연스럽게)." },
			{ NodeType::Terrace, "terrace", "Terrace", "Adjust", { "In" }, { { "Out", H } },
				{ { "steps", "Steps", K::Int, 8.0f, 2.0f, 64.0f }, { "sharpness", "Sharpness", K::Float, 0.6f, 0.0f, 1.0f } },
				"계단 (층층 바위 · 메사)." },
			{ NodeType::Curve, "curve", "Curve", "Adjust", { "In" }, { { "Out", H } },
				{ { "power", "Power", K::Float, 1.4f, 0.2f, 5.0f }, { "contrast", "Contrast", K::Float, 0.0f, -1.0f, 1.0f }, { "bias", "Bias", K::Float, 0.0f, -0.5f, 0.5f } },
				"높이 곡선: Power > 1 = 골은 넓고 봉우리는 뾰족." },
			{ NodeType::Clamp, "clamp", "Clamp", "Adjust", { "In" }, { { "Out", H } },
				{ { "min", "Min", K::Float, 0.0f, 0.0f, 1.0f }, { "max", "Max", K::Float, 1.0f, 0.0f, 1.0f } }, "범위로 자른다." },
			{ NodeType::Normalize, "normalize", "Normalize", "Adjust", { "In" }, { { "Out", H } }, {}, "가장 낮은 곳 0, 높은 곳 1 로." },
			{ NodeType::Invert, "invert", "Invert", "Adjust", { "In" }, { { "Out", H } }, {}, "1 - 높이." },
			{ NodeType::Blur, "blur", "Blur", "Adjust", { "In" }, { { "Out", H } },
				{ { "radius", "Radius (m)", K::Float, 100.0f, 1.0f, 5000.0f } }, "흐리게 (상자 세 번 ≈ 가우스)." },
			{ NodeType::Erosion, "erosion", "Erosion", "Simulate", { "In" },
				{ { "Out", H }, { "Flow", M }, { "Deposits", M }, { "Wear", M } },
				{ { "amount", "Droplets / Cell", K::Float, 1.0f, 0.05f, 4.0f, nullptr, "칸마다 떨어뜨리는 물방울 (많을수록 깊게)" },
				  { "erosion", "Erosion Rate", K::Float, 0.3f, 0.01f, 1.0f }, { "deposition", "Deposition", K::Float, 0.3f, 0.01f, 1.0f },
				  { "capacity", "Sediment Capacity", K::Float, 4.0f, 0.5f, 16.0f }, { "inertia", "Inertia", K::Float, 0.05f, 0.0f, 0.5f },
				  { "evaporation", "Evaporation", K::Float, 0.02f, 0.001f, 0.2f }, { "lifetime", "Lifetime (cells)", K::Int, 48.0f, 8.0f, 128.0f },
				  { "radius", "Brush Radius (m)", K::Float, 30.0f, 1.0f, 500.0f }, { "seed", "Seed", K::Int, 6.0f, 0.0f, 100000.0f } },
				"수력 침식 (물방울): 비탈을 따라 흙을 깎아 골 · 물길을 내고 느려지는 곳에 쌓는다. Flow = 물길, Deposits = 쌓인 곳, Wear = 깎인 곳." },
			{ NodeType::Thermal, "thermal", "Thermal", "Simulate", { "In" }, { { "Out", H } },
				{ { "angle", "Talus Angle", K::Float, 38.0f, 5.0f, 80.0f }, { "iterations", "Iterations", K::Int, 30.0f, 1.0f, 300.0f },
				  { "strength", "Strength", K::Float, 0.5f, 0.0f, 1.0f } },
				"열 침식: 이 각도보다 가파른 곳이 무너져 아래로 흘러내린다 (애추 · 둥근 산자락)." },
			{ NodeType::Slope, "slope", "Slope", "Data", { "In" }, { { "Mask", M } },
				{ { "min", "Min Angle", K::Float, 25.0f, 0.0f, 90.0f }, { "max", "Max Angle", K::Float, 90.0f, 0.0f, 90.0f }, { "falloff", "Falloff", K::Float, 5.0f, 0.0f, 45.0f } },
				"경사 마스크 (도)." },
			{ NodeType::HeightMask, "heightMask", "Height Mask", "Data", { "In" }, { { "Mask", M } },
				{ { "min", "Min", K::Float, 0.5f, 0.0f, 1.0f }, { "max", "Max", K::Float, 1.0f, 0.0f, 1.0f }, { "falloff", "Falloff", K::Float, 0.05f, 0.0f, 0.5f } },
				"높이 마스크 (0 ~ 1)." },
			{ NodeType::Curvature, "curvature", "Curvature", "Data", { "In" }, { { "Mask", M } },
				{ { "radius", "Radius (m)", K::Float, 80.0f, 1.0f, 2000.0f }, { "mode", "Mode", K::Enum, 0.0f, 0.0f, 2.0f, "Concave|Convex|Both" },
				  { "contrast", "Contrast", K::Float, 4.0f, 0.1f, 50.0f } },
				"굴곡 마스크 (오목 = 골, 볼록 = 능선)." },
			{ NodeType::Output, "output", "Output", "Output", { "Height", "Flow", "Deposits", "Wear", "Color" }, {}, {},
				"월드 높이 (0 ~ 1 → 0 ~ Max Height) 와 텍스처 규칙이 읽는 물길 · 퇴적 · 깎임, 컬러 맵 (SatMap)." },
			// ---- Gaea 식 더 (2026-10-12)
			{ NodeType::MountainRange, "mountainRange", "Mountain Range", "Primitive", {}, { { "Out", H } },
				{ { "scale", "Scale (m)", K::Float, 16000.0f, 500.0f, 80000.0f }, { "height", "Height", K::Float, 1.0f, 0.0f, 1.0f },
				  { "uplift", "Uplift (coverage)", K::Float, 0.55f, 0.05f, 1.0f, nullptr, "산맥 띠가 차지하는 넓이 (0.5 = 월드의 절반쯤)" },
				  { "octaves", "Octaves", K::Int, 11.0f, 3.0f, 14.0f }, { "warp", "Warp", K::Float, 0.4f, 0.0f, 1.5f },
				  { "sharpness", "Sharpness", K::Float, 1.4f, 0.5f, 4.0f }, { "seed", "Seed", K::Int, 21.0f, 0.0f, 100000.0f } },
				"이어진 산맥 (스위스 난류 — 앞 옥타브 기울기로 다음 옥타브를 밀어 봉우리 · 능선이 프랙탈로 갈라진다). 낮은 주파수 띠를 따라 솟는다." },
			{ NodeType::Rugged, "rugged", "Rugged", "Adjust", { "In" }, { { "Out", H } },
				{ { "amount", "Amount (m)", K::Float, 30.0f, 0.0f, 300.0f }, { "scale", "Scale (m)", K::Float, 300.0f, 20.0f, 5000.0f },
				  { "octaves", "Octaves", K::Int, 6.0f, 1.0f, 10.0f }, { "slopeMin", "Min Slope (deg)", K::Float, 12.0f, 0.0f, 80.0f },
				  { "mode", "Mode", K::Enum, 0.0f, 0.0f, 2.0f, "Rugged|Crumble|Shatter" }, { "seed", "Seed", K::Int, 31.0f, 0.0f, 100000.0f } },
				"비탈을 거칠게 (Gaea 의 Rugged · Crumble · Shatter): 리지 결 · 갈라진 금 · 깨진 바위 덩어리. 평지는 그대로." },
			{ NodeType::Stratify, "stratify", "Stratify", "Adjust", { "In" }, { { "Out", H }, { "Layers", M } },
				{ { "step", "Layer Height (m)", K::Float, 30.0f, 2.0f, 400.0f }, { "strength", "Strength", K::Float, 0.65f, 0.0f, 1.0f },
				  { "sharpness", "Sharpness", K::Float, 0.6f, 0.0f, 1.0f }, { "tilt", "Tilt (deg)", K::Float, 4.0f, 0.0f, 45.0f },
				  { "direction", "Tilt Direction (deg)", K::Float, 30.0f, 0.0f, 360.0f }, { "noise", "Noise (m)", K::Float, 20.0f, 0.0f, 200.0f },
				  { "slopeOnly", "Slopes Only", K::Bool, 1.0f, 0.0f, 1.0f }, { "seed", "Seed", K::Int, 41.0f, 0.0f, 100000.0f } },
				"지층 (Gaea 의 Stratify · Sandstone): 기운 층마다 단단한 턱과 무른 비탈. Layers = 층 번호 (색 칠하기에)." },
			{ NodeType::Gullies, "gullies", "Gullies", "Simulate", { "In" }, { { "Out", H }, { "Gullies", M } },
				{ { "depth", "Depth (m)", K::Float, 16.0f, 0.0f, 200.0f }, { "scale", "Scale (m)", K::Float, 700.0f, 30.0f, 10000.0f },
				  { "octaves", "Octaves", K::Int, 5.0f, 1.0f, 8.0f }, { "branch", "Branching", K::Float, 1.0f, 0.0f, 3.0f },
				  { "slopeMin", "Min Slope (deg)", K::Float, 6.0f, 0.0f, 60.0f }, { "seed", "Seed", K::Int, 51.0f, 0.0f, 100000.0f } },
				"골 (침식 필터): 비탈을 따라 내려가는 가지 친 물길 무늬를 칸보다 잘게 (프랙탈). 물방울 침식 뒤에 더하면 잔 골짜기." },
			{ NodeType::Sediment, "sediment", "Sediment", "Simulate", { "In" }, { { "Out", H }, { "Deposits", M } },
				{ { "amount", "Amount", K::Float, 0.6f, 0.0f, 1.0f }, { "radius", "Radius (m)", K::Float, 160.0f, 10.0f, 3000.0f },
				  { "angle", "Settle Angle (deg)", K::Float, 14.0f, 1.0f, 45.0f } },
				"퇴적: 오목한 골 바닥 · 낮은 곳을 흙으로 메워 평평한 골짜기 바닥 · 선상지 (이 각도보다 완만한 곳만)." },
			{ NodeType::Erosion2, "erosion2", "Erosion2", "Simulate", { "In" },
				{ { "Out", H }, { "Flow", M }, { "Wear", M }, { "Deposits", M } },
				{ { "duration", "Duration", K::Float, 0.15f, 0.01f, 1.0f, nullptr, "얼마나 오래 (물방울 수)" },
				  { "downcutting", "Downcutting", K::Float, 0.2f, 0.0f, 1.0f, nullptr, "깊게 파는 정도 (깎는 비율 · 실어 가는 양)" },
				  { "scale", "Erosion Scale (m)", K::Float, 3000.0f, 50.0f, 30000.0f, nullptr, "가장 큰 골의 크기 — 이 크기부터 칸 크기까지 여러 단계로 깎는다" },
				  { "seed", "Seed", K::Int, 61.0f, 0.0f, 100000.0f },
				  { "suspended", "Suspended Load", K::Float, 0.15f, 0.0f, 1.0f, nullptr, "물에 떠서 멀리 실려 가는 흙 (클수록 덜 쌓인다)" },
				  { "bedLoad", "Bed Load", K::Float, 0.5f, 0.0f, 1.0f }, { "bedAngle", "Bed Discharge Angle", K::Float, 30.0f, 5.0f, 60.0f },
				  { "coarse", "Coarse Sediments", K::Float, 0.4f, 0.0f, 1.0f }, { "coarseAngle", "Coarse Discharge Angle", K::Float, 38.0f, 5.0f, 70.0f },
				  { "shape", "Shape", K::Float, 0.3f, 0.0f, 1.0f, nullptr, "깎기 전에 능선을 세운다" }, { "sharpness", "Shape Sharpness", K::Float, 0.5f, 0.0f, 1.0f },
				  { "detail", "Shape Detail Scale", K::Float, 0.25f, 0.0f, 1.0f, nullptr, "물길이 시작할 잔 결" },
				  { "direction", "Direction", K::Enum, 0.0f, 0.0f, 4.0f, "None|N|E|S|W" } },
				"침식 2 (Gaea 의 Erosion2): 큰 골짜기부터 칸 크기 골까지 여러 해상도로 물방울 침식 + 흙 (부유 · 하상 · 굵은 퇴적물) 이 각도에 맞게 흘러내림." },
			{ NodeType::River, "river", "River", "Simulate", { "In" }, { { "Out", H }, { "Rivers", M } },
				{ { "threshold", "Min Catchment (km2)", K::Float, 20.0f, 0.5f, 2000.0f, nullptr, "이만큼 넓은 땅의 물이 모이면 강" },
				  { "depth", "Depth (m)", K::Float, 5.0f, 0.5f, 40.0f }, { "width", "Max Width (m)", K::Float, 60.0f, 4.0f, 400.0f },
				  { "valley", "Bank Width (m)", K::Float, 40.0f, 0.0f, 400.0f }, { "count", "Max Rivers", K::Int, 60.0f, 1.0f, 400.0f } },
				"강: 웅덩이를 채우고 (우선순위 채우기) 가장 가파른 쪽으로 물을 모아 (D8) 넓은 땅의 물이 모이는 골에 강바닥을 판다. 강 선은 World Terrain 이 물 (Water Body) 로 만든다." },
			{ NodeType::Road, "road", "Road", "Simulate", { "In", "Avoid" }, { { "Out", H }, { "Roads", M } },
				{ { "towns", "Towns", K::Int, 14.0f, 2.0f, 80.0f, nullptr, "이을 마을 수 (완만한 낮은 땅에 고르게)" },
				  { "width", "Width (m)", K::Float, 7.0f, 2.0f, 30.0f }, { "maxSlope", "Max Slope (deg)", K::Float, 9.0f, 2.0f, 30.0f },
				  { "loops", "Extra Links", K::Float, 0.3f, 0.0f, 1.0f, nullptr, "최소 신장 트리에 더하는 고리" }, { "seed", "Seed", K::Int, 71.0f, 0.0f, 100000.0f } },
				"길: 마을을 정해 경사 · 강 (Avoid) 을 피하는 A* 로 잇고 (길끼리 모이게), 매끈한 기울기로 땅을 깎고 메운다. 타일은 선을 따라 날카롭게 (흙 길)." },
			{ NodeType::SatMap, "satmap", "SatMap", "Color", { "In" }, { { "Color", C } },
				{ { "palette", "Palette", K::Enum, 0.0f, 0.0f, 31.0f, PaletteOptions() },
				  { "bias", "Bias", K::Float, 0.0f, -0.5f, 0.5f }, { "reverse", "Reverse", K::Bool, 0.0f, 0.0f, 1.0f },
				  { "enhance", "Enhance", K::Enum, 1.0f, 0.0f, 2.0f, "None|Autolevel|Equalize" },
				  { "hue", "Hue", K::Float, 0.0f, -180.0f, 180.0f }, { "saturation", "Saturation", K::Float, 0.0f, -1.0f, 1.0f },
				  { "lightness", "Lightness", K::Float, 0.0f, -1.0f, 1.0f } },
				"색 칠하기 (Gaea 의 SatMap): 높이 · 마스크 값을 위성 사진에서 뽑은 듯한 색 띠로. Output 의 Color 에 이으면 지형 컬러 맵 (텍스처는 명암 디테일)." },
			{ NodeType::ColorBlend, "colorBlend", "Color Blend", "Color", { "A", "B", "Mask" }, { { "Color", C } },
				{ { "ratio", "Ratio", K::Float, 1.0f, 0.0f, 1.0f } },
				"두 색을 마스크로 섞는다 (예: 경사 마스크로 바위 색)." },
		};
		return kDescs;
	}

	const NodeDesc* FindDesc(NodeType type)
	{
		for (const NodeDesc& d : NodeDescs())
			if (d.Type == type)
				return &d;
		return nullptr;
	}

	namespace
	{
		const NodeDesc* FindDescByKey(const std::string& key)
		{
			for (const NodeDesc& d : NodeDescs())
				if (key == d.Key)
					return &d;
			return nullptr;
		}
		int ParamIndex(const NodeDesc* d, const char* key)
		{
			if (d)
				for (size_t i = 0; i < d->Params.size(); ++i)
					if (strcmp(d->Params[i].Key, key) == 0)
						return (int)i;
			return -1;
		}
		uint64_t Mix(uint64_t h, uint64_t v) { return (h ^ v) * 1099511628211ull; }
		uint64_t MixF(uint64_t h, float f) { uint32_t u; memcpy(&u, &f, 4); return Mix(h, u); }
	}

	float Node::Get(const char* key) const
	{
		const NodeDesc* d = FindDesc(Type);
		const int i = ParamIndex(d, key);
		if (i < 0)
			return 0.0f;
		return i < (int)Values.size() ? Values[(size_t)i] : d->Params[(size_t)i].Default;
	}

	void Node::Set(const char* key, float value)
	{
		const NodeDesc* d = FindDesc(Type);
		const int i = ParamIndex(d, key);
		if (i < 0)
			return;
		while ((int)Values.size() < (int)d->Params.size())
			Values.push_back(d->Params[Values.size()].Default);
		Values[(size_t)i] = value;
	}

	// ================================================================ 월드 · 규칙
	uint64_t WorldSettings::Hash() const
	{
		uint64_t h = 1469598103934665603ull;
		h = MixF(h, WorldSize); h = MixF(h, MaxHeight);
		h = Mix(h, (uint64_t)BuildResolution); h = Mix(h, (uint64_t)(uint32_t)Seed);
		return h;
	}

	uint64_t WorldSettings::TileHash() const
	{
		uint64_t h = 7809847782465536322ull;
		h = MixF(h, TileSize); h = Mix(h, (uint64_t)TileResolution);
		for (float f : { DetailAmplitude, DetailScale, DetailErosion, DetailSlope, DetailDeposits, ColorStrength })
			h = MixF(h, f);
		return h;
	}

	json WorldSettings::ToJson() const
	{
		return { { "worldSize", WorldSize }, { "tileSize", TileSize }, { "tileResolution", TileResolution }, { "maxHeight", MaxHeight },
			{ "buildResolution", BuildResolution }, { "previewResolution", PreviewResolution }, { "seed", Seed },
			{ "detailAmplitude", DetailAmplitude }, { "detailScale", DetailScale }, { "detailErosion", DetailErosion },
			{ "detailSlope", DetailSlope }, { "detailDeposits", DetailDeposits },
			{ "colorStrength", ColorStrength } };
	}

	void WorldSettings::FromJson(const json& j)
	{
		WorldSize = j.value("worldSize", WorldSize);
		TileSize = j.value("tileSize", TileSize);
		TileResolution = j.value("tileResolution", TileResolution);
		MaxHeight = j.value("maxHeight", MaxHeight);
		BuildResolution = j.value("buildResolution", BuildResolution);
		PreviewResolution = j.value("previewResolution", PreviewResolution);
		Seed = j.value("seed", Seed);
		DetailAmplitude = j.value("detailAmplitude", DetailAmplitude);
		DetailScale = j.value("detailScale", DetailScale);
		DetailErosion = j.value("detailErosion", DetailErosion);
		DetailSlope = j.value("detailSlope", DetailSlope);
		DetailDeposits = j.value("detailDeposits", DetailDeposits);
		ColorStrength = j.value("colorStrength", ColorStrength);
	}

	json LayerRule::ToJson() const
	{
		return { { "enabled", Enabled }, { "name", Name }, { "layer", Layer },
			{ "heightMin", HeightMin }, { "heightMax", HeightMax }, { "heightFalloff", HeightFalloff },
			{ "slopeMin", SlopeMin }, { "slopeMax", SlopeMax }, { "slopeFalloff", SlopeFalloff },
			{ "flow", Flow }, { "deposits", Deposits }, { "curvature", Curvature }, { "noise", Noise }, { "noiseScale", NoiseScale }, { "strength", Strength } };
	}

	void LayerRule::FromJson(const json& j)
	{
		Enabled = j.value("enabled", Enabled);
		Name = j.value("name", Name);
		Layer = j.value("layer", Layer);
		HeightMin = j.value("heightMin", HeightMin); HeightMax = j.value("heightMax", HeightMax); HeightFalloff = j.value("heightFalloff", HeightFalloff);
		SlopeMin = j.value("slopeMin", SlopeMin); SlopeMax = j.value("slopeMax", SlopeMax); SlopeFalloff = j.value("slopeFalloff", SlopeFalloff);
		Flow = j.value("flow", Flow); Deposits = j.value("deposits", Deposits); Curvature = j.value("curvature", Curvature);
		Noise = j.value("noise", Noise); NoiseScale = j.value("noiseScale", NoiseScale); Strength = j.value("strength", Strength);
	}

	namespace
	{
		// 범위 [lo, hi] 안 = 1, 밖으로 falloff 만큼 부드럽게 0
		float Band(float v, float lo, float hi, float falloff)
		{
			const float f = (std::max)(falloff, 1e-4f);
			const float a = std::clamp((v - (lo - f)) / f, 0.0f, 1.0f);
			const float b = std::clamp(((hi + f) - v) / f, 0.0f, 1.0f);
			const float t = (std::min)(a, b);
			return t * t * (3.0f - 2.0f * t);
		}
	}

	void SplatRules(const LayerRule rules[4], float heightM, float slopeDeg, float flow, float deposits, float curvature, float x, float z, int seed, float out[4])
	{
		out[0] = 1.0f; out[1] = out[2] = out[3] = 0.0f;
		for (int i = 1; i < 4; ++i)
		{
			const LayerRule& r = rules[i];
			if (!r.Enabled || r.Strength <= 0.0f)
				continue;
			// 경계 노이즈: 범위를 흔든다 (높이는 falloff 만큼, 경사는 도)
			float n = 0.0f;
			if (r.Noise > 0.0f)
				n = N::Fbm(x / (std::max)(r.NoiseScale, 0.5f), z / (std::max)(r.NoiseScale, 0.5f), (uint32_t)seed * 31u + (uint32_t)i * 7919u, 3) * r.Noise;
			float m = Band(heightM + n * (r.HeightFalloff + 1.0f) * 2.0f, r.HeightMin, r.HeightMax, r.HeightFalloff);
			m *= Band(slopeDeg + n * (r.SlopeFalloff + 2.0f), r.SlopeMin, r.SlopeMax, r.SlopeFalloff);
			if (r.Flow > 0.0f) m *= 1.0f + (std::min)(r.Flow, 1.0f) * (std::clamp(flow * (1.0f + r.Flow) + n * 0.3f, 0.0f, 1.0f) - 1.0f);
			if (r.Deposits > 0.0f) m *= 1.0f + (std::min)(r.Deposits, 1.0f) * (std::clamp(deposits * (1.0f + r.Deposits) + n * 0.3f, 0.0f, 1.0f) - 1.0f);
			if (r.Curvature != 0.0f)
			{
				const float c = std::clamp(curvature * (r.Curvature > 0.0f ? 1.0f : -1.0f) * 3.0f + n * 0.3f, 0.0f, 1.0f);
				m *= 1.0f + (std::min)(fabsf(r.Curvature), 1.0f) * (c - 1.0f);
			}
			m = std::clamp(m * r.Strength, 0.0f, 1.0f);
			for (int k = 0; k < 4; ++k)
				out[k] *= 1.0f - m;
			out[i] += m;
		}
	}

	// ================================================================ 그래프
	Node& Graph::Add(NodeType type, float x, float y)
	{
		Node n;
		n.Id = NextId++;
		n.Type = type;
		n.X = x;
		n.Y = y;
		if (const NodeDesc* d = FindDesc(type))
			for (const ParamDesc& p : d->Params)
				n.Values.push_back(p.Default);
		Nodes.push_back(n);
		Touch();
		return Nodes.back();
	}

	void Graph::Remove(int id)
	{
		Nodes.erase(std::remove_if(Nodes.begin(), Nodes.end(), [&](const Node& n) { return n.Id == id; }), Nodes.end());
		Edges.erase(std::remove_if(Edges.begin(), Edges.end(), [&](const Edge& e) { return e.From == id || e.To == id; }), Edges.end());
		Touch();
	}

	Node* Graph::Find(int id)
	{
		for (Node& n : Nodes)
			if (n.Id == id)
				return &n;
		return nullptr;
	}

	const Node* Graph::Find(int id) const
	{
		for (const Node& n : Nodes)
			if (n.Id == id)
				return &n;
		return nullptr;
	}

	const Edge* Graph::InputOf(int to, int toPin) const
	{
		for (const Edge& e : Edges)
			if (e.To == to && e.ToPin == toPin)
				return &e;
		return nullptr;
	}

	bool Graph::Connect(int from, int fromPin, int to, int toPin)
	{
		const Node* a = Find(from);
		const Node* b = Find(to);
		if (!a || !b || from == to)
			return false;
		const NodeDesc* da = FindDesc(a->Type);
		const NodeDesc* db = FindDesc(b->Type);
		if (!da || !db || fromPin < 0 || fromPin >= (int)da->Outputs.size() || toPin < 0 || toPin >= (int)db->Inputs.size())
			return false;
		// 고리: to 에서 위로 (입력 쪽) 가면 from 이 나오면 안 된다 → from 의 위쪽에 to 가 있으면 고리
		std::vector<int> stack{ from };
		std::set<int> seen;
		while (!stack.empty())
		{
			const int cur = stack.back();
			stack.pop_back();
			if (cur == to)
				return false;
			if (!seen.insert(cur).second)
				continue;
			for (const Edge& e : Edges)
				if (e.To == cur)
					stack.push_back(e.From);
		}
		Edges.erase(std::remove_if(Edges.begin(), Edges.end(), [&](const Edge& e) { return e.To == to && e.ToPin == toPin; }), Edges.end());
		Edges.push_back({ from, fromPin, to, toPin });
		Touch();
		return true;
	}

	int Graph::OutputNode() const
	{
		for (const Node& n : Nodes)
			if (n.Type == NodeType::Output)
				return n.Id;
		return 0;
	}

	uint64_t Graph::ContentHash() const
	{
		uint64_t h = World.Hash();
		for (const Node& n : Nodes)
		{
			h = Mix(h, (uint64_t)n.Id * 31 + (uint64_t)n.Type);
			h = Mix(h, n.Enabled ? 1 : 2);
			for (float v : n.Values)
				h = MixF(h, v);
		}
		for (const Edge& e : Edges)
			h = Mix(h, ((uint64_t)e.From << 40) ^ ((uint64_t)e.FromPin << 32) ^ ((uint64_t)e.To << 8) ^ (uint64_t)e.ToPin);
		return h;
	}

	uint64_t Graph::RulesHash() const
	{
		uint64_t h = World.TileHash();
		for (const LayerRule& r : Layers)
		{
			h = Mix(h, std::hash<std::string>()(r.Layer));
			h = Mix(h, r.Enabled ? 3 : 4);
			for (float f : { r.HeightMin, r.HeightMax, r.HeightFalloff, r.SlopeMin, r.SlopeMax, r.SlopeFalloff, r.Flow, r.Deposits, r.Curvature, r.Noise, r.NoiseScale, r.Strength })
				h = MixF(h, f);
		}
		return h;
	}

	json Graph::ToJson() const
	{
		json j;
		j["nova_terraingraph"] = 1;
		j["world"] = World.ToJson();
		j["nextId"] = NextId;
		json nodes = json::array();
		for (const Node& n : Nodes)
		{
			const NodeDesc* d = FindDesc(n.Type);
			json params = json::object();
			if (d)
				for (size_t i = 0; i < d->Params.size() && i < n.Values.size(); ++i)
					params[d->Params[i].Key] = n.Values[i];
			json jn = { { "id", n.Id }, { "type", d ? d->Key : "?" }, { "x", n.X }, { "y", n.Y }, { "params", params } };
			if (!n.Enabled) jn["enabled"] = false;
			if (!n.Comment.empty()) jn["comment"] = n.Comment;
			nodes.push_back(jn);
		}
		j["nodes"] = nodes;
		json edges = json::array();
		for (const Edge& e : Edges)
			edges.push_back({ { "from", e.From }, { "fromPin", e.FromPin }, { "to", e.To }, { "toPin", e.ToPin } });
		j["edges"] = edges;
		json layers = json::array();
		for (const LayerRule& r : Layers)
			layers.push_back(r.ToJson());
		j["layers"] = layers;
		return j;
	}

	bool Graph::FromJson(const json& j)
	{
		if (!j.is_object() || !j.contains("nodes"))
			return false;
		*this = Graph();
		if (j.contains("world"))
			World.FromJson(j["world"]);
		for (const json& jn : j["nodes"])
		{
			const NodeDesc* d = FindDescByKey(jn.value("type", std::string()));
			if (!d)
				continue;
			Node n;
			n.Id = jn.value("id", 0);
			n.Type = d->Type;
			n.X = jn.value("x", 0.0f);
			n.Y = jn.value("y", 0.0f);
			n.Enabled = jn.value("enabled", true);
			n.Comment = jn.value("comment", std::string());
			for (const ParamDesc& p : d->Params)
				n.Values.push_back(jn.contains("params") ? jn["params"].value(p.Key, p.Default) : p.Default);
			Nodes.push_back(n);
			NextId = (std::max)(NextId, n.Id + 1);
		}
		if (j.contains("edges"))
			for (const json& je : j["edges"])
				Edges.push_back({ je.value("from", 0), je.value("fromPin", 0), je.value("to", 0), je.value("toPin", 0) });
		if (j.contains("layers") && j["layers"].is_array())
			for (size_t i = 0; i < 4 && i < j["layers"].size(); ++i)
				Layers[i].FromJson(j["layers"][i]);
		NextId = (std::max)(NextId, j.value("nextId", NextId));
		Revision = 1;
		return true;
	}

	Graph MakeDefault(int seed)
	{
		Graph g;
		g.World.Seed = seed;
		g.World.MaxHeight = 2200.0f;
		// Gaea 식 시작 그래프 (20 노드): 산맥 · 봉우리 · 대륙 → 휨 → 곡선 → 거친 비탈 · 지층 → Erosion2 (여러 크기) → 골 → 무너짐 → 퇴적 →
		//  강 → 길 → 색 (SatMap: 높이 = 고원 띠, 가파른 곳 = 바위 띠) → Output
		auto add = [&](NodeType t, float x, float y, std::initializer_list<std::pair<const char*, float>> values, const char* name = nullptr) {
			Node& n = g.Add(t, x, y);
			for (const auto& [k, v] : values)
				n.Set(k, v);
			if (name)
				n.Comment = name;
			return n.Id;
		};
		const float sd = (float)(seed * 7);
		const int range = add(NodeType::MountainRange, 0, 0, { { "scale", 15000.0f }, { "uplift", 0.6f }, { "seed", sd + 1 } }, "Main Range");
		const int peaks = add(NodeType::Mountain, 0, 240, { { "scale", 9000.0f }, { "style", 1.0f }, { "bulk", 0.4f }, { "seed", sd + 2 } }, "Peaks");
		const int land = add(NodeType::Perlin, 0, 480, { { "scale", 21000.0f }, { "octaves", 6.0f }, { "amplitude", 0.8f }, { "seed", sd + 3 } }, "Continent");
		const int ranges = add(NodeType::Combine, 280, 100, { { "mode", 3.0f }, { "ratio", 1.0f } });
		const int base = add(NodeType::Combine, 520, 260, { { "mode", 0.0f }, { "ratio", 0.85f } });
		const int warp = add(NodeType::Warp, 760, 260, { { "strength", 1300.0f }, { "scale", 7500.0f }, { "seed", sd + 4 } });
		const int norm = add(NodeType::Normalize, 1000, 260, {});
		const int curve = add(NodeType::Curve, 1220, 260, { { "power", 1.3f } });
		const int rugged = add(NodeType::Rugged, 1440, 260, { { "amount", 45.0f }, { "scale", 280.0f }, { "slopeMin", 14.0f }, { "seed", sd + 5 } });
		// 지층은 옅게 (12 m 빌드 칸에서 22 m 층이 비탈 전체에 등고선 같은 줄로 보였다) — 절벽에만 조금
		const int strat = add(NodeType::Stratify, 1680, 260, { { "step", 35.0f }, { "strength", 0.12f }, { "sharpness", 0.35f }, { "noise", 30.0f }, { "seed", sd + 6 } });
		const int ero = add(NodeType::Erosion2, 1920, 260, { { "duration", 0.18f }, { "downcutting", 0.25f }, { "scale", 4000.0f }, { "seed", sd + 7 } });
		const int gully = add(NodeType::Gullies, 2200, 200, { { "depth", 35.0f }, { "scale", 900.0f }, { "branch", 1.6f }, { "seed", sd + 8 } });
		const int thermal = add(NodeType::Thermal, 2440, 200, { { "angle", 36.0f }, { "iterations", 20.0f }, { "strength", 0.4f } });
		const int sed = add(NodeType::Sediment, 2680, 200, { { "amount", 0.5f }, { "radius", 150.0f } });
		const int river = add(NodeType::River, 2920, 200, { { "threshold", 25.0f }, { "count", 40.0f } });
		const int road = add(NodeType::Road, 3160, 200, { { "towns", 14.0f }, { "seed", sd + 9 } });
		const int satLand = add(NodeType::SatMap, 3420, 360, { { "palette", 14.0f }, { "bias", -0.05f } }, "Land Colors");
		const int slope = add(NodeType::Slope, 3420, 560, { { "min", 33.0f }, { "falloff", 8.0f } });
		const int satRock = add(NodeType::SatMap, 3420, 740, { { "palette", 0.0f } }, "Rock Colors");
		const int blend = add(NodeType::ColorBlend, 3680, 520, { { "ratio", 0.85f } });
		const int out = add(NodeType::Output, 3940, 260, {});
		g.Connect(range, 0, ranges, 0);
		g.Connect(peaks, 0, ranges, 1);
		g.Connect(land, 0, base, 0);
		g.Connect(ranges, 0, base, 1);
		g.Connect(base, 0, warp, 0);
		g.Connect(warp, 0, norm, 0);
		g.Connect(norm, 0, curve, 0);
		g.Connect(curve, 0, rugged, 0);
		g.Connect(rugged, 0, strat, 0);
		g.Connect(strat, 0, ero, 0);
		g.Connect(ero, 0, gully, 0);
		g.Connect(gully, 0, thermal, 0);
		g.Connect(thermal, 0, sed, 0);
		g.Connect(sed, 0, river, 0);
		g.Connect(river, 0, road, 0);
		g.Connect(river, 1, road, 1);       // 길은 강을 피한다
		g.Connect(road, 0, satLand, 0);
		g.Connect(road, 0, slope, 0);
		g.Connect(road, 0, satRock, 0);
		g.Connect(satLand, 0, blend, 0);
		g.Connect(satRock, 0, blend, 1);
		g.Connect(slope, 0, blend, 2);
		g.Connect(road, 0, out, 0);
		g.Connect(ero, 1, out, 1);          // Flow
		g.Connect(ero, 3, out, 2);          // Deposits
		g.Connect(ero, 2, out, 3);          // Wear
		g.Connect(blend, 0, out, 4);        // Color
		// 텍스처 규칙: 풀 (바탕) · 흙 (물길 · 퇴적 · 길) · 바위 (가파름) · 눈 (높은 곳, 덜 가파른 곳)
		g.Layers[0].Name = "Grass";
		g.Layers[1].Name = "Dirt";
		g.Layers[1].Flow = 0.8f; g.Layers[1].Deposits = 0.6f; g.Layers[1].SlopeMax = 40.0f; g.Layers[1].Noise = 0.5f; g.Layers[1].Strength = 1.0f;
		g.Layers[2].Name = "Rock";
		g.Layers[2].SlopeMin = 30.0f; g.Layers[2].SlopeFalloff = 6.0f; g.Layers[2].Noise = 0.4f;
		g.Layers[3].Name = "Snow";
		// 눈은 위쪽 (최고 2200 m 의 1800 m 부터) · 덜 가파른 곳만 — 가파른 능선은 바위가 드러난다
		g.Layers[3].HeightMin = 1800.0f; g.Layers[3].HeightFalloff = 110.0f; g.Layers[3].SlopeMax = 33.0f; g.Layers[3].SlopeFalloff = 6.0f; g.Layers[3].Noise = 0.6f;
		return g;
	}

	namespace
	{
		std::mutex s_GraphLock;
		std::map<std::string, std::shared_ptr<Graph>> s_Graphs;

		std::wstring AbsPath(const std::string& path)
		{
			const std::filesystem::path p(string_to_wstring(path));
			return p.is_absolute() ? p.wstring() : PathManager::GetI()->GetMovePathW(string_to_wstring(path));
		}
	}

	std::shared_ptr<Graph> Load(const std::string& path)
	{
		std::lock_guard<std::mutex> lock(s_GraphLock);
		auto it = s_Graphs.find(path);
		if (it != s_Graphs.end())
			return it->second;
		std::ifstream in(AbsPath(path), std::ios::binary);
		if (!in)
			return nullptr;
		json j = json::parse(in, nullptr, false);
		auto g = std::make_shared<Graph>();
		if (j.is_discarded() || !g->FromJson(j))
			return nullptr;
		s_Graphs[path] = g;
		return g;
	}

	bool Save(const std::string& path, const Graph& g)
	{
		const std::wstring abs = AbsPath(path);
		std::error_code ec;
		std::filesystem::create_directories(std::filesystem::path(abs).parent_path(), ec);
		std::ofstream out(abs, std::ios::binary | std::ios::trunc);
		if (!out)
			return false;
		out << g.ToJson().dump(1);
		std::lock_guard<std::mutex> lock(s_GraphLock);
		auto it = s_Graphs.find(path);
		if (it == s_Graphs.end())
			s_Graphs[path] = std::make_shared<Graph>(g);
		else if (it->second.get() != &g)
			*it->second = g;
		return true;
	}

	// ================================================================ 맵
	float Map::Bilinear(float fx, float fz) const
	{
		fx = std::clamp(fx, 0.0f, (float)(Res - 1));
		fz = std::clamp(fz, 0.0f, (float)(Res - 1));
		const int x0 = (std::min)((int)fx, Res - 2), z0 = (std::min)((int)fz, Res - 2);
		const float tx = fx - x0, tz = fz - z0;
		const float* r0 = &V[(size_t)z0 * Res + x0];
		const float* r1 = r0 + Res;
		return (r0[0] * (1 - tx) + r0[1] * tx) * (1 - tz) + (r1[0] * (1 - tx) + r1[1] * tx) * tz;
	}

	// ================================================================ 평가
	namespace
	{
		using MapW = std::shared_ptr<Map>;

		MapW NewMap(int res, float value = 0.0f)
		{
			auto m = std::make_shared<Map>();
			m->Res = res;
			m->V.assign((size_t)res * res, value);
			return m;
		}

		bool Cancelled(const EvalContext& c) { return c.Cancel && c.Cancel->load(std::memory_order_relaxed); }

		// 줄마다 병렬 (fn(x, z, worldX, worldZ) → 값)
		template <class F>
		void FillRows(Map& m, const EvalContext& c, F&& fn)
		{
			const int res = m.Res;
			const double x0 = c.X0(), cell = c.Cell();
			Jobs::ParallelFor(res, 4, [&](int b, int e) {
				for (int z = b; z < e; ++z)
				{
					float* row = &m.V[(size_t)z * res];
					const double wz = x0 + z * cell;
					for (int x = 0; x < res; ++x)
						row[x] = fn(x, z, x0 + x * cell, wz);
				}
			}, "TerrainGraph Rows");
		}

		// 상자 흐림 (가로 → 세로), 반지름 칸
		void BoxBlur(std::vector<float>& v, int res, int r)
		{
			if (r <= 0)
				return;
			std::vector<float> tmp(v.size());
			Jobs::ParallelFor(res, 8, [&](int b, int e) {
				for (int z = b; z < e; ++z)
				{
					const float* src = &v[(size_t)z * res];
					float* dst = &tmp[(size_t)z * res];
					double sum = 0.0;
					for (int k = -r; k <= r; ++k)
						sum += src[std::clamp(k, 0, res - 1)];
					for (int x = 0; x < res; ++x)
					{
						dst[x] = (float)(sum / (2 * r + 1));
						sum += src[(std::min)(x + r + 1, res - 1)] - src[(std::max)(x - r, 0)];
					}
				}
			}, "TerrainGraph Blur");
			Jobs::ParallelFor(res, 8, [&](int b, int e) {
				for (int x = b; x < e; ++x)
				{
					double sum = 0.0;
					for (int k = -r; k <= r; ++k)
						sum += tmp[(size_t)std::clamp(k, 0, res - 1) * res + x];
					for (int z = 0; z < res; ++z)
					{
						v[(size_t)z * res + x] = (float)(sum / (2 * r + 1));
						sum += tmp[(size_t)(std::min)(z + r + 1, res - 1) * res + x] - tmp[(size_t)(std::max)(z - r, 0) * res + x];
					}
				}
			}, "TerrainGraph Blur");
		}

		// ---- 수력 침식 (Hans Beyer 의 물방울, Sebastian Lague 구현 — 속도 식 고침). 높이 단위 = 칸 (기울기가 그대로 경사)
		//  병렬: 칸을 블록 (한 변 > 2 × 물방울이 닿는 거리) 으로 나눠 2 x 2 색으로 네 번 — 같은 색 블록은 닿는 곳이 겹치지 않아 잠금 없이 · 같은 결과
		struct ErosionParams
		{
			float Amount = 1.0f, Erosion = 0.3f, Deposition = 0.3f, Capacity = 4.0f, Inertia = 0.05f, Evaporation = 0.02f;
			int Lifetime = 48, Radius = 2;
			uint32_t Seed = 1;
			float DirX = 0.0f, DirZ = 0.0f;   // Erosion2 Direction: 물방울이 이쪽으로 조금 더 (바람 · 기운 지층)
		};

		void SmoothChange(std::vector<float>& H, const std::vector<float>& before, std::vector<float>& tmp, int n);
		void HydraulicErosion(std::vector<float>& H, int n, const ErosionParams& p, std::vector<float>& flow, std::vector<float>& deposits, std::vector<float>& wear,
			const EvalContext& ctx, float progress0, float progress1)
		{
			// 붓 (반지름 안 무게, 합 1)
			struct BrushCell { int Dx, Dz; float W; };
			std::vector<BrushCell> brush;
			{
				float sum = 0.0f;
				for (int dz = -p.Radius; dz <= p.Radius; ++dz)
					for (int dx = -p.Radius; dx <= p.Radius; ++dx)
					{
						const float d = sqrtf((float)(dx * dx + dz * dz));
						if (d <= p.Radius)
						{
							const float w = (std::max)(0.0f, p.Radius + 0.5f - d);
							brush.push_back({ dx, dz, w });
							sum += w;
						}
					}
				for (BrushCell& b : brush)
					b.W /= sum;
			}
			const int reach = p.Lifetime + p.Radius + 2;
			const int block = (std::max)(32, 2 * reach + 2);
			const int nb = (n + block - 1) / block;
			const int passes = 4;
			const double total = (double)p.Amount * n * n;
			const int perBlock = (std::max)(1, (int)(total / ((double)nb * nb * passes)));
			const float gravity = 4.0f, minCapacity = 0.01f;
			std::vector<float> before(H.size()), delta(H.size());
			for (int pass = 0; pass < passes; ++pass)
			{
				before = H;
				for (int phase = 0; phase < 4; ++phase)
				{
					if (Cancelled(ctx))
						return;
					std::vector<int> blocks;
					for (int bz = phase >> 1; bz < nb; bz += 2)
						for (int bx = phase & 1; bx < nb; bx += 2)
							blocks.push_back(bz * nb + bx);
					Jobs::ParallelFor((int)blocks.size(), 1, [&](int b0, int b1) {
						for (int bi = b0; bi < b1; ++bi)
						{
							const int bx = blocks[(size_t)bi] % nb, bz = blocks[(size_t)bi] / nb;
							uint32_t rng = N::Hash(bx, bz, p.Seed * 7919u + (uint32_t)pass * 104729u) | 1u;
							auto next = [&]() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (rng & 0xFFFFFF) / 16777216.0f; };
							for (int d = 0; d < perBlock; ++d)
							{
								float x = (float)(bx * block) + next() * block;
								float z = (float)(bz * block) + next() * block;
								if (x >= n - 2 || z >= n - 2)
									continue;   // 맵 밖 (가장자리 블록) — 가장자리 줄에 몰리지 않게 건너뛴다
								float dirX = 0.0f, dirZ = 0.0f, speed = 1.0f, water = 1.0f, sediment = 0.0f;
								for (int life = 0; life < p.Lifetime; ++life)
								{
									const int ix = (int)x, iz = (int)z;
									const float u = x - ix, v = z - iz;
									const size_t idx = (size_t)iz * n + ix;
									const float h00 = H[idx], h10 = H[idx + 1], h01 = H[idx + n], h11 = H[idx + n + 1];
									const float gx = (h10 - h00) * (1 - v) + (h11 - h01) * v;
									const float gz = (h01 - h00) * (1 - u) + (h11 - h10) * u;
									const float h = h00 * (1 - u) * (1 - v) + h10 * u * (1 - v) + h01 * (1 - u) * v + h11 * u * v;
									dirX = dirX * p.Inertia - gx * (1 - p.Inertia) + p.DirX * 0.1f;
									dirZ = dirZ * p.Inertia - gz * (1 - p.Inertia) + p.DirZ * 0.1f;
									const float len = sqrtf(dirX * dirX + dirZ * dirZ);
									if (len < 1e-6f)
										break;
									dirX /= len; dirZ /= len;
									x += dirX; z += dirZ;
									if (x < 0.0f || z < 0.0f || x >= n - 1.001f || z >= n - 1.001f)
										break;
									const int jx = (int)x, jz = (int)z;
									const float uu = x - jx, vv = z - jz;
									const size_t jdx = (size_t)jz * n + jx;
									const float nh = H[jdx] * (1 - uu) * (1 - vv) + H[jdx + 1] * uu * (1 - vv) + H[jdx + n] * (1 - uu) * vv + H[jdx + n + 1] * uu * vv;
									const float dh = nh - h;
									const float capacity = (std::max)(-dh * speed * water * p.Capacity, minCapacity);
									if (sediment > capacity || dh > 0.0f)
									{
										const float amount = dh > 0.0f ? (std::min)(dh, sediment) : (sediment - capacity) * p.Deposition;
										sediment -= amount;
										H[idx] += amount * (1 - u) * (1 - v);
										H[idx + 1] += amount * u * (1 - v);
										H[idx + n] += amount * (1 - u) * v;
										H[idx + n + 1] += amount * u * v;
										deposits[idx] += amount;
									}
									else
									{
										const float amount = (std::min)((capacity - sediment) * p.Erosion, -dh);
										for (const BrushCell& bc : brush)
										{
											const int cx = ix + bc.Dx, cz = iz + bc.Dz;
											if (cx < 0 || cz < 0 || cx >= n || cz >= n)
												continue;
											float& cell = H[(size_t)cz * n + cx];
											const float want = amount * bc.W;
											const float take = cell < want ? (std::max)(cell, 0.0f) : want;
											cell -= take;
											sediment += take;
											wear[(size_t)cz * n + cx] += take;
										}
									}
									speed = sqrtf((std::max)(0.0f, speed * speed - dh * gravity));
									water *= 1.0f - p.Evaporation;
									flow[idx] += water;
								}
							}
						}
					}, "TerrainGraph Erosion");
					if (ctx.Progress)
						ctx.Progress(progress0 + (progress1 - progress0) * (pass * 4 + phase + 1) / (passes * 4.0f), "Erosion");
				}
				// 이 번에 바뀐 양을 1-2-1 로 고르게 (물방울 하나가 남긴 한 칸짜리 구덩이 · 둔덕을 지운다 — 골은 붓보다 넓어 남는다, 합은 그대로)
				SmoothChange(H, before, delta, n);
			}
		}

		void SmoothChange(std::vector<float>& H, const std::vector<float>& before, std::vector<float>& tmp, int n)
		{
			Jobs::ParallelFor(n, 16, [&](int z0, int z1) {
				for (int z = z0; z < z1; ++z)
				{
					const size_t row = (size_t)z * n;
					for (int x = 0; x < n; ++x)
					{
						const int xl = x > 0 ? x - 1 : x, xr = x < n - 1 ? x + 1 : x;
						const float dl = H[row + xl] - before[row + xl], dc = H[row + x] - before[row + x], dr = H[row + xr] - before[row + xr];
						tmp[row + x] = (dl + 2.0f * dc + dr) * 0.25f;
					}
				}
			}, "TerrainGraph Erosion Smooth");
			Jobs::ParallelFor(n, 16, [&](int z0, int z1) {
				for (int z = z0; z < z1; ++z)
				{
					const size_t up = (size_t)(z > 0 ? z - 1 : z) * n, row = (size_t)z * n, dn = (size_t)(z < n - 1 ? z + 1 : z) * n;
					for (int x = 0; x < n; ++x)
						H[row + x] = before[row + x] + (tmp[up + x] + 2.0f * tmp[row + x] + tmp[dn + x]) * 0.25f;
				}
			}, "TerrainGraph Erosion Smooth");
		}

		// 누적 맵 → 0..1 (1 - exp(-v / (평균 × k)) — 극단값에 끌려가지 않게)
		void NormalizeAccum(std::vector<float>& v, float k)
		{
			double sum = 0.0;
			size_t count = 0;
			for (float f : v)
				if (f > 0.0f) { sum += f; ++count; }
			const float mean = count ? (float)(sum / count) : 1.0f;
			const float inv = 1.0f / (std::max)(mean * k, 1e-9f);
			Jobs::ParallelFor((int)(v.size() / 4096 + 1), 1, [&](int b, int e) {
				for (size_t i = (size_t)b * 4096; i < (std::min)(v.size(), (size_t)e * 4096); ++i)
					v[i] = 1.0f - expf(-v[i] * inv);
			}, "TerrainGraph Normalize");
		}

		// ---- 열 침식: 이웃과의 차가 talus 를 넘는 만큼 옮긴다 (짝마다 같은 식 — 질량이 보존된다), 야코비 (병렬)
		void ThermalErosion(std::vector<float>& H, int n, float talus, int iterations, float strength, const EvalContext& ctx)
		{
			std::vector<float> next(H.size());
			const float c = 0.1f * strength;
			const float tDiag = talus * 1.41421356f;
			for (int it = 0; it < iterations; ++it)
			{
				if (Cancelled(ctx))
					return;
				Jobs::ParallelFor(n, 8, [&](int b, int e) {
					for (int z = b; z < e; ++z)
						for (int x = 0; x < n; ++x)
						{
							const size_t i = (size_t)z * n + x;
							const float h = H[i];
							float delta = 0.0f;
							for (int dz = -1; dz <= 1; ++dz)
								for (int dx = -1; dx <= 1; ++dx)
								{
									if ((dx | dz) == 0)
										continue;
									const int nx = x + dx, nz = z + dz;
									if (nx < 0 || nz < 0 || nx >= n || nz >= n)
										continue;
									const float hn = H[(size_t)nz * n + nx];
									const float t = (dx != 0 && dz != 0) ? tDiag : talus;
									const float d = hn - h;
									if (d > t) delta += (d - t);          // 이웃에서 내게
									else if (-d > t) delta -= (-d - t);   // 내게서 이웃으로
								}
							next[i] = h + c * delta * 0.125f;
						}
				}, "TerrainGraph Thermal");
				H.swap(next);
			}
		}

		// 보로노이 (F1, F2): 격자 칸마다 점 하나
		void Voronoi(double x, double z, uint32_t seed, float jitter, float& f1, float& f2, uint32_t& cellHash)
		{
			const int ix = (int)std::floor(x), iz = (int)std::floor(z);
			f1 = f2 = 1e9f;
			cellHash = 0;
			for (int dz = -1; dz <= 1; ++dz)
				for (int dx = -1; dx <= 1; ++dx)
				{
					const uint32_t h = N::Hash(ix + dx, iz + dz, seed);
					const double px = ix + dx + 0.5 + (N::Rand01(h) - 0.5) * jitter;
					const double pz = iz + dz + 0.5 + (N::Rand01(h * 747796405u + 1u) - 0.5) * jitter;
					const float d = (float)std::sqrt((px - x) * (px - x) + (pz - z) * (pz - z));
					if (d < f1) { f2 = f1; f1 = d; cellHash = h; }
					else if (d < f2) f2 = d;
				}
		}

		// 노드 하나 (입력 맵 · 출력 맵들)
		// ---- 여러 해상도 침식 (Erosion2): f 배 성긴 격자로 줄여 깎고, 바뀐 양만 쌍선형으로 되돌려 더한다 (큰 골짜기)
		void ErodeAtScale(std::vector<float>& H, int n, int f, float cell, const ErosionParams& p, std::vector<float>& flow, std::vector<float>& dep,
			std::vector<float>& wear, const EvalContext& c, float p0, float p1)
		{
			const float toCells = c.MaxHeight / (cell * f);
			if (f <= 1)
			{
				for (float& v : H)
					v *= toCells;
				HydraulicErosion(H, n, p, flow, dep, wear, c, p0, p1);
				for (float& v : H)
					v /= toCells;
				return;
			}
			const int m = (n - 1) / f + 1;
			std::vector<float> low((size_t)m * m), before, fl((size_t)m * m, 0.0f), dp((size_t)m * m, 0.0f), wr((size_t)m * m, 0.0f);
			const int half = f / 2;
			Jobs::ParallelFor(m, 8, [&](int b, int e) {
				for (int j = b; j < e; ++j)
					for (int i = 0; i < m; ++i)
					{
						double sum = 0.0;
						int cnt = 0;
						for (int dz = -half; dz <= half; ++dz)
							for (int dx = -half; dx <= half; ++dx)
							{
								const int x = std::clamp(i * f + dx, 0, n - 1), z = std::clamp(j * f + dz, 0, n - 1);
								sum += H[(size_t)z * n + x];
								++cnt;
							}
						low[(size_t)j * m + i] = (float)(sum / cnt) * toCells;
					}
			}, "TerrainGraph Erosion Down");
			before = low;
			HydraulicErosion(low, m, p, fl, dp, wr, c, p0, p1);
			for (size_t k = 0; k < low.size(); ++k)
				low[k] = (low[k] - before[k]) / toCells;   // 바뀐 양 (정규화 높이)
			auto bil = [&](const std::vector<float>& g, float fx, float fz) {
				const int ix = std::clamp((int)fx, 0, m - 2), iz = std::clamp((int)fz, 0, m - 2);
				const float tx = fx - ix, tz = fz - iz;
				const size_t i0 = (size_t)iz * m + ix;
				return (g[i0] * (1 - tx) + g[i0 + 1] * tx) * (1 - tz) + (g[i0 + m] * (1 - tx) + g[i0 + m + 1] * tx) * tz;
			};
			const float inv = 1.0f / f;
			Jobs::ParallelFor(n, 8, [&](int b, int e) {
				for (int z = b; z < e; ++z)
					for (int x = 0; x < n; ++x)
					{
						const size_t i = (size_t)z * n + x;
						const float fx = x * inv, fz = z * inv;
						H[i] += bil(low, fx, fz);
						flow[i] += bil(fl, fx, fz);
						dep[i] += bil(dp, fx, fz);
						wear[i] += bil(wr, fx, fz);
					}
			}, "TerrainGraph Erosion Up");
		}

		// 점 p 에서 선분 (a → b) 까지 거리 · 매개변수
		inline float SegDist(float px, float pz, float ax, float az, float bx, float bz, float& t)
		{
			const float dx = bx - ax, dz = bz - az;
			const float l2 = dx * dx + dz * dz;
			t = l2 > 1e-9f ? std::clamp(((px - ax) * dx + (pz - az) * dz) / l2, 0.0f, 1.0f) : 0.0f;
			const float qx = ax + dx * t - px, qz = az + dz * t - pz;
			return sqrtf(qx * qx + qz * qz);
		}

		// 선을 맵에 찍는다: 선분마다 (bbox 안) fn(i, d, t, seg) — 순서대로 (min 으로 합치는 것은 순서와 상관없다)
		template <class F>
		void StampPath(const Path& path, int n, const EvalContext& c, float reach, F&& fn)
		{
			const float cell = c.Cell(), x0 = c.X0();
			for (size_t s = 0; s + 1 < path.Points.size(); ++s)
			{
				const PathPoint& a = path.Points[s];
				const PathPoint& b = path.Points[s + 1];
				const float r = reach + (std::max)(a.Width, b.Width) * 0.5f;
				const int ix0 = std::clamp((int)floorf(((std::min)(a.X, b.X) - r - x0) / cell), 0, n - 1), ix1 = std::clamp((int)ceilf(((std::max)(a.X, b.X) + r - x0) / cell), 0, n - 1);
				const int iz0 = std::clamp((int)floorf(((std::min)(a.Z, b.Z) - r - x0) / cell), 0, n - 1), iz1 = std::clamp((int)ceilf(((std::max)(a.Z, b.Z) + r - x0) / cell), 0, n - 1);
				for (int z = iz0; z <= iz1; ++z)
					for (int x = ix0; x <= ix1; ++x)
					{
						float t;
						const float d = SegDist(x0 + x * cell, x0 + z * cell, a.X, a.Z, b.X, b.Z, t);
						if (d <= r)
							fn((size_t)z * n + x, d, t, a, b);
					}
			}
		}

		// 차이킨 (모서리 깎기) — 끝점은 그대로
		void Chaikin(std::vector<PathPoint>& p, int iterations)
		{
			for (int it = 0; it < iterations && p.size() >= 3; ++it)
			{
				std::vector<PathPoint> q;
				q.reserve(p.size() * 2);
				q.push_back(p.front());
				for (size_t i = 0; i + 1 < p.size(); ++i)
				{
					const PathPoint& a = p[i];
					const PathPoint& b = p[i + 1];
					auto mix = [](const PathPoint& u, const PathPoint& v, float t) {
						return PathPoint{ u.X + (v.X - u.X) * t, u.Z + (v.Z - u.Z) * t, u.Y + (v.Y - u.Y) * t, u.Width + (v.Width - u.Width) * t };
					};
					if (i > 0) q.push_back(mix(a, b, 0.25f));
					if (i + 2 < p.size()) q.push_back(mix(a, b, 0.75f));
				}
				q.push_back(p.back());
				p.swap(q);
			}
		}

		// ---- 강: 우선순위 채우기 (Barnes) → D8 받는 칸 → 물 모으기 → 넓은 땅의 물이 모인 칸을 하류로 따라가 선 → 강바닥 · 둑을 판다
		struct RiverParams { float ThresholdKm2 = 20.0f, Depth = 5.0f, Width = 60.0f, Bank = 40.0f; int Count = 60; };
		void MakeRivers(std::vector<float>& H, std::vector<float>& mask, int n, const EvalContext& c, const RiverParams& rp, std::vector<Path>& out)
		{
			int f = 1;
			while ((n - 1) / f + 1 > 1025)
				f *= 2;
			const int m = (n - 1) / f + 1;
			const float cell = c.Cell(), gcell = cell * f, x0 = c.X0();
			std::vector<float> g((size_t)m * m);
			for (int j = 0; j < m; ++j)
				for (int i = 0; i < m; ++i)
					g[(size_t)j * m + i] = H[(size_t)(j * f) * n + i * f];
			// 웅덩이 채우기 (가장자리에서 안으로, 낮은 것부터) — 모든 칸이 가장자리로 흘러 나간다
			std::vector<float> filled = g;
			std::vector<uint8_t> closed((size_t)m * m, 0);
			using QE = std::pair<float, int>;
			std::priority_queue<QE, std::vector<QE>, std::greater<QE>> pq;
			for (int i = 0; i < m; ++i)
				for (int idx : { i, (m - 1) * m + i, i * m, i * m + m - 1 })
					if (!closed[(size_t)idx])
					{
						closed[(size_t)idx] = 1;
						pq.push({ filled[(size_t)idx], idx });
					}
			const float eps = 1e-6f;
			static const int kDx[8] = { 1, -1, 0, 0, 1, 1, -1, -1 }, kDz[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
			while (!pq.empty())
			{
				const auto [h, idx] = pq.top();
				pq.pop();
				const int ix = idx % m, iz = idx / m;
				for (int k = 0; k < 8; ++k)
				{
					const int nx = ix + kDx[k], nz = iz + kDz[k];
					if (nx < 0 || nz < 0 || nx >= m || nz >= m)
						continue;
					const int ni = nz * m + nx;
					if (closed[(size_t)ni])
						continue;
					closed[(size_t)ni] = 1;
					filled[(size_t)ni] = (std::max)(filled[(size_t)ni], h + eps);
					pq.push({ filled[(size_t)ni], ni });
				}
			}
			// 받는 칸: 가장 가파르게 내려가는 이웃 (가장자리 = 밖으로)
			std::vector<int> recv((size_t)m * m, -1);
			for (int iz = 1; iz < m - 1; ++iz)
				for (int ix = 1; ix < m - 1; ++ix)
				{
					const int idx = iz * m + ix;
					float best = 0.0f;
					for (int k = 0; k < 8; ++k)
					{
						const int ni = (iz + kDz[k]) * m + ix + kDx[k];
						const float drop = (filled[(size_t)idx] - filled[(size_t)ni]) / (k < 4 ? 1.0f : 1.41421356f);
						if (drop > best) { best = drop; recv[(size_t)idx] = ni; }
					}
				}
			// 물 모으기 (높은 칸부터)
			std::vector<int> order((size_t)m * m);
			for (int i = 0; i < m * m; ++i)
				order[(size_t)i] = i;
			std::sort(order.begin(), order.end(), [&](int a, int b) { return filled[(size_t)a] > filled[(size_t)b]; });
			const float cellKm2 = gcell * gcell * 1e-6f;
			std::vector<float> acc((size_t)m * m, cellKm2);
			for (int idx : order)
				if (recv[(size_t)idx] >= 0)
					acc[(size_t)recv[(size_t)idx]] += acc[(size_t)idx];
			const float thr = rp.ThresholdKm2;
			// 머리 (위로 강 칸이 없는 강 칸) 에서 하류로
			std::vector<uint8_t> hasUp((size_t)m * m, 0);
			for (int idx = 0; idx < m * m; ++idx)
				if (acc[(size_t)idx] >= thr && recv[(size_t)idx] >= 0)
					hasUp[(size_t)recv[(size_t)idx]] = 1;
			std::vector<int> heads;
			for (int idx = 0; idx < m * m; ++idx)
				if (acc[(size_t)idx] >= thr && !hasUp[(size_t)idx])
					heads.push_back(idx);
			// 큰 강부터 (하구에서 모인 물이 많은 것) — 머리가 같은 하구로 가면 먼저 간 선이 본류
			auto mouthAcc = [&](int idx) { float a = 0.0f; for (int k = 0; idx >= 0 && k < m * 4; ++k) { a = acc[(size_t)idx]; idx = recv[(size_t)idx]; } return a; };
			std::vector<std::pair<float, int>> ranked;
			for (int h : heads)
				ranked.push_back({ mouthAcc(h), h });
			std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
			const float big = thr * 40.0f;
			std::vector<uint8_t> visited((size_t)m * m, 0);
			std::vector<Path> paths;
			for (const auto& [ma, head] : ranked)
			{
				if ((int)paths.size() >= rp.Count * 3)
					break;
				Path p;
				p.Kind = 0;
				int idx = head;
				float maxAcc = 0.0f;
				while (idx >= 0)
				{
					const int ix = idx % m, iz = idx / m;
					const float a = acc[(size_t)idx];
					maxAcc = (std::max)(maxAcc, a);
					const float order01 = sqrtf((std::min)(1.0f, a / big));
					// 높이 = 채우기 전 땅 (채운 높이는 넓은 분지에서 땅보다 높아 물이 떠 보였다)
					p.Points.push_back({ x0 + ix * gcell, x0 + iz * gcell, g[(size_t)idx] * c.MaxHeight, (std::max)(3.0f, rp.Width * order01) });
					if (visited[(size_t)idx])
						break;   // 이미 그린 강에 합류
					visited[(size_t)idx] = 1;
					idx = recv[(size_t)idx];
				}
				if (p.Points.size() < 4)
					continue;
				p.Order = sqrtf((std::min)(1.0f, maxAcc / big));
				paths.push_back(std::move(p));
			}
			std::sort(paths.begin(), paths.end(), [](const Path& a, const Path& b) { return a.Order > b.Order; });
			if ((int)paths.size() > rp.Count)
				paths.resize((size_t)rp.Count);
			// 매끈하게 → 굽이 (D8 은 평평한 곳에서 곧은 줄 — 하류 거리를 따라 옆으로 미는 노이즈, 완만할수록 크게) → 물높이는 하류로 갈수록 낮게
			for (Path& p : paths)
			{
				Chaikin(p.Points, 2);
				float run = 0.0f;
				std::vector<PathPoint> moved = p.Points;
				for (size_t k = 0; k < p.Points.size(); ++k)
				{
					if (k > 0)
						run += sqrtf((p.Points[k].X - p.Points[k - 1].X) * (p.Points[k].X - p.Points[k - 1].X) + (p.Points[k].Z - p.Points[k - 1].Z) * (p.Points[k].Z - p.Points[k - 1].Z));
					const size_t a = k > 0 ? k - 1 : k, b = k + 1 < p.Points.size() ? k + 1 : k;
					float tx = p.Points[b].X - p.Points[a].X, tz = p.Points[b].Z - p.Points[a].Z;
					const float tl = sqrtf(tx * tx + tz * tz);
					if (tl < 1e-3f)
						continue;
					tx /= tl; tz /= tl;
					// 경사 (이웃 점 높이 차) 가 작을수록 크게 굽는다. 끝점은 그대로 (합류 · 하구가 맞게)
					const float slope = fabsf(p.Points[b].Y - p.Points[a].Y) / (std::max)(tl, 1.0f);
					const float flat = std::clamp(1.0f - slope * 25.0f, 0.15f, 1.0f);
					const float ends = (std::min)(1.0f, (std::min)((float)k, (float)(p.Points.size() - 1 - k)) / 6.0f);
					const float amp = (25.0f + 90.0f * p.Order) * flat * ends;
					const float m = N::Fbm(run / (300.0 + 500.0 * p.Order), (double)(&p - paths.data()) * 7.31, (uint32_t)c.Seed * 977u + 31u, 3);
					moved[k].X += -tz * m * amp;
					moved[k].Z += tx * m * amp;
				}
				p.Points.swap(moved);
				Chaikin(p.Points, 1);
				float lowest = 1e9f;
				for (PathPoint& q : p.Points)
				{
					// 굽은 자리의 땅 높이 (쌍선형) 로 물높이 — 하류로 갈수록 낮게, 땅보다 조금 아래
					const float fx = (q.X - x0) / cell, fz = (q.Z - x0) / cell;
					const int ix = std::clamp((int)fx, 0, n - 2), iz = std::clamp((int)fz, 0, n - 2);
					const float tx2 = fx - ix, tz2 = fz - iz;
					const size_t i0 = (size_t)iz * n + ix;
					const float ground = ((H[i0] * (1 - tx2) + H[i0 + 1] * tx2) * (1 - tz2) + (H[i0 + n] * (1 - tx2) + H[i0 + n + 1] * tx2) * tz2) * c.MaxHeight;
					// 하류로 갈수록 낮게 — 다만 땅보다 6 m 넘게 낮아지지 않게 (턱을 넘는 곳에서 수십 m 협곡 벽이 생겼다: 그때는 물이 조금 올라간다)
					lowest = (std::min)(lowest, (std::min)(q.Y, ground));
					lowest = (std::max)(lowest, ground - 6.0f);
					q.Y = lowest - 0.4f;
				}
			}
			// 판다: 바닥 (포물선) + 둑 (완만한 비탈)
			const float bankSlope = 0.3f;
			for (const Path& p : paths)
				StampPath(p, n, c, rp.Bank, [&](size_t i, float d, float t, const PathPoint& a, const PathPoint& b) {
					const float wl = a.Y + (b.Y - a.Y) * t;
					const float hw = (a.Width + (b.Width - a.Width) * t) * 0.5f;
					float& h = H[i];
					const float hm = h * c.MaxHeight;
					float target;
					if (d < hw)
					{
						const float u = d / hw;
						target = wl - rp.Depth * (1.0f - u * u);
						mask[i] = 1.0f;
					}
					else
					{
						const float u = std::clamp((d - hw) / (std::max)(rp.Bank, 1.0f), 0.0f, 1.0f);
						const float bank = wl + 0.5f + (d - hw) * bankSlope;
						target = bank + (hm - bank) * u * u;
						mask[i] = (std::max)(mask[i], 1.0f - u);
					}
					if (target < hm)
						h = target / c.MaxHeight;
				});
			out = std::move(paths);
		}

		// ---- 길: 마을 (완만 · 낮은 땅, 고르게) → 최소 신장 트리 (+ 고리) → 경사 · 피할 곳을 꺼리는 A* (이미 낸 길은 싸게) → 매끈한 선 · 기울기 → 깎고 메운다
		struct RoadParams { int Towns = 14; float Width = 7.0f, MaxSlope = 9.0f, Loops = 0.3f; uint32_t Seed = 1; };
		void MakeRoads(std::vector<float>& H, std::vector<float>& mask, int n, const EvalContext& c, const RoadParams& rp, const Map* avoid, std::vector<Path>& out)
		{
			int f = 1;
			while ((n - 1) / f + 1 > 513)
				f *= 2;
			const int m = (n - 1) / f + 1;
			const float cell = c.Cell(), gcell = cell * f, x0 = c.X0();
			std::vector<float> g((size_t)m * m), av((size_t)m * m, 0.0f), sd((size_t)m * m, 0.0f);
			for (int j = 0; j < m; ++j)
				for (int i = 0; i < m; ++i)
				{
					g[(size_t)j * m + i] = H[(size_t)(j * f) * n + i * f] * c.MaxHeight;
					if (avoid && avoid->Res == n)
					{
						float a = 0.0f;
						for (int dz = -f / 2; dz <= f / 2; dz += (std::max)(1, f / 2))
							for (int dx = -f / 2; dx <= f / 2; dx += (std::max)(1, f / 2))
								a = (std::max)(a, avoid->At(i * f + dx, j * f + dz));
						av[(size_t)j * m + i] = a;
					}
				}
			auto G = [&](int i, int j) { return g[(size_t)std::clamp(j, 0, m - 1) * m + std::clamp(i, 0, m - 1)]; };
			for (int j = 0; j < m; ++j)
				for (int i = 0; i < m; ++i)
				{
					const float gx = (G(i + 1, j) - G(i - 1, j)) / (2 * gcell), gz = (G(i, j + 1) - G(i, j - 1)) / (2 * gcell);
					sd[(size_t)j * m + i] = atanf(sqrtf(gx * gx + gz * gz)) * 57.29578f;
				}
			// 마을: 완만하고 (최대 경사의 0.6 배 아래) 높이 아래 70 % · 피할 곳 밖, 서로 멀리
			std::vector<float> sortedH = g;
			std::nth_element(sortedH.begin(), sortedH.begin() + (ptrdiff_t)(sortedH.size() * 7 / 10), sortedH.end());
			const float hCut = sortedH[sortedH.size() * 7 / 10];
			std::vector<std::pair<uint32_t, int>> cand;
			const int margin = (std::max)(2, m / 30);
			for (int j = margin; j < m - margin; ++j)
				for (int i = margin; i < m - margin; ++i)
				{
					const size_t idx = (size_t)j * m + i;
					if (sd[idx] < rp.MaxSlope * 0.6f && g[idx] < hCut && av[idx] < 0.3f)
						cand.push_back({ N::Hash(i, j, rp.Seed), (int)idx });
				}
			std::sort(cand.begin(), cand.end());
			std::vector<int> towns;
			float minDist = c.WorldSize / (sqrtf((float)rp.Towns) * 1.25f);
			for (int pass = 0; pass < 4 && (int)towns.size() < rp.Towns; ++pass, minDist *= 0.75f)
				for (const auto& [hsh, idx] : cand)
				{
					if ((int)towns.size() >= rp.Towns)
						break;
					const float tx = (idx % m) * gcell, tz = (idx / m) * gcell;
					bool ok = true;
					for (int t : towns)
					{
						const float dx = (t % m) * gcell - tx, dz = (t / m) * gcell - tz;
						if (dx * dx + dz * dz < minDist * minDist) { ok = false; break; }
					}
					if (ok)
						towns.push_back(idx);
				}
			if (towns.size() < 2)
				return;
			// 이을 짝: 최소 신장 트리 (Prim) + 짧은 고리
			const int T = (int)towns.size();
			auto tdist = [&](int a, int b) { const float dx = (float)(towns[a] % m - towns[b] % m), dz = (float)(towns[a] / m - towns[b] / m); return sqrtf(dx * dx + dz * dz); };
			std::vector<std::pair<int, int>> links;
			{
				std::vector<uint8_t> in((size_t)T, 0);
				std::vector<float> best((size_t)T, 1e30f);
				std::vector<int> from((size_t)T, -1);
				best[0] = 0.0f;
				for (int it = 0; it < T; ++it)
				{
					int u = -1;
					for (int k = 0; k < T; ++k)
						if (!in[(size_t)k] && (u < 0 || best[(size_t)k] < best[(size_t)u]))
							u = k;
					in[(size_t)u] = 1;
					if (from[(size_t)u] >= 0)
						links.push_back({ from[(size_t)u], u });
					for (int k = 0; k < T; ++k)
						if (!in[(size_t)k] && tdist(u, k) < best[(size_t)k]) { best[(size_t)k] = tdist(u, k); from[(size_t)k] = u; }
				}
				std::vector<std::tuple<float, int, int>> extra;
				for (int a = 0; a < T; ++a)
					for (int b = a + 1; b < T; ++b)
					{
						bool have = false;
						for (const auto& l : links)
							have |= (l.first == a && l.second == b) || (l.first == b && l.second == a);
						if (!have && tdist(a, b) * gcell < minDist * 2.2f)
							extra.push_back({ tdist(a, b), a, b });
					}
				std::sort(extra.begin(), extra.end());
				const int add = (int)std::lround(T * rp.Loops);
				for (int k = 0; k < add && k < (int)extra.size(); ++k)
					links.push_back({ std::get<1>(extra[(size_t)k]), std::get<2>(extra[(size_t)k]) });
			}
			// A* (8 방향): 칸 사이 기울기 (오르막 · 내리막 같게) 가 최대 경사를 넘으면 매우 비싸게, 피할 곳 (강) 도. 이미 낸 길은 싸게 (길이 모인다)
			std::vector<uint8_t> onRoad((size_t)m * m, 0);
			std::vector<float> cost((size_t)m * m);
			std::vector<int> parent((size_t)m * m);
			const float maxGrade = tanf(rp.MaxSlope * 0.0174533f);
			static const int kDx[8] = { 1, -1, 0, 0, 1, 1, -1, -1 }, kDz[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
			std::vector<Path> paths;
			for (const auto& [ta, tb] : links)
			{
				if (Cancelled(c))
					return;
				const int s = towns[(size_t)ta], goal = towns[(size_t)tb];
				std::fill(cost.begin(), cost.end(), 1e30f);
				std::fill(parent.begin(), parent.end(), -1);
				using QE = std::pair<float, int>;
				std::priority_queue<QE, std::vector<QE>, std::greater<QE>> open;
				cost[(size_t)s] = 0.0f;
				const int gx = goal % m, gz = goal / m;
				auto heur = [&](int idx) { const float dx = (float)(idx % m - gx), dz = (float)(idx / m - gz); return sqrtf(dx * dx + dz * dz) * gcell * 0.35f; };
				open.push({ heur(s), s });
				while (!open.empty())
				{
					const auto [fcost, idx] = open.top();
					open.pop();
					if (idx == goal)
						break;
					if (fcost - heur(idx) > cost[(size_t)idx] + 1e-3f)
						continue;
					const int ix = idx % m, iz = idx / m;
					for (int k = 0; k < 8; ++k)
					{
						const int nx = ix + kDx[k], nz = iz + kDz[k];
						if (nx < 1 || nz < 1 || nx >= m - 1 || nz >= m - 1)
							continue;
						const int ni = nz * m + nx;
						const float len = gcell * (k < 4 ? 1.0f : 1.41421356f);
						const float grade = fabsf(g[(size_t)ni] - g[(size_t)idx]) / len;
						float step = len * (1.0f + 6.0f * (grade / maxGrade) * (grade / maxGrade));
						if (grade > maxGrade * 1.8f)
							step += len * 60.0f;
						step += av[(size_t)ni] * len * 30.0f;
						if (onRoad[(size_t)ni])
							step *= 0.35f;
						const float nc = cost[(size_t)idx] + step;
						if (nc < cost[(size_t)ni])
						{
							cost[(size_t)ni] = nc;
							parent[(size_t)ni] = idx;
							open.push({ nc + heur(ni), ni });
						}
					}
				}
				if (parent[(size_t)goal] < 0)
					continue;
				Path p;
				p.Kind = 1;
				for (int idx = goal; idx >= 0; idx = parent[(size_t)idx])
				{
					onRoad[(size_t)idx] = 1;
					p.Points.push_back({ x0 + (idx % m) * gcell, x0 + (idx / m) * gcell, 0.0f, rp.Width });
					if (idx == s)
						break;
				}
				if (p.Points.size() < 2)
					continue;
				Chaikin(p.Points, 3);
				// 높이: 지형을 따라 재고 250 m 창으로 두 번 고르게 (급한 오르내림 없이)
				for (PathPoint& q : p.Points)
				{
					const float fx = (q.X - x0) / cell, fz = (q.Z - x0) / cell;
					const int ix = std::clamp((int)fx, 0, n - 2), iz = std::clamp((int)fz, 0, n - 2);
					const float tx = fx - ix, tz = fz - iz;
					const size_t i0 = (size_t)iz * n + ix;
					q.Y = ((H[i0] * (1 - tx) + H[i0 + 1] * tx) * (1 - tz) + (H[i0 + n] * (1 - tx) + H[i0 + n + 1] * tx) * tz) * c.MaxHeight;
				}
				float spacing = 0.0f;
				for (size_t k = 1; k < p.Points.size(); ++k)
					spacing += sqrtf((p.Points[k].X - p.Points[k - 1].X) * (p.Points[k].X - p.Points[k - 1].X) + (p.Points[k].Z - p.Points[k - 1].Z) * (p.Points[k].Z - p.Points[k - 1].Z));
				spacing /= (float)(p.Points.size() - 1);
				const int win = (std::max)(1, (int)(125.0f / (std::max)(spacing, 1.0f)));
				for (int pass = 0; pass < 2; ++pass)
				{
					std::vector<float> ys(p.Points.size());
					for (size_t k = 0; k < p.Points.size(); ++k)
					{
						float sum = 0.0f;
						int cnt = 0;
						for (int d = -win; d <= win; ++d)
						{
							const int q = std::clamp((int)k + d, 0, (int)p.Points.size() - 1);
							sum += p.Points[(size_t)q].Y;
							++cnt;
						}
						ys[k] = sum / cnt;
					}
					for (size_t k = 0; k < p.Points.size(); ++k)
						p.Points[k].Y = ys[k];
				}
				paths.push_back(std::move(p));
			}
			// 깎고 메운다: 노면은 길 높이, 어깨 (너비 1.5 배 + 4 m) 는 부드럽게 원래 땅으로
			for (const Path& p : paths)
			{
				const float shoulder = rp.Width * 1.5f + 4.0f;
				StampPath(p, n, c, shoulder, [&](size_t i, float d, float t, const PathPoint& a, const PathPoint& b) {
					const float y = (a.Y + (b.Y - a.Y) * t) / c.MaxHeight;
					const float hw = rp.Width * 0.5f;
					float& h = H[i];
					if (d < hw)
					{
						h = y;
						mask[i] = 1.0f;
					}
					else
					{
						const float u = std::clamp((d - hw) / shoulder, 0.0f, 1.0f);
						const float s2 = u * u * (3.0f - 2.0f * u);
						const float w = 1.0f - s2;
						// 여러 길이 겹치면 가장 가까운 길이 이긴다 (mask 로 견준다)
						if (w > mask[i])
						{
							h = y + (h - y) * s2;
							mask[i] = w;
						}
					}
				});
			}
			out = std::move(paths);
		}

		// ---- SatMap: 값 → 팔레트 색 (sRGB) → 색조 · 채도 · 밝기 → 선형
		struct SatMapParams { int Palette = 0; float Bias = 0.0f; bool Reverse = false; int Enhance = 1; float Hue = 0.0f, Saturation = 0.0f, Lightness = 0.0f; };
		void ApplySatMap(const std::vector<float>& v, int res, const SatMapParams& sp, std::vector<float>& rgb)
		{
			const size_t count = (size_t)res * res;
			float lo = 0.0f, hi = 1.0f;
			std::vector<float> cdf;
			if (sp.Enhance == 1)
			{
				lo = 1e30f; hi = -1e30f;
				for (float x : v) { lo = (std::min)(lo, x); hi = (std::max)(hi, x); }
				if (hi - lo < 1e-6f) hi = lo + 1e-6f;
			}
			else if (sp.Enhance == 2)
			{
				lo = 1e30f; hi = -1e30f;
				for (float x : v) { lo = (std::min)(lo, x); hi = (std::max)(hi, x); }
				if (hi - lo < 1e-6f) hi = lo + 1e-6f;
				cdf.assign(1024, 0.0f);
				for (float x : v)
					cdf[(size_t)std::clamp((int)((x - lo) / (hi - lo) * 1023.0f), 0, 1023)] += 1.0f;
				float acc = 0.0f;
				for (float& b : cdf) { acc += b; b = acc / (float)count; }
			}
			Jobs::ParallelFor(res, 16, [&](int b, int e) {
				for (size_t i = (size_t)b * res; i < (size_t)e * res; ++i)
				{
					float t = v[i];
					if (sp.Enhance == 1)
						t = (t - lo) / (hi - lo);
					else if (sp.Enhance == 2)
						t = cdf[(size_t)std::clamp((int)((t - lo) / (hi - lo) * 1023.0f), 0, 1023)];
					t = std::clamp(t + sp.Bias, 0.0f, 1.0f);
					if (sp.Reverse)
						t = 1.0f - t;
					float c3[3];
					PaletteColor(sp.Palette, t, c3);
					if (sp.Hue != 0.0f || sp.Saturation != 0.0f || sp.Lightness != 0.0f)
					{
						// HSL
						const float mx = (std::max)({ c3[0], c3[1], c3[2] }), mn = (std::min)({ c3[0], c3[1], c3[2] });
						float hh = 0.0f, ss = 0.0f, ll = (mx + mn) * 0.5f;
						const float d = mx - mn;
						if (d > 1e-6f)
						{
							ss = ll > 0.5f ? d / (2.0f - mx - mn) : d / (mx + mn);
							if (mx == c3[0]) hh = (c3[1] - c3[2]) / d + (c3[1] < c3[2] ? 6.0f : 0.0f);
							else if (mx == c3[1]) hh = (c3[2] - c3[0]) / d + 2.0f;
							else hh = (c3[0] - c3[1]) / d + 4.0f;
							hh /= 6.0f;
						}
						hh = hh + sp.Hue / 360.0f;
						hh -= floorf(hh);
						ss = std::clamp(ss * (1.0f + sp.Saturation), 0.0f, 1.0f);
						ll = std::clamp(ll + sp.Lightness * 0.5f, 0.0f, 1.0f);
						auto hue2 = [](float p, float q, float tt) {
							if (tt < 0.0f) tt += 1.0f;
							if (tt > 1.0f) tt -= 1.0f;
							if (tt < 1.0f / 6.0f) return p + (q - p) * 6.0f * tt;
							if (tt < 0.5f) return q;
							if (tt < 2.0f / 3.0f) return p + (q - p) * (2.0f / 3.0f - tt) * 6.0f;
							return p;
						};
						if (ss < 1e-6f)
							c3[0] = c3[1] = c3[2] = ll;
						else
						{
							const float q = ll < 0.5f ? ll * (1.0f + ss) : ll + ss - ll * ss, p = 2.0f * ll - q;
							c3[0] = hue2(p, q, hh + 1.0f / 3.0f);
							c3[1] = hue2(p, q, hh);
							c3[2] = hue2(p, q, hh - 1.0f / 3.0f);
						}
					}
					for (int ch = 0; ch < 3; ++ch)
						rgb[i * 3 + ch] = powf(std::clamp(c3[ch], 0.0f, 1.0f), 2.2f);
				}
			}, "TerrainGraph SatMap");
		}

		std::vector<MapW> EvalNode(const Node& node, const std::vector<MapPtr>& in, const EvalContext& c, EvalResult& side)
		{
			const int res = c.Res;
			const float cell = c.Cell();
			auto input = [&](size_t i) -> MapPtr { return i < in.size() ? in[i] : nullptr; };
			auto copyIn = [&](size_t i) -> MapW {
				MapPtr m = input(i);
				auto out = NewMap(res);
				if (m)
					out->V = m->V;
				return out;
			};
			MapW out = NewMap(res);
			const uint32_t seedBase = (uint32_t)c.Seed * 1000003u;
			switch (node.Type)
			{
			case NodeType::Constant:
				std::fill(out->V.begin(), out->V.end(), node.Get("value"));
				break;
			case NodeType::Gradient:
			{
				const float a = node.Get("angle") * 0.0174533f;
				const float dx = cosf(a), dz = sinf(a);
				const float lo = node.Get("min"), hi = node.Get("max");
				const float half = c.WorldSize * 0.5f;
				FillRows(*out, c, [&](int, int, double wx, double wz) {
					const float t = std::clamp((float)((wx * dx + wz * dz) / (2.0 * half) + 0.5), 0.0f, 1.0f);
					return lo + (hi - lo) * t;
				});
				break;
			}
			case NodeType::Radial:
			{
				const double cx = node.Get("centerX"), cz = node.Get("centerZ");
				const double r = (std::max)(1.0f, node.Get("radius"));
				const float fall = node.Get("falloff");
				const bool inv = node.Get("invert") > 0.5f;
				FillRows(*out, c, [&](int, int, double wx, double wz) {
					const float d = (float)(std::sqrt((wx - cx) * (wx - cx) + (wz - cz) * (wz - cz)) / r);
					const float v = powf(std::clamp(1.0f - d, 0.0f, 1.0f), 1.0f / (std::max)(fall, 0.05f));
					return inv ? 1.0f - v : v;
				});
				break;
			}
			case NodeType::Perlin:
			{
				const double s = (std::max)(1.0f, node.Get("scale"));
				const int oct = (int)node.Get("octaves");
				const float gain = node.Get("gain"), lac = node.Get("lacunarity"), amp = node.Get("amplitude");
				const uint32_t seed = seedBase + (uint32_t)node.Get("seed") * 7919u;
				FillRows(*out, c, [&](int, int, double wx, double wz) { return 0.5f + 0.5f * amp * N::Fbm(wx / s, wz / s, seed, oct, lac, gain); });
				break;
			}
			case NodeType::Ridged:
			{
				const double s = (std::max)(1.0f, node.Get("scale"));
				const int oct = (int)node.Get("octaves");
				const float gain = node.Get("gain"), sharp = node.Get("sharpness");
				const uint32_t seed = seedBase + (uint32_t)node.Get("seed") * 7919u;
				FillRows(*out, c, [&](int, int, double wx, double wz) { return N::Ridged(wx / s, wz / s, seed, oct, gain, sharp); });
				break;
			}
			case NodeType::Mountain:
			{
				// 휜 좌표의 리지 (산등성이) × 덩어리 (산자락) + 스타일: Eroded = 침식 결 · Old = 둥글게
				const double s = (std::max)(10.0f, node.Get("scale"));
				const float height = node.Get("height"), bulk = node.Get("bulk");
				const int style = (int)node.Get("style");
				const uint32_t seed = seedBase + (uint32_t)node.Get("seed") * 7919u;
				FillRows(*out, c, [&](int, int, double wx, double wz) {
					const double u = wx / s, v = wz / s;
					const double qx = u + 0.6 * N::Fbm(u * 0.7 + 3.1, v * 0.7, seed + 11u, 3);
					const double qz = v + 0.6 * N::Fbm(u * 0.7, v * 0.7 + 7.7, seed + 23u, 3);
					const float mass = 0.5f + 0.5f * N::Fbm(qx * 0.45, qz * 0.45, seed + 37u, 4);   // 산 덩어리 (낮은 주파수)
					float ridge = N::Ridged(qx * 1.6, qz * 1.6, seed + 51u, 7, 0.5f, style == 2 ? 1.2f : 2.2f);
					if (style == 1)
						ridge = 0.55f * ridge + 0.45f * (0.5f + 0.5f * N::Eroded(qx * 1.3, qz * 1.3, seed + 67u, 1.0, 1.0 / 64.0, 1.6f)) * ridge * 1.6f;
					const float m = std::clamp((mass - 0.35f) / 0.5f, 0.0f, 1.0f);
					const float shaped = m * m * (3.0f - 2.0f * m);
					return height * (shaped * (bulk * 0.45f + (1.0f - bulk * 0.45f) * ridge));
				});
				break;
			}
			case NodeType::Voronoi:
			{
				const double s = (std::max)(1.0f, node.Get("scale"));
				const float jitter = node.Get("jitter");
				const int mode = (int)node.Get("mode");
				const uint32_t seed = seedBase + (uint32_t)node.Get("seed") * 7919u;
				FillRows(*out, c, [&](int, int, double wx, double wz) {
					float f1, f2;
					uint32_t h;
					Voronoi(wx / s, wz / s, seed, jitter, f1, f2, h);
					if (mode == 1) return std::clamp((f2 - f1) * 1.5f, 0.0f, 1.0f);
					if (mode == 2) return N::Rand01(h * 2654435761u);
					return std::clamp(1.0f - f1 * 1.2f, 0.0f, 1.0f);
				});
				break;
			}
			case NodeType::Combine:
			{
				MapPtr a = input(0), b = input(1), mask = input(2);
				const int mode = (int)node.Get("mode");
				const float ratio = node.Get("ratio");
				Jobs::ParallelFor(res, 16, [&](int z0, int z1) {
					for (size_t i = (size_t)z0 * res; i < (size_t)z1 * res; ++i)
					{
						const float va = a ? a->V[i] : 0.0f, vb = b ? b->V[i] : 0.0f;
						const float m = mask ? mask->V[i] : 1.0f;
						const float t = ratio * m;
						float r;
						switch (mode)
						{
						case 0: r = va + vb * t; break;
						case 1: r = va - vb * t; break;
						case 2: r = va * (1.0f + (vb - 1.0f) * t); break;
						case 3: r = va + ((std::max)(va, vb) - va) * m; break;
						case 4: r = va + ((std::min)(va, vb) - va) * m; break;
						case 6: r = va + (fabsf(va - vb) - va) * t; break;
						case 7: r = va + ((1.0f - (1.0f - va) * (1.0f - vb)) - va) * t; break;
						default: r = va + (vb - va) * t; break;
						}
						out->V[i] = r;
					}
				}, "TerrainGraph Combine");
				break;
			}
			case NodeType::Warp:
			{
				MapPtr src = input(0);
				if (!src)
					break;
				const double s = (std::max)(1.0f, node.Get("scale"));
				const float strength = node.Get("strength") / cell;   // 칸
				const int oct = (int)node.Get("octaves");
				const uint32_t seed = seedBase + (uint32_t)node.Get("seed") * 7919u;
				FillRows(*out, c, [&](int x, int z, double wx, double wz) {
					const float ox = N::Fbm(wx / s, wz / s, seed + 1u, oct) * strength;
					const float oz = N::Fbm(wx / s + 5.2, wz / s - 1.3, seed + 2u, oct) * strength;
					return src->Bilinear(x + ox, z + oz);
				});
				break;
			}
			case NodeType::Terrace:
			{
				out = copyIn(0);
				const float steps = (std::max)(2.0f, node.Get("steps"));
				const float sharp = node.Get("sharpness");
				for (float& v : out->V)
				{
					const float t = v * steps;
					const float f = t - floorf(t);
					// 계단 모서리: f 를 sharpness 만큼 꺾는다 (0 = 그대로, 1 = 평평한 계단)
					const float k = powf(f, 1.0f + sharp * 8.0f);
					v = (floorf(t) + k) / steps;
				}
				break;
			}
			case NodeType::Curve:
			{
				out = copyIn(0);
				const float power = node.Get("power"), contrast = node.Get("contrast"), bias = node.Get("bias");
				for (float& v : out->V)
				{
					float t = std::clamp(v + bias, 0.0f, 1.0f);
					t = powf(t, power);
					t = 0.5f + (t - 0.5f) * (1.0f + contrast);
					v = t;
				}
				break;
			}
			case NodeType::Clamp:
			{
				out = copyIn(0);
				const float lo = node.Get("min"), hi = node.Get("max");
				for (float& v : out->V)
					v = std::clamp(v, lo, hi);
				break;
			}
			case NodeType::Normalize:
			{
				out = copyIn(0);
				const auto [lo, hi] = std::minmax_element(out->V.begin(), out->V.end());
				const float a = *lo, d = (std::max)(*hi - *lo, 1e-6f);
				for (float& v : out->V)
					v = (v - a) / d;
				break;
			}
			case NodeType::Invert:
				out = copyIn(0);
				for (float& v : out->V)
					v = 1.0f - v;
				break;
			case NodeType::Blur:
			{
				out = copyIn(0);
				const int r = (std::max)(1, (int)std::lround(node.Get("radius") / cell / 1.73f));
				for (int k = 0; k < 3; ++k)
					BoxBlur(out->V, res, r);
				break;
			}
			case NodeType::Slope:
			{
				MapPtr src = input(0);
				if (!src)
					break;
				const float lo = node.Get("min"), hi = node.Get("max"), fall = node.Get("falloff");
				const float k = c.MaxHeight / (2.0f * cell);
				FillRows(*out, c, [&](int x, int z, double, double) {
					const float gx = (src->At(x + 1, z) - src->At(x - 1, z)) * k;
					const float gz = (src->At(x, z + 1) - src->At(x, z - 1)) * k;
					const float deg = atanf(sqrtf(gx * gx + gz * gz)) * 57.29578f;
					return Band(deg, lo, hi, fall);
				});
				return { out };
			}
			case NodeType::HeightMask:
			{
				MapPtr src = input(0);
				if (!src)
					break;
				const float lo = node.Get("min"), hi = node.Get("max"), fall = node.Get("falloff");
				for (size_t i = 0; i < out->V.size(); ++i)
					out->V[i] = Band(src->V[i], lo, hi, fall);
				return { out };
			}
			case NodeType::Curvature:
			{
				MapPtr src = input(0);
				if (!src)
					break;
				auto blurred = NewMap(res);
				blurred->V = src->V;
				const int r = (std::max)(1, (int)std::lround(node.Get("radius") / cell / 1.73f));
				for (int k = 0; k < 3; ++k)
					BoxBlur(blurred->V, res, r);
				const int mode = (int)node.Get("mode");
				const float contrast = node.Get("contrast") * c.MaxHeight / (std::max)(node.Get("radius"), 1.0f);
				for (size_t i = 0; i < out->V.size(); ++i)
				{
					const float d = (blurred->V[i] - src->V[i]) * contrast;   // + = 둘레보다 낮다 (오목)
					out->V[i] = mode == 0 ? std::clamp(d, 0.0f, 1.0f) : mode == 1 ? std::clamp(-d, 0.0f, 1.0f) : std::clamp(0.5f + d * 0.5f, 0.0f, 1.0f);
				}
				return { out };
			}
			case NodeType::Erosion:
			{
				MapPtr src = input(0);
				out = copyIn(0);
				auto flow = NewMap(res), dep = NewMap(res), wear = NewMap(res);
				if (!src)
					return { out, flow, dep, wear };
				// 높이 → 칸 단위 (m / 칸 크기), 끝나면 되돌린다
				const float toCells = c.MaxHeight / cell;
				for (float& v : out->V)
					v *= toCells;
				ErosionParams p;
				p.Amount = node.Get("amount");
				p.Erosion = node.Get("erosion");
				p.Deposition = node.Get("deposition");
				p.Capacity = node.Get("capacity");
				p.Inertia = node.Get("inertia");
				p.Evaporation = node.Get("evaporation");
				p.Lifetime = (int)node.Get("lifetime");
				p.Radius = std::clamp((int)std::lround(node.Get("radius") / cell), 1, 8);
				p.Seed = seedBase + (uint32_t)node.Get("seed") * 7919u;
				HydraulicErosion(out->V, res, p, flow->V, dep->V, wear->V, c, 0.0f, 1.0f);
				for (float& v : out->V)
					v /= toCells;
				NormalizeAccum(flow->V, 6.0f);
				NormalizeAccum(dep->V, 3.0f);
				NormalizeAccum(wear->V, 3.0f);
				return { out, flow, dep, wear };
			}
			case NodeType::Thermal:
			{
				out = copyIn(0);
				const float talus = tanf(node.Get("angle") * 0.0174533f) * cell / c.MaxHeight;   // 정규화 높이 단위
				ThermalErosion(out->V, res, talus, (int)node.Get("iterations"), node.Get("strength"), c);
				break;
			}
			// ================================================================ Gaea 식 더
			case NodeType::MountainRange:
			{
				const double s = (std::max)(100.0f, node.Get("scale"));
				const float height = node.Get("height"), uplift = node.Get("uplift"), warp = node.Get("warp"), sharp = node.Get("sharpness");
				const int oct = (int)node.Get("octaves");
				const uint32_t seed = seedBase + (uint32_t)node.Get("seed") * 7919u;
				// 옥타브는 칸 크기까지만 (그보다 잔 결은 타일 디테일이 맡는다)
				int octaves = 1;
				for (double wl = s / 1.3; octaves < oct && wl > cell * 2.0; wl *= 0.5)
					++octaves;
				FillRows(*out, c, [&](int, int, double wx, double wz) {
					const double u = wx / s, v = wz / s;
					// 산맥 축: 낮은 주파수 노이즈의 0 근처 (길게 이어진 띠) — uplift 가 클수록 넓다
					const float axisN = N::Fbm(u * 0.32 + 1.7, v * 0.32 - 4.1, seed + 3u, 4);
					const float a = std::clamp(1.0f - fabsf(axisN) / (0.08f + uplift * 0.55f), 0.0f, 1.0f);
					const float upl = a * a * (3.0f - 2.0f * a);
					const double qx = u + warp * N::Fbm(u * 0.8, v * 0.8 + 5.2, seed + 7u, 4), qz = v + warp * N::Fbm(u * 0.8 + 9.1, v * 0.8, seed + 9u, 4);
					// 안쪽 휨은 작게 (크면 잔 옥타브가 한 방향으로 늘어나 나란한 줄 (등고선 같은 띠) 이 된다)
					float sw = N::Swiss(qx * 1.3, qz * 1.3, seed + 11u, octaves, 2.0f, 0.5f, 0.1f);
					sw = powf(std::clamp((sw - 0.25f) / 0.65f, 0.0f, 1.0f), sharp);
					return height * upl * (0.18f + 0.82f * sw);
				});
				break;
			}
			case NodeType::Rugged:
			{
				MapPtr src = input(0);
				if (!src)
					break;
				const float amt = node.Get("amount") / c.MaxHeight;
				const double s = (std::max)(1.0f, node.Get("scale"));
				const int oct = (int)node.Get("octaves"), mode = (int)node.Get("mode");
				const float smin = node.Get("slopeMin");
				const uint32_t seed = seedBase + (uint32_t)node.Get("seed") * 7919u;
				const float k = c.MaxHeight / (2.0f * cell);
				FillRows(*out, c, [&](int x, int z, double wx, double wz) {
					const float h = src->At(x, z);
					const float gx = (src->At(x + 1, z) - src->At(x - 1, z)) * k, gz = (src->At(x, z + 1) - src->At(x, z - 1)) * k;
					const float deg = atanf(sqrtf(gx * gx + gz * gz)) * 57.29578f;
					const float m = std::clamp((deg - smin) / 15.0f, 0.0f, 1.0f);
					if (m <= 0.0f)
						return h;
					float n;
					if (mode == 1)
						n = 0.3f - fabsf(N::Fbm(wx / s, wz / s, seed, oct)) * 1.6f;   // Crumble: 갈라진 금
					else if (mode == 2)
					{
						float f1, f2;
						uint32_t ch;
						Voronoi(wx / s, wz / s, seed, 0.9f, f1, f2, ch);
						n = std::clamp((f2 - f1) * 4.0f, 0.0f, 1.0f) * 0.5f - 0.35f + N::Rand01(ch) * 0.4f;   // Shatter: 깨진 덩어리
					}
					else
						n = N::Ridged(wx / s, wz / s, seed, oct, 0.5f, 2.0f) - 0.4f;   // Rugged: 리지 결
					return h + amt * m * m * (3.0f - 2.0f * m) * n;
				});
				break;
			}
			case NodeType::Stratify:
			{
				MapPtr src = input(0);
				auto layers = NewMap(res);
				if (!src)
					return { out, layers };
				const float step = (std::max)(0.5f, node.Get("step")) / c.MaxHeight;
				const float strength = node.Get("strength"), sharp = node.Get("sharpness");
				const float tilt = tanf(node.Get("tilt") * 0.0174533f) / c.MaxHeight, dir = node.Get("direction") * 0.0174533f;
				const float noiseAmp = node.Get("noise") / c.MaxHeight;
				const bool slopeOnly = node.Get("slopeOnly") > 0.5f;
				const uint32_t seed = seedBase + (uint32_t)node.Get("seed") * 7919u;
				const float k = c.MaxHeight / (2.0f * cell);
				const float cd = cosf(dir), sd = sinf(dir);
				const float power = 1.0f + sharp * 5.0f;
				Jobs::ParallelFor(res, 4, [&](int b, int e) {
					for (int z = b; z < e; ++z)
						for (int x = 0; x < res; ++x)
						{
							const size_t i = (size_t)z * res + x;
							const double wx = c.X0() + x * (double)cell, wz = c.X0() + z * (double)cell;
							const float h = src->V[i];
							const float off = (float)(wx * cd + wz * sd) * tilt + noiseAmp * N::Fbm(wx / 1500.0, wz / 1500.0, seed, 4);
							const float t = (h + off) / step;
							const float kf = floorf(t), f = t - kf;
							// 단단한 층 (턱) 은 평평, 사이는 가파른 절벽
							const float strat = (kf + powf(f, power)) * step - off;
							float m = strength;
							if (slopeOnly)
							{
								const float gx = (src->At(x + 1, z) - src->At(x - 1, z)) * k, gz = (src->At(x, z + 1) - src->At(x, z - 1)) * k;
								const float deg = atanf(sqrtf(gx * gx + gz * gz)) * 57.29578f;
								m *= std::clamp((deg - 20.0f) / 15.0f, 0.0f, 1.0f);   // 가파른 곳 (절벽) 만 — 완만한 비탈에 등고선 같은 계단이 생기지 않게
							}
							out->V[i] = h + (strat - h) * m;
							layers->V[i] = N::Rand01(N::Hash((int)kf, 7, seed));
						}
				}, "TerrainGraph Stratify");
				return { out, layers };
			}
			case NodeType::Gullies:
			{
				MapPtr src = input(0);
				auto mask = NewMap(res);
				if (!src)
					return { out, mask };
				const float depthM = node.Get("depth"), depth = depthM / c.MaxHeight;
				const double s = (std::max)(cell * 2.0f, node.Get("scale"));
				const int oct = (int)node.Get("octaves");
				const float branch = node.Get("branch"), smin = node.Get("slopeMin");
				const uint32_t seed = seedBase + (uint32_t)node.Get("seed") * 7919u;
				const float k = c.MaxHeight / (4.0f * cell);   // 2 칸 간격 기울기 (m/m) — 덜 흔들린다
				Jobs::ParallelFor(res, 4, [&](int b, int e) {
					for (int z = b; z < e; ++z)
						for (int x = 0; x < res; ++x)
						{
							const size_t i = (size_t)z * res + x;
							const double wx = c.X0() + x * (double)cell, wz = c.X0() + z * (double)cell;
							const float gx = (src->At(x + 2, z) - src->At(x - 2, z)) * k, gz = (src->At(x, z + 2) - src->At(x, z - 2)) * k;
							const float deg = atanf(sqrtf(gx * gx + gz * gz)) * 57.29578f;
							const float m = std::clamp((deg - smin) / 15.0f, 0.0f, 1.0f);
							if (m <= 0.0f)
							{
								out->V[i] = src->V[i];
								continue;
							}
							const float ev = N::ErosionFilter(wx, wz, gx, gz, seed, s, oct, cell * 1.5, depthM * m, branch);
							const float shaped = ev < 0.0f ? ev : ev * 0.45f;   // 골은 깊게, 사이 등성이는 덜
							out->V[i] = src->V[i] + depth * m * shaped;
							mask->V[i] = std::clamp(-ev * m, 0.0f, 1.0f);
						}
				}, "TerrainGraph Gullies");
				return { out, mask };
			}
			case NodeType::Sediment:
			{
				MapPtr src = input(0);
				auto dep = NewMap(res);
				if (!src)
					return { out, dep };
				auto blurred = NewMap(res);
				blurred->V = src->V;
				const int r = (std::max)(1, (int)std::lround(node.Get("radius") / cell / 1.73f));
				for (int kk = 0; kk < 3; ++kk)
					BoxBlur(blurred->V, res, r);
				const float amount = node.Get("amount"), angle = node.Get("angle");
				const float k = c.MaxHeight / (2.0f * cell);
				FillRows(*out, c, [&](int x, int z, double, double) {
					const size_t i = (size_t)z * res + x;
					const float h = src->V[i];
					const float concave = (std::max)(0.0f, blurred->V[i] - h);
					const float gx = (src->At(x + 1, z) - src->At(x - 1, z)) * k, gz = (src->At(x, z + 1) - src->At(x, z - 1)) * k;
					const float deg = atanf(sqrtf(gx * gx + gz * gz)) * 57.29578f;
					const float flat = std::clamp((angle - deg) / (angle * 0.5f), 0.0f, 1.0f);
					const float fill = concave * amount * flat;
					dep->V[i] = std::clamp(fill * c.MaxHeight / 4.0f, 0.0f, 1.0f);
					return h + fill;
				});
				return { out, dep };
			}
			case NodeType::Erosion2:
			{
				MapPtr src = input(0);
				out = copyIn(0);
				auto flow = NewMap(res), wear = NewMap(res), dep = NewMap(res);
				if (!src)
					return { out, flow, wear, dep };
				const uint32_t seed = seedBase + (uint32_t)node.Get("seed") * 7919u;
				// 모양: 깎기 전에 능선을 세우고 (언샤프) 물길이 시작할 잔 결을 뿌린다
				const float shape = node.Get("shape"), sharpness = node.Get("sharpness"), detail = node.Get("detail");
				if (shape > 0.0f)
				{
					std::vector<float> b = out->V;
					const int r = (std::max)(1, (int)std::lround(2.0f + sharpness * 4.0f));
					for (int kk = 0; kk < 3; ++kk)
						BoxBlur(b, res, r);
					for (size_t i = 0; i < b.size(); ++i)
						out->V[i] += (out->V[i] - b[i]) * shape * 1.5f;
				}
				if (detail > 0.0f)
				{
					const float amp = detail * cell * 1.2f / c.MaxHeight;
					Jobs::ParallelFor(res, 8, [&](int b0, int e0) {
						for (int z = b0; z < e0; ++z)
							for (int x = 0; x < res; ++x)
							{
								const double wx = c.X0() + x * (double)cell, wz = c.X0() + z * (double)cell;
								out->V[(size_t)z * res + x] += amp * (N::Ridged(wx / (cell * 9.0), wz / (cell * 9.0), seed + 5u, 3, 0.5f, 2.0f) - 0.4f);
							}
					}, "TerrainGraph Erosion2 Detail");
				}
				ErosionParams p;
				const float duration = node.Get("duration"), down = node.Get("downcutting"), suspended = node.Get("suspended");
				p.Erosion = 0.12f + 0.5f * down;
				p.Capacity = 2.5f + 8.0f * down;
				p.Deposition = 0.35f * (1.0f - 0.85f * suspended);
				p.Inertia = 0.05f;
				p.Evaporation = 0.015f;
				p.Lifetime = 48;
				p.Radius = 2;
				const int dirMode = (int)node.Get("direction");
				static const float kDir[5][2] = { { 0, 0 }, { 0, 1 }, { 1, 0 }, { 0, -1 }, { -1, 0 } };
				p.DirX = kDir[std::clamp(dirMode, 0, 4)][0] * 0.15f;
				p.DirZ = kDir[std::clamp(dirMode, 0, 4)][1] * 0.15f;
				// 여러 해상도: Erosion Scale 의 골이 칸 24 개쯤이 되는 해상도부터 반씩 올라가며 (큰 골 → 잔 골)
				int factor = 1;
				while (factor < 16 && (res - 1) / (factor * 2) >= 64 && node.Get("scale") / (cell * factor * 2) >= 24.0f)
					factor *= 2;
				// 가장 잔 단계는 칸 20 m 이상 (12 m 칸에서 물방울이 나란한 잔 골 (커튼 무늬) 을 판다 — 그보다 잔 골은 Gullies · 타일 디테일이)
				int finest = 1;
				while (cell * finest < 20.0f && finest < factor)
					finest *= 2;
				std::vector<int> levels;
				for (int f = factor; f >= finest; f /= 2)
					levels.push_back(f);
				const float totalAmount = 0.3f + duration * 5.0f;
				for (size_t li = 0; li < levels.size(); ++li)
				{
					if (Cancelled(c))
						break;
					const int f = levels[li];
					ErosionParams lp = p;
					lp.Amount = totalAmount / levels.size();
					lp.Seed = seed + (uint32_t)li * 7919u;
					const float p0 = (float)li / levels.size(), p1 = (float)(li + 1) / levels.size();
					ErodeAtScale(out->V, res, f, cell, lp, flow->V, dep->V, wear->V, c, p0, p1);
				}
				// 흙 흘러내림: 하상 퇴적물 (Bed Load) · 굵은 퇴적물 (Coarse) 이 각자의 각도까지
				const float bed = node.Get("bedLoad"), coarse = node.Get("coarse");
				if (bed > 0.0f)
					ThermalErosion(out->V, res, tanf(node.Get("bedAngle") * 0.0174533f) * cell / c.MaxHeight, (int)(4 + bed * 16), 0.5f * bed, c);
				if (coarse > 0.0f)
					ThermalErosion(out->V, res, tanf(node.Get("coarseAngle") * 0.0174533f) * cell / c.MaxHeight, (int)(4 + coarse * 16), 0.5f * coarse, c);
				NormalizeAccum(flow->V, 6.0f);
				NormalizeAccum(dep->V, 3.0f);
				NormalizeAccum(wear->V, 3.0f);
				return { out, flow, wear, dep };
			}
			case NodeType::River:
			{
				auto mask = NewMap(res);
				out = copyIn(0);
				if (!input(0))
					return { out, mask };
				RiverParams rp;
				rp.ThresholdKm2 = node.Get("threshold");
				rp.Depth = node.Get("depth");
				rp.Width = node.Get("width");
				rp.Bank = node.Get("valley");
				rp.Count = (int)node.Get("count");
				MakeRivers(out->V, mask->V, res, c, rp, side.Rivers);
				return { out, mask };
			}
			case NodeType::Road:
			{
				auto mask = NewMap(res);
				out = copyIn(0);
				if (!input(0))
					return { out, mask };
				RoadParams rp;
				rp.Towns = (int)node.Get("towns");
				rp.Width = node.Get("width");
				rp.MaxSlope = node.Get("maxSlope");
				rp.Loops = node.Get("loops");
				rp.Seed = seedBase + (uint32_t)node.Get("seed") * 7919u;
				MakeRoads(out->V, mask->V, res, c, rp, input(1).get(), side.Roads);
				return { out, mask };
			}
			case NodeType::SatMap:
			{
				MapPtr src = input(0);
				auto col = NewMap(res);
				col->Channels = 3;
				col->V.assign((size_t)res * res * 3, 0.5f);
				if (!src)
					return { col };
				SatMapParams sp;
				sp.Palette = (int)node.Get("palette");
				sp.Bias = node.Get("bias");
				sp.Reverse = node.Get("reverse") > 0.5f;
				sp.Enhance = (int)node.Get("enhance");
				sp.Hue = node.Get("hue");
				sp.Saturation = node.Get("saturation");
				sp.Lightness = node.Get("lightness");
				ApplySatMap(src->V, res, sp, col->V);
				return { col };
			}
			case NodeType::ColorBlend:
			{
				MapPtr a = input(0), b = input(1), mask = input(2);
				auto col = NewMap(res);
				col->Channels = 3;
				col->V.assign((size_t)res * res * 3, 0.5f);
				const float ratio = node.Get("ratio");
				auto colorAt = [](const MapPtr& m, size_t i, int ch) { return m && m->Channels == 3 ? m->V[i * 3 + ch] : (m ? m->V[i] : 0.5f); };
				for (size_t i = 0; i < (size_t)res * res; ++i)
				{
					const float t = ratio * (mask ? std::clamp(mask->V[i], 0.0f, 1.0f) : 1.0f);
					for (int ch = 0; ch < 3; ++ch)
						col->V[i * 3 + ch] = colorAt(a, i, ch) + (colorAt(b, i, ch) - colorAt(a, i, ch)) * (b ? t : 0.0f);
				}
				return { col };
			}
			case NodeType::Output:
			default:
				break;
			}
			return { out };
		}
	}

	// 진행 무게 (오래 걸리는 노드)
	static float NodeWeight(NodeType t)
	{
		switch (t)
		{
		case NodeType::Erosion: return 6.0f;
		case NodeType::Erosion2: return 9.0f;
		case NodeType::Thermal: return 2.0f;
		case NodeType::River: case NodeType::Road: case NodeType::Gullies: return 2.0f;
		default: return 1.0f;
		}
	}

	EvalResult Evaluate(const Graph& g, const EvalContext& ctx)
	{
		const auto t0 = std::chrono::steady_clock::now();
		EvalResult result;
		const int outputId = g.OutputNode();
		// 계산할 노드: Output 위쪽 (KeepAll 이면 모든 노드)
		std::set<int> needed;
		{
			std::vector<int> stack;
			if (ctx.KeepAll)
				for (const Node& n : g.Nodes)
					stack.push_back(n.Id);
			else if (outputId)
				stack.push_back(outputId);
			while (!stack.empty())
			{
				const int id = stack.back();
				stack.pop_back();
				if (!needed.insert(id).second)
					continue;
				for (const Edge& e : g.Edges)
					if (e.To == id)
						stack.push_back(e.From);
			}
		}
		if (!outputId && !ctx.KeepAll)
		{
			result.Error = "no Output node";
			return result;
		}
		// 차례 (위상 정렬) + 출력을 읽는 수 (다 읽으면 놓는다 — 빌드 메모리)
		std::vector<int> order;
		std::map<int, int> consumers;
		{
			std::set<int> done;
			bool progressMade = true;
			while (order.size() < needed.size() && progressMade)
			{
				progressMade = false;
				for (int id : needed)
				{
					if (done.count(id))
						continue;
					bool ready = true;
					for (const Edge& e : g.Edges)
						if (e.To == id && needed.count(e.From) && !done.count(e.From))
							ready = false;
					if (ready)
					{
						order.push_back(id);
						done.insert(id);
						progressMade = true;
					}
				}
			}
			for (const Edge& e : g.Edges)
				if (needed.count(e.To))
					++consumers[e.From];
		}
		// 진행: 침식 노드는 무게 6, 나머지 1
		float totalWeight = 0.0f;
		for (int id : order)
			if (const Node* n = g.Find(id))
				totalWeight += NodeWeight(n->Type);
		float doneWeight = 0.0f;
		std::map<std::pair<int, int>, MapPtr> outputs;
		for (int id : order)
		{
			if (Cancelled(ctx))
			{
				result.Cancelled = true;
				return result;
			}
			const Node* n = g.Find(id);
			const NodeDesc* d = n ? FindDesc(n->Type) : nullptr;
			if (!n || !d)
				continue;
			const float w = NodeWeight(n->Type);
			std::vector<MapPtr> in(d->Inputs.size());
			for (size_t i = 0; i < d->Inputs.size(); ++i)
				if (const Edge* e = g.InputOf(id, (int)i))
				{
					auto it = outputs.find({ e->From, e->FromPin });
					if (it != outputs.end())
						in[i] = it->second;
				}
			if (n->Type == NodeType::Output)
			{
				result.Height = in.size() > 0 ? in[0] : nullptr;
				result.Flow = in.size() > 1 ? in[1] : nullptr;
				result.Deposits = in.size() > 2 ? in[2] : nullptr;
				result.Wear = in.size() > 3 ? in[3] : nullptr;
				result.Color = in.size() > 4 && in[4] && in[4]->Channels == 3 ? in[4] : nullptr;
				continue;
			}
			const auto n0 = std::chrono::steady_clock::now();
			std::vector<MapW> outs;
			if (!n->Enabled)
			{
				// 꺼진 노드: 첫 출력 = 첫 입력 그대로 (없으면 0), 나머지 출력 (Flow · Deposits 같은 마스크) 은 0
				auto pass = NewMap(ctx.Res);
				if (!in.empty() && in[0])
					pass->V = in[0]->V;
				outs.assign((std::max)((size_t)1, d->Outputs.size()), NewMap(ctx.Res));
				outs[0] = pass;
			}
			else
			{
				EvalContext sub = ctx;
				const float p0 = doneWeight / (std::max)(totalWeight, 1.0f), p1 = (doneWeight + w) / (std::max)(totalWeight, 1.0f);
				if (ctx.Progress)
					sub.Progress = [&, p0, p1](float t, const char* stage) { ctx.Progress(p0 + (p1 - p0) * t, stage); };
				outs = EvalNode(*n, in, sub, result);
			}
			result.NodeMs[id] = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - n0).count();
			for (size_t pin = 0; pin < outs.size(); ++pin)
				outputs[{ id, (int)pin }] = outs[pin];
			doneWeight += w;
			if (ctx.Progress)
				ctx.Progress(doneWeight / (std::max)(totalWeight, 1.0f), d->Label);
			// 다 읽힌 출력은 놓는다 (KeepAll 이 아니면)
			if (!ctx.KeepAll)
				for (size_t i = 0; i < d->Inputs.size(); ++i)
					if (const Edge* e = g.InputOf(id, (int)i))
						if (--consumers[e->From] <= 0)
							for (auto it = outputs.begin(); it != outputs.end();)
							{
								// Output 노드가 읽을 것은 남긴다
								bool keep = false;
								for (const Edge& oe : g.Edges)
									if (oe.To == outputId && oe.From == it->first.first && oe.FromPin == it->first.second)
										keep = true;
								if (it->first.first == e->From && !keep)
									it = outputs.erase(it);
								else
									++it;
							}
		}
		if (ctx.KeepAll)
			result.Outputs = outputs;
		if (outputId && !result.Height)
			result.Error = "Output has no Height input";
		result.Ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		return result;
	}
}
