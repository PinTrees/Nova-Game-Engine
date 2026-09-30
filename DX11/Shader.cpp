#include "pch.h"
#include "Shader.h"
#include <fstream>
#include "ShaderCache.h"
#include "Utils.h" 

Shader::Shader(ComPtr<ID3D11Device> device, const std::wstring& filename)
{
	WORD shaderFlags = 0;
#if defined( DEBUG ) || defined( _DEBUG )
	shaderFlags |= D3D10_SHADER_DEBUG;
	shaderFlags |= D3D10_SHADER_SKIP_OPTIMIZATION;
#endif

	ComPtr<ID3D10Blob> compiledShader = 0;
	ComPtr<ID3D10Blob> compilationMsgs = 0;

	m_FileName = filename;

	HRESULT hr = ShaderCache::CompileEffect(filename, shaderFlags, compiledShader, compilationMsgs);

	// compilationMsgs can store errors or warnings.
	if (FAILED(hr))
	{
		if (compilationMsgs != 0)
			::MessageBoxA(0, (char*)compilationMsgs->GetBufferPointer(), 0, 0);

		assert(false);
	}

	CHECK(::D3DX11CreateEffectFromMemory(compiledShader->GetBufferPointer(),
		compiledShader->GetBufferSize(), 0, device.Get(), m_pFx.GetAddressOf()));
}







vector<Shader*> Shaders::m_Shaders = {};

void Shaders::InitAll(ComPtr<ID3D11Device> device)
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
