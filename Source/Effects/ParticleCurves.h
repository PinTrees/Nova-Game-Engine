#pragma once
#include <vector>
#include <nlohmann/json.hpp>

// Particle System 이 쓰는 값 형식 (Unity 와 같은 이름·모드 번호)
//  - ParticleCurve  : AnimationCurve 의 단순판. 키(시간 0~1, 값) 사이를 부드럽게(Catmull-Rom 접선) 잇는다.
//  - ParticleGradient: Gradient. 색 키 + 알파 키, Blend / Fixed.
//  - MinMaxCurve    : Constant / Curve / Random Between Two Curves / Random Between Two Constants (+ 곡선 배율)
//  - MinMaxGradient : Color / Gradient / Random Between Two Colors / Random Between Two Gradients / Random Color

struct ParticleCurve
{
	struct Key { float Time = 0.0f; float Value = 0.0f; };
	std::vector<Key> Keys;   // 시간 순, 비어 있으면 0

	ParticleCurve() = default;
	ParticleCurve(std::initializer_list<Key> keys) : Keys(keys) {}
	static ParticleCurve Linear(float from, float to) { return ParticleCurve{ { 0.0f, from }, { 1.0f, to } }; }
	static ParticleCurve Constant(float v) { return ParticleCurve{ { 0.0f, v }, { 1.0f, v } }; }

	float Evaluate(float t) const;
	void Sort();
	// 곡선 값의 최소/최대 (편집기 그래프 범위)
	void Range(float& lo, float& hi) const;
};

struct ParticleGradient
{
	struct ColorKey { float Time = 0.0f; float R = 1.0f, G = 1.0f, B = 1.0f; };
	struct AlphaKey { float Time = 0.0f; float A = 1.0f; };
	std::vector<ColorKey> Colors = { { 0.0f, 1, 1, 1 }, { 1.0f, 1, 1, 1 } };
	std::vector<AlphaKey> Alphas = { { 0.0f, 1.0f }, { 1.0f, 1.0f } };
	int Mode = 0;   // 0 Blend, 1 Fixed

	Vec4 Evaluate(float t) const;
	void Sort();
};

enum class ParticleCurveMode { Constant = 0, Curve = 1, TwoCurves = 2, TwoConstants = 3 };
enum class ParticleGradientMode { Color = 0, Gradient = 1, TwoColors = 2, TwoGradients = 3, RandomColor = 4 };

struct MinMaxCurve
{
	ParticleCurveMode Mode = ParticleCurveMode::Constant;
	float ConstantMin = 0.0f;
	float ConstantMax = 0.0f;       // Constant 모드의 값
	float Multiplier = 1.0f;        // Curve 모드의 배율
	ParticleCurve CurveMin;
	ParticleCurve CurveMax = ParticleCurve::Linear(0.0f, 1.0f);

	MinMaxCurve() = default;
	MinMaxCurve(float constant) : ConstantMax(constant) {}
	static MinMaxCurve Curve(const ParticleCurve& c, float multiplier = 1.0f)
	{
		MinMaxCurve m;
		m.Mode = ParticleCurveMode::Curve;
		m.CurveMax = c;
		m.Multiplier = multiplier;
		return m;
	}

	// t = 정규화 시간(입자 나이 / 수명 또는 시스템 시간 / Duration), random = 입자마다 고정된 0~1 난수
	float Evaluate(float t, float random) const;
	bool IsConstantZero() const { return Mode == ParticleCurveMode::Constant && ConstantMax == 0.0f; }
	// 가능한 가장 큰 값 (경계 추정용)
	float MaxValue() const;
};

struct MinMaxGradient
{
	ParticleGradientMode Mode = ParticleGradientMode::Color;
	Vec4 ColorMin = Vec4(1, 1, 1, 1);
	Vec4 ColorMax = Vec4(1, 1, 1, 1);   // Color 모드의 값
	ParticleGradient GradientMin;
	ParticleGradient GradientMax;

	MinMaxGradient() = default;
	MinMaxGradient(const Vec4& c) : ColorMax(c) {}

	Vec4 Evaluate(float t, float random) const;
};

void to_json(nlohmann::json& j, const ParticleCurve& c);
void from_json(const nlohmann::json& j, ParticleCurve& c);
void to_json(nlohmann::json& j, const ParticleGradient& g);
void from_json(const nlohmann::json& j, ParticleGradient& g);
void to_json(nlohmann::json& j, const MinMaxCurve& c);
void from_json(const nlohmann::json& j, MinMaxCurve& c);
void to_json(nlohmann::json& j, const MinMaxGradient& g);
void from_json(const nlohmann::json& j, MinMaxGradient& g);
