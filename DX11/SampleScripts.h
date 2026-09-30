#pragma once
#include "MonoBehaviour.h"

// -------------------------------------------------------------
// Sample 1: Rotator (Automatically rotates the GameObject)
// -------------------------------------------------------------
class Rotator : public MonoBehaviour
{
public:
	// Smart Fields: 100% automated serialization & Inspector GUI!
	Field<float> rotateSpeed = { this, "Rotate Speed", 60.0f };
	Field<Vec3>  rotationAxis = { this, "Rotation Axis", Vec3(0, 1, 0) };

	void Update() override
	{
		Transform* tf = GetTransform();
		if (tf)
		{
			Vec3 euler = tf->GetLocalEulerAngles();
			float dt = Time::GetDeltaTime();
			euler.x += rotationAxis.Get().x * rotateSpeed.Get() * dt;
			euler.y += rotationAxis.Get().y * rotateSpeed.Get() * dt;
			euler.z += rotationAxis.Get().z * rotateSpeed.Get() * dt;
			tf->SetLocalEulerAngles(euler);
		}
	}
};

REGISTER_SCRIPT(Rotator)

// -------------------------------------------------------------
// Sample 2: PlayerController (WASD movement with Sprint)
// -------------------------------------------------------------
class PlayerController : public MonoBehaviour
{
public:
	// Smart Fields: 100% automated serialization & Inspector GUI!
	Field<float> moveSpeed = { this, "Move Speed", 5.0f };
	Field<float> sprintMultiplier = { this, "Sprint Multiplier", 2.0f };
	Field<bool>  invertY = { this, "Invert Y Axis", false };

	void Start() override
	{
		// Called on Play mode start!
	}

	void Update() override
	{
		Transform* tf = GetTransform();
		if (!tf) return;

		float speed = moveSpeed.Get();
		if (Input::GetKey(KEY::LSHIFT))
		{
			speed *= sprintMultiplier.Get();
		}

		float dt = Time::GetDeltaTime();
		Vec3 moveDir(0, 0, 0);

		if (Input::GetKey(KEY::W) || Input::GetKey(KEY::UP_ARROW))    moveDir += tf->GetForward();
		if (Input::GetKey(KEY::S) || Input::GetKey(KEY::DOWN_ARROW))  moveDir -= tf->GetForward();
		if (Input::GetKey(KEY::D) || Input::GetKey(KEY::RIGHT_ARROW)) moveDir += tf->GetRight();
		if (Input::GetKey(KEY::A) || Input::GetKey(KEY::LEFT_ARROW))  moveDir -= tf->GetRight();

		if (moveDir.LengthSquared() > 0.0001f)
		{
			moveDir.Normalize();
			Vec3 pos = tf->GetLocalPosition();
			pos += moveDir * speed * dt;
			tf->SetLocalPosition(pos);
		}
	}
};

REGISTER_SCRIPT(PlayerController)

// -------------------------------------------------------------
// Sample 3: SineWaveMover (Moves back and forth like a floating platform)
// -------------------------------------------------------------
class SineWaveMover : public MonoBehaviour
{
public:
	Field<float> frequency = { this, "Frequency", 2.0f };
	Field<float> amplitude = { this, "Amplitude", 2.0f };
	Field<Vec3>  moveAxis  = { this, "Move Axis", Vec3(0, 1, 0) };

private:
	float m_TotalTime = 0.0f;
	Vec3  m_InitialPos;

public:
	void Start() override
	{
		if (GetTransform())
		{
			m_InitialPos = GetTransform()->GetLocalPosition();
		}
	}

	void Update() override
	{
		if (!GetTransform()) return;

		m_TotalTime += Time::GetDeltaTime();
		float offset = sinf(m_TotalTime * frequency.Get()) * amplitude.Get();

		Vec3 pos = m_InitialPos;
		pos.x += moveAxis.Get().x * offset;
		pos.y += moveAxis.Get().y * offset;
		pos.z += moveAxis.Get().z * offset;

		GetTransform()->SetLocalPosition(pos);
	}
};

REGISTER_SCRIPT(SineWaveMover)
