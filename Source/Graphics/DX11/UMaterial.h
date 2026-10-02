#pragma once
#include <nlohmann/json.hpp>
#include "ShaderSetting.h"
#include "PbrMaterial.h"

using json = nlohmann::json;

class TextureMgr;
class InstancedBasicEffect;

// 재질 에셋 (.mat, JSON). Unity URP 의 Lit / Unlit 셰이더 값.
//  - Surface Options: Alpha Clipping(Threshold), Receive Shadows
//  - Surface Inputs : Base Map + Color, Metallic Map / Metallic, Smoothness(+Source), Normal Map(+Scale),
//                     Occlusion Map(+Strength), Emission(Map, HDR Color, Intensity), Tiling / Offset
//  - Advanced       : Specular Highlights, Environment Reflections, Priority
// 예전 형식(Ambient/Diffuse/Specular)의 파일은 읽을 때 Base Color / Smoothness 로 바꾼다.
// 패키지 셰이더 (CustomShaders, 예: lilToon): "Shader" = 그 이름, 셰이더 값 = "Properties" (JSON), 패키지가 없으면 "Fallback" (Lit / Unlit) 으로 그린다.
class NOVA_API UMaterial
{
public:
	enum class ShaderKind { Lit = 0, Unlit = 1, Custom = 2 };

private:
	string m_ResourcePath;

	// 텍스처 (프로젝트 기준 경로)
	wstring m_BaseMapPath;
	wstring m_NormalMapPath;
	wstring m_MetallicMapPath;
	wstring m_OcclusionMapPath;
	wstring m_EmissionMapPath;
	ComPtr<GfxShaderResourceView> BaseMapSRV;
	ComPtr<GfxShaderResourceView> NormalMapSRV;
	ComPtr<GfxShaderResourceView> MetallicMapSRV;
	ComPtr<GfxShaderResourceView> OcclusionMapSRV;
	ComPtr<GfxShaderResourceView> EmissionMapSRV;

	// 예전 Blinn-Phong 값 (지형 등 예전 경로와 파일 호환용)
	XMFLOAT4 Ambient;
	XMFLOAT4 Diffuse;
	XMFLOAT4 Specular; // w = SpecPower
	XMFLOAT4 Reflect;

	ShaderSetting m_shaderSetting;

	// URP Lit
	ShaderKind m_Shader = ShaderKind::Lit;
	PbrMaterial m_Pbr;
	bool m_EmissionEnabled = false;
	XMFLOAT3 m_EmissionColor = XMFLOAT3(0, 0, 0);   // 감마 공간 색
	float m_EmissionIntensity = 1.0f;
	int m_Priority = 0;

	// 패키지 셰이더
	std::string m_CustomShader;
	json m_Properties = json::object();
	bool m_FallbackUnlit = false;
	uint64 m_PropertiesRevision = NextRevision();   // 전역에서 유일 (패키지가 재질 주소 + 이 값으로 캐시)

public:
	Material Mat;

	// ---- 패키지 셰이더 (CustomShaders)
	bool IsCustom() const { return m_Shader == ShaderKind::Custom; }
	const std::string& CustomShader() const { return m_CustomShader; }
	json& Properties() { return m_Properties; }
	const json& Properties() const { return m_Properties; }
	uint64 PropertiesRevision() const { return m_PropertiesRevision; }
	void TouchProperties() { m_PropertiesRevision = NextRevision(); }
	static uint64 NextRevision();
	// 셰이더 이름 바꾸기 (Lit / Unlit / 등록된 패키지 셰이더 이름)
	void SetShaderName(const std::string& name, const json& defaultProperties = json());
	std::string ShaderName() const;   // (패키지는 PropertiesRevision 이 바뀔 때만 Properties 를 다시 해석하면 된다)

public:
	UMaterial();
	~UMaterial();

	string GetName();
	const string& GetPath() const { return m_ResourcePath; }

	// 엔진 내장 기본 재질 (Unity 의 Default-Material 과 같은 밝은 회색). 씬에는 "builtin:Default-Material" 로 저장된다.
	static shared_ptr<UMaterial> GetDefault();
	static bool IsBuiltinPath(const string& path) { return path.rfind("builtin:", 0) == 0; }

	// 폴더에 새 재질을 만든다 (이미 있으면 "New Material 1" 처럼). 만든 경로(프로젝트 기준)를 돌려준다
	static string Create(string fullPath);
	static UMaterial* Load(string fullPath);
	static void Save(UMaterial* material);

	friend void from_json(const json& j, UMaterial& m);
	friend void to_json(json& j, const UMaterial& m);

	// 그리기 직전: 재질 값·텍스처를 InstancedBasic 이펙트에 넣는다. forPreview = 그림자/SSAO 끔 (재질 미리보기)
	void Apply(InstancedBasicEffect* fx, bool forPreview = false);
	// material 이 없으면 기본 재질로
	static void ApplyOrDefault(const shared_ptr<UMaterial>& material, InstancedBasicEffect* fx);

	void SetBaseMap(TextureMgr texMgr, wstring fullPath);
	// 저장된 경로로 텍스처를 다시 읽는다 (Undo 로 값을 되돌린 뒤)
	void ReloadTextures();
	void SetNormalMap(TextureMgr texMgr, wstring fullPath);

	GfxShaderResourceView* GetBaseMapSRV() { return BaseMapSRV.Get(); }
	GfxShaderResourceView* GetNormalMapSRV() { return NormalMapSRV.Get(); }
	ShaderSetting GetShaderSetting() { return m_shaderSetting; }
	const PbrMaterial& GetPbr() const { return m_Pbr; }
	ShaderKind GetShader() const { return m_Shader; }

	// Inspector: 재질 에셋을 골랐을 때(전체) 와 Mesh Renderer 아래(embedded = 머리글을 접을 수 있게)
	void OnInspectorGUI(bool embedded = false);

private:
	void SyncLegacy();   // m_Pbr → Mat (Diffuse = Base Color)
	friend class MaterialInspector;
};
