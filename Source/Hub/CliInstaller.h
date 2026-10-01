#pragma once
#include <string>

// NOVA CLI 설치 (Hub > 설치 탭).
//  - 엔진 Binaries\nova.exe → %LOCALAPPDATA%\NOVA\CLI\nova.exe 복사 + engine.json(엔진 exe 위치: nova open 이 쓴다)
//  - 사용자 PATH(HKCU\Environment\Path)에 그 폴더를 더하고 WM_SETTINGCHANGE 알림 → 새로 연 터미널에서 `nova` 로 바로 실행
//  - 검사용: NOVA_CLI_INSTALL_DIR = 설치 폴더 바꾸기, NOVA_CLI_NO_PATH = PATH 를 건드리지 않기
namespace CliInstaller
{
	struct Status
	{
		bool SourceFound = false;   // 엔진에 nova.exe 가 있음 (빌드됨)
		bool Installed = false;
		bool UpToDate = false;      // 설치본이 엔진의 nova.exe 와 같음
		bool OnPath = false;        // 사용자 PATH 에 설치 폴더가 있음
		std::wstring Dir;           // 설치 폴더
	};

	Status Query();
	bool Install(std::string& error);
	bool Uninstall(std::string& error);
}
