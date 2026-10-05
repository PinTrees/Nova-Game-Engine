#pragma once
#include <string>
#include <nlohmann/json.hpp>

// Unity 의 Import Settings Inspector: Project 창에서 고른 텍스처 · 모델(FBX) · 오디오의 가져오기 설정을 바꾸고 Apply / Revert.
//  - 텍스처: Max Size · Compression · Generate Mip Maps + 가져온 결과(크기 · 형식) + 미리보기
//  - 모델: Model(Scale Factor) / Rig(Animation Type, Humanoid 본 매핑 고치기) / Animation(Import Animation, 클립 목록) 탭
//  - 오디오: Force To Mono · Load Type
// Apply = `<파일>.meta` 저장 → 그 에셋 캐시를 비우고 씬을 다시 만들어 바로 반영 (Play 중에는 막는다)
namespace ImportSettingsInspector
{
	// fullPath = 디스크 전체 경로. 가져오기 설정이 없는 파일이면 false (아무것도 그리지 않음)
	bool Draw(const std::wstring& fullPath);
	// .meta 를 저장하고 다시 가져온다 (CLI 도 쓴다). 실패하면 false + error
	bool Apply(const std::wstring& fullPath, const nlohmann::json& settings, std::string& error);
	// 캐시를 비우고 씬을 다시 만든다 (.meta 는 그대로)
	void Reimport(const std::wstring& fullPath);
	// 다음 프레임 처음에 Apply (Inspector 를 그리는 중에 씬을 다시 만들지 않게 — 재질 Inspector 의 Fix Now 등)
	void ApplyDeferred(const std::wstring& fullPath, const nlohmann::json& settings);
	void Update();   // EditorGUIManager::Update 가 프레임마다
	// 텍스처를 Normal map 으로 (재질 Inspector 의 "Fix Now")
	void MarkAsNormalMap(const std::wstring& fullPath);
	// 높이 맵 (재질 · Terrain Layer 의 "Fix Now"): sRGB 끄기 (감마로 휘지 않게) + High Quality (BC7 — BC1 은 높이가 계단이 된다)
	void MarkAsHeightMap(const std::wstring& fullPath);
	// 이 텍스처가 높이 맵으로 알맞게 가져와졌는가 (가져오기 설정이 없거나 선형이면 true)
	bool IsLinearHeightMap(const std::wstring& fullPath);
}
