#pragma once

class InstancedBasicEffect;
class GameObject;

// Unity 의 Camera.cullingMask · Light.cullingMask (레이어 = GameObject 레이어 0..31, Tags and Layers).
//  - 화면 마스크: Game 뷰 = 그 카메라의 Culling Mask, Scene 뷰 = 모두. 렌더러들이 그리기 전에 Visible(go) 로 묻는다
//  - 그림자 패스: 빛마다 화면 마스크 & 그 빛의 Culling Mask (빛이 안 비추는 레이어는 그 빛의 그림자도 드리우지 않는다)
//  - 셰이더 (32. InstancedBasic.fx 의 ShadeLit): 빛마다 마스크 (gDirLightMask …) & 물체 레이어 비트 (gObjectLayer) 가 0 이면 그 빛을 건너뛴다
//    gObjectLayer 를 정하지 않는 그리기 (나무 · 바위 · 풀) 는 모든 빛을 받는다 (~0)
namespace RenderLayers
{
	NOVA_API void SetViewMask(uint32 mask);
	NOVA_API uint32 ViewMask();
	NOVA_API void SetPassMask(uint32 mask);        // 그림자 패스 동안 (~0 = 끔)
	NOVA_API uint32 ActiveMask();                  // 화면 & 패스
	NOVA_API bool Visible(GameObject* go);         // 지금 그려도 되나
	inline bool VisibleLayer(int layer) { return (ActiveMask() >> (layer & 31)) & 1u; }

	// 셰이더 상수
	NOVA_API void SetLightMasks(InstancedBasicEffect* fx, int scenePointLights, bool editor);   // LightManager 의 정렬된 빛 순서 (입자 빛 = 모두)
	NOVA_API void SetObjectLayer(InstancedBasicEffect* fx, uint32 layerBit);       // ~0 = 모든 빛
}
