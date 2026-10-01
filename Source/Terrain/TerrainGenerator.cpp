#include "pch.h"
#include "TerrainGenerator.h"
#include "TerrainData.h"
#include "Terrain.h"
#include "TerrainStamp.h"
#include "Transform.h"
#include "FrameProfiler.h"
#include <algorithm>
#include <chrono>
#include <execution>
#include <future>
#include <map>
#include <mutex>
#include <numeric>
#include <random>
#include <unordered_map>

using nlohmann::json;

// ================================================================ 설정 (저장)
namespace
{
	const TerrainGenFilter::ParamInfo kHydraulic[6] = {
		{ "Droplets (k)", 5, 400, 90, true }, { "Erosion", 0.05f, 1, 0.35f, false }, { "Deposition", 0.05f, 1, 0.3f, false },
		{ "Evaporation", 0.001f, 0.1f, 0.012f, false }, { "Inertia", 0, 0.5f, 0.06f, false }, { "Radius", 1, 6, 3, true } };
	const TerrainGenFilter::ParamInfo kThermal[6] = {
		{ "Talus Angle", 15, 70, 38, false }, { "Iterations", 1, 200, 40, true }, { "Amount", 0.05f, 1, 0.5f, false },
		{ nullptr, 0, 0, 0, false }, { nullptr, 0, 0, 0, false }, { nullptr, 0, 0, 0, false } };
	const TerrainGenFilter::ParamInfo kTerrace[6] = {
		{ "Steps", 2, 40, 10, true }, { "Sharpness", 0, 1, 0.6f, false }, { "Min Height", 0, 2000, 30, false },
		{ "Max Height", 0, 2000, 400, false }, { nullptr, 0, 0, 0, false }, { nullptr, 0, 0, 0, false } };
	const TerrainGenFilter::ParamInfo kSmooth[6] = {
		{ "Radius", 1, 12, 2, true }, { "Iterations", 1, 10, 1, true }, { nullptr, 0, 0, 0, false },
		{ nullptr, 0, 0, 0, false }, { nullptr, 0, 0, 0, false }, { nullptr, 0, 0, 0, false } };
	const TerrainGenFilter::ParamInfo kCurve[6] = {
		{ "Gamma", 0.2f, 4, 1.3f, false }, { "Gain", 0.1f, 3, 1, false }, { "Offset (m)", -500, 500, 0, false },
		{ nullptr, 0, 0, 0, false }, { nullptr, 0, 0, 0, false }, { nullptr, 0, 0, 0, false } };
	const TerrainGenFilter::ParamInfo kDetail[6] = {
		{ "Amount (m)", 0, 60, 6, false }, { "Scale (m)", 4, 400, 40, false }, { "Ridged", 0, 1, 0.5f, false },
		{ "Seed", 1, 9999, 7, true }, { nullptr, 0, 0, 0, false }, { nullptr, 0, 0, 0, false } };
}

const char* TerrainGenFilter::Name(Type t)
{
	switch (t)
	{
	case Type::HydraulicErosion: return "Hydraulic Erosion";
	case Type::ThermalErosion: return "Thermal Erosion";
	case Type::Terrace: return "Terrace";
	case Type::Smooth: return "Smooth";
	case Type::HeightCurve: return "Height Curve";
	case Type::DetailNoise: return "Detail Noise";
	default: return "?";
	}
}

const TerrainGenFilter::ParamInfo* TerrainGenFilter::Params(Type t)
{
	switch (t)
	{
	case Type::HydraulicErosion: return kHydraulic;
	case Type::ThermalErosion: return kThermal;
	case Type::Terrace: return kTerrace;
	case Type::Smooth: return kSmooth;
	case Type::HeightCurve: return kCurve;
	default: return kDetail;
	}
}

TerrainGenFilter TerrainGenFilter::Make(Type t)
{
	TerrainGenFilter f;
	f.FilterType = t;
	const ParamInfo* p = Params(t);
	for (int i = 0; i < 6; ++i)
		f.P[i] = p[i].Default;
	return f;
}

json TerrainGenSettings::ToJson() const
{
	json j;
	j["enabled"] = Enabled;
	j["autoUpdate"] = AutoUpdate;
	json b;
	b["type"] = (int)Base.NoiseType; b["seed"] = Base.Seed; b["scale"] = Base.Scale;
	b["minHeight"] = Base.MinHeight; b["maxHeight"] = Base.MaxHeight; b["shapePower"] = Base.ShapePower;
	b["offsetX"] = Base.OffsetX; b["offsetZ"] = Base.OffsetZ;
	b["octaves"] = std::vector<float>(Base.Octaves, Base.Octaves + TerrainGenBase::kOctaves);
	j["base"] = b;
	json filters = json::array();
	for (const auto& f : Filters)
		filters.push_back({ { "type", (int)f.FilterType }, { "enabled", f.Enabled }, { "strength", f.Strength }, { "p", std::vector<float>(f.P, f.P + 6) } });
	j["filters"] = filters;
	j["paintMaterials"] = PaintMaterials;
	json mats = json::array();
	for (const auto& m : Materials)
		mats.push_back({ { "enabled", m.Enabled }, { "layer", m.Layer }, { "hMin", m.HeightMin }, { "hMax", m.HeightMax }, { "hBlend", m.HeightBlend },
			{ "sMin", m.SlopeMin }, { "sMax", m.SlopeMax }, { "sBlend", m.SlopeBlend }, { "sediment", m.Sediment }, { "noise", m.Noise }, { "opacity", m.Opacity } });
	j["materials"] = mats;
	return j;
}

void TerrainGenSettings::FromJson(const json& j)
{
	if (!j.is_object())
		return;
	Enabled = j.value("enabled", Enabled);
	AutoUpdate = j.value("autoUpdate", AutoUpdate);
	if (j.contains("base"))
	{
		const json& b = j["base"];
		Base.NoiseType = (TerrainGenBase::Type)std::clamp(b.value("type", (int)Base.NoiseType), 0, 5);
		Base.Seed = b.value("seed", Base.Seed);
		Base.Scale = b.value("scale", Base.Scale);
		Base.MinHeight = b.value("minHeight", Base.MinHeight);
		Base.MaxHeight = b.value("maxHeight", Base.MaxHeight);
		Base.ShapePower = b.value("shapePower", Base.ShapePower);
		Base.OffsetX = b.value("offsetX", Base.OffsetX);
		Base.OffsetZ = b.value("offsetZ", Base.OffsetZ);
		if (b.contains("octaves") && b["octaves"].is_array())
			for (int i = 0; i < TerrainGenBase::kOctaves && i < (int)b["octaves"].size(); ++i)
				Base.Octaves[i] = b["octaves"][i].get<float>();
	}
	Filters.clear();
	if (j.contains("filters"))
		for (const auto& f : j["filters"])
		{
			TerrainGenFilter x = TerrainGenFilter::Make((TerrainGenFilter::Type)std::clamp(f.value("type", 0), 0, (int)TerrainGenFilter::Type::Count - 1));
			x.Enabled = f.value("enabled", true);
			x.Strength = f.value("strength", 1.0f);
			if (f.contains("p") && f["p"].is_array())
				for (int i = 0; i < 6 && i < (int)f["p"].size(); ++i)
					x.P[i] = f["p"][i].get<float>();
			Filters.push_back(x);
		}
	PaintMaterials = j.value("paintMaterials", PaintMaterials);
	Materials.clear();
	if (j.contains("materials"))
		for (const auto& m : j["materials"])
		{
			TerrainGenMaterialRule r;
			r.Enabled = m.value("enabled", true); r.Layer = m.value("layer", 1);
			r.HeightMin = m.value("hMin", r.HeightMin); r.HeightMax = m.value("hMax", r.HeightMax); r.HeightBlend = m.value("hBlend", r.HeightBlend);
			r.SlopeMin = m.value("sMin", r.SlopeMin); r.SlopeMax = m.value("sMax", r.SlopeMax); r.SlopeBlend = m.value("sBlend", r.SlopeBlend);
			r.Sediment = m.value("sediment", r.Sediment); r.Noise = m.value("noise", r.Noise); r.Opacity = m.value("opacity", r.Opacity);
			Materials.push_back(r);
		}
}

TerrainGenSettings TerrainGenSettings::MakeDefault()
{
	TerrainGenSettings s;
	s.Filters.push_back(TerrainGenFilter::Make(TerrainGenFilter::Type::HydraulicErosion));
	s.Filters.push_back(TerrainGenFilter::Make(TerrainGenFilter::Type::ThermalErosion));
	TerrainGenMaterialRule rock;   // 가파른 곳 = 바위 (레이어 1)
	rock.Layer = 1; rock.SlopeMin = 32.0f; rock.SlopeBlend = 6.0f;
	TerrainGenMaterialRule sediment;   // 침식 퇴적 = 흙/모래 (레이어 2)
	sediment.Layer = 2; sediment.SlopeMax = 28.0f; sediment.Sediment = 1.0f; sediment.Opacity = 0.85f;
	TerrainGenMaterialRule snow;   // 높은 곳 = 눈 (레이어 3)
	snow.Layer = 3; snow.HeightMin = 180.0f; snow.HeightBlend = 25.0f; snow.SlopeMax = 45.0f;
	s.Materials = { rock, sediment, snow };
	return s;
}

// ================================================================ 생성
namespace
{
	float Saturate(float x) { return std::clamp(x, 0.0f, 1.0f); }
	float Lerp(float a, float b, float t) { return a + (b - a) * t; }
	float Smoothstep(float e0, float e1, float x) { if (e1 == e0) return x < e0 ? 0.0f : 1.0f; const float t = Saturate((x - e0) / (e1 - e0)); return t * t * (3.0f - 2.0f * t); }

	// 개선된 Perlin 노이즈 (시드마다 순열). 결과 약 -1~1
	struct Perlin
	{
		uint8_t P[512];
		explicit Perlin(int seed)
		{
			uint8_t base[256];
			std::iota(base, base + 256, 0);
			std::mt19937 rng((uint32_t)seed * 2654435761u + 12345u);
			std::shuffle(base, base + 256, rng);
			for (int i = 0; i < 512; ++i)
				P[i] = base[i & 255];
		}
		static float Fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
		static float Grad(int h, float x, float y)
		{
			switch (h & 7)
			{
			case 0: return x + y; case 1: return -x + y; case 2: return x - y; case 3: return -x - y;
			case 4: return x; case 5: return -x; case 6: return y; default: return -y;
			}
		}
		float Noise(float x, float y) const
		{
			const float fx = floorf(x), fy = floorf(y);
			const int X = (int)fx & 255, Y = (int)fy & 255;
			x -= fx; y -= fy;
			const float u = Fade(x), v = Fade(y);
			const int a = P[X] + Y, b = P[X + 1] + Y;
			const float n = Lerp(Lerp(Grad(P[a], x, y), Grad(P[b], x - 1, y), u), Lerp(Grad(P[a + 1], x, y - 1), Grad(P[b + 1], x - 1, y - 1), u), v);
			return n * 0.9f;
		}
		float Fbm(float x, float y, int octaves, float ridged = 0.0f) const
		{
			float sum = 0.0f, amp = 0.5f, norm = 0.0f;
			for (int i = 0; i < octaves; ++i)
			{
				float n = Noise(x, y);
				n = Lerp(n, (1.0f - fabsf(n)) * 2.0f - 1.0f, ridged);
				sum += n * amp;
				norm += amp;
				amp *= 0.5f;
				x = x * 2.03f + 17.1f;
				y = y * 2.03f - 9.7f;
			}
			return sum / norm;
		}
	};

	template <class F>
	void ParallelRows(int rows, F&& f)
	{
		std::vector<int> r(rows);
		std::iota(r.begin(), r.end(), 0);
		std::for_each(std::execution::par, r.begin(), r.end(), f);
	}

	// ---------------------------------------------------------------- Base
	void BuildBase(const TerrainGenerator::Input& in, std::vector<float>& H)
	{
		const TerrainGenBase& b = in.Settings.Base;
		const int res = in.Resolution;
		const float cx = in.SizeX / (res - 1), cz = in.SizeZ / (res - 1);
		if (b.NoiseType == TerrainGenBase::Type::CurrentTerrain && in.Snapshot.size() == (size_t)res * res)
		{
			for (size_t i = 0; i < H.size(); ++i)
				H[i] = in.Snapshot[i] * in.SizeY;
			return;
		}
		if (b.NoiseType == TerrainGenBase::Type::Flat || b.NoiseType == TerrainGenBase::Type::CurrentTerrain)
		{
			std::fill(H.begin(), H.end(), b.MinHeight);
			return;
		}
		std::vector<Perlin> octave;
		for (int o = 0; o < TerrainGenBase::kOctaves; ++o)
			octave.emplace_back(b.Seed * 131 + o * 7919);
		float wsum = 0.0f;
		for (float w : b.Octaves)
			wsum += (std::max)(0.0f, w);
		wsum = (std::max)(wsum, 1e-4f);
		const float scale = (std::max)(b.Scale, 1.0f);
		ParallelRows(res, [&](int z) {
			for (int x = 0; x < res; ++x)
			{
				const float px = (x * cx + b.OffsetX) / scale, pz = (z * cz + b.OffsetZ) / scale;
				float sum = 0.0f, prev = 1.0f, dx = 0.0f, dz = 0.0f;
				float freq = 1.0f;
				for (int o = 0; o < TerrainGenBase::kOctaves; ++o, freq *= 2.0f)
				{
					const float w = (std::max)(0.0f, b.Octaves[o]);
					if (w <= 0.0f)
						continue;
					const float qx = px * freq, qz = pz * freq;
					const float n = octave[o].Noise(qx, qz);
					switch (b.NoiseType)
					{
					case TerrainGenBase::Type::Ridged:
					{
						float s = 1.0f - fabsf(n);
						s *= s;
						sum += w * s * prev;
						prev = Saturate(s * 1.6f);
						break;
					}
					case TerrainGenBase::Type::Billow:
						sum += w * fabsf(n);
						break;
					case TerrainGenBase::Type::Eroded:
					{
						// 경사(앞 옥타브들의 미분 합)가 큰 곳은 잔 디테일을 줄인다 (침식된 듯한 모양)
						const float e = 0.01f;
						const float gx = (octave[o].Noise(qx + e, qz) - n) / e, gz = (octave[o].Noise(qx, qz + e) - n) / e;   // 옥타브 자기 좌표의 기울기 (IQ)
						dx += gx * w; dz += gz * w;
						sum += w * (n * 0.5f + 0.5f) / (1.0f + (dx * dx + dz * dz) * 0.35f);
						break;
					}
					default:
						sum += w * n;
						break;
					}
				}
				float h01;
				if (b.NoiseType == TerrainGenBase::Type::Classic)
					h01 = Saturate(0.5f + 0.5f * sum / wsum * 1.6f);
				else
					h01 = Saturate(sum / wsum * (b.NoiseType == TerrainGenBase::Type::Billow ? 1.7f : 1.15f));
				h01 = powf(h01, (std::max)(0.1f, b.ShapePower));
				H[(size_t)z * res + x] = Lerp(b.MinHeight, b.MaxHeight, h01);
			}
		});
	}

	// ---------------------------------------------------------------- 스탬프 모양 (u, v ∈ -1~1, 결과 -1~1)
	float ShapeValue(const TerrainGenerator::StampInput& s, const Perlin& noise, float u, float v)
	{
		using Shape = TerrainStamp::Shape;
		const float r = sqrtf(u * u + v * v);
		const float warp = noise.Fbm(u * 1.7f + 3.1f, v * 1.7f - 1.3f, 3);
		switch ((Shape)s.Shape)
		{
		case Shape::Mountain:
		{
			// 도메인 워프(비대칭 산자락) + 방사형 능선 + 높을수록 강한 릿지 디테일
			const float wu = u + 0.28f * noise.Noise(u * 1.3f + 7.0f, v * 1.3f + 2.0f);
			const float wv = v + 0.28f * noise.Noise(u * 1.3f - 5.0f, v * 1.3f + 9.0f);
			const float rr = sqrtf(wu * wu + wv * wv);
			const float base = Saturate(1.0f - rr);
			const float profile = base * base * (3.0f - 2.0f * base) * 0.55f + powf(base, 2.4f) * 0.45f;   // 넓은 산자락 + 뾰족한 꼭대기
			const float ang = atan2f(wv, wu);
			const float arms = 0.5f + 0.5f * cosf(ang * 5.0f + 3.0f * noise.Noise(rr * 2.0f + 3.3f, 1.7f));
			const float ridged = 1.0f - fabsf(noise.Fbm(wu * 2.4f + 11.0f, wv * 2.4f + 5.0f, 4));
			return profile * (0.72f + 0.18f * arms * (1.0f - base) + 0.35f * ridged * ridged * (0.3f + 0.7f * base));
		}
		case Shape::Hill:
		{
			const float t = Saturate(1.0f - r * r * (1.0f + 0.15f * warp));
			return t * t * (3.0f - 2.0f * t);
		}
		case Shape::Crater:
		{
			const float R = 0.6f;
			const float rr = r * (1.0f + 0.08f * warp);
			const float bowl = rr < R ? -(1.0f - (rr / R) * (rr / R)) * 0.65f : 0.0f;
			const float rim = expf(-((rr - R) / 0.16f) * ((rr - R) / 0.16f)) * 0.55f * Saturate((1.0f - rr) / 0.35f);
			return bowl + rim;
		}
		case Shape::Volcano:
		{
			const float rr = r * (1.0f + 0.12f * warp);
			const float cone = powf(Saturate(1.0f - rr), 1.25f);
			const float caldera = rr < 0.16f ? (1.0f - rr / 0.16f) * 0.42f : 0.0f;
			return cone - caldera;
		}
		case Shape::Mesa:
		{
			const float edge = 0.62f + 0.1f * warp;
			const float top = 1.0f - Smoothstep(edge - 0.05f, edge + 0.12f, r);
			const float talus = (1.0f - Smoothstep(edge, 1.0f, r)) * 0.18f;
			return (std::max)(top * (0.95f + 0.05f * warp), talus);
		}
		case Shape::Ridge:
		{
			const float vv = v + 0.18f * noise.Noise(u * 2.2f + 4.0f, 1.7f);
			const float across = powf(Saturate(1.0f - fabsf(vv)), 1.8f);
			const float along = 1.0f - Smoothstep(0.55f, 1.0f, fabsf(u));
			const float peaks = 0.8f + 0.3f * noise.Noise(u * 4.0f, 9.3f);
			return across * along * peaks;
		}
		case Shape::Canyon:
		{
			const float meander = 0.22f * sinf(u * 3.1f + (float)(s.Seed % 17)) + 0.12f * noise.Noise(u * 2.5f, 3.3f);
			const float d = fabsf(v - meander);
			const float wall = 1.0f - Smoothstep(0.06f, 0.32f + 0.05f * warp, d);
			const float along = 1.0f - Smoothstep(0.75f, 1.0f, fabsf(u));
			return -wall * along;
		}
		case Shape::Dunes:
		{
			const float phase = u * 5.0f + 0.6f * noise.Noise(v * 1.5f + 2.0f, u * 0.7f);
			const float f = phase - floorf(phase);
			const float profile = f < 0.72f ? f / 0.72f : (1.0f - f) / 0.28f;   // 완만한 바람받이 + 가파른 미끄럼면
			const float env = Saturate(1.0f - powf(r, 3.0f));
			return profile * profile * 0.6f * env * (0.8f + 0.2f * warp);
		}
		case Shape::Island:
		{
			const float t = Saturate(1.0f - r * r + 0.28f * warp);
			return t * t * (3.0f - 2.0f * t) * (0.85f + 0.25f * noise.Fbm(u * 3.0f, v * 3.0f, 3, 0.7f));
		}
		case Shape::Heightmap:
		{
			if (!s.Image || s.Image->Width < 2)
				return 0.0f;
			const auto& img = *s.Image;
			const float fx = Saturate(u * 0.5f + 0.5f) * (img.Width - 1), fy = Saturate(0.5f - v * 0.5f) * (img.Height - 1);
			const int x0 = (int)fx, y0 = (int)fy;
			const int x1 = (std::min)(x0 + 1, img.Width - 1), y1 = (std::min)(y0 + 1, img.Height - 1);
			const float tx = fx - x0, ty = fy - y0;
			const float a = Lerp(img.Pixels[(size_t)y0 * img.Width + x0], img.Pixels[(size_t)y0 * img.Width + x1], tx);
			const float c = Lerp(img.Pixels[(size_t)y1 * img.Width + x0], img.Pixels[(size_t)y1 * img.Width + x1], tx);
			return Lerp(a, c, ty);
		}
		default:
			return 0.0f;
		}
	}

	void ApplyStamp(const TerrainGenerator::Input& in, const TerrainGenerator::StampInput& s, std::vector<float>& H)
	{
		const int res = in.Resolution;
		const float cx = in.SizeX / (res - 1), cz = in.SizeZ / (res - 1);
		// 회전된 사각형의 축 정렬 범위
		const float ex = fabsf(s.Cos) * s.HalfX + fabsf(s.Sin) * s.HalfZ, ez = fabsf(s.Sin) * s.HalfX + fabsf(s.Cos) * s.HalfZ;
		const int x0 = (std::max)(0, (int)floorf((s.Cx - ex) / cx)), x1 = (std::min)(res - 1, (int)ceilf((s.Cx + ex) / cx));
		const int z0 = (std::max)(0, (int)floorf((s.Cz - ez) / cz)), z1 = (std::min)(res - 1, (int)ceilf((s.Cz + ez) / cz));
		if (x0 > x1 || z0 > z1)
			return;
		const Perlin noise(s.Seed * 977 + 31);
		const Perlin detail(s.Seed * 613 + 77);
		const float blend = std::clamp(s.Blend, 0.01f, 1.0f);
		ParallelRows(z1 - z0 + 1, [&](int row) {
			const int z = z0 + row;
			for (int x = x0; x <= x1; ++x)
			{
				const float dx = x * cx - s.Cx, dz = z * cz - s.Cz;
				const float u = (dx * s.Cos + dz * s.Sin) / s.HalfX;
				const float v = (-dx * s.Sin + dz * s.Cos) / s.HalfZ;
				const float d = Lerp((std::max)(fabsf(u), fabsf(v)), sqrtf(u * u + v * v), s.Roundness);
				if (d >= 1.0f)
					continue;
				const float mask = (1.0f - Smoothstep(1.0f - blend, 1.0f, d)) * s.Opacity;
				if (mask <= 0.0f)
					continue;
				float value = ShapeValue(s, noise, u, v);
				if (s.Power != 1.0f)
					value = (value < 0.0f ? -1.0f : 1.0f) * powf(fabsf(value), (std::max)(0.1f, s.Power));
				if (s.Detail > 0.0f)
				{
					const float ds = (std::max)(0.02f, s.DetailScale);
					value += s.Detail * 0.25f * detail.Fbm(u / ds, v / ds, 4, 0.6f) * (0.35f + 0.65f * fabsf(value));
				}
				float& h = H[(size_t)z * res + x];
				const float target = s.BaseY + value * s.Height;
				switch ((TerrainStamp::Operation)s.Op)
				{
				case TerrainStamp::Operation::Add: h += mask * value * s.Height; break;
				case TerrainStamp::Operation::Subtract: h -= mask * value * s.Height; break;
				case TerrainStamp::Operation::Max: h = Lerp(h, (std::max)(h, target), mask); break;
				case TerrainStamp::Operation::Min: h = Lerp(h, (std::min)(h, target), mask); break;
				default: h = Lerp(h, target, mask); break;
				}
			}
		});
	}

	// ---------------------------------------------------------------- 필터
	// 수력 침식 (빗방울 입자: 경사를 따라 흐르며 깎고, 느려지면 쌓는다). 높이는 칸 단위로 바꿔 계산 → 실제 경사
	void HydraulicErosion(const TerrainGenerator::Input& in, const TerrainGenFilter& f, std::vector<float>& H, std::vector<float>& sediment)
	{
		const int res = in.Resolution;
		const float cell = in.SizeX / (res - 1);
		// 칸 단위 높이. 가장 낮은 곳을 0 으로 옮겨 계산 (협곡 스탬프가 0 아래로 판 곳에서 깎는 양이 음수가 되지 않게)
		const float base = *std::min_element(H.begin(), H.end());
		std::vector<float> h(H.size());
		for (size_t i = 0; i < H.size(); ++i)
			h[i] = (H[i] - base) / cell;
		const int droplets = (int)(f.P[0] * 1000.0f * ((float)res * res) / (513.0f * 513.0f));
		const float erodeSpeed = f.P[1], depositSpeed = f.P[2], evaporate = f.P[3], inertia = f.P[4];
		const int radius = std::clamp((int)f.P[5], 1, 6);
		constexpr float kCapacity = 4.0f, kMinCapacity = 0.01f, kGravity = 4.0f;
		constexpr int kLifetime = 40;
		// 깎는 붓 (반지름 안의 가중치)
		std::vector<int> bx, bz;
		std::vector<float> bw;
		float wsum = 0.0f;
		for (int dz = -radius; dz <= radius; ++dz)
			for (int dx = -radius; dx <= radius; ++dx)
			{
				const float d = sqrtf((float)(dx * dx + dz * dz));
				if (d <= radius)
				{
					bx.push_back(dx); bz.push_back(dz); bw.push_back(1.0f - d / radius);
					wsum += bw.back();
				}
			}
		for (float& w : bw)
			w /= wsum;
		auto heightGrad = [&](float px, float pz, float& gx, float& gz) {
			const int x = (int)px, z = (int)pz;
			const float u = px - x, v = pz - z;
			const size_t i = (size_t)z * res + x;
			const float a = h[i], b = h[i + 1], c = h[i + res], d = h[i + res + 1];
			gx = (b - a) * (1 - v) + (d - c) * v;
			gz = (c - a) * (1 - u) + (d - b) * u;
			return a * (1 - u) * (1 - v) + b * u * (1 - v) + c * (1 - u) * v + d * u * v;
		};
		std::mt19937 rng((uint32_t)(f.P[0] * 1000.0f) + 99u);
		std::uniform_real_distribution<float> uni(0.0f, (float)(res - 2));
		for (int n = 0; n < droplets; ++n)
		{
			float px = uni(rng), pz = uni(rng);
			float dirX = 0, dirZ = 0, speed = 1, water = 1, sed = 0;
			for (int life = 0; life < kLifetime; ++life)
			{
				const int nodeX = (int)px, nodeZ = (int)pz;
				const float offX = px - nodeX, offZ = pz - nodeZ;
				float gx, gz;
				const float height = heightGrad(px, pz, gx, gz);
				dirX = dirX * inertia - gx * (1 - inertia);
				dirZ = dirZ * inertia - gz * (1 - inertia);
				const float len = sqrtf(dirX * dirX + dirZ * dirZ);
				if (len < 1e-6f)
					break;
				dirX /= len; dirZ /= len;
				px += dirX; pz += dirZ;
				if (px < 0 || pz < 0 || px >= res - 2 || pz >= res - 2)
					break;
				float ngx, ngz;
				const float dh = heightGrad(px, pz, ngx, ngz) - height;
				const float capacity = (std::max)(-dh * speed * water * kCapacity, kMinCapacity);
				const size_t i = (size_t)nodeZ * res + nodeX;
				if (sed > capacity || dh > 0)
				{
					const float amount = dh > 0 ? (std::min)(dh, sed) : (sed - capacity) * depositSpeed;
					sed -= amount;
					h[i] += amount * (1 - offX) * (1 - offZ);
					h[i + 1] += amount * offX * (1 - offZ);
					h[i + res] += amount * (1 - offX) * offZ;
					h[i + res + 1] += amount * offX * offZ;
					sediment[i] += amount;
				}
				else
				{
					const float amount = (std::min)((capacity - sed) * erodeSpeed, -dh);
					for (size_t k = 0; k < bw.size(); ++k)
					{
						const int x = nodeX + bx[k], z = nodeZ + bz[k];
						if (x < 0 || z < 0 || x >= res || z >= res)
							continue;
						h[(size_t)z * res + x] -= amount * bw[k];
						sed += amount * bw[k];
					}
				}
				speed = sqrtf((std::max)(0.0f, speed * speed - dh * kGravity));   // 내리막(dh < 0)이면 빨라진다
				water *= 1 - evaporate;
			}
		}
		for (size_t i = 0; i < H.size(); ++i)
			H[i] = std::isfinite(h[i]) ? h[i] * cell + base : H[i];
	}

	// 열 침식: 이웃과의 높이 차가 안식각을 넘으면 넘친 만큼 아래로 (주변을 모아 계산 → 병렬)
	void ThermalErosion(const TerrainGenerator::Input& in, const TerrainGenFilter& f, std::vector<float>& H)
	{
		const int res = in.Resolution;
		const float cell = in.SizeX / (res - 1);
		const float talus = tanf(XMConvertToRadians(f.P[0])) * cell;
		const int iterations = std::clamp((int)f.P[1], 1, 500);
		const float k = 0.25f * Saturate(f.P[2]) * 0.5f;
		std::vector<float> next(H.size());
		for (int it = 0; it < iterations; ++it)
		{
			ParallelRows(res, [&](int z) {
				for (int x = 0; x < res; ++x)
				{
					const size_t i = (size_t)z * res + x;
					const float hi = H[i];
					float d = 0.0f;
					auto flow = [&](int nx, int nz) {
						if (nx < 0 || nz < 0 || nx >= res || nz >= res)
							return;
						const float hn = H[(size_t)nz * res + nx];
						d += (std::max)(0.0f, hn - hi - talus) - (std::max)(0.0f, hi - hn - talus);
					};
					flow(x - 1, z); flow(x + 1, z); flow(x, z - 1); flow(x, z + 1);
					next[i] = hi + k * d;
				}
			});
			H.swap(next);
		}
	}

	void Terrace(const TerrainGenFilter& f, std::vector<float>& H)
	{
		const float steps = (std::max)(2.0f, f.P[0]);
		const float sharp = Saturate(f.P[1]);
		const float lo = f.P[2], hi = (std::max)(f.P[3], f.P[2] + 1.0f);
		std::for_each(std::execution::par, H.begin(), H.end(), [&](float& h) {
			if (h < lo || h > hi)
				return;
			const float t = (h - lo) / (hi - lo) * steps;
			const float fl = floorf(t), fr = t - fl;
			const float shaped = powf(fr, 1.0f + sharp * 7.0f);
			h = lo + (fl + shaped) / steps * (hi - lo);
		});
	}

	void Smooth(const TerrainGenerator::Input& in, const TerrainGenFilter& f, std::vector<float>& H)
	{
		const int res = in.Resolution;
		const int r = std::clamp((int)f.P[0], 1, 12);
		const int iterations = std::clamp((int)f.P[1], 1, 10);
		std::vector<float> tmp(H.size());
		for (int it = 0; it < iterations; ++it)
		{
			ParallelRows(res, [&](int z) {   // 가로
				for (int x = 0; x < res; ++x)
				{
					float s = 0; int n = 0;
					for (int k = -r; k <= r; ++k) { const int xx = std::clamp(x + k, 0, res - 1); s += H[(size_t)z * res + xx]; ++n; }
					tmp[(size_t)z * res + x] = s / n;
				}
			});
			ParallelRows(res, [&](int z) {   // 세로
				for (int x = 0; x < res; ++x)
				{
					float s = 0; int n = 0;
					for (int k = -r; k <= r; ++k) { const int zz = std::clamp(z + k, 0, res - 1); s += tmp[(size_t)zz * res + x]; ++n; }
					H[(size_t)z * res + x] = s / n;
				}
			});
		}
	}

	void HeightCurve(const TerrainGenFilter& f, std::vector<float>& H)
	{
		const auto [mnIt, mxIt] = std::minmax_element(H.begin(), H.end());
		const float mn = *mnIt, mx = (std::max)(*mxIt, mn + 0.001f);
		const float gamma = (std::max)(0.05f, f.P[0]), gain = f.P[1], offset = f.P[2];
		std::for_each(std::execution::par, H.begin(), H.end(), [&](float& h) {
			const float t = powf(Saturate((h - mn) / (mx - mn)), gamma);
			h = mn + t * (mx - mn) * gain + offset;
		});
	}

	void DetailNoise(const TerrainGenerator::Input& in, const TerrainGenFilter& f, std::vector<float>& H)
	{
		const int res = in.Resolution;
		const float cx = in.SizeX / (res - 1), cz = in.SizeZ / (res - 1);
		const Perlin noise((int)f.P[3] * 389 + 5);
		const float amount = f.P[0], scale = (std::max)(1.0f, f.P[1]), ridged = Saturate(f.P[2]);
		ParallelRows(res, [&](int z) {
			for (int x = 0; x < res; ++x)
				H[(size_t)z * res + x] += amount * noise.Fbm(x * cx / scale, z * cz / scale, 5, ridged);
		});
	}

	// ---------------------------------------------------------------- 재질 (스플랫)
	void PaintMaterials(const TerrainGenerator::Input& in, const std::vector<float>& H, const std::vector<float>& sediment, std::vector<uint8_t>& control)
	{
		const int res = in.Resolution, cres = in.ControlResolution;
		const int layers = (std::min)(in.LayerCount, 4);
		control.assign((size_t)cres * cres * 4, 0);
		// 퇴적 마스크: 흐린 뒤 상위 값으로 정규화
		std::vector<float> sed = sediment;
		float smax = 0.0f;
		if (!sed.empty())
		{
			std::vector<float> sorted = sed;
			const size_t k = sorted.size() * 98 / 100;
			std::nth_element(sorted.begin(), sorted.begin() + k, sorted.end());
			smax = (std::max)(sorted[k], 1e-6f);
		}
		const Perlin noise(4242);
		const float cellX = in.SizeX / (res - 1), cellZ = in.SizeZ / (res - 1);
		auto sample = [&](const std::vector<float>& m, float gx, float gz) {
			gx = std::clamp(gx, 0.0f, (float)res - 1.001f); gz = std::clamp(gz, 0.0f, (float)res - 1.001f);
			const int x = (int)gx, z = (int)gz;
			const float u = gx - x, v = gz - z;
			const size_t i = (size_t)z * res + x;
			return Lerp(Lerp(m[i], m[i + 1], u), Lerp(m[i + res], m[i + res + 1], u), v);
		};
		ParallelRows(cres, [&](int z) {
			for (int x = 0; x < cres; ++x)
			{
				const float gx = (float)x / (cres - 1) * (res - 1), gz = (float)z / (cres - 1) * (res - 1);
				const float h = sample(H, gx, gz);
				const float sx = (sample(H, gx + 1, gz) - sample(H, gx - 1, gz)) / (2 * cellX);
				const float sz = (sample(H, gx, gz + 1) - sample(H, gx, gz - 1)) / (2 * cellZ);
				const float slope = XMConvertToDegrees(atanf(sqrtf(sx * sx + sz * sz)));
				const float s = sed.empty() ? 0.0f : Saturate(sample(sed, gx, gz) / smax);
				const float n = noise.Fbm(x * 0.045f, z * 0.045f, 4);
				float w[4] = { 1, 0, 0, 0 };
				for (const auto& r : in.Settings.Materials)
				{
					if (!r.Enabled || r.Layer < 0 || r.Layer >= layers)
						continue;
					const float hb = (std::max)(0.01f, r.HeightBlend), sb = (std::max)(0.01f, r.SlopeBlend);
					const float hn = h + n * r.Noise * hb * 2.0f, sn = slope + n * r.Noise * sb * 2.0f;
					float f = Smoothstep(r.HeightMin - hb, r.HeightMin + hb, hn) * (1.0f - Smoothstep(r.HeightMax - hb, r.HeightMax + hb, hn));
					f *= Smoothstep(r.SlopeMin - sb, r.SlopeMin + sb, sn) * (1.0f - Smoothstep(r.SlopeMax - sb, r.SlopeMax + sb, sn));
					f *= Lerp(1.0f, Smoothstep(0.05f, 0.45f, s + n * r.Noise * 0.15f), r.Sediment);
					f = Saturate(f * r.Opacity);
					for (int c = 0; c < 4; ++c)
						w[c] *= 1.0f - f;
					w[r.Layer] += f;
				}
				uint8_t* px = &control[((size_t)z * cres + x) * 4];
				for (int c = 0; c < 4; ++c)
					px[c] = (uint8_t)std::lround(Saturate(c < layers ? w[c] : 0.0f) * 255.0f);
			}
		});
	}
}

namespace TerrainGenerator
{
	Output Generate(const Input& in)
	{
		using clock = std::chrono::steady_clock;
		Output out;
		const int res = in.Resolution;
		std::vector<float> H((size_t)res * res);
		// 단계별 높이 범위 (Editor.log, 문제를 찾을 때)
		const bool verbose = !in.Preview && FrameProfiler::Enabled();   // NOVA_DEV_PROFILE=1
		auto logStats = [&](const char* stage) {
			if (!verbose)
				return;
			double sum = 0.0;
			float mn = 1e30f, mx = -1e30f;
			for (float h : H) { mn = (std::min)(mn, h); mx = (std::max)(mx, h); sum += h; }
			EditorLog::Write("TerrainGen", "  %-18s min %.1f  max %.1f  mean %.1f m", stage, mn, mx, sum / (double)H.size());
		};
		auto t0 = clock::now();
		BuildBase(in, H);
		logStats("base");
		auto t1 = clock::now();
		for (const StampInput& s : in.Stamps)
			ApplyStamp(in, s, H);
		logStats("stamps");
		auto t2 = clock::now();
		std::vector<float> sediment(H.size(), 0.0f);
		for (const TerrainGenFilter& f : in.Settings.Filters)
		{
			if (!f.Enabled || f.Strength <= 0.0f || (in.Preview && TerrainGenFilter::IsHeavy(f.FilterType)))
				continue;
			const std::vector<float> before = f.Strength < 1.0f ? H : std::vector<float>();
			switch (f.FilterType)
			{
			case TerrainGenFilter::Type::HydraulicErosion: HydraulicErosion(in, f, H, sediment); break;
			case TerrainGenFilter::Type::ThermalErosion: ThermalErosion(in, f, H); break;
			case TerrainGenFilter::Type::Terrace: Terrace(f, H); break;
			case TerrainGenFilter::Type::Smooth: Smooth(in, f, H); break;
			case TerrainGenFilter::Type::HeightCurve: HeightCurve(f, H); break;
			default: DetailNoise(in, f, H); break;
			}
			if (!before.empty())
				for (size_t i = 0; i < H.size(); ++i)
					H[i] = Lerp(before[i], H[i], f.Strength);
			logStats(TerrainGenFilter::Name(f.FilterType));
		}
		auto t3 = clock::now();
		out.Heights.resize(H.size());
		const float inv = 1.0f / (std::max)(in.SizeY, 0.001f);
		for (size_t i = 0; i < H.size(); ++i)
			out.Heights[i] = Saturate(H[i] * inv);
		if (in.Settings.PaintMaterials && in.LayerCount > 0)
		{
			PaintMaterials(in, H, sediment, out.Control);
			out.HasControl = true;
		}
		auto t4 = clock::now();
		auto ms = [](clock::time_point a, clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
		out.Ms[0] = ms(t0, t1); out.Ms[1] = ms(t1, t2); out.Ms[2] = ms(t2, t3); out.Ms[3] = ms(t3, t4);
		return out;
	}

	std::shared_ptr<const HeightImage> LoadHeightImage(const std::string& projectPath)
	{
		static std::mutex s_Lock;
		static std::map<std::string, std::shared_ptr<const HeightImage>> s_Cache;
		std::lock_guard<std::mutex> lock(s_Lock);
		if (projectPath.empty())
			return nullptr;
		if (auto it = s_Cache.find(projectPath); it != s_Cache.end())
			return it->second;
		std::shared_ptr<HeightImage> img;
		const std::wstring path = PathManager::GetI()->GetMovePathW(string_to_wstring(projectPath));
		DirectX::ScratchImage loaded;
		HRESULT hr = E_FAIL;
		const std::wstring ext = std::filesystem::path(path).extension().wstring();
		if (_wcsicmp(ext.c_str(), L".dds") == 0)
			hr = DirectX::LoadFromDDSFile(path.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, loaded);
		else if (_wcsicmp(ext.c_str(), L".tga") == 0)
			hr = DirectX::LoadFromTGAFile(path.c_str(), nullptr, loaded);
		else
			hr = DirectX::LoadFromWICFile(path.c_str(), DirectX::WIC_FLAGS_IGNORE_SRGB, nullptr, loaded);
		if (SUCCEEDED(hr))
		{
			DirectX::ScratchImage converted;
			const DirectX::Image* src = loaded.GetImage(0, 0, 0);
			if (src && SUCCEEDED(DirectX::Convert(*src, DXGI_FORMAT_R32_FLOAT, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, converted)))
			{
				const DirectX::Image* f = converted.GetImage(0, 0, 0);
				img = std::make_shared<HeightImage>();
				img->Width = (int)f->width;
				img->Height = (int)f->height;
				img->Pixels.resize((size_t)f->width * f->height);
				for (size_t y = 0; y < f->height; ++y)
					memcpy(&img->Pixels[y * f->width], f->pixels + y * f->rowPitch, f->width * sizeof(float));
			}
		}
		EditorLog::Write("TerrainGen", "heightmap stamp image %s: %s", projectPath.c_str(), img ? "loaded" : "failed");
		s_Cache[projectPath] = img;
		return img;
	}
}

// ================================================================ 에디터 연결 (백그라운드 생성 + 적용)
namespace
{
	struct Job
	{
		std::future<TerrainGenerator::Output> Result;
		uint64_t Hash = 0;
		bool Preview = false;
		int Stamps = 0;
		std::chrono::steady_clock::time_point Start;
	};
	struct State
	{
		std::weak_ptr<TerrainData> Data;
		uint64_t AppliedHash = 0;
		bool AppliedPreview = false;
		bool Force = false;
		std::unique_ptr<Job> Running;
		TerrainGenerator::Status Status;
	};
	std::unordered_map<const TerrainData*, State> s_States;

	uint64_t Mix(uint64_t h, uint64_t v) { return h ^ (v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2)); }
	uint64_t Bits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

	// 이 지형에 들어갈 스탬프 (Order 순, 같으면 등록 순)
	std::vector<TerrainGenerator::StampInput> CollectStamps(const Vec3& terrainPos, uint64_t& hash)
	{
		std::vector<std::pair<int, TerrainStamp*>> list;
		int idx = 0;
		for (TerrainStamp* s : TerrainStamp::All())
			if (s->IsActiveStamp())
				list.push_back({ s->Order * 100000 + idx++, s });
		std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
		std::vector<TerrainGenerator::StampInput> out;
		for (const auto& [order, s] : list)
		{
			XMFLOAT4X4 w;
			XMStoreFloat4x4(&w, s->GetGameObject()->GetTransform()->GetWorldMatrix());
			const float sx = sqrtf(w._11 * w._11 + w._12 * w._12 + w._13 * w._13);
			const float sy = sqrtf(w._21 * w._21 + w._22 * w._22 + w._23 * w._23);
			const float sz = sqrtf(w._31 * w._31 + w._32 * w._32 + w._33 * w._33);
			TerrainGenerator::StampInput in;
			in.Shape = (int)s->StampShape;
			in.Op = (int)s->Op;
			in.Cx = w._41 - terrainPos.x;
			in.Cz = w._43 - terrainPos.z;
			in.BaseY = w._42 - terrainPos.y;
			in.HalfX = (std::max)(0.5f, sx * 0.5f);
			in.HalfZ = (std::max)(0.5f, sz * 0.5f);
			const float len = sqrtf(w._11 * w._11 + w._13 * w._13);
			in.Cos = len > 1e-6f ? w._11 / len : 1.0f;
			in.Sin = len > 1e-6f ? w._13 / len : 0.0f;   // 로컬 X 축 = (Cos, Sin) → u = 오프셋·X축
			in.Height = s->Height * sy;
			in.Opacity = std::clamp(s->Opacity, 0.0f, 1.0f);
			in.Blend = s->BlendSize;
			in.Roundness = std::clamp(s->Roundness, 0.0f, 1.0f);
			in.Power = s->Power;
			in.Detail = s->Detail;
			in.DetailScale = s->DetailScale;
			in.Seed = s->Seed;
			if (s->StampShape == TerrainStamp::Shape::Heightmap)
				in.Image = TerrainGenerator::LoadHeightImage(s->HeightmapPath);
			out.push_back(in);
			// 해시: 행렬 + 값
			for (int k = 0; k < 16; ++k)
				hash = Mix(hash, Bits((&w._11)[k]));
			for (float f : { s->Height, s->Opacity, s->BlendSize, s->Roundness, s->Power, s->Detail, s->DetailScale })
				hash = Mix(hash, Bits(f));
			hash = Mix(hash, (uint64_t)s->StampShape * 31 + (uint64_t)s->Op * 7 + (uint64_t)s->Seed * 1009 + (uint64_t)(int64_t)s->Order);
			hash = Mix(hash, std::hash<std::string>()(s->HeightmapPath));
		}
		return out;
	}

	void Apply(TerrainData& data, TerrainGenerator::Output& out)
	{
		if (out.Heights.size() == data.Heights.size())
		{
			data.Heights.swap(out.Heights);
			data.OnHeightsChanged(0, 0, data.HeightmapResolution - 1, data.HeightmapResolution - 1);
		}
		if (out.HasControl && out.Control.size() == data.Control.size())
		{
			data.Control.swap(out.Control);
			data.OnControlChanged(0, 0, data.ControlResolution - 1, data.ControlResolution - 1);
		}
	}
}

namespace TerrainGenerator
{
	void Regenerate(const std::shared_ptr<TerrainData>& data)
	{
		if (data)
			s_States[data.get()].Force = true;
	}

	Status GetStatus(const TerrainData* data)
	{
		auto it = s_States.find(data);
		return it != s_States.end() ? it->second.Status : Status();
	}

	void Update()
	{
		if (Application::IsPlaying())
			return;
		const bool interacting = ImGui::GetCurrentContext() != nullptr &&
			(ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsAnyItemActive());
		for (Terrain* terrain : Terrain::GetActiveTerrains())
		{
			auto data = terrain->GetTerrainData();
			if (!data || !data->Generator.Enabled)
				continue;
			State& st = s_States[data.get()];
			st.Data = data;

			// 끝난 작업 적용
			if (st.Running && st.Running->Result.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
			{
				Output out = st.Running->Result.get();
				Apply(*data, out);
				st.AppliedHash = st.Running->Hash;
				st.AppliedPreview = st.Running->Preview;
				st.Status.LastPreview = st.Running->Preview;
				st.Status.LastMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - st.Running->Start).count();
				for (int i = 0; i < 4; ++i)
					st.Status.StageMs[i] = out.Ms[i];
				st.Status.StampCount = st.Running->Stamps;
				if (!st.Running->Preview || FrameProfiler::Enabled())   // 미리보기(끄는 중)는 너무 잦아 기본으로는 남기지 않는다
					EditorLog::Write("TerrainGen", "%s %s: %.1f ms (base %.1f, stamps %.1f, filters %.1f, materials %.1f; %d stamps)",
						data->Name().c_str(), st.Running->Preview ? "preview" : "generated", st.Status.LastMs, out.Ms[0], out.Ms[1], out.Ms[2], out.Ms[3], st.Running->Stamps);
				st.Running.reset();
			}
			st.Status.Running = st.Running != nullptr;
			if (st.Running)
				continue;

			// 입력 해시 (설정 + 스탬프 + 지형 위치·크기·해상도·레이어 수)
			const Vec3 pos = terrain->GetPosition();
			uint64_t hash = std::hash<std::string>()(data->Generator.ToJson().dump());
			hash = Mix(hash, Bits(pos.x)); hash = Mix(hash, Bits(pos.y)); hash = Mix(hash, Bits(pos.z));
			hash = Mix(hash, Bits(data->Size.x)); hash = Mix(hash, Bits(data->Size.y)); hash = Mix(hash, Bits(data->Size.z));
			hash = Mix(hash, (uint64_t)data->HeightmapResolution * 7 + data->Layers.size() + data->BaseSnapshot.size() * 13);
			std::vector<StampInput> stamps = CollectStamps(pos, hash);

			const bool changed = hash != st.AppliedHash;
			const bool needFinal = !changed && st.AppliedPreview && !interacting;
			if (!(st.Force || (data->Generator.AutoUpdate && (changed || needFinal))))
				continue;
			const bool preview = interacting && !st.Force;
			st.Force = false;

			auto in = std::make_shared<Input>();
			in->Resolution = data->HeightmapResolution;
			in->SizeX = data->Size.x; in->SizeY = data->Size.y; in->SizeZ = data->Size.z;
			in->ControlResolution = data->ControlResolution;
			in->LayerCount = (int)data->Layers.size();
			in->Settings = data->Generator;
			if (data->Generator.Base.NoiseType == TerrainGenBase::Type::CurrentTerrain)
				in->Snapshot = data->BaseSnapshot;
			in->Stamps = std::move(stamps);
			in->Preview = preview;
			auto job = std::make_unique<Job>();
			job->Hash = hash;
			job->Preview = preview;
			job->Stamps = (int)in->Stamps.size();
			job->Start = std::chrono::steady_clock::now();
			job->Result = std::async(std::launch::async, [in]() { return Generate(*in); });
			st.Running = std::move(job);
			st.Status.Running = true;
		}
	}
}
