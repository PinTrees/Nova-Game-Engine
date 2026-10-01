#include "pch.h"
#include "UMaterial.h"
#include "Effects.h"
#include "MaterialInspector.h"
#include <filesystem>

namespace
{
	json F4(const XMFLOAT4& v) { return json::array({ v.x, v.y, v.z, v.w }); }
	json F3(const XMFLOAT3& v) { return json::array({ v.x, v.y, v.z }); }
	json F2(const XMFLOAT2& v) { return json::array({ v.x, v.y }); }
	XMFLOAT4 ReadF4(const json& j, const char* key, XMFLOAT4 def)
	{
		if (!j.contains(key) || !j[key].is_array() || j[key].size() < 4) return def;
		return XMFLOAT4(j[key][0].get<float>(), j[key][1].get<float>(), j[key][2].get<float>(), j[key][3].get<float>());
	}
	XMFLOAT3 ReadF3(const json& j, const char* key, XMFLOAT3 def)
	{
		if (!j.contains(key) || !j[key].is_array() || j[key].size() < 3) return def;
		return XMFLOAT3(j[key][0].get<float>(), j[key][1].get<float>(), j[key][2].get<float>());
	}
	XMFLOAT2 ReadF2(const json& j, const char* key, XMFLOAT2 def)
	{
		if (!j.contains(key) || !j[key].is_array() || j[key].size() < 2) return def;
		return XMFLOAT2(j[key][0].get<float>(), j[key][1].get<float>());
	}
	std::wstring ReadPath(const json& j, const char* key)
	{
		return j.contains(key) && j[key].is_string() ? string_to_wstring(j[key].get<std::string>()) : std::wstring();
	}
	ComPtr<GfxShaderResourceView> LoadTex(const std::wstring& path)
	{
		return path.empty() ? nullptr : ResourceManager::GetI()->LoadTexture(path);
	}
	float ToLinear(float c) { return powf((std::max)(c, 0.0f), 2.2f); }
}

UMaterial::UMaterial()
	: m_ResourcePath("")
	, Ambient(0.8f, 0.8f, 0.8f, 1.0f)
	, Diffuse(0.8f, 0.8f, 0.8f, 1.0f)
	, Specular(0.2f, 0.2f, 0.2f, 16.0f)
	, Reflect(0.0f, 0.0f, 0.0f, 1.0f)
{
	// 초기화되지 않은 값으로 재질 파일이 만들어지던 문제를 방지 (알파 0 이면 오브젝트가 투명하게 보임)
	Mat.Ambient = Ambient;
	Mat.Diffuse = Diffuse;
	Mat.Specular = Specular;
	Mat.Reflect = Reflect;
}

shared_ptr<UMaterial> UMaterial::GetDefault()
{
	static shared_ptr<UMaterial> s_Default;
	if (s_Default == nullptr)
	{
		// Unity 의 Default-Material: 밝은 회색, Smoothness 0.5
		s_Default = make_shared<UMaterial>();
		s_Default->m_ResourcePath = "builtin:Default-Material";
		s_Default->m_shaderSetting.UseShadowMap = 1;
		s_Default->m_Pbr.BaseColor = XMFLOAT4(0.8f, 0.8f, 0.8f, 1.0f);
		s_Default->SyncLegacy();
	}
	return s_Default;
}

UMaterial::~UMaterial()
{
}

string UMaterial::GetName()
{
	return filesystem::path(m_ResourcePath).filename().string();
}

string UMaterial::Create(string fullPath)
{
	// Unity 처럼 같은 이름이 있으면 "New Material 1", "New Material 2" ...
	const std::filesystem::path dir = std::filesystem::path(string_to_wstring(fullPath));
	std::filesystem::path file = dir / L"New Material.mat";
	std::error_code ec;
	for (int i = 1; std::filesystem::exists(file, ec); ++i)
		file = dir / (L"New Material " + std::to_wstring(i) + L".mat");

	UMaterial material;
	material.m_ResourcePath = wstring_to_string(PathManager::GetI()->GetCutSolutionPath(file.wstring()));
	material.SyncLegacy();
	Save(&material);
	return material.m_ResourcePath;
}

UMaterial* UMaterial::Load(string fullPath)
{
	std::ifstream is(PathManager::GetI()->GetMovePathS(fullPath));
	if (!is)
		return nullptr;
	json j = json::parse(is, nullptr, false);
	if (j.is_discarded() || !j.is_object())
		return nullptr;

	UMaterial* material = new UMaterial;
	material->m_ResourcePath = fullPath;
	j.get_to(*material);
	material->m_ResourcePath = fullPath;   // 파일을 옮겨도 지금 경로를 쓴다
	material->ReloadTextures();
	return material;
}

void UMaterial::Save(UMaterial* material)
{
	if (material == nullptr || IsBuiltinPath(material->m_ResourcePath))
		return;   // 내장 재질은 파일로 저장하지 않는다
	json j = *material;
	std::ofstream os(PathManager::GetI()->GetMovePathS(material->m_ResourcePath));
	if (!os)
	{
		EditorLog::Write("Material", "cannot save %s", material->m_ResourcePath.c_str());
		return;
	}
	os << j.dump(4);
}

void UMaterial::ReloadTextures()
{
	BaseMapSRV = LoadTex(m_BaseMapPath);
	NormalMapSRV = LoadTex(m_NormalMapPath);
	MetallicMapSRV = LoadTex(m_MetallicMapPath);
	OcclusionMapSRV = LoadTex(m_OcclusionMapPath);
	EmissionMapSRV = LoadTex(m_EmissionMapPath);
	SyncLegacy();
}

void UMaterial::SetBaseMap(TextureMgr texMgr, wstring fullPath)
{
	BaseMapSRV = texMgr.CreateTexture(fullPath);
}

void UMaterial::SetNormalMap(TextureMgr texMgr, wstring fullPath)
{
	NormalMapSRV = texMgr.CreateTexture(fullPath);
}

void UMaterial::SyncLegacy()
{
	// 예전 경로(지형 등)와 파일 호환: Diffuse = Base Color
	Diffuse = m_Pbr.BaseColor;
	Ambient = XMFLOAT4(m_Pbr.BaseColor.x, m_Pbr.BaseColor.y, m_Pbr.BaseColor.z, 1.0f);
	m_shaderSetting.UseTexture = BaseMapSRV ? 1 : 0;
	m_shaderSetting.UseNormalMap = NormalMapSRV ? 1 : 0;
	m_shaderSetting.AlphaClip = m_Pbr.AlphaClip;
	m_shaderSetting.UseShadowMap = m_Pbr.ReceiveShadows;
	Mat.Ambient = Ambient;
	Mat.Diffuse = Diffuse;
	Mat.Specular = Specular;
	Mat.Reflect = Reflect;
}

void UMaterial::Apply(InstancedBasicEffect* fx, bool forPreview)
{
	if (fx == nullptr)
		return;
	PbrMaterial p = m_Pbr;
	p.UseBaseMap = BaseMapSRV ? 1 : 0;
	p.UseNormalMap = NormalMapSRV ? 1 : 0;
	p.UseMetallicMap = MetallicMapSRV ? 1 : 0;
	p.UseOcclusionMap = OcclusionMapSRV ? 1 : 0;
	p.UseEmissionMap = m_EmissionEnabled && EmissionMapSRV ? 1 : 0;
	p.Unlit = m_Shader == ShaderKind::Unlit ? 1 : 0;
	// Emission: 감마 색 → 선형 × Intensity (HDR 이라 Bloom 이 번진다)
	if (m_EmissionEnabled)
		p.EmissionColor = XMFLOAT4(ToLinear(m_EmissionColor.x) * m_EmissionIntensity, ToLinear(m_EmissionColor.y) * m_EmissionIntensity,
			ToLinear(m_EmissionColor.z) * m_EmissionIntensity, 1.0f);
	else
		p.EmissionColor = XMFLOAT4(0, 0, 0, 1);
	ShaderSetting setting = m_shaderSetting;
	if (forPreview)
	{
		setting.UseShadowMap = 0;
		setting.UseSsaoMap = 0;
		setting.FogEnabled = 0;
		p.ReceiveShadows = 0;
	}
	fx->SetMaterial(Mat);
	fx->SetShaderSetting(setting);
	fx->SetPbr(p);
	fx->SetDiffuseMap(BaseMapSRV.Get());
	fx->SetNormalMap(NormalMapSRV.Get());
	fx->SetMetallicMap(MetallicMapSRV.Get());
	fx->SetOcclusionMap(OcclusionMapSRV.Get());
	fx->SetEmissionMap(EmissionMapSRV.Get());
}

void UMaterial::ApplyOrDefault(const shared_ptr<UMaterial>& material, InstancedBasicEffect* fx)
{
	(material ? material : GetDefault())->Apply(fx);
}

void UMaterial::OnInspectorGUI(bool embedded)
{
	MaterialInspector::Draw(*this, embedded);
}

// ------------------------------------------------------------------ 저장
void from_json(const json& j, UMaterial& m)
{
	m.m_ResourcePath = j.value("ResourcePath", m.m_ResourcePath);
	m.m_BaseMapPath = ReadPath(j, "BaseMapPath");
	m.m_NormalMapPath = ReadPath(j, "NormalMapPath");
	m.m_MetallicMapPath = ReadPath(j, "MetallicMapPath");
	m.m_OcclusionMapPath = ReadPath(j, "OcclusionMapPath");
	m.m_EmissionMapPath = ReadPath(j, "EmissionMapPath");

	m.Ambient = ReadF4(j, "Ambient", m.Ambient);
	m.Diffuse = ReadF4(j, "Diffuse", m.Diffuse);
	m.Specular = ReadF4(j, "Specular", m.Specular);
	m.Reflect = ReadF4(j, "Reflect", m.Reflect);
	m.m_shaderSetting.UseTexture = j.value("UseTexture", m.m_shaderSetting.UseTexture);
	m.m_shaderSetting.AlphaClip = j.value("AlphaClip", m.m_shaderSetting.AlphaClip);
	m.m_shaderSetting.UseNormalMap = j.value("UseNormalMap", m.m_shaderSetting.UseNormalMap);
	m.m_shaderSetting.UseShadowMap = j.value("UseShadowMap", m.m_shaderSetting.UseShadowMap);
	m.m_shaderSetting.UseSsaoMap = j.value("UseSsaoMap", m.m_shaderSetting.UseSsaoMap);
	m.m_shaderSetting.ReflectionEnabled = j.value("ReflectionEnabled", m.m_shaderSetting.ReflectionEnabled);
	m.m_shaderSetting.FogEnabled = j.value("FogEnabled", m.m_shaderSetting.FogEnabled);

	PbrMaterial& p = m.m_Pbr;
	if (j.contains("BaseColor"))
	{
		// URP Lit 형식
		m.m_Shader = j.value("Shader", std::string()) == "Universal Render Pipeline/Unlit" ? UMaterial::ShaderKind::Unlit : UMaterial::ShaderKind::Lit;
		p.BaseColor = ReadF4(j, "BaseColor", p.BaseColor);
		p.Metallic = j.value("Metallic", p.Metallic);
		p.Smoothness = j.value("Smoothness", p.Smoothness);
		p.SmoothnessFromAlbedo = j.value("SmoothnessSource", 0);
		p.NormalScale = j.value("NormalScale", p.NormalScale);
		p.OcclusionStrength = j.value("OcclusionStrength", p.OcclusionStrength);
		p.Tiling = ReadF2(j, "Tiling", p.Tiling);
		p.Offset = ReadF2(j, "Offset", p.Offset);
		p.AlphaClip = j.value("AlphaClipping", 0);
		p.Cutoff = j.value("Cutoff", p.Cutoff);
		p.ReceiveShadows = j.value("ReceiveShadows", 1);
		p.SpecularHighlights = j.value("SpecularHighlights", 1);
		p.EnvironmentReflections = j.value("EnvironmentReflections", 1);
		m.m_EmissionEnabled = j.value("Emission", false);
		m.m_EmissionColor = ReadF3(j, "EmissionColor", m.m_EmissionColor);
		m.m_EmissionIntensity = j.value("EmissionIntensity", 1.0f);
		m.m_Priority = j.value("Priority", 0);
	}
	else
	{
		// 예전 형식: Diffuse → Base Color, 반사 지수 → Smoothness, 쓰지 않던 텍스처는 버린다
		p.BaseColor = m.Diffuse;
		const float power = (std::max)(1.0f, m.Specular.w);
		p.Smoothness = std::clamp((log2f(power) - 1.0f) / 10.0f, 0.0f, 1.0f);
		if (!m.m_shaderSetting.UseTexture) m.m_BaseMapPath.clear();
		if (!m.m_shaderSetting.UseNormalMap) m.m_NormalMapPath.clear();
		p.AlphaClip = m.m_shaderSetting.AlphaClip;
		p.Cutoff = 0.1f;
		p.ReceiveShadows = m.m_shaderSetting.UseShadowMap;
	}
}

void to_json(json& j, const UMaterial& m)
{
	const PbrMaterial& p = m.m_Pbr;
	j = json{
		{ "Shader", m.m_Shader == UMaterial::ShaderKind::Unlit ? "Universal Render Pipeline/Unlit" : "Universal Render Pipeline/Lit" },
		{ "ResourcePath", m.m_ResourcePath },
		{ "BaseMapPath", wstring_to_string(m.m_BaseMapPath) },
		{ "NormalMapPath", wstring_to_string(m.m_NormalMapPath) },
		{ "MetallicMapPath", wstring_to_string(m.m_MetallicMapPath) },
		{ "OcclusionMapPath", wstring_to_string(m.m_OcclusionMapPath) },
		{ "EmissionMapPath", wstring_to_string(m.m_EmissionMapPath) },
		{ "BaseColor", F4(p.BaseColor) },
		{ "Metallic", p.Metallic },
		{ "Smoothness", p.Smoothness },
		{ "SmoothnessSource", p.SmoothnessFromAlbedo },
		{ "NormalScale", p.NormalScale },
		{ "OcclusionStrength", p.OcclusionStrength },
		{ "Tiling", F2(p.Tiling) },
		{ "Offset", F2(p.Offset) },
		{ "AlphaClipping", p.AlphaClip },
		{ "Cutoff", p.Cutoff },
		{ "ReceiveShadows", p.ReceiveShadows },
		{ "SpecularHighlights", p.SpecularHighlights },
		{ "EnvironmentReflections", p.EnvironmentReflections },
		{ "Emission", m.m_EmissionEnabled },
		{ "EmissionColor", F3(m.m_EmissionColor) },
		{ "EmissionIntensity", m.m_EmissionIntensity },
		{ "Priority", m.m_Priority },
		// 예전 형식 값 (지형 등 예전 경로, 이전 버전 호환)
		{ "Ambient", F4(m.Ambient) },
		{ "Diffuse", F4(m.Diffuse) },
		{ "Specular", F4(m.Specular) },
		{ "Reflect", F4(m.Reflect) },
		{ "UseTexture", m.m_shaderSetting.UseTexture },
		{ "AlphaClip", m.m_shaderSetting.AlphaClip },
		{ "UseNormalMap", m.m_shaderSetting.UseNormalMap },
		{ "UseShadowMap", m.m_shaderSetting.UseShadowMap },
		{ "UseSsaoMap", m.m_shaderSetting.UseSsaoMap },
		{ "ReflectionEnabled", m.m_shaderSetting.ReflectionEnabled },
		{ "FogEnabled", m.m_shaderSetting.FogEnabled },
	};
}
