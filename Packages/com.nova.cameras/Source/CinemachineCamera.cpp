#include "pch.h"
#include "CinemachineCamera.h"
#include "CinemachineBrain.h"
#include "UnityGUI.h"
#include "SceneViewOverlay.h"
#include "SelectionManager.h"
#include "UndoSystem.h"

CinemachineCamera::CinemachineCamera()
{
	m_InspectorTitleName = "Cinemachine Camera";
	CmCore::Register(this);
}

CinemachineCamera::~CinemachineCamera()
{
	CmCore::Unregister(this);
}

GameObject* CinemachineCamera::Follow() const
{
	return CmCore::FindObject(TrackingTarget);
}

GameObject* CinemachineCamera::LookAt() const
{
	return CmCore::FindObject(CustomLookAtTarget ? LookAtTarget : TrackingTarget);
}

bool CinemachineCamera::IsEligible() const
{
	// 지금 씬의 것만 (프리팹 원본 · 복사해 둔 컴포넌트는 아님)
	return m_pGameObject != nullptr && m_Enabled && GameObject::IsAlive(m_pGameObject) && m_pGameObject->IsActiveInHierarchy() &&
		CmCore::FindObject(m_pGameObject->GetFileID()) == m_pGameObject;
}

bool CinemachineCamera::IsLive() const
{
	for (CinemachineBrain* b : CmCore::Brains())
		if (b->IsLiveChild(this))
			return true;
	return false;
}

void CinemachineCamera::Prioritize()
{
	ActivationStamp = CmCore::NextActivationStamp();
}

CinemachineComponentBase* CinemachineCamera::Pipeline(CmStage stage) const
{
	if (m_pGameObject == nullptr)
		return nullptr;
	for (auto& c : m_pGameObject->GetComponents())
		if (auto* b = dynamic_cast<CinemachineComponentBase*>(c.get()))
			if (b->Stage() == stage && b->IsEnabled())
				return b;
	return nullptr;
}

void CinemachineCamera::UpdateCameraState(float dt)
{
	if (m_pGameObject == nullptr)
		return;
	Transform* tr = m_pGameObject->GetTransform();
	const bool editing = !Application::IsPlaying();

	CmState s;
	s.RawPosition = tr->GetPosition();
	s.RawOrientation = tr->GetRotation();
	s.Lens = Lens;
	if (GameObject* look = LookAt())
	{
		s.HasLookAt = true;
		s.ReferenceLookAt = look->GetTransform()->GetPosition();
	}

	const float d = (m_PreviousValid && !editing) ? dt : -1.0f;
	// 단계 순서대로 (같은 단계는 붙인 순서)
	CinemachineComponentBase* stages[8];
	int count = 0;
	for (auto& c : m_pGameObject->GetComponents())
		if (auto* b = dynamic_cast<CinemachineComponentBase*>(c.get()))
			if (b->IsEnabled() && count < 8)
				stages[count++] = b;
	std::stable_sort(stages, stages + count, [](CinemachineComponentBase* a, CinemachineComponentBase* b) { return (int)a->Stage() < (int)b->Stage(); });

	bool moved = false;
	for (int i = 0; i < count; ++i)
	{
		CinemachineComponentBase* b = stages[i];
		if (d < 0.0f)
			b->OnSnap();
		if (editing && b->Stage() >= CmStage::Noise)
			continue;   // 흔들림 · 충격은 Play 에서만
		b->MutateCameraState(this, s, d);
		moved |= b->Stage() <= CmStage::Aim;
	}

	// 원래 자세는 가상 카메라 Transform 에 남긴다 (Unity 와 같이 Scene 에서 가상 카메라가 따라 움직인다). 바뀔 때만
	if (moved)
	{
		if ((s.RawPosition - tr->GetPosition()).LengthSquared() > 1e-10f)
			tr->SetPosition(s.RawPosition);
		const Quaternion old = tr->GetRotation();
		if (1.0f - fabsf(old.Dot(s.RawOrientation)) > 1e-9f)
			tr->SetRotation(s.RawOrientation);
	}
	m_State = s;
	m_HasState = true;
	m_PreviousValid = true;
}

float* CinemachineCamera::FloatProp(int i)
{
	switch (i)
	{
	case 0: return &Lens.FieldOfView;
	case 1: return &Lens.OrthographicSize;
	case 2: return &Lens.NearClipPlane;
	case 3: return &Lens.FarClipPlane;
	case 4: return &Lens.Dutch;
	default: return nullptr;
	}
}

uint64* CinemachineCamera::ObjectProp(int i)
{
	return i == 0 ? &TrackingTarget : i == 1 ? &LookAtTarget : nullptr;
}

void CinemachineCamera::Validate()
{
	Lens.FieldOfView = std::clamp(Lens.FieldOfView, 1.0f, 179.0f);
	Lens.OrthographicSize = (std::max)(0.01f, Lens.OrthographicSize);
	Lens.NearClipPlane = (std::max)(0.001f, Lens.NearClipPlane);
	Lens.FarClipPlane = (std::max)(Lens.NearClipPlane + 0.01f, Lens.FarClipPlane);
	Lens.Dutch = std::clamp(Lens.Dutch, -180.0f, 180.0f);
}

json CinemachineCamera::Info() const
{
	json j;
	j["name"] = m_pGameObject ? m_pGameObject->GetName() : std::string();
	j["priority"] = Priority;
	j["eligible"] = IsEligible();
	j["live"] = IsLive();
	j["stamp"] = ActivationStamp;
	const Vec3 p = m_State.FinalPosition();
	j["position"] = { p.x, p.y, p.z };
	const Vec3 raw = m_State.RawPosition;
	j["rawPosition"] = { raw.x, raw.y, raw.z };
	const Vec3 e = CmCore::EulerDegrees(m_State.FinalOrientation());
	j["rotation"] = { e.x, e.y, e.z };
	const Vec3 re = CmCore::EulerDegrees(m_State.RawOrientation);
	j["rawRotation"] = { re.x, re.y, re.z };
	const Vec3 c = m_State.PositionCorrection;
	j["positionCorrection"] = { c.x, c.y, c.z };
	j["fov"] = m_State.Lens.FieldOfView;
	j["dutch"] = m_State.Lens.Dutch;
	const char* stageNames[] = { "body", "aim", "noise", "extension" };
	for (int s = 0; s < 4; ++s)
	{
		CinemachineComponentBase* b = Pipeline((CmStage)s);
		j[stageNames[s]] = b ? b->GetType() : std::string();
	}
	if (GameObject* f = Follow())
		j["trackingTarget"] = f->GetName();
	if (GameObject* l = LookAt())
		j["lookAtTarget"] = l->GetName();
	return j;
}

// ---------------------------------------------------------------- Inspector
namespace
{
	// 단계별 고를 수 있는 컴포넌트 (Unity CinemachineCamera Inspector 의 Procedural Components)
	struct StageChoice { const char* Label; const char* Type; };
	const StageChoice kBody[] = { { "None", nullptr }, { "Follow", "CinemachineFollow" }, { "Orbital Follow", "CinemachineOrbitalFollow" },
		{ "Third Person Follow", "CinemachineThirdPersonFollow" } };
	const StageChoice kAim[] = { { "None", nullptr }, { "Rotation Composer", "CinemachineRotationComposer" }, { "Hard Look At", "CinemachineHardLookAt" },
		{ "Rotate With Follow Target", "CinemachineRotateWithFollowTarget" } };
	const StageChoice kNoise[] = { { "None", nullptr }, { "Basic Multi Channel Perlin", "CinemachineBasicMultiChannelPerlin" } };

	// 프레임 끝에 바꾼다 (Inspector 가 컴포넌트 목록을 도는 중)
	void ReplaceStage(GameObject* go, CmStage stage, const char* type)
	{
		SceneManager::GetI()->AddLastUpdate([go, stage, newType = std::string(type ? type : "")]() {
			if (!GameObject::IsAlive(go))
				return;
			Scene* scene = SceneManager::GetI()->GetCurrentScene();
			std::vector<Component*> old;
			for (auto& c : go->GetComponents())
				if (auto* b = dynamic_cast<CinemachineComponentBase*>(c.get()))
					if (b->Stage() == stage)
						old.push_back(c.get());
			for (Component* c : old)
				if (scene)
					scene->DestroyComponent(c);
			if (!newType.empty())
				if (std::shared_ptr<Component> c = ComponentFactory::Instance().CreateComponent(newType))
					go->AddComponent(c);
			Undo::SetActionName("Change Cinemachine Component");
			Undo::RequestCheck();
		});
	}

	template <size_t N>
	void StageDropdown(GameObject* go, const char* label, CmStage stage, CinemachineComponentBase* current, const StageChoice(&choices)[N])
	{
		int index = 0;
		const char* names[N];
		for (size_t i = 0; i < N; ++i)
		{
			names[i] = choices[i].Label;
			if (current && choices[i].Type && current->GetType() == choices[i].Type)
				index = (int)i;
		}
		if (UnityGUI::Dropdown(label, &index, names, (int)N))
			ReplaceStage(go, stage, choices[index].Type);
	}
}

void CinemachineCamera::PipelineInspector()
{
	UnityGUI::Label("Procedural Components", 0, true);
	StageDropdown(m_pGameObject, "Position Control", CmStage::Body, Pipeline(CmStage::Body), kBody);
	StageDropdown(m_pGameObject, "Rotation Control", CmStage::Aim, Pipeline(CmStage::Aim), kAim);
	StageDropdown(m_pGameObject, "Noise", CmStage::Noise, Pipeline(CmStage::Noise), kNoise);
}

void CinemachineCamera::OnInspectorGUI()
{
	const bool live = IsLive();
	UnityGUI::ValueLabel("Status", !IsEligible() ? "Disabled" : live ? "Live" : "Standby");
	UnityGUI::Int("Priority", &Priority);

	UnityGUI::Label("Lens", 0, true);
	bool lens = false;
	lens |= UnityGUI::Slider("Field Of View", &Lens.FieldOfView, 1.0f, 179.0f, 1);
	lens |= UnityGUI::Float("Orthographic Size", &Lens.OrthographicSize, 1);
	lens |= UnityGUI::Float("Near Clip Plane", &Lens.NearClipPlane, 1);
	lens |= UnityGUI::Float("Far Clip Plane", &Lens.FarClipPlane, 1);
	lens |= UnityGUI::Slider("Dutch", &Lens.Dutch, -180.0f, 180.0f, 1);
	if (lens)
		Validate();

	UnityGUI::Label("Target", 0, true);
	UnityGUI::GameObjectField("Tracking Target", &TrackingTarget, 1);
	UnityGUI::Toggle("Custom Look At Target", &CustomLookAtTarget, 1);
	if (CustomLookAtTarget)
		UnityGUI::GameObjectField("Look At Target", &LookAtTarget, 1);

	PipelineInspector();
	if (CmCore::Brains().empty())
		UnityGUI::HelpBox("No Cinemachine Brain in the scene. Add a Cinemachine Brain to the camera that should be driven (usually Main Camera).", true);
	if (TrackingTarget == 0 && (Pipeline(CmStage::Body) || Pipeline(CmStage::Aim)))
		UnityGUI::HelpBox("Position and Rotation Control need a Tracking Target.", true);
}

void CinemachineCamera::OnDrawGizmos()
{
	if (m_pGameObject == nullptr || !SceneViewOverlay::IsActive() || !IsEligible())
		return;
	const bool selected = SelectionManager::GetSelectedObjectType() == SelectionType::GAMEOBJECT && SelectionManager::GetSelectedGameObject() == m_pGameObject;
	const bool live = IsLive();
	const ImU32 color = live ? IM_COL32(255, 96, 96, 255) : selected ? IM_COL32(255, 255, 255, 255) : IM_COL32(150, 150, 150, 200);
	Transform* tr = m_pGameObject->GetTransform();
	const Matrix world = Matrix::CreateFromQuaternion(tr->GetRotation()) * Matrix::CreateTranslation(tr->GetPosition());
	SceneViewOverlay::DrawFrustum(world, Lens.NearClipPlane, selected ? 3.0f : 1.5f, Lens.FieldOfView, color);
	// 보는 대상까지 선
	if (selected)
		if (GameObject* look = LookAt())
		{
			const Vec3 a = tr->GetPosition(), b = look->GetTransform()->GetPosition();
			SceneViewOverlay::DrawLine(XMFLOAT3(a.x, a.y, a.z), XMFLOAT3(b.x, b.y, b.z), IM_COL32(255, 220, 60, 200), 1.0f);
		}
}

void CinemachineCamera::RemapFileIDs(const std::unordered_map<uint64, uint64>& map)
{
	auto remap = [&map](uint64& id) {
		auto it = map.find(id);
		if (it != map.end())
			id = it->second;
	};
	remap(TrackingTarget);
	remap(LookAtTarget);
}

GENERATE_COMPONENT_FUNC_TOJSON(CinemachineCamera)
{
	json j;
	j["type"] = "CinemachineCamera";
	j["enabled"] = m_Enabled;
	j["priority"] = Priority;
	j["trackingTarget"] = TrackingTarget;
	j["lookAtTarget"] = LookAtTarget;
	j["customLookAtTarget"] = CustomLookAtTarget;
	j["fieldOfView"] = Lens.FieldOfView;
	j["orthographicSize"] = Lens.OrthographicSize;
	j["nearClipPlane"] = Lens.NearClipPlane;
	j["farClipPlane"] = Lens.FarClipPlane;
	j["dutch"] = Lens.Dutch;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(CinemachineCamera)
{
	m_Enabled = j.value("enabled", true);
	Priority = j.value("priority", 0);
	TrackingTarget = j.value("trackingTarget", (uint64)0);
	LookAtTarget = j.value("lookAtTarget", (uint64)0);
	CustomLookAtTarget = j.value("customLookAtTarget", false);
	Lens.FieldOfView = j.value("fieldOfView", 60.0f);
	Lens.OrthographicSize = j.value("orthographicSize", 5.0f);
	Lens.NearClipPlane = j.value("nearClipPlane", 0.3f);
	Lens.FarClipPlane = j.value("farClipPlane", 1000.0f);
	Lens.Dutch = j.value("dutch", 0.0f);
	Validate();
	m_PreviousValid = false;
}
