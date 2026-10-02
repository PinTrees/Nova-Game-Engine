#pragma once
#include "AssetImportSettings.h"

// Unity 의 Sprite Editor > Slice: 그림 한 장을 여러 스프라이트 사각형으로 (Sprite Mode = Multiple).
//  - Grid By Cell Size (칸 크기 · 여백 · 간격), Grid By Cell Count (열 × 행), Automatic (투명이 아닌 덩어리마다)
//  - 2D Animator 의 스프라이트 시트 JSON (export 의 frames · pivot) 을 그대로 → 프레임 애니메이션
//  - 사각형 = 원본 픽셀, 왼쪽 아래 원점 (Unity 와 같음). 이름 = "<그림 이름>_<번호>" (위 → 아래, 왼쪽 → 오른쪽)
namespace SpriteSlicer
{
	struct Pixels
	{
		int W = 0, H = 0;
		std::vector<uint32> RGBA;   // 위 줄부터
		bool Load(const std::wstring& fullPath);
		uint8 Alpha(int x, int yTop) const { return (uint8)(RGBA[(size_t)yTop * W + x] >> 24); }
		bool Empty(int x, int yTop, int w, int h) const;   // 사각형 (위 기준) 안이 모두 투명
	};

	struct Options
	{
		std::string BaseName;          // 이름 앞부분 (보통 그림 파일 이름)
		float PivotX = 0.5f, PivotY = 0.5f;
		bool KeepEmpty = false;        // Grid: 빈 칸도 남길지
	};

	std::vector<AssetImport::SpriteRect> GridBySize(const Pixels& px, int cellW, int cellH, int offX, int offY, int padX, int padY, const Options& o);
	std::vector<AssetImport::SpriteRect> GridByCount(const Pixels& px, int columns, int rows, const Options& o);
	std::vector<AssetImport::SpriteRect> Automatic(const Pixels& px, int minSize, const Options& o);
	// 2D Animator 의 export JSON ({frames: [{x,y,w,h}], cell, pivot (칸 안 픽셀, 위 기준)}) → 사각형 + 기준점 (발 밑)
	bool FromSheetJson(const std::wstring& jsonPath, int texW, int texH, const Options& o, std::vector<AssetImport::SpriteRect>& out, float* fps = nullptr);
	// 그림 옆의 같은 이름 .json 이 2D Animator 시트인가
	std::wstring SheetJsonFor(const std::wstring& imagePath);
}
