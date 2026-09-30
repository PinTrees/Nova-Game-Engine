#include "pch.h"
#include "TreeGenerator.h"
#include <random>

using json = nlohmann::json;

// ================================================================== 프리셋
static const char* const kPresetNames[] = { "Oak", "Pine", "Birch", "Bush" };

const char* const* TreeParams::PresetNames(int& count)
{
	count = 4;
	return kPresetNames;
}

void TreeParams::ApplyPreset(int preset)
{
	const int seed = Seed;
	*this = TreeParams();
	Seed = seed;
	switch (preset)
	{
	default:
	case 0:   // Oak: 굵은 줄기, 넓게 퍼진 둥근 수관, 넓은 잎
		Height = 9.0f; Radius = 0.34f; TipRadius = 0.3f; Flare = 0.7f; Gnarl = 0.25f; CrownShape = Crown::Spherical;
		Levels = 2;
		L[0] = { 11, 0.32f, 55.0f, 15.0f, 0.58f, 0.2f, 0.12f, 0.1f, 0.45f, 0.3f };
		L[1] = { 7, 0.2f, 45.0f, 15.0f, 0.5f, 0.2f, 0.2f, 0.05f, 0.5f, 0.35f };
		Leaf = LeafShape::Broad; LeafCards = 10; LeavesPerCard = 5; LeafCardSize = 0.9f; LeafStart = 0.2f;
		break;
	case 1:   // Pine: 곧은 줄기가 끝까지, 원뿔 수관, 수평으로 처진 가지, 바늘잎
		Height = 13.0f; Radius = 0.28f; TipRadius = 0.08f; Flare = 0.45f; Gnarl = 0.04f; CrownShape = Crown::Conical;
		Levels = 2;
		L[0] = { 30, 0.18f, 82.0f, 8.0f, 0.34f, 0.12f, 0.35f, 0.0f, 0.35f, 0.08f };
		L[1] = { 8, 0.15f, 52.0f, 10.0f, 0.42f, 0.2f, 0.25f, 0.05f, 0.5f, 0.1f };
		Leaf = LeafShape::Needle; LeafCards = 12; LeavesPerCard = 16; LeafCardSize = 0.75f; LeafStart = 0.05f;
		break;
	case 2:   // Birch: 가는 흰 줄기, 불꽃 모양 수관, 늘어진 잔가지, 작은 타원 잎
		Height = 11.0f; Radius = 0.17f; TipRadius = 0.2f; Flare = 0.25f; Gnarl = 0.12f; CrownShape = Crown::Flame;
		Levels = 2;
		L[0] = { 15, 0.38f, 38.0f, 10.0f, 0.42f, 0.2f, 0.3f, 0.25f, 0.45f, 0.2f };
		L[1] = { 6, 0.2f, 40.0f, 12.0f, 0.55f, 0.2f, 0.65f, 0.0f, 0.5f, 0.3f };
		Leaf = LeafShape::Oval; LeafCards = 8; LeavesPerCard = 6; LeafCardSize = 0.55f; LeafStart = 0.2f;
		break;
	case 3:   // Bush: 밑동에서 바로 갈라지는 덤불
		Height = 1.2f; Radius = 0.06f; TipRadius = 0.5f; Flare = 0.2f; Gnarl = 0.2f; CrownShape = Crown::Hemispherical;
		Levels = 2;
		L[0] = { 9, 0.05f, 48.0f, 15.0f, 1.3f, 0.25f, 0.1f, 0.15f, 0.8f, 0.4f };
		L[1] = { 5, 0.25f, 45.0f, 15.0f, 0.5f, 0.2f, 0.15f, 0.1f, 0.6f, 0.4f };
		Leaf = LeafShape::Broad; LeafCards = 7; LeavesPerCard = 5; LeafCardSize = 0.45f; LeafStart = 0.2f;
		break;
	}
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
			if (level == P.Levels)
				AddLeaves(nodes, segs, level, length, wi);
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
				const XMVECTOR right = XMVector3Normalize(XMVector3Cross(up, n));

				float size = P.LeafCardSize * (0.8f + 0.4f * R.U());
				const float seed = R.U();
				// LOD1: 카드 두 장 중 한 장만, 대신 크게 (난수는 모두 쓴 뒤에 건너뛴다)
				if (Lod > 0)
				{
					if (c & 1)
						continue;
					size *= 1.45f;
				}
				float w1, w2;
				Winds(level, tl, length, wi, w1, w2);

				const XMVECTOR corners[4] = {
					XMVectorSubtract(p, XMVectorScale(right, size * 0.5f)),
					XMVectorAdd(p, XMVectorScale(right, size * 0.5f)),
					XMVectorAdd(XMVectorAdd(p, XMVectorScale(up, size)), XMVectorScale(right, size * 0.5f)),
					XMVectorSubtract(XMVectorAdd(p, XMVectorScale(up, size)), XMVectorScale(right, size * 0.5f)) };
				const XMFLOAT2 uvs[4] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
				const uint32_t base = (uint32_t)LeafV.size();
				for (int k = 0; k < 4; ++k)
				{
					TreeVertex v = {};
					v.Pos = F3(corners[k]);
					v.Normal = F3(n);
					v.UV = uvs[k];
					v.Wind = XMFLOAT4(0.0f, w1, w2, uvs[k].y * size);
					v.Axis = XMFLOAT4(XMVectorGetX(up), XMVectorGetY(up), XMVectorGetZ(up), 1.0f);
					v.Phase = XMFLOAT4(wi.Phase1, wi.Phase2, seed, 0.0f);
					LeafV.push_back(v);
					LeafNormals.push_back(F3(n));
				}
				AddTri(LeafI, LeafV, base, base + 1, base + 2, n);
				AddTri(LeafI, LeafV, base, base + 2, base + 3, n);
				M.LeafCardCount++;
			}
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

		Builder b(p, out, lod);
		const float lean = XMConvertToRadians(p.Lean);
		const XMVECTOR dir = XMVectorSet(0.0f, cosf(lean), sinf(lean), 0.0f);
		WindInfo wi;
		b.Grow(0, XMVectorZero(), dir, p.Height, p.Radius, p.Radius * std::clamp(p.TipRadius, 0.01f, 1.0f), wi);
		b.FinishLeaves();

		// 수피 다음에 잎을 이어 붙인다
		out.BarkIndexCount = (uint32_t)out.Indices.size();
		const uint32_t leafBase = (uint32_t)out.Vertices.size();
		out.Vertices.insert(out.Vertices.end(), b.LeafV.begin(), b.LeafV.end());
		for (uint32_t i : b.LeafI)
			out.Indices.push_back(i + leafBase);
		out.LeafIndexCount = (uint32_t)out.Indices.size() - out.BarkIndexCount;

		// 줄기 흔들림 가중치 = (높이 / 나무 높이)², 범위
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
