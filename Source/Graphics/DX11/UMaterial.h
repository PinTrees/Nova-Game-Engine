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
class UMaterial
{
public:
	enum class ShaderKind { Lit = 0, Unlit = 1 };

private:
	string m_ResourcePath;

	// 텍스처 (프로젝트 기준 경로)
	wstring m_BaseMapPath;
	wstring m_NormalMapPath;
	wstring m_MetallicMapPath;
	wstring m_OcclusionMapPath;
	wstring m_EmissionMapPath;
	ComPtr<ID3D11ShaderResourceView> BaseMapSRV;
	ComPtr<ID3D11ShaderResourceView> NormalMapSRV;
	ComPtr<ID3D11ShaderResourceView> MetallicMapSRV;
	ComPtr<ID3D11ShaderResourceView> OcclusionMapSRV;
	ComPtr<ID3D11ShaderResourceView> EmissionMapSRV;

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

public:
	Material Mat;

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

	ID3D11ShaderResourceView* GetBaseMapSRV() { return BaseMapSRV.Get(); }
	ID3D11ShaderResourceView* GetNormalMapSRV() { return NormalMapSRV.Get(); }
	ShaderSetting GetShaderSetting() { return m_shaderSetting; }
	const PbrMaterial& GetPbr() const { return m_Pbr; }
	ShaderKind GetShader() const { return m_Shader; }

	// Inspector: 재질 에셋을 골랐을 때(전체) 와 Mesh Renderer 아래(embedded = 머리글을 접을 수 있게)
	void OnInspectorGUI(bool embedded = false);

private:
	void SyncLegacy();   // m_Pbr → Mat (Diffuse = Base Color)
	friend class MaterialInspector;
};
