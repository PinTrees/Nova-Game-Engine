#include "pch.h"
#include "JsonUtility.h"

float SafeGetFloat(const json& j, const std::string& key, size_t index, float defaultValue)
{
    try
    {
        float value = j.at(key).at(index).get<float>();
        if (std::isnan(value) || value == 0.0f)
        {
            return defaultValue;
        }
        return value;
    }
    catch (const std::exception& e)
    {
        // Log the error if needed
        return defaultValue;
    }
}

Vector3 SafeGetVector3(const json& j, const std::string& key, const Vector3& defaultValue)
{
    return {
        SafeGetFloat(j, key, 0, defaultValue.x),
        SafeGetFloat(j, key, 1, defaultValue.y),
        SafeGetFloat(j, key, 2, defaultValue.z)
    };
}

json QuaternionToJSON(const Quaternion& q) 
{
    return json{ {"x", q.x}, {"y", q.y}, {"z", q.z}, {"w", q.w} };
}
 
Quaternion SafeGetQuaternion(const json& j, const std::string& key, const Quaternion& defaultValue)
{
    if (j.contains(key) && j[key].is_object())
    {
        const json& q = j[key];
        return Quaternion(q.value("x", defaultValue.x),
            q.value("y", defaultValue.y),
            q.value("z", defaultValue.z),
            q.value("w", defaultValue.w));
    }
    return defaultValue;
} 