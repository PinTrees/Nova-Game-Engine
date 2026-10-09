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
#include "JobSystem.h"
#include "TransformStore.h"
#include <atomic>
#include <mutex>

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
		Box Local;                          // 메시 (렌더러) 의 로컬 상자 — 움직이기만 했으면 메시를 다시 찾지 않고 이것으로
		const void* MeshKey = nullptr;
		const Transform* Tr = nullptr;      // 마지막으로 본 Transform 과 그 월드 번호 — 같으면 행렬을 읽지도 않는다
		uint32_t TrSlot = UINT32_MAX;
		uint32_t TrVersion = 0;
		int Node = -1;
		int Slot = -1;          // Node 의 Items 안 위치
		uint32_t Seen = 0;
		bool Alive = false;
		bool Skinned = false;   // Skinned Mesh Renderer (오클루전 쿼리 대상)
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
	// 자리마다 월드 상자의 중심 · 반폭 (SoA) — 절두체 검사가 4 개씩 SIMD 로 읽는다
	std::vector<float> s_CX, s_CY, s_CZ, s_HX, s_HY, s_HZ;
	std::vector<int> s_Free;
	std::unordered_map<Component*, int> s_Map;
	std::vector<Node> s_Nodes;
	uint32_t s_Frame = 0;
	SceneCulling::Stats s_Stats[2];
	bool s_EditorView = false;

	// ---- 렌더러 목록 (Mesh Renderer · Skinned Mesh Renderer 가 생성자 · 소멸자에서 들어오고 나간다)
	//  프레임마다 모든 GameObject 에서 렌더러를 찾던 것 (GetComponent 두 번) 대신 이 목록만 돈다
	std::mutex s_RegLock;
	std::vector<Component*> s_Renderers;
	std::vector<uint8_t> s_RendererSkinned;
	uint32_t s_LastBinding = 0;     // 지난 Update 의 Component::s_BindingSerial (메시 · 컴포넌트 연결이 바뀌었나)
	size_t s_VerifyCursor = 0;      // 돌아가며 메시를 실제로 확인하는 자리 (번호를 올리지 않고 바뀐 메시도 1 ~ 2 초 안에 바로잡는다)
	uint64_t s_FastSkips = 0, s_Updated = 0, s_VerifyFixes = 0;
	size_t s_TrackedThisFrame = 0;  // 이번 Update 의 2) 에서 자리를 갱신한 렌더러 수 (1) 의 표시와 합쳐 추적 수와 같으면 3) 을 건너뛴다)

	// ---- 메시 로컬 범위 (메시마다 한 번)
	std::unordered_map<const void*, Box> s_MeshBounds;

	// 바로 앞 메시 (같은 메시를 쓰는 오브젝트가 이어지면 해시 찾기 없이)
	const void* s_LastMesh = nullptr;
	Box s_LastBox;

	bool MeshBox(const Mesh* mesh, Box& out)
	{
		if (mesh == s_LastMesh) { out = s_LastBox; return true; }
		if (auto it = s_MeshBounds.find(mesh); it != s_MeshBounds.end()) { out = s_LastBox = it->second; s_LastMesh = mesh; return true; }
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

	void SetBounds(int index, const Box& b)
	{
		s_Entries[(size_t)index].Bounds = b;
		const Vec3 c = b.Center(), h = b.Half();
		s_CX[(size_t)index] = c.x; s_CY[(size_t)index] = c.y; s_CZ[(size_t)index] = c.z;
		s_HX[(size_t)index] = h.x; s_HY[(size_t)index] = h.y; s_HZ[(size_t)index] = h.z;
	}

	int NewEntry()
	{
		if (!s_Free.empty())
		{
			const int i = s_Free.back();
			s_Free.pop_back();
			return i;
		}
		s_Entries.emplace_back();
		for (auto* v : { &s_CX, &s_CY, &s_CZ, &s_HX, &s_HY, &s_HZ })
			v->push_back(0.0f);
		return (int)s_Entries.size() - 1;
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

	// 상자가 지금 노드에 그대로 들어가는가 (중심이 칸 안 + 반폭 ≤ 칸 반폭 → 느슨한 영역 (2 배) 안). 그러면 다시 넣지 않는다
	//  (더 깊은 칸에 들어갈 수도 있지만 검사는 여전히 맞다 — 움직이는 물체마다 뿌리부터 다시 내려가지 않게)
	bool FitsNode(int node, const Box& b)
	{
		const Node& n = s_Nodes[(size_t)node];
		const Vec3 c = b.Center(), h = b.Half();
		const float m = (std::max)(h.x, (std::max)(h.y, h.z));
		return m <= n.Half && fabsf(c.x - n.Center.x) <= n.Half && fabsf(c.y - n.Center.y) <= n.Half && fabsf(c.z - n.Center.z) <= n.Half;
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
		// SIMD (상자 4 개씩): 평면 성분 · 절댓값을 4 칸에 복제
		XMVECTOR PX[6], PY[6], PZ[6], PW[6], AX[6], AY[6], AZ[6];
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
		for (int i = 0; i < 6; ++i)
		{
			const XMFLOAT4& p = fr.Planes[i];
			fr.PX[i] = XMVectorReplicate(p.x); fr.PY[i] = XMVectorReplicate(p.y); fr.PZ[i] = XMVectorReplicate(p.z); fr.PW[i] = XMVectorReplicate(p.w);
			fr.AX[i] = XMVectorReplicate(fabsf(p.x)); fr.AY[i] = XMVectorReplicate(fabsf(p.y)); fr.AZ[i] = XMVectorReplicate(fabsf(p.z));
		}
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

	bool s_MarkShadow = false;   // 이번 Cull 이 그림자 — 표시할 칸 (잡도 읽기만)
	void Mark(int index)
	{
		Component* r = s_Entries[index].Renderer;
		if (s_MarkShadow)
			r->ShadowCullStamp = SceneCulling::ShadowStamp;
		else
			r->CullStamp = SceneCulling::Stamp;
	}

	void MarkSubtree(int node, int& visible)
	{
		const Node& n = s_Nodes[node];
		for (int i : n.Items) { Mark(i); ++visible; }
		for (int c : n.Child)
			if (c >= 0)
				MarkSubtree(c, visible);
	}

	// 칸의 물체들: 상자 4 개를 한 번에 (SoA 중심 · 반폭 → 평면마다 d = p·c + w, r = |p|·h, d < -r 이면 밖)
	void TestItems(const CullFrustum& fr, const std::vector<int>& items, int& visible)
	{
		const size_t n = items.size();
		size_t k = 0;
		for (; k + 4 <= n; k += 4)
		{
			const int i0 = items[k], i1 = items[k + 1], i2 = items[k + 2], i3 = items[k + 3];
			const XMVECTOR cx = XMVectorSet(s_CX[i0], s_CX[i1], s_CX[i2], s_CX[i3]);
			const XMVECTOR cy = XMVectorSet(s_CY[i0], s_CY[i1], s_CY[i2], s_CY[i3]);
			const XMVECTOR cz = XMVectorSet(s_CZ[i0], s_CZ[i1], s_CZ[i2], s_CZ[i3]);
			const XMVECTOR hx = XMVectorSet(s_HX[i0], s_HX[i1], s_HX[i2], s_HX[i3]);
			const XMVECTOR hy = XMVectorSet(s_HY[i0], s_HY[i1], s_HY[i2], s_HY[i3]);
			const XMVECTOR hz = XMVectorSet(s_HZ[i0], s_HZ[i1], s_HZ[i2], s_HZ[i3]);
			XMVECTOR outside = XMVectorFalseInt();
			for (int p = 0; p < fr.Count; ++p)
			{
				const XMVECTOR d = XMVectorMultiplyAdd(cx, fr.PX[p], XMVectorMultiplyAdd(cy, fr.PY[p], XMVectorMultiplyAdd(cz, fr.PZ[p], fr.PW[p])));
				const XMVECTOR r = XMVectorMultiplyAdd(hx, fr.AX[p], XMVectorMultiplyAdd(hy, fr.AY[p], XMVectorMultiply(hz, fr.AZ[p])));
				outside = XMVectorOrInt(outside, XMVectorLess(d, XMVectorNegate(r)));
			}
			XMUINT4 lanes;
			XMStoreUInt4(&lanes, outside);
			if (!lanes.x) { Mark(i0); ++visible; }
			if (!lanes.y) { Mark(i1); ++visible; }
			if (!lanes.z) { Mark(i2); ++visible; }
			if (!lanes.w) { Mark(i3); ++visible; }
		}
		for (; k < n; ++k)
		{
			const int i = items[k];
			if (Classify(fr, Vec3(s_CX[i], s_CY[i], s_CZ[i]), Vec3(s_HX[i], s_HY[i], s_HZ[i])) != 0)
			{
				Mark(i);
				++visible;
			}
		}
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
		TestItems(fr, n.Items, visible);
		for (int c : n.Child)
			if (c >= 0)
				Query(fr, c, visible, visited);
	}

	// 큰 장면: 위쪽 칸을 펼쳐 겹치지 않는 하위 나무 여럿으로 나눈 뒤 잡으로 (물체마다 다른 렌더러의 CullStamp 만 쓴다 — 경쟁 없음)
	void QueryParallel(const CullFrustum& fr, int& visible, int& visited)
	{
		static std::vector<int> s_Frontier, s_Next;
		s_Frontier.assign(1, 0);
		for (int level = 0; level < 2 && !s_Frontier.empty(); ++level)
		{
			s_Next.clear();
			for (const int node : s_Frontier)
			{
				const Node& n = s_Nodes[(size_t)node];
				++visited;
				const float loose = n.Half * 2.0f;
				const int cls = Classify(fr, n.Center, Vec3(loose, loose, loose));
				if (cls == 0)
					continue;
				if (cls == 2)
				{
					MarkSubtree(node, visible);
					continue;
				}
				TestItems(fr, n.Items, visible);
				for (const int c : n.Child)
					if (c >= 0)
						s_Next.push_back(c);
			}
			s_Frontier.swap(s_Next);
		}
		std::atomic<int> vis{ 0 }, vit{ 0 };
		Jobs::ParallelFor((int)s_Frontier.size(), 1, [&](int b, int e) {
			int v = 0, t = 0;
			for (int k = b; k < e; ++k)
				Query(fr, s_Frontier[(size_t)k], v, t);
			vis.fetch_add(v, std::memory_order_relaxed);
			vit.fetch_add(t, std::memory_order_relaxed);
		}, "Culling Query");
		visible += vis.load();
		visited += vit.load();
	}

	std::vector<std::pair<Vec3, Vec3>> s_Changed;

	// 이 파일의 정적 값이 먼저 사라진 뒤 (프로그램 끝) 렌더러 소멸자가 목록을 만지지 않게 — 위의 정적 값들보다 뒤에 선언 (먼저 소멸)
	struct ShutdownGuard { ~ShutdownGuard() { s_Dead = true; } static inline bool s_Dead = false; } s_Guard;

	// 지난 프레임에 추적한 렌더러의 자리 (자리 번호로 바로, 아니면 해시) — 없으면 -1
	int FindEntry(const Component* renderer)
	{
		const uint32_t slot = renderer->CullSlot;
		if (renderer->CullTracked && slot < s_Entries.size() && s_Entries[slot].Alive && s_Entries[slot].Renderer == renderer)
			return (int)slot;
		auto it = s_Map.find(const_cast<Component*>(renderer));
		return it == s_Map.end() ? -1 : it->second;
	}

	void Untrack(int index)
	{
		Entry& e = s_Entries[(size_t)index];
		if (!e.Alive)
			return;
		if (e.Node >= 0)
			s_Changed.push_back({ e.Bounds.Min, e.Bounds.Max });   // 사라진 자리
		RemoveFromNode(e);
		if (e.Renderer)
		{
			e.Renderer->CullTracked = false;
			s_Map.erase(e.Renderer);
		}
		e.Alive = false;
		e.Renderer = nullptr;
		e.Tr = nullptr;
		s_Free.push_back(index);
	}

	// 자리의 새 월드 상자 (같으면 아무것도) — 옥트리: 지금 칸에 그대로 들어가면 두고, 아니면 다시 넣는다
	void Place(int index, const Box& nb, bool& needRebuild)
	{
		Entry& e = s_Entries[(size_t)index];
		if (e.Node >= 0 && memcmp(&e.Bounds, &nb, sizeof(Box)) == 0)
			return;   // 그대로 (번호만 바뀌었다 — 같은 값으로 다시 놓음 등)
		if (e.Node >= 0)
			s_Changed.push_back({ e.Bounds.Min, e.Bounds.Max });   // 예전 자리
		SetBounds(index, nb);
		s_Changed.push_back({ nb.Min, nb.Max });
		++s_Updated;
		if (e.Node >= 0 && !needRebuild && FitsNode(e.Node, nb))
			return;   // 같은 칸 (느슨한 영역 안) — 다시 넣지 않는다
		RemoveFromNode(e);
		if (!needRebuild && FitsRoot(nb))
			InsertIntoTree(index);
		else
			needRebuild = true;
	}

	// 이번 프레임의 렌더러 상태를 자리에 (차례로 — 옥트리를 바꾼다)
	void Track(Component* renderer, const Box& local, const void* meshKey, const XMFLOAT4X4& world, const Transform* tr, bool& needRebuild, bool skinned = false)
	{
		int index = FindEntry(renderer);
		if (index < 0)
		{
			index = NewEntry();
			s_Map[renderer] = index;
			Entry& e = s_Entries[(size_t)index];
			e = Entry();
			e.Renderer = renderer;
			e.Alive = true;
		}
		renderer->CullTracked = true;
		renderer->CullSlot = (uint32_t)index;
		Entry& e = s_Entries[(size_t)index];
		if (e.Seen != s_Frame)
			++s_TrackedThisFrame;
		e.Seen = s_Frame;
		e.Skinned = skinned;
		e.Tr = tr;
		e.TrSlot = tr->Slot();
		e.TrVersion = tr->WorldVersion();
		e.MeshKey = meshKey;
		e.Local = local;
		Place(index, WorldBox(local, world), needRebuild);
	}

	// 렌더러마다 이번 프레임에 할 일 (병렬로 정하고 차례로 적용)
	enum class Action : uint8_t { None, Leave, Seen, Moved, Mesh, Skinned };
	struct Work
	{
		Action Do = Action::None;
		bool Verify = false;
		const Mesh* MeshPtr = nullptr;
		Transform* Tr = nullptr;
		Box Bounds;   // Moved: 잡이 계산한 새 월드 상자
	};
	std::vector<Work> s_Work;
}

namespace SceneCulling
{
	void RegisterRenderer(Component* renderer, bool skinned)
	{
		if (ShutdownGuard::s_Dead)
			return;
		std::lock_guard<std::mutex> lock(s_RegLock);
		renderer->CullReg = (int32_t)s_Renderers.size();
		s_Renderers.push_back(renderer);
		s_RendererSkinned.push_back(skinned ? 1 : 0);
	}

	void UnregisterRenderer(Component* renderer)
	{
		if (ShutdownGuard::s_Dead)
			return;
		std::lock_guard<std::mutex> lock(s_RegLock);
		// 추적 중이면 옥트리에서 바로 뺀다 (지워진 렌더러의 포인터가 남지 않게)
		const int index = FindEntry(renderer);
		if (index >= 0)
			Untrack(index);
		const int32_t i = renderer->CullReg;
		if (i < 0 || (size_t)i >= s_Renderers.size() || s_Renderers[(size_t)i] != renderer)
			return;
		const size_t last = s_Renderers.size() - 1;
		s_Renderers[(size_t)i] = s_Renderers[last];
		s_RendererSkinned[(size_t)i] = s_RendererSkinned[last];
		s_Renderers[(size_t)i]->CullReg = i;
		s_Renderers.pop_back();
		s_RendererSkinned.pop_back();
		renderer->CullReg = -1;
	}

	void Update(Scene* scene)
	{
		static const bool s_NoCull = [] { char v[8] = {}; return ::GetEnvironmentVariableA("NOVA_DEV_NOCULL", v, sizeof(v)) > 0 && v[0] == '1'; }();
		Enabled = !s_NoCull;
		// 이번 프레임에 바뀐 Transform 계층을 한 번에 (잡으로 나눠) — 아래 병렬 읽기 · 그리기가 계산 없이 배열을 읽는다
		TransformStore::Flush();
		++s_Frame;
		s_Changed.clear();
		bool needRebuild = s_Nodes.empty();

		// 0) 이 씬의 오브젝트에 이번 프레임 번호 (등록된 렌더러가 이 씬 것인지 — 프리팹 · 다른 씬의 렌더러는 넣지 않는다)
		if (scene)
		{
			const std::vector<GameObject*>& gos = scene->GameObjectsView();
			const uint32_t frame = s_Frame;
			Jobs::ParallelFor((int)gos.size(), 4096, [&](int b, int e) {
				for (int i = b; i < e; ++i)
					if (GameObject* go = gos[(size_t)i])
						go->CullSceneStamp = frame;
			}, "Culling Scene Stamp");
		}

		std::lock_guard<std::mutex> lock(s_RegLock);
		// 메시 · 컴포넌트 연결이 바뀌었으면 (MeshFilter · SetMesh · 컴포넌트 붙이기) 모든 Mesh Renderer 의 메시를 다시 본다
		const uint32_t binding = Component::s_BindingSerial;
		const bool bindingChanged = binding != s_LastBinding;
		s_LastBinding = binding;
		const size_t count = s_Renderers.size();
		s_Work.resize(count);
		// 돌아가며 512 개씩은 메시를 실제로 확인한다 (번호를 올리지 않는 경로가 있어도 1 ~ 2 초 안에 바로잡힌다)
		const size_t verifyCount = (std::min)(count, (size_t)512);
		const size_t verifyBegin = count ? s_VerifyCursor % count : 0;
		s_VerifyCursor = verifyBegin + verifyCount;
		const uint32_t frame = s_Frame;
		std::atomic<size_t> seenInJobs{ 0 };

		// 1) 병렬 (옥트리는 읽기만): 렌더러마다 할 일. 바뀌지 않은 것 (같은 Transform · 같은 월드 번호) 은 여기서 표시까지 끝
		//  (자리의 Seen 은 렌더러마다 다른 자리라 잡이 바로 써도 된다)
		Jobs::ParallelFor((int)count, 512, [&](int b, int e) {
			size_t seen = 0;
			for (int i = b; i < e; ++i)
			{
				Work& w = s_Work[(size_t)i];
				w = Work();
				Component* r = s_Renderers[(size_t)i];
				GameObject* go = r->GetGameObject();
				Transform* tr = go ? go->GetTransform() : nullptr;
				if (!scene || !go || go->CullSceneStamp != frame || !tr)
				{
					w.Do = Action::Leave;
					continue;
				}
				if (s_RendererSkinned[(size_t)i])
				{
					w.Do = Action::Skinned;   // 스킨 메시는 2) 에서 (드물다)
					w.Tr = tr;
					continue;
				}
				const size_t offset = ((size_t)i + count - verifyBegin) % (count ? count : 1);
				w.Verify = offset < verifyCount;
				if (!bindingChanged && !w.Verify && r->CullTracked)
				{
					const uint32_t slot = r->CullSlot;
					if (slot < s_Entries.size())
					{
						const Entry& en = s_Entries[slot];
						if (en.Alive && en.Renderer == r && en.Tr == tr && en.TrSlot == tr->Slot() && en.Node >= 0 && !TransformStore::IsDirty(en.TrSlot))
						{
							if (en.TrVersion == tr->WorldVersion())
							{
								w.Do = Action::Seen;   // 그대로
								const_cast<Entry&>(en).Seen = frame;
								++seen;
								continue;
							}
							// 움직이기만 했다 (메시 · 연결 그대로): 로컬 상자 + 새 월드 → 새 상자를 여기서 (병렬)
							w.Do = Action::Moved;
							w.Tr = tr;
							w.Bounds = WorldBox(en.Local, TransformStore::WorldRef(en.TrSlot));   // 깨끗하다 — 배열을 읽기만
							continue;
						}
					}
				}
				// 바뀌었거나 확인할 차례: 메시 (MeshFilter 동기화 — 이 렌더러만 쓴다)
				w.Do = Action::Mesh;
				w.Tr = tr;
				w.MeshPtr = static_cast<MeshRenderer*>(r)->GetMesh().get();   // 렌더러가 메시를 잡고 있다
			}
			seenInJobs.fetch_add(seen, std::memory_order_relaxed);
		}, "Culling Gather");

		// 2) 차례로: 자리 · 옥트리
		uint64_t fast = 0;
		s_TrackedThisFrame = 0;
		for (size_t i = 0; i < count; ++i)
		{
			const Work& w = s_Work[i];
			Component* r = s_Renderers[i];
			switch (w.Do)
			{
			case Action::Leave:
				if (r->CullTracked)
					if (const int index = FindEntry(r); index >= 0)
						Untrack(index);
				break;
			case Action::Seen:
				break;   // 1) 이 이미 표시했다
			case Action::Moved:
			{
				Entry& e = s_Entries[r->CullSlot];
				if (e.Seen != s_Frame)
					++s_TrackedThisFrame;
				e.Seen = s_Frame;
				e.TrVersion = w.Tr->WorldVersion();
				Place((int)r->CullSlot, w.Bounds, needRebuild);
				break;
			}
			case Action::Mesh:
			{
				Box local;
				if (w.MeshPtr && MeshBox(w.MeshPtr, local))
				{
#ifdef _DEBUG
					// 연결 번호를 올리지 않고 메시가 바뀐 경로 (확인 차례에 찾았다) — 한 번 알린다
					if (w.Verify && !bindingChanged && r->CullTracked)
						if (const int index = FindEntry(r); index >= 0 && s_Entries[(size_t)index].MeshKey != w.MeshPtr)
						{
							static bool s_Logged = false;
							if (!s_Logged)
							{
								s_Logged = true;
								EditorLog::Write("Culling", "%s", "mesh changed without Component::s_BindingSerial (fixed by the rotating check) - add the bump to that code path");
							}
							++s_VerifyFixes;
						}
#endif
					Track(r, local, w.MeshPtr, TransformStore::WorldRef(w.Tr->Slot()), w.Tr, needRebuild);
				}
				else if (r->CullTracked)
				{
					if (const int index = FindEntry(r); index >= 0)
						Untrack(index);
				}
				break;
			}
			case Action::Skinned:
			{
				SkinnedMeshRenderer* sr = static_cast<SkinnedMeshRenderer*>(r);
				auto mesh = sr->GetMesh();
				const XMFLOAT4X4& world = TransformStore::WorldRef(w.Tr->Slot());
				Box local;
				// 렌더러의 바운드 (메시 노드 · 단위 변환 반영) — 정점 그대로는 FBX 의 cm 단위라 100 배 큰 상자가 된다
				//  애니메이션으로 팔다리가 기본 자세 밖으로 나가므로 넉넉하게 (SkinnedBox 와 같은 여유)
				Vec3 bc, be;
				if (mesh && sr->LocalBounds(bc, be))
				{
					const Vec3 pad = be * 0.6f + Vec3(0.25f, 0.25f, 0.25f);
					local = Box{ bc - be - pad, bc + be + pad };
					Track(sr, local, mesh.get(), world, w.Tr, needRebuild, true);
				}
				else if (mesh && SkinnedBox(mesh.get(), local))
					Track(sr, local, mesh.get(), world, w.Tr, needRebuild, true);
				else if (sr->CullTracked)
				{
					if (const int index = FindEntry(sr); index >= 0)
						Untrack(index);
				}
				break;
			}
			default:
				break;
			}
		}
		fast = seenInJobs.load();
		s_FastSkips += fast;
		// 3) 이번 프레임에 못 본 자리는 뺀다 (보통 없다 — 지워진 렌더러는 소멸자가, 씬을 떠난 렌더러는 위의 Leave 가 이미 뺐다)
		//  추적 중인 수만큼 다 봤으면 (보통) 훑지 않는다
		if (fast + s_TrackedThisFrame != s_Map.size())
			for (size_t i = 0; i < s_Entries.size(); ++i)
				if (s_Entries[i].Alive && s_Entries[i].Seen != s_Frame)
					Untrack((int)i);
		if (needRebuild)
			Rebuild();
	}

	size_t EntryCount() { return s_Entries.size(); }

	bool EntryAt(size_t index, EntryView& out)
	{
		if (index >= s_Entries.size())
			return false;
		const Entry& e = s_Entries[index];
		if (!e.Alive || !e.Renderer)
			return false;
		out.Renderer = e.Renderer;
		out.TrSlot = e.TrSlot;
		out.TrVersion = e.TrVersion;
		out.Skinned = e.Skinned;
		return true;
	}

	nlohmann::json Info()
	{
		return { { "renderers", s_Renderers.size() }, { "tracked", s_Map.size() }, { "nodes", s_Nodes.size() }, { "fastSkips", s_FastSkips },
			{ "updated", s_Updated }, { "verifyFixes", s_VerifyFixes }, { "bindingSerial", Component::s_BindingSerial } };
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

	void ForEachSkinned(const std::function<void(Component*, const Vec3&, const Vec3&)>& f)
	{
		for (const Entry& e : s_Entries)
			if (e.Alive && e.Skinned && e.Renderer)
				f(e.Renderer, e.Bounds.Min, e.Bounds.Max);
	}

	bool SlotBounds(uint32_t slot, Vec3& mn, Vec3& mx)
	{
		if (slot >= s_Entries.size() || !s_Entries[slot].Alive)
			return false;
		mn = s_Entries[slot].Bounds.Min;
		mx = s_Entries[slot].Bounds.Max;
		return true;
	}

	void Cull(CXMMATRIX viewProj, bool shadowPass)
	{
		PROFILE_SCOPE("Culling");
		if (shadowPass)
			++ShadowStamp;
		else
			++Stamp;
		ShadowPass = shadowPass;
		s_MarkShadow = shadowPass;
		if (s_Nodes.empty())
			return;
		const CullFrustum fr = MakeFrustum(viewProj, shadowPass);
		int visible = 0, visited = 0;
		if (s_Map.size() >= 2048 && !Jobs::Inline())
			QueryParallel(fr, visible, visited);   // 큰 장면: 하위 나무를 잡으로 나눠
		else
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
