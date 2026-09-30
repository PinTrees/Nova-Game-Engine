#include "pch.h"
#include "TaskSystem.h"

queue<function<void()>> TaskSystem::mainThreadTasks = {};

void TaskSystem::ExecuteMainThreadTasks()
{
    while (!mainThreadTasks.empty())
    {
        mainThreadTasks.front()();  // 작업 실행
        mainThreadTasks.pop();      // 큐에서 제거
    }
}
