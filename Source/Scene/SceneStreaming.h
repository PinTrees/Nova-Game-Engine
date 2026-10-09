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
	void ClearPrewarmJobs();   // 끝나기를 기다린 뒤 비운다
}
