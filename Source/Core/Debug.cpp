#include "pch.h"
#include "Debug.h"

deque<string> Debug::messageQueue = {};

string ToString(Vec3 vec)
{
    std::stringstream ss;
    ss << "(" << vec.x << ", " << vec.y << ", " << vec.z << ")";
    return ss.str();
}
