#include "pch.h"
#include "BuildingGenerator.h"
#include <random>

using BD = BuildingDesc;

namespace
{
	using namespace BuildingGenerator;
	const Vec3 kUp(0.0f, 1.0f, 0.0f);

	Vec3 VCross(const Vec3& a, const Vec3& b) { return Vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
	float Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
	Vec3 Norm(const Vec3& a) { const float l = a.Length(); return l > 1e-9f ? a / l : Vec3(0, 1, 0); }

	// 노멀맵 규약: T = 면 위 가로 (N x 위), B = N x T 가 아래 / 비탈 아래. 누운 면은 T = +X
	Vec3 TangentOf(const Vec3& n)
	{
		const Vec3 t = VCross(n, kUp);
		return t.LengthSquared() > 1e-6f ? Norm(t) : Vec3(1, 0, 0);
	}

	// ================================================================ 메시 쌓기
	struct Mesher
	{
		Output& O;
		Vec3 Grain = Vec3(0, 0, 0);   // 상자 · 들보: 무늬 (나뭇결 · 돌 단) 를 긴 축으로 — 0 이면 면 위 가로
		explicit Mesher(Output& o) : O(o) {}
		static bool Woody(int slot) { return slot == Timber || slot == Frame || slot == Door || slot == Shutter; }   // 나뭇결을 긴 축으로

		uint32_t Vert(int slot, const Vec3& p, const Vec3& n, const Vec3& t, float u, float v)
		{
			Vertex::PosNormalTexTan2 x;
			x.pos = XMFLOAT3(p.x, p.y, p.z);
			x.normal = XMFLOAT3(n.x, n.y, n.z);
			x.tex = XMFLOAT2(u, v);
			x.tangentU = XMFLOAT4(t.x, t.y, t.z, 1.0f);
			Part& part = O.Parts[slot];
			part.V.push_back(x);
			return (uint32_t)part.V.size() - 1;
		}

		// 앞면 = 감김이 want 쪽 (엔진: cross(b-a, c-a) 가 보는 사람 쪽)
		void Tri(int slot, uint32_t a, uint32_t b, uint32_t c, const Vec3& want)
		{
			Part& part = O.Parts[slot];
			const XMFLOAT3& pa = part.V[a].pos;
			const XMFLOAT3& pb = part.V[b].pos;
			const XMFLOAT3& pc = part.V[c].pos;
			const Vec3 n = VCross(Vec3(pb.x - pa.x, pb.y - pa.y, pb.z - pa.z), Vec3(pc.x - pa.x, pc.y - pa.y, pc.z - pa.z));
			if (Dot(n, want) < 0.0f)
				std::swap(b, c);
			part.I.push_back(a); part.I.push_back(b); part.I.push_back(c);
		}

		// 볼록 다각형 (점 차례는 둘레를 따라, 감김은 n 으로 맞춘다). UV = 면 평면 투영 (미터)
		void Poly(int slot, const std::vector<Vec3>& pts, const Vec3& n)
		{
			if (pts.size() < 3)
				return;
			Vec3 t = TangentOf(n);
			if (Grain.LengthSquared() > 0.5f && fabsf(Dot(Grain, n)) < 0.95f)
				t = Norm(Grain - n * Dot(Grain, n));
			const Vec3 b = VCross(n, t);
			std::vector<uint32_t> idx;
			for (const Vec3& p : pts)
				idx.push_back(Vert(slot, p, n, t, Dot(p, t), Dot(p, b)));
			for (size_t k = 1; k + 1 < idx.size(); ++k)
				Tri(slot, idx[0], idx[k], idx[k + 1], n);
		}
		void Quad(int slot, const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d, const Vec3& n) { Poly(slot, { a, b, c, d }, n); }

		// 가운데에서 부채꼴 (원 · 아치 윤곽 — 끝점이 겹쳐도 된다)
		void Fan(int slot, const std::vector<Vec3>& pts, const Vec3& n)
		{
			if (pts.size() < 3)
				return;
			Vec3 c(0, 0, 0);
			for (const Vec3& p : pts)
				c += p;
			c /= (float)pts.size();
			const Vec3 t = TangentOf(n), b = VCross(n, t);
			const uint32_t ci = Vert(slot, c, n, t, Dot(c, t), Dot(c, b));
			std::vector<uint32_t> idx;
			for (const Vec3& p : pts)
				idx.push_back(Vert(slot, p, n, t, Dot(p, t), Dot(p, b)));
			for (size_t k = 0; k < idx.size(); ++k)
				Tri(slot, ci, idx[k], idx[(k + 1) % idx.size()], n);
		}

		// 축 상자
		void Box(int slot, const Vec3& mn, const Vec3& mx)
		{
			const float x0 = mn.x, y0 = mn.y, z0 = mn.z, x1 = mx.x, y1 = mx.y, z1 = mx.z;
			const Vec3 keep = Grain;
			const float ex = x1 - x0, ey = y1 - y0, ez = z1 - z0;
			if (Woody(slot))
				Grain = ex >= ey && ex >= ez ? Vec3(1, 0, 0) : (ey >= ez ? Vec3(0, 1, 0) : Vec3(0, 0, 1));
			struct Restore { Vec3& g; Vec3 v; ~Restore() { g = v; } } restore{ Grain, keep };
			Quad(slot, { x0, y0, z0 }, { x1, y0, z0 }, { x1, y1, z0 }, { x0, y1, z0 }, { 0, 0, -1 });
			Quad(slot, { x0, y0, z1 }, { x1, y0, z1 }, { x1, y1, z1 }, { x0, y1, z1 }, { 0, 0, 1 });
			Quad(slot, { x0, y0, z0 }, { x0, y0, z1 }, { x0, y1, z1 }, { x0, y1, z0 }, { -1, 0, 0 });
			Quad(slot, { x1, y0, z0 }, { x1, y0, z1 }, { x1, y1, z1 }, { x1, y1, z0 }, { 1, 0, 0 });
			Quad(slot, { x0, y1, z0 }, { x1, y1, z0 }, { x1, y1, z1 }, { x0, y1, z1 }, { 0, 1, 0 });
			Quad(slot, { x0, y0, z0 }, { x1, y0, z0 }, { x1, y0, z1 }, { x0, y0, z1 }, { 0, -1, 0 });
		}

		// 돌린 상자: 가운데 c, 축 ax · ay · az (단위), 반 크기
		void OBox(int slot, const Vec3& c, const Vec3& ax, const Vec3& ay, const Vec3& az, float hx, float hy, float hz, bool bottom = true)
		{
			auto P = [&](float sx, float sy, float sz) { return c + ax * (sx * hx) + ay * (sy * hy) + az * (sz * hz); };
			const Vec3 keep = Grain;
			if (Woody(slot))
				Grain = hx >= hy && hx >= hz ? ax : (hy >= hz ? ay : az);
			struct Restore { Vec3& g; Vec3 v; ~Restore() { g = v; } } restore{ Grain, keep };
			Quad(slot, P(-1, -1, -1), P(1, -1, -1), P(1, 1, -1), P(-1, 1, -1), -az);
			Quad(slot, P(-1, -1, 1), P(1, -1, 1), P(1, 1, 1), P(-1, 1, 1), az);
			Quad(slot, P(-1, -1, -1), P(-1, -1, 1), P(-1, 1, 1), P(-1, 1, -1), -ax);
			Quad(slot, P(1, -1, -1), P(1, -1, 1), P(1, 1, 1), P(1, 1, -1), ax);
			Quad(slot, P(-1, 1, -1), P(1, 1, -1), P(1, 1, 1), P(-1, 1, 1), ay);
			if (bottom)
				Quad(slot, P(-1, -1, -1), P(1, -1, -1), P(1, -1, 1), P(-1, -1, 1), -ay);
		}

		// 두 점 사이 들보 (폭 w — side 방향, 두께 d — out 방향)
		void Beam(int slot, const Vec3& p0, const Vec3& p1, float w, float d, const Vec3& out)
		{
			const Vec3 dir = p1 - p0;
			const float len = dir.Length();
			if (len < 1e-4f)
				return;
			const Vec3 ax = dir / len;
			const Vec3 az = Norm(out - ax * Dot(out, ax));
			const Vec3 ay = VCross(az, ax);
			OBox(slot, (p0 + p1) * 0.5f + az * (d * 0.5f), ax, ay, az, len * 0.5f, w * 0.5f, d * 0.5f);
		}

		// 원뿔대 · 원기둥 옆면 (매끈한 법선, UV = 둘레 m · 높이 m). r1 = 0 이면 원뿔
		void Frustum(int slot, const Vec3& base, float r0, float r1, float h, int sides, bool capTop, bool capBottom = false)
		{
			const float slope = (r0 - r1) / (std::max)(h, 1e-4f);
			std::vector<uint32_t> ring0, ring1;
			for (int i = 0; i <= sides; ++i)
			{
				const float a = XM_2PI * i / sides;
				const Vec3 radial(cosf(a), 0, sinf(a));
				const Vec3 tng(-sinf(a), 0, cosf(a));
				const Vec3 n = Norm(radial + kUp * slope);
				const float u = a * r0;
				ring0.push_back(Vert(slot, base + radial * r0, n, tng, u, 0.0f));
				ring1.push_back(Vert(slot, base + radial * r1 + kUp * h, n, tng, a * (std::max)(r1, 0.05f), -h * sqrtf(1.0f + slope * slope)));
			}
			for (int i = 0; i < sides; ++i)
			{
				const float a = XM_2PI * (i + 0.5f) / sides;
				const Vec3 want = Norm(Vec3(cosf(a), 0, sinf(a)) + kUp * slope);
				Tri(slot, ring0[i], ring0[i + 1], ring1[i + 1], want);
				if (r1 > 1e-4f)
					Tri(slot, ring0[i], ring1[i + 1], ring1[i], want);
			}
			auto cap = [&](float y, float r, const Vec3& n) {
				std::vector<Vec3> pts;
				for (int i = 0; i < sides; ++i)
				{
					const float a = XM_2PI * i / sides;
					pts.push_back(base + Vec3(cosf(a) * r, y, sinf(a) * r));
				}
				Fan(slot, pts, n);
			};
			if (capTop && r1 > 1e-4f)
				cap(h, r1, kUp);
			if (capBottom)
				cap(0.0f, r0, -kUp);
		}

		// 임의 방향 원기둥 (p0 → p1, 반지름 r) — 통나무 끝 · 장작 · 막대. u = 축 (나뭇결), v = 둘레
		void Cyl(int slot, const Vec3& p0, const Vec3& p1, float r, int sides, bool caps = true)
		{
			const Vec3 axis = p1 - p0;
			const float len = axis.Length();
			if (len < 1e-4f || r < 1e-4f)
				return;
			const Vec3 ax = axis / len;
			const Vec3 ref = fabsf(ax.y) < 0.9f ? kUp : Vec3(1, 0, 0);
			const Vec3 e1 = Norm(VCross(ref, ax)), e2 = VCross(ax, e1);
			std::vector<uint32_t> r0, r1;
			for (int i = 0; i <= sides; ++i)
			{
				const float a = XM_2PI * i / sides;
				const Vec3 n = e1 * cosf(a) + e2 * sinf(a);
				r0.push_back(Vert(slot, p0 + n * r, n, ax, 0.0f, -a * r));
				r1.push_back(Vert(slot, p1 + n * r, n, ax, len, -a * r));
			}
			for (int i = 0; i < sides; ++i)
			{
				const float a = XM_2PI * (i + 0.5f) / sides;
				const Vec3 want = e1 * cosf(a) + e2 * sinf(a);
				Tri(slot, r0[i], r0[i + 1], r1[i + 1], want);
				Tri(slot, r0[i], r1[i + 1], r1[i], want);
			}
			if (!caps)
				return;
			for (int e = 0; e < 2; ++e)
			{
				std::vector<Vec3> pts;
				for (int i = 0; i < sides; ++i)
				{
					const float a = XM_2PI * i / sides;
					pts.push_back((e ? p1 : p0) + (e1 * cosf(a) + e2 * sinf(a)) * r);
				}
				Fan(slot, pts, e ? ax : -ax);
			}
		}

		// UV 를 직접 준 볼록 다각형 (깃발 — 텍스처 한 장을 판 하나에)
		void PolyUV(int slot, const std::vector<Vec3>& pts, const std::vector<XMFLOAT2>& uv, const Vec3& n, const Vec3& t)
		{
			std::vector<uint32_t> idx;
			for (size_t i = 0; i < pts.size(); ++i)
				idx.push_back(Vert(slot, pts[i], n, t, uv[i].x, uv[i].y));
			for (size_t k = 1; k + 1 < idx.size(); ++k)
				Tri(slot, idx[0], idx[k], idx[k + 1], n);
		}

		// 판 (박공 판 · 간판): 위 가장자리 p0 → p1, 아래로 down 만큼 폭 width(t), 두께 th (n 반대쪽)
		template <typename W>
		void Board(int slot, const Vec3& p0, const Vec3& p1, const Vec3& down, const Vec3& n, float th, int segs, W width)
		{
			std::vector<Vec3> top, bot;
			for (int i = 0; i <= segs; ++i)
			{
				const float t = (float)i / segs;
				const Vec3 p = p0 + (p1 - p0) * t;
				top.push_back(p);
				bot.push_back(p + down * (std::max)(0.01f, width(t)));
			}
			const Vec3 back = -n * th;
			for (int i = 0; i < segs; ++i)
			{
				Quad(slot, top[i], top[i + 1], bot[i + 1], bot[i], n);
				Quad(slot, top[i] + back, top[i + 1] + back, bot[i + 1] + back, bot[i] + back, -n);
				Vec3 en = VCross(bot[i + 1] - bot[i], n);
				if (Dot(en, down) < 0.0f)
					en = -en;
				Quad(slot, bot[i], bot[i + 1], bot[i + 1] + back, bot[i] + back, Norm(en));
				Quad(slot, top[i], top[i + 1], top[i + 1] + back, top[i] + back, -down);
			}
			Quad(slot, top[0], bot[0], bot[0] + back, top[0] + back, Norm(p0 - p1));
			Quad(slot, top[segs], bot[segs], bot[segs] + back, top[segs] + back, Norm(p1 - p0));
		}

		// 작은 공 (꽃 · 덤불 · 장식 머리)
		void Blob(int slot, const Vec3& c, float r, int sides = 7)
		{
			Revolve(slot, c - kUp * r, sides, 4, [&](float t) { return XMFLOAT2((std::max)(0.001f, r * sinf(t * XM_PI)), r * (1.0f - cosf(t * XM_PI))); });
		}

		// 돌림면 (돔 · 양파): profile(t) = (반지름, 높이), t 0 → 1
		template <typename F>
		void Revolve(int slot, const Vec3& base, int sides, int rings, F profile)
		{
			std::vector<std::vector<uint32_t>> grid(rings + 1);
			for (int j = 0; j <= rings; ++j)
			{
				const float t = (float)j / rings;
				const XMFLOAT2 p = profile(t);
				const XMFLOAT2 pa = profile((std::max)(0.0f, t - 0.01f)), pb = profile((std::min)(1.0f, t + 0.01f));
				const float dr = pb.x - pa.x, dy = pb.y - pa.y;
				for (int i = 0; i <= sides; ++i)
				{
					const float a = XM_2PI * i / sides;
					const Vec3 radial(cosf(a), 0, sinf(a));
					const Vec3 n = Norm(radial * dy - kUp * dr);
					grid[j].push_back(Vert(slot, base + radial * p.x + kUp * p.y, n, Vec3(-sinf(a), 0, cosf(a)), a * (std::max)(p.x, 0.1f), -p.y));
				}
			}
			for (int j = 0; j < rings; ++j)
				for (int i = 0; i < sides; ++i)
				{
					const Part& part = O.Parts[slot];
					const XMFLOAT3 n = part.V[grid[j][i]].normal;
					const Vec3 want(n.x, n.y, n.z);
					Tri(slot, grid[j][i], grid[j][i + 1], grid[j + 1][i + 1], want);
					Tri(slot, grid[j][i], grid[j + 1][i + 1], grid[j + 1][i], want);
				}
		}
	};

	// ================================================================ 날개 (사각 몸채)
	struct Wing
	{
		float X0, X1, Z0, Z1;
		bool AlongX;     // 용마루 방향 (긴 쪽)
		bool Main;
		float W() const { return X1 - X0; }
		float D() const { return Z1 - Z0; }
		bool Inside(float x, float z, float m = 0.05f) const { return x > X0 + m && x < X1 - m && z > Z0 + m && z < Z1 - m; }
	};

	struct Ctx
	{
		const BD& D;
		Mesher& M;
		std::mt19937 Rng;
		std::vector<Wing> Wings;
		float FrontZ = 0.0f;      // 문이 있는 앞면 z
		float DoorX = 0.0f;
		Ctx(const BD& d, Mesher& m) : D(d), M(m), Rng((uint32_t)d.Seed * 2654435761u + 17u) {}
		float U01() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(Rng); }
		bool InsideOther(float x, float z, const Wing* self) const
		{
			for (const Wing& w : Wings)
				if (&w != self && w.Inside(x, z))
					return true;
			return false;
		}
		bool NearTower(float x, float z) const;
		std::vector<Wing> Blockers;   // 벽이 아닌 붙은 것 (헛간) — 창을 내지 않는다
		bool Blocked(float x, float z) const
		{
			for (const Wing& w : Blockers)
				if (w.Inside(x, z, -0.2f))
					return true;
			return false;
		}
		float RoundR = 0.0f;          // 둥근 몸채면 반지름 (앞벽 z 가 휜다)
		float FrontWallZ(float x) const { return RoundR > 0.0f ? -sqrtf((std::max)(0.0f, RoundR * RoundR - x * x)) : FrontZ; }
		// 테라스 (간판이 들보에 걸린다)
		float TerraceH = 0.0f, TerraceZ = 0.0f, TerraceDeck = 0.0f, TerraceX0 = 0.0f, TerraceX1 = 0.0f;
		std::vector<Vec3> TowerCenters;
		float TowerR = 0.0f;
	};

	bool Ctx::NearTower(float x, float z) const
	{
		for (const Vec3& c : TowerCenters)
			if ((x - c.x) * (x - c.x) + (z - c.z) * (z - c.z) < (TowerR + 0.6f) * (TowerR + 0.6f))
				return true;
		return false;
	}

	// ================================================================ 구멍 (창 · 문 · 열린 아치)
	enum class Hole { Window, Door, Arch };
	struct Opening
	{
		float A, B;       // 벽을 따라 (m, 띠 시작에서)
		float S, H;       // 아래 y, 전체 높이 (아치 꼭대기까지)
		BD::Window Shape;
		Hole Kind = Hole::Window;
		float Depth = 0.25f;

		float Width() const { return B - A; }
		float Center() const { return (A + B) * 0.5f; }
		// 윗변이 시작되는 높이 (아치 · 첨두 · 활꼴의 어깨)
		float Shoulder() const
		{
			const float w = Width();
			switch (Shape)
			{
			case BD::Window::Arched: return S + H - w * 0.5f;
			case BD::Window::Gothic: return S + H - w * 0.866f;
			case BD::Window::Baroque: return S + H - w * 0.18f;
			default: return S + H;
			}
		}
		float Top(float x) const
		{
			const float w = Width(), c = Center(), t = Shoulder();
			switch (Shape)
			{
			case BD::Window::Arched: { const float r = w * 0.5f; return t + sqrtf((std::max)(0.0f, r * r - (x - c) * (x - c))); }
			case BD::Window::Gothic:
			{
				const float R = w;
				return x <= c ? t + sqrtf((std::max)(0.0f, R * R - (x - B) * (x - B))) : t + sqrtf((std::max)(0.0f, R * R - (x - A) * (x - A)));
			}
			case BD::Window::Baroque:
			{
				const float k = w * 0.18f, R = (w * w * 0.25f + k * k) / (2.0f * k), cy = t + k - R;
				return cy + sqrtf((std::max)(0.0f, R * R - (x - c) * (x - c)));
			}
			case BD::Window::Round: { const float r = (std::min)(w, H) * 0.5f, cy = S + H * 0.5f; return cy + sqrtf((std::max)(0.0f, r * r - (x - c) * (x - c))); }
			default: return S + H;
			}
		}
		float Bottom(float x) const
		{
			if (Shape == BD::Window::Round)
			{
				const float r = (std::min)(Width(), H) * 0.5f, cy = S + H * 0.5f, c = Center();
				return cy - sqrtf((std::max)(0.0f, r * r - (x - c) * (x - c)));
			}
			return S;
		}
		bool Curved() const { return Shape == BD::Window::Arched || Shape == BD::Window::Gothic || Shape == BD::Window::Baroque || Shape == BD::Window::Round; }
		std::vector<float> Samples() const
		{
			const int n = Shape == BD::Window::Round ? 16 : (Curved() ? 12 : 1);
			std::vector<float> xs;
			for (int i = 0; i <= n; ++i)
				xs.push_back(A + Width() * i / n);
			return xs;
		}
	};

	// 벽 띠 한 장: 시작 P0, 방향 d (단위, 바깥 = 위 x d), 길이 len, 높이 y0 ~ y1, 바깥으로 off 만큼. u0 = 띠 시작의 둘레 거리 (무늬가 이어지게)
	struct Band
	{
		Vec3 P0, Dir, N;
		float Len, Y0, Y1, U0;
		bool Masonry = false;   // 돌 · 벽돌 벽 (아치 쐐기돌 · 상인방 돌 · 문설주 돌)
		bool Wood = false;      // 판자 · 통나무 벽 (칠한 창틀 판)
		Vec3 At(float x, float y, float depth = 0.0f) const { return Vec3(P0.x, 0, P0.z) + Dir * x - N * depth + Vec3(0, y, 0); }
	};

	void WallRect(Mesher& m, int slot, const Band& b, float xa, float xb, float ya, float yb, float yc = -1.0f, float yd = -1.0f)
	{
		// 사다리꼴: 아래 ya (xa · xb 같다), 위 yb (xa) · yc (xb) — 아치 둘레 기둥
		if (yc < 0.0f) yc = yb;
		if (yd < 0.0f) yd = ya;
		if (xb - xa < 1e-4f || ((yb - ya) < 1e-4f && (yc - yd) < 1e-4f))
			return;
		const Vec3 t = b.Dir;
		const uint32_t i0 = m.Vert(slot, b.At(xa, ya), b.N, t, b.U0 + xa, -ya);
		const uint32_t i1 = m.Vert(slot, b.At(xb, yd), b.N, t, b.U0 + xb, -yd);
		const uint32_t i2 = m.Vert(slot, b.At(xb, yc), b.N, t, b.U0 + xb, -yc);
		const uint32_t i3 = m.Vert(slot, b.At(xa, yb), b.N, t, b.U0 + xa, -yb);
		m.Tri(slot, i0, i1, i2, b.N);
		m.Tri(slot, i0, i2, i3, b.N);
	}

	bool IsMasonry(BD::WallMat m) { return m == BD::WallMat::Rubble || m == BD::WallMat::Ashlar || m == BD::WallMat::Brick || m == BD::WallMat::Marble; }

	// 띠 좌표 (x 벽을 따라, y 높이, depth 음수 = 밖) 의 돌 한 덩이 (앞면이 밖으로 pz)
	void WallBlock(Mesher& m, int slot, const Band& b, float x0, float x1, float y0, float y1, float pz)
	{
		const Vec3 c = b.At((x0 + x1) * 0.5f, (y0 + y1) * 0.5f, -pz * 0.5f + 0.01f);
		m.OBox(slot, c, b.Dir, kUp, b.N, (x1 - x0) * 0.5f, (y1 - y0) * 0.5f, pz * 0.5f + 0.01f, false);
	}

	// 석조 벽의 구멍 둘레: 아치 = 쐐기돌 (가운데 이맛돌이 크다), 네모 = 상인방 돌, 문 · 열린 아치 = 문설주 돌 (길고 짧게 번갈아)
	void StoneSurround(Ctx& c, const Band& b, const Opening& o)
	{
		Mesher& m = c.M;
		const bool big = o.Kind != Hole::Window;
		const float vw = big ? 0.36f : 0.2f, pz = big ? 0.07f : 0.045f, gap = 0.012f;
		const float cx = o.Center();
		if (o.Curved() && o.Shape != BD::Window::Round)
		{
			const int N = 72;
			std::vector<XMFLOAT2> P(N + 1);
			std::vector<float> L(N + 1, 0.0f);
			for (int i = 0; i <= N; ++i)
			{
				const float x = o.A + o.Width() * i / N;
				P[i] = XMFLOAT2(x, o.Top(x));
				if (i)
					L[i] = L[i - 1] + sqrtf((P[i].x - P[i - 1].x) * (P[i].x - P[i - 1].x) + (P[i].y - P[i - 1].y) * (P[i].y - P[i - 1].y));
			}
			const float total = L[N];
			auto at = [&](float sLen, XMFLOAT2& p, XMFLOAT2& t) {
				int i = 0;
				while (i < N - 1 && L[i + 1] < sLen) ++i;
				const float f = std::clamp((sLen - L[i]) / (std::max)(1e-5f, L[i + 1] - L[i]), 0.0f, 1.0f);
				p = XMFLOAT2(P[i].x + (P[i + 1].x - P[i].x) * f, P[i].y + (P[i + 1].y - P[i].y) * f);
				const float tx = P[i + 1].x - P[i].x, ty = P[i + 1].y - P[i].y, tl = (std::max)(1e-6f, sqrtf(tx * tx + ty * ty));
				t = XMFLOAT2(tx / tl, ty / tl);
			};
			int n = (std::max)(5, (int)roundf(total / (big ? 0.34f : 0.26f)));
			if ((n & 1) == 0) ++n;
			const XMFLOAT2 c0(cx, o.Shoulder() - 0.01f);
			auto W = [&](float x, float y, float dz) { return b.At(x, y, dz); };
			for (int k = 0; k < n; ++k)
			{
				const bool key = k == n / 2;
				const float s0 = total * k / n + gap, s1 = total * (k + 1) / n - gap;
				const float width = vw * (key ? 1.3f : 1.0f) * (0.92f + 0.16f * c.U01()), depth = pz * (key ? 1.6f : 1.0f);
				std::vector<XMFLOAT2> in, outer, nrm;
				for (int j = 0; j <= 3; ++j)
				{
					XMFLOAT2 p, t;
					at(s0 + (s1 - s0) * j / 3.0f, p, t);
					XMFLOAT2 nn(-t.y, t.x);
					if ((p.x - c0.x) * nn.x + (p.y - c0.y) * nn.y < 0.0f)
						nn = XMFLOAT2(-nn.x, -nn.y);
					in.push_back(p);
					outer.push_back(XMFLOAT2(p.x + nn.x * width, p.y + nn.y * width));
					nrm.push_back(nn);
				}
				for (int j = 0; j < 3; ++j)
				{
					m.Quad(Trim, W(in[j].x, in[j].y, -depth), W(in[j + 1].x, in[j + 1].y, -depth), W(outer[j + 1].x, outer[j + 1].y, -depth), W(outer[j].x, outer[j].y, -depth), b.N);
					const Vec3 on = Norm(b.Dir * nrm[j].x + kUp * nrm[j].y);
					m.Quad(Trim, W(outer[j].x, outer[j].y, 0.0f), W(outer[j + 1].x, outer[j + 1].y, 0.0f), W(outer[j + 1].x, outer[j + 1].y, -depth), W(outer[j].x, outer[j].y, -depth), on);
					m.Quad(Trim, W(in[j].x, in[j].y, 0.0f), W(in[j + 1].x, in[j + 1].y, 0.0f), W(in[j + 1].x, in[j + 1].y, -depth), W(in[j].x, in[j].y, -depth), -on);
				}
				for (int e = 0; e < 2; ++e)
				{
					const int j = e ? 3 : 0;
					XMFLOAT2 p, t;
					at(e ? s1 : s0, p, t);
					const Vec3 tn = Norm(b.Dir * t.x + kUp * t.y) * (e ? 1.0f : -1.0f);
					m.Quad(Trim, W(in[j].x, in[j].y, 0.0f), W(outer[j].x, outer[j].y, 0.0f), W(outer[j].x, outer[j].y, -depth), W(in[j].x, in[j].y, -depth), tn);
				}
			}
		}
		else if (o.Kind == Hole::Window)
		{
			// 상인방 (가운데 이맛돌) — 창턱은 BuildBand 가
			const float top = o.Top(cx);
			WallBlock(m, Trim, b, o.A - 0.2f, cx - 0.14f, top + 0.02f, top + 0.24f, pz);
			WallBlock(m, Trim, b, cx + 0.14f, o.B + 0.2f, top + 0.02f, top + 0.24f, pz);
			WallBlock(m, Trim, b, cx - 0.12f, cx + 0.12f, top - 0.02f, top + 0.3f, pz * 1.5f);
		}
		else
		{
			const float top = o.Top(cx);
			WallBlock(m, Trim, b, o.A - 0.3f, o.B + 0.3f, top + 0.02f, top + 0.4f, pz);
		}
		if (big)
		{
			// 문설주 돌 (어깨까지)
			int k = 0;
			for (float y = o.S; y < o.Shoulder() - 0.12f; y += 0.4f, ++k)
			{
				const float y1 = (std::min)(y + 0.38f, o.Shoulder());
				const float wl = (k & 1) ? 0.28f : 0.44f, wr = (k & 1) ? 0.44f : 0.28f;
				WallBlock(m, Trim, b, o.A - wl, o.A, y + gap, y1 - gap, pz);
				WallBlock(m, Trim, b, o.B, o.B + wr, y + gap, y1 - gap, pz);
			}
		}
	}

	// 목조 벽의 문: 굵은 문설주 + 양쪽으로 내민 상인방 들보
	void TimberDoorFrame(Ctx& c, const Band& b, const Opening& o)
	{
		Mesher& m = c.M;
		const float cx = o.Center();
		const float postTop = o.Shoulder() + 0.08f;
		const float ly = o.Top(cx) + 0.14f;
		m.Beam(Timber, b.At(o.A - 0.1f, o.S), b.At(o.A - 0.1f, postTop), 0.2f, 0.12f, b.N);
		m.Beam(Timber, b.At(o.B + 0.1f, o.S), b.At(o.B + 0.1f, postTop), 0.2f, 0.12f, b.N);
		m.Beam(Timber, b.At(o.A - 0.5f, ly), b.At(o.B + 0.5f, ly), 0.26f, 0.15f, b.N);
	}

	// 판자 · 통나무 벽의 창: 칠한 창틀 판 (위는 길고 아래는 받침)
	void WindowCasing(Ctx& c, const Band& b, const Opening& o)
	{
		Mesher& m = c.M;
		const float top = o.Top(o.Center());
		const float bw = 0.12f, d = 0.04f;
		m.Beam(Frame, b.At(o.A - bw * 0.5f, o.S - 0.05f), b.At(o.A - bw * 0.5f, top + 0.05f), bw, d, b.N);
		m.Beam(Frame, b.At(o.B + bw * 0.5f, o.S - 0.05f), b.At(o.B + bw * 0.5f, top + 0.05f), bw, d, b.N);
		m.Beam(Frame, b.At(o.A - 0.24f, top + 0.11f), b.At(o.B + 0.24f, top + 0.11f), 0.16f, d * 1.5f, b.N);
		m.Beam(Frame, b.At(o.A - 0.16f, o.S - 0.07f), b.At(o.B + 0.16f, o.S - 0.07f), 0.1f, d * 2.0f, b.N);
	}

	// 창 아래 꽃 상자 (나무 상자 + 꽃 덤불)
	void FlowerBox(Ctx& c, const Band& b, const Opening& o)
	{
		Mesher& m = c.M;
		const float w = o.Width() + 0.1f;
		const Vec3 center = b.At(o.Center(), o.S - 0.24f, -0.17f);
		m.OBox(Door, center, b.Dir, kUp, b.N, w * 0.5f, 0.12f, 0.14f);
		const int n = (std::max)(3, (int)(w / 0.17f));
		for (int i = 0; i < n; ++i)
		{
			const float x = o.A - 0.05f + w * (i + 0.5f) / n;
			const float r = 0.1f + 0.05f * c.U01();
			m.Blob(Plants, b.At(x, o.S - 0.1f + r * 0.4f, -0.17f + (c.U01() - 0.5f) * 0.1f), r, 6);
		}
	}

	// 구멍이 뚫린 벽 띠 + 구멍 안쪽 면 · 유리 / 문 / 안쪽 벽 · 창틀 · 창턱 · 덧창
	void BuildBand(Ctx& c, int slot, const Band& b, std::vector<Opening> holes, bool decorate)
	{
		Mesher& m = c.M;
		const BD& d = c.D;
		std::sort(holes.begin(), holes.end(), [](const Opening& x, const Opening& y) { return x.A < y.A; });
		float x = 0.0f;
		for (const Opening& o : holes)
		{
			if (o.A > x)
				WallRect(m, slot, b, x, o.A, b.Y0, b.Y1);
			const std::vector<float> xs = o.Samples();
			for (size_t i = 0; i + 1 < xs.size(); ++i)
			{
				const float xa = xs[i], xb = xs[i + 1];
				WallRect(m, slot, b, xa, xb, b.Y0, (std::max)(b.Y0, o.Bottom(xa)), (std::max)(b.Y0, o.Bottom(xb)), b.Y0);   // 아래
				const float ta = (std::min)(b.Y1, o.Top(xa)), tb = (std::min)(b.Y1, o.Top(xb));
				// 위: ta → Y1 (사다리꼴 — 아래 변이 기울어짐)
				if (b.Y1 - (std::min)(ta, tb) > 1e-4f)
				{
					const uint32_t i0 = m.Vert(slot, b.At(xa, ta), b.N, b.Dir, b.U0 + xa, -ta);
					const uint32_t i1 = m.Vert(slot, b.At(xb, tb), b.N, b.Dir, b.U0 + xb, -tb);
					const uint32_t i2 = m.Vert(slot, b.At(xb, b.Y1), b.N, b.Dir, b.U0 + xb, -b.Y1);
					const uint32_t i3 = m.Vert(slot, b.At(xa, b.Y1), b.N, b.Dir, b.U0 + xa, -b.Y1);
					m.Tri(slot, i0, i1, i2, b.N);
					m.Tri(slot, i0, i2, i3, b.N);
				}
			}
			x = o.B;

			// ---- 구멍 안쪽 면 (깊이 Depth): 옆 · 위 · 아래
			const float dp = o.Depth;
			const Vec3 center = b.At(o.Center(), (o.Bottom(o.Center()) + o.Top(o.Center())) * 0.5f);
			auto reveal = [&](const Vec3& p0, const Vec3& p1) {
				const Vec3 q0 = p0 - b.N * dp, q1 = p1 - b.N * dp;
				Vec3 want = center - (p0 + p1) * 0.5f;
				want = want - b.N * Dot(want, b.N);
				m.Quad(slot, p0, p1, q1, q0, Norm(want));
			};
			const int revealSlot = o.Kind == Hole::Arch ? slot : slot;
			(void)revealSlot;
			if (o.Top(o.A) - o.Bottom(o.A) > 1e-3f)
				reveal(b.At(o.A, o.Bottom(o.A)), b.At(o.A, (std::min)(b.Y1, o.Top(o.A))));
			if (o.Top(o.B) - o.Bottom(o.B) > 1e-3f)
				reveal(b.At(o.B, o.Bottom(o.B)), b.At(o.B, (std::min)(b.Y1, o.Top(o.B))));
			for (size_t i = 0; i + 1 < xs.size(); ++i)
			{
				reveal(b.At(xs[i], (std::min)(b.Y1, o.Top(xs[i]))), b.At(xs[i + 1], (std::min)(b.Y1, o.Top(xs[i + 1]))));
				if (o.Kind != Hole::Door && o.Kind != Hole::Arch)
					reveal(b.At(xs[i], o.Bottom(xs[i])), b.At(xs[i + 1], o.Bottom(xs[i + 1])));
			}
			if (o.Kind == Hole::Arch)
				for (size_t i = 0; i + 1 < xs.size(); ++i)   // 열린 아치의 바닥 (안쪽으로 걷는 길)
					m.Quad(c.D.Arcade ? Trim : slot, b.At(xs[i], b.Y0), b.At(xs[i + 1], b.Y0), b.At(xs[i + 1], b.Y0, dp), b.At(xs[i], b.Y0, dp), kUp);

			// ---- 안쪽 판: 유리 / 문짝 / 열린 아치의 안쪽 벽
			std::vector<Vec3> outline;
			for (float s : xs)
				outline.push_back(b.At(s, o.Bottom(s), dp));
			for (auto it = xs.rbegin(); it != xs.rend(); ++it)
				outline.push_back(b.At(*it, (std::min)(b.Y1, o.Top(*it)), dp));
			const int paneSlot = o.Kind == Hole::Door ? Door : (o.Kind == Hole::Arch ? slot : Glass);
			m.Fan(paneSlot, outline, b.N);
			if (b.Masonry && decorate && o.Shape != BD::Window::Slit)
				StoneSurround(c, b, o);
			if (o.Kind == Hole::Arch)
				continue;
			if (!b.Masonry && decorate && o.Kind == Hole::Door)
				TimberDoorFrame(c, b, o);
			if (b.Wood && decorate && o.Kind == Hole::Window && o.Shape != BD::Window::Slit && o.Shape != BD::Window::Round)
				WindowCasing(c, b, o);
			if (o.Kind == Hole::Door)
				++c.M.O.Doors;
			else
				++c.M.O.Windows;

			// ---- 창틀 (안쪽 판 앞 테두리) — 윤곽을 가운데로 줄인 띠
			{
				Vec3 cen(0, 0, 0);
				for (const Vec3& p : outline)
					cen += p;
				cen /= (float)outline.size();
				const float fw = o.Kind == Hole::Door ? 0.1f : 0.06f;
				std::vector<Vec3> inner;
				for (const Vec3& p : outline)
				{
					Vec3 q = cen - p;
					const float l = q.Length();
					inner.push_back(l > fw * 1.5f ? p + q / l * fw : p + q * 0.3f);
				}
				const Vec3 lift = b.N * 0.012f;
				for (size_t i = 0; i < outline.size(); ++i)
				{
					const size_t j = (i + 1) % outline.size();
					if ((outline[i] - outline[j]).LengthSquared() < 1e-8f)
						continue;
					m.Quad(Frame, outline[i] + lift, outline[j] + lift, inner[j] + lift, inner[i] + lift, b.N);
				}
			}
			const float cx = o.Center();
			const float yMid = (o.Bottom(cx) + o.Shoulder()) * 0.5f;
			// 문 두 짝 · 큰 문의 가운데 틀
			if (o.Kind == Hole::Door && (d.DoorStyle == BD::Door::Double || d.DoorStyle == BD::Door::Grand))
				m.Beam(Frame, b.At(cx, o.S, dp - 0.012f), b.At(cx, o.Top(cx), dp - 0.012f), 0.08f, 0.03f, b.N);
			// 문 쇠붙이 (경첩 띠 두 줄)
			if (o.Kind == Hole::Door)
				for (float hy : { o.S + 0.35f, o.Shoulder() - 0.35f })
					m.Beam(Metal, b.At(o.A + 0.05f, hy, dp - 0.015f), b.At(o.B - 0.05f, hy, dp - 0.015f), 0.06f, 0.015f, b.N);
			// 창살: 돌 창살 (Mullioned) 또는 나무 창살 (납틀이 아니면)
			if (o.Kind == Hole::Window)
			{
				if (o.Shape == BD::Window::Mullioned)
				{
					m.Beam(Trim, b.At(cx, o.S, dp - 0.08f), b.At(cx, o.Shoulder(), dp - 0.08f), 0.1f, 0.12f, b.N);
					const float ty = o.S + (o.Shoulder() - o.S) * 0.62f;
					m.Beam(Trim, b.At(o.A, ty, dp - 0.08f), b.At(o.B, ty, dp - 0.08f), 0.1f, 0.12f, b.N);
				}
				else if (!d.Lattice && o.Shape != BD::Window::Slit && o.Shape != BD::Window::Round)
				{
					m.Beam(Frame, b.At(cx, o.S, dp - 0.02f), b.At(cx, o.Shoulder(), dp - 0.02f), 0.04f, 0.03f, b.N);
					m.Beam(Frame, b.At(o.A, yMid, dp - 0.02f), b.At(o.B, yMid, dp - 0.02f), 0.04f, 0.03f, b.N);
				}
			}
			if (!decorate || o.Kind != Hole::Window)
				continue;
			// ---- 창턱 (돌) · 쐐기돌 · 창 위 박공 · 덧창
			if (o.Shape != BD::Window::Slit)
			{
				const Vec3 s0 = b.At(o.A - 0.1f, o.S - 0.08f), s1 = b.At(o.B + 0.1f, o.S - 0.08f);
				m.Beam(Trim, s0 + Vec3(0, 0.04f, 0), s1 + Vec3(0, 0.04f, 0), 0.08f, 0.1f, b.N);
			}
			if (d.FlowerBoxes && o.Shape != BD::Window::Slit && o.Shape != BD::Window::Round && o.Width() >= 0.5f && o.S - b.Y0 > 0.6f)
				FlowerBox(c, b, o);
			if (!b.Masonry && (o.Shape == BD::Window::Arched || o.Shape == BD::Window::Baroque))
			{
				const float ty = o.Top(cx);
				m.Beam(Trim, b.At(cx, ty - 0.12f), b.At(cx, ty + 0.12f), 0.22f, 0.05f, b.N);
			}
			if (d.WindowPediments)
			{
				const float ty = o.Top(cx) + 0.18f;
				const float hw = o.Width() * 0.5f + 0.2f;
				m.Beam(Trim, b.At(cx - hw, ty), b.At(cx + hw, ty), 0.12f, 0.16f, b.N);
				// 작은 세모 박공 (앞 · 위 두 비탈)
				const Vec3 l = b.At(cx - hw, ty + 0.06f, -0.16f), r = b.At(cx + hw, ty + 0.06f, -0.16f), top = b.At(cx, ty + 0.06f + hw * 0.35f, -0.16f);
				const Vec3 lb = b.At(cx - hw, ty + 0.06f), rb = b.At(cx + hw, ty + 0.06f), topb = b.At(cx, ty + 0.06f + hw * 0.35f);
				m.Poly(Trim, { l, r, top }, b.N);
				m.Quad(Trim, l, top, topb, lb, Norm(b.N * 0.2f + Norm(VCross(top - l, b.N)) * -1.0f + kUp));
				m.Quad(Trim, top, r, rb, topb, Norm(b.N * 0.2f + kUp));
			}
			if (d.Shutters && o.Shape != BD::Window::Slit && o.Shape != BD::Window::Round && o.Shape != BD::Window::Gothic)
			{
				const float sw = o.Width() * 0.5f, top = o.Shoulder();
				for (int side = 0; side < 2; ++side)
				{
					const float xa = side == 0 ? o.A - sw - 0.04f : o.B + 0.04f;
					const Vec3 cbox = b.At(xa + sw * 0.5f, (o.S + top) * 0.5f, -0.03f);
					m.OBox(Shutter, cbox, b.Dir, kUp, -b.N, sw * 0.5f, (top - o.S) * 0.5f, 0.025f);
				}
			}
		}
		if (x < b.Len)
			WallRect(m, slot, b, x, b.Len, b.Y0, b.Y1);
	}

	BD::Window DoorShape(const BD& d)
	{
		switch (d.DoorStyle)
		{
		case BD::Door::Arched: return d.WindowStyle == BD::Window::Gothic ? BD::Window::Gothic : BD::Window::Arched;
		case BD::Door::Grand: return d.WindowStyle == BD::Window::Gothic ? BD::Window::Gothic : (d.WindowStyle == BD::Window::Rectangular ? BD::Window::Rectangular : BD::Window::Arched);
		default: return BD::Window::Rectangular;
		}
	}

	bool IsPlaster(BD::WallMat m) { return m == BD::WallMat::Plaster || m == BD::WallMat::Stucco; }

	// ================================================================ 반목조: 띠 하나 (구멍 자리는 피한다)
	void TimberBand(Ctx& c, const Band& b, const std::vector<Opening>& holes)
	{
		const BD& d = c.D;
		Mesher& m = c.M;
		const float tw = d.TimberWidth, td = 0.05f;
		auto P = [&](float x, float y) { return b.At(x, y, -0.005f); };
		// 가로: 밑 · 위
		m.Beam(Timber, P(0, b.Y0 + tw * 0.5f), P(b.Len, b.Y0 + tw * 0.5f), tw, td, b.N);
		m.Beam(Timber, P(0, b.Y1 - tw * 0.5f), P(b.Len, b.Y1 - tw * 0.5f), tw, td, b.N);
		// 칸 경계: 양 끝 + 구멍 사이 가운데
		std::vector<float> posts = { tw * 0.5f, b.Len - tw * 0.5f };
		std::vector<Opening> hs = holes;
		std::sort(hs.begin(), hs.end(), [](const Opening& x, const Opening& y) { return x.A < y.A; });
		for (size_t i = 0; i + 1 < hs.size(); ++i)
			posts.push_back((hs[i].B + hs[i + 1].A) * 0.5f);
		// 구멍이 없는 긴 띠는 2 m 마다
		if (hs.empty())
			for (float x = 2.0f; x < b.Len - 1.0f; x += 2.0f)
				posts.push_back(x);
		for (const Opening& o : hs)
		{
			posts.push_back(o.A - tw * 0.5f);
			posts.push_back(o.B + tw * 0.5f);
		}
		std::sort(posts.begin(), posts.end());
		for (float x : posts)
			if (x > 0.0f && x < b.Len)
				m.Beam(Timber, P(x, b.Y0), P(x, b.Y1), tw, td, b.N);
		// 창 위아래 가로대
		for (const Opening& o : hs)
		{
			if (o.Kind != Hole::Window)
				continue;
			m.Beam(Timber, P(o.A - tw, o.S - tw * 0.5f), P(o.B + tw, o.S - tw * 0.5f), tw * 0.8f, td, b.N);
			const float top = o.Top(o.Center()) + tw * 0.5f;
			if (top < b.Y1 - tw)
				m.Beam(Timber, P(o.A - tw, top), P(o.B + tw, top), tw * 0.8f, td, b.N);
		}
		// 빈 칸 (구멍 없는 칸) 의 가새
		for (size_t i = 0; i + 1 < posts.size(); ++i)
		{
			const float xa = posts[i], xb = posts[i + 1];
			if (xb - xa < 0.9f)
				continue;
			bool hasHole = false;
			for (const Opening& o : hs)
				hasHole |= o.B > xa + 0.05f && o.A < xb - 0.05f;
			if (hasHole)
				continue;
			const float y0 = b.Y0 + tw, y1 = b.Y1 - tw;
			switch (d.Frame)
			{
			case BD::Timber::Cross:
				m.Beam(Timber, P(xa, y0), P(xb, y1), tw * 0.8f, td, b.N);
				m.Beam(Timber, P(xa, y1), P(xb, y0), tw * 0.8f, td * 0.9f, b.N);
				break;
			case BD::Timber::Chevron:
				m.Beam(Timber, P(xa, y0), P((xa + xb) * 0.5f, y1), tw * 0.8f, td, b.N);
				m.Beam(Timber, P(xb, y0), P((xa + xb) * 0.5f, y1), tw * 0.8f, td, b.N);
				break;
			case BD::Timber::Tudor:
			{
				// 굽은 가새 (모서리마다 1/4 원을 세 토막으로)
				const float r = (std::min)((xb - xa) * 0.45f, (y1 - y0) * 0.6f);
				for (int corner = 0; corner < 4; ++corner)
				{
					const float cxp = corner & 1 ? xb : xa, cyp = corner & 2 ? y1 : y0;
					const float sx = corner & 1 ? -1.0f : 1.0f, sy = corner & 2 ? -1.0f : 1.0f;
					Vec3 prev = P(cxp + sx * r, cyp);
					for (int k = 1; k <= 3; ++k)
					{
						const float a = XM_PIDIV2 * k / 3.0f;
						const Vec3 q = P(cxp + sx * r * cosf(a), cyp + sy * r * sinf(a));
						m.Beam(Timber, prev, q, tw * 0.7f, td, b.N);
						prev = q;
					}
				}
				break;
			}
			case BD::Timber::Grid:
				m.Beam(Timber, P(xa, (y0 + y1) * 0.5f), P(xb, (y0 + y1) * 0.5f), tw * 0.8f, td, b.N);
				m.Beam(Timber, P((xa + xb) * 0.5f, y0), P((xa + xb) * 0.5f, y1), tw * 0.8f, td, b.N);
				break;
			default:
				break;
			}
		}
	}

	// ================================================================ 벽 (날개 하나: 층마다 네 면)
	struct Side { Vec3 P0, Dir; float Len; bool Front; };

	std::vector<Side> SidesOf(const Wing& w, float out)
	{
		// 둘레를 노멀맵 규약 방향 (앞 +X → 오른쪽 +Z → 뒤 -X → 왼쪽 -Z) 으로. out = 밖으로 민 만큼 (돌출 층)
		const float x0 = w.X0 - out, x1 = w.X1 + out, z0 = w.Z0 - out, z1 = w.Z1 + out;
		return {
			{ Vec3(x0, 0, z0), Vec3(1, 0, 0), x1 - x0, true },
			{ Vec3(x1, 0, z0), Vec3(0, 0, 1), z1 - z0, false },
			{ Vec3(x1, 0, z1), Vec3(-1, 0, 0), x1 - x0, false },
			{ Vec3(x0, 0, z1), Vec3(0, 0, -1), z1 - z0, false } };
	}

	std::vector<Opening> PlaceOpenings(Ctx& c, const Wing& w, const Side& s, int floor, float y0, float y1)
	{
		const BD& d = c.D;
		std::vector<Opening> out;
		const Vec3 n = VCross(kUp, s.Dir);
		auto worldAt = [&](float x) { return s.P0 + s.Dir * x + n * 0.3f; };   // 벽 바로 밖 (다른 날개에 묻혔는지)
		const bool ground = floor == 0;
		// 문: 앞면 (FrontZ) 이 이 면이고 문 x 가 이 면 위면
		bool doorHere = false;
		float doorAlong = 0.0f;
		if (ground && fabsf(s.Dir.z) < 0.5f && s.Dir.x > 0.0f && fabsf(s.P0.z - c.FrontZ) < 0.6f + d.Jetty && c.DoorX > s.P0.x + 0.5f && c.DoorX < s.P0.x + s.Len - 0.5f)
		{
			doorHere = true;
			doorAlong = c.DoorX - s.P0.x;
		}
		const BD::Window style = ground ? d.GroundWindowStyle : d.WindowStyle;
		const bool arcade = ground && d.Arcade;
		const float spacing = (std::max)(d.WindowSpacing, d.WindowWidth + 0.4f);
		// 열린 아케이드 (시장 · 문루) 는 좁아져도 아치 하나는 (길이 지나간다)
		const int count = arcade && s.Len > 2.5f ? (std::max)(1, (int)floorf((s.Len - 0.6f) / spacing)) : (int)floorf((s.Len - 0.6f) / spacing);
		const float bandH = y1 - y0;
		for (int i = 0; i < count; ++i)
		{
			const float slotW = s.Len / (std::max)(count, 1);
			float cx = s.Len * 0.5f + (i - (count - 1) * 0.5f) * slotW;
			// 손으로 지은 집: 창 자리 · 높이가 조금씩 다르고 가끔 빠진다
			const float irr = d.Irregularity;
			const float jx = (c.U01() - 0.5f) * irr * (std::max)(0.0f, slotW - d.WindowWidth - 0.3f) * 0.5f;
			const float jy = (c.U01() - 0.5f) * irr * 0.16f;
			const bool drop = !arcade && count >= 3 && c.U01() < irr * 0.12f;
			cx += jx;
			const Vec3 wp = worldAt(cx);
			if (c.InsideOther(wp.x, wp.z, &w) || c.NearTower(wp.x, wp.z) || c.Blocked(wp.x, wp.z))
				continue;
			Opening o;
			if (arcade)
			{
				const float aw = (std::min)(spacing * 0.72f, s.Len / (std::max)(count, 1) - 0.6f);
				o.A = cx - aw * 0.5f; o.B = cx + aw * 0.5f; o.S = y0; o.H = (std::min)(bandH * 0.88f, aw * 0.5f + bandH * 0.55f);
				o.Shape = BD::Window::Arched; o.Kind = Hole::Arch; o.Depth = (std::min)(2.4f, w.D() * 0.3f);
				out.push_back(o);
				continue;
			}
			if (doorHere && fabsf(cx - doorAlong) < (spacing + d.DoorWidth) * 0.5f)
				continue;   // 문 자리
			if (drop)
				continue;
			if (style == BD::Window::None)
				continue;
			float ww = d.WindowWidth, wh = d.WindowHeight;
			if (style == BD::Window::Slit) { ww = (std::min)(ww, 0.35f); }
			if (style == BD::Window::Round) { wh = ww; }
			const float sill = (std::min)(d.SillHeight, bandH - wh - 0.25f);
			o.A = cx - ww * 0.5f; o.B = cx + ww * 0.5f; o.S = y0 + (std::max)(0.3f, sill + jy); o.H = (std::min)(wh, bandH - 0.3f - (o.S - y0));
			if (o.H < 0.3f)
				continue;
			o.Shape = style; o.Depth = style == BD::Window::Slit ? 0.5f : 0.2f;
			out.push_back(o);
		}
		if (doorHere && !arcade)
		{
			Opening o;
			o.A = doorAlong - d.DoorWidth * 0.5f; o.B = doorAlong + d.DoorWidth * 0.5f; o.S = y0; o.H = (std::min)(d.DoorHeight, bandH - 0.2f);
			o.Shape = DoorShape(d); o.Kind = Hole::Door; o.Depth = 0.25f;
			out.push_back(o);
		}
		return out;
	}

	void BuildWingWalls(Ctx& c, const Wing& w)
	{
		const BD& d = c.D;
		Mesher& m = c.M;
		// 밑돌 (조금 밖으로)
		if (d.Plinth > 0.01f)
		{
			// 막돌 벽이면 밑돌도 막돌 (1 층 재질 — 때가 가장 진하다), 위는 비스듬히 (물이 흘러내리게)
			const int ps = d.GroundWall == BD::WallMat::Rubble ? WallLower : Trim;
			const float e = 0.12f, ch = (std::min)(0.1f, d.Plinth * 0.4f);
			m.Box(ps, Vec3(w.X0 - e, 0.0f, w.Z0 - e), Vec3(w.X1 + e, d.Plinth - ch, w.Z1 + e));
			const float yb = d.Plinth - ch, sl = ch / e;
			const Vec3 o0(w.X0 - e, yb, w.Z0 - e), o1(w.X1 + e, yb, w.Z0 - e), o2(w.X1 + e, yb, w.Z1 + e), o3(w.X0 - e, yb, w.Z1 + e);
			const Vec3 i0(w.X0, d.Plinth, w.Z0), i1(w.X1, d.Plinth, w.Z0), i2(w.X1, d.Plinth, w.Z1), i3(w.X0, d.Plinth, w.Z1);
			m.Quad(ps, o0, o1, i1, i0, Norm(Vec3(0, 1, -sl)));
			m.Quad(ps, o1, o2, i2, i1, Norm(Vec3(sl, 1, 0)));
			m.Quad(ps, o2, o3, i3, i2, Norm(Vec3(0, 1, sl)));
			m.Quad(ps, o3, o0, i0, i3, Norm(Vec3(-sl, 1, 0)));
			// 문 앞 계단 (문 지붕 · 테라스가 있으면 그쪽에서)
			if (w.Main || (c.DoorX > w.X0 && c.DoorX < w.X1 && fabsf(w.Z0 - c.FrontZ) < 0.1f))
				if (fabsf(w.Z0 - c.FrontZ) < 0.1f && d.Plinth > 0.15f && !d.Arcade && !d.Porch && !d.Terrace)
				{
					const int steps = (std::max)(1, (int)roundf(d.Plinth / 0.18f));
					for (int s = 0; s < steps; ++s)
					{
						const float h = d.Plinth * (s + 1) / steps;
						const float out = 0.32f * (steps - s);
						m.Box(Trim, Vec3(c.DoorX - d.DoorWidth * 0.5f - 0.4f, 0.0f, w.Z0 - e - out), Vec3(c.DoorX + d.DoorWidth * 0.5f + 0.4f, h, w.Z0 - e));
					}
				}
		}
		float uRun = 0.0f;
		for (int f = 0; f < d.Floors; ++f)
		{
			const float y0 = d.Plinth + f * d.FloorHeight, y1 = y0 + d.FloorHeight;
			const float out = d.Jetty * f;
			const BD::WallMat mat = f == 0 ? d.GroundWall : d.UpperWall;
			const int slot = f == 0 ? WallLower : WallUpper;
			uRun = 0.0f;
			for (const Side& s : SidesOf(w, out))
			{
				Band b;
				b.P0 = s.P0; b.Dir = s.Dir; b.N = VCross(kUp, s.Dir); b.Len = s.Len; b.Y0 = y0; b.Y1 = y1; b.U0 = uRun;
				b.Masonry = IsMasonry(mat);
				b.Wood = mat == BD::WallMat::Planks || mat == BD::WallMat::Logs;
				uRun += s.Len;
				std::vector<Opening> holes = PlaceOpenings(c, w, s, f, y0, y1);
				BuildBand(c, slot, b, holes, true);
				if (IsPlaster(mat) && d.Frame != BD::Timber::None)
					TimberBand(c, b, holes);
				// 돌출 층 밑면 (아래층 벽 → 이 층 벽)
				if (f > 0 && d.Jetty > 0.01f)
				{
					const Vec3 p0 = b.At(0, y0), p1 = b.At(b.Len, y0);
					const Vec3 q0 = p0 - b.N * d.Jetty - b.Dir * d.Jetty, q1 = p1 - b.N * d.Jetty + b.Dir * d.Jetty;
					m.Quad(Timber, p0, p1, q1, q0, -kUp);
					// 들보 끝 (장선)
					for (float x = 0.4f; x < b.Len - 0.2f; x += 0.6f)
						m.Beam(Timber, b.At(x, y0 - 0.06f, d.Jetty), b.At(x, y0 - 0.06f, -0.02f), 0.12f, 0.1f, kUp);
				}
			}
			// 모서리: 굵은 기둥 (목조) · 엇갈린 통나무 끝 (통나무 벽)
			{
				const float x0 = w.X0 - out, x1 = w.X1 + out, z0 = w.Z0 - out, z1 = w.Z1 + out;
				const bool timberWall = mat == BD::WallMat::Planks || (IsPlaster(mat) && d.Frame != BD::Timber::None);
				for (int corner = 0; corner < 4; ++corner)
				{
					const float sx = (corner & 1) ? 1.0f : -1.0f, sz = (corner & 2) ? 1.0f : -1.0f;
					const float cxp = sx > 0 ? x1 : x0, czp = sz > 0 ? z1 : z0;
					if (c.InsideOther(cxp + sx * 0.25f, czp + sz * 0.25f, &w) || c.InsideOther(cxp - sx * 0.25f, czp + sz * 0.25f, &w) || c.InsideOther(cxp + sx * 0.25f, czp - sz * 0.25f, &w))
						continue;
					if (d.CornerPosts && timberWall)
						m.OBox(Timber, Vec3(cxp + sx * 0.03f, (y0 + y1) * 0.5f, czp + sz * 0.03f), Vec3(1, 0, 0), kUp, Vec3(0, 0, 1), 0.17f, (y1 - y0) * 0.5f, 0.17f);
					if (mat == BD::WallMat::Logs)
					{
						const float rh = 2.0f / 7.0f, r = 0.14f;
						for (int k = (int)ceilf(y0 / rh - 0.5f); (k + 0.5f) * rh < y1; ++k)
						{
							const float yc = (k + 0.5f) * rh;
							const float stick = 0.32f + 0.08f * c.U01();
							if (k & 1)
								m.Cyl(Timber, Vec3(cxp - sx * 0.1f, yc, czp - sz * r), Vec3(cxp + sx * stick, yc, czp - sz * r), r * (0.9f + 0.15f * c.U01()), 8);
							else
								m.Cyl(Timber, Vec3(cxp - sx * r, yc, czp - sz * 0.1f), Vec3(cxp - sx * r, yc, czp + sz * stick), r * (0.9f + 0.15f * c.U01()), 8);
						}
					}
				}
			}
			// 층 사이 띠
			if (d.StringCourse && f > 0)
			{
				const float e = out + 0.07f;
				m.Box(Trim, Vec3(w.X0 - e, y0 - 0.1f, w.Z0 - e), Vec3(w.X1 + e, y0 + 0.08f, w.Z1 + e));
			}
		}
		const float top = d.WallHeight();
		const float outTop = d.Jetty * (d.Floors - 1);
		// 처마 돌림띠 (두 단)
		if (d.Cornice)
		{
			const float e = outTop;
			m.Box(Trim, Vec3(w.X0 - e - 0.12f, top - 0.35f, w.Z0 - e - 0.12f), Vec3(w.X1 + e + 0.12f, top - 0.12f, w.Z1 + e + 0.12f));
			m.Box(Trim, Vec3(w.X0 - e - 0.28f, top - 0.12f, w.Z0 - e - 0.28f), Vec3(w.X1 + e + 0.28f, top + 0.02f, w.Z1 + e + 0.28f));
		}
		// 모서리 돌 (번갈아 긴 · 짧은)
		if (d.Quoins)
		{
			const float xs[2] = { w.X0, w.X1 }, zs[2] = { w.Z0, w.Z1 };
			for (int i = 0; i < 2; ++i)
				for (int k = 0; k < 2; ++k)
				{
					const float x = xs[i], z = zs[k];
					const float sx = i == 0 ? 1.0f : -1.0f, sz = k == 0 ? 1.0f : -1.0f;
					if (c.InsideOther(x - sx * 0.3f, z - sz * 0.3f, &w))
						continue;
					int n = 0;
					for (float y = d.Plinth; y < top - 0.3f; y += 0.36f, ++n)
					{
						const float lx = n & 1 ? 0.65f : 0.35f, lz = n & 1 ? 0.35f : 0.65f;
						const float x0 = (std::min)(x - sx * 0.04f, x + sx * lx), x1 = (std::max)(x - sx * 0.04f, x + sx * lx);
						const float z0 = (std::min)(z - sz * 0.04f, z + sz * lz), z1 = (std::max)(z - sz * 0.04f, z + sz * lz);
						m.Box(Trim, Vec3(x0, y + 0.02f, z0), Vec3(x1, y + 0.34f, z1));
					}
				}
		}
		// 벽기둥 · 버팀벽: 면마다 칸 경계 (창 사이)
		if (d.Pilasters || d.Buttresses)
			for (const Side& s : SidesOf(w, 0.0f))
			{
				const Vec3 n = VCross(kUp, s.Dir);
				const float spacing = (std::max)(d.WindowSpacing, d.WindowWidth + 0.4f);
				const int count = (int)floorf((s.Len - 0.6f) / spacing);
				if (count < 1)
					continue;
				const float step = s.Len / count;
				for (int i = 0; i <= count; ++i)
				{
					const float x = (std::clamp)(i * step, 0.3f, s.Len - 0.3f);
					const Vec3 base = s.P0 + s.Dir * x;
					if (c.InsideOther(base.x + n.x * 0.5f, base.z + n.z * 0.5f, &w) || c.NearTower(base.x, base.z))
						continue;
					if (d.Pilasters)
					{
						m.OBox(Trim, base + n * 0.06f + kUp * ((d.Plinth + top) * 0.5f), s.Dir, kUp, n, 0.24f, (top - d.Plinth) * 0.5f, 0.07f);
						m.OBox(Trim, base + n * 0.1f + kUp * (top - 0.5f), s.Dir, kUp, n, 0.34f, 0.14f, 0.1f);
						m.OBox(Trim, base + n * 0.1f + kUp * (d.Plinth + 0.15f), s.Dir, kUp, n, 0.32f, 0.15f, 0.1f);
					}
					if (d.Buttresses && (s.Len > 8.0f))
					{
						const float h = top * 0.85f;
						m.OBox(Trim, base + n * 0.55f + kUp * (h * 0.3f), s.Dir, kUp, n, 0.35f, h * 0.3f, 0.55f);
						m.OBox(Trim, base + n * 0.35f + kUp * (h * 0.78f), s.Dir, kUp, n, 0.3f, h * 0.18f, 0.35f);
						// 비스듬한 머리 (물매)
						const Vec3 cc = base + n * 0.35f + kUp * (h * 0.96f + 0.15f);
						const Vec3 slope = Norm(kUp - n * 0.9f);
						m.OBox(Trim, cc, s.Dir, slope, Norm(VCross(s.Dir, slope)), 0.3f, 0.06f, 0.45f);
					}
				}
			}
	}

	// ================================================================ 지붕
	struct RoofFrame
	{
		// 용마루 방향 축: 날개를 (a, b) 로 — a = 용마루 방향 길이, b = 가로 (처마 사이)
		Vec3 Ax, Bx;            // 단위 (용마루 방향, 처마 → 처마)
		Vec3 Center;            // 날개 가운데 (y = 벽 꼭대기)
		float A, B;             // 반 길이 · 반 폭
		Vec3 P(float a, float b, float y) const { return Center + Ax * a + Bx * b + kUp * y; }
	};

	RoofFrame FrameOf(const Wing& w, float top, float out)
	{
		RoofFrame f;
		f.Center = Vec3((w.X0 + w.X1) * 0.5f, top, (w.Z0 + w.Z1) * 0.5f);
		if (w.AlongX) { f.Ax = Vec3(1, 0, 0); f.Bx = Vec3(0, 0, 1); f.A = w.W() * 0.5f + out; f.B = w.D() * 0.5f + out; }
		else { f.Ax = Vec3(0, 0, 1); f.Bx = Vec3(1, 0, 0); f.A = w.D() * 0.5f + out; f.B = w.W() * 0.5f + out; }
		return f;
	}

	// 두께 있는 비탈 판: 윗면 (지붕 재질) 네 점 → 아랫면 (나무) + 처마 · 박공 테두리
	void Slab(Mesher& m, const std::vector<Vec3>& top, const Vec3& n, float th, bool edges = true)
	{
		m.Poly(Roof, top, n);
		std::vector<Vec3> bottom;
		for (const Vec3& p : top)
			bottom.push_back(p - n * th);
		m.Poly(Timber, bottom, -n);
		if (!edges)
			return;
		for (size_t i = 0; i < top.size(); ++i)
		{
			const size_t j = (i + 1) % top.size();
			const Vec3 mid = (top[i] + top[j]) * 0.5f;
			Vec3 c(0, 0, 0);
			for (const Vec3& p : top) c += p;
			c /= (float)top.size();
			Vec3 outDir = mid - c;
			outDir = outDir - n * Dot(outDir, n);
			if (outDir.LengthSquared() < 1e-8f)
				continue;
			m.Quad(Timber, top[i], top[j], bottom[j], bottom[i], Norm(outDir));
		}
	}

	float RoofThickness(const BD& d) { return d.RoofMaterial == BD::RoofMat::Thatch ? 0.45f : (d.RoofMaterial == BD::RoofMat::Turf ? 0.3f : 0.16f); }

	// 처마 밑: 벽 위 도리 + 서까래 끝 (0.6 m 마다) + 굽은 까치발 (2.2 m 마다). w0 → w1 = 벽 위 선 (지붕 윗면 높이), outDir = 처마 쪽 (수평), n = 지붕면 법선
	void EaveDetails(Ctx& c, const Vec3& w0, const Vec3& w1, const Vec3& outDir, const Vec3& n, float o, float tanP, float th)
	{
		Mesher& m = c.M;
		const Vec3 line = w1 - w0;
		const float len = line.Length();
		if (len < 0.8f || o < 0.2f)
			return;
		const Vec3 ld = line / len;
		const float vth = th * sqrtf(1.0f + tanP * tanP);
		m.Beam(Timber, w0 - kUp * (vth + 0.13f), w1 - kUp * (vth + 0.13f), 0.26f, 0.12f, outDir);
		const int nr = (std::max)(2, (int)(len / 0.6f));
		for (int i = 0; i <= nr; ++i)
		{
			const Vec3 p = w0 + ld * (0.06f + (len - 0.12f) * i / nr);
			const Vec3 a = p - outDir * 0.2f + kUp * (0.2f * tanP - vth);
			const Vec3 b = p + outDir * (o - 0.05f) - kUp * ((o - 0.05f) * tanP + vth);
			m.Beam(Timber, a - ld * 0.055f, b - ld * 0.055f, 0.14f, 0.11f, ld);
		}
		const float reach = (std::min)(o * 0.7f, 0.95f);
		const int nb = (std::max)(1, (int)roundf((len - 0.7f) / 2.2f));
		for (int i = 0; i <= nb; ++i)
		{
			const Vec3 p = w0 + ld * (0.35f + (len - 0.7f) * i / nb);
			const Vec3 q0 = p - kUp * (vth + 0.3f + reach * 1.25f);
			const Vec3 q1 = p + outDir * reach - kUp * (reach * tanP + vth + 0.12f);
			const Vec3 cc = p - kUp * (vth + 0.3f);
			Vec3 prev = q0;
			for (int k = 1; k <= 4; ++k)
			{
				const float t = k / 4.0f;
				const Vec3 qt = q0 * ((1 - t) * (1 - t)) + cc * (2 * (1 - t) * t) + q1 * (t * t);
				m.Beam(Timber, prev - ld * 0.075f, qt - ld * 0.075f, 0.16f, 0.15f, ld);
				prev = qt;
			}
			m.OBox(Timber, q0 + outDir * 0.07f - kUp * 0.05f, ld, kUp, outDir, 0.1f, 0.14f, 0.07f);
		}
	}

	// 박공 판 하나: 처마 끝 e → 꼭대기 x (위 가장자리), n = 박공 바깥. carved = 물결 아래 끝 + 꼭대기 너머로 뿔처럼
	void BargeBoard(Mesher& m, const Vec3& e, const Vec3& x, const Vec3& n, float th, int kind, bool horn)
	{
		const Vec3 dir = Norm(x - e);
		const Vec3 end = horn ? x + dir * 0.9f : x;
		const float total = (end - e).Length(), apexT = (x - e).Length() / (std::max)(0.01f, total);
		Vec3 down = Norm(VCross(n, dir));
		if (down.y > 0.0f)
			down = -down;
		const float base = th * 1.25f + 0.17f;
		const int segs = kind >= 2 ? (std::max)(8, (int)(total / 0.08f)) : 2;
		m.Board(Timber, e, end, down, n, 0.06f, segs, [&](float t) {
			float w = base;
			if (kind >= 2)
				w *= 0.66f + 0.34f * fabsf(sinf(t * total / 0.42f * XM_PI));
			if (t > apexT)
				w *= 1.0f - 0.55f * (t - apexT) / (std::max)(0.01f, 1.0f - apexT);
			return w;
		});
	}

	void Finial(Mesher& m, const Vec3& base)
	{
		m.Frustum(Timber, base, 0.075f, 0.055f, 0.7f, 6, true);
		m.Frustum(Timber, base + kUp * 0.7f, 0.1f, 0.1f, 0.06f, 6, true, true);
		m.Blob(Timber, base + kUp * 0.9f, 0.11f, 7);
		m.Frustum(Timber, base + kUp * 1.0f, 0.04f, 0.0f, 0.25f, 6, false);
	}

	// 박공벽 (세모) — 위층 벽 재질. 계단 박공이면 계단 윤곽 (지붕 위로 솟는다)
	void GableWall(Ctx& c, const RoofFrame& f, float sign, float rise, float halfB, int slot, bool timber)
	{
		Mesher& m = c.M;
		const BD& d = c.D;
		const Vec3 n = f.Ax * sign;
		const float a = (f.A - d.Overhang * 0.6f) * sign;   // 벽 자리 (처마 내민 것 빼고)
		const float aw = sign * (std::max)(0.0f, fabsf(a));
		if (d.CrowSteps)
		{
			// 계단: 4 ~ 6 단, 지붕면보다 0.4 m 위로
			const int steps = std::clamp((int)(rise / 0.7f), 3, 7);
			std::vector<Vec3> front;
			for (int s = 0; s < steps; ++s)
			{
				const float y0 = rise * s / steps, y1 = rise * (s + 1) / steps;
				const float half = halfB * (1.0f - (float)s / steps);
				for (float th : { 0.0f, -0.4f })
				{
					(void)th;
				}
				const Vec3 p0 = f.P(aw, -half, y0), p1 = f.P(aw, half, y0), p2 = f.P(aw, half, y1 + 0.4f), p3 = f.P(aw, -half, y1 + 0.4f);
				m.Quad(slot, p0, p1, p2, p3, n);
				m.Quad(slot, p0 - n * 0.4f, p1 - n * 0.4f, p2 - n * 0.4f, p3 - n * 0.4f, -n);
				// 계단 윗면 · 옆면
				m.Quad(Trim, p3, p2, p2 - n * 0.42f, p3 - n * 0.42f, kUp);
				m.Quad(slot, p1, p2, p2 - n * 0.4f, p1 - n * 0.4f, f.Bx);
				m.Quad(slot, p0, p3, p3 - n * 0.4f, p0 - n * 0.4f, -f.Bx);
			}
			return;
		}
		const Vec3 p0 = f.P(aw, -halfB, 0.0f), p1 = f.P(aw, halfB, 0.0f), p2 = f.P(aw, 0.0f, rise);
		// u 를 노멀맵 규약 방향으로 (B = 아래)
		m.Poly(slot, { p0, p1, p2 }, n);
		if (timber)
		{
			const float tw = d.TimberWidth;
			const Vec3 o = n * 0.005f;
			m.Beam(Timber, p0 + o + kUp * (tw * 0.5f), p1 + o + kUp * (tw * 0.5f), tw, 0.05f, n);
			m.Beam(Timber, f.P(aw, 0.0f, 0.0f) + o, p2 + o, tw, 0.05f, n);
			m.Beam(Timber, f.P(aw, -halfB * 0.5f, 0.0f) + o, f.P(aw, -halfB * 0.12f, rise * 0.78f) + o, tw * 0.8f, 0.05f, n);
			m.Beam(Timber, f.P(aw, halfB * 0.5f, 0.0f) + o, f.P(aw, halfB * 0.12f, rise * 0.78f) + o, tw * 0.8f, 0.05f, n);
		}
	}

	// 박공 지붕 (gambrel = 두 꺾임)
	void GableRoof(Ctx& c, const Wing& w, bool gambrel, int wallSlot, bool timberGable, float baseY, float pitchDeg, float outA, float outB)
	{
		const BD& d = c.D;
		Mesher& m = c.M;
		const float top = baseY;
		RoofFrame f = FrameOf(w, top, 0.0f);
		const float halfB = f.B;      // 벽 사이 반 폭
		const float o = outB;         // 처마
		const float ro = outA;        // 박공 끝 내밈
		const float th = RoofThickness(d);
		const float tanP = tanf(XMConvertToRadians(std::clamp(pitchDeg, 5.0f, 80.0f)));
		const float A = f.A + (d.CrowSteps ? -0.2f : ro);
		float rise;
		if (!gambrel)
		{
			rise = halfB * tanP;
			for (float sb : { -1.0f, 1.0f })
			{
				const Vec3 e0 = f.P(-A, sb * (halfB + o), -o * tanP), e1 = f.P(A, sb * (halfB + o), -o * tanP);
				const Vec3 r0 = f.P(-A, 0.0f, rise), r1 = f.P(A, 0.0f, rise);
				const Vec3 n = Norm(f.Bx * (sb * tanP) + kUp);
				Slab(m, { e0, e1, r1, r0 }, n, th);
			}
		}
		else
		{
			// 아래 가파른 (68°) 비탈 → 꺾임 → 위 완만한 비탈
			const float t1 = tanf(XMConvertToRadians(68.0f));
			const float inset = halfB * 0.32f, h1 = inset * t1;
			const float h2 = (halfB - inset) * tanP;
			rise = h1 + h2;
			for (float sb : { -1.0f, 1.0f })
			{
				const Vec3 n1 = Norm(f.Bx * (sb * t1) + kUp), n2 = Norm(f.Bx * (sb * tanP) + kUp);
				Slab(m, { f.P(-A, sb * (halfB + o), -o * t1), f.P(A, sb * (halfB + o), -o * t1), f.P(A, sb * (halfB - inset), h1), f.P(-A, sb * (halfB - inset), h1) }, n1, th);
				Slab(m, { f.P(-A, sb * (halfB - inset), h1), f.P(A, sb * (halfB - inset), h1), f.P(A, 0.0f, rise), f.P(-A, 0.0f, rise) }, n2, th);
			}
		}
		// 용마루 덮개
		const float capW = d.RoofMaterial == BD::RoofMat::Thatch ? 0.5f : 0.22f;
		m.OBox(Roof, f.P(0.0f, 0.0f, rise + capW * 0.25f), f.Ax, kUp, f.Bx, A, capW * 0.3f, capW * 0.5f);
		// 박공벽 (양 끝)
		for (float sa : { -1.0f, 1.0f })
		{
			const float aw = (f.A) * sa;
			const Vec3 n = f.Ax * sa;
			if (c.InsideOther(f.P(aw + sa * 0.4f, 0.0f, 0.0f).x, f.P(aw + sa * 0.4f, 0.0f, 0.0f).z, &w))
				continue;   // 다른 날개 속 박공
			if (!gambrel)
			{
				if (d.CrowSteps)
				{
					RoofFrame g = f;
					g.A = f.A;
					GableWall(c, g, sa, rise, halfB, wallSlot, false);
				}
				else
				{
					m.Poly(wallSlot, { f.P(aw, -halfB, 0.0f), f.P(aw, halfB, 0.0f), f.P(aw, 0.0f, rise) }, n);
					if (d.PaintedGables && !timberGable && rise > 1.2f)
					{
						// 칠한 무늬 판 (가장자리에서 안으로)
						const Vec3 g0 = f.P(aw, -halfB, 0.0f), g1 = f.P(aw, halfB, 0.0f), g2 = f.P(aw, 0.0f, rise);
						const Vec3 gc = (g0 + g1 + g2) / 3.0f + kUp * 0.05f;
						const Vec3 lift = n * 0.015f;
						m.Poly(Ornament, { gc + (g0 - gc) * 0.8f + lift, gc + (g1 - gc) * 0.8f + lift, gc + (g2 - gc) * 0.8f + lift }, n);
					}
					if (timberGable)
					{
						const float tw = d.TimberWidth;
						const Vec3 off = n * 0.005f;
						m.Beam(Timber, f.P(aw, -halfB, tw * 0.5f) + off, f.P(aw, halfB, tw * 0.5f) + off, tw, 0.05f, n);
						m.Beam(Timber, f.P(aw, 0.0f, 0.0f) + off, f.P(aw, 0.0f, rise) + off, tw, 0.05f, n);
						m.Beam(Timber, f.P(aw, -halfB * 0.55f, 0.0f) + off, f.P(aw, -halfB * 0.1f, rise * 0.8f) + off, tw * 0.8f, 0.05f, n);
						m.Beam(Timber, f.P(aw, halfB * 0.55f, 0.0f) + off, f.P(aw, halfB * 0.1f, rise * 0.8f) + off, tw * 0.8f, 0.05f, n);
					}
				}
			}
			else
			{
				const float t1 = tanf(XMConvertToRadians(68.0f)), inset = halfB * 0.32f, h1 = inset * t1;
				m.Poly(wallSlot, { f.P(aw, -halfB, 0.0f), f.P(aw, halfB, 0.0f), f.P(aw, halfB - inset, h1), f.P(aw, 0.0f, rise), f.P(aw, -(halfB - inset), h1) }, n);
			}
			// 로즈 창 (고딕 — 앞 박공) · 박공 창
			const bool front = Dot(n, Vec3(0, 0, -1)) > 0.7f;
			if (front && d.RoseWindow && rise > 2.0f)
			{
				const Vec3 cpos = f.P(aw, 0.0f, rise * 0.42f) + n * 0.02f;
				const float r = (std::min)(halfB * 0.45f, rise * 0.3f);
				m.Frustum(Trim, cpos - n * 0.0f + Vec3(0, 0, 0), 0, 0, 0, 3, false);   // (자리 표시 없음)
				std::vector<Vec3> pts;
				const Vec3 side = VCross(kUp, n) * -1.0f;
				for (int i = 0; i < 24; ++i)
				{
					const float a = XM_2PI * i / 24;
					pts.push_back(cpos + side * (cosf(a) * r) + kUp * (sinf(a) * r));
				}
				m.Fan(Glass, pts, n);
				// 돌 바퀴살 · 테
				for (int i = 0; i < 8; ++i)
				{
					const float a = XM_2PI * i / 8;
					m.Beam(Trim, cpos, cpos + side * (cosf(a) * r) + kUp * (sinf(a) * r), 0.08f, 0.08f, n);
				}
				for (int i = 0; i < 24; ++i)
				{
					const float a0 = XM_2PI * i / 24, a1 = XM_2PI * (i + 1) / 24;
					m.Beam(Trim, cpos + side * (cosf(a0) * r) + kUp * (sinf(a0) * r), cpos + side * (cosf(a1) * r) + kUp * (sinf(a1) * r), 0.16f, 0.1f, n);
				}
				++m.O.Windows;
			}
		}
		// 처마 밑 · 박공 도리 끝 · 박공 판 · 꼭대기 장식
		if (d.Brackets)
			for (float sb : { -1.0f, 1.0f })
			{
				const float tE = gambrel ? tanf(XMConvertToRadians(68.0f)) : tanP;
				EaveDetails(c, f.P(-f.A + 0.1f, sb * halfB, 0.0f), f.P(f.A - 0.1f, sb * halfB, 0.0f), f.Bx * sb, Norm(f.Bx * (sb * tE) + kUp), o, tE, th);
			}
		const float vth = th * sqrtf(1.0f + tanP * tanP);
		for (float sa : { -1.0f, 1.0f })
		{
			if (d.CrowSteps || gambrel)
				break;
			const Vec3 probe = f.P(sa * (f.A + 0.4f), 0.0f, 0.0f);
			if (c.InsideOther(probe.x, probe.z, &w))
				continue;
			const Vec3 n = f.Ax * sa;
			if (d.Brackets && ro > 0.25f)
				for (float bp : { 0.0f, -halfB * 0.55f, halfB * 0.55f })
				{
					const float y = (halfB - fabsf(bp)) * tanP - vth;
					m.Beam(Timber, f.P(sa * (f.A - 0.4f), bp - 0.11f, y), f.P(sa * (A + 0.04f), bp - 0.11f, y), 0.22f, 0.22f, -kUp);
				}
			if (d.Bargeboards > 0)
				for (float sb : { -1.0f, 1.0f })
					BargeBoard(m, f.P(sa * (A + 0.07f), sb * (halfB + o + 0.03f), -o * tanP + 0.04f), f.P(sa * (A + 0.07f), 0.0f, rise + 0.04f), n, th, d.Bargeboards,
						d.Bargeboards >= 2 && !d.DragonHeads);
			if (d.Finials && !d.DragonHeads)
				Finial(m, f.P(sa * (A + 0.04f), 0.0f, rise + 0.08f));
		}
		// 용머리 (박공 꼭대기에서 엇갈려 솟은 판)
		if (d.DragonHeads)
			for (float sa : { -1.0f, 1.0f })
			{
				const Vec3 apex = f.P(sa * A, 0.0f, rise + th);
				for (float sb : { -1.0f, 1.0f })
				{
					const Vec3 dir = Norm(f.Bx * (-sb) + kUp * tanP);
					const Vec3 p1 = apex + dir * 1.1f;
					const Vec3 p2 = p1 + Norm(dir + f.Bx * (-sb * 0.8f)) * 0.5f;
					const Vec3 p3 = p2 + Norm(f.Bx * (sb * 0.6f) + kUp * 0.4f + f.Ax * sa * 0.3f) * 0.35f;
					m.Beam(Timber, apex - dir * 0.6f, p1, 0.16f, 0.06f, f.Ax * sa);
					m.Beam(Timber, p1, p2, 0.14f, 0.06f, f.Ax * sa);
					m.Beam(Timber, p2, p3, 0.12f, 0.06f, f.Ax * sa);
				}
			}
	}

	// 모임 지붕 (hip) · 사각뿔 (ridge 0). baseY 높이, 처마 o. inset = 지붕 밑변을 안으로 (맨사드 윗단)
	void HipRoof(Ctx& c, const RoofFrame& f, float baseY, float pitchDeg, float o, bool cap = true)
	{
		Mesher& m = c.M;
		const BD& d = c.D;
		const float tanP = tanf(XMConvertToRadians(std::clamp(pitchDeg, 5.0f, 80.0f)));
		const float th = RoofThickness(d);
		const float A = f.A + o, B = f.B + o;
		const float rise = B * tanP;
		const float ridge = (std::max)(0.0f, A - B);
		auto P = [&](float a, float b, float y) { return Vec3(f.Center.x, baseY, f.Center.z) + f.Ax * a + f.Bx * b + kUp * y; };
		const float ey = -o * tanP;
		const Vec3 c00 = P(-A, -B, ey), c10 = P(A, -B, ey), c11 = P(A, B, ey), c01 = P(-A, B, ey);
		const Vec3 r0 = P(-ridge, 0.0f, rise + ey), r1 = P(ridge, 0.0f, rise + ey);
		for (float sb : { -1.0f, 1.0f })
		{
			const Vec3 n = Norm(f.Bx * (sb * tanP) + kUp);
			if (sb < 0) Slab(m, { c00, c10, r1, r0 }, n, th);
			else Slab(m, { c01, c11, r1, r0 }, n, th);
		}
		for (float sa : { -1.0f, 1.0f })
		{
			const Vec3 n = Norm(f.Ax * (sa * tanP) + kUp);
			if (sa < 0) Slab(m, { c00, c01, r0 }, n, th);
			else Slab(m, { c10, c11, r1 }, n, th);
		}
		if (cap && ridge > 0.05f)
			m.OBox(Roof, P(0.0f, 0.0f, rise + ey + 0.06f), f.Ax, kUp, f.Bx, ridge, 0.07f, 0.11f);
		if (d.Brackets && o >= 0.2f)
			for (float sg : { -1.0f, 1.0f })
			{
				EaveDetails(c, P(-f.A + 0.3f, sg * f.B, 0.0f), P(f.A - 0.3f, sg * f.B, 0.0f), f.Bx * sg, Norm(f.Bx * (sg * tanP) + kUp), o, tanP, th);
				EaveDetails(c, P(sg * f.A, -f.B + 0.3f, 0.0f), P(sg * f.A, f.B - 0.3f, 0.0f), f.Ax * sg, Norm(f.Ax * (sg * tanP) + kUp), o, tanP, th);
			}
	}

	// 굴뚝 (용마루 근처, 위로 1 m 넘게)
	void Chimneys(Ctx& c, const Wing& w, float top, float rise)
	{
		const BD& d = c.D;
		for (int i = 0; i < d.Chimneys; ++i)
		{
			RoofFrame f = FrameOf(w, top, 0.0f);
			const float t = d.Chimneys == 1 ? 0.3f : -0.7f + 1.4f * i / (d.Chimneys - 1);
			const float a = f.A * t * 0.85f, b = f.B * (c.U01() < 0.5f ? 0.25f : -0.25f);
			const Vec3 base = f.P(a, b, -1.0f);
			const float h = rise + 1.0f + 1.2f + c.U01() * 0.5f;
			c.M.Box(Trim, base + Vec3(-0.38f, 0.0f, -0.38f), base + Vec3(0.38f, h, 0.38f));
			c.M.Box(Trim, base + Vec3(-0.48f, h, -0.48f), base + Vec3(0.48f, h + 0.15f, 0.48f));
			c.M.Box(Metal, base + Vec3(-0.12f, h + 0.15f, -0.12f), base + Vec3(0.12f, h + 0.45f, 0.12f));
		}
	}

	// 지붕창 (앞 비탈): 작은 상자 벽 + 창 + 작은 박공 지붕
	void Dormers(Ctx& c, const Wing& w, float top, float pitchDeg, int count)
	{
		const BD& d = c.D;
		Mesher& m = c.M;
		if (count <= 0 || !w.AlongX)
			return;
		RoofFrame f = FrameOf(w, top, 0.0f);
		const float tanP = tanf(XMConvertToRadians(std::clamp(pitchDeg, 15.0f, 75.0f)));
		const float dw = 1.5f, dh = 1.5f;
		const float inset = f.B * 0.28f;
		const float baseY = top + inset * tanP - 0.15f;
		for (int i = 0; i < count; ++i)
		{
			const float t = count == 1 ? 0.0f : -0.65f + 1.3f * i / (count - 1);
			const float cx = f.Center.x + f.A * t;
			const float fz = w.Z0 + inset;          // 앞벽 z
			const float back = fz + 2.6f;
			// 앞벽 (창 하나)
			Band b;
			b.P0 = Vec3(cx - dw * 0.5f, 0, fz); b.Dir = Vec3(1, 0, 0); b.N = VCross(kUp, b.Dir); b.Len = dw; b.Y0 = baseY; b.Y1 = baseY + dh; b.U0 = 0.0f;
			Opening o;
			o.A = 0.3f; o.B = dw - 0.3f; o.S = baseY + 0.35f; o.H = dh - 0.55f; o.Shape = BD::Window::Rectangular; o.Depth = 0.12f;
			BuildBand(c, WallUpper, b, { o }, false);
			// 옆벽
			m.Quad(WallUpper, Vec3(cx - dw * 0.5f, baseY, fz), Vec3(cx - dw * 0.5f, baseY + dh, fz), Vec3(cx - dw * 0.5f, baseY + dh, back), Vec3(cx - dw * 0.5f, baseY, back), Vec3(-1, 0, 0));
			m.Quad(WallUpper, Vec3(cx + dw * 0.5f, baseY, fz), Vec3(cx + dw * 0.5f, baseY + dh, fz), Vec3(cx + dw * 0.5f, baseY + dh, back), Vec3(cx + dw * 0.5f, baseY, back), Vec3(1, 0, 0));
			// 박공 지붕 (용마루 = 앞뒤 방향)
			const float rr = (dw * 0.5f + 0.2f) * 0.9f;
			for (float sx : { -1.0f, 1.0f })
			{
				const Vec3 n = Norm(Vec3(sx * 0.9f, 1.0f, 0.0f));
				Slab(m, { Vec3(cx + sx * (dw * 0.5f + 0.2f), baseY + dh - 0.18f, fz - 0.3f), Vec3(cx + sx * (dw * 0.5f + 0.2f), baseY + dh - 0.18f, back),
					Vec3(cx, baseY + dh - 0.18f + rr, back), Vec3(cx, baseY + dh - 0.18f + rr, fz - 0.3f) }, n, 0.1f);
			}
			m.Poly(WallUpper, { Vec3(cx - dw * 0.5f, baseY + dh, fz), Vec3(cx + dw * 0.5f, baseY + dh, fz), Vec3(cx, baseY + dh - 0.18f + rr - 0.05f, fz) }, Vec3(0, 0, -1));
		}
		(void)d;
	}

	// 탑 · 둥근 몸채: 다각형 (sides) 벽 + 창 + 띠 + 지붕
	void TowerBody(Ctx& c, const Vec3& center, float r0, float h, int sides, float taper, BD::Roof roof, bool frontDoor, bool crenel, float spire, bool mainBody)
	{
		const BD& d = c.D;
		Mesher& m = c.M;
		const int floors = (std::max)(1, (int)roundf((h - d.Plinth) / d.FloorHeight));
		const float fh = (h - d.Plinth) / floors;
		auto R = [&](float y) { return r0 * (1.0f - taper * std::clamp(y / (std::max)(h, 0.1f), 0.0f, 1.0f)); };
		// 밑돌 (조금 굵게)
		m.Frustum(Trim, center, R(0) + 0.1f, R(d.Plinth) + 0.1f, d.Plinth, sides, false);
		// 면 목록 (각 a0 ~ a1): 문이 있으면 앞 (-Z, 270°) 면을 문이 들어갈 만큼 넓게 — 나머지 둘레를 고르게
		std::vector<std::pair<float, float>> faces;
		int frontFace = -1;
		if (frontDoor && sides > 4)
		{
			const float step = XM_2PI / sides;
			const float need = 2.0f * asinf((std::min)(0.98f, (d.DoorWidth + 0.7f) / (2.0f * r0)));
			const float span = (std::max)(step, need);
			const int rest = (std::max)(3, (int)roundf((XM_2PI - span) / step));
			const float start = XM_PI * 1.5f - span * 0.5f;
			faces.push_back({ start, start + span });
			frontFace = 0;
			for (int i = 0; i < rest; ++i)
				faces.push_back({ start + span + (XM_2PI - span) * i / rest, start + span + (XM_2PI - span) * (i + 1) / rest });
		}
		else
		{
			const float phase = sides == 4 ? XM_PIDIV4 : 0.0f;
			for (int i = 0; i < sides; ++i)
				faces.push_back({ phase + XM_2PI * i / sides, phase + XM_2PI * (i + 1) / sides });
		}
		// 많은 면 (둥근 탑) 은 네 방향 (층마다 45° 돌려) 에 가장 가까운 면에만 창
		auto nearestFace = [&](float angle) {
			int best = 0;
			float bd = 1e9f;
			for (int i = 0; i < (int)faces.size(); ++i)
			{
				float diff = fmodf(fabsf((faces[i].first + faces[i].second) * 0.5f - angle), XM_2PI);
				diff = (std::min)(diff, XM_2PI - diff);
				if (diff < bd) { bd = diff; best = i; }
			}
			return best;
		};
		for (int fl = 0; fl < floors; ++fl)
		{
			const float y0 = d.Plinth + fl * fh, y1 = y0 + fh;
			const int slot = fl == 0 ? WallLower : WallUpper;
			// 면마다 띠 (위아래 반지름이 달라 사다리꼴 — 띠는 아래 반지름으로, 위쪽은 기울임으로 맞춘다)
			const float ra = R(y0), rb = R(y1);
			const int faceCount = (int)faces.size();
			std::vector<char> holeFace(faces.size(), 0);
			if (faceCount > 8)
				for (int k = 0; k < 4; ++k)
					holeFace[(size_t)nearestFace(XM_PI * 1.5f + XM_PIDIV2 * k + (fl & 1) * XM_PIDIV4)] = 1;
			for (int i = 0; i < faceCount; ++i)
			{
				const float a0 = faces[(size_t)i].first, a1 = faces[(size_t)i].second;
				const Vec3 pa0 = center + Vec3(cosf(a0) * ra, y0, sinf(a0) * ra), pa1 = center + Vec3(cosf(a1) * ra, y0, sinf(a1) * ra);
				const Vec3 pb0 = center + Vec3(cosf(a0) * rb, y1, sinf(a0) * rb), pb1 = center + Vec3(cosf(a1) * rb, y1, sinf(a1) * rb);
				const float am = (a0 + a1) * 0.5f;
				const Vec3 outN = Norm(Vec3(cosf(am), (ra - rb) / fh, sinf(am)));
				const float faceLen = (pa1 - pa0).Length();
				// 창: 면이 넓으면 (둥근 탑은 네 방향만)
				const bool wantHole = faceCount <= 8 ? faceLen > d.WindowWidth + 0.8f : holeFace[(size_t)i] != 0;
				const bool isFront = fl == 0 && i == frontFace;
				const Vec3 wpos = center + Vec3(cosf(am) * (ra + 0.5f), 0, sinf(am) * (ra + 0.5f));
				const bool blocked = !mainBody && c.InsideOther(wpos.x, wpos.z, nullptr);
				if ((wantHole || isFront) && !blocked && ra - rb < 0.6f)
				{
					// 면 하나를 띠로 (세로 기울기는 작다고 보고 아래 둘레로)
					Band b;
					b.Dir = Norm(pa1 - pa0); b.N = VCross(kUp, b.Dir); b.P0 = pa0; b.Len = faceLen; b.Y0 = y0; b.Y1 = y1; b.U0 = a0 * ra;
					b.Masonry = IsMasonry(fl == 0 ? d.GroundWall : d.UpperWall);
					b.Wood = (fl == 0 ? d.GroundWall : d.UpperWall) == BD::WallMat::Planks;
					std::vector<Opening> holes;
					Opening o;
					if (isFront)
					{
						o.A = faceLen * 0.5f - d.DoorWidth * 0.5f; o.B = faceLen * 0.5f + d.DoorWidth * 0.5f; o.S = y0; o.H = (std::min)(d.DoorHeight, fh - 0.3f);
						o.Shape = DoorShape(d); o.Kind = Hole::Door; o.Depth = 0.35f;
						if (o.A > 0.1f)
							holes.push_back(o);
					}
					else
					{
						const BD::Window st = fl == 0 ? d.GroundWindowStyle : d.WindowStyle;
						if (st != BD::Window::None)
						{
							const float ww = st == BD::Window::Slit ? 0.3f : (std::min)(d.WindowWidth, faceLen - 0.5f);
							const float wh = st == BD::Window::Round ? ww : (std::min)(d.WindowHeight, fh - 0.8f);
							o.A = faceLen * 0.5f - ww * 0.5f; o.B = faceLen * 0.5f + ww * 0.5f; o.S = y0 + (std::min)(d.SillHeight, fh - wh - 0.3f); o.H = wh;
							o.Shape = st; o.Depth = st == BD::Window::Slit ? 0.6f : 0.3f;
							if (o.A > 0.1f && o.H > 0.3f)
								holes.push_back(o);
						}
					}
					if (rb != ra)
					{
						// 기운 면: 띠 대신 사다리꼴 한 장 + 구멍 없음 (가늘어지는 탑은 구멍을 같은 면 위쪽에 두지 않는다)
						Band bb = b;
						BuildBand(c, slot, bb, holes, true);
						// 위로 좁아지는 몫 (띠 위 가장자리 → 다음 반지름): 얇은 사다리꼴로 덮는다
						m.Quad(slot, center + Vec3(cosf(a0) * ra, y1, sinf(a0) * ra), center + Vec3(cosf(a1) * ra, y1, sinf(a1) * ra), pb1, pb0, kUp);
					}
					else
						BuildBand(c, slot, b, holes, true);
				}
				else
				{
					const uint32_t i0 = m.Vert(slot, pa0, outN, Norm(pa1 - pa0), a0 * ra, -y0);
					const uint32_t i1 = m.Vert(slot, pa1, outN, Norm(pa1 - pa0), a1 * ra, -y0);
					const uint32_t i2 = m.Vert(slot, pb1, outN, Norm(pa1 - pa0), a1 * ra, -y1);
					const uint32_t i3 = m.Vert(slot, pb0, outN, Norm(pa1 - pa0), a0 * ra, -y1);
					m.Tri(slot, i0, i1, i2, outN);
					m.Tri(slot, i0, i2, i3, outN);
				}
			}
			if (d.StringCourse && fl > 0)
				m.Frustum(Trim, center + Vec3(0, y0 - 0.1f, 0), R(y0) + 0.08f, R(y0) + 0.08f, 0.2f, sides, true, true);
		}
		const float rt = R(h);
		// 지붕
		const float o = d.Overhang;
		if (crenel || roof == BD::Roof::Flat)
		{
			m.Frustum(Roof, center + Vec3(0, h - 0.05f, 0), rt, rt, 0.05f, sides, true);
			// 흉벽 + 총안 (돌출 받침 위)
			m.Frustum(Trim, center + Vec3(0, h - 0.4f, 0), rt, rt + 0.35f, 0.4f, sides, false);
			m.Frustum(WallUpper, center + Vec3(0, h, 0), rt + 0.35f, rt + 0.35f, 0.7f, sides, false);
			m.Frustum(WallUpper, center + Vec3(0, h, 0), rt - 0.1f, rt - 0.1f, 0.7f, sides, false);
			const int merlons = (std::max)(6, (int)(XM_2PI * rt / 1.3f));
			for (int i = 0; i < merlons; ++i)
			{
				const float a = XM_2PI * (i + 0.25f) / merlons;
				const Vec3 radial(cosf(a), 0, sinf(a));
				m.OBox(WallUpper, center + Vec3(0, h + 0.7f + 0.4f, 0) + radial * (rt + 0.12f), Vec3(-sinf(a), 0, cosf(a)), kUp, radial, XM_PI * rt / merlons * 0.55f, 0.4f, 0.24f);
			}
			if (roof == BD::Roof::Flat || roof == BD::Roof::Count)
				return;
		}
		const float baseY = crenel ? h + 0.2f : h;
		const float rr = crenel ? rt * 0.85f : rt + o;
		const float pitch = tanf(XMConvertToRadians(std::clamp(d.RoofPitch < 50.0f && roof == BD::Roof::Cone ? 62.0f : d.RoofPitch, 10.0f, 82.0f)));
		switch (roof)
		{
		case BD::Roof::Dome:
			m.Revolve(Roof, center + Vec3(0, baseY, 0), (std::max)(16, sides), 10, [&](float t) { const float a = t * XM_PIDIV2; return XMFLOAT2(rr * cosf(a) + 0.0001f, rr * sinf(a) * 0.95f); });
			break;
		case BD::Roof::Onion:
		{
			const float H = rr * 2.6f;
			m.Revolve(Roof, center + Vec3(0, baseY, 0), (std::max)(16, sides), 14, [&](float t) {
				const float rad = rr * (t < 0.45f ? 1.0f + 0.32f * sinf(t / 0.45f * XM_PI * 0.9f) : (1.0f - (t - 0.45f) / 0.55f) * 1.22f * (1.0f - (t - 0.45f) / 0.55f * 0.2f));
				return XMFLOAT2((std::max)(0.001f, rad), H * t);
			});
			break;
		}
		case BD::Roof::Pyramid:
		{
			const float h2 = rr * pitch;
			m.Frustum(Roof, center + Vec3(0, baseY - o * 0.3f, 0), rr * 1.0f, 0.0f, h2, 4, false);
			break;
		}
		default:   // Cone
		{
			const float h2 = rr * pitch;
			m.Frustum(Roof, center + Vec3(0, baseY - o * 0.4f, 0), rr, 0.0f, h2, (std::max)(sides, 12), false);
			m.Frustum(Timber, center + Vec3(0, baseY - o * 0.4f - 0.15f, 0), rr - 0.02f, rr - 0.02f, 0.15f, (std::max)(sides, 12), false);
			// 처마 밑 (아래를 보는 고리)
			std::vector<Vec3> under;
			for (int i = 0; i < (std::max)(sides, 12); ++i)
			{
				const float a = XM_2PI * i / (std::max)(sides, 12);
				under.push_back(center + Vec3(cosf(a) * rr, baseY - o * 0.4f - 0.15f, sinf(a) * rr));
			}
			m.Fan(Timber, under, -kUp);
			// 꼭대기 장식
			m.Frustum(Metal, center + Vec3(0, baseY - o * 0.4f + h2 - 0.2f, 0), 0.06f, 0.02f, 1.0f + spire * 0.1f, 6, true);
			break;
		}
		}
		if (spire > 0.1f && roof != BD::Roof::Cone)
		{
			const float top = baseY + rr * (roof == BD::Roof::Pyramid ? pitch : (roof == BD::Roof::Onion ? 2.6f : 0.95f));
			m.Frustum(Roof, center + Vec3(0, top - 0.3f, 0), (std::min)(rr * 0.35f, 1.0f), 0.0f, spire, 8, false);
			m.Frustum(Metal, center + Vec3(0, top + spire - 0.4f, 0), 0.05f, 0.02f, 1.2f, 6, true);
		}
		(void)fh;
	}

	// ================================================================ 날개 만들기
	void MakeWings(Ctx& c)
	{
		const BD& d = c.D;
		const float W = (std::max)(2.0f, d.Width), D = (std::max)(2.0f, d.Depth);
		auto add = [&](float x0, float x1, float z0, float z1, bool main) {
			Wing w;
			w.X0 = x0; w.X1 = x1; w.Z0 = z0; w.Z1 = z1; w.Main = main;
			w.AlongX = (x1 - x0) >= (z1 - z0);
			c.Wings.push_back(w);
		};
		const float ww = (std::max)(2.5f, D * d.WingWidth);
		const float wl = (std::max)(2.0f, W * d.WingLength);
		switch (d.Shape)
		{
		case BD::Plan::LShape:
			add(-W * 0.5f, W * 0.5f, -D * 0.5f, D * 0.5f, true);
			add(-W * 0.5f, -W * 0.5f + ww, -D * 0.5f - wl, 0.0f, false);
			c.FrontZ = -D * 0.5f; c.DoorX = (-W * 0.5f + ww + W * 0.5f) * 0.5f;
			break;
		case BD::Plan::TShape:
			add(-W * 0.5f, W * 0.5f, -D * 0.5f, D * 0.5f, true);
			add(-ww * 0.5f, ww * 0.5f, -D * 0.5f - wl, 0.0f, false);
			c.FrontZ = -D * 0.5f - wl; c.DoorX = 0.0f;
			break;
		case BD::Plan::UShape:
			add(-W * 0.5f, W * 0.5f, -D * 0.5f, D * 0.5f, true);
			add(-W * 0.5f, -W * 0.5f + ww, -D * 0.5f - wl, 0.0f, false);
			add(W * 0.5f - ww, W * 0.5f, -D * 0.5f - wl, 0.0f, false);
			c.FrontZ = -D * 0.5f; c.DoorX = 0.0f;
			break;
		case BD::Plan::Courtyard:
		{
			const float t = (std::max)(3.0f, (std::min)(W, D) * 0.3f);
			add(-W * 0.5f, W * 0.5f, -D * 0.5f, -D * 0.5f + t, true);
			add(-W * 0.5f, W * 0.5f, D * 0.5f - t, D * 0.5f, false);
			add(-W * 0.5f, -W * 0.5f + t, -D * 0.5f + t * 0.5f, D * 0.5f - t * 0.5f, false);
			add(W * 0.5f - t, W * 0.5f, -D * 0.5f + t * 0.5f, D * 0.5f - t * 0.5f, false);
			c.FrontZ = -D * 0.5f; c.DoorX = 0.0f;
			break;
		}
		case BD::Plan::Cross:
		{
			// 본당 (앞뒤로 긴 = Width) + 가로 날개 (뒤쪽)
			add(-D * 0.5f, D * 0.5f, -W * 0.5f, W * 0.5f, true);
			const float tl = (std::max)(D + 2.0f, W * d.WingLength);
			add(-tl * 0.5f, tl * 0.5f, W * 0.12f - ww * 0.5f, W * 0.12f + ww * 0.5f, false);
			c.FrontZ = -W * 0.5f; c.DoorX = 0.0f;
			break;
		}
		default:
			add(-W * 0.5f, W * 0.5f, -D * 0.5f, D * 0.5f, true);
			c.FrontZ = -D * 0.5f; c.DoorX = 0.0f;
			break;
		}
		// 손으로 지은 집: 문이 한가운데가 아니다 (대칭 양식 — 주랑 · 둘레 기둥 · 아케이드 — 은 그대로)
		if (d.Shape == BD::Plan::Rectangle && !d.Portico && !d.Peristyle && !d.Arcade && d.Irregularity > 0.01f)
		{
			const float lim = (std::max)(0.0f, W * 0.5f - d.DoorWidth * 0.5f - 1.1f);
			c.DoorX = std::clamp((c.U01() - 0.5f) * W * 0.4f * d.Irregularity, -lim, lim);
		}
		c.M.O.Wings = (int)c.Wings.size();
	}

	// ================================================================ 주랑 · 둘레 기둥 · 발코니 · 돔 · 풍차 날개
	void Column(Mesher& m, const Vec3& base, float h, float r)
	{
		m.Box(Trim, base + Vec3(-r * 1.35f, 0, -r * 1.35f), base + Vec3(r * 1.35f, r * 0.6f, r * 1.35f));
		m.Frustum(Trim, base + Vec3(0, r * 0.6f, 0), r, r * 0.86f, h - r * 1.3f, 16, false);
		m.Frustum(Trim, base + Vec3(0, h - r * 0.7f, 0), r * 0.9f, r * 1.25f, r * 0.35f, 16, false);
		m.Box(Trim, base + Vec3(-r * 1.4f, h - r * 0.35f, -r * 1.4f), base + Vec3(r * 1.4f, h, r * 1.4f));
	}

	void PorticoFront(Ctx& c, const Wing& w)
	{
		const BD& d = c.D;
		Mesher& m = c.M;
		const int n = std::clamp(d.PorticoColumns, 2, 12);
		const float r = 0.32f + d.FloorHeight * 0.03f;
		const float span = (n - 1) * (r * 2.0f * 3.4f);
		const float depth = 3.2f + r * 2.0f;
		const float zf = w.Z0 - depth;
		const float colH = (std::min)(d.WallHeight() - d.Plinth - 0.1f, d.FloorHeight * 2.0f);
		const float baseY = d.Plinth;
		// 받침 + 계단
		m.Box(Trim, Vec3(-span * 0.5f - 1.0f, 0.0f, zf - 0.6f), Vec3(span * 0.5f + 1.0f, baseY, w.Z0));
		for (int s = 0; s < 3; ++s)
			m.Box(Trim, Vec3(-span * 0.5f - 0.6f, 0.0f, zf - 0.6f - 0.35f * (3 - s)), Vec3(span * 0.5f + 0.6f, baseY * (s + 1) / 4.0f, zf - 0.6f));
		for (int i = 0; i < n; ++i)
			Column(m, Vec3(-span * 0.5f + span * i / (n - 1), baseY, zf), colH, r);
		// 들보 + 박공 + 지붕
		const float ey = baseY + colH;
		m.Box(Trim, Vec3(-span * 0.5f - r * 2.0f, ey, zf - r * 1.6f), Vec3(span * 0.5f + r * 2.0f, ey + 0.8f, w.Z0 + 0.2f));
		const float half = span * 0.5f + r * 2.0f;
		const float rise = half * tanf(XMConvertToRadians(18.0f));
		const float py = ey + 0.8f;
		m.Poly(Trim, { Vec3(-half, py, zf - r * 1.6f), Vec3(half, py, zf - r * 1.6f), Vec3(0.0f, py + rise, zf - r * 1.6f) }, Vec3(0, 0, -1));
		for (float sx : { -1.0f, 1.0f })
		{
			const Vec3 n2 = Norm(Vec3(sx * tanf(XMConvertToRadians(18.0f)), 1.0f, 0.0f));
			Slab(m, { Vec3(sx * (half + 0.3f), py - 0.1f, zf - r * 1.6f - 0.3f), Vec3(sx * (half + 0.3f), py - 0.1f, w.Z0 + 0.5f),
				Vec3(0.0f, py + rise + 0.05f, w.Z0 + 0.5f), Vec3(0.0f, py + rise + 0.05f, zf - r * 1.6f - 0.3f) }, n2, 0.18f);
		}
	}

	void PeristyleColumns(Ctx& c, const Wing& w, float gap)
	{
		const BD& d = c.D;
		Mesher& m = c.M;
		const float r = 0.45f;
		const float x0 = w.X0 - gap, x1 = w.X1 + gap, z0 = w.Z0 - gap, z1 = w.Z1 + gap;
		const float colH = d.WallHeight() - d.Plinth;
		const int nx = (std::max)(4, (int)roundf((x1 - x0) / 2.6f)), nz = (std::max)(6, (int)roundf((z1 - z0) / 2.6f));
		for (int i = 0; i <= nx; ++i)
			for (float z : { z0, z1 })
				Column(m, Vec3(x0 + (x1 - x0) * i / nx, d.Plinth, z), colH, r);
		for (int k = 1; k < nz; ++k)
			for (float x : { x0, x1 })
				Column(m, Vec3(x, d.Plinth, z0 + (z1 - z0) * k / nz), colH, r);
		// 들보 고리
		const float ey = d.Plinth + colH;
		m.Box(Trim, Vec3(x0 - 0.6f, ey, z0 - 0.6f), Vec3(x1 + 0.6f, ey + 0.9f, z0 + 0.6f));
		m.Box(Trim, Vec3(x0 - 0.6f, ey, z1 - 0.6f), Vec3(x1 + 0.6f, ey + 0.9f, z1 + 0.6f));
		m.Box(Trim, Vec3(x0 - 0.6f, ey, z0 - 0.6f), Vec3(x0 + 0.6f, ey + 0.9f, z1 + 0.6f));
		m.Box(Trim, Vec3(x1 - 0.6f, ey, z0 - 0.6f), Vec3(x1 + 0.6f, ey + 0.9f, z1 + 0.6f));
		// 기단 (넓게)
		m.Box(Trim, Vec3(x0 - 1.0f, 0.0f, z0 - 1.0f), Vec3(x1 + 1.0f, d.Plinth, z1 + 1.0f));
		for (int s = 0; s < 4; ++s)
			m.Box(Trim, Vec3(-d.DoorWidth - 1.5f, 0.0f, z0 - 1.0f - 0.4f * (4 - s)), Vec3(d.DoorWidth + 1.5f, d.Plinth * (s + 1) / 5.0f, z0 - 1.0f));
	}

	void BalconyFront(Ctx& c, const Wing& w)
	{
		const BD& d = c.D;
		Mesher& m = c.M;
		if (d.Floors < 2)
			return;
		const float y = d.Plinth + d.FloorHeight;
		const float x0 = w.X0 + 0.4f, x1 = w.X1 - 0.4f, z = w.Z0 - d.Jetty;
		const float depth = 1.2f;
		m.Box(Timber, Vec3(x0, y - 0.12f, z - depth), Vec3(x1, y + 0.05f, z));
		// 받침 까치발
		for (float x = x0 + 0.3f; x <= x1 - 0.2f; x += 1.6f)
			m.Beam(Timber, Vec3(x, y - 0.9f, z), Vec3(x, y - 0.12f, z - depth * 0.85f), 0.12f, 0.12f, Vec3(1, 0, 0));
		// 난간: 기둥 · 손잡이 · 살
		for (float x = x0 + 0.05f; x <= x1; x += 1.6f)
			m.Box(Timber, Vec3(x - 0.06f, y, z - depth), Vec3(x + 0.06f, y + 1.0f, z - depth + 0.12f));
		m.Box(Timber, Vec3(x0, y + 0.92f, z - depth), Vec3(x1, y + 1.02f, z - depth + 0.14f));
		m.Box(Timber, Vec3(x0, y + 0.08f, z - depth), Vec3(x1, y + 0.16f, z - depth + 0.12f));
		for (float x = x0 + 0.15f; x < x1; x += 0.16f)
			m.Box(Timber, Vec3(x - 0.035f, y + 0.16f, z - depth + 0.03f), Vec3(x + 0.035f, y + 0.92f, z - depth + 0.09f));
		for (float zz : { z - depth, z })
		{
			(void)zz;
		}
		m.Box(Timber, Vec3(x0, y + 0.92f, z - depth), Vec3(x0 + 0.12f, y + 1.02f, z));
		m.Box(Timber, Vec3(x1 - 0.12f, y + 0.92f, z - depth), Vec3(x1, y + 1.02f, z));
	}

	void Dome(Ctx& c, const Wing& w, float baseY)
	{
		Mesher& m = c.M;
		const Vec3 cpos((w.X0 + w.X1) * 0.5f, baseY, (w.Z0 + w.Z1) * 0.5f);
		const float r = (std::min)(w.D() * 0.32f, 6.0f);
		m.Frustum(WallUpper, cpos, r, r, 3.0f, 24, false);
		// 북 (드럼) 의 창 · 띠
		for (int i = 0; i < 8; ++i)
		{
			const float a = XM_2PI * (i + 0.5f) / 8;
			const Vec3 radial(cosf(a), 0, sinf(a)), tang(-sinf(a), 0, cosf(a));
			m.OBox(Glass, cpos + radial * (r + 0.01f) + Vec3(0, 1.55f, 0), tang, kUp, radial, 0.35f, 0.75f, 0.02f);
			m.OBox(Trim, cpos + radial * (r + 0.06f) + Vec3(0, 1.5f, 0), tang, kUp, radial, 0.12f, 1.3f, 0.08f);
		}
		m.Frustum(Trim, cpos + Vec3(0, 3.0f, 0), r + 0.25f, r + 0.25f, 0.3f, 24, false, true);
		m.Revolve(Metal, cpos + Vec3(0, 3.3f, 0), 24, 12, [&](float t) { const float a = t * XM_PIDIV2; return XMFLOAT2(r * cosf(a) + 0.0001f, r * 1.15f * sinf(a)); });
		// 랜턴
		const float ly = 3.3f + r * 1.15f - 0.2f;
		m.Frustum(Trim, cpos + Vec3(0, ly, 0), r * 0.18f, r * 0.18f, 1.2f, 12, true);
		m.Revolve(Metal, cpos + Vec3(0, ly + 1.2f, 0), 12, 6, [&](float t) { const float a = t * XM_PIDIV2; return XMFLOAT2(r * 0.22f * cosf(a) + 0.0001f, r * 0.3f * sinf(a)); });
		m.Frustum(Metal, cpos + Vec3(0, ly + 1.2f + r * 0.3f - 0.05f, 0), 0.05f, 0.02f, 1.0f, 6, true);
	}

	void Sails(Ctx& c, const Vec3& hub)
	{
		Mesher& m = c.M;
		const Vec3 fwd(0, 0, -1);
		m.Frustum(Timber, hub - Vec3(0, 0.4f, 0), 0.35f, 0.35f, 0.8f, 10, true, true);
		for (int i = 0; i < 4; ++i)
		{
			const float a = XM_PIDIV4 + XM_PIDIV2 * i;
			const Vec3 dir(cosf(a), sinf(a), 0.0f);
			const Vec3 side(-sinf(a), cosf(a), 0.0f);
			const float L = 9.0f;
			m.Beam(Timber, hub + fwd * 0.5f, hub + fwd * 0.5f + dir * L, 0.22f, 0.22f, fwd);
			// 살 틀 · 돛 (한쪽)
			const Vec3 o0 = hub + fwd * 0.62f + dir * 1.6f;
			m.Beam(Timber, o0 + side * 1.6f, o0 + side * 1.6f + dir * (L - 1.8f), 0.08f, 0.08f, fwd);
			for (float t = 0.0f; t <= L - 1.8f; t += 0.7f)
				m.Beam(Timber, o0 + dir * t, o0 + dir * t + side * 1.6f, 0.06f, 0.06f, fwd);
			const Vec3 s0 = o0 + fwd * 0.05f + dir * 0.2f, s1 = s0 + dir * (L - 2.4f);
			m.Quad(Fabric, s0 + side * 0.1f, s1 + side * 0.1f, s1 + side * 1.5f, s0 + side * 1.5f, fwd);
			m.Quad(Fabric, s0 + side * 0.1f - fwd * 0.02f, s1 + side * 0.1f - fwd * 0.02f, s1 + side * 1.5f - fwd * 0.02f, s0 + side * 1.5f - fwd * 0.02f, -fwd);
		}
	}

	// ================================================================ 스타브 교회: 둘레 회랑 지붕 + 위층 탑신
	void StaveTiers(Ctx& c, const Wing& w, float top)
	{
		const BD& d = c.D;
		Mesher& m = c.M;
		// 회랑 지붕 (둘레, 2.4 m 높이에서 바깥으로)
		const float gy = d.Plinth + 2.4f, gout = 1.4f;
		const float x0 = w.X0, x1 = w.X1, z0 = w.Z0, z1 = w.Z1;
		const float drop = gout * 0.5f;
		Slab(m, { Vec3(x0 - gout, gy - drop, z0 - gout), Vec3(x1 + gout, gy - drop, z0 - gout), Vec3(x1, gy, z0), Vec3(x0, gy, z0) }, Norm(Vec3(0, 1, -0.5f)), 0.1f);
		Slab(m, { Vec3(x1 + gout, gy - drop, z1 + gout), Vec3(x0 - gout, gy - drop, z1 + gout), Vec3(x0, gy, z1), Vec3(x1, gy, z1) }, Norm(Vec3(0, 1, 0.5f)), 0.1f);
		Slab(m, { Vec3(x0 - gout, gy - drop, z1 + gout), Vec3(x0 - gout, gy - drop, z0 - gout), Vec3(x0, gy, z0), Vec3(x0, gy, z1) }, Norm(Vec3(-0.5f, 1, 0)), 0.1f);
		Slab(m, { Vec3(x1 + gout, gy - drop, z0 - gout), Vec3(x1 + gout, gy - drop, z1 + gout), Vec3(x1, gy, z1), Vec3(x1, gy, z0) }, Norm(Vec3(0.5f, 1, 0)), 0.1f);
		// 회랑 기둥
		for (float x = x0 - gout + 0.3f; x <= x1 + gout - 0.2f; x += 1.5f)
			for (float z : { z0 - gout + 0.2f, z1 + gout - 0.2f })
				m.Box(Timber, Vec3(x - 0.08f, 0.0f, z - 0.08f), Vec3(x + 0.08f, gy - drop, z + 0.08f));
		(void)top;
	}

	// 앞면 (w 의 z0) 앞으로 튀어나온 다른 날개를 피한 x 범위 (문이 있는 쪽 조각)
	void FreeFrontSpan(const Ctx& c, const Wing& w, float& x0, float& x1)
	{
		for (const Wing& o : c.Wings)
		{
			if (&o == &w || o.Z0 > w.Z0 - 0.1f || o.X1 < x0 || o.X0 > x1)
				continue;
			if (c.DoorX >= o.X1)
				x0 = (std::max)(x0, o.X1 + 0.15f);
			else
				x1 = (std::min)(x1, o.X0 - 0.15f);
		}
	}

	// ================================================================ 문 지붕 (기둥 둘 + 작은 박공 지붕 + 마루 · 계단)
	void PorchFront(Ctx& c)
	{
		const BD& d = c.D;
		Mesher& m = c.M;
		const float x = c.DoorX, z = c.FrontZ - 0.12f;
		const float hw = d.DoorWidth * 0.5f + 0.75f, depth = 2.0f;
		const float deck = (std::max)(0.15f, d.Plinth);
		float H = d.Plinth + d.DoorHeight + 0.4f;
		const float half = hw + 0.3f;
		float rise = half * tanf(XMConvertToRadians(35.0f));
		const float limit = d.Floors > 1 ? d.Plinth + d.FloorHeight + d.SillHeight - 0.15f : d.WallHeight() - 0.1f;
		if (H + rise > limit)
		{
			rise = (std::max)(0.35f, limit - H);
			H = (std::min)(H, limit - rise);
		}
		const float tanP = rise / half;
		m.Box(Timber, Vec3(x - hw - 0.15f, 0.0f, z - depth - 0.15f), Vec3(x + hw + 0.15f, deck, z));
		const int steps = (std::max)(1, (int)roundf(deck / 0.18f));
		for (int s = 0; s < steps; ++s)
			m.Box(Timber, Vec3(x - d.DoorWidth * 0.5f - 0.2f, 0.0f, z - depth - 0.15f - 0.3f * (steps - s)), Vec3(x + d.DoorWidth * 0.5f + 0.2f, deck * (s + 1) / (steps + 1), z - depth - 0.15f));
		for (float sx : { -1.0f, 1.0f })
		{
			const float px = x + sx * hw;
			m.OBox(Timber, Vec3(px, (deck + H) * 0.5f, z - depth), Vec3(1, 0, 0), kUp, Vec3(0, 0, 1), 0.1f, (H - deck) * 0.5f, 0.1f);
			m.Beam(Timber, Vec3(px, H, z - depth - 0.3f), Vec3(px, H, z + 0.12f), 0.16f, 0.2f, -kUp);
			m.Beam(Timber, Vec3(px - 0.05f, H - 0.75f, z - depth + 0.05f), Vec3(px - 0.05f, H - 0.18f, z - depth + 0.6f), 0.12f, 0.1f, Vec3(1, 0, 0));
			const Vec3 nrm = Norm(Vec3(sx * tanP, 1.0f, 0.0f));
			Slab(m, { Vec3(x + sx * half, H, z - depth - 0.4f), Vec3(x + sx * half, H, z + 0.12f), Vec3(x, H + rise, z + 0.12f), Vec3(x, H + rise, z - depth - 0.4f) }, nrm, 0.12f);
		}
		m.Beam(Timber, Vec3(x - hw - 0.1f, H, z - depth), Vec3(x + hw + 0.1f, H, z - depth), 0.2f, 0.16f, -kUp);
		m.OBox(Roof, Vec3(x, H + rise + 0.04f, z - depth * 0.5f - 0.14f), Vec3(0, 0, 1), kUp, Vec3(1, 0, 0), depth * 0.5f + 0.26f, 0.06f, 0.1f);
		// 앞 박공: 판 (칠한 무늬 또는 판자) · 박공 판 · 꼭대기 장식
		const float gz = z - depth - 0.05f;
		m.Poly(d.PaintedGables ? Ornament : Door, { Vec3(x - hw, H + 0.02f, gz), Vec3(x + hw, H + 0.02f, gz), Vec3(x, H + rise * (hw / half) - 0.02f, gz) }, Vec3(0, 0, -1));
		const Vec3 apex(x, H + rise + 0.04f, z - depth - 0.46f);
		if (d.Bargeboards > 0)
			for (float sx : { -1.0f, 1.0f })
				BargeBoard(m, Vec3(x + sx * (half + 0.03f), H + 0.04f, z - depth - 0.46f), apex, Vec3(0, 0, -1), 0.12f, d.Bargeboards, d.Bargeboards >= 2);
		if (d.Finials)
			Finial(m, apex + kUp * 0.04f);
	}

	// ================================================================ 앞 테라스 (마루 · 기둥 · 까치발 · 외쪽 지붕 · 난간 · 계단)
	void TerraceFront(Ctx& c, const Wing& w)
	{
		const BD& d = c.D;
		Mesher& m = c.M;
		const float z = w.Z0 - 0.12f, depth = 2.4f;
		float x0 = w.X0 + 0.15f, x1 = w.X1 - 0.15f;
		FreeFrontSpan(c, w, x0, x1);
		if (x1 - x0 < 3.0f)
			return;
		const float deck = (std::max)(0.18f, d.Plinth);
		float H = (std::max)(d.Plinth + d.DoorHeight + 0.35f, d.Plinth + d.FloorHeight * 0.72f);
		float hiY = H + (depth + 0.4f) * 0.32f;
		const float limit = d.Floors > 1 ? d.Plinth + d.FloorHeight + (std::min)(d.SillHeight, 0.8f) - 0.1f : d.WallHeight() - 0.15f;
		if (hiY > limit)
		{
			hiY = limit;
			H = (std::min)(H, hiY - 0.25f);
		}
		m.Box(Timber, Vec3(x0, 0.0f, z - depth), Vec3(x1, deck, z));
		const int posts = (std::max)(2, (int)roundf((x1 - x0) / 2.6f));
		const float zp = z - depth + 0.14f;
		auto postX = [&](int i) { return x0 + 0.12f + (x1 - x0 - 0.24f) * i / posts; };
		for (int i = 0; i <= posts; ++i)
		{
			const float px = postX(i);
			m.OBox(Timber, Vec3(px, (deck + H) * 0.5f, zp), Vec3(1, 0, 0), kUp, Vec3(0, 0, 1), 0.1f, (H - deck) * 0.5f, 0.1f);
			for (float sx : { -1.0f, 1.0f })
				if ((i > 0 || sx > 0) && (i < posts || sx < 0))
					m.Beam(Timber, Vec3(px, H - 0.72f, zp + 0.05f), Vec3(px + sx * 0.6f, H - 0.14f, zp + 0.05f), 0.11f, 0.1f, Vec3(0, 0, -1));
		}
		m.Beam(Timber, Vec3(x0 - 0.2f, H, zp - 0.11f), Vec3(x1 + 0.2f, H, zp - 0.11f), 0.22f, 0.18f, -kUp);
		const float rl = depth + 0.52f;
		Slab(m, { Vec3(x0 - 0.3f, H, z - depth - 0.4f), Vec3(x1 + 0.3f, H, z - depth - 0.4f), Vec3(x1 + 0.3f, hiY, z + 0.12f), Vec3(x0 - 0.3f, hiY, z + 0.12f) },
			Norm(Vec3(0.0f, rl, -(hiY - H))), 0.1f);
		// 서까래 (밑에서 보이게)
		for (float x = x0 + 0.2f; x < x1 - 0.1f; x += 0.7f)
			m.Beam(Timber, Vec3(x, H - 0.1f, z - depth - 0.3f), Vec3(x, hiY - 0.1f, z + 0.1f), 0.08f, 0.12f, Norm(Vec3(0.0f, -rl, hiY - H)));
		// 난간 (문 앞 칸은 비우고 계단)
		const float ry = deck + 0.9f;
		for (int i = 0; i < posts; ++i)
		{
			const float xa = postX(i) + 0.1f, xb = postX(i + 1) - 0.1f;
			if (c.DoorX > xa - 0.4f && c.DoorX < xb + 0.4f)
			{
				const float sw = (std::min)(xb - xa, d.DoorWidth + 0.8f);
				const float lo = xa + sw * 0.5f, hi = xb - sw * 0.5f;   // 칸 = 계단 폭이면 lo ≈ hi (부동소수 오차로 거꾸로일 수 있다)
				const float sx = hi > lo ? std::clamp(c.DoorX, lo, hi) : (xa + xb) * 0.5f;
				const int steps = (std::max)(1, (int)roundf(deck / 0.17f));
				for (int s = 0; s < steps; ++s)
					m.Box(Timber, Vec3(sx - sw * 0.5f, 0.0f, z - depth - 0.3f * (steps - s)), Vec3(sx + sw * 0.5f, deck * (s + 1) / (steps + 1), z - depth));
				continue;
			}
			m.Box(Timber, Vec3(xa, ry - 0.08f, zp - 0.05f), Vec3(xb, ry, zp + 0.05f));
			m.Box(Timber, Vec3(xa, deck + 0.1f, zp - 0.04f), Vec3(xb, deck + 0.17f, zp + 0.04f));
			for (float bx = xa + 0.1f; bx < xb - 0.05f; bx += 0.16f)
				m.Box(Timber, Vec3(bx - 0.025f, deck + 0.17f, zp - 0.025f), Vec3(bx + 0.025f, ry - 0.08f, zp + 0.025f));
		}
		c.TerraceH = H; c.TerraceZ = zp; c.TerraceDeck = deck; c.TerraceX0 = x0; c.TerraceX1 = x1;
	}

	// ================================================================ 문 옆 등 (쇠 팔 + 사슬 + 유리 등)
	void DoorLanterns(Ctx& c)
	{
		const BD& d = c.D;
		Mesher& m = c.M;
		const float y = d.Plinth + d.DoorHeight + 0.2f;
		for (float sx : { -1.0f, 1.0f })
		{
			const float x = c.DoorX + sx * (d.DoorWidth * 0.5f + 0.6f);
			const float z = c.FrontWallZ(x);
			const Vec3 wall(x, y, z), tip(x, y, z - 0.45f);
			m.Beam(Metal, wall - kUp * 0.02f, tip - kUp * 0.02f, 0.04f, 0.04f, kUp);
			m.Beam(Metal, Vec3(x, y - 0.32f, z), Vec3(x, y - 0.02f, z - 0.3f), 0.025f, 0.025f, Vec3(1, 0, 0));
			m.OBox(Metal, wall + Vec3(0.0f, -0.12f, 0.02f), Vec3(1, 0, 0), kUp, Vec3(0, 0, 1), 0.06f, 0.18f, 0.02f);
			const Vec3 lc = tip - kUp * 0.36f;
			m.Beam(Metal, tip, lc + kUp * 0.24f, 0.015f, 0.015f, Vec3(1, 0, 0));
			m.Frustum(Metal, lc + kUp * 0.12f, 0.13f, 0.02f, 0.13f, 4, false);
			m.OBox(Glass, lc, Vec3(1, 0, 0), kUp, Vec3(0, 0, 1), 0.075f, 0.115f, 0.075f);
			for (int k = 0; k < 4; ++k)
			{
				const float ox = (k & 1) ? 0.085f : -0.085f, oz = (k & 2) ? 0.085f : -0.085f;
				m.OBox(Metal, lc + Vec3(ox, 0.0f, oz), Vec3(1, 0, 0), kUp, Vec3(0, 0, 1), 0.012f, 0.125f, 0.012f);
			}
			m.Box(Metal, lc + Vec3(-0.1f, -0.15f, -0.1f), lc + Vec3(0.1f, -0.115f, 0.1f));
		}
	}

	// ================================================================ 내건 간판 (쇠 팔 + 사슬 + 칠한 판) — 테라스가 있으면 들보에
	void HangingSign(Ctx& c, float side)
	{
		const BD& d = c.D;
		Mesher& m = c.M;
		const float x = c.DoorX + side * (d.DoorWidth * 0.5f + 1.3f);
		if (c.TerraceH > 0.0f && x > c.TerraceX0 + 0.3f && x < c.TerraceX1 - 0.3f)
		{
			const float y = c.TerraceH - 0.18f, z = c.TerraceZ - 0.12f;
			const Vec3 bc(x, y - 0.42f, z);
			for (float o : { -0.24f, 0.24f })
				m.Beam(Metal, Vec3(x + o, y, z), Vec3(x + o, y - 0.18f, z), 0.015f, 0.015f, Vec3(0, 0, 1));
			m.OBox(Ornament, bc, Vec3(1, 0, 0), kUp, Vec3(0, 0, -1), 0.36f, 0.24f, 0.025f);
			return;
		}
		const float y = d.Plinth + d.DoorHeight + 0.6f, z = c.FrontWallZ(x);
		m.Beam(Metal, Vec3(x, y, z), Vec3(x, y, z - 1.05f), 0.05f, 0.05f, kUp);
		m.Beam(Metal, Vec3(x, y - 0.5f, z), Vec3(x, y, z - 0.7f), 0.03f, 0.03f, Vec3(1, 0, 0));
		m.Blob(Metal, Vec3(x, y + 0.025f, z - 1.08f), 0.045f, 6);
		for (float o : { -0.38f, -0.86f })
			m.Beam(Metal, Vec3(x, y, z + o), Vec3(x, y - 0.18f, z + o), 0.015f, 0.015f, Vec3(1, 0, 0));
		m.OBox(Ornament, Vec3(x, y - 0.42f, z - 0.62f), Vec3(0, 0, 1), kUp, Vec3(1, 0, 0), 0.34f, 0.24f, 0.025f);
	}

	// ================================================================ 깃발 (창 사이, 위층 — 쇠 막대 + 제비꼬리 천)
	void FrontBanners(Ctx& c, const Wing& w)
	{
		const BD& d = c.D;
		Mesher& m = c.M;
		const bool upper = d.Floors >= 2;
		const int fl = upper ? 1 : 0;
		const float out = d.Jetty * fl;
		const float x0 = w.X0 - out, len = w.W() + 2.0f * out, z = w.Z0 - out;
		const float yTop = d.Plinth + d.FloorHeight * (fl + 1) - 0.22f;
		const float L = upper ? (std::min)(2.6f, d.FloorHeight * 0.85f) : (std::min)(1.5f, d.FloorHeight * 0.5f);
		const float spacing = (std::max)(d.WindowSpacing, d.WindowWidth + 0.4f);
		const int count = (std::max)(1, (int)floorf((len - 0.6f) / spacing));
		const float slotW = len / count;
		std::vector<float> spots;
		for (int i = 0; i + 1 < count; ++i)
			spots.push_back(len * 0.5f + (i + 0.5f - (count - 1) * 0.5f) * slotW);
		spots.push_back(0.1f * len + 0.35f);
		spots.push_back(0.9f * len - 0.35f);
		std::stable_sort(spots.begin(), spots.end(), [&](float a, float b) { return fabsf(a - len * 0.5f) < fabsf(b - len * 0.5f); });
		const float bw = std::clamp(slotW - d.WindowWidth - 0.35f, 0.45f, 0.95f);
		int made = 0;
		std::vector<float> used;
		float fx0 = x0, fx1 = x0 + len;
		FreeFrontSpan(c, w, fx0, fx1);
		for (float sp : spots)
		{
			if (made >= d.Banners)
				break;
			const float X = x0 + sp;
			if (X - bw * 0.5f < fx0 || X + bw * 0.5f > fx1)
				continue;
			if (!upper && fabsf(X - c.DoorX) < d.DoorWidth * 0.5f + bw * 0.5f + 0.7f)
				continue;
			bool clash = false;
			for (float u : used)
				clash |= fabsf(u - X) < bw + 0.3f;
			if (clash)
				continue;
			used.push_back(X);
			++made;
			const float zb = z - 0.09f;
			const float l = X - bw * 0.5f, r = X + bw * 0.5f, b = yTop - L * (0.92f + 0.12f * c.U01());
			const float notch = b + 0.3f * bw;
			const std::vector<Vec3> pts = { Vec3(l, yTop, zb), Vec3(r, yTop, zb), Vec3(r, b, zb), Vec3(X, notch, zb), Vec3(l, b, zb) };
			std::vector<XMFLOAT2> uv;
			for (const Vec3& p : pts)
				uv.push_back(XMFLOAT2((p.x - l) / bw, (yTop - p.y) / (yTop - b)));
			m.PolyUV(Banner, pts, uv, Vec3(0, 0, -1), Vec3(1, 0, 0));
			m.PolyUV(Banner, pts, uv, Vec3(0, 0, 1), Vec3(1, 0, 0));
			m.Cyl(Metal, Vec3(l - 0.09f, yTop + 0.03f, zb), Vec3(r + 0.09f, yTop + 0.03f, zb), 0.025f, 6);
			m.Blob(Metal, Vec3(l - 0.12f, yTop + 0.03f, zb), 0.04f, 6);
			m.Blob(Metal, Vec3(r + 0.12f, yTop + 0.03f, zb), 0.04f, 6);
			for (float bx : { l + 0.05f, r - 0.05f })
				m.Beam(Metal, Vec3(bx, yTop + 0.03f, z + 0.01f), Vec3(bx, yTop + 0.03f, zb), 0.03f, 0.03f, kUp);
		}
	}

	// ================================================================ 소품: 통 · 상자 · 장작
	void Barrel(Mesher& m, const Vec3& base, float r, float h)
	{
		m.Revolve(Door, base, 12, 6, [&](float t) { return XMFLOAT2(r * (0.86f + 0.14f * sinf(t * XM_PI)), h * t); });
		std::vector<Vec3> lid;
		for (int i = 0; i < 12; ++i)
		{
			const float a = XM_2PI * i / 12;
			lid.push_back(base + Vec3(cosf(a) * r * 0.86f, h, sinf(a) * r * 0.86f));
		}
		m.Fan(Door, lid, kUp);
		for (float t : { 0.12f, 0.5f, 0.88f })
		{
			const float rr = r * (0.86f + 0.14f * sinf(t * XM_PI)) + 0.01f;
			m.Frustum(Metal, base + kUp * (h * t - 0.03f), rr, rr, 0.06f, 12, false);
		}
	}

	void Crate(Mesher& m, const Vec3& c, float s, float yaw)
	{
		const Vec3 ax(cosf(yaw), 0, sinf(yaw)), az(-sinf(yaw), 0, cosf(yaw));
		m.OBox(Door, c + kUp * s, ax, kUp, az, s, s, s);
		for (int i = 0; i < 4; ++i)
		{
			const float sx = (i & 1) ? 1.0f : -1.0f, sz = (i & 2) ? 1.0f : -1.0f;
			m.OBox(Timber, c + kUp * s + ax * (sx * (s - 0.035f)) + az * (sz * (s - 0.035f)), ax, kUp, az, 0.045f, s + 0.006f, 0.045f);
		}
	}

	void Firewood(Ctx& c, float xw, float side, float zc)
	{
		Mesher& m = c.M;
		for (float y = 0.09f; y < 1.2f; y += 0.165f)
			for (float z = zc - 0.92f; z < zc + 0.95f; z += 0.17f)
			{
				const float r = 0.07f + 0.02f * c.U01();
				const float j = (c.U01() - 0.5f) * 0.08f;
				m.Cyl(Timber, Vec3(xw + side * (0.05f + j), y, z + (c.U01() - 0.5f) * 0.03f), Vec3(xw + side * (0.55f + j), y, z), r, 6);
			}
		Slab(m, { Vec3(xw, 1.75f, zc - 1.2f), Vec3(xw, 1.75f, zc + 1.2f), Vec3(xw + side * 0.85f, 1.5f, zc + 1.2f), Vec3(xw + side * 0.85f, 1.5f, zc - 1.2f) },
			Norm(Vec3(side * (0.25f / 0.85f), 1.0f, 0.0f)), 0.08f);
	}

	void PlaceProps(Ctx& c, const Wing& w, int annexSide)
	{
		const BD& d = c.D;
		Mesher& m = c.M;
		auto groundY = [&](float x, float z) {
			return c.TerraceH > 0.0f && x > c.TerraceX0 && x < c.TerraceX1 && z > c.TerraceZ - 0.2f ? c.TerraceDeck : 0.0f;
		};
		// 문 왼쪽: 통 둘
		{
			const float x = c.DoorX - d.DoorWidth * 0.5f - 1.15f, z = c.FrontWallZ(x) - 0.55f;
			float bx0 = w.X0, bx1 = w.X1;
			FreeFrontSpan(c, w, bx0, bx1);
			if (x > bx0 + 0.4f || c.RoundR > 0.0f)
			{
				Barrel(m, Vec3(x, groundY(x, z), z), 0.3f, 0.85f);
				if (d.Props >= 2)
					Barrel(m, Vec3(x - 0.68f, groundY(x - 0.68f, z), z + 0.05f), 0.26f, 0.72f);
			}
		}
		// 장작 (헛간이 없는 옆벽)
		if (c.RoundR <= 0.0f && d.Towers == 0)
		{
			const float side = annexSide != 0 ? -(float)annexSide : (c.U01() < 0.5f ? 1.0f : -1.0f);
			const float xw = side > 0 ? w.X1 + 0.12f : w.X0 - 0.12f;
			if (w.D() > 3.2f)
				Firewood(c, xw, side, (w.Z0 + w.Z1) * 0.5f + (c.U01() - 0.5f) * (w.D() - 3.0f));
		}
		if (d.Props < 2)
			return;
		// 앞 모서리: 상자 더미 (문에서 먼 쪽)
		float fx0 = w.X0, fx1 = w.X1;
		FreeFrontSpan(c, w, fx0, fx1);
		float x = fx1 - 0.9f;
		if (fabsf(x - c.DoorX) < d.DoorWidth * 0.5f + 1.6f)
			x = fx0 + 2.2f;
		const float z = c.FrontZ - 0.75f, y = groundY(x, z);
		Crate(m, Vec3(x, y, z), 0.32f, 0.1f);
		Crate(m, Vec3(x - 0.72f, y, z + 0.08f), 0.27f, -0.25f);
		Crate(m, Vec3(x - 0.1f, y + 0.64f, z + 0.02f), 0.24f, 0.6f);
	}

	// ================================================================ 헛간 (옆에 붙인 낮은 외쪽 지붕 집) — 생성기를 한 번 더 돌려 돌려 붙인다
	struct AnnexPlan { int Side = 0; float Width = 0.0f, Depth = 0.0f, Zc = 0.0f; };

	AnnexPlan PlanAnnex(Ctx& c)
	{
		const BD& d = c.D;
		AnnexPlan p;
		if (!d.Annex || d.Shape != BD::Plan::Rectangle || d.Towers > 0 || c.Wings.empty())
			return p;
		const Wing& w = c.Wings[0];
		p.Side = c.U01() < 0.5f ? 1 : -1;
		p.Width = std::clamp(w.D() * 0.62f, 3.0f, 7.0f);
		p.Depth = std::clamp(w.W() * 0.3f, 2.6f, 3.8f);
		const float spare = (std::max)(0.0f, w.D() - p.Width);
		p.Zc = (w.Z0 + w.Z1) * 0.5f + (c.U01() - 0.5f) * spare;
		Wing b;
		b.X0 = p.Side > 0 ? w.X1 : w.X0 - p.Depth;
		b.X1 = p.Side > 0 ? w.X1 + p.Depth : w.X0;
		b.Z0 = p.Zc - p.Width * 0.5f;
		b.Z1 = p.Zc + p.Width * 0.5f;
		b.AlongX = false;
		b.Main = false;
		c.Blockers.push_back(b);
		return p;
	}

	void AddAnnex(Ctx& c, const AnnexPlan& p)
	{
		const BD& d = c.D;
		const Wing& w = c.Wings[0];
		BD a = d;
		a.Shape = BD::Plan::Rectangle;
		a.Floors = 1;
		a.Width = p.Width;
		a.Depth = p.Depth;
		a.Plinth = (std::min)(d.Plinth, 0.3f);
		a.FloorHeight = (std::min)(d.FloorHeight * 0.85f, 2.7f);
		const float rise = a.Depth * tanf(XMConvertToRadians(16.0f));
		if (!w.AlongX)   // 본채의 처마 쪽에 붙는다 — 처마 밑으로
			a.FloorHeight = (std::min)(a.FloorHeight, d.WallHeight() - a.Plinth - rise - 0.45f);
		if (a.FloorHeight < 2.0f)
			return;
		a.Jetty = 0.0f; a.Towers = 0; a.Taper = 0.0f;
		const bool plaster = d.UpperWall == BD::WallMat::Plaster || d.UpperWall == BD::WallMat::Stucco;
		a.GroundWall = a.UpperWall = (plaster || d.UpperWall == BD::WallMat::Ashlar || d.UpperWall == BD::WallMat::Marble || d.UpperWall == BD::WallMat::Brick) ? BD::WallMat::Planks : d.UpperWall;
		const float* wc = d.UpperWall == a.UpperWall ? d.UpperColor : d.TimberColor;
		for (int i = 0; i < 3; ++i)
			a.WallColor[i] = a.UpperColor[i] = (std::min)(1.0f, wc[i] * (d.UpperWall == a.UpperWall ? 1.0f : 1.6f));
		a.Frame = BD::Timber::None;
		a.RoofStyle = BD::Roof::Shed; a.RoofPitch = 16.0f; a.Overhang = 0.3f;
		a.Dormers = 0; a.Chimneys = 0;
		a.Portico = a.Peristyle = a.Arcade = a.Balcony = a.Crenellations = a.CentralDome = a.Sails = false;
		a.SpireHeight = 0.0f;
		a.Quoins = a.StringCourse = a.Cornice = a.Pilasters = a.Buttresses = a.CrowSteps = a.DragonHeads = false;
		a.Brackets = false; a.Bargeboards = 0; a.Finials = a.PaintedGables = false;
		a.Porch = a.Terrace = a.Lanterns = a.ShopSign = a.FlowerBoxes = a.Annex = false;
		a.Banners = 0; a.Props = 0; a.Irregularity = 0.0f; a.CornerPosts = true;
		a.WindowStyle = a.GroundWindowStyle = BD::Window::Rectangular;
		a.WindowWidth = 0.6f; a.WindowHeight = 0.6f; a.WindowSpacing = 3.0f; a.SillHeight = 1.2f;
		a.DoorStyle = BD::Door::Plank; a.DoorWidth = 1.0f; a.DoorHeight = (std::min)(2.0f, a.FloorHeight - 0.3f);
		a.Seed = d.Seed * 31 + 7;
		Output tmp;
		BuildingGenerator::Generate(a, tmp);
		// 외쪽 지붕의 높은 쪽 (+Z) 이 본채 벽에: +X 쪽이면 -90° (x' = -z, z' = x), -X 쪽이면 +90°
		const float sn = p.Side > 0 ? -1.0f : 1.0f;
		const float xc = p.Side > 0 ? w.X1 + p.Depth * 0.5f : w.X0 - p.Depth * 0.5f;
		auto rot = [&](float x, float z, float& ox, float& oz) { ox = z * sn; oz = -x * sn; };
		Output& out = c.M.O;
		for (int slot = 0; slot < SlotCount; ++slot)
		{
			Part& dst = out.Parts[slot];
			const Part& src = tmp.Parts[slot];
			const uint32_t base = (uint32_t)dst.V.size();
			for (Vertex::PosNormalTexTan2 v : src.V)
			{
				float x, z;
				rot(v.pos.x, v.pos.z, x, z);
				v.pos.x = x + xc; v.pos.z = z + p.Zc;
				rot(v.normal.x, v.normal.z, x, z);
				v.normal.x = x; v.normal.z = z;
				rot(v.tangentU.x, v.tangentU.z, x, z);
				v.tangentU.x = x; v.tangentU.z = z;
				dst.V.push_back(v);
			}
			for (uint32_t i : src.I)
				dst.I.push_back(base + i);
		}
		out.Windows += tmp.Windows;
		out.Doors += tmp.Doors;
		out.Wings += 1;
	}

	// ================================================================ 손으로 지은 불규칙: 위층 기울기 · 용마루 처짐 · 낮은 주파수 흔들림 (모든 정점을 같은 식으로 — 틈이 생기지 않는다)
	void Deform(Output& out, const BD& d)
	{
		const float irr = std::clamp(d.Irregularity, 0.0f, 1.0f);
		if (irr < 0.01f)
			return;
		std::mt19937 rng((uint32_t)d.Seed * 7919u + 3u);
		std::uniform_real_distribution<float> U(0.0f, 1.0f);
		XMFLOAT3 mn(1e9f, 1e9f, 1e9f), mx(-1e9f, -1e9f, -1e9f);
		for (const Part& p : out.Parts)
			for (const auto& v : p.V)
			{
				mn.x = (std::min)(mn.x, v.pos.x); mn.y = (std::min)(mn.y, v.pos.y); mn.z = (std::min)(mn.z, v.pos.z);
				mx.x = (std::max)(mx.x, v.pos.x); mx.y = (std::max)(mx.y, v.pos.y); mx.z = (std::max)(mx.z, v.pos.z);
			}
		if (mn.x > mx.x)
			return;
		const float top = d.WallHeight();
		const float H = (std::max)(1.0f, mx.y);
		const float leanA = U(rng) * XM_2PI;
		const float lean = irr * (0.008f + 0.014f * U(rng));
		const float y0 = d.Floors > 1 ? d.Plinth + d.FloorHeight : d.Plinth + d.FloorHeight * 0.5f;
		const float sag = irr * (0.1f + 0.12f * U(rng));
		const float cx = (mn.x + mx.x) * 0.5f, cz = (mn.z + mx.z) * 0.5f;
		const float hx = (std::max)(1.0f, (mx.x - mn.x) * 0.5f), hz = (std::max)(1.0f, (mx.z - mn.z) * 0.5f);
		float ph[4];
		for (float& p : ph)
			p = U(rng) * XM_2PI;
		const float wob = irr * 0.04f;
		const bool round = d.Shape == BD::Plan::Round;
		const float bend = round ? irr * (0.35f + 0.4f * U(rng)) : 0.0f;
		for (Part& p : out.Parts)
			for (auto& v : p.V)
			{
				const float x = v.pos.x, y = v.pos.y, z = v.pos.z;
				const float hy = (std::max)(0.0f, y - y0);
				const float grow = (std::min)(1.0f, (std::max)(0.0f, y) / 2.0f);
				float dx = cosf(leanA) * lean * hy, dz = sinf(leanA) * lean * hy, dy = 0.0f;
				if (round)
				{
					const float t = std::clamp(y / H, 0.0f, 1.0f);
					dx += cosf(leanA + 0.6f) * bend * t * t;
					dz += sinf(leanA + 0.6f) * bend * t * t;
				}
				if (y > top)
				{
					const float s = std::clamp((y - top) / (std::max)(0.5f, H - top), 0.0f, 1.0f);
					const float ux = (x - cx) / hx, uz = (z - cz) / hz;
					dy -= sag * s * (std::max)(0.0f, 1.0f - ux * ux) * (std::max)(0.0f, 1.0f - uz * uz);
				}
				dx += wob * sinf(y * 0.9f + z * 0.45f + ph[0]) * grow;
				dz += wob * sinf(y * 0.8f + x * 0.5f + ph[1]) * grow;
				dy += wob * 0.6f * sinf(x * 0.6f + ph[2]) * sinf(z * 0.55f + ph[3]) * grow;
				v.pos.x += dx; v.pos.y += dy; v.pos.z += dz;
			}
	}

	const Wing* FrontWing(const Ctx& c)
	{
		for (const Wing& w : c.Wings)
			if (fabsf(w.Z0 - c.FrontZ) < 0.1f && c.DoorX > w.X0 && c.DoorX < w.X1)
				return &w;
		return c.Wings.empty() ? nullptr : &c.Wings[0];
	}

	void Bounds(Output& o)
	{
		XMFLOAT3 mn(1e9f, 1e9f, 1e9f), mx(-1e9f, -1e9f, -1e9f);
		for (const Part& p : o.Parts)
			for (const auto& v : p.V)
			{
				mn.x = (std::min)(mn.x, v.pos.x); mn.y = (std::min)(mn.y, v.pos.y); mn.z = (std::min)(mn.z, v.pos.z);
				mx.x = (std::max)(mx.x, v.pos.x); mx.y = (std::max)(mx.y, v.pos.y); mx.z = (std::max)(mx.z, v.pos.z);
			}
		if (mn.x > mx.x)
			mn = mx = XMFLOAT3(0, 0, 0);
		o.Min = mn;
		o.Max = mx;
	}
}

namespace BuildingGenerator
{
	const char* SlotKey(int slot)
	{
		static const char* const keys[] = { "wallLower", "wallUpper", "trim", "timber", "roof", "glass", "frame", "shutter", "door", "metal", "fabric", "ornament", "banner", "plants" };
		static_assert(sizeof(keys) / sizeof(keys[0]) == SlotCount, "keys must cover every slot");
		return slot >= 0 && slot < SlotCount ? keys[slot] : "";
	}

	int SlotFromKey(const std::string& key)
	{
		for (int s = 0; s < SlotCount; ++s)
			if (_stricmp(SlotKey(s), key.c_str()) == 0)
				return s;
		return -1;
	}

	const char* SlotName(int slot)
	{
		static const char* const names[] = { "Lower Walls", "Upper Walls", "Stone Trim", "Timber", "Roof", "Glass", "Frames", "Shutters", "Door", "Metal", "Fabric",
			"Ornament", "Banner", "Plants" };
		return slot >= 0 && slot < SlotCount ? names[slot] : "";
	}

	void Generate(const BuildingDesc& src, Output& out)
	{
		out = Output();
		BuildingDesc d = src;
		d.Floors = std::clamp(d.Floors, 1, 12);
		d.FloorHeight = std::clamp(d.FloorHeight, 2.0f, 12.0f);
		d.Width = std::clamp(d.Width, 2.0f, 120.0f);
		d.Depth = std::clamp(d.Depth, 2.0f, 120.0f);
		d.Plinth = std::clamp(d.Plinth, 0.0f, 4.0f);
		d.Jetty = std::clamp(d.Jetty, 0.0f, 1.0f);
		d.Dormers = std::clamp(d.Dormers, 0, 16);
		d.Chimneys = std::clamp(d.Chimneys, 0, 8);
		d.WindowWidth = std::clamp(d.WindowWidth, 0.2f, 4.0f);
		d.WindowHeight = std::clamp(d.WindowHeight, 0.3f, 10.0f);
		d.DoorWidth = std::clamp(d.DoorWidth, 0.6f, 6.0f);
		d.DoorHeight = std::clamp(d.DoorHeight, 1.6f, 8.0f);
		d.Taper = std::clamp(d.Taper, 0.0f, 0.6f);
		d.Banners = std::clamp(d.Banners, 0, 8);
		d.Props = std::clamp(d.Props, 0, 2);
		d.Irregularity = std::clamp(d.Irregularity, 0.0f, 1.0f);
		Mesher m(out);
		Ctx c(d, m);
		const float top = d.WallHeight();

		// ---- 둥근 몸채 (탑 · 풍차)
		if (d.Shape == BD::Plan::Round)
		{
			const float r = d.Width * 0.5f;
			c.FrontZ = -r;
			const bool crenel = d.Crenellations || d.RoofStyle == BD::Roof::Flat;
			BD::Roof roof = d.RoofStyle;
			if (roof != BD::Roof::Dome && roof != BD::Roof::Onion && roof != BD::Roof::Flat && roof != BD::Roof::Pyramid)
				roof = BD::Roof::Cone;
			TowerBody(c, Vec3(0, 0, 0), r, top, 16, d.Taper, roof, true, crenel, d.SpireHeight, true);
			if (d.Sails)
			{
				const float rt = r * (1.0f - d.Taper);
				Sails(c, Vec3(0, top + 0.6f, -rt - 0.3f));
			}
			if (d.Balcony && d.Floors >= 2)
			{
				const float y = d.Plinth + d.FloorHeight * (d.Floors - 1);
				const float rr = r * (1.0f - d.Taper * (y / top));
				m.Frustum(Timber, Vec3(0, y - 0.12f, 0), rr + 1.2f, rr + 1.2f, 0.12f, 24, true, true);
			}
			c.RoundR = r;
			if (d.Lanterns)
				DoorLanterns(c);
			if (d.Props > 0)
			{
				Barrel(m, Vec3(-d.DoorWidth * 0.5f - 1.2f, 0.0f, c.FrontWallZ(-d.DoorWidth * 0.5f - 1.2f) - 0.5f), 0.3f, 0.85f);
				if (d.Props >= 2)
					Crate(m, Vec3(d.DoorWidth * 0.5f + 1.3f, 0.0f, c.FrontWallZ(d.DoorWidth * 0.5f + 1.3f) - 0.7f), 0.3f, 0.3f);
			}
			Deform(out, d);
			Bounds(out);
			return;
		}

		MakeWings(c);
		const AnnexPlan annex = PlanAnnex(c);
		// 탑 자리 (모서리)
		if (d.Towers > 0)
		{
			const Wing& w0 = c.Wings[0];
			float x0 = w0.X0, x1 = w0.X1, z0 = w0.Z0, z1 = w0.Z1;
			for (const Wing& w : c.Wings) { x0 = (std::min)(x0, w.X0); x1 = (std::max)(x1, w.X1); z0 = (std::min)(z0, w.Z0); z1 = (std::max)(z1, w.Z1); }
			std::vector<Vec3> corners = { { x0, 0, z0 }, { x1, 0, z0 }, { x0, 0, z1 }, { x1, 0, z1 } };
			const int n = d.Towers >= 4 ? 4 : (d.Towers >= 2 ? 2 : 1);
			for (int i = 0; i < n; ++i)
				c.TowerCenters.push_back(corners[(size_t)i]);
			c.TowerR = d.TowerRadius;
		}
		// ---- 날개마다 벽 · 지붕
		const bool timberGable = IsPlaster(d.UpperWall) && d.Frame != BD::Timber::None;
		for (const Wing& w : c.Wings)
		{
			BuildWingWalls(c, w);
			const float outTop = d.Jetty * (d.Floors - 1);
			Wing roofWing = w;
			roofWing.X0 -= outTop; roofWing.X1 += outTop; roofWing.Z0 -= outTop; roofWing.Z1 += outTop;
			if (d.Peristyle)
			{
				const float gap = 2.4f;
				roofWing.X0 -= gap; roofWing.X1 += gap; roofWing.Z0 -= gap; roofWing.Z1 += gap;
				PeristyleColumns(c, w, gap);
			}
			const float baseY = top + (d.Cornice ? 0.02f : 0.0f) + (d.Peristyle ? 0.9f : 0.0f);
			RoofFrame rf = FrameOf(roofWing, baseY, 0.0f);
			const float tanP = tanf(XMConvertToRadians(std::clamp(d.RoofPitch, 5.0f, 80.0f)));
			float rise = rf.B * tanP;
			const int gableSlot = d.Peristyle ? Trim : WallUpper;
			switch (d.RoofStyle)
			{
			case BD::Roof::Hip:
				HipRoof(c, rf, baseY, d.RoofPitch, d.Overhang);
				break;
			case BD::Roof::Pyramid:
			{
				RoofFrame sq = rf;
				sq.A = sq.B = (std::max)(rf.A, rf.B);
				HipRoof(c, rf, baseY, d.RoofPitch, d.Overhang);
				break;
			}
			case BD::Roof::Mansard:
			{
				// 아래 가파른 단 (72°) 에 지붕층 하나 + 위 완만한 모임 지붕
				const float h1 = (std::min)(d.FloorHeight * 0.95f, rf.B * 0.8f);
				const float t1 = tanf(XMConvertToRadians(74.0f));
				const float inset = h1 / t1;
				const float o = d.Overhang;
				auto P = [&](float a, float b, float y) { return rf.Center + rf.Ax * a + rf.Bx * b + kUp * y; };
				const float A = rf.A + o, B = rf.B + o;
				const Vec3 c00 = P(-A, -B, -o * t1 * 0.2f), c10 = P(A, -B, -o * t1 * 0.2f), c11 = P(A, B, -o * t1 * 0.2f), c01 = P(-A, B, -o * t1 * 0.2f);
				const float Ai = rf.A - inset, Bi = rf.B - inset;
				const Vec3 u00 = P(-Ai, -Bi, h1), u10 = P(Ai, -Bi, h1), u11 = P(Ai, Bi, h1), u01 = P(-Ai, Bi, h1);
				const float th = RoofThickness(d);
				Slab(m, { c00, c10, u10, u00 }, Norm(rf.Bx * -t1 + kUp), th);
				Slab(m, { c11, c01, u01, u11 }, Norm(rf.Bx * t1 + kUp), th);
				Slab(m, { c01, c00, u00, u01 }, Norm(rf.Ax * -t1 + kUp), th);
				Slab(m, { c10, c11, u11, u10 }, Norm(rf.Ax * t1 + kUp), th);
				RoofFrame up = rf;
				up.A = Ai; up.B = Bi;
				HipRoof(c, up, baseY + h1, d.RoofPitch, 0.15f);
				rise = h1 + Bi * tanP;
				// 지붕창 (가파른 단)
				if (w.Main)
				{
					const int n = d.Dormers;
					for (int i = 0; i < n && rf.Ax.x > 0.5f; ++i)
					{
						const float t = n == 1 ? 0.0f : -0.7f + 1.4f * i / (n - 1);
						const float cx = rf.Center.x + rf.A * t;
						const float fz = roofWing.Z0 + inset * 0.5f - 0.05f;
						Band b;
						b.P0 = Vec3(cx - 0.8f, 0, fz); b.Dir = Vec3(1, 0, 0); b.N = VCross(kUp, b.Dir); b.Len = 1.6f; b.Y0 = baseY + 0.1f; b.Y1 = baseY + h1 * 0.85f; b.U0 = 0.0f;
						Opening o2;
						o2.A = 0.3f; o2.B = 1.3f; o2.S = b.Y0 + 0.25f; o2.H = (b.Y1 - b.Y0) - 0.45f; o2.Shape = BD::Window::Arched; o2.Depth = 0.12f;
						BuildBand(c, Trim, b, { o2 }, false);
						m.Quad(Trim, Vec3(cx - 0.8f, b.Y0, fz), Vec3(cx - 0.8f, b.Y1, fz), Vec3(cx - 0.8f, b.Y1, fz + 1.2f), Vec3(cx - 0.8f, b.Y0, fz + 1.2f), Vec3(-1, 0, 0));
						m.Quad(Trim, Vec3(cx + 0.8f, b.Y0, fz), Vec3(cx + 0.8f, b.Y1, fz), Vec3(cx + 0.8f, b.Y1, fz + 1.2f), Vec3(cx + 0.8f, b.Y0, fz + 1.2f), Vec3(1, 0, 0));
						m.Box(Trim, Vec3(cx - 0.95f, b.Y1, fz - 0.1f), Vec3(cx + 0.95f, b.Y1 + 0.18f, fz + 1.2f));
					}
				}
				break;
			}
			case BD::Roof::Gambrel:
				GableRoof(c, roofWing, true, gableSlot, false, baseY, d.RoofPitch, d.Overhang * 0.6f, d.Overhang);
				break;
			case BD::Roof::Flat:
			{
				m.Box(Roof, Vec3(roofWing.X0 - 0.05f, baseY - 0.1f, roofWing.Z0 - 0.05f), Vec3(roofWing.X1 + 0.05f, baseY + 0.05f, roofWing.Z1 + 0.05f));
				rise = 0.0f;
				if (d.Crenellations)
				{
					const float t = 0.55f;
					// 돌출 받침 (총안 아래) + 흉벽 + 총안
					for (const Side& s : SidesOf(roofWing, 0.25f))
					{
						const Vec3 n = VCross(kUp, s.Dir);
						m.OBox(Trim, s.P0 + s.Dir * (s.Len * 0.5f) + kUp * (baseY - 0.25f) - n * 0.15f, s.Dir, kUp, n, s.Len * 0.5f + 0.3f, 0.25f, 0.4f);
						m.OBox(WallUpper, s.P0 + s.Dir * (s.Len * 0.5f) + kUp * (baseY + 0.4f) - n * (t * 0.5f - 0.1f), s.Dir, kUp, n, s.Len * 0.5f, 0.4f, t * 0.5f);
						const int count = (std::max)(2, (int)(s.Len / 1.5f));
						for (int i = 0; i < count; ++i)
						{
							const float x = s.Len * (i + 0.5f) / count;
							m.OBox(WallUpper, s.P0 + s.Dir * x + kUp * (baseY + 0.8f + 0.45f) - n * (t * 0.5f - 0.1f), s.Dir, kUp, n, s.Len / count * 0.3f, 0.45f, t * 0.5f);
						}
					}
				}
				else
					m.Box(Trim, Vec3(roofWing.X0 - 0.15f, baseY - 0.05f, roofWing.Z0 - 0.15f), Vec3(roofWing.X1 + 0.15f, baseY + 0.25f, roofWing.Z1 + 0.15f));
				break;
			}
			case BD::Roof::Shed:
			{
				// 앞 (-Z) 이 낮고 뒤가 높은 한쪽 비탈
				const float dz = roofWing.D(), hgt = dz * tanP;
				const float o = d.Overhang;
				const Vec3 a0(roofWing.X0 - o, baseY - o * tanP, roofWing.Z0 - o), a1(roofWing.X1 + o, baseY - o * tanP, roofWing.Z0 - o);
				const Vec3 b1(roofWing.X1 + o, baseY + hgt + o * tanP, roofWing.Z1 + o), b0(roofWing.X0 - o, baseY + hgt + o * tanP, roofWing.Z1 + o);
				Slab(m, { a0, a1, b1, b0 }, Norm(Vec3(0, 1, -tanP)), RoofThickness(d));
				m.Poly(WallUpper, { Vec3(roofWing.X0, baseY, roofWing.Z0), Vec3(roofWing.X0, baseY, roofWing.Z1), Vec3(roofWing.X0, baseY + hgt, roofWing.Z1) }, Vec3(-1, 0, 0));
				m.Poly(WallUpper, { Vec3(roofWing.X1, baseY, roofWing.Z0), Vec3(roofWing.X1, baseY, roofWing.Z1), Vec3(roofWing.X1, baseY + hgt, roofWing.Z1) }, Vec3(1, 0, 0));
				m.Quad(WallUpper, Vec3(roofWing.X0, baseY, roofWing.Z1), Vec3(roofWing.X1, baseY, roofWing.Z1), Vec3(roofWing.X1, baseY + hgt, roofWing.Z1), Vec3(roofWing.X0, baseY + hgt, roofWing.Z1), Vec3(0, 0, 1));
				rise = hgt;
				break;
			}
			case BD::Roof::Tiered:
			{
				StaveTiers(c, w, top);
				GableRoof(c, roofWing, false, gableSlot, false, baseY, d.RoofPitch, d.Overhang * 0.6f, d.Overhang);
				break;
			}
			case BD::Roof::Cone:
			case BD::Roof::Dome:
			case BD::Roof::Onion:
				HipRoof(c, rf, baseY, d.RoofPitch, d.Overhang);
				break;
			default:   // Gable
				GableRoof(c, roofWing, false, gableSlot, timberGable, baseY, d.RoofPitch, d.Overhang * 0.6f, d.Overhang);
				break;
			}
			if (w.Main)
			{
				if (d.Chimneys > 0 && d.RoofStyle != BD::Roof::Flat)
					Chimneys(c, roofWing, baseY, rise);
				if (d.RoofStyle == BD::Roof::Gable || d.RoofStyle == BD::Roof::Hip || d.RoofStyle == BD::Roof::Gambrel)
					Dormers(c, roofWing, baseY, d.RoofPitch, d.Dormers);
				if (d.Portico && !d.Peristyle)
					PorticoFront(c, w);
				if (d.Balcony)
					BalconyFront(c, w);
				if (d.CentralDome)
					Dome(c, w, baseY + rise * 0.55f);
				// 스타브 교회 · 가운데 첨탑 (용마루 위 작은 탑신 + 첨탑)
				if (d.SpireHeight > 0.1f && d.Towers == 0)
				{
					const Vec3 cpos(rf.Center.x, baseY + rise - 0.4f, rf.Center.z);
					const float tr = (std::min)(rf.B * 0.32f, 1.6f);
					m.Frustum(WallUpper, cpos, tr, tr, 2.6f, 8, false);
					for (int i = 0; i < 8; ++i)
					{
						const float a = XM_2PI * (i + 0.5f) / 8;
						m.OBox(Timber, cpos + Vec3(cosf(a) * (tr + 0.01f), 1.6f, sinf(a) * (tr + 0.01f)), Vec3(-sinf(a), 0, cosf(a)), kUp, Vec3(cosf(a), 0, sinf(a)), 0.18f, 0.5f, 0.02f);
					}
					m.Frustum(Roof, cpos + Vec3(0, 2.4f, 0), tr + 0.5f, 0.0f, d.SpireHeight, 8, false);
					m.Frustum(Metal, cpos + Vec3(0, 2.4f + d.SpireHeight - 0.3f, 0), 0.06f, 0.02f, 1.2f, 6, true);
				}
			}
		}
		// ---- 모서리 탑
		for (const Vec3& tc : c.TowerCenters)
		{
			const int sides = d.TowerKind == BD::TowerShape::Square ? 4 : (d.TowerKind == BD::TowerShape::Octagon ? 8 : 16);
			const float r = d.TowerKind == BD::TowerShape::Square ? d.TowerRadius * 1.41421356f : d.TowerRadius;
			const bool crenel = d.TowerRoof == BD::Roof::Flat || (d.Crenellations && d.TowerRoof == BD::Roof::Flat);
			TowerBody(c, tc, r, top + d.TowerExtra, sides, 0.0f, d.TowerRoof, false, crenel, d.Towers == 1 ? d.SpireHeight : 0.0f, false);
			++out.Towers;
		}
		// ---- 앞 꾸밈 (테라스 · 문 지붕 · 등 · 간판 · 깃발) · 소품 · 헛간
		if (const Wing* fw = FrontWing(c))
		{
			if (d.Terrace && !d.Arcade && !d.Portico && !d.Peristyle)
				TerraceFront(c, *fw);
			else if (d.Porch && !d.Arcade && !d.Portico && !d.Peristyle)
				PorchFront(c);
			if (d.Lanterns)
				DoorLanterns(c);
			if (d.ShopSign)
				HangingSign(c, c.DoorX + d.DoorWidth * 0.5f + 1.8f < fw->X1 ? 1.0f : -1.0f);
			if (d.Banners > 0)
				FrontBanners(c, *fw);
			if (d.Props > 0)
				PlaceProps(c, *fw, annex.Side);
		}
		if (annex.Side != 0)
			AddAnnex(c, annex);
		Deform(out, d);
		Bounds(out);
	}
}
