#pragma once
#include "LightHelper.h"

class Shader
{
protected:
	ComPtr<ID3DX11Effect> m_pFx;
	std::wstring m_FileName;

public:
	Shader(ComPtr<ID3D11Device> device, const std::wstring& filename);
	virtual ~Shader() = default;  // 가상 소멸자 추가

private:
	Shader(const Shader& rhs);
	Shader& operator=(const Shader& rhs);

public:
	const wstring& GetFileName() { return m_FileName; }
};

class Shaders
{
private:
	static vector<Shader*> m_Shaders;

public:
	static void InitAll(ComPtr<ID3D11Device> device);
	static void DestroyAll();

	static Shader* GetShaderByName(const std::wstring& shaderFileName);
	template <class T>
	static T* GetShader()
	{
		for (auto& shader : m_Shaders)
		{
			T* castedShader = dynamic_cast<T*>(shader);
			if (castedShader)
			{
				return castedShader;
			}
		}
		return nullptr;
	}
};
