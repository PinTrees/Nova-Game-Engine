#pragma once
#include <string>
#include <vector>

// Unity 의 Tile 에셋 (.tile, JSON): 스프라이트 하나 + 색 + 콜라이더 종류
//  {"sprite": "Assets/Tiles/terrain.png#terrain_3", "color": [1, 1, 1, 1], "colliderType": "sprite"}
//  - colliderType: none (부딪히지 않음) · sprite (그림 사각형 — Unity 의 기본) · grid (칸 전체)
//  - 스프라이트 크기 = 그림 픽셀 / Pixels Per Unit (텍스처 가져오기 설정), 기준점이 칸의 Tile Anchor 에 놓인다
namespace TileAssets
{
	enum class ColliderType { None = 0, Sprite = 1, Grid = 2 };

	struct Tile
	{
		std::string Sprite;
		float Color[4] = { 1, 1, 1, 1 };
		ColliderType Collider = ColliderType::Sprite;

		// 풀어 둔 그림 (SpriteRenderer::ResolveSprite)
		bool HasSprite = false;
		GfxShaderResourceView* Texture = nullptr;
		Vec2 Size = Vec2(0, 0);          // 월드 단위
		Vec2 Pivot = Vec2(0.5f, 0.5f);
		Vec4 UV = Vec4(0, 0, 1, 1);      // u0, v위, u1, v아래
		bool Point = false;
	};

	// 프로젝트 기준 경로 → 읽은 타일 (파일 · 그림 가져오기 설정이 바뀌면 다시 읽는다 — 0.5 초마다 확인). 없으면 nullptr
	const Tile* Get(const std::string& path);
	// 어떤 타일이든 모양 (그림 · 크기 · 색 · 콜라이더) 이 바뀌면 + (렌더러 · 콜라이더가 다시 만든다)
	uint32 Revision();
	bool Save(const std::string& path, const Tile& tile, std::string& error);
	const char* ColliderName(ColliderType t);
	bool ParseCollider(const std::string& name, ColliderType& out);

	// 프로젝트 기준 경로 ↔ 디스크 경로
	std::wstring FullPath(const std::string& relPath);
	std::string ProjectRelative(const std::wstring& fullPath);
	// folder 아래 (하위 폴더 포함) 의 .tile — 이름 순
	std::vector<std::string> FindAll(const std::string& folder = "Assets");
	// 경로가 .tile 로 끝나나 (대소문자 무시)
	bool IsTilePath(const std::string& path);
	// 그림 (Sprite Mode = Multiple 이면 잘라 놓은 사각형마다, 아니면 하나) → 스프라이트 경로 목록 ("그림.png#이름")
	std::vector<std::string> SpritesOf(const std::string& texturePath);
}
