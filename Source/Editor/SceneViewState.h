#pragma once

class EditorCamera;

// Scene 뷰 카메라 시점 저장 (Unity 처럼 씬마다 마지막 시점을 기억).
//  - 프로젝트의 UserSettings/SceneView.json 에 씬 경로별 위치 + 바라보는 방향
//  - 씬이 바뀌면(에디터 시작 포함) 그 씬의 저장된 시점으로 되돌린다 (없으면 그대로)
//  - 카메라가 움직였다가 0.5 초 멈추면 저장 (에디터가 갑자기 꺼져도 마지막 시점이 남는다)
namespace SceneViewState
{
	void Update(EditorCamera* camera);   // Scene 뷰가 매 프레임
}
