#pragma once
#include <windows.h>
#include "DirectXMath.h"
#include "SimpleMath.h"

using int8 = __int8;
using int16 = __int16;
using int32 = __int32;
using int64 = __int64;
using uint8 = unsigned __int8;
using uint16 = unsigned __int16;
using uint32 = unsigned __int32;
using uint64 = unsigned __int64;

using Color = DirectX::XMFLOAT4;

using Vector2 = DirectX::SimpleMath::Vector2;
using Vector3 = DirectX::SimpleMath::Vector3;
using Vector4 = DirectX::SimpleMath::Vector4;

using Vec2 = DirectX::SimpleMath::Vector2;
using Vec3 = DirectX::SimpleMath::Vector3;
using Vec4 = DirectX::SimpleMath::Vector4;
using Matrix = DirectX::SimpleMath::Matrix;
using Quaternion = DirectX::SimpleMath::Quaternion;
using Ray = DirectX::SimpleMath::Ray;

// MeshID / MaterialID / ShaderSettingID
using InstanceID = std::tuple<uint64, uint64, uint64>;

struct BouncingBall
{
	Vec3 center;
	float radius;
};

struct Plane
{
	DirectX::XMFLOAT3 normal; // 면의 법선 벡터
	float d;                  // 평면의 거리 (ax + by + cz + d = 0)
};

struct Frustum
{
	Plane planes[6]; // 6개의 면 (left, right, top, bottom, near, far)
};

enum class LightType
{
	Directional,
	Spot,
	Point,
	End
};