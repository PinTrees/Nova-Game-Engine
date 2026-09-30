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

struct tKeyInfo
{
	KEY_STATE	eState; //키의 상태값
	bool		bPrevPush;	//이전프레임에 눌렀는지 안눌렀는지
};

class InputManager
{
	SINGLE_HEADER(InputManager)

private:
	vector<tKeyInfo>	m_vecKey;
	Vec2				mvCurMousePos;
	float				mWheelAxis;


public:
	void Init();
	void Update();

public:
	KEY_STATE	GetKeyState(KEY _eKey) { return m_vecKey[(int)_eKey].eState; };
	Vec2		GetMousePos() { return mvCurMousePos; }

	void		SetWheelAxis(float amount) { mWheelAxis = amount; }
	float		GetWheelAxis()
	{
		float result = mWheelAxis;
		mWheelAxis = 0.f;
		return result;
	}
};

