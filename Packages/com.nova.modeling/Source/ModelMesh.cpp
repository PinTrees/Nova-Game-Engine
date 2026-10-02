#include "pch.h"
#include "ModelMesh.h"
#include <array>
#include <numeric>
#include <queue>

namespace Modeling
{
	namespace
	{
		constexpr float kPi = 3.14159265358979f;

		Vec3 Newell(const std::vector<Vert>& verts, const std::vector<int>& idx)
		{
			Vec3 n(0, 0, 0);
			const size_t c = idx.size();
			for (size_t i = 0; i < c; ++i)
			{
				const Vec3& a = verts[idx[i]].P;
				const Vec3& b = verts[idx[(i + 1) % c]].P;
				n.x += (a.y - b.y) * (a.z + b.z);
				n.y += (a.z - b.z) * (a.x + b.x);
				n.z += (a.x - b.x) * (a.y + b.y);
			}
			const float len = n.Length();
			return len > 1e-12f ? n / len : Vec3(0, 1, 0);
		}

		int Index(std::vector<Vert>& verts, const Vec3& p)
		{
			verts.push_back({ p, true });
			return (int)verts.size() - 1;
		}

		void Basis(const Vec3& n, Vec3& u, Vec3& v)
		{
			u = fabsf(n.x) < 0.9f ? Vec3(1, 0, 0).Cross(n) : Vec3(0, 1, 0).Cross(n);
			u.Normalize();
			v = n.Cross(u);
		}
	}

	// ------------------------------------------------------------------ 점 보간 · 버텍스 그룹
	Vert MixVert(const Vert& a, const Vert& b, float t)
	{
		Vert v;
		v.P = a.P + (b.P - a.P) * t;
		v.Sel = true;
		for (const auto& [g, w] : a.W) v.W.push_back({ g, w * (1.0f - t) });
		for (const auto& [g, w] : b.W)
		{
			auto it = std::find_if(v.W.begin(), v.W.end(), [&](const auto& x) { return x.first == g; });
			if (it != v.W.end()) it->second += w * t;
			else v.W.push_back({ g, w * t });
		}
		v.W.erase(std::remove_if(v.W.begin(), v.W.end(), [](const auto& x) { return x.second <= 1e-6f; }), v.W.end());
		return v;
	}

	Vert AverageVert(const std::vector<Vert>& verts, const std::vector<int>& ids)
	{
		Vert v;
		v.Sel = true;
		if (ids.empty()) return v;
		const float inv = 1.0f / ids.size();
		for (int i : ids)
		{
			v.P += verts[i].P * inv;
			for (const auto& [g, w] : verts[i].W)
			{
				auto it = std::find_if(v.W.begin(), v.W.end(), [&](const auto& x) { return x.first == g; });
				if (it != v.W.end()) it->second += w * inv;
				else v.W.push_back({ g, w * inv });
			}
		}
		return v;
	}

	int Mesh::FindGroup(const std::string& name) const
	{
		for (int i = 0; i < (int)Groups.size(); ++i) if (Groups[i] == name) return i;
		return -1;
	}

	int Mesh::AddGroup(const std::string& name)
	{
		const int g = FindGroup(name);
		if (g >= 0) return g;
		Groups.push_back(name);
		return (int)Groups.size() - 1;
	}

	int Mesh::AssignGroup(int group, float weight)
	{
		int n = 0;
		for (Vert& v : Verts)
		{
			if (!v.Sel) continue;
			auto it = std::find_if(v.W.begin(), v.W.end(), [&](const auto& x) { return x.first == group; });
			if (it != v.W.end()) it->second = weight;
			else v.W.push_back({ group, weight });
			++n;
		}
		return n;
	}

	int Mesh::RemoveFromGroup(int group)
	{
		int n = 0;
		for (Vert& v : Verts)
			if (v.Sel)
			{
				const size_t before = v.W.size();
				v.W.erase(std::remove_if(v.W.begin(), v.W.end(), [&](const auto& x) { return x.first == group; }), v.W.end());
				n += before != v.W.size();
			}
		return n;
	}

	int Mesh::SelectGroup(int group, bool select)
	{
		int n = 0;
		for (Vert& v : Verts)
			if (Weight((int)(&v - Verts.data()), group) > 0.0f) { v.Sel = select; ++n; }
		Flush(SelectMode::Vertex);
		return n;
	}

	void Mesh::DeleteGroup(int group)
	{
		if (group < 0 || group >= (int)Groups.size()) return;
		Groups.erase(Groups.begin() + group);
		for (Vert& v : Verts)
		{
			v.W.erase(std::remove_if(v.W.begin(), v.W.end(), [&](const auto& x) { return x.first == group; }), v.W.end());
			for (auto& x : v.W) if (x.first > group) --x.first;
		}
	}

	int Mesh::GroupCount(int group) const
	{
		int n = 0;
		for (const Vert& v : Verts) for (const auto& x : v.W) if (x.first == group && x.second > 0.0f) { ++n; break; }
		return n;
	}

	float Mesh::Weight(int vert, int group) const
	{
		for (const auto& x : Verts[vert].W) if (x.first == group) return x.second;
		return 0.0f;
	}

	// ------------------------------------------------------------------ 변
	const std::vector<Edge>& Mesh::Edges()
	{
		if (!m_EdgesDirty)
			return m_Edges;
		m_Edges.clear();
		m_EdgeIndex.clear();
		for (int f = 0; f < (int)Faces.size(); ++f)
		{
			const auto& v = Faces[f].V;
			for (size_t i = 0; i < v.size(); ++i)
			{
				const int a = v[i], b = v[(i + 1) % v.size()];
				const uint64 key = EdgeKey(a, b);
				auto it = m_EdgeIndex.find(key);
				if (it == m_EdgeIndex.end())
				{
					it = m_EdgeIndex.emplace(key, (int)m_Edges.size()).first;
					m_Edges.push_back({ (std::min)(a, b), (std::max)(a, b), {} });
				}
				m_Edges[it->second].Faces.push_back(f);
			}
		}
		m_EdgesDirty = false;
		return m_Edges;
	}

	int Mesh::FindEdge(int a, int b)
	{
		Edges();
		auto it = m_EdgeIndex.find(EdgeKey(a, b));
		return it == m_EdgeIndex.end() ? -1 : it->second;
	}

	// ------------------------------------------------------------------ 기하
	Vec3 Mesh::FaceNormal(int f) const { return Newell(Verts, Faces[f].V); }

	Vec3 Mesh::FaceCenter(int f) const
	{
		Vec3 c(0, 0, 0);
		for (int i : Faces[f].V) c += Verts[i].P;
		return Faces[f].V.empty() ? c : c / (float)Faces[f].V.size();
	}

	void Mesh::Bounds(Vec3& mn, Vec3& mx) const
	{
		mn = Vec3(FLT_MAX, FLT_MAX, FLT_MAX);
		mx = Vec3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
		for (const Vert& v : Verts) { mn = Vec3::Min(mn, v.P); mx = Vec3::Max(mx, v.P); }
		if (Verts.empty()) mn = mx = Vec3(0, 0, 0);
	}

	// ------------------------------------------------------------------ 선택
	void Mesh::DeselectAll()
	{
		for (Vert& v : Verts) v.Sel = false;
		for (Face& f : Faces) f.Sel = false;
		SelEdges.clear();
	}

	void Mesh::SelectAll(bool on)
	{
		if (!on) { DeselectAll(); return; }
		for (Vert& v : Verts) v.Sel = true;
		for (Face& f : Faces) f.Sel = true;
		SelEdges.clear();
		for (const Edge& e : Edges()) SelEdges.insert(EdgeKey(e.A, e.B));
	}

	void Mesh::InvertSelection(SelectMode mode)
	{
		if (mode == SelectMode::Face)
		{
			for (Face& f : Faces) f.Sel = !f.Sel;
		}
		else if (mode == SelectMode::Edge)
		{
			std::unordered_set<uint64> inv;
			for (const Edge& e : Edges())
				if (!SelEdges.count(EdgeKey(e.A, e.B))) inv.insert(EdgeKey(e.A, e.B));
			SelEdges = inv;
		}
		else
			for (Vert& v : Verts) v.Sel = !v.Sel;
		Flush(mode);
	}

	void Mesh::Flush(SelectMode mode)
	{
		const auto& edges = Edges();
		if (mode == SelectMode::Vertex)
		{
			SelEdges.clear();
			for (const Edge& e : edges)
				if (Verts[e.A].Sel && Verts[e.B].Sel) SelEdges.insert(EdgeKey(e.A, e.B));
			for (Face& f : Faces)
			{
				f.Sel = !f.V.empty();
				for (int i : f.V) f.Sel = f.Sel && Verts[i].Sel;
			}
		}
		else if (mode == SelectMode::Edge)
		{
			for (Vert& v : Verts) v.Sel = false;
			for (uint64 k : SelEdges) { Verts[(int)(k >> 32)].Sel = true; Verts[(int)(k & 0xFFFFFFFF)].Sel = true; }
			for (Face& f : Faces)
			{
				f.Sel = !f.V.empty();
				for (size_t i = 0; i < f.V.size() && f.Sel; ++i)
					f.Sel = SelEdges.count(EdgeKey(f.V[i], f.V[(i + 1) % f.V.size()])) > 0;
			}
		}
		else
		{
			for (Vert& v : Verts) v.Sel = false;
			SelEdges.clear();
			for (const Face& f : Faces)
				if (f.Sel)
					for (size_t i = 0; i < f.V.size(); ++i)
					{
						Verts[f.V[i]].Sel = true;
						SelEdges.insert(EdgeKey(f.V[i], f.V[(i + 1) % f.V.size()]));
					}
		}
	}

	std::vector<int> Mesh::SelectedVerts() const
	{
		std::vector<int> out;
		for (int i = 0; i < (int)Verts.size(); ++i) if (Verts[i].Sel) out.push_back(i);
		return out;
	}

	std::vector<int> Mesh::SelectedFaces() const
	{
		std::vector<int> out;
		for (int i = 0; i < (int)Faces.size(); ++i) if (Faces[i].Sel) out.push_back(i);
		return out;
	}

	std::vector<int> Mesh::SelectedEdgeIndices()
	{
		std::vector<int> out;
		const auto& edges = Edges();
		for (int i = 0; i < (int)edges.size(); ++i)
			if (SelEdges.count(EdgeKey(edges[i].A, edges[i].B))) out.push_back(i);
		return out;
	}

	Vec3 Mesh::SelectionCenter() const
	{
		Vec3 c(0, 0, 0);
		int n = 0;
		for (const Vert& v : Verts) if (v.Sel) { c += v.P; ++n; }
		return n ? c / (float)n : c;
	}

	int Mesh::SelectLinked()
	{
		// 점 연결 (면으로 이어진 것)
		std::vector<std::vector<int>> adj(Verts.size());
		for (const Edge& e : Edges()) { adj[e.A].push_back(e.B); adj[e.B].push_back(e.A); }
		std::queue<int> q;
		for (int i = 0; i < (int)Verts.size(); ++i) if (Verts[i].Sel) q.push(i);
		while (!q.empty())
		{
			const int v = q.front(); q.pop();
			for (int n : adj[v]) if (!Verts[n].Sel) { Verts[n].Sel = true; q.push(n); }
		}
		Flush(SelectMode::Vertex);
		return (int)SelectedVerts().size();
	}

	int Mesh::SelectByNormal(const Vec3& dirIn, float maxAngleDeg)
	{
		Vec3 dir = dirIn;
		dir.Normalize();
		const float c = cosf(maxAngleDeg * kPi / 180.0f);
		int n = 0;
		for (int f = 0; f < (int)Faces.size(); ++f)
			if (FaceNormal(f).Dot(dir) >= c) { Faces[f].Sel = true; ++n; }
		Flush(SelectMode::Face);
		return n;
	}

	int Mesh::SelectByPosition(int axis, float minV, float maxV)
	{
		int n = 0;
		for (Vert& v : Verts)
		{
			const float p = axis == 0 ? v.P.x : (axis == 1 ? v.P.y : v.P.z);
			if (p >= minV && p <= maxV) { v.Sel = true; ++n; }
		}
		Flush(SelectMode::Vertex);
		return n;
	}

	int Mesh::SelectEdgeLoop(int edge)
	{
		// 변 고리: 점의 차수가 4 인 동안, 지금 변과 이웃 면을 공유하지 않는 맞은편 변으로 이어 간다
		const auto& edges = Edges();
		if (edge < 0 || edge >= (int)edges.size())
			return 0;
		std::vector<std::vector<int>> vertEdges(Verts.size());
		for (int i = 0; i < (int)edges.size(); ++i) { vertEdges[edges[i].A].push_back(i); vertEdges[edges[i].B].push_back(i); }
		std::unordered_set<int> loop = { edge };
		for (int dir = 0; dir < 2; ++dir)
		{
			int cur = edge;
			int v = dir == 0 ? edges[edge].B : edges[edge].A;
			for (int guard = 0; guard < 100000; ++guard)
			{
				if (vertEdges[v].size() != 4)
					break;
				const std::vector<int>& curFaces = edges[cur].Faces;
				int next = -1;
				for (int e : vertEdges[v])
				{
					if (e == cur) continue;
					bool sharesFace = false;
					for (int f : edges[e].Faces)
						if (std::find(curFaces.begin(), curFaces.end(), f) != curFaces.end()) sharesFace = true;
					if (!sharesFace) { next = e; break; }
				}
				if (next < 0 || loop.count(next))
					break;
				loop.insert(next);
				v = edges[next].A == v ? edges[next].B : edges[next].A;
				cur = next;
			}
		}
		for (int e : loop) SelEdges.insert(EdgeKey(edges[e].A, edges[e].B));
		Flush(SelectMode::Edge);
		return (int)loop.size();
	}

	int Mesh::SelectEdgeRing(int edge)
	{
		const auto& edges = Edges();
		if (edge < 0 || edge >= (int)edges.size())
			return 0;
		std::unordered_set<int> ring = { edge };
		std::queue<int> q;
		q.push(edge);
		while (!q.empty())
		{
			const int e = q.front(); q.pop();
			for (int f : edges[e].Faces)
			{
				const auto& v = Faces[f].V;
				if (v.size() != 4) continue;
				for (int i = 0; i < 4; ++i)
					if (EdgeKey(v[i], v[(i + 1) % 4]) == EdgeKey(edges[e].A, edges[e].B))
					{
						const int opp = FindEdge(v[(i + 2) % 4], v[(i + 3) % 4]);
						if (opp >= 0 && !ring.count(opp)) { ring.insert(opp); q.push(opp); }
					}
			}
		}
		for (int e : ring) SelEdges.insert(EdgeKey(edges[e].A, edges[e].B));
		Flush(SelectMode::Edge);
		return (int)ring.size();
	}

	int Mesh::GrowSelection()
	{
		std::vector<int> add;
		for (const Edge& e : Edges())
		{
			if (Verts[e.A].Sel && !Verts[e.B].Sel) add.push_back(e.B);
			if (Verts[e.B].Sel && !Verts[e.A].Sel) add.push_back(e.A);
		}
		for (int i : add) Verts[i].Sel = true;
		Flush(SelectMode::Vertex);
		return (int)SelectedVerts().size();
	}

	int Mesh::ShrinkSelection()
	{
		std::vector<int> drop;
		for (const Edge& e : Edges())
			if (Verts[e.A].Sel != Verts[e.B].Sel) drop.push_back(Verts[e.A].Sel ? e.A : e.B);
		for (int i : drop) Verts[i].Sel = false;
		Flush(SelectMode::Vertex);
		return (int)SelectedVerts().size();
	}

	// ------------------------------------------------------------------ 만들기
	int Mesh::AddFace(const std::vector<Vec3>& points)
	{
		if (points.size() < 3) return 0;
		Face f;
		for (const Vec3& p : points) f.V.push_back(Index(Verts, p));
		f.Sel = true;
		Faces.push_back(f);
		Touch();
		return 1;
	}

	int Mesh::AddCube(const Vec3& s, const Vec3& c)
	{
		DeselectAll();
		const Vec3 h = s * 0.5f;
		const int base = (int)Verts.size();
		for (int i = 0; i < 8; ++i)
			Index(Verts, c + Vec3((i & 1) ? h.x : -h.x, (i & 2) ? h.y : -h.y, (i & 4) ? h.z : -h.z));
		// 반시계 (밖에서 볼 때, 왼손 좌표)
		const int quads[6][4] = { { 0, 2, 3, 1 }, { 4, 5, 7, 6 }, { 0, 1, 5, 4 }, { 2, 6, 7, 3 }, { 0, 4, 6, 2 }, { 1, 3, 7, 5 } };
		for (auto& q : quads)
		{
			Face f;
			for (int k : q) f.V.push_back(base + k);
			f.UV = { Vec2(0, 1), Vec2(0, 0), Vec2(1, 0), Vec2(1, 1) };
			f.Sel = true;
			Faces.push_back(f);
		}
		Touch();
		RecalculateNormals();
		return 6;
	}

	int Mesh::AddPlane(const Vec2& size, const Vec3& c, int sx, int sy)
	{
		DeselectAll();
		sx = (std::max)(1, sx); sy = (std::max)(1, sy);
		const int base = (int)Verts.size();
		for (int y = 0; y <= sy; ++y)
			for (int x = 0; x <= sx; ++x)
				Index(Verts, c + Vec3((x / (float)sx - 0.5f) * size.x, 0.0f, (y / (float)sy - 0.5f) * size.y));
		for (int y = 0; y < sy; ++y)
			for (int x = 0; x < sx; ++x)
			{
				Face f;
				const int i0 = base + y * (sx + 1) + x;
				f.V = { i0, i0 + sx + 1, i0 + sx + 2, i0 + 1 };   // 위(+Y) 가 앞면
				f.UV = { Vec2(x / (float)sx, 1 - y / (float)sy), Vec2(x / (float)sx, 1 - (y + 1) / (float)sy),
					Vec2((x + 1) / (float)sx, 1 - (y + 1) / (float)sy), Vec2((x + 1) / (float)sx, 1 - y / (float)sy) };
				f.Sel = true;
				Faces.push_back(f);
			}
		Touch();
		RecalculateNormals();
		return sx * sy;
	}

	int Mesh::AddCircle(int n, float r, const Vec3& c, bool fill)
	{
		DeselectAll();
		n = (std::max)(3, n);
		Face f;
		for (int i = n - 1; i >= 0; --i)   // 위(+Y) 를 보게
		{
			const float a = 2.0f * kPi * i / n;
			f.V.push_back(Index(Verts, c + Vec3(cosf(a) * r, 0.0f, sinf(a) * r)));
		}
		if (fill)
		{
			f.Sel = true;
			Faces.push_back(f);
			Touch();
			RecalculateNormals();
			return 1;
		}
		// 면 없이 점만 (변 = 면이 있어야 해서 고리는 Fill 로)
		return 0;
	}

	int Mesh::AddCylinder(int n, float r, float depth, const Vec3& c, bool caps, float rTop)
	{
		DeselectAll();
		n = (std::max)(3, n);
		if (rTop < 0.0f) rTop = r;
		const int base = (int)Verts.size();
		for (int ring = 0; ring < 2; ++ring)
			for (int i = 0; i < n; ++i)
			{
				const float a = 2.0f * kPi * i / n, rr = ring ? rTop : r;
				Index(Verts, c + Vec3(cosf(a) * rr, (ring ? 0.5f : -0.5f) * depth, sinf(a) * rr));
			}
		int faces = 0;
		for (int i = 0; i < n; ++i)
		{
			Face f;
			const int i1 = (i + 1) % n;
			f.V = { base + i, base + n + i, base + n + i1, base + i1 };
			f.UV = { Vec2(i / (float)n, 1), Vec2(i / (float)n, 0), Vec2((i + 1) / (float)n, 0), Vec2((i + 1) / (float)n, 1) };
			f.Sel = true;
			f.Smooth = true;
			Faces.push_back(f);
			++faces;
		}
		if (caps)
		{
			Face bottom, top;
			for (int i = 0; i < n; ++i) { bottom.V.push_back(base + i); top.V.push_back(base + n + (n - 1 - i)); }
			bottom.Sel = top.Sel = true;
			if (rTop > 1e-6f) { Faces.push_back(top); ++faces; }
			if (r > 1e-6f) { Faces.push_back(bottom); ++faces; }
		}
		Touch();
		RecalculateNormals();
		MergeByDistance(1e-6f, true);
		return faces;
	}

	int Mesh::AddUVSphere(int seg, int rings, float r, const Vec3& c)
	{
		DeselectAll();
		seg = (std::max)(3, seg); rings = (std::max)(2, rings);
		const int top = Index(Verts, c + Vec3(0, r, 0));
		const int ringBase = (int)Verts.size();
		for (int y = 1; y < rings; ++y)
		{
			const float phi = kPi * y / rings;
			for (int x = 0; x < seg; ++x)
			{
				const float th = 2.0f * kPi * x / seg;
				Index(Verts, c + Vec3(sinf(phi) * cosf(th) * r, cosf(phi) * r, sinf(phi) * sinf(th) * r));
			}
		}
		const int bottom = Index(Verts, c + Vec3(0, -r, 0));
		auto at = [&](int y, int x) { return ringBase + (y - 1) * seg + (x % seg); };
		for (int x = 0; x < seg; ++x)
		{
			Face f; f.V = { top, at(1, x + 1), at(1, x) }; f.Sel = true; f.Smooth = true; Faces.push_back(f);
			Face g; g.V = { bottom, at(rings - 1, x), at(rings - 1, x + 1) }; g.Sel = true; g.Smooth = true; Faces.push_back(g);
		}
		for (int y = 1; y < rings - 1; ++y)
			for (int x = 0; x < seg; ++x)
			{
				Face f; f.V = { at(y, x), at(y, x + 1), at(y + 1, x + 1), at(y + 1, x) }; f.Sel = true; f.Smooth = true; Faces.push_back(f);
			}
		Touch();
		RecalculateNormals();
		return (int)Faces.size();
	}

	int Mesh::AddIcoSphere(int subdiv, float r, const Vec3& c)
	{
		DeselectAll();
		const float t = (1.0f + sqrtf(5.0f)) * 0.5f;
		std::vector<Vec3> p = { { -1, t, 0 }, { 1, t, 0 }, { -1, -t, 0 }, { 1, -t, 0 }, { 0, -1, t }, { 0, 1, t }, { 0, -1, -t }, { 0, 1, -t },
			{ t, 0, -1 }, { t, 0, 1 }, { -t, 0, -1 }, { -t, 0, 1 } };
		std::vector<std::array<int, 3>> tris = { { 0, 11, 5 }, { 0, 5, 1 }, { 0, 1, 7 }, { 0, 7, 10 }, { 0, 10, 11 }, { 1, 5, 9 }, { 5, 11, 4 }, { 11, 10, 2 },
			{ 10, 7, 6 }, { 7, 1, 8 }, { 3, 9, 4 }, { 3, 4, 2 }, { 3, 2, 6 }, { 3, 6, 8 }, { 3, 8, 9 }, { 4, 9, 5 }, { 2, 4, 11 }, { 6, 2, 10 }, { 8, 6, 7 }, { 9, 8, 1 } };
		for (Vec3& v : p) v.Normalize();
		for (int s = 0; s < std::clamp(subdiv, 0, 6); ++s)
		{
			std::map<uint64, int> mid;
			auto midpoint = [&](int a, int b) {
				const uint64 k = EdgeKey(a, b);
				auto it = mid.find(k);
				if (it != mid.end()) return it->second;
				Vec3 m = (p[a] + p[b]) * 0.5f; m.Normalize();
				p.push_back(m);
				return mid[k] = (int)p.size() - 1;
			};
			std::vector<std::array<int, 3>> next;
			for (auto& tri : tris)
			{
				const int a = midpoint(tri[0], tri[1]), b = midpoint(tri[1], tri[2]), cc = midpoint(tri[2], tri[0]);
				next.push_back({ tri[0], a, cc }); next.push_back({ tri[1], b, a }); next.push_back({ tri[2], cc, b }); next.push_back({ a, b, cc });
			}
			tris = next;
		}
		const int base = (int)Verts.size();
		for (const Vec3& v : p) Index(Verts, c + v * r);
		for (auto& tri : tris) { Face f; f.V = { base + tri[0], base + tri[1], base + tri[2] }; f.Sel = true; f.Smooth = true; Faces.push_back(f); }
		Touch();
		RecalculateNormals();
		return (int)tris.size();
	}

	int Mesh::AddTorus(int major, int minor, float R, float rr, const Vec3& c)
	{
		DeselectAll();
		major = (std::max)(3, major); minor = (std::max)(3, minor);
		const int base = (int)Verts.size();
		for (int i = 0; i < major; ++i)
			for (int j = 0; j < minor; ++j)
			{
				const float u = 2.0f * kPi * i / major, v = 2.0f * kPi * j / minor;
				Index(Verts, c + Vec3((R + rr * cosf(v)) * cosf(u), rr * sinf(v), (R + rr * cosf(v)) * sinf(u)));
			}
		for (int i = 0; i < major; ++i)
			for (int j = 0; j < minor; ++j)
			{
				const int i1 = (i + 1) % major, j1 = (j + 1) % minor;
				Face f; f.V = { base + i * minor + j, base + i * minor + j1, base + i1 * minor + j1, base + i1 * minor + j }; f.Sel = true; f.Smooth = true;
				Faces.push_back(f);
			}
		Touch();
		RecalculateNormals();
		return major * minor;
	}

	// ------------------------------------------------------------------ 변환
	int Mesh::Translate(const Vec3& d)
	{
		int n = 0;
		for (Vert& v : Verts) if (v.Sel) { v.P += d; ++n; }
		return n;
	}

	int Mesh::Rotate(const Quaternion& q, const Vec3& pivot)
	{
		int n = 0;
		for (Vert& v : Verts) if (v.Sel) { v.P = Vec3::Transform(v.P - pivot, q) + pivot; ++n; }
		return n;
	}

	int Mesh::Scale(const Vec3& s, const Vec3& pivot)
	{
		int n = 0;
		for (Vert& v : Verts) if (v.Sel) { v.P = (v.P - pivot) * s + pivot; ++n; }
		return n;
	}

	int Mesh::SetPositions(const std::vector<std::pair<int, Vec3>>& pos)
	{
		int n = 0;
		for (const auto& [i, p] : pos)
			if (i >= 0 && i < (int)Verts.size()) { Verts[i].P = p; ++n; }
		return n;
	}

	// ------------------------------------------------------------------ Extrude / Inset
	// 면 영역: 영역의 점을 복제해 영역 면이 새 점을 쓰게 하고, 경계 변마다 옆면 [a, b, b', a'] (바깥 이웃은 b→a 로 돈다)
	int Mesh::ExtrudeFaces(const std::vector<int>& faces, float distance, const Vec3* direction, bool alongNormal)
	{
		if (faces.empty())
			return 0;
		std::unordered_set<int> inRegion(faces.begin(), faces.end());
		// 경계 변: 영역 면이 하나뿐인 변 (영역 면 안 방향 그대로)
		std::unordered_map<uint64, int> count;
		for (int f : faces)
		{
			const auto& v = Faces[f].V;
			for (size_t i = 0; i < v.size(); ++i) ++count[EdgeKey(v[i], v[(i + 1) % v.size()])];
		}
		std::vector<std::pair<int, int>> boundary;
		for (int f : faces)
		{
			const auto& v = Faces[f].V;
			for (size_t i = 0; i < v.size(); ++i)
			{
				const int a = v[i], b = v[(i + 1) % v.size()];
				if (count[EdgeKey(a, b)] == 1) boundary.push_back({ a, b });
			}
		}
		// 영역 평균 법선 (방향을 주지 않으면 그쪽으로)
		Vec3 normal(0, 0, 0);
		for (int f : faces) normal += FaceNormal(f);
		if (normal.LengthSquared() < 1e-12f) normal = Vec3(0, 1, 0);
		normal.Normalize();
		std::unordered_map<int, int> dup;
		for (int f : faces)
			for (int i : Faces[f].V)
				if (!dup.count(i)) { Verts.push_back(CopyVert(Verts[i], Verts[i].P)); dup[i] = (int)Verts.size() - 1; }
		for (int f : faces)
			for (int& i : Faces[f].V) i = dup[i];
		int sides = 0;
		for (const auto& [a, b] : boundary)
		{
			Face side;
			side.V = { a, b, dup[b], dup[a] };
			side.Sel = false;
			Faces.push_back(side);
			++sides;
		}
		// 옮기기
		const Vec3 d = direction ? *direction : normal * distance;
		if (alongNormal && !direction)
		{
			// 면마다 자기 법선 (Individual): 점마다 소속 면 법선 평균
			std::unordered_map<int, Vec3> acc;
			for (int f : faces) { const Vec3 n = FaceNormal(f); for (int i : Faces[f].V) acc[i] += n; }
			for (auto& [i, n] : acc) { Vec3 nn = n; nn.Normalize(); Verts[i].P += nn * distance; }
		}
		else
			for (auto& [old, nw] : dup) Verts[nw].P += d;
		// 선택 = 새 뚜껑
		for (Vert& v : Verts) v.Sel = false;
		for (Face& f : Faces) f.Sel = false;
		for (int f : faces) Faces[f].Sel = true;
		Touch();
		Flush(SelectMode::Face);
		Cleanup();
		return (int)faces.size() + sides;
	}

	int Mesh::ExtrudeBoundaryEdges(const Vec3& offset)
	{
		// 선택한 경계 변 (면이 하나): 새 점을 만들고 [b, a, a', b'] (이웃 면은 a→b 로 돈다)
		const auto& edges = Edges();
		std::vector<std::pair<int, int>> todo;
		for (int e : SelectedEdgeIndices())
		{
			if (edges[e].Faces.size() != 1) continue;
			const auto& v = Faces[edges[e].Faces[0]].V;
			for (size_t i = 0; i < v.size(); ++i)
				if (EdgeKey(v[i], v[(i + 1) % v.size()]) == EdgeKey(edges[e].A, edges[e].B))
					todo.push_back({ v[i], v[(i + 1) % v.size()] });
		}
		if (todo.empty())
			return 0;
		std::unordered_map<int, int> dup;
		for (auto& [a, b] : todo)
			for (int i : { a, b })
				if (!dup.count(i)) { Verts.push_back(CopyVert(Verts[i], Verts[i].P + offset)); dup[i] = (int)Verts.size() - 1; }
		DeselectAll();
		for (auto& [a, b] : todo)
		{
			Face f; f.V = { b, a, dup[a], dup[b] }; Faces.push_back(f);
			SelEdges.insert(EdgeKey(dup[a], dup[b]));
		}
		for (auto& [o, n] : dup) Verts[n].Sel = true;
		Touch();
		return (int)todo.size();
	}

	int Mesh::ExtrudeRegion(float distance, const Vec3* direction)
	{
		const std::vector<int> faces = SelectedFaces();
		if (!faces.empty())
			return ExtrudeFaces(faces, distance, direction, false);
		// 면이 없으면: 선택한 경계 변을 (방향 또는 위로) 늘인다
		const Vec3 off = direction ? *direction : Vec3(0, distance, 0);
		return ExtrudeBoundaryEdges(off);
	}

	int Mesh::ExtrudeIndividual(float distance)
	{
		int n = 0;
		for (int f : SelectedFaces())
			n += ExtrudeFaces({ f }, distance, nullptr, true);
		return n;
	}

	int Mesh::Inset(float thickness, float depth, bool individual)
	{
		std::vector<std::vector<int>> groups;
		const std::vector<int> sel = SelectedFaces();
		if (sel.empty())
			return 0;
		if (individual) for (int f : sel) groups.push_back({ f });
		else groups.push_back(sel);
		int made = 0;
		std::vector<int> keep;
		for (const auto& faces : groups)
		{
			std::unordered_map<uint64, int> count;
			for (int f : faces) { const auto& v = Faces[f].V; for (size_t i = 0; i < v.size(); ++i) ++count[EdgeKey(v[i], v[(i + 1) % v.size()])]; }
			// 경계 변 (영역 면 방향) + 경계 점의 안쪽 방향
			std::vector<std::pair<int, int>> boundary;
			std::unordered_map<int, std::vector<Vec3>> inward;
			for (int f : faces)
			{
				const auto& v = Faces[f].V;
				const Vec3 n = FaceNormal(f);
				for (size_t i = 0; i < v.size(); ++i)
				{
					const int a = v[i], b = v[(i + 1) % v.size()];
					if (count[EdgeKey(a, b)] != 1) continue;
					boundary.push_back({ a, b });
					Vec3 e = Verts[b].P - Verts[a].P;
					e.Normalize();
					// 면 안쪽 = n × e (법선 = (b-a)×(c-a) 규약)
					Vec3 in = n.Cross(e);
					in.Normalize();
					inward[a].push_back(in);
					inward[b].push_back(in);
				}
			}
			std::unordered_map<int, int> dup;
			for (auto& [i, dirs] : inward)
			{
				Vec3 d(0, 0, 0);
				for (const Vec3& x : dirs) d += x;
				if (d.LengthSquared() < 1e-12f) d = dirs.front();
				d.Normalize();
				const float c = (std::max)(0.2f, d.Dot(dirs.front()));
				Verts.push_back(CopyVert(Verts[i], Verts[i].P + d * (thickness / c)));
				dup[i] = (int)Verts.size() - 1;
			}
			for (int f : faces)
				for (int& i : Faces[f].V)
					if (dup.count(i)) i = dup[i];
			for (const auto& [a, b] : boundary)
			{
				Face ring; ring.V = { a, b, dup[b], dup[a] };
				Faces.push_back(ring);
				++made;
			}
			if (depth != 0.0f)
			{
				Vec3 n(0, 0, 0);
				for (int f : faces) n += FaceNormal(f);
				n.Normalize();
				std::unordered_set<int> moved;
				for (int f : faces) for (int i : Faces[f].V) if (moved.insert(i).second) Verts[i].P += n * depth;
			}
			keep.insert(keep.end(), faces.begin(), faces.end());
			made += (int)faces.size();
		}
		for (Face& f : Faces) f.Sel = false;
		for (int f : keep) Faces[f].Sel = true;
		Touch();
		Flush(SelectMode::Face);
		return made;
	}

	// ------------------------------------------------------------------ 나누기
	int Mesh::Subdivide(int cuts)
	{
		cuts = std::clamp(cuts, 1, 1);   // (지금은 1 번 — 여러 번은 반복 호출)
		const std::vector<int> sel = SelectedFaces();
		if (sel.empty())
			return 0;
		std::unordered_set<int> selSet(sel.begin(), sel.end());
		std::unordered_map<uint64, int> mid;
		auto midpoint = [&](int a, int b) {
			const uint64 k = EdgeKey(a, b);
			auto it = mid.find(k);
			if (it != mid.end()) return it->second;
			Verts.push_back(MixVert(Verts[a], Verts[b], 0.5f));
			return mid[k] = (int)Verts.size() - 1;
		};
		for (int f : sel) { const auto& v = Faces[f].V; for (size_t i = 0; i < v.size(); ++i) midpoint(v[i], v[(i + 1) % v.size()]); }
		std::vector<Face> out;
		for (int f = 0; f < (int)Faces.size(); ++f)
		{
			const Face& face = Faces[f];
			const auto v = face.V;
			const size_t n = v.size();
			const bool hasUV = face.UV.size() == n;
			auto uv = [&](size_t i) { return hasUV ? face.UV[i % n] : Vec2(0, 0); };
			auto uvMid = [&](size_t i) { return (uv(i) + uv(i + 1)) * 0.5f; };   // 변 i → i+1 의 가운데
			if (!selSet.count(f))
			{
				// 이웃 면: 나뉜 변에 중점을 끼워 넣는다 (구멍 방지, UV 도 가운데)
				Face g = face;
				g.V.clear();
				g.UV.clear();
				for (size_t i = 0; i < n; ++i)
				{
					g.V.push_back(v[i]);
					if (hasUV) g.UV.push_back(uv(i));
					auto it = mid.find(EdgeKey(v[i], v[(i + 1) % n]));
					if (it != mid.end()) { g.V.push_back(it->second); if (hasUV) g.UV.push_back(uvMid(i)); }
				}
				out.push_back(g);
				continue;
			}
			std::vector<int> m(n);
			for (size_t i = 0; i < n; ++i) m[i] = mid[EdgeKey(v[i], v[(i + 1) % n])];
			auto emit = [&](std::vector<int> vs, std::vector<Vec2> uvs) {
				Face g = face;   // Smooth · Material · Origin 유지
				g.V = std::move(vs);
				g.UV = hasUV ? std::move(uvs) : std::vector<Vec2>();
				g.Sel = true;
				out.push_back(g);
			};
			if (n == 3)
			{
				emit({ v[0], m[0], m[2] }, { uv(0), uvMid(0), uvMid(2) });
				emit({ m[0], v[1], m[1] }, { uvMid(0), uv(1), uvMid(1) });
				emit({ m[2], m[1], v[2] }, { uvMid(2), uvMid(1), uv(2) });
				emit({ m[0], m[1], m[2] }, { uvMid(0), uvMid(1), uvMid(2) });
			}
			else
			{
				Verts.push_back(AverageVert(Verts, v));
				const int ci = (int)Verts.size() - 1;
				Vec2 cuv(0, 0);
				for (size_t i = 0; i < n; ++i) cuv += uv(i) / (float)n;
				for (size_t i = 0; i < n; ++i)
					emit({ v[i], m[i], ci, m[(i + n - 1) % n] }, { uv(i), uvMid(i), cuv, uvMid(i + n - 1) });
			}
		}
		Faces = out;
		Touch();
		Flush(SelectMode::Face);
		return (int)SelectedFaces().size();
	}

	int Mesh::CatmullClark(int levels)
	{
		for (int level = 0; level < std::clamp(levels, 1, 4); ++level)
		{
			const auto& edges = Edges();
			const int nv = (int)Verts.size(), nf = (int)Faces.size();
			std::vector<Vec3> facePt(nf);
			for (int f = 0; f < nf; ++f) facePt[f] = FaceCenter(f);
			std::vector<Vec3> edgePt(edges.size());
			std::vector<bool> boundaryVert(nv, false);
			for (size_t e = 0; e < edges.size(); ++e)
			{
				const Edge& ed = edges[e];
				const Vec3 m = (Verts[ed.A].P + Verts[ed.B].P) * 0.5f;
				if (ed.Faces.size() == 2)
					edgePt[e] = (Verts[ed.A].P + Verts[ed.B].P + facePt[ed.Faces[0]] + facePt[ed.Faces[1]]) * 0.25f;
				else
				{
					edgePt[e] = m;
					boundaryVert[ed.A] = boundaryVert[ed.B] = true;
				}
			}
			// 점 새 위치
			std::vector<Vec3> F(nv, Vec3(0, 0, 0)), R(nv, Vec3(0, 0, 0)), B(nv, Vec3(0, 0, 0));
			std::vector<int> nF(nv, 0), nE(nv, 0), nB(nv, 0);
			for (int f = 0; f < nf; ++f) for (int i : Faces[f].V) { F[i] += facePt[f]; ++nF[i]; }
			for (const Edge& ed : edges)
			{
				const Vec3 m = (Verts[ed.A].P + Verts[ed.B].P) * 0.5f;
				R[ed.A] += m; R[ed.B] += m; ++nE[ed.A]; ++nE[ed.B];
				if (ed.Faces.size() < 2) { B[ed.A] += m; B[ed.B] += m; ++nB[ed.A]; ++nB[ed.B]; }
			}
			std::vector<Vec3> newPos(nv);
			for (int i = 0; i < nv; ++i)
			{
				const Vec3 P = Verts[i].P;
				if (boundaryVert[i] && nB[i] >= 2)
					newPos[i] = P * 0.5f + (B[i] / (float)nB[i]) * 0.5f;   // 경계: 곡선처럼
				else if (nF[i] > 0 && nE[i] > 0)
				{
					const float n = (float)nE[i];
					newPos[i] = (F[i] / (float)nF[i] + (R[i] / n) * 2.0f + P * (n - 3.0f)) / n;
				}
				else newPos[i] = P;
			}
			// 새 위상: 면마다 (점, 변 점, 면 점, 이전 변 점) 사각형
			std::vector<Vert> verts(nv);
			for (int i = 0; i < nv; ++i) verts[i] = CopyVert(Verts[i], newPos[i], Verts[i].Sel);
			std::vector<int> edgeIdx(edges.size()), faceIdx(nf);
			for (size_t e = 0; e < edges.size(); ++e)
			{
				Vert v = MixVert(Verts[edges[e].A], Verts[edges[e].B], 0.5f);
				v.P = edgePt[e];
				v.Sel = Verts[edges[e].A].Sel && Verts[edges[e].B].Sel;
				verts.push_back(v);
				edgeIdx[e] = (int)verts.size() - 1;
			}
			for (int f = 0; f < nf; ++f)
			{
				Vert v = AverageVert(Verts, Faces[f].V);
				v.P = facePt[f];
				v.Sel = Faces[f].Sel;
				verts.push_back(v);
				faceIdx[f] = (int)verts.size() - 1;
			}
			std::vector<Face> faces;
			for (int f = 0; f < nf; ++f)
			{
				const auto& v = Faces[f].V;
				const size_t n = v.size();
				for (size_t i = 0; i < n; ++i)
				{
					const int eNext = FindEdge(v[i], v[(i + 1) % n]), ePrev = FindEdge(v[(i + n - 1) % n], v[i]);
					Face g = Faces[f];   // Material · Origin 유지
					g.V = { v[i], edgeIdx[eNext], faceIdx[f], edgeIdx[ePrev] };
					g.Smooth = true;
					if (Faces[f].UV.size() == n)
					{
						const auto& u = Faces[f].UV;
						Vec2 c(0, 0);
						for (const Vec2& t : u) c += t / (float)n;
						g.UV = { u[i], (u[i] + u[(i + 1) % n]) * 0.5f, c, (u[(i + n - 1) % n] + u[i]) * 0.5f };
					}
					faces.push_back(g);
				}
			}
			Verts = verts;
			Faces = faces;
			Touch();
		}
		Flush(SelectMode::Face);
		return (int)Faces.size();
	}

	int Mesh::LoopCut(int edge, int cuts, float factor)
	{
		const auto& edges0 = Edges();
		if (edge < 0 || edge >= (int)edges0.size())
			return 0;
		cuts = std::clamp(cuts, 1, 32);
		// 고리: 사각형을 건너 맞은편 변으로 (처음 변에서 양쪽으로). 변마다 "시작 쪽" 점 (자르는 비율의 기준) 을 함께 기록
		struct RingEdge { int A, B; };   // A = 시작 쪽
		const RingEdge first = { edges0[edge].A, edges0[edge].B };
		std::vector<RingEdge> ring = { first };
		std::unordered_set<uint64> seen = { EdgeKey(first.A, first.B) };
		std::unordered_set<int> faceSeen;
		for (int dir = 0; dir < 2; ++dir)
		{
			RingEdge cur = first;
			for (int guard = 0; guard < 100000; ++guard)
			{
				const int e = FindEdge(cur.A, cur.B);
				int nextFace = -1;
				for (int f : Edges()[e].Faces)
					if (Faces[f].V.size() == 4 && !faceSeen.count(f)) { nextFace = f; break; }
				if (nextFace < 0)
					break;
				faceSeen.insert(nextFace);
				const auto& v = Faces[nextFace].V;
				int k = 0;
				for (; k < 4; ++k) if (EdgeKey(v[k], v[(k + 1) % 4]) == EdgeKey(cur.A, cur.B)) break;
				// 사각형 [p0 p1 p2 p3], 변 p0p1 의 맞은편 = p3p2 (p0 ↔ p3 이 같은 쪽)
				const int p0 = v[k], p2 = v[(k + 2) % 4], p3 = v[(k + 3) % 4];
				const RingEdge opp = cur.A == p0 ? RingEdge{ p3, p2 } : RingEdge{ p2, p3 };
				if (!seen.insert(EdgeKey(opp.A, opp.B)).second)
					break;   // 한 바퀴
				ring.push_back(opp);
				cur = opp;
			}
		}
		// 새 점 (변마다 cuts 개, 시작 쪽에서)
		std::unordered_map<uint64, std::vector<int>> cutPts;
		std::unordered_map<uint64, int> startOf;
		for (const RingEdge& r : ring)
		{
			std::vector<int> pts;
			for (int c = 1; c <= cuts; ++c)
			{
				const float t = cuts == 1 ? std::clamp(factor, 0.01f, 0.99f) : c / (float)(cuts + 1);
				Verts.push_back(MixVert(Verts[r.A], Verts[r.B], t));
				pts.push_back((int)Verts.size() - 1);
			}
			cutPts[EdgeKey(r.A, r.B)] = pts;
			startOf[EdgeKey(r.A, r.B)] = r.A;
		}
		auto cutT = [&](int c) { return cuts == 1 ? std::clamp(factor, 0.01f, 0.99f) : (c + 1) / (float)(cuts + 1); };   // 시작 쪽에서
		auto ptsFrom = [&](int a, int b) {
			// a → b 방향 순서로
			std::vector<int> p = cutPts[EdgeKey(a, b)];
			if (startOf[EdgeKey(a, b)] != a) std::reverse(p.begin(), p.end());
			return p;
		};
		auto tFrom = [&](int a, int b) {
			// ptsFrom 과 같은 순서의 a 에서의 비율
			std::vector<float> t(cuts);
			const bool fromA = startOf[EdgeKey(a, b)] == a;
			for (int c = 0; c < cuts; ++c) t[c] = fromA ? cutT(c) : 1.0f - cutT(cuts - 1 - c);
			return t;
		};
		DeselectAll();
		std::vector<Face> out;
		for (int f = 0; f < (int)Faces.size(); ++f)
		{
			const auto v = Faces[f].V;
			int k = 4;
			if (faceSeen.count(f))
				for (k = 0; k < 4; ++k)
					if (cutPts.count(EdgeKey(v[k], v[(k + 1) % 4])) && cutPts.count(EdgeKey(v[(k + 2) % 4], v[(k + 3) % 4]))) break;
			if (k == 4)
			{
				// 고리 밖 면 (고리 끝의 삼각형 등): 잘린 변에 새 점을 끼워 구멍을 막는다
				Face g = Faces[f];
				const bool hasUV = g.UV.size() == v.size();
				g.V.clear();
				g.UV.clear();
				for (size_t i = 0; i < v.size(); ++i)
				{
					const int a = v[i], b = v[(i + 1) % v.size()];
					g.V.push_back(a);
					if (hasUV) g.UV.push_back(Faces[f].UV[i]);
					if (cutPts.count(EdgeKey(a, b)))
					{
						const std::vector<int> p = ptsFrom(a, b);
						const std::vector<float> t = tFrom(a, b);
						for (size_t j = 0; j < p.size(); ++j)
						{
							g.V.push_back(p[j]);
							if (hasUV) g.UV.push_back(Faces[f].UV[i] + (Faces[f].UV[(i + 1) % v.size()] - Faces[f].UV[i]) * t[j]);
						}
					}
				}
				out.push_back(g);
				continue;
			}
			const int p0 = v[k], p1 = v[(k + 1) % 4], p2 = v[(k + 2) % 4], p3 = v[(k + 3) % 4];
			const std::vector<int> top = ptsFrom(p0, p1), bottom = ptsFrom(p3, p2);   // 같은 쪽에서 같은 순서
			std::vector<int> colA = { p0 }, colB = { p3 };
			colA.insert(colA.end(), top.begin(), top.end()); colA.push_back(p1);
			colB.insert(colB.end(), bottom.begin(), bottom.end()); colB.push_back(p2);
			// UV: 위 · 아래 변을 같은 비율로
			const bool hasUV = Faces[f].UV.size() == 4;
			std::vector<Vec2> uvA, uvB;
			if (hasUV)
			{
				const Vec2 u0 = Faces[f].UV[k], u1 = Faces[f].UV[(k + 1) % 4], u2 = Faces[f].UV[(k + 2) % 4], u3 = Faces[f].UV[(k + 3) % 4];
				const std::vector<float> tt = tFrom(p0, p1), tb = tFrom(p3, p2);
				uvA.push_back(u0); for (float t : tt) uvA.push_back(u0 + (u1 - u0) * t); uvA.push_back(u1);
				uvB.push_back(u3); for (float t : tb) uvB.push_back(u3 + (u2 - u3) * t); uvB.push_back(u2);
			}
			for (size_t s = 0; s + 1 < colA.size(); ++s)
			{
				Face g = Faces[f];
				g.V = { colA[s], colA[s + 1], colB[s + 1], colB[s] };
				g.UV = hasUV ? std::vector<Vec2>{ uvA[s], uvA[s + 1], uvB[s + 1], uvB[s] } : std::vector<Vec2>();
				g.Sel = false;
				out.push_back(g);
			}
		}
		Faces = out;
		for (auto& [key, pts] : cutPts) for (int p : pts) Verts[p].Sel = true;
		Touch();
		Flush(SelectMode::Vertex);
		return (int)ring.size() * cuts;
	}

	int Mesh::Bevel(float offset, int segments)
	{
		// 모서리 깎기 (segments = 1): 선택한 변 AB 의 양 끝 P 마다
		//  - 두 면 F1 · F2 에서 P 의 다른 이웃 X1 · X2 쪽 변 위로 offset 만큼 옮긴 P1 · P2 를 만들어 F1 · F2 의 P 를 바꾼다
		//  - 변 P-X1 · P-X2 의 건너편 면이 같은 면이면 (차수 3) 그 면의 P 를 P1 · P2 로, 다르면 각 면에 끼우고 삼각형 (P, P1, P2) 로 막는다
		//  - 띠 면 (A1, B1, B2, A2). 방향은 마지막에 이웃 면에 맞춘다
		(void)segments;
		if (offset <= 0.0f)
			return 0;
		std::vector<uint64> todo(SelEdges.begin(), SelEdges.end());
		int made = 0;
		DeselectAll();
		auto otherNeighbor = [&](int face, int p, int q) {
			const auto& v = Faces[face].V;
			const size_t n = v.size();
			for (size_t i = 0; i < n; ++i)
				if (v[i] == p)
				{
					const int prev = v[(i + n - 1) % n], next = v[(i + 1) % n];
					return prev == q ? next : prev;
				}
			return -1;
		};
		auto otherFace = [&](int a, int b, int notFace) {
			const int e = FindEdge(a, b);
			if (e < 0) return -1;
			for (int f : Edges()[e].Faces) if (f != notFace) return f;
			return -1;
		};
		auto replaceIn = [&](int face, int p, const std::vector<int>& with) {
			auto& v = Faces[face].V;
			auto it = std::find(v.begin(), v.end(), p);
			if (it == v.end()) return;
			const size_t at = it - v.begin();
			v.erase(it);
			v.insert(v.begin() + at, with.begin(), with.end());
			Faces[face].UV.clear();
		};
		auto insertBetween = [&](int face, int a, int b, int x) {
			auto& v = Faces[face].V;
			const size_t n = v.size();
			for (size_t i = 0; i < n; ++i)
			{
				const int c = v[i], d = v[(i + 1) % n];
				if ((c == a && d == b) || (c == b && d == a)) { v.insert(v.begin() + i + 1, x); Faces[face].UV.clear(); return; }
			}
		};
		for (uint64 key : todo)
		{
			const int A = (int)(key >> 32), B = (int)(key & 0xFFFFFFFF);
			const int e = FindEdge(A, B);
			if (e < 0 || Edges()[e].Faces.size() != 2)
				continue;
			const int F1 = Edges()[e].Faces[0], F2 = Edges()[e].Faces[1];
			// 이웃 X 는 면을 바꾸기 전에 (A 쪽을 바꾸면 F1 · F2 에 A 가 없어진다)
			const int X[2][2] = { { otherNeighbor(F1, A, B), otherNeighbor(F2, A, B) }, { otherNeighbor(F1, B, A), otherNeighbor(F2, B, A) } };
			int newPts[2][2];   // [끝점 A/B][F1/F2]
			for (int end = 0; end < 2; ++end)
			{
				const int P = end == 0 ? A : B;
				const int X1 = X[end][0], X2 = X[end][1];
				if (X1 < 0 || X2 < 0) { newPts[end][0] = newPts[end][1] = P; continue; }
				auto slide = [&](int X) {
					Vec3 d = Verts[X].P - Verts[P].P;
					const float len = d.Length();
					d /= (std::max)(len, 1e-9f);
					Verts.push_back(CopyVert(Verts[P], Verts[P].P + d * (std::min)(offset, len * 0.45f)));
					return (int)Verts.size() - 1;
				};
				const int P1 = slide(X1), P2 = slide(X2);
				const int G1 = otherFace(P, X1, F1), G2 = otherFace(P, X2, F2);
				replaceIn(F1, P, { P1 });
				replaceIn(F2, P, { P2 });
				if (G1 >= 0 && G1 == G2)
				{
					// G 를 돌 때 P 앞이 X1 이면 [P1, P2], 아니면 [P2, P1]
					const auto& v = Faces[G1].V;
					const size_t n = v.size();
					const size_t i = std::find(v.begin(), v.end(), P) - v.begin();
					const bool x1First = v[(i + n - 1) % n] == X1;
					replaceIn(G1, P, x1First ? std::vector<int>{ P1, P2 } : std::vector<int>{ P2, P1 });
				}
				else
				{
					if (G1 >= 0) insertBetween(G1, P, X1, P1);
					if (G2 >= 0) insertBetween(G2, P, X2, P2);
					Face tri;
					tri.V = { P, P1, P2 };
					Faces.push_back(tri);
				}
				newPts[end][0] = P1;
				newPts[end][1] = P2;
				Touch();
			}
			Face band;
			band.V = { newPts[0][0], newPts[1][0], newPts[1][1], newPts[0][1] };
			band.Sel = true;
			Faces.push_back(band);
			Touch();
			++made;
		}
		Cleanup();
		RecalculateNormals();
		Flush(SelectMode::Face);
		return made;
	}

	// ------------------------------------------------------------------ 지우기 · 합치기
	int Mesh::DeleteVerts()
	{
		const std::vector<int> sel = SelectedVerts();
		if (sel.empty()) return 0;
		std::unordered_set<int> del(sel.begin(), sel.end());
		std::vector<Face> keep;
		for (const Face& f : Faces)
		{
			bool hit = false;
			for (int i : f.V) hit = hit || del.count(i) > 0;
			if (!hit) keep.push_back(f);
		}
		Faces = keep;
		for (int i : sel) Verts[i].Sel = false;
		// 지울 점 표시: 아무 면도 쓰지 않으면 Cleanup 이 지운다
		Touch();
		Cleanup();
		return (int)sel.size();
	}

	int Mesh::DeleteFaces(bool onlyFaces)
	{
		const std::vector<int> sel = SelectedFaces();
		if (sel.empty()) return 0;
		std::vector<Face> keep;
		for (const Face& f : Faces) if (!f.Sel) keep.push_back(f);
		Faces = keep;
		Touch();
		if (!onlyFaces) Cleanup();
		DeselectAll();
		return (int)sel.size();
	}

	int Mesh::MergeByDistance(float dist, bool selectedOnly)
	{
		const int n = (int)Verts.size();
		std::vector<int> remap(n);
		std::iota(remap.begin(), remap.end(), 0);
		// 격자 해시
		const float cell = (std::max)(dist, 1e-6f) * 2.0f;
		std::unordered_map<int64_t, std::vector<int>> grid;
		auto key = [&](const Vec3& p) {
			const int64_t x = (int64_t)floorf(p.x / cell), y = (int64_t)floorf(p.y / cell), z = (int64_t)floorf(p.z / cell);
			return (x * 73856093) ^ (y * 19349663) ^ (z * 83492791);
		};
		int merged = 0;
		for (int i = 0; i < n; ++i)
		{
			if (selectedOnly && !Verts[i].Sel) continue;
			const Vec3 p = Verts[i].P;
			int found = -1;
			for (int dx = -1; dx <= 1 && found < 0; ++dx)
				for (int dy = -1; dy <= 1 && found < 0; ++dy)
					for (int dz = -1; dz <= 1 && found < 0; ++dz)
					{
						const Vec3 q = p + Vec3((float)dx, (float)dy, (float)dz) * cell;
						auto it = grid.find(key(q));
						if (it == grid.end()) continue;
						for (int j : it->second)
							if ((Verts[j].P - p).LengthSquared() <= dist * dist) { found = j; break; }
					}
			if (found >= 0) { remap[i] = found; ++merged; }
			else grid[key(p)].push_back(i);
		}
		if (merged == 0) return 0;
		for (Face& f : Faces)
		{
			std::vector<int> v;
			std::vector<Vec2> uv;
			for (size_t k = 0; k < f.V.size(); ++k)
			{
				const int r = remap[f.V[k]];
				if (!v.empty() && v.back() == r) continue;
				v.push_back(r);
				if (!f.UV.empty()) uv.push_back(f.UV[k]);
			}
			if (v.size() > 1 && v.front() == v.back()) { v.pop_back(); if (!uv.empty()) uv.pop_back(); }
			f.V = v;
			f.UV = uv.size() == v.size() ? uv : std::vector<Vec2>();
		}
		Touch();
		Cleanup();
		return merged;
	}

	int Mesh::MergeAtCenter()
	{
		const std::vector<int> sel = SelectedVerts();
		if (sel.size() < 2) return 0;
		const Vec3 c = SelectionCenter();
		for (int i : sel) Verts[i].P = c;
		return MergeByDistance(1e-6f, true);
	}

	int Mesh::Fill()
	{
		const std::vector<int> sel = SelectedVerts();
		if (sel.size() < 3) return 0;
		Vec3 c(0, 0, 0);
		for (int i : sel) c += Verts[i].P;
		c /= (float)sel.size();
		// 평면: 첫 세 점 (거의 일직선이면 다음 점)
		Vec3 n(0, 0, 0);
		for (size_t i = 0; i + 2 < sel.size() && n.LengthSquared() < 1e-10f; ++i)
			n = (Verts[sel[i + 1]].P - Verts[sel[i]].P).Cross(Verts[sel[i + 2]].P - Verts[sel[i]].P);
		if (n.LengthSquared() < 1e-10f) return 0;
		n.Normalize();
		Vec3 u, v;
		Basis(n, u, v);
		std::vector<std::pair<float, int>> order;
		for (int i : sel) { const Vec3 d = Verts[i].P - c; order.push_back({ atan2f(d.Dot(v), d.Dot(u)), i }); }
		std::sort(order.begin(), order.end());
		Face f;
		for (auto& o : order) f.V.push_back(o.second);
		f.Sel = true;
		Faces.push_back(f);
		Touch();
		RecalculateNormals();
		return 1;
	}

	int Mesh::BridgeEdgeLoops()
	{
		// 선택한 경계 변을 이어진 고리 두 개로 나누고, 점 수가 같으면 가까운 순서로 사각형을 잇는다
		const auto& edges = Edges();
		std::unordered_map<int, std::vector<int>> adj;
		for (int e : SelectedEdgeIndices()) { adj[edges[e].A].push_back(edges[e].B); adj[edges[e].B].push_back(edges[e].A); }
		std::vector<std::vector<int>> loops;
		std::unordered_set<int> used;
		for (auto& [start, nb] : adj)
		{
			if (used.count(start)) continue;
			std::vector<int> loop = { start };
			used.insert(start);
			int prev = -1, cur = start;
			for (;;)
			{
				int next = -1;
				for (int x : adj[cur]) if (x != prev && !used.count(x)) { next = x; break; }
				if (next < 0) break;
				loop.push_back(next);
				used.insert(next);
				prev = cur; cur = next;
			}
			loops.push_back(loop);
		}
		if (loops.size() != 2 || loops[0].size() != loops[1].size() || loops[0].size() < 3)
			return 0;
		auto& a = loops[0];
		auto& b = loops[1];
		const size_t n = a.size();
		// b 의 시작과 방향: 거리 합이 가장 작게
		float best = FLT_MAX; int bestShift = 0; bool bestRev = false;
		for (int rev = 0; rev < 2; ++rev)
			for (size_t s = 0; s < n; ++s)
			{
				float d = 0;
				for (size_t i = 0; i < n; ++i)
				{
					const int j = rev ? (int)((s + n - i) % n) : (int)((s + i) % n);
					d += (Verts[a[i]].P - Verts[b[j]].P).LengthSquared();
				}
				if (d < best) { best = d; bestShift = (int)s; bestRev = rev != 0; }
			}
		std::vector<int> bb(n);
		for (size_t i = 0; i < n; ++i) bb[i] = b[bestRev ? (bestShift + n - i) % n : (bestShift + i) % n];
		for (size_t i = 0; i < n; ++i)
		{
			Face f; f.V = { a[i], a[(i + 1) % n], bb[(i + 1) % n], bb[i] }; f.Sel = true;
			Faces.push_back(f);
		}
		Touch();
		RecalculateNormals();
		return (int)n;
	}

	// ------------------------------------------------------------------ 대칭
	int Mesh::Mirror(int axis, bool merge, float mergeDist)
	{
		axis = std::clamp(axis, 0, 2);
		const int base = (int)Verts.size(), nf = (int)Faces.size();
		// X 거울이면 좌우 그룹 이름을 바꿔 단다 (Blender: Hand.L ↔ Hand.R) — 없으면 만든다
		const int groupCount = (int)Groups.size();   // 아래에서 .R 그룹이 늘어난다 — 원래 것만 돈다
		std::vector<int> swap(groupCount);
		for (int g = 0; g < groupCount; ++g)
		{
			swap[g] = g;
			if (axis != 0) continue;
			const std::string n = Groups[g];   // 복사 (AddGroup 이 목록을 키운다)
			static const std::pair<const char*, const char*> sides[] = { { ".L", ".R" }, { "_L", "_R" }, { ".l", ".r" }, { "_l", "_r" }, { "Left", "Right" } };
			for (const auto& [l, r] : sides)
			{
				const std::string sl = l, sr = r;
				if (n.size() > sl.size() && n.compare(n.size() - sl.size(), sl.size(), sl) == 0) { swap[g] = AddGroup(n.substr(0, n.size() - sl.size()) + sr); break; }
				if (n.size() > sr.size() && n.compare(n.size() - sr.size(), sr.size(), sr) == 0) { swap[g] = AddGroup(n.substr(0, n.size() - sr.size()) + sl); break; }
			}
		}
		for (int i = 0; i < base; ++i)
		{
			Vec3 p = Verts[i].P;
			(axis == 0 ? p.x : (axis == 1 ? p.y : p.z)) *= -1.0f;
			Vert v = CopyVert(Verts[i], p, false);
			for (auto& x : v.W) if (x.first < (int)swap.size()) x.first = swap[x.first];
			Verts.push_back(v);
		}
		for (int f = 0; f < nf; ++f)
		{
			Face g = Faces[f];
			for (int& i : g.V) i += base;
			std::reverse(g.V.begin(), g.V.end());   // 뒤집었으니 감김도 반대로
			std::reverse(g.UV.begin(), g.UV.end());
			g.Sel = false;
			Faces.push_back(g);
		}
		Touch();
		int merged = 0;
		if (merge)
		{
			// 가운데 평면 위의 점만 용접
			for (Vert& v : Verts) v.Sel = fabsf(axis == 0 ? v.P.x : (axis == 1 ? v.P.y : v.P.z)) <= mergeDist;
			merged = MergeByDistance(mergeDist, true);
			for (Vert& v : Verts) v.Sel = false;
		}
		Flush(SelectMode::Vertex);
		(void)merged;
		return nf;
	}

	int Mesh::Symmetrize(int axis, bool positiveToNegative)
	{
		// 한쪽 반을 지우고 다른 반을 뒤집어 복사
		axis = std::clamp(axis, 0, 2);
		auto coord = [&](const Vec3& p) { return axis == 0 ? p.x : (axis == 1 ? p.y : p.z); };
		const float eps = 1e-4f;
		for (Vert& v : Verts)
		{
			const float c = coord(v.P);
			v.Sel = positiveToNegative ? c < -eps : c > eps;
		}
		Flush(SelectMode::Vertex);
		DeleteVerts();
		for (Vert& v : Verts) if (fabsf(coord(v.P)) <= eps) (axis == 0 ? v.P.x : (axis == 1 ? v.P.y : v.P.z)) = 0.0f;
		return Mirror(axis, true, eps);
	}

	// ------------------------------------------------------------------ 법선 · 부드럽게
	int Mesh::FlipNormals()
	{
		int n = 0;
		const bool any = !SelectedFaces().empty();
		for (Face& f : Faces)
			if (f.Sel || !any) { std::reverse(f.V.begin(), f.V.end()); std::reverse(f.UV.begin(), f.UV.end()); ++n; }
		Touch();
		return n;
	}

	int Mesh::RecalculateNormals(bool inside)
	{
		// 이웃끼리 감김을 맞추고 (BFS), 덩어리마다 부호 있는 부피가 음수면 뒤집는다
		const auto& edges = Edges();
		const int nf = (int)Faces.size();
		std::vector<int> comp(nf, -1);
		int flipped = 0, comps = 0;
		for (int start = 0; start < nf; ++start)
		{
			if (comp[start] >= 0) continue;
			std::vector<int> members;
			std::queue<int> q;
			q.push(start);
			comp[start] = comps;
			while (!q.empty())
			{
				const int f = q.front(); q.pop();
				members.push_back(f);
				const auto& v = Faces[f].V;
				for (size_t i = 0; i < v.size(); ++i)
				{
					const int a = v[i], b = v[(i + 1) % v.size()];
					const int e = FindEdge(a, b);
					if (e < 0) continue;
					for (int g : edges[e].Faces)
					{
						if (g == f || comp[g] >= 0) continue;
						// 이웃 g 는 b→a 로 돌아야 한다
						const auto& w = Faces[g].V;
						bool sameDir = false;
						for (size_t k = 0; k < w.size(); ++k) if (w[k] == a && w[(k + 1) % w.size()] == b) sameDir = true;
						if (sameDir) { std::reverse(Faces[g].V.begin(), Faces[g].V.end()); std::reverse(Faces[g].UV.begin(), Faces[g].UV.end()); ++flipped; }
						comp[g] = comps;
						q.push(g);
					}
				}
			}
			// 닫힌 덩어리: 부피 (덩어리 중심과 잇는 사면체 합) 가 + 면 바깥을 향한다.
			// 열린 덩어리 (평면 등): 부피가 뜻이 없으니 첫 면 방향을 그대로 둔다
			bool closed = true;
			Vec3 center(0, 0, 0);
			int count = 0;
			for (int f : members)
			{
				const auto& v = Faces[f].V;
				for (size_t i = 0; i < v.size(); ++i)
				{
					center += Verts[v[i]].P; ++count;
					const int e = FindEdge(v[i], v[(i + 1) % v.size()]);
					if (e < 0 || edges[e].Faces.size() < 2) closed = false;
				}
			}
			if (count) center /= (float)count;
			double vol = 0.0;
			for (int f : members)
			{
				const auto& v = Faces[f].V;
				for (size_t i = 1; i + 1 < v.size(); ++i)
				{
					const Vec3 a = Verts[v[0]].P - center, b = Verts[v[i]].P - center, c = Verts[v[i + 1]].P - center;
					vol += a.Dot(b.Cross(c));
				}
			}
			const bool outward = closed ? vol >= 0.0 : true;
			if (outward == inside)
				for (int f : members) { std::reverse(Faces[f].V.begin(), Faces[f].V.end()); std::reverse(Faces[f].UV.begin(), Faces[f].UV.end()); ++flipped; }
			++comps;
		}
		Touch();
		return flipped;
	}

	int Mesh::Smooth(float factor, int iterations)
	{
		const std::vector<int> sel = SelectedVerts();
		if (sel.empty()) return 0;
		std::vector<std::vector<int>> adj(Verts.size());
		for (const Edge& e : Edges()) { adj[e.A].push_back(e.B); adj[e.B].push_back(e.A); }
		for (int it = 0; it < std::clamp(iterations, 1, 100); ++it)
		{
			std::vector<Vec3> np(Verts.size());
			for (int i : sel)
			{
				if (adj[i].empty()) { np[i] = Verts[i].P; continue; }
				Vec3 avg(0, 0, 0);
				for (int j : adj[i]) avg += Verts[j].P;
				avg /= (float)adj[i].size();
				np[i] = Verts[i].P + (avg - Verts[i].P) * std::clamp(factor, 0.0f, 1.0f);
			}
			for (int i : sel) Verts[i].P = np[i];
		}
		return (int)sel.size();
	}

	int Mesh::SetSmooth(bool smooth)
	{
		int n = 0;
		const bool any = !SelectedFaces().empty();
		for (Face& f : Faces) if (!any || f.Sel) { f.Smooth = smooth; ++n; }
		return n;
	}

	int Mesh::Triangulate()
	{
		std::vector<Face> out;
		int made = 0;
		for (int f = 0; f < (int)Faces.size(); ++f)
		{
			const Face& face = Faces[f];
			if (!face.Sel || face.V.size() <= 3) { out.push_back(face); continue; }
			std::vector<std::array<int, 3>> tris;
			Modeling::Triangulate(*this, f, tris);
			for (auto& t : tris)
			{
				Face g = face;
				g.V = { face.V[t[0]], face.V[t[1]], face.V[t[2]] };
				g.UV = face.UV.size() == face.V.size() ? std::vector<Vec2>{ face.UV[t[0]], face.UV[t[1]], face.UV[t[2]] } : std::vector<Vec2>();
				out.push_back(g);
				++made;
			}
		}
		Faces = out;
		Touch();
		Flush(SelectMode::Face);
		return made;
	}

	int Mesh::Duplicate()
	{
		const std::vector<int> faces = SelectedFaces();
		if (faces.empty()) return 0;
		std::unordered_map<int, int> dup;
		std::vector<Face> add;
		for (int f : faces)
		{
			Face g = Faces[f];
			for (int& i : g.V)
			{
				if (!dup.count(i)) { Verts.push_back(CopyVert(Verts[i], Verts[i].P)); dup[i] = (int)Verts.size() - 1; }
				i = dup[i];
			}
			add.push_back(g);
		}
		DeselectAll();
		for (Face& g : add) { g.Sel = true; Faces.push_back(g); }
		Touch();
		Flush(SelectMode::Face);
		return (int)add.size();
	}

	void Mesh::Cleanup()
	{
		// 3 점 미만 면, 쓰지 않는 점 지우기
		Faces.erase(std::remove_if(Faces.begin(), Faces.end(), [](const Face& f) { return f.V.size() < 3; }), Faces.end());
		std::vector<int> used(Verts.size(), 0);
		for (const Face& f : Faces) for (int i : f.V) used[i] = 1;
		std::vector<int> remap(Verts.size(), -1);
		std::vector<Vert> verts;
		for (int i = 0; i < (int)Verts.size(); ++i)
			if (used[i]) { remap[i] = (int)verts.size(); verts.push_back(Verts[i]); }
		if (verts.size() == Verts.size())
		{
			Touch();
			return;
		}
		for (Face& f : Faces) for (int& i : f.V) i = remap[i];
		std::unordered_set<uint64> sel;
		for (uint64 k : SelEdges)
		{
			const int a = remap[(int)(k >> 32)], b = remap[(int)(k & 0xFFFFFFFF)];
			if (a >= 0 && b >= 0) sel.insert(EdgeKey(a, b));
		}
		SelEdges = sel;
		Verts = verts;
		Touch();
	}

	int Mesh::NonManifoldEdges()
	{
		int n = 0;
		for (const Edge& e : Edges()) if (e.Faces.size() > 2) ++n;
		return n;
	}

	int Mesh::BoundaryEdges()
	{
		int n = 0;
		for (const Edge& e : Edges()) if (e.Faces.size() == 1) ++n;
		return n;
	}

	// ------------------------------------------------------------------ 저장
	nlohmann::json Mesh::ToJson(bool withSelection) const
	{
		nlohmann::json j;
		std::vector<float> p;
		p.reserve(Verts.size() * 3);
		for (const Vert& v : Verts) { p.push_back(v.P.x); p.push_back(v.P.y); p.push_back(v.P.z); }
		j["positions"] = p;
		nlohmann::json faces = nlohmann::json::array();
		for (const Face& f : Faces)
		{
			nlohmann::json fj = { { "v", f.V } };
			if (f.Smooth) fj["smooth"] = true;
			if (f.Material) fj["material"] = f.Material;
			if (!f.UV.empty())
			{
				std::vector<float> uv;
				for (const Vec2& t : f.UV) { uv.push_back(t.x); uv.push_back(t.y); }
				fj["uv"] = uv;
			}
			faces.push_back(fj);
		}
		j["faces"] = faces;
		if (!Groups.empty())
		{
			// 그룹마다 [[점, 가중치], …]
			nlohmann::json gj = nlohmann::json::array();
			for (int g = 0; g < (int)Groups.size(); ++g)
			{
				nlohmann::json members = nlohmann::json::array();
				for (int i = 0; i < (int)Verts.size(); ++i)
					for (const auto& x : Verts[i].W)
						if (x.first == g) members.push_back({ i, x.second });
				gj.push_back({ { "name", Groups[g] }, { "verts", members } });
			}
			j["groups"] = gj;
		}
		if (withSelection)
		{
			std::vector<int> vs, fs;
			std::vector<uint64> es(SelEdges.begin(), SelEdges.end());
			for (int i = 0; i < (int)Verts.size(); ++i) if (Verts[i].Sel) vs.push_back(i);
			for (int i = 0; i < (int)Faces.size(); ++i) if (Faces[i].Sel) fs.push_back(i);
			j["vsel"] = vs;
			j["fsel"] = fs;
			j["esel"] = es;
		}
		return j;
	}

	void Mesh::FromJson(const nlohmann::json& j)
	{
		Verts.clear();
		Faces.clear();
		SelEdges.clear();
		Groups.clear();
		if (j.contains("positions"))
		{
			const auto& p = j["positions"];
			for (size_t i = 0; i + 2 < p.size(); i += 3) Verts.push_back({ Vec3(p[i].get<float>(), p[i + 1].get<float>(), p[i + 2].get<float>()), false });
		}
		if (j.contains("faces"))
			for (const auto& fj : j["faces"])
			{
				Face f;
				f.V = fj.value("v", std::vector<int>());
				f.Smooth = fj.value("smooth", false);
				f.Material = fj.value("material", 0);
				if (fj.contains("uv"))
				{
					const auto& uv = fj["uv"];
					for (size_t i = 0; i + 1 < uv.size(); i += 2) f.UV.push_back(Vec2(uv[i].get<float>(), uv[i + 1].get<float>()));
				}
				bool ok = f.V.size() >= 3;
				for (int i : f.V) ok = ok && i >= 0 && i < (int)Verts.size();
				if (ok) Faces.push_back(f);
			}
		if (j.contains("groups"))
			for (const auto& gj : j["groups"])
			{
				const int g = (int)Groups.size();
				Groups.push_back(gj.value("name", std::string("Group")));
				if (gj.contains("verts"))
					for (const auto& m : gj["verts"])
					{
						const int i = m[0].get<int>();
						if (i >= 0 && i < (int)Verts.size()) Verts[i].W.push_back({ g, m[1].get<float>() });
					}
			}
		if (j.contains("vsel")) for (int i : j["vsel"]) if (i >= 0 && i < (int)Verts.size()) Verts[i].Sel = true;
		if (j.contains("fsel")) for (int i : j["fsel"]) if (i >= 0 && i < (int)Faces.size()) Faces[i].Sel = true;
		if (j.contains("esel")) for (uint64 k : j["esel"]) SelEdges.insert(k);
		Touch();
	}

	void Mesh::Append(const Mesh& other, const Matrix& transform)
	{
		const int base = (int)Verts.size();
		std::vector<int> remap(other.Groups.size());
		for (size_t g = 0; g < other.Groups.size(); ++g) remap[g] = AddGroup(other.Groups[g]);
		for (const Vert& v : other.Verts)
		{
			Vert c = CopyVert(v, Vec3::Transform(v.P, transform), v.Sel);
			for (auto& x : c.W) x.first = remap[x.first];
			Verts.push_back(c);
		}
		for (const Face& f : other.Faces) { Face g = f; for (int& i : g.V) i += base; Faces.push_back(g); }
		Touch();
	}

	// ------------------------------------------------------------------ 삼각형 나누기 (귀 자르기, 평면 투영)
	void Triangulate(const Mesh& m, int face, std::vector<std::array<int, 3>>& out)
	{
		out.clear();
		const auto& v = m.Faces[face].V;
		const int n = (int)v.size();
		if (n < 3) return;
		if (n == 3) { out.push_back({ 0, 1, 2 }); return; }
		const Vec3 nrm = m.FaceNormal(face);
		Vec3 u, w;
		Basis(nrm, u, w);
		std::vector<Vec2> p(n);
		for (int i = 0; i < n; ++i) { const Vec3 q = m.Verts[v[i]].P; p[i] = Vec2(q.Dot(u), q.Dot(w)); }
		// 투영 다각형의 방향 (부호 넓이)
		float area = 0;
		for (int i = 0; i < n; ++i) area += p[i].x * p[(i + 1) % n].y - p[(i + 1) % n].x * p[i].y;
		const float sign = area >= 0 ? 1.0f : -1.0f;
		std::vector<int> idx(n);
		std::iota(idx.begin(), idx.end(), 0);
		auto cross = [&](int a, int b, int c) { return ((p[b].x - p[a].x) * (p[c].y - p[a].y) - (p[b].y - p[a].y) * (p[c].x - p[a].x)) * sign; };
		int guard = 0;
		while (idx.size() > 3 && guard++ < 10000)
		{
			bool clipped = false;
			for (size_t i = 0; i < idx.size(); ++i)
			{
				const int a = idx[(i + idx.size() - 1) % idx.size()], b = idx[i], c = idx[(i + 1) % idx.size()];
				if (cross(a, b, c) <= 1e-12f) continue;
				bool inside = false;
				for (int k : idx)
				{
					if (k == a || k == b || k == c) continue;
					if (cross(a, b, k) >= 0 && cross(b, c, k) >= 0 && cross(c, a, k) >= 0) { inside = true; break; }
				}
				if (inside) continue;
				out.push_back({ a, b, c });
				idx.erase(idx.begin() + i);
				clipped = true;
				break;
			}
			if (!clipped)
				break;
		}
		if (idx.size() == 3)
			out.push_back({ idx[0], idx[1], idx[2] });
		else if (idx.size() > 3)
			for (size_t i = 1; i + 1 < idx.size(); ++i) out.push_back({ idx[0], idx[i], idx[i + 1] });   // 꼬인 면: 부채꼴
	}
}
