#pragma once
#include <string>

// 자동 저장 + 충돌 복구 (Unreal 의 Auto Save / Crash Recovery 와 같은 방식).
//  - 에디터가 켜져 있는 동안 <프로젝트>/Library/AutoSave/session_<pid>.json 을 둔다 (정상 종료하면 지운다).
//  - Interval 마다(기본 5 분), 그리고 Play 를 시작할 때 저장하지 않은 변경이 있으면 지금 씬을 autosave_<pid>.scene 으로 쓴다.
//    원래 씬 파일은 건드리지 않는다. 충돌 순간에도 한 번 시도한다(EmergencySave).
//  - 다음 시작 때 주인이 없는 session 파일(= 충돌로 끝남) + 씬 파일보다 새 autosave 가 있으면 복구할지 묻는다.
//    Recover = 그 내용을 원래 경로의 씬으로 연다(변경됨 표시 — Ctrl+S 로 저장), Discard = 지운다.
//  - 지형 높이맵 등 씬 밖 데이터(.terraindata)는 복구 대상이 아니다.
namespace AutoSave
{
	void Init();          // 시작 씬을 연 뒤 (지난 세션 확인 + 이번 세션 표시)
	void Update();        // 매 프레임 (편집 중에만)
	// 모델 원본 (FBX · VRM · GLB) 이 바뀌면 다시 가져오기 (1 초마다 확인, Play 중이 아닐 때) — Unity 의 자동 Reimport
	void WatchModels();
	void Shutdown();      // 정상 종료
	void OnEnterPlay();   // Play 직전: 변경이 있으면 저장
	void EmergencySave(); // 충돌 처리기
	void DrawRecoveryPrompt();

	bool SaveNow(std::string* error = nullptr);   // 지금 저장 (변경이 없어도) — CLI / 시험
	bool Enabled();
	void SetEnabled(bool enabled);
	int IntervalMinutes();
	void SetIntervalMinutes(int minutes);
	std::wstring Folder();                        // <프로젝트>/Library/AutoSave
	bool HasPendingRecovery();
	std::string PendingRecoveryInfo();            // "씬 이름 · 시각" (CLI)
	bool Recover(bool accept);                    // 묻는 창 없이 (CLI / 시험)
}
