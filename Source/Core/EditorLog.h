#pragma once
#include <string>

// 에디터 전용 디버그 로그 (Unity 의 Editor.log 와 같은 역할).
//  - 사용자 Console(Debug::Log) 과 따로, 개발자가 문제를 진단할 때 읽는 파일: <실행 폴더>/Logs/Editor.log
//  - 실행할 때마다 새로 쓰고, 직전 실행 로그는 Editor-prev.log 로 남긴다.
//  - 한 줄 = [경과 시간] [분류] 내용. 여러 스레드에서 써도 안전하고 매 줄 바로 디스크에 쓴다(갑자기 멈춰도 남는다).
//  - C++ 런타임 assert 메시지와 처리되지 않은 예외(충돌)도 여기에 기록한다.
namespace EditorLog
{
	void Init();
	void Shutdown();
	void Write(const char* category, const char* format, ...);
	std::wstring GetFilePath();
	// 파일을 기본 텍스트 편집기로 연다 (Console 의 "Open Editor Log")
	void OpenInEditor();
}

#define EDITOR_LOG(category, ...) EditorLog::Write(category, __VA_ARGS__)
