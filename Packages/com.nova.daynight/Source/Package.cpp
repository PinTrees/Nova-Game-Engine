// com.nova.daynight 진입점: CLI (nova daynight) + C# API (Runtime/DayNight.cs 가 DllImport("NovaDayNight") 로 부른다)
#include "pch.h"
#include "DayNightCycle.h"
#include "DayNightState.h"
#include "CliServer.h"
#include "EditorExtensions.h"

NOVA_PACKAGE_EXPORT const char* NovaPackage_Abi() { return NOVA_PACKAGE_ABI_VERSION; }

namespace
{
	constexpr const char* kPackage = "com.nova.daynight";

	int PhaseFromName(std::string name)
	{
		std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return (char)std::tolower(c); });
		name.erase(std::remove(name.begin(), name.end(), ' '), name.end());
		for (int i = 0; i < DayNightCycle::PhaseCount; ++i)
		{
			std::string n = DayNightCycle::PhaseName(i);
			std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return (char)std::tolower(c); });
			if (n == name)
				return i;
		}
		return -1;
	}

	// 단계의 가운데 시각
	float PhaseCenter(int p)
	{
		const float a = DayNightCycle::PhaseStart(p);
		float b = DayNightCycle::PhaseStart((p + 1) % DayNightCycle::PhaseCount);
		if (b <= a) b += 24.0f;
		return fmodf(0.5f * (a + b), 24.0f);
	}

	json Status(DayNightCycle* d)
	{
		const DayNightState& s = DayNightState::Get();
		const Vec3 sun = d->SunDirection();
		json j = {
			{ "timeOfDay", d->TimeOfDay }, { "phase", DayNightCycle::PhaseName(d->CurrentPhase()) }, { "dayLengthMinutes", d->DayLengthMinutes },
			{ "paused", d->Paused }, { "sunElevation", d->SunElevation() }, { "sunDirection", { sun.x, sun.y, sun.z } },
			{ "sunIntensity", s.SunIntensity }, { "ambientIntensity", s.AmbientIntensity }, { "skyBrightness", s.SkyBrightness },
			{ "stars", s.Stars }, { "milkyWay", s.MilkyWay }, { "moon", s.Moon }, { "glow", s.Glow.w }, { "gradient", s.Zenith.w } };
		if (GameObject* lo = d->LightObject())
		{
			const Matrix w = lo->GetTransform()->GetWorldMatrix();
			Vec3 f = Vec3::TransformNormal(Vec3(0, 0, 1), w);
			f.Normalize();
			j["light"] = lo->GetName();
			j["lightForward"] = { f.x, f.y, f.z };
		}
		return j;
	}
}

NOVA_PACKAGE_EXPORT void NovaPackage_OnLoad()
{
	CliServer::Register("daynight", "day night cycle: {op: status|set|phase, time?, minutes?, paused?, azimuth?, name?} (nova daynight status)",
		[](const json& args, json& result, std::string& error) {
			const std::string op = args.value("op", std::string("status"));
			DayNightCycle* d = DayNightCycle::Active();
			if (d == nullptr) { error = "no Day Night Cycle in the scene (Add Component > Environment > Day Night Cycle)"; return false; }
			if (op == "set")
			{
				if (args.contains("time")) d->TimeOfDay = fmodf(fmodf(args["time"].get<float>(), 24.0f) + 24.0f, 24.0f);
				if (args.contains("minutes")) d->DayLengthMinutes = (std::max)(0.0f, args["minutes"].get<float>());
				if (args.contains("paused")) d->Paused = args["paused"].get<bool>();
				if (args.contains("azimuth")) d->SunAzimuth = args["azimuth"].get<float>();
			}
			else if (op == "phase")
			{
				const int p = PhaseFromName(args.value("name", args.value("path", std::string())));
				if (p < 0) { error = "unknown phase (Dawn, Morning, Day, Evening, Sunset, Night, MilkyWay)"; return false; }
				d->TimeOfDay = PhaseCenter(p);
			}
			else if (op != "status")
			{
				error = "unknown op '" + op + "' (status, set, phase)";
				return false;
			}
			d->Apply();
			result = Status(d);
			return true;
		});
}

NOVA_PACKAGE_EXPORT void NovaPackage_OnUnload()
{
	CliServer::Unregister("daynight");
	EditorExtensions::UnregisterOwner(kPackage);
	DayNightState::Reset();
}

// ---- C# API (DayNight 정적 클래스): 장면의 Day Night Cycle 하나
NOVA_PACKAGE_EXPORT int NovaDayNight_Exists() { return DayNightCycle::Active() != nullptr; }

// 0 timeOfDay, 1 dayLengthMinutes, 2 paused, 3 phase (읽기), 4 sunElevation (읽기), 5 sunAzimuth, 6 maxElevation, 7 stars, 8 milkyWay
NOVA_PACKAGE_EXPORT float NovaDayNight_GetFloat(int prop)
{
	DayNightCycle* d = DayNightCycle::Active();
	if (d == nullptr) return 0.0f;
	switch (prop)
	{
	case 0: return d->TimeOfDay;
	case 1: return d->DayLengthMinutes;
	case 2: return d->Paused ? 1.0f : 0.0f;
	case 3: return (float)d->CurrentPhase();
	case 4: return d->SunElevation();
	case 5: return d->SunAzimuth;
	case 6: return d->MaxElevation;
	case 7: return d->StarBrightness;
	default: return d->MilkyWayBrightness;
	}
}

NOVA_PACKAGE_EXPORT void NovaDayNight_SetFloat(int prop, float v)
{
	DayNightCycle* d = DayNightCycle::Active();
	if (d == nullptr) return;
	switch (prop)
	{
	case 0: d->TimeOfDay = fmodf(fmodf(v, 24.0f) + 24.0f, 24.0f); break;
	case 1: d->DayLengthMinutes = (std::max)(0.0f, v); break;
	case 2: d->Paused = v != 0.0f; break;
	case 5: d->SunAzimuth = v; break;
	case 6: d->MaxElevation = std::clamp(v, 1.0f, 90.0f); break;
	case 7: d->StarBrightness = (std::max)(0.0f, v); break;
	case 8: d->MilkyWayBrightness = (std::max)(0.0f, v); break;
	default: break;
	}
}

NOVA_PACKAGE_EXPORT void NovaDayNight_GetSunDirection(Vec3* out)
{
	DayNightCycle* d = DayNightCycle::Active();
	if (out) *out = d ? d->SunDirection() : Vec3(0, 1, 0);
}
