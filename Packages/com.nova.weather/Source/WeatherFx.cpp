#include "pch.h"
#include "WeatherFx.h"
#include "VfxAsset.h"
#include "Debug.h"

namespace WeatherFx
{
	using json = nlohmann::json;

	namespace
	{
		json Block(const char* type, json params = json::object(), json bind = json::object())
		{
			json b = { { "type", type }, { "params", std::move(params) } };
			if (!bind.empty()) b["bind"] = std::move(bind);
			return b;
		}

		json Spawn(const char* rateBind)
		{
			return { { "rate", 0.0 }, { "rateBind", rateBind }, { "loop", true }, { "duration", 0.0 } };
		}

		json Child(const char* parent, int perEvent)
		{
			return { { "rate", 0.0 }, { "loop", true }, { "duration", 0.0 }, { "parent", parent }, { "countPerEvent", perEvent }, { "trigger", "OnDie" } };
		}

		const json kFadeInOut = json::array({ { 0.0, 1, 1, 1, 0 }, { 0.06, 1, 1, 1, 1 }, { 0.85, 1, 1, 1, 1 }, { 1.0, 1, 1, 1, 0 } });
		const json kRipple = json::array({ { 0.0, 1, 1, 1, 0.9 }, { 1.0, 1, 1, 1, 0 } });
	}

	json Precipitation()
	{
		json systems = json::array();
		// ---- 가까운 비: 카메라 둘레 26 m 상자, 위 14 m 에서. 보이는 장면 (깊이) 에 닿으면 죽고 → 튀는 물 · 동그라미
		systems.push_back({
			{ "name", "Rain" }, { "capacity", 45000 }, { "space", "World" },
			{ "spawn", Spawn("Rain Rate") },
			{ "initialize", json::array({
				Block("SetPosition", { { "Shape", "Box" }, { "Size", { 26, 1, 26 } }, { "Center", { 0, 14, 0 } } }, { { "Center", "Rain Offset" } }),
				Block("SetVelocity", { { "Mode", "Direction" }, { "Direction", { 0, -1, 0 } }, { "MinSpeed", 10.0 }, { "MaxSpeed", 13.0 } }),
				Block("SetVelocity", { { "Mode", "Direction" }, { "MinSpeed", 0.45 }, { "MaxSpeed", 0.55 } }, { { "Direction", "Wind" } }),   // 바람의 반 (다 따르면 센 바람에 빗줄기가 한 점에서 쏟아지듯 보인다)
				Block("SetLifetime", { { "Min", 2.6 }, { "Max", 3.0 } }),
				Block("SetSize", { { "Min", 0.011 }, { "Max", 0.018 } }),
				Block("SetColor", { { "ColorA", { 0.72, 0.78, 0.88, 0.42 } }, { "Intensity", 1.0 } }),
			}) },
			{ "update", json::array({
				Block("CollideDepth", { { "Bounce", 0.0 }, { "Friction", 0.0 }, { "LifetimeLoss", 1.0 }, { "Thickness", 1.5 } }),
			}) },
			{ "output", { { "blend", "Alpha" }, { "shape", "Spark" }, { "orient", "AlongVelocity" }, { "stretch", 1.7 }, { "softDistance", 0.0 }, { "intensity", 1.25 }, { "sort", "Off" } } },
			{ "editor", { { "x", 0.0 }, { "y", 0.0 } } },
		});
		// 튀는 물방울
		systems.push_back({
			{ "name", "Splash" }, { "capacity", 24000 }, { "space", "World" },
			{ "spawn", Child("Rain", 2) },
			{ "initialize", json::array({
				Block("SetVelocity", { { "Mode", "Cone" }, { "Direction", { 0, 1, 0 } }, { "Spread", 60.0 }, { "MinSpeed", 0.7 }, { "MaxSpeed", 2.0 } }),
				Block("SetLifetime", { { "Min", 0.16 }, { "Max", 0.3 } }),
				Block("SetSize", { { "Min", 0.008 }, { "Max", 0.016 } }),
				Block("SetColor", { { "ColorA", { 0.8, 0.85, 0.95, 0.55 } }, { "Intensity", 1.2 } }),
			}) },
			{ "update", json::array({ Block("Gravity") }) },
			{ "output", { { "blend", "Alpha" }, { "shape", "SoftDot" }, { "orient", "FaceCamera" }, { "softDistance", 0.0 }, { "intensity", 1.0 }, { "sort", "Off" } } },
			{ "editor", { { "x", 360.0 }, { "y", 0.0 } } },
		});
		// 땅의 동그라미 물결 (수평 고리가 퍼지며 사라진다)
		systems.push_back({
			{ "name", "Ripple" }, { "capacity", 12000 }, { "space", "World" },
			{ "spawn", Child("Rain", 1) },
			{ "initialize", json::array({
				Block("SetLifetime", { { "Min", 0.3 }, { "Max", 0.45 } }),
				Block("SetSize", { { "Min", 0.05 }, { "Max", 0.09 } }),
				Block("SetColor", { { "ColorA", { 0.85, 0.9, 1.0, 0.35 } } }),
			}) },
			{ "update", json::array({
				Block("SizeOverLife", { { "Curve", json::array({ { 0, 0.3 }, { 1, 3.5 } }) } }),
				Block("ColorOverLife", { { "Gradient", kRipple } }),
			}) },
			{ "output", { { "blend", "Alpha" }, { "shape", "Ring" }, { "orient", "Horizontal" }, { "softDistance", 0.0 }, { "intensity", 1.0 }, { "sort", "Off" } } },
			{ "editor", { { "x", 720.0 }, { "y", 0.0 } } },
		});
		// ---- 먼 비: 90 m 상자 (충돌 없이 — 지형 뒤는 깊이 검사로 가려진다), 굵고 옅은 빗줄기로 깊이감
		systems.push_back({
			{ "name", "Far Rain" }, { "capacity", 20000 }, { "space", "World" },
			{ "spawn", Spawn("Far Rain Rate") },
			{ "initialize", json::array({
				Block("SetPosition", { { "Shape", "Box" }, { "Size", { 90, 1, 90 } }, { "Center", { 0, 30, 0 } } }, { { "Center", "Far Rain Offset" } }),
				Block("SetVelocity", { { "Mode", "Direction" }, { "Direction", { 0, -1, 0 } }, { "MinSpeed", 12.0 }, { "MaxSpeed", 15.0 } }),
				Block("SetVelocity", { { "Mode", "Direction" }, { "MinSpeed", 0.45 }, { "MaxSpeed", 0.55 } }, { { "Direction", "Wind" } }),
				Block("SetLifetime", { { "Min", 3.6 }, { "Max", 4.2 } }),
				Block("SetSize", { { "Min", 0.03 }, { "Max", 0.05 } }),
				Block("SetColor", { { "ColorA", { 0.7, 0.75, 0.85, 0.16 } } }),
			}) },
			{ "update", json::array() },
			{ "output", { { "blend", "Alpha" }, { "shape", "Spark" }, { "orient", "AlongVelocity" }, { "stretch", 2.2 }, { "softDistance", 0.6 }, { "intensity", 1.2 }, { "sort", "Off" } } },
			{ "editor", { { "x", 0.0 }, { "y", 520.0 } } },
		});
		// ---- 눈: 카메라 둘레 40 × 18 × 40 m 공간 어디서나 (높이마다 고르게) 천천히. 바람에 실려 흔들리고, 보이는 장면에 닿으면 멈춰 앉았다가 사라진다
		//  바람이 세면 수명을 줄이고 (Snow Life) 상자를 바람 위쪽으로 (Snow Offset) → 카메라 둘레의 눈 수가 바람과 상관없이 같다
		systems.push_back({
			{ "name", "Snow" }, { "capacity", 100000 }, { "space", "World" },
			{ "spawn", Spawn("Snow Rate") },
			{ "initialize", json::array({
				Block("SetPosition", { { "Shape", "Box" }, { "Size", { 40, 18, 40 } }, { "Center", { 0, 6, 0 } } }, { { "Center", "Snow Offset" } }),
				Block("SetVelocity", { { "Mode", "Direction" }, { "Direction", { 0, -1, 0 } }, { "MinSpeed", 0.7 }, { "MaxSpeed", 1.3 } }),
				Block("SetVelocity", { { "Mode", "Direction" }, { "MinSpeed", 0.85 }, { "MaxSpeed", 1.0 } }, { { "Direction", "Wind" } }),
				Block("SetVelocity", { { "Mode", "Random" }, { "MinSpeed", 0.05 }, { "MaxSpeed", 0.3 } }),
				Block("SetLifetime", { { "Min", 7.0 }, { "Max", 9.0 } }, { { "Min", "Snow Life" }, { "Max", "Snow Life Max" } }),
				Block("SetSize", { { "Min", 0.03 }, { "Max", 0.06 } }),
				Block("SetColor", { { "ColorA", { 1, 1, 1, 0.92 } }, { "Intensity", 1.15 } }),
			}) },
			{ "update", json::array({
				Block("Turbulence", { { "Intensity", 0.7 }, { "Frequency", 0.3 }, { "Octaves", 2 }, { "Drag", 0.0 }, { "Scroll", { 0, 0.15, 0 } } }),
				Block("CollideDepth", { { "Bounce", 0.0 }, { "Friction", 1.0 }, { "LifetimeLoss", 0.0 }, { "Thickness", 0.6 } }),
				Block("ColorOverLife", { { "Gradient", kFadeInOut } }),
			}) },
			{ "output", { { "blend", "Alpha" }, { "shape", "SoftDot" }, { "orient", "AlongVelocity" }, { "stretch", 0.07 }, { "softDistance", 0.08 }, { "intensity", 1.0 }, { "sort", "Off" } } },   // 센 바람에 늘어난다 (눈보라)
			{ "editor", { { "x", 360.0 }, { "y", 520.0 } } },
		});
		json props = json::array({
			{ { "name", "Rain Rate" }, { "type", "Float" }, { "value", 0.0 } },
			{ { "name", "Far Rain Rate" }, { "type", "Float" }, { "value", 0.0 } },
			{ { "name", "Snow Rate" }, { "type", "Float" }, { "value", 0.0 } },
			{ { "name", "Wind" }, { "type", "Vector3" }, { "value", { 0, 0, 0 } } },
			{ { "name", "Rain Offset" }, { "type", "Vector3" }, { "value", { 0, 14, 0 } } },
			{ { "name", "Far Rain Offset" }, { "type", "Vector3" }, { "value", { 0, 30, 0 } } },
			{ { "name", "Snow Offset" }, { "type", "Vector3" }, { "value", { 0, 6, 0 } } },
			{ { "name", "Snow Life" }, { "type", "Float" }, { "value", 7.0 } },
			{ { "name", "Snow Life Max" }, { "type", "Float" }, { "value", 9.0 } },
		});
		return { { "version", 1 }, { "properties", props }, { "systems", systems }, { "culling", "AlwaysSimulate" } };
	}

	namespace { bool s_Registered = false; }

	void EnsureAsset()
	{
		if (s_Registered)
			return;
		std::string error;
		s_Registered = Vfx::SetLiveJson(kAssetPath, Precipitation(), error);
		if (!s_Registered)
			Debug::LogWarning("Weather: precipitation effect failed: " + error);
	}

	void ReleaseAsset()
	{
		if (s_Registered)
			Vfx::ClearLive(kAssetPath);
		s_Registered = false;
	}
}
