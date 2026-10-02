// 메시 도구 (3 단계: 캐릭터): 비례 편집 감쇠, UV 투영 · Smart UV Project, 머리카락 다발 · 카드
#include "pch.h"
#include "ModelMesh.h"
#include <numeric>
#include <queue>

namespace Modeling
{
	namespace
	{
		constexpr float kPi = 3.14159265358979f;

		void PlaneBasis(const Vec3& n, Vec3& u, Vec3& v)
		{
			u = fabsf(n.y) < 0.9f ? Vec3(0, 1, 0).Cross(n) : Vec3(1, 0, 0).Cross(n);
			u.Normalize();
			v = n.Cross(u);
		}

		// 고른 면 (없으면 전체)
		std::vector<int> TargetFaces(const Mesh& m)
		{
			std::vector<int> f = m.SelectedFaces();
			if (f.empty()) { f.resize(m.Faces.size()); std::iota(f.begin(), f.end(), 0); }
			return f;
		}

		// 면들의 UV 를 0..1 에 맞춘다 (비율 유지)
		void FitUV(Mesh& m, const std::vector<int>& faces, float margin)
		{
			Vec2 mn(FLT_MAX, FLT_MAX), mx(-FLT_MAX, -FLT_MAX);
			for (int f : faces) for (const Vec2& t : m.Faces[f].UV) { mn = Vec2::Min(mn, t); mx = Vec2::Max(mx, t); }
			const float size = (std::max)((std::max)(mx.x - mn.x, mx.y - mn.y), 1e-8f);
			const float s = (1.0f - 2.0f * margin) / size;
			for (int f : faces) for (Vec2& t : m.Faces[f].UV) t = Vec2(margin, margin) + (t - mn) * s;
		}
	}

	Falloff FalloffFromName(const std::string& n)
	{
		if (n == "sphere") return Falloff::Sphere;
		if (n == "root") return Falloff::Root;
		if (n == "sharp") return Falloff::Sharp;
		if (n == "linear") return Falloff::Linear;
		if (n == "constant") return Falloff::Constant;
		return Falloff::Smooth;
	}

	// ------------------------------------------------------------------ 비례 편집
	std::vector<float> Mesh::ProportionalWeights(float radius, Falloff falloff) const
	{
		std::vector<float> w(Verts.size(), 0.0f);
		std::vector<Vec3> sel;
		for (size_t i = 0; i < Verts.size(); ++i)
			if (Verts[i].Sel) { w[i] = 1.0f; sel.push_back(Verts[i].P); }
		if (sel.empty() || radius <= 0.0f)
			return w;
		// 격자로 고른 점 찾기 (칸 = 반경)
		std::unordered_map<int64_t, std::vector<int>> grid;
		auto key = [&](int64_t x, int64_t y, int64_t z) { return (x * 73856093) ^ (y * 19349663) ^ (z * 83492791); };
		auto cell = [&](float v) { return (int64_t)floorf(v / radius); };
		for (int i = 0; i < (int)sel.size(); ++i) grid[key(cell(sel[i].x), cell(sel[i].y), cell(sel[i].z))].push_back(i);
		for (size_t i = 0; i < Verts.size(); ++i)
		{
			if (Verts[i].Sel) continue;
			const Vec3& p = Verts[i].P;
			float best = radius * radius;
			bool found = false;
			const int64_t cx = cell(p.x), cy = cell(p.y), cz = cell(p.z);
			for (int64_t dx = -1; dx <= 1; ++dx)
				for (int64_t dy = -1; dy <= 1; ++dy)
					for (int64_t dz = -1; dz <= 1; ++dz)
					{
						auto it = grid.find(key(cx + dx, cy + dy, cz + dz));
						if (it == grid.end()) continue;
						for (int s : it->second)
						{
							const float d2 = (sel[s] - p).LengthSquared();
							if (d2 < best) { best = d2; found = true; }
						}
					}
			if (!found) continue;
			const float x = sqrtf(best) / radius, t = 1.0f - x;
			switch (falloff)
			{
			case Falloff::Sphere: w[i] = sqrtf((std::max)(0.0f, 1.0f - x * x)); break;
			case Falloff::Root: w[i] = sqrtf(t); break;
			case Falloff::Sharp: w[i] = t * t; break;
			case Falloff::Linear: w[i] = t; break;
			case Falloff::Constant: w[i] = 1.0f; break;
			default: w[i] = t * t * (3.0f - 2.0f * t); break;
			}
		}
		return w;
	}

	// ------------------------------------------------------------------ UV
	int Mesh::FacesWithUV() const
	{
		int n = 0;
		for (const Face& f : Faces) n += !f.UV.empty() && f.UV.size() == f.V.size();
		return n;
	}

	int Mesh::ProjectUV(int mode)
	{
		const std::vector<int> faces = TargetFaces(*this);
		if (faces.empty()) return 0;
		Vec3 mn(FLT_MAX, FLT_MAX, FLT_MAX), mx(-FLT_MAX, -FLT_MAX, -FLT_MAX), avgN(0, 0, 0);
		for (int f : faces) { for (int v : Faces[f].V) { mn = Vec3::Min(mn, Verts[v].P); mx = Vec3::Max(mx, Verts[v].P); } avgN += FaceNormal(f); }
		const Vec3 c = (mn + mx) * 0.5f;
		const float r = (std::max)((mx - mn).Length() * 0.5f, 1e-6f);
		Vec3 pu, pv;
		if (avgN.LengthSquared() < 1e-12f) avgN = Vec3(0, 0, 1);
		avgN.Normalize();
		PlaneBasis(avgN, pu, pv);
		for (int f : faces)
		{
			Face& face = Faces[f];
			face.UV.clear();
			const Vec3 n = FaceNormal(f);
			for (int v : face.V)
			{
				const Vec3 p = Verts[v].P - c;
				Vec2 t;
				if (mode == 0)
				{
					// 면 법선이 가장 큰 축으로 (Cube Projection)
					const float ax = fabsf(n.x), ay = fabsf(n.y), az = fabsf(n.z);
					if (ax >= ay && ax >= az) t = Vec2(n.x > 0 ? -p.z : p.z, -p.y);
					else if (ay >= az) t = Vec2(p.x, n.y > 0 ? p.z : -p.z);
					else t = Vec2(n.z > 0 ? p.x : -p.x, -p.y);
				}
				else if (mode == 1) t = Vec2(atan2f(p.x, p.z) / (2.0f * kPi) + 0.5f, -p.y / (2.0f * r));
				else if (mode == 2) t = Vec2(atan2f(p.x, p.z) / (2.0f * kPi) + 0.5f, 0.5f - asinf(std::clamp(p.y / (std::max)(p.Length(), 1e-8f), -1.0f, 1.0f)) / kPi);
				else t = Vec2(p.Dot(pu), -p.Dot(pv));
				face.UV.push_back(t);
			}
			if (mode == 1 || mode == 2)
			{
				// 이음매를 넘는 면: u 가 0.5 넘게 벌어지면 작은 쪽에 +1
				float lo = FLT_MAX, hi = -FLT_MAX;
				for (const Vec2& t : face.UV) { lo = (std::min)(lo, t.x); hi = (std::max)(hi, t.x); }
				if (hi - lo > 0.5f) for (Vec2& t : face.UV) if (t.x < 0.5f) t.x += 1.0f;
			}
		}
		if (mode == 0 || mode == 3) FitUV(*this, faces, 0.0f);
		else
		{
			// 원기둥 · 구: u 는 그대로 (0..1 둘레), v 만 0..1 로
			float lo = FLT_MAX, hi = -FLT_MAX;
			for (int f : faces) for (const Vec2& t : Faces[f].UV) { lo = (std::min)(lo, t.y); hi = (std::max)(hi, t.y); }
			for (int f : faces) for (Vec2& t : Faces[f].UV) t.y = (t.y - lo) / (std::max)(hi - lo, 1e-8f);
		}
		return (int)faces.size();
	}

	int Mesh::SmartUV(float angleDeg, float margin)
	{
		const std::vector<int> faces = TargetFaces(*this);
		if (faces.empty()) return 0;
		std::unordered_set<int> inSet(faces.begin(), faces.end());
		const float cosLimit = cosf(std::clamp(angleDeg, 1.0f, 89.0f) * kPi / 180.0f);
		const auto& edges = Edges();
		std::vector<Vec3> normal(Faces.size());
		std::vector<float> area(Faces.size(), 0.0f);
		for (int f : faces)
		{
			normal[f] = FaceNormal(f);
			Vec3 a(0, 0, 0);
			const auto& v = Faces[f].V;
			for (size_t i = 1; i + 1 < v.size(); ++i) a += (Verts[v[i]].P - Verts[v[0]].P).Cross(Verts[v[i + 1]].P - Verts[v[0]].P);
			area[f] = a.Length() * 0.5f;
		}
		// 덩어리: 넓은 면부터 씨앗, 변으로 이웃한 면 중 법선이 씨앗과 각도 안인 것
		std::vector<int> order = faces;
		std::sort(order.begin(), order.end(), [&](int a, int b) { return area[a] > area[b]; });
		std::unordered_map<int, int> chartOf;
		std::vector<std::vector<int>> charts;
		for (int seed : order)
		{
			if (chartOf.count(seed)) continue;
			const int id = (int)charts.size();
			charts.push_back({});
			std::queue<int> q;
			q.push(seed);
			chartOf[seed] = id;
			while (!q.empty())
			{
				const int f = q.front(); q.pop();
				charts[id].push_back(f);
				const auto& v = Faces[f].V;
				for (size_t i = 0; i < v.size(); ++i)
				{
					const int e = FindEdge(v[i], v[(i + 1) % v.size()]);
					if (e < 0) continue;
					for (int g : edges[e].Faces)
						if (inSet.count(g) && !chartOf.count(g) && normal[g].Dot(normal[seed]) >= cosLimit) { chartOf[g] = id; q.push(g); }
				}
			}
		}
		// 덩어리마다 평균 법선 평면에 투영
		struct Box { float W, H; int Chart; float X = 0, Y = 0; };
		std::vector<Box> boxes;
		float totalArea = 0.0f;
		for (int id = 0; id < (int)charts.size(); ++id)
		{
			Vec3 n(0, 0, 0);
			for (int f : charts[id]) n += normal[f] * area[f];
			if (n.LengthSquared() < 1e-12f) n = normal[charts[id][0]];
			n.Normalize();
			Vec3 u, v;
			PlaneBasis(n, u, v);
			Vec2 mn(FLT_MAX, FLT_MAX), mx(-FLT_MAX, -FLT_MAX);
			for (int f : charts[id])
			{
				Faces[f].UV.clear();
				for (int vi : Faces[f].V)
				{
					const Vec2 t(Verts[vi].P.Dot(u), -Verts[vi].P.Dot(v));
					Faces[f].UV.push_back(t);
					mn = Vec2::Min(mn, t); mx = Vec2::Max(mx, t);
				}
			}
			for (int f : charts[id]) for (Vec2& t : Faces[f].UV) t -= mn;
			boxes.push_back({ (std::max)(mx.x - mn.x, 1e-6f), (std::max)(mx.y - mn.y, 1e-6f), id });
			totalArea += boxes.back().W * boxes.back().H;
		}
		// 선반 쌓기: 높은 것부터, 너비 = 넓이의 제곱근 × 1.3
		const float pad = margin * sqrtf((std::max)(totalArea, 1e-12f));
		float maxW = 0.0f;
		for (const Box& b : boxes) maxW = (std::max)(maxW, b.W + pad * 2);
		const float rowW = (std::max)(sqrtf(totalArea) * 1.3f, maxW);
		std::vector<int> byH(boxes.size());
		std::iota(byH.begin(), byH.end(), 0);
		std::sort(byH.begin(), byH.end(), [&](int a, int b) { return boxes[a].H > boxes[b].H; });
		float x = 0, y = 0, shelfH = 0, usedW = 0;
		for (int i : byH)
		{
			Box& b = boxes[i];
			if (x > 0 && x + b.W + pad * 2 > rowW) { x = 0; y += shelfH; shelfH = 0; }
			b.X = x + pad;
			b.Y = y + pad;
			x += b.W + pad * 2;
			shelfH = (std::max)(shelfH, b.H + pad * 2);
			usedW = (std::max)(usedW, x);
		}
		const float usedH = y + shelfH;
		const float s = 1.0f / (std::max)((std::max)(usedW, usedH), 1e-8f);
		for (const Box& b : boxes)
			for (int f : charts[b.Chart])
				for (Vec2& t : Faces[f].UV) t = Vec2((t.x + b.X) * s, (t.y + b.Y) * s);
		return (int)charts.size();
	}

	// ------------------------------------------------------------------ 머리카락
	int Mesh::AddStrand(const std::vector<Vec3>& points, float width, float thickness, float tip, int sides, const Vec3& center, int segments)
	{
		if (points.size() < 2) return 0;
		const int n = (int)points.size();
		segments = segments > 0 ? segments : (std::max)(4, (n - 1) * 4);
		// Catmull-Rom 으로 고르게 나눈 중심선
		auto cr = [&](int k, float s) {
			const Vec3& p0 = points[(std::max)(0, k - 1)], &p1 = points[k], &p2 = points[(std::min)(n - 1, k + 1)], &p3 = points[(std::min)(n - 1, k + 2)];
			const float s2 = s * s, s3 = s2 * s;
			return (p1 * 2.0f + (p2 - p0) * s + (p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * s2 + (p1 * 3.0f - p0 - p2 * 3.0f + p3) * s3) * 0.5f;
		};
		std::vector<Vec3> P(segments + 1);
		for (int i = 0; i <= segments; ++i)
		{
			const float u = i / (float)segments * (n - 1);
			const int k = (std::min)((int)floorf(u), n - 2);
			P[i] = cr(k, u - k);
		}
		// 단면 축: T 진행, O 바깥 (center 에서 멀어지는 쪽), S = 옆
		std::vector<Vec3> T(segments + 1), O(segments + 1), S(segments + 1);
		Vec3 prevO(0, 1, 0);
		for (int i = 0; i <= segments; ++i)
		{
			Vec3 t = P[(std::min)(i + 1, segments)] - P[(std::max)(i - 1, 0)];
			if (t.LengthSquared() < 1e-12f) t = Vec3(0, -1, 0);
			t.Normalize();
			Vec3 o = P[i] - center;
			o -= t * o.Dot(t);
			if (o.LengthSquared() < 1e-10f) o = prevO - t * prevO.Dot(t);
			if (o.LengthSquared() < 1e-10f) o = fabsf(t.y) < 0.9f ? Vec3(0, 1, 0) : Vec3(1, 0, 0);
			o.Normalize();
			prevO = o;
			Vec3 s = t.Cross(o);
			s.Normalize();
			T[i] = t; O[i] = o; S[i] = s;
		}
		auto taper = [&](int i) { const float t = i / (float)segments; return 1.0f - (1.0f - std::clamp(tip, 0.0f, 1.0f)) * powf(t, 1.5f); };
		const bool pointTip = tip < 1e-3f;
		for (Vert& v : Verts) v.Sel = false;
		for (Face& f : Faces) f.Sel = false;
		SelEdges.clear();
		const int base = (int)Verts.size(), firstFace = (int)Faces.size();
		auto addV = [&](const Vec3& p) { Vert v; v.P = p; v.Sel = true; Verts.push_back(v); return (int)Verts.size() - 1; };
		auto addF = [&](std::vector<int> vs, std::vector<Vec2> uvs) { Face f; f.V = std::move(vs); f.UV = std::move(uvs); f.Sel = true; f.Smooth = true; Faces.push_back(f); };
		if (sides < 3)
		{
			// 카드: 띠 (L, R)
			std::vector<int> L, R;
			for (int i = 0; i <= segments; ++i)
			{
				const float k = taper(i) * width * 0.5f;
				if (i == segments && pointTip) { const int t = addV(P[i]); L.push_back(t); R.push_back(t); }
				else { L.push_back(addV(P[i] - S[i] * k)); R.push_back(addV(P[i] + S[i] * k)); }
			}
			for (int i = 0; i < segments; ++i)
			{
				const float v0 = i / (float)segments, v1 = (i + 1) / (float)segments;
				if (L[i + 1] == R[i + 1]) addF({ L[i], L[i + 1], R[i] }, { Vec2(0, v0), Vec2(0.5f, v1), Vec2(1, v0) });
				else addF({ L[i], L[i + 1], R[i + 1], R[i] }, { Vec2(0, v0), Vec2(0, v1), Vec2(1, v1), Vec2(1, v0) });
			}
			// 앞면 = 바깥 (O) 쪽
			if (FaceNormal(firstFace).Dot(O[0]) < 0.0f)
				for (int f = firstFace; f < (int)Faces.size(); ++f) { std::reverse(Faces[f].V.begin(), Faces[f].V.end()); std::reverse(Faces[f].UV.begin(), Faces[f].UV.end()); }
		}
		else
		{
			// 다발: 고리마다 sides 점 (납작한 타원), 끝 = 한 점
			std::vector<std::vector<int>> ring(segments + 1);
			for (int i = 0; i <= segments; ++i)
			{
				if (i == segments && pointTip) { ring[i] = { addV(P[i]) }; continue; }
				const float k = taper(i);
				for (int j = 0; j < sides; ++j)
				{
					const float a = 2.0f * kPi * j / sides;
					ring[i].push_back(addV(P[i] + (S[i] * (cosf(a) * width * 0.5f) + O[i] * (sinf(a) * thickness * 0.5f)) * k));
				}
			}
			for (int i = 0; i < segments; ++i)
			{
				const float v0 = i / (float)segments, v1 = (i + 1) / (float)segments;
				for (int j = 0; j < sides; ++j)
				{
					const int j1 = (j + 1) % sides;
					const float u0 = j / (float)sides, u1 = (j + 1) / (float)sides;
					if (ring[i + 1].size() == 1) addF({ ring[i][j], ring[i + 1][0], ring[i][j1] }, { Vec2(u0, v0), Vec2((u0 + u1) * 0.5f, v1), Vec2(u1, v0) });
					else addF({ ring[i][j], ring[i + 1][j], ring[i + 1][j1], ring[i][j1] }, { Vec2(u0, v0), Vec2(u0, v1), Vec2(u1, v1), Vec2(u1, v0) });
				}
			}
			// 뿌리 막기 (끝이 뾰족하지 않으면 끝도)
			auto cap = [&](const std::vector<int>& r, float v) {
				std::vector<Vec2> uv;
				for (int j = 0; j < (int)r.size(); ++j) { const float a = 2.0f * kPi * j / r.size(); uv.push_back(Vec2(0.5f + 0.5f * cosf(a), v)); }
				addF(r, uv);
			};
			cap(ring[0], 0.0f);
			if (!pointTip) cap(ring[segments], 1.0f);
			// 바깥을 보게 (첫 옆면 법선이 중심선에서 멀어지는 쪽)
			const Vec3 fc = FaceCenter(firstFace);
			if (FaceNormal(firstFace).Dot(fc - P[0] - T[0] * (fc - P[0]).Dot(T[0])) < 0.0f)
				for (int f = firstFace; f < (int)Faces.size(); ++f) { std::reverse(Faces[f].V.begin(), Faces[f].V.end()); std::reverse(Faces[f].UV.begin(), Faces[f].UV.end()); }
			// 뿌리 뚜껑은 뒤 (-T) 를 보게
			const int capF = firstFace + segments * sides;
			if (capF < (int)Faces.size() && FaceNormal(capF).Dot(T[0]) > 0.0f) { std::reverse(Faces[capF].V.begin(), Faces[capF].V.end()); std::reverse(Faces[capF].UV.begin(), Faces[capF].UV.end()); }
			if (!pointTip && capF + 1 < (int)Faces.size() && FaceNormal(capF + 1).Dot(T[segments]) < 0.0f) { std::reverse(Faces[capF + 1].V.begin(), Faces[capF + 1].V.end()); std::reverse(Faces[capF + 1].UV.begin(), Faces[capF + 1].UV.end()); }
		}
		(void)base;
		Touch();
		Flush(SelectMode::Face);
		return (int)Faces.size() - firstFace;
	}
}
