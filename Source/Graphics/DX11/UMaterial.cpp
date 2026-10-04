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

XMFLOAT3 UMaterial::EmissionLinear() const
{
	if (!m_EmissionEnabled)
		return XMFLOAT3(0, 0, 0);
	return XMFLOAT3(ToLinear(m_EmissionColor.x) * m_EmissionIntensity, ToLinear(m_EmissionColor.y) * m_EmissionIntensity, ToLinear(m_EmissionColor.z) * m_EmissionIntensity);
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

std::string UMaterial::ScriptName() const
{
	// Unity 의 Material.name: 확장자 없는 이름, 런타임 사본은 " (Instance)"
	std::string name = IsBuiltinPath(m_ResourcePath) ? std::string("Default-Material") : filesystem::path(m_ResourcePath).stem().string();
	return m_Instance ? name + " (Instance)" : name;
}

using Prop = UMaterial::Prop;

namespace
{
	// Unity URP 속성 이름 → 엔진 값 (색 · 수)
	Prop PropOf(const std::string& n)
	{
		if (n == "_BaseColor" || n == "_Color") return Prop::BaseColor;
		if (n == "_EmissionColor") return Prop::Emission;
		if (n == "_Metallic") return Prop::Metallic;
		if (n == "_Smoothness" || n == "_Glossiness") return Prop::Smoothness;
		if (n == "_Cutoff") return Prop::Cutoff;
		if (n == "_BumpScale") return Prop::BumpScale;
		if (n == "_OcclusionStrength") return Prop::Occlusion;
		return Prop::None;
	}
	// 패키지 · Shader Graph 속성 이름: 그대로, 없으면 앞의 "_" 를 뗀 이름 (그래프 속성 참조 이름이 "_" 없이 저장된 재질)
	const char* CustomKey(const json& props, const std::string& n)
	{
		if (props.contains(n)) return n.c_str();
		if (n.size() > 1 && n[0] == '_' && props.contains(n.substr(1))) return n.c_str() + 1;
		return nullptr;
	}
}

bool UMaterial::IsBaseColorProperty(const std::string& name)
{
	return PropOf(name) == Prop::BaseColor;
}

UMaterial::InstanceProp UMaterial::InstancePropOf(const std::string& name)
{
	switch (PropOf(name))
	{
	case Prop::BaseColor: return InstanceProp::BaseColor;
	case Prop::Emission: return InstanceProp::Emission;
	case Prop::Metallic: return InstanceProp::Metallic;
	case Prop::Smoothness: return InstanceProp::Smoothness;
	default: return InstanceProp::None;
	}
}

XMFLOAT3 UMaterial::EmissionToLinear(const XMFLOAT4& c)
{
	const float m = (std::max)((std::max)(c.x, c.y), c.z);
	if (m <= 0.0f)
		return XMFLOAT3(0, 0, 0);   // 꺼짐
	const float k = (std::max)(1.0f, m);
	return XMFLOAT3(ToLinear(c.x / k) * k, ToLinear(c.y / k) * k, ToLinear(c.z / k) * k);
}

// 패키지 · Shader Graph 셰이더에 같은 이름의 속성이 있으면 엔진 값 대신 그 속성 (Unity 와 같이 셰이더가 가진 속성 —
//  예전엔 Shader Graph 의 _BaseColor 속성을 SetColor 해도 엔진 BaseColor 가 바뀌어 그래프에 보이지 않았다)
UMaterial::Prop UMaterial::ScriptProp(const std::string& name) const
{
	if (IsCustom() && CustomKey(m_Properties, name))
		return Prop::None;
	return PropOf(name);
}

bool UMaterial::SetColorProperty(const std::string& name, const XMFLOAT4& c)
{
	switch (ScriptProp(name))
	{
	case Prop::BaseColor:
		m_Pbr.BaseColor = c;
		SyncLegacy();
		m_PropertiesRevision = NextRevision();   // 패키지 셰이더도 엔진 값을 다시 읽게
		return true;
	case Prop::Emission:
	{
		// HDR 색 = 색 × Intensity (1 넘는 성분은 Intensity 로)
		const float m = (std::max)((std::max)(c.x, c.y), c.z);
		m_EmissionIntensity = (std::max)(1.0f, m);
		m_EmissionColor = XMFLOAT3(c.x / m_EmissionIntensity, c.y / m_EmissionIntensity, c.z / m_EmissionIntensity);
		m_EmissionEnabled = m > 0.0f;
		m_PropertiesRevision = NextRevision();
		return true;
	}
	case Prop::None:
		break;
	default:
		return false;   // 수 속성
	}
	if (const char* key = CustomKey(m_Properties, name))
	{
		m_Properties[key] = { c.x, c.y, c.z, c.w };
		TouchProperties();
		return true;
	}
	return false;
}

bool UMaterial::GetColorProperty(const std::string& name, XMFLOAT4& c) const
{
	switch (ScriptProp(name))
	{
	case Prop::BaseColor: c = m_Pbr.BaseColor; return true;
	case Prop::Emission:
	{
		const float k = m_EmissionEnabled ? m_EmissionIntensity : 0.0f;
		c = XMFLOAT4(m_EmissionColor.x * k, m_EmissionColor.y * k, m_EmissionColor.z * k, 1.0f);
		return true;
	}
	case Prop::None: break;
	default: return false;
	}
	if (const char* key = CustomKey(m_Properties, name))
	{
		const json& v = m_Properties[key];
		if (!v.is_array() || v.empty()) return false;
		float f[4] = { 0, 0, 0, 1 };
		for (size_t i = 0; i < v.size() && i < 4; ++i)
			if (v[i].is_number()) f[i] = v[i].get<float>();
		c = XMFLOAT4(f[0], f[1], f[2], f[3]);
		return true;
	}
	return false;
}

bool UMaterial::SetFloatProperty(const std::string& name, float v)
{
	switch (ScriptProp(name))
	{
	case Prop::Metallic: m_Pbr.Metallic = v; break;
	case Prop::Smoothness: m_Pbr.Smoothness = v; break;
	case Prop::Cutoff: m_Pbr.Cutoff = v; break;
	case Prop::BumpScale: m_Pbr.NormalScale = v; break;
	case Prop::Occlusion: m_Pbr.OcclusionStrength = v; break;
	case Prop::None:
		if (const char* key = CustomKey(m_Properties, name))
		{
			m_Properties[key] = v;
			TouchProperties();
			return true;
		}
		return false;
	default: return false;   // 색 속성
	}
	SyncLegacy();
	m_PropertiesRevision = NextRevision();
	return true;
}

bool UMaterial::GetFloatProperty(const std::string& name, float& v) const
{
	switch (ScriptProp(name))
	{
	case Prop::Metallic: v = m_Pbr.Metallic; return true;
	case Prop::Smoothness: v = m_Pbr.Smoothness; return true;
	case Prop::Cutoff: v = m_Pbr.Cutoff; return true;
	case Prop::BumpScale: v = m_Pbr.NormalScale; return true;
	case Prop::Occlusion: v = m_Pbr.OcclusionStrength; return true;
	case Prop::None: break;
	default: return false;
	}
	if (const char* key = CustomKey(m_Properties, name))
	{
		const json& j = m_Properties[key];
		if (j.is_number()) { v = j.get<float>(); return true; }
		if (j.is_boolean()) { v = j.get<bool>() ? 1.0f : 0.0f; return true; }
		if (j.is_array() && !j.empty() && j[0].is_number()) { v = j[0].get<float>(); return true; }   // Shader Graph Float = [x, 0, 0, 0]
	}
	return false;
}

bool UMaterial::HasProperty(const std::string& name) const
{
	return PropOf(name) != Prop::None || CustomKey(m_Properties, name) != nullptr;
}

std::shared_ptr<UMaterial> UMaterial::CloneInstance() const
{
	auto c = std::make_shared<UMaterial>(*this);
	c->m_Instance = true;
	c->m_PropertiesRevision = NextRevision();   // 패키지 캐시 (주소 + 수정 번호) 가 섞이지 않게
	return c;
}

uint64 UMaterial::StateHash() const
{
	// 엔진 값 (PBR · 발광) + 패키지 속성 수정 번호 (스크립트 · Inspector 가 바꾸면 달라진다)
	uint64 h = 1469598103934665603ull;
	auto mix = [&](const void* p, size_t n) { for (size_t i = 0; i < n; ++i) { h ^= static_cast<const uint8_t*>(p)[i]; h *= 1099511628211ull; } };
	mix(&m_Pbr, sizeof(m_Pbr));
	mix(&m_EmissionEnabled, sizeof(m_EmissionEnabled));
	mix(&m_EmissionColor, sizeof(m_EmissionColor));
	mix(&m_EmissionIntensity, sizeof(m_EmissionIntensity));
	mix(&m_PropertiesRevision, sizeof(m_PropertiesRevision));
	const void* srv = BaseMapSRV.Get();
	mix(&srv, sizeof(srv));
	return h;
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
	p.Unlit = m_Shader == ShaderKind::Unlit || (m_Shader == ShaderKind::Custom && m_FallbackUnlit) ? 1 : 0;   // 패키지 셰이더가 없을 때
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

std::string UMaterial::ShaderName() const
{
	if (m_Shader == ShaderKind::Custom) return m_CustomShader;
	return m_Shader == ShaderKind::Unlit ? "Universal Render Pipeline/Unlit" : "Universal Render Pipeline/Lit";
}

void UMaterial::SetShaderName(const std::string& name, const json& defaultProperties)
{
	if (name == "Universal Render Pipeline/Unlit") m_Shader = ShaderKind::Unlit;
	else if (name.empty() || name == "Universal Render Pipeline/Lit") m_Shader = ShaderKind::Lit;
	else
	{
		m_Shader = ShaderKind::Custom;
		if (m_CustomShader != name && defaultProperties.is_object())
		{
			// 새 셰이더의 기본값 위에 지금 값 (같은 이름) 을 남긴다
			json merged = defaultProperties;
			for (auto it = m_Properties.begin(); it != m_Properties.end(); ++it) merged[it.key()] = it.value();
			m_Properties = merged;
		}
		m_CustomShader = name;
	}
	m_PropertiesRevision = NextRevision();
}

uint64 UMaterial::NextRevision()
{
	static std::atomic<uint64> s_Next{ 1 };
	return s_Next++;
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
		const std::string shaderName = j.value("Shader", std::string());
		m.m_Shader = shaderName == "Universal Render Pipeline/Unlit" ? UMaterial::ShaderKind::Unlit :
			(shaderName.empty() || shaderName == "Universal Render Pipeline/Lit" ? UMaterial::ShaderKind::Lit : UMaterial::ShaderKind::Custom);
		m.m_CustomShader = m.m_Shader == UMaterial::ShaderKind::Custom ? shaderName : std::string();
		m.m_Properties = j.contains("Properties") && j["Properties"].is_object() ? j["Properties"] : json::object();
		m.m_FallbackUnlit = j.value("Fallback", std::string("Lit")) == "Unlit";
		m.m_PropertiesRevision = UMaterial::NextRevision();
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
		{ "Shader", m.ShaderName() },
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
	if (m.m_Shader == UMaterial::ShaderKind::Custom)
	{
		j["Properties"] = m.m_Properties;
		j["Fallback"] = m.m_FallbackUnlit ? "Unlit" : "Lit";
	}
}
