#include "pch.h"
#include "ModelRaster.h"
#include <filesystem>

namespace Modeling
{
	namespace
	{
		constexpr float kPi = 3.14159265358979f;

		// ---- 색 (Blender 기본 테마와 비슷하게)
		const Vec3 kBase(0.78f, 0.78f, 0.78f);
		const Vec3 kSelFace(1.0f, 0.55f, 0.15f);
		const uint32 kWire = Rgba(18, 18, 18, 255);
		const uint32 kWireObject = Rgba(10, 10, 10, 90);
		const uint32 kWireSelected = Rgba(255, 140, 30, 255);
		const uint32 kWireActive = Rgba(255, 200, 90, 255);
		const uint32 kVert = Rgba(10, 10, 10, 255);
		const uint32 kVertSel = Rgba(255, 140, 30, 255);
		const uint32 kHover = Rgba(255, 255, 255, 255);

		float Saturate(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
		uint32 Pack(const Vec3& c, float a = 1.0f)
		{
			return Rgba((int)(Saturate(c.x) * 255.0f + 0.5f), (int)(Saturate(c.y) * 255.0f + 0.5f), (int)(Saturate(c.z) * 255.0f + 0.5f), (int)(Saturate(a) * 255.0f + 0.5f));
		}
		Vec3 Unpack(uint32 c) { return Vec3((c & 255) / 255.0f, ((c >> 8) & 255) / 255.0f, ((c >> 16) & 255) / 255.0f); }
	}

	// ------------------------------------------------------------------ 카메라
	Vec3 ViewCamera::Forward() const
	{
		const float p = std::clamp(Pitch, -kPi * 0.5f + 1e-4f, kPi * 0.5f - 1e-4f);
		// 눈 = 대상 + 방향 · 거리, 보는 방향 = -방향
		return -Vec3(cosf(p) * sinf(Yaw), sinf(p), cosf(p) * cosf(Yaw));
	}

	Vec3 ViewCamera::Eye() const
	{
		// 직교: 장면 앞쪽까지 다 보이게 눈을 멀리 (확대는 Distance 로)
		return Target - Forward() * (Ortho ? (std::max)(Distance * 20.0f, 200.0f) : Distance);
	}

	Matrix ViewCamera::View() const
	{
		return Matrix(XMMatrixLookAtLH(Eye(), Target, Vec3(0, 1, 0)));
	}

	Matrix ViewCamera::Proj(float aspect) const
	{
		const float fov = std::clamp(Fov, 5.0f, 120.0f) * kPi / 180.0f;
		if (Ortho)
		{
			const float h = 2.0f * Distance * tanf(fov * 0.5f);
			const float farZ = (std::max)(Distance * 40.0f, 400.0f) + Distance * 40.0f;
			return Matrix(XMMatrixOrthographicLH(h * aspect, h, 0.01f, farZ));
		}
		const float nearZ = (std::max)(0.001f, Distance * 0.005f);
		return Matrix(XMMatrixPerspectiveFovLH(fov, aspect, nearZ, Distance * 100.0f + 100.0f));
	}

	bool ViewCamera::SetPreset(const std::string& name)
	{
		// Blender 의 숫자 패드 시점 (캐릭터 앞 = +Z). 정면 · 옆 · 위는 직교
		struct P { const char* Name; float Yaw, Pitch; bool Ortho; };
		static const P presets[] = {
			{ "front", 0.0f, 0.0f, true }, { "back", kPi, 0.0f, true }, { "right", kPi * 0.5f, 0.0f, true }, { "left", -kPi * 0.5f, 0.0f, true },
			{ "top", 0.0f, kPi * 0.5f, true }, { "bottom", 0.0f, -kPi * 0.5f, true }, { "persp", 0.6f, 0.35f, false }, { "front-persp", 0.0f, 0.1f, false },
			{ "three-quarter", 0.65f, 0.15f, false },
		};
		for (const P& p : presets)
			if (name == p.Name)
			{
				Yaw = p.Yaw;
				Pitch = p.Pitch;
				Ortho = p.Ortho;
				return true;
			}
		return false;
	}

	void ViewCamera::Frame(const Vec3& mn, const Vec3& mx, float aspect)
	{
		Target = (mn + mx) * 0.5f;
		const float radius = (std::max)((mx - mn).Length() * 0.5f, 0.05f);
		const float fov = std::clamp(Fov, 5.0f, 120.0f) * kPi / 180.0f;
		const float fit = tanf(fov * 0.5f) * (std::min)(1.0f, aspect);
		Distance = radius / fit * 1.1f;
	}

	bool ViewBasis(const std::string& view, Vec3& right, Vec3& up, Vec3& fwd)
	{
		ViewCamera cam;
		if (!cam.SetPreset(view))
			return false;
		const Matrix v = cam.View();   // 보기 행렬의 열 = 카메라 축
		right = Vec3(v._11, v._21, v._31);
		up = Vec3(v._12, v._22, v._32);
		fwd = Vec3(v._13, v._23, v._33);
		return true;
	}

	// ------------------------------------------------------------------ 버퍼
	void Raster::Resize(int w, int h)
	{
		w = (std::max)(w, 8);
		h = (std::max)(h, 8);
		if (w == Width && h == Height)
			return;
		Width = w;
		Height = h;
		Color.assign((size_t)w * h, 0);
		Depth.assign((size_t)w * h, FLT_MAX);
		FaceId.assign((size_t)w * h, -1);
	}

	void Raster::Clear(bool background)
	{
		for (int y = 0; y < Height; ++y)
		{
			// 위가 조금 밝은 회색 (Blender 뷰포트)
			const float t = y / (float)(std::max)(1, Height - 1);
			const uint32 c = background ? Pack(Vec3(0.235f, 0.235f, 0.24f) * (1.0f - t) + Vec3(0.17f, 0.17f, 0.175f) * t) : Rgba(0, 0, 0, 0);
			std::fill(Color.begin() + (size_t)y * Width, Color.begin() + (size_t)(y + 1) * Width, c);
		}
		std::fill(Depth.begin(), Depth.end(), FLT_MAX);
		std::fill(FaceId.begin(), FaceId.end(), -1);
	}

	// 화면 좌표: x 오른쪽, y 아래 (픽셀), z = 선형 깊이 (카메라 앞 거리)
	bool Raster::Project(const Vec3& world, Vec3& screen) const
	{
		const Vec4 c = Vec4::Transform(Vec4(world.x, world.y, world.z, 1.0f), ViewProj);
		if (c.w <= 1e-6f)
			return false;
		const float ix = c.x / c.w, iy = c.y / c.w;
		screen.x = (ix * 0.5f + 0.5f) * Width;
		screen.y = (0.5f - iy * 0.5f) * Height;
		screen.z = m_Cam.Ortho ? Vec3::Transform(world, m_View).z : c.w;
		return c.z >= 0.0f || m_Cam.Ortho;
	}

	bool Raster::Visible(const Vec3& s, float bias) const
	{
		const int x = (int)s.x, y = (int)s.y;
		if (x < 0 || y < 0 || x >= Width || y >= Height)
			return false;
		// 둘레 픽셀 중 가장 먼 깊이와 비교 (변 · 점이 면 가장자리에 걸릴 때)
		float d = 0.0f;
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
			{
				const int xx = std::clamp(x + dx, 0, Width - 1), yy = std::clamp(y + dy, 0, Height - 1);
				d = (std::max)(d, Depth[(size_t)yy * Width + xx]);
			}
		return s.z <= d * (1.0f + bias) + 1e-4f;
	}

	void Raster::Ray(float x, float y, Vec3& origin, Vec3& dir) const
	{
		const float nx = x / Width * 2.0f - 1.0f, ny = 1.0f - y / Height * 2.0f;
		const Vec4 a = Vec4::Transform(Vec4(nx, ny, 0.0f, 1.0f), m_InvViewProj);
		const Vec4 b = Vec4::Transform(Vec4(nx, ny, 1.0f, 1.0f), m_InvViewProj);
		origin = Vec3(a.x, a.y, a.z) / a.w;
		dir = Vec3(b.x, b.y, b.z) / b.w - origin;
		dir.Normalize();
	}

	Vec3 Raster::Unproject(float x, float y, float ndcZ) const
	{
		const Vec4 p = Vec4::Transform(Vec4(x / Width * 2.0f - 1.0f, 1.0f - y / Height * 2.0f, ndcZ, 1.0f), m_InvViewProj);
		return Vec3(p.x, p.y, p.z) / p.w;
	}

	void Raster::DrawRefs(const Document& doc)
	{
		const Vec3 camF = m_Cam.Forward();
		for (const RefImage& ref : doc.Refs)
		{
			Vec3 R, U, F;
			if (!ref.Visible || ref.Pixels.empty() || !ViewBasis(ref.View, R, U, F) || camF.Dot(F) < 0.995f)
				continue;
			const float w = ref.Width(), h = ref.Height;
			// 화면 픽셀 → 광선은 픽셀 좌표에 대해 선형 (가까운 · 먼 평면 위 점)
			const Vec3 n00 = Unproject(0, 0, 0), n10 = Unproject((float)Width, 0, 0), n01 = Unproject(0, (float)Height, 0);
			const Vec3 f00 = Unproject(0, 0, 1), f10 = Unproject((float)Width, 0, 1), f01 = Unproject(0, (float)Height, 1);
			const Vec3 ndx = (n10 - n00) / (float)Width, ndy = (n01 - n00) / (float)Height;
			const Vec3 fdx = (f10 - f00) / (float)Width, fdy = (f01 - f00) / (float)Height;
			const float a = Saturate(ref.Opacity);
			for (int y = 0; y < Height; ++y)
				for (int x = 0; x < Width; ++x)
				{
					const float px = x + 0.5f, py = y + 0.5f;
					const Vec3 o = n00 + ndx * px + ndy * py;
					const Vec3 d = (f00 + fdx * px + fdy * py) - o;
					const float den = d.Dot(F);
					if (fabsf(den) < 1e-9f) continue;
					const Vec3 p = o + d * ((ref.Center - o).Dot(F) / den);
					const float u = (p - ref.Center).Dot(R) / w + 0.5f, v = 0.5f - (p - ref.Center).Dot(U) / h;
					if (u < 0.0f || v < 0.0f || u >= 1.0f || v >= 1.0f) continue;
					// 겹선형 표본
					const float fx = u * ref.W - 0.5f, fy = v * ref.H - 0.5f;
					const int x0 = std::clamp((int)floorf(fx), 0, ref.W - 1), y0 = std::clamp((int)floorf(fy), 0, ref.H - 1);
					const int x1 = (std::min)(x0 + 1, ref.W - 1), y1 = (std::min)(y0 + 1, ref.H - 1);
					const float tx = std::clamp(fx - x0, 0.0f, 1.0f), ty = std::clamp(fy - y0, 0.0f, 1.0f);
					Vec4 c(0, 0, 0, 0);
					const uint32 s[4] = { ref.Pixels[(size_t)y0 * ref.W + x0], ref.Pixels[(size_t)y0 * ref.W + x1], ref.Pixels[(size_t)y1 * ref.W + x0], ref.Pixels[(size_t)y1 * ref.W + x1] };
					const float k[4] = { (1 - tx) * (1 - ty), tx * (1 - ty), (1 - tx) * ty, tx * ty };
					for (int i = 0; i < 4; ++i)
						c += Vec4((s[i] & 255) / 255.0f, ((s[i] >> 8) & 255) / 255.0f, ((s[i] >> 16) & 255) / 255.0f, (s[i] >> 24) / 255.0f) * k[i];
					Blend(x, y, Pack(Vec3(c.x, c.y, c.z), c.w * a), 1.0f);
				}
		}
	}

	void Raster::Blend(int x, int y, uint32 color, float coverage)
	{
		const float a = ((color >> 24) / 255.0f) * coverage;
		if (a <= 0.004f)
			return;
		uint32& dst = Color[(size_t)y * Width + x];
		const Vec3 d = Unpack(dst), s = Unpack(color);
		dst = Pack(d + (s - d) * a, 1.0f);
	}

	// ------------------------------------------------------------------ 삼각형
	void Raster::Triangle(const Vec3 s[3], const Vec3 n[3], const Vec2* uv, const Vec3 baseColor, float alpha, int id, const Vec3& lightDir, Shading shade, const Vec3* vcol)
	{
		const float area = (s[1].x - s[0].x) * (s[2].y - s[0].y) - (s[1].y - s[0].y) * (s[2].x - s[0].x);
		if (fabsf(area) < 1e-8f)
			return;
		const int x0 = (std::max)(0, (int)floorf((std::min)({ s[0].x, s[1].x, s[2].x })));
		const int x1 = (std::min)(Width - 1, (int)ceilf((std::max)({ s[0].x, s[1].x, s[2].x })));
		const int y0 = (std::max)(0, (int)floorf((std::min)({ s[0].y, s[1].y, s[2].y })));
		const int y1 = (std::min)(Height - 1, (int)ceilf((std::max)({ s[0].y, s[1].y, s[2].y })));
		if (x0 > x1 || y0 > y1)
			return;
		const bool persp = !m_Cam.Ortho;
		const float inv[3] = { 1.0f / (std::max)(s[0].z, 1e-6f), 1.0f / (std::max)(s[1].z, 1e-6f), 1.0f / (std::max)(s[2].z, 1e-6f) };
		const float invArea = 1.0f / area;
		for (int y = y0; y <= y1; ++y)
		{
			const float py = y + 0.5f;
			for (int x = x0; x <= x1; ++x)
			{
				const float px = x + 0.5f;
				float w0 = ((s[1].x - px) * (s[2].y - py) - (s[1].y - py) * (s[2].x - px)) * invArea;
				float w1 = ((s[2].x - px) * (s[0].y - py) - (s[2].y - py) * (s[0].x - px)) * invArea;
				float w2 = 1.0f - w0 - w1;
				if (w0 < -1e-5f || w1 < -1e-5f || w2 < -1e-5f)
					continue;
				float depth;
				Vec3 nn;
				float k0 = w0, k1 = w1, k2 = w2;   // 원근 보정한 무게
				if (persp)
				{
					const float a = w0 * inv[0], b = w1 * inv[1], c = w2 * inv[2];
					const float sum = a + b + c;
					depth = 1.0f / sum;
					k0 = a / sum; k1 = b / sum; k2 = c / sum;
				}
				else
					depth = w0 * s[0].z + w1 * s[1].z + w2 * s[2].z;
				nn = n[0] * k0 + n[1] * k1 + n[2] * k2;
				const size_t i = (size_t)y * Width + x;
				if (depth >= Depth[i])
					continue;
				nn.Normalize();
				Vec3 col;
				Vec3 surface = vcol ? vcol[0] * k0 + vcol[1] * k1 + vcol[2] * k2 : baseColor;
				if (shade == Shading::UVChecker)
				{
					// UV 확인용 바둑판 (8 x 8), UV 가 없으면 분홍
					if (uv)
					{
						const Vec2 t = uv[0] * k0 + uv[1] * k1 + uv[2] * k2;
						const int cx = (int)floorf(t.x * 8.0f), cy = (int)floorf(t.y * 8.0f);
						const bool odd = ((cx + cy) & 1) != 0;
						surface = odd ? Vec3(0.35f, 0.35f, 0.4f) : Vec3(0.92f, 0.92f, 0.95f);
						if (t.x < 0.0f || t.y < 0.0f || t.x > 1.0f || t.y > 1.0f) surface = surface * Vec3(1.0f, 0.55f, 0.55f);   // 0..1 밖 = 붉게
					}
					else
						surface = Vec3(0.95f, 0.35f, 0.75f);
				}
				if (shade == Shading::Normals)
					col = nn * 0.5f + Vec3(0.5f, 0.5f, 0.5f);
				else
				{
					// 보기 공간 법선 (z = 화면 안쪽). 뒷면은 뒤집어 비춘다
					if (nn.z > 0.0f) nn = -nn;
					const float key = (std::max)(0.0f, nn.Dot(lightDir));
					const float fill = (std::max)(0.0f, nn.Dot(Vec3(0.6f, -0.2f, -0.75f))) * 0.25f;
					const float rim = powf(1.0f - (std::max)(0.0f, -nn.z), 3.0f) * 0.18f;
					if (shade == Shading::Toon)
					{
						// 애니메이션풍 2 톤: 밝은 면 = 색 그대로, 그림자 = 푸르스름하게 어둡게, 가장자리 살짝 밝게
						const bool lit = key > 0.3f;
						col = lit ? surface * (0.98f + rim * 0.4f) : surface * Vec3(0.68f, 0.64f, 0.8f);
					}
					else
						col = surface * (0.36f + key * 0.62f + fill + rim);
				}
				Depth[i] = depth;
				FaceId[i] = id;
				if (alpha >= 0.999f)
					Color[i] = Pack(col);
				else
					Blend(x, y, Pack(col, 1.0f), alpha);
			}
		}
	}

	// ------------------------------------------------------------------ 선 · 점 (부드러운 가장자리, 깊이 비교)
	void Raster::Line(Vec3 a, Vec3 b, uint32 color, float width, bool depthTest)
	{
		const float dx = b.x - a.x, dy = b.y - a.y;
		const float len = sqrtf(dx * dx + dy * dy);
		if (len < 1e-4f)
			return;
		// 화면 밖이면 건너뛴다 (아주 긴 선)
		if ((std::max)(a.x, b.x) < -width || (std::min)(a.x, b.x) > Width + width || (std::max)(a.y, b.y) < -width || (std::min)(a.y, b.y) > Height + width)
			return;
		const float half = width * 0.5f;
		const bool steep = fabsf(dy) > fabsf(dx);
		// 큰 축을 따라 한 칸씩, 작은 축으로 두께만큼 픽셀을 칠한다 (선분까지 거리 → 덮임)
		const float major0 = steep ? (std::min)(a.y, b.y) : (std::min)(a.x, b.x);
		const float major1 = steep ? (std::max)(a.y, b.y) : (std::max)(a.x, b.x);
		const int m0 = (std::max)(0, (int)floorf(major0 - half)), m1 = (std::min)((steep ? Height : Width) - 1, (int)ceilf(major1 + half));
		const int span = (int)ceilf(half + 1.5f);
		for (int m = m0; m <= m1; ++m)
		{
			const float pm = m + 0.5f;
			// 선 위에서 이 칸의 작은 축 좌표
			const float t = steep ? (pm - a.y) / dy : (pm - a.x) / dx;
			const float tc = std::clamp(t, 0.0f, 1.0f);
			const float minorC = steep ? a.x + dx * tc : a.y + dy * tc;
			for (int k = (int)floorf(minorC) - span; k <= (int)floorf(minorC) + span; ++k)
			{
				const int x = steep ? k : m, y = steep ? m : k;
				if (x < 0 || y < 0 || x >= Width || y >= Height)
					continue;
				const float px = x + 0.5f, py = y + 0.5f;
				float u = ((px - a.x) * dx + (py - a.y) * dy) / (len * len);
				u = std::clamp(u, 0.0f, 1.0f);
				const float cx = a.x + dx * u - px, cy = a.y + dy * u - py;
				const float dist = sqrtf(cx * cx + cy * cy);
				const float cov = Saturate(half + 0.5f - dist);
				if (cov <= 0.0f)
					continue;
				if (depthTest)
				{
					const float z = m_Cam.Ortho ? a.z + (b.z - a.z) * u : 1.0f / (1.0f / a.z + (1.0f / b.z - 1.0f / a.z) * u);
					const float d = Depth[(size_t)y * Width + x];
					if (z > d * 1.002f + 1e-4f)
						continue;
				}
				Blend(x, y, color, cov);
			}
		}
	}

	void Raster::Dot(const Vec3& s, float size, uint32 color, bool depthTest)
	{
		if (depthTest && !Visible(s))
			return;
		const float r = size * 0.5f;
		for (int y = (int)floorf(s.y - r - 1); y <= (int)ceilf(s.y + r + 1); ++y)
			for (int x = (int)floorf(s.x - r - 1); x <= (int)ceilf(s.x + r + 1); ++x)
			{
				if (x < 0 || y < 0 || x >= Width || y >= Height)
					continue;
				// 둥근 사각형 (Blender 점)
				const float ex = fabsf(x + 0.5f - s.x) - r, ey = fabsf(y + 0.5f - s.y) - r;
				const float dist = (std::max)(ex, ey);
				Blend(x, y, color, Saturate(0.5f - dist));
			}
	}

	void Raster::DrawOutline(float width)
	{
		// 앞에 있는 픽셀만 칠한다: 둘레 (반경 width) 에 빈 곳 · 다른 오브젝트 · 훨씬 먼 면이 있으면 외곽선
		const int r = (std::max)(1, (int)roundf(width));
		std::vector<uint8_t> edge((size_t)Width * Height, 0);
		static const int dirs[8][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }, { 1, 1 }, { -1, -1 }, { 1, -1 }, { -1, 1 } };
		for (int y = 0; y < Height; ++y)
			for (int x = 0; x < Width; ++x)
			{
				const size_t i = (size_t)y * Width + x;
				const int id = FaceId[i];
				if (id < 0) continue;
				const float d = Depth[i];
				for (const auto& dv : dirs)
				{
					const int xx = x + dv[0] * r, yy = y + dv[1] * r;
					if (xx < 0 || yy < 0 || xx >= Width || yy >= Height) { edge[i] = 1; break; }
					const size_t j = (size_t)yy * Width + xx;
					if (FaceId[j] < 0 || (FaceId[j] >> 20) != (id >> 20) || Depth[j] > d * 1.04f + 0.02f) { edge[i] = 1; break; }
				}
			}
		const uint32 ink = Rgba(30, 24, 32);
		for (size_t i = 0; i < edge.size(); ++i)
			if (edge[i]) Color[i] = ink;
	}

	void Raster::DrawGrid()
	{
		// 바닥 (XZ) 격자: 거리에 따라 1 m / 10 m, 축 = X 빨강 · Z 파랑
		const float step = m_Cam.Distance > 40.0f ? 10.0f : (m_Cam.Distance > 4.0f ? 1.0f : 0.1f);
		const int count = 40;
		const Vec3 c(roundf(m_Cam.Target.x / step) * step, 0.0f, roundf(m_Cam.Target.z / step) * step);
		const float extent = step * count;
		auto line = [&](const Vec3& p, const Vec3& q, uint32 col, float w) {
			// 가까운 면에서 잘라 카메라 뒤를 막는다 (선분을 잘게 나눠 앞에 있는 조각만)
			const int seg = 16;
			for (int i = 0; i < seg; ++i)
			{
				const Vec3 a = p + (q - p) * (i / (float)seg), b = p + (q - p) * ((i + 1) / (float)seg);
				Vec3 sa, sb;
				if (Project(a, sa) && Project(b, sb))
					Line(sa, sb, col, w, true);
			}
		};
		// 정면 · 옆 직교에서는 바닥이 선 하나라 그리지 않는다 (축만)
		const bool flat = m_Cam.Ortho && fabsf(m_Cam.Forward().y) < 0.05f;
		if (!flat)
			for (int i = -count; i <= count; ++i)
			{
				const float o = i * step;
				const bool major = (int)roundf((c.x + o) / step) % 10 == 0;
				const uint32 col = Rgba(110, 110, 110, major ? 110 : 55);
				if (fabsf(c.x + o) > 1e-5f) line(Vec3(c.x + o, 0, c.z - extent), Vec3(c.x + o, 0, c.z + extent), col, 1.0f);
				if (fabsf(c.z + o) > 1e-5f) line(Vec3(c.x - extent, 0, c.z + o), Vec3(c.x + extent, 0, c.z + o), col, 1.0f);
			}
		line(Vec3(c.x - extent, 0, 0), Vec3(c.x + extent, 0, 0), Rgba(200, 70, 80, 200), 1.5f);
		line(Vec3(0, 0, c.z - extent), Vec3(0, 0, c.z + extent), Rgba(70, 120, 220, 200), 1.5f);
	}

	// ------------------------------------------------------------------ 장면
	void Raster::Render(Document& doc, const ViewCamera& cam, const RasterOptions& opt)
	{
		m_Cam = cam;
		const float aspect = Width / (float)Height;
		const Matrix view = cam.View();
		m_View = view;
		ViewProj = view * cam.Proj(aspect);
		m_InvViewProj = ViewProj.Invert();
		Clear(opt.Background);
		if (opt.Refs)
			DrawRefs(doc);
		// 빛: 카메라 왼쪽 위에서 (보기 공간, 표면 → 빛)
		Vec3 lightDir(-0.45f, 0.55f, -0.7f);
		lightDir.Normalize();
		std::vector<std::array<int, 3>> tris;
		for (int oi = 0; oi < (int)doc.Objects.size(); ++oi)
		{
			Object& o = doc.Objects[oi];
			if (!o.Visible)
				continue;
			const Mesh& m = doc.Displayed(oi);   // 모디파이어 결과 + 포즈 (없으면 원래 메시)
			// 가중치 보기: 고른 본의 그룹 가중치 → 색 (0 파랑, 0.5 초록, 1 빨강)
			std::vector<Vec3> wcol;
			if (opt.Shade == Shading::Weights)
			{
				const int g = m.FindGroup(opt.WeightBone);
				wcol.resize(m.Verts.size());
				for (size_t v = 0; v < m.Verts.size(); ++v)
				{
					const float t = g >= 0 ? m.Weight((int)v, g) : 0.0f;
					wcol[v] = t < 0.5f ? Vec3::Lerp(Vec3(0.1f, 0.2f, 0.85f), Vec3(0.15f, 0.85f, 0.25f), t * 2.0f) : Vec3::Lerp(Vec3(0.15f, 0.85f, 0.25f), Vec3(0.95f, 0.15f, 0.1f), t * 2.0f - 1.0f);
				}
			}
			const Matrix world = o.World();
			Matrix nrmW = world;
			nrmW.Translation(Vec3(0, 0, 0));
			const Matrix nrmV = nrmW * view;
			const bool editing = doc.EditMode && oi == doc.Active && opt.Selection;   // 깨끗한 렌더 (--selection false) 면 편집 표시 없이
			// 점마다 화면 좌표 · 부드러운 법선
			std::vector<Vec3> screen(m.Verts.size());
			std::vector<uint8_t> ok(m.Verts.size());
			for (size_t v = 0; v < m.Verts.size(); ++v)
				ok[v] = Project(Vec3::Transform(m.Verts[v].P, world), screen[v]) ? 1 : 0;
			std::vector<Vec3> faceN(m.Faces.size()), smooth(m.Verts.size(), Vec3(0, 0, 0));
			for (int f = 0; f < (int)m.Faces.size(); ++f)
			{
				faceN[f] = m.FaceNormal(f);
				if (m.Faces[f].Smooth)
					for (int v : m.Faces[f].V) smooth[v] += faceN[f];
			}
			for (int f = 0; f < (int)m.Faces.size(); ++f)
			{
				const Face& face = m.Faces[f];
				bool all = true;
				for (int v : face.V) all = all && ok[v];
				if (!all)
					continue;   // 카메라 뒤에 걸친 면은 건너뛴다 (편집기 카메라는 보통 밖에 있다)
				Triangulate(m, f, tris);
				const int origin = face.Origin >= 0 ? face.Origin : f;
				Vec3 base = doc.MaterialColor(face.Material);
				if (editing && face.Sel) base = base * 0.6f + kSelFace * 0.4f;
				if (editing && doc.Mode == SelectMode::Face && opt.HoverObject == oi && opt.HoverElement == origin) base = base * 0.8f + Vec3(1, 1, 1) * 0.2f;
				const int id = (oi << 20) | origin;
				const bool hasUV = face.UV.size() == face.V.size();
				for (const auto& t : tris)
				{
					Vec3 s[3], n[3];
					Vec2 uv[3];
					if (hasUV) for (int k = 0; k < 3; ++k) uv[k] = face.UV[t[k]];
					for (int k = 0; k < 3; ++k)
					{
						const int v = face.V[t[k]];
						s[k] = screen[v];
						Vec3 nn = face.Smooth && smooth[v].LengthSquared() > 1e-12f ? smooth[v] : faceN[f];
						n[k] = opt.Shade == Shading::Normals ? Vec3::TransformNormal(nn, nrmW) : Vec3::TransformNormal(nn, nrmV);
						n[k].Normalize();
					}
					Vec3 vc[3];
					if (!wcol.empty()) for (int k = 0; k < 3; ++k) vc[k] = wcol[face.V[t[k]]];
					Triangle(s, n, hasUV ? uv : nullptr, base, opt.XRay ? 0.45f : 1.0f, id, lightDir, opt.Shade == Shading::Weights ? Shading::Solid : opt.Shade, wcol.empty() ? nullptr : vc);
				}
			}
		}
		if (opt.Outline)
			DrawOutline(opt.OutlineWidth);
		if (opt.Grid)
			DrawGrid();
		// 선 · 점
		for (int oi = 0; oi < (int)doc.Objects.size(); ++oi)
		{
			Object& o = doc.Objects[oi];
			if (!o.Visible)
				continue;
			Mesh& m = o.M;
			const Matrix world = o.World();
			const bool editing = doc.EditMode && oi == doc.Active && opt.Selection;   // 깨끗한 렌더 (--selection false) 면 편집 표시 없이
			if (!editing && !opt.Wireframe && !(o.Selected && opt.Selection))
				continue;
			std::vector<Vec3> screen(m.Verts.size());
			std::vector<uint8_t> ok(m.Verts.size());
			for (size_t v = 0; v < m.Verts.size(); ++v)
				ok[v] = Project(Vec3::Transform(m.Verts[v].P, world), screen[v]) ? 1 : 0;
			const bool depthTest = !opt.XRay;
			const auto& edges = m.Edges();
			if (!editing)
			{
				const bool sel = o.Selected && opt.Selection;
				const uint32 col = sel ? (oi == doc.Active ? kWireActive : kWireSelected) : kWireObject;
				const float w = sel ? 1.2f : 1.0f;
				for (const Edge& e : edges)
					if (ok[e.A] && ok[e.B])
						Line(screen[e.A], screen[e.B], sel ? (col & 0x00FFFFFF) | (150u << 24) : col, w, depthTest);
				continue;
			}
			for (int ei = 0; ei < (int)edges.size(); ++ei)
			{
				const Edge& e = edges[ei];
				if (!ok[e.A] || !ok[e.B])
					continue;
				const bool sel = m.SelEdges.count(EdgeKey(e.A, e.B)) > 0;
				const bool hover = doc.Mode == SelectMode::Edge && opt.HoverObject == oi && opt.HoverElement == ei;
				Line(screen[e.A], screen[e.B], hover ? kHover : (sel ? kWireSelected : kWire), sel || hover ? 1.6f : 1.0f, depthTest);
			}
			if (doc.Mode == SelectMode::Vertex)
				for (int v = 0; v < (int)m.Verts.size(); ++v)
				{
					if (!ok[v]) continue;
					const bool hover = opt.HoverObject == oi && opt.HoverElement == v;
					Dot(screen[v], hover ? 6.0f : (m.Verts[v].Sel ? 5.0f : 3.5f), hover ? kHover : (m.Verts[v].Sel ? kVertSel : kVert), depthTest);
				}
			else if (doc.Mode == SelectMode::Face)
				for (int f = 0; f < (int)m.Faces.size(); ++f)
				{
					Vec3 c;
					if (Project(Vec3::Transform(m.FaceCenter(f), world), c))
						Dot(c, m.Faces[f].Sel ? 4.5f : 3.0f, m.Faces[f].Sel ? kVertSel : kVert, depthTest);
				}
		}
		if (opt.Bones && !doc.Rig.Empty())
			DrawBones(doc, opt.SelectedBone);
		for (const auto& [a, b] : opt.ExtraLines)
		{
			Vec3 sa, sb;
			if (Project(a, sa) && Project(b, sb))
				Line(sa, sb, opt.ExtraColor, 1.5f, false);
		}
	}

	// 본: 머리 → 꼬리 팔면체 (Blender Octahedral 비슷하게) — 사람 본 = 노랑, 흔들림 = 하늘색, 고른 본 = 주황. 메시 앞에 그린다
	void Raster::DrawBones(const Document& doc, int selected)
	{
		const Armature& arm = doc.Rig;
		const std::vector<Matrix> skin = arm.SkinMatrices();
		for (int i = 0; i < (int)arm.Bones.size(); ++i)
		{
			const Bone& b = arm.Bones[i];
			Vec3 h, t;
			arm.PosedSegment(i, skin, h, t);
			Vec3 dir = t - h;
			const float len = dir.Length();
			if (len < 1e-5f) continue;
			dir /= len;
			Vec3 side = fabsf(dir.y) < 0.9f ? dir.Cross(Vec3(0, 1, 0)) : dir.Cross(Vec3(1, 0, 0));
			side.Normalize();
			const Vec3 up = side.Cross(dir);
			const float r = len * 0.1f;
			const Vec3 mid = h + dir * (len * 0.2f);
			const Vec3 ring[4] = { mid + side * r, mid + up * r, mid - side * r, mid - up * r };
			const uint32 col = i == selected ? Rgba(255, 150, 40) : (b.Spring ? Rgba(90, 200, 255) : (b.Human.empty() ? Rgba(200, 200, 200) : Rgba(255, 220, 60)));
			Vec3 sh, st, sr[4];
			if (!Project(h, sh) || !Project(t, st)) continue;
			bool ok = true;
			for (int k = 0; k < 4; ++k) ok = ok && Project(ring[k], sr[k]);
			if (!ok) continue;
			const float w = i == selected ? 2.0f : 1.3f;
			for (int k = 0; k < 4; ++k)
			{
				Line(sh, sr[k], col, w, false);
				Line(sr[k], st, col, w, false);
				Line(sr[k], sr[(k + 1) % 4], col, w, false);
			}
			Dot(sh, 4.0f, col, false);
		}
	}

	bool Raster::SavePng(const std::string& path, std::string& error) const
	{
		DirectX::Image img = {};
		img.width = Width;
		img.height = Height;
		img.format = DXGI_FORMAT_R8G8B8A8_UNORM;
		img.rowPitch = (size_t)Width * 4;
		img.slicePitch = img.rowPitch * Height;
		std::vector<uint32> opaque(Color);
		for (uint32& c : opaque) c |= 0xFF000000u;
		img.pixels = (uint8_t*)opaque.data();
		const std::filesystem::path p = PathU8(path);
		std::error_code ec;
		if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
		const HRESULT hr = DirectX::SaveToWICFile(img, DirectX::WIC_FLAGS_FORCE_SRGB, DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), p.wstring().c_str());
		if (FAILED(hr))
		{
			char buf[64];
			snprintf(buf, sizeof(buf), "PNG save failed (0x%08X): ", (unsigned)hr);
			error = buf + path;
			return false;
		}
		return true;
	}
}
