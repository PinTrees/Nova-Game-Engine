#pragma once
#include <string>

// UI 스프라이트 = 텍스처 + 크기 + 9 조각 테두리.
//  - 내장: "builtin:UISprite"(둥근 사각형 + 얇은 테두리, Button), "builtin:Background"(둥근 사각형, Panel), "builtin:Knob"(원)
//    Unity 의 기본 UI 스프라이트와 같은 역할이며 실행 중에 만들어진다.
//  - 파일: Assets 의 .png/.jpg/.tga/.bmp/.dds (테두리 없음)
namespace UISprites
{
	struct Info
	{
		GfxShaderResourceView* Texture = nullptr;
		Vec2 Size = Vec2(0, 0);                 // 픽셀
		Vec4 Border = Vec4(0, 0, 0, 0);         // 왼쪽, 아래, 오른쪽, 위 (픽셀)
	};

	bool Get(const std::string& path, Info& out);
	std::vector<std::string> FindAll();
	// SpriteRenderer 용: 2D 내장 도형 (Square · Circle · Capsule · Triangle) + Assets 의 그림
	std::vector<std::string> FindAll2D();
	bool IsBuiltin2D(const std::string& path);
	std::string DisplayName(const std::string& path);
	bool IsImagePath(const std::string& path);
}
