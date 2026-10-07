#include "pch.h"
#include "Cloth.h"
#include "MeshFilter.h"
#include "Mesh.h"
#include "SkinnedMesh.h"
#include "SkinnedMeshRenderer.h"
#include "PhysicsManager.h"
#include "PhysicsSettings.h"
#include "UnityGUI.h"
#include "EditorLog.h"

namespace
{
	Vec3 V(const XMFLOAT3& f) { return Vec3(f.x, f.y, f.z); }

	// 서브메시마다 기준 정점 (VertexStart) 을 더한 삼각형 목록 (서브메시가 없으면 인덱스 그대로)
	template <typename M>
	void GlobalIndices(const M& mesh, std::vector<uint32>& out)
	{
		out.clear();
		if (mesh.Subsets.empty())
		{
			for (USHORT i : mesh.Indices) out.push_back(i);
			return;
		}
		for (const MeshGeometry::Subset& s : mesh.Subsets)
			for (uint32 k = s.FaceStart * 3; k < (s.FaceStart + s.FaceCount) * 3 && k < mesh.Indices.size(); ++k)
				out.push_back((uint32)mesh.Indices[k] + s.VertexStart);
		if (out.empty())
			for (USHORT i : mesh.Indices) out.push_back(i);
	}
}

Cloth::Cloth()
{
	m_InspectorTitleName = "Cloth";
}

Cloth::~Cloth()
{
	Release();
}

void Cloth::OnDestroy()
{
	Release();
}

void Cloth::Release()
{
	if (m_Handle != 0)
		PhysicsManager::GetI()->DestroyCloth(m_Handle);
	m_Handle = 0;
	// 원래 메시로 되돌린다
	if (m_Original && m_pGameObject)
		if (MeshFilter* f = m_pGameObject->GetComponent<MeshFilter>(); f && f->GetMesh() == m_Copy)
			f->SetMesh(m_Original, f->GetMeshPath(), f->GetSubsetIndex());
	if (m_Skinned && m_pGameObject)
		if (SkinnedMeshRenderer* r = m_pGameObject->GetComponent<SkinnedMeshRenderer>())
			r->SetSimulatedVertices(nullptr);
	m_Skinned = false;
	m_SkinVerts.clear();
	m_Copy.reset();
	m_Original.reset();
}

bool Cloth::SourceGeometry(std::vector<Vec3>& positions, std::vector<uint32>& indices, SkinnedMeshRenderer** skinned)
{
	*skinned = nullptr;
	positions.clear();
	indices.clear();
	if (m_pGameObject == nullptr)
		return false;
	if (MeshFilter* f = m_pGameObject->GetComponent<MeshFilter>())
	{
		std::shared_ptr<Mesh> mesh = m_Original ? m_Original : f->GetMesh();
		if (mesh == nullptr)
			return false;
		positions.reserve(mesh->Vertices.size());
		for (const auto& v : mesh->Vertices)
			positions.push_back(V(v.pos));
		GlobalIndices(*mesh, indices);
		return positions.size() >= 3 && indices.size() >= 3;
	}
	// 스킨: 바인드 정점을 이 오브젝트 공간으로 (메시 노드 변환 + 단위 변환)
	SkinnedMeshRenderer* r = m_pGameObject->GetComponent<SkinnedMeshRenderer>();
	std::shared_ptr<SkinnedMesh> mesh = r ? r->GetMesh() : nullptr;
	if (mesh == nullptr || !r->CanSimulate())
		return false;
	const Matrix toObject(r->BindToObject());
	positions.reserve(mesh->Vertices.size());
	for (const auto& v : mesh->Vertices)
		positions.push_back(Vec3::Transform(V(v.pos), toObject));
	GlobalIndices(*mesh, indices);
	*skinned = r;
	return positions.size() >= 3 && indices.size() >= 3;
}

void Cloth::Weld(const std::vector<Vec3>& positions, const std::vector<uint32>& indices)
{
	// 같은 자리 정점 묶기 (이음매 · 법선이 다른 정점)
	m_RenderToSim.assign(positions.size(), -1);
	m_RestLocal.clear();
	std::unordered_map<long long, int> byPos;
	for (size_t i = 0; i < positions.size(); ++i)
	{
		const Vec3& p = positions[i];
		const long long key = ((long long)llroundf(p.x * 2000.0f) * 73856093LL) ^ ((long long)llroundf(p.y * 2000.0f) * 19349663LL) ^ ((long long)llroundf(p.z * 2000.0f) * 83492791LL);
		auto it = byPos.find(key);
		if (it != byPos.end() && (m_RestLocal[it->second] - p).LengthSquared() < 1e-8f)
			m_RenderToSim[i] = it->second;
		else
		{
			m_RenderToSim[i] = (int)m_RestLocal.size();
			byPos[key] = m_RenderToSim[i];
			m_RestLocal.push_back(p);
		}
	}
	m_Triangles.clear();
	for (size_t i = 0; i + 2 < indices.size(); i += 3)
	{
		if (indices[i] >= positions.size() || indices[i + 1] >= positions.size() || indices[i + 2] >= positions.size())
			continue;
		for (int k = 0; k < 3; ++k)
			m_Triangles.push_back((uint32)m_RenderToSim[indices[i + k]]);
	}
}

int Cloth::ClothVertexCount()
{
	if (m_Handle != 0)
		return SimVertexCount();
	std::vector<Vec3> pos;
	std::vector<uint32> idx;
	SkinnedMeshRenderer* r = nullptr;
	if (!SourceGeometry(pos, idx, &r))
		return 0;
	Weld(pos, idx);
	return SimVertexCount();
}

// 스킨 본 행렬: 메시 바인드 정점 → 월드 (렌더러 팔레트 · 이 오브젝트 월드)
void Cloth::SkinJoints(SkinnedMeshRenderer* r, std::vector<Matrix>& joints)
{
	if (r->GetFinalTransforms().empty())
		r->ResetToBindPose();
	const auto& palette = r->GetFinalTransforms();
	const int count = (std::max)(1, r->PaletteBoneCount());
	const Matrix w = m_pGameObject->GetTransform()->GetWorldMatrix();
	joints.resize(count);
	for (int k = 0; k < count; ++k)
		joints[k] = (k < (int)palette.size() ? Matrix(palette[k]) : Matrix::Identity) * w;
}

void Cloth::RecomputeNormals(const std::vector<Vec3>& local, std::vector<Vec3>& n) const
{
	n.assign(local.size(), Vec3::Zero);
	for (size_t t = 0; t + 2 < m_Triangles.size(); t += 3)
	{
		const Vec3 fn = (local[m_Triangles[t + 1]] - local[m_Triangles[t]]).Cross(local[m_Triangles[t + 2]] - local[m_Triangles[t]]);
		for (int k = 0; k < 3; ++k) n[m_Triangles[t + k]] += fn;
	}
}

bool Cloth::Create()
{
	std::vector<Vec3> positions;
	std::vector<uint32> indices;
	SkinnedMeshRenderer* skinned = nullptr;
	if (!SourceGeometry(positions, indices, &skinned))
		return false;
	Weld(positions, indices);

	// 시작 정점 (월드): 오브젝트 — 스킨이면 바인드 자세 (천의 rest 모양) 를 이 오브젝트 자리에
	const Matrix w = m_pGameObject->GetTransform()->GetWorldMatrix();
	std::vector<Vec3> world(m_RestLocal.size());
	for (size_t i = 0; i < world.size(); ++i)
		world[i] = Vec3::Transform(m_RestLocal[i], w);

	// 고정점: 위쪽 가장자리 (가장 높은 줄) · 그 줄의 양 끝 · 고른 정점 · coefficients 의 maxDistance 0
	m_Pinned.clear();
	if (Pin == PinTopEdge || Pin == PinTopCorners)
	{
		float minY = FLT_MAX, maxY = -FLT_MAX;
		for (const Vec3& p : world) { minY = (std::min)(minY, p.y); maxY = (std::max)(maxY, p.y); }
		const float eps = (std::max)(1e-3f, (maxY - minY) * 0.01f);
		std::vector<uint32> top;
		for (uint32 i = 0; i < (uint32)world.size(); ++i)
			if (world[i].y >= maxY - eps)
				top.push_back(i);
		if (Pin == PinTopEdge)
			m_Pinned = top;
		else if (!top.empty())
		{
			// 위 줄에서 가장 멀리 떨어진 두 점
			uint32 a = top[0], b = top[0];
			float best = -1.0f;
			for (uint32 i : top)
				for (uint32 k : top)
					if (float d = (world[i] - world[k]).LengthSquared(); d > best) { best = d; a = i; b = k; }
			m_Pinned = { a };
			if (b != a) m_Pinned.push_back(b);
		}
	}
	auto addPin = [&](uint32 i) {
		if (i < world.size() && std::find(m_Pinned.begin(), m_Pinned.end(), i) == m_Pinned.end())
			m_Pinned.push_back(i);
	};
	for (uint32 i : PinnedVertices)
		addPin(i);
	const bool useCoefficients = Coefficients.size() == world.size();
	if (useCoefficients)
		for (uint32 i = 0; i < (uint32)world.size(); ++i)
			if (Coefficients[i].MaxDistance <= 0.0f)
				addPin(i);
	std::vector<float> invMass(world.size(), 1.0f);
	for (uint32 i : m_Pinned)
		invMass[i] = 0.0f;

	PhysicsManager::ClothSettings s;
	const float st = std::clamp(StretchingStiffness, 0.0f, 1.0f), bt = std::clamp(BendingStiffness, 0.0f, 1.0f);
	s.Compliance = 0.001f * (1.0f - st) * (1.0f - st);
	s.ShearCompliance = s.Compliance;
	s.BendCompliance = 0.1f * (1.0f - bt) * (1.0f - bt) + 1e-5f;
	s.Damping = 0.05f + 2.0f * std::clamp(Damping, 0.0f, 1.0f);
	s.Friction = (std::max)(0.0f, Friction);
	s.Thickness = (std::max)(0.0f, Thickness);
	s.Iterations = (uint32)std::clamp(SolverFrequency / 20.0f, 2.0f, 30.0f);

	// 스킨: 정점마다 본 · 가중치 (첫 그리기 정점의 것), 피부에서 벗어날 거리 (고정 = 0), 뒤 막이
	PhysicsManager::ClothSkin skin;
	std::vector<Matrix> joints;
	if (skinned)
	{
		const auto& verts = skinned->GetMesh()->Vertices;
		const int n = (int)world.size();
		skin.Bones.assign(n, { 0, 0, 0, 0 });
		skin.Weights.assign(n, { 0.0f, 0.0f, 0.0f, 0.0f });
		std::vector<bool> done(n, false);
		for (size_t i = 0; i < verts.size() && i < m_RenderToSim.size(); ++i)
		{
			const int sv = m_RenderToSim[i];
			if (sv < 0 || done[sv]) continue;
			done[sv] = true;
			const auto& v = verts[i];
			const float w3 = (std::max)(0.0f, 1.0f - v.weights.x - v.weights.y - v.weights.z);
			skin.Bones[sv] = { v.boneIndices[0], v.boneIndices[1], v.boneIndices[2], v.boneIndices[3] };
			skin.Weights[sv] = { v.weights.x, v.weights.y, v.weights.z, w3 };
		}
		skin.JointCount = (std::max)(1, skinned->PaletteBoneCount());
		skin.RestToWorld = Matrix(skinned->BindToObject()) * w;
		skin.MaxDistance.assign(n, FLT_MAX);
		skin.BackStopDistance.assign(n, FLT_MAX);
		for (int i = 0; i < n; ++i)
			if (useCoefficients)
			{
				skin.MaxDistance[i] = (std::max)(0.0f, Coefficients[i].MaxDistance);
				skin.BackStopDistance[i] = Coefficients[i].CollisionSphereDistance;
			}
		for (uint32 i : m_Pinned)
			skin.MaxDistance[i] = 0.0f;
		SkinJoints(skinned, joints);
		// 뒤 막이 (collisionSphereDistance) 는 천 삼각형의 법선 쪽을 바깥으로 본다 → 메시 법선 (바깥) 과 반대로 감겼으면 삼각형을 뒤집어 맞춘다
		std::vector<Vec3> faceN;
		RecomputeNormals(m_RestLocal, faceN);
		const Matrix toObject(skinned->BindToObject());
		float agree = 0.0f;
		for (size_t i = 0; i < verts.size() && i < m_RenderToSim.size(); ++i)
			agree += faceN[m_RenderToSim[i]].Dot(Vec3::TransformNormal(V(verts[i].normal), toObject)) < 0.0f ? -1.0f : 1.0f;
		if (agree < 0.0f)
			for (size_t t = 0; t + 2 < m_Triangles.size(); t += 3)
				std::swap(m_Triangles[t + 1], m_Triangles[t + 2]);
	}
	m_Handle = PhysicsManager::GetI()->CreateCloth(m_pGameObject, world, m_Triangles, invMass, s, skinned ? &skin : nullptr);
	if (m_Handle == 0)
		return false;
	m_Serial = PhysicsManager::GetI()->WorldSerial();
	m_LastPosition = m_pGameObject->GetTransform()->GetPosition();
	m_Skinned = skinned != nullptr;
	if (m_Skinned)
	{
		// 처음엔 모든 정점을 지금 자세의 피부 자리로 (바인드 자세 → 애니메이션 자세)
		PhysicsManager::GetI()->SkinCloth(m_Handle, joints, true);
		PhysicsManager::GetI()->GetClothVertices(m_Handle, world);
		// 그리기: 원래 정점을 팔레트 끝의 단위 본 하나에 묶는다 (위치는 LateUpdate 가 이 오브젝트 공간으로)
		const auto& verts = skinned->GetMesh()->Vertices;
		m_SkinVerts = verts;
		const BYTE slot = (BYTE)skinned->SimulatedBoneSlot();
		for (auto& v : m_SkinVerts)
		{
			v.weights = XMFLOAT3(1.0f, 0.0f, 0.0f);
			v.boneIndices[0] = slot; v.boneIndices[1] = v.boneIndices[2] = v.boneIndices[3] = 0;
		}
		std::vector<Vec3> n;
		RecomputeNormals(m_RestLocal, n);
		const Matrix toObject(skinned->BindToObject());
		m_NormalSign.assign(verts.size(), 1.0f);
		for (size_t i = 0; i < verts.size(); ++i)
			m_NormalSign[i] = n[m_RenderToSim[i]].Dot(Vec3::TransformNormal(V(verts[i].normal), toObject)) < 0.0f ? -1.0f : 1.0f;
		m_World = world;
		EditorLog::Write("Cloth", "skinned cloth '%s': %zu vertices, %zu pinned, %d bones, coefficients %s", m_pGameObject->GetName().c_str(),
			m_RestLocal.size(), m_Pinned.size(), skin.JointCount, useCoefficients ? "yes" : "no");
		return true;
	}

	// 그리기용 메시 사본 (이 오브젝트만) — 원래 법선 쪽을 기억 (천 삼각형으로 다시 낸 법선과 맞추기)
	MeshFilter* filter = m_pGameObject->GetComponent<MeshFilter>();
	std::shared_ptr<Mesh> mesh = filter->GetMesh();
	m_Original = mesh;
	m_Copy = std::make_shared<Mesh>(*mesh);
	filter->SetMesh(m_Copy, filter->GetMeshPath(), filter->GetSubsetIndex());
	std::vector<Vec3> n;
	RecomputeNormals(m_RestLocal, n);
	m_NormalSign.assign(mesh->Vertices.size(), 1.0f);
	for (size_t i = 0; i < mesh->Vertices.size(); ++i)
		m_NormalSign[i] = n[m_RenderToSim[i]].Dot(V(mesh->Vertices[i].normal)) < 0.0f ? -1.0f : 1.0f;
	m_World = world;
	return true;
}

void Cloth::FixedUpdate()
{
	if (!Application::IsPlaying() || !m_Enabled || m_pGameObject == nullptr)
		return;
	PhysicsManager* pm = PhysicsManager::GetI();
	if (m_Handle != 0 && m_Serial != pm->WorldSerial())
	{
		m_Handle = 0;   // 월드가 새로 만들어졌다
		Release();
	}
	if (m_Handle == 0 && !Create())
		return;
	const float dt = pm->GetFixedTimestep();
	m_Time += dt;
	// 순간 이동 (1 m 넘게 한 스텝에, 또는 ClearTransformMotion): 천 전체를 같이 옮겨 고정점에 끌려 휘날리지 않게
	const Vec3 pos = m_pGameObject->GetTransform()->GetPosition();
	const bool teleport = m_ClearMotion || (pos - m_LastPosition).LengthSquared() > 1.0f;
	m_ClearMotion = false;
	// 바람: External + Random (출렁이는 잡음), 중력을 끄면 중력만큼 반대로
	const float t = (float)m_Time;
	Vec3 a = ExternalAcceleration + Vec3(RandomAcceleration.x * sinf(t * 1.7f) * cosf(t * 0.63f), RandomAcceleration.y * sinf(t * 2.3f + 1.0f),
		RandomAcceleration.z * sinf(t * 1.3f + 2.0f) * cosf(t * 0.91f));
	if (!UseGravity)
		a -= PhysicsSettings::Gravity();
	if (m_Skinned)
	{
		// 스킨: 뼈대 자세로 피부 자리를 다시 (고정 정점은 그 자리로, 나머지는 maxDistance 안에서) — 순간 이동이면 모두 피부 자리로
		SkinnedMeshRenderer* r = m_pGameObject->GetComponent<SkinnedMeshRenderer>();
		if (r == nullptr)
			return;
		std::vector<Matrix> joints;
		SkinJoints(r, joints);
		pm->SkinCloth(m_Handle, joints, teleport);
		m_LastPosition = pos;
		pm->DriveCloth(m_Handle, {}, {}, a, dt);
		return;
	}
	if (teleport)
		pm->ShiftCloth(m_Handle, pos - m_LastPosition);
	m_LastPosition = pos;
	// 고정점은 오브젝트를 따라간다
	const Matrix w = m_pGameObject->GetTransform()->GetWorldMatrix();
	std::vector<Vec3> pinnedWorld(m_Pinned.size());
	for (size_t k = 0; k < m_Pinned.size(); ++k)
		pinnedWorld[k] = Vec3::Transform(m_RestLocal[m_Pinned[k]], w);
	pm->DriveCloth(m_Handle, m_Pinned, pinnedWorld, a, dt);
}

void Cloth::LateUpdate()
{
	if (m_Handle == 0 || m_pGameObject == nullptr || (m_Copy == nullptr && !m_Skinned))
		return;
	if (!PhysicsManager::GetI()->GetClothVertices(m_Handle, m_World) || m_World.size() != m_RestLocal.size())
		return;
	const Matrix inv = m_pGameObject->GetTransform()->GetWorldMatrix().Invert();
	std::vector<Vec3> local(m_World.size());
	for (size_t i = 0; i < local.size(); ++i)
		local[i] = Vec3::Transform(m_World[i], inv);
	std::vector<Vec3> n;
	RecomputeNormals(local, n);
	for (Vec3& v : n)
		if (v.LengthSquared() > 1e-20f) v.Normalize();
	// 그리기 정점: 위치 · 법선 · 접선 (법선에 수직으로)
	auto write = [&](auto& verts) {
		for (size_t i = 0; i < verts.size() && i < m_RenderToSim.size(); ++i)
		{
			auto& v = verts[i];
			const int s = m_RenderToSim[i];
			v.pos = XMFLOAT3(local[s].x, local[s].y, local[s].z);
			const Vec3 nn = n[s] * m_NormalSign[i];
			v.normal = XMFLOAT3(nn.x, nn.y, nn.z);
			Vec3 tg(v.tangentU.x, v.tangentU.y, v.tangentU.z);
			tg -= nn * tg.Dot(nn);
			if (tg.LengthSquared() > 1e-12f) { tg.Normalize(); v.tangentU = XMFLOAT4(tg.x, tg.y, tg.z, v.tangentU.w); }
		}
	};
	if (m_Skinned)
	{
		SkinnedMeshRenderer* r = m_pGameObject->GetComponent<SkinnedMeshRenderer>();
		if (r == nullptr)
			return;
		write(m_SkinVerts);
		r->SetSimulatedVertices(&m_SkinVerts);
		return;
	}
	write(m_Copy->Vertices);
	Vec3 mn(FLT_MAX, FLT_MAX, FLT_MAX), mx(-FLT_MAX, -FLT_MAX, -FLT_MAX);
	for (const Vec3& p : local)
	{
		mn = Vec3::Min(mn, p);
		mx = Vec3::Max(mx, p);
	}
	m_Copy->Ball.center = (mn + mx) * 0.5f;
	m_Copy->Ball.radius = (mx - mn).Length() * 0.5f;
	m_Copy->ModelMesh.UpdateVertices(Application::GetI()->GetDevice(), Application::GetI()->GetDeviceContext(), m_Copy->Vertices.data(), (uint32)m_Copy->Vertices.size());
}

void Cloth::LocalVertices(std::vector<Vec3>& out)
{
	out.clear();
	if (m_Handle != 0 && m_World.size() == m_RestLocal.size() && m_pGameObject)
	{
		const Matrix inv = m_pGameObject->GetTransform()->GetWorldMatrix().Invert();
		out.resize(m_RenderToSim.size());
		for (size_t i = 0; i < out.size(); ++i)
			out[i] = Vec3::Transform(m_World[m_RenderToSim[i]], inv);
		return;
	}
	std::vector<uint32> idx;
	SkinnedMeshRenderer* r = nullptr;
	SourceGeometry(out, idx, &r);
}

void Cloth::OnInspectorGUI()
{
	UnityGUI::Slider("Stretching Stiffness", &StretchingStiffness, 0.0f, 1.0f);
	UnityGUI::Slider("Bending Stiffness", &BendingStiffness, 0.0f, 1.0f);
	UnityGUI::Toggle("Use Gravity", &UseGravity);
	UnityGUI::Slider("Damping", &Damping, 0.0f, 1.0f);
	UnityGUI::Vector3("External Acceleration", &ExternalAcceleration.x);
	UnityGUI::Vector3("Random Acceleration", &RandomAcceleration.x);
	UnityGUI::Slider("Friction", &Friction, 0.0f, 1.0f);
	if (UnityGUI::Float("Thickness", &Thickness)) Thickness = (std::max)(0.0f, Thickness);
	if (UnityGUI::Float("Cloth Solver Frequency", &SolverFrequency)) SolverFrequency = std::clamp(SolverFrequency, 40.0f, 600.0f);
	static const char* kPin[] = { "None", "Top Edge", "Top Corners" };
	UnityGUI::Dropdown("Pin", &Pin, kPin, 3);
	if (!PinnedVertices.empty())
	{
		char buf[48];
		snprintf(buf, sizeof(buf), "%zu vertices", PinnedVertices.size());
		UnityGUI::ValueLabel("Pinned Vertices", buf);
	}
	if (!Coefficients.empty())
	{
		int fixed = 0;
		for (const Coefficient& c : Coefficients) fixed += c.MaxDistance <= 0.0f ? 1 : 0;
		char buf[64];
		snprintf(buf, sizeof(buf), "%zu vertices (%d fixed)", Coefficients.size(), fixed);
		UnityGUI::ValueLabel("Coefficients", buf);
	}
	if (m_pGameObject && m_pGameObject->GetComponent<MeshFilter>() == nullptr && m_pGameObject->GetComponent<SkinnedMeshRenderer>() == nullptr)
		UnityGUI::HelpBox("Cloth needs a Mesh Filter (for example a Plane) or a Skinned Mesh Renderer (a skirt or a cape) on the same GameObject.");
	if (Application::IsPlaying())
	{
		char buf[96];
		snprintf(buf, sizeof(buf), "%s%s, %d vertices, %zu pinned", m_Handle ? "simulating" : "not simulating", m_Skinned ? " (skinned)" : "", SimVertexCount(), m_Pinned.size());
		UnityGUI::ValueLabel("State", buf);
	}
}

GENERATE_COMPONENT_FUNC_TOJSON(Cloth)
{
	json j;
	j["type"] = "Cloth";
	j["enabled"] = m_Enabled;
	j["stretchingStiffness"] = StretchingStiffness;
	j["bendingStiffness"] = BendingStiffness;
	j["useGravity"] = UseGravity;
	j["damping"] = Damping;
	j["friction"] = Friction;
	j["thickness"] = Thickness;
	j["externalAcceleration"] = { ExternalAcceleration.x, ExternalAcceleration.y, ExternalAcceleration.z };
	j["randomAcceleration"] = { RandomAcceleration.x, RandomAcceleration.y, RandomAcceleration.z };
	j["pin"] = Pin;
	j["pinnedVertices"] = PinnedVertices;
	j["solverFrequency"] = SolverFrequency;
	if (!Coefficients.empty())
	{
		// [maxDistance, collisionSphereDistance] — 제한 없음 (FLT_MAX) 은 -1
		json c = json::array();
		for (const Coefficient& k : Coefficients)
			c.push_back({ k.MaxDistance >= FLT_MAX ? -1.0f : k.MaxDistance, k.CollisionSphereDistance >= FLT_MAX ? -1.0f : k.CollisionSphereDistance });
		j["coefficients"] = c;
	}
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(Cloth)
{
	auto v3 = [&](const char* k, Vec3 def) {
		if (j.contains(k) && j[k].is_array() && j[k].size() == 3) return Vec3(j[k][0].get<float>(), j[k][1].get<float>(), j[k][2].get<float>());
		return def;
	};
	m_Enabled = j.value("enabled", true);
	StretchingStiffness = j.value("stretchingStiffness", 1.0f);
	BendingStiffness = j.value("bendingStiffness", 0.0f);
	UseGravity = j.value("useGravity", true);
	Damping = j.value("damping", 0.0f);
	Friction = j.value("friction", 0.5f);
	Thickness = j.value("thickness", 0.02f);
	ExternalAcceleration = v3("externalAcceleration", Vec3::Zero);
	RandomAcceleration = v3("randomAcceleration", Vec3::Zero);
	Pin = std::clamp(j.value("pin", 0), 0, 2);
	PinnedVertices.clear();
	if (j.contains("pinnedVertices") && j["pinnedVertices"].is_array())
		for (const json& v : j["pinnedVertices"]) PinnedVertices.push_back(v.get<uint32>());
	SolverFrequency = j.value("solverFrequency", 120.0f);
	Coefficients.clear();
	if (j.contains("coefficients") && j["coefficients"].is_array())
		for (const json& c : j["coefficients"])
		{
			Coefficient k;
			if (c.is_array() && c.size() == 2 && c[0].is_number() && c[1].is_number())
			{
				const float m = c[0].get<float>(), b = c[1].get<float>();
				k.MaxDistance = m < 0.0f ? FLT_MAX : m;
				k.CollisionSphereDistance = b < 0.0f ? FLT_MAX : b;
			}
			Coefficients.push_back(k);
		}
}
