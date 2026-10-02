#pragma once
#include <string>

// UI 스프라이트 = 텍스처 + 크기 + 9 조각 테두리.
//  - 내장: "builtin:UISprite"(둥근 사각형 + 얇은 테두리, Button), "builtin:Background"(둥근 사각형, Panel), "builtin:Knob"(원)
//    Unity 의 기본 UI 스프라이트와 같은 역할이며 실행 중에 만들어진다.
//  - 파일: Assets 의 .png/.jpg/.tga/.bmp/.dds (테두리 없음)
//  - 잘라 놓은 스프라이트: "Assets/…png#이름" (텍스처 가져오기 설정 Sprite Mode = Multiple 의 사각형)
namespace UISprites
{
	struct Info
	{
		GfxShaderResourceView* Texture = nullptr;
		Vec2 Size = Vec2(0, 0);                 // 픽셀
		Vec4 Border = Vec4(0, 0, 0, 0);         // 왼쪽, 아래, 오른쪽, 위 (픽셀)
		// 잘라 놓은 스프라이트 ("그림.png#이름", Sprite Mode = Multiple): 텍스처 안의 UV (u0, v위, u1, v아래) · 기준점
		Vec4 UV = Vec4(0, 0, 1, 1);
		Vec2 Pivot = Vec2(0.5f, 0.5f);
		bool SubSprite = false;
	};

	bool Get(const std::string& path, Info& out);
	std::vector<std::string> FindAll();
	// SpriteRenderer 용: 2D 내장 도형 (Square · Circle · Capsule · Triangle) + Assets 의 그림
	std::vector<std::string> FindAll2D();
	bool IsBuiltin2D(const std::string& path);
	std::string DisplayName(const std::string& path);
	bool IsImagePath(const std::string& path);
}
