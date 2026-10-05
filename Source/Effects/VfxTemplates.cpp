#include "pch.h"
#include "VfxAsset.h"

// 견본 에셋 (그래프 창의 New 메뉴 · nova vfx new --template). 값은 HDR — 1 보다 큰 색은 Bloom 으로 빛난다
namespace Vfx
{
	namespace
	{
		Block B(const char* type, json params = json::object(), json bind = json::object())
		{
			Block b;
			b.Type = type;
			b.Params = std::move(params);
			b.Bind = std::move(bind);
			return b;
		}

		System Sys(const char* name, int capacity, float rate)
		{
			System s;
			s.Name = name;
			s.Capacity = capacity;
			s.SpawnCtx.Rate = rate;
			s.SpawnCtx.Duration = 0.0f;   // 끝없이
			return s;
		}

		Output Out(Shape look, float intensity, Blend blend = Blend::Additive, Orient orient = Orient::FaceCamera, float stretch = 0.05f)
		{
			Output o;
			o.Look = look;
			o.Intensity = intensity;
			o.BlendMode = blend;
			o.Orientation = orient;
			o.Stretch = stretch;
			return o;
		}

		Property Prop(const char* name, PropertyType type, std::array<float, 4> value, float mn = 0.0f, float mx = 0.0f)
		{
			Property p;
			p.Name = name;
			p.Type = type;
			p.Value = value;
			p.Min = mn;
			p.Max = mx;
			return p;
		}

		const json kFade = json::array({ { 0.0, 1, 1, 1, 0 }, { 0.08, 1, 1, 1, 1 }, { 0.6, 1, 1, 1, 0.8 }, { 1.0, 1, 1, 1, 0 } });

		Asset SimpleLoop()
		{
			Asset a;
			System s = Sys("Particles", 4096, 60.0f);
			s.Initialize = {
				B("SetPosition", { { "Shape", "Sphere" }, { "Radius", 0.3 } }),
				B("SetVelocity", { { "Mode", "Cone" }, { "MinSpeed", 1.0 }, { "MaxSpeed", 2.5 }, { "Spread", 30 } }),
				B("SetLifetime", { { "Min", 1.5 }, { "Max", 2.5 } }),
				B("SetSize", { { "Min", 0.08 }, { "Max", 0.18 } }),
				B("SetColor", { { "Mode", "Random Between" }, { "ColorA", { 1.0, 0.55, 0.15, 1 } }, { "ColorB", { 1.0, 0.25, 0.6, 1 } }, { "Intensity", 3 } }),
			};
			s.Update = { B("Turbulence", { { "Intensity", 1.5 } }), B("ColorOverLife", { { "Gradient", kFade } }), B("SizeOverLife") };
			s.OutputCtx = Out(Shape::Glow, 1.0f);
			a.Systems.push_back(s);
			return a;
		}

		// 불꽃놀이: 로켓이 솟아 (Rocket) 죽을 때 GPU Event 로 폭발 (Explosion), 폭발 조각이 죽을 때 반짝임 (Crackle)
		Asset Fireworks()
		{
			Asset a;
			a.Properties = {
				Prop("Launch Rate", PropertyType::Float, { 1.6f }, 0.1f, 10.0f),
				Prop("Glow", PropertyType::Float, { 7.0f }, 0.0f, 30.0f),
				Prop("Launch Area", PropertyType::Float, { 4.0f }, 0.0f, 30.0f),
			};
			System rocket = Sys("Rocket", 128, 1.6f);
			rocket.Initialize = {
				B("SetPosition", { { "Shape", "Circle" }, { "Radius", 4.0 } }, { { "Radius", "Launch Area" } }),
				B("SetVelocity", { { "Mode", "Cone" }, { "MinSpeed", 12.0 }, { "MaxSpeed", 16.0 }, { "Spread", 10 } }),
				B("SetLifetime", { { "Min", 1.3 }, { "Max", 1.8 } }),
				B("SetSize", { { "Min", 0.18 }, { "Max", 0.22 } }),
				B("SetColor", { { "Mode", "Rainbow" }, { "Saturation", 0.85 }, { "Brightness", 1.0 }, { "Intensity", 3 } }),
			};
			rocket.SpawnCtx.RateBind = "Launch Rate";
			rocket.Update = { B("Gravity", { { "Force", { 0, -6.0, 0 } } }) };
			rocket.OutputCtx = Out(Shape::Spark, 3.0f, Blend::Additive, Orient::AlongVelocity, 0.06f);
			rocket.OutputCtx.Trail = true;   // 솟는 로켓 뒤의 빛 꼬리
			rocket.OutputCtx.TrailPoints = 16;
			rocket.OutputCtx.TrailLength = 0.45f;
			rocket.OutputCtx.TrailWidth = 0.7f;
			// 로켓이 날아가는 동안 흘리는 불티 (Trigger Event Rate — 살아 있는 동안 초당 40 번)
			System sparkle = Sys("Rocket Sparks", 6000, 0.0f);
			sparkle.SpawnCtx.Parent = "Rocket";
			sparkle.SpawnCtx.Trigger = EventTrigger::Rate;
			sparkle.SpawnCtx.EventRate = 40.0f;
			sparkle.SpawnCtx.CountPerEvent = 1;
			sparkle.Initialize = {
				B("InheritSource", { { "Velocity", 0.1 }, { "Color", true } }),
				B("SetVelocity", { { "Mode", "Random" }, { "MinSpeed", 0.3 }, { "MaxSpeed", 1.2 } }),
				B("SetLifetime", { { "Min", 0.3 }, { "Max", 0.8 } }),
				B("SetSize", { { "Min", 0.03 }, { "Max", 0.06 } }),
			};
			sparkle.Update = { B("Gravity", { { "Force", { 0, -4.0, 0 } } }), B("ColorOverLife", { { "Gradient", kFade } }) };
			sparkle.OutputCtx = Out(Shape::Glow, 4.0f);
			System boom = Sys("Explosion", 150000, 0.0f);
			boom.SpawnCtx.Parent = "Rocket";
			boom.SpawnCtx.CountPerEvent = 900;
			boom.Initialize = {
				B("InheritSource", { { "Velocity", 0.15 }, { "Color", true } }),
				B("SetVelocity", { { "Mode", "Random" }, { "MinSpeed", 7.0 }, { "MaxSpeed", 10.0 } }),
				B("SetLifetime", { { "Min", 1.4 }, { "Max", 2.4 } }),
				B("SetSize", { { "Min", 0.05 }, { "Max", 0.09 } }),
			};
			boom.Update = {
				B("Gravity", { { "Force", { 0, -2.5, 0 } } }),
				B("Drag", { { "Coefficient", 1.1 } }),
				B("ColorOverLife", { { "Gradient", json::array({ { 0.0, 3, 3, 3, 1 }, { 0.15, 1, 1, 1, 1 }, { 0.75, 1, 0.8, 0.6, 0.8 }, { 1.0, 1, 0.4, 0.2, 0 } }) } }),
			};
			boom.OutputCtx = Out(Shape::Spark, 7.0f, Blend::Additive, Orient::AlongVelocity, 0.09f);
			System crackle = Sys("Crackle", 60000, 0.0f);
			crackle.SpawnCtx.Parent = "Explosion";
			crackle.SpawnCtx.CountPerEvent = 1;
			crackle.Initialize = {
				B("InheritSource", { { "Velocity", 0.3 }, { "Color", false } }),
				B("SetLifetime", { { "Min", 0.15 }, { "Max", 0.45 } }),
				B("SetSize", { { "Min", 0.12 }, { "Max", 0.22 } }),
				B("SetColor", { { "ColorA", { 1.0, 0.95, 0.8, 1 } }, { "Intensity", 8 } }),
				B("SetAngle", { { "AngleMin", 0 }, { "AngleMax", 90 } }),
			};
			crackle.Update = { B("Gravity", { { "Force", { 0, -1.0, 0 } } }), B("ColorOverLife", { { "Gradient", json::array({ { 0.0, 1, 1, 1, 1 }, { 0.3, 1, 1, 1, 0.2 }, { 0.5, 1, 1, 1, 1 }, { 1.0, 1, 1, 1, 0 } }) } }) };
			crackle.OutputCtx = Out(Shape::Sparkle, 1.0f);
			a.Systems = { rocket, sparkle, boom, crackle };
			a.Systems[2].Initialize[3].Bind = { { "Intensity", "Glow" } };   // Glow 속성 → 반짝임 세기
			return a;
		}

		// 마법진: 바닥의 빛나는 원 두 겹 (반대로 돈다) + 떠오르는 룬 + 가운데 빛기둥
		Asset MagicCircle()
		{
			Asset a;
			a.Properties = {
				Prop("Main Color", PropertyType::Color, { 0.25f, 0.7f, 1.0f, 1.0f }),
				Prop("Accent Color", PropertyType::Color, { 0.75f, 0.35f, 1.0f, 1.0f }),
				Prop("Radius", PropertyType::Float, { 3.0f }, 0.5f, 10.0f),
				Prop("Spin", PropertyType::Float, { 35.0f }, -360.0f, 360.0f),
			};
			System outer = Sys("Outer Ring", 30000, 6000.0f);
			outer.Initialize = {
				B("SetPosition", { { "Shape", "Circle" }, { "Radius", 3.0 }, { "Surface", true } }, { { "Radius", "Radius" } }),
				B("SetVelocity", { { "Mode", "Direction" }, { "MinSpeed", 0.0 }, { "MaxSpeed", 0.25 } }),
				B("SetLifetime", { { "Min", 0.8 }, { "Max", 1.6 } }),
				B("SetSize", { { "Min", 0.04 }, { "Max", 0.09 } }),
				B("SetColor", { { "ColorA", { 0.25, 0.7, 1.0, 1 } }, { "Intensity", 5 } }, { { "ColorA", "Main Color" } }),
			};
			outer.Update = { B("Orbit", { { "Speed", 35 } }, { { "Speed", "Spin" } }), B("ColorOverLife", { { "Gradient", kFade } }) };
			outer.OutputCtx = Out(Shape::Glow, 1.0f);

			System inner = Sys("Inner Ring", 20000, 3500.0f);
			inner.Initialize = {
				B("SetPosition", { { "Shape", "Torus" }, { "Radius", 2.0 }, { "Thickness", 0.05 } }),
				B("SetLifetime", { { "Min", 0.6 }, { "Max", 1.2 } }),
				B("SetSize", { { "Min", 0.03 }, { "Max", 0.07 } }),
				B("SetColor", { { "ColorA", { 0.75, 0.35, 1.0, 1 } }, { "Intensity", 6 } }, { { "ColorA", "Accent Color" } }),
			};
			inner.Update = { B("Orbit", { { "Speed", -60 } }), B("ColorOverLife", { { "Gradient", kFade } }) };
			inner.OutputCtx = Out(Shape::Glow, 1.0f);

			System glyph = Sys("Glyph", 16, 3.0f);
			glyph.Initialize = {
				B("SetLifetime", { { "Min", 1.2 }, { "Max", 1.2 } }),
				B("SetSize", { { "Min", 6.6 }, { "Max", 6.6 } }),
				B("SetColor", { { "ColorA", { 0.25, 0.7, 1.0, 0.5 } }, { "Intensity", 2.5 } }, { { "ColorA", "Main Color" } }),
				B("SetAngle", { { "AngleMin", 0 }, { "AngleMax", 360 }, { "SpinMin", 20 }, { "SpinMax", 20 } }),
			};
			glyph.Update = { B("ColorOverLife", { { "Gradient", json::array({ { 0.0, 1, 1, 1, 0 }, { 0.5, 1, 1, 1, 0.6 }, { 1.0, 1, 1, 1, 0 } }) } }) };
			glyph.OutputCtx = Out(Shape::Ring, 1.0f, Blend::Additive, Orient::Horizontal);

			System runes = Sys("Runes", 512, 40.0f);
			runes.Initialize = {
				B("SetPosition", { { "Shape", "Circle" }, { "Radius", 2.6 }, { "Surface", true } }),
				B("SetVelocity", { { "Mode", "Direction" }, { "MinSpeed", 0.4 }, { "MaxSpeed", 0.9 } }),
				B("SetLifetime", { { "Min", 1.5 }, { "Max", 2.5 } }),
				B("SetSize", { { "Min", 0.25 }, { "Max", 0.45 } }),
				B("SetColor", { { "Mode", "Random Between" }, { "ColorA", { 0.3, 0.8, 1.0, 1 } }, { "ColorB", { 0.8, 0.4, 1.0, 1 } }, { "Intensity", 4 } }),
				B("SetAngle", { { "AngleMin", 0 }, { "AngleMax", 360 }, { "SpinMin", -90 }, { "SpinMax", 90 } }),
			};
			runes.Update = { B("Orbit", { { "Speed", 35 } }, { { "Speed", "Spin" } }), B("ColorOverLife", { { "Gradient", kFade } }) };
			runes.OutputCtx = Out(Shape::Star, 1.0f);

			System pillar = Sys("Pillar", 20000, 2500.0f);
			pillar.Initialize = {
				B("SetPosition", { { "Shape", "Circle" }, { "Radius", 0.6 } }),
				B("SetVelocity", { { "Mode", "Direction" }, { "MinSpeed", 2.0 }, { "MaxSpeed", 5.0 } }),
				B("SetLifetime", { { "Min", 0.8 }, { "Max", 1.4 } }),
				B("SetSize", { { "Min", 0.05 }, { "Max", 0.12 } }),
				B("SetColor", { { "ColorA", { 0.5, 0.85, 1.0, 1 } }, { "Intensity", 5 } }),
			};
			pillar.Update = {
				B("Vortex", { { "Speed", 8 }, { "Pull", 3 } }),
				B("Turbulence", { { "Intensity", 1.0 }, { "Frequency", 1.2 } }),
				B("ColorOverLife", { { "Gradient", kFade } }),
				B("SizeOverLife", { { "Curve", json::array({ { 0, 1 }, { 1, 0 } }) } }),
			};
			pillar.OutputCtx = Out(Shape::Glow, 1.0f);
			a.Systems = { glyph, outer, inner, runes, pillar };
			return a;
		}

		// 토네이도: 축 둘레로 돌며 (Orbit) 올라갈수록 벌어지는 깔때기 + 빨려 드는 파편 + 바닥 먼지
		Asset Tornado()
		{
			Asset a;
			a.Properties = { Prop("Spin", PropertyType::Float, { 260.0f }, 0.0f, 900.0f), Prop("Dust Color", PropertyType::Color, { 0.6f, 0.55f, 0.5f, 0.45f }) };
			System funnel = Sys("Funnel", 24000, 7000.0f);   // 용량 ≈ 초당 수 × 가장 긴 수명 (정렬은 용량만큼 — 넉넉하면 그만큼 느리다)
			funnel.Initialize = {
				B("SetPosition", { { "Shape", "Circle" }, { "Radius", 0.35 }, { "Surface", true } }),
				B("SetVelocity", { { "Mode", "From Shape" }, { "MinSpeed", 0.35 }, { "MaxSpeed", 0.7 } }),
				B("SetVelocity", { { "Mode", "Direction" }, { "MinSpeed", 3.0 }, { "MaxSpeed", 4.5 } }),
				B("SetLifetime", { { "Min", 2.5 }, { "Max", 3.2 } }),
				B("SetSize", { { "Min", 0.3 }, { "Max", 0.6 } }),
				B("SetColor", { { "ColorA", { 0.6, 0.55, 0.5, 0.45 } } }, { { "ColorA", "Dust Color" } }),
				B("SetAngle", { { "SpinMin", -90 }, { "SpinMax", 90 } }),
			};
			funnel.Update = {
				B("Orbit", { { "Speed", 260 } }, { { "Speed", "Spin" } }),
				B("Turbulence", { { "Intensity", 0.8 }, { "Frequency", 0.7 } }),
				B("ColorOverLife", { { "Gradient", kFade } }),
				B("SizeOverLife", { { "Curve", json::array({ { 0, 0.5 }, { 1, 2.2 } }) } }),
			};
			funnel.OutputCtx = Out(Shape::Smoke, 1.0f, Blend::Alpha);
			System debris = Sys("Debris", 2500, 500.0f);
			debris.Initialize = {
				B("SetPosition", { { "Shape", "Circle" }, { "Radius", 3.0 } }),
				B("SetVelocity", { { "Mode", "From Shape" }, { "MinSpeed", -0.6 }, { "MaxSpeed", -0.2 } }),
				B("SetVelocity", { { "Mode", "Direction" }, { "MinSpeed", 1.5 }, { "MaxSpeed", 4.0 } }),
				B("SetLifetime", { { "Min", 2.5 }, { "Max", 4.0 } }),
				B("SetSize", { { "Min", 0.04 }, { "Max", 0.1 } }),
				B("SetColor", { { "ColorA", { 1.0, 0.75, 0.45, 1 } }, { "Intensity", 4 } }),
			};
			debris.Update = { B("Orbit", { { "Speed", 200 } }), B("ColorOverLife", { { "Gradient", kFade } }) };
			debris.OutputCtx = Out(Shape::Spark, 1.0f, Blend::Additive, Orient::AlongVelocity, 0.03f);
			System ground = Sys("Ground Dust", 5000, 1800.0f);
			ground.Initialize = {
				B("SetPosition", { { "Shape", "Circle" }, { "Radius", 3.5 } }),
				B("SetVelocity", { { "Mode", "From Shape" }, { "MinSpeed", 0.3 }, { "MaxSpeed", 1.0 } }),
				B("SetVelocity", { { "Mode", "Direction" }, { "MinSpeed", 0.0 }, { "MaxSpeed", 0.5 } }),
				B("SetLifetime", { { "Min", 1.5 }, { "Max", 2.5 } }),
				B("SetSize", { { "Min", 0.6 }, { "Max", 1.2 } }),
				B("SetColor", { { "ColorA", { 0.5, 0.45, 0.4, 0.15 } } }),
			};
			ground.Update = { B("Orbit", { { "Speed", 120 } }), B("ColorOverLife", { { "Gradient", kFade } }) };
			ground.OutputCtx = Out(Shape::Smoke, 1.0f, Blend::Alpha);
			a.Systems = { ground, funnel, debris };
			return a;
		}

		// 불티: 그라인더 불꽃 (바닥에 튕긴다) + 번쩍임 + 연기
		Asset Sparks()
		{
			Asset a;
			a.Properties = { Prop("Direction", PropertyType::Vector3, { 1.0f, 0.5f, 0.0f, 0.0f }), Prop("Heat", PropertyType::Float, { 14.0f }, 0.0f, 40.0f) };
			System sparks = Sys("Sparks", 20000, 1400.0f);
			sparks.Initialize = {
				B("SetPosition", { { "Shape", "Sphere" }, { "Radius", 0.05 }, { "Center", { 0, 1, 0 } } }),
				B("SetVelocity", { { "Mode", "Cone" }, { "Direction", { 1, 0.5, 0 } }, { "MinSpeed", 5.0 }, { "MaxSpeed", 12.0 }, { "Spread", 28 } }, { { "Direction", "Direction" } }),
				B("SetLifetime", { { "Min", 0.6 }, { "Max", 1.6 } }),
				B("SetSize", { { "Min", 0.03 }, { "Max", 0.06 } }),
				B("SetColor", { { "ColorA", { 1.0, 0.6, 0.2, 1 } }, { "Intensity", 14 } }, { { "Intensity", "Heat" } }),
			};
			sparks.Update = {
				B("Gravity"),
				B("CollidePlane", { { "Bounce", 0.35 }, { "Friction", 0.15 }, { "LifetimeLoss", 0.1 } }),
				B("ColorOverLife", { { "Gradient", json::array({ { 0.0, 1.5, 1.4, 1.2, 1 }, { 0.4, 1, 0.6, 0.25, 1 }, { 1.0, 0.8, 0.15, 0.05, 0 } }) } }),
			};
			sparks.OutputCtx = Out(Shape::Spark, 1.0f, Blend::Additive, Orient::AlongVelocity, 0.045f);
			System flash = Sys("Flash", 64, 25.0f);
			flash.Initialize = {
				B("SetPosition", { { "Shape", "Point" }, { "Center", { 0, 1, 0 } } }),
				B("SetLifetime", { { "Min", 0.05 }, { "Max", 0.1 } }),
				B("SetSize", { { "Min", 0.5 }, { "Max", 0.8 } }),
				B("SetColor", { { "ColorA", { 1.0, 0.75, 0.4, 1 } }, { "Intensity", 2 } }),
			};
			flash.OutputCtx = Out(Shape::Glow, 1.0f);
			System smoke = Sys("Smoke", 2000, 40.0f);
			smoke.Initialize = {
				B("SetPosition", { { "Shape", "Sphere" }, { "Radius", 0.1 }, { "Center", { 0, 1, 0 } } }),
				B("SetVelocity", { { "Mode", "Direction" }, { "MinSpeed", 0.3 }, { "MaxSpeed", 0.8 } }),
				B("SetLifetime", { { "Min", 2.0 }, { "Max", 3.0 } }),
				B("SetSize", { { "Min", 0.3 }, { "Max", 0.5 } }),
				B("SetColor", { { "ColorA", { 0.35, 0.33, 0.32, 0.25 } } }),
			};
			smoke.Update = { B("Turbulence", { { "Intensity", 0.8 } }), B("ColorOverLife", { { "Gradient", kFade } }), B("SizeOverLife", { { "Curve", json::array({ { 0, 0.5 }, { 1, 3 } }) } }) };
			smoke.OutputCtx = Out(Shape::Smoke, 1.0f, Blend::Alpha);
			a.Systems = { smoke, sparks, flash };
			return a;
		}

		// 은하: 나선 팔 별 18 만 개 (멀수록 느리게 도는 차등 회전) + 먼지 띠 + 은은한 핵
		Asset Galaxy()
		{
			Asset a;
			a.Properties = { Prop("Spin", PropertyType::Float, { 40.0f }, -180.0f, 180.0f), Prop("Arm Color", PropertyType::Color, { 0.45f, 0.6f, 1.0f, 1.0f }) };
			System stars = Sys("Stars", 160000, 24000.0f);
			stars.Initialize = {
				B("SetPositionSpiral", { { "Arms", 3 }, { "Radius", 7.0 }, { "Twist", 0.9 }, { "Spread", 0.45 }, { "Thickness", 0.5 } }),
				B("SetLifetime", { { "Min", 5.0 }, { "Max", 6.0 } }),
				B("SetSize", { { "Min", 0.02 }, { "Max", 0.06 } }),
				B("SetColor", { { "Mode", "Random Between" }, { "ColorA", { 0.45, 0.6, 1.0, 1 } }, { "ColorB", { 1.0, 0.75, 0.9, 1 } }, { "Intensity", 2.5 } }, { { "ColorA", "Arm Color" } }),
			};
			stars.Update = {
				B("Orbit", { { "Speed", 40 }, { "Falloff", 0.5 } }, { { "Speed", "Spin" } }),
				B("ColorOverLife", { { "Gradient", kFade } }),
			};
			stars.OutputCtx = Out(Shape::Glow, 1.0f);
			System dust = Sys("Dust Lanes", 6000, 1200.0f);
			dust.Initialize = {
				B("SetPositionSpiral", { { "Arms", 3 }, { "Radius", 6.0 }, { "Twist", 0.9 }, { "Spread", 0.25 }, { "Thickness", 0.2 } }),
				B("SetLifetime", { { "Min", 4.0 }, { "Max", 5.0 } }),
				B("SetSize", { { "Min", 0.4 }, { "Max", 0.9 } }),
				B("SetColor", { { "ColorA", { 0.75, 0.4, 0.9, 0.1 } }, { "Intensity", 1.0 } }),
			};
			dust.Update = { B("Orbit", { { "Speed", 40 }, { "Falloff", 0.5 } }, { { "Speed", "Spin" } }), B("ColorOverLife", { { "Gradient", kFade } }) };
			dust.OutputCtx = Out(Shape::Smoke, 1.0f);
			System core = Sys("Core", 3000, 800.0f);
			core.Initialize = {
				B("SetPosition", { { "Shape", "Sphere" }, { "Radius", 0.7 } }),
				B("SetLifetime", { { "Min", 1.5 }, { "Max", 2.5 } }),
				B("SetSize", { { "Min", 0.06 }, { "Max", 0.16 } }),
				B("SetColor", { { "ColorA", { 1.0, 0.85, 0.6, 1 } }, { "Intensity", 1.2 } }),
			};
			core.Update = { B("Orbit", { { "Speed", 70 } }), B("ColorOverLife", { { "Gradient", kFade } }) };
			core.OutputCtx = Out(Shape::Glow, 1.0f);
			a.Systems = { dust, stars, core };
			return a;
		}

		// 모닥불: 불꽃 (노랑 → 주황 → 빨강, 가라앉은 세기 — 겹쳐도 하얗게 타지 않게) + 불티 + 연기
		Asset Fire()
		{
			Asset a;
			a.Properties = { Prop("Flame Color", PropertyType::Color, { 1.0f, 0.42f, 0.08f, 1.0f }), Prop("Size", PropertyType::Float, { 0.45f }, 0.1f, 3.0f) };
			System flames = Sys("Flames", 4000, 360.0f);
			flames.Initialize = {
				B("SetPosition", { { "Shape", "Circle" }, { "Radius", 0.4 } }),
				B("SetVelocity", { { "Mode", "Direction" }, { "MinSpeed", 1.0 }, { "MaxSpeed", 1.8 } }),
				B("SetLifetime", { { "Min", 0.7 }, { "Max", 1.1 } }),
				B("SetSize", { { "Min", 0.4 }, { "Max", 0.65 } }, { { "Max", "Size" } }),
				B("SetColor", { { "ColorA", { 1.0, 0.42, 0.08, 1 } }, { "Intensity", 1.0 } }, { { "ColorA", "Flame Color" } }),
				B("SetAngle", { { "SpinMin", -45 }, { "SpinMax", 45 } }),
			};
			flames.Update = {
				B("Turbulence", { { "Intensity", 2.0 }, { "Frequency", 1.4 }, { "Scroll", { 0, 1.5, 0 } } }),
				B("Attractor", { { "Position", { 0, 2.0, 0 } }, { "Strength", 1.2 }, { "Drag", 0.3 } }),
				B("ColorOverLife", { { "Mode", "Multiply" }, { "Gradient", json::array({ { 0.0, 1.6, 1.5, 1.2, 0 }, { 0.1, 1.5, 1.2, 0.9, 0.75 }, { 0.45, 1, 0.6, 0.4, 0.55 }, { 1.0, 0.5, 0.15, 0.08, 0 } }) } }),
				B("SizeOverLife", { { "Curve", json::array({ { 0, 0.7 }, { 0.3, 1 }, { 1, 0.15 } }) } }),
			};
			flames.OutputCtx = Out(Shape::Glow, 1.1f);
			System core = Sys("Core", 600, 120.0f);
			core.Initialize = {
				B("SetPosition", { { "Shape", "Circle" }, { "Radius", 0.25 } }),
				B("SetVelocity", { { "Mode", "Direction" }, { "MinSpeed", 0.6 }, { "MaxSpeed", 1.0 } }),
				B("SetLifetime", { { "Min", 0.3 }, { "Max", 0.5 } }),
				B("SetSize", { { "Min", 0.2 }, { "Max", 0.3 } }),
				B("SetColor", { { "ColorA", { 1.0, 0.8, 0.45, 1 } }, { "Intensity", 0.8 } }),
			};
			core.Update = { B("ColorOverLife", { { "Gradient", kFade } }) };
			core.OutputCtx = Out(Shape::Glow, 1.0f);
			System embers = Sys("Embers", 2000, 60.0f);
			embers.Initialize = {
				B("SetPosition", { { "Shape", "Circle" }, { "Radius", 0.4 } }),
				B("SetVelocity", { { "Mode", "Cone" }, { "MinSpeed", 1.5 }, { "MaxSpeed", 3.5 }, { "Spread", 25 } }),
				B("SetLifetime", { { "Min", 1.5 }, { "Max", 3.0 } }),
				B("SetSize", { { "Min", 0.02 }, { "Max", 0.04 } }),
				B("SetColor", { { "ColorA", { 1.0, 0.5, 0.15, 1 } }, { "Intensity", 8 } }),
			};
			embers.Update = { B("Turbulence", { { "Intensity", 3 }, { "Frequency", 0.9 } }), B("ColorOverLife", { { "Gradient", kFade } }) };
			embers.OutputCtx = Out(Shape::Spark, 1.0f, Blend::Additive, Orient::AlongVelocity, 0.05f);
			System smoke = Sys("Smoke", 1500, 30.0f);
			smoke.Initialize = {
				B("SetPosition", { { "Shape", "Circle" }, { "Radius", 0.3 }, { "Center", { 0, 1.4, 0 } } }),
				B("SetVelocity", { { "Mode", "Direction" }, { "MinSpeed", 0.6 }, { "MaxSpeed", 1.0 } }),
				B("SetLifetime", { { "Min", 3.0 }, { "Max", 4.0 } }),
				B("SetSize", { { "Min", 0.5 }, { "Max", 0.8 } }),
				B("SetColor", { { "ColorA", { 0.2, 0.19, 0.18, 0.3 } } }),
			};
			smoke.Update = { B("Turbulence", { { "Intensity", 0.7 } }), B("ColorOverLife", { { "Gradient", kFade } }), B("SizeOverLife", { { "Curve", json::array({ { 0, 0.6 }, { 1, 3.5 } }) } }) };
			smoke.OutputCtx = Out(Shape::Smoke, 1.0f, Blend::Alpha);
			a.Systems = { smoke, flames, core, embers };
			return a;
		}

		// 포털: 세워진 (XY) 고리 테두리가 돌고, 둘레의 빛이 고리 가운데로 소용돌이치며 빨려 든다
		Asset Portal()
		{
			Asset a;
			a.Properties = { Prop("Portal Color", PropertyType::Color, { 0.3f, 1.0f, 0.6f, 1.0f }) };
			System rim = Sys("Rim", 30000, 9000.0f);
			rim.Initialize = {
				B("SetPosition", { { "Shape", "Torus" }, { "Radius", 2.0 }, { "Thickness", 0.06 }, { "Center", { 0, 2.2, 0 } }, { "Plane", "XY (Facing Z)" } }),
				B("SetLifetime", { { "Min", 0.4 }, { "Max", 0.9 } }),
				B("SetSize", { { "Min", 0.03 }, { "Max", 0.08 } }),
				B("SetColor", { { "ColorA", { 0.3, 1.0, 0.6, 1 } }, { "Intensity", 5 } }, { { "ColorA", "Portal Color" } }),
			};
			rim.Update = { B("Orbit", { { "Center", { 0, 2.2, 0 } }, { "Speed", 160 }, { "Axis", { 0, 0, 1 } } }), B("ColorOverLife", { { "Gradient", kFade } }) };
			rim.OutputCtx = Out(Shape::Glow, 1.0f);
			System swirl = Sys("Swirl", 30000, 7000.0f);
			swirl.Initialize = {
				B("SetPosition", { { "Shape", "Circle" }, { "Radius", 2.0 }, { "Surface", true }, { "Center", { 0, 2.2, 0 } }, { "Plane", "XY (Facing Z)" } }),
				B("SetLifetime", { { "Min", 1.2 }, { "Max", 1.8 } }),
				B("SetSize", { { "Min", 0.02 }, { "Max", 0.05 } }),
				B("SetColor", { { "Mode", "Random Between" }, { "ColorA", { 0.3, 1.0, 0.6, 1 } }, { "ColorB", { 0.2, 0.5, 1.0, 1 } }, { "Intensity", 4 } }),
			};
			swirl.Update = {
				B("Orbit", { { "Center", { 0, 2.2, 0 } }, { "Speed", 120 }, { "Axis", { 0, 0, 1 } }, { "Falloff", 1.0 } }),
				B("Attractor", { { "Position", { 0, 2.2, 0 } }, { "Strength", 1.2 }, { "Drag", 2.0 } }),
				B("ColorOverLife", { { "Gradient", kFade } }),
			};
			swirl.OutputCtx = Out(Shape::Glow, 1.0f);
			System core = Sys("Core", 200, 40.0f);
			core.Initialize = {
				B("SetPosition", { { "Shape", "Point" }, { "Center", { 0, 2.2, 0 } } }),
				B("SetLifetime", { { "Min", 1.0 }, { "Max", 1.0 } }),
				B("SetSize", { { "Min", 3.6 }, { "Max", 3.6 } }),
				B("SetColor", { { "ColorA", { 0.2, 0.8, 0.5, 0.12 } }, { "Intensity", 1.0 } }),
			};
			core.Update = { B("ColorOverLife", { { "Gradient", kFade } }) };
			core.OutputCtx = Out(Shape::SoftDot, 1.0f);
			a.Systems = { core, swirl, rim };
			return a;
		}

		// 에너지 소용돌이: 꼬리만 그리는 빛줄기가 가운데 둘레를 휘감는다 (Output 의 Trail Only)
		Asset EnergySwirl()
		{
			Asset a;
			a.Properties = { Prop("Core Color", PropertyType::Color, { 0.35f, 0.6f, 1.0f, 1.0f }), Prop("Swirl", PropertyType::Float, { 9.0f }, 0.0f, 40.0f) };
			System ribbons = Sys("Ribbons", 800, 90.0f);
			ribbons.Initialize = {
				B("SetPosition", { { "Shape", "Sphere" }, { "Radius", 2.2 }, { "Surface", true }, { "Center", { 0, 1.5, 0 } } }),
				B("SetVelocity", { { "Mode", "Random" }, { "MinSpeed", 1.5 }, { "MaxSpeed", 3.0 } }),
				B("SetLifetime", { { "Min", 2.0 }, { "Max", 3.0 } }),
				B("SetSize", { { "Min", 0.12 }, { "Max", 0.2 } }),
				B("SetColor", { { "Mode", "Random Between" }, { "ColorA", { 0.35, 0.6, 1.0, 1 } }, { "ColorB", { 0.8, 0.4, 1.0, 1 } }, { "Intensity", 5 } }, { { "ColorA", "Core Color" } }),
			};
			ribbons.Update = {
				B("Vortex", { { "Speed", 9 }, { "Center", { 0, 1.5, 0 } }, { "Pull", 2 } }, { { "Speed", "Swirl" } }),
				B("Attractor", { { "Position", { 0, 1.5, 0 } }, { "Strength", 2.5 }, { "Radius", 1.6 }, { "Drag", 0.6 } }),
				B("ColorOverLife", { { "Gradient", kFade } }),
			};
			ribbons.OutputCtx = Out(Shape::Glow, 1.0f);
			ribbons.OutputCtx.Trail = true;
			ribbons.OutputCtx.TrailOnly = true;
			ribbons.OutputCtx.TrailPoints = 24;
			ribbons.OutputCtx.TrailLength = 0.6f;
			ribbons.OutputCtx.TrailWidth = 0.6f;
			System core = Sys("Core", 400, 120.0f);
			core.Initialize = {
				B("SetPosition", { { "Shape", "Sphere" }, { "Radius", 0.3 }, { "Center", { 0, 1.5, 0 } } }),
				B("SetLifetime", { { "Min", 0.5 }, { "Max", 0.8 } }),
				B("SetSize", { { "Min", 0.6 }, { "Max", 1.0 } }),
				B("SetColor", { { "ColorA", { 0.35, 0.6, 1.0, 1 } }, { "Intensity", 1.2 } }, { { "ColorA", "Core Color" } }),
			};
			core.Update = { B("ColorOverLife", { { "Gradient", kFade } }) };
			core.OutputCtx = Out(Shape::Glow, 1.0f);
			a.Systems = { core, ribbons };
			return a;
		}

		OperatorNode Op(int id, const char* type, json params, json inputs, float x, float y)
		{
			OperatorNode n;
			n.Id = id;
			n.Type = type;
			n.Params = std::move(params);
			n.Inputs = std::move(inputs);
			n.X = x;
			n.Y = y;
			return n;
		}

		// 무지개 나선 (연산 노드 예): 색 = HSV(시간 × 0.15 + 파티클마다 무작위), 도는 빠르기 = sin(시간) × 220 — 나선이 번갈아 거꾸로 돈다
		Asset RainbowSpiral()
		{
			Asset a;
			a.Properties = { Prop("Hue Speed", PropertyType::Float, { 0.15f }, 0.0f, 2.0f), Prop("Swirl", PropertyType::Float, { 220.0f }, 0.0f, 720.0f) };
			System s = Sys("Spiral", 8000, 1800.0f);
			s.Initialize = {
				B("SetPosition", { { "Shape", "Circle" }, { "Radius", 0.25 }, { "Surface", true } }),
				B("SetVelocity", { { "Mode", "From Shape" }, { "MinSpeed", 0.6 }, { "MaxSpeed", 1.0 } }),
				B("SetVelocity", { { "Mode", "Direction" }, { "MinSpeed", 3.0 }, { "MaxSpeed", 4.5 } }),
				B("SetLifetime", { { "Min", 1.6 }, { "Max", 2.4 } }),
				B("SetSize", { { "Min", 0.04 }, { "Max", 0.08 } }),
				B("SetColor", { { "ColorA", { 1, 1, 1, 1 } }, { "Intensity", 2.2 } }),
			};
			s.Initialize[5].Links = { { "ColorA", 6 } };
			s.Update = {
				B("Orbit", { { "Speed", 180 } }),
				B("Gravity", { { "Force", { 0, -1.5, 0 } } }),
				B("ColorOverLife", { { "Gradient", kFade } }),
				B("SizeOverLife", { { "Curve", json::array({ { 0, 0.4 }, { 0.2, 1 }, { 1, 0.2 } }) } }),
			};
			s.Update[0].Links = { { "Speed", 9 } };
			s.OutputCtx = Out(Shape::Glow, 1.0f);
			s.OutputCtx.Trail = true;
			s.OutputCtx.TrailPoints = 8;
			s.OutputCtx.TrailLength = 0.25f;
			s.OutputCtx.TrailWidth = 0.8f;
			s.Editor = { { "x", 0.0f }, { "y", 0.0f } };
			a.Systems = { s };
			a.Operators = {
				Op(1, "Time", json::object(), json::object(), -720, 40),
				Op(10, "Property", { { "Name", "Hue Speed" } }, json::object(), -720, 140),
				Op(2, "Multiply", json::object(), { { "A", 1 }, { "B", 10 } }, -500, 60),
				Op(3, "RandomPerParticle", { { "Min", 0.0 }, { "Max", 0.25 } }, json::object(), -500, 200),
				Op(4, "Add", json::object(), { { "A", 2 }, { "B", 3 } }, -300, 100),
				Op(5, "Fractional", json::object(), { { "X", 4 } }, -300, 230),
				Op(6, "HSVToRGB", { { "S", 0.85 }, { "V", 1.0 } }, { { "H", 5 } }, -300, 330),
				Op(7, "Sine", json::object(), { { "X", 1 } }, -500, 420),
				Op(11, "Property", { { "Name", "Swirl" } }, json::object(), -720, 520),
				Op(9, "Multiply", json::object(), { { "A", 7 }, { "B", 11 } }, -300, 480),
			};
			return a;
		}

		// 파편 (Output Mesh + 깊이 버퍼 충돌): 2.5 초마다 돌 조각이 터져 바닥 · 장면의 물체에 튕기고 굴러 멈춘다, 먼지와 함께
		Asset Debris()
		{
			Asset a;
			System shards = Sys("Shards", 1500, 0.0f);
			shards.SpawnCtx.Duration = 2.5f;
			shards.SpawnCtx.Bursts = { { 0.0f, 140, 1, 1.0f } };
			shards.Initialize = {
				B("SetPosition", { { "Shape", "Sphere" }, { "Radius", 0.3 }, { "Center", { 0, 0.3, 0 } } }),
				B("SetVelocity", { { "Mode", "Cone" }, { "MinSpeed", 3.5 }, { "MaxSpeed", 8.0 }, { "Spread", 45 } }),
				B("SetLifetime", { { "Min", 3.0 }, { "Max", 4.5 } }),
				B("SetSize", { { "Min", 0.07 }, { "Max", 0.2 } }),
				B("SetColor", { { "Mode", "Random Between" }, { "ColorA", { 0.62, 0.56, 0.5, 1 } }, { "ColorB", { 0.32, 0.29, 0.27, 1 } } }),
				B("SetAngle", { { "AngleMin", 0 }, { "AngleMax", 360 }, { "SpinMin", -400 }, { "SpinMax", 400 } }),
			};
			shards.Update = {
				B("Gravity"),
				B("CollideDepth", { { "Bounce", 0.3 }, { "Friction", 0.5 }, { "Thickness", 0.8 } }),
				B("CollidePlane", { { "Bounce", 0.3 }, { "Friction", 0.5 } }),   // 화면 밖 (깊이가 없는 곳) 에서도 바닥
				B("Drag", { { "Coefficient", 0.15 } }),
				B("SizeOverLife", { { "Curve", json::array({ { 0, 1 }, { 0.85, 1 }, { 1, 0 } }) } }),
			};
			shards.OutputCtx = Out(Shape::Mesh, 1.0f, Blend::Opaque);
			shards.OutputCtx.Mesh = "Crystal";
			shards.Editor = { { "x", 0.0f }, { "y", 0.0f } };
			System dust = Sys("Dust", 400, 0.0f);
			dust.SpawnCtx.Duration = 2.5f;
			dust.SpawnCtx.Bursts = { { 0.0f, 40, 1, 1.0f } };
			dust.Initialize = {
				B("SetPosition", { { "Shape", "Sphere" }, { "Radius", 0.5 }, { "Center", { 0, 0.4, 0 } } }),
				B("SetVelocity", { { "Mode", "Random" }, { "MinSpeed", 0.5 }, { "MaxSpeed", 2.0 } }),
				B("SetLifetime", { { "Min", 1.5 }, { "Max", 2.5 } }),
				B("SetSize", { { "Min", 0.6 }, { "Max", 1.2 } }),
				B("SetColor", { { "ColorA", { 0.55, 0.5, 0.45, 0.45 } } }),
			};
			dust.Update = { B("Drag", { { "Coefficient", 1.5 } }), B("ColorOverLife", { { "Gradient", kFade } }),
				B("SizeOverLife", { { "Curve", json::array({ { 0, 0.5 }, { 1, 1.6 } }) } }) };
			dust.OutputCtx = Out(Shape::Smoke, 1.0f, Blend::Alpha);
			dust.Editor = { { "x", 360.0f }, { "y", 0.0f } };
			a.Systems = { shards, dust };
			return a;
		}

		// 반딧불 (사용자 속성 · Update 의 Set Color): 태어날 때 Phase 를 기억해 제각각 깜빡인다
		//  색 = 연두 HDR × saturate(sin(Time × 3 + Phase))³ — Get Attribute 로 Phase 를 읽는다
		Asset Fireflies()
		{
			Asset a;
			a.Attributes = { { "Phase", false } };
			System s = Sys("Fireflies", 800, 70.0f);
			s.Initialize = {
				B("SetPosition", { { "Shape", "Box" }, { "Size", { 9, 2.5, 9 } }, { "Center", { 0, 1.4, 0 } } }),
				B("SetLifetime", { { "Min", 6.0 }, { "Max", 9.0 } }),
				B("SetSize", { { "Min", 0.07 }, { "Max", 0.13 } }),
				B("SetAttribute", { { "Attribute", "Phase" } }),
			};
			s.Initialize[3].Links = { { "Value", 1 } };
			s.Update = {
				B("Turbulence", { { "Intensity", 0.9 }, { "Frequency", 0.35 }, { "Drag", 1.2 }, { "Scroll", { 0, 0.15, 0 } } }),
				B("SetColor", { { "ColorA", { 1, 1, 1, 1 } } }),
				B("ColorOverLife", { { "Gradient", kFade } }),
			};
			s.Update[1].Links = { { "ColorA", 8 } };
			s.OutputCtx = Out(Shape::Glow, 1.0f);
			s.Editor = { { "x", 0.0f }, { "y", 0.0f } };
			a.Systems = { s };
			a.Operators = {
				Op(1, "RandomPerParticle", { { "Min", 0.0 }, { "Max", 6.2832 } }, json::object(), -760, 80),
				Op(2, "Time", json::object(), json::object(), -760, 260),
				Op(3, "GetAttribute", { { "Attribute", "Phase" } }, json::object(), -760, 360),
				Op(4, "Multiply", { { "B", 3.0 } }, { { "A", 2 } }, -560, 260),
				Op(5, "Add", json::object(), { { "A", 4 }, { "B", 3 } }, -560, 380),
				Op(6, "Sine", json::object(), { { "X", 5 } }, -360, 300),
				Op(7, "Power", { { "B", 3.0 } }, { { "A", 9 } }, -160, 400),
				Op(9, "Saturate", json::object(), { { "X", 6 } }, -360, 420),
				Op(8, "Multiply", { { "A", { 4.0, 6.5, 1.2, 1.0 } } }, { { "B", 7 } }, -160, 260),
			};
			return a;
		}

		struct Entry { const char* Name; Asset (*Make)(); };
		const Entry kTemplates[] = {
			{ "Simple Loop", SimpleLoop }, { "Fireworks", Fireworks }, { "Magic Circle", MagicCircle }, { "Tornado", Tornado },
			{ "Sparks", Sparks }, { "Galaxy", Galaxy }, { "Fire", Fire }, { "Portal", Portal }, { "Energy Swirl", EnergySwirl }, { "Rainbow Spiral", RainbowSpiral },
			{ "Debris", Debris }, { "Fireflies", Fireflies },
		};
	}

	Asset DefaultAsset() { return SimpleLoop(); }

	std::vector<std::string> TemplateNames()
	{
		std::vector<std::string> n;
		for (const Entry& e : kTemplates)
			n.push_back(e.Name);
		return n;
	}

	bool MakeTemplate(const std::string& name, Asset& out)
	{
		auto simple = [](const std::string& s) {
			std::string o;
			for (char c : s) if (isalnum((unsigned char)c)) o += (char)tolower((unsigned char)c);
			return o;
		};
		for (const Entry& e : kTemplates)
			if (simple(e.Name) == simple(name))
			{
				out = e.Make();
				return true;
			}
		return false;
	}
}
