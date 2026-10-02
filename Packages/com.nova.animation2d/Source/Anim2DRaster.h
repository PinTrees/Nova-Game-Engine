#pragma once
#include "Anim2DDocument.h"

// 2D 애니메이터 그리기 = CPU (창 · CLI PNG · 스프라이트 시트가 같은 그림, DirectX 11 / OpenGL 무관).
//  첨부 그림 = 사각형 두 삼각형 (bilinear + 알파 섞기), 슬롯 색을 곱한다. 본 = Spine 처럼 마름모 (선택 = 주황)
namespace Anim2D
{
	struct View2D
	{
		float CX = 0, CY = 150;   // 화면 가운데의 월드 점
		float Zoom = 1;           // 화면 픽셀 / 월드 단위
	};

	struct Image
	{
		int W = 0, H = 0;
		std::vector<uint32> Px;   // RGBA8 (R 이 낮은 바이트), 곱하지 않은 알파
		bool Ok = false;
	};
	// 그림 (경로 = 프로젝트 상대 또는 절대). 파일이 바뀌면 다시 읽는다
	const Image* GetImage(const std::string& path);
	void ForgetImages();

	struct RenderOptions
	{
		bool Bones = true;
		bool Grid = true;
		bool Background = true;      // false = 투명 (내보내기)
		uint32 BgColor = 0xFF2A2725;
		int SelectedBone = -1;
		int SelectedSlot = -1;
		int HoverBone = -1;
	};

	// 첨부의 네 귀퉁이 (월드): 왼아래 · 오른아래 · 오른위 · 왼위, 크기 (그림이 없으면 Width/Height)
	bool AttachmentCorners(const Document& d, const Slot& s, const Attachment& a, float out[4][2]);

	class Raster
	{
	public:
		int Width = 0, Height = 0;
		std::vector<uint32> Color;
		std::vector<int> SlotId;     // 픽셀마다 맨 위 슬롯 (-1 = 없음) — 클릭으로 고르기
		View2D View;

		void Resize(int w, int h);
		void Render(const Document& d, const View2D& view, const RenderOptions& opt);
		void WorldToScreen(float wx, float wy, float& sx, float& sy) const;
		void ScreenToWorld(float sx, float sy, float& wx, float& wy) const;
		bool SavePng(const std::string& path, std::string& error) const;
		// 보이는 것 전체 (그림 + 본) 의 월드 경계 상자
		static bool Bounds(const Document& d, float& minX, float& minY, float& maxX, float& maxY, bool withBones = true);

		void Line(float x0, float y0, float x1, float y1, uint32 color, float width);
		void FillTriangle(const float a[2], const float b[2], const float c[2], uint32 color);
		void Disc(float x, float y, float r, uint32 color);

	private:
		bool m_Transparent = false;
		void Blend(int i, float r, float g, float b, float a);
		void TexturedTriangle(const float s[3][2], const float uv[3][2], const Image& img, const float tint[4], int slot);
		void DrawGrid();
		void DrawBones(const Document& d, const RenderOptions& opt);
	};
}
