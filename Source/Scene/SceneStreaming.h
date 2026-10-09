#pragma once

// 씬 · 에셋 스트리밍 (docs/SCENE_STREAMING.md): SceneManager.LoadSceneAsync 를 프레임을 멈추지 않게.
//  CLI: nova scenestream load | status | allow | stats (Play 중 불러오기를 몰고 단계별 시간을 읽는다)
#include <functional>

namespace SceneStreaming
{
	void RegisterEditor();

	// 미리 데우기 (Component::PrewarmStaged) 가 메인 프레임을 오래 잡을 계산을 백그라운드 잡으로 (예: Humanoid 아바타 표).
	//  미리 짓기는 이 잡이 다 끝나야 0.9 에 이른다. fn 은 메인이 아닌 스레드에서 돈다 — 바뀌지 않는 데이터만 읽을 것
	NOVA_API void QueuePrewarmJob(std::function<void()> fn);
	bool PrewarmJobsDone();
	void ClearPrewarmJobs();   // 끝나기를 기다린 뒤 비운다 (메인 미리 데우기도 버린다)

	// 메인 스레드에서 해야 하는 미리 데우기 (GPU — 나무 메시 올리기 · 껍질 텍스처 · 임포스터 굽기): 잡이 다 끝난 뒤 프레임 예산만큼 하나씩.
	//  다 하기 전에는 0.9 에 이르지 않는다 (예전: 바꿔 끼운 다음 프레임에 나무 종류마다 약 10 ms — Release 도시)
	NOVA_API void QueueMainPrewarm(std::function<void()> fn);
	bool RunMainPrewarms(double budgetMs);   // 남은 것이 없으면 true
	size_t MainPrewarmPending();
}
