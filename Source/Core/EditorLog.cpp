#include "pch.h"
#include "AutoSave.h"
#include "EditorLog.h"
#include <mutex>
#include <cstdarg>
#include <crtdbg.h>
#include <share.h>
#include <DbgHelp.h>
#include <atomic>
#include <thread>
#pragma comment(lib, "dbghelp.lib")

namespace
{
	std::mutex s_Lock;
	FILE* s_File = nullptr;
	std::wstring s_Path;
	ULONGLONG s_Start = 0;
	DWORD s_MainThread = 0;

	// C++ 런타임 assert/오류 보고를 로그에 남긴다 (대화 상자는 그대로 뜬다)
	int __cdecl CrtReportHook(int reportType, wchar_t* message, int* returnValue)
	{
		const char* kind = reportType == _CRT_ASSERT ? "ASSERT" : (reportType == _CRT_ERROR ? "CRT ERROR" : "CRT WARN");
		EditorLog::Write(kind, "%s", message ? wstring_to_string(message).c_str() : "(no message)");
		if (returnValue)
			*returnValue = 0;
		return FALSE;   // 기본 처리(대화 상자) 계속
	}

	// 호출 스택: 주소만 모은다 (StackWalk64) / 주소 → 함수 이름 + 파일:줄 (PDB 가 있으면)
	int CaptureStack(HANDLE thread, CONTEXT ctx, DWORD64* out, int max)
	{
		HANDLE process = ::GetCurrentProcess();
		static bool s_SymReady = false;
		if (!s_SymReady)
		{
			::SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
			s_SymReady = ::SymInitialize(process, nullptr, TRUE) != FALSE;
		}
		STACKFRAME64 frame = {};
		frame.AddrPC.Offset = ctx.Rip;
		frame.AddrPC.Mode = AddrModeFlat;
		frame.AddrFrame.Offset = ctx.Rbp;
		frame.AddrFrame.Mode = AddrModeFlat;
		frame.AddrStack.Offset = ctx.Rsp;
		frame.AddrStack.Mode = AddrModeFlat;
		int count = 0;
		while (count < max && ::StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &ctx, nullptr, ::SymFunctionTableAccess64, ::SymGetModuleBase64, nullptr) && frame.AddrPC.Offset != 0)
			out[count++] = frame.AddrPC.Offset;
		return count;
	}

	void WriteStack(const char* tag, const DWORD64* pcs, int count)
	{
		HANDLE process = ::GetCurrentProcess();
		for (int i = 0; i < count; ++i)
		{
			char buffer[sizeof(SYMBOL_INFO) + 256] = {};
			SYMBOL_INFO* sym = reinterpret_cast<SYMBOL_INFO*>(buffer);
			sym->SizeOfStruct = sizeof(SYMBOL_INFO);
			sym->MaxNameLen = 255;
			DWORD64 symOffset = 0;
			const char* name = ::SymFromAddr(process, pcs[i], &symOffset, sym) ? sym->Name : "?";
			IMAGEHLP_LINE64 line = {};
			line.SizeOfStruct = sizeof(line);
			DWORD lineOffset = 0;
			if (::SymGetLineFromAddr64(process, pcs[i], &lineOffset, &line))
				EditorLog::Write(tag, "  #%d %s  (%s:%lu)", i, name, line.FileName, line.LineNumber);
			else
				EditorLog::Write(tag, "  #%d %s  [%p]", i, name, (void*)pcs[i]);
		}
	}

	// 처리되지 않은 예외 (충돌): 코드와 주소, 그리고 호출 스택(함수 이름 + 파일:줄, PDB 가 있으면)을 남긴다
	LONG WINAPI CrashFilter(EXCEPTION_POINTERS* info)
	{
		if (info == nullptr || info->ExceptionRecord == nullptr)
			return EXCEPTION_CONTINUE_SEARCH;
		EditorLog::Write("CRASH", "unhandled exception 0x%08X at %p", (unsigned)info->ExceptionRecord->ExceptionCode, info->ExceptionRecord->ExceptionAddress);
		if (info->ContextRecord == nullptr)
			return EXCEPTION_CONTINUE_SEARCH;

		CONTEXT ctx = *info->ContextRecord;
		DWORD64 pcs[24];
		const int count = CaptureStack(::GetCurrentThread(), ctx, pcs, 24);
		WriteStack("CRASH", pcs, count);
		AutoSave::EmergencySave();   // 저장하지 않은 씬을 한 번 더 (실패해도 그대로 진행)
		return EXCEPTION_CONTINUE_SEARCH;
	}

	// ---------------------------------------------------------------- 멈춤 감시
	// 메인 루프가 4초 넘게 Heartbeat 를 부르지 않으면 메인 스레드를 잠깐 멈춰 호출 스택을 [HANG] 으로 남긴다.
	// (디버거 없이 무한 루프/교착을 찾기 위함. 모달 대화 상자나 창 크기 조절 중에도 한 번 남을 수 있다)
	std::atomic<ULONGLONG> s_Heartbeat{ 0 };
	std::atomic<bool> s_WatchdogStop{ false };

	void WatchdogLoop()
	{
		ULONGLONG reported = 0;
		while (!s_WatchdogStop)
		{
			::Sleep(500);
			const ULONGLONG beat = s_Heartbeat.load();
			if (beat == 0 || beat == reported || ::GetTickCount64() - beat < 4000)
				continue;
			reported = beat;
			HANDLE thread = ::OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, s_MainThread);
			if (thread == nullptr)
				continue;
			DWORD64 pcs[32];
			int count = 0;
			// 멈춘 동안에는 메모리 할당/로그 잠금을 쓰지 않는다 (메인 스레드가 그 잠금을 쥐고 있을 수 있다): 주소만 모으고 풀어 준 뒤 기록
			if (::SuspendThread(thread) != (DWORD)-1)
			{
				CONTEXT ctx = {};
				ctx.ContextFlags = CONTEXT_FULL;
				if (::GetThreadContext(thread, &ctx))
					count = CaptureStack(thread, ctx, pcs, 32);
				::ResumeThread(thread);
			}
			::CloseHandle(thread);
			EditorLog::Write("HANG", "main thread has not finished a frame for %.1f s", (::GetTickCount64() - beat) / 1000.0);
			WriteStack("HANG", pcs, count);
		}
	}
}

namespace EditorLog
{
	void Init()
	{
		std::lock_guard<std::mutex> g(s_Lock);
		if (s_File)
			return;
		std::error_code ec;
		std::filesystem::create_directories(L"Logs", ec);
		s_Path = std::filesystem::absolute(L"Logs\\Editor.log", ec).wstring();
		const std::wstring prev = std::filesystem::absolute(L"Logs\\Editor-prev.log", ec).wstring();
		std::filesystem::remove(prev, ec);
		std::filesystem::rename(s_Path, prev, ec);
		// 다른 프로그램(메모장, 진단 스크립트)이 실행 중에도 읽을 수 있게 쓰기만 막는다 (_wfopen_s 는 읽기도 막음)
		s_File = _wfsopen(s_Path.c_str(), L"w, ccs=UTF-8", _SH_DENYWR);
		s_Start = ::GetTickCount64();
		s_MainThread = ::GetCurrentThreadId();
		_CrtSetReportHookW2(_CRT_RPTHOOK_INSTALL, CrtReportHook);
		::SetUnhandledExceptionFilter(CrashFilter);
		s_WatchdogStop = false;
		std::thread(WatchdogLoop).detach();   // 프로세스가 끝나면 같이 끝난다 (join 하지 않음)
		if (s_File)
		{
			SYSTEMTIME t;
			::GetLocalTime(&t);
			fwprintf(s_File, L"NOVA Editor log  %04d-%02d-%02d %02d:%02d:%02d\n", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
			fflush(s_File);
		}
	}

	void Shutdown()
	{
		Write("App", "shutdown");
		s_WatchdogStop = true;
		std::lock_guard<std::mutex> g(s_Lock);
		_CrtSetReportHookW2(_CRT_RPTHOOK_REMOVE, CrtReportHook);
		if (s_File)
			fclose(s_File);
		s_File = nullptr;
	}

	void Write(const char* category, const char* format, ...)
	{
		char text[2048];
		va_list args;
		va_start(args, format);
		vsnprintf(text, sizeof(text), format, args);
		va_end(args);

		std::lock_guard<std::mutex> g(s_Lock);
		if (s_File == nullptr)
			return;
		const double seconds = (::GetTickCount64() - s_Start) / 1000.0;
		const DWORD tid = ::GetCurrentThreadId();
		const std::wstring line = string_to_wstring(text);
		if (tid == s_MainThread)
			fwprintf(s_File, L"[%8.3f] [%S] %s\n", seconds, category, line.c_str());
		else
			fwprintf(s_File, L"[%8.3f] [%S] (thread %lu) %s\n", seconds, category, tid, line.c_str());
		fflush(s_File);
	}

	void Heartbeat() { s_Heartbeat = ::GetTickCount64(); }

	std::wstring GetFilePath()
	{
		std::lock_guard<std::mutex> g(s_Lock);
		return s_Path;
	}

	void OpenInEditor()
	{
		const std::wstring path = GetFilePath();
		if (!path.empty())
			::ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	}
}
