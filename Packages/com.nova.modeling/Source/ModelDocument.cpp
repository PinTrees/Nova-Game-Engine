#include "pch.h"
#include "ModelDocument.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/config.h>
#include <filesystem>
#include <fstream>

namespace Modeling
{
	namespace
	{
		constexpr size_t kUndoLimit = 64;
		constexpr size_t kUndoBytes = 256u << 20;   // 스냅숏 합이 이보다 크면 오래된 것부터 버린다

		std::filesystem::path U8(const std::string& s) { return PathU8(s); }

		nlohmann::json Vec(const Vec3& v) { return { v.x, v.y, v.z }; }
		Vec3 ToVec3(const nlohmann::json& j, const Vec3& def)
		{
			return j.is_array() && j.size() == 3 ? Vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>()) : def;
		}

		// FBXLoader 와 같은 축: Assimp ConvertToLeftHanded 뒤 Y 축 180° (Unity 와 같은 방향, 캐릭터 앞 = +Z)
		Vec3 ToEngine(const aiVector3D& v) { return Vec3(-v.x, v.y, -v.z); }

		Matrix ToEngineMatrix(const aiMatrix4x4& m)
		{
			// Assimp 열 벡터 → 행 벡터 (전치) 후 R·M·R, R = diag(-1, 1, -1)
			Matrix r(m.a1, m.b1, m.c1, m.d1, m.a2, m.b2, m.c2, m.d2, m.a3, m.b3, m.c3, m.d3, m.a4, m.b4, m.c4, m.d4);
			const float s[4] = { -1.0f, 1.0f, -1.0f, 1.0f };
			for (int i = 0; i < 4; ++i)
				for (int k = 0; k < 4; ++k)
					r.m[i][k] *= s[i] * s[k];
			return r;
		}
	}

	Document& Doc()
	{
		static Document s_Doc;
		return s_Doc;
	}

	int Document::Find(const std::string& name) const
	{
		for (int i = 0; i < (int)Objects.size(); ++i)
			if (Objects[i].Name == name)
				return i;
		return -1;
	}

	std::string Document::UniqueName(const std::string& base) const
	{
		if (Find(base) < 0)
			return base;
		for (int n = 1;; ++n)
		{
			char buf[16];
			snprintf(buf, sizeof(buf), ".%03d", n);   // Blender: Cube.001
			if (Find(base + buf) < 0)
				return base + buf;
		}
	}

	bool RefImage::Load(std::string& error)
	{
		DirectX::ScratchImage img, conv;
		const std::filesystem::path p = PathU8(Path);
		HRESULT hr = DirectX::LoadFromWICFile(p.wstring().c_str(), DirectX::WIC_FLAGS_IGNORE_SRGB, nullptr, img);
		if (FAILED(hr))
		{
			error = "cannot read image " + Path;
			return false;
		}
		const DirectX::Image* src = img.GetImage(0, 0, 0);
		if (src->format != DXGI_FORMAT_R8G8B8A8_UNORM)
		{
			hr = DirectX::Convert(*src, DXGI_FORMAT_R8G8B8A8_UNORM, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, conv);
			if (FAILED(hr)) { error = "cannot convert image " + Path; return false; }
			src = conv.GetImage(0, 0, 0);
		}
		W = (int)src->width;
		H = (int)src->height;
		Pixels.resize((size_t)W * H);
		for (int y = 0; y < H; ++y)
			memcpy(Pixels.data() + (size_t)y * W, src->pixels + y * src->rowPitch, (size_t)W * 4);
		return true;
	}

	RefImage* Document::FindRef(const std::string& name)
	{
		for (RefImage& r : Refs) if (r.Name == name) return &r;
		return nullptr;
	}

	bool Document::SaveCheckpoint(const std::string& name)
	{
		if (name.empty()) return false;
		m_Checkpoints[name] = Serialize();
		return true;
	}

	bool Document::RestoreCheckpoint(const std::string& name)
	{
		auto it = m_Checkpoints.find(name);
		if (it == m_Checkpoints.end()) return false;
		PushUndo("Checkpoint " + name);
		Deserialize(it->second);
		return true;
	}

	std::vector<std::string> Document::CheckpointNames() const
	{
		std::vector<std::string> out;
		for (const auto& [k, v] : m_Checkpoints) out.push_back(k);
		return out;
	}

	void Document::New()
	{
		Objects.clear();
		Materials.clear();
		Refs.clear();
		m_Checkpoints.clear();
		Active = -1;
		EditMode = false;
		Path.clear();
		SourcePath.clear();
		m_Undo.clear();
		m_Redo.clear();
		Dirty = false;
		++Revision;
	}

	int Document::AddObject(const std::string& name)
	{
		for (Object& o : Objects) o.Selected = false;
		Object o;
		o.Name = UniqueName(name.empty() ? "Object" : name);
		o.Selected = true;
		Objects.push_back(std::move(o));
		Active = (int)Objects.size() - 1;
		Changed();
		return Active;
	}

	// ------------------------------------------------------------------ JSON
	nlohmann::json Document::ToJson() const
	{
		nlohmann::json j;
		j["format"] = "nova-model";
		j["version"] = 1;
		j["materials"] = Materials;
		nlohmann::json objs = nlohmann::json::array();
		for (const Object& o : Objects)
		{
			nlohmann::json oj;
			oj["name"] = o.Name;
			oj["position"] = Vec(o.Position);
			oj["rotation"] = { o.Rotation.x, o.Rotation.y, o.Rotation.z, o.Rotation.w };
			oj["scale"] = Vec(o.Scale);
			if (!o.Visible) oj["visible"] = false;
			oj["mesh"] = o.M.ToJson();
			objs.push_back(oj);
		}
		j["objects"] = objs;
		if (!Refs.empty())
		{
			nlohmann::json refs = nlohmann::json::array();
			for (const RefImage& r : Refs)
				refs.push_back({ { "name", r.Name }, { "path", r.Path }, { "view", r.View }, { "center", Vec(r.Center) }, { "height", r.Height }, { "opacity", r.Opacity }, { "visible", r.Visible } });
			j["references"] = refs;
		}
		return j;
	}

	void Document::FromJson(const nlohmann::json& j)
	{
		Objects.clear();
		Refs.clear();
		if (j.contains("references"))
			for (const auto& rj : j["references"])
			{
				RefImage r;
				r.Name = rj.value("name", std::string("Ref"));
				r.Path = rj.value("path", std::string());
				r.View = rj.value("view", std::string("front"));
				r.Center = ToVec3(rj.value("center", nlohmann::json()), r.Center);
				r.Height = rj.value("height", r.Height);
				r.Opacity = rj.value("opacity", r.Opacity);
				r.Visible = rj.value("visible", true);
				std::string err;
				r.Load(err);   // 그림이 없어도 설정은 남긴다
				Refs.push_back(std::move(r));
			}
		Materials = j.value("materials", std::vector<std::string>());
		if (j.contains("objects"))
			for (const auto& oj : j["objects"])
			{
				Object o;
				o.Name = oj.value("name", std::string("Object"));
				o.Position = ToVec3(oj.value("position", nlohmann::json()), Vec3(0, 0, 0));
				if (oj.contains("rotation") && oj["rotation"].size() == 4)
					o.Rotation = Quaternion(oj["rotation"][0].get<float>(), oj["rotation"][1].get<float>(), oj["rotation"][2].get<float>(), oj["rotation"][3].get<float>());
				o.Scale = ToVec3(oj.value("scale", nlohmann::json()), Vec3(1, 1, 1));
				o.Visible = oj.value("visible", true);
				if (oj.contains("mesh")) o.M.FromJson(oj["mesh"]);
				Objects.push_back(std::move(o));
			}
		Active = Objects.empty() ? -1 : 0;
		EditMode = false;
		++Revision;
	}

	bool Document::Save(const std::string& path, std::string& error)
	{
		std::ofstream f(U8(path), std::ios::binary);
		if (!f)
		{
			error = "cannot write " + path;
			return false;
		}
		f << ToJson().dump(1, '\t');
		Path = path;
		Dirty = false;
		return true;
	}

	bool Document::Load(const std::string& path, std::string& error)
	{
		std::ifstream f(U8(path), std::ios::binary);
		if (!f)
		{
			error = "cannot read " + path;
			return false;
		}
		nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
		if (j.is_discarded() || j.value("format", std::string()) != "nova-model")
		{
			error = "not a .nmodel file: " + path;
			return false;
		}
		New();
		FromJson(j);
		Path = path;
		Dirty = false;
		return true;
	}

	// ------------------------------------------------------------------ 가져오기 (Assimp)
	bool Document::Import(const std::string& path, bool append, std::string& error)
	{
		Assimp::Importer importer;
		importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
		// 삼각형으로 나누지 않는다 (사각형 그대로 편집), 같은 점은 합친다
		const unsigned flags = aiProcess_ConvertToLeftHanded | aiProcess_JoinIdenticalVertices | aiProcess_ValidateDataStructure;
		const aiScene* scene = importer.ReadFile(path, flags);
		if (scene == nullptr || scene->mRootNode == nullptr)
		{
			error = std::string("import failed: ") + importer.GetErrorString();
			return false;
		}
		// FBX 단위 → 미터 (UnitScaleFactor 는 cm 기준)
		double unit = 1.0;
		if (scene->mMetaData != nullptr)
		{
			float fv = 0.0f;
			double dv = 0.0;
			if (scene->mMetaData->Get("UnitScaleFactor", dv) && dv > 0.0) unit = dv * 0.01;
			else if (scene->mMetaData->Get("UnitScaleFactor", fv) && fv > 0.0f) unit = fv * 0.01;
		}
		if (!append)
			New();
		const int materialBase = (int)Materials.size();
		for (unsigned i = 0; i < scene->mNumMaterials; ++i)
		{
			aiString name;
			scene->mMaterials[i]->Get(AI_MATKEY_NAME, name);
			Materials.push_back(name.length ? name.C_Str() : "Material");
		}
		const Matrix unitScale = Matrix::CreateScale((float)unit);
		int added = 0;
		std::function<void(const aiNode*, const Matrix&)> walk = [&](const aiNode* node, const Matrix& parent) {
			const Matrix world = ToEngineMatrix(node->mTransformation) * parent;
			for (unsigned k = 0; k < node->mNumMeshes; ++k)
			{
				const aiMesh* am = scene->mMeshes[node->mMeshes[k]];
				if (am == nullptr || am->mNumVertices == 0 || !(am->mPrimitiveTypes & (aiPrimitiveType_TRIANGLE | aiPrimitiveType_POLYGON)))
					continue;
				Object o;
				std::string name = node->mName.length ? node->mName.C_Str() : (am->mName.length ? am->mName.C_Str() : "Mesh");
				if (node->mNumMeshes > 1 && am->mName.length) name = am->mName.C_Str();
				o.Name = UniqueName(name);
				const Matrix bake = world * unitScale;
				o.M.Verts.reserve(am->mNumVertices);
				for (unsigned v = 0; v < am->mNumVertices; ++v)
					o.M.Verts.push_back({ Vec3::Transform(ToEngine(am->mVertices[v]), bake), false });
				const bool hasUV = am->HasTextureCoords(0);
				// 회전·크기만 (법선)
				Matrix nrm = bake;
				nrm.Translation(Vec3(0, 0, 0));
				for (unsigned f = 0; f < am->mNumFaces; ++f)
				{
					const aiFace& af = am->mFaces[f];
					if (af.mNumIndices < 3) continue;
					Face face;
					face.Material = materialBase + (int)am->mMaterialIndex;
					for (unsigned c = 0; c < af.mNumIndices; ++c)
					{
						face.V.push_back((int)af.mIndices[c]);
						if (hasUV) face.UV.push_back(Vec2(am->mTextureCoords[0][af.mIndices[c]].x, am->mTextureCoords[0][af.mIndices[c]].y));
					}
					o.M.Faces.push_back(face);
				}
				o.M.Touch();
				// Shade Smooth: 모서리 법선이 면 법선과 다르면 부드러운 면
				if (am->HasNormals())
					for (int f = 0; f < (int)o.M.Faces.size(); ++f)
					{
						const Vec3 fn = o.M.FaceNormal(f);
						for (int vi : o.M.Faces[f].V)
						{
							Vec3 n = Vec3::TransformNormal(ToEngine(am->mNormals[vi]), nrm);
							n.Normalize();
							if (n.Dot(fn) < 0.9995f) { o.M.Faces[f].Smooth = true; break; }
						}
					}
				// 법선 · UV 때문에 갈라진 점을 위치로 합친다 (모서리 UV 는 면에 남아 있다)
				o.M.MergeByDistance(1e-6f, false);
				o.M.Cleanup();
				for (Object& other : Objects) other.Selected = false;
				o.Selected = true;
				Objects.push_back(std::move(o));
				Active = (int)Objects.size() - 1;
				++added;
			}
			for (unsigned c = 0; c < node->mNumChildren; ++c)
				walk(node->mChildren[c], world);
		};
		walk(scene->mRootNode, Matrix::Identity);
		if (added == 0)
		{
			error = "no polygon meshes in " + path;
			return false;
		}
		if (!append)
		{
			SourcePath = path;
			Dirty = false;
		}
		else
			Dirty = true;
		++Revision;
		return true;
	}

	// ------------------------------------------------------------------ Undo
	std::string Document::Serialize() const
	{
		nlohmann::json j;
		j["materials"] = Materials;
		j["active"] = Active;
		j["edit"] = EditMode;
		j["mode"] = (int)Mode;
		nlohmann::json objs = nlohmann::json::array();
		for (const Object& o : Objects)
		{
			objs.push_back({ { "name", o.Name }, { "p", Vec(o.Position) }, { "r", { o.Rotation.x, o.Rotation.y, o.Rotation.z, o.Rotation.w } },
				{ "s", Vec(o.Scale) }, { "vis", o.Visible }, { "sel", o.Selected }, { "mesh", o.M.ToJson(true) } });
		}
		j["objects"] = objs;
		return j.dump();
	}

	void Document::Deserialize(const std::string& data)
	{
		const nlohmann::json j = nlohmann::json::parse(data, nullptr, false);
		if (j.is_discarded())
			return;
		Materials = j.value("materials", std::vector<std::string>());
		Objects.clear();
		for (const auto& oj : j["objects"])
		{
			Object o;
			o.Name = oj.value("name", std::string("Object"));
			o.Position = ToVec3(oj["p"], Vec3(0, 0, 0));
			o.Rotation = Quaternion(oj["r"][0].get<float>(), oj["r"][1].get<float>(), oj["r"][2].get<float>(), oj["r"][3].get<float>());
			o.Scale = ToVec3(oj["s"], Vec3(1, 1, 1));
			o.Visible = oj.value("vis", true);
			o.Selected = oj.value("sel", false);
			o.M.FromJson(oj["mesh"]);
			Objects.push_back(std::move(o));
		}
		Active = j.value("active", -1);
		if (Active >= (int)Objects.size()) Active = (int)Objects.size() - 1;
		EditMode = j.value("edit", false) && Active >= 0;
		Mode = (SelectMode)j.value("mode", 0);
		Changed();
	}

	void Document::PushUndo(const std::string& label)
	{
		m_Undo.push_back({ label, Serialize() });
		m_Redo.clear();
		size_t bytes = 0;
		for (const Snapshot& s : m_Undo) bytes += s.Data.size();
		while (m_Undo.size() > 1 && (m_Undo.size() > kUndoLimit || bytes > kUndoBytes))
		{
			bytes -= m_Undo.front().Data.size();
			m_Undo.erase(m_Undo.begin());
		}
	}

	bool Document::Undo()
	{
		if (m_Undo.empty())
			return false;
		Snapshot s = std::move(m_Undo.back());
		m_Undo.pop_back();
		m_Redo.push_back({ s.Label, Serialize() });
		Deserialize(s.Data);
		return true;
	}

	bool Document::Redo()
	{
		if (m_Redo.empty())
			return false;
		Snapshot s = std::move(m_Redo.back());
		m_Redo.pop_back();
		m_Undo.push_back({ s.Label, Serialize() });
		Deserialize(s.Data);
		return true;
	}

	void Document::RestoreLastSnapshot()
	{
		if (!m_Undo.empty())
			Deserialize(m_Undo.back().Data);
	}

	void Document::CancelUndo()
	{
		if (m_Undo.empty())
			return;
		Deserialize(m_Undo.back().Data);
		m_Undo.pop_back();
	}

	const std::string& Document::UndoLabel() const
	{
		static const std::string empty;
		return m_Undo.empty() ? empty : m_Undo.back().Label;
	}

	const std::string& Document::RedoLabel() const
	{
		static const std::string empty;
		return m_Redo.empty() ? empty : m_Redo.back().Label;
	}

	// ------------------------------------------------------------------ 요약
	nlohmann::json Document::Summary(bool objectsDetail)
	{
		nlohmann::json r;
		r["objects"] = (int)Objects.size();
		r["mode"] = EditMode ? "edit" : "object";
		r["selectMode"] = Mode == SelectMode::Vertex ? "vertex" : (Mode == SelectMode::Edge ? "edge" : "face");
		r["active"] = ActiveObject() ? ActiveObject()->Name : "";
		int verts = 0, faces = 0, tris = 0;
		Vec3 mn(FLT_MAX, FLT_MAX, FLT_MAX), mx(-FLT_MAX, -FLT_MAX, -FLT_MAX);
		nlohmann::json list = nlohmann::json::array();
		for (Object& o : Objects)
		{
			verts += (int)o.M.Verts.size();
			faces += (int)o.M.Faces.size();
			int t = 0;
			for (const Face& f : o.M.Faces) t += (int)f.V.size() - 2;
			tris += t;
			const Matrix w = o.World();
			Vec3 omn(FLT_MAX, FLT_MAX, FLT_MAX), omx(-FLT_MAX, -FLT_MAX, -FLT_MAX);
			for (const Vert& v : o.M.Verts)
			{
				const Vec3 p = Vec3::Transform(v.P, w);
				omn = Vec3::Min(omn, p);
				omx = Vec3::Max(omx, p);
			}
			if (!o.M.Verts.empty()) { mn = Vec3::Min(mn, omn); mx = Vec3::Max(mx, omx); }
			if (objectsDetail)
			{
				nlohmann::json oj = { { "name", o.Name }, { "verts", (int)o.M.Verts.size() }, { "faces", (int)o.M.Faces.size() }, { "tris", t } };
				if (!o.M.Verts.empty()) { oj["min"] = Vec(omn); oj["max"] = Vec(omx); }
				oj["position"] = Vec(o.Position);
				if (o.Selected) oj["selected"] = true;
				list.push_back(oj);
			}
		}
		r["verts"] = verts;
		r["faces"] = faces;
		r["tris"] = tris;
		if (verts > 0) { r["min"] = Vec(mn); r["max"] = Vec(mx); r["size"] = Vec(mx - mn); }
		if (objectsDetail) r["list"] = list;
		if (Object* a = ActiveObject(); a && EditMode)
		{
			Mesh& m = a->M;
			nlohmann::json s;
			s["verts"] = (int)m.SelectedVerts().size();
			s["edges"] = (int)m.SelectedEdgeIndices().size();
			s["faces"] = (int)m.SelectedFaces().size();
			if (s["verts"].get<int>() > 0)
			{
				// 월드 좌표: 중심 + 경계 상자
				const Matrix w = a->World();
				Vec3 smn(FLT_MAX, FLT_MAX, FLT_MAX), smx(-FLT_MAX, -FLT_MAX, -FLT_MAX);
				for (const Vert& v : m.Verts)
					if (v.Sel) { const Vec3 p = Vec3::Transform(v.P, w); smn = Vec3::Min(smn, p); smx = Vec3::Max(smx, p); }
				s["center"] = Vec(Vec3::Transform(m.SelectionCenter(), w));
				s["min"] = Vec(smn);
				s["max"] = Vec(smx);
			}
			r["selection"] = s;
			if (!m.Groups.empty()) r["groups"] = m.Groups;
			r["boundaryEdges"] = m.BoundaryEdges();
			r["nonManifoldEdges"] = m.NonManifoldEdges();
		}
		r["undo"] = UndoLabel();
		r["dirty"] = Dirty;
		return r;
	}
}
