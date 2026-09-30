#pragma once
#include "InputManager.h"
#include <string>

class Input
{
public:
	static bool GetKey(KEY key)
	{
		KEY_STATE state = InputManager::GetI()->GetKeyState(key);
		return state == KEY_STATE::HOLD || state == KEY_STATE::TAP;
	}

	static bool GetKeyDown(KEY key)
	{
		return InputManager::GetI()->GetKeyState(key) == KEY_STATE::TAP;
	}

	static bool GetKeyUp(KEY key)
	{
		return InputManager::GetI()->GetKeyState(key) == KEY_STATE::AWAY;
	}

	static bool GetMouseButton(int button)
	{
		KEY k = (button == 0) ? KEY::Mouse0 : KEY::Mouse1;
		return GetKey(k);
	}

	static bool GetMouseButtonDown(int button)
	{
		KEY k = (button == 0) ? KEY::Mouse0 : KEY::Mouse1;
		return GetKeyDown(k);
	}

	static bool GetMouseButtonUp(int button)
	{
		KEY k = (button == 0) ? KEY::Mouse0 : KEY::Mouse1;
		return GetKeyUp(k);
	}

	static Vec2 GetMousePosition()
	{
		return InputManager::GetI()->GetMousePos();
	}

	static float GetWheel()
	{
		return InputManager::GetI()->GetWheelAxis();
	}

	static float GetAxis(const std::string& axisName)
	{
		if (axisName == "Horizontal")
		{
			float val = 0.0f;
			if (GetKey(KEY::D) || GetKey(KEY::RIGHT_ARROW)) val += 1.0f;
			if (GetKey(KEY::A) || GetKey(KEY::LEFT_ARROW)) val -= 1.0f;
			return val;
		}
		else if (axisName == "Vertical")
		{
			float val = 0.0f;
			if (GetKey(KEY::W) || GetKey(KEY::UP_ARROW)) val += 1.0f;
			if (GetKey(KEY::S) || GetKey(KEY::DOWN_ARROW)) val -= 1.0f;
			return val;
		}
		return 0.0f;
	}
};
