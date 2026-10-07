// 내보내기: FBX 7.4 바이너리 · OBJ · GLB (glTF 2.0) 를 직접 쓴다.
//  (Assimp Exporter 는 쓰지 않는다: 포함한 헤더와 DLL 의 구조체 크기가 달라 aiScene 을 손으로 만들면 위험 — FBXLoader 의 DetectQuatKeyStride 참고)
//  - 파일 좌표 = 오른손 Y 위 (FBX · OBJ · glTF 공통). 엔진 (x, y, z) → 파일 (-x, y, z), 면 감김은 반대로 (가져오기의 역)
//  - FBX: 단위 = 미터 (UnitScaleFactor 100), n 각형 그대로, 모서리 법선 · UV · 재질 번호
//  - OBJ: n 각형 그대로 (v / vt / vn), GLB: 삼각형 + 정점 분리 (위치 · UV · 법선이 같으면 공유)
#include "pch.h"
#include "ModelDocument.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <set>

namespace Modeling
{
	namespace
	{
		// 오브젝트 하나 → 파일 좌표의 다각형 + 모서리 속성
		struct ExportMesh
		{
			std::string Name;
			std::vector<Vec3> Positions;
			std::vector<std::vector<int>> Polys;        // 점 번호 (파일 감김)
			std::vector<std::vector<Vec3>> Normals;     // 모서리마다
			std::vector<std::vector<Vec2>> UVs;         // 모서리마다 (엔진 UV = 왼쪽 위 원점)
			std::vector<int> Materials;
			bool HasUV = false;
			std::vector<std::vector<std::pair<int, float>>> Skin;   // 점마다 (본 번호, 가중치) 최대 4 (아마추어가 있을 때)
			std::vector<std::string> ShapeNames;                    // 셰이프 키 (모프 타깃)
			std::vector<std::vector<Vec3>> ShapeDeltas;             // [셰이프][점] 위치 차이 (파일 좌표)
		};

		Vec3 ToFile(const Vec3& v) { return Vec3(-v.x, v.y, v.z); }

		ExportMesh Build(const Object& o, uint64 revision, const Armature& arm)
		{
			ExportMesh e;
			e.Name = o.Name;
			const Mesh m = o.RestEvaluated();   // 모디파이어 결과 (Blender 처럼 적용해서 내보낸다) — 셰이프 미리보기 없이, 셰이프는 모프 타깃으로
			const Matrix w = o.World();
			Matrix nrm = w;
			nrm.Translation(Vec3(0, 0, 0));
			// 부드러운 면의 점 법선 = 이웃 부드러운 면 법선의 평균 (면적 가중 = 정규화 전 Newell 합 대신 면 법선 × 면 수)
			std::vector<Vec3> smooth(m.Verts.size(), Vec3(0, 0, 0));
			std::vector<Vec3> faceN(m.Faces.size());
			for (int f = 0; f < (int)m.Faces.size(); ++f)
			{
				faceN[f] = m.FaceNormal(f);
				if (m.Faces[f].Smooth)
					for (int v : m.Faces[f].V) smooth[v] += faceN[f];
			}
			for (const Vert& v : m.Verts) e.Positions.push_back(ToFile(Vec3::Transform(v.P, w)));
			// 셰이프 키 → 모프 타깃 (월드 회전 · 크기만, 파일 좌표)
			e.ShapeNames = m.Shapes;
			e.ShapeDeltas.assign(m.Shapes.size(), std::vector<Vec3>(m.Verts.size(), Vec3(0, 0, 0)));
			for (size_t i = 0; i < m.Verts.size(); ++i)
				for (const auto& [sh, d] : m.Verts[i].K)
					if (sh >= 0 && sh < (int)m.Shapes.size()) e.ShapeDeltas[sh][i] = ToFile(Vec3::TransformNormal(d, w));
			if (!arm.Empty())
			{
				// 본 이름 그룹 → 가중치, 없으면 가장 가까운 본 (점이 원점에 남지 않게)
				const std::vector<int> g2b = GroupToBone(arm, m);
				e.Skin.resize(m.Verts.size());
				for (size_t i = 0; i < m.Verts.size(); ++i)
					VertexBones(arm, g2b, m.Verts[i], Vec3::Transform(m.Verts[i].P, w), true, e.Skin[i]);
			}
			for (int f = 0; f < (int)m.Faces.size(); ++f)
			{
				const Face& face = m.Faces[f];
				std::vector<int> poly(face.V.rbegin(), face.V.rend());   // 감김 반대
				std::vector<Vec3> ns;
				std::vector<Vec2> uvs;
				for (size_t c = 0; c < face.V.size(); ++c)
				{
					const size_t src = face.V.size() - 1 - c;
					Vec3 n = face.Smooth ? smooth[face.V[src]] : faceN[f];
					n = Vec3::TransformNormal(n, nrm);
					if (n.LengthSquared() < 1e-12f) n = Vec3(0, 1, 0);
					n.Normalize();
					ns.push_back(ToFile(n));
					uvs.push_back(face.UV.size() == face.V.size() ? face.UV[src] : Vec2(0, 0));
				}
				e.HasUV = e.HasUV || face.UV.size() == face.V.size();
				e.Polys.push_back(poly);
				e.Normals.push_back(ns);
				e.UVs.push_back(uvs);
				e.Materials.push_back(face.Material);
			}
			return e;
		}

		std::vector<ExportMesh> BuildAll(const Document& doc, bool selectedOnly)
		{
			std::vector<ExportMesh> out;
			for (const Object& o : doc.Objects)
				if (o.Visible && !o.M.Faces.empty() && (!selectedOnly || o.Selected))
					out.push_back(Build(o, doc.Revision, doc.Rig));
			return out;
		}

		std::string MaterialName(const Document& doc, int index)
		{
			if (index >= 0 && index < (int)doc.Materials.size() && !doc.Materials[index].empty())
				return doc.Materials[index];
			return index == 0 ? "Material" : "Material." + std::to_string(index);
		}

		// ============================================================== OBJ
		bool WriteObj(const std::filesystem::path& path, const std::vector<ExportMesh>& meshes, const Document& doc, std::string& error)
		{
			std::ofstream f(path, std::ios::binary);
			if (!f) { error = "cannot write " + U8String(path); return false; }
			f << "# NOVA Model Editor\n";
			f.precision(6);
			f << std::fixed;
			// 재질 색 (.mtl 옆에)
			{
				std::filesystem::path mtl = path;
				mtl.replace_extension(".mtl");
				std::ofstream m(mtl, std::ios::binary);
				std::vector<int> used;
				for (const ExportMesh& e : meshes) for (int i : e.Materials) if (std::find(used.begin(), used.end(), i) == used.end()) used.push_back(i);
				for (int i : used)
				{
					const Vec3 c = doc.MaterialColor(i);
					m << "newmtl " << MaterialName(doc, i) << "\nKd " << c.x << " " << c.y << " " << c.z << "\n\n";
				}
				f << "mtllib " << U8String(mtl.filename()) << "\n";
			}
			size_t vBase = 1, tBase = 1, nBase = 1;
			for (const ExportMesh& e : meshes)
			{
				f << "o " << e.Name << "\n";
				for (const Vec3& p : e.Positions) f << "v " << p.x << " " << p.y << " " << p.z << "\n";
				size_t tCount = 0, nCount = 0;
				for (size_t p = 0; p < e.Polys.size(); ++p)
				{
					for (const Vec2& t : e.UVs[p]) { f << "vt " << t.x << " " << (1.0f - t.y) << "\n"; ++tCount; }   // OBJ = 왼쪽 아래 원점
					for (const Vec3& n : e.Normals[p]) { f << "vn " << n.x << " " << n.y << " " << n.z << "\n"; ++nCount; }
				}
				int curMat = -1;
				size_t corner = 0;
				for (size_t p = 0; p < e.Polys.size(); ++p)
				{
					if (e.Materials[p] != curMat)
					{
						curMat = e.Materials[p];
						f << "usemtl " << MaterialName(doc, curMat) << "\n";
					}
					f << "f";
					for (size_t c = 0; c < e.Polys[p].size(); ++c, ++corner)
					{
						f << " " << (vBase + e.Polys[p][c]);
						if (e.HasUV) f << "/" << (tBase + corner) << "/" << (nBase + corner);
						else f << "//" << (nBase + corner);
					}
					f << "\n";
				}
				vBase += e.Positions.size();
				tBase += tCount;
				nBase += nCount;
			}
			return true;
		}

		// ============================================================== GLB (+ 스킨, + VRM 1.0)
		// 아마추어가 있으면 본 노드 (회전 없음, 이동 = 부모 머리에서) + skin (JOINTS_0 · WEIGHTS_0 · inverseBindMatrices)
		// vrm = VRMC_vrm (humanoid · meta) + VRMC_springBone (흔들림 사슬 · 충돌체) + VRMC_materials_mtoon (툰) → 엔진이 그대로 Humanoid · Dynamic Bone · lilToon
		float ToLinear(float c) { return powf(std::clamp(c, 0.0f, 1.0f), 2.2f); }

		bool WriteGlb(const std::filesystem::path& path, const std::vector<ExportMesh>& meshes, const Document& doc, bool vrm, const nlohmann::json& opts, std::string& error)
		{
			using json = nlohmann::json;
			const Armature& arm = doc.Rig;
			const bool skinned = !arm.Empty();
			if (vrm && !skinned) { error = "VRM needs an armature (model rig.humanoid, rig.weights)"; return false; }
			if (vrm)
			{
				static const char* kRequired[] = { "Hips", "Spine", "Head", "LeftUpperArm", "LeftLowerArm", "LeftHand", "RightUpperArm", "RightLowerArm", "RightHand", "LeftUpperLeg", "LeftLowerLeg", "LeftFoot", "RightUpperLeg", "RightLowerLeg", "RightFoot" };
				for (const char* h : kRequired) if (arm.FindHuman(h) < 0) { error = std::string("VRM needs the humanoid bone ") + h + " (model rig.humanoid)"; return false; }
			}
			std::vector<uint8_t> bin;
			auto append = [&](const void* data, size_t bytes) {
				const size_t at = bin.size();
				bin.resize(at + bytes);
				memcpy(bin.data() + at, data, bytes);
				while (bin.size() % 4) bin.push_back(0);
				return at;
			};
			json gltf;
			gltf["asset"] = { { "version", "2.0" }, { "generator", "NOVA Model Editor" } };
			json nodes = json::array(), gmeshes = json::array(), views = json::array(), accessors = json::array();
			auto view = [&](size_t offset, size_t length, int target) {
				json v = { { "buffer", 0 }, { "byteOffset", offset }, { "byteLength", length } };
				if (target) v["target"] = target;
				views.push_back(v);
				return (int)views.size() - 1;
			};
			// ---- 본 노드 (번호 = 본 번호), 흔들림 사슬 끝 노드
			std::vector<int> endNode(arm.Bones.size(), -1);
			json sceneNodes = json::array();
			if (skinned)
			{
				for (int i = 0; i < (int)arm.Bones.size(); ++i)
				{
					const Bone& b = arm.Bones[i];
					const Vec3 h = ToFile(b.Head), ph = b.Parent >= 0 ? ToFile(arm.Bones[b.Parent].Head) : Vec3(0, 0, 0);
					nodes.push_back({ { "name", b.Name }, { "translation", { h.x - ph.x, h.y - ph.y, h.z - ph.z } } });
					if (b.Parent < 0) sceneNodes.push_back(i);
				}
				for (int i = 0; i < (int)arm.Bones.size(); ++i)
				{
					const Bone& b = arm.Bones[i];
					if (b.Parent >= 0) nodes[b.Parent]["children"].push_back(i);
				}
				for (int i = 0; i < (int)arm.Bones.size(); ++i)
				{
					const Bone& b = arm.Bones[i];
					if (!b.Spring) continue;
					bool hasChainChild = false;
					for (const Bone& c : arm.Bones) if (c.Parent == i && c.Spring && c.Chain == b.Chain) hasChainChild = true;
					if (hasChainChild) continue;
					const Vec3 t = ToFile(b.Tail) - ToFile(b.Head);
					endNode[i] = (int)nodes.size();
					nodes.push_back({ { "name", b.Name + "_end" }, { "translation", { t.x, t.y, t.z } } });
					nodes[i]["children"].push_back(endNode[i]);
				}
			}
			// ---- 재질 (쓰는 번호마다)
			std::vector<int> usedMats;
			// 처음 쓰는 순서 (메시 노드 순서) 로 — Assimp glTF 읽기는 재질 번호를 처음 만난 순서로 다시 매긴다 (LazyDict).
			//  파일 순서가 같아야 엔진의 VRM 재질 꺼내기 (glTF 순서) 와 메시 재질 번호 (Assimp 순서) 가 맞는다
			for (const ExportMesh& e : meshes) for (int m : e.Materials) if (std::find(usedMats.begin(), usedMats.end(), m) == usedMats.end()) usedMats.push_back(m);
			json materials = json::array();
			for (int m : usedMats)
			{
				const Vec3 c = doc.MaterialColor(m);
				json mat = { { "name", MaterialName(doc, m) }, { "pbrMetallicRoughness", { { "baseColorFactor", { ToLinear(c.x), ToLinear(c.y), ToLinear(c.z), 1.0 } }, { "metallicFactor", 0.0 }, { "roughnessFactor", 0.8 } } } };
				if (vrm)
				{
					// 툰: 그림자 = 푸르스름하게 어둡게 (편집기 툰 미리보기와 같은 색), 얇은 외곽선
					const Vec3 sh = c * Vec3(0.72f, 0.68f, 0.84f);
					mat["extensions"]["VRMC_materials_mtoon"] = {
						{ "specVersion", "1.0" },
						{ "shadeColorFactor", { ToLinear(sh.x), ToLinear(sh.y), ToLinear(sh.z) } },
						{ "shadingShiftFactor", -0.05 }, { "shadingToonyFactor", 0.95 },
						{ "outlineWidthMode", "worldCoordinates" }, { "outlineWidthFactor", opts.value("outlineWidth", 0.003) },
						{ "outlineColorFactor", { ToLinear(c.x * 0.3f), ToLinear(c.y * 0.25f), ToLinear(c.z * 0.3f) } }, { "outlineLightingMixFactor", 1.0 },
						{ "parametricRimColorFactor", { 0.0, 0.0, 0.0 } } };
				}
				materials.push_back(mat);
			}
			// ---- 메시 (재질마다 primitive, 정점 속성은 같이)
			std::vector<int> meshNodes;
			std::map<ptrdiff_t, int> meshNodeOf;   // meshes 번호 → 노드 번호 (표정 묶음)
			for (const ExportMesh& e : meshes)
			{
				struct Key { int P; float u, v, nx, ny, nz; bool operator<(const Key& o) const { return std::tie(P, u, v, nx, ny, nz) < std::tie(o.P, o.u, o.v, o.nx, o.ny, o.nz); } };
				std::map<Key, uint32_t> index;
				std::vector<float> pos, nrm, uv, wts;
				std::vector<uint16_t> jnt;
				std::map<int, std::vector<uint32_t>> idxByMat;
				for (size_t p = 0; p < e.Polys.size(); ++p)
				{
					const auto& poly = e.Polys[p];
					std::vector<uint32_t> corners;
					for (size_t c = 0; c < poly.size(); ++c)
					{
						const Vec3& n = e.Normals[p][c];
						const Vec2& t = e.UVs[p][c];
						const Key k = { poly[c], t.x, t.y, n.x, n.y, n.z };
						auto it = index.find(k);
						if (it == index.end())
						{
							const Vec3& q = e.Positions[poly[c]];
							pos.insert(pos.end(), { q.x, q.y, q.z });
							nrm.insert(nrm.end(), { n.x, n.y, n.z });
							uv.insert(uv.end(), { t.x, t.y });   // glTF = 왼쪽 위 원점 (엔진과 같음)
							if (skinned)
							{
								const auto& sk = e.Skin[poly[c]];
								for (int s = 0; s < 4; ++s)
								{
									jnt.push_back(s < (int)sk.size() ? (uint16_t)sk[s].first : 0);
									wts.push_back(s < (int)sk.size() ? sk[s].second : 0.0f);
								}
							}
							it = index.emplace(k, (uint32_t)(pos.size() / 3 - 1)).first;
						}
						corners.push_back(it->second);
					}
					// 부채꼴 (볼록한 면 가정 — 오목한 면은 내보내기 전에 Triangulate)
					std::vector<uint32_t>& idx = idxByMat[e.Materials[p]];
					for (size_t c = 1; c + 1 < corners.size(); ++c)
						idx.insert(idx.end(), { corners[0], corners[c], corners[c + 1] });
				}
				if (pos.empty())
					continue;
				Vec3 mn(FLT_MAX, FLT_MAX, FLT_MAX), mx(-FLT_MAX, -FLT_MAX, -FLT_MAX);
				for (size_t i = 0; i < pos.size(); i += 3) { mn = Vec3::Min(mn, Vec3(pos[i], pos[i + 1], pos[i + 2])); mx = Vec3::Max(mx, Vec3(pos[i], pos[i + 1], pos[i + 2])); }
				const size_t count = pos.size() / 3;
				const int aPos = (int)accessors.size();
				accessors.push_back({ { "bufferView", view(append(pos.data(), pos.size() * 4), pos.size() * 4, 34962) }, { "componentType", 5126 }, { "count", count }, { "type", "VEC3" }, { "min", { mn.x, mn.y, mn.z } }, { "max", { mx.x, mx.y, mx.z } } });
				accessors.push_back({ { "bufferView", view(append(nrm.data(), nrm.size() * 4), nrm.size() * 4, 34962) }, { "componentType", 5126 }, { "count", count }, { "type", "VEC3" } });
				accessors.push_back({ { "bufferView", view(append(uv.data(), uv.size() * 4), uv.size() * 4, 34962) }, { "componentType", 5126 }, { "count", count }, { "type", "VEC2" } });
				json attrs = { { "POSITION", aPos }, { "NORMAL", aPos + 1 } };
				if (e.HasUV) attrs["TEXCOORD_0"] = aPos + 2;
				if (skinned)
				{
					attrs["JOINTS_0"] = (int)accessors.size();
					accessors.push_back({ { "bufferView", view(append(jnt.data(), jnt.size() * 2), jnt.size() * 2, 34962) }, { "componentType", 5123 }, { "count", count }, { "type", "VEC4" } });
					attrs["WEIGHTS_0"] = (int)accessors.size();
					accessors.push_back({ { "bufferView", view(append(wts.data(), wts.size() * 4), wts.size() * 4, 34962) }, { "componentType", 5126 }, { "count", count }, { "type", "VEC4" } });
				}
				json prims = json::array();
				for (int m : usedMats)   // 재질 순서 그대로 (위 참고)
				{
					auto found = idxByMat.find(m);
					if (found == idxByMat.end() || found->second.empty()) continue;
					const std::vector<uint32_t>& idx = found->second;
					const int aIdx = (int)accessors.size();
					accessors.push_back({ { "bufferView", view(append(idx.data(), idx.size() * 4), idx.size() * 4, 34963) }, { "componentType", 5125 }, { "count", idx.size() }, { "type", "SCALAR" } });
					prims.push_back({ { "attributes", attrs }, { "indices", aIdx }, { "material", (int)(std::find(usedMats.begin(), usedMats.end(), m) - usedMats.begin()) } });
				}
				// 노드 이름은 본과 겹치면 안 된다 (엔진 · Unity 가 이름으로 노드를 찾는다 — 메시 "Head" 와 본 "Head")
				std::string nodeName = e.Name;
				auto taken = [&](const std::string& n) { for (const json& nd : nodes) if (nd.value("name", std::string()) == n) return true; return false; };
				if (taken(nodeName)) { nodeName = e.Name + "_Mesh"; for (int k = 2; taken(nodeName); ++k) nodeName = e.Name + "_Mesh" + std::to_string(k); }
				json gm = { { "name", nodeName }, { "primitives", prims } };
				if (!e.ShapeNames.empty())
				{
					// 모프 타깃: 셰이프마다 POSITION 차이 (정점 분리 순서), 이름 = extras.targetNames (Assimp · UniVRM 이 읽는다)
					json targets = json::array();
					for (size_t s = 0; s < e.ShapeNames.size(); ++s)
					{
						std::vector<float> d(count * 3, 0.0f);
						for (const auto& [k, out] : index)
						{
							const Vec3& v = e.ShapeDeltas[s][k.P];
							d[out * 3] = v.x; d[out * 3 + 1] = v.y; d[out * 3 + 2] = v.z;
						}
						Vec3 dmn(FLT_MAX, FLT_MAX, FLT_MAX), dmx(-FLT_MAX, -FLT_MAX, -FLT_MAX);
						for (size_t i = 0; i < d.size(); i += 3) { dmn = Vec3::Min(dmn, Vec3(d[i], d[i + 1], d[i + 2])); dmx = Vec3::Max(dmx, Vec3(d[i], d[i + 1], d[i + 2])); }
						const int aT = (int)accessors.size();
						accessors.push_back({ { "bufferView", view(append(d.data(), d.size() * 4), d.size() * 4, 34962) }, { "componentType", 5126 }, { "count", count }, { "type", "VEC3" }, { "min", { dmn.x, dmn.y, dmn.z } }, { "max", { dmx.x, dmx.y, dmx.z } } });
						targets.push_back({ { "POSITION", aT } });
					}
					for (json& p : gm["primitives"]) p["targets"] = targets;
					gm["weights"] = std::vector<float>(e.ShapeNames.size(), 0.0f);
					gm["extras"]["targetNames"] = e.ShapeNames;
				}
				gmeshes.push_back(gm);
				json node = { { "name", nodeName }, { "mesh", (int)gmeshes.size() - 1 } };
				if (skinned) node["skin"] = 0;
				meshNodeOf[&e - meshes.data()] = (int)nodes.size();
				meshNodes.push_back((int)nodes.size());
				nodes.push_back(node);
			}
			if (meshNodes.empty()) { error = "nothing to export"; return false; }
			for (int n : meshNodes) sceneNodes.push_back(n);
			if (skinned)
			{
				// 역 바인드 = 머리 위치의 반대 이동 (열 우선 4x4)
				std::vector<float> ibm;
				json joints = json::array();
				for (int i = 0; i < (int)arm.Bones.size(); ++i)
				{
					const Vec3 h = ToFile(arm.Bones[i].Head);
					const float m[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -h.x, -h.y, -h.z, 1 };
					ibm.insert(ibm.end(), m, m + 16);
					joints.push_back(i);
				}
				const int aIbm = (int)accessors.size();
				accessors.push_back({ { "bufferView", view(append(ibm.data(), ibm.size() * 4), ibm.size() * 4, 0) }, { "componentType", 5126 }, { "count", arm.Bones.size() }, { "type", "MAT4" } });
				int root = 0;
				for (int i = 0; i < (int)arm.Bones.size(); ++i) if (arm.Bones[i].Parent < 0) { root = i; break; }
				gltf["skins"] = { { { "name", "Armature" }, { "joints", joints }, { "inverseBindMatrices", aIbm }, { "skeleton", root } } };

				// ---- 애니메이션 클립: 본마다 rotation (파일 좌표 (x, -y, -z, w)), 루트 본 translation (쉬는 자세 + 이동)
				//  루프 클립은 끝 (Length) 에 처음 키를 한 번 더 — 엔진 · Unity 가 끝에서 처음으로 이을 때 끊기지 않게
				if (!doc.Clips.empty())
				{
					json anims = json::array();
					auto addSampler = [&](json& samplers, const std::vector<float>& times, const std::vector<float>& values, const char* type) {
						const int aIn = (int)accessors.size();
						accessors.push_back({ { "bufferView", view(append(times.data(), times.size() * 4), times.size() * 4, 0) }, { "componentType", 5126 }, { "count", times.size() }, { "type", "SCALAR" },
							{ "min", { times.front() } }, { "max", { times.back() } } });
						const int aOut = (int)accessors.size();
						accessors.push_back({ { "bufferView", view(append(values.data(), values.size() * 4), values.size() * 4, 0) }, { "componentType", 5126 }, { "count", times.size() }, { "type", type } });
						samplers.push_back({ { "input", aIn }, { "output", aOut }, { "interpolation", "LINEAR" } });
						return (int)samplers.size() - 1;
					};
					for (const AnimClip& cl : doc.Clips)
					{
						json samplers = json::array(), channels = json::array();
						for (const auto& [boneName, keys] : cl.Tracks)
						{
							const int b = arm.Find(boneName);
							if (b < 0 || keys.empty()) continue;
							std::vector<float> times, values;
							auto push = [&](float t, const Quaternion& q) { times.push_back(t); values.insert(values.end(), { q.x, -q.y, -q.z, q.w }); };
							if (keys.front().Time > 1e-4f) push(0.0f, SampleRotation(cl, keys, 0.0f));
							for (const AnimKey& k : keys) push(k.Time, k.Rot);
							if (cl.Loop && keys.back().Time < cl.Length - 1e-4f) push(cl.Length, SampleRotation(cl, keys, 0.0f));
							channels.push_back({ { "sampler", addSampler(samplers, times, values, "VEC4") }, { "target", { { "node", b }, { "path", "rotation" } } } });
						}
						const int rb = arm.Find(cl.RootBone);
						if (rb >= 0 && !cl.Root.empty())
						{
							const Bone& r = arm.Bones[rb];
							const Vec3 rest = ToFile(r.Head) - (r.Parent >= 0 ? ToFile(arm.Bones[r.Parent].Head) : Vec3(0, 0, 0));
							std::vector<float> times, values;
							auto push = [&](float t, const Vec3& m) { const Vec3 v = rest + ToFile(m); times.push_back(t); values.insert(values.end(), { v.x, v.y, v.z }); };
							if (cl.Root.front().first > 1e-4f) push(0.0f, SampleRoot(cl, 0.0f));
							for (const auto& [t, m] : cl.Root) push(t, m);
							if (cl.Loop && cl.Root.back().first < cl.Length - 1e-4f) push(cl.Length, SampleRoot(cl, 0.0f));
							channels.push_back({ { "sampler", addSampler(samplers, times, values, "VEC3") }, { "target", { { "node", rb }, { "path", "translation" } } } });
						}
						if (!channels.empty()) anims.push_back({ { "name", cl.Name }, { "samplers", samplers }, { "channels", channels } });
					}
					if (!anims.empty()) gltf["animations"] = anims;
				}
			}
			gltf["scene"] = 0;
			gltf["scenes"] = { { { "nodes", sceneNodes } } };
			gltf["nodes"] = nodes;
			gltf["meshes"] = gmeshes;
			gltf["materials"] = materials;
			gltf["accessors"] = accessors;
			gltf["bufferViews"] = views;
			// GLB 도 사람 본이 다 있으면 VRMC_vrm humanoid 를 넣는다 — 엔진이 이 파일의 클립을 이름 추측 대신 정확한 매핑으로 리타게팅
			bool fullHumanoid = skinned;
			{
				static const char* kRequired[] = { "Hips", "Spine", "Head", "LeftUpperArm", "LeftLowerArm", "LeftHand", "RightUpperArm", "RightLowerArm", "RightHand", "LeftUpperLeg", "LeftLowerLeg", "LeftFoot", "RightUpperLeg", "RightLowerLeg", "RightFoot" };
				for (const char* h : kRequired) fullHumanoid = fullHumanoid && arm.FindHuman(h) >= 0;
			}
			if (vrm || fullHumanoid)
			{
				// ---- VRM 1.0: meta · humanoid
				json human = json::object();
				for (int i = 0; i < (int)arm.Bones.size(); ++i) if (!arm.Bones[i].Human.empty()) human[VrmHumanName(arm.Bones[i].Human)] = { { "node", i } };
				const std::string title = opts.value("title", std::string("NOVA Character"));
				gltf["extensions"]["VRMC_vrm"] = {
					{ "specVersion", "1.0" },
					{ "meta", { { "name", title }, { "version", "1.0" }, { "authors", { opts.value("author", std::string("NOVA Model Editor")) } },
						{ "licenseUrl", "https://vrm.dev/licenses/1.0/" }, { "avatarPermission", "onlyAuthor" }, { "allowExcessivelyViolentUsage", false }, { "allowExcessivelySexualUsage", false },
						{ "commercialUsage", "personalNonProfit" }, { "allowPoliticalOrReligiousUsage", false }, { "allowAntisocialOrHateUsage", false },
						{ "creditNotation", "required" }, { "allowRedistribution", false }, { "modification", "prohibited" } } },
					{ "humanoid", { { "humanBones", human } } } };
				// ---- 표정: 셰이프 키 이름이 VRM 프리셋이면 preset, 아니면 custom (같은 이름 셰이프가 여러 메시에 있으면 묶음 여러 개)
				{
					static const char* kPresets[] = { "happy", "angry", "sad", "relaxed", "surprised", "aa", "ih", "ou", "ee", "oh", "blink", "blinkLeft", "blinkRight", "lookUp", "lookDown", "lookLeft", "lookRight", "neutral" };
					std::vector<std::string> names;
					for (const ExportMesh& e : meshes) for (const std::string& n : e.ShapeNames) if (std::find(names.begin(), names.end(), n) == names.end()) names.push_back(n);
					json preset = json::object(), custom = json::object();
					for (const std::string& n : names)
					{
						json binds = json::array();
						for (size_t mi = 0; mi < meshes.size(); ++mi)
						{
							auto it = meshNodeOf.find((ptrdiff_t)mi);
							if (it == meshNodeOf.end()) continue;
							const auto& sn = meshes[mi].ShapeNames;
							const auto f = std::find(sn.begin(), sn.end(), n);
							if (f != sn.end()) binds.push_back({ { "node", it->second }, { "index", (int)(f - sn.begin()) }, { "weight", 1.0 } });
						}
						const bool emotion = n == "happy" || n == "angry" || n == "sad" || n == "relaxed" || n == "surprised";
						json ex = { { "morphTargetBinds", binds }, { "isBinary", false }, { "overrideBlink", emotion ? "block" : "none" }, { "overrideLookAt", "none" }, { "overrideMouth", "none" } };
						bool isPreset = false;
						for (const char* p : kPresets) isPreset = isPreset || n == p;
						(isPreset ? preset : custom)[n] = ex;
					}
					if (!names.empty()) gltf["extensions"]["VRMC_vrm"]["expressions"] = { { "preset", preset }, { "custom", custom } };
				}
				json used = { "VRMC_vrm" };
				if (vrm) used.push_back("VRMC_materials_mtoon");
				// ---- Spring Bone: 사슬 = 같은 Chain 이름의 본 (부모가 앞) + 끝 노드
				std::vector<std::string> chainNames;
				for (const Bone& b : arm.Bones) if (b.Spring && std::find(chainNames.begin(), chainNames.end(), b.Chain) == chainNames.end()) chainNames.push_back(b.Chain);
				if (!chainNames.empty())
				{
					json colliders = json::array(), group = json::array();
					for (const Collider& c : arm.Colliders)
					{
						if (c.Bone < 0) continue;
						const Vec3 o = ToFile(c.Offset);
						json shape;
						if (c.Capsule) { const Vec3 t = ToFile(c.Tail); shape["capsule"] = { { "offset", { o.x, o.y, o.z } }, { "radius", c.Radius }, { "tail", { t.x, t.y, t.z } } }; }
						else shape["sphere"] = { { "offset", { o.x, o.y, o.z } }, { "radius", c.Radius } };
						group.push_back((int)colliders.size());
						colliders.push_back({ { "node", c.Bone }, { "shape", shape } });
					}
					json springs = json::array();
					for (const std::string& cn : chainNames)
					{
						json jts = json::array();
						int last = -1;
						for (int i = 0; i < (int)arm.Bones.size(); ++i)
						{
							const Bone& b = arm.Bones[i];
							if (!b.Spring || b.Chain != cn) continue;
							jts.push_back({ { "node", i }, { "hitRadius", b.HitRadius }, { "stiffness", b.Stiffness }, { "gravityPower", b.Gravity }, { "gravityDir", { 0.0, -1.0, 0.0 } }, { "dragForce", b.Drag } });
							last = i;
						}
						if (last >= 0 && endNode[last] >= 0)
						{
							const Bone& b = arm.Bones[last];
							jts.push_back({ { "node", endNode[last] }, { "hitRadius", b.HitRadius }, { "stiffness", b.Stiffness }, { "gravityPower", b.Gravity }, { "gravityDir", { 0.0, -1.0, 0.0 } }, { "dragForce", b.Drag } });
						}
						json sp = { { "name", cn }, { "joints", jts } };
						if (!group.empty()) sp["colliderGroups"] = { 0 };
						springs.push_back(sp);
					}
					json sb = { { "specVersion", "1.0" }, { "springs", springs } };
					if (!colliders.empty()) { sb["colliders"] = colliders; sb["colliderGroups"] = { { { "name", "Body" }, { "colliders", group } } }; }
					gltf["extensions"]["VRMC_springBone"] = sb;
					used.push_back("VRMC_springBone");
				}
				gltf["extensionsUsed"] = used;
			}
			gltf["buffers"] = { { { "byteLength", bin.size() } } };
			std::string js = gltf.dump();
			while (js.size() % 4) js.push_back(' ');
			std::ofstream f(path, std::ios::binary);
			if (!f) { error = "cannot write " + U8String(path); return false; }
			auto u32 = [&](uint32_t v) { f.write((const char*)&v, 4); };
			u32(0x46546C67); u32(2); u32((uint32_t)(12 + 8 + js.size() + 8 + bin.size()));
			u32((uint32_t)js.size()); u32(0x4E4F534A); f.write(js.data(), js.size());
			u32((uint32_t)bin.size()); u32(0x004E4942); f.write((const char*)bin.data(), bin.size());
			return true;
		}

		// ============================================================== FBX 7.4 바이너리
		// 노드 = [끝 오프셋 u32][속성 수 u32][속성 바이트 u32][이름 길이 u8][이름][속성…][자식…][자식이 있으면 13 바이트 0]
		class FbxNode
		{
		public:
			std::string Name;
			std::vector<uint8_t> Props;
			uint32_t PropCount = 0;
			std::vector<FbxNode> Children;

			explicit FbxNode(std::string name) : Name(std::move(name)) {}
			FbxNode& Add(const std::string& name) { Children.emplace_back(name); return Children.back(); }

			FbxNode& I(int32_t v) { Tag('I'); Raw(&v, 4); return *this; }
			FbxNode& L(int64_t v) { Tag('L'); Raw(&v, 8); return *this; }
			FbxNode& D(double v) { Tag('D'); Raw(&v, 8); return *this; }
			FbxNode& C(bool v) { Tag('C'); uint8_t b = v ? 1 : 0; Raw(&b, 1); return *this; }
			FbxNode& S(const std::string& s) { Tag('S'); uint32_t n = (uint32_t)s.size(); Raw(&n, 4); Raw(s.data(), s.size()); return *this; }
			FbxNode& R(const void* data, size_t n) { Tag('R'); uint32_t len = (uint32_t)n; Raw(&len, 4); Raw(data, n); return *this; }
			FbxNode& Di(const std::vector<double>& a) { return Array('d', a.data(), a.size(), 8); }
			FbxNode& Ii(const std::vector<int32_t>& a) { return Array('i', a.data(), a.size(), 4); }

			// Properties70 의 P 한 줄
			static void P(FbxNode& props, const std::string& name, const std::string& type, const std::string& label, const std::string& flags, std::initializer_list<double> values, bool isInt = false)
			{
				FbxNode& p = props.Add("P");
				p.S(name).S(type).S(label).S(flags);
				for (double v : values)
				{
					if (isInt) p.I((int32_t)v);
					else p.D(v);
				}
			}

			void Write(std::vector<uint8_t>& out) const
			{
				const size_t start = out.size();
				Put32(out, 0); Put32(out, PropCount); Put32(out, (uint32_t)Props.size());
				out.push_back((uint8_t)Name.size());
				out.insert(out.end(), Name.begin(), Name.end());
				out.insert(out.end(), Props.begin(), Props.end());
				if (!Children.empty() || Props.empty())
				{
					for (const FbxNode& c : Children) c.Write(out);
					out.insert(out.end(), 13, 0);
				}
				const uint32_t end = (uint32_t)out.size();
				memcpy(out.data() + start, &end, 4);
			}

		private:
			void Tag(char t) { Props.push_back((uint8_t)t); ++PropCount; }
			void Raw(const void* d, size_t n) { const uint8_t* b = (const uint8_t*)d; Props.insert(Props.end(), b, b + n); }
			FbxNode& Array(char t, const void* data, size_t count, size_t elem)
			{
				Tag(t);
				const uint32_t n = (uint32_t)count, enc = 0, bytes = (uint32_t)(count * elem);
				Raw(&n, 4); Raw(&enc, 4); Raw(&bytes, 4); Raw(data, bytes);
				return *this;
			}
			static void Put32(std::vector<uint8_t>& out, uint32_t v) { const uint8_t* b = (const uint8_t*)&v; out.insert(out.end(), b, b + 4); }
		};

		// 바이너리 FBX 의 객체 이름 = "이름\x00\x01클래스"
		std::string FbxName(const std::string& name, const char* cls) { std::string s = name; s.push_back('\0'); s.push_back('\x01'); s += cls; return s; }

		bool WriteFbx(const std::filesystem::path& path, const std::vector<ExportMesh>& meshes, const Document& doc, std::string& error)
		{
			if (meshes.empty()) { error = "nothing to export"; return false; }
			std::vector<FbxNode> top;
			// ---- 머리
			{
				FbxNode h("FBXHeaderExtension");
				h.Add("FBXHeaderVersion").I(1003);
				h.Add("FBXVersion").I(7400);
				h.Add("EncryptionType").I(0);
				FbxNode& ts = h.Add("CreationTimeStamp");
				ts.Add("Version").I(1000);
				ts.Add("Year").I(1970); ts.Add("Month").I(1); ts.Add("Day").I(1);
				ts.Add("Hour").I(10); ts.Add("Minute").I(0); ts.Add("Second").I(0); ts.Add("Millisecond").I(0);
				h.Add("Creator").S("NOVA Model Editor");
				top.push_back(h);
				static const uint8_t kFileId[16] = { 0x28, 0xb3, 0x2a, 0xeb, 0xb6, 0x24, 0xcc, 0xc2, 0xbf, 0xc8, 0xb0, 0x2a, 0xa9, 0x2b, 0xfc, 0xf1 };
				top.emplace_back("FileId"); top.back().R(kFileId, 16);
				top.emplace_back("CreationTime"); top.back().S("1970-01-01 10:00:00:000");
				top.emplace_back("Creator"); top.back().S("NOVA Model Editor");
			}
			// ---- 전역 설정: Y 위, Z 앞, X 오른쪽 (오른손), 단위 미터
			{
				FbxNode g("GlobalSettings");
				g.Add("Version").I(1000);
				FbxNode& p = g.Add("Properties70");
				FbxNode::P(p, "UpAxis", "int", "Integer", "", { 1 }, true);
				FbxNode::P(p, "UpAxisSign", "int", "Integer", "", { 1 }, true);
				FbxNode::P(p, "FrontAxis", "int", "Integer", "", { 2 }, true);
				FbxNode::P(p, "FrontAxisSign", "int", "Integer", "", { 1 }, true);
				FbxNode::P(p, "CoordAxis", "int", "Integer", "", { 0 }, true);
				FbxNode::P(p, "CoordAxisSign", "int", "Integer", "", { 1 }, true);
				FbxNode::P(p, "OriginalUpAxis", "int", "Integer", "", { 1 }, true);
				FbxNode::P(p, "OriginalUpAxisSign", "int", "Integer", "", { 1 }, true);
				FbxNode::P(p, "UnitScaleFactor", "double", "Number", "", { 100.0 });
				FbxNode::P(p, "OriginalUnitScaleFactor", "double", "Number", "", { 100.0 });
				top.push_back(g);
			}
			const int64_t kDocId = 1000000;
			{
				FbxNode d("Documents");
				d.Add("Count").I(1);
				FbxNode& doc0 = d.Add("Document");
				doc0.L(kDocId).S("Scene").S("Scene");
				doc0.Add("Properties70");
				doc0.Add("RootNode").L(0);
				top.push_back(d);
				top.emplace_back("References");
			}
			// ---- 객체
			int64_t nextId = 2000000;
			FbxNode objects("Objects");
			std::vector<std::pair<int64_t, int64_t>> links;   // (자식, 부모)
			std::map<int, int64_t> materialIds;
			int geometryCount = 0, modelCount = 0;
			std::vector<std::tuple<int64_t, int64_t, const ExportMesh*>> meshIds;   // (Geometry, Model, 메시) — 스킨
			// 메시 모델 이름은 본과 겹치면 안 된다 (엔진 · 여러 도구가 이름으로 노드를 찾는다 — 메시 "Head" 와 본 "Head") → "<이름>_Mesh" (VRM 과 같다)
			std::set<std::string> takenNames;
			for (const Bone& b : doc.Rig.Bones)
				takenNames.insert(b.Name);
			for (const ExportMesh& e : meshes)
			{
				std::string modelName = e.Name;
				if (takenNames.count(modelName))
				{
					modelName = e.Name + "_Mesh";
					for (int k = 2; takenNames.count(modelName); ++k)
						modelName = e.Name + "_Mesh" + std::to_string(k);
				}
				takenNames.insert(modelName);
				const int64_t geomId = nextId++, modelId = nextId++;
				FbxNode& g = objects.Add("Geometry");
				g.L(geomId).S(FbxName(e.Name, "Geometry")).S("Mesh");
				g.Add("Properties70");
				g.Add("GeometryVersion").I(124);
				std::vector<double> verts;
				for (const Vec3& p : e.Positions) verts.insert(verts.end(), { (double)p.x, (double)p.y, (double)p.z });
				std::vector<int32_t> pvi;
				std::vector<double> normals, uvs;
				std::vector<int32_t> uvIndex, mats;
				for (size_t p = 0; p < e.Polys.size(); ++p)
				{
					const auto& poly = e.Polys[p];
					for (size_t c = 0; c < poly.size(); ++c)
					{
						pvi.push_back(c + 1 == poly.size() ? ~poly[c] : poly[c]);   // 마지막 모서리 = -(i+1)
						const Vec3& n = e.Normals[p][c];
						normals.insert(normals.end(), { (double)n.x, (double)n.y, (double)n.z });
						const Vec2& t = e.UVs[p][c];
						uvs.insert(uvs.end(), { (double)t.x, 1.0 - (double)t.y });   // FBX = 왼쪽 아래 원점
						uvIndex.push_back((int32_t)uvIndex.size());
					}
				}
				// 재질: 이 오브젝트가 쓰는 재질만, 쓰는 순서대로 0, 1, … (Model 에 이 순서로 잇는다)
				std::vector<int> used;
				for (int m : e.Materials) if (std::find(used.begin(), used.end(), m) == used.end()) used.push_back(m);
				for (int m : e.Materials) mats.push_back((int32_t)(std::find(used.begin(), used.end(), m) - used.begin()));
				g.Add("Vertices").Di(verts);
				g.Add("PolygonVertexIndex").Ii(pvi);
				{
					FbxNode& ln = g.Add("LayerElementNormal");
					ln.I(0);
					ln.Add("Version").I(101);
					ln.Add("Name").S("");
					ln.Add("MappingInformationType").S("ByPolygonVertex");
					ln.Add("ReferenceInformationType").S("Direct");
					ln.Add("Normals").Di(normals);
				}
				if (e.HasUV)
				{
					FbxNode& lu = g.Add("LayerElementUV");
					lu.I(0);
					lu.Add("Version").I(101);
					lu.Add("Name").S("UVMap");
					lu.Add("MappingInformationType").S("ByPolygonVertex");
					lu.Add("ReferenceInformationType").S("IndexToDirect");
					lu.Add("UV").Di(uvs);
					lu.Add("UVIndex").Ii(uvIndex);
				}
				{
					FbxNode& lm = g.Add("LayerElementMaterial");
					lm.I(0);
					lm.Add("Version").I(101);
					lm.Add("Name").S("");
					lm.Add("MappingInformationType").S("ByPolygon");
					lm.Add("ReferenceInformationType").S("IndexToDirect");
					lm.Add("Materials").Ii(mats);
				}
				{
					FbxNode& layer = g.Add("Layer");
					layer.I(0);
					layer.Add("Version").I(100);
					auto element = [&](const char* type) {
						FbxNode& le = layer.Add("LayerElement");
						le.Add("Type").S(type);
						le.Add("TypedIndex").I(0);
					};
					element("LayerElementNormal");
					if (e.HasUV) element("LayerElementUV");
					element("LayerElementMaterial");
				}
				FbxNode& mdl = objects.Add("Model");
				mdl.L(modelId).S(FbxName(modelName, "Model")).S("Mesh");
				mdl.Add("Version").I(232);
				FbxNode& mp = mdl.Add("Properties70");
				FbxNode::P(mp, "Lcl Translation", "Lcl Translation", "", "A", { 0.0, 0.0, 0.0 });
				FbxNode::P(mp, "Lcl Rotation", "Lcl Rotation", "", "A", { 0.0, 0.0, 0.0 });
				FbxNode::P(mp, "Lcl Scaling", "Lcl Scaling", "", "A", { 1.0, 1.0, 1.0 });
				FbxNode::P(mp, "DefaultAttributeIndex", "int", "Integer", "", { 0 }, true);
				mdl.Add("Shading").C(true);
				mdl.Add("Culling").S("CullingOff");
				links.push_back({ modelId, 0 });
				links.push_back({ geomId, modelId });
				for (int m : used)
				{
					if (!materialIds.count(m))
					{
						const int64_t matId = nextId++;
						materialIds[m] = matId;
						FbxNode& mat = objects.Add("Material");
						mat.L(matId).S(FbxName(MaterialName(doc, m), "Material")).S("");
						mat.Add("Version").I(102);
						mat.Add("ShadingModel").S("phong");
						mat.Add("MultiLayer").I(0);
						FbxNode& pp = mat.Add("Properties70");
						const Vec3 dc = doc.MaterialColor(m);
						FbxNode::P(pp, "DiffuseColor", "Color", "", "A", { (double)dc.x, (double)dc.y, (double)dc.z });
					}
					links.push_back({ materialIds[m], modelId });
				}
				++geometryCount;
				++modelCount;
				meshIds.push_back({ geomId, modelId, &e });
			}
			// ---- 아마추어: 본 = LimbNode 모델 (회전 없음, 이동 = 부모 머리에서), 메시마다 Skin + 본마다 Cluster, BindPose
			//  (Unity · Blender · Assimp 가 읽는 FBX SDK 의 형식. 행렬 = 열 우선 16 개, 미터)
			const Armature& arm = doc.Rig;
			int attrCount = 0, deformerCount = 0, poseCount = 0;
			if (!arm.Empty())
			{
				auto translation = [](const Vec3& t) { return std::vector<double>{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, t.x, t.y, t.z, 1 }; };
				std::vector<int64_t> boneModel(arm.Bones.size());
				for (int i = 0; i < (int)arm.Bones.size(); ++i)
				{
					const Bone& b = arm.Bones[i];
					const int64_t attrId = nextId++, id = nextId++;
					FbxNode& na = objects.Add("NodeAttribute");
					na.L(attrId).S(FbxName(b.Name, "NodeAttribute")).S("LimbNode");
					FbxNode::P(na.Add("Properties70"), "Size", "double", "Number", "", { 10.0 });
					na.Add("TypeFlags").S("Skeleton");
					FbxNode& m = objects.Add("Model");
					m.L(id).S(FbxName(b.Name, "Model")).S("LimbNode");
					m.Add("Version").I(232);
					FbxNode& mp = m.Add("Properties70");
					const Vec3 h = ToFile(b.Head), ph = b.Parent >= 0 ? ToFile(arm.Bones[b.Parent].Head) : Vec3(0, 0, 0);
					FbxNode::P(mp, "Lcl Translation", "Lcl Translation", "", "A", { (double)(h.x - ph.x), (double)(h.y - ph.y), (double)(h.z - ph.z) });
					FbxNode::P(mp, "Lcl Rotation", "Lcl Rotation", "", "A", { 0.0, 0.0, 0.0 });
					FbxNode::P(mp, "Lcl Scaling", "Lcl Scaling", "", "A", { 1.0, 1.0, 1.0 });
					FbxNode::P(mp, "DefaultAttributeIndex", "int", "Integer", "", { 0 }, true);
					m.Add("Shading").C(true);
					m.Add("Culling").S("CullingOff");
					boneModel[i] = id;
					links.push_back({ attrId, id });
					++attrCount;
					++modelCount;
				}
				for (int i = 0; i < (int)arm.Bones.size(); ++i)
					links.push_back({ boneModel[i], arm.Bones[i].Parent >= 0 ? boneModel[arm.Bones[i].Parent] : 0 });
				// 메시마다 Skin, 그 메시에 쓰이는 본마다 Cluster (점 번호 · 가중치, Transform = 메시 (단위), TransformLink = 본 전역)
				// (objects 에 더하면 앞의 참조가 무효가 될 수 있어 Pose 는 맨 끝에 만든다)
				std::vector<std::pair<int64_t, std::vector<double>>> poseNodes;
				auto poseNode = [&](int64_t node, const std::vector<double>& mtx) { poseNodes.push_back({ node, mtx }); };
				for (const auto& [geomId, modelId, em] : meshIds)
				{
					if (em->Skin.empty()) continue;
					const int64_t skinId = nextId++;
					FbxNode& skin = objects.Add("Deformer");
					skin.L(skinId).S(FbxName(em->Name, "Deformer")).S("Skin");
					skin.Add("Version").I(101);
					skin.Add("Link_DeformAcuracy").D(50.0);
					links.push_back({ skinId, geomId });
					++deformerCount;
					std::vector<std::vector<int32_t>> idx(arm.Bones.size());
					std::vector<std::vector<double>> wts(arm.Bones.size());
					for (size_t v = 0; v < em->Skin.size(); ++v)
						for (const auto& [bone, w] : em->Skin[v])
							if (bone >= 0 && bone < (int)arm.Bones.size() && w > 0.0f) { idx[bone].push_back((int32_t)v); wts[bone].push_back(w); }
					for (int b = 0; b < (int)arm.Bones.size(); ++b)
					{
						if (idx[b].empty()) continue;
						const int64_t clusterId = nextId++;
						FbxNode& cl = objects.Add("Deformer");
						cl.L(clusterId).S(FbxName(arm.Bones[b].Name, "SubDeformer")).S("Cluster");
						cl.Add("Version").I(100);
						cl.Add("UserData").S("").S("");
						cl.Add("Indexes").Ii(idx[b]);
						cl.Add("Weights").Di(wts[b]);
						cl.Add("Transform").Di(translation(Vec3(0, 0, 0)));
						cl.Add("TransformLink").Di(translation(ToFile(arm.Bones[b].Head)));
						links.push_back({ clusterId, skinId });
						links.push_back({ boneModel[b], clusterId });
						++deformerCount;
					}
					poseNode(modelId, translation(Vec3(0, 0, 0)));
				}
				for (int i = 0; i < (int)arm.Bones.size(); ++i) poseNode(boneModel[i], translation(ToFile(arm.Bones[i].Head)));
				FbxNode& pose = objects.Add("Pose");
				pose.L(nextId++).S(FbxName("BindPose", "Pose")).S("BindPose");
				pose.Add("Type").S("BindPose");
				pose.Add("Version").I(100);
				pose.Add("NbPoseNodes").I((int32_t)poseNodes.size());
				for (const auto& [node, mtx] : poseNodes)
				{
					FbxNode& pn = pose.Add("PoseNode");
					pn.Add("Node").L(node);
					pn.Add("Matrix").Di(mtx);
				}
				poseCount = 1;
			}
			{
				FbxNode defs("Definitions");
				defs.Add("Version").I(100);
				defs.Add("Count").I(1 + geometryCount + modelCount + (int)materialIds.size() + attrCount + deformerCount + poseCount);
				defs.Add("ObjectType").S("GlobalSettings").Add("Count").I(1);
				defs.Add("ObjectType").S("Model").Add("Count").I(modelCount);
				defs.Add("ObjectType").S("Geometry").Add("Count").I(geometryCount);
				if (!materialIds.empty()) defs.Add("ObjectType").S("Material").Add("Count").I((int)materialIds.size());
				if (attrCount) defs.Add("ObjectType").S("NodeAttribute").Add("Count").I(attrCount);
				if (deformerCount) defs.Add("ObjectType").S("Deformer").Add("Count").I(deformerCount);
				if (poseCount) defs.Add("ObjectType").S("Pose").Add("Count").I(poseCount);
				top.push_back(defs);
			}
			top.push_back(objects);
			{
				FbxNode con("Connections");
				for (auto& [child, parent] : links) con.Add("C").S("OO").L(child).L(parent);
				top.push_back(con);
			}
			std::vector<uint8_t> out;
			const char magic[] = "Kaydara FBX Binary  ";
			out.insert(out.end(), magic, magic + 21);   // 끝의 '\0' 포함
			out.push_back(0x1A);
			out.push_back(0x00);
			const uint32_t version = 7400;
			out.insert(out.end(), (const uint8_t*)&version, (const uint8_t*)&version + 4);
			for (const FbxNode& n : top) n.Write(out);
			out.insert(out.end(), 13, 0);
			// 꼬리 (Blender 와 같은 고정 값)
			static const uint8_t kFoot[16] = { 0xfa, 0xbc, 0xab, 0x09, 0xd0, 0xc8, 0xd4, 0x66, 0xb1, 0x76, 0xfb, 0x83, 0x1c, 0xf7, 0x26, 0x7e };
			out.insert(out.end(), kFoot, kFoot + 16);
			out.insert(out.end(), 4, 0);
			size_t pad = ((out.size() + 15) & ~(size_t)15) - out.size();
			if (pad == 0) pad = 16;
			out.insert(out.end(), pad, 0);
			out.insert(out.end(), (const uint8_t*)&version, (const uint8_t*)&version + 4);
			out.insert(out.end(), 120, 0);
			static const uint8_t kMagic[16] = { 0xf8, 0x5a, 0x8c, 0x6a, 0xde, 0xf5, 0xd9, 0x7e, 0xec, 0xe9, 0x0c, 0xe3, 0x75, 0x8f, 0x29, 0x0b };
			out.insert(out.end(), kMagic, kMagic + 16);
			std::ofstream f(path, std::ios::binary);
			if (!f) { error = "cannot write " + U8String(path); return false; }
			f.write((const char*)out.data(), out.size());
			return true;
		}
	}

	bool Document::Export(const std::string& path, bool selectedOnly, std::string& error, const nlohmann::json& options) const
	{
		const std::filesystem::path p = PathU8(path);
		std::string ext = U8String(p.extension());
		std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
		const std::vector<ExportMesh> meshes = BuildAll(*this, selectedOnly);
		if (meshes.empty())
		{
			error = "nothing to export (no visible meshes" + std::string(selectedOnly ? " selected)" : ")");
			return false;
		}
		std::error_code ec;
		if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
		if (ext == ".fbx") return WriteFbx(p, meshes, *this, error);
		if (ext == ".obj") return WriteObj(p, meshes, *this, error);
		if (ext == ".glb") return WriteGlb(p, meshes, *this, false, options, error);
		if (ext == ".vrm") return WriteGlb(p, meshes, *this, true, options, error);
		error = "unsupported export format '" + ext + "' (use .fbx, .obj, .glb or .vrm)";
		return false;
	}
}
