#include "pch.h"
#include "VectorUtils.h"

Vec3 Normalize(Vec3 vec)
{
    vec.Normalize();
    return vec;
}

float Dot(Vec3 vec1, Vec3 vec2)
{
    return vec1.Dot(vec2); 
}

Vec3 Cross(Vec3 vec1, Vec3 vec2)
{
    return vec1.Cross(vec2);
}

Vec3 Clamp(const Vec3& value, const Vec3& min, const Vec3& max)
{
    return Vec3(
        max(min.x, min(value.x, max.x)), 
        max(min.y, min(value.y, max.y)), 
        max(min.z, min(value.z, max.z)) 
    );
}

// Fixed
Quaternion RotateByScaledVector(const Quaternion& quat, const Vector3& vec, float scale)
{
    return quat * Quaternion(vec.x * scale, vec.y * scale, vec.z * scale, 0.0f);
}
