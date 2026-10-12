#include "pch.h"
#include "TreeGenerator.h"
#include <random>
#include "TreeTextures.h"
#include "PlantLibrary.h"

using json = nlohmann::json;

// ================================================================== 프리셋 (PlantLibrary — 기후대별 종 100 여 개, 처음 12 개는 예전 프리셋 번호 그대로)
const char* const* TreeParams::PresetNames(int& count)
{
	return PlantLibrary::Names(count);
}

void TreeParams::ApplyPreset(int preset)
{
	const int seed = Seed;
	const PlantLibrary::Species* sp = PlantLibrary::At(preset);
	*this = sp ? sp->Desc.Params : TreeParams();
	Seed = seed;
}

// ================================================================== 저장
json TreeParams::ToJson() const
{
	json j;
	j["seed"] = Seed;
	j["height"] = Height; j["radius"] = Radius; j["tipRadius"] = TipRadius; j["flare"] = Flare; j["gnarl"] = Gnarl; j["lean"] = Lean;
	j["radialSegments"] = RadialSegments; j["crown"] = (int)CrownShape;
	j["levels"] = Levels;
	json levels = json::array();
	for (const Level& l : L)
		levels.push_back({ { "count", l.Count }, { "start", l.Start }, { "angle", l.Angle }, { "angleVariance", l.AngleVariance },
			{ "length", l.Length }, { "lengthVariance", l.LengthVariance }, { "gravity", l.Gravity }, { "up", l.Up },
			{ "radius", l.Radius }, { "gnarl", l.Gnarl } });
	j["branches"] = levels;
	j["leafShape"] = (int)Leaf; j["leafCards"] = LeafCards; j["leavesPerCard"] = LeavesPerCard;
	j["leafCardSize"] = LeafCardSize; j["leafStart"] = LeafStart;
	// 새 값은 기본이 아닐 때만 (예전 나무의 MeshKey · 저장이 그대로)
	if (Shape != Form::Tree) j["form"] = (int)Shape;
	if (CardSegments != 1) j["cardSegments"] = CardSegments;
	if (Droop != 0.0f) j["droop"] = Droop;
	if (CardSpread != 45.0f) j["cardSpread"] = CardSpread;
	if (FlowerPetals > 0)
	{
		j["flowerPetals"] = FlowerPetals; j["flowerCards"] = FlowerCards; j["flowerSize"] = FlowerSize;
	}
	if (Stems > 0) j["stems"] = Stems;
	return j;
}

void TreeParams::FromJson(const json& j)
{
	Seed = j.value("seed", Seed);
	Height = j.value("height", Height); Radius = j.value("radius", Radius); TipRadius = j.value("tipRadius", TipRadius);
	Flare = j.value("flare", Flare); Gnarl = j.value("gnarl", Gnarl); Lean = j.value("lean", Lean);
	RadialSegments = j.value("radialSegments", RadialSegments); CrownShape = (Crown)j.value("crown", (int)CrownShape);
	Levels = j.value("levels", Levels);
	if (j.contains("branches") && j["branches"].is_array())
		for (int i = 0; i < 3 && i < (int)j["branches"].size(); ++i)
		{
			const json& b = j["branches"][i];
			Level& l = L[i];
			l.Count = b.value("count", l.Count); l.Start = b.value("start", l.Start); l.Angle = b.value("angle", l.Angle);
			l.AngleVariance = b.value("angleVariance", l.AngleVariance); l.Length = b.value("length", l.Length);
			l.LengthVariance = b.value("lengthVariance", l.LengthVariance); l.Gravity = b.value("gravity", l.Gravity);
			l.Up = b.value("up", l.Up); l.Radius = b.value("radius", l.Radius); l.Gnarl = b.value("gnarl", l.Gnarl);
		}
	Leaf = (LeafShape)j.value("leafShape", (int)Leaf); LeafCards = j.value("leafCards", LeafCards);
	LeavesPerCard = j.value("leavesPerCard", LeavesPerCard); LeafCardSize = j.value("leafCardSize", LeafCardSize);
	LeafStart = j.value("leafStart", LeafStart);
	Shape = (Form)std::clamp(j.value("form", (int)Shape), 0, kForms - 1);
	CardSegments = j.value("cardSegments", CardSegments); Droop = j.value("droop", Droop); CardSpread = j.value("cardSpread", CardSpread);
	FlowerPetals = j.value("flowerPetals", FlowerPetals); FlowerCards = j.value("flowerCards", FlowerCards); FlowerSize = j.value("flowerSize", FlowerSize);
	Stems = j.value("stems", Stems);
}

// ================================================================== 생성
namespace
{
	constexpr int kMaxBranches = 4000;
	constexpr int kMaxLeafCards = 12000;

	struct Rng
	{
		std::mt19937 Engine;
		explicit Rng(int seed) : Engine((uint32_t)seed * 2654435761u + 12345u) {}
		float U() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(Engine); }
		float S() { return U() * 2.0f - 1.0f; }
	};

	XMVECTOR Perpendicular(FXMVECTOR d)
	{
		const XMVECTOR a = fabsf(XMVectorGetY(d)) < 0.9f ? XMVectorSet(0, 1, 0, 0) : XMVectorSet(1, 0, 0, 0);
		return XMVector3Normalize(XMVector3Cross(d, a));
	}

	// Weber-Penn 수관 모양: r = 1 이면 수관 아래, 0 이면 꼭대기 → 가지 길이 비율
	float CrownRatio(TreeParams::Crown c, float r)
	{
		switch (c)
		{
		case TreeParams::Crown::Conical: return 0.2f + 0.8f * r;
		case TreeParams::Crown::Spherical: return 0.2f + 0.8f * sinf(XM_PI * r);
		case TreeParams::Crown::Hemispherical: return 0.2f + 0.8f * sinf(XM_PIDIV2 * r);
		case TreeParams::Crown::Cylindrical: return 1.0f;
		case TreeParams::Crown::TaperedCylindrical: return 0.5f + 0.5f * r;
		case TreeParams::Crown::Flame: return r <= 0.7f ? r / 0.7f : (1.0f - r) / 0.3f;
		case TreeParams::Crown::InverseConical: return 1.0f - 0.8f * r;
		}
		return 1.0f;
	}

	struct Node
	{
		XMFLOAT3 Pos;
		XMFLOAT3 Dir;
		float Radius;
		float Dist;   // 가지 시작에서의 거리 (m)
	};

	// 가지가 물려받는 바람 값 (붙은 지점의 부모 흔들림 → 관절이 떨어지지 않는다)
	struct WindInfo
	{
		float W1Base = 0.0f, Phase1 = 0.0f;
		float W2Base = 0.0f, Phase2 = 0.0f;
	};

	XMFLOAT3 F3(FXMVECTOR v) { XMFLOAT3 f; XMStoreFloat3(&f, v); return f; }

	struct Builder
	{
		const TreeParams& P;
		TreeMeshData& M;
		Rng R;
		std::vector<TreeVertex> LeafV;
		std::vector<uint32_t> LeafI;
		std::vector<XMFLOAT3> LeafNormals;   // 카드 평면 법선 (나중에 수관 구 쪽으로 굽힌다)

		int Lod = 0;   // 1 = 가지 링·잔가지·잎 카드를 줄인 중간 단계 (난수 순서는 그대로 → 모양이 같다)

		Builder(const TreeParams& p, TreeMeshData& m, int lod) : P(p), M(m), R(p.Seed), Lod(lod) {}

		// 가지 가중치: 1차 가지는 끝으로 갈수록 크게, 길이가 길수록 크게
		static float Weight1(float t, float length) { return powf(t, 1.5f) * (std::min)(1.0f, length / 4.0f); }
		static float Weight2(float t, float length) { return powf(t, 1.5f) * (std::min)(1.0f, length / 1.5f); }

		void Winds(int level, float t, float length, const WindInfo& wi, float& w1, float& w2) const
		{
			w1 = level == 0 ? 0.0f : (level == 1 ? Weight1(t, length) : wi.W1Base);
			w2 = level <= 1 ? 0.0f : wi.W2Base + Weight2(t, length);
		}

		// 삼각형 앞면(시계 방향)이 기준 법선을 향하도록
		void AddTri(std::vector<uint32_t>& idx, const std::vector<TreeVertex>& verts, uint32_t a, uint32_t b, uint32_t c, FXMVECTOR refNormal)
		{
			const XMVECTOR pa = XMLoadFloat3(&verts[a].Pos), pb = XMLoadFloat3(&verts[b].Pos), pc = XMLoadFloat3(&verts[c].Pos);
			const XMVECTOR n = XMVector3Cross(XMVectorSubtract(pb, pa), XMVectorSubtract(pc, pa));
			if (XMVectorGetX(XMVector3Dot(n, refNormal)) < 0.0f)
				std::swap(b, c);
			idx.push_back(a); idx.push_back(b); idx.push_back(c);
		}

		void Grow(int level, XMVECTOR start, XMVECTOR dir, float length, float r0, float r1, const WindInfo& wi)
		{
			if (M.BranchCount >= kMaxBranches || length < 0.02f)
				return;
			M.BranchCount++;

			const TreeParams::Level* lp = level > 0 ? &P.L[level - 1] : nullptr;
			const float segLen = level == 0 ? 0.35f : (level == 1 ? 0.25f : 0.15f);
			const int segs = std::clamp((int)ceilf(length / segLen), 3, 40);
			const float gnarl = lp ? lp->Gnarl : P.Gnarl;
			const float gravity = lp ? lp->Gravity : 0.0f;
			const float up = lp ? lp->Up : 0.0f;
			const float step = length / segs;

			// ---- 경로: 무작위 휘어짐 + 처짐(중력) + 위로 향함
			std::vector<Node> nodes(segs + 1);
			XMVECTOR pos = start;
			XMVECTOR d = XMVector3Normalize(dir);
			for (int i = 0; i <= segs; ++i)
			{
				const float t = (float)i / segs;
				float r = r0 + (r1 - r0) * t;
				if (level == 0)
					r *= 1.0f + P.Flare * expf(-(t * length) / 0.55f);   // 뿌리 쪽 퍼짐
				nodes[i] = { F3(pos), F3(d), r, t * length };
				if (i == segs)
					break;
				pos = XMVectorAdd(pos, XMVectorScale(d, step));
				const XMVECTOR wobble = XMVectorSet(R.S(), R.S() * 0.5f, R.S(), 0.0f);
				d = XMVectorAdd(d, XMVectorScale(wobble, gnarl * 0.35f));
				d = XMVectorAdd(d, XMVectorSet(0.0f, (up - gravity) * 2.0f * step / length, 0.0f, 0.0f));
				d = XMVector3Normalize(d);
			}

			// ---- 관(링을 이은 원통). 링 방향은 평행 이동으로 이어 비틀림이 없게
			//  LOD1: 링 변을 줄이고 짧은 잔가지(0.6 m 미만)는 그리지 않는다 (난수를 쓰지 않는 부분이라 모양은 그대로)
			const bool emitBark = Lod == 0 || level <= 1 || length >= 0.6f;
			if (emitBark)
			{
			const int sides = Lod == 0
				? (level == 0 ? (std::max)(5, P.RadialSegments) : (level == 1 ? (std::max)(4, P.RadialSegments * 2 / 3) : 4))
				: (level == 0 ? (std::max)(5, P.RadialSegments / 2) : (level == 1 ? 4 : 3));
			const uint32_t base = (uint32_t)M.Vertices.size();
			XMVECTOR n = Perpendicular(XMLoadFloat3(&nodes[0].Dir));
			const float ao = level == 0 ? 1.0f : (level == 1 ? 0.9f : 0.8f);
			for (int i = 0; i <= segs; ++i)
			{
				const XMVECTOR di = XMLoadFloat3(&nodes[i].Dir);
				n = XMVector3Normalize(XMVectorSubtract(n, XMVectorScale(di, XMVectorGetX(XMVector3Dot(n, di)))));
				const XMVECTOR b = XMVector3Cross(di, n);
				const float t = (float)i / segs;
				float w1, w2;
				Winds(level, t, length, wi, w1, w2);
				for (int k = 0; k <= sides; ++k)
				{
					const float a = XM_2PI * k / sides;
					const XMVECTOR off = XMVectorAdd(XMVectorScale(n, cosf(a)), XMVectorScale(b, sinf(a)));
					TreeVertex v = {};
					v.Pos = F3(XMVectorAdd(XMLoadFloat3(&nodes[i].Pos), XMVectorScale(off, nodes[i].Radius)));
					v.Normal = F3(off);
					v.UV = XMFLOAT2((float)k / sides, nodes[i].Dist);
					v.Wind = XMFLOAT4(0.0f, w1, w2, 0.0f);
					v.Axis = XMFLOAT4(nodes[i].Dir.x, nodes[i].Dir.y, nodes[i].Dir.z, ao * (level == 0 ? (0.75f + 0.25f * (std::min)(1.0f, nodes[i].Dist / 1.5f)) : 1.0f));
					v.Phase = XMFLOAT4(wi.Phase1, wi.Phase2, 0.0f, r0);   // w = 밑동 반지름 (수피 무늬 칸 수)
					M.Vertices.push_back(v);
				}
			}
			for (int i = 0; i < segs; ++i)
				for (int k = 0; k < sides; ++k)
				{
					const uint32_t a = base + i * (sides + 1) + k, bb = a + 1, c = a + (sides + 1), dd = c + 1;
					const XMVECTOR ref = XMLoadFloat3(&M.Vertices[a].Normal);
					AddTri(M.Indices, M.Vertices, a, c, bb, ref);
					AddTri(M.Indices, M.Vertices, bb, c, dd, ref);
				}
			// 끝 마개 (가지 끝이 뚫려 보이지 않게)
			{
				const Node& last = nodes[segs];
				TreeVertex tip = M.Vertices.back();
				const XMVECTOR ld = XMLoadFloat3(&last.Dir);
				tip.Pos = F3(XMVectorAdd(XMLoadFloat3(&last.Pos), XMVectorScale(ld, last.Radius * 0.6f)));
				tip.Normal = last.Dir;
				const uint32_t tipIndex = (uint32_t)M.Vertices.size();
				M.Vertices.push_back(tip);
				const uint32_t ring = base + segs * (sides + 1);
				for (int k = 0; k < sides; ++k)
					AddTri(M.Indices, M.Vertices, ring + k, ring + k + 1, tipIndex, ld);
			}
			}   // emitBark

			// ---- 자식 가지
			if (level < P.Levels)
			{
				const TreeParams::Level& cl = P.L[level];
				for (int c = 0; c < cl.Count; ++c)
				{
					const float u = std::clamp((c + 0.5f + R.S() * 0.35f) / (float)cl.Count, 0.0f, 1.0f);
					const float tA = cl.Start + (1.0f - cl.Start) * u;
					const float fi = tA * segs;
					const int i0 = (std::min)((int)fi, segs - 1);
					const float fr = fi - i0;
					const XMVECTOR p = XMVectorLerp(XMLoadFloat3(&nodes[i0].Pos), XMLoadFloat3(&nodes[i0 + 1].Pos), fr);
					const XMVECTOR pd = XMVector3Normalize(XMVectorLerp(XMLoadFloat3(&nodes[i0].Dir), XMLoadFloat3(&nodes[i0 + 1].Dir), fr));
					const float pr = nodes[i0].Radius + (nodes[i0 + 1].Radius - nodes[i0].Radius) * fr;

					// 황금각으로 돌아가며 붙인다 (잎차례처럼 고르게 퍼짐)
					const float az = XMConvertToRadians(c * 137.5f + R.S() * 20.0f);
					const XMVECTOR n0 = Perpendicular(pd);
					const XMVECTOR b0 = XMVector3Cross(pd, n0);
					const XMVECTOR side = XMVectorAdd(XMVectorScale(n0, cosf(az)), XMVectorScale(b0, sinf(az)));
					const float angle = XMConvertToRadians(cl.Angle + R.S() * cl.AngleVariance);
					const XMVECTOR cd = XMVector3Normalize(XMVectorAdd(XMVectorScale(pd, cosf(angle)), XMVectorScale(side, sinf(angle))));

					float len = length * cl.Length * (1.0f + R.S() * cl.LengthVariance);
					if (level == 0)
						len *= CrownRatio(P.CrownShape, std::clamp((1.0f - tA) / (std::max)(0.01f, 1.0f - cl.Start), 0.0f, 1.0f));
					else
						len *= 1.0f - 0.5f * tA;   // 부모 끝쪽일수록 짧게
					const float cr0 = (std::min)(pr * cl.Radius, pr * 0.9f);
					const float cr1 = (std::max)(cr0 * 0.2f, 0.003f);

					WindInfo cw;
					if (level == 0)
					{
						cw.Phase1 = R.U();
						cw.Phase2 = R.U();
					}
					else if (level == 1)
					{
						cw.W1Base = Weight1(tA, length);
						cw.Phase1 = wi.Phase1;
						cw.Phase2 = R.U();
					}
					else
					{
						cw.W1Base = wi.W1Base;
						cw.Phase1 = wi.Phase1;
						cw.W2Base = wi.W2Base + Weight2(tA, length);
						cw.Phase2 = wi.Phase2;
					}
					Grow(level + 1, p, cd, len, cr0, cr1, cw);
				}
			}

			// ---- 잎 (마지막 단계 가지)
			if (P.Shape == TreeParams::Form::Palm && level == 0)
				AddCrown(nodes, segs, length);
			else if (level == P.Levels)
				AddLeaves(nodes, segs, level, length, wi);
		}

		// 꽃이 있는 종: 잎 카드는 아틀라스 칸 0 ~ 2, 꽃 카드는 칸 3 (꽃잎 무늬). 난수 하나로 칸 · 칸 안 값을 정한다 (난수 순서가 그대로)
		float LeafSeed(float r) const
		{
			if (P.FlowerPetals <= 0)
				return r;
			const float s3 = r * 3.0f;
			const float k = floorf(s3);
			return (k + 0.04f + 0.92f * (s3 - k)) * 0.25f;
		}
		static float FlowerSeed(float r) { return (3.0f + 0.04f + 0.92f * r) * 0.25f; }

		// 카드 한 장. p = 카드 아래 가운데 (v = 0), up = 카드 위쪽, n = 면 법선 (right = up × n).
		//  segments == 1: 잎이 덮인 곳을 감싸는 다각형 (투명한 귀퉁이를 덜 칠한다 — 셰이더 TreeLeafSample 과 같은 시드 → 칸 · 뒤집기)
		//  segments > 1: 길이 방향 띠, 끝으로 갈수록 droop 만큼 아래로 휜다 (면에 수직으로 — 풀잎 · 고사리 · 야자 잎)
		void EmitCard(FXMVECTOR p, FXMVECTOR up, FXMVECTOR n, float size, float seed, int segments, float droop, float w1, float w2, const WindInfo& wi)
		{
			const XMVECTOR right = XMVector3Normalize(XMVector3Cross(up, n));
			if (segments <= 1)
			{
				XMFLOAT2 uvs[8] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
				int count = 4;
				if (P.LeafLengthForHull >= 0.0f)
				{
					const float cellF = (seed - floorf(seed)) * 4.0f;
					const float flipF = seed * 7.31f - floorf(seed * 7.31f);
					const bool nearEdge = fabsf(cellF - roundf(cellF)) < 1e-3f || fabsf(flipF - 0.5f) < 1e-3f;   // 셰이더와 반올림이 갈릴 수 있으면 사각형
					if (!nearEdge)
					{
						const TreeTextures::LeafHull& hull = TreeTextures::LeafCardHull((int)P.Leaf, P.LeavesPerCard, P.LeafLengthForHull, (int)floorf(cellF), P.FlowerPetals);
						const bool flip = flipF > 0.5f;
						count = hull.Count;
						for (int k = 0; k < count; ++k)
							uvs[k] = { flip ? 1.0f - hull.Points[k].x : hull.Points[k].x, hull.Points[k].y };
					}
				}
				const uint32_t base = (uint32_t)LeafV.size();
				for (int k = 0; k < count; ++k)
				{
					TreeVertex v = {};
					v.Pos = F3(XMVectorAdd(XMVectorAdd(p, XMVectorScale(right, (uvs[k].x - 0.5f) * size)), XMVectorScale(up, uvs[k].y * size)));
					v.Normal = F3(n);
					v.UV = uvs[k];
					v.Wind = XMFLOAT4(0.0f, w1, w2, uvs[k].y * size);
					v.Axis = XMFLOAT4(XMVectorGetX(up), XMVectorGetY(up), XMVectorGetZ(up), 1.0f);
					v.Phase = XMFLOAT4(wi.Phase1, wi.Phase2, seed, 0.0f);
					LeafV.push_back(v);
					LeafNormals.push_back(F3(n));
				}
				for (int k = 1; k + 1 < count; ++k)   // 볼록 다각형 = 부채꼴 삼각형
					AddTri(LeafI, LeafV, base, base + k, base + k + 1, n);
				M.LeafCardCount++;
				return;
			}
			// 휘는 띠: 마디 i 의 방향 = up 을 (right 축으로) droop · v 만큼 돌린 것. 아래로 휘는 쪽 = 법선 반대 / 같은 쪽 중 y 가 줄어드는 쪽
			const float sgn = XMVectorGetY(n) > 0.0f ? -1.0f : 1.0f;
			const uint32_t base = (uint32_t)LeafV.size();
			XMVECTOR pos = p;
			for (int i = 0; i <= segments; ++i)
			{
				const float v = (float)i / segments;
				const float th = droop * v;
				const XMVECTOR d = XMVectorAdd(XMVectorScale(up, cosf(th)), XMVectorScale(n, sgn * sinf(th)));
				const XMVECTOR nn = XMVectorSubtract(XMVectorScale(n, cosf(th)), XMVectorScale(up, sgn * sinf(th)));
				for (int k = 0; k < 2; ++k)
				{
					TreeVertex vx = {};
					vx.Pos = F3(XMVectorAdd(pos, XMVectorScale(right, (k - 0.5f) * size)));
					vx.Normal = F3(nn);
					vx.UV = XMFLOAT2((float)k, v);
					vx.Wind = XMFLOAT4(0.0f, w1, w2, v * size);
					vx.Axis = XMFLOAT4(XMVectorGetX(d), XMVectorGetY(d), XMVectorGetZ(d), 1.0f);
					vx.Phase = XMFLOAT4(wi.Phase1, wi.Phase2, seed, 0.0f);
					LeafV.push_back(vx);
					LeafNormals.push_back(F3(nn));
				}
				if (i < segments)
				{
					// 다음 마디까지: 마디 가운데 방향으로 한 칸
					const float thm = droop * (v + 0.5f / segments);
					const XMVECTOR dm = XMVectorAdd(XMVectorScale(up, cosf(thm)), XMVectorScale(n, sgn * sinf(thm)));
					pos = XMVectorAdd(pos, XMVectorScale(dm, size / segments));
				}
			}
			for (int i = 0; i < segments; ++i)
			{
				const uint32_t a = base + i * 2, b = a + 1, c = a + 2, d = a + 3;
				const XMVECTOR ref = XMLoadFloat3(&LeafNormals[a]);
				AddTri(LeafI, LeafV, a, b, d, ref);
				AddTri(LeafI, LeafV, a, d, c, ref);
			}
			M.LeafCardCount++;
		}

		void AddLeaves(const std::vector<Node>& nodes, int segs, int level, float length, const WindInfo& wi)
		{
			const bool needle = P.Leaf == TreeParams::LeafShape::Needle;
			for (int c = 0; c < P.LeafCards && M.LeafCardCount < kMaxLeafCards; ++c)
			{
				const float tl = std::clamp(P.LeafStart + (1.0f - P.LeafStart) * (c + 0.5f + R.S() * 0.4f) / (float)P.LeafCards, 0.0f, 1.0f);
				const float fi = tl * segs;
				const int i0 = (std::min)((int)fi, segs - 1);
				const float fr = fi - i0;
				const XMVECTOR p = XMVectorLerp(XMLoadFloat3(&nodes[i0].Pos), XMLoadFloat3(&nodes[i0 + 1].Pos), fr);
				const XMVECTOR d = XMVector3Normalize(XMVectorLerp(XMLoadFloat3(&nodes[i0].Dir), XMLoadFloat3(&nodes[i0 + 1].Dir), fr));

				// 카드 위쪽: 가지 방향에서 바깥으로 기울임 (끝 카드는 가지 방향 그대로)
				const float az = R.U() * XM_2PI;
				const XMVECTOR n0 = Perpendicular(d);
				const XMVECTOR side = XMVectorAdd(XMVectorScale(n0, cosf(az)), XMVectorScale(XMVector3Cross(d, n0), sinf(az)));
				const float tilt = (c == P.LeafCards - 1) ? 0.15f : (needle ? 0.55f : 0.95f) + R.S() * 0.25f;
				const XMVECTOR up = XMVector3Normalize(XMVectorAdd(XMVectorAdd(XMVectorScale(d, cosf(tilt)), XMVectorScale(side, sinf(tilt))), XMVectorSet(0, 0.25f, 0, 0)));
				// 카드 면: 대략 위/바깥을 보게 + 무작위 비틀기
				XMVECTOR ref = XMVector3Normalize(XMVectorAdd(side, XMVectorSet(0, 0.7f, 0, 0)));
				XMVECTOR n = XMVectorSubtract(ref, XMVectorScale(up, XMVectorGetX(XMVector3Dot(ref, up))));
				if (XMVectorGetX(XMVector3LengthSq(n)) < 1e-6f)
					n = Perpendicular(up);
				n = XMVector3Normalize(n);
				const float roll = R.S() * 0.8f;
				n = XMVector3Normalize(XMVectorAdd(XMVectorScale(n, cosf(roll)), XMVectorScale(XMVector3Cross(up, n), sinf(roll))));

				float size = P.LeafCardSize * (0.8f + 0.4f * R.U());
				const float seed = LeafSeed(R.U());
				// LOD1: 카드 두 장 중 한 장만, 대신 크게 (난수는 모두 쓴 뒤에 건너뛴다)
				if (Lod > 0)
				{
					if (c & 1)
						continue;
					size *= 1.45f;
				}
				float w1, w2;
				Winds(level, tl, length, wi, w1, w2);
				EmitCard(p, up, n, size, seed, P.CardSegments, P.Droop, w1, w2, wi);
			}
			// 꽃: 마지막 단계 가지의 끝쪽 (잎 다음 난수 — 꽃 없는 종은 모양이 그대로)
			for (int c = 0; c < P.FlowerCards && P.FlowerPetals > 0 && M.LeafCardCount < kMaxLeafCards; ++c)
			{
				const float tl = 0.55f + 0.45f * R.U();
				const float fi = tl * segs;
				const int i0 = (std::min)((int)fi, segs - 1);
				const float fr = fi - i0;
				const XMVECTOR p = XMVectorLerp(XMLoadFloat3(&nodes[i0].Pos), XMLoadFloat3(&nodes[i0 + 1].Pos), fr);
				const XMVECTOR d = XMVector3Normalize(XMVectorLerp(XMLoadFloat3(&nodes[i0].Dir), XMLoadFloat3(&nodes[i0 + 1].Dir), fr));
				const float az = R.U() * XM_2PI;
				const XMVECTOR n0 = Perpendicular(d);
				const XMVECTOR side = XMVectorAdd(XMVectorScale(n0, cosf(az)), XMVectorScale(XMVector3Cross(d, n0), sinf(az)));
				const float size = P.FlowerSize * (0.8f + 0.4f * R.U());
				const float seed = FlowerSeed(R.U());
				if (Lod > 0 && (c & 1))
					continue;
				float w1, w2;
				Winds(level, tl, length, wi, w1, w2);
				AddFlower(XMVectorAdd(p, XMVectorScale(side, size * 0.2f)), XMVector3Normalize(XMVectorAdd(side, XMVectorSet(0, 0.8f, 0, 0))), size, seed, w1, w2, wi);
			}
		}

		// 꽃 한 송이: 가운데 = at, 꽃이 보는 쪽 = face (카드 면 법선)
		void AddFlower(FXMVECTOR at, FXMVECTOR face, float size, float seed, float w1, float w2, const WindInfo& wi)
		{
			const XMVECTOR n = XMVector3Normalize(face);
			XMVECTOR up = Perpendicular(n);
			const XMVECTOR p = XMVectorSubtract(at, XMVectorScale(up, size * 0.5f));
			EmitCard(p, up, n, size, seed, 1, 0.0f, w1, w2, wi);
		}

		// 밑동에서 잎 카드 (Grass · Fern · Rosette · Flower 잎): 황금각으로 돌며, 세로에서 CardSpread 쯤 벌어진다
		void AddBaseCards()
		{
			WindInfo wi;
			for (int c = 0; c < P.LeafCards && M.LeafCardCount < kMaxLeafCards; ++c)
			{
				const float az = c * 2.39996f + R.S() * 0.35f;
				const float el = XMConvertToRadians(P.CardSpread * (0.55f + 0.6f * R.U()));
				const XMVECTOR radial = XMVectorSet(cosf(az), 0.0f, sinf(az), 0.0f);
				const XMVECTOR tangent = XMVectorSet(-sinf(az), 0.0f, cosf(az), 0.0f);
				const XMVECTOR up = XMVector3Normalize(XMVectorAdd(XMVectorScale(radial, sinf(el)), XMVectorSet(0.0f, cosf(el), 0.0f, 0.0f)));
				XMVECTOR n = XMVector3Normalize(XMVector3Cross(up, tangent));
				if (XMVectorGetY(n) < 0.0f)
					n = XMVectorNegate(n);
				float size = P.LeafCardSize * (0.75f + 0.45f * R.U());
				const XMVECTOR base = XMVectorAdd(XMVectorScale(radial, R.U() * 0.06f * size), XMVectorSet(0.0f, -0.01f * size, 0.0f, 0.0f));
				const float seed = LeafSeed(R.U());
				const float droop = P.Droop * (0.7f + 0.6f * R.U());
				wi.Phase1 = R.U();
				wi.Phase2 = R.U();
				if (Lod > 0)
				{
					if (c & 1)
						continue;
					size *= 1.3f;
				}
				EmitCard(base, up, n, size, seed, P.CardSegments, droop, 0.0f, 0.0f, wi);
			}
		}

		// 꽃대 (Flower): 가는 관 + 끝 (이삭이면 위쪽 여러 송이)
		void AddStems()
		{
			for (int s = 0; s < P.Stems; ++s)
			{
				const float az = s * 2.39996f + R.S() * 0.5f;
				const float tilt = XMConvertToRadians(P.CardSpread * 0.35f * R.U());
				const float len = P.Height * (0.75f + 0.3f * R.U());
				const float bend = R.S() * 0.25f;
				WindInfo wi;
				wi.Phase1 = R.U();
				wi.Phase2 = R.U();
				const XMVECTOR radial = XMVectorSet(cosf(az), 0.0f, sinf(az), 0.0f);
				XMVECTOR dir = XMVector3Normalize(XMVectorAdd(XMVectorScale(radial, sinf(tilt)), XMVectorSet(0.0f, cosf(tilt), 0.0f, 0.0f)));
				constexpr int kSegs = 4;
				std::vector<Node> nodes(kSegs + 1);
				XMVECTOR pos = XMVectorScale(radial, 0.02f * P.Height);
				const float r0 = (std::max)(P.Radius, 0.002f);
				for (int i = 0; i <= kSegs; ++i)
				{
					const float t = (float)i / kSegs;
					nodes[i] = { F3(pos), F3(dir), r0 * (1.0f - 0.5f * t), t * len };
					pos = XMVectorAdd(pos, XMVectorScale(dir, len / kSegs));
					dir = XMVector3Normalize(XMVectorAdd(dir, XMVectorScale(radial, bend / kSegs)));
				}
				// 관 (4 변, 가지 1 단계처럼 흔들린다)
				if (Lod == 0 || s % 2 == 0)
				{
					const int sides = Lod == 0 ? 4 : 3;
					const uint32_t base = (uint32_t)M.Vertices.size();
					XMVECTOR n = Perpendicular(XMLoadFloat3(&nodes[0].Dir));
					for (int i = 0; i <= kSegs; ++i)
					{
						const XMVECTOR di = XMLoadFloat3(&nodes[i].Dir);
						n = XMVector3Normalize(XMVectorSubtract(n, XMVectorScale(di, XMVectorGetX(XMVector3Dot(n, di)))));
						const XMVECTOR b = XMVector3Cross(di, n);
						const float t = (float)i / kSegs;
						for (int k = 0; k <= sides; ++k)
						{
							const float a = XM_2PI * k / sides;
							const XMVECTOR off = XMVectorAdd(XMVectorScale(n, cosf(a)), XMVectorScale(b, sinf(a)));
							TreeVertex v = {};
							v.Pos = F3(XMVectorAdd(XMLoadFloat3(&nodes[i].Pos), XMVectorScale(off, nodes[i].Radius)));
							v.Normal = F3(off);
							v.UV = XMFLOAT2((float)k / sides, nodes[i].Dist);
							v.Wind = XMFLOAT4(0.0f, Weight1(t, len), 0.0f, 0.0f);
							v.Axis = XMFLOAT4(nodes[i].Dir.x, nodes[i].Dir.y, nodes[i].Dir.z, 0.9f);
							v.Phase = XMFLOAT4(wi.Phase1, wi.Phase2, 0.0f, r0);
							M.Vertices.push_back(v);
						}
					}
					for (int i = 0; i < kSegs; ++i)
						for (int k = 0; k < sides; ++k)
						{
							const uint32_t a = base + i * (sides + 1) + k, bb = a + 1, c = a + (sides + 1), dd = c + 1;
							const XMVECTOR ref = XMLoadFloat3(&M.Vertices[a].Normal);
							AddTri(M.Indices, M.Vertices, a, c, bb, ref);
							AddTri(M.Indices, M.Vertices, bb, c, dd, ref);
						}
					M.BranchCount++;
				}
				// 꽃: 하나면 끝에, 여럿이면 위쪽 30 % 에 (이삭)
				const int flowers = (std::max)(1, P.FlowerCards);
				for (int f = 0; f < flowers && P.FlowerPetals > 0; ++f)
				{
					const float t = flowers == 1 ? 1.0f : 0.7f + 0.3f * (f + 0.5f) / flowers;
					const float fi = t * kSegs;
					const int i0 = (std::min)((int)fi, kSegs - 1);
					const float fr = fi - i0;
					const XMVECTOR p = XMVectorLerp(XMLoadFloat3(&nodes[i0].Pos), XMLoadFloat3(&nodes[i0 + 1].Pos), fr);
					const XMVECTOR d = XMLoadFloat3(&nodes[i0].Dir);
					const float faz = R.U() * XM_2PI;
					const float size = P.FlowerSize * (flowers == 1 ? (0.85f + 0.3f * R.U()) : (1.1f - 0.4f * t + 0.1f * R.U()));
					const float seed = FlowerSeed(R.U());
					if (Lod > 0 && flowers > 1 && (f & 1))
						continue;
					// 하나면 위 · 바깥을 보고, 이삭이면 꽃대 둘레로
					const XMVECTOR out = XMVectorSet(cosf(faz), 0.0f, sinf(faz), 0.0f);
					const XMVECTOR face = flowers == 1 ? XMVector3Normalize(XMVectorAdd(d, XMVectorScale(radial, 0.6f))) : XMVector3Normalize(XMVectorAdd(out, XMVectorScale(d, 0.4f)));
					AddFlower(XMVectorAdd(p, XMVectorScale(face, size * (flowers == 1 ? 0.05f : 0.25f))), face, size, seed, Weight1(t, len), 0.0f, wi);
				}
			}
		}

		// 야자 · 나무고사리: 줄기 끝에서 잎 (휘는 띠) 이 사방으로 — 아래쪽 (오래된) 잎은 처지고 위쪽은 선다
		void AddCrown(const std::vector<Node>& nodes, int segs, float length)
		{
			const XMVECTOR top = XMLoadFloat3(&nodes[segs].Pos);
			const float maxUp = XMConvertToRadians(P.CardSpread);
			for (int c = 0; c < P.LeafCards && M.LeafCardCount < kMaxLeafCards; ++c)
			{
				const float age = (c + 0.5f) / (std::max)(1, P.LeafCards);   // 0 = 늙은 잎 (아래)
				const float az = c * 2.39996f + R.S() * 0.25f;
				const float elev = XMConvertToRadians(-20.0f) + (maxUp - XMConvertToRadians(-20.0f)) * age + R.S() * 0.12f;
				const XMVECTOR dirH = XMVectorSet(cosf(az), 0.0f, sinf(az), 0.0f);
				const XMVECTOR tangent = XMVectorSet(-sinf(az), 0.0f, cosf(az), 0.0f);
				const XMVECTOR up = XMVector3Normalize(XMVectorAdd(XMVectorScale(dirH, cosf(elev)), XMVectorSet(0.0f, sinf(elev), 0.0f, 0.0f)));
				XMVECTOR n = XMVector3Normalize(XMVector3Cross(up, tangent));
				if (XMVectorGetY(n) < 0.0f)
					n = XMVectorNegate(n);
				float size = P.LeafCardSize * (0.8f + 0.3f * R.U());
				const float seed = LeafSeed(R.U());
				const float droop = P.Droop * (0.8f + 0.5f * (1.0f - age)) * (0.8f + 0.4f * R.U());
				WindInfo wi;
				wi.Phase1 = R.U();
				wi.Phase2 = R.U();
				if (Lod > 0)
				{
					if (c & 1)
						continue;
					size *= 1.15f;
				}
				// 잎 밑동은 줄기 축에서 조금 바깥, 잎은 1 차 가지처럼 펄럭인다
				const XMVECTOR p = XMVectorAdd(top, XMVectorScale(dirH, nodes[segs].Radius * 1.5f));
				EmitCard(p, up, n, size, seed, (std::max)(2, P.CardSegments), droop, (std::min)(1.0f, size / 4.0f), 0.0f, wi);
			}
			// 꽃 · 열매 다발 (잎 아래)
			for (int c = 0; c < P.FlowerCards && P.FlowerPetals > 0; ++c)
			{
				const float az = R.U() * XM_2PI;
				const XMVECTOR out = XMVectorSet(cosf(az), -0.3f, sinf(az), 0.0f);
				const float size = P.FlowerSize * (0.8f + 0.4f * R.U());
				const float seed = FlowerSeed(R.U());
				WindInfo wi;
				AddFlower(XMVectorAdd(top, XMVectorScale(out, nodes[segs].Radius * 2.0f)), out, size, seed, 0.3f, 0.0f, wi);
			}
			(void)length;
		}

		// 잎 법선을 수관 구 쪽으로 굽혀 (SpeedTree 처럼) 덩어리가 부드럽게 빛을 받게, 안쪽 잎은 어둡게(AO)
		void FinishLeaves()
		{
			if (LeafV.empty())
				return;
			XMVECTOR center = XMVectorZero();
			for (const TreeVertex& v : LeafV)
				center = XMVectorAdd(center, XMLoadFloat3(&v.Pos));
			center = XMVectorScale(center, 1.0f / LeafV.size());
			float radius = 0.01f;
			for (const TreeVertex& v : LeafV)
				radius = (std::max)(radius, XMVectorGetX(XMVector3Length(XMVectorSubtract(XMLoadFloat3(&v.Pos), center))));
			for (size_t i = 0; i < LeafV.size(); ++i)
			{
				TreeVertex& v = LeafV[i];
				const XMVECTOR off = XMVectorSubtract(XMLoadFloat3(&v.Pos), center);
				const float dist = XMVectorGetX(XMVector3Length(off));
				const XMVECTOR sphereN = dist > 1e-4f ? XMVectorScale(off, 1.0f / dist) : XMVectorSet(0, 1, 0, 0);
				const XMVECTOR n = XMLoadFloat3(&LeafNormals[i]);
				// 카드 면 법선과 같은 쪽 반구로 맞춘 뒤 섞는다
				const XMVECTOR nn = XMVectorGetX(XMVector3Dot(n, sphereN)) < 0.0f ? XMVectorNegate(n) : n;
				v.Normal = F3(XMVector3Normalize(XMVectorLerp(nn, sphereN, 0.7f)));
				const float depth = std::clamp(dist / radius, 0.0f, 1.0f);
				const float below = std::clamp((XMVectorGetY(off) / radius) * 0.5f + 0.5f, 0.0f, 1.0f);
				v.Axis.w = (0.35f + 0.65f * powf(depth, 1.3f)) * (0.75f + 0.25f * below);
			}
		}
	};
}

namespace TreeGenerator
{
	void Generate(const TreeParams& params, TreeMeshData& out, int lod)
	{
		out = TreeMeshData();
		TreeParams p = params;
		p.Levels = std::clamp(p.Levels, 0, 3);
		p.RadialSegments = std::clamp(p.RadialSegments, 3, 32);
		p.LeafCards = std::clamp(p.LeafCards, 0, 64);
		p.Height = (std::max)(p.Height, 0.1f);
		p.Radius = (std::max)(p.Radius, 0.005f);
		for (auto& l : p.L)
			l.Count = std::clamp(l.Count, 0, 80);

		p.CardSegments = std::clamp(p.CardSegments, 1, 8);
		p.Stems = std::clamp(p.Stems, 0, 32);
		p.FlowerCards = std::clamp(p.FlowerCards, 0, 32);

		Builder b(p, out, lod);
		const float lean = XMConvertToRadians(p.Lean);
		const XMVECTOR dir = XMVectorSet(0.0f, cosf(lean), sinf(lean), 0.0f);
		WindInfo wi;
		switch (p.Shape)
		{
		case TreeParams::Form::Grass:
		case TreeParams::Form::Fern:
		case TreeParams::Form::Rosette:
			b.AddBaseCards();   // 줄기 없이 밑동에서 잎
			break;
		case TreeParams::Form::Flower:
			b.AddBaseCards();   // 밑동 잎 + 꽃대 · 꽃
			b.AddStems();
			break;
		default:
			b.Grow(0, XMVectorZero(), dir, p.Height, p.Radius, p.Radius * std::clamp(p.TipRadius, 0.01f, 1.0f), wi);
			break;
		}
		b.FinishLeaves();

		// 수피 다음에 잎을 이어 붙인다
		out.BarkIndexCount = (uint32_t)out.Indices.size();
		const uint32_t leafBase = (uint32_t)out.Vertices.size();
		out.Vertices.insert(out.Vertices.end(), b.LeafV.begin(), b.LeafV.end());
		for (uint32_t i : b.LeafI)
			out.Indices.push_back(i + leafBase);
		out.LeafIndexCount = (uint32_t)out.Indices.size() - out.BarkIndexCount;

		// 줄기 흔들림 가중치 = (높이 / 나무 높이)², 범위. 밑동 잎 꼴 (풀 · 고사리 …) 은 실제로 가장 높은 곳이 1
		if (p.Shape == TreeParams::Form::Grass || p.Shape == TreeParams::Form::Fern || p.Shape == TreeParams::Form::Rosette || p.Shape == TreeParams::Form::Flower)
		{
			float top = 0.05f;
			for (const TreeVertex& v : out.Vertices)
				top = (std::max)(top, v.Pos.y);
			p.Height = top;
		}
		XMVECTOR mn = XMVectorReplicate(FLT_MAX), mx = XMVectorReplicate(-FLT_MAX);
		for (TreeVertex& v : out.Vertices)
		{
			const float h = std::clamp(v.Pos.y / p.Height, 0.0f, 1.5f);
			v.Wind.x = h * h;
			mn = XMVectorMin(mn, XMLoadFloat3(&v.Pos));
			mx = XMVectorMax(mx, XMLoadFloat3(&v.Pos));
		}
		if (!out.Vertices.empty())
		{
			XMStoreFloat3(&out.BoundsMin, mn);
			XMStoreFloat3(&out.BoundsMax, mx);
		}
	}
}
