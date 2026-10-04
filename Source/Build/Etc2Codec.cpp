#include "pch.h"
#include "Etc2Codec.h"

// ETC1 / ETC2 블록 배치 (OES_compressed_ETC1_RGB8_texture · OpenGL ES 3.0 부록 C):
//  개별 모드 (diff 0): R1 63..60, R2 59..56, G1 55..52, G2 51..48, B1 47..44, B2 43..40 (4 비트 → c<<4 | c)
//  차분 모드 (diff 1): R1 63..59, dR 58..56, G1 55..51, dG 50..48, B1 47..43, dB 42..40 (5 비트 → c<<3 | c>>2, 두 번째 = 첫째 + d, 0..31 안 —
//    밖이면 ETC2 의 T · H · 평면 모드라는 뜻이므로 쓰지 않는다)
//  표 1 39..37, 표 2 36..34, diff 33, flip 32 (0 = 왼쪽 · 오른쪽 2x4, 1 = 위 · 아래 4x2)
//  화소 번호 p = x * 4 + y (열 우선): 색인의 높은 비트 = 비트 16 + p, 낮은 비트 = 비트 p. 색인 0 +a, 1 +b, 2 -a, 3 -b
//  ETC2 평면 모드 (차분 모드로 읽었을 때 B 만 범위 밖): 부드러운 변화 (노멀맵 · 그라데이션) 를 세 점 (O · H · V) 의 평면으로.
//    RO 62..57, GO 56 · 54..49, BO 48 · 44..43 · 41..39, RH 38..34 · 32, GH 31..25, BH 24..19, RV 18..13, GV 12..6, BV 5..0 (R · B 6 비트, G 7 비트).
//    남는 비트 63 · 55 는 R · G 가 넘치지 않게, 47..45 · 42 는 B 가 넘치게 고른다. 화소 = clamp((x(H-O) + y(V-O) + 4O + 2) >> 2)
//  EAC 알파: 기준 63..56, 곱 55..52, 표 51..48, 화소 p 의 3 비트 색인 = 비트 47 - 3p .. 45 - 3p. 값 = clamp(기준 + 표[색인] × 곱)
namespace Etc2Codec
{
	namespace
	{
		const int kModifiers[8][2] = { { 2, 8 }, { 5, 17 }, { 9, 29 }, { 13, 42 }, { 18, 60 }, { 24, 80 }, { 33, 106 }, { 47, 183 } };
		const int kEac[16][8] = {
			{ -3, -6, -9, -15, 2, 5, 8, 14 }, { -3, -7, -10, -13, 2, 6, 9, 12 }, { -2, -5, -8, -13, 1, 4, 7, 12 }, { -2, -4, -6, -13, 1, 3, 5, 12 },
			{ -3, -6, -8, -12, 2, 5, 7, 11 }, { -3, -7, -9, -11, 2, 6, 8, 10 }, { -4, -7, -8, -11, 3, 6, 7, 10 }, { -3, -5, -8, -11, 2, 4, 7, 10 },
			{ -2, -6, -8, -10, 1, 5, 7, 9 }, { -2, -5, -8, -10, 1, 4, 7, 9 }, { -2, -4, -8, -10, 1, 3, 7, 9 }, { -2, -5, -7, -10, 1, 4, 6, 9 },
			{ -3, -4, -7, -10, 2, 3, 6, 9 }, { -1, -2, -3, -10, 0, 1, 2, 9 }, { -4, -6, -8, -9, 3, 5, 7, 8 }, { -3, -5, -7, -9, 2, 4, 6, 8 } };

		int Clamp255(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }
		// 오차 가중치: 색 그림 = 눈 (초록에 민감), 선형 (노멀맵 · 마스크) = 성분 똑같이
		thread_local int tW[3] = { 3, 6, 1 };
		int Modifier(int table, int index) { const int m = kModifiers[table][index & 1]; return (index & 2) ? -m : m; }

		// 화소 p (열 우선) 의 RGB
		void Pixel(const uint8_t rgba[64], int p, int c[3])
		{
			const int x = p / 4, y = p % 4;
			const uint8_t* s = rgba + (y * 4 + x) * 4;
			c[0] = s[0]; c[1] = s[1]; c[2] = s[2];
		}

		// 부분 블록 (8 화소) 의 화소 번호
		void SubPixels(bool flip, int sub, int out[8])
		{
			int n = 0;
			for (int p = 0; p < 16; ++p)
			{
				const int x = p / 4, y = p % 4;
				const bool second = flip ? (y >= 2) : (x >= 2);
				if (second == (sub == 1)) out[n++] = p;
			}
		}

		// 기준 색 base (8 비트) 로 부분 블록의 가장 좋은 표 · 색인 → 오차
		int FitSub(const uint8_t rgba[64], const int pix[8], const int base[3], int& bestTable, int idx[8])
		{
			int best = INT32_MAX;
			for (int t = 0; t < 8; ++t)
			{
				int err = 0, tmp[8];
				for (int i = 0; i < 8 && err < best; ++i)
				{
					int c[3];
					Pixel(rgba, pix[i], c);
					int be = INT32_MAX, bi = 0;
					for (int k = 0; k < 4; ++k)
					{
						const int m = Modifier(t, k);
						const int dr = Clamp255(base[0] + m) - c[0], dg = Clamp255(base[1] + m) - c[1], db = Clamp255(base[2] + m) - c[2];
						const int e = dr * dr * tW[0] + dg * dg * tW[1] + db * db * tW[2];
						if (e < be) { be = e; bi = k; }
					}
					tmp[i] = bi;
					err += be;
				}
				if (err < best) { best = err; bestTable = t; for (int i = 0; i < 8; ++i) idx[i] = tmp[i]; }
			}
			return best;
		}

		void Average(const uint8_t rgba[64], const int pix[8], float avg[3])
		{
			avg[0] = avg[1] = avg[2] = 0;
			for (int i = 0; i < 8; ++i)
			{
				int c[3];
				Pixel(rgba, pix[i], c);
				for (int k = 0; k < 3; ++k) avg[k] += c[k] / 8.0f;
			}
		}

		int Expand4(int v) { return (v << 4) | v; }
		int Expand5(int v) { return (v << 3) | (v >> 2); }

		struct Candidate
		{
			int Err = INT32_MAX;
			bool Diff = false, Flip = false;
			int Q[2][3] = {};      // 양자화한 기준 색 (4 비트 또는 5 비트)
			int Table[2] = {};
			int Idx[2][8] = {};
		};

		// 부분 블록 하나: 평균 주변 기준 색 (각 성분 -1 · 0 · +1) 을 시험
		int BestSub(const uint8_t rgba[64], const int pix[8], int bits, int q[3], int& table, int idx[8])
		{
			float avg[3];
			Average(rgba, pix, avg);
			const int levels = (1 << bits) - 1;
			int center[3];
			for (int k = 0; k < 3; ++k) center[k] = (std::min)(levels, (std::max)(0, (int)std::lround(avg[k] * levels / 255.0f)));
			int best = INT32_MAX;
			for (int dr = -2; dr <= 2; ++dr)
				for (int dg = -2; dg <= 2; ++dg)
					for (int db = -2; db <= 2; ++db)
					{
						const int c[3] = { center[0] + dr, center[1] + dg, center[2] + db };
						if (c[0] < 0 || c[1] < 0 || c[2] < 0 || c[0] > levels || c[1] > levels || c[2] > levels) continue;
						int base[3];
						for (int k = 0; k < 3; ++k) base[k] = bits == 4 ? Expand4(c[k]) : Expand5(c[k]);
						int t, tmp[8];
						const int e = FitSub(rgba, pix, base, t, tmp);
						if (e < best)
						{
							best = e;
							table = t;
							for (int k = 0; k < 3; ++k) q[k] = c[k];
							for (int i = 0; i < 8; ++i) idx[i] = tmp[i];
						}
					}
			return best;
		}

		int Expand6(int v) { return (v << 2) | (v >> 4); }
		int Expand7(int v) { return (v << 1) | (v >> 6); }

		// 평면 모드: 세 점 O (0,0) · H (4,0) · V (0,4) 의 각 성분 (R 6 · G 7 · B 6 비트)
		struct Planar { int O[3], H[3], V[3]; };

		void PlanarColor(const Planar& p, int x, int y, int out[3])
		{
			for (int k = 0; k < 3; ++k)
			{
				const int bits = k == 1 ? 7 : 6;
				const int o = bits == 7 ? Expand7(p.O[k]) : Expand6(p.O[k]);
				const int h = bits == 7 ? Expand7(p.H[k]) : Expand6(p.H[k]);
				const int v = bits == 7 ? Expand7(p.V[k]) : Expand6(p.V[k]);
				out[k] = Clamp255((x * (h - o) + y * (v - o) + 4 * o + 2) >> 2);
			}
		}

		int PlanarError(const uint8_t rgba[64], const Planar& p)
		{
			int err = 0;
			for (int y = 0; y < 4; ++y)
				for (int x = 0; x < 4; ++x)
				{
					int c[3];
					PlanarColor(p, x, y, c);
					const uint8_t* s = rgba + (y * 4 + x) * 4;
					const int dr = c[0] - s[0], dg = c[1] - s[1], db = c[2] - s[2];
					err += dr * dr * tW[0] + dg * dg * tW[1] + db * db * tW[2];
				}
			return err;
		}

		// 최소 제곱 평면 c = a + b·x + d·y → O = a, H = a + 4b, V = a + 4d, 양자화 뒤 각 값 ±1 로 다듬기
		int FitPlanar(const uint8_t rgba[64], Planar& best)
		{
			for (int k = 0; k < 3; ++k)
			{
				double sc = 0, sxc = 0, syc = 0;
				for (int y = 0; y < 4; ++y)
					for (int x = 0; x < 4; ++x)
					{
						const double c = rgba[(y * 4 + x) * 4 + k];
						sc += c; sxc += x * c; syc += y * c;
					}
				// x, y = 0..3: 평균 1.5, 분산 합 = 16 · 1.25 = 20
				const double b = (sxc - 1.5 * sc) / 20.0, d = (syc - 1.5 * sc) / 20.0;
				const double a = sc / 16.0 - 1.5 * b - 1.5 * d;
				const int levels = k == 1 ? 127 : 63;
				auto q = [&](double v) { return (std::min)(levels, (std::max)(0, (int)std::lround(v * levels / 255.0))); };
				best.O[k] = q(a);
				best.H[k] = q(a + 4.0 * b);
				best.V[k] = q(a + 4.0 * d);
			}
			int err = PlanarError(rgba, best);
			for (int pass = 0; pass < 2; ++pass)
				for (int k = 0; k < 3; ++k)
					for (int which = 0; which < 3; ++which)
						for (int delta : { -1, 1 })
						{
							Planar t = best;
							int* v = which == 0 ? &t.O[k] : which == 1 ? &t.H[k] : &t.V[k];
							*v += delta;
							if (*v < 0 || *v > (k == 1 ? 127 : 63)) continue;
							const int e = PlanarError(rgba, t);
							if (e < err) { err = e; best = t; }
						}
			return err;
		}

		uint64_t PackPlanar(const Planar& p)
		{
			const uint64_t RO = p.O[0], GO = p.O[1], BO = p.O[2], RH = p.H[0], GH = p.H[1], BH = p.H[2], RV = p.V[0], GV = p.V[1], BV = p.V[2];
			uint64_t v = 0;
			v |= RO << 57;
			v |= ((GO >> 6) & 1) << 56;
			v |= (GO & 63) << 49;
			v |= ((BO >> 5) & 1) << 48;
			v |= ((BO >> 3) & 3) << 43;
			v |= (BO & 7) << 39;
			v |= ((RH >> 1) & 31) << 34;
			v |= 1ull << 33;   // diff
			v |= (RH & 1) << 32;
			v |= GH << 25;
			v |= BH << 19;
			v |= RV << 13;
			v |= GV << 6;
			v |= BV;
			// 차분 모드로 읽을 때 R · G 는 범위 안, B 는 범위 밖이 되게 남는 비트를 고른다
			auto sext3 = [](int d) { return d >= 4 ? d - 8 : d; };
			{
				const int r = (int)((v >> 59) & 15), dr = sext3((int)((v >> 56) & 7));
				if (r + dr < 0) v |= 1ull << 63;
			}
			{
				const int g = (int)((v >> 51) & 15), dg = sext3((int)((v >> 48) & 7));
				if (g + dg < 0) v |= 1ull << 55;
			}
			{
				const int b43 = (int)((v >> 43) & 3), b21 = (int)((v >> 40) & 3);
				if (b43 + b21 >= 4) v |= 7ull << 45;   // B = 28 + b43, dB = b21 (0..3) → 31 넘음
				else v |= 1ull << 42;                  // B = b43, dB = -4 + b21 → 0 아래
			}
			return v;
		}

		void Write64(uint64_t v, uint8_t out[8]) { for (int i = 0; i < 8; ++i) out[i] = (uint8_t)(v >> (56 - 8 * i)); }
		uint64_t Read64(const uint8_t in[8]) { uint64_t v = 0; for (int i = 0; i < 8; ++i) v = (v << 8) | in[i]; return v; }
	}

	void EncodeRgb(const uint8_t rgba[64], uint8_t out[8], bool perceptual)
	{
		tW[0] = perceptual ? 3 : 1;
		tW[1] = perceptual ? 6 : 1;
		tW[2] = 1;
		Candidate best;
		for (int f = 0; f < 2; ++f)
		{
			int pix[2][8];
			SubPixels(f == 1, 0, pix[0]);
			SubPixels(f == 1, 1, pix[1]);
			// 개별 모드 (4 비트 둘)
			{
				Candidate c;
				c.Flip = f == 1;
				c.Err = 0;
				for (int s = 0; s < 2; ++s)
					c.Err += BestSub(rgba, pix[s], 4, c.Q[s], c.Table[s], c.Idx[s]);
				if (c.Err < best.Err) best = c;
			}
			// 차분 모드 (5 비트, 둘째 = 첫째 + -4..3): 각자 가장 좋은 색이 범위 안이면 그대로, 아니면 둘째를 범위 안으로 당겨 다시 맞춘다
			{
				Candidate c;
				c.Flip = f == 1;
				c.Diff = true;
				int e0 = BestSub(rgba, pix[0], 5, c.Q[0], c.Table[0], c.Idx[0]);
				int q1[3], t1, i1[8];
				int e1 = BestSub(rgba, pix[1], 5, q1, t1, i1);
				bool inRange = true;
				for (int k = 0; k < 3; ++k) inRange = inRange && q1[k] - c.Q[0][k] >= -4 && q1[k] - c.Q[0][k] <= 3;
				if (!inRange)
				{
					int clamped[3], base[3];
					for (int k = 0; k < 3; ++k)
					{
						clamped[k] = (std::min)(c.Q[0][k] + 3, (std::max)(c.Q[0][k] - 4, q1[k]));
						clamped[k] = (std::min)(31, (std::max)(0, clamped[k]));
						base[k] = Expand5(clamped[k]);
						q1[k] = clamped[k];
					}
					e1 = FitSub(rgba, pix[1], base, t1, i1);
				}
				for (int k = 0; k < 3; ++k) c.Q[1][k] = q1[k];
				c.Table[1] = t1;
				for (int i = 0; i < 8; ++i) c.Idx[1][i] = i1[i];
				c.Err = e0 + e1;
				if (c.Err < best.Err) best = c;
			}
		}
		// ETC2 평면 모드가 더 나으면 그것으로
		{
			Planar p;
			if (FitPlanar(rgba, p) < best.Err)
			{
				Write64(PackPlanar(p), out);
				return;
			}
		}
		// 비트로
		uint64_t v = 0;
		if (best.Diff)
		{
			for (int k = 0; k < 3; ++k)
			{
				const int shift = 59 - 8 * k;
				v |= (uint64_t)best.Q[0][k] << shift;
				v |= (uint64_t)((best.Q[1][k] - best.Q[0][k]) & 7) << (shift - 3);
			}
			v |= 1ull << 33;
		}
		else
			for (int k = 0; k < 3; ++k)
			{
				v |= (uint64_t)best.Q[0][k] << (60 - 8 * k);
				v |= (uint64_t)best.Q[1][k] << (56 - 8 * k);
			}
		v |= (uint64_t)best.Table[0] << 37;
		v |= (uint64_t)best.Table[1] << 34;
		if (best.Flip) v |= 1ull << 32;
		for (int s = 0; s < 2; ++s)
		{
			int pix[8];
			SubPixels(best.Flip, s, pix);
			for (int i = 0; i < 8; ++i)
			{
				const int p = pix[i], index = best.Idx[s][i];
				if (index & 2) v |= 1ull << (16 + p);
				if (index & 1) v |= 1ull << p;
			}
		}
		Write64(v, out);
	}

	void DecodeRgb(const uint8_t in[8], uint8_t rgba[64])
	{
		const uint64_t v = Read64(in);
		const bool diff = (v >> 33) & 1, flip = (v >> 32) & 1;
		if (diff)
		{
			auto sext3 = [](int d) { return d >= 4 ? d - 8 : d; };
			const int b = (int)((v >> 43) & 31) + sext3((int)((v >> 40) & 7));
			const int r = (int)((v >> 59) & 31) + sext3((int)((v >> 56) & 7));
			const int g = (int)((v >> 51) & 31) + sext3((int)((v >> 48) & 7));
			if (r >= 0 && r <= 31 && g >= 0 && g <= 31 && (b < 0 || b > 31))
			{
				Planar p;
				p.O[0] = (int)((v >> 57) & 63);
				p.O[1] = (int)(((v >> 56) & 1) << 6 | ((v >> 49) & 63));
				p.O[2] = (int)(((v >> 48) & 1) << 5 | ((v >> 43) & 3) << 3 | ((v >> 39) & 7));
				p.H[0] = (int)(((v >> 34) & 31) << 1 | ((v >> 32) & 1));
				p.H[1] = (int)((v >> 25) & 127);
				p.H[2] = (int)((v >> 19) & 63);
				p.V[0] = (int)((v >> 13) & 63);
				p.V[1] = (int)((v >> 6) & 127);
				p.V[2] = (int)(v & 63);
				for (int y = 0; y < 4; ++y)
					for (int x = 0; x < 4; ++x)
					{
						int c[3];
						PlanarColor(p, x, y, c);
						uint8_t* d = rgba + (y * 4 + x) * 4;
						d[0] = (uint8_t)c[0]; d[1] = (uint8_t)c[1]; d[2] = (uint8_t)c[2]; d[3] = 255;
					}
				return;
			}
		}
		int base[2][3];
		for (int k = 0; k < 3; ++k)
		{
			if (diff)
			{
				const int c1 = (int)((v >> (59 - 8 * k)) & 31);
				int d = (int)((v >> (56 - 8 * k)) & 7);
				if (d >= 4) d -= 8;
				base[0][k] = Expand5(c1);
				base[1][k] = Expand5((c1 + d) & 31);
			}
			else
			{
				base[0][k] = Expand4((int)((v >> (60 - 8 * k)) & 15));
				base[1][k] = Expand4((int)((v >> (56 - 8 * k)) & 15));
			}
		}
		const int table[2] = { (int)((v >> 37) & 7), (int)((v >> 34) & 7) };
		for (int p = 0; p < 16; ++p)
		{
			const int x = p / 4, y = p % 4;
			const int s = flip ? (y >= 2) : (x >= 2);
			const int index = (int)(((v >> (16 + p)) & 1) << 1 | ((v >> p) & 1));
			const int m = Modifier(table[s], index);
			uint8_t* d = rgba + (y * 4 + x) * 4;
			for (int k = 0; k < 3; ++k) d[k] = (uint8_t)Clamp255(base[s][k] + m);
			d[3] = 255;
		}
	}

	void EncodeAlpha(const uint8_t rgba[64], uint8_t out[8])
	{
		int a[16], lo = 255, hi = 0, sum = 0;
		for (int p = 0; p < 16; ++p)
		{
			const int x = p / 4, y = p % 4;
			a[p] = rgba[(y * 4 + x) * 4 + 3];
			lo = (std::min)(lo, a[p]);
			hi = (std::max)(hi, a[p]);
			sum += a[p];
		}
		int bestErr = INT32_MAX, bestBase = 0, bestMul = 1, bestTable = 0, bestIdx[16] = {};
		const int bases[3] = { (lo + hi + 1) / 2, (sum + 8) / 16, hi == lo ? lo : (lo + hi) / 2 };
		for (int bi = 0; bi < 3 && bestErr > 0; ++bi)
		{
			const int base = bases[bi];
			for (int t = 0; t < 16 && bestErr > 0; ++t)
				for (int mul = (hi == lo ? 0 : 1); mul < 16 && bestErr > 0; ++mul)
				{
					int err = 0, idx[16];
					for (int p = 0; p < 16 && err < bestErr; ++p)
					{
						int be = INT32_MAX, bk = 0;
						for (int k = 0; k < 8; ++k)
						{
							const int d = Clamp255(base + kEac[t][k] * mul) - a[p];
							if (d * d < be) { be = d * d; bk = k; }
						}
						idx[p] = bk;
						err += be;
					}
					if (err < bestErr) { bestErr = err; bestBase = base; bestMul = mul; bestTable = t; for (int p = 0; p < 16; ++p) bestIdx[p] = idx[p]; }
				}
		}
		uint64_t v = (uint64_t)bestBase << 56 | (uint64_t)bestMul << 52 | (uint64_t)bestTable << 48;
		for (int p = 0; p < 16; ++p)
			v |= (uint64_t)bestIdx[p] << (45 - 3 * p);
		Write64(v, out);
	}

	void DecodeAlpha(const uint8_t in[8], uint8_t rgba[64])
	{
		const uint64_t v = Read64(in);
		const int base = (int)(v >> 56), mul = (int)((v >> 52) & 15), table = (int)((v >> 48) & 15);
		for (int p = 0; p < 16; ++p)
		{
			const int x = p / 4, y = p % 4;
			const int index = (int)((v >> (45 - 3 * p)) & 7);
			rgba[(y * 4 + x) * 4 + 3] = (uint8_t)Clamp255(base + kEac[table][index] * mul);
		}
	}
}
