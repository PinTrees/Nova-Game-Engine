#pragma once
#include <string>
#include <vector>
#include <memory>
#include <nlohmann/json.hpp>

// Unity URP 의 Volume 프레임워크 (데이터).
//  - VolumeParameter : 값 하나 + "Override" 체크 (체크한 값만 섞인다)
//  - VolumeComponent : 효과 하나 (Bloom, Tonemapping, ...) = 파라미터 목록
//  - VolumeProfile   : 효과 목록을 담는 에셋 (.volumeprofile, JSON)
//  - VolumeStack     : 카메라 위치에서 모든 Volume 을 우선순위/가중치/거리로 섞은 최종 값
// 파라미터는 종류와 무관하게 float[4] 로 보관한다 → 저장/섞기/Inspector 를 한 코드로 처리.

enum class VolumeParamKind
{
	Float,      // Min 이상 (Max 무시)
	Clamped,    // Min..Max 슬라이더
	Int,        // Min..Max 정수
	Bool,
	Enum,
	Color,      // HDR 가능한 색 (rgb)
	Vector2,
};

struct VolumeParameter
{
	std::string Key;        // 저장 이름 (예: "intensity")
	std::string Label;      // Inspector 표시 이름
	VolumeParamKind Kind = VolumeParamKind::Float;
	float Min = 0.0f, Max = 1.0f;
	std::vector<std::string> Items;   // Enum 항목
	bool Override = false;
	float Value[4] = { 0, 0, 0, 0 };
	// Inspector: 같은 효과의 다른 칸 (Enum) 이 이 값일 때만 보인다 (예: Depth Of Field 의 Gaussian 칸 · Bokeh 칸)
	std::string ShowIfKey;
	int ShowIfValue = 0;

	float F() const { return Value[0]; }
	int I() const { return (int)(Value[0] + 0.5f); }
	bool B() const { return Value[0] > 0.5f; }
};

class VolumeComponent
{
public:
	std::string Type;          // "Bloom" 등 (저장 키)
	std::string DisplayName;   // "Bloom"
	std::string Category;      // Add Override 메뉴의 분류 ("Post-processing")
	bool Active = true;        // 헤더 체크박스
	std::vector<VolumeParameter> Params;

	VolumeParameter* Find(const std::string& key);
	const VolumeParameter* Find(const std::string& key) const;
	float F(const std::string& key) const;
	bool B(const std::string& key) const;
	int I(const std::string& key) const;
	const float* V(const std::string& key) const;

	// 파라미터 기본값으로 된 새 컴포넌트 (알 수 없는 타입이면 nullptr)
	static std::unique_ptr<VolumeComponent> Create(const std::string& type);
	// Add Override 메뉴에 보이는 타입 목록 (Unity URP 순서)
	static const std::vector<std::string>& Types();
};

class VolumeProfile
{
public:
	std::string Path;   // 프로젝트 기준 상대 경로 (Assets\...)
	std::vector<std::unique_ptr<VolumeComponent>> Components;

	VolumeComponent* Get(const std::string& type);
	const VolumeComponent* Get(const std::string& type) const;
	bool Has(const std::string& type) const { return Get(type) != nullptr; }
	VolumeComponent* Add(const std::string& type);
	void Remove(const std::string& type);

	std::string Name() const;
	nlohmann::json ToJson() const;
	void FromJson(const nlohmann::json& j);
	std::string ToJsonString() const { return ToJson().dump(); }
	void ApplyJson(const std::string& text);
	bool Save() const;

	// 경로로 읽기 (같은 경로는 같은 객체, 파일이 바뀌면 다시 읽음). 없으면 nullptr
	static std::shared_ptr<VolumeProfile> Load(const std::string& path);
	// 새 에셋 만들기 (이미 있으면 " 1" 등을 붙인다). 만든 상대 경로를 돌려준다
	static std::string CreateAsset(const std::string& directoryOrPath, const std::string& baseName = "New Volume Profile");
};

// 섞인 최종 값: 각 효과 타입의 기본값 위에 Volume 들이 덮어쓴 결과
class VolumeStack
{
public:
	std::vector<std::unique_ptr<VolumeComponent>> Components;

	void Reset();   // 모든 효과를 기본값으로
	const VolumeComponent* Get(const std::string& type) const;
	VolumeComponent* Get(const std::string& type);
	// 이 효과가 실제로 무언가를 하는지 (예: Bloom intensity > 0)
	bool IsActive(const std::string& type) const;
};
