#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

// Unity 의 ScriptableObject 에셋 (.asset) — 씬과 따로 저장하는 데이터 (무기 · 아이템 · 적 스탯 · 설정).
//  파일 (JSON): { "type": "ScriptableObject", "class": "WeaponData", "fields": { "damage": 12, ... } }
//  값 형식은 C# 필드 직렬화와 같다 (Bridge.WriteValue). 게임은 C# 이 읽고 (같은 경로 = 같은 인스턴스), 편집기는 Inspector · CLI 가 고친다.
//  Resources: 프로젝트의 아무 "Resources" 폴더 안 파일을 그 폴더 기준 경로 (확장자 없이) 로 찾는다 (C# Resources.Load)
namespace ScriptableAssets
{
	// relPath = "Assets\\Data\\Sword.asset" (프로젝트 기준). 읽기 실패 · ScriptableObject 아님 = false
	bool Read(const std::string& relPath, std::string& className, nlohmann::json& fields);
	bool Write(const std::string& relPath, const std::string& className, const nlohmann::json& fields);
	// 새 에셋 (클래스 기본값). path 가 폴더면 그 안에 "<FileName>.asset" (겹치면 번호). 만든 상대 경로, 실패 = 빈 문자열
	std::string Create(const std::string& path, const std::string& className, std::string* error = nullptr);
	// 그 클래스 (또는 하위 클래스) 의 .asset 전부 (상대 경로, 정렬). baseClass 비우면 모든 ScriptableObject 에셋
	std::vector<std::string> FindAll(const std::string& baseClass);
	// 클래스 기본값 위에 파일 값을 덮은 전체 값 (Inspector · CLI get)
	nlohmann::json ResolvedFields(const std::string& className, const nlohmann::json& fields);
	// Resources 폴더 안 경로 → 파일들 (all = 그 폴더 아래 전부)
	std::vector<std::string> FindResources(const std::string& path, bool all);
	// 편집기: .asset Inspector 등록
	void RegisterEditor();
}
