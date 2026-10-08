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

	// 테셀레이션 변위 (HDRP Lit 의 Displacement Mode = Tessellation): 카메라 가까이에서 삼각형을 잘게 나눠 Height Map 만큼 민다 (60. Tessellation.fx)
	struct Tessellation
	{
		bool Enabled = false;
		float Amplitude = 0.05f;     // 높이 맵 0..1 → 이 높이 (m)
		float Base = 0.5f;           // 높이 맵에서 원래 면의 자리 (0..1) — 아래는 들어가고 위는 나온다
		float MaxFactor = 16.0f;     // 변 하나를 최대 몇 조각으로 (1..64)
		float TriangleSize = 12.0f;  // 원하는 삼각형 변 길이 (1080p 화면의 픽셀)
		float FadeDistance = 50.0f;  // 이 거리 (m) 너머는 나누지 않는다 (끝 1/4 에서 줄어든다)
	};

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
	int m_VirtualTexture = 0;   // VirtualTexturing 번호 (Base Map 이 가상 텍스처면)
	wstring m_HeightMapPath;
	ComPtr<GfxShaderResourceView> HeightMapSRV;
	bool m_EmissionEnabled = false;
	XMFLOAT3 m_EmissionColor = XMFLOAT3(0, 0, 0);   // 감마 공간 색
	float m_EmissionIntensity = 1.0f;
	int m_Priority = 0;
	Tessellation m_Tess;

	// 패키지 셰이더
	std::string m_CustomShader;
	json m_Properties = json::object();
	bool m_FallbackUnlit = false;
	uint64 m_PropertiesRevision = NextRevision();   // 전역에서 유일 (패키지가 재질 주소 + 이 값으로 캐시)
	bool m_Instance = false;   // 런타임 사본 (Renderer.material · new Material) — 이름에 " (Instance)", 파일에 저장하지 않는다

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
	GfxShaderResourceView* GetHeightMapSRV() const { return HeightMapSRV.Get(); }
	const Tessellation& GetTessellation() const { return m_Tess; }
	// 테셀레이션으로 그리는가: Lit · 켜짐 · Height Map 있음 · 알파 자르기 아님 (자르는 재질은 예전 길로)
	bool UsesTessellation() const;
	ShaderSetting GetShaderSetting() { return m_shaderSetting; }
	const PbrMaterial& GetPbr() const { return m_Pbr; }
	XMFLOAT2 Tiling() const { return m_Pbr.Tiling; }
	XMFLOAT2 Offset() const { return m_Pbr.Offset; }
	// Base Map 이 가상 텍스처 (가져오기 설정 Virtual Texture Only) 면 VirtualTexturing 번호, 아니면 0
	int VirtualTextureId() const { return m_VirtualTexture; }
	// 발광 (선형 × Intensity, 꺼져 있으면 0) — Adaptive Probe Volume 이 발광 렌더러를 찾는다
	XMFLOAT3 EmissionLinear() const;
	ShaderKind GetShader() const { return m_Shader; }

	// Inspector: 재질 에셋을 골랐을 때(전체) 와 Mesh Renderer 아래(embedded = 머리글을 접을 수 있게)
	void OnInspectorGUI(bool embedded = false);

	// ---- 스크립트 (C# Material · MaterialPropertyBlock): Unity URP 의 이름 — _BaseColor · _Color · _EmissionColor (HDR) ·
	//  _Metallic · _Smoothness · _Glossiness · _Cutoff · _BumpScale · _OcclusionStrength. 그 밖의 이름 = 패키지 · Shader Graph 속성 (Properties)
	//  색은 감마 (Unity 의 Color 와 같은 값). 없는 이름이면 false
	bool SetColorProperty(const std::string& name, const XMFLOAT4& c);
	bool GetColorProperty(const std::string& name, XMFLOAT4& c) const;
	bool SetFloatProperty(const std::string& name, float v);
	bool GetFloatProperty(const std::string& name, float& v) const;
	bool HasProperty(const std::string& name) const;
	static bool IsBaseColorProperty(const std::string& name);   // _BaseColor · _Color
	// 이름 → 엔진 값 (Unity URP 이름). 패키지 · Shader Graph 재질에 같은 이름의 속성이 있으면 None (그 속성을 쓴다 — ScriptProp)
	enum class Prop { None, BaseColor, Emission, Metallic, Smoothness, Cutoff, BumpScale, Occlusion };
	Prop ScriptProp(const std::string& name) const;
	// GPU 인스턴싱 속성 (MeshBatcher 의 인스턴스 값 — MaterialBlock::Instanced) 으로 넣을 수 있는 이름
	enum class InstanceProp { None, BaseColor, Emission, Metallic, Smoothness };
	static InstanceProp InstancePropOf(const std::string& name);
	// _EmissionColor (HDR 감마, 1 넘는 성분 = Intensity) → 셰이더가 쓰는 선형 값 (SetColorProperty + Apply 와 같은 계산)
	static XMFLOAT3 EmissionToLinear(const XMFLOAT4& hdr);
	// Emission 을 인스턴스 값으로 바꿔도 같은 그림인가 (꺼져 있는데 Emission 맵이 있으면 아니다 — 켜야 맵을 곱한다)
	bool CanInstanceEmission() const { return m_EmissionEnabled || !EmissionMapSRV; }
	// 런타임 사본 (Renderer.material · new Material(source)) — 같은 값 · 텍스처, 파일에는 저장하지 않는다
	std::shared_ptr<UMaterial> CloneInstance() const;
	bool IsInstance() const { return m_Instance; }
	std::string ScriptName() const;   // C# Material.name
	// 값이 바뀌었는지 (MaterialPropertyBlock 의 파생 재질을 다시 만들지) — 엔진 값 + 패키지 속성 수정 번호
	uint64 StateHash() const;

private:
	void SyncLegacy();   // m_Pbr → Mat (Diffuse = Base Color)
	friend class MaterialInspector;
};
