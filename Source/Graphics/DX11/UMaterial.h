#pragma once
#include <nlohmann/json.hpp>
#include "ShaderSetting.h"

using json = nlohmann::json;

class TextureMgr;

class UMaterial
{
private:
	string m_ResourcePath;

	wstring m_BaseMapPath;
	wstring m_NormalMapPath;
	ComPtr<ID3D11ShaderResourceView> BaseMapSRV;
	ComPtr<ID3D11ShaderResourceView> NormalMapSRV;
	
	XMFLOAT4 Ambient;
	XMFLOAT4 Diffuse;
	XMFLOAT4 Specular; // w = SpecPower
	XMFLOAT4 Reflect;

	ShaderSetting m_shaderSetting;

	// base
public:
	Material Mat;

public:
	UMaterial();
	~UMaterial();

public:
	string GetName();

public:
	// 엔진 내장 기본 재질 (Unity 의 Default-Material 과 같은 밝은 회색). 씬에는 "builtin:Default-Material" 로 저장된다.
	static shared_ptr<UMaterial> GetDefault();
	static bool IsBuiltinPath(const string& path) { return path.rfind("builtin:", 0) == 0; }

	static void Create(string fullPath);
	static UMaterial* Load(string fullPath);
	static void Save(UMaterial* material);

	friend void from_json(const json& j, UMaterial& m);
	friend void to_json(json& j, const UMaterial& m);

public:
	void SetBaseMap(TextureMgr texMgr, wstring fullPath);
	// 저장된 경로로 텍스처를 다시 읽는다 (Undo 로 값을 되돌린 뒤)
	void ReloadTextures();
	void SetNormalMap(TextureMgr texMgr, wstring fullPath);

	ID3D11ShaderResourceView* GetBaseMapSRV() { return BaseMapSRV.Get(); }
	ID3D11ShaderResourceView* GetNormalMapSRV() { return NormalMapSRV.Get(); }

	ShaderSetting GetShaderSetting() { return m_shaderSetting; }

public:
	void OnInspectorGUI();
};

