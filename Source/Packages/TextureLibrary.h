#pragma once
#include <string>
#include <vector>
#include <map>
#include <nlohmann/json.hpp>

// 텍스처 팩 (패키지의 catalog.json) 의 재질 목록 — AI · CLI · 패키지가 이름으로 재질을 찾는다.
//  catalog.json: {"materials": [{"id": "roof/clay-tiles-weathered", "name", "category", "tags", "description", "tileMeters",
//                 "maps": {"color", "normal" (DirectX), "roughness", "ao", "height", "metalness" — 패키지 폴더 기준 .webp}, "source"}]}
//  id = "분류/이름" (소문자 · 하이픈). 프로젝트에 들어간 패키지만 (Packages/manifest.json) 읽는다.
//  CLI: nova textures list [--category roof] · search <말> · info <id> · categories
namespace TextureLibrary
{
	struct Material
	{
		std::string Id, Name, Category, Description, Package;
		std::vector<std::string> Tags;
		float TileMeters = 2.0f;
		std::map<std::string, std::wstring> Maps;   // "color" → 절대 경로
		nlohmann::json Source;
	};

	NOVA_API const std::vector<Material>& All();
	NOVA_API void Reload();                                     // 패키지를 넣고 뺀 뒤 (PackageManager 가 부른다)
	// id 그대로 → 대소문자 무시 id → 이름 → id 끝 (예 "slate" → "roof/slate")
	NOVA_API const Material* Find(const std::string& idOrName);
	// 낱말들이 id · 이름 · 분류 · 태그 · 설명에 얼마나 맞는지 (점수 순). category 가 있으면 그 분류만
	NOVA_API std::vector<const Material*> Search(const std::string& query, const std::string& category = "");
	NOVA_API std::vector<std::string> Categories();
	NOVA_API nlohmann::json ToJson(const Material& m, bool full);
}
