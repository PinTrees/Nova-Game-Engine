#include "pch.h"
#include "ImageIO.h"
#include <webp/decode.h>
#include <fstream>
#include <filesystem>
#include <complex>
#include <algorithm>

namespace
{
	bool ReadAll(const wchar_t* path, std::vector<uint8_t>& out)
	{
		std::ifstream is(std::filesystem::path(path), std::ios::binary);
		if (!is)
			return false;
		is.seekg(0, std::ios::end);
		const std::streamoff n = is.tellg();
		if (n <= 0)
			return false;
		out.resize((size_t)n);
		is.seekg(0, std::ios::beg);
		is.read(reinterpret_cast<char*>(out.data()), n);
		return (bool)is;
	}
}

namespace ImageIO
{
	bool IsWebP(const std::wstring& path)
	{
		std::wstring ext = std::filesystem::path(path).extension().wstring();
		for (wchar_t& ch : ext)
			ch = (wchar_t)towlower(ch);
		return ext == L".webp";
	}

	bool DecodeWebP(const void* data, size_t size, std::vector<uint8_t>& rgba, int& w, int& h)
	{
		w = h = 0;
		uint8_t* px = WebPDecodeRGBA(static_cast<const uint8_t*>(data), size, &w, &h);
		if (px == nullptr || w <= 0 || h <= 0)
			return false;
		rgba.assign(px, px + (size_t)w * h * 4);
		WebPFree(px);
		return true;
	}

	HRESULT Load(const wchar_t* path, DirectX::WIC_FLAGS flags, DirectX::TexMetadata* metadata, DirectX::ScratchImage& image)
	{
		if (!IsWebP(path))
			return DirectX::LoadFromWICFile(path, flags, metadata, image);
		std::vector<uint8_t> file, rgba;
		int w = 0, h = 0;
		if (!ReadAll(path, file) || !DecodeWebP(file.data(), file.size(), rgba, w, h))
			return E_FAIL;
		HRESULT hr = image.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, (size_t)w, (size_t)h, 1, 1);
		if (FAILED(hr))
			return hr;
		const DirectX::Image* img = image.GetImage(0, 0, 0);
		for (int y = 0; y < h; ++y)
			memcpy(img->pixels + (size_t)y * img->rowPitch, rgba.data() + (size_t)y * w * 4, (size_t)w * 4);
		if (metadata)
			*metadata = image.GetMetadata();
		return S_OK;
	}

	HRESULT GetMetadata(const wchar_t* path, DirectX::WIC_FLAGS flags, DirectX::TexMetadata& metadata)
	{
		if (!IsWebP(path))
			return DirectX::GetMetadataFromWICFile(path, flags, metadata);
		std::vector<uint8_t> file;
		int w = 0, h = 0;
		if (!ReadAll(path, file) || !WebPGetInfo(file.data(), file.size(), &w, &h))
			return E_FAIL;
		metadata = DirectX::TexMetadata();
		metadata.width = (size_t)w;
		metadata.height = (size_t)h;
		metadata.depth = metadata.arraySize = metadata.mipLevels = 1;
		metadata.format = DXGI_FORMAT_R8G8B8A8_UNORM;
		metadata.dimension = DirectX::TEX_DIMENSION_TEXTURE2D;
		return S_OK;
	}
}

namespace
{
	// 제자리 기수 2 FFT (n = 2 의 거듭제곱). inverse = 역변환 (1/n 은 부른 쪽)
	void Fft(std::complex<float>* a, int n, bool inverse)
	{
		for (int i = 1, j = 0; i < n; ++i)
		{
			int bit = n >> 1;
			for (; j & bit; bit >>= 1)
				j ^= bit;
			j ^= bit;
			if (i < j)
				std::swap(a[i], a[j]);
		}
		for (int len = 2; len <= n; len <<= 1)
		{
			const float ang = 2.0f * 3.14159265358979f / len * (inverse ? 1.0f : -1.0f);
			const std::complex<float> wl(cosf(ang), sinf(ang));
			for (int i = 0; i < n; i += len)
			{
				std::complex<float> w(1.0f, 0.0f);
				for (int k = 0; k < len / 2; ++k)
				{
					const std::complex<float> u = a[i + k], v = a[i + k + len / 2] * w;
					a[i + k] = u + v;
					a[i + k + len / 2] = u - v;
					w *= wl;
				}
			}
		}
	}

	// 2 차원 FFT (행 → 열)
	void Fft2(std::vector<std::complex<float>>& g, int n, bool inverse)
	{
		for (int y = 0; y < n; ++y)
			Fft(&g[(size_t)y * n], n, inverse);
		std::vector<std::complex<float>> col(n);
		for (int x = 0; x < n; ++x)
		{
			for (int y = 0; y < n; ++y)
				col[y] = g[(size_t)y * n + x];
			Fft(col.data(), n, inverse);
			for (int y = 0; y < n; ++y)
				g[(size_t)y * n + x] = col[y];
		}
	}
}

namespace ImageIO
{
	bool HeightFromNormal(const uint8_t* rgba, int w, int h, int size, std::vector<float>& height, bool flipGreen)
	{
		if (!rgba || w <= 0 || h <= 0)
			return false;
		int n = 16;
		while (n < size && n < 1024)
			n <<= 1;
		// 기울기 (텍셀 단위): 가까운 원본 칸들의 평균 노멀 (n 이 원본보다 작으면 칸을 모아서)
		std::vector<std::complex<float>> P((size_t)n * n), Q((size_t)n * n);
		const int bx = (std::max)(1, w / n), by = (std::max)(1, h / n);
		for (int y = 0; y < n; ++y)
			for (int x = 0; x < n; ++x)
			{
				const int x0 = x * w / n, y0 = y * h / n;
				float sx = 0, sy = 0, sz = 0;
				for (int j = 0; j < by; ++j)
					for (int i = 0; i < bx; ++i)
					{
						const uint8_t* p = rgba + ((size_t)((y0 + j) % h) * w + (x0 + i) % w) * 4;
						sx += p[0] / 127.5f - 1.0f;
						sy += p[1] / 127.5f - 1.0f;
						sz += p[2] / 127.5f - 1.0f;
					}
				if (flipGreen)
					sy = -sy;
				sz = (std::max)(sz, 0.2f * bx * by);
				// 한 칸 = 원본 텍셀 여러 개 → 기울기는 칸 크기만큼 (결과는 끝에 0..1 로 다시 맞춘다)
				P[(size_t)y * n + x] = std::complex<float>(-sx / sz, 0.0f);
				Q[(size_t)y * n + x] = std::complex<float>(-sy / sz, 0.0f);
			}
		Fft2(P, n, false);
		Fft2(Q, n, false);
		// H = (-i wu P - i wv Q) / (wu² + wv²), 낮은 주파수 (타일 2 번 미만) 는 부드럽게 줄인다
		const float tau = 6.28318530718f;
		const float f0 = 2.0f;
		for (int v = 0; v < n; ++v)
			for (int u = 0; u < n; ++u)
			{
				const size_t i = (size_t)v * n + u;
				const float fu = (float)(u <= n / 2 ? u : u - n), fv = (float)(v <= n / 2 ? v : v - n);
				if (u == 0 && v == 0)
				{
					P[i] = 0.0f;
					continue;
				}
				const float wu = sinf(tau * fu / n), wv = sinf(tau * fv / n);   // 중앙 차분과 같은 미분 (나이퀴스트에서 0)
				const float den = wu * wu + wv * wv;
				const float f2 = fu * fu + fv * fv;
				const float keep = 1.0f - expf(-f2 / (f0 * f0));
				if (den < 1e-8f)
				{
					P[i] = 0.0f;
					continue;
				}
				const std::complex<float> I(0.0f, 1.0f);
				P[i] = (-I * wu * P[i] - I * wv * Q[i]) * (keep / den);
			}
		Fft2(P, n, true);
		height.resize((size_t)n * n);
		for (size_t i = 0; i < height.size(); ++i)
			height[i] = P[i].real();
		// 1 % · 99 % → 0 · 1
		std::vector<float> sorted = height;
		const size_t lo = sorted.size() / 100, hi = sorted.size() - 1 - sorted.size() / 100;
		std::nth_element(sorted.begin(), sorted.begin() + lo, sorted.end());
		const float a = sorted[lo];
		std::nth_element(sorted.begin(), sorted.begin() + hi, sorted.end());
		const float b = sorted[hi];
		const float k = b - a > 1e-8f ? 1.0f / (b - a) : 0.0f;
		for (float& v : height)
			v = std::clamp((v - a) * k, 0.0f, 1.0f);
		return true;
	}
}
