#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

// Unity 의 Tags and Layers (Project Settings > Tags and Layers, ProjectSettings/TagManager.json).
//  - 레이어 32 개. 번호는 Unity 와 같다: 0 Default, 1 TransparentFX, 2 Ignore Raycast, 4 Water, 5 UI (Builtin — 이름을 바꿀 수 없다)
//    3 · 6 ~ 31 = User Layer (이름을 붙여 쓴다, 빈 이름 = 쓰지 않음)
//  - 태그: Untagged, Respawn, Finish, EditorOnly, MainCamera, Player, GameController (Builtin) + 프로젝트 태그
//  - Sorting Layers: 스프라이트 그리기 층 (Default + 프로젝트, 순서 = 그리는 순서)
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

	// Sorting Layers (스프라이트 그리기 층): 목록 순서 = 그리는 순서 (앞 = 뒤에 그려짐 = 가장 뒤). "Default" (id 0) 는 늘 있다
	//  렌더러는 id 로 기억한다 (이름을 바꾸거나 순서를 옮겨도 그대로)
	struct SortingLayer { std::string Name; int Id = 0; };
	NOVA_API const std::vector<SortingLayer>& SortingLayers();
	NOVA_API int SortingLayerIndex(int id);                    // 그리는 순서 (없는 id = Default 의 순서)
	NOVA_API int SortingLayerIdFromName(const std::string& name);   // 없으면 -1
	NOVA_API std::string SortingLayerName(int id);
	NOVA_API int AddSortingLayer(const std::string& name);     // 새 id (같은 이름이 있으면 -1)
	NOVA_API bool RemoveSortingLayer(int id);                  // Default 는 지울 수 없다
	NOVA_API bool RenameSortingLayer(int id, const std::string& name);
	NOVA_API bool MoveSortingLayer(int id, int delta);         // -1 = 뒤로 (먼저 그림), +1 = 앞으로

	NOVA_API void Reload();   // 프로젝트를 바꿨을 때

	// 예전 번호 → Unity 번호 (3 Water → 4, 4 UI → 5)
	NOVA_API int MigrateLegacyLayer(int layer);
	NOVA_API uint32 MigrateLegacyMask(uint32 mask);
	// GameObject JSON (자식 · 컴포넌트의 includeLayers / excludeLayers 까지)
	NOVA_API void MigrateLegacyObjectJson(nlohmann::json& go);
}

// 예전 Camera · Light 의 Culling Mask 드롭다운 번호 (0 Everything, 1 Nothing, 2 Default, 3 TransparentFX, 4 Ignore Raycast, 5 Water, 6 UI) → 레이어 비트
inline uint32 LegacyCullingMask(int index)
{
	switch (index)
	{
	case 1: return 0u;
	case 2: return 1u << 0;
	case 3: return 1u << 1;
	case 4: return 1u << 2;
	case 5: return 1u << 4;
	case 6: return 1u << 5;
	default: return 0xFFFFFFFFu;
	}
}
