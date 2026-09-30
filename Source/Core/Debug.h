#pragma once

// Unity 의 Console 로그 (Debug.Log / LogWarning / LogError).
// 항목마다 종류, 메시지, 스택, 파일:줄(더블클릭하면 코드 편집기에서 열림)을 담는다.
enum class LogType { Log = 0, Warning = 1, Error = 2 };

struct LogEntry
{
	LogType Type = LogType::Log;
	std::string Message;
	std::string StackTrace;
	std::string File;      // 절대 경로 (없으면 빈 문자열)
	int Line = 0;
	std::string Time;      // [HH:MM:SS]
	bool Compile = false;  // 스크립트 컴파일 오류/경고 (다시 컴파일하면 지운다)
};

class Debug
{
private:
	static std::deque<LogEntry> s_Entries;
	static uint64_t s_Version;
	static const size_t kMaxEntries = 2000;

public:
	// Console 옵션 (Unity 와 같은 기본값)
	static inline bool ClearOnPlay = true;
	static inline bool ErrorPause = false;
	static inline bool Collapse = false;

	static void Log(const std::string& message) { Write(LogType::Log, message); }
	static void LogWarning(const std::string& message) { Write(LogType::Warning, message); }
	static void LogError(const std::string& message) { Write(LogType::Error, message); }
	static void Write(LogType type, const std::string& message, const std::string& stack = std::string(),
		const std::string& file = std::string(), int line = 0, bool compile = false);

	static const std::deque<LogEntry>& Entries() { return s_Entries; }
	static uint64_t Version() { return s_Version; }   // 항목이 바뀔 때마다 증가 (Console 자동 스크롤)
	static int Count(LogType type);
	static void ClearAll();                              // 컴파일 오류는 남긴다 (Unity 의 Clear 와 같음)
	static void ClearCompileMessages();

	// 이전 코드 호환: 메시지만
	static std::vector<std::string> GetAllLogs();
};


string ToString(Vec3 vec);
