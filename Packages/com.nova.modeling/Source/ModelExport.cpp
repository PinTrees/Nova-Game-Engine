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
		};

		Vec3 ToFile(const Vec3& v) { return Vec3(-v.x, v.y, v.z); }

		ExportMesh Build(const Object& o)
		{
			ExportMesh e;
			e.Name = o.Name;
			const Mesh& m = o.M;
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
					out.push_back(Build(o));
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

		// ============================================================== GLB
		bool WriteGlb(const std::filesystem::path& path, const std::vector<ExportMesh>& meshes, std::string& error)
		{
			std::vector<uint8_t> bin;
			auto append = [&](const void* data, size_t bytes) {
				const size_t at = bin.size();
				bin.resize(at + bytes);
				memcpy(bin.data() + at, data, bytes);
				while (bin.size() % 4) bin.push_back(0);
				return at;
			};
			nlohmann::json gltf;
			gltf["asset"] = { { "version", "2.0" }, { "generator", "NOVA Model Editor" } };
			nlohmann::json nodes = nlohmann::json::array(), gmeshes = nlohmann::json::array(), views = nlohmann::json::array(), accessors = nlohmann::json::array();
			auto view = [&](size_t offset, size_t length, int target) {
				views.push_back({ { "buffer", 0 }, { "byteOffset", offset }, { "byteLength", length }, { "target", target } });
				return (int)views.size() - 1;
			};
			for (const ExportMesh& e : meshes)
			{
				// 정점 분리: (점, UV, 법선) 이 같으면 공유
				struct Key { int P; float u, v, nx, ny, nz; bool operator<(const Key& o) const { return std::tie(P, u, v, nx, ny, nz) < std::tie(o.P, o.u, o.v, o.nx, o.ny, o.nz); } };
				std::map<Key, uint32_t> index;
				std::vector<float> pos, nrm, uv;
				std::vector<uint32_t> idx;
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
							it = index.emplace(k, (uint32_t)(pos.size() / 3 - 1)).first;
						}
						corners.push_back(it->second);
					}
					// 부채꼴 (볼록한 면 가정 — 오목한 면은 내보내기 전에 Triangulate)
					for (size_t c = 1; c + 1 < corners.size(); ++c)
						idx.insert(idx.end(), { corners[0], corners[c], corners[c + 1] });
				}
				if (idx.empty())
					continue;
				Vec3 mn(FLT_MAX, FLT_MAX, FLT_MAX), mx(-FLT_MAX, -FLT_MAX, -FLT_MAX);
				for (size_t i = 0; i < pos.size(); i += 3) { mn = Vec3::Min(mn, Vec3(pos[i], pos[i + 1], pos[i + 2])); mx = Vec3::Max(mx, Vec3(pos[i], pos[i + 1], pos[i + 2])); }
				const size_t count = pos.size() / 3;
				const int vPos = view(append(pos.data(), pos.size() * 4), pos.size() * 4, 34962);
				const int vNrm = view(append(nrm.data(), nrm.size() * 4), nrm.size() * 4, 34962);
				const int vUv = view(append(uv.data(), uv.size() * 4), uv.size() * 4, 34962);
				const int vIdx = view(append(idx.data(), idx.size() * 4), idx.size() * 4, 34963);
				const int aPos = (int)accessors.size();
				accessors.push_back({ { "bufferView", vPos }, { "componentType", 5126 }, { "count", count }, { "type", "VEC3" }, { "min", { mn.x, mn.y, mn.z } }, { "max", { mx.x, mx.y, mx.z } } });
				accessors.push_back({ { "bufferView", vNrm }, { "componentType", 5126 }, { "count", count }, { "type", "VEC3" } });
				accessors.push_back({ { "bufferView", vUv }, { "componentType", 5126 }, { "count", count }, { "type", "VEC2" } });
				accessors.push_back({ { "bufferView", vIdx }, { "componentType", 5125 }, { "count", idx.size() }, { "type", "SCALAR" } });
				nlohmann::json attrs = { { "POSITION", aPos }, { "NORMAL", aPos + 1 } };
				if (e.HasUV) attrs["TEXCOORD_0"] = aPos + 2;
				gmeshes.push_back({ { "name", e.Name }, { "primitives", { { { "attributes", attrs }, { "indices", aPos + 3 }, { "material", 0 } } } } });
				nodes.push_back({ { "name", e.Name }, { "mesh", (int)gmeshes.size() - 1 } });
			}
			if (nodes.empty()) { error = "nothing to export"; return false; }
			nlohmann::json sceneNodes = nlohmann::json::array();
			for (int i = 0; i < (int)nodes.size(); ++i) sceneNodes.push_back(i);
			gltf["scene"] = 0;
			gltf["scenes"] = { { { "nodes", sceneNodes } } };
			gltf["nodes"] = nodes;
			gltf["meshes"] = gmeshes;
			gltf["materials"] = { { { "name", "Material" }, { "pbrMetallicRoughness", { { "baseColorFactor", { 0.8, 0.8, 0.8, 1.0 } }, { "metallicFactor", 0.0 }, { "roughnessFactor", 0.5 } } } } };
			gltf["accessors"] = accessors;
			gltf["bufferViews"] = views;
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
			for (const ExportMesh& e : meshes)
			{
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
				mdl.L(modelId).S(FbxName(e.Name, "Model")).S("Mesh");
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
						FbxNode::P(pp, "DiffuseColor", "Color", "", "A", { 0.8, 0.8, 0.8 });
					}
					links.push_back({ materialIds[m], modelId });
				}
				++geometryCount;
				++modelCount;
			}
			{
				FbxNode defs("Definitions");
				defs.Add("Version").I(100);
				defs.Add("Count").I(1 + geometryCount + modelCount + (int)materialIds.size());
				defs.Add("ObjectType").S("GlobalSettings").Add("Count").I(1);
				defs.Add("ObjectType").S("Model").Add("Count").I(modelCount);
				defs.Add("ObjectType").S("Geometry").Add("Count").I(geometryCount);
				if (!materialIds.empty()) defs.Add("ObjectType").S("Material").Add("Count").I((int)materialIds.size());
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

	bool Document::Export(const std::string& path, bool selectedOnly, std::string& error) const
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
		if (ext == ".glb") return WriteGlb(p, meshes, error);
		error = "unsupported export format '" + ext + "' (use .fbx, .obj or .glb)";
		return false;
	}
}
