#include "pch.h"
#include "SceneCulling.h"
#include "Scene.h"
#include "MeshRenderer.h"
#include "SkinnedMeshRenderer.h"
#include "SkinnedMesh.h"
#include "Mesh.h"
#include "Transform.h"
#include <unordered_map>
#include "FrameProfiler.h"

namespace
{
	constexpr int kMaxDepth = 10;
	constexpr int kMaxNodes = 1 << 20;

	struct Box
	{
		Vec3 Min, Max;
		Vec3 Center() const { return (Min + Max) * 0.5f; }
		Vec3 Half() const { return (Max - Min) * 0.5f; }
	};

	struct Entry
	{
		Component* Renderer = nullptr;
		Box Bounds;
		XMFLOAT4X4 World;
		const void* MeshKey = nullptr;
		int Node = -1;
		int Slot = -1;          // Node 의 Items 안 위치
		uint32_t Seen = 0;
		bool Alive = false;
	};

	struct Node
	{
		Vec3 Center;
		float Half = 0.0f;      // 칸 반폭 (느슨한 영역 = 2 배)
		int Child[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };
		std::vector<int> Items;
		int Depth = 0;
	};

	std::vector<Entry> s_Entries;
	std::vector<int> s_Free;
	std::unordered_map<Component*, int> s_Map;
	std::vector<Node> s_Nodes;
	uint32_t s_Frame = 0;
	SceneCulling::Stats s_Stats[2];
	bool s_EditorView = false;

	// ---- 메시 로컬 범위 (메시마다 한 번)
	std::unordered_map<const void*, Box> s_MeshBounds;

	bool MeshBox(const Mesh* mesh, Box& out)
	{
		if (auto it = s_MeshBounds.find(mesh); it != s_MeshBounds.end()) { out = it->second; return true; }
		if (mesh->Vertices.empty())
			return false;
		Box b{ Vec3(FLT_MAX, FLT_MAX, FLT_MAX), Vec3(-FLT_MAX, -FLT_MAX, -FLT_MAX) };
		for (const auto& v : mesh->Vertices)
		{
			b.Min = Vec3::Min(b.Min, Vec3(v.pos));
			b.Max = Vec3::Max(b.Max, Vec3(v.pos));
		}
		s_MeshBounds[mesh] = b;
		out = b;
		return true;
	}

	bool SkinnedBox(const SkinnedMesh* mesh, Box& out)
	{
		if (auto it = s_MeshBounds.find(mesh); it != s_MeshBounds.end()) { out = it->second; return true; }
		if (mesh->Vertices.empty())
			return false;
		Box b{ Vec3(FLT_MAX, FLT_MAX, FLT_MAX), Vec3(-FLT_MAX, -FLT_MAX, -FLT_MAX) };
		for (const auto& v : mesh->Vertices)
		{
			b.Min = Vec3::Min(b.Min, Vec3(v.pos));
			b.Max = Vec3::Max(b.Max, Vec3(v.pos));
		}
		// 애니메이션으로 팔다리가 기본 자세 밖으로 나가므로 넉넉하게
		const Vec3 pad = b.Half() * 0.6f + Vec3(0.25f, 0.25f, 0.25f);
		b.Min -= pad;
		b.Max += pad;
		s_MeshBounds[mesh] = b;
		out = b;
		return true;
	}

	// 로컬 상자 → 월드 AABB (중심 + |행렬| · 반폭)
	Box WorldBox(const Box& local, const XMFLOAT4X4& w)
	{
		const Vec3 c = local.Center(), h = local.Half();
		Box out;
		const Vec3 wc(c.x * w._11 + c.y * w._21 + c.z * w._31 + w._41,
			c.x * w._12 + c.y * w._22 + c.z * w._32 + w._42,
			c.x * w._13 + c.y * w._23 + c.z * w._33 + w._43);
		const Vec3 wh(h.x * fabsf(w._11) + h.y * fabsf(w._21) + h.z * fabsf(w._31),
			h.x * fabsf(w._12) + h.y * fabsf(w._22) + h.z * fabsf(w._32),
			h.x * fabsf(w._13) + h.y * fabsf(w._23) + h.z * fabsf(w._33));
		out.Min = wc - wh;
		out.Max = wc + wh;
		return out;
	}

	// ---- 옥트리
	int NewNode(const Vec3& center, float half, int depth)
	{
		Node n;
		n.Center = center;
		n.Half = half;
		n.Depth = depth;
		s_Nodes.push_back(std::move(n));
		return (int)s_Nodes.size() - 1;
	}

	void RemoveFromNode(Entry& e)
	{
		if (e.Node < 0)
			return;
		auto& items = s_Nodes[e.Node].Items;
		const int last = items.back();
		items[e.Slot] = last;
		s_Entries[last].Slot = e.Slot;
		items.pop_back();
		e.Node = e.Slot = -1;
	}

	bool FitsRoot(const Box& b)
	{
		if (s_Nodes.empty())
			return false;
		const Node& r = s_Nodes[0];
		const Vec3 c = b.Center(), h = b.Half();
		const float m = (std::max)(h.x, (std::max)(h.y, h.z));
		return fabsf(c.x - r.Center.x) <= r.Half && fabsf(c.y - r.Center.y) <= r.Half && fabsf(c.z - r.Center.z) <= r.Half && m <= r.Half;
	}

	void InsertIntoTree(int index)
	{
		Entry& e = s_Entries[index];
		const Vec3 c = e.Bounds.Center(), h = e.Bounds.Half();
		const float extent = (std::max)(h.x, (std::max)(h.y, h.z));
		int node = 0;
		// 물체 반폭이 자식 칸 반폭 이하이면 중심이 든 자식으로 (느슨한 영역 = 칸의 2 배라 반드시 들어간다)
		while (s_Nodes[node].Depth < kMaxDepth && extent <= s_Nodes[node].Half * 0.5f && (int)s_Nodes.size() < kMaxNodes)
		{
			const Node& n = s_Nodes[node];
			const int ci = (c.x >= n.Center.x ? 1 : 0) | (c.y >= n.Center.y ? 2 : 0) | (c.z >= n.Center.z ? 4 : 0);
			if (n.Child[ci] < 0)
			{
				const float q = n.Half * 0.5f;
				const Vec3 cc(n.Center.x + (ci & 1 ? q : -q), n.Center.y + (ci & 2 ? q : -q), n.Center.z + (ci & 4 ? q : -q));
				const int depth = n.Depth + 1;
				const int child = NewNode(cc, q, depth);   // push_back 뒤에는 n 참조가 무효 → 다시 찾는다
				s_Nodes[node].Child[ci] = child;
			}
			node = s_Nodes[node].Child[ci];
		}
		e.Node = node;
		e.Slot = (int)s_Nodes[node].Items.size();
		s_Nodes[node].Items.push_back(index);
	}

	// 뿌리를 모든 물체를 덮도록 다시 만든다 (새 물체가 뿌리 밖일 때)
	void Rebuild()
	{
		s_Nodes.clear();
		Vec3 mn(FLT_MAX, FLT_MAX, FLT_MAX), mx(-FLT_MAX, -FLT_MAX, -FLT_MAX);
		for (const Entry& e : s_Entries)
			if (e.Alive)
			{
				mn = Vec3::Min(mn, e.Bounds.Min);
				mx = Vec3::Max(mx, e.Bounds.Max);
			}
		if (mn.x > mx.x)
			mn = mx = Vec3(0, 0, 0);
		const Vec3 center = (mn + mx) * 0.5f;
		const Vec3 half = (mx - mn) * 0.5f;
		float size = 16.0f;
		while (size < (std::max)(half.x, (std::max)(half.y, half.z)) * 1.25f)
			size *= 2.0f;
		NewNode(center, size, 0);
		for (int i = 0; i < (int)s_Entries.size(); ++i)
		{
			s_Entries[i].Node = s_Entries[i].Slot = -1;
			if (s_Entries[i].Alive)
				InsertIntoTree(i);
		}
	}

	// ---- 절두체
	struct CullFrustum
	{
		XMFLOAT4 Planes[6];
		int Count = 6;
	};

	CullFrustum MakeFrustum(CXMMATRIX m, bool shadow)
	{
		XMFLOAT4X4 f;
		XMStoreFloat4x4(&f, m);
		auto col = [&](int c) { return XMVectorSet(f.m[0][c], f.m[1][c], f.m[2][c], f.m[3][c]); };
		const XMVECTOR c0 = col(0), c1 = col(1), c2 = col(2), c3 = col(3);
		const XMVECTOR p[6] = { c3 + c0, c3 - c0, c3 + c1, c3 - c1, c3 - c2, c2 };   // 좌 우 아래 위 먼 가까운
		CullFrustum fr;
		for (int i = 0; i < 6; ++i)
			XMStoreFloat4(&fr.Planes[i], XMPlaneNormalize(p[i]));
		fr.Count = shadow ? 5 : 6;
		return fr;
	}

	// 0 = 밖, 1 = 걸침, 2 = 완전히 안
	int Classify(const CullFrustum& fr, const Vec3& c, const Vec3& h)
	{
		int result = 2;
		for (int i = 0; i < fr.Count; ++i)
		{
			const XMFLOAT4& p = fr.Planes[i];
			const float d = p.x * c.x + p.y * c.y + p.z * c.z + p.w;
			const float r = h.x * fabsf(p.x) + h.y * fabsf(p.y) + h.z * fabsf(p.z);
			if (d < -r)
				return 0;
			if (d < r)
				result = 1;
		}
		return result;
	}

	void Mark(int index)
	{
		s_Entries[index].Renderer->CullStamp = SceneCulling::Stamp;
	}

	void MarkSubtree(int node, int& visible)
	{
		const Node& n = s_Nodes[node];
		for (int i : n.Items) { Mark(i); ++visible; }
		for (int c : n.Child)
			if (c >= 0)
				MarkSubtree(c, visible);
	}

	void Query(const CullFrustum& fr, int node, int& visible, int& visited)
	{
		const Node& n = s_Nodes[node];
		++visited;
		const float loose = n.Half * 2.0f;
		const int cls = Classify(fr, n.Center, Vec3(loose, loose, loose));
		if (cls == 0)
			return;
		if (cls == 2)
		{
			MarkSubtree(node, visible);
			return;
		}
		for (int i : n.Items)
		{
			const Box& b = s_Entries[i].Bounds;
			if (Classify(fr, b.Center(), b.Half()) != 0)
			{
				Mark(i);
				++visible;
			}
		}
		for (int c : n.Child)
			if (c >= 0)
				Query(fr, c, visible, visited);
	}

	std::vector<std::pair<Vec3, Vec3>> s_Changed;

	void Track(Component* renderer, const Box& local, const void* meshKey, const XMFLOAT4X4& world, bool& needRebuild)
	{
		renderer->CullTracked = true;
		int index;
		auto it = s_Map.find(renderer);
		if (it == s_Map.end())
		{
			if (!s_Free.empty()) { index = s_Free.back(); s_Free.pop_back(); }
			else { index = (int)s_Entries.size(); s_Entries.emplace_back(); }
			s_Map[renderer] = index;
			Entry& e = s_Entries[index];
			e = Entry();
			e.Renderer = renderer;
			e.Alive = true;
			e.MeshKey = nullptr;
		}
		else
			index = it->second;
		Entry& e = s_Entries[index];
		e.Seen = s_Frame;
		if (e.MeshKey == meshKey && e.Node >= 0 && memcmp(&e.World, &world, sizeof(world)) == 0)
			return;   // 그대로
		if (e.Node >= 0)
			s_Changed.push_back({ e.Bounds.Min, e.Bounds.Max });   // 예전 자리
		e.MeshKey = meshKey;
		e.World = world;
		e.Bounds = WorldBox(local, world);
		s_Changed.push_back({ e.Bounds.Min, e.Bounds.Max });
		RemoveFromNode(e);
		if (!needRebuild && FitsRoot(e.Bounds))
			InsertIntoTree(index);
		else
			needRebuild = true;
	}
}

namespace SceneCulling
{
	void Update(Scene* scene)
	{
		static const bool s_NoCull = [] { char v[8] = {}; return ::GetEnvironmentVariableA("NOVA_DEV_NOCULL", v, sizeof(v)) > 0 && v[0] == '1'; }();
		Enabled = !s_NoCull;
		++s_Frame;
		s_Changed.clear();
		bool needRebuild = s_Nodes.empty();
		if (scene)
			for (GameObject* go : scene->GetAllGameObjects())
			{
				if (go == nullptr)
					continue;
				Transform* tr = go->GetTransform();
				if (tr == nullptr)
					continue;
				MeshRenderer* mr = go->GetComponent<MeshRenderer>();
				SkinnedMeshRenderer* sr = go->GetComponent<SkinnedMeshRenderer>();
				if (!mr && !sr)
					continue;
				XMFLOAT4X4 world;
				XMStoreFloat4x4(&world, tr->GetWorldMatrix());
				Box local;
				if (mr)
				{
					auto mesh = mr->GetMesh();
					if (mesh && MeshBox(mesh.get(), local))
						Track(mr, local, mesh.get(), world, needRebuild);
					else
						mr->CullTracked = false;
				}
				if (sr)
				{
					auto mesh = sr->GetMesh();
					if (mesh && SkinnedBox(mesh.get(), local))
						Track(sr, local, mesh.get(), world, needRebuild);
					else
						sr->CullTracked = false;
				}
			}
		// 이번 프레임에 못 본 렌더러(지워짐)는 뺀다. 포인터는 이미 해제됐을 수 있으므로 건드리지 않는다
		for (auto it = s_Map.begin(); it != s_Map.end();)
		{
			Entry& e = s_Entries[it->second];
			if (e.Seen == s_Frame)
			{
				++it;
				continue;
			}
			s_Changed.push_back({ e.Bounds.Min, e.Bounds.Max });   // 지워짐
			RemoveFromNode(e);
			e.Alive = false;
			e.Renderer = nullptr;
			s_Free.push_back(it->second);
			it = s_Map.erase(it);
		}
		if (needRebuild)
			Rebuild();
	}

	void SetEditorView(bool editorView) { s_EditorView = editorView; }

	const std::vector<std::pair<Vec3, Vec3>>& ChangedBounds() { return s_Changed; }

	bool TrackedBounds(const Component* renderer, Vec3& mn, Vec3& mx)
	{
		auto it = s_Map.find(const_cast<Component*>(renderer));
		if (it == s_Map.end())
			return false;
		const Entry& e = s_Entries[it->second];
		mn = e.Bounds.Min;
		mx = e.Bounds.Max;
		return true;
	}

	bool TrackedSlot(const Component* renderer, uint32_t& slot, Vec3& mn, Vec3& mx)
	{
		auto it = s_Map.find(const_cast<Component*>(renderer));
		if (it == s_Map.end())
			return false;
		const Entry& e = s_Entries[it->second];
		slot = (uint32_t)it->second;
		mn = e.Bounds.Min;
		mx = e.Bounds.Max;
		return true;
	}

	uint32_t SlotCount() { return (uint32_t)s_Entries.size(); }

	void Cull(CXMMATRIX viewProj, bool shadowPass)
	{
		PROFILE_SCOPE("Culling");
		++Stamp;
		ShadowPass = shadowPass;
		if (s_Nodes.empty())
			return;
		const CullFrustum fr = MakeFrustum(viewProj, shadowPass);
		int visible = 0, visited = 0;
		Query(fr, 0, visible, visited);
		if (!shadowPass)
		{
			Stats& st = s_Stats[s_EditorView ? 1 : 0];
			st.Objects = (int)s_Map.size();
			st.Visible = visible;
			st.NodesVisited = visited;
			st.Nodes = (int)s_Nodes.size();
			int depth = 0;
			for (const Node& n : s_Nodes)
				depth = (std::max)(depth, n.Depth);
			st.Depth = depth;
			// 측정용 (NOVA_DEV_PROFILE=1): 3 초마다 카메라 컬링 결과
			static auto s_Last = std::chrono::steady_clock::now();
			if (FrameProfiler::Enabled() && std::chrono::steady_clock::now() - s_Last > std::chrono::seconds(3))
			{
				s_Last = std::chrono::steady_clock::now();
				EditorLog::Write("Culling", "%s view: %d / %d renderers visible, octree %d nodes (visited %d), depth %d",
					s_EditorView ? "Scene" : "Game", st.Visible, st.Objects, st.Nodes, st.NodesVisited, st.Depth);
			}
		}
	}

	const Stats& LastStats(bool editorView) { return s_Stats[editorView ? 1 : 0]; }
	uint32_t FrameIndex() { return s_Frame; }
}
