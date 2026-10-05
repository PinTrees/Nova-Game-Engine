#pragma once

class InstancedBasicEffect;

// 날씨의 덮개 맵 (com.nova.weather 가 WeatherState 로 켠다): 카메라 둘레 128 m 를 바로 위에서 본 깊이.
//  - 받는 쪽 (32. InstancedBasic.fx 의 ShadeLit): 맵의 깊이보다 아래 = 지붕 · 처마 · 나무 아래 → 젖지 않는다 · 웅덩이가 없다
//  - Visual Effect (58. VFX.fx 의 Kill Under Cover 블록): 빗방울이 지붕에 닿으면 그 높이에서 죽는다 (집 안에 비가 오지 않게)
//  - 그림자 캐스터 패스를 그대로 쓴다 (풀 · 디테일은 빼고). 8 m 칸으로 맞춰 카메라가 그만큼 움직이거나 30 프레임마다 다시 그린다
//  화면마다 하나 (0 = Game 뷰, 1 = Scene 뷰)
namespace WeatherCover
{
	struct Info
	{
		bool Valid = false;
		GfxShaderResourceView* Srv = nullptr;
		XMFLOAT4X4 ToTex;      // 월드 → (u, v, 깊이 0..1)
		float TopY = 0.0f;     // 깊이 0 의 높이
		float Range = 1.0f;    // 깊이 1 까지의 거리 (m)
		float InvSize = 0.0f;  // 1 / 맵 크기 (텍셀)
	};

	// 날씨가 젖음 · 비 · 눈을 쓸 때만 그린다 (아니면 아무것도 하지 않음). 부른 뒤 뷰포트 · 컬링은 호출자가 되돌린다
	void Render(GfxContext* dc, int view, const XMFLOAT3& viewer);
	// 받는 쪽 이펙트에 날씨 표면 값 + 덮개 맵 (날씨가 꺼져 있으면 0 — 이펙트에 남은 값을 지운다)
	void Bind(InstancedBasicEffect* fx, int view);
	const Info& Get(int view);
	size_t MemoryBytes();
}
