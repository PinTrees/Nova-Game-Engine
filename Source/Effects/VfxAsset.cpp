#include "pch.h"
#include "VfxAsset.h"
#include "VfxMeshes.h"
#include <set>
#include <filesystem>
#include <functional>
#include <map>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace fs = std::filesystem;

namespace Vfx
{
	namespace
	{
		using K = ParamKind;
		using S = ParamSpace;

		std::string Simplify(const std::string& s)
		{
			std::string o;
			for (char c : s)
				if (isalnum((unsigned char)c))
					o += (char)tolower((unsigned char)c);
			return o;
		}

		// 곡선 · 그라디언트 기본값은 Default 로 나타낼 수 없다 → 아래 DefaultCurve / DefaultGradient
		std::vector<BlockDesc> MakeBlocks()
		{
			const char* shapes = "Point|Sphere|Circle|Box|Cone|Line|Torus";
			return {
				// ---------------------------------------------------------------- Initialize Particle
				{ "SetPosition", "Set Position (Shape)", "Position", Context::Initialize, 1, {
					{ "Shape", K::Enum, { 1 }, 0, 0, shapes, "태어나는 모양" },
					{ "Radius", K::Float, { 0.5f }, 0, 20, nullptr, "구 · 원 · 원뿔 바닥 · 토러스 큰 반지름" },
					{ "Size", K::Vector3, { 1, 1, 1 }, 0, 50, nullptr, "상자 크기" },
					{ "Center", K::Vector3, { 0, 0, 0 }, -50, 50, nullptr, "가운데 (Visual Effect 기준)" },
					{ "Surface", K::Bool, { 0 }, 0, 0, nullptr, "표면에서만 (끄면 부피 전체)" },
					{ "Arc", K::Float, { 1 }, 0, 1, nullptr, "원 · 원뿔 · 토러스의 호 (1 = 한 바퀴)" },
					{ "Height", K::Float, { 1 }, 0, 50, nullptr, "원뿔 · 선의 높이" },
					{ "ConeAngle", K::Float, { 25 }, 0, 89, nullptr, "원뿔이 벌어지는 각 (도)" },
					{ "Thickness", K::Float, { 0.1f }, 0, 10, nullptr, "토러스 단면 반지름" },
					{ "Plane", K::Enum, { 0 }, 0, 0, "XZ (Floor)|XY (Facing Z)|YZ (Facing X)", "원 · 원뿔 · 토러스 · 선이 놓이는 평면 (XY = 세운 고리, 포털)" },
				}, "모양 안 (또는 표면) 의 무작위 자리에서 태어난다. Velocity 의 From Shape 는 이 모양의 바깥 방향" },
				{ "SetPositionSpiral", "Set Position (Spiral Arms)", "Position", Context::Initialize, 8, {
					{ "Arms", K::Int, { 3 }, 1, 12 },
					{ "Radius", K::Float, { 6 }, 0, 100 },
					{ "Twist", K::Float, { 0.8f }, -5, 5, nullptr, "가운데에서 끝까지 감기는 바퀴 수" },
					{ "Spread", K::Float, { 0.5f }, 0, 3, nullptr, "팔 둘레로 흩어지는 각 (라디안)" },
					{ "Thickness", K::Float, { 0.4f }, 0, 20, nullptr, "위아래 두께 (가운데가 가장 두껍다)" },
					{ "Center", K::Vector3, { 0, 0, 0 }, -50, 50 },
				}, "나선 팔 (은하 · 소용돌이). Velocity 의 From Shape 는 도는 방향" },
				{ "SetVelocity", "Set Velocity", "Velocity", Context::Initialize, 2, {
					{ "Mode", K::Enum, { 0 }, 0, 0, "Direction|Random|From Shape|Cone", "방향: 정한 방향 · 모든 방향 · 모양 바깥 · 방향 둘레 원뿔" },
					{ "MinSpeed", K::Float, { 1 }, 0, 100 },
					{ "MaxSpeed", K::Float, { 2 }, 0, 100 },
					{ "Spread", K::Float, { 20 }, 0, 180, nullptr, "Cone: 벌어지는 각 (도)" },
					{ "Direction", K::Vector3, { 0, 1, 0 }, -1, 1, nullptr, "Direction · Cone 의 방향 (Visual Effect 기준)" },
				}, "처음 속도" },
				{ "SetLifetime", "Set Lifetime (Random)", "Attribute", Context::Initialize, 3, {
					{ "Min", K::Float, { 1 }, 0, 60 },
					{ "Max", K::Float, { 2 }, 0, 60 },
				}, "수명 (초) — 둘 사이 무작위" },
				{ "SetSize", "Set Size (Random)", "Attribute", Context::Initialize, 4, {
					{ "Min", K::Float, { 0.1f }, 0, 20 },
					{ "Max", K::Float, { 0.2f }, 0, 20 },
				}, "크기 (미터) — 둘 사이 무작위" },
				{ "SetColor", "Set Color", "Attribute", Context::Initialize, 5, {
					{ "Mode", K::Enum, { 0 }, 0, 0, "Constant|Random Between|Rainbow", "고정 · A 와 B 사이 · 무지개 (채도 · 밝기)" },
					{ "ColorA", K::Color, { 1, 0.6f, 0.2f, 1 } },
					{ "ColorB", K::Color, { 1, 0.2f, 0.6f, 1 } },
					{ "Saturation", K::Float, { 1 }, 0, 1, nullptr, "Rainbow 채도" },
					{ "Brightness", K::Float, { 1 }, 0, 4, nullptr, "Rainbow 밝기" },
					{ "Intensity", K::Float, { 1 }, 0, 50, nullptr, "HDR 배율 (1 보다 크면 Bloom)" },
				}, "색 (HDR). Update 에 두면 프레임마다 (연산 노드를 이어 반짝임 · 색 바꾸기 — 뒤의 Color over Life 가 곱한다)", true },
				{ "SetAngle", "Set Angle · Angular Velocity", "Attribute", Context::Initialize, 6, {
					{ "AngleMin", K::Float, { 0 }, -360, 360 },
					{ "AngleMax", K::Float, { 360 }, -360, 360 },
					{ "SpinMin", K::Float, { 0 }, -1440, 1440, nullptr, "초당 도" },
					{ "SpinMax", K::Float, { 0 }, -1440, 1440 },
				}, "화면에서 돌아간 각과 도는 빠르기 (도)" },
				{ "InheritSource", "Inherit Source (GPU Event)", "GPU Event", Context::Initialize, 7, {
					{ "Velocity", K::Float, { 0.3f }, 0, 2, nullptr, "부모 파티클 속도의 배율" },
					{ "Color", K::Bool, { 1 }, 0, 0, nullptr, "부모 색을 이어받는다" },
				}, "GPU Event 로 태어날 때 부모 파티클의 속도 · 색" },
				{ "SetAttribute", "Set Attribute (Custom)", "Attribute", Context::Initialize, 9, {
					{ "Attribute", K::Text, {}, 0, 0, nullptr, "사용자 속성 이름 (Blackboard 의 Custom Attributes)" },
					{ "Mode", K::Enum, { 0 }, 0, 0, "Set|Add|Multiply", "바꾸기 · 더하기 · 곱하기" },
					{ "Value", K::Float, { 0 }, -100, 100, nullptr, "Float 속성의 값" },
					{ "VectorValue", K::Vector3, { 0, 0, 0 }, -100, 100, nullptr, "Vector3 속성의 값" },
				}, "사용자 속성에 값을 쓴다 (Initialize · Update). 연산 노드 Get Attribute 로 읽는다", true },
				// ---------------------------------------------------------------- Update Particle
				{ "Gravity", "Gravity", "Force", Context::Update, 20, {
					{ "Force", K::Vector3, { 0, -9.81f, 0 }, -50, 50, nullptr, "가속도 (m/s², 월드 — Visual Effect 가 돌아도 아래로)" },
				}, "일정한 가속도" },
				{ "Drag", "Linear Drag", "Force", Context::Update, 21, {
					{ "Coefficient", K::Float, { 1 }, 0, 20 },
				}, "속도를 줄인다 (초당 비율)" },
				{ "Turbulence", "Turbulence", "Force", Context::Update, 22, {
					{ "Intensity", K::Float, { 3 }, 0, 50 },
					{ "Frequency", K::Float, { 0.6f }, 0, 10 },
					{ "Octaves", K::Int, { 2 }, 1, 4 },
					{ "Drag", K::Float, { 0 }, 0, 20, nullptr, "0 = 힘으로 더하기, 크면 잡음 속도로 끌려간다" },
					{ "Scroll", K::Vector3, { 0, 0.5f, 0 }, -10, 10, nullptr, "잡음이 흐르는 빠르기" },
				}, "3D 잡음 힘 (연기 · 마법 가루)" },
				{ "Vortex", "Vortex", "Force", Context::Update, 23, {
					{ "Axis", K::Vector3, { 0, 1, 0 }, -1, 1, nullptr, "도는 축", S::Direction },
					{ "Speed", K::Float, { 6 }, -100, 100, nullptr, "도는 힘" },
					{ "Center", K::Vector3, { 0, 0, 0 }, -50, 50, nullptr, nullptr, S::Position },
					{ "Pull", K::Float, { 2 }, -100, 100, nullptr, "안으로 당기는 힘 (음수 = 밀기)" },
				}, "축 둘레로 돌린다 (토네이도 · 소용돌이)" },
				{ "Attractor", "Conform to Sphere (Attractor)", "Force", Context::Update, 24, {
					{ "Position", K::Vector3, { 0, 1, 0 }, -50, 50, nullptr, nullptr, S::Position },
					{ "Strength", K::Float, { 5 }, -100, 100 },
					{ "Radius", K::Float, { 0 }, 0, 50, nullptr, "0 = 한 점으로, 크면 그 반지름의 구 표면으로" },
					{ "Drag", K::Float, { 0.5f }, 0, 20 },
				}, "한 점 (또는 구 표면) 으로 끌어당긴다" },
				{ "CollidePlane", "Collide with Plane", "Collision", Context::Update, 25, {
					{ "Normal", K::Vector3, { 0, 1, 0 }, -1, 1, nullptr, "평면 법선", S::Direction },
					{ "Offset", K::Float, { 0 }, -50, 50, nullptr, "평면 높이 (Visual Effect 기준)" },
					{ "Bounce", K::Float, { 0.4f }, 0, 1 },
					{ "Friction", K::Float, { 0.2f }, 0, 1 },
					{ "LifetimeLoss", K::Float, { 0 }, 0, 1, nullptr, "부딪힐 때마다 줄어드는 수명 비율" },
				}, "평면에 튕긴다" },
				{ "ColorOverLife", "Color over Life", "Attribute", Context::Update, 26, {
					{ "Mode", K::Enum, { 0 }, 0, 0, "Multiply|Replace", "처음 색에 곱하기 · 바꾸기" },
					{ "Gradient", K::Gradient, {} },
				}, "나이에 따라 색 · 투명도" },
				{ "SizeOverLife", "Size over Life", "Attribute", Context::Update, 27, {
					{ "Curve", K::Curve, {} },
				}, "나이에 따라 크기 (처음 크기의 배율)" },
				{ "SpeedLimit", "Speed Limit", "Velocity", Context::Update, 28, {
					{ "Max", K::Float, { 10 }, 0, 200 },
				}, "속력의 위 한계" },
				{ "Orbit", "Orbit", "Position", Context::Update, 29, {
					{ "Center", K::Vector3, { 0, 0, 0 }, -50, 50, nullptr, nullptr, S::Position },
					{ "Speed", K::Float, { 90 }, -1440, 1440, nullptr, "초당 도" },
					{ "Axis", K::Vector3, { 0, 1, 0 }, -1, 1, nullptr, "도는 축", S::Direction },
					{ "Falloff", K::Float, { 0 }, 0, 2, nullptr, "0 = 모두 같은 빠르기, 1 = 거리에 반비례 (은하의 차등 회전)" },
				}, "축 둘레로 위치를 돌린다 (마법진 · 토네이도 · 은하)" },
				{ "CollideDepth", "Collide with Depth Buffer", "Collision", Context::Update, 30, {
					{ "Bounce", K::Float, { 0.3f }, 0, 1 },
					{ "Friction", K::Float, { 0.3f }, 0, 1 },
					{ "LifetimeLoss", K::Float, { 0 }, 0, 1, nullptr, "부딪힐 때마다 줄어드는 수명 비율 (1 = 바로 죽음)" },
					{ "Thickness", K::Float, { 1 }, 0.01f, 20, nullptr, "보이는 표면 뒤로 이 두께까지를 물체 속으로 본다 (m)" },
				}, "화면에 보이는 장면 (프레임의 첫 뷰 깊이) 에 튕긴다 — 바닥 · 벽 · 물체 모양 그대로. 화면 밖 · 가려진 곳은 지나간다" },
				{ "CollideCover", "Collide with Weather Cover", "Collision", Context::Update, 32, {
					{ "Bounce", K::Float, { 0 }, 0, 1 },
					{ "Friction", K::Float, { 1 }, 0, 1 },
					{ "LifetimeLoss", K::Float, { 1 }, 0, 1, nullptr, "부딪힐 때 줄어드는 수명 비율 (1 = 바로 죽음 — 빗방울)" },
					{ "Thickness", K::Float, { 1.5f }, 0.01f, 20, nullptr, "맨 위 표면 아래 이 두께까지를 물체 속으로 본다 (m)" },
				}, "날씨 (com.nova.weather) 의 덮개 맵 — 위에서 본 맨 위 표면 (지붕 · 나무 · 땅) 에 부딪힌다. 화면 밖 · 가려진 곳도, 집 안에 비가 들지 않는다. 날씨가 없으면 아무것도 하지 않는다" },
				{ "CollideSDF", "Collide with Signed Distance Field", "Collision", Context::Update, 31, {
					{ "Mesh", K::Text, {}, 0, 0, nullptr, "거리장을 구울 메시: Cube · Sphere · Cylinder · Cone · Crystal 또는 모델 파일 (Assets/…/x.fbx, #n = n 번째 메시)" },
					{ "Position", K::Vector3, { 0, 0, 0 }, -50, 50, nullptr, "거리장의 자리 (Visual Effect 기준)", S::None, true },
					{ "Rotation", K::Vector3, { 0, 0, 0 }, -360, 360, nullptr, "회전 (도)", S::None, true },
					{ "Scale", K::Vector3, { 1, 1, 1 }, 0.01f, 50, nullptr, "크기 (메시 단위의 배율)", S::None, true },
					{ "Resolution", K::Int, { 48 }, 16, 96, nullptr, "가장 긴 변의 칸 수 (클수록 정확, 굽기 · 메모리가 늘어난다)", S::None, true },
					{ "Radius", K::Float, { 0.05f }, 0, 5, nullptr, "파티클 반지름 (표면에서 이만큼 띄운다)" },
					{ "Bounce", K::Float, { 0.3f }, 0, 1 },
					{ "Friction", K::Float, { 0.3f }, 0, 1 },
					{ "LifetimeLoss", K::Float, { 0 }, 0, 1, nullptr, "부딪힐 때마다 줄어드는 수명 비율" },
				}, "메시를 거리장으로 구워 그 모양에 튕긴다 — 화면에 보이지 않아도 (Collide with Depth Buffer 와 달리). 시스템마다 거리장 하나" },
				// ---------------------------------------------------------------- 둘 다: Block Sub Graph (값은 그 파일의 속성 — EffectiveBlockDesc)
				{ "SubgraphBlock", "Sub Graph Block", "Sub Graph", Context::Initialize, 0, {
					{ "Path", K::Text, {}, 0, 0, nullptr, "Block Sub Graph 파일 (.vfxblock)" },
				}, "다른 파일 (.vfxblock) 의 블록 묶음을 블록 하나로 — 그 파일의 Blackboard 속성이 이 블록의 값, 놓인 문맥 (Initialize · Update) 의 블록들이 펼쳐진다", true },
			};
		}

		std::vector<CurveKey> DefaultCurve() { return { { 0.0f, 0.0f }, { 0.15f, 1.0f }, { 1.0f, 0.0f } }; }
		std::vector<GradientKey> DefaultGradient()
		{
			return { { 0.0f, { 1, 1, 1, 0 } }, { 0.1f, { 1, 1, 1, 1 } }, { 0.7f, { 1, 1, 1, 1 } }, { 1.0f, { 1, 1, 1, 0 } } };
		}

		float SampleCurve(const std::vector<CurveKey>& k, float t)
		{
			if (k.empty()) return 1.0f;
			if (t <= k.front().T) return k.front().V;
			for (size_t i = 1; i < k.size(); ++i)
				if (t <= k[i].T)
				{
					const float span = (std::max)(k[i].T - k[i - 1].T, 1e-6f);
					return k[i - 1].V + (k[i].V - k[i - 1].V) * (t - k[i - 1].T) / span;
				}
			return k.back().V;
		}

		std::array<float, 4> SampleGradient(const std::vector<GradientKey>& k, float t)
		{
			if (k.empty()) return { 1, 1, 1, 1 };
			auto out = [](const GradientKey& g) { return std::array<float, 4>{ g.C[0], g.C[1], g.C[2], g.C[3] }; };
			if (t <= k.front().T) return out(k.front());
			for (size_t i = 1; i < k.size(); ++i)
				if (t <= k[i].T)
				{
					const float f = (t - k[i - 1].T) / (std::max)(k[i].T - k[i - 1].T, 1e-6f);
					std::array<float, 4> r;
					for (int c = 0; c < 4; ++c) r[c] = k[i - 1].C[c] + (k[i].C[c] - k[i - 1].C[c]) * f;
					return r;
				}
			return out(k.back());
		}

		std::string Lower(std::string s)
		{
			for (char& c : s) c = (char)tolower((unsigned char)c);
			return s;
		}

		std::string Key(const std::string& asset)
		{
			std::string s = Lower(asset);
			for (char& c : s) if (c == '/') c = '\\';
			return s;
		}

		struct CacheEntry
		{
			std::shared_ptr<const Asset> Data, Live;
			uint64_t Revision = 0;
			fs::file_time_type Time{};
			std::chrono::steady_clock::time_point Checked{};
			std::string Error;
		};
		std::unordered_map<std::string, CacheEntry> s_Cache;
		uint64_t s_Revision = 0;

		json Vec(const std::array<float, 4>& v, int n)
		{
			json a = json::array();
			for (int i = 0; i < n; ++i) a.push_back(v[i]);
			return a;
		}
	}

	const std::vector<std::string>& MeshNames()
	{
		static const std::vector<std::string> s = { "Cube", "Sphere", "Cylinder", "Cone", "Crystal" };
		return s;
	}

	const std::vector<BlockDesc>& Blocks()
	{
		static const std::vector<BlockDesc> s = MakeBlocks();
		return s;
	}

	const BlockDesc* FindBlock(const std::string& type)
	{
		const std::string t = Simplify(type);
		for (const BlockDesc& d : Blocks())
			if (Simplify(d.Type) == t || Simplify(d.Label) == t)
				return &d;
		return nullptr;
	}

	const ParamDesc* FindParam(const BlockDesc& d, const std::string& name)
	{
		const std::string n = Simplify(name);
		for (const ParamDesc& p : d.Params)
			if (Simplify(p.Name) == n)
				return &p;
		return nullptr;
	}

	std::shared_ptr<const BlockDesc> EffectiveBlockDesc(const Block& b)
	{
		const BlockDesc* d = FindBlock(b.Type);
		if (!d)
			return nullptr;
		if (std::string(d->Type) != "SubgraphBlock")
			return std::shared_ptr<const BlockDesc>(d, [](const BlockDesc*) {});   // 정의표 그대로 (지우지 않는다)
		// Sub Graph Block: Path + 파일의 속성 (파일의 판이 바뀌면 다시)
		std::string path;
		for (auto it = b.Params.begin(); it != b.Params.end(); ++it)
			if (Simplify(it.key()) == "path" && it->is_string()) path = it->get<std::string>();
		const Loaded l = path.empty() ? Loaded{} : Load(path);
		static std::map<std::string, std::pair<uint64_t, std::shared_ptr<BlockDesc>>> s_Cache;
		static std::set<std::string> s_Names;   // ParamDesc.Name 이 가리키는 글자 (오래 산다)
		auto& slot = s_Cache[path];
		if (slot.second && slot.first == l.Revision)
			return slot.second;
		auto desc = std::make_shared<BlockDesc>(*d);
		if (l.Data)
			for (const Property& p : l.Data->Properties)
			{
				const char* name = s_Names.insert(p.Name).first->c_str();
				ParamDesc pd{ name, K::Float, p.Value, p.Min, p.Max, nullptr, "Sub Graph Block 입력 (파일의 Blackboard 속성)" };
				pd.Kind = p.Type == PropertyType::Vector3 ? K::Vector3 : p.Type == PropertyType::Color ? K::Color : p.Type == PropertyType::Int ? K::Int : p.Type == PropertyType::Bool ? K::Bool : K::Float;
				pd.NoLink = true;   // 연산 노드는 이을 수 없다 (속성 연결 🔗 은 된다)
				desc->Params.push_back(pd);
			}
		slot = { l.Revision, desc };
		return desc;
	}

	std::vector<std::string> EnumOptions(const ParamDesc& p)
	{
		std::vector<std::string> o;
		if (!p.Options) return o;
		std::stringstream ss(p.Options);
		std::string item;
		while (std::getline(ss, item, '|'))
			o.push_back(item);
		return o;
	}

	std::array<float, 4> ValueFromJson(const json& v, std::array<float, 4> fallback)
	{
		if (v.is_number())
			fallback[0] = v.get<float>();
		else if (v.is_boolean())
			fallback[0] = v.get<bool>() ? 1.0f : 0.0f;
		else if (v.is_array())
		{
			for (size_t i = 0; i < 4 && i < v.size(); ++i)
				if (v[i].is_number())
					fallback[i] = v[i].get<float>();
		}
		else if (v.is_object())
		{
			const char* keys[2][4] = { { "x", "y", "z", "w" }, { "r", "g", "b", "a" } };
			for (auto& set : keys)
				for (int i = 0; i < 4; ++i)
					if (v.contains(set[i]) && v[set[i]].is_number())
						fallback[i] = v[set[i]].get<float>();
		}
		return fallback;
	}

	// ---------------------------------------------------------------- 블록 값
	namespace
	{
		const json* ParamJson(const Block& b, const char* name)
		{
			const std::string n = Simplify(name);
			for (auto it = b.Params.begin(); it != b.Params.end(); ++it)
				if (Simplify(it.key()) == n)
					return &*it;
			return nullptr;
		}
	}

	float Block::GetFloat(const BlockDesc& d, const char* name) const
	{
		return GetVector(d, name)[0];
	}

	std::array<float, 4> Block::GetVector(const BlockDesc& d, const char* name) const
	{
		const ParamDesc* p = FindParam(d, name);
		std::array<float, 4> v = p ? p->Default : std::array<float, 4>{};
		if (p && p->Kind == ParamKind::Enum)
		{
			// Enum 은 번호 또는 이름
			if (const json* j = ParamJson(*this, name); j && j->is_string())
			{
				const auto opts = EnumOptions(*p);
				for (size_t i = 0; i < opts.size(); ++i)
					if (Simplify(opts[i]) == Simplify(j->get<std::string>()))
						v[0] = (float)i;
				return v;
			}
		}
		if (const json* j = ParamJson(*this, name))
			v = ValueFromJson(*j, v);
		return v;
	}

	std::vector<CurveKey> Block::GetCurve(const BlockDesc&, const char* name) const
	{
		const json* j = ParamJson(*this, name);
		if (!j || !j->is_array() || j->empty())
			return DefaultCurve();
		std::vector<CurveKey> k;
		for (const json& e : *j)
			if (e.is_array() && e.size() >= 2)
				k.push_back({ e[0].get<float>(), e[1].get<float>() });
		std::sort(k.begin(), k.end(), [](const CurveKey& a, const CurveKey& b) { return a.T < b.T; });
		return k.empty() ? DefaultCurve() : k;
	}

	std::vector<GradientKey> Block::GetGradient(const BlockDesc&, const char* name) const
	{
		const json* j = ParamJson(*this, name);
		if (!j || !j->is_array() || j->empty())
			return DefaultGradient();
		std::vector<GradientKey> k;
		for (const json& e : *j)
			if (e.is_array() && e.size() >= 5)
				k.push_back({ e[0].get<float>(), { e[1].get<float>(), e[2].get<float>(), e[3].get<float>(), e[4].get<float>() } });
		std::sort(k.begin(), k.end(), [](const GradientKey& a, const GradientKey& b) { return a.T < b.T; });
		return k.empty() ? DefaultGradient() : k;
	}

	// ---------------------------------------------------------------- JSON
	json BlockToJson(const Block& b)
	{
		json j = { { "type", b.Type } };
		if (!b.Enabled) j["enabled"] = false;
		if (!b.Params.empty()) j["params"] = b.Params;
		if (!b.Bind.empty()) j["bind"] = b.Bind;
		if (!b.Links.empty()) j["links"] = b.Links;
		return j;
	}

	Block BlockFromJson(const json& j)
	{
		Block b;
		b.Type = j.value("type", std::string());
		if (const BlockDesc* d = FindBlock(b.Type))
			b.Type = d->Type;   // 화면 이름 · 대소문자로 써도 저장 이름으로
		b.Enabled = j.value("enabled", true);
		if (j.contains("params") && j["params"].is_object()) b.Params = j["params"];
		if (j.contains("bind") && j["bind"].is_object()) b.Bind = j["bind"];
		if (j.contains("links") && j["links"].is_object()) b.Links = j["links"];
		return b;
	}

	const char* PropertyTypeName(PropertyType t)
	{
		switch (t)
		{
		case PropertyType::Int: return "Int";
		case PropertyType::Bool: return "Bool";
		case PropertyType::Vector3: return "Vector3";
		case PropertyType::Color: return "Color";
		default: return "Float";
		}
	}

	bool PropertyTypeFromName(const std::string& s, PropertyType& t)
	{
		const std::string n = Simplify(s);
		if (n == "float") t = PropertyType::Float;
		else if (n == "int" || n == "uint") t = PropertyType::Int;
		else if (n == "bool") t = PropertyType::Bool;
		else if (n == "vector3" || n == "vec3" || n == "position" || n == "direction") t = PropertyType::Vector3;
		else if (n == "color") t = PropertyType::Color;
		else return false;
		return true;
	}

	json PropertyToJson(const Property& p)
	{
		json j = { { "name", p.Name }, { "type", PropertyTypeName(p.Type) } };
		switch (p.Type)
		{
		case PropertyType::Vector3: j["value"] = Vec(p.Value, 3); break;
		case PropertyType::Color: j["value"] = Vec(p.Value, 4); break;
		case PropertyType::Bool: j["value"] = p.Value[0] > 0.5f; break;
		case PropertyType::Int: j["value"] = (int)p.Value[0]; break;
		default: j["value"] = p.Value[0]; break;
		}
		if (p.Min != 0.0f || p.Max != 0.0f) j["range"] = { p.Min, p.Max };
		if (!p.Tooltip.empty()) j["tooltip"] = p.Tooltip;
		return j;
	}

	Property PropertyFromJson(const json& j)
	{
		Property p;
		p.Name = j.value("name", std::string("Property"));
		PropertyTypeFromName(j.value("type", std::string("Float")), p.Type);
		if (p.Type == PropertyType::Color) p.Value = { 1, 1, 1, 1 };
		if (j.contains("value")) p.Value = ValueFromJson(j["value"], p.Value);
		if (j.contains("range") && j["range"].is_array() && j["range"].size() == 2)
		{
			p.Min = j["range"][0].get<float>();
			p.Max = j["range"][1].get<float>();
		}
		p.Tooltip = j.value("tooltip", std::string());
		return p;
	}

	namespace
	{
		const char* kBlendNames[] = { "Additive", "Alpha", "Opaque" };
		const char* kShapeNames[] = { "SoftDot", "Glow", "Star", "Sparkle", "Ring", "Spark", "Smoke", "Square", "Texture", "Heart", "Mesh" };
		const char* kOrientNames[] = { "FaceCamera", "AlongVelocity", "Horizontal" };
		const char* kSortNames[] = { "Auto", "On", "Off" };
		const char* kTriggerNames[] = { "OnDie", "Rate" };

		template<size_t N>
		int NameIndex(const json& j, const char* key, const char* (&names)[N], int fallback)
		{
			if (!j.contains(key)) return fallback;
			const json& v = j[key];
			if (v.is_number_integer()) return std::clamp(v.get<int>(), 0, (int)N - 1);
			if (v.is_string())
				for (size_t i = 0; i < N; ++i)
					if (Simplify(names[i]) == Simplify(v.get<std::string>()))
						return (int)i;
			return fallback;
		}
	}

	json SystemToJson(const System& s)
	{
		json spawn = { { "rate", s.SpawnCtx.Rate }, { "loop", s.SpawnCtx.Loop }, { "duration", s.SpawnCtx.Duration }, { "delay", s.SpawnCtx.Delay },
			{ "startEvent", s.SpawnCtx.StartEvent }, { "stopEvent", s.SpawnCtx.StopEvent } };
		json bursts = json::array();
		for (const Burst& b : s.SpawnCtx.Bursts)
			bursts.push_back({ { "time", b.Time }, { "count", b.Count }, { "cycles", b.Cycles }, { "interval", b.Interval } });
		spawn["bursts"] = bursts;
		if (!s.SpawnCtx.RateBind.empty())
			spawn["rateBind"] = s.SpawnCtx.RateBind;
		if (!s.SpawnCtx.Parent.empty())
		{
			spawn["parent"] = s.SpawnCtx.Parent;
			spawn["countPerEvent"] = s.SpawnCtx.CountPerEvent;
			spawn["trigger"] = kTriggerNames[(int)s.SpawnCtx.Trigger];
			if (s.SpawnCtx.Trigger == EventTrigger::Rate)
				spawn["eventRate"] = s.SpawnCtx.EventRate;
		}
		json init = json::array(), update = json::array();
		for (const Block& b : s.Initialize) init.push_back(BlockToJson(b));
		for (const Block& b : s.Update) update.push_back(BlockToJson(b));
		const Output& o = s.OutputCtx;
		json output = { { "blend", kBlendNames[(int)o.BlendMode] }, { "shape", kShapeNames[(int)o.Look] }, { "orient", kOrientNames[(int)o.Orientation] },
			{ "stretch", o.Stretch }, { "softDistance", o.SoftDistance }, { "intensity", o.Intensity } };
		if (!o.Texture.empty()) output["texture"] = o.Texture;
		if (o.FlipbookColumns * o.FlipbookRows > 1)
			output["flipbook"] = { { "columns", o.FlipbookColumns }, { "rows", o.FlipbookRows }, { "fps", o.FlipbookFps } };
		if (o.Sort != SortMode::Auto) output["sort"] = kSortNames[(int)o.Sort];
		if (o.Trail)
			output["trail"] = { { "points", o.TrailPoints }, { "length", o.TrailLength }, { "width", o.TrailWidth }, { "only", o.TrailOnly } };
		if (o.Look == Shape::Mesh)
		{
			output["mesh"] = o.Mesh;
			output["lit"] = o.Lit;
		}
		json j = { { "name", s.Name }, { "capacity", s.Capacity }, { "space", s.Local ? "Local" : "World" },
			{ "spawn", spawn }, { "initialize", init }, { "update", update }, { "output", output } };
		if (!s.Enabled) j["enabled"] = false;
		if (!s.Editor.empty()) j["editor"] = s.Editor;
		return j;
	}

	System SystemFromJson(const json& j)
	{
		System s;
		s.Name = j.value("name", std::string("System"));
		s.Enabled = j.value("enabled", true);
		s.Capacity = std::clamp(j.value("capacity", 4096), 1, 4 * 1024 * 1024);
		s.Local = Simplify(j.value("space", std::string("World"))) == "local";
		if (j.contains("spawn") && j["spawn"].is_object())
		{
			const json& sp = j["spawn"];
			Spawn& d = s.SpawnCtx;
			d.Rate = sp.value("rate", d.Rate);
			d.Loop = sp.value("loop", d.Loop);
			d.Duration = sp.value("duration", d.Duration);
			d.Delay = sp.value("delay", d.Delay);
			d.StartEvent = sp.value("startEvent", d.StartEvent);
			d.StopEvent = sp.value("stopEvent", d.StopEvent);
			d.Parent = sp.value("parent", std::string());
			d.RateBind = sp.value("rateBind", std::string());
			d.CountPerEvent = std::clamp(sp.value("countPerEvent", d.CountPerEvent), 1, 4096);
			d.Trigger = (EventTrigger)NameIndex(sp, "trigger", kTriggerNames, 0);
			d.EventRate = (std::max)(0.0f, sp.value("eventRate", d.EventRate));
			if (sp.contains("bursts") && sp["bursts"].is_array())
				for (const json& b : sp["bursts"])
					d.Bursts.push_back({ b.value("time", 0.0f), (std::max)(0, b.value("count", 100)), (std::max)(0, b.value("cycles", 1)), (std::max)(0.01f, b.value("interval", 1.0f)) });
		}
		auto blocks = [&](const char* key, std::vector<Block>& out) {
			if (j.contains(key) && j[key].is_array())
				for (const json& b : j[key])
					out.push_back(BlockFromJson(b));
		};
		blocks("initialize", s.Initialize);
		blocks("update", s.Update);
		if (j.contains("output") && j["output"].is_object())
		{
			const json& o = j["output"];
			Output& d = s.OutputCtx;
			d.BlendMode = (Blend)NameIndex(o, "blend", kBlendNames, 0);
			d.Look = (Shape)NameIndex(o, "shape", kShapeNames, 1);
			d.Orientation = (Orient)NameIndex(o, "orient", kOrientNames, 0);
			d.Stretch = o.value("stretch", d.Stretch);
			d.SoftDistance = o.value("softDistance", d.SoftDistance);
			d.Intensity = o.value("intensity", d.Intensity);
			d.Texture = o.value("texture", std::string());
			if (o.contains("flipbook") && o["flipbook"].is_object())
			{
				d.FlipbookColumns = std::clamp(o["flipbook"].value("columns", 1), 1, 64);
				d.FlipbookRows = std::clamp(o["flipbook"].value("rows", 1), 1, 64);
				d.FlipbookFps = o["flipbook"].value("fps", 0.0f);
			}
			d.Sort = (SortMode)NameIndex(o, "sort", kSortNames, 0);
			if (o.contains("trail") && o["trail"].is_object())
			{
				const json& t = o["trail"];
				d.Trail = t.value("enabled", true);
				d.TrailPoints = std::clamp(t.value("points", d.TrailPoints), 2, 32);
				d.TrailLength = (std::max)(0.01f, t.value("length", d.TrailLength));
				d.TrailWidth = (std::max)(0.0f, t.value("width", d.TrailWidth));
				d.TrailOnly = t.value("only", false);
			}
			d.Mesh = o.value("mesh", d.Mesh);
			d.Lit = o.value("lit", d.Lit);
		}
		if (j.contains("editor") && j["editor"].is_object()) s.Editor = j["editor"];
		return s;
	}

	json Asset::ToJson() const
	{
		json props = json::array(), systems = json::array();
		for (const Property& p : Properties) props.push_back(PropertyToJson(p));
		for (const System& s : Systems) systems.push_back(SystemToJson(s));
		json j = { { "version", 1 }, { "properties", props }, { "systems", systems } };
		if (!Operators.empty())
		{
			json ops = json::array();
			for (const OperatorNode& n : Operators) ops.push_back(OperatorToJson(n));
			j["operators"] = ops;
		}
		if (CullingMode == Culling::AlwaysSimulate) j["culling"] = "AlwaysSimulate";
		if (!Attributes.empty())
		{
			json at = json::array();
			for (const Attribute& a : Attributes) at.push_back({ { "name", a.Name }, { "type", a.Vector ? "Vector3" : "Float" } });
			j["attributes"] = at;
		}
		if (!Editor.empty()) j["editor"] = Editor;
		return j;
	}

	bool Asset::FromJson(const json& j, std::string& error)
	{
		if (!j.is_object() || !j.contains("systems") || !j["systems"].is_array())
		{
			error = "not a .vfx (needs a \"systems\" array)";
			return false;
		}
		Properties.clear();
		Systems.clear();
		if (j.contains("properties") && j["properties"].is_array())
			for (const json& p : j["properties"])
				Properties.push_back(PropertyFromJson(p));
		for (const json& s : j["systems"])
			Systems.push_back(SystemFromJson(s));
		Editor = j.contains("editor") && j["editor"].is_object() ? j["editor"] : json::object();
		Operators.clear();
		if (j.contains("operators") && j["operators"].is_array())
			for (const json& o : j["operators"])
				Operators.push_back(OperatorFromJson(o));
		CullingMode = Simplify(j.value("culling", std::string())) == "alwayssimulate" ? Culling::AlwaysSimulate : Culling::SimulateWhenVisible;
		Attributes.clear();
		if (j.contains("attributes") && j["attributes"].is_array())
			for (const json& a : j["attributes"])
				if (a.is_object() && a.contains("name"))
					Attributes.push_back({ a.value("name", std::string()), Simplify(a.value("type", std::string("Float"))).rfind("vector", 0) == 0 });
		return true;
	}

	const Attribute* Asset::FindAttribute(const std::string& name) const
	{
		for (const Attribute& a : Attributes)
			if (a.Name == name)
				return &a;
		return nullptr;
	}

	bool Asset::AttributeLanes(const std::string& name, int& lane, int& width) const
	{
		int at = 0;
		for (const Attribute& a : Attributes)
		{
			const int w = a.Vector ? 3 : 1;
			if (a.Name == name)
			{
				lane = at;
				width = w;
				return at + w <= kAttributeLanes;
			}
			at += w;
		}
		return false;
	}

	const Property* Asset::FindProperty(const std::string& name) const
	{
		for (const Property& p : Properties)
			if (p.Name == name)
				return &p;
		return nullptr;
	}

	int Asset::FindSystem(const std::string& name) const
	{
		for (size_t i = 0; i < Systems.size(); ++i)
			if (Systems[i].Name == name)
				return (int)i;
		return -1;
	}

	std::vector<int> Asset::SimulationOrder() const
	{
		std::vector<int> order;
		std::vector<int> state(Systems.size(), 0);   // 0 아직, 1 보는 중, 2 됨, -1 고리
		std::function<bool(int)> visit = [&](int i) -> bool {
			if (state[i] == 2) return true;
			if (state[i] != 0) return false;
			state[i] = 1;
			const std::string& parent = Systems[i].SpawnCtx.Parent;
			if (!parent.empty())
			{
				const int p = FindSystem(parent);
				if (p < 0 || p == i || !visit(p))
				{
					state[i] = -1;
					return false;
				}
			}
			state[i] = 2;
			order.push_back(i);
			return true;
		};
		for (int i = 0; i < (int)Systems.size(); ++i)
			visit(i);
		return order;
	}

	std::vector<std::string> Asset::Validate() const
	{
		std::vector<std::string> issues;
		for (const System& s : Systems)
		{
			if (!s.SpawnCtx.RateBind.empty() && !FindProperty(s.SpawnCtx.RateBind))
				issues.push_back(s.Name + ": spawn rate binds to a missing property '" + s.SpawnCtx.RateBind + "'");
			if (!s.SpawnCtx.Parent.empty())
			{
				const int p = FindSystem(s.SpawnCtx.Parent);
				if (p < 0) issues.push_back(s.Name + ": parent system '" + s.SpawnCtx.Parent + "' does not exist");
				else if (Systems[p].Name == s.Name) issues.push_back(s.Name + ": a system cannot be its own parent");
			}
			auto check = [&](const std::vector<Block>& blocks, Context ctx) {
				for (const Block& b : blocks)
				{
					const BlockDesc* d = FindBlock(b.Type);
					if (!d) { issues.push_back(s.Name + ": unknown block '" + b.Type + "'"); continue; }
					if (!AllowedIn(*d, ctx)) issues.push_back(s.Name + ": block '" + b.Type + "' belongs to " + (d->Ctx == Context::Initialize ? "initialize" : "update"));
					if (d->Id == 9)
					{
						std::string name;
						for (auto it = b.Params.begin(); it != b.Params.end(); ++it)
							if (Simplify(it.key()) == "attribute" && it->is_string()) name = it->get<std::string>();
						int lane = 0, width = 0;
						if (!AttributeLanes(name, lane, width))
							issues.push_back(s.Name + ": Set Attribute uses " + (name.empty() ? std::string("no attribute") : "a missing (or past 4 floats) attribute '" + name + "'"));
					}
					auto text = [&](const char* key) { const json* j = ParamJson(b, key); return j && j->is_string() ? j->get<std::string>() : std::string(); };
					if (d->Id == 0)
					{
						const std::string path = text("Path");
						const Loaded l = path.empty() ? Loaded{} : Load(path);
						if (path.empty()) issues.push_back(s.Name + ": Sub Graph Block has no file (.vfxblock)");
						else if (!l.Data) issues.push_back(s.Name + ": Sub Graph Block '" + path + "': " + (l.Error.empty() ? std::string("cannot load") : l.Error));
						else if (l.Data->Systems.empty()) issues.push_back(s.Name + ": Sub Graph Block '" + path + "' has no block list (needs one system)");
					}
					if (d->Id == 31)
					{
						const std::string mesh = text("Mesh").empty() ? std::string("Sphere") : text("Mesh");
						const auto m = LoadCpuMesh(mesh);
						if (!m->Error.empty()) issues.push_back(s.Name + ": Collide with SDF: " + m->Error);
						for (const Block& o : blocks)
							if (&o != &b && o.Type == b.Type)
							{
								const json* oj = ParamJson(o, "Mesh");
								const std::string om = oj && oj->is_string() && !oj->get<std::string>().empty() ? oj->get<std::string>() : std::string("Sphere");
								if (om != mesh) { issues.push_back(s.Name + ": one SDF mesh per system - Collide with SDF blocks use '" + mesh + "' and '" + om + "'"); break; }
							}
					}
					for (auto it = b.Bind.begin(); it != b.Bind.end(); ++it)
						if (!it->is_string() || !FindProperty(it->get<std::string>()))
							issues.push_back(s.Name + ": block '" + b.Type + "' binds '" + it.key() + "' to a missing property");
					for (auto it = b.Links.begin(); it != b.Links.end(); ++it)
					{
						const ParamDesc* pd = FindParam(*d, it.key());
						if (!pd || !Linkable(*pd))
							issues.push_back(s.Name + ": block '" + b.Type + "' value '" + it.key() + "' cannot take an operator");
						else if (!it->is_number_integer() || !FindOperatorNode(it->get<int>()))
							issues.push_back(s.Name + ": block '" + b.Type + "' links '" + it.key() + "' to a missing operator");
						else
						{
							struct NoProps : PropertySource { const Asset& A; NoProps(const Asset& a) : A(a) {} bool Get(const std::string& n, std::array<float, 4>& o) const override { const Property* p = A.FindProperty(n); if (!p) return false; o = p->Value; return true; } } props(*this);
							std::vector<std::array<float, 4>> code;
							std::string error;
							if (!CompileOperator(*this, it->get<int>(), props, code, error))
								issues.push_back(s.Name + ": block '" + b.Type + "' value '" + it.key() + "': " + error);
						}
					}
				}
			};
			check(s.Initialize, Context::Initialize);
			check(s.Update, Context::Update);
		}
		for (const OperatorNode& n : Operators)
		{
			const OperatorDesc* od = FindOperator(n.Type);
			if (!od) { issues.push_back("unknown operator '" + n.Type + "' (id " + std::to_string(n.Id) + ")"); continue; }
			for (auto it = n.Inputs.begin(); it != n.Inputs.end(); ++it)
				if (!it->is_number_integer() || !FindOperatorNode(it->get<int>()))
					issues.push_back("operator " + std::to_string(n.Id) + " (" + n.Type + ") input '" + it.key() + "' points to a missing operator");
		}
		{
			int lanes = 0;
			std::unordered_map<std::string, int> seen;
			for (const Attribute& a : Attributes)
			{
				lanes += a.Vector ? 3 : 1;
				if (a.Name.empty()) issues.push_back("a custom attribute has no name");
				if (++seen[a.Name] == 2) issues.push_back("two custom attributes are named '" + a.Name + "'");
			}
			if (lanes > kAttributeLanes)
				issues.push_back("custom attributes use " + std::to_string(lanes) + " floats (at most " + std::to_string(kAttributeLanes) + " - Float = 1, Vector3 = 3)");
		}
		const std::vector<int> order = SimulationOrder();
		if (order.size() != Systems.size())
			issues.push_back("GPU event parents form a loop (or point to a missing system) — those systems do not run");
		std::unordered_map<std::string, int> names;
		for (const System& s : Systems)
			if (++names[s.Name] == 2)
				issues.push_back("two systems are named '" + s.Name + "' (GPU events need unique names)");
		return issues;
	}

	// ---------------------------------------------------------------- 블록 목록 (58. VFX.fx 의 RunInitialize · RunUpdate 와 같은 배치)
	namespace
	{
		using F4 = std::array<float, 4>;

		struct Ctx
		{
			const Block& B;
			const BlockDesc& D;
			const PropertySource& Props;
			const float* World;   // nullptr = Local 시스템 (변환 없음)

			F4 V(const char* name) const
			{
				F4 v = B.GetVector(D, name);
				// 속성 연결 (값 이름 → Blackboard 속성)
				for (auto it = B.Bind.begin(); it != B.Bind.end(); ++it)
					if (Simplify(it.key()) == Simplify(name) && it->is_string())
					{
						F4 p;
						if (Props.Get(it->get<std::string>(), p))
						{
							const ParamDesc* pd = FindParam(D, name);
							const int n = !pd ? 1 : pd->Kind == ParamKind::Vector3 ? 3 : pd->Kind == ParamKind::Color ? 4 : 1;
							for (int i = 0; i < n; ++i) v[i] = p[i];
						}
					}
				const ParamDesc* pd = FindParam(D, name);
				if (World && pd && pd->Space != ParamSpace::None)
				{
					const float* m = World;
					const float w = pd->Space == ParamSpace::Position ? 1.0f : 0.0f;
					const F4 in = v;
					for (int c = 0; c < 3; ++c)
						v[c] = in[0] * m[0 * 4 + c] + in[1] * m[1 * 4 + c] + in[2] * m[2 * 4 + c] + w * m[3 * 4 + c];
				}
				return v;
			}
			float S(const char* name) const { return V(name)[0]; }
		};

		F4 Make(float x, float y = 0, float z = 0, float w = 0) { return { x, y, z, w }; }
	}

	// 블록 하나의 값 칸들 (58. VFX.fx 의 블록 코드가 읽는 배치). 모르는 블록이면 false
	static bool EncodeSlots(const Block& b, const BlockDesc& d0, const PropertySource& props, const float* w, std::vector<F4>& v, const Asset* asset)
	{
		const BlockDesc* d = &d0;
		const Ctx c{ b, *d, props, w };
		v.clear();
		switch (d->Id)
		{
		case 1:
		{
			const float shape = std::round(c.S("Shape"));
			const F4 size = c.V("Size");
			const F4 center = c.V("Center");
			// Center 는 Visual Effect 기준 (World 시스템은 Spawn 이 gWorld 로 옮긴다 — 여기서 변환하지 않는다)
			v.push_back(shape == 3 ? Make(3, size[0], size[1], size[2]) : Make(shape, c.S("Radius")));
			v.push_back(Make(center[0], center[1], center[2], c.S("Surface")));
			v.push_back(Make(c.S("Arc"), c.S("Height"), c.S("ConeAngle"), c.S("Thickness")));
			v.push_back(Make(std::round(c.S("Plane"))));
			break;
		}
		case 8:
		{
			const F4 center = c.V("Center");
			v.push_back(Make(std::round(c.S("Arms")), c.S("Radius"), c.S("Twist"), c.S("Spread")));
			v.push_back(Make(center[0], center[1], center[2], c.S("Thickness")));
			break;
		}
		case 2:
		{
			// 방향은 Spawn 이 gWorld 로 옮긴다 (World 시스템)
			const F4 dir = c.V("Direction");
			v.push_back(Make(std::round(c.S("Mode")), c.S("MinSpeed"), c.S("MaxSpeed"), c.S("Spread")));
			v.push_back(Make(dir[0], dir[1], dir[2]));
			break;
		}
		case 3: case 4: v.push_back(Make(c.S("Min"), c.S("Max"))); break;
		case 5:
			v.push_back(c.V("ColorA"));
			v.push_back(c.V("ColorB"));
			v.push_back(Make(std::round(c.S("Mode")), c.S("Saturation"), c.S("Brightness"), c.S("Intensity")));
			break;
		case 6: v.push_back(Make(c.S("AngleMin"), c.S("AngleMax"), c.S("SpinMin"), c.S("SpinMax"))); break;
		case 7: v.push_back(Make(c.S("Velocity"), c.S("Color"))); break;
		case 9:
		{
			// 사용자 속성: (첫 칸, 칸 수, 모드), 값 (Float 은 네 칸에 같은 값)
			std::string name;
			if (const json* j = ParamJson(b, "Attribute"); j && j->is_string()) name = j->get<std::string>();
			int lane = 0, width = 0;
			if (!asset || !asset->AttributeLanes(name, lane, width))
				return false;
			v.push_back(Make((float)lane, (float)width, std::round(c.S("Mode"))));
			if (width == 1)
			{
				const float x = c.S("Value");
				v.push_back(Make(x, x, x, x));
			}
			else
			{
				const F4 vv = c.V("VectorValue");
				v.push_back(Make(vv[0], vv[1], vv[2]));
			}
			break;
		}
		case 20: { const F4 f = c.V("Force"); v.push_back(Make(f[0], f[1], f[2])); break; }
		case 21: v.push_back(Make(c.S("Coefficient"))); break;
		case 22:
		{
			const F4 scroll = c.V("Scroll");
			v.push_back(Make(c.S("Intensity"), c.S("Frequency"), std::round(c.S("Octaves")), c.S("Drag")));
			v.push_back(Make(scroll[0], scroll[1], scroll[2]));
			break;
		}
		case 23:
		{
			const F4 axis = c.V("Axis"), center = c.V("Center");
			v.push_back(Make(axis[0], axis[1], axis[2], c.S("Speed")));
			v.push_back(Make(center[0], center[1], center[2], c.S("Pull")));
			break;
		}
		case 24:
		{
			const F4 p = c.V("Position");
			v.push_back(Make(p[0], p[1], p[2], c.S("Strength")));
			v.push_back(Make(c.S("Radius"), c.S("Drag")));
			break;
		}
		case 25:
		{
			F4 n = c.V("Normal");
			const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
			if (len > 1e-6f) for (int i = 0; i < 3; ++i) n[i] /= len;
			float offset = c.S("Offset");
			if (w) offset += n[0] * w[12] + n[1] * w[13] + n[2] * w[14];   // 평면이 Visual Effect 와 함께 움직인다
			v.push_back(Make(n[0], n[1], n[2], offset));
			v.push_back(Make(c.S("Bounce"), c.S("Friction"), c.S("LifetimeLoss")));
			break;
		}
		case 26:
		{
			v.push_back(Make(std::round(c.S("Mode"))));
			const auto g = b.GetGradient(*d, "Gradient");
			for (int i = 0; i < 8; ++i) v.push_back(SampleGradient(g, i / 7.0f));
			break;
		}
		case 27:
		{
			v.push_back(Make(0));
			const auto k = b.GetCurve(*d, "Curve");
			for (int i = 0; i < 4; ++i)
				v.push_back(Make(SampleCurve(k, (i * 4 + 0) / 15.0f), SampleCurve(k, (i * 4 + 1) / 15.0f), SampleCurve(k, (i * 4 + 2) / 15.0f), SampleCurve(k, (i * 4 + 3) / 15.0f)));
			break;
		}
		case 28: v.push_back(Make(c.S("Max"))); break;
		case 30: case 32: v.push_back(Make(c.S("Bounce"), c.S("Friction"), c.S("LifetimeLoss"), c.S("Thickness"))); break;
		case 31:
		{
			// 거리장: 시뮬레이션 공간 → 메시 공간 (역행렬 세 열), 메시 → 시뮬레이션 (세 열), 칸 (모서리 · 한 변), 칸 수 · 거리 배율, 튕김 값
			std::string mesh = "Sphere";
			if (const json* j = ParamJson(b, "Mesh"); j && j->is_string() && !j->get<std::string>().empty()) mesh = j->get<std::string>();
			const auto sdf = BakeSdf(mesh, (int)std::round(std::clamp(c.S("Resolution"), 16.0f, 96.0f)));
			if (!sdf->Error.empty())
				return false;
			const F4 pos = c.V("Position"), rot = c.V("Rotation"), scl = c.V("Scale");
			auto rad = [](float d) { return d * 3.14159265f / 180.0f; };
			Matrix m = Matrix::CreateScale(Vec3((std::max)(scl[0], 1e-4f), (std::max)(scl[1], 1e-4f), (std::max)(scl[2], 1e-4f))) *
				Matrix::CreateFromYawPitchRoll(rad(rot[1]), rad(rot[0]), rad(rot[2])) * Matrix::CreateTranslation(Vec3(pos[0], pos[1], pos[2]));
			if (w) m = m * Matrix(w);   // World 시스템: Visual Effect 의 변환까지 (파티클 자리는 월드)
			const Matrix inv = m.Invert();
			// 행 벡터 (p' = p M): p'.x = dot((p, 1), 열 0)
			auto column = [](const Matrix& x, int k) { const float* f = &x._11; return Make(f[k], f[4 + k], f[8 + k], f[12 + k]); };
			for (int k = 0; k < 3; ++k) v.push_back(column(inv, k));
			for (int k = 0; k < 3; ++k) v.push_back(column(m, k));
			const float scale = (Vec3(m._11, m._12, m._13).Length() + Vec3(m._21, m._22, m._23).Length() + Vec3(m._31, m._32, m._33).Length()) / 3.0f;
			v.push_back(Make(sdf->Min[0], sdf->Min[1], sdf->Min[2], sdf->Voxel));
			v.push_back(Make((float)sdf->N[0], (float)sdf->N[1], (float)sdf->N[2], scale));
			v.push_back(Make(c.S("Bounce"), c.S("Friction"), c.S("LifetimeLoss"), c.S("Radius")));
			break;
		}
		case 29:
		{
			const F4 p = c.V("Center"), axis = c.V("Axis");
			v.push_back(Make(p[0], p[1], p[2], c.S("Speed")));
			v.push_back(Make(axis[0], axis[1], axis[2], c.S("Falloff")));
			break;
		}
		default: return false;
		}
		return true;
	}

	// 연산 노드에 이은 값 → 칸마다 식. 값이 든 칸 · 성분은 그 값만 표지 값으로 바꿔 다시 만들어 찾는다 (블록마다 배치표를 따로 두지 않게)
	static void EncodeLinks(const Asset& asset, const Block& b, const BlockDesc& d, const PropertySource& props, const float* w,
		const std::vector<F4>& slots, std::vector<F4>& exprs, uint32_t& mask)
	{
		struct Patch { int Slot, Comp, Src; };
		struct Piece { std::vector<F4> Code; std::vector<Patch> Patches; };
		std::map<int, std::vector<Piece>> perSlot;
		static const float kSentinel[4] = { 1.2345e30f, 1.3579e30f, 1.4321e30f, 1.5791e30f };
		for (auto it = b.Links.begin(); it != b.Links.end(); ++it)
		{
			const ParamDesc* pd = FindParam(d, it.key());
			if (!pd || !Linkable(*pd) || !it->is_number_integer())
				continue;
			const int n = pd->Kind == ParamKind::Vector3 ? 3 : pd->Kind == ParamKind::Color ? 4 : 1;
			Block probe = b;
			probe.Links = json::object();
			for (auto bi = probe.Bind.begin(); bi != probe.Bind.end(); ++bi)
				if (Simplify(bi.key()) == Simplify(pd->Name)) { probe.Bind.erase(bi); break; }
			if (n == 1)
				probe.Params[pd->Name] = kSentinel[0];
			else
			{
				json arr = json::array();
				for (int c = 0; c < n; ++c) arr.push_back(kSentinel[c]);
				probe.Params[pd->Name] = arr;
			}
			std::vector<F4> pv;
			if (!EncodeSlots(probe, d, props, nullptr, pv, &asset))
				continue;
			Piece piece;
			for (int c = 0; c < n; ++c)
				for (int k = 0; k < (int)pv.size() && k < 8; ++k)   // 식은 앞 8 칸까지 (58. VFX.fx 의 sSlot)
					for (int m = 0; m < 4; ++m)
						if (pv[k][m] == kSentinel[c])
							piece.Patches.push_back({ k, m, c });
			if (piece.Patches.empty())
				continue;   // 이 블록은 값을 바꿔 써서 (예: 법선을 정규화) 이을 수 없다 — Validate 가 알린다
			std::string error;
			if (!CompileOperator(asset, it->get<int>(), props, piece.Code, error))
				continue;
			// World 시스템의 위치 · 방향 값: 연산 결과에 Visual Effect 변환
			if (w && pd->Space == ParamSpace::Position) piece.Code.push_back(Make(90));
			else if (w && pd->Space == ParamSpace::Direction) piece.Code.push_back(Make(91));
			std::map<int, std::vector<Patch>> bySlot;
			for (const Patch& pt : piece.Patches) bySlot[pt.Slot].push_back(pt);
			for (auto& [slot, list] : bySlot)
				perSlot[slot].push_back({ piece.Code, list });
		}
		for (auto& [slot, pieces] : perSlot)
		{
			// 칸의 상수 → (식 → 성분 바꾸기) … — 마지막 성분 바꾸기가 식 값을 내린다
			std::vector<F4> e;
			e.push_back(Make(1));
			e.push_back(slots[slot]);
			for (const Piece& pc : pieces)
			{
				e.insert(e.end(), pc.Code.begin(), pc.Code.end());
				for (size_t k = 0; k < pc.Patches.size(); ++k)
					e.push_back(Make(95, (float)pc.Patches[k].Comp, (float)pc.Patches[k].Src, k + 1 == pc.Patches.size() ? 1.0f : 0.0f));
			}
			exprs.push_back(Make((float)slot, (float)e.size()));
			exprs.insert(exprs.end(), e.begin(), e.end());
			mask |= 1u << slot;
		}
	}

	namespace
	{
		// Sub Graph Block 안의 속성: 파일의 Blackboard 기본값 → 바깥 블록의 값 → 바깥 블록의 속성 연결 (바깥 Visual Effect 의 값)
		struct SubgraphProps : PropertySource
		{
			const Asset& Sub;
			const Block& Outer;
			const PropertySource& OuterProps;
			SubgraphProps(const Asset& sub, const Block& outer, const PropertySource& outerProps) : Sub(sub), Outer(outer), OuterProps(outerProps) {}
			bool Get(const std::string& name, std::array<float, 4>& out) const override
			{
				const Property* p = Sub.FindProperty(name);
				if (!p)
					return false;
				out = p->Value;
				for (auto it = Outer.Bind.begin(); it != Outer.Bind.end(); ++it)
					if (Simplify(it.key()) == Simplify(name) && it->is_string())
					{
						std::array<float, 4> v;
						if (OuterProps.Get(it->get<std::string>(), v)) { out = v; return true; }
					}
				if (const json* j = ParamJson(Outer, name.c_str()))
					out = ValueFromJson(*j, out);
				return true;
			}
		};
		constexpr int kMaxBlockSubgraphLevel = 4;

		// 블록 목록 하나 (Sub Graph Block 은 그 파일의 같은 문맥 블록들로 펼친다). top = 사용자 속성을 정한 에셋 (맨 위 .vfx)
		void EncodeBlocks(const std::vector<Block>& blocks, Context ctx, const PropertySource& props, const float* w, const Asset* asset, const Asset* top,
			Encoded& out, uint32_t& count, int level)
		{
			for (const Block& b : blocks)
			{
				const BlockDesc* d = FindBlock(b.Type);
				if (!b.Enabled || !d || !AllowedIn(*d, ctx))
					continue;
				if (d->Id == 0)
				{
					if (level >= kMaxBlockSubgraphLevel)
						continue;
					const json* pj = ParamJson(b, "Path");
					const std::string path = pj && pj->is_string() ? pj->get<std::string>() : std::string();
					const Loaded l = path.empty() ? Loaded{} : Load(path);
					if (!l.Data || l.Data->Systems.empty())
						continue;
					const System& inner = l.Data->Systems[0];
					const SubgraphProps sp(*l.Data, b, props);
					EncodeBlocks(ctx == Context::Initialize ? inner.Initialize : inner.Update, ctx, sp, w, l.Data.get(), top, out, count, level + 1);
					continue;
				}
				std::vector<F4> v;
				if (!EncodeSlots(b, *d, props, w, v, top))
					continue;
				// 연산 노드에 이은 값: 그 값이 든 칸마다 식 (칸의 상수 → 노드 값으로 성분 바꾸기). 머리 = (종류, 칸 + 식 수, 칸 수, 식 있는 칸 비트)
				std::vector<F4> exprs;
				uint32_t mask = 0;
				if (asset && !b.Links.empty())
					EncodeLinks(*asset, b, *d, props, w, v, exprs, mask);
				out.Program.push_back(Make((float)d->Id, (float)(v.size() + exprs.size()), (float)v.size(), (float)mask));
				out.Program.insert(out.Program.end(), v.begin(), v.end());
				out.Program.insert(out.Program.end(), exprs.begin(), exprs.end());
				++count;
			}
		}
	}

	void Encode(const System& s, const PropertySource& props, const float* world, Encoded& out, const Asset* asset)
	{
		out.Program.clear();
		const float* w = s.Local ? nullptr : world;
		out.InitStart = (uint32_t)out.Program.size();
		out.InitCount = 0;
		EncodeBlocks(s.Initialize, Context::Initialize, props, w, asset, asset, out, out.InitCount, 0);
		out.UpdateStart = (uint32_t)out.Program.size();
		out.UpdateCount = 0;
		EncodeBlocks(s.Update, Context::Update, props, w, asset, asset, out, out.UpdateCount, 0);
		if (out.Program.empty())
			out.Program.push_back(Make(0));   // 빈 버퍼는 만들 수 없다
	}

	// ---------------------------------------------------------------- 파일
	std::wstring FullPath(const std::string& assetPath)
	{
		const std::wstring w = string_to_wstring(assetPath);
		if (fs::path(w).is_absolute())
			return w;
		return PathManager::GetI()->GetMovePathW(w);
	}

	Loaded Load(const std::string& assetPath)
	{
		Loaded r;
		if (assetPath.empty())
			return r;
		CacheEntry& e = s_Cache[Key(assetPath)];
		if (e.Live)
		{
			r.Data = e.Live;
			r.Revision = e.Revision;
			return r;
		}
		// 파일 시각은 0.5 초에 한 번만 본다 (Visual Effect 마다 프레임마다 묻는다)
		const auto now = std::chrono::steady_clock::now();
		if ((!e.Data && e.Error.empty()) || now - e.Checked > std::chrono::milliseconds(500))
		{
			e.Checked = now;
			std::error_code ec;
			const std::wstring full = FullPath(assetPath);
			const auto t = fs::last_write_time(full, ec);
			if (ec)
			{
				if (e.Error.empty() || e.Data)
				{
					e.Data.reset();
					e.Error = "file not found: " + assetPath;
					e.Revision = ++s_Revision;
				}
			}
			else if (!e.Data || t != e.Time)
			{
				std::ifstream in(full, std::ios::binary);
				std::stringstream ss;
				ss << in.rdbuf();
				const json j = json::parse(ss.str(), nullptr, false);
				auto a = std::make_shared<Asset>();
				std::string error;
				if (j.is_discarded())
					error = "invalid JSON";
				else
					a->FromJson(j, error);
				e.Time = t;
				e.Revision = ++s_Revision;
				if (error.empty())
				{
					e.Data = a;
					e.Error.clear();
				}
				else
				{
					e.Data.reset();
					e.Error = assetPath + ": " + error;
					EditorLog::Write("VFX", "%s", e.Error.c_str());
				}
			}
		}
		r.Data = e.Data;
		r.Revision = e.Revision;
		r.Error = e.Error;
		return r;
	}

	void SetLive(const std::string& assetPath, std::shared_ptr<const Asset> asset)
	{
		CacheEntry& e = s_Cache[Key(assetPath)];
		e.Live = std::move(asset);
		e.Revision = ++s_Revision;
	}

	bool SetLiveJson(const std::string& assetPath, const json& j, std::string& error)
	{
		auto a = std::make_shared<Asset>();
		if (!a->FromJson(j, error))
			return false;
		SetLive(assetPath, a);
		return true;
	}

	void ClearLive(const std::string& assetPath)
	{
		auto it = s_Cache.find(Key(assetPath));
		if (it == s_Cache.end() || !it->second.Live)
			return;
		it->second.Live.reset();
		it->second.Data.reset();   // 파일에서 다시
		it->second.Error.clear();
		it->second.Revision = ++s_Revision;
	}

	bool Save(const std::string& assetPath, const Asset& asset, std::string& error)
	{
		const std::wstring full = FullPath(assetPath);
		std::error_code ec;
		fs::create_directories(fs::path(full).parent_path(), ec);
		std::ofstream os(full, std::ios::binary | std::ios::trunc);
		if (!os)
		{
			error = "cannot write " + assetPath;
			return false;
		}
		os << asset.ToJson().dump(2);
		os.close();
		// 저장한 내용이 곧 캐시 (다음 Load 가 파일을 다시 읽지 않게 시각도 맞춘다)
		CacheEntry& e = s_Cache[Key(assetPath)];
		e.Live.reset();   // 저장한 것이 곧 최신 (그래프 창의 편집 중 내용도 이 저장에 들어 있다)
		e.Data = std::make_shared<Asset>(asset);
		e.Time = fs::last_write_time(full, ec);
		e.Error.clear();
		e.Checked = std::chrono::steady_clock::now();
		e.Revision = ++s_Revision;
		return true;
	}

	std::vector<std::string> FindAssets(const char* extension)
	{
		std::vector<std::string> found;
		const fs::path root = PathManager::GetI()->GetContentPathW();
		std::error_code ec;
		for (fs::recursive_directory_iterator it(root / L"Assets", fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec))
		{
			if (ec) break;
			if (it->is_regular_file(ec) && Lower(it->path().extension().string()) == extension)
				found.push_back(wstring_to_string(fs::relative(it->path(), root, ec).wstring()));
		}
		std::sort(found.begin(), found.end());
		return found;
	}
}
