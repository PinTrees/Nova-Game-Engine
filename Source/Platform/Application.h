#pragma once

class App;
class GameObject;

class Application
{
	SINGLE_HEADER(Application)

private:
	static bool isPlaying;
	static bool isPaused;
	static bool stepRequested;

private:
	App* m_pCurrApp;

public:
	void SetApp(App* app) { m_pCurrApp = app; }
	App* GetApp() { return m_pCurrApp; }

public:
	HWND GetMainHwnd();
	HINSTANCE GetInstance();
	ID3D11Device* GetDevice();
	ID3D11DeviceContext* GetDeviceContext();

public:
	static wstring GetDataPath();
	// 빌드된 게임(플레이어)로 실행 중인지 (에디터 창 없이 첫 씬을 바로 Play)
	static inline bool isPlayer = false;
	// --no-activate (nova open --background): 창을 띄우되 앞으로 가져오지 않는다 (작업 중인 창의 포커스를 뺏지 않게)
	static inline bool noActivate = false;
	static bool IsPlayer() { return isPlayer; }
	static void SetPlaying(bool active) { isPlaying = active; }
	static bool IsPlaying() { return isPlaying; }

	// Pause / Step (Unity 툴바)
	static void SetPaused(bool paused) { isPaused = paused; }
	static bool IsPaused() { return isPaused; }
	static void RequestStep() { stepRequested = true; }
	// 재생 중이고 (일시정지가 아니거나 Step 요청이 있을 때) 이번 프레임에 게임 로직을 갱신해야 하는지
	static bool ShouldUpdateGame()
	{
		if (!isPlaying) return false;
		if (!isPaused) return true;
		bool step = stepRequested;
		stepRequested = false;
		return step;
	}
};

