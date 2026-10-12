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
	// 노멀맵 → 높이 (높이 맵이 없는 재질의 테셀레이션 · POM): DirectX 노멀 (g = 아래로 = 행이 느는 쪽) 의 기울기 (-x/z, -y/z) 를
	//  푸리에로 적분 (Frankot-Chellappa — 이어지는 타일 텍스처는 주기 경계라 꼭 맞는다). 아주 낮은 주파수 (타일에 두 번 미만) 는 줄여
	//  큰 덩어리 대신 돌 · 줄눈 같은 무늬가 높이가 된다. rgba = RGBA8 노멀맵 (w × h, 줄 = w * 4), size = 결과 한 변 (2 의 거듭제곱, ≤ 1024),
	//  결과 0..1 (1 % · 99 % 를 0 · 1 로). flipGreen = OpenGL 노멀 (g = 위로)
	NOVA_API bool HeightFromNormal(const uint8_t* rgba, int w, int h, int size, std::vector<float>& height, bool flipGreen = false);
}
