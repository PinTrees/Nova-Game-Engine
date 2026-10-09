#include "pch.h"
#include "Utils.h"
#include "MathHelper.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include "Debug.h"
#include "AssetImportSettings.h"
#include "JobSystem.h"
#include <condition_variable>
#include <mutex>
#include <unordered_map>
namespace fs = std::filesystem;

namespace
{
	// sRGB 형식 → 같은 배치의 선형 형식 (이 DirectXTex 에는 MakeLinear 가 없다)
	DXGI_FORMAT ToLinear(DXGI_FORMAT f)
	{
		switch (f)
		{
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM;
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM;
		case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8X8_UNORM;
		case DXGI_FORMAT_BC1_UNORM_SRGB: return DXGI_FORMAT_BC1_UNORM;
		case DXGI_FORMAT_BC2_UNORM_SRGB: return DXGI_FORMAT_BC2_UNORM;
		case DXGI_FORMAT_BC3_UNORM_SRGB: return DXGI_FORMAT_BC3_UNORM;
		case DXGI_FORMAT_BC7_UNORM_SRGB: return DXGI_FORMAT_BC7_UNORM;
		default: return f;
		}
	}

	// Import Settings 적용: Max Size (큰 쪽 기준, 비율 유지) → 밉맵 → 압축 (BC1/BC3 · BC7)
	void ApplyTextureImport(ScratchImage& img, TexMetadata& md, const AssetImport::TextureSettings& ts, const wstring& path)
	{
		if (md.dimension != TEX_DIMENSION_TEXTURE2D || md.arraySize != 1 || md.depth != 1)
			return;   // 큐브맵·배열·3D 는 그대로
		// 색 공간: Normal map · sRGB 끔 = 선형 (같은 바이트를 선형으로 읽는다 — 하드웨어 감마 풀기 없음)
		const bool linear = ts.TextureType == AssetImport::TextureSettings::NormalMap || !ts.SRGB;
		if (linear && IsSRGB(md.format))
		{
			img.OverrideFormat(ToLinear(md.format));
			md = img.GetMetadata();
		}
		const bool tooBig = (int)(std::max)(md.width, md.height) > ts.MaxSize;
		const bool wantCompressed = ts.Compression != AssetImport::TextureSettings::None;
		const bool wantMips = ts.MipMaps && md.width > 1 && md.height > 1;
		const bool mipsOk = wantMips ? md.mipLevels > 1 : md.mipLevels == 1;
		if (!tooBig && mipsOk && (IsCompressed(md.format) || !wantCompressed))
			return;   // 바꿀 것 없음 (이미 압축된 DDS 는 그대로 쓴다)

		HRESULT hr = S_OK;
		// 압축된 원본이면 풀어서 다시
		if (IsCompressed(md.format))
		{
			ScratchImage raw;
			hr = ::Decompress(img.GetImages(), img.GetImageCount(), md, IsSRGB(md.format) ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM, raw);
			if (FAILED(hr))
				return;
			img = std::move(raw);
			md = img.GetMetadata();
		}
		// 맨 위 밉만 남기고 (크기를 바꾸거나 밉을 다시 만든다)
		if (md.mipLevels > 1 && (tooBig || !wantMips))
		{
			ScratchImage top;
			if (SUCCEEDED(top.InitializeFromImage(*img.GetImage(0, 0, 0))))
			{
				img = std::move(top);
				md = img.GetMetadata();
			}
		}
		if (tooBig)
		{
			const double k = (double)ts.MaxSize / (double)(std::max)(md.width, md.height);
			const size_t w = (std::max)((size_t)1, (size_t)std::lround(md.width * k));
			const size_t h = (std::max)((size_t)1, (size_t)std::lround(md.height * k));
			ScratchImage resized;
			if (SUCCEEDED(::Resize(*img.GetImage(0, 0, 0), w, h, TEX_FILTER_DEFAULT, resized)))
			{
				img = std::move(resized);
				md = img.GetMetadata();
			}
		}
		if (wantMips && md.mipLevels == 1)
		{
			ScratchImage mipped;
			if (SUCCEEDED(::GenerateMipMaps(img.GetImages(), img.GetImageCount(), md, TEX_FILTER_DEFAULT, 0, mipped)))
			{
				img = std::move(mipped);
				md = img.GetMetadata();
			}
		}
		if (wantCompressed)
		{
			// BC 는 맨 위 크기가 4 의 배수여야 한다 (D3D11)
			if (md.width % 4 != 0 || md.height % 4 != 0)
			{
				EditorLog::Write("Texture", "%s: %zux%zu is not a multiple of 4 - left uncompressed", wstring_to_string(fs::path(path).filename().wstring()).c_str(), md.width, md.height);
				return;
			}
			const bool srgb = IsSRGB(md.format);
			DXGI_FORMAT bc;
			if (ts.Compression == AssetImport::TextureSettings::HighQuality)
				bc = srgb ? DXGI_FORMAT_BC7_UNORM_SRGB : DXGI_FORMAT_BC7_UNORM;
			else if (img.IsAlphaAllOpaque())
				bc = srgb ? DXGI_FORMAT_BC1_UNORM_SRGB : DXGI_FORMAT_BC1_UNORM;
			else
				bc = srgb ? DXGI_FORMAT_BC3_UNORM_SRGB : DXGI_FORMAT_BC3_UNORM;
			ScratchImage packed;
			hr = ::Compress(img.GetImages(), img.GetImageCount(), md, bc, TEX_COMPRESS_PARALLEL, TEX_THRESHOLD_DEFAULT, packed);
			if (SUCCEEDED(hr))
			{
				img = std::move(packed);
				md = img.GetMetadata();
			}
		}
	}
}

// ---- 씬 스트리밍: 텍스처 디코드를 백그라운드 잡에서 미리 (Utils.h)
namespace
{
	struct Prefetched
	{
		bool Started = false, Done = false;
		HRESULT Hr = E_FAIL;
		DirectX::ScratchImage Img;
		DirectX::TexMetadata Md = {};
		int SourceW = 0, SourceH = 0;
	};
	std::mutex s_PrefetchLock;
	std::condition_variable s_PrefetchDone;
	std::unordered_map<std::wstring, std::shared_ptr<Prefetched>> s_Prefetch;
	Utils::PrefetchStats s_PrefetchStats;

	std::wstring PrefetchKey(std::wstring p)
	{
		std::replace(p.begin(), p.end(), L'/', L'\\');
		std::transform(p.begin(), p.end(), p.begin(), ::towlower);
		return p;
	}

	// 메인: 미리 디코드한 것이 있으면 가져간다 — 끝났으면 바로, 디코드 중이면 끝날 때까지, 아직 시작 전이면 가져가 직접 (잡은 건너뛴다)
	std::shared_ptr<Prefetched> TakePrefetched(const std::wstring& path)
	{
		std::unique_lock<std::mutex> lock(s_PrefetchLock);
		auto it = s_Prefetch.find(PrefetchKey(path));
		if (it == s_Prefetch.end())
			return nullptr;
		std::shared_ptr<Prefetched> p = it->second;
		if (!p->Started)
		{
			s_Prefetch.erase(it);   // 직접 디코드한다
			++s_PrefetchStats.Claimed;
			return nullptr;
		}
		if (!p->Done)
		{
			++s_PrefetchStats.Waited;
			s_PrefetchDone.wait(lock, [&] { return p->Done; });
		}
		s_Prefetch.erase(PrefetchKey(path));
		++s_PrefetchStats.Used;
		return p;
	}
}

void Utils::PrefetchTexture(const wstring& path)
{
	const std::wstring key = PrefetchKey(path);
	auto entry = std::make_shared<Prefetched>();
	{
		std::lock_guard<std::mutex> lock(s_PrefetchLock);
		if (s_Prefetch.count(key))
			return;
		s_Prefetch[key] = entry;
		++s_PrefetchStats.Requested;
	}
	Jobs::Run([entry, path, key]() {
		{
			std::lock_guard<std::mutex> lock(s_PrefetchLock);
			auto it = s_Prefetch.find(key);
			if (it == s_Prefetch.end() || it->second != entry)
				return;   // 메인이 먼저 가져가 직접 디코드했다
			entry->Started = true;
		}
		const auto t0 = std::chrono::steady_clock::now();
		const HRESULT co = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);   // WIC (PNG · JPG) 는 이 스레드에서 COM 이 열려 있어야 한다
		entry->Hr = Utils::DecodeTexture(path, entry->Img, entry->Md, entry->SourceW, entry->SourceH);
		if (SUCCEEDED(co))
			::CoUninitialize();
		{
			std::lock_guard<std::mutex> lock(s_PrefetchLock);
			entry->Done = true;
			s_PrefetchStats.DecodeMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		}
		s_PrefetchDone.notify_all();
	}, nullptr, Jobs::Priority::Background, "Texture Prefetch");
}

void Utils::PrefetchFile(const wstring& path)
{
	{
		std::lock_guard<std::mutex> lock(s_PrefetchLock);
		++s_PrefetchStats.Files;
	}
	Jobs::Run([path]() {
		std::ifstream f(fs::path(path), std::ios::binary);
		if (!f)
			return;
		char buffer[1 << 16];
		while (f.read(buffer, sizeof(buffer)) || f.gcount() > 0) {}   // 읽기만 (OS 파일 캐시)
	}, nullptr, Jobs::Priority::Background, "File Prefetch");
}

Utils::PrefetchStats Utils::GetPrefetchStats()
{
	std::lock_guard<std::mutex> lock(s_PrefetchLock);
	return s_PrefetchStats;
}

void Utils::ClearPrefetched()
{
	std::unique_lock<std::mutex> lock(s_PrefetchLock);
	// 디코드 중인 것은 끝나기를 기다린 뒤 (잡이 entry 를 쥐고 있어 지워도 되지만, 이미지 메모리를 바로 돌려주려고)
	for (auto it = s_Prefetch.begin(); it != s_Prefetch.end();)
	{
		if (it->second->Started && !it->second->Done)
			++it;
		else
			it = s_Prefetch.erase(it);
	}
}

HRESULT Utils::DecodeTexture(const wstring& path, DirectX::ScratchImage& img, DirectX::TexMetadata& md, int& sourceW, int& sourceH)
{
	// 파일 확장자 얻기
	wstring ext = fs::path(path).extension().wstring();
	HRESULT hr;
	// 프로젝트 · 패키지 에셋만 가져오기 설정을 쓴다 (에디터 아이콘 · 엔진 내부 텍스처는 원래대로)
	const AssetImport::TextureSettings ts = AssetImport::AppliesTo(path) ? AssetImport::LoadTexture(path) : AssetImport::TextureSettings::Raw();
	sourceW = 0;
	sourceH = 0;

	if (ext == L".dds" || ext == L".DDS" || ext == L".tga" || ext == L".TGA")
	{
		if (ext == L".dds" || ext == L".DDS")
			hr = ::LoadFromDDSFile(path.c_str(), DDS_FLAGS_NONE, &md, img);
		else
			hr = ::LoadFromTGAFile(path.c_str(), &md, img);
		if (SUCCEEDED(hr))
		{
			sourceW = (int)md.width;
			sourceH = (int)md.height;
			ApplyTextureImport(img, md, ts, path);
		}
	}
	else // png, jpg, jpeg, bmp
	{
		// 디코딩 + 가져오기 설정을 적용한 텍스처를 밉맵 포함 DDS로 캐시해 다음 실행의 PNG 디코딩을 생략
		// (이름에 설정 태그 — 설정을 바꾸면 다른 캐시. v2 = Import Settings 이전 캐시는 쓰지 않음)
		fs::path cacheDir = L"TextureCache";
		fs::path cacheFile = cacheDir / (fs::path(path).stem().wstring() + L"_" + std::to_wstring(std::hash<wstring>{}(fs::path(path).lexically_normal().wstring()))
			+ L"_v2" + string_to_wstring(ts.CacheTag()) + L".dds");

		std::error_code ec;
		bool cacheValid = fs::exists(cacheFile, ec) && fs::exists(path, ec) &&
			fs::last_write_time(cacheFile, ec) >= fs::last_write_time(path, ec);

		hr = E_FAIL;
		if (cacheValid)
		{
			hr = ::LoadFromDDSFile(cacheFile.c_str(), DDS_FLAGS_NONE, &md, img);
			TexMetadata src = {};
			if (SUCCEEDED(hr) && SUCCEEDED(::GetMetadataFromWICFile(path.c_str(), WIC_FLAGS_NONE, src)))
			{
				sourceW = (int)src.width;
				sourceH = (int)src.height;
			}
		}

		if (FAILED(hr))
		{
			hr = ::LoadFromWICFile(path.c_str(), WIC_FLAGS_NONE, &md, img);
			if (SUCCEEDED(hr))
			{
				sourceW = (int)md.width;
				sourceH = (int)md.height;
				ApplyTextureImport(img, md, ts, path);
				fs::create_directories(cacheDir, ec);
				::SaveToDDSFile(img.GetImages(), img.GetImageCount(), md, DDS_FLAGS_NONE, cacheFile.c_str());
			}
		}
	}
	return hr;
}

ComPtr<GfxShaderResourceView> Utils::LoadTexture(ComPtr<GfxDevice> device, const wstring& path)
{
	DirectX::TexMetadata md = {};
	DirectX::ScratchImage img;
	int sourceW = 0, sourceH = 0;
	HRESULT hr;
	// 씬 스트리밍이 미리 디코드했으면 그것을 (GPU 로 올리기만)
	if (std::shared_ptr<Prefetched> pre = TakePrefetched(path))
	{
		hr = pre->Hr;
		img = std::move(pre->Img);
		md = pre->Md;
		sourceW = pre->SourceW;
		sourceH = pre->SourceH;
	}
	else
		hr = DecodeTexture(path, img, md, sourceW, sourceH);

	ComPtr<GfxShaderResourceView> srv;
	if (SUCCEEDED(hr))
		hr = Gfx::CreateShaderResourceView(device.Get(), img.GetImages(), img.GetImageCount(), md, srv.GetAddressOf());
	if (SUCCEEDED(hr))
	{
		AssetImport::TextureInfo info;
		info.SourceWidth = sourceW;
		info.SourceHeight = sourceH;
		info.Width = (int)md.width;
		info.Height = (int)md.height;
		info.Mips = (int)md.mipLevels;
		info.Bytes = img.GetPixelsSize();
		switch (MakeTypeless(md.format))
		{
		case DXGI_FORMAT_R8G8B8A8_TYPELESS: info.Format = "RGBA32"; break;
		case DXGI_FORMAT_B8G8R8A8_TYPELESS: info.Format = "BGRA32"; break;
		case DXGI_FORMAT_B8G8R8X8_TYPELESS: info.Format = "BGRX32"; break;
		case DXGI_FORMAT_BC1_TYPELESS: info.Format = "BC1 (DXT1)"; break;
		case DXGI_FORMAT_BC2_TYPELESS: info.Format = "BC2 (DXT3)"; break;
		case DXGI_FORMAT_BC3_TYPELESS: info.Format = "BC3 (DXT5)"; break;
		case DXGI_FORMAT_BC4_TYPELESS: info.Format = "BC4"; break;
		case DXGI_FORMAT_BC5_TYPELESS: info.Format = "BC5"; break;
		case DXGI_FORMAT_BC7_TYPELESS: info.Format = "BC7"; break;
		case DXGI_FORMAT_R16G16B16A16_TYPELESS: info.Format = "RGBA64"; break;
		default: info.Format = "format " + std::to_string((int)md.format); break;
		}
		if (IsSRGB(md.format))
			info.Format += " sRGB";
		AssetImport::RecordTexture(path, info);
	}

	if (FAILED(hr))
	{
		std::ostringstream message;
		message << "Texture load failed: " << wstring_to_string(path)
			<< " (HRESULT 0x" << std::hex << static_cast<unsigned long>(hr) << std::dec
			<< ", " << md.width << "x" << md.height << ", format " << md.format << ")";
		Debug::Log(message.str());
		std::ofstream log("texture_errors.txt", std::ios::app);
		log << message.str() << std::endl;

		// Keep the editor usable when an asset cannot be uploaded to the GPU.
		const uint32_t missingTexturePixel = 0xFFFF00FF;
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = desc.Height = desc.MipLevels = desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_IMMUTABLE;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		D3D11_SUBRESOURCE_DATA data = {};
		data.pSysMem = &missingTexturePixel;
		data.SysMemPitch = sizeof(missingTexturePixel);
		ComPtr<GfxTexture2D> fallback;
		if (SUCCEEDED(device->CreateTexture2D(&desc, &data, fallback.GetAddressOf())))
			device->CreateShaderResourceView(fallback.Get(), nullptr, srv.ReleaseAndGetAddressOf());
	}

	return srv;
}

ComPtr<GfxShaderResourceView> Utils::CreateTexture2DArraySRV(ComPtr<GfxDevice> device, ComPtr<GfxContext> context, std::vector<std::wstring>& filenames)
{
	//
	// Load the texture elements individually from file.  These textures
	// won't be used by the GPU (0 bind flags), they are just used to 
	// load the image data from file.  We use the STAGING usage so the
	// CPU can read the resource.
	//

	uint32 size = filenames.size();

	std::vector<ComPtr<GfxTexture2D>> srcTex(size);

	for (uint32 i = 0; i < size; ++i)
	{
		DirectX::TexMetadata md = {};

		DirectX::ScratchImage img;
		HRESULT hr = ::LoadFromDDSFile(filenames[i].c_str(), DDS_FLAGS_NONE, &md, img);
		CHECK(hr);

		hr = Gfx::CreateTexture(device.Get(), img.GetImages(), img.GetImageCount(), md,
			D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE, 0, (GfxResource**)srcTex[i].GetAddressOf());
		
		CHECK(hr);
	}

	//
	// Create the texture array.  Each element in the texture 
	// array has the same format/dimensions.
	//

	D3D11_TEXTURE2D_DESC texElementDesc;
	srcTex[0]->GetDesc(&texElementDesc);
	//D3D11_TEXTURE2D_DESC texElementDesc1;
	//srcTex[1]->GetDesc(&texElementDesc1);
	//D3D11_TEXTURE2D_DESC texElementDesc2;
	//srcTex[2]->GetDesc(&texElementDesc2);
	//D3D11_TEXTURE2D_DESC texElementDesc3;
	//srcTex[3]->GetDesc(&texElementDesc3);


	D3D11_TEXTURE2D_DESC texArrayDesc;
	texArrayDesc.Width = texElementDesc.Width;
	texArrayDesc.Height = texElementDesc.Height;
	texArrayDesc.MipLevels = texElementDesc.MipLevels;
	texArrayDesc.ArraySize = size;
	texArrayDesc.Format = texElementDesc.Format;
	texArrayDesc.SampleDesc.Count = 1;
	texArrayDesc.SampleDesc.Quality = 0;
	texArrayDesc.Usage = D3D11_USAGE_DEFAULT;
	texArrayDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	texArrayDesc.CPUAccessFlags = 0;
	texArrayDesc.MiscFlags = 0;

	ComPtr<GfxTexture2D> texArray;
	HR(device->CreateTexture2D(&texArrayDesc, 0, texArray.GetAddressOf()));

	//
	// Copy individual texture elements into texture array.
	//

	// for each texture element...
	for (uint32 texElement = 0; texElement < size; ++texElement)
	{
		// for each mipmap level...
		for (uint32 mipLevel = 0; mipLevel < texElementDesc.MipLevels; ++mipLevel)
		{
			D3D11_MAPPED_SUBRESOURCE mappedTex2D;
			HR(context->Map(srcTex[texElement].Get(), mipLevel, D3D11_MAP_READ, 0, &mappedTex2D));

			context->UpdateSubresource(texArray.Get(),
				::D3D11CalcSubresource(mipLevel, texElement, texElementDesc.MipLevels), 
				0, mappedTex2D.pData, mappedTex2D.RowPitch, mappedTex2D.DepthPitch);

			context->Unmap(srcTex[texElement].Get(), mipLevel);
		}
	}

	//
	// Create a resource view to the texture array.
	//

	D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc;
	ZeroMemory(&viewDesc, sizeof(D3D11_SHADER_RESOURCE_VIEW_DESC));
	viewDesc.Format = texArrayDesc.Format;
	viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
	viewDesc.Texture2DArray.MostDetailedMip = 0;
	viewDesc.Texture2DArray.MipLevels = texArrayDesc.MipLevels;
	viewDesc.Texture2DArray.FirstArraySlice = 0;
	viewDesc.Texture2DArray.ArraySize = size;

	ComPtr<GfxShaderResourceView> texArraySRV;
	HR(device->CreateShaderResourceView(texArray.Get(), &viewDesc, texArraySRV.GetAddressOf()));

	return texArraySRV;
}


ComPtr<GfxShaderResourceView> Utils::CreateRandomTexture1DSRV(ComPtr<GfxDevice> device)
{
	// 
	// Create the random data.
	//
	vector<XMFLOAT4> randomValues(1024);

	for (int32 i = 0; i < 1024; ++i)
	{
		randomValues[i].x = MathHelper::RandF(-1.0f, 1.0f);
		randomValues[i].y = MathHelper::RandF(-1.0f, 1.0f);
		randomValues[i].z = MathHelper::RandF(-1.0f, 1.0f);
		randomValues[i].w = MathHelper::RandF(-1.0f, 1.0f);
	}

    D3D11_SUBRESOURCE_DATA initData;
    initData.pSysMem = randomValues.data();
	initData.SysMemPitch = 1024 * sizeof(XMFLOAT4);
    initData.SysMemSlicePitch = 0;

	//
	// Create the texture.
	//
    D3D11_TEXTURE1D_DESC texDesc;
    texDesc.Width = 1024;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    texDesc.Usage = D3D11_USAGE_IMMUTABLE;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    texDesc.CPUAccessFlags = 0;
    texDesc.MiscFlags = 0;
    texDesc.ArraySize = 1;

	ComPtr<GfxTexture1D> randomTex;
    HR(device->CreateTexture1D(&texDesc, &initData, randomTex.GetAddressOf()));

	//
	// Create the resource view.
	//
    D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc;
	viewDesc.Format = texDesc.Format;
    viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE1D;
    viewDesc.Texture1D.MipLevels = texDesc.MipLevels;
	viewDesc.Texture1D.MostDetailedMip = 0;
	
	ComPtr<GfxShaderResourceView> randomTexSRV;
    HR(device->CreateShaderResourceView(randomTex.Get(), &viewDesc, randomTexSRV.GetAddressOf()));

	return randomTexSRV;
}

std::wstring string_to_wstring(const std::string& str)
{
	if (str.empty()) return std::wstring();
	int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
	std::wstring wstr(size_needed, 0);
	MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstr[0], size_needed);
	return wstr;
}

std::string wstring_to_string(const std::wstring& wstr)
{
	if (wstr.empty()) return std::string();
	int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
	std::string str(size_needed, 0);
	WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &str[0], size_needed, NULL, NULL);
	return str;
}
