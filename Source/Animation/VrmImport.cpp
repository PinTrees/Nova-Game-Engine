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

		// glTF (오른손) 로컬 벡터 · 방향 → 엔진 (FBXLoader 와 같은 변환)
		json Conv(const json& v)
		{
			if (v.is_array() && v.size() == 3) return json::array({ -v[0].get<float>(), v[1].get<float>(), v[2].get<float>() });
			if (v.is_object()) return json::array({ -v.value("x", 0.0f), v.value("y", 0.0f), v.value("z", 0.0f) });
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

	bool ReadGlb(const std::wstring& path, json& out, std::vector<uint8_t>* bin)
	{
		std::ifstream f(Full(path), std::ios::binary);
		if (!f) return false;
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
			const json ext = mat.value("extensions", json::object());
			const bool unlit = ext.contains("KHR_materials_unlit") || ext.contains("VRMC_materials_mtoon");
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
			std::ofstream o(Full(file.wstring()));
			o << j.dump(4);
		}
		return result;
	}

	json DynamicBoneJson(const std::wstring& path)
	{
		json g;
		if (!ReadGlb(path, g) || !g.contains("extensions")) return nullptr;
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

	json Meta(const std::wstring& path)
	{
		json g;
		bool v1 = false;
		if (!ReadGlb(path, g)) return nullptr;
		const json* vrm = VrmExt(g, v1);
		return vrm ? vrm->value("meta", json()) : json();
	}
}
