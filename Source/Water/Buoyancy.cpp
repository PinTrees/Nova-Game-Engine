#include "pch.h"
#include "Buoyancy.h"
#include "WaterBody.h"
#include "RigidBody.h"
#include "PhysicsManager.h"
#include "Transform.h"
#include "UnityGUI.h"
#include "SceneViewOverlay.h"
#include "SelectionManager.h"
#include "FrameProfiler.h"

namespace
{
	// 폰툰: 바닥 네 모서리 + 가운데 (로컬)
	void Pontoons(const Vec3& size, Vec3 out[5])
	{
		const float hx = size.x * 0.5f, hy = size.y * 0.5f, hz = size.z * 0.5f;
		out[0] = Vec3(-hx, -hy, -hz);
		out[1] = Vec3(hx, -hy, -hz);
		out[2] = Vec3(-hx, -hy, hz);
		out[3] = Vec3(hx, -hy, hz);
		out[4] = Vec3(0, -hy, 0);
	}
}

Buoyancy::Buoyancy()
{
	m_InspectorTitleName = "Buoyancy";
}

void Buoyancy::FixedUpdate()
{
	m_Submerged = 0.0f;
	if (!m_Enabled || m_pGameObject == nullptr)
		return;
	RigidBody* rb = m_pGameObject->GetComponent<RigidBody>();
	if (rb == nullptr || rb->IsKinematic())
		return;
	const XMMATRIX world = m_pGameObject->GetTransform()->GetWorldMatrix();
	Vec3 local[5];
	Pontoons(Size, local);
	// 상자 높이 (월드, Transform 크기 포함)
	const float height = (std::max)(0.05f, Vec3(XMVector3TransformNormal(Vec3(0.0f, Size.y, 0.0f), world)).Length());
	const float mass = rb->GetMass();
	const Vec3 com = XMVector3TransformCoord(Vec3(0, 0, 0), world);
	const Vec3 v = rb->GetVelocity();
	const Vec3 w = rb->GetAngularVelocity();
	constexpr int n = 5;
	float total = 0.0f;
	for (int i = 0; i < n; ++i)
	{
		const Vec3 p = XMVector3TransformCoord(local[i], world);
		float surface;
		Vec3 normal, flow;
		if (!WaterBody::Query(p, surface, &normal, &flow))
			continue;
		const float sub = std::clamp((surface - p.y) / height, 0.0f, 1.0f);
		if (sub <= 0.0f)
			continue;
		total += sub;
		// 위로: 절반 잠기면(sub 0.5) 폰툰 몫의 무게와 같게
		Vec3 force(0.0f, mass * 9.81f * FloatStrength * 2.0f * sub / n, 0.0f);
		// 폰툰 속도 = v + w × r. 흐름과의 차이에 맞서는 저항
		const Vec3 r = p - com;
		const Vec3 pv = v + w.Cross(r);
		Vec3 rel = pv - flow * FlowForce;
		force -= rel * (mass * WaterDrag * sub / n);
		rb->AddForceAtPosition(force, p, ForceMode::Force);
	}
	m_Submerged = total / n;
	if (m_Submerged > 0.0f)
		rb->AddTorque(-w * (mass * WaterAngularDrag * m_Submerged), ForceMode::Force);
	// (검증용) NOVA_DEV_PROFILE=1 이면 1 초(50 스텝)마다 높이·잠긴 비율
	if (FrameProfiler::Enabled() && ++m_LogStep % 50 == 0)
	{
		float surface = 0.0f;
		WaterBody::Query(com, surface);
		EditorLog::Write("Water", "buoyancy %s: y %.2f, water %.2f, submerged %.2f, speed %.2f", m_pGameObject->GetName().c_str(), com.y, surface, m_Submerged, v.Length());
	}
}

void Buoyancy::OnInspectorGUI()
{
	using namespace UnityGUI;
	UnityGUI::Vector3("Size", &Size.x);
	Slider("Float Strength", &FloatStrength, 0.0f, 4.0f);
	Slider("Water Drag", &WaterDrag, 0.0f, 5.0f);
	Slider("Water Angular Drag", &WaterAngularDrag, 0.0f, 5.0f);
	Slider("Flow Force", &FlowForce, 0.0f, 3.0f);
	if (m_pGameObject && m_pGameObject->GetComponent<RigidBody>() == nullptr)
		HelpBox("Needs a Rigidbody on the same GameObject.", true);
	else
		HelpBox("Five pontoons (bottom corners + center of Size) push up by how deep they are under the water surface (same waves as the screen). 1 = floats half submerged.", false);
}

// 선택하면 폰툰 상자와 각 폰툰 위 수면 표시
void Buoyancy::OnDrawGizmos()
{
	if (!SceneViewOverlay::IsActive() || SelectionManager::GetSelectedGameObject() != m_pGameObject || m_pGameObject == nullptr)
		return;
	const XMMATRIX world = m_pGameObject->GetTransform()->GetWorldMatrix();
	const float hx = Size.x * 0.5f, hy = Size.y * 0.5f, hz = Size.z * 0.5f;
	Vec3 c[8];
	for (int i = 0; i < 8; ++i)
		c[i] = XMVector3TransformCoord(Vec3(i & 1 ? hx : -hx, i & 2 ? hy : -hy, i & 4 ? hz : -hz), world);
	const int e[12][2] = { {0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7} };
	const ImU32 col = IM_COL32(80, 190, 255, 200);
	for (const auto& k : e)
		SceneViewOverlay::DrawLine(XMFLOAT3(c[k[0]].x, c[k[0]].y, c[k[0]].z), XMFLOAT3(c[k[1]].x, c[k[1]].y, c[k[1]].z), col, 1.0f);
	Vec3 local[5];
	Pontoons(Size, local);
	for (const Vec3& l : local)
	{
		const Vec3 p = XMVector3TransformCoord(l, world);
		float surface;
		if (WaterBody::Query(p, surface))
			SceneViewOverlay::DrawLine(XMFLOAT3(p.x, p.y, p.z), XMFLOAT3(p.x, surface, p.z), surface > p.y ? IM_COL32(80, 255, 160, 230) : IM_COL32(255, 160, 80, 200), 2.0f);
	}
}

GENERATE_COMPONENT_FUNC_TOJSON(Buoyancy)
{
	json j;
	SERIALIZE_TYPE(j, Buoyancy);
	j["enabled"] = m_Enabled;
	j["size"] = { Size.x, Size.y, Size.z };
	j["floatStrength"] = FloatStrength;
	j["waterDrag"] = WaterDrag;
	j["waterAngularDrag"] = WaterAngularDrag;
	j["flowForce"] = FlowForce;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(Buoyancy)
{
	m_Enabled = j.value("enabled", true);
	if (j.contains("size") && j["size"].is_array() && j["size"].size() >= 3)
		Size = Vec3(j["size"][0].get<float>(), j["size"][1].get<float>(), j["size"][2].get<float>());
	FloatStrength = j.value("floatStrength", FloatStrength);
	WaterDrag = j.value("waterDrag", WaterDrag);
	WaterAngularDrag = j.value("waterAngularDrag", WaterAngularDrag);
	FlowForce = j.value("flowForce", FlowForce);
}
