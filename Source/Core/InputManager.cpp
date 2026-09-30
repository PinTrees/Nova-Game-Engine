#include "pch.h"
#include "InputManager.h"

SINGLE_BODY(InputManager)

int g_arrVK[(int)KEY::LAST] =
{
	VK_LEFT,
	VK_RIGHT,
	VK_UP,
	VK_DOWN,

	'A',
	'B',
	'C',
	'D',
	'E',
	'F',
	'G',
	'H',
	'I',
	'J',
	'K',
	'L',
	'M',
	'N',
	'O',
	'P',
	'Q',
	'R',
	'S',
	'T',
	'U',
	'V',
	'W',
	'X',
	'Y',
	'Z',

	VK_MENU,	   	//ALT,
	VK_CONTROL,	   	//CTRL,
	VK_LSHIFT,	   	//LSHIFT,
	VK_SPACE,	   	//SPACE,
	VK_RETURN,	   	//ENTER
	VK_ESCAPE,   	//ESC,

	VK_LBUTTON,
	VK_RBUTTON,

	//
	//LAST
};

InputManager::InputManager()
{

}
InputManager::~InputManager()
{

}

void InputManager::Init()
{
	//초기화 반복문 
	for (int i = 0; i < (int)KEY::LAST; ++i)
	{
		m_vecKey.push_back(tKeyInfo{ KEY_STATE::NONE,false });
	}
}

void InputManager::Update()
{
	// 윈도우 포커싱 알아내기

	//HWND hMainWnd = CCore::GetI()->GetMainHwnd();
	HWND hWnd = GetFocus();

	// 윈도우 포커싱 중일때 키 이벤트 동작
	if (nullptr != hWnd)
	{

		for (int i = 0; i < (int)KEY::LAST; ++i)
		{
			if (GetAsyncKeyState(g_arrVK[i]) & 0x8000)
			{
				if (m_vecKey[i].bPrevPush)
				{
					//이전에도 눌려있었으면
					m_vecKey[i].eState = KEY_STATE::HOLD;
				}
				else
				{
					m_vecKey[i].eState = KEY_STATE::TAP;
				}
				//현재프레임 눌려있었으니 true
				m_vecKey[i].bPrevPush = true;
			}
			else// 키가 안눌려있었을때
			{
				if (m_vecKey[i].bPrevPush)
				{
					// 이전에 눌려있었다.
					m_vecKey[i].eState = KEY_STATE::AWAY;
				}
				else
				{
					m_vecKey[i].eState = KEY_STATE::NONE;

				}
				m_vecKey[i].bPrevPush = false;
			}
		}
		//Mouse 위치 계산
		POINT ptPos = {};
		GetCursorPos(&ptPos);
		ScreenToClient(Application::GetI()->GetMainHwnd(), &ptPos);

		mvCurMousePos = Vec2((float)ptPos.x, (float)ptPos.y);
	}
	// 윈도우 포커싱 해제 상태
	else
	{
		for (int i = 0; i < (int)KEY::LAST; ++i)
		{
			m_vecKey[i].bPrevPush = false;

			if (KEY_STATE::TAP == m_vecKey[i].eState || KEY_STATE::HOLD == m_vecKey[i].eState)
			{
				m_vecKey[i].eState = KEY_STATE::AWAY;
			}
			else if (KEY_STATE::AWAY == m_vecKey[i].eState)
			{
				m_vecKey[i].eState = KEY_STATE::NONE;
			}
		}
	}
}

