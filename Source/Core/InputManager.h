#pragma once

enum class KEY_STATE
{
	NONE,
	TAP, //누를떄
	HOLD,//누르고 있을때
	AWAY, // 똇을때
};

enum class KEY
{
	LEFT_ARROW,
	RIGHT_ARROW,
	UP_ARROW,
	DOWN_ARROW,

	A,
	B,
	C,
	D,
	E,
	F,
	G,
	H,
	I,
	J,
	K,
	L,
	M,
	N,
	O,
	P,
	Q,
	R,
	S,
	T,
	U,
	V,
	W,
	X,
	Y,
	Z,

	ALT,
	CTRL,
	LSHIFT,
	SPACE,
	ENTER,
	ESC,

	Mouse0,
	Mouse1,

	LAST
};

// 터치 (Unity 의 Touch · TouchPhase). 좌표 = 게임 화면 픽셀, GetMousePos 와 같이 왼쪽 위 0,0 (C# 에서는 Unity 처럼 왼쪽 아래)
enum class TouchPhase { Began, Moved, Stationary, Ended, Canceled };
struct Touch
{
	int fingerId = 0;
	Vec2 position;
	Vec2 deltaPosition;
	TouchPhase phase = TouchPhase::Began;
	int tapCount = 1;
};

struct tKeyInfo
{
	KEY_STATE	eState; //키의 상태값
	bool		bPrevPush;	//이전프레임에 눌렀는지 안눌렀는지
};

class NOVA_API InputManager
{
	SINGLE_HEADER(InputManager)

private:
	vector<tKeyInfo>	m_vecKey;
	Vec2				mvCurMousePos;
	float				mWheelAxis;
	vector<Touch>		m_Touches;


public:
	void Init();
	void Update();

public:
	KEY_STATE	GetKeyState(KEY _eKey) { return m_vecKey[(int)_eKey].eState; };
	Vec2		GetMousePos() { return mvCurMousePos; }

	void		SetWheelAxis(float amount) { mWheelAxis = amount; }

	// 터치: 플랫폼이 프레임마다 넣는다 (안드로이드. PC 는 Unity 처럼 비어 있다)
	const vector<Touch>& GetTouches() const { return m_Touches; }
	void		SetTouches(vector<Touch> touches) { m_Touches = std::move(touches); }
	float		GetWheelAxis()
	{
		float result = mWheelAxis;
		mWheelAxis = 0.f;
		return result;
	}
};

