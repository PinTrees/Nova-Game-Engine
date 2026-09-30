#pragma once


struct DirectionalLight
{
	DirectionalLight() { ZeroMemory(this, sizeof(this)); }
	void Init()
	{
		Ambient = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
		Diffuse = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
		Specular = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
		Direction = XMFLOAT3(0.0f, 0.0f, 0.0f);
	}

	XMFLOAT4 Ambient;
	XMFLOAT4 Diffuse;
	XMFLOAT4 Specular;
	XMFLOAT3 Direction;
	float Pad; // Pad the last float so we can set an array of lights if we wanted.
};

struct PointLight
{
	PointLight() { ZeroMemory(this, sizeof(this)); }
	void Init()
	{
		Ambient = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
		Diffuse = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
		Specular = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
		Position = XMFLOAT3(0.0f, 0.0f, 0.0f);
		Range = 10.0f;
		Att = XMFLOAT3(0.0f, 0.0f, 0.0f);
	}

	XMFLOAT4 Ambient;
	XMFLOAT4 Diffuse;
	XMFLOAT4 Specular;

	// Packed into 4D vector: (Position, Range)
	XMFLOAT3 Position;
	float Range;

	// Packed into 4D vector: (A0, A1, A2, Pad)
	XMFLOAT3 Att; // 조명의 감쇠 특성을 나타내는 3D 벡터
	float Pad; // Pad the last float so we can set an array of lights if we wanted.
};

struct SpotLight
{
	SpotLight() { ZeroMemory(this, sizeof(this)); }
	void Init()
	{
		Ambient = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
		Diffuse = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
		Specular = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
		Position = XMFLOAT3(0.0f, 0.0f, 0.0f);
		Range = 10.0f;
		Direction = XMFLOAT3(0.0f, 0.0f, 0.0f);
		Spot = 60.0f;
		Att = XMFLOAT3(0.0f, 0.0f, 0.0f);
	}
	XMFLOAT4 Ambient;
	XMFLOAT4 Diffuse;
	XMFLOAT4 Specular;

	// Packed into 4D vector: (Position, Range)
	XMFLOAT3 Position;
	float Range;

	// Packed into 4D vector: (Direction, Spot)
	XMFLOAT3 Direction;
	float Spot;

	// Packed into 4D vector: (Att, Pad)
	XMFLOAT3 Att; // 조명의 감쇠 특성을 나타내는 3D 벡터
	float Pad; // Pad the last float so we can set an array of lights if we wanted.
};

struct Material
{
	Material() { ZeroMemory(this, sizeof(this)); }

	XMFLOAT4 Ambient;
	XMFLOAT4 Diffuse;
	XMFLOAT4 Specular; // w = SpecPower
	XMFLOAT4 Reflect;
};