#include "pch.h"
#include "VrmImport.h"
#include <filesystem>
#include <fstream>
#include <set>

namespace fs = std::filesystem;

namespace VrmImport
{
	namespace
	{
		std::wstring Full(const std::wstring& path)
		{
			fs::path p(path);
			return p.is_absolute() ? p.wstring() : PathManager::GetI()->GetMovePathW(path);
		}

		std::string Lower(std::string s) { std::transform(s.begin(), s.end(), s.begin(), ::tolower); return s; }

		// 스프링 본 벡터 → 엔진 본 로컬: VRM 1.0 = glTF (오른손) → (-x, y, z), VRM 0.x = Unity 좌표 그대로
		bool s_ConvVrm0 = false;
		json Conv(const json& v)
		{
			const float sx = s_ConvVrm0 ? 1.0f : -1.0f;
			if (v.is_array() && v.size() == 3) return json::array({ sx * v[0].get<float>(), v[1].get<float>(), v[2].get<float>() });
			if (v.is_object()) return json::array({ sx * v.value("x", 0.0f), v.value("y", 0.0f), v.value("z", 0.0f) });
			return json::array({ 0.0f, 0.0f, 0.0f });
		}

		std::string NodeName(const json& gltf, int node)
		{
			if (!gltf.contains("nodes") || node < 0 || node >= (int)gltf["nodes"].size()) return std::string();
			return gltf["nodes"][node].value("name", std::string());
		}

		// 파일 이름으로 쓸 수 있게
		std::string Safe(std::string s)
		{
			for (char& c : s) if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') c = '_';
			return s.empty() ? std::string("unnamed") : s;
		}

		const json* VrmExt(const json& gltf, bool& vrm1)
		{
			vrm1 = false;
			if (!gltf.contains("extensions")) return nullptr;
			const json& e = gltf["extensions"];
			if (e.contains("VRMC_vrm")) { vrm1 = true; return &e["VRMC_vrm"]; }
			if (e.contains("VRM")) return &e["VRM"];
			return nullptr;
		}

		float ToGamma(float c) { return powf(std::clamp(c, 0.0f, 1.0f), 1.0f / 2.2f); }
	}

	bool IsVrm(const std::wstring& path)
	{
		const std::string ext = Lower(fs::path(path).extension().string());
		if (ext == ".vrm") return true;
		if (ext != ".glb") return false;
		json j;
		bool v1 = false;
		return ReadGlb(path, j) && VrmExt(j, v1) != nullptr;
	}

	bool IsVrm0(const std::wstring& path)
	{
		const std::string ext = Lower(fs::path(path).extension().string());
		if (ext != ".vrm" && ext != ".glb") return false;
		json j;
		bool v1 = false;
		return ReadGlb(path, j) && VrmExt(j, v1) != nullptr && !v1;
	}

	bool ReadGlb(const std::wstring& path, json& out, std::vector<uint8_t>* bin)
	{
		std::ifstream f(Full(path), std::ios::binary);
		if (!f) return false;
		if (!bin)
		{
			// JSON 덩어리만 (머리 12 + 덩어리 머리 8)
			uint8_t head[20] = {};
			f.read((char*)head, 20);
			if (f.gcount() == 20 && memcmp(head, "glTF", 4) == 0)
			{
				uint32_t len = 0, type = 0;
				memcpy(&len, head + 12, 4);
				memcpy(&type, head + 16, 4);
				if (type != 0x4E4F534A || len > (256u << 20)) return false;
				std::string text(len, '\0');
				f.read(text.data(), len);
				out = json::parse(text, nullptr, false);
				return !out.is_discarded();
			}
			f.clear();
			f.seekg(0);
		}
		std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
		if (d.size() < 20 || memcmp(d.data(), "glTF", 4) != 0)
		{
			out = json::parse(d.begin(), d.end(), nullptr, false);   // .gltf (글자)
			return !out.is_discarded();
		}
		size_t at = 12;
		bool gotJson = false;
		while (at + 8 <= d.size())
		{
			uint32_t len = 0, type = 0;
			memcpy(&len, d.data() + at, 4);
			memcpy(&type, d.data() + at + 4, 4);
			at += 8;
			if (at + len > d.size()) break;
			if (type == 0x4E4F534A) { out = json::parse(d.begin() + at, d.begin() + at + len, nullptr, false); gotJson = !out.is_discarded(); }
			else if (type == 0x004E4942 && bin) bin->assign(d.begin() + at, d.begin() + at + len);
			at += len;
		}
		return gotJson;
	}

	std::map<std::string, std::string> HumanBones(const std::wstring& path)
	{
		std::map<std::string, std::string> out;
		json g;
		bool v1 = false;
		if (!ReadGlb(path, g)) return out;
		const json* vrm = VrmExt(g, v1);
		if (!vrm || !vrm->contains("humanoid")) return out;
		auto unityName = [&](std::string b) {
			if (b.empty()) return b;
			if (v1)
			{
				// VRM 1.0 엄지: Metacarpal · Proximal · Distal → Unity Proximal · Intermediate · Distal
				auto rep = [&](const char* from, const char* to) { const size_t p = b.find(from); if (p != std::string::npos) b.replace(p, strlen(from), to); };
				if (b.find("Thumb") != std::string::npos) { rep("Proximal", "Intermediate"); rep("Metacarpal", "Proximal"); }
			}
			b[0] = (char)toupper(b[0]);
			return b;
		};
		const json& hb = (*vrm)["humanoid"].value("humanBones", json());
		if (hb.is_object())
			for (auto it = hb.begin(); it != hb.end(); ++it)
			{
				const std::string n = NodeName(g, it.value().value("node", -1));
				if (!n.empty()) out[unityName(it.key())] = n;
			}
		else if (hb.is_array())
			for (const json& e : hb)
			{
				const std::string n = NodeName(g, e.value("node", -1));
				if (!n.empty()) out[unityName(e.value("bone", std::string()))] = n;
			}
		return out;
	}

	std::vector<std::wstring> ExtractMaterials(const std::wstring& assetPath)
	{
		std::vector<std::wstring> result;
		json g;
		std::vector<uint8_t> bin;
		if (!ReadGlb(assetPath, g, &bin) || !g.contains("materials")) return result;
		const fs::path rel(assetPath);
		const fs::path texRel = rel.parent_path() / (rel.stem().wstring() + L".Textures");
		const fs::path matRel = rel.parent_path() / (rel.stem().wstring() + L".Materials");
		std::error_code ec;
		fs::create_directories(Full(texRel.wstring()), ec);
		fs::create_directories(Full(matRel.wstring()), ec);

		// 그림 → 파일 (bufferView 에 묻힌 것)
		std::vector<std::wstring> imageFile;
		std::set<std::string> used;
		const json images = g.value("images", json::array());
		for (int i = 0; i < (int)images.size(); ++i)
		{
			const json& im = images[i];
			std::string name = Safe(im.value("name", "image" + std::to_string(i)));
			const std::string ext = im.value("mimeType", std::string()) == "image/jpeg" ? ".jpg" : ".png";
			std::string unique = name;
			for (int n = 1; used.count(Lower(unique)); ++n) unique = name + "_" + std::to_string(n);
			used.insert(Lower(unique));
			const fs::path file = texRel / string_to_wstring(unique + ext);
			imageFile.push_back(file.wstring());
			if (fs::exists(Full(file.wstring()), ec) || !im.contains("bufferView")) continue;
			const json& bv = g["bufferViews"][im["bufferView"].get<int>()];
			const size_t off = bv.value("byteOffset", (size_t)0), len = bv.value("byteLength", (size_t)0);
			if (off + len > bin.size()) continue;
			std::ofstream o(Full(file.wstring()), std::ios::binary);
			o.write((const char*)bin.data() + off, len);
		}
		auto texFile = [&](const json& texInfo) -> std::wstring {
			if (!texInfo.is_object() || !texInfo.contains("index") || !g.contains("textures")) return std::wstring();
			const int t = texInfo["index"].get<int>();
			if (t < 0 || t >= (int)g["textures"].size()) return std::wstring();
			const int src = g["textures"][t].value("source", -1);
			return src >= 0 && src < (int)imageFile.size() ? imageFile[src] : std::wstring();
		};

		// 재질 → .mat (MToon · Unlit = Universal Render Pipeline/Unlit, 나머지 = Lit)
		std::set<std::string> usedMat;
		for (int m = 0; m < (int)g["materials"].size(); ++m)
		{
			const json& mat = g["materials"][m];
			std::string name = Safe(mat.value("name", "Material" + std::to_string(m)));
			std::string unique = name;
			for (int n = 1; usedMat.count(Lower(unique)); ++n) unique = name + "_" + std::to_string(n);
			usedMat.insert(Lower(unique));
			const fs::path file = matRel / string_to_wstring(unique + ".mat");
			result.push_back(file.wstring());
			if (fs::exists(Full(file.wstring()), ec)) continue;
			json ext = mat.value("extensions", json::object());
			// VRM 0.x: MToon 값은 extensions.VRM.materialProperties (재질 이름으로 짝) — Unity 셰이더 속성 이름
			json mtoon0;
			if (g.contains("extensions") && g["extensions"].contains("VRM"))
				for (const json& mp : g["extensions"]["VRM"].value("materialProperties", json::array()))
					if (mp.value("name", std::string()) == mat.value("name", std::string()) && mp.value("shader", std::string()).find("MToon") != std::string::npos)
						mtoon0 = mp;
			const bool mtoon = ext.contains("VRMC_materials_mtoon") || !mtoon0.is_null();
			const bool unlit = mtoon || ext.contains("KHR_materials_unlit");
			const json pbr = mat.value("pbrMetallicRoughness", json::object());
			json bc = pbr.value("baseColorFactor", json::array({ 1.0, 1.0, 1.0, 1.0 }));
			const std::string alpha = mat.value("alphaMode", std::string("OPAQUE"));
			json j;
			j["Shader"] = unlit ? "Universal Render Pipeline/Unlit" : "Universal Render Pipeline/Lit";
			j["ResourcePath"] = wstring_to_string(file.wstring());
			j["BaseMapPath"] = wstring_to_string(texFile(pbr.value("baseColorTexture", json())));
			j["NormalMapPath"] = unlit ? std::string() : wstring_to_string(texFile(mat.value("normalTexture", json())));
			j["MetallicMapPath"] = "";
			j["OcclusionMapPath"] = "";
			j["EmissionMapPath"] = wstring_to_string(texFile(mat.value("emissiveTexture", json())));
			// glTF 색 = 선형 → 엔진 재질 색 = 감마
			j["BaseColor"] = { ToGamma(bc[0].get<float>()), ToGamma(bc[1].get<float>()), ToGamma(bc[2].get<float>()), bc[3].get<float>() };
			j["Metallic"] = unlit ? 0.0f : pbr.value("metallicFactor", 1.0f);
			j["Smoothness"] = unlit ? 0.0f : 1.0f - pbr.value("roughnessFactor", 1.0f);
			j["AlphaClipping"] = alpha != "OPAQUE" ? 1 : 0;   // BLEND 도 잘라내기로 (엔진 불투명 경로)
			j["Cutoff"] = mat.value("alphaCutoff", 0.5f);
			j["ReceiveShadows"] = 1;
			j["SpecularHighlights"] = unlit ? 0 : 1;
			j["EnvironmentReflections"] = unlit ? 0 : 1;
			const json em = mat.value("emissiveFactor", json::array({ 0.0, 0.0, 0.0 }));
			const bool emissive = em[0].get<float>() + em[1].get<float>() + em[2].get<float>() > 0.001f;
			j["Emission"] = emissive;
			j["EmissionColor"] = { ToGamma(em[0].get<float>()), ToGamma(em[1].get<float>()), ToGamma(em[2].get<float>()) };
			j["EmissionIntensity"] = 1.0f;
			if (mtoon)
			{
				// MToon → lilToon (패키지 com.nova.toon). 패키지가 없으면 Unlit 으로 그린다
				//  MToon: shading = linearstep(-1 + toony, 1 - toony, N·L + shift) → lilToon 의 N·L*0.5+0.5 에서 Border = 0.5 - shift/2, Blur = 1 - toony
				json props;
				if (!mtoon0.is_null())
				{
					// VRM 0.x MToon: smoothstep(ShadeShift, ShadeShift + (1 - Toony), N·L) — 색은 Unity 색 (감마) 그대로
					const json fp = mtoon0.value("floatProperties", json::object());
					const json vp = mtoon0.value("vectorProperties", json::object());
					const json tp = mtoon0.value("textureProperties", json::object());
					auto fv = [&](const char* k, float d) { return fp.contains(k) && fp[k].is_number() ? fp[k].get<float>() : d; };
					auto col3 = [&](const char* k, std::initializer_list<float> d) {
						const json c = vp.contains(k) && vp[k].is_array() && vp[k].size() >= 3 ? vp[k] : json(d);
						return json::array({ c[0].get<float>(), c[1].get<float>(), c[2].get<float>() });
					};
					auto tex = [&](const char* k) {
						if (!tp.contains(k) || !tp[k].is_number_integer()) return std::string();
						return wstring_to_string(texFile(json{ { "index", tp[k].get<int>() } }));
					};
					const float shift = fv("_ShadeShift", 0.0f), toony = fv("_ShadeToony", 0.9f);
					props["UseShadow"] = true;
					props["ShadowColor"] = col3("_ShadeColor", { 0.97f, 0.81f, 0.86f });
					props["ShadowColorTex"] = tex("_ShadeTexture");
					props["ShadowBorder"] = std::clamp(0.5f + (shift + (1.0f - toony) * 0.5f) * 0.5f, 0.0f, 1.0f);
					props["ShadowBlur"] = std::clamp((1.0f - toony) * 0.5f, 0.01f, 1.0f);
					props["ShadowStrength"] = 1.0f;
					props["ShadowMainStrength"] = 0.0f;
					props["ShadowEnvStrength"] = 0.0f;
					props["ShadowReceive"] = std::clamp(fv("_ReceiveShadowRate", 1.0f), 0.0f, 1.0f);
					props["ShadowBorderRange"] = 0.0f;
					const json rim = col3("_RimColor", { 0.0f, 0.0f, 0.0f });
					props["UseRim"] = rim[0].get<float>() + rim[1].get<float>() + rim[2].get<float>() > 0.001f;
					props["RimColor"] = rim;
					props["RimStrength"] = 1.0f;
					props["RimBorder"] = 0.5f;
					props["RimBlur"] = 1.0f;
					props["RimFresnelPower"] = fv("_RimFresnelPower", 1.0f);
					props["RimShadowMask"] = 0.0f;
					props["RimEnableLighting"] = fv("_RimLightingMix", 0.0f);
					const int mode = (int)fv("_OutlineWidthMode", 0.0f);   // 0 없음, 1 월드 (cm), 2 화면
					const float w = fv("_OutlineWidth", 0.0f);
					const float widthCm = mode == 1 ? w : (mode == 2 ? w * 2.0f : 0.0f);
					props["UseOutline"] = widthCm > 0.0f;
					props["OutlineWidth"] = widthCm;
					props["OutlineFixWidth"] = 0.0f;
					props["OutlineColor"] = col3("_OutlineColor", { 0.0f, 0.0f, 0.0f });
					props["OutlineEnableLighting"] = fv("_OutlineLightingMix", 1.0f);
					// 기본 색 · 잘라내기 (0.x 는 glTF 재질보다 이 값이 맞다)
					if (vp.contains("_Color") && vp["_Color"].is_array() && vp["_Color"].size() == 4)
						j["BaseColor"] = vp["_Color"];
					const std::string mainTex = tex("_MainTex");
					if (!mainTex.empty()) j["BaseMapPath"] = mainTex;
					const int blend = (int)fv("_BlendMode", 0.0f);
					j["AlphaClipping"] = blend != 0 ? 1 : 0;
					j["Cutoff"] = blend == 1 ? fv("_Cutoff", 0.5f) : 0.5f;
					j["Shader"] = "lilToon";
					j["Fallback"] = "Unlit";
					j["Properties"] = props;
					j["NormalMapPath"] = tex("_BumpMap");
				}
				else
				{
				const json mt = ext["VRMC_materials_mtoon"];
				auto gamma3 = [&](const char* key, std::initializer_list<float> def) {
					const json c = mt.contains(key) ? mt[key] : json(def);
					return json::array({ ToGamma(c[0].get<float>()), ToGamma(c[1].get<float>()), ToGamma(c[2].get<float>()) });
				};
				props["UseShadow"] = true;
				props["ShadowColor"] = gamma3("shadeColorFactor", { 1.0f, 1.0f, 1.0f });
				props["ShadowColorTex"] = wstring_to_string(texFile(mt.value("shadeMultiplyTexture", json())));
				props["ShadowBorder"] = std::clamp(0.5f - mt.value("shadingShiftFactor", 0.0f) * 0.5f, 0.0f, 1.0f);
				props["ShadowBlur"] = std::clamp(1.0f - mt.value("shadingToonyFactor", 0.9f), 0.01f, 1.0f);
				props["ShadowStrength"] = 1.0f;
				props["ShadowMainStrength"] = 0.0f;
				props["ShadowEnvStrength"] = 0.0f;
				props["ShadowReceive"] = 1.0f;
				props["ShadowBorderRange"] = 0.0f;
				const json rim = gamma3("parametricRimColorFactor", { 0.0f, 0.0f, 0.0f });
				props["UseRim"] = rim[0].get<float>() + rim[1].get<float>() + rim[2].get<float>() > 0.001f;
				props["RimColor"] = rim;
				props["RimStrength"] = 1.0f;
				props["RimBorder"] = 0.5f;
				props["RimBlur"] = 1.0f;
				props["RimFresnelPower"] = mt.value("parametricRimFresnelPowerFactor", 5.0f);
				props["RimShadowMask"] = 0.0f;
				props["RimEnableLighting"] = mt.value("rimLightingMixFactor", 1.0f);
				const std::string mode = mt.value("outlineWidthMode", std::string("none"));
				const float wf = mt.value("outlineWidthFactor", 0.0f);
				// 외곽선: worldCoordinates = 미터 → cm, screenCoordinates = 화면 비율 → 대략 2 m 거리 기준
				const float widthCm = mode == "worldCoordinates" ? wf * 100.0f : (mode == "screenCoordinates" ? wf * 200.0f : 0.0f);
				props["UseOutline"] = widthCm > 0.0f;
				props["OutlineWidth"] = widthCm;
				props["OutlineFixWidth"] = 0.0f;
				props["OutlineColor"] = gamma3("outlineColorFactor", { 0.0f, 0.0f, 0.0f });
				props["OutlineEnableLighting"] = mt.value("outlineLightingMixFactor", 1.0f);
				j["Shader"] = "lilToon";
				j["Fallback"] = "Unlit";
				j["Properties"] = props;
				j["NormalMapPath"] = wstring_to_string(texFile(mat.value("normalTexture", json())));
				}
			}
			std::ofstream o(Full(file.wstring()));
			o << j.dump(4);
		}
		return result;
	}

	json DynamicBoneJson(const std::wstring& path)
	{
		json g;
		if (!ReadGlb(path, g) || !g.contains("extensions")) return nullptr;
		s_ConvVrm0 = !g["extensions"].contains("VRMC_springBone") && g["extensions"].contains("VRM");
		const json& e = g["extensions"];
		json chains = json::array(), colliders = json::array();
		if (e.contains("VRMC_springBone"))
		{
			const json& sb = e["VRMC_springBone"];
			for (const json& c : sb.value("colliders", json::array()))
			{
				json cj = { { "bone", NodeName(g, c.value("node", -1)) } };
				const json shape = c.value("shape", json::object());
				if (shape.contains("capsule"))
				{
					cj["offset"] = Conv(shape["capsule"].value("offset", json::array({ 0, 0, 0 })));
					cj["tail"] = Conv(shape["capsule"].value("tail", json::array({ 0, 0, 0 })));
					cj["radius"] = shape["capsule"].value("radius", 0.0f);
					cj["capsule"] = true;
				}
				else
				{
					const json s = shape.value("sphere", json::object());
					cj["offset"] = Conv(s.value("offset", json::array({ 0, 0, 0 })));
					cj["radius"] = s.value("radius", 0.0f);
				}
				colliders.push_back(cj);
			}
			const json groups = sb.value("colliderGroups", json::array());
			for (const json& s : sb.value("springs", json::array()))
			{
				json joints = json::array();
				for (const json& jt : s.value("joints", json::array()))
					joints.push_back({ { "bone", NodeName(g, jt.value("node", -1)) }, { "stiffness", jt.value("stiffness", 1.0f) }, { "drag", jt.value("dragForce", 0.5f) },
						{ "gravity", jt.value("gravityPower", 0.0f) }, { "gravityDir", Conv(jt.value("gravityDir", json::array({ 0, -1, 0 }))) }, { "radius", jt.value("hitRadius", 0.0f) } });
				std::set<int> cs;
				for (int gi : s.value("colliderGroups", std::vector<int>()))
					if (gi >= 0 && gi < (int)groups.size())
						for (int ci : groups[gi].value("colliders", std::vector<int>())) cs.insert(ci);
				chains.push_back({ { "name", s.value("name", std::string("Spring")) }, { "joints", joints }, { "colliders", std::vector<int>(cs.begin(), cs.end()) } });
			}
		}
		else if (e.contains("VRM") && e["VRM"].contains("secondaryAnimation"))
		{
			// VRM 0.x: 본 묶음의 뿌리부터 자식을 따라 사슬 (갈래마다 새 사슬)
			const json& sa = e["VRM"]["secondaryAnimation"];
			std::vector<std::vector<int>> groupColliders;
			for (const json& cg : sa.value("colliderGroups", json::array()))
			{
				std::vector<int> ids;
				for (const json& c : cg.value("colliders", json::array()))
				{
					ids.push_back((int)colliders.size());
					colliders.push_back({ { "bone", NodeName(g, cg.value("node", -1)) }, { "offset", Conv(c.value("offset", json::object())) }, { "radius", c.value("radius", 0.0f) } });
				}
				groupColliders.push_back(ids);
			}
			const json nodes = g.value("nodes", json::array());
			for (const json& bg : sa.value("boneGroups", json::array()))
			{
				std::set<int> cs;
				for (int gi : bg.value("colliderGroups", std::vector<int>()))
					if (gi >= 0 && gi < (int)groupColliders.size()) cs.insert(groupColliders[gi].begin(), groupColliders[gi].end());
				const json gravDir = Conv(bg.value("gravityDir", json::object({ { "x", 0 }, { "y", -1 }, { "z", 0 } })));
				auto joint = [&](int node) {
					return json{ { "bone", NodeName(g, node) }, { "stiffness", bg.value("stiffiness", 1.0f) }, { "drag", bg.value("dragForce", 0.4f) },
						{ "gravity", bg.value("gravityPower", 0.0f) }, { "gravityDir", gravDir }, { "radius", bg.value("hitRadius", 0.02f) } };
				};
				std::function<void(int)> walk = [&](int start) {
					json joints = json::array();
					int cur = start;
					while (cur >= 0 && cur < (int)nodes.size())
					{
						joints.push_back(joint(cur));
						const std::vector<int> kids = nodes[cur].value("children", std::vector<int>());
						if (kids.empty()) break;
						for (size_t k = 1; k < kids.size(); ++k) walk(kids[k]);
						cur = kids[0];
					}
					if (joints.size() >= 2)
						chains.push_back({ { "name", bg.value("comment", std::string("Spring")) }, { "joints", joints }, { "colliders", std::vector<int>(cs.begin(), cs.end()) } });
				};
				for (int root : bg.value("bones", std::vector<int>())) walk(root);
			}
		}
		if (chains.empty()) return nullptr;
		return { { "type", "DynamicBone" }, { "enabled", true }, { "chains", chains }, { "colliders", colliders } };
	}

	json ExpressionsJson(const std::wstring& path)
	{
		json g;
		bool v1 = false;
		if (!ReadGlb(path, g)) return nullptr;
		const json* vrm = VrmExt(g, v1);
		if (!vrm) return nullptr;
		// 셰이프 이름: mesh.extras.targetNames (없으면 첫 primitive 의 것) — Assimp 도 이 이름을 쓴다, 없으면 번호로 찾는다
		auto shapeName = [&](int mesh, int index) -> std::string {
			if (!g.contains("meshes") || mesh < 0 || mesh >= (int)g["meshes"].size()) return std::string();
			const json& m = g["meshes"][mesh];
			const json* names = nullptr;
			if (m.contains("extras") && m["extras"].contains("targetNames")) names = &m["extras"]["targetNames"];
			else if (m.contains("primitives") && !m["primitives"].empty() && m["primitives"][0].contains("extras") && m["primitives"][0]["extras"].contains("targetNames"))
				names = &m["primitives"][0]["extras"]["targetNames"];
			return names && index >= 0 && index < (int)names->size() && (*names)[index].is_string() ? (*names)[index].get<std::string>() : std::string();
		};
		auto meshOfNode = [&](int node) { return g.contains("nodes") && node >= 0 && node < (int)g["nodes"].size() ? g["nodes"][node].value("mesh", -1) : -1; };
		auto nodeOfMesh = [&](int mesh) {
			if (g.contains("nodes")) for (int i = 0; i < (int)g["nodes"].size(); ++i) if (g["nodes"][i].value("mesh", -1) == mesh) return i;
			return -1;
		};
		json list = json::array();
		if (v1)
		{
			const json ex = vrm->value("expressions", json::object());
			for (const char* group : { "preset", "custom" })
			{
				const json set = ex.value(group, json::object());
				for (auto it = set.begin(); it != set.end(); ++it)
				{
					const json& e = it.value();
					json binds = json::array();
					for (const json& b : e.value("morphTargetBinds", json::array()))
					{
						const int node = b.value("node", -1), index = b.value("index", -1);
						binds.push_back({ { "renderer", NodeName(g, node) }, { "shape", shapeName(meshOfNode(node), index) }, { "index", index }, { "weight", b.value("weight", 1.0f) * 100.0f } });
					}
					json ej = { { "name", it.key() }, { "binds", binds } };
					if (e.value("isBinary", false)) ej["binary"] = true;
					ej["overrideBlink"] = e.value("overrideBlink", std::string("none"));
					ej["overrideMouth"] = e.value("overrideMouth", std::string("none"));
					list.push_back(ej);
				}
			}
		}
		else
		{
			// VRM 0.x: presetName → VRM 1.0 이름, 웃음 · 화남 · 슬픔 · 놀람은 눈 깜빡임을 막는다 (1.0 의 기본과 같게)
			static const std::map<std::string, std::string> kPreset = {
				{ "joy", "happy" }, { "angry", "angry" }, { "sorrow", "sad" }, { "fun", "relaxed" }, { "surprised", "surprised" },
				{ "a", "aa" }, { "i", "ih" }, { "u", "ou" }, { "e", "ee" }, { "o", "oh" },
				{ "blink", "blink" }, { "blink_l", "blinkLeft" }, { "blink_r", "blinkRight" }, { "neutral", "neutral" },
				{ "lookup", "lookUp" }, { "lookdown", "lookDown" }, { "lookleft", "lookLeft" }, { "lookright", "lookRight" } };
			const json master = vrm->value("blendShapeMaster", json::object());
			for (const json& e : master.value("blendShapeGroups", json::array()))
			{
				const std::string preset = Lower(e.value("presetName", std::string()));
				auto it = kPreset.find(preset);
				const std::string name = it != kPreset.end() ? it->second : e.value("name", std::string("Expression"));
				json binds = json::array();
				for (const json& b : e.value("binds", json::array()))
				{
					const int mesh = b.value("mesh", -1), index = b.value("index", -1);
					binds.push_back({ { "renderer", NodeName(g, nodeOfMesh(mesh)) }, { "shape", shapeName(mesh, index) }, { "index", index }, { "weight", b.value("weight", 100.0f) } });
				}
				json ej = { { "name", name }, { "binds", binds } };
				if (e.value("isBinary", false)) ej["binary"] = true;
				if (name == "happy" || name == "angry" || name == "sad" || name == "surprised" || name == "relaxed") ej["overrideBlink"] = "block";
				list.push_back(ej);
			}
		}
		if (list.empty()) return nullptr;
		return { { "type", "Expressions" }, { "enabled", true }, { "autoBlink", true }, { "expressions", list } };
	}

	json Meta(const std::wstring& path)
	{
		json g;
		bool v1 = false;
		if (!ReadGlb(path, g)) return nullptr;
		const json* vrm = VrmExt(g, v1);
		return vrm ? vrm->value("meta", json()) : json();
	}
}
