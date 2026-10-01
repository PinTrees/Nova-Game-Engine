// NOVA 실행 파일: 엔진 본체는 NovaCore.dll (Unity 의 UnityPlayer.dll 처럼). 빌드한 게임은 이 파일이 <제품>.exe 가 된다.
// 아이콘·버전 리소스(NovaEngine.rc)는 이 실행 파일에 있다 (엔진은 GetModuleHandle(nullptr) 로 읽는다).
#include <windows.h>

extern "C" __declspec(dllimport) int NovaMain(HINSTANCE hInstance, int showCmd);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, PSTR, int showCmd)
{
	return NovaMain(hInstance, showCmd);
}
