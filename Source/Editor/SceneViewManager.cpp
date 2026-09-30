#include "pch.h"
#include "SceneViewManager.h"
#include "SceneEditorWindow.h"
#include "EditorCamera.h"
#include "SceneToolbar.h"

SINGLE_BODY(SceneViewManager)

SceneViewManager::SceneViewManager()
{

}

SceneViewManager::~SceneViewManager()
{

}

void SceneViewManager::Update()
{
	if (m_LastActiveSceneEditorWindow == nullptr)
		return;

	EditorCamera* sceneCamera = m_LastActiveSceneEditorWindow->GetSceneCamera();
	if (sceneCamera == nullptr)
		return;

	// 시야각/클리핑 변경 반영 (Scene Camera 설정)
	SceneToolbar::ApplyCameraLens(sceneCamera);
	SceneToolbar::SceneCameraSettings& cfg = SceneToolbar::CameraSettings();

	// Unity 처럼 우클릭을 누르고 있을 때만 WASD(+Q/E 아래/위) 로 비행한다 (도구 단축키 W/E/R 와 충돌 방지)
	const bool flyMode = INPUT_KEY_HOLD(KEY::Mouse1);
	Vec3 input(0, 0, 0);
	if (flyMode)
	{
		if (INPUT_KEY_HOLD(KEY::W)) input.z += 1.0f;
		if (INPUT_KEY_HOLD(KEY::S)) input.z -= 1.0f;
		if (INPUT_KEY_HOLD(KEY::D)) input.x += 1.0f;
		if (INPUT_KEY_HOLD(KEY::A)) input.x -= 1.0f;
		if (INPUT_KEY_HOLD(KEY::E)) input.y += 1.0f;
		if (INPUT_KEY_HOLD(KEY::Q)) input.y -= 1.0f;

		// 비행 중 휠 = 속도 조절 (Unity)
		const float wheel = ImGui::GetIO().MouseWheel;
		if (wheel != 0.0f)
			cfg.Speed = std::clamp(cfg.Speed * powf(1.15f, wheel), cfg.SpeedMin, cfg.SpeedMax);
	}

	// 기본 20 유닛/초 × 속도, Shift = 4배, 가속 = 누르고 있을수록 최대 6배
	static float s_HoldTime = 0.0f;
	static Vec3 s_Velocity(0, 0, 0);
	const bool moving = input.LengthSquared() > 0.0f;
	s_HoldTime = moving ? s_HoldTime + DT : 0.0f;
	float speed = 20.0f * cfg.Speed;
	if (ImGui::GetIO().KeyShift)
		speed *= 4.0f;
	if (cfg.Acceleration)
		speed *= (std::min)(1.0f + s_HoldTime * 1.5f, 6.0f);
	if (moving)
		input.Normalize();
	const Vec3 target = input * speed;
	if (cfg.Easing)
		s_Velocity = Vec3::Lerp(s_Velocity, target, 1.0f - expf(-DT * 12.0f));
	else
		s_Velocity = target;
	if (s_Velocity.LengthSquared() < 1e-6f)
		s_Velocity = Vec3(0, 0, 0);
	else
	{
		sceneCamera->Walk(s_Velocity.z * DT);
		sceneCamera->Strafe(s_Velocity.x * DT);
		XMFLOAT3 pos = sceneCamera->GetPosition();
		sceneCamera->SetPosition(pos.x, pos.y + s_Velocity.y * DT, pos.z);
	}

	if (INPUT_KEY_DOWN(KEY::Mouse1))
	{
		m_LastMousePos = MOUSE_POSITION;
	}

	if (INPUT_KEY_HOLD(KEY::Mouse1))
	{
		Vec2 curMousePos = MOUSE_POSITION;

		float dx = XMConvertToRadians(0.25f * static_cast<float>(curMousePos.x - m_LastMousePos.x));
		float dy = XMConvertToRadians(0.25f * static_cast<float>(curMousePos.y - m_LastMousePos.y));

		sceneCamera->Pitch(dy);
		sceneCamera->RotateY(dx);

		m_LastMousePos = curMousePos;
	}

	sceneCamera->UpdateViewMatrix();
}
