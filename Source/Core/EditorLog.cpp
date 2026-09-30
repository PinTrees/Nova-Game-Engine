#include "pch.h"
#include "EditorLog.h"
#include <mutex>
#include <cstdarg>
#include <crtdbg.h>
#include <share.h>
#include <DbgHelp.h>
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

	// 처리되지 않은 예외 (충돌): 코드와 주소, 그리고 호출 스택(함수 이름 + 파일:줄, PDB 가 있으면)을 남긴다
	LONG WINAPI CrashFilter(EXCEPTION_POINTERS* info)
	{
		if (info == nullptr || info->ExceptionRecord == nullptr)
			return EXCEPTION_CONTINUE_SEARCH;
		EditorLog::Write("CRASH", "unhandled exception 0x%08X at %p", (unsigned)info->ExceptionRecord->ExceptionCode, info->ExceptionRecord->ExceptionAddress);
		if (info->ContextRecord == nullptr)
			return EXCEPTION_CONTINUE_SEARCH;

		HANDLE process = ::GetCurrentProcess();
		HANDLE thread = ::GetCurrentThread();
		::SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
		::SymInitialize(process, nullptr, TRUE);
		CONTEXT ctx = *info->ContextRecord;
		STACKFRAME64 frame = {};
		frame.AddrPC.Offset = ctx.Rip;
		frame.AddrPC.Mode = AddrModeFlat;
		frame.AddrFrame.Offset = ctx.Rbp;
		frame.AddrFrame.Mode = AddrModeFlat;
		frame.AddrStack.Offset = ctx.Rsp;
		frame.AddrStack.Mode = AddrModeFlat;
		for (int i = 0; i < 24; ++i)
		{
			if (!::StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &ctx, nullptr, ::SymFunctionTableAccess64, ::SymGetModuleBase64, nullptr) || frame.AddrPC.Offset == 0)
				break;
			char buffer[sizeof(SYMBOL_INFO) + 256] = {};
			SYMBOL_INFO* sym = reinterpret_cast<SYMBOL_INFO*>(buffer);
			sym->SizeOfStruct = sizeof(SYMBOL_INFO);
			sym->MaxNameLen = 255;
			DWORD64 symOffset = 0;
			const char* name = ::SymFromAddr(process, frame.AddrPC.Offset, &symOffset, sym) ? sym->Name : "?";
			IMAGEHLP_LINE64 line = {};
			line.SizeOfStruct = sizeof(line);
			DWORD lineOffset = 0;
			if (::SymGetLineFromAddr64(process, frame.AddrPC.Offset, &lineOffset, &line))
				EditorLog::Write("CRASH", "  #%d %s  (%s:%lu)", i, name, line.FileName, line.LineNumber);
			else
				EditorLog::Write("CRASH", "  #%d %s  [%p]", i, name, (void*)frame.AddrPC.Offset);
		}
		::SymCleanup(process);
		return EXCEPTION_CONTINUE_SEARCH;
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
