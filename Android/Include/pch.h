#pragma once
// 안드로이드 빌드의 "pch.h" — 엔진 소스의 #include "pch.h" 가 이 파일을 찾는다 (Android/CMakeLists.txt 의 include 순서).
//  Windows 의 Source/Platform/pch.h 대신: STL + Windows/D3D11 대체 타입 + DirectXMath (ThirdParty) + 로그 · 문자열 도우미
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "WinCompat.h"
#include <DirectXMath.h>
#include <DirectXPackedVector.h>
using namespace DirectX;
using namespace std;   // Windows pch 와 같게 (엔진 코드가 기댄다)

#define NOVA_API

// EditorLog: logcat (태그 NOVA) + 앱 파일 폴더의 Editor.log
namespace EditorLog
{
	void Write(const char* category, const char* format, ...);
	inline void Heartbeat() {}
}
#define EDITOR_LOG(category, ...) EditorLog::Write(category, __VA_ARGS__)

std::wstring string_to_wstring(const std::string& str);
std::string wstring_to_string(const std::wstring& wstr);
