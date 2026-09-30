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

#define HR(p)	assert(SUCCEEDED(p))
#define CHECK(p)	assert(SUCCEEDED(p))

#define DXGI_FORMAT_FROM_FILE ((DXGI_FORMAT)0xfffffffdu)

class Utils
{
public:
	static ComPtr<ID3D11ShaderResourceView> LoadTexture(ComPtr<ID3D11Device> device, const wstring& path);

	static ComPtr<ID3D11ShaderResourceView> CreateTexture2DArraySRV(
		ComPtr<ID3D11Device> device, ComPtr<ID3D11DeviceContext> context,
		std::vector<std::wstring>& filenames);

	static ComPtr<ID3D11ShaderResourceView> CreateRandomTexture1DSRV(ComPtr<ID3D11Device> device);
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


std::wstring string_to_wstring(const std::string& str);

std::string wstring_to_string(const std::wstring& wstr);