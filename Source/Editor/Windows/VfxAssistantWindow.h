#pragma once
#include "EditorWindow.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

// Window > VFX Assistant — 대화로 Visual Effect 를 만든다.
//  - 이 PC 에 설치되어 로그인된 Claude Code (claude CLI) 를 headless (-p, stream-json) 로 실행한다. API 키는 쓰지 않는다
//    (자식 프로세스 환경에서 ANTHROPIC_API_KEY 를 지운다 → 구독 로그인으로만). Claude Code 가 없으면 동작하지 않는다
//  - Claude 가 쓸 수 있는 도구는 nova vfx · nova screenshot · nova camera · nova create visual-effect 와 파일 읽기뿐
//    (--allowedTools) → 그래프 창 · 장면이 바로 따라온다
//  - 대화는 이어진다 (--resume 세션), New Chat 으로 새로
class VfxAssistantWindow : public EditorWindow
{
public:
	VfxAssistantWindow();
	~VfxAssistantWindow();
	static VfxAssistantWindow* Instance() { return s_Instance; }
	static void Open(const std::string& assetPath);
	// nova vfx assistant.send --message "..." · assistant.status · assistant.stop (자동 검사 · 터미널에서)
	static bool CliOp(const std::string& op, const nlohmann::json& args, nlohmann::json& result, std::string& error);
	void OnRender() override;

	// 대화 한 줄 (사용자 · Claude 글 · 도구 호출 · 도구 결과 · 오류)
	struct Entry
	{
		enum Kind { User, Text, Tool, ToolResult, Error, Info } Type = Info;
		std::string Body;
	};

	// claude 실행 파일 (claude.exe 또는 node + cli.js). 못 찾으면 false
	struct Runner { std::wstring Exe; std::wstring Script; std::string Version; };
	static bool FindClaude(Runner& out, std::string& why);

protected:
	void BeforeBegin() override;
	ImVec2 WindowPaddingOverride() const override { return ImVec2(8.0f, 6.0f); }

private:
	static VfxAssistantWindow* s_Instance;
	bool m_FocusPending = false;

	struct Job;   // 실행 중인 claude 프로세스 (읽기 스레드)
	std::shared_ptr<Job> m_Job;

	std::string m_Asset;               // 대화가 다루는 .vfx (그래프 창의 것)
	std::vector<Entry> m_Log;
	std::string m_Session;             // Claude Code 세션 (이어 말하기)
	char m_Input[4096] = {};
	bool m_ScrollToEnd = false;
	int m_Probe = 0;                   // 0 아직, 1 찾음, -1 없음
	Runner m_Runner;
	std::string m_ProbeError;
	std::chrono::steady_clock::time_point m_Started;
	double m_CostUsd = 0.0;            // Claude Code 가 알려 주는 값 (구독이면 참고용)

	void Send(const std::string& text);
	void Stop();
	void Poll();
	void DrawLog(float height);
};
