#include "pch.h"
#include "Shader.h"
#include <fstream>
#include "ShaderCache.h"
#include "Utils.h" 

Shader::Shader(ComPtr<GfxDevice> device, const std::wstring& filename)
{
	(void)device;
	m_FileName = filename;
	std::string error;
	m_pFx = FxEffect::Load(filename, error);   // RHI 장치로 (Effects.cpp 의 Effect 와 같음)
	if (!m_pFx)
	{
		::MessageBoxA(0, error.c_str(), wstring_to_string(filename).c_str(), 0);
		assert(false);
	}
}







vector<Shader*> Shaders::m_Shaders = {};

void Shaders::InitAll(ComPtr<GfxDevice> device)
{
	m_Shaders.push_back(new Shader(device, L"../Shaders/28. Basic.fx"));
	m_Shaders.push_back(new NormalMapSkinnedShader(device, L"../Shaders/31. NormalMapSkinned.fx"));
}

void Shaders::DestroyAll()
{
}

Shader* Shaders::GetShaderByName(const std::wstring& shaderFileName)
{
	for (auto shader : m_Shaders)
	{
		if (shader->GetFileName() == shaderFileName)
		{
			return shader;
		}
	}
	return nullptr;
}
