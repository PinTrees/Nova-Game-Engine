#include "pch.h"
#include "Debug.h"
#include <ctime>

std::deque<LogEntry> Debug::s_Entries;
uint64_t Debug::s_Version = 0;

void Debug::Write(LogType type, const std::string& message, const std::string& stack, const std::string& file, int line, bool compile)
{
	LogEntry e;
	e.Type = type;
	e.Message = message;
	e.StackTrace = stack;
	e.File = file;
	e.Line = line;
	e.Compile = compile;
	std::time_t now = std::time(nullptr);
	std::tm tm = {};
	localtime_s(&tm, &now);
	char buf[16];
	std::strftime(buf, sizeof(buf), "[%H:%M:%S]", &tm);
	e.Time = buf;
	if (s_Entries.size() >= kMaxEntries)
		s_Entries.pop_front();
	// Unity 처럼 Console 내용은 Editor.log 에도 남긴다 (첫 줄 + 위치)
	{
		const size_t nl = e.Message.find('\n');
		const std::string first = nl == std::string::npos ? e.Message : e.Message.substr(0, nl);
		static const char* kType[] = { "Log", "Warning", "Error" };
		if (e.File.empty())
			EditorLog::Write("Console", "%s: %s", kType[(int)type], first.c_str());
		else
			EditorLog::Write("Console", "%s: %s  (%s:%d)", kType[(int)type], first.c_str(), e.File.c_str(), e.Line);
	}
	s_Entries.push_back(std::move(e));
	++s_Version;
	// Console 의 Error Pause: Play 중 오류가 나면 일시정지 (Unity)
	if (type == LogType::Error && ErrorPause && !compile && Application::IsPlaying())
		Application::SetPaused(true);
}

int Debug::Count(LogType type)
{
	int n = 0;
	for (const LogEntry& e : s_Entries)
		n += e.Type == type;
	return n;
}

void Debug::ClearAll()
{
	s_Entries.erase(std::remove_if(s_Entries.begin(), s_Entries.end(), [](const LogEntry& e) { return !e.Compile; }), s_Entries.end());
	++s_Version;
}

void Debug::ClearCompileMessages()
{
	s_Entries.erase(std::remove_if(s_Entries.begin(), s_Entries.end(), [](const LogEntry& e) { return e.Compile; }), s_Entries.end());
	++s_Version;
}

std::vector<std::string> Debug::GetAllLogs()
{
	std::vector<std::string> out;
	for (const LogEntry& e : s_Entries)
		out.push_back(e.Message);
	return out;
}

string ToString(Vec3 vec)
{
    std::stringstream ss;
    ss << "(" << vec.x << ", " << vec.y << ", " << vec.z << ")";
    return ss.str();
}
