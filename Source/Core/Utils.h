#pragma once

#if defined(DEBUG) || defined(_DEBUG)
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif
 
#include <d3d11.h>
#include <cassert>
#include <ctime>
#include <algorithm>
#include <string>
#include <sstream>
#include <fstream>
#include <vector>
#include <codecvt>

// 식 p 는 Release(NDEBUG, assert 가 사라짐)에서도 반드시 실행한다. 예전에는 assert(SUCCEEDED(p)) 라
// Release 에서 D3DX11CreateEffectFromMemory·Present 같은 호출 자체가 빠졌다
#define HR(p)	do { const HRESULT _novaHr = (p); assert(SUCCEEDED(_novaHr)); (void)_novaHr; } while (0)
#define CHECK(p)	HR(p)

#define DXGI_FORMAT_FROM_FILE ((DXGI_FORMAT)0xfffffffdu)

class Utils
{
public:
	static ComPtr<GfxShaderResourceView> LoadTexture(ComPtr<GfxDevice> device, const wstring& path);

	static ComPtr<GfxShaderResourceView> CreateTexture2DArraySRV(
		ComPtr<GfxDevice> device, ComPtr<GfxContext> context,
		std::vector<std::wstring>& filenames);

	static ComPtr<GfxShaderResourceView> CreateRandomTexture1DSRV(ComPtr<GfxDevice> device);
};

class TextHelper
{
public:

	template<typename T>
	static std::wstring ToString(const T& s)
	{
		std::wostringstream oss;
		oss << s;

		return oss.str();
	}

	template<typename T>
	static T FromString(const std::wstring& s)
	{
		T x;
		std::wistringstream iss(s);
		iss >> x;

		return x;
	}
};

namespace Colors
{
	XMGLOBALCONST XMVECTORF32 White     = {1.0f, 1.0f, 1.0f, 1.0f};
	XMGLOBALCONST XMVECTORF32 Black     = {0.0f, 0.0f, 0.0f, 1.0f};
	XMGLOBALCONST XMVECTORF32 Red       = {1.0f, 0.0f, 0.0f, 1.0f};
	XMGLOBALCONST XMVECTORF32 Green     = {0.0f, 1.0f, 0.0f, 1.0f};
	XMGLOBALCONST XMVECTORF32 Blue      = {0.0f, 0.0f, 1.0f, 1.0f};
	XMGLOBALCONST XMVECTORF32 Yellow    = {1.0f, 1.0f, 0.0f, 1.0f};
	XMGLOBALCONST XMVECTORF32 Cyan      = {0.0f, 1.0f, 1.0f, 1.0f};
	XMGLOBALCONST XMVECTORF32 Magenta   = {1.0f, 0.0f, 1.0f, 1.0f};

	XMGLOBALCONST XMVECTORF32 Silver    = {0.75f, 0.75f, 0.75f, 1.0f};
	XMGLOBALCONST XMVECTORF32 LightSteelBlue = {0.69f, 0.77f, 0.87f, 1.0f};
}


NOVA_API std::wstring string_to_wstring(const std::string& str);

NOVA_API std::string wstring_to_string(const std::wstring& wstr);