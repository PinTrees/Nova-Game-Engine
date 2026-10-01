#include "pch.h"
#include "Utils.h"
#include "MathHelper.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include "Debug.h"
namespace fs = std::filesystem;

ComPtr<GfxShaderResourceView> Utils::LoadTexture(ComPtr<GfxDevice> device, const wstring& path)
{
	// 파일 확장자 얻기
	wstring ext = fs::path(path).extension();

	DirectX::TexMetadata md = {};
	DirectX::ScratchImage img;

	HRESULT hr;

	if (ext == L".dds" || ext == L".DDS")
		hr = ::LoadFromDDSFile(path.c_str(), DDS_FLAGS_NONE,& md, img);
	else if (ext == L".tga" || ext == L".TGA")
		hr = ::LoadFromTGAFile(path.c_str(), &md, img);
	else // png, jpg, jpeg, bmp
	{
		// 디코딩된 텍스처를 밉맵 포함 DDS로 캐시해 다음 실행의 PNG 디코딩을 생략
		fs::path cacheDir = L"TextureCache";
		fs::path cacheFile = cacheDir / (fs::path(path).stem().wstring() + L"_" + std::to_wstring(std::hash<wstring>{}(fs::path(path).lexically_normal().wstring())) + L".dds");

		std::error_code ec;
		bool cacheValid = fs::exists(cacheFile, ec) && fs::exists(path, ec) &&
			fs::last_write_time(cacheFile, ec) >= fs::last_write_time(path, ec);

		hr = E_FAIL;
		if (cacheValid)
			hr = ::LoadFromDDSFile(cacheFile.c_str(), DDS_FLAGS_NONE, &md, img);

		if (FAILED(hr))
		{
			hr = ::LoadFromWICFile(path.c_str(), WIC_FLAGS_NONE, &md, img);
			if (SUCCEEDED(hr))
			{
				if (md.mipLevels == 1 && md.width > 1 && md.height > 1)
				{
					ScratchImage mipped;
					if (SUCCEEDED(::GenerateMipMaps(img.GetImages(), img.GetImageCount(), md, TEX_FILTER_DEFAULT, 0, mipped)))
					{
						img = std::move(mipped);
						md = img.GetMetadata();
					}
				}
				fs::create_directories(cacheDir, ec);
				::SaveToDDSFile(img.GetImages(), img.GetImageCount(), md, DDS_FLAGS_NONE, cacheFile.c_str());
			}
		}
	}

	ComPtr<GfxShaderResourceView> srv;
	if (SUCCEEDED(hr))
		hr = Gfx::CreateShaderResourceView(device.Get(), img.GetImages(), img.GetImageCount(), md, srv.GetAddressOf());

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
