#pragma once
#include <string>
#include "AssetImportSettings.h"

// 안드로이드 텍스처 굽기 (Unity 의 플랫폼별 텍스처 압축): 원본 그림 → 가져오기 설정 (PC 와 같은 크기 · 밉 · sRGB) → ASTC · ETC2 · BC · RGBA32 → DDS.
//  기기에는 그림 디코더 (WIC) · 압축기가 없으므로 nova android export 가 PC 에서 미리 굽는다. ASTC = ARM astc-encoder, ETC2 = Etc2Codec
namespace TextureCompressor
{
	// Build Settings > Android > Texture Compression (Unity 와 같은 뜻: 가져오기 설정의 Android Format 이 Automatic 일 때 쓰는 형식)
	enum class AndroidDefault { ASTC = 0, ETC2 = 1, DXT = 2, None = 3 };
	const char* AndroidDefaultName(AndroidDefault d);

	struct Result
	{
		std::string Format;      // "ASTC 6x6", "ETC2 RGBA", "BC1", "RGBA32"
		int Width = 0, Height = 0, Mips = 0;
		size_t Bytes = 0;
		double Psnr = 0.0;       // 밉 0 의 원본 대비 (dB, 압축 없음 = 0)
		bool Srgb = false;
	};

	// 이 텍스처가 안드로이드에서 쓸 형식의 이름 (Inspector 의 Automatic 설명)
	std::string AndroidFormatFor(const AssetImport::TextureSettings& ts, AndroidDefault projectDefault);

	// 원본 (png · jpg · tga · bmp …) → outDds. 실패하면 false + error
	bool BuildAndroid(const std::wstring& source, const AssetImport::TextureSettings& ts, AndroidDefault projectDefault, const std::wstring& outDds,
		Result& result, std::string& error);
}
