#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

// Unity 의 Tags and Layers (Project Settings > Tags and Layers, ProjectSettings/TagManager.json).
//  - 레이어 32 개. 번호는 Unity 와 같다: 0 Default, 1 TransparentFX, 2 Ignore Raycast, 4 Water, 5 UI (Builtin — 이름을 바꿀 수 없다)
//    3 · 6 ~ 31 = User Layer (이름을 붙여 쓴다, 빈 이름 = 쓰지 않음)
//  - 태그: Untagged, Respawn, Finish, EditorOnly, MainCamera, Player, GameController (Builtin) + 프로젝트 태그
//  - 옛 파일 (이 엔진의 예전 번호 3 Water, 4 UI) 은 불러올 때 Unity 번호로 옮긴다 (scene · prefab 의 "layerFormat" 이 없으면)
namespace TagsAndLayers
{
	constexpr int kLayerCount = 32;
	constexpr int kLayerFormat = 2;   // scene · prefab 파일에 적는 레이어 번호 형식
	enum BuiltinLayer { Default = 0, TransparentFX = 1, IgnoreRaycast = 2, Water = 4, UI = 5 };

	NOVA_API const std::string& LayerName(int layer);          // "" = 이름 없음
	NOVA_API bool IsBuiltinLayer(int layer);
	NOVA_API bool SetLayerName(int layer, const std::string& name);   // User Layer 만, 저장한다
	NOVA_API int NameToLayer(const std::string& name);         // 없으면 -1
	NOVA_API std::vector<int> NamedLayers();                   // 이름이 있는 레이어 번호 (작은 것부터)

	NOVA_API const std::vector<std::string>& Tags();           // Builtin + 프로젝트 태그
	NOVA_API bool IsBuiltinTag(const std::string& tag);
	NOVA_API bool AddTag(const std::string& tag);              // 저장한다
	NOVA_API bool RemoveTag(const std::string& tag);

	NOVA_API void Reload();   // 프로젝트를 바꿨을 때

	// 예전 번호 → Unity 번호 (3 Water → 4, 4 UI → 5)
	NOVA_API int MigrateLegacyLayer(int layer);
	NOVA_API uint32 MigrateLegacyMask(uint32 mask);
	// GameObject JSON (자식 · 컴포넌트의 includeLayers / excludeLayers 까지)
	NOVA_API void MigrateLegacyObjectJson(nlohmann::json& go);
}
