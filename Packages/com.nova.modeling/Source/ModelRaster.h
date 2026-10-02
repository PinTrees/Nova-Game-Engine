#pragma once
#include "ModelDocument.h"

// 모델 편집기 그리기 = CPU 래스터라이저 (GPU 셰이더 없음).
//  - 편집기 뷰포트와 CLI 의 PNG 렌더 (nova model render) 가 같은 그림을 쓴다 → AI 가 보는 것 = 사람이 보는 것
//  - DirectX 11 / OpenGL 어느 쪽이든 같고, 창이 없어도 그린다
//  - 픽셀마다 깊이 · 면 번호 (오브젝트, 면) 를 남겨 고르기 (클릭 · 상자 선택) 에 쓴다
namespace Modeling
{
	struct ViewCamera
	{
		Vec3 Target = Vec3(0, 0.8f, 0);
		float Yaw = 0.6f;          // 0 = 앞 (+Z 쪽에서 -Z 를 본다)
		float Pitch = 0.35f;
		float Distance = 4.0f;
		bool Ortho = false;
		float Fov = 50.0f;         // 세로, 도

		Vec3 Forward() const;      // 카메라가 보는 방향
		Vec3 Eye() const;
		Matrix View() const;
		Matrix Proj(float aspect) const;
		// 이름 있는 시점: front back left right top bottom persp
		bool SetPreset(const std::string& name);
		// 경계 상자가 화면에 꽉 차게
		void Frame(const Vec3& mn, const Vec3& mx, float aspect);
	};

	enum class Shading { Solid = 0, Toon = 1, Normals = 2 };

	struct RasterOptions
	{
		bool Wireframe = false;      // Object 모드에서도 선 (Edit 모드는 늘). CLI 렌더는 기본 켬
		bool Grid = true;
		bool Selection = true;       // Object 모드에서 고른 오브젝트 선 (주황)
		bool XRay = false;           // 면을 반투명 · 가려진 점도 고르기
		bool Background = true;
		Shading Shade = Shading::Solid;
		int HoverObject = -1, HoverElement = -1;   // 강조 (Edit 모드: 모드에 따라 점 · 변 · 면 번호)
		std::vector<std::pair<Vec3, Vec3>> ExtraLines;   // 월드 선 (축 제한 안내 등)
		uint32 ExtraColor = 0xFF3399FF;
	};

	class Raster
	{
	public:
		int Width = 0, Height = 0;
		std::vector<uint32> Color;      // RGBA8 (R 이 낮은 바이트)
		std::vector<float> Depth;       // 0..1 (NDC z), 1 = 비어 있음
		std::vector<int> FaceId;        // (오브젝트 << 20) | 면, -1 = 없음
		Matrix ViewProj;

		void Resize(int w, int h);
		void Render(Document& doc, const ViewCamera& cam, const RasterOptions& opt);

		// 월드 → 화면 픽셀 (z = 깊이). 카메라 뒤면 false
		bool Project(const Vec3& world, Vec3& screen) const;
		// 화면 점이 이 깊이에서 보이나 (깊이 버퍼와 비교)
		bool Visible(const Vec3& screen, float bias = 0.0015f) const;
		// 화면 픽셀 → 월드 광선
		void Ray(float x, float y, Vec3& origin, Vec3& dir) const;
		int FaceAt(int x, int y) const { return x >= 0 && y >= 0 && x < Width && y < Height ? FaceId[y * Width + x] : -1; }

		bool SavePng(const std::string& path, std::string& error) const;

	private:
		Matrix m_InvViewProj;
		Matrix m_View;
		ViewCamera m_Cam;
		void Clear(bool background);
		void Triangle(const Vec3 s[3], const Vec3 n[3], const Vec3 baseColor, float alpha, int id, const Vec3& viewDir, Shading shade);
		void Line(Vec3 a, Vec3 b, uint32 color, float width, bool depthTest);
		void Dot(const Vec3& s, float size, uint32 color, bool depthTest);
		void Blend(int x, int y, uint32 color, float coverage);
		void DrawGrid();
	};

	inline uint32 Rgba(int r, int g, int b, int a = 255) { return (uint32)r | ((uint32)g << 8) | ((uint32)b << 16) | ((uint32)a << 24); }
}
