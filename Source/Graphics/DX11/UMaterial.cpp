#include "pch.h"
#include "UMaterial.h"
#include <filesystem>

UMaterial::UMaterial()
	: m_ResourcePath("")
	, Ambient(0.8f, 0.8f, 0.8f, 1.0f)
	, Diffuse(0.8f, 0.8f, 0.8f, 1.0f)
	, Specular(0.2f, 0.2f, 0.2f, 16.0f)
	, Reflect(0.0f, 0.0f, 0.0f, 1.0f)
{
	// Ï¥àÍ∏∞ÌôîÎêòÏßÄ ÏïäÏùÄ Í∞íÏúºÎ°ú Ïû¨Ïßà ÌååÏùºÏù¥ ÎßåÎì§Ïñ¥ÏßÄÎçò Î¨∏Ï†úÎ•º Î∞©ÏßÄ (ÏïåÌåå 0 Ïù¥Î©¥ Ïò§Î∏åÏ†ùÌä∏Í∞Ä Ìà¨Î™ÖÌïòÍ≤å Î≥¥ÏûÑ)
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
		s_Default = make_shared<UMaterial>();
		s_Default->m_ResourcePath = "builtin:Default-Material";
		s_Default->m_shaderSetting.UseShadowMap = 1;
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

void UMaterial::Create(string fullPath)
{
	UMaterial* material = new UMaterial;

	string filePath = PathManager::GetI()->GetCutSolutionPath(fullPath);
	
	material->m_ResourcePath = filePath + "\\New Material.mat";

	Save(material);
}

UMaterial* UMaterial::Load(string fullPath)
{
	std::ifstream is(PathManager::GetI()->GetMovePathS(fullPath));
	if (!is)
	{
		return nullptr;
	}

	json j;
	is >> j;

	UMaterial* material = new UMaterial;
	material->m_ResourcePath = fullPath;
	j.get_to(*material);

	if (!material->m_BaseMapPath.empty())
		material->BaseMapSRV = ResourceManager::GetI()->LoadTexture(material->m_BaseMapPath);
	if (!material->m_NormalMapPath.empty())
		material->NormalMapSRV = ResourceManager::GetI()->LoadTexture(material->m_NormalMapPath);

	material->Mat.Ambient = material->Ambient;
	material->Mat.Diffuse = material->Diffuse;
	material->Mat.Specular = material->Specular;
	material->Mat.Reflect = material->Reflect;

	return material;
}

void UMaterial::Save(UMaterial* material)
{
	if (material == nullptr)
		return;

	// ÎÇ¥Ïû• Ïû¨ÏßàÏùÄ ÌååÏùºÎ°ú Ï†ÄÏû•ÌïòÏßÄ ÏïäÎäîÎã§
	if (IsBuiltinPath(material->m_ResourcePath))
		return;

	// JSON ∞¥√º ª˝º∫
	json j = *material;

	// JSON ∆ƒ¿œ∑Œ ¿˙¿Â
	std::ofstream os(PathManager::GetI()->GetMovePathS(material->m_ResourcePath));
	if (!os)
	{
		std::cerr << "∆ƒ¿œ¿ª ø≠ ºˆ æ¯Ω¿¥œ¥Ÿ: " << PathManager::GetI()->GetMovePathS(material->m_ResourcePath) << std::endl;
		return;
	}

	os << j.dump(4); 
	os.close();
}

void UMaterial::ReloadTextures()
{
	BaseMapSRV = m_BaseMapPath.empty() ? nullptr : ResourceManager::GetI()->LoadTexture(m_BaseMapPath);
	NormalMapSRV = m_NormalMapPath.empty() ? nullptr : ResourceManager::GetI()->LoadTexture(m_NormalMapPath);
}

void UMaterial::SetBaseMap(TextureMgr texMgr, wstring fullPath)
{
	ComPtr<ID3D11ShaderResourceView> baseMapSRV = texMgr.CreateTexture(fullPath);
	BaseMapSRV = baseMapSRV;
}

void UMaterial::SetNormalMap(TextureMgr texMgr, wstring fullPath)
{
	ComPtr<ID3D11ShaderResourceView> normalMapSRV = texMgr.CreateTexture(fullPath);
	NormalMapSRV = normalMapSRV;
}

void UMaterial::OnInspectorGUI()
{
	bool changed = false;
	
	if (BaseMapSRV != nullptr) 
	{
		if (ImGui::ImageButton((void*)BaseMapSRV.Get(), ImVec2(64, 64)))
		{
			std::wstring filePath = EditorUtility::OpenTextureFileDialog(PathManager::GetI()->GetMovePathS(m_ResourcePath));
			filePath = PathManager::GetI()->GetCutSolutionPath(filePath);

			if (!filePath.empty())
			{
				BaseMapSRV = ResourceManager::GetI()->LoadTexture(filePath);
				m_BaseMapPath = filePath;
				changed = true;
			}
		}
	}
	else 
	{
		if (ImGui::Button("Load Base Map##image", ImVec2(64, 64)))
		{
			std::wstring filePath = EditorUtility::OpenTextureFileDialog(PathManager::GetI()->GetMovePathS(m_ResourcePath));
			filePath = PathManager::GetI()->GetCutSolutionPath(filePath);

			if (!filePath.empty()) 
			{
				BaseMapSRV = ResourceManager::GetI()->LoadTexture(filePath);
				m_BaseMapPath = filePath;
				changed = true;
			}
		}
	}

	if (NormalMapSRV != nullptr)
	{
		if (ImGui::ImageButton((void*)NormalMapSRV.Get(), ImVec2(64, 64)))
		{
			std::wstring filePath = EditorUtility::OpenTextureFileDialog(PathManager::GetI()->GetMovePathS(m_ResourcePath));
			filePath = PathManager::GetI()->GetCutSolutionPath(filePath);

			if (!filePath.empty())
			{
				NormalMapSRV = ResourceManager::GetI()->LoadTexture(filePath);
				m_NormalMapPath = filePath;
				changed = true;
			}
		}
	}
	else
	{
		if (ImGui::Button("Load Normal Map##image", ImVec2(64, 64)))
		{
			std::wstring filePath = EditorUtility::OpenTextureFileDialog(PathManager::GetI()->GetMovePathS(m_ResourcePath));
			filePath = PathManager::GetI()->GetCutSolutionPath(filePath);

			if (!filePath.empty())
			{
				NormalMapSRV = ResourceManager::GetI()->LoadTexture(filePath);
				m_NormalMapPath = filePath;
				changed = true;
			}
		}
	}

	changed |= ImGui::ColorEdit4("Ambient", reinterpret_cast<float*>(&Ambient));
	changed |= ImGui::ColorEdit4("Diffuse", reinterpret_cast<float*>(&Diffuse));
	changed |= ImGui::ColorEdit4("Specular", reinterpret_cast<float*>(&Specular));
	changed |= ImGui::ColorEdit4("Reflect", reinterpret_cast<float*>(&Reflect));

	//ShaderSetting
	{
		bool is = m_shaderSetting.UseTexture;
		if (ImGui::Checkbox("UseTexture", &is))
		{
			m_shaderSetting.UseTexture = is ? 1 : 0;
			changed = true;
		}

		is = m_shaderSetting.AlphaClip;
		if (ImGui::Checkbox("AlphaClip", &is))
		{
			m_shaderSetting.AlphaClip = is ? 1 : 0;
			changed = true;
		}

		is = m_shaderSetting.UseNormalMap;
		if (ImGui::Checkbox("UseNormalMap", &is))
		{
			m_shaderSetting.UseNormalMap = is ? 1 : 0;
			changed = true;
		}

		is = m_shaderSetting.UseShadowMap;
		if (ImGui::Checkbox("UseShadowMap", &is))
		{
			m_shaderSetting.UseShadowMap = is ? 1 : 0;
			changed = true;
		}

		is = m_shaderSetting.UseSsaoMap;
		if (ImGui::Checkbox("UseSsaoMap", &is))
		{
			m_shaderSetting.UseSsaoMap = is ? 1 : 0;
			changed = true;
		}

		is = m_shaderSetting.ReflectionEnabled;
		if (ImGui::Checkbox("ReflectionEnabled", &is))
		{
			m_shaderSetting.ReflectionEnabled = is ? 1 : 0;
			changed = true;
		}

		is = m_shaderSetting.FogEnabled;
		if (ImGui::Checkbox("FogEnabled", &is))
		{
			m_shaderSetting.FogEnabled = is ? 1 : 0;
			changed = true;
		}
	}

	if (changed)
	{
		UMaterial::Save(this);
		Mat.Ambient = Ambient;
		Mat.Diffuse = Diffuse;
		Mat.Reflect = Reflect;
		Mat.Specular = Specular;
	}
}

void from_json(const json& j, UMaterial& m)
{
	m.m_BaseMapPath = string_to_wstring(j.at("BaseMapPath").get<std::string>());
	m.m_NormalMapPath = string_to_wstring(j.at("NormalMapPath").get<std::string>());
	m.m_ResourcePath = j.at("ResourcePath").get<std::string>();
	
	DE_SERIALIZE_FLOAT4(j, m.Ambient, "Ambient");
	DE_SERIALIZE_FLOAT4(j, m.Diffuse, "Diffuse");
	DE_SERIALIZE_FLOAT4(j, m.Specular, "Specular");
	DE_SERIALIZE_FLOAT4(j, m.Reflect, "Reflect");

	// ShaderSetting
	DE_SERIALIZE_INT(j, m.m_shaderSetting.UseTexture, "UseTexture");
	DE_SERIALIZE_INT(j, m.m_shaderSetting.AlphaClip, "AlphaClip");
	DE_SERIALIZE_INT(j, m.m_shaderSetting.UseNormalMap, "UseNormalMap");
	DE_SERIALIZE_INT(j, m.m_shaderSetting.UseShadowMap, "UseShadowMap");
	DE_SERIALIZE_INT(j, m.m_shaderSetting.UseSsaoMap, "UseSsaoMap");
	DE_SERIALIZE_INT(j, m.m_shaderSetting.ReflectionEnabled, "ReflectionEnabled");
	DE_SERIALIZE_INT(j, m.m_shaderSetting.FogEnabled, "FogEnabled");
}

void to_json(json& j, const UMaterial& m)
{
	j = json {};

	SERIALIZE_WSTRING(j, m.m_BaseMapPath, "BaseMapPath");
	SERIALIZE_WSTRING(j, m.m_NormalMapPath, "NormalMapPath");
	SERIALIZE_STRING(j, m.m_ResourcePath, "ResourcePath"); 

	SERIALIZE_FLOAT4(j, m.Ambient, "Ambient");
	SERIALIZE_FLOAT4(j, m.Diffuse, "Diffuse");
	SERIALIZE_FLOAT4(j, m.Specular, "Specular");
	SERIALIZE_FLOAT4(j, m.Reflect, "Reflect");

	// ShaderSetting
	SERIALIZE_INT(j, m.m_shaderSetting.UseTexture, "UseTexture");
	SERIALIZE_INT(j, m.m_shaderSetting.AlphaClip, "AlphaClip");
	SERIALIZE_INT(j, m.m_shaderSetting.UseNormalMap, "UseNormalMap");
	SERIALIZE_INT(j, m.m_shaderSetting.UseShadowMap, "UseShadowMap");
	SERIALIZE_INT(j, m.m_shaderSetting.UseSsaoMap, "UseSsaoMap");
	SERIALIZE_INT(j, m.m_shaderSetting.ReflectionEnabled, "ReflectionEnabled");
	SERIALIZE_INT(j, m.m_shaderSetting.FogEnabled, "FogEnabled");
}
