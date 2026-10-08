#include "pch.h"
#include "TaskSystem.h"
#include "LockFree.h"

namespace
{
    LockFree::MpscQueue<std::function<void()>>& Queue()
    {
        static LockFree::MpscQueue<std::function<void()>> s_Queue;
        return s_Queue;
    }
}

void TaskSystem::Post(std::function<void()> task)
{
    if (task)
        Queue().Push(std::move(task));
}

void TaskSystem::ExecuteMainThreadTasks()
{
    std::function<void()> task;
    while (Queue().Pop(task))
        task();
}
