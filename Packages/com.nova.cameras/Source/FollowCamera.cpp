#include "pch.h"
#include "FollowCamera.h"
#include "UnityGUI.h"

FollowCamera::FollowCamera()
{
	m_InspectorTitleName = "Follow Camera";
}

GameObject* FollowCamera::FindTarget() const
{
	Scene* scene = SceneManager::GetI()->GetCurrentScene();
	return Target != 0 && scene ? scene->FindByFileID(Target) : nullptr;
}

void FollowCamera::Desired(GameObject* target, Vec3& pos, Vec3& look) const
{
	Transform* t = target->GetTransform();
	const Vec3 base = t->GetPosition();
	look = base + Vec3(0.0f, LookAtHeight, 0.0f);
	// 대상 앞 방향의 수평 성분 (거의 수직이면 고정 Yaw)
	Vec3 back;
	Vec3 f = t->GetForward();
	f.y = 0.0f;
	if (FollowTargetRotation && f.LengthSquared() > 1e-6f)
	{
		f.Normalize();
		back = -f;
	}
	else
	{
		const float y = XMConvertToRadians(Yaw);
		back = -Vec3(sinf(y), 0.0f, cosf(y));
	}
	pos = base + back * Distance + Vec3(0.0f, Height, 0.0f);

	if (AvoidObstacles && PhysicsManager::GetI()->IsSimulating())
	{
		// 보는 점 → 카메라: 대상 자신은 건너뛰고 가장 가까운 벽 앞으로
		Vec3 dir = pos - look;
		const float len = dir.Length();
		if (len > 1e-3f)
		{
			dir /= len;
			Vec3 origin = look;
			float travelled = 0.0f;
			for (int i = 0; i < 4; ++i)
			{
				RaycastHit hit;
				if (!PhysicsManager::GetI()->Raycast(origin, dir, hit, len - travelled))
					break;
				bool self = false;
				for (GameObject* g = hit.gameObject; g != nullptr; g = g->GetParent())
					if (g == target) { self = true; break; }
				if (!self)
				{
					pos = look + dir * (std::max)(0.3f, travelled + hit.distance - ObstaclePadding);
					break;
				}
				// 대상의 콜라이더(캐릭터 캡슐 등): 그 뒤에서 다시 쏜다
				travelled += hit.distance + 0.01f;
				origin = look + dir * travelled;
			}
		}
	}
}

void FollowCamera::Start()
{
	m_Snap = true;
}

void FollowCamera::LateUpdate()
{
	if (!Application::IsPlaying() || m_pGameObject == nullptr)
		return;
	GameObject* target = FindTarget();
	if (target == nullptr || target == m_pGameObject)
		return;
	Vec3 pos, look;
	Desired(target, pos, look);
	Transform* tr = m_pGameObject->GetTransform();
	if (!m_Snap && Damping > 0.0f)
	{
		const float dt = (float)DT;
		const float k = 1.0f - expf(-Damping * dt);   // 프레임 수와 무관한 지수 감쇠
		pos = Vec3::Lerp(tr->GetPosition(), pos, k);
	}
	m_Snap = false;
	tr->SetPosition(pos);

	// 보는 방향 → 회전 (DirectX 왼손 좌표: 앞 = +Z, 피치 + = 아래)
	Vec3 d = look - pos;
	if (d.LengthSquared() > 1e-8f)
	{
		d.Normalize();
		const float yaw = atan2f(d.x, d.z);
		const float pitch = asinf(std::clamp(-d.y, -1.0f, 1.0f));
		tr->SetRotation(Quaternion::CreateFromYawPitchRoll(yaw, pitch, 0.0f));
	}
}

void FollowCamera::OnInspectorGUI()
{
	UnityGUI::GameObjectField("Target", &Target);
	if (UnityGUI::Float("Distance", &Distance)) Distance = (std::max)(0.0f, Distance);
	UnityGUI::Float("Height", &Height);
	UnityGUI::Float("Look At Height", &LookAtHeight);
	if (UnityGUI::Float("Damping", &Damping)) Damping = (std::max)(0.0f, Damping);
	UnityGUI::Toggle("Follow Target Rotation", &FollowTargetRotation);
	if (!FollowTargetRotation)
		UnityGUI::Float("Yaw", &Yaw);
	UnityGUI::Toggle("Avoid Obstacles", &AvoidObstacles);
	if (AvoidObstacles)
		UnityGUI::Float("Obstacle Padding", &ObstaclePadding);
	if (Target == 0)
		UnityGUI::HelpBox("Drag the object to follow (for example the player) from the Hierarchy into Target.", false);
}

void FollowCamera::RemapFileIDs(const std::unordered_map<uint64, uint64>& map)
{
	auto it = map.find(Target);
	if (it != map.end())
		Target = it->second;
}

GENERATE_COMPONENT_FUNC_TOJSON(FollowCamera)
{
	json j;
	j["type"] = "FollowCamera";
	j["enabled"] = m_Enabled;
	j["target"] = Target;
	j["distance"] = Distance;
	j["height"] = Height;
	j["lookAtHeight"] = LookAtHeight;
	j["damping"] = Damping;
	j["followTargetRotation"] = FollowTargetRotation;
	j["yaw"] = Yaw;
	j["avoidObstacles"] = AvoidObstacles;
	j["obstaclePadding"] = ObstaclePadding;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(FollowCamera)
{
	m_Enabled = j.value("enabled", true);
	Target = j.value("target", (uint64)0);
	Distance = j.value("distance", 5.0f);
	Height = j.value("height", 2.0f);
	LookAtHeight = j.value("lookAtHeight", 1.2f);
	Damping = j.value("damping", 8.0f);
	FollowTargetRotation = j.value("followTargetRotation", true);
	Yaw = j.value("yaw", 0.0f);
	AvoidObstacles = j.value("avoidObstacles", true);
	ObstaclePadding = j.value("obstaclePadding", 0.2f);
}
