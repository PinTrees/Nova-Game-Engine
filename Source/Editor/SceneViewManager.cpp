#include "pch.h"
#include "SceneViewManager.h"
#include "SceneEditorWindow.h"
#include "EditorCamera.h"

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

	float camSpeed = 50;

	if (INPUT_KEY_HOLD(KEY::W))
		sceneCamera->Walk(camSpeed * DT);

	if (INPUT_KEY_HOLD(KEY::S))
		sceneCamera->Walk(-camSpeed * DT);

	if (INPUT_KEY_HOLD(KEY::A))
		sceneCamera->Strafe(-camSpeed * DT);

	if (INPUT_KEY_HOLD(KEY::D))
		sceneCamera->Strafe(camSpeed * DT);

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
