#pragma once


class TaskSystem
{
public:
    static queue<function<void()>> mainThreadTasks;

public:
    static void ExecuteMainThreadTasks();
};

