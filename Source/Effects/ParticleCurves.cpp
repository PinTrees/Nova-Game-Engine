#include "pch.h"
#include "ParticleCurves.h"

using json = nlohmann::json;

// ------------------------------------------------------------------ ParticleCurve
float ParticleCurve::Evaluate(float t) const
{
	if (Keys.empty())
		return 0.0f;
	if (Keys.size() == 1 || t <= Keys.front().Time)
		return Keys.front().Value;
	if (t >= Keys.back().Time)
		return Keys.back().Value;
	size_t i = 1;
	while (i < Keys.size() && Keys[i].Time < t)
		++i;
	const Key& a = Keys[i - 1];
	const Key& b = Keys[i];
	const float span = b.Time - a.Time;
	if (span <= 1e-6f)
		return b.Value;
	// 접선: 양옆 키를 잇는 기울기 (끝 키는 한쪽). 선형 두 키면 직선과 같다
	auto slope = [&](size_t k) {
		const Key& p = Keys[k > 0 ? k - 1 : k];
		const Key& n = Keys[k + 1 < Keys.size() ? k + 1 : k];
		const float dt = n.Time - p.Time;
		return dt > 1e-6f ? (n.Value - p.Value) / dt : 0.0f;
	};
	const float m0 = slope(i - 1) * span, m1 = slope(i) * span;
	const float s = (t - a.Time) / span, s2 = s * s, s3 = s2 * s;
	return (2 * s3 - 3 * s2 + 1) * a.Value + (s3 - 2 * s2 + s) * m0 + (-2 * s3 + 3 * s2) * b.Value + (s3 - s2) * m1;
}

void ParticleCurve::Sort()
{
	std::stable_sort(Keys.begin(), Keys.end(), [](const Key& a, const Key& b) { return a.Time < b.Time; });
}

void ParticleCurve::Range(float& lo, float& hi) const
{
	lo = 0.0f;
	hi = 1.0f;
	if (Keys.empty())
		return;
	lo = hi = Keys[0].Value;
	for (int i = 0; i <= 32; ++i)
	{
		const float v = Evaluate(i / 32.0f);
		lo = (std::min)(lo, v);
		hi = (std::max)(hi, v);
	}
}

// ------------------------------------------------------------------ ParticleGradient
Vec4 ParticleGradient::Evaluate(float t) const
{
	t = std::clamp(t, 0.0f, 1.0f);
	Vec4 out(1, 1, 1, 1);
	if (!Colors.empty())
	{
		if (t <= Colors.front().Time)
			out = Vec4(Colors.front().R, Colors.front().G, Colors.front().B, 1);
		else if (t >= Colors.back().Time)
			out = Vec4(Colors.back().R, Colors.back().G, Colors.back().B, 1);
		else
		{
			size_t i = 1;
			while (i < Colors.size() && Colors[i].Time < t)
				++i;
			const ColorKey& a = Colors[i - 1];
			const ColorKey& b = Colors[i];
			// Fixed: 다음 키에 닿기 전까지 다음 키의 색 (Unity)
			const float s = Mode == 1 ? 1.0f : (b.Time - a.Time > 1e-6f ? (t - a.Time) / (b.Time - a.Time) : 1.0f);
			out = Vec4(a.R + (b.R - a.R) * s, a.G + (b.G - a.G) * s, a.B + (b.B - a.B) * s, 1);
		}
	}
	if (!Alphas.empty())
	{
		if (t <= Alphas.front().Time)
			out.w = Alphas.front().A;
		else if (t >= Alphas.back().Time)
			out.w = Alphas.back().A;
		else
		{
			size_t i = 1;
			while (i < Alphas.size() && Alphas[i].Time < t)
				++i;
			const AlphaKey& a = Alphas[i - 1];
			const AlphaKey& b = Alphas[i];
			const float s = Mode == 1 ? 1.0f : (b.Time - a.Time > 1e-6f ? (t - a.Time) / (b.Time - a.Time) : 1.0f);
			out.w = a.A + (b.A - a.A) * s;
		}
	}
	return out;
}

void ParticleGradient::Sort()
{
	std::stable_sort(Colors.begin(), Colors.end(), [](const ColorKey& a, const ColorKey& b) { return a.Time < b.Time; });
	std::stable_sort(Alphas.begin(), Alphas.end(), [](const AlphaKey& a, const AlphaKey& b) { return a.Time < b.Time; });
}

// ------------------------------------------------------------------ MinMax
float MinMaxCurve::Evaluate(float t, float random) const
{
	switch (Mode)
	{
	case ParticleCurveMode::Constant: return ConstantMax;
	case ParticleCurveMode::TwoConstants: return ConstantMin + (ConstantMax - ConstantMin) * random;
	case ParticleCurveMode::Curve: return CurveMax.Evaluate(t) * Multiplier;
	case ParticleCurveMode::TwoCurves:
	{
		const float a = CurveMin.Evaluate(t), b = CurveMax.Evaluate(t);
		return (a + (b - a) * random) * Multiplier;
	}
	}
	return 0.0f;
}

float MinMaxCurve::MaxValue() const
{
	switch (Mode)
	{
	case ParticleCurveMode::Constant: return ConstantMax;
	case ParticleCurveMode::TwoConstants: return (std::max)(ConstantMin, ConstantMax);
	default:
	{
		float lo, hi, lo2, hi2;
		CurveMax.Range(lo, hi);
		if (Mode == ParticleCurveMode::TwoCurves)
		{
			CurveMin.Range(lo2, hi2);
			hi = (std::max)(hi, hi2);
		}
		return hi * Multiplier;
	}
	}
}

Vec4 MinMaxGradient::Evaluate(float t, float random) const
{
	switch (Mode)
	{
	case ParticleGradientMode::Color: return ColorMax;
	case ParticleGradientMode::TwoColors: return Vec4::Lerp(ColorMin, ColorMax, random);
	case ParticleGradientMode::Gradient: return GradientMax.Evaluate(t);
	case ParticleGradientMode::TwoGradients: return Vec4::Lerp(GradientMin.Evaluate(t), GradientMax.Evaluate(t), random);
	case ParticleGradientMode::RandomColor: return GradientMax.Evaluate(random);   // 그라디언트에서 무작위로 한 색
	}
	return Vec4(1, 1, 1, 1);
}

// ------------------------------------------------------------------ JSON
void to_json(json& j, const ParticleCurve& c)
{
	j = json::array();
	for (const auto& k : c.Keys)
		j.push_back({ k.Time, k.Value });
}

void from_json(const json& j, ParticleCurve& c)
{
	c.Keys.clear();
	if (!j.is_array())
		return;
	for (const auto& k : j)
		if (k.is_array() && k.size() >= 2)
			c.Keys.push_back({ k[0].get<float>(), k[1].get<float>() });
	c.Sort();
}

void to_json(json& j, const ParticleGradient& g)
{
	json colors = json::array(), alphas = json::array();
	for (const auto& k : g.Colors)
		colors.push_back({ k.Time, k.R, k.G, k.B });
	for (const auto& k : g.Alphas)
		alphas.push_back({ k.Time, k.A });
	j = json{ { "colors", colors }, { "alphas", alphas }, { "mode", g.Mode } };
}

void from_json(const json& j, ParticleGradient& g)
{
	g = ParticleGradient();
	if (!j.is_object())
		return;
	if (j.contains("colors") && j["colors"].is_array() && !j["colors"].empty())
	{
		g.Colors.clear();
		for (const auto& k : j["colors"])
			if (k.is_array() && k.size() >= 4)
				g.Colors.push_back({ k[0].get<float>(), k[1].get<float>(), k[2].get<float>(), k[3].get<float>() });
	}
	if (j.contains("alphas") && j["alphas"].is_array() && !j["alphas"].empty())
	{
		g.Alphas.clear();
		for (const auto& k : j["alphas"])
			if (k.is_array() && k.size() >= 2)
				g.Alphas.push_back({ k[0].get<float>(), k[1].get<float>() });
	}
	g.Mode = j.value("mode", 0);
	g.Sort();
}

void to_json(json& j, const MinMaxCurve& c)
{
	// 상수면 숫자 하나로 (씬 파일을 읽기 쉽게)
	if (c.Mode == ParticleCurveMode::Constant)
	{
		j = c.ConstantMax;
		return;
	}
	j = json{ { "mode", (int)c.Mode }, { "min", c.ConstantMin }, { "max", c.ConstantMax }, { "multiplier", c.Multiplier },
		{ "curveMin", c.CurveMin }, { "curveMax", c.CurveMax } };
}

void from_json(const json& j, MinMaxCurve& c)
{
	c = MinMaxCurve();
	if (j.is_number())
	{
		c.ConstantMax = j.get<float>();
		return;
	}
	if (!j.is_object())
		return;
	c.Mode = (ParticleCurveMode)std::clamp(j.value("mode", 0), 0, 3);
	c.ConstantMin = j.value("min", 0.0f);
	c.ConstantMax = j.value("max", 0.0f);
	c.Multiplier = j.value("multiplier", 1.0f);
	if (j.contains("curveMin")) c.CurveMin = j["curveMin"].get<ParticleCurve>();
	if (j.contains("curveMax")) c.CurveMax = j["curveMax"].get<ParticleCurve>();
}

namespace
{
	json ColorJson(const Vec4& c) { return json::array({ c.x, c.y, c.z, c.w }); }
	Vec4 ColorFrom(const json& j, const Vec4& def)
	{
		if (!j.is_array() || j.size() < 4)
			return def;
		return Vec4(j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>());
	}
}

void to_json(json& j, const MinMaxGradient& g)
{
	if (g.Mode == ParticleGradientMode::Color)
	{
		j = ColorJson(g.ColorMax);
		return;
	}
	j = json{ { "mode", (int)g.Mode }, { "colorMin", ColorJson(g.ColorMin) }, { "colorMax", ColorJson(g.ColorMax) },
		{ "gradientMin", g.GradientMin }, { "gradientMax", g.GradientMax } };
}

void from_json(const json& j, MinMaxGradient& g)
{
	g = MinMaxGradient();
	if (j.is_array())
	{
		g.ColorMax = ColorFrom(j, g.ColorMax);
		return;
	}
	if (!j.is_object())
		return;
	g.Mode = (ParticleGradientMode)std::clamp(j.value("mode", 0), 0, 4);
	g.ColorMin = ColorFrom(j.value("colorMin", json()), g.ColorMin);
	g.ColorMax = ColorFrom(j.value("colorMax", json()), g.ColorMax);
	if (j.contains("gradientMin")) g.GradientMin = j["gradientMin"].get<ParticleGradient>();
	if (j.contains("gradientMax")) g.GradientMax = j["gradientMax"].get<ParticleGradient>();
}
