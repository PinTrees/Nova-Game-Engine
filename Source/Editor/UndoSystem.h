#pragma once
#include <functional>
#include <string>

// Unity 의 Undo / Redo (Ctrl+Z / Ctrl+Y, Edit 메뉴).
//
// 기록 방식
//  - 씬: 마우스/키를 떼거나 입력 칸을 벗어나는 등 "조작이 끝난 순간" 씬 JSON 을 직전 확정 상태와 비교해 바뀌었으면 한 단계로 기록한다.
//    Inspector 값, 이동/회전/크기 핸들, 생성·삭제·복제·부모 변경·이름·컴포넌트 추가/제거가 모두 같은 방법으로 잡힌다.
//    되돌리기는 씬을 그 JSON 으로 다시 만들고 선택/펼침 상태는 GameObject 의 fileID 로 이어 간다.
//  - 에셋(Animator Controller, 재질 등): 편집 중인 창이 매 프레임 WatchAsset 으로 알려 주면 같은 방식으로 비교한다.
//  - 지형 브러시처럼 JSON 이 아닌 큰 데이터: 도구가 Push 로 직접 기록한다 (바뀐 영역만).
//  - Play 중에는 기록하지 않는다 (Stop 하면 씬이 Play 직전으로 돌아가므로).
namespace Undo
{
	struct Record
	{
		std::string Name;                     // Edit 메뉴에 "Undo <Name>" 으로 표시
		std::function<void()> UndoAction;
		std::function<void()> RedoAction;
		size_t Bytes = 0;                     // 메모리 한도 계산용
	};

	void Push(Record record);                 // 새 기록 (Redo 목록은 비운다)
	bool PerformUndo();
	bool PerformRedo();
	bool CanUndo();
	bool CanRedo();
	std::string UndoName();
	std::string RedoName();
	void Clear();

	// 다음에 확정되는 씬 변경의 이름 (예: "Move", "Create GameObject"). 도구/메뉴가 부른다
	void SetActionName(const std::string& name);
	// 조작이 끝나지 않았어도 이번 프레임에 확정 검사를 한다 (메뉴 명령 등)
	void RequestCheck();

	// JSON 상태로 읽고 되돌릴 수 있는 에셋을 감시한다 (편집 중인 창이 매 프레임 부른다)
	void WatchAsset(const std::string& key, const std::string& label,
		std::function<std::string()> capture, std::function<void(const std::string&)> restore);

	// 매 프레임 (ImGui 프레임 안, 창들을 그린 뒤): 단축키 처리 + 조작이 끝났으면 변경 확정
	void Update();

	// 실행한 Undo/Redo 수 (검사용)
	int HistoryCount();

	// 마지막으로 확정한 씬 JSON 의 해시 (확정할 때만 다시 계산). 씬을 추적하지 않으면 false
	//  SceneManager 가 "저장 안 된 변경(*)" 판단에 쓴다 → 매 프레임 씬 전체를 직렬화하지 않는다
	bool CommittedSceneHash(size_t& outHash);
}
