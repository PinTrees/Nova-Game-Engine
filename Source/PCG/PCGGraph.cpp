#include "pch.h"
#include "PCGGraph.h"
#include "PathManager.h"
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>

namespace PCG
{
	const std::vector<NodeDesc>& NodeDescs()
	{
		static const std::vector<NodeDesc> descs = {
			{ NodeType::SurfaceSampler, "SurfaceSampler", "Surface Sampler", "Sampler", {}, true, {
				{ "pointsPerSquaredMeter", "Points Per m2", ParamKind::Float, 0.05f, 0.0001f, 20.0f, nullptr, "1 m2 당 점 수 (밀도). 점은 월드 격자에 고정 — 셀 경계가 맞는다" },
				{ "looseness", "Looseness", ParamKind::Float, 1.0f, 0.0f, 1.0f, nullptr, "격자에서 흩뜨리는 정도 (0 = 반듯한 격자)" },
				{ "pointExtents", "Point Extents", ParamKind::Float, 1.0f, 0.05f, 30.0f, nullptr, "점 하나의 크기 (m) — Self Pruning 이 쓴다" },
				{ "seed", "Seed", ParamKind::Int, 0.0f, 0.0f, 100000.0f, nullptr, "" } },
				"월드 지형 표면에서 점을 뽑는다 (높이 · 법선 · 경사 · 바이옴이 점에 담긴다)" },
			{ NodeType::DensityNoise, "DensityNoise", "Density Noise", "Filter", { "In" }, true, {
				{ "scale", "Scale (m)", ParamKind::Float, 60.0f, 1.0f, 5000.0f, nullptr, "노이즈 무늬 크기" },
				{ "octaves", "Octaves", ParamKind::Int, 3.0f, 1.0f, 6.0f, nullptr, "" },
				{ "contrast", "Contrast", ParamKind::Float, 1.5f, 0.1f, 8.0f, nullptr, "" },
				{ "offset", "Offset", ParamKind::Float, 0.0f, -1.0f, 1.0f, nullptr, "더하면 짙어진다" },
				{ "mode", "Mode", ParamKind::Enum, 0.0f, 0.0f, 3.0f, "Multiply|Set|Minimum|Maximum", "점 밀도와 합치는 법" },
				{ "invert", "Invert", ParamKind::Bool, 0.0f, 0.0f, 1.0f, nullptr, "" },
				{ "seed", "Seed", ParamKind::Int, 0.0f, 0.0f, 100000.0f, nullptr, "" } },
				"점의 밀도에 노이즈 (무리 · 빈터)" },
			{ NodeType::HeightFilter, "HeightFilter", "Height Filter", "Filter", { "In" }, true, {
				{ "minHeight", "Min Height (m)", ParamKind::Float, 0.0f, 0.0f, 5000.0f, nullptr, "" },
				{ "maxHeight", "Max Height (m)", ParamKind::Float, 700.0f, 0.0f, 5000.0f, nullptr, "" },
				{ "falloff", "Falloff (m)", ParamKind::Float, 40.0f, 0.0f, 1000.0f, nullptr, "경계에서 밀도가 줄어드는 폭" } },
				"높이 범위 밖의 점 밀도를 0 으로" },
			{ NodeType::SlopeFilter, "SlopeFilter", "Slope Filter", "Filter", { "In" }, true, {
				{ "minAngle", "Min Angle", ParamKind::Float, 0.0f, 0.0f, 90.0f, nullptr, "도" },
				{ "maxAngle", "Max Angle", ParamKind::Float, 30.0f, 0.0f, 90.0f, nullptr, "도" },
				{ "falloff", "Falloff", ParamKind::Float, 5.0f, 0.0f, 45.0f, nullptr, "도" } },
				"경사 범위 밖의 점 밀도를 0 으로" },
			{ NodeType::BiomeFilter, "BiomeFilter", "Biome Filter", "Filter", { "In" }, true, {
				{ "biome", "Biome", ParamKind::Enum, 0.0f, 0.0f, 3.0f, "Forest|Meadow|Desert|Rock", "" },
				{ "minWeight", "Min Weight", ParamKind::Float, 0.3f, 0.0f, 1.0f, nullptr, "바이옴 비중이 이보다 작으면 뺀다" },
				{ "useAsDensity", "Use As Density", ParamKind::Bool, 1.0f, 0.0f, 1.0f, nullptr, "비중을 밀도에 곱한다 (경계가 부드럽게 옅어진다)" } },
				"World Terrain 의 바이옴 (숲 · 초원 · 사막 · 바위) 으로 거른다" },
			{ NodeType::DensityFilter, "DensityFilter", "Density Filter", "Filter", { "In" }, true, {
				{ "lowerBound", "Lower Bound", ParamKind::Float, 0.0f, 0.0f, 1.0f, nullptr, "" },
				{ "upperBound", "Upper Bound", ParamKind::Float, 1.0f, 0.0f, 1.0f, nullptr, "" },
				{ "randomize", "Randomize", ParamKind::Bool, 1.0f, 0.0f, 1.0f, nullptr, "밀도를 남을 확률로 (끄면 범위 안만 남김)" } },
				"밀도로 점을 남기거나 뺀다" },
			{ NodeType::SelfPruning, "SelfPruning", "Self Pruning", "Spatial", { "In" }, true, {
				{ "radius", "Radius (m)", ParamKind::Float, 4.0f, 0.05f, 100.0f, nullptr, "이 거리 안의 다른 점을 지운다 (밀도가 큰 것을 남김)" },
				{ "scaleWithSize", "Scale With Size", ParamKind::Bool, 1.0f, 0.0f, 1.0f, nullptr, "점 크기 (Transform 의 Scale) 를 곱한다" } },
				"겹치지 않게 솎는다" },
			{ NodeType::Difference, "Difference", "Difference", "Spatial", { "In", "Exclusions" }, true, {
				{ "radius", "Radius (m)", ParamKind::Float, 3.0f, 0.0f, 100.0f, nullptr, "Exclusions 의 점에서 이 거리 안의 점을 지운다" } },
				"다른 규칙의 점 (예: 나무) 근처를 뺀다" },
			{ NodeType::TransformPoints, "TransformPoints", "Transform Points", "Spatial", { "In" }, true, {
				{ "yawMin", "Yaw Min", ParamKind::Float, 0.0f, 0.0f, 360.0f, nullptr, "도" },
				{ "yawMax", "Yaw Max", ParamKind::Float, 360.0f, 0.0f, 360.0f, nullptr, "도" },
				{ "scaleMin", "Scale Min", ParamKind::Float, 0.8f, 0.05f, 10.0f, nullptr, "" },
				{ "scaleMax", "Scale Max", ParamKind::Float, 1.2f, 0.05f, 10.0f, nullptr, "" },
				{ "alignToNormal", "Align To Normal", ParamKind::Float, 0.0f, 0.0f, 1.0f, nullptr, "지형 기울기를 따르는 정도" },
				{ "tiltMax", "Tilt Max", ParamKind::Float, 0.0f, 0.0f, 45.0f, nullptr, "무작위 기울기 (도)" },
				{ "offsetMin", "Offset Y Min", ParamKind::Float, 0.0f, -10.0f, 10.0f, nullptr, "m" },
				{ "offsetMax", "Offset Y Max", ParamKind::Float, 0.0f, -10.0f, 10.0f, nullptr, "m" } },
				"돌리고 · 키우고 · 기울인다 (점마다 무작위 — 같은 점 = 같은 값)" },
			{ NodeType::Merge, "Merge", "Merge", "Spatial", { "In" }, true, {},
				"여러 줄기의 점을 하나로" },
			{ NodeType::StaticMeshSpawner, "StaticMeshSpawner", "Static Mesh Spawner", "Spawner", { "In" }, false, {
				{ "cullDistance", "Cull Distance (m)", ParamKind::Float, 300.0f, 10.0f, 20000.0f, nullptr, "이 거리 안의 셀만 만들고 그린다 (Runtime Generation 반지름)" },
				{ "cellSize", "Cell Size (m)", ParamKind::Float, 0.0f, 0.0f, 4096.0f, nullptr, "생성 격자 (0 = 자동: Cull Distance / 8, 32 ~ 128 m)" },
				{ "castShadows", "Cast Shadows", ParamKind::Bool, 1.0f, 0.0f, 1.0f, nullptr, "" },
				{ "shadowDistance", "Shadow Distance (m)", ParamKind::Float, 150.0f, 0.0f, 5000.0f, nullptr, "이 거리 안만 그림자" },
				{ "lodBias", "LOD Bias", ParamKind::Float, 1.0f, 0.1f, 4.0f, nullptr, "크면 더 멀리까지 자세한 LOD" },
				{ "seed", "Seed", ParamKind::Int, 0.0f, 0.0f, 100000.0f, nullptr, "메시 고르기" } },
				"점마다 메시 하나 (가중치로 고른다). 모델의 _LODn 노드 = LOD, 인스턴싱으로 그린다" },
		};
		return descs;
	}

	const NodeDesc* FindDesc(NodeType type)
	{
		for (const NodeDesc& d : NodeDescs())
			if (d.Type == type)
				return &d;
		return nullptr;
	}

	const NodeDesc* FindDesc(const std::string& name)
	{
		for (const NodeDesc& d : NodeDescs())
			if (name == d.Name)
				return &d;
		return nullptr;
	}

	float Node::Get(const char* name) const
	{
		const NodeDesc* d = FindDesc(Type);
		if (d == nullptr)
			return 0.0f;
		for (size_t i = 0; i < d->Params.size(); ++i)
			if (strcmp(d->Params[i].Name, name) == 0)
				return i < Values.size() ? Values[i] : d->Params[i].Default;
		return 0.0f;
	}

	void Node::Set(const char* name, float v)
	{
		const NodeDesc* d = FindDesc(Type);
		if (d == nullptr)
			return;
		for (size_t i = 0; i < d->Params.size(); ++i)
			if (strcmp(d->Params[i].Name, name) == 0)
			{
				if (Values.size() < d->Params.size())
				{
					const size_t old = Values.size();
					Values.resize(d->Params.size());
					for (size_t k = old; k < Values.size(); ++k)
						Values[k] = d->Params[k].Default;
				}
				Values[i] = v;
				return;
			}
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

	bool Graph::Connect(int from, int to, int pin)
	{
		if (from == to || Find(from) == nullptr || Find(to) == nullptr)
			return false;
		// 순환: to 에서 출력 쪽으로 가다 from 을 만나면
		std::vector<int> stack = { to };
		std::vector<int> seen;
		while (!stack.empty())
		{
			const int n = stack.back();
			stack.pop_back();
			if (n == from)
				return false;
			if (std::find(seen.begin(), seen.end(), n) != seen.end())
				continue;
			seen.push_back(n);
			for (const Edge& e : Edges)
				if (e.From == n)
					stack.push_back(e.To);
		}
		const Node* target = Find(to);
		if (target->Type != NodeType::Merge)
			Edges.erase(std::remove_if(Edges.begin(), Edges.end(), [&](const Edge& e) { return e.To == to && e.Pin == pin; }), Edges.end());
		for (const Edge& e : Edges)
			if (e.From == from && e.To == to && e.Pin == pin)
				return true;
		Edges.push_back({ from, to, pin });
		Touch();
		return true;
	}

	void Graph::Disconnect(int to, int pin, int from)
	{
		Edges.erase(std::remove_if(Edges.begin(), Edges.end(), [&](const Edge& e) { return e.To == to && e.Pin == pin && (from == 0 || e.From == from); }), Edges.end());
		Touch();
	}

	std::vector<int> Graph::InputsOf(int node, int pin) const
	{
		std::vector<int> out;
		for (const Edge& e : Edges)
			if (e.To == node && e.Pin == pin)
				out.push_back(e.From);
		return out;
	}

	std::vector<int> Graph::Spawners() const
	{
		std::vector<int> out;
		for (const Node& n : Nodes)
			if (n.Type == NodeType::StaticMeshSpawner && n.Enabled)
				out.push_back(n.Id);
		return out;
	}

	namespace
	{
		json NodeJson(const Node& n, bool withLayout)
		{
			json j;
			j["id"] = n.Id;
			const NodeDesc* d = FindDesc(n.Type);
			j["type"] = d ? d->Name : "Unknown";
			if (withLayout)
			{
				j["x"] = n.X;
				j["y"] = n.Y;
				if (!n.Comment.empty())
					j["comment"] = n.Comment;
			}
			if (!n.Enabled)
				j["enabled"] = false;
			json params = json::object();
			if (d)
				for (size_t i = 0; i < d->Params.size(); ++i)
					params[d->Params[i].Name] = i < n.Values.size() ? n.Values[i] : d->Params[i].Default;
			j["params"] = params;
			if (n.Type == NodeType::StaticMeshSpawner)
			{
				json meshes = json::array();
				for (const MeshEntry& m : n.Meshes)
					meshes.push_back({ { "path", m.Path }, { "weight", m.Weight } });
				j["meshes"] = meshes;
			}
			return j;
		}
	}

	uint64_t Graph::ContentHash() const
	{
		// 배치 (x · y · 설명) 는 빼고 — 노드를 옮겨도 다시 만들지 않는다
		json j;
		json nodes = json::array();
		for (const Node& n : Nodes)
			nodes.push_back(NodeJson(n, false));
		j["nodes"] = nodes;
		json edges = json::array();
		for (const Edge& e : Edges)
			edges.push_back({ e.From, e.To, e.Pin });
		j["edges"] = edges;
		return std::hash<std::string>()(j.dump());
	}

	std::string Graph::ToJson() const
	{
		json j;
		j["nova_pcg"] = 1;
		j["nextId"] = NextId;
		json nodes = json::array();
		for (const Node& n : Nodes)
			nodes.push_back(NodeJson(n, true));
		j["nodes"] = nodes;
		json edges = json::array();
		for (const Edge& e : Edges)
			edges.push_back({ { "from", e.From }, { "to", e.To }, { "pin", e.Pin } });
		j["edges"] = edges;
		return j.dump(2);
	}

	bool Graph::FromJson(const std::string& text, std::string* error)
	{
		json j = json::parse(text, nullptr, false);
		if (j.is_discarded() || !j.is_object() || !j.contains("nodes"))
		{
			if (error) *error = "not a PCG graph";
			return false;
		}
		Nodes.clear();
		Edges.clear();
		NextId = j.value("nextId", 1);
		for (const json& nj : j["nodes"])
		{
			const NodeDesc* d = FindDesc(nj.value("type", std::string()));
			if (d == nullptr)
				continue;
			Node n;
			n.Id = nj.value("id", 0);
			n.Type = d->Type;
			n.X = nj.value("x", 0.0f);
			n.Y = nj.value("y", 0.0f);
			n.Comment = nj.value("comment", std::string());
			n.Enabled = nj.value("enabled", true);
			const json params = nj.value("params", json::object());
			for (const ParamDesc& p : d->Params)
				n.Values.push_back(params.contains(p.Name) && params[p.Name].is_number() ? params[p.Name].get<float>() : p.Default);
			if (nj.contains("meshes"))
				for (const json& m : nj["meshes"])
					n.Meshes.push_back({ m.value("path", std::string()), m.value("weight", 1.0f) });
			NextId = (std::max)(NextId, n.Id + 1);
			Nodes.push_back(std::move(n));
		}
		if (j.contains("edges"))
			for (const json& e : j["edges"])
				if (Find(e.value("from", 0)) && Find(e.value("to", 0)))
					Edges.push_back({ e.value("from", 0), e.value("to", 0), e.value("pin", 0) });
		Touch();
		return true;
	}

	namespace
	{
		std::mutex s_GraphLock;
		std::map<std::string, std::shared_ptr<Graph>> s_Graphs;

		std::string Key(const std::string& path)
		{
			std::string k = path;
			std::replace(k.begin(), k.end(), '/', '\\');
			std::transform(k.begin(), k.end(), k.begin(), ::tolower);
			return k;
		}
	}

	std::shared_ptr<Graph> LoadGraph(const std::string& path)
	{
		if (path.empty())
			return nullptr;
		std::lock_guard<std::mutex> lock(s_GraphLock);
		auto it = s_Graphs.find(Key(path));
		if (it != s_Graphs.end())
			return it->second;
		std::ifstream in(PathManager::GetI()->GetMovePathW(string_to_wstring(path)), std::ios::binary);
		if (!in)
			return nullptr;
		const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		auto g = std::make_shared<Graph>();
		if (!g->FromJson(text))
			return nullptr;
		s_Graphs[Key(path)] = g;
		return g;
	}

	bool SaveGraph(const std::string& path, const Graph& graph)
	{
		const std::wstring file = PathManager::GetI()->GetMovePathW(string_to_wstring(path));
		std::error_code ec;
		std::filesystem::create_directories(std::filesystem::path(file).parent_path(), ec);
		std::ofstream out(file, std::ios::binary | std::ios::trunc);
		if (!out)
			return false;
		out << graph.ToJson();
		return true;
	}

	Graph MakeOpenWorldPreset(const std::vector<std::pair<std::string, std::string>>& models)
	{
		Graph g;
		// 역할의 모델 (많으면 고르게 max 개 — 변형마다 메시 · 텍스처가 GPU 에 올라간다)
		auto of = [&](std::initializer_list<const char*> kinds, size_t max = 8) {
			std::vector<MeshEntry> all;
			for (const auto& m : models)
				for (const char* k : kinds)
					if (m.first == k)
						all.push_back({ m.second, 1.0f });
			if (all.size() <= max)
				return all;
			std::vector<MeshEntry> out;
			for (size_t i = 0; i < max; ++i)
				out.push_back(all[i * all.size() / max]);
			return out;
		};
		float y = 0.0f;
		// 줄 하나 = 표본 → 거르기 … → 놓기. 반환 = 마지막 점 노드 (Difference 의 Exclusions 로 쓴다)
		auto chain = [&](const char* name, float density, float extents, int biome, float minWeight, float noiseScale, float pruning, float scaleMin, float scaleMax,
			float align, float maxSlope, std::vector<MeshEntry> meshes, float cull, bool shadows, float shadowDistance, int exclude, float excludeRadius) -> int {
			if (meshes.empty())
				return 0;
			float x = 0.0f;
			Node& s = g.Add(NodeType::SurfaceSampler, x, y); x += 260.0f;
			s.Comment = name;
			s.Set("pointsPerSquaredMeter", density);
			s.Set("pointExtents", extents);
			s.Set("seed", (float)(g.NextId * 37));
			int last = s.Id;
			if (biome >= 0)
			{
				Node& b = g.Add(NodeType::BiomeFilter, x, y); x += 260.0f;
				b.Set("biome", (float)biome);
				b.Set("minWeight", minWeight);
				g.Connect(last, b.Id, 0);
				last = b.Id;
			}
			Node& sl = g.Add(NodeType::SlopeFilter, x, y); x += 260.0f;
			sl.Set("maxAngle", maxSlope);
			g.Connect(last, sl.Id, 0);
			last = sl.Id;
			if (noiseScale > 0.0f)
			{
				Node& nn = g.Add(NodeType::DensityNoise, x, y); x += 260.0f;
				nn.Set("scale", noiseScale);
				nn.Set("seed", (float)(g.NextId * 53));
				g.Connect(last, nn.Id, 0);
				last = nn.Id;
			}
			Node& df = g.Add(NodeType::DensityFilter, x, y); x += 260.0f;
			g.Connect(last, df.Id, 0);
			last = df.Id;
			if (exclude > 0)
			{
				Node& d = g.Add(NodeType::Difference, x, y); x += 260.0f;
				d.Set("radius", excludeRadius);
				g.Connect(last, d.Id, 0);
				g.Connect(exclude, d.Id, 1);
				last = d.Id;
			}
			if (pruning > 0.0f)
			{
				Node& p = g.Add(NodeType::SelfPruning, x, y); x += 260.0f;
				p.Set("radius", pruning);
				g.Connect(last, p.Id, 0);
				last = p.Id;
			}
			Node& t = g.Add(NodeType::TransformPoints, x, y); x += 260.0f;
			t.Set("scaleMin", scaleMin);
			t.Set("scaleMax", scaleMax);
			t.Set("alignToNormal", align);
			g.Connect(last, t.Id, 0);
			const int points = t.Id;
			Node& sp = g.Add(NodeType::StaticMeshSpawner, x, y);
			sp.Comment = name;
			sp.Meshes = std::move(meshes);
			sp.Set("cullDistance", cull);
			sp.Set("castShadows", shadows ? 1.0f : 0.0f);
			sp.Set("shadowDistance", shadowDistance);
			g.Connect(points, sp.Id, 0);
			y += 220.0f;
			return points;
		};
		enum { Forest = 0, Meadow = 1, Desert = 2, Rock = 3 };
		const int trees = chain("Forest Trees", 0.014f, 3.0f, Forest, 0.35f, 220.0f, 6.0f, 0.8f, 1.25f, 0.0f, 32.0f, of({ "BigTree" }), 3000.0f, true, 250.0f, 0, 0.0f);
		const int meadowTrees = chain("Meadow Trees", 0.0012f, 4.0f, Meadow, 0.4f, 400.0f, 14.0f, 0.8f, 1.2f, 0.0f, 25.0f, of({ "BigTree", "SmallTree" }), 3000.0f, true, 250.0f, 0, 0.0f);
		chain("Forest Understory", 0.18f, 0.8f, Forest, 0.3f, 45.0f, 1.2f, 0.7f, 1.3f, 0.5f, 38.0f, of({ "Fern", "Bush" }), 220.0f, true, 60.0f, trees, 2.5f);
		chain("Forest Floor", 1.4f, 0.3f, Forest, 0.3f, 18.0f, 0.0f, 0.7f, 1.3f, 0.8f, 40.0f, of({ "ForestCover", "Grass" }), 75.0f, false, 0.0f, trees, 1.5f);
		chain("Forest Debris", 0.012f, 1.5f, Forest, 0.4f, 90.0f, 3.0f, 0.8f, 1.3f, 1.0f, 30.0f, of({ "Debris" }), 220.0f, true, 60.0f, trees, 3.0f);
		chain("Mushrooms", 0.02f, 0.2f, Forest, 0.5f, 12.0f, 0.4f, 0.7f, 1.4f, 0.6f, 30.0f, of({ "Mushroom" }), 50.0f, false, 0.0f, trees, 1.0f);
		chain("Meadow Grass", 4.0f, 0.25f, Meadow, 0.2f, 30.0f, 0.0f, 0.75f, 1.35f, 0.7f, 35.0f, of({ "Grass" }, 12), 75.0f, false, 0.0f, meadowTrees, 1.0f);
		chain("Meadow Bushes", 0.012f, 1.0f, Meadow, 0.35f, 120.0f, 2.5f, 0.7f, 1.3f, 0.4f, 30.0f, of({ "Bush" }), 400.0f, true, 80.0f, meadowTrees, 3.0f);
		chain("Desert Cactus", 0.025f, 1.5f, Desert, 0.4f, 160.0f, 3.0f, 0.7f, 1.3f, 0.0f, 25.0f, of({ "Desert" }), 1500.0f, true, 120.0f, 0, 0.0f);
		chain("Desert Shrubs", 0.15f, 0.6f, Desert, 0.3f, 50.0f, 1.2f, 0.7f, 1.3f, 0.6f, 30.0f, of({ "DesertGrass" }), 200.0f, false, 0.0f, 0, 0.0f);
		chain("Rocks", 0.003f, 2.5f, Rock, 0.3f, 300.0f, 6.0f, 0.6f, 2.2f, 1.0f, 70.0f, of({ "Rock" }), 1500.0f, true, 200.0f, 0, 0.0f);
		return g;
	}
}

#include <regex>

namespace PCG
{
	std::string ClassifyModel(const std::string& rawPath)
	{
		std::string p = rawPath;
		std::replace(p.begin(), p.end(), '\\', '/');
		std::transform(p.begin(), p.end(), p.begin(), ::tolower);
		const std::string file = p.substr(p.find_last_of('/') + 1);
		auto has = [&](const char* rx) { return std::regex_search(file, std::regex(rx)); };
		// 놓지 않는 것: 건물 · 소품 · 지면 스캔 · 빌보드 전용 · 고해상도 중복 (Very High / High Geometry)
		if (has("cabin|bonfire|camping|cup|grill|stake|bucket|chair|ladder|pitchfork|plank|shovel|wheel|ground_scan|billboard|mannequin|skeletalmesh|skelmesh|_mesh\\.")
			|| p.find("/very high geometry/") != std::string::npos || p.find("/high geometry/") != std::string::npos)
			return "";
		if (has("cactus|agave|biznaga|nopal|beavertail")) return "Desert";
		if (has("dry_grass|dry_plant|dead_bush")) return "DesertGrass";
		if (has("mushroom")) return "Mushroom";
		if (has("young_.*tree|tabacco_tree")) return "SmallTree";
		if (has("cedrus_tree_var|sweetgum_.*tree_var|oreopanax")) return "BigTree";
		if (has("rock|cliff|boulder|stone")) return "Rock";
		if (has("log|branch|stump|debris|dry_leaves|dead_.*leaves")) return "Debris";
		if (has("fern")) return "Fern";
		if (has("bush|roldana_plant|shrub")) return "Bush";
		if (has("plants_cover|forest_grass|ground_weed|herb|moss_cover|cover_var")) return "ForestCover";
		if (has("grass|weed|clover|dandelion|flower")) return "Grass";
		return "";
	}

	std::vector<std::pair<std::string, std::string>> ScanProjectModels()
	{
		std::vector<std::pair<std::string, std::string>> out;
		const std::filesystem::path root = PathManager::GetI()->GetMovePathW(L"Assets");
		std::error_code ec;
		for (auto it = std::filesystem::recursive_directory_iterator(root, ec); it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
		{
			if (ec || !it->is_regular_file())
				continue;
			std::wstring ext = it->path().extension().wstring();
			std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
			if (ext != L".fbx" && ext != L".glb")
				continue;
			const std::string rel = wstring_to_string(PathManager::GetI()->GetCutSolutionPath(it->path().wstring()));
			const std::string role = ClassifyModel(rel);
			if (!role.empty())
				out.push_back({ role, rel });
		}
		std::sort(out.begin(), out.end());
		return out;
	}
}
