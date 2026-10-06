#include "pch.h"
#include "Cloth.h"
#include "MeshFilter.h"
#include "Mesh.h"
#include "PhysicsManager.h"
#include "PhysicsSettings.h"
#include "UnityGUI.h"
#include "EditorLog.h"

namespace
{
	Vec3 V(const XMFLOAT3& f) { return Vec3(f.x, f.y, f.z); }
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
	m_Copy.reset();
	m_Original.reset();
}

bool Cloth::Create()
{
	MeshFilter* filter = m_pGameObject ? m_pGameObject->GetComponent<MeshFilter>() : nullptr;
	std::shared_ptr<Mesh> mesh = filter ? filter->GetMesh() : nullptr;
	if (mesh == nullptr || mesh->Vertices.size() < 3 || mesh->Indices.size() < 3)
		return false;
	// 같은 자리 정점 묶기 (이음매 · 법선이 다른 정점)
	m_RenderToSim.assign(mesh->Vertices.size(), -1);
	m_RestLocal.clear();
	std::unordered_map<long long, int> byPos;
	for (size_t i = 0; i < mesh->Vertices.size(); ++i)
	{
		const XMFLOAT3& p = mesh->Vertices[i].pos;
		const long long key = ((long long)llroundf(p.x * 2000.0f) * 73856093LL) ^ ((long long)llroundf(p.y * 2000.0f) * 19349663LL) ^ ((long long)llroundf(p.z * 2000.0f) * 83492791LL);
		auto it = byPos.find(key);
		if (it != byPos.end() && (m_RestLocal[it->second] - V(p)).LengthSquared() < 1e-8f)
			m_RenderToSim[i] = it->second;
		else
		{
			m_RenderToSim[i] = (int)m_RestLocal.size();
			byPos[key] = m_RenderToSim[i];
			m_RestLocal.push_back(V(p));
		}
	}
	m_Triangles.clear();
	for (size_t i = 0; i + 2 < mesh->Indices.size(); i += 3)
		for (int k = 0; k < 3; ++k)
			m_Triangles.push_back((uint32)m_RenderToSim[mesh->Indices[i + k]]);

	const Matrix w = m_pGameObject->GetTransform()->GetWorldMatrix();
	std::vector<Vec3> world(m_RestLocal.size());
	for (size_t i = 0; i < world.size(); ++i)
		world[i] = Vec3::Transform(m_RestLocal[i], w);

	// 고정점: 위쪽 가장자리 (가장 높은 줄) · 그 줄의 양 끝 · 고른 정점
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
	for (uint32 i : PinnedVertices)
		if (i < world.size() && std::find(m_Pinned.begin(), m_Pinned.end(), i) == m_Pinned.end())
			m_Pinned.push_back(i);
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
	m_Handle = PhysicsManager::GetI()->CreateCloth(m_pGameObject, world, m_Triangles, invMass, s);
	if (m_Handle == 0)
		return false;
	m_Serial = PhysicsManager::GetI()->WorldSerial();
	m_LastPosition = m_pGameObject->GetTransform()->GetPosition();

	// 그리기용 메시 사본 (이 오브젝트만) — 원래 법선 쪽을 기억 (천 삼각형으로 다시 낸 법선과 맞추기)
	m_Original = mesh;
	m_Copy = std::make_shared<Mesh>(*mesh);
	filter->SetMesh(m_Copy, filter->GetMeshPath(), filter->GetSubsetIndex());
	std::vector<Vec3> n(m_RestLocal.size(), Vec3::Zero);
	for (size_t t = 0; t + 2 < m_Triangles.size(); t += 3)
	{
		const Vec3 fn = (m_RestLocal[m_Triangles[t + 1]] - m_RestLocal[m_Triangles[t]]).Cross(m_RestLocal[m_Triangles[t + 2]] - m_RestLocal[m_Triangles[t]]);
		for (int k = 0; k < 3; ++k) n[m_Triangles[t + k]] += fn;
	}
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
	if (m_ClearMotion || (pos - m_LastPosition).LengthSquared() > 1.0f)
		pm->ShiftCloth(m_Handle, pos - m_LastPosition);
	m_ClearMotion = false;
	m_LastPosition = pos;
	// 고정점은 오브젝트를 따라간다
	const Matrix w = m_pGameObject->GetTransform()->GetWorldMatrix();
	std::vector<Vec3> pinnedWorld(m_Pinned.size());
	for (size_t k = 0; k < m_Pinned.size(); ++k)
		pinnedWorld[k] = Vec3::Transform(m_RestLocal[m_Pinned[k]], w);
	// 바람: External + Random (출렁이는 잡음), 중력을 끄면 중력만큼 반대로
	const float t = (float)m_Time;
	Vec3 a = ExternalAcceleration + Vec3(RandomAcceleration.x * sinf(t * 1.7f) * cosf(t * 0.63f), RandomAcceleration.y * sinf(t * 2.3f + 1.0f),
		RandomAcceleration.z * sinf(t * 1.3f + 2.0f) * cosf(t * 0.91f));
	if (!UseGravity)
		a -= PhysicsSettings::Gravity();
	pm->DriveCloth(m_Handle, m_Pinned, pinnedWorld, a, dt);
}

void Cloth::LateUpdate()
{
	if (m_Handle == 0 || m_Copy == nullptr || m_pGameObject == nullptr)
		return;
	if (!PhysicsManager::GetI()->GetClothVertices(m_Handle, m_World) || m_World.size() != m_RestLocal.size())
		return;
	const Matrix inv = m_pGameObject->GetTransform()->GetWorldMatrix().Invert();
	std::vector<Vec3> local(m_World.size());
	for (size_t i = 0; i < local.size(); ++i)
		local[i] = Vec3::Transform(m_World[i], inv);
	std::vector<Vec3> n(local.size(), Vec3::Zero);
	for (size_t t = 0; t + 2 < m_Triangles.size(); t += 3)
	{
		const Vec3 fn = (local[m_Triangles[t + 1]] - local[m_Triangles[t]]).Cross(local[m_Triangles[t + 2]] - local[m_Triangles[t]]);
		for (int k = 0; k < 3; ++k) n[m_Triangles[t + k]] += fn;
	}
	for (Vec3& v : n)
		if (v.LengthSquared() > 1e-20f) v.Normalize();
	Vec3 mn(FLT_MAX, FLT_MAX, FLT_MAX), mx(-FLT_MAX, -FLT_MAX, -FLT_MAX);
	for (size_t i = 0; i < m_Copy->Vertices.size() && i < m_RenderToSim.size(); ++i)
	{
		auto& v = m_Copy->Vertices[i];
		const int s = m_RenderToSim[i];
		v.pos = XMFLOAT3(local[s].x, local[s].y, local[s].z);
		const Vec3 nn = n[s] * m_NormalSign[i];
		v.normal = XMFLOAT3(nn.x, nn.y, nn.z);
		Vec3 tg(v.tangentU.x, v.tangentU.y, v.tangentU.z);
		tg -= nn * tg.Dot(nn);
		if (tg.LengthSquared() > 1e-12f) { tg.Normalize(); v.tangentU = XMFLOAT4(tg.x, tg.y, tg.z, v.tangentU.w); }
		mn = Vec3::Min(mn, local[s]);
		mx = Vec3::Max(mx, local[s]);
	}
	m_Copy->Ball.center = (mn + mx) * 0.5f;
	m_Copy->Ball.radius = (mx - mn).Length() * 0.5f;
	m_Copy->ModelMesh.UpdateVertices(Application::GetI()->GetDevice(), Application::GetI()->GetDeviceContext(), m_Copy->Vertices.data(), (uint32)m_Copy->Vertices.size());
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
	if (m_pGameObject && m_pGameObject->GetComponent<MeshFilter>() == nullptr)
		UnityGUI::HelpBox("Cloth needs a Mesh Filter (for example a Plane) on the same GameObject.");
	if (Application::IsPlaying())
	{
		char buf[96];
		snprintf(buf, sizeof(buf), "%s, %d vertices, %zu pinned", m_Handle ? "simulating" : "not simulating", SimVertexCount(), m_Pinned.size());
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
}
