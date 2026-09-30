#pragma once

class Debug
{
private:
    static deque<std::string> messageQueue; 
    static const size_t maxQueueSize = 100; 

public:
    static std::vector<std::string> GetAllLogs()
    {
        std::vector<std::string> logs(messageQueue.begin(), messageQueue.end());
        return logs;
    }
    static void ClearAll()
    {
        messageQueue.clear();
    }

public:
    static void Log(const string message)
    {
#ifdef _DEBUG 
        AddMessageToQueue(message);
#endif
    }

private:
    static void AddMessageToQueue(const std::string& message)
    {
        if (messageQueue.size() >= maxQueueSize)
        {
            messageQueue.pop_front();
        }
        messageQueue.push_back(message);
    }
};


string ToString(Vec3 vec);