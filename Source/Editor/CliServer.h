#pragma once
#include <functional>
#include <string>
#include <nlohmann/json.hpp>

// NOVA CLI 서버 (Unity CLI 처럼 터미널·AI 가 실행 중인 에디터를 다룬다).
//  - 이름 있는 파이프 \\.\pipe\nova-editor-<pid> 에서 듣는다 (이 PC 안에서만, 포트·방화벽 없음).
//    파이프 이름·임의 토큰은 %LOCALAPPDATA%\NOVA\Instances\<pid>.json 에 적는다 (그 사용자 폴더 →
//    토큰을 읽을 수 있는 같은 사용자의 프로세스만 명령을 보낼 수 있다)
//  - 요청 = 한 줄 JSON {"token", "id", "cmd", "args"}, 응답 = 한 줄 JSON {"id", "ok", "result" | "error"}
//  - 명령은 메인 스레드에서 실행한다 (Pump, 프레임마다). 에디터가 포커스를 잃어 멈춰 있어도 요청이 오면 깨어나
//    프레임을 돌린다 → 창을 앞으로 가져오거나 마우스로 조작하지 않아도 된다
namespace CliServer
{
	using json = nlohmann::json;
	// 성공하면 true (result 채움), 실패면 false (error 채움). 메인 스레드에서 불린다
	using Handler = std::function<bool(const json& args, json& result, std::string& error)>;

	// delayFrames: 실행 전에 기다릴 프레임 수 (예: 스크린샷은 앞 명령의 변경이 그려진 뒤)
	// beforePresent: 프레임 끝이 아니라 백버퍼를 다 그린 뒤 Present 직전에 실행 (에디터 전체 캡처)
	void Register(const std::string& cmd, const std::string& help, Handler handler, int delayFrames = 0, bool beforePresent = false);

	void Start();                    // 에디터가 프로젝트를 연 뒤 한 번
	void Stop();                     // 종료 때 (인스턴스 파일 삭제)
	bool HasWork();                  // 처리할 요청이 있거나, 처리 뒤 화면을 몇 프레임 더 그려야 함
	void WaitForWork(unsigned ms);   // 멈춘 동안: 요청이 오면 바로 깨어난다
	void Pump();                     // 메인 스레드, 프레임 끝
	void PumpBeforePresent();        // 메인 스레드, Present 직전
	const std::wstring& PipeName();
	const json& Commands();          // 등록된 명령 목록 ({cmd: help}) — "help" 명령이 돌려준다
}
