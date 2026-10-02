// com.nova.toon 진입점: 재질 셰이더 "lilToon" 을 엔진 (CustomShaders) 에 등록한다.
//  셰이딩 = Shaders/lilToon.fx (lilToon, MIT, lilxyzw 의 식을 옮김 — LICENSE-lilToon.txt)
//  재질 값 = .mat 의 "Properties" (아래 Defaults 의 이름), Inspector 는 이 패키지가 그린다
#include "pch.h"
#include "CustomShaders.h"
#include "UMaterial.h"
#include "Effects.h"
#include "UnityGUI.h"
#include <filesystem>
#include <unordered_map>

NOVA_PACKAGE_EXPORT const char* NovaPackage_Abi() { return NOVA_PACKAGE_ABI_VERSION; }

namespace
{
	constexpr const char* kPackage = "com.nova.toon";
	constexpr const char* kShader = "lilToon";

	InstancedBasicEffect* s_Fx = nullptr;
	FxTechnique* s_Main = nullptr;
	FxTechnique* s_Outline = nullptr;
	enum Var { ShadowColor, Shadow2ndColor, ShadowBorderColor, Shadow, Shadow2, Light, RimColor, Rim, Rim2, BacklightColor, Backlight, Backlight2, OutlineColor, Outline, Flags, VarCount };
	const char* kVarNames[VarCount] = { "gLilShadowColor", "gLilShadow2ndColor", "gLilShadowBorderColor", "gLilShadow", "gLilShadow2", "gLilLight", "gLilRimColor", "gLilRim",
		"gLilRim2", "gLilBacklightColor", "gLilBacklight", "gLilBacklight2", "gLilOutlineColor", "gLilOutline", "gLilFlags" };
	FxVar* s_Vars[VarCount] = {};
	FxVar* s_ShadowTex = nullptr;
	FxVar* s_Shadow2Tex = nullptr;

	// ---- 재질 값 (이름은 lilToon 의 속성 이름에서 _ 를 뺀 것)
	nlohmann::json Defaults()
	{
		return {
			{ "UseShadow", true }, { "ShadowColor", { 0.82, 0.76, 0.85 } }, { "ShadowBorder", 0.5 }, { "ShadowBlur", 0.1 },
			{ "UseShadow2nd", false }, { "Shadow2ndColor", { 0.68, 0.66, 0.79 } }, { "Shadow2ndStrength", 1.0 }, { "Shadow2ndBorder", 0.15 }, { "Shadow2ndBlur", 0.1 },
			{ "ShadowStrength", 1.0 }, { "ShadowMainStrength", 0.0 }, { "ShadowEnvStrength", 0.0 }, { "ShadowReceive", 1.0 },
			{ "ShadowBorderColor", { 1.0, 0.1, 0.0 } }, { "ShadowBorderRange", 0.08 },
			{ "ShadowColorTex", "" }, { "Shadow2ndColorTex", "" },
			{ "LightMinLimit", 0.05 }, { "LightMaxLimit", 1.0 }, { "MonochromeLighting", 0.0 }, { "AsUnlit", 0.0 },
			{ "UseRim", false }, { "RimColor", { 0.66, 0.5, 0.48 } }, { "RimStrength", 1.0 }, { "RimBorder", 0.5 }, { "RimBlur", 0.65 }, { "RimFresnelPower", 3.5 },
			{ "RimShadowMask", 0.5 }, { "RimEnableLighting", 1.0 }, { "RimMainStrength", 0.0 },
			{ "UseBacklight", false }, { "BacklightColor", { 0.85, 0.8, 0.7 } }, { "BacklightStrength", 1.0 }, { "BacklightBorder", 0.35 }, { "BacklightBlur", 0.05 },
			{ "BacklightDirectivity", 5.0 }, { "BacklightViewStrength", 1.0 }, { "BacklightMainStrength", 0.0 },
			{ "UseOutline", true }, { "OutlineColor", { 0.6, 0.56, 0.73 } }, { "OutlineWidth", 0.08 }, { "OutlineFixWidth", 0.5 }, { "OutlineZBias", 0.0 }, { "OutlineEnableLighting", 1.0 },
		};
	}

	// 기본값은 한 번만 만든다 (참조로 넘겨도 안전하게 — 임시 JSON 의 원소를 잡으면 사라진 값을 읽는다)
	const nlohmann::json& DefaultsRef()
	{
		static const nlohmann::json d = Defaults();
		return d;
	}

	float F(const nlohmann::json& j, const char* key)
	{
		if (j.contains(key) && j[key].is_number()) return j[key].get<float>();
		if (j.contains(key) && j[key].is_boolean()) return j[key].get<bool>() ? 1.0f : 0.0f;
		const nlohmann::json& d = DefaultsRef();
		if (!d.contains(key)) return 0.0f;
		return d[key].is_boolean() ? (d[key].get<bool>() ? 1.0f : 0.0f) : (d[key].is_number() ? d[key].get<float>() : 0.0f);
	}
	XMFLOAT3 C(const nlohmann::json& j, const char* key)
	{
		auto ok = [](const nlohmann::json& a) { return a.is_array() && a.size() >= 3 && a[0].is_number() && a[1].is_number() && a[2].is_number(); };
		const nlohmann::json& d = DefaultsRef();
		const nlohmann::json* a = j.contains(key) && ok(j[key]) ? &j[key] : (d.contains(key) && ok(d[key]) ? &d[key] : nullptr);
		if (a == nullptr) return XMFLOAT3(1, 1, 1);
		return XMFLOAT3((*a)[0].get<float>(), (*a)[1].get<float>(), (*a)[2].get<float>());
	}
	XMFLOAT4 C4(const nlohmann::json& j, const char* key, float a) { const XMFLOAT3 c = C(j, key); return XMFLOAT4(c.x, c.y, c.z, a); }

	// 재질마다 해석해 둔 값 (패키지 안에 둔다 — 재질에 두면 패키지를 내린 뒤 해제가 위험)
	struct Parsed
	{
		uint64 Revision = 0;
		XMFLOAT4 V[VarCount] = {};
		ComPtr<GfxShaderResourceView> ShadowTex, Shadow2Tex;
		bool Outline = false;
	};
	std::unordered_map<const UMaterial*, Parsed> s_Cache;

	ComPtr<GfxShaderResourceView> LoadTex(const nlohmann::json& j, const char* key)
	{
		const std::string path = j.contains(key) && j[key].is_string() ? j[key].get<std::string>() : std::string();
		return path.empty() ? nullptr : ResourceManager::GetI()->LoadTexture(string_to_wstring(path));
	}

	const Parsed& Get(const UMaterial& m)
	{
		Parsed& p = s_Cache[&m];
		if (p.Revision == m.PropertiesRevision())
			return p;
		const nlohmann::json& j = m.Properties();
		p.Revision = m.PropertiesRevision();
		p.ShadowTex = LoadTex(j, "ShadowColorTex");
		p.Shadow2Tex = LoadTex(j, "Shadow2ndColorTex");
		p.V[ShadowColor] = C4(j, "ShadowColor", 1.0f);
		p.V[Shadow2ndColor] = C4(j, "Shadow2ndColor", F(j, "Shadow2ndStrength"));
		p.V[ShadowBorderColor] = C4(j, "ShadowBorderColor", F(j, "ShadowBorderRange"));
		p.V[Shadow] = XMFLOAT4(F(j, "ShadowBorder"), F(j, "ShadowBlur"), F(j, "Shadow2ndBorder"), F(j, "Shadow2ndBlur"));
		p.V[Shadow2] = XMFLOAT4(F(j, "ShadowStrength"), F(j, "ShadowMainStrength"), F(j, "ShadowEnvStrength"), F(j, "ShadowReceive"));
		p.V[Light] = XMFLOAT4(F(j, "LightMinLimit"), F(j, "LightMaxLimit"), F(j, "MonochromeLighting"), F(j, "AsUnlit"));
		p.V[RimColor] = C4(j, "RimColor", F(j, "UseRim") > 0.5f ? F(j, "RimStrength") : 0.0f);
		p.V[Rim] = XMFLOAT4(F(j, "RimBorder"), F(j, "RimBlur"), F(j, "RimFresnelPower"), F(j, "RimShadowMask"));
		p.V[Rim2] = XMFLOAT4(F(j, "RimEnableLighting"), F(j, "RimMainStrength"), 0, 0);
		p.V[BacklightColor] = C4(j, "BacklightColor", F(j, "UseBacklight") > 0.5f ? F(j, "BacklightStrength") : 0.0f);
		p.V[Backlight] = XMFLOAT4(F(j, "BacklightBorder"), F(j, "BacklightBlur"), F(j, "BacklightDirectivity"), F(j, "BacklightViewStrength"));
		p.V[Backlight2] = XMFLOAT4(F(j, "BacklightMainStrength"), 0, 0, 0);
		p.Outline = F(j, "UseOutline") > 0.5f && F(j, "OutlineWidth") > 0.0f;
		p.V[OutlineColor] = C4(j, "OutlineColor", p.Outline ? F(j, "OutlineWidth") : 0.0f);
		p.V[Outline] = XMFLOAT4(F(j, "OutlineFixWidth"), F(j, "OutlineZBias"), F(j, "OutlineEnableLighting"), 0);
		p.V[Flags] = XMFLOAT4(F(j, "UseShadow"), p.ShadowTex ? 1.0f : 0.0f, p.Shadow2Tex ? 1.0f : 0.0f, F(j, "UseShadow2nd"));
		return p;
	}

	// 엔진 스킨 패스와 같은 행렬 · 본 · 재질 (UMaterial::Apply) + lilToon 값 → 패스
	void Setup(CustomShaders::SkinnedDraw& d, FxTechnique* tech)
	{
		static const XMMATRIX toTex(0.5f, 0.0f, 0.0f, 0.0f, 0.0f, -0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.5f, 0.5f, 0.0f, 1.0f);
		XMMATRIX w = d.World;
		w.r[3] = XMVectorSet(0, 0, 0, 1);
		XMVECTOR det;
		const XMMATRIX invT = XMMatrixTranspose(XMMatrixInverse(&det, w));
		const XMMATRIX wvp = d.World * d.ViewProj;
		s_Fx->SetWorld(d.World);
		s_Fx->SetWorldInvTranspose(invT);
		s_Fx->SetViewProj(d.ViewProj);
		s_Fx->SetWorldViewProj(wvp);
		s_Fx->SetWorldViewProjTex(wvp * toTex);
		s_Fx->SetTexTransform(XMMatrixIdentity());
		if (d.Bones && d.BoneCount > 0)
			s_Fx->SetBoneTransforms(d.Bones, d.BoneCount);
		d.Material->Apply(s_Fx);
		const Parsed& p = Get(*d.Material);
		for (int i = 0; i < VarCount; ++i)
			if (s_Vars[i]) s_Vars[i]->SetFloatVector(&p.V[i].x);
		if (s_ShadowTex) s_ShadowTex->SetResource(p.ShadowTex.Get());
		if (s_Shadow2Tex) s_Shadow2Tex->SetResource(p.Shadow2Tex.Get());
		tech->GetPassByIndex(0)->Apply(0, d.Context);
	}

	// ---- Inspector (lilToon 의 메뉴 묶음과 같은 순서)
	bool Inspector(UMaterial& m)
	{
		nlohmann::json& j = m.Properties();
		bool changed = false;
		auto boolean = [&](const char* label, const char* key) {
			bool v = F(j, key) > 0.5f;
			if (UnityGUI::Toggle(label, &v)) { j[key] = v; changed = true; }
			return v;
		};
		auto slider = [&](const char* label, const char* key, float lo, float hi) {
			float v = F(j, key);
			if (UnityGUI::Slider(label, &v, lo, hi, 1)) { j[key] = v; changed = true; }
		};
		auto color = [&](const char* label, const char* key) {
			const XMFLOAT3 c = C(j, key);
			float rgba[4] = { c.x, c.y, c.z, 1.0f };
			if (UnityGUI::Color(label, rgba, 1)) { j[key] = { rgba[0], rgba[1], rgba[2] }; changed = true; }
		};
		auto texture = [&](const char* label, const char* key) {
			std::string v = j.contains(key) && j[key].is_string() ? j[key].get<std::string>() : std::string();
			if (UnityGUI::TextField(label, &v, 1)) { j[key] = v; changed = true; }
		};
		if (UnityGUI::FoldoutPlain("Lighting", 0, true))
		{
			slider("Min Limit", "LightMinLimit", 0.0f, 1.0f);
			slider("Max Limit", "LightMaxLimit", 0.0f, 10.0f);
			slider("Monochrome Lighting", "MonochromeLighting", 0.0f, 1.0f);
			slider("As Unlit", "AsUnlit", 0.0f, 1.0f);
		}
		if (UnityGUI::FoldoutPlain("Shadow", 0, true) && boolean("Use Shadow", "UseShadow"))
		{
			color("1st Color", "ShadowColor");
			texture("1st Color Tex", "ShadowColorTex");
			slider("1st Border", "ShadowBorder", 0.0f, 1.0f);
			slider("1st Blur", "ShadowBlur", 0.0f, 1.0f);
			if (boolean("Use 2nd", "UseShadow2nd"))
			{
				color("2nd Color", "Shadow2ndColor");
				texture("2nd Color Tex", "Shadow2ndColorTex");
				slider("2nd Strength", "Shadow2ndStrength", 0.0f, 1.0f);
				slider("2nd Border", "Shadow2ndBorder", 0.0f, 1.0f);
				slider("2nd Blur", "Shadow2ndBlur", 0.0f, 1.0f);
			}
			slider("Strength", "ShadowStrength", 0.0f, 1.0f);
			slider("Multiply Main Color", "ShadowMainStrength", 0.0f, 1.0f);
			slider("Environment Strength", "ShadowEnvStrength", 0.0f, 1.0f);
			slider("Receive Shadow", "ShadowReceive", 0.0f, 1.0f);
			color("Border Color", "ShadowBorderColor");
			slider("Border Range", "ShadowBorderRange", 0.0f, 1.0f);
		}
		if (UnityGUI::FoldoutPlain("Rim Light", 0, false) && boolean("Use Rim", "UseRim"))
		{
			color("Color", "RimColor");
			slider("Strength", "RimStrength", 0.0f, 1.0f);
			slider("Border", "RimBorder", 0.0f, 1.0f);
			slider("Blur", "RimBlur", 0.0f, 1.0f);
			slider("Fresnel Power", "RimFresnelPower", 0.01f, 50.0f);
			slider("Shadow Mask", "RimShadowMask", 0.0f, 1.0f);
			slider("Enable Lighting", "RimEnableLighting", 0.0f, 1.0f);
			slider("Multiply Main Color", "RimMainStrength", 0.0f, 1.0f);
		}
		if (UnityGUI::FoldoutPlain("Backlight", 0, false) && boolean("Use Backlight", "UseBacklight"))
		{
			color("Color", "BacklightColor");
			slider("Strength", "BacklightStrength", 0.0f, 1.0f);
			slider("Border", "BacklightBorder", 0.0f, 1.0f);
			slider("Blur", "BacklightBlur", 0.0f, 1.0f);
			slider("Directivity", "BacklightDirectivity", 0.0f, 20.0f);
			slider("View Direction Strength", "BacklightViewStrength", 0.0f, 1.0f);
			slider("Multiply Main Color", "BacklightMainStrength", 0.0f, 1.0f);
		}
		if (UnityGUI::FoldoutPlain("Outline", 0, true) && boolean("Use Outline", "UseOutline"))
		{
			color("Color", "OutlineColor");
			slider("Width (cm)", "OutlineWidth", 0.0f, 1.0f);
			slider("Fix Width", "OutlineFixWidth", 0.0f, 1.0f);
			slider("Z Bias (m)", "OutlineZBias", -0.01f, 0.05f);
			slider("Enable Lighting", "OutlineEnableLighting", 0.0f, 1.0f);
		}
		UnityGUI::HelpBox("lilToon (MIT, lilxyzw) shading ported to NOVA: shadow layers, border, light limits, rim, backlight and outline.", false);
		return changed;
	}
}

NOVA_PACKAGE_EXPORT void NovaPackage_OnLoad()
{
	// 셰이더 = 이 DLL (Plugins) 옆 ../Shaders/lilToon.fx
	HMODULE self = nullptr;
	GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&NovaPackage_OnLoad, &self);
	wchar_t buf[MAX_PATH] = {};
	GetModuleFileNameW(self, buf, MAX_PATH);
	const std::filesystem::path fxPath = std::filesystem::path(buf).parent_path().parent_path() / L"Shaders" / L"lilToon.fx";
	std::string error;
	s_Fx = CustomShaders::LoadEffect(kPackage, fxPath.wstring(), error);
	if (s_Fx == nullptr)
	{
		EditorLog::Write("Toon", "lilToon shader failed: %s", error.c_str());
		return;   // 셰이더 없이 등록하지 않는다 → 재질은 Fallback 으로 그려진다
	}
	FxEffect* fx = s_Fx->GetFX();
	s_Main = fx->GetTechniqueByName("LilSkinnedTech");
	s_Outline = fx->GetTechniqueByName("LilSkinnedOutlineTech");
	for (int i = 0; i < VarCount; ++i)
		s_Vars[i] = fx->GetVariableByName(kVarNames[i])->AsVector();
	s_ShadowTex = fx->GetVariableByName("gLilShadowColorTex")->AsShaderResource();
	s_Shadow2Tex = fx->GetVariableByName("gLilShadow2ndColorTex")->AsShaderResource();

	CustomShaders::Shader shader;
	shader.Name = kShader;
	shader.Owner = kPackage;
	shader.DrawSkinned = [](CustomShaders::SkinnedDraw& d) {
		Setup(d, s_Main);
		d.Draw();
	};
	shader.DrawSkinnedOutline = [](CustomShaders::SkinnedDraw& d) {
		Setup(d, s_Outline);
		d.Draw();
	};
	shader.HasOutline = [](const UMaterial& m) { return Get(m).Outline; };
	shader.Inspector = Inspector;
	shader.DefaultProperties = Defaults;
	CustomShaders::Register(shader);
}

NOVA_PACKAGE_EXPORT void NovaPackage_OnUnload()
{
	CustomShaders::UnregisterOwner(kPackage);   // 이펙트도 같이 (엔진이 가진 것)
	s_Fx = nullptr;
	s_Main = s_Outline = nullptr;
	for (FxVar*& v : s_Vars) v = nullptr;
	s_ShadowTex = s_Shadow2Tex = nullptr;
	s_Cache.clear();
}
