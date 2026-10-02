#include "pch.h"
#include "RenderLayers.h"
#include "Effects.h"
#include "LightManager.h"

namespace
{
	uint32 s_View = 0xFFFFFFFFu;
	uint32 s_Pass = 0xFFFFFFFFu;

	// 이펙트마다 변수 찾기 (이름으로 찾는 비용을 한 번만)
	struct Vars { FxVar* Dir = nullptr; FxVar* Spot = nullptr; FxVar* Point = nullptr; FxVar* Object = nullptr; };
	std::unordered_map<FxEffect*, Vars> s_Vars;

	Vars& VarsOf(InstancedBasicEffect* fx)
	{
		FxEffect* e = fx->GetFX();
		auto it = s_Vars.find(e);
		if (it != s_Vars.end())
			return it->second;
		Vars v;
		auto find = [&](const char* name) -> FxVar* {
			FxVar* var = e->GetVariableByName(name);
			return var && var->IsValid() ? var : nullptr;
		};
		v.Dir = find("gDirLightMask");
		v.Spot = find("gSpotLightMask");
		v.Point = find("gPointLightMask");
		v.Object = find("gObjectLayer");
		return s_Vars.emplace(e, v).first->second;
	}
}

namespace RenderLayers
{
	void SetViewMask(uint32 mask) { s_View = mask; }
	uint32 ViewMask() { return s_View; }
	void SetPassMask(uint32 mask) { s_Pass = mask; }
	uint32 ActiveMask() { return s_View & s_Pass; }

	bool Visible(GameObject* go)
	{
		return go == nullptr || VisibleLayer(go->GetLayerIndex());
	}

	void SetLightMasks(InstancedBasicEffect* fx, int scenePointLights, bool editor)
	{
		if (fx == nullptr)
			return;
		uint32 dir[4] = { ~0u, ~0u, ~0u, ~0u }, spot[4] = { ~0u, ~0u, ~0u, ~0u }, point[4] = { ~0u, ~0u, ~0u, ~0u };
		int nd = 0, ns = 0, np = 0;
		// GetSortedLights 순서 = 셰이더 배열 순서 (종류마다 따로 세면 된다)
		for (const shared_ptr<Light>& l : editor ? LightManager::GetI()->GetSortedEditorLights() : LightManager::GetI()->GetSortedLights())
		{
			if (!l) continue;
			const uint32 m = l->GetCullingMaskBits();
			switch (l->GetLightType())
			{
			case LightType::Directional: if (nd < 4) dir[nd++] = m; break;
			case LightType::Spot: if (ns < 4) spot[ns++] = m; break;
			case LightType::Point: if (np < 4 && np < scenePointLights) point[np++] = m; break;
			default: break;
			}
		}
		Vars& v = VarsOf(fx);
		if (v.Dir) v.Dir->SetRawValue(dir, 0, sizeof(dir));
		if (v.Spot) v.Spot->SetRawValue(spot, 0, sizeof(spot));
		if (v.Point) v.Point->SetRawValue(point, 0, sizeof(point));
		SetObjectLayer(fx, ~0u);
	}

	void SetObjectLayer(InstancedBasicEffect* fx, uint32 layerBit)
	{
		if (fx == nullptr)
			return;
		Vars& v = VarsOf(fx);
		if (v.Object)
			v.Object->SetRawValue(&layerBit, 0, sizeof(layerBit));
	}
}
