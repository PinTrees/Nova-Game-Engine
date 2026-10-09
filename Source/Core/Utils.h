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
	// 텍스처 파일 → CPU 이미지 (DDS · TGA · WIC 디코드, Import Settings, PNG 등은 DDS 캐시). 메인이 아닌 스레드에서도 된다
	static HRESULT DecodeTexture(const wstring& path, DirectX::ScratchImage& img, DirectX::TexMetadata& md, int& sourceW, int& sourceH);
	// 씬 스트리밍: 디코드를 백그라운드 잡에서 미리 (LoadTexture 는 GPU 로 올리기만). path = 디스크 전체 경로 (GetMovePathW)
	static void PrefetchTexture(const wstring& path);
	// 씬 스트리밍: 파일을 백그라운드에서 읽어 OS 캐시를 데운다 (모델 캐시 .mesh 등 — 메인이 읽을 때 디스크를 기다리지 않게)
	static void PrefetchFile(const wstring& path);
	struct PrefetchStats { uint64_t Requested = 0, Used = 0, Waited = 0, Claimed = 0, Files = 0; double DecodeMs = 0; };
	static PrefetchStats GetPrefetchStats();
	static void ClearPrefetched();   // 쓰지 않은 미리 디코드한 이미지를 버린다 (불러오기가 끝난 뒤)

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