#include "pch.h"
#include "DynamicBone.h"
#include "UnityGUI.h"
#include "SceneViewOverlay.h"
#include "SelectionManager.h"
#include "SkinnedMeshRenderer.h"

using namespace AnimatorIK;

namespace
{
	XMVECTOR Load(const Vec3& v) { return XMVectorSet(v.x, v.y, v.z, 0.0f); }
	XMVECTOR Load3(const XMFLOAT3& v) { return XMLoadFloat3(&v); }

	// 선분 ab 에서 p 에 가장 가까운 점
	XMVECTOR ClosestOnSegment(FXMVECTOR p, FXMVECTOR a, FXMVECTOR b)
	{
		const XMVECTOR ab = XMVectorSubtract(b, a);
		const float l2 = XMVectorGetX(XMVector3LengthSq(ab));
		if (l2 < 1e-12f) return a;
		const float t = std::clamp(XMVectorGetX(XMVector3Dot(XMVectorSubtract(p, a), ab)) / l2, 0.0f, 1.0f);
		return XMVectorAdd(a, XMVectorScale(ab, t));
	}

	// 노드 이름 → 번호 (SkeletonAvataData::FindNode 는 엔진 밖으로 내보내지 않는다)
	int FindBone(const SkeletonAvataData& sk, const std::string& name)
	{
		for (int i = 0; i < (int)sk.NodeNames.size(); ++i) if (sk.NodeNames[i] == name) return i;
		return -1;
	}

	SkinnedMeshRenderer* FindRenderer(GameObject* go)
	{
		if (go == nullptr) return nullptr;
		if (auto* r = go->GetComponent<SkinnedMeshRenderer>()) if (r->GetSkeleton()) return r;
		for (GameObject* c : go->GetChildren())
			if (auto* r = FindRenderer(c)) return r;
		return nullptr;
	}
}

DynamicBone::DynamicBone()
{
	m_InspectorTitleName = "Dynamic Bone";
}

void DynamicBone::Resolve(const SkeletonAvataData& skeleton)
{
	m_JointIndex.assign(Chains.size(), {});
	for (size_t c = 0; c < Chains.size(); ++c)
		for (const Joint& j : Chains[c].Joints)
			m_JointIndex[c].push_back(FindBone(skeleton, j.Bone));
	m_ColliderIndex.clear();
	for (const Collider& col : Colliders)
		m_ColliderIndex.push_back(FindBone(skeleton, col.Bone));
	m_State.clear();
	m_ResolvedFor = &skeleton;
	m_ResolvedChains = Chains.size();
	m_ResolvedColliders = Colliders.size();
}

void DynamicBone::ModifyPose(AnimatorPose& pose)
{
	if (m_pGameObject == nullptr || Chains.empty())
		return;
	// 사슬 · 충돌체가 바뀌면 (Inspector · 스크립트) 다시 찾는다
	size_t joints = 0;
	for (const auto& ji : m_JointIndex) joints += ji.size();
	size_t want = 0;
	for (const Chain& ch : Chains) want += ch.Joints.size();
	if (m_ResolvedFor != &pose.Skeleton || m_ResolvedChains != Chains.size() || m_ResolvedColliders != Colliders.size() || joints != want)
		Resolve(pose.Skeleton);

	const float dt = std::clamp(pose.DeltaTime, 0.0f, 1.0f / 20.0f);
	m_Time += dt;
	const float weight = std::clamp(Weight, 0.0f, 1.0f);

	// 크게 순간 이동했으면 (텔레포트 · 첫 프레임) 흔들림을 초기화
	XMFLOAT3 root;
	XMStoreFloat3(&root, pose.ModelToWorld.r[3]);
	if (XMVectorGetX(XMVector3Length(XMVectorSubtract(Load3(root), Load3(m_LastRoot)))) > 1.0f)
		m_State.clear();
	m_LastRoot = root;

	// 충돌체 (월드)
	std::vector<GizmoCollider> cols(Colliders.size());
	for (size_t i = 0; i < Colliders.size(); ++i)
	{
		const int b = m_ColliderIndex[i];
		if (b < 0 || b >= (int)pose.Global.size()) { cols[i].R = -1.0f; continue; }
		const XMMATRIX world = XMMatrixMultiply(XMLoadFloat4x4(&pose.Global[b]), pose.ModelToWorld);
		const float scale = XMVectorGetX(XMVector3Length(world.r[0]));
		XMStoreFloat3(&cols[i].A, XMVector3TransformCoord(XMVectorSetW(Load(Colliders[i].Offset), 1.0f), world));
		XMStoreFloat3(&cols[i].B, XMVector3TransformCoord(XMVectorSetW(Load(Colliders[i].Capsule ? Colliders[i].Tail : Colliders[i].Offset), 1.0f), world));
		cols[i].R = Colliders[i].Radius * scale;
	}
	// 바람: 세기 · 방향이 천천히 흔들린다
	const float gust = 1.0f + WindTurbulence * (0.6f * sinf(m_Time * 1.7f) + 0.4f * sinf(m_Time * 4.3f + 1.3f));
	const XMVECTOR wind = XMVectorScale(Load(Wind), gust);

	if (m_State.size() != Chains.size())
		m_State.assign(Chains.size(), {});
	m_GizmoChains.assign(Chains.size(), {});
	for (size_t c = 0; c < Chains.size(); ++c)
	{
		const Chain& ch = Chains[c];
		const auto& idx = m_JointIndex[c];
		auto& state = m_State[c];
		const bool fresh = state.size() != ch.Joints.size();
		if (fresh) state.assign(ch.Joints.size(), {});
		for (size_t i = 0; i + 1 < ch.Joints.size(); ++i)
		{
			const int b = idx[i], t = idx[i + 1];
			if (b < 0 || t < 0 || b >= (int)pose.Global.size() || t >= (int)pose.Global.size())
				continue;
			const Joint& jt = ch.Joints[i];
			// 지금 (애니메이션 + 앞 본 흔들림) 자세의 머리 · 꼬리
			const XMVECTOR headM = Position(pose, b), tailM = Position(pose, t);
			const XMVECTOR head = XMVector3TransformCoord(headM, pose.ModelToWorld);
			const XMVECTOR tailRest = XMVector3TransformCoord(tailM, pose.ModelToWorld);
			XMVECTOR restDir = XMVectorSubtract(tailRest, head);
			const float len = XMVectorGetX(XMVector3Length(restDir));
			if (len < 1e-5f)
				continue;
			restDir = XMVectorScale(restDir, 1.0f / len);
			TailState& st = state[i];
			if (fresh)
			{
				XMStoreFloat3(&st.Cur, tailRest);
				st.Prev = st.Cur;
			}
			const XMVECTOR cur = Load3(st.Cur), prev = Load3(st.Prev);
			const float drag = std::clamp(jt.Drag * DragScale, 0.0f, 1.0f);
			XMVECTOR next = XMVectorAdd(cur, XMVectorScale(XMVectorSubtract(cur, prev), 1.0f - drag));
			next = XMVectorAdd(next, XMVectorScale(restDir, jt.Stiffness * StiffnessScale * dt));
			next = XMVectorAdd(next, XMVectorScale(XMVector3Normalize(Load(jt.GravityDir)), jt.Gravity * GravityScale * dt));
			next = XMVectorAdd(next, XMVectorScale(wind, dt));
			// 본 길이 유지
			auto keepLength = [&](XMVECTOR p) {
				XMVECTOR d = XMVectorSubtract(p, head);
				const float l = XMVectorGetX(XMVector3Length(d));
				return l < 1e-6f ? tailRest : XMVectorAdd(head, XMVectorScale(d, len / l));
			};
			next = keepLength(next);
			// 충돌: 꼬리 구가 충돌체 밖으로
			for (int ci : ch.Colliders)
			{
				if (ci < 0 || ci >= (int)cols.size() || cols[ci].R < 0.0f) continue;
				const XMVECTOR p = ClosestOnSegment(next, Load3(cols[ci].A), Load3(cols[ci].B));
				const float r = jt.Radius + cols[ci].R;
				XMVECTOR d = XMVectorSubtract(next, p);
				const float l = XMVectorGetX(XMVector3Length(d));
				if (l < r)
				{
					d = l > 1e-6f ? XMVectorScale(d, 1.0f / l) : restDir;
					next = keepLength(XMVectorAdd(p, XMVectorScale(d, r)));
				}
			}
			st.Prev = st.Cur;
			XMStoreFloat3(&st.Cur, next);
			// 본이 새 꼬리를 보게 (모델 공간 회전), Weight 로 섞는다
			XMVECTOR want = XMVector3Normalize(XMVector3TransformNormal(XMVectorSubtract(next, head), pose.WorldToModel));
			const XMVECTOR have = XMVector3Normalize(XMVectorSubtract(tailM, headM));
			if (weight < 0.999f) want = XMVector3Normalize(XMVectorLerp(have, want, weight));
			RotateBone(pose, b, FromTo(have, want));
			if (ShowGizmos)
			{
				XMFLOAT3 h;
				XMStoreFloat3(&h, head);
				if (m_GizmoChains[c].empty()) m_GizmoChains[c].push_back(h);
				m_GizmoChains[c].push_back(st.Cur);
			}
		}
	}
	m_GizmoColliders = cols;
}

int DynamicBone::AddChainsFromBone(const std::string& rootBone, float stiffness, float drag, float gravity)
{
	SkinnedMeshRenderer* r = FindRenderer(m_pGameObject);
	if (r == nullptr) return 0;
	const SkeletonAvataData& sk = *r->GetSkeleton();
	const int root = FindBone(sk, rootBone);
	if (root < 0) return 0;
	int made = 0;
	std::function<void(int)> walk = [&](int start) {
		Chain ch;
		ch.Name = sk.NodeNames[start];
		int cur = start;
		for (int guard = 0; guard < 256 && cur >= 0; ++guard)
		{
			Joint j;
			j.Bone = sk.NodeNames[cur];
			j.Stiffness = stiffness;
			j.Drag = drag;
			j.Gravity = gravity;
			ch.Joints.push_back(j);
			std::vector<int> kids;
			for (int k = 0; k < (int)sk.BoneHierarchy.size(); ++k) if (sk.BoneHierarchy[k] == cur) kids.push_back(k);
			if (kids.empty()) break;
			for (size_t k = 1; k < kids.size(); ++k) walk(kids[k]);
			cur = kids[0];
		}
		if (ch.Joints.size() >= 2)
		{
			for (int ci = 0; ci < (int)Colliders.size(); ++ci) ch.Colliders.push_back(ci);
			Chains.push_back(ch);
			++made;
		}
	};
	walk(root);
	return made;
}

void DynamicBone::OnInspectorGUI()
{
	UnityGUI::Slider("Weight", &Weight, 0.0f, 1.0f);
	UnityGUI::Slider("Stiffness Scale", &StiffnessScale, 0.0f, 4.0f, 0);
	UnityGUI::Slider("Gravity Scale", &GravityScale, 0.0f, 4.0f, 0);
	UnityGUI::Slider("Drag Scale", &DragScale, 0.0f, 2.0f, 0);
	UnityGUI::Vector3("Wind", &Wind.x);
	UnityGUI::Slider("Wind Turbulence", &WindTurbulence, 0.0f, 1.0f, 0);
	UnityGUI::Toggle("Show Gizmos", &ShowGizmos);
	char buf[96];
	snprintf(buf, sizeof(buf), "Chains (%d)", (int)Chains.size());
	if (UnityGUI::FoldoutPlain(buf, 0, true))
	{
		for (size_t c = 0; c < Chains.size(); ++c)
		{
			Chain& ch = Chains[c];
			ImGui::PushID((int)c);
			snprintf(buf, sizeof(buf), "%s  (%d bones, %d colliders)", ch.Name.c_str(), (int)ch.Joints.size(), (int)ch.Colliders.size());
			if (UnityGUI::FoldoutPlain(buf, 1, false) && !ch.Joints.empty())
			{
				// 사슬 전체 값 (첫 본 기준으로 보이고, 바꾸면 모든 본에)
				Joint j0 = ch.Joints[0];
				bool changed = false;
				changed |= UnityGUI::Slider("Stiffness", &j0.Stiffness, 0.0f, 8.0f, 2);
				changed |= UnityGUI::Slider("Drag", &j0.Drag, 0.0f, 1.0f, 2);
				changed |= UnityGUI::Slider("Gravity", &j0.Gravity, 0.0f, 4.0f, 2);
				changed |= UnityGUI::Slider("Radius", &j0.Radius, 0.0f, 0.2f, 2);
				if (changed)
					for (Joint& j : ch.Joints) { j.Stiffness = j0.Stiffness; j.Drag = j0.Drag; j.Gravity = j0.Gravity; j.Radius = j0.Radius; }
				std::string bones;
				for (const Joint& j : ch.Joints) bones += (bones.empty() ? "" : " > ") + j.Bone;
				UnityGUI::HelpBox(bones.c_str(), false, 2);
				if (ImGui::SmallButton("Remove Chain")) { Chains.erase(Chains.begin() + c); ImGui::PopID(); break; }
			}
			ImGui::PopID();
		}
	}
	snprintf(buf, sizeof(buf), "Colliders: %d (sphere / capsule on bones)", (int)Colliders.size());
	UnityGUI::Label(buf);
	static std::string s_Root;
	UnityGUI::TextField("Add From Bone", &s_Root);
	if (ImGui::Button("Add Chains From Bone") && !s_Root.empty())
		AddChainsFromBone(s_Root, 1.0f, 0.4f, 0.0f);
	if (Chains.empty())
		UnityGUI::HelpBox("Type a bone name (for example a hair or skirt root) and press Add Chains From Bone. VRM characters get their Spring Bones here automatically.", false);
}

void DynamicBone::OnDrawGizmos()
{
	if (!ShowGizmos || !Application::IsPlaying() || m_pGameObject == nullptr || !SceneViewOverlay::IsActive() ||
		SelectionManager::GetSelectedObjectType() != SelectionType::GAMEOBJECT || SelectionManager::GetSelectedGameObject() != m_pGameObject)
		return;
	for (const auto& chain : m_GizmoChains)
		for (size_t i = 0; i + 1 < chain.size(); ++i)
			SceneViewOverlay::DrawLine(chain[i], chain[i + 1], IM_COL32(255, 200, 80, 255), 2.0f);
	for (const GizmoCollider& c : m_GizmoColliders)
	{
		if (c.R <= 0.0f) continue;
		// 원 3 개 (구) + 캡슐 축
		for (int axis = 0; axis < 3; ++axis)
			for (int k = 0; k < 24; ++k)
			{
				const float a0 = XM_2PI * k / 24, a1 = XM_2PI * (k + 1) / 24;
				auto pt = [&](float a) {
					XMFLOAT3 p = c.A;
					const float u = cosf(a) * c.R, v = sinf(a) * c.R;
					if (axis == 0) { p.y += u; p.z += v; } else if (axis == 1) { p.x += u; p.z += v; } else { p.x += u; p.y += v; }
					return p;
				};
				SceneViewOverlay::DrawLine(pt(a0), pt(a1), IM_COL32(120, 220, 120, 160), 1.0f);
			}
		SceneViewOverlay::DrawLine(c.A, c.B, IM_COL32(120, 220, 120, 200), 1.5f);
	}
}

GENERATE_COMPONENT_FUNC_TOJSON(DynamicBone)
{
	json j;
	j["type"] = "DynamicBone";
	j["enabled"] = m_Enabled;
	j["weight"] = Weight;
	j["stiffnessScale"] = StiffnessScale;
	j["gravityScale"] = GravityScale;
	j["dragScale"] = DragScale;
	j["wind"] = { Wind.x, Wind.y, Wind.z };
	j["windTurbulence"] = WindTurbulence;
	j["showGizmos"] = ShowGizmos;
	json chains = json::array();
	for (const Chain& ch : Chains)
	{
		json joints = json::array();
		for (const Joint& jt : ch.Joints)
			joints.push_back({ { "bone", jt.Bone }, { "stiffness", jt.Stiffness }, { "drag", jt.Drag }, { "gravity", jt.Gravity },
				{ "gravityDir", { jt.GravityDir.x, jt.GravityDir.y, jt.GravityDir.z } }, { "radius", jt.Radius } });
		chains.push_back({ { "name", ch.Name }, { "joints", joints }, { "colliders", ch.Colliders } });
	}
	j["chains"] = chains;
	json cols = json::array();
	for (const Collider& c : Colliders)
	{
		json cj = { { "bone", c.Bone }, { "offset", { c.Offset.x, c.Offset.y, c.Offset.z } }, { "radius", c.Radius } };
		if (c.Capsule) { cj["capsule"] = true; cj["tail"] = { c.Tail.x, c.Tail.y, c.Tail.z }; }
		cols.push_back(cj);
	}
	j["colliders"] = cols;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(DynamicBone)
{
	auto v3 = [](const json& a, const Vec3& def) { return a.is_array() && a.size() == 3 ? Vec3(a[0].get<float>(), a[1].get<float>(), a[2].get<float>()) : def; };
	m_Enabled = j.value("enabled", true);
	Weight = j.value("weight", 1.0f);
	StiffnessScale = j.value("stiffnessScale", 1.0f);
	GravityScale = j.value("gravityScale", 1.0f);
	DragScale = j.value("dragScale", 1.0f);
	Wind = v3(j.value("wind", json()), Vec3::Zero);
	WindTurbulence = j.value("windTurbulence", 0.3f);
	ShowGizmos = j.value("showGizmos", true);
	Chains.clear();
	for (const json& cj : j.value("chains", json::array()))
	{
		Chain ch;
		ch.Name = cj.value("name", std::string("Chain"));
		for (const json& jt : cj.value("joints", json::array()))
		{
			Joint jo;
			jo.Bone = jt.value("bone", std::string());
			jo.Stiffness = jt.value("stiffness", 1.0f);
			jo.Drag = jt.value("drag", 0.4f);
			jo.Gravity = jt.value("gravity", 0.0f);
			jo.GravityDir = v3(jt.value("gravityDir", json()), Vec3(0, -1, 0));
			jo.Radius = jt.value("radius", 0.02f);
			ch.Joints.push_back(jo);
		}
		ch.Colliders = cj.value("colliders", std::vector<int>());
		Chains.push_back(ch);
	}
	Colliders.clear();
	for (const json& cj : j.value("colliders", json::array()))
	{
		Collider c;
		c.Bone = cj.value("bone", std::string());
		c.Offset = v3(cj.value("offset", json()), Vec3::Zero);
		c.Radius = cj.value("radius", 0.05f);
		c.Capsule = cj.value("capsule", false);
		c.Tail = v3(cj.value("tail", json()), c.Offset);
		Colliders.push_back(c);
	}
	m_ResolvedFor = nullptr;
	m_State.clear();
}
