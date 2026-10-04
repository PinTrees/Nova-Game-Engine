#pragma once
#include <string>

// 안드로이드: 엔진 PathManager 의 대체 (검사 장면이 쓰는 엔진 경로만). 셰이더는 경로의 이름만 보고 APK 의 assets/Shaders 에서 찾는다
class PathManager
{
public:
	static PathManager* GetI() { static PathManager p; return &p; }
	std::wstring GetEnginePathW() const { return L"/nova/"; }
};
