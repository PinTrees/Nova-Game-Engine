#pragma once

// 비 · 눈 (Visual Effect Graph 에셋 — 파일 없이 메모리에, Vfx::SetLiveJson).
//  Visual Effect 를 카메라 자리에 두면 그 둘레 상자에서 태어난다 (World 공간 — 카메라가 움직여도 내리던 것은 제자리).
//  드러난 속성: Rain Rate · Far Rain Rate · Snow Rate (초당) · Wind (m/s 벡터)
//             Rain · Far Rain · Snow Offset (태어나는 상자 가운데 — 바람 위쪽으로 미리), Snow Life · Snow Life Max (초)
namespace WeatherFx
{
	constexpr const char* kAssetPath = "Packages/com.nova.weather/Precipitation.vfx";
	nlohmann::json Precipitation();
	void EnsureAsset();    // 처음 쓸 때 한 번 (메모리 에셋 등록)
	void ReleaseAsset();   // 패키지를 내릴 때
}
