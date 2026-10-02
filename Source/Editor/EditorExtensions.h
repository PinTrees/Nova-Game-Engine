#pragma once
#include <functional>
#include <string>
#include <vector>

// 패키지가 편집기에 붙이는 에셋 종류 (예: Animation 패키지의 .controller).
//  - Project 창: 아이콘 · Create 메뉴 항목 · 더블클릭
//  - Inspector: Project 에서 골랐을 때 그리는 함수
//  패키지를 내리면 UnregisterOwner(패키지 이름) 로 한꺼번에 뺀다.
namespace EditorExtensions
{
	struct AssetType
	{
		std::string Owner;            // 패키지 이름
		std::string Extension;        // ".controller" (소문자)
		std::string Icon;             // ProjectSetting/icons 이름
		std::string CreateMenu;       // Project 창 Create 메뉴 이름 (빈 문자열 = 없음)
		std::string DefaultName;      // 새 파일 이름 ("New Animator Controller")
		std::string DragPayload;      // 끌어 놓기 종류 ("CONTROLLER_FILE", 빈 문자열 = 기본 ASSET_FILE)
		std::function<void(const std::string& relPath)> Create;      // 새 파일을 만든다 (Assets\...)
		std::function<void(const std::string& relPath)> Open;        // 더블클릭
		std::function<void(const std::string& relPath)> Inspector;   // Project 에서 골랐을 때
	};

	NOVA_API void RegisterAssetType(const AssetType& type);
	NOVA_API void UnregisterOwner(const std::string& owner);
	NOVA_API const AssetType* FindAssetType(const std::string& extension);   // 대소문자 무시
	NOVA_API std::vector<const AssetType*> AssetTypes();
}
