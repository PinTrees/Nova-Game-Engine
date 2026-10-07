#include "pch.h"
#include "Ragdoll.h"
#include "SkinnedMeshRenderer.h"
#include "HumanoidAvatar.h"
#include "RigidBody.h"
#include "Joint.h"
#include "BoxCollider.h"
#include "SphereCollider.h"
#include "CapsuleCollider.h"
#include "UnityGUI.h"
#include "PhysicsManager.h"
#include "EditorLog.h"

namespace
{
	void CollectSkinned(GameObject* go, std::vector<SkinnedMeshRenderer*>& out)
	{
		if (auto* s = go->GetComponent<SkinnedMeshRenderer>())
			if (s->GetSkeleton())
				out.push_back(s);
		for (GameObject* c : go->GetChildren())
			CollectSkinned(c, out);
	}

	json MatJson(const Matrix& m)
	{
		json a = json::array();
		const float* f = &m._11;
		for (int i = 0; i < 16; ++i)
			a.push_back(f[i]);
		return a;
	}

	Matrix ReadMat(const json& j)
	{
		Matrix m;
		if (j.is_array() && j.size() == 16)
		{
			float* f = &m._11;
			for (int i = 0; i < 16; ++i)
				f[i] = j[i].get<float>();
		}
		return m;
	}

	// 위치 · 회전만 (크기 1)
	Matrix RigidOf(Transform* t) { return Matrix::CreateFromQuaternion(t->GetRotation()) * Matrix::CreateTranslation(t->GetPosition()); }

	bool IsAnimation(Component* c)
	{
		const std::string t = c->GetType();
		return t == "Animator" || t == "AnimationPlayer";
	}

	// 콜라이더를 선분 + 반지름으로 (캡슐 = 그대로, 상자 = 가장 긴 축, 구 = 점) — 쉬는 자세에서 겹치는 바디 쌍 찾기
	struct Segment { Vec3 A, B; float R = 0.0f; };
	bool SegmentOf(GameObject* body, Segment& s)
	{
		const Matrix w = RigidOf(body->GetTransform());
		Vec3 a, b;
		if (auto* c = body->GetComponent<CapsuleCollider>())
		{
			Vec3 e = Vec3::Zero;
			(&e.x)[c->GetDirection()] = 1.0f;
			const float half = (std::max)(0.0f, c->GetHeight() * 0.5f - c->GetRadius());
			a = c->GetCenter() - e * half; b = c->GetCenter() + e * half; s.R = c->GetRadius();
		}
		else if (auto* c = body->GetComponent<BoxCollider>())
		{
			const Vec3 size = c->GetSize();
			const int k = size.x >= size.y && size.x >= size.z ? 0 : (size.y >= size.z ? 1 : 2);
			s.R = 0.5f * (std::min)({ k == 0 ? size.y : size.x, k == 2 ? size.y : size.z });
			Vec3 e = Vec3::Zero;
			(&e.x)[k] = (std::max)(0.0f, (&size.x)[k] * 0.5f - s.R);
			a = c->GetCenter() - e; b = c->GetCenter() + e;
		}
		else if (auto* c = body->GetComponent<SphereCollider>())
		{
			a = b = c->GetCenter(); s.R = c->GetRadius();
		}
		else
			return false;
		s.A = Vec3::Transform(a, w);
		s.B = Vec3::Transform(b, w);
		return true;
	}

	// 두 선분 사이 가장 가까운 거리
	float SegmentDistance(const Segment& p, const Segment& q)
	{
		const Vec3 d1 = p.B - p.A, d2 = q.B - q.A, r = p.A - q.A;
		const float a = d1.Dot(d1), e = d2.Dot(d2), f = d2.Dot(r);
		float s = 0.0f, t = 0.0f;
		if (a < 1e-9f && e < 1e-9f)
			return (p.A - q.A).Length();
		if (a < 1e-9f)
			t = std::clamp(f / e, 0.0f, 1.0f);
		else
		{
			const float c = d1.Dot(r);
			if (e < 1e-9f)
				s = std::clamp(-c / a, 0.0f, 1.0f);
			else
			{
				const float b = d1.Dot(d2), denom = a * e - b * b;
				s = denom > 1e-9f ? std::clamp((b * f - c * e) / denom, 0.0f, 1.0f) : 0.0f;
				t = (b * s + f) / e;
				if (t < 0.0f) { t = 0.0f; s = std::clamp(-c / a, 0.0f, 1.0f); }
				else if (t > 1.0f) { t = 1.0f; s = std::clamp((b - c) / a, 0.0f, 1.0f); }
			}
		}
		return ((p.A + d1 * s) - (q.A + d2 * t)).Length();
	}

	Vec3 Normalized(Vec3 v, const Vec3& fallback)
	{
		if (v.LengthSquared() < 1e-12f)
			return fallback;
		v.Normalize();
		return v;
	}
}

Ragdoll::Ragdoll()
{
	m_InspectorTitleName = "Ragdoll";
}

GameObject* Ragdoll::PelvisBody() const
{
	// Wizard 는 골반 바디를 처음에 둔다 (틀: Y = 머리 쪽, Z = 배 쪽)
	Scene* scene = SceneManager::GetI()->GetCurrentScene();
	return scene && !Parts.empty() ? scene->FindByFileID(Parts[0].Body) : nullptr;
}

bool Ragdoll::IsFaceUp() const
{
	GameObject* pelvis = PelvisBody();
	return pelvis && Vec3::TransformNormal(Vec3(0, 0, 1), RigidOf(pelvis->GetTransform())).y > 0.0f;
}

void Ragdoll::BeginRecover()
{
	// 쓰러진 자세를 월드로 기억한다 (루트를 옮겨도 그 자리에서 섞는다)
	std::vector<SkinnedMeshRenderer*> skinned;
	CollectSkinned(m_pGameObject, skinned);
	m_BlendFrom.clear();
	for (SkinnedMeshRenderer* smr : skinned)
	{
		std::vector<XMFLOAT4X4> g;
		smr->GetNodeGlobals(g);
		const XMMATRIX w = smr->GetGameObject()->GetTransform()->GetWorldMatrix();
		for (XMFLOAT4X4& m : g)
			XMStoreFloat4x4(&m, XMLoadFloat4x4(&m) * w);
		m_BlendFrom.push_back(std::move(g));
	}
	m_BlendLeft = BlendTime;
	GameObject* pelvis = PelvisBody();
	if (!AlignRoot || pelvis == nullptr)
		return;
	// 루트 = 골반 자리 (높이는 그대로), 방향 = 등을 대고 누웠으면 발 쪽 (앉았다 일어선다), 엎드렸으면 머리 쪽 (팔로 밀고 일어선다)
	const Matrix pw = RigidOf(pelvis->GetTransform());
	Vec3 f = Vec3::TransformNormal(Vec3(0, 1, 0), pw) * (IsFaceUp() ? -1.0f : 1.0f);
	f.y = 0.0f;
	Transform* t = m_pGameObject->GetTransform();
	const Vec3 p = pw.Translation();
	t->SetPosition(Vec3(p.x, t->GetPosition().y, p.z));
	if (f.LengthSquared() > 1e-6f)
		t->SetRotation(Quaternion::CreateFromYawPitchRoll(std::atan2(f.x, f.z), 0.0f, 0.0f));
}

void Ragdoll::SetActiveState(bool active)
{
	Scene* scene = SceneManager::GetI()->GetCurrentScene();
	if (!m_Applied && scene)
	{
		// 처음 (Play 시작): 쉬는 자세에서 겹치는 바디 쌍은 서로 부딪히지 않게 (왼 · 오른 넓적다리 등 — Jolt 래그돌과 같은 방법).
		// 겹치면 시작부터 밀어내며 관절과 싸워 튄다. 떨어진 팔 · 몸은 그대로 부딪힌다
		std::vector<GameObject*> bodies;
		for (const Part& p : Parts)
			bodies.push_back(scene->FindByFileID(p.Body));
		for (size_t i = 0; i < bodies.size(); ++i)
			for (size_t k = i + 1; k < bodies.size(); ++k)
			{
				Segment a, b;
				if (bodies[i] && bodies[k] && SegmentOf(bodies[i], a) && SegmentOf(bodies[k], b) && SegmentDistance(a, b) < a.R + b.R + 0.02f)
					PhysicsManager::GetI()->IgnoreCollision(bodies[i], bodies[k], true);
			}
	}
	if (active)
	{
		m_BlendLeft = 0.0f;
		m_BlendFrom.clear();
		// 켤 때 자세를 기억한다: 바디가 없는 노드는 이 로컬로 부모를 따라간다
		std::vector<SkinnedMeshRenderer*> skinned;
		CollectSkinned(m_pGameObject, skinned);
		m_BaseLocal.clear();
		std::vector<XMFLOAT4X4> base;
		if (!skinned.empty() && skinned[0]->GetNodeGlobals(base))
		{
			const SkeletonAvataData& s = *skinned[0]->GetSkeleton();
			m_BaseLocal.resize(base.size());
			for (size_t i = 0; i < base.size(); ++i)
			{
				const int parent = i < s.BoneHierarchy.size() ? s.BoneHierarchy[i] : -1;
				const XMMATRIX g = XMLoadFloat4x4(&base[i]);
				XMStoreFloat4x4(&m_BaseLocal[i], parent >= 0 && parent < (int)i ? g * XMMatrixInverse(nullptr, XMLoadFloat4x4(&base[parent])) : g);
			}
		}
		m_DisabledAnimation.clear();
		for (const auto& c : m_pGameObject->GetComponents())
			if (IsAnimation(c.get()) && c->IsEnabled())
			{
				c->SetEnabled(false);
				m_DisabledAnimation.push_back(c.get());
			}
	}
	else
	{
		// 쓰러져 있다가 꺼짐 = 일어나기 (Play 시작의 꺼짐은 아님)
		if (m_Applied && m_AppliedActive && Application::IsPlaying())
			BeginRecover();
		for (Component* c : m_DisabledAnimation)
			for (const auto& own : m_pGameObject->GetComponents())
				if (own.get() == c)
					c->SetEnabled(true);
		m_DisabledAnimation.clear();
	}
	int switched = 0;
	for (const Part& p : Parts)
		if (GameObject* body = scene ? scene->FindByFileID(p.Body) : nullptr)
			if (RigidBody* rb = body->GetComponent<RigidBody>())
			{
				rb->SetKinematic(!active);
				++switched;
			}
	EditorLog::Write("Ragdoll", "'%s' %s: %d/%zu bodies %s, %zu animation components off", m_pGameObject->GetName().c_str(), active ? "active" : "inactive",
		switched, Parts.size(), active ? "dynamic" : "kinematic", m_DisabledAnimation.size());
	m_Applied = true;
	m_AppliedActive = active;
}

void Ragdoll::LateUpdate()
{
	if (!Application::IsPlaying() || m_pGameObject == nullptr)
		return;
	if (!m_Applied || m_AppliedActive != Active)
		SetActiveState(Active);
	Scene* scene = SceneManager::GetI()->GetCurrentScene();
	if (scene == nullptr)
		return;
	std::vector<SkinnedMeshRenderer*> skinned;
	CollectSkinned(m_pGameObject, skinned);
	if (skinned.empty())
		return;

	if (!Active)
	{
		// 일어나는 중: 쓰러진 자세 (월드) → 이번 프레임 애니메이션 자세 (Animator 가 Update 에서 넣었다) 로 섞는다
		if (m_BlendLeft > 0.0f)
		{
			const float k = BlendTime > 0.0f ? std::clamp(1.0f - m_BlendLeft / BlendTime, 0.0f, 1.0f) : 1.0f;
			const float w = k * k * (3.0f - 2.0f * k);
			m_BlendLeft -= DT;
			for (size_t m = 0; m < skinned.size() && m < m_BlendFrom.size(); ++m)
			{
				std::vector<XMFLOAT4X4> anim;
				if (!skinned[m]->GetNodeGlobals(anim) || anim.size() != m_BlendFrom[m].size())
					continue;
				const XMMATRIX toModel = XMMatrixInverse(nullptr, skinned[m]->GetGameObject()->GetTransform()->GetWorldMatrix());
				for (size_t i = 0; i < anim.size(); ++i)
				{
					XMVECTOR s0, r0, t0, s1, r1, t1;
					if (!XMMatrixDecompose(&s0, &r0, &t0, XMLoadFloat4x4(&m_BlendFrom[m][i]) * toModel) ||
						!XMMatrixDecompose(&s1, &r1, &t1, XMLoadFloat4x4(&anim[i])))
						continue;
					XMStoreFloat4x4(&anim[i], XMMatrixScalingFromVector(XMVectorLerp(s0, s1, w)) * XMMatrixRotationQuaternion(XMQuaternionSlerp(r0, r1, w)) *
						XMMatrixTranslationFromVector(XMVectorLerp(t0, t1, w)));
				}
				skinned[m]->ApplyPose(anim);
			}
			if (m_BlendLeft <= 0.0f)
				m_BlendFrom.clear();
		}
		// 바디가 애니메이션 자세를 따라간다 (키네마틱 → 다음 물리 스텝에 MoveKinematic)
		std::vector<XMFLOAT4X4> g;
		if (!skinned[0]->GetNodeGlobals(g))
			return;
		const SkeletonAvataData& s = *skinned[0]->GetSkeleton();
		const Matrix smrWorld = skinned[0]->GetGameObject()->GetTransform()->GetWorldMatrix();
		for (const Part& p : Parts)
		{
			const int node = s.FindNode(p.Bone);
			GameObject* body = scene->FindByFileID(p.Body);
			if (node < 0 || node >= (int)g.size() || body == nullptr)
				continue;
			Matrix bodyWorld = p.Offset.Invert() * (Matrix(g[node]) * smrWorld);
			Vec3 scale, pos;
			Quaternion rot;
			if (bodyWorld.Decompose(scale, rot, pos))
			{
				body->GetTransform()->SetPosition(pos);
				body->GetTransform()->SetRotation(rot);
			}
		}
		return;
	}

	// 바디 → 뼈대 자세: 바디가 있는 본 = Offset × 바디 월드, 나머지 = 켤 때의 로컬 × 부모
	for (SkinnedMeshRenderer* smr : skinned)
	{
		const SkeletonAvataData& s = *smr->GetSkeleton();
		const size_t n = s.BoneHierarchy.size();
		if (m_BaseLocal.size() != n)
			continue;
		std::vector<int> partOf(n, -1);
		std::vector<Matrix> bodyWorld(Parts.size());
		for (size_t k = 0; k < Parts.size(); ++k)
		{
			const int node = s.FindNode(Parts[k].Bone);
			GameObject* body = scene->FindByFileID(Parts[k].Body);
			if (node < 0 || node >= (int)n || body == nullptr)
				continue;
			partOf[node] = (int)k;
			bodyWorld[k] = RigidOf(body->GetTransform());
		}
		const Matrix toModel = smr->GetGameObject()->GetTransform()->GetWorldMatrix().Invert();
		std::vector<XMFLOAT4X4> g(n);
		for (size_t i = 0; i < n; ++i)
		{
			const int k = partOf[i];
			if (k >= 0)
			{
				XMStoreFloat4x4(&g[i], Parts[k].Offset * bodyWorld[k] * toModel);
				continue;
			}
			const int parent = s.BoneHierarchy[i];
			const XMMATRIX local = XMLoadFloat4x4(&m_BaseLocal[i]);
			XMStoreFloat4x4(&g[i], parent >= 0 && parent < (int)i ? local * XMLoadFloat4x4(&g[parent]) : local);
		}
		smr->ApplyPose(g);
	}
}

void Ragdoll::OnInspectorGUI()
{
	UnityGUI::Toggle("Active", &Active);
	UnityGUI::HelpBox(Active ? "Physics drives the skeleton (Animator off, bodies dynamic)." :
		"Bodies follow the animation (kinematic).", false);
	if (UnityGUI::Float("Blend Time", &BlendTime))
		BlendTime = (std::max)(0.0f, BlendTime);
	UnityGUI::Toggle("Align Root", &AlignRoot);
	Scene* scene = SceneManager::GetI()->GetCurrentScene();
	if (UnityGUI::FoldoutPlain("Bodies", 0, false))
		for (const Part& p : Parts)
		{
			GameObject* body = scene ? scene->FindByFileID(p.Body) : nullptr;
			UnityGUI::ValueLabel(p.Bone.c_str(), body ? body->GetName().c_str() : "(missing)");
		}
}

void Ragdoll::RemapFileIDs(const std::unordered_map<uint64, uint64>& map)
{
	for (Part& p : Parts)
	{
		auto it = map.find(p.Body);
		if (it != map.end())
			p.Body = it->second;
	}
}

GENERATE_COMPONENT_FUNC_TOJSON(Ragdoll)
{
	json j;
	j["type"] = "Ragdoll";
	j["enabled"] = m_Enabled;
	j["active"] = Active;
	j["blendTime"] = BlendTime;
	j["alignRoot"] = AlignRoot;
	json parts = json::array();
	for (const Part& p : Parts)
		parts.push_back({ { "bone", p.Bone }, { "body", p.Body }, { "offset", MatJson(p.Offset) } });
	j["parts"] = parts;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(Ragdoll)
{
	m_Enabled = j.value("enabled", true);
	Active = j.value("active", true);
	BlendTime = j.value("blendTime", 0.5f);
	AlignRoot = j.value("alignRoot", true);
	Parts.clear();
	if (j.contains("parts") && j["parts"].is_array())
		for (const json& p : j["parts"])
		{
			Part part;
			part.Bone = p.value("bone", std::string());
			part.Body = p.value("body", (uint64)0);
			if (p.contains("offset"))
				part.Offset = ReadMat(p["offset"]);
			Parts.push_back(part);
		}
}

// ====================================================================== Ragdoll Wizard
Ragdoll* Ragdoll::Build(GameObject* root, float totalMass, std::string& error)
{
	if (root == nullptr) { error = "no target"; return nullptr; }
	if (root->GetComponent<Ragdoll>()) { error = "'" + root->GetName() + "' already has a Ragdoll"; return nullptr; }
	std::vector<SkinnedMeshRenderer*> skinned;
	CollectSkinned(root, skinned);
	if (skinned.empty()) { error = "no Skinned Mesh Renderer with a skeleton under '" + root->GetName() + "'"; return nullptr; }
	SkinnedMeshRenderer* smr = skinned[0];
	const SkeletonAvataData& s = *smr->GetSkeleton();
	const Humanoid::Avatar& av = Humanoid::Get(s);
	if (av.Generic || !av.Valid) { error = "the skeleton is not a humanoid (Hips, Spine, Head, arms and legs were not all found)"; return nullptr; }
	std::vector<XMFLOAT4X4> g;
	if (!smr->GetNodeGlobals(g)) { error = "could not read the pose"; return nullptr; }
	const Matrix smrWorld = smr->GetGameObject()->GetTransform()->GetWorldMatrix();
	auto boneWorld = [&](int bone) { return Matrix(g[av.Node[bone]]) * smrWorld; };
	auto pos = [&](int bone) { return boneWorld(bone).Translation(); };
	using namespace Humanoid;
	const int middle = av.Node[Chest] >= 0 ? Chest : Spine;   // Unity 의 Middle Spine

	// 몸의 방향 (Unity Wizard 와 같이 뼈 위치로): 위 = 골반 → 머리, 오른쪽 = 왼쪽 → 오른쪽 넓적다리, 앞 = 오른쪽 × 위
	const Vec3 up = Normalized(pos(Head) - pos(Hips), Vec3(0, 1, 0));
	Vec3 right = pos(RightUpperLeg) - pos(LeftUpperLeg);
	right = Normalized(right - up * right.Dot(up), Vec3(1, 0, 0));
	const Vec3 forward = Normalized(right.Cross(up), Vec3(0, 0, 1));

	struct Spec
	{
		const char* Name; int Bone; int Child; int Parent;   // Parent = 이 표의 번호 (-1 = 없음)
		int Shape;                                            // 0 캡슐, 1 상자, 2 구
		float RadiusScale, Density;
		float LowTwist, HighTwist, Swing;                    // 앞으로 굽힘 = + (축 = 뼈 방향 × 앞)
	};
	// Unity Ragdoll Wizard 의 값 (밀도 · 반지름 비율 · 다리 · 척추 · 머리 한계). 팔은 앞으로 드는 쪽을 넓게 (어깨 -30 ~ 70, 팔꿈치 0 ~ 90)
	const Spec specs[] = {
		{ "Pelvis", Hips, middle, -1, 1, 0.0f, 2.5f, 0, 0, 0 },
		{ "Left Hips", LeftUpperLeg, LeftLowerLeg, 0, 0, 0.3f, 1.5f, -20, 70, 30 },
		{ "Left Knee", LeftLowerLeg, LeftFoot, 1, 0, 0.25f, 1.5f, -80, 0, 0 },
		{ "Right Hips", RightUpperLeg, RightLowerLeg, 0, 0, 0.3f, 1.5f, -20, 70, 30 },
		{ "Right Knee", RightLowerLeg, RightFoot, 3, 0, 0.25f, 1.5f, -80, 0, 0 },
		{ "Middle Spine", middle, Head, 0, 1, 0.0f, 2.5f, -20, 20, 10 },
		{ "Left Arm", LeftUpperArm, LeftLowerArm, 5, 0, 0.25f, 1.0f, -30, 70, 50 },
		{ "Left Elbow", LeftLowerArm, LeftHand, 6, 0, 0.2f, 1.0f, 0, 90, 0 },
		{ "Right Arm", RightUpperArm, RightLowerArm, 5, 0, 0.25f, 1.0f, -30, 70, 50 },
		{ "Right Elbow", RightLowerArm, RightHand, 8, 0, 0.2f, 1.0f, 0, 90, 0 },
		{ "Head", Head, -1, 5, 2, 0.0f, 1.0f, -40, 25, 25 },
	};
	constexpr int kCount = (int)(sizeof(specs) / sizeof(specs[0]));
	float densitySum = 0.0f;
	for (const Spec& sp : specs)
		densitySum += sp.Density;
	const float shoulders = (pos(RightUpperArm) - pos(LeftUpperArm)).Length();

	auto ragdoll = std::make_shared<Ragdoll>();
	GameObject* bodies[kCount] = {};
	for (int i = 0; i < kCount; ++i)
	{
		const Spec& sp = specs[i];
		const Vec3 p = pos(sp.Bone);
		// 바디 틀: Y = 뼈 방향 (자식 쪽, 머리 · 골반은 위), Z = 앞, X = Y × Z → 모든 관절의 굽힘 축 = 로컬 X, 흔들기 축 = 로컬 Z
		Vec3 dir = sp.Child >= 0 && av.Node[sp.Child] >= 0 ? pos(sp.Child) - p : up;
		const float length = dir.Length();
		dir = Normalized(dir, up);
		const Vec3 z = Normalized(forward - dir * forward.Dot(dir), dir.Cross(right));
		const Vec3 x = dir.Cross(z);
		const Quaternion rot = Quaternion::CreateFromRotationMatrix(Matrix(x, dir, z));

		GameObject* body = new GameObject(std::string(sp.Name));
		body->SetParent(root, false);
		body->GetTransform()->SetPosition(p);
		body->GetTransform()->SetRotation(rot);
		bodies[i] = body;

		RigidBody* rb = body->AddComponent<RigidBody>();
		rb->SetMass(totalMass * sp.Density / densitySum);
		if (sp.Shape == 0)
		{
			CapsuleCollider* c = body->AddComponent<CapsuleCollider>();
			c->SetDirection(1);
			c->SetRadius(length * sp.RadiusScale);
			c->SetHeight(length);
			c->SetCenter(Vec3(0, length * 0.5f, 0));
		}
		else if (sp.Shape == 1)
		{
			// 골반 = 넓적다리 사이 ~ 척추, 척추 = 어깨 너비 ~ 목
			BoxCollider* c = body->AddComponent<BoxCollider>();
			const Matrix toLocal = (Matrix::CreateFromQuaternion(rot) * Matrix::CreateTranslation(p)).Invert();
			float low = 0.0f, high = (std::max)(length, 0.1f), width = shoulders;
			if (sp.Bone == Hips)
			{
				low = (std::min)(Vec3::Transform(pos(LeftUpperLeg), toLocal).y, Vec3::Transform(pos(RightUpperLeg), toLocal).y) - 0.05f;
				width = (pos(RightUpperLeg) - pos(LeftUpperLeg)).Length() * 2.0f;
			}
			else if (av.Node[Neck] >= 0)
				high = (std::max)(Vec3::Transform(pos(Neck), toLocal).y, 0.1f);
			c->SetSize(Vec3(width, high - low, width * 0.6f));
			c->SetCenter(Vec3(0, (low + high) * 0.5f, 0));
		}
		else
		{
			SphereCollider* c = body->AddComponent<SphereCollider>();
			const float r = shoulders * 0.25f;
			c->SetRadius(r);
			c->SetCenter(Vec3(0, r, 0));
		}
		if (sp.Parent >= 0)
		{
			auto joint = std::make_shared<CharacterJoint>();
			joint->SetConnectedBody(bodies[sp.Parent]->GetFileID());
			joint->SetAxis(Vec3(1, 0, 0));
			joint->SwingAxis = Vec3(0, 0, 1);
			joint->LowTwistLimit.Limit = sp.LowTwist;
			joint->HighTwistLimit.Limit = sp.HighTwist;
			joint->Swing1Limit.Limit = sp.Swing;
			joint->Swing2Limit.Limit = 0.0f;
			body->AddComponent(joint);
		}

		Part part;
		part.Bone = s.NodeNames[av.Node[sp.Bone]];
		part.Body = body->GetFileID();
		part.Offset = boneWorld(sp.Bone) * (Matrix::CreateFromQuaternion(rot) * Matrix::CreateTranslation(p)).Invert();
		ragdoll->Parts.push_back(part);
	}
	root->AddComponent(ragdoll);
	return ragdoll.get();
}
