#pragma once
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <string>

// .fx 이펙트를 fx_5_0으로 컴파일하되, 소스(및 include) 변경이 없으면 디스크 캐시(.fxo)를 재사용한다.
namespace ShaderCache
{
	HRESULT CompileEffect(const std::wstring& filename, UINT shaderFlags,
		Microsoft::WRL::ComPtr<ID3DBlob>& outBlob, Microsoft::WRL::ComPtr<ID3DBlob>& outMsgs);
}
