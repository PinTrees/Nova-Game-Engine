#pragma once
#include <functional>

// 메인 스레드에서 돌릴 작업 (다른 스레드 · 잡이 넣고, 메인이 프레임마다 ExecuteMainThreadTasks 로 돌린다).
//  무잠금 MPSC 큐 (LockFree.h) — 예전에는 동기화 없는 std::queue 라 다른 스레드에서 넣으면 깨질 수 있었다
class TaskSystem
{
public:
    static void Post(std::function<void()> task);   // 아무 스레드
    static void ExecuteMainThreadTasks();           // 메인 스레드만
};
