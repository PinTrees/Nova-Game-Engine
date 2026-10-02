#pragma once
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <string>

// .fx 이펙트를 fx_5_0으로 컴파일하되, 소스(및 include) 변경이 없으면 디스크 캐시(.fxo)를 재사용한다.
namespace ShaderCache
{
	HRESULT CompileEffect(const std::wstring& filename, UINT shaderFlags,
		Microsoft::WRL::ComPtr<ID3DBlob>& outBlob, Microsoft::WRL::ComPtr<ID3DBlob>& outMsgs);

	// Effect/Shader 가 쓰는 컴파일 옵션 (Debug 빌드는 디버그 정보 + 최적화 생략)
	UINT DefaultFlags();

	// 캐시가 오래된 셰이더를 여러 스레드로 미리 컴파일해 캐시에 넣는다 (시작 시 한 번).
	// 이후 Effect 생성은 캐시에서 바로 읽는다. 목록에 없는 파일은 원래대로 그때 컴파일된다.
	void PrecompileParallel(const std::vector<std::wstring>& files, UINT shaderFlags);

	// 마지막으로 컴파일한 파일의 컴파일러 메시지 (오류 · 경고, 없으면 빈 문자열) — Shader Graph 가 오류를 보여 줄 때
	NOVA_API std::string LastMessages(const std::wstring& filename);
}
