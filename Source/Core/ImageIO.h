#pragma once
#include <string>
#include <vector>

// 이미지 파일 읽기: WIC (PNG · JPG · BMP · GIF · TIFF) + WebP (libwebp — Windows 의 WebP 코덱이 없어도).
//  엔진이 이미지를 읽는 곳 (텍스처 가져오기 · 스프라이트 자르기 · 가상 텍스처 · 빌드 압축) 은 LoadFromWICFile 대신 이것을 부른다.
//  WebP 는 R8G8B8A8_UNORM 한 장 (밉 없음) — 가져오기 설정 · DDS 캐시는 부른 쪽이 PNG 와 같은 길로.
namespace ImageIO
{
	NOVA_API bool IsWebP(const std::wstring& path);
	NOVA_API HRESULT Load(const wchar_t* path, DirectX::WIC_FLAGS flags, DirectX::TexMetadata* metadata, DirectX::ScratchImage& image);
	NOVA_API HRESULT GetMetadata(const wchar_t* path, DirectX::WIC_FLAGS flags, DirectX::TexMetadata& metadata);
	// 메모리의 WebP → RGBA8 (줄 = w * 4 바이트)
	NOVA_API bool DecodeWebP(const void* data, size_t size, std::vector<uint8_t>& rgba, int& w, int& h);
}
