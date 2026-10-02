// 리깅: Humanoid 뼈대 맞추기 · 자동 가중치 · 흔들림 사슬 · 충돌체 · 포즈 미리보기 (ModelRig.h)
#include "pch.h"
#include "ModelRig.h"
#include "ModelDocument.h"
#include <queue>
#include <sstream>

namespace Modeling
{
	using json = nlohmann::json;

	namespace
	{
		json V(const Vec3& v) { return { roundf(v.x * 1e4f) / 1e4f, roundf(v.y * 1e4f) / 1e4f, roundf(v.z * 1e4f) / 1e4f }; }
		Vec3 ToV(const json& j, const Vec3& def) { return j.is_array() && j.size() == 3 ? Vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>()) : def; }
		float F(const json& a, const char* key, float def) { return a.contains(key) && a[key].is_number() ? a[key].get<float>() : def; }
		int I(const json& a, const char* key, int def) { return a.contains(key) && a[key].is_number() ? a[key].get<int>() : def; }
		std::string S(const json& a, const char* key, const std::string& def = "") { return a.contains(key) && a[key].is_string() ? a[key].get<std::string>() : def; }
		bool B(const json& a, const char* key, bool def) { return a.contains(key) && a[key].is_boolean() ? a[key].get<bool>() : def; }
		std::string Lower(std::string s) { for (char& c : s) c = (char)tolower((unsigned char)c); return s; }

		// --objects [..] | --object 이름 → 이름 목록 (없으면 비어 있음)
		std::vector<std::string> Names(const json& a)
		{
			std::vector<std::string> out;
			if (a.contains("objects") && a["objects"].is_array()) for (const auto& n : a["objects"]) out.push_back(n.get<std::string>());
			else if (a.contains("objects") && a["objects"].is_string())
			{
				std::string s = a["objects"].get<std::string>(), t;
				std::stringstream ss(s);
				while (std::getline(ss, t, ',')) if (!t.empty()) out.push_back(t);
			}
			if (a.contains("object") && a["object"].is_string()) out.push_back(a["object"].get<std::string>());
			return out;
		}

		// 대상 오브젝트: 이름을 주면 그것, 아니면 Object 모드에서 고른 것, 그것도 없으면 보이는 모두
		std::vector<int> Targets(Document& d, const json& a, bool fallbackAll, std::string& error)
		{
			std::vector<int> out;
			const std::vector<std::string> names = Names(a);
			for (const std::string& n : names)
			{
				// 끝이 * 면 그 이름으로 시작하는 모두 (hair.strand 가 Bang, Bang.001 … 로 만든다)
				if (!n.empty() && n.back() == '*')
				{
					const std::string pre = n.substr(0, n.size() - 1);
					bool any = false;
					for (int i = 0; i < (int)d.Objects.size(); ++i)
						if (d.Objects[i].Name.compare(0, pre.size(), pre) == 0 && std::find(out.begin(), out.end(), i) == out.end()) { out.push_back(i); any = true; }
					if (!any) { error = "no object named '" + n + "'"; return {}; }
					continue;
				}
				const int i = d.Find(n);
				if (i < 0) { error = "no object named '" + n + "'"; return {}; }
				if (std::find(out.begin(), out.end(), i) == out.end()) out.push_back(i);
			}
			if (!names.empty()) return out;
			for (int i = 0; i < (int)d.Objects.size(); ++i) if (d.Objects[i].Selected && d.Objects[i].Visible) out.push_back(i);
			if (!out.empty() && !fallbackAll) return out;
			if (fallbackAll)
			{
				out.clear();
				for (int i = 0; i < (int)d.Objects.size(); ++i) if (d.Objects[i].Visible && !d.Objects[i].M.Verts.empty()) out.push_back(i);
			}
			return out;
		}

		// 점 이웃 (변으로)
		std::vector<std::vector<int>> Neighbors(Mesh& m)
		{
			std::vector<std::vector<int>> nb(m.Verts.size());
			for (const Edge& e : m.Edges()) { nb[e.A].push_back(e.B); nb[e.B].push_back(e.A); }
			return nb;
		}

		bool IsSpringBone(const Armature& arm, int b) { return b >= 0 && b < (int)arm.Bones.size() && arm.Bones[b].Spring; }
	}

	// ================================================================== Armature
	int Armature::Find(const std::string& name) const
	{
		for (int i = 0; i < (int)Bones.size(); ++i) if (Bones[i].Name == name) return i;
		return -1;
	}

	int Armature::FindHuman(const std::string& human) const
	{
		for (int i = 0; i < (int)Bones.size(); ++i) if (Bones[i].Human == human) return i;
		return -1;
	}

	bool Armature::HasPose() const
	{
		for (const Bone& b : Bones)
			if (fabsf(b.Pose.x) + fabsf(b.Pose.y) + fabsf(b.Pose.z) > 1e-6f) return true;
		return false;
	}

	void Armature::ClearPose() { for (Bone& b : Bones) { b.Pose = Quaternion::Identity; b.PoseEuler = Vec3(0, 0, 0); } }

	std::vector<Matrix> Armature::SkinMatrices() const
	{
		// S_b = T(-머리) · R_b · T(머리) · S_부모 (행 벡터) — 부모가 먼저 계산돼 있어야 한다
		std::vector<Matrix> s(Bones.size(), Matrix::Identity);
		for (int i = 0; i < (int)Bones.size(); ++i)
		{
			const Bone& b = Bones[i];
			Matrix local = Matrix::CreateTranslation(-b.Head) * Matrix::CreateFromQuaternion(b.Pose) * Matrix::CreateTranslation(b.Head);
			s[i] = b.Parent >= 0 && b.Parent < i ? local * s[b.Parent] : local;
		}
		return s;
	}

	void Armature::PosedSegment(int bone, const std::vector<Matrix>& skin, Vec3& head, Vec3& tail) const
	{
		const Bone& b = Bones[bone];
		head = Vec3::Transform(b.Head, skin[bone]);
		tail = Vec3::Transform(b.Tail, skin[bone]);
	}

	void Armature::Sort()
	{
		// 부모가 앞에: 깊이 우선 (원래 순서 유지)
		std::vector<int> order;
		std::vector<uint8_t> done(Bones.size(), 0);
		std::function<void(int)> visit = [&](int i) {
			if (done[i]) return;
			done[i] = 1;
			if (Bones[i].Parent >= 0 && Bones[i].Parent < (int)Bones.size()) visit(Bones[i].Parent);
			order.push_back(i);
		};
		for (int i = 0; i < (int)Bones.size(); ++i) visit(i);
		std::vector<int> newIndex(Bones.size());
		for (int k = 0; k < (int)order.size(); ++k) newIndex[order[k]] = k;
		std::vector<Bone> sorted;
		for (int i : order)
		{
			Bone b = Bones[i];
			if (b.Parent >= 0) b.Parent = newIndex[b.Parent];
			sorted.push_back(b);
		}
		Bones = sorted;
		for (Collider& c : Colliders) if (c.Bone >= 0) c.Bone = newIndex[c.Bone];
	}

	void Armature::Remove(int bone)
	{
		if (bone < 0 || bone >= (int)Bones.size()) return;
		const int parent = Bones[bone].Parent;
		for (Bone& b : Bones)
			if (b.Parent == bone) b.Parent = parent;
		Bones.erase(Bones.begin() + bone);
		for (Bone& b : Bones) if (b.Parent > bone) --b.Parent;
		Colliders.erase(std::remove_if(Colliders.begin(), Colliders.end(), [&](const Collider& c) { return c.Bone == bone; }), Colliders.end());
		for (Collider& c : Colliders) if (c.Bone > bone) --c.Bone;
	}

	json Armature::ToJson(bool withPose) const
	{
		json bones = json::array();
		for (const Bone& b : Bones)
		{
			json j = { { "name", b.Name }, { "parent", b.Parent }, { "head", { b.Head.x, b.Head.y, b.Head.z } }, { "tail", { b.Tail.x, b.Tail.y, b.Tail.z } } };
			if (!b.Human.empty()) j["human"] = b.Human;
			if (!b.Deform) j["deform"] = false;
			if (b.Spring)
				j["spring"] = { { "stiffness", b.Stiffness }, { "drag", b.Drag }, { "gravity", b.Gravity }, { "radius", b.HitRadius }, { "chain", b.Chain } };
			if (withPose) { j["pose"] = { b.Pose.x, b.Pose.y, b.Pose.z, b.Pose.w }; j["poseEuler"] = { b.PoseEuler.x, b.PoseEuler.y, b.PoseEuler.z }; }
			bones.push_back(j);
		}
		json cols = json::array();
		for (const Collider& c : Colliders)
		{
			json j = { { "bone", c.Bone }, { "offset", { c.Offset.x, c.Offset.y, c.Offset.z } }, { "radius", c.Radius } };
			if (c.Capsule) j["tail"] = { c.Tail.x, c.Tail.y, c.Tail.z };
			cols.push_back(j);
		}
		return { { "bones", bones }, { "colliders", cols } };
	}

	void Armature::FromJson(const json& j)
	{
		Bones.clear();
		Colliders.clear();
		if (!j.is_object()) return;
		for (const json& bj : j.value("bones", json::array()))
		{
			Bone b;
			b.Name = bj.value("name", std::string("Bone"));
			b.Parent = bj.value("parent", -1);
			b.Head = ToV(bj.value("head", json()), b.Head);
			b.Tail = ToV(bj.value("tail", json()), b.Tail);
			b.Human = bj.value("human", std::string());
			b.Deform = bj.value("deform", true);
			if (bj.contains("spring"))
			{
				const json& s = bj["spring"];
				b.Spring = true;
				b.Stiffness = s.value("stiffness", b.Stiffness);
				b.Drag = s.value("drag", b.Drag);
				b.Gravity = s.value("gravity", b.Gravity);
				b.HitRadius = s.value("radius", b.HitRadius);
				b.Chain = s.value("chain", std::string());
			}
			if (bj.contains("pose") && bj["pose"].size() == 4)
				b.Pose = Quaternion(bj["pose"][0].get<float>(), bj["pose"][1].get<float>(), bj["pose"][2].get<float>(), bj["pose"][3].get<float>());
			b.PoseEuler = ToV(bj.value("poseEuler", json()), Vec3(0, 0, 0));
			Bones.push_back(b);
		}
		for (const json& cj : j.value("colliders", json::array()))
		{
			Collider c;
			c.Bone = cj.value("bone", -1);
			c.Offset = ToV(cj.value("offset", json()), c.Offset);
			c.Radius = cj.value("radius", c.Radius);
			c.Capsule = cj.contains("tail");
			if (c.Capsule) c.Tail = ToV(cj["tail"], c.Tail);
			Colliders.push_back(c);
		}
	}

	// ================================================================== 도우미
	float SegmentDistance(const Vec3& p, const Vec3& a, const Vec3& b, float* tOut)
	{
		const Vec3 ab = b - a;
		const float len2 = ab.LengthSquared();
		float t = len2 > 1e-12f ? std::clamp((p - a).Dot(ab) / len2, 0.0f, 1.0f) : 0.0f;
		if (tOut) *tOut = t;
		return (p - (a + ab * t)).Length();
	}

	int Islands(const Mesh& m, std::vector<int>& islandOf)
	{
		std::vector<int> parent(m.Verts.size());
		for (int i = 0; i < (int)parent.size(); ++i) parent[i] = i;
		std::function<int(int)> find = [&](int x) { while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; } return x; };
		for (const Face& f : m.Faces)
			for (size_t k = 1; k < f.V.size(); ++k)
			{
				const int a = find(f.V[0]), b = find(f.V[k]);
				if (a != b) parent[a] = b;
			}
		islandOf.assign(m.Verts.size(), -1);
		std::map<int, int> ids;
		for (int i = 0; i < (int)m.Verts.size(); ++i)
		{
			const int r = find(i);
			auto it = ids.find(r);
			if (it == ids.end()) it = ids.emplace(r, (int)ids.size()).first;
			islandOf[i] = it->second;
		}
		return (int)ids.size();
	}

	std::vector<int> GroupToBone(const Armature& arm, const Mesh& mesh)
	{
		std::vector<int> g2b(mesh.Groups.size(), -1);
		for (int g = 0; g < (int)mesh.Groups.size(); ++g) g2b[g] = arm.Find(mesh.Groups[g]);
		return g2b;
	}

	void VertexBones(const Armature& arm, const std::vector<int>& g2b, const Vert& v, const Vec3& worldPos, bool nearest, std::vector<std::pair<int, float>>& out)
	{
		out.clear();
		for (const auto& [g, w] : v.W)
		{
			if (g < 0 || g >= (int)g2b.size() || w <= 0.0f) continue;
			const int b = g2b[g];
			if (b < 0 || !arm.Bones[b].Deform) continue;
			out.push_back({ b, w });
		}
		std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
		if (out.size() > 4) out.resize(4);
		float sum = 0.0f;
		for (const auto& e : out) sum += e.second;
		if (sum > 1e-6f) { for (auto& e : out) e.second /= sum; return; }
		out.clear();
		if (!nearest) return;
		int best = -1;
		float bestD = FLT_MAX;
		for (int b = 0; b < (int)arm.Bones.size(); ++b)
		{
			if (!arm.Bones[b].Deform) continue;
			const float d = SegmentDistance(worldPos, arm.Bones[b].Head, arm.Bones[b].Tail);
			if (d < bestD) { bestD = d; best = b; }
		}
		if (best >= 0) out.push_back({ best, 1.0f });
	}

	void PoseMesh(const Armature& arm, const std::vector<Matrix>& skin, const Matrix& objectWorld, Mesh& mesh)
	{
		const std::vector<int> g2b = GroupToBone(arm, mesh);
		const Matrix inv = objectWorld.Invert();
		std::vector<std::pair<int, float>> bw;
		for (Vert& v : mesh.Verts)
		{
			const Vec3 p = Vec3::Transform(v.P, objectWorld);
			VertexBones(arm, g2b, v, p, false, bw);
			if (bw.empty()) continue;
			Vec3 q(0, 0, 0);
			for (const auto& [b, w] : bw) q += Vec3::Transform(p, skin[b]) * w;
			v.P = Vec3::Transform(q, inv);
		}
		mesh.Touch();
	}

	std::string VrmHumanName(const std::string& unity)
	{
		std::string s = unity;
		if (!s.empty()) s[0] = (char)tolower((unsigned char)s[0]);
		return s;
	}

	// ================================================================== Humanoid 맞추기
	namespace
	{
		enum class PartKind { None, Head, Torso, Arm, Hand, Leg, Foot };

		PartKind Classify(const std::string& rawName)
		{
			const std::string n = Lower(rawName);
			auto has = [&](const char* k) { return n.find(k) != std::string::npos; };
			if (has("hair") || has("bang") || has("ahoge") || has("tail") || has("eye") || has("brow") || has("skirt") || has("ribbon")) return PartKind::None;
			if (has("hand") || has("palm") || has("glove") || has("finger")) return PartKind::Hand;
			if (has("arm") || has("sleeve")) return PartKind::Arm;
			if (has("foot") || has("feet") || has("shoe") || has("boot")) return PartKind::Foot;
			if (has("leg") || has("thigh") || has("shin") || has("calf") || has("stocking") || has("sock")) return PartKind::Leg;
			if (has("head") || has("face")) return PartKind::Head;
			if (has("body") || has("torso") || has("chest") || has("spine") || has("trunk") || has("pelvis") || has("hip") || has("dress") || has("shirt") || has("cloth")) return PartKind::Torso;
			return PartKind::None;
		}

		struct PointSet
		{
			std::vector<Vec3> P;
			bool Empty() const { return P.empty(); }
			Vec3 Min() const { Vec3 m(FLT_MAX, FLT_MAX, FLT_MAX); for (const Vec3& p : P) m = Vec3::Min(m, p); return m; }
			Vec3 Max() const { Vec3 m(-FLT_MAX, -FLT_MAX, -FLT_MAX); for (const Vec3& p : P) m = Vec3::Max(m, p); return m; }
			Vec3 Centroid() const { Vec3 c(0, 0, 0); for (const Vec3& p : P) c += p; return P.empty() ? c : c / (float)P.size(); }
			// y 가 [lo, hi] 인 점들의 중심 (없으면 def)
			Vec3 BandCentroid(float lo, float hi, const Vec3& def) const
			{
				Vec3 c(0, 0, 0);
				int n = 0;
				for (const Vec3& p : P) if (p.y >= lo && p.y <= hi) { c += p; ++n; }
				return n ? c / (float)n : def;
			}
		};

		// 주축 (공분산의 가장 큰 고유벡터, 거듭제곱법)
		Vec3 PrincipalAxis(const PointSet& s)
		{
			const Vec3 c = s.Centroid();
			float xx = 0, xy = 0, xz = 0, yy = 0, yz = 0, zz = 0;
			for (const Vec3& p : s.P)
			{
				const Vec3 d = p - c;
				xx += d.x * d.x; xy += d.x * d.y; xz += d.x * d.z; yy += d.y * d.y; yz += d.y * d.z; zz += d.z * d.z;
			}
			Vec3 v(0.3f, 1.0f, 0.2f);
			for (int i = 0; i < 32; ++i)
			{
				Vec3 n(xx * v.x + xy * v.y + xz * v.z, xy * v.x + yy * v.y + yz * v.z, xz * v.x + yz * v.y + zz * v.z);
				if (n.LengthSquared() < 1e-20f) break;
				n.Normalize();
				v = n;
			}
			return v;
		}

		// 축을 따라 양 끝 (끝 쪽 frac 비율 점의 중심)
		void AxisEnds(const PointSet& s, const Vec3& axis, float frac, Vec3& e0, Vec3& e1, float& len)
		{
			float lo = FLT_MAX, hi = -FLT_MAX;
			for (const Vec3& p : s.P) { const float t = p.Dot(axis); lo = (std::min)(lo, t); hi = (std::max)(hi, t); }
			len = hi - lo;
			Vec3 a(0, 0, 0), b(0, 0, 0);
			int na = 0, nb = 0;
			for (const Vec3& p : s.P)
			{
				const float t = p.Dot(axis);
				if (t <= lo + len * frac) { a += p; ++na; }
				if (t >= hi - len * frac) { b += p; ++nb; }
			}
			e0 = na ? a / (float)na : s.Centroid();
			e1 = nb ? b / (float)nb : s.Centroid();
		}

		struct Template { float Hips, Neck, Head, Shoulder, Knee, Ankle, LegX, ArmX, Hand; };
		const Template kAdult = { 0.53f, 0.83f, 0.875f, 0.815f, 0.28f, 0.045f, 0.055f, 0.10f, 0.42f };
		const Template kChibi = { 0.30f, 0.56f, 0.60f, 0.53f, 0.15f, 0.035f, 0.07f, 0.13f, 0.30f };
	}

	bool FitHumanoid(Document& d, const json& args, json& report, std::string& error)
	{
		// ---- 부위 모으기 (월드 점)
		PointSet all, head, torso, arm[2], hand[2], leg[2], foot[2];   // [0] = Left (-X), [1] = Right (+X)
		json found = json::object();
		auto addPoint = [&](PartKind k, const Vec3& p) {
			const int side = p.x < 0.0f ? 0 : 1;
			switch (k)
			{
			case PartKind::Head: head.P.push_back(p); break;
			case PartKind::Torso: torso.P.push_back(p); break;
			case PartKind::Arm: arm[side].P.push_back(p); break;
			case PartKind::Hand: hand[side].P.push_back(p); break;
			case PartKind::Leg: leg[side].P.push_back(p); break;
			case PartKind::Foot: foot[side].P.push_back(p); break;
			default: break;
			}
		};
		for (Object& o : d.Objects)
		{
			if (!o.Visible || o.M.Verts.empty()) continue;
			const Mesh& m = o.Evaluated(d.Revision);
			const Matrix w = o.World();
			// 그룹 이름이 부위면 그 점 (가중치 > 0.5), 남은 점은 오브젝트 이름으로
			std::vector<PartKind> groupKind(m.Groups.size(), PartKind::None);
			for (int g = 0; g < (int)m.Groups.size(); ++g) groupKind[g] = Classify(m.Groups[g]);
			const PartKind objKind = Classify(o.Name);
			if (objKind != PartKind::None) found[o.Name] = (int)objKind;
			for (const Vert& v : m.Verts)
			{
				const Vec3 p = Vec3::Transform(v.P, w);
				all.P.push_back(p);
				PartKind k = PartKind::None;
				for (const auto& [g, wt] : v.W)
					if (wt > 0.5f && g >= 0 && g < (int)groupKind.size() && groupKind[g] != PartKind::None) { k = groupKind[g]; break; }
				if (k == PartKind::None) k = objKind;
				addPoint(k, p);
			}
			for (int g = 0; g < (int)m.Groups.size(); ++g) if (groupKind[g] != PartKind::None) found[o.Name + ":" + m.Groups[g]] = (int)groupKind[g];
		}
		if (all.Empty()) { error = "no visible mesh to fit a skeleton to"; return false; }
		const Vec3 mn = all.Min(), mx = all.Max();
		const float floorY = mn.y, height = (std::max)(0.05f, mx.y - mn.y);
		std::string style = S(args, "style", "auto");
		if (style == "auto") style = height < 1.4f ? "chibi" : "adult";
		const Template& T = style == "chibi" ? kChibi : kAdult;
		const float cz = torso.Empty() ? all.Centroid().z : torso.Centroid().z;
		auto Y = [&](float f) { return floorY + f * height; };

		std::map<std::string, Vec3> H;     // 본 머리
		std::map<std::string, Vec3> ends;  // 끝 본 꼬리 (Head, Hand, Toes)

		// ---- 몸통 · 목 · 머리
		float neckY = Y(T.Neck), hipsY = Y(T.Hips);
		float neckZ = cz;
		if (!torso.Empty())
		{
			const Vec3 tmn = torso.Min(), tmx = torso.Max();
			neckY = tmx.y;
			hipsY = tmn.y + (tmx.y - tmn.y) * 0.12f;
			neckZ = torso.BandCentroid(tmx.y - (tmx.y - tmn.y) * 0.1f, tmx.y, Vec3(0, 0, cz)).z;
		}
		Vec3 headHead(0, Y(T.Head), cz), headTop(0, mx.y, cz);
		if (!head.Empty())
		{
			const Vec3 hmn = head.Min(), hmx = head.Max(), hc = head.Centroid();
			if (torso.Empty() || hmn.y < neckY) neckY = (std::min)(neckY, hmn.y + (hmx.y - hmn.y) * 0.15f);
			headHead = Vec3(0, (std::max)(neckY + height * 0.02f, hmn.y + (hmx.y - hmn.y) * 0.12f), hc.z);
			headTop = Vec3(0, hmx.y, hc.z);
		}
		else if (headHead.y <= neckY) headHead.y = neckY + height * 0.03f;

		// ---- 다리 (먼저: 엉덩이 높이)
		const char* sideName[2] = { "Left", "Right" };
		Vec3 legRoot[2], ankle[2], knee[2], toes[2], toeEnd[2];
		for (int s = 0; s < 2; ++s)
		{
			const float sx = s == 0 ? -1.0f : 1.0f;
			if (!leg[s].Empty())
			{
				const Vec3 lmn = leg[s].Min(), lmx = leg[s].Max();
				const float lh = lmx.y - lmn.y;
				const Vec3 top = leg[s].BandCentroid(lmx.y - lh * 0.08f, lmx.y, leg[s].Centroid());
				legRoot[s] = Vec3(top.x, lmx.y - lh * 0.06f, top.z);
				if (!foot[s].Empty())
				{
					const Vec3 fmn = foot[s].Min(), fmx = foot[s].Max();
					const Vec3 ft = foot[s].BandCentroid(fmx.y - (fmx.y - fmn.y) * 0.3f, fmx.y, foot[s].Centroid());
					ankle[s] = Vec3(ft.x, fmn.y + (fmx.y - fmn.y) * 0.6f, ft.z);
				}
				else
				{
					const Vec3 bot = leg[s].BandCentroid(lmn.y, lmn.y + lh * 0.1f, leg[s].Centroid());
					ankle[s] = Vec3(bot.x, lmn.y + lh * 0.08f, bot.z);
				}
			}
			else
			{
				legRoot[s] = Vec3(sx * T.LegX * height, hipsY - height * 0.03f, cz);
				ankle[s] = Vec3(sx * T.LegX * height, Y(T.Ankle), cz);
			}
			knee[s] = (legRoot[s] + ankle[s]) * 0.5f;
			{
				PointSet both = leg[s];
				both.P.insert(both.P.end(), foot[s].P.begin(), foot[s].P.end());
				if (!both.Empty())
				{
					const float band = (std::max)(0.01f, (legRoot[s].y - ankle[s].y) * 0.08f);
					const Vec3 kc = both.BandCentroid(knee[s].y - band, knee[s].y + band, knee[s]);
					knee[s] = Vec3(kc.x, knee[s].y, kc.z);
				}
				// 발끝: 발목 아래 점 중 가장 앞 (+Z)
				float frontZ = -FLT_MAX;
				for (const Vec3& p : both.P) if (p.y < ankle[s].y) frontZ = (std::max)(frontZ, p.z);
				for (const Vec3& p : all.P) if (p.y < ankle[s].y && fabsf(p.x - ankle[s].x) < height * 0.06f) frontZ = (std::max)(frontZ, p.z);
				if (frontZ < ankle[s].z + height * 0.02f) frontZ = ankle[s].z + height * 0.06f;
				const float footY = floorY + (ankle[s].y - floorY) * 0.3f;
				toes[s] = Vec3(ankle[s].x, footY, ankle[s].z + (frontZ - ankle[s].z) * 0.6f);
				toeEnd[s] = Vec3(ankle[s].x, footY, frontZ);
			}
		}
		hipsY = (std::max)(hipsY, (legRoot[0].y + legRoot[1].y) * 0.5f + height * 0.02f);
		if (hipsY >= neckY - height * 0.1f) hipsY = neckY - (neckY - floorY) * 0.4f;
		const Vec3 hips(0, hipsY, cz), neck(0, neckY, neckZ);
		H["Hips"] = hips;
		H["Spine"] = Vec3::Lerp(hips, neck, 0.15f);
		H["Chest"] = Vec3::Lerp(hips, neck, 0.55f);
		const bool upperChest = B(args, "upperChest", false);
		if (upperChest) H["UpperChest"] = Vec3::Lerp(hips, neck, 0.78f);
		H["Neck"] = neck;
		H["Head"] = headHead;
		ends["Head"] = headTop;

		// ---- 팔
		float torsoHalf = T.ArmX * height;
		if (!torso.Empty())
		{
			const Vec3 tmn = torso.Min(), tmx = torso.Max();
			float lo = FLT_MAX, hi = -FLT_MAX;
			for (const Vec3& p : torso.P) if (p.y >= tmx.y - (tmx.y - tmn.y) * 0.25f) { lo = (std::min)(lo, p.x); hi = (std::max)(hi, p.x); }
			if (hi > lo) torsoHalf = (hi - lo) * 0.5f;
		}
		const float shoulderY = torso.Empty() ? Y(T.Shoulder) : neckY - (neckY - hipsY) * 0.08f;
		for (int s = 0; s < 2; ++s)
		{
			const float sx = s == 0 ? -1.0f : 1.0f;
			Vec3 root, end;
			if (!arm[s].Empty())
			{
				const Vec3 axis = PrincipalAxis(arm[s]);
				Vec3 e0, e1;
				float len = 0.0f;
				AxisEnds(arm[s], axis, 0.08f, e0, e1, len);
				const Vec3 guess(sx * torsoHalf * 0.5f, shoulderY, neckZ);
				if ((e1 - guess).Length() < (e0 - guess).Length()) std::swap(e0, e1);
				Vec3 dir = e1 - e0;
				dir.Normalize();
				root = e0 + dir * (len * 0.04f);
				end = e1;
			}
			else
			{
				root = Vec3(sx * torsoHalf, shoulderY, neckZ);
				end = Vec3(sx * (torsoHalf + height * 0.03f), Y(T.Hand), neckZ);   // 팔을 내린 자세
			}
			Vec3 wrist = Vec3::Lerp(root, end, 0.82f), handEnd = end;
			if (!hand[s].Empty())
			{
				Vec3 h0, h1;
				float hl = 0.0f;
				AxisEnds(hand[s], PrincipalAxis(hand[s]), 0.15f, h0, h1, hl);
				if ((h1 - end).Length() < (h0 - end).Length()) std::swap(h0, h1);
				wrist = h0;
				handEnd = h1;
				end = h0;
			}
			const std::string side = sideName[s];
			H[side + "Shoulder"] = Vec3(root.x * 0.3f, root.y, (root.z + neckZ) * 0.5f);
			H[side + "UpperArm"] = root;
			H[side + "LowerArm"] = Vec3::Lerp(root, wrist, 0.5f);
			H[side + "Hand"] = wrist;
			ends[side + "Hand"] = handEnd;
			H[side + "UpperLeg"] = legRoot[s];
			H[side + "LowerLeg"] = knee[s];
			H[side + "Foot"] = ankle[s];
			H[side + "Toes"] = toes[s];
			ends[side + "Toes"] = toeEnd[s];
		}

		// ---- 사용자가 준 위치 (--points {"LeftHand": [x,y,z], ...})
		if (args.contains("points") && args["points"].is_object())
			for (auto it = args["points"].begin(); it != args["points"].end(); ++it)
			{
				if (!it.value().is_array() || it.value().size() != 3) continue;
				const Vec3 p = ToV(it.value(), Vec3(0, 0, 0));
				if (H.count(it.key())) H[it.key()] = p;
				else if (it.key().size() > 4 && it.key().substr(it.key().size() - 4) == ".end") ends[it.key().substr(0, it.key().size() - 4)] = p;
				else { error = "unknown humanoid bone '" + it.key() + "' in --points (Hips, Spine, Chest, Neck, Head, LeftUpperArm, ..., Head.end)"; return false; }
			}

		// ---- 본 만들기: 이름 = Human 이름. 사람 본이 아닌 본 (흔들림 사슬 등) 은 이름으로 부모를 다시 잇는다
		struct Def { const char* Name; const char* Parent; const char* TailFrom; };
		std::vector<Def> defs = {
			{ "Hips", nullptr, "Spine" }, { "Spine", "Hips", "Chest" },
			{ "Chest", "Spine", upperChest ? "UpperChest" : "Neck" } };
		if (upperChest) defs.push_back({ "UpperChest", "Chest", "Neck" });
		const char* chestTop = upperChest ? "UpperChest" : "Chest";
		defs.push_back({ "Neck", chestTop, "Head" });
		defs.push_back({ "Head", "Neck", nullptr });
		static const char* kArm[2][4] = { { "LeftShoulder", "LeftUpperArm", "LeftLowerArm", "LeftHand" }, { "RightShoulder", "RightUpperArm", "RightLowerArm", "RightHand" } };
		static const char* kLeg[2][4] = { { "LeftUpperLeg", "LeftLowerLeg", "LeftFoot", "LeftToes" }, { "RightUpperLeg", "RightLowerLeg", "RightFoot", "RightToes" } };
		for (int s = 0; s < 2; ++s)
		{
			defs.push_back({ kArm[s][0], chestTop, kArm[s][1] });
			defs.push_back({ kArm[s][1], kArm[s][0], kArm[s][2] });
			defs.push_back({ kArm[s][2], kArm[s][1], kArm[s][3] });
			defs.push_back({ kArm[s][3], kArm[s][2], nullptr });
			defs.push_back({ kLeg[s][0], "Hips", kLeg[s][1] });
			defs.push_back({ kLeg[s][1], kLeg[s][0], kLeg[s][2] });
			defs.push_back({ kLeg[s][2], kLeg[s][1], kLeg[s][3] });
			defs.push_back({ kLeg[s][3], kLeg[s][2], nullptr });
		}
		Armature& arm0 = d.Rig;
		// 남길 본 (사람 본이 아닌 것) — 부모 이름
		std::vector<std::pair<Bone, std::string>> keep;
		for (const Bone& b : arm0.Bones)
			if (b.Human.empty() && arm0.Find(b.Name) >= 0)
				keep.push_back({ b, b.Parent >= 0 ? arm0.Bones[b.Parent].Name : std::string() });
		Armature rig;
		for (const Def& def : defs)
		{
			Bone b;
			b.Name = def.Name;
			b.Human = def.Name;
			b.Head = H[def.Name];
			b.Tail = def.TailFrom ? H[def.TailFrom] : ends[def.Name];
			if ((b.Tail - b.Head).Length() < 1e-3f) b.Tail = b.Head + Vec3(0, height * 0.03f, 0);
			b.Parent = def.Parent ? rig.Find(def.Parent) : -1;
			rig.Bones.push_back(b);
		}
		for (auto& [b, parentName] : keep)
		{
			Bone nb = b;
			nb.Parent = -1;
			rig.Bones.push_back(nb);
		}
		for (size_t k = 0; k < keep.size(); ++k)
		{
			const int i = (int)(rig.Bones.size() - keep.size() + k);
			rig.Bones[i].Parent = keep[k].second.empty() ? -1 : rig.Find(keep[k].second);
		}
		rig.Sort();
		d.Rig = rig;
		d.Rig.Colliders.clear();

		json bones = json::array();
		for (const Bone& b : d.Rig.Bones)
			if (!b.Human.empty()) bones.push_back({ { "name", b.Name }, { "head", V(b.Head) }, { "tail", V(b.Tail) } });
		report["style"] = style;
		report["height"] = height;
		report["parts"] = found;
		report["bones"] = bones;
		if (head.Empty() || torso.Empty() || arm[0].Empty() || arm[1].Empty() || leg[0].Empty() || leg[1].Empty())
		{
			json missing = json::array();
			if (head.Empty()) missing.push_back("head");
			if (torso.Empty()) missing.push_back("torso/body");
			if (arm[0].Empty() || arm[1].Empty()) missing.push_back("arms");
			if (leg[0].Empty() || leg[1].Empty()) missing.push_back("legs");
			report["note"] = "parts not found by name (used " + style + " proportions): " + missing.dump() + " — name objects or vertex groups Head / Body / ArmL / ArmR / LegL / LegR, or pass --points, or fix bones with rig.bone";
		}
		return true;
	}

	// ================================================================== 자동 가중치
	bool AutoWeights(Document& d, const json& args, json& report, std::string& error)
	{
		Armature& arm = d.Rig;
		if (arm.Empty()) { error = "no armature (model rig.humanoid first)"; return false; }
		std::vector<int> deform;
		for (int b = 0; b < (int)arm.Bones.size(); ++b) if (arm.Bones[b].Deform && !arm.Bones[b].Spring) deform.push_back(b);
		if (deform.empty()) { error = "no deform bones"; return false; }
		const std::vector<int> targets = Targets(d, args, true, error);
		if (!error.empty()) return false;
		const std::string method = S(args, "method", "auto");
		const int smoothIter = std::clamp(I(args, "smooth", 4), 0, 50);
		const bool keepSpring = B(args, "keepSpring", true);
		const int nb = (int)deform.size();
		json per = json::array();
		for (int oi : targets)
		{
			Object& o = d.Objects[oi];
			Mesh& m = o.M;
			const int n = (int)m.Verts.size();
			if (n == 0) continue;
			const Matrix w = o.World();
			std::vector<Vec3> P(n);
			for (int i = 0; i < n; ++i) P[i] = Vec3::Transform(m.Verts[i].P, w);
			// 이미 흔들림 본 가중치가 있는 점 (사슬) 은 건드리지 않는다
			const std::vector<int> g2b = GroupToBone(arm, m);
			std::vector<uint8_t> locked(n, 0);
			if (keepSpring)
				for (int i = 0; i < n; ++i)
					for (const auto& [g, wt] : m.Verts[i].W)
						if (wt > 0.0f && g >= 0 && g < (int)g2b.size() && IsSpringBone(arm, g2b[g])) { locked[i] = 1; break; }
			std::vector<int> isl;
			const int islands = Islands(m, isl);
			// 점 × 본 거리
			std::vector<float> dist((size_t)n * nb);
			std::vector<int> nearest(n, 0);
			for (int i = 0; i < n; ++i)
			{
				float best = FLT_MAX;
				for (int k = 0; k < nb; ++k)
				{
					const Bone& b = arm.Bones[deform[k]];
					const float dd = SegmentDistance(P[i], b.Head, b.Tail);
					dist[(size_t)i * nb + k] = dd;
					if (dd < best) { best = dd; nearest[i] = k; }
				}
			}
			// 덩어리마다 후보 본 = 덩어리 안을 지나는 본: 본까지 가장 가까운 점 거리 ≤ 덩어리 굵기 (가장 가까운 본 거리의 중앙값) × 1.5
			//  (점이 적은 원기둥 팔처럼 위 · 아래 고리뿐이어도 그 사이 본 (위팔 · 아래팔) 이 후보가 된다)
			std::vector<std::vector<int>> count(islands, std::vector<int>(nb, 0));
			std::vector<int> size(islands, 0);
			{
				std::vector<std::vector<float>> nearD(islands);
				std::vector<std::vector<float>> minD(islands, std::vector<float>(nb, FLT_MAX));
				for (int i = 0; i < n; ++i)
				{
					nearD[isl[i]].push_back(dist[(size_t)i * nb + nearest[i]]);
					for (int k = 0; k < nb; ++k) minD[isl[i]][k] = (std::min)(minD[isl[i]][k], dist[(size_t)i * nb + k]);
					++size[isl[i]];
				}
				for (int is = 0; is < islands; ++is)
				{
					std::vector<float>& v = nearD[is];
					std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
					const float thick = v[v.size() / 2] * 1.5f + 0.005f;
					for (int k = 0; k < nb; ++k) if (minD[is][k] <= thick) count[is][k] = size[is];
				}
				for (int i = 0; i < n; ++i) count[isl[i]][nearest[i]] = (std::max)(count[isl[i]][nearest[i]], size[isl[i]]);
			}
			std::vector<float> W((size_t)n * nb, 0.0f);
			const float eps = 0.004f;
			for (int i = 0; i < n; ++i)
			{
				if (locked[i]) continue;
				const int is = isl[i];
				if (method == "nearest") { W[(size_t)i * nb + nearest[i]] = 1.0f; continue; }
				float sum = 0.0f;
				for (int k = 0; k < nb; ++k)
				{
					if (count[is][k] < (std::max)(1, size[is] / 50)) continue;
					const float dd = dist[(size_t)i * nb + k] + eps;
					const float v = 1.0f / (dd * dd * dd * dd);
					W[(size_t)i * nb + k] = v;
					sum += v;
				}
				if (sum <= 0.0f) { W[(size_t)i * nb + nearest[i]] = 1.0f; continue; }
				for (int k = 0; k < nb; ++k) W[(size_t)i * nb + k] /= sum;
			}
			// 이웃으로 부드럽게 (열 확산 근사) — 덩어리 경계를 넘지 않는다 (변으로만 이어짐)
			if (method != "nearest" && smoothIter > 0)
			{
				const std::vector<std::vector<int>> nbr = Neighbors(m);
				std::vector<float> next(W.size());
				for (int it = 0; it < smoothIter; ++it)
				{
					for (int i = 0; i < n; ++i)
					{
						float* dst = &next[(size_t)i * nb];
						const float* src = &W[(size_t)i * nb];
						if (locked[i] || nbr[i].empty()) { std::copy(src, src + nb, dst); continue; }
						int cnt = 0;
						for (int k = 0; k < nb; ++k) dst[k] = 0.0f;
						for (int j : nbr[i]) { if (locked[j]) continue; const float* sj = &W[(size_t)j * nb]; for (int k = 0; k < nb; ++k) dst[k] += sj[k]; ++cnt; }
						for (int k = 0; k < nb; ++k) dst[k] = cnt ? src[k] * 0.5f + dst[k] * 0.5f / cnt : src[k];
					}
					W.swap(next);
				}
			}
			// 그룹에 쓰기: deform 본 그룹을 지우고 (흔들림 점 제외) 최대 4 개
			std::vector<int> groupOf(nb);
			for (int k = 0; k < nb; ++k) groupOf[k] = m.AddGroup(arm.Bones[deform[k]].Name);
			std::vector<uint8_t> isDeformGroup(m.Groups.size(), 0);
			for (int k = 0; k < nb; ++k) isDeformGroup[groupOf[k]] = 1;
			std::map<std::string, int> used;
			for (int i = 0; i < n; ++i)
			{
				if (locked[i]) continue;
				Vert& v = m.Verts[i];
				v.W.erase(std::remove_if(v.W.begin(), v.W.end(), [&](const auto& e) { return e.first >= 0 && e.first < (int)isDeformGroup.size() && isDeformGroup[e.first]; }), v.W.end());
				std::vector<std::pair<int, float>> top;
				for (int k = 0; k < nb; ++k) if (W[(size_t)i * nb + k] > 0.01f) top.push_back({ k, W[(size_t)i * nb + k] });
				std::sort(top.begin(), top.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
				if (top.size() > 4) top.resize(4);
				float sum = 0.0f;
				for (auto& e : top) sum += e.second;
				for (auto& [k, wt] : top)
				{
					v.W.push_back({ groupOf[k], wt / sum });
					++used[arm.Bones[deform[k]].Name];
				}
			}
			// 쓰지 않는 deform 그룹은 지운다 (목록을 짧게)
			for (int k = nb - 1; k >= 0; --k)
				if (!used.count(arm.Bones[deform[k]].Name) && m.GroupCount(m.FindGroup(arm.Bones[deform[k]].Name)) == 0)
					m.DeleteGroup(m.FindGroup(arm.Bones[deform[k]].Name));
			m.Touch();
			json bonesUsed = json::object();
			for (auto& [name, c] : used) bonesUsed[name] = c;
			per.push_back({ { "object", o.Name }, { "verts", n }, { "islands", islands }, { "bones", bonesUsed } });
		}
		report["objects"] = per;
		return true;
	}

	// ================================================================== 흔들림 사슬
	namespace
	{
		// 점 집합 안의 측지 거리 (변 길이, 다익스트라) — 뿌리 점들에서
		std::vector<float> Geodesic(const std::vector<Vec3>& P, const std::vector<std::vector<int>>& nbr, const std::vector<uint8_t>& in, const std::vector<int>& roots)
		{
			std::vector<float> g(P.size(), FLT_MAX);
			using Item = std::pair<float, int>;
			std::priority_queue<Item, std::vector<Item>, std::greater<Item>> q;
			for (int r : roots) { g[r] = 0.0f; q.push({ 0.0f, r }); }
			while (!q.empty())
			{
				auto [dd, i] = q.top();
				q.pop();
				if (dd > g[i]) continue;
				for (int j : nbr[i])
				{
					if (!in[j]) continue;
					const float nd = dd + (P[i] - P[j]).Length();
					if (nd < g[j]) { g[j] = nd; q.push({ nd, j }); }
				}
			}
			return g;
		}
	}

	bool AddChains(Document& d, const json& args, json& report, std::string& error)
	{
		Armature& arm = d.Rig;
		if (arm.Empty()) { error = "no armature (model rig.humanoid first)"; return false; }
		std::vector<int> targets = Targets(d, args, false, error);
		if (!error.empty()) return false;
		if (targets.empty()) { error = "which objects? --objects Hair,Bang (or select them in object mode)"; return false; }
		const int radial = std::clamp(I(args, "radial", 0), 0, 32);
		const std::string groupName = S(args, "group");
		const std::string parentName = S(args, "parent");
		const int parentArg = parentName.empty() ? -1 : arm.Find(parentName);
		if (!parentName.empty() && parentArg < 0) { error = "no bone '" + parentName + "'"; return false; }
		const int bonesArg = I(args, "bones", 0);
		const float stiffness = F(args, "stiffness", radial ? 0.9f : 0.75f), drag = F(args, "drag", 0.4f), gravity = F(args, "gravity", 0.05f), hitR = F(args, "radius", 0.02f);
		const int minVerts = (std::max)(3, I(args, "minVerts", 6));
		json chains = json::array();
		int chainIndex = 0;
		for (int oi : targets)
		{
			Object& o = d.Objects[oi];
			Mesh& m = o.M;
			const int n = (int)m.Verts.size();
			if (n == 0) continue;
			const Matrix w = o.World();
			std::vector<Vec3> P(n);
			for (int i = 0; i < n; ++i) P[i] = Vec3::Transform(m.Verts[i].P, w);
			// 대상 점: 그룹을 주면 그 그룹 (가중치 > 0.5)
			std::vector<uint8_t> in(n, 1);
			if (!groupName.empty())
			{
				const int g = m.FindGroup(groupName);
				if (g < 0) { error = "no vertex group '" + groupName + "' in " + o.Name; return false; }
				for (int i = 0; i < n; ++i) in[i] = m.Weight(i, g) > 0.5f;
			}
			const std::vector<std::vector<int>> nbr = Neighbors(m);
			// 덩어리 (대상 점끼리 변으로)
			std::vector<int> comp(n, -1);
			int comps = 0;
			for (int i = 0; i < n; ++i)
			{
				if (!in[i] || comp[i] >= 0) continue;
				std::vector<int> stack = { i };
				comp[i] = comps;
				while (!stack.empty())
				{
					const int v = stack.back();
					stack.pop_back();
					for (int j : nbr[v]) if (in[j] && comp[j] < 0) { comp[j] = comps; stack.push_back(j); }
				}
				++comps;
			}
			const std::string baseName = S(args, "name", o.Name);
			// 이 오브젝트의 본 그룹 (모든 본) — 사슬 점은 다른 본 가중치를 지운다
			std::vector<std::vector<std::pair<int, float>>> newW(n);
			std::vector<uint8_t> touched(n, 0);
			for (int c = 0; c < comps; ++c)
			{
				std::vector<int> verts;
				for (int i = 0; i < n; ++i) if (comp[i] == c) verts.push_back(i);
				if ((int)verts.size() < minVerts) continue;
				// 부모 본: 주거나, 덩어리에서 가장 가까운 (흔들림 아닌) 본
				int parent = parentArg;
				if (parent < 0)
				{
					float best = FLT_MAX;
					for (int b = 0; b < (int)arm.Bones.size(); ++b)
					{
						if (arm.Bones[b].Spring) continue;
						for (int i : verts)
						{
							const float dd = SegmentDistance(P[i], arm.Bones[b].Head, arm.Bones[b].Tail);
							if (dd < best) { best = dd; parent = b; }
						}
					}
				}
				if (parent < 0) continue;
				const Bone pb = arm.Bones[parent];
				// 조각: 방사 (치마) = 부모 축 둘레 각도로 radial 개, 아니면 덩어리 하나 = 사슬 하나
				const int pieces = radial > 0 ? radial : 1;
				Vec3 center(0, 0, 0);
				for (int i : verts) center += P[i];
				center /= (float)verts.size();
				std::vector<float> angle(n, 0.0f);
				for (int i : verts) angle[i] = atan2f(P[i].z - center.z, P[i].x - center.x);
				struct ChainInfo { std::vector<int> Bones; std::vector<float> Joint; float Length = 0; };
				std::vector<ChainInfo> made(pieces);
				std::vector<float> param(n, 0.0f);   // 뿌리에서 거리 (사슬 방향)
				for (int pc = 0; pc < pieces; ++pc)
				{
					std::vector<int> pv;
					const float a0 = -3.14159265f + (pc + 0.5f) * 6.2831853f / pieces;
					for (int i : verts)
					{
						if (pieces == 1) { pv.push_back(i); continue; }
						float da = angle[i] - a0;
						while (da > 3.14159265f) da -= 6.2831853f;
						while (da < -3.14159265f) da += 6.2831853f;
						if (fabsf(da) <= 3.14159265f / pieces + 1e-4f) pv.push_back(i);
					}
					if ((int)pv.size() < 3) continue;
					std::vector<float> g(n, FLT_MAX);
					if (pieces == 1)
					{
						// 뿌리 = 부모 본에 가장 가까운 점들, 길이 = 측지 거리
						float dmin = FLT_MAX, dmax = 0.0f;
						std::vector<float> dp(n, 0.0f);
						for (int i : pv) { dp[i] = SegmentDistance(P[i], pb.Head, pb.Tail); dmin = (std::min)(dmin, dp[i]); dmax = (std::max)(dmax, dp[i]); }
						std::vector<int> roots;
						for (int i : pv) if (dp[i] <= dmin + (dmax - dmin) * 0.12f) roots.push_back(i);
						std::vector<uint8_t> inPiece(n, 0);
						for (int i : pv) inPiece[i] = 1;
						g = Geodesic(P, nbr, inPiece, roots);
						for (int i : pv) if (g[i] == FLT_MAX) g[i] = (P[i] - P[roots[0]]).Length();
					}
					else
					{
						// 치마: 위 → 아래 (높이)
						float top = -FLT_MAX;
						for (int i : pv) top = (std::max)(top, P[i].y);
						for (int i : pv) g[i] = top - P[i].y;
					}
					float G = 0.0f;
					for (int i : pv) G = (std::max)(G, g[i]);
					if (G < 1e-3f) continue;
					const int nbones = bonesArg > 0 ? std::clamp(bonesArg, 1, 12) : std::clamp((int)roundf(G / 0.09f), 2, 6);
					// 마디 = 거리 띠의 중심
					std::vector<Vec3> joint(nbones + 1);
					for (int k = 0; k <= nbones; ++k)
					{
						const float at = G * k / nbones, half = G / (2.0f * nbones);
						const float lo = k == 0 ? -1.0f : at - half, hi = k == nbones ? FLT_MAX : (k == 0 ? half * 0.5f : at + half);
						Vec3 sum(0, 0, 0);
						int cnt = 0;
						for (int i : pv) if (g[i] >= (k == nbones ? G - half * 0.6f : lo) && g[i] <= hi) { sum += P[i]; ++cnt; }
						joint[k] = cnt ? sum / (float)cnt : (k ? joint[k - 1] : P[pv[0]]);
					}
					// 본 (이름 = <이름><번호>_<마디>)
					ChainInfo& ci = made[pc];
					ci.Length = G;
					// 사슬 이름 = 오브젝트 이름 (점 → _), 겹치면 _1, _2 …
					std::string base = baseName;
					for (char& ch : base) if (ch == '.' || ch == ' ') ch = '_';
					std::string chainName = base;
					auto taken = [&](const std::string& cn) { for (const Bone& tb : arm.Bones) if (tb.Chain == cn || tb.Name.rfind(cn + "_", 0) == 0) return true; return false; };
					for (int sfx = 1; taken(chainName); ++sfx) chainName = base + "_" + std::to_string(sfx);
					++chainIndex;
					int prev = parent;
					for (int k = 0; k < nbones; ++k)
					{
						Bone b;
						b.Name = chainName + "_" + std::to_string(k);
						while (arm.Find(b.Name) >= 0) b.Name += "_";
						b.Parent = prev;
						b.Head = joint[k];
						b.Tail = joint[k + 1];
						if ((b.Tail - b.Head).Length() < 1e-4f) b.Tail = b.Head + Vec3(0, -0.02f, 0);
						b.Spring = true;
						b.Chain = chainName;
						b.Stiffness = stiffness;
						b.Drag = drag;
						b.Gravity = gravity;
						b.HitRadius = hitR;
						arm.Bones.push_back(b);
						prev = (int)arm.Bones.size() - 1;
						ci.Bones.push_back(prev);
					}
					chains.push_back({ { "name", chainName }, { "object", o.Name }, { "bones", nbones }, { "parent", pb.Name }, { "length", roundf(G * 1000.0f) / 1000.0f }, { "root", V(joint[0]) }, { "tip", V(joint[nbones]) } });
					for (int i : pv) if (pieces == 1 || param[i] == 0.0f) param[i] = g[i];
				}
				// 가중치: 사슬 방향 (마디 사이 선형) × 방사면 이웃 조각과 각도로 섞기
				for (int i : verts)
				{
					std::vector<std::pair<int, float>> acc;
					auto alongChain = [&](const ChainInfo& ci, float scale) {
						const int nbn = (int)ci.Bones.size();
						if (nbn == 0 || ci.Length <= 0.0f) return;
						const float s = (std::max)(0.0f, param[i]) / ci.Length * nbn;   // 0..nbones
						if (s < 0.5f)
						{
							// 뿌리: 부모와 섞어 붙는 곳이 찢어지지 않게
							const float wp = (0.5f - s) * 0.8f;
							acc.push_back({ parent, wp * scale });
							acc.push_back({ ci.Bones[0], (1.0f - wp) * scale });
							return;
						}
						const float u = s - 0.5f;
						const int k = (std::min)((int)u, nbn - 1);
						const float f = std::clamp(u - k, 0.0f, 1.0f);
						const int k1 = (std::min)(k + 1, nbn - 1);
						acc.push_back({ ci.Bones[k], (1.0f - f) * scale });
						if (k1 != k) acc.push_back({ ci.Bones[k1], f * scale });
						else acc.back().second += f * scale;
					};
					if (pieces == 1) alongChain(made[0], 1.0f);
					else
					{
						// 가장 가까운 두 조각 (각도)
						const float step = 6.2831853f / pieces;
						float u = (angle[i] + 3.14159265f) / step - 0.5f;
						int p0 = (int)floorf(u);
						const float f = u - p0;
						p0 = ((p0 % pieces) + pieces) % pieces;
						const int p1 = (p0 + 1) % pieces;
						alongChain(made[p0], 1.0f - f);
						alongChain(made[p1], f);
					}
					float sum = 0.0f;
					for (auto& e : acc) sum += e.second;
					if (sum <= 1e-6f) continue;
					for (auto& e : acc) e.second /= sum;
					newW[i] = acc;
					touched[i] = 1;
				}
			}
			// 그룹에 쓰기: 사슬 점은 모든 본 그룹을 비우고 새 가중치
			std::vector<uint8_t> isBoneGroup(m.Groups.size(), 0);
			for (int g = 0; g < (int)m.Groups.size(); ++g) isBoneGroup[g] = arm.Find(m.Groups[g]) >= 0;
			for (int i = 0; i < n; ++i)
			{
				if (!touched[i]) continue;
				Vert& v = m.Verts[i];
				v.W.erase(std::remove_if(v.W.begin(), v.W.end(), [&](const auto& e) { return e.first >= 0 && e.first < (int)isBoneGroup.size() && isBoneGroup[e.first]; }), v.W.end());
				std::map<int, float> merged;
				for (auto& [b, wt] : newW[i]) merged[b] += wt;
				std::vector<std::pair<int, float>> top(merged.begin(), merged.end());
				std::sort(top.begin(), top.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
				if (top.size() > 4) top.resize(4);
				float sum = 0.0f;
				for (auto& e : top) sum += e.second;
				for (auto& [b, wt] : top) if (wt > 0.0f) v.W.push_back({ m.AddGroup(arm.Bones[b].Name), wt / sum });
			}
			m.Touch();
		}
		arm.Sort();
		if (chains.empty()) { error = "no chain made (islands too small? --minVerts, or pick other objects)"; return false; }
		report["chains"] = chains;
		// 충돌체가 없으면 몸에 맞춰 만든다 (머리카락이 몸을 뚫지 않게)
		if (arm.Colliders.empty() && arm.FindHuman("Head") >= 0)
		{
			json cr;
			std::string ce;
			if (AutoColliders(d, json::object(), cr, ce)) report["colliders"] = cr.value("colliders", json::array()).size();
		}
		return true;
	}

	// ================================================================== 충돌체
	bool AutoColliders(Document& d, const json& args, json& report, std::string& error)
	{
		Armature& arm = d.Rig;
		if (arm.FindHuman("Hips") < 0) { error = "no humanoid armature (model rig.humanoid first)"; return false; }
		// 본마다 그 본이 가장 큰 가중치인 점들의 선분 거리
		std::vector<std::vector<float>> dists(arm.Bones.size());
		std::vector<std::vector<Vec3>> pts(arm.Bones.size());
		for (Object& o : d.Objects)
		{
			if (!o.Visible) continue;
			const Matrix w = o.World();
			const std::vector<int> g2b = GroupToBone(arm, o.M);
			std::vector<std::pair<int, float>> bw;
			for (const Vert& v : o.M.Verts)
			{
				const Vec3 p = Vec3::Transform(v.P, w);
				VertexBones(arm, g2b, v, p, false, bw);
				if (bw.empty() || arm.Bones[bw[0].first].Spring) continue;
				const int b = bw[0].first;
				dists[b].push_back(SegmentDistance(p, arm.Bones[b].Head, arm.Bones[b].Tail));
				pts[b].push_back(p);
			}
		}
		auto median = [](std::vector<float> v, float def) { if (v.empty()) return def; std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end()); return v[v.size() / 2]; };
		const float scale = F(args, "scale", 0.9f);
		arm.Colliders.clear();
		json list = json::array();
		const char* kCapsules[] = { "Spine", "Chest", "UpperChest", "LeftUpperArm", "LeftLowerArm", "RightUpperArm", "RightLowerArm", "LeftUpperLeg", "LeftLowerLeg", "RightUpperLeg", "RightLowerLeg" };
		const int head = arm.FindHuman("Head");
		if (head >= 0)
		{
			const Bone& b = arm.Bones[head];
			const Vec3 c = (b.Head + b.Tail) * 0.5f;
			std::vector<float> r;
			for (const Vec3& p : pts[head]) r.push_back((p - c).Length());
			Collider col;
			col.Bone = head;
			col.Offset = c - b.Head;
			col.Radius = median(r, (b.Tail - b.Head).Length() * 0.5f) * scale;
			arm.Colliders.push_back(col);
			list.push_back({ { "bone", b.Name }, { "radius", col.Radius }, { "shape", "sphere" } });
		}
		for (const char* h : kCapsules)
		{
			const int bi = arm.FindHuman(h);
			if (bi < 0) continue;
			const Bone& b = arm.Bones[bi];
			const float len = (b.Tail - b.Head).Length();
			Collider col;
			col.Bone = bi;
			col.Capsule = true;
			col.Offset = Vec3(0, 0, 0);
			col.Tail = b.Tail - b.Head;
			col.Radius = (std::max)(0.01f, median(dists[bi], len * 0.25f) * scale);
			arm.Colliders.push_back(col);
			list.push_back({ { "bone", b.Name }, { "radius", roundf(col.Radius * 1000.0f) / 1000.0f }, { "shape", "capsule" } });
		}
		report["colliders"] = list;
		return true;
	}
}
