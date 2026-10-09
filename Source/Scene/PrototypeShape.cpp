#include "pch.h"
#include "PrototypeShape.h"
#include "GameObject.h"
#include "MeshFilter.h"
#include "Mesh.h"
#include "UnityGUI.h"
#include "EditorLog.h"

namespace
{
	const char* kShapes[] = { "Box", "Stairs", "Ramp", "Cylinder", "Cone", "Arch Wall", "Curved Wall" };

	// 메시 만들기: 프로토타입 패키지 (Tools/prototype/make_prototype.py) 와 같은 규약 — 오른손 · Y 위 · 바깥에서 반시계로 짓고,
	//  끝에 엔진 (왼손) 으로 바꾼다 (Z 뒤집기 + 감김 뒤집기 — 모델을 가져올 때 ConvertToLeftHanded 와 같다). UV 는 면 평면에 미터로
	struct Builder
	{
		std::vector<Vertex::PosNormalTexTan2> V;
		std::vector<uint32_t> I;

		static Vec3 Cross(const Vec3& a, const Vec3& b) { return Vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
		static Vec3 Norm(Vec3 a) { const float l = a.Length(); return l > 1e-12f ? a / l : Vec3(0, 1, 0); }

		void Vert(const Vec3& p, const Vec3& n, const Vec3& t, const XMFLOAT2& uv)
		{
			Vertex::PosNormalTexTan2 v;
			v.pos = XMFLOAT3(p.x, p.y, p.z);
			v.normal = XMFLOAT3(n.x, n.y, n.z);
			v.tex = uv;
			v.tangentU = XMFLOAT4(t.x, t.y, t.z, 1.0f);
			V.push_back(v);
		}

		// 평평한 볼록 다각형 (바깥에서 반시계)
		void Poly(const std::vector<Vec3>& pts)
		{
			if (pts.size() < 3)
				return;
			Vec3 n(0, 1, 0);
			for (size_t k = 2; k < pts.size(); ++k)
			{
				const Vec3 c = Cross(pts[1] - pts[0], pts[k] - pts[0]);
				if (c.LengthSquared() > 1e-12f) { n = Norm(c); break; }
			}
			const Vec3 up = fabsf(n.y) < 0.999f ? Vec3(0, 1, 0) : Vec3(0, 0, n.y > 0 ? -1.0f : 1.0f);
			const Vec3 t = Norm(Cross(up, n));
			const Vec3 b = Cross(n, t);
			const uint32_t base = (uint32_t)V.size();
			for (const Vec3& p : pts)
				Vert(p, n, t, XMFLOAT2(p.Dot(t), -p.Dot(b)));
			for (uint32_t k = 1; k + 1 < (uint32_t)pts.size(); ++k)
			{
				I.push_back(base); I.push_back(base + k); I.push_back(base + k + 1);
			}
		}
		void Quad(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d) { Poly({ a, b, c, d }); }

		void Box(const Vec3& mn, const Vec3& mx)
		{
			const float x0 = mn.x, y0 = mn.y, z0 = mn.z, x1 = mx.x, y1 = mx.y, z1 = mx.z;
			Quad({ x0, y0, z1 }, { x1, y0, z1 }, { x1, y1, z1 }, { x0, y1, z1 });
			Quad({ x1, y0, z0 }, { x0, y0, z0 }, { x0, y1, z0 }, { x1, y1, z0 });
			Quad({ x1, y0, z1 }, { x1, y0, z0 }, { x1, y1, z0 }, { x1, y1, z1 });
			Quad({ x0, y0, z0 }, { x0, y0, z1 }, { x0, y1, z1 }, { x0, y1, z0 });
			Quad({ x0, y1, z1 }, { x1, y1, z1 }, { x1, y1, z0 }, { x0, y1, z0 });
			Quad({ x0, y0, z0 }, { x1, y0, z0 }, { x1, y0, z1 }, { x0, y0, z1 });
		}

		// 둥근 옆면 띠 (점마다 노멀 — 매끈하게). U = 둘레 길이, V = 높이
		void SmoothStrip(float rx, float rz, float y0, float y1, float rTop, float a0, float a1, int seg, bool inward, bool cone)
		{
			const float slope = cone ? (1.0f - rTop) * rx / (std::max)(1e-4f, y1 - y0) : 0.0f;
			for (int i = 0; i < seg; ++i)
			{
				const float t0 = a0 + (a1 - a0) * i / seg, t1 = a0 + (a1 - a0) * (i + 1) / seg;
				auto P = [&](float t, float s, float y) { return Vec3(rx * s * cosf(t), y, -rz * s * sinf(t)); };
				auto N = [&](float t) { Vec3 n = Norm(Vec3(cosf(t) * rz, slope, -sinf(t) * rx)); return inward ? -n : n; };
				auto T = [&](float t) { return Norm(Vec3(-sinf(t), 0, -cosf(t))); };
				const float r = (rx + rz) * 0.5f;
				const float u0 = t0 * r, u1 = t1 * r;
				Vec3 p[4] = { P(t0, 1, y0), P(t1, 1, y0), P(t1, rTop, y1), P(t0, rTop, y1) };
				Vec3 n[4] = { N(t0), N(t1), N(t1), N(t0) };
				XMFLOAT2 uv[4] = { { u0, -y0 }, { u1, -y0 }, { u1, -y1 }, { u0, -y1 } };
				Vec3 tg[4] = { T(t0), T(t1), T(t1), T(t0) };
				int order[4] = { 0, 1, 2, 3 };
				if (inward) { order[0] = 3; order[1] = 2; order[2] = 1; order[3] = 0; }
				const uint32_t base = (uint32_t)V.size();
				const int count = (rTop < 1e-5f) ? 3 : 4;
				if (count == 3) { p[2] = Vec3(0, y1, 0); uv[2] = XMFLOAT2((u0 + u1) * 0.5f, -y1); }
				for (int k = 0; k < count; ++k)
				{
					const int o = count == 3 ? k : order[k];
					Vert(p[o], n[o], tg[o], uv[o]);
				}
				for (int k = 1; k + 1 < count; ++k)
				{
					I.push_back(base); I.push_back(base + k); I.push_back(base + k + 1);
				}
			}
		}

		// 오른손 → 엔진 (왼손): Z 뒤집기 + 감김 뒤집기
		std::shared_ptr<Mesh> ToMesh(const std::string& name)
		{
			for (auto& v : V)
			{
				v.pos.z = -v.pos.z;
				v.normal.z = -v.normal.z;
				v.tangentU.z = -v.tangentU.z;
			}
			for (size_t k = 0; k + 2 < I.size(); k += 3)
				std::swap(I[k + 1], I[k + 2]);
			if (V.empty() || V.size() > 65535)
				return nullptr;
			auto mesh = std::make_shared<Mesh>();
			mesh->Name = name;
			mesh->Vertices = V;
			mesh->Indices.resize(I.size());
			for (size_t k = 0; k < I.size(); ++k)
				mesh->Indices[k] = (USHORT)I[k];
			MeshGeometry::Subset s;
			s.Id = 0;
			s.VertexStart = 0;
			s.VertexCount = (uint32)V.size();
			s.FaceStart = 0;
			s.FaceCount = (uint32)(I.size() / 3);
			s.MaterialIndex = 0;
			mesh->Subsets.push_back(s);
			Material m;
			m.Ambient = XMFLOAT4(0.8f, 0.8f, 0.8f, 1.0f);
			m.Diffuse = XMFLOAT4(0.8f, 0.8f, 0.8f, 1.0f);
			m.Specular = XMFLOAT4(0.4f, 0.4f, 0.4f, 16.0f);
			mesh->Mat.push_back(m);
			mesh->Setup();
			return mesh;
		}
	};

	// 벽 (X 너비 w, Y 높이 h, Z 두께 th — 가운데) 에 아치 구멍 (너비 ow, 반원 꼭대기 oh, 바닥에서)
	void ArchWall(Builder& b, float w, float h, float th, float ow, float oh, int seg)
	{
		ow = std::clamp(ow, 0.1f, w - 0.1f);
		const float r = ow * 0.5f;
		oh = std::clamp(oh, r + 0.05f, h - 0.05f);
		const float x0 = -w / 2, x1 = w / 2, z0 = -th / 2, z1 = th / 2;
		const float ox0 = -r, ox1 = r, ry = oh - r;
		for (int side = 0; side < 2; ++side)
		{
			const float z = side == 0 ? z1 : z0;
			auto face = [&](std::vector<Vec3> pts) { if (side == 1) std::reverse(pts.begin(), pts.end()); b.Poly(pts); };
			face({ { x0, 0, z }, { ox0, 0, z }, { ox0, h, z }, { x0, h, z } });
			face({ { ox1, 0, z }, { x1, 0, z }, { x1, h, z }, { ox1, h, z } });
			for (int i = 0; i < seg; ++i)
			{
				const float t0 = XM_PI * i / seg, t1 = XM_PI * (i + 1) / seg;
				const float ax = -r * cosf(t0), ay = ry + r * sinf(t0), bx = -r * cosf(t1), by = ry + r * sinf(t1);
				face({ { ax, ay, z }, { bx, by, z }, { bx, h, z }, { ax, h, z } });
			}
		}
		b.Quad({ x0, h, z1 }, { x1, h, z1 }, { x1, h, z0 }, { x0, h, z0 });
		b.Quad({ x1, 0, z1 }, { x1, 0, z0 }, { x1, h, z0 }, { x1, h, z1 });
		b.Quad({ x0, 0, z0 }, { x0, 0, z1 }, { x0, h, z1 }, { x0, h, z0 });
		b.Quad({ x0, 0, z0 }, { ox0, 0, z0 }, { ox0, 0, z1 }, { x0, 0, z1 });
		b.Quad({ ox1, 0, z0 }, { x1, 0, z0 }, { x1, 0, z1 }, { ox1, 0, z1 });
		b.Quad({ ox0, 0, z1 }, { ox0, 0, z0 }, { ox0, ry, z0 }, { ox0, ry, z1 });
		b.Quad({ ox1, 0, z0 }, { ox1, 0, z1 }, { ox1, ry, z1 }, { ox1, ry, z0 });
		for (int i = 0; i < seg; ++i)
		{
			const float t0 = XM_PI * i / seg, t1 = XM_PI * (i + 1) / seg;
			const Vec3 a(-r * cosf(t0), ry + r * sinf(t0), 0), c(-r * cosf(t1), ry + r * sinf(t1), 0);
			b.Quad({ c.x, c.y, z1 }, { a.x, a.y, z1 }, { a.x, a.y, z0 }, { c.x, c.y, z0 });
		}
	}

	// 원호 벽 (원통 껍질 조각): 안쪽 반지름 r, 두께 th, 높이 h, 각도 (라디안) — 원점 = 원의 가운데
	void CurvedWall(Builder& b, float r, float th, float h, float angle, int seg)
	{
		const float a0 = -angle * 0.5f, a1 = angle * 0.5f;
		const float ro = r + th;
		b.SmoothStrip(ro, ro, 0, h, 1.0f, a0, a1, seg, false, false);
		b.SmoothStrip(r, r, 0, h, 1.0f, a0, a1, seg, true, false);
		auto P = [](float rad, float t, float y) { return Vec3(rad * cosf(t), y, -rad * sinf(t)); };
		for (int i = 0; i < seg; ++i)
		{
			const float t0 = a0 + (a1 - a0) * i / seg, t1 = a0 + (a1 - a0) * (i + 1) / seg;
			b.Quad(P(r, t0, h), P(ro, t0, h), P(ro, t1, h), P(r, t1, h));   // 위
			b.Quad(P(r, t1, 0), P(ro, t1, 0), P(ro, t0, 0), P(r, t0, 0));   // 아래
		}
		if (angle < XM_2PI - 1e-3f)
		{
			b.Quad(P(r, a0, 0), P(ro, a0, 0), P(ro, a0, h), P(r, a0, h));   // 시작 끝면 (각이 줄어드는 쪽)
			b.Quad(P(ro, a1, 0), P(r, a1, 0), P(r, a1, h), P(ro, a1, h));   // 끝 끝면
		}
	}
}

PrototypeShape::PrototypeShape()
{
	m_InspectorTitleName = "Prototype Shape";
}

PrototypeShape::~PrototypeShape() {}

const char* PrototypeShape::ShapeName(Shape s)
{
	return kShapes[std::clamp((int)s, 0, (int)Shape::Count - 1)];
}

void PrototypeShape::EnsureBuilt()
{
	if (m_BuiltRevision == Revision || m_pGameObject == nullptr)
		return;
	MeshFilter* mf = m_pGameObject->GetComponent<MeshFilter>();
	if (mf && mf->GetMesh() && m_BuiltRevision == Revision)
		return;
	Rebuild();
}

void PrototypeShape::Rebuild()
{
	m_BuiltRevision = Revision;
	if (m_pGameObject == nullptr)
		return;
	MeshFilter* mf = m_pGameObject->GetComponent<MeshFilter>();
	if (mf == nullptr)
		mf = m_pGameObject->AddComponent<MeshFilter>();
	const float x = (std::max)(0.01f, Size.x), y = (std::max)(0.01f, Size.y), z = (std::max)(0.01f, Size.z);
	const int seg = std::clamp(Segments, 3, 256);
	Builder b;
	switch (Kind)
	{
	case Shape::Box:
		b.Box(Vec3(-x / 2, 0, -z / 2), Vec3(x / 2, y, z / 2));
		break;
	case Shape::Stairs:
	{
		// 단 높이 그대로 — 단 수 = 높이 / 단 높이. 엔진의 +Z 쪽으로 올라간다 (오른손으로 지어 -Z → 바꾸면 +Z)
		const int steps = std::clamp((int)roundf(y / (std::max)(0.02f, StepHeight)), 1, 400);
		const float sr = y / steps, sd = z / steps;
		for (int i = 0; i < steps; ++i)
			b.Box(Vec3(-x / 2, 0, z / 2 - (i + 1) * sd), Vec3(x / 2, (i + 1) * sr, z / 2 - i * sd));
		break;
	}
	case Shape::Ramp:
	{
		const float z0 = z / 2, z1 = -z / 2;   // 엔진 +Z 쪽이 높다
		const float x0 = -x / 2, x1 = x / 2;
		b.Quad({ x0, 0, z0 }, { x1, 0, z0 }, { x1, y, z1 }, { x0, y, z1 });   // 경사면
		b.Quad({ x1, 0, z1 }, { x0, 0, z1 }, { x0, y, z1 }, { x1, y, z1 });   // 높은 쪽 세로면
		b.Quad({ x0, 0, z1 }, { x1, 0, z1 }, { x1, 0, z0 }, { x0, 0, z0 });   // 바닥
		b.Poly({ { x1, 0, z0 }, { x1, 0, z1 }, { x1, y, z1 } });
		b.Poly({ { x0, 0, z1 }, { x0, 0, z0 }, { x0, y, z1 } });
		break;
	}
	case Shape::Cylinder:
	case Shape::Cone:
	{
		const bool cone = Kind == Shape::Cone;
		b.SmoothStrip(x / 2, z / 2, 0, y, cone ? 0.0f : 1.0f, 0, XM_2PI, seg, false, cone);
		std::vector<Vec3> bottom, top;
		for (int i = 0; i < seg; ++i)
		{
			const float t = XM_2PI * i / seg;
			bottom.push_back(Vec3(x / 2 * cosf(t), 0, -z / 2 * sinf(t)));
			top.push_back(Vec3(x / 2 * cosf(t), y, -z / 2 * sinf(t)));
		}
		std::reverse(bottom.begin(), bottom.end());
		b.Poly(bottom);
		if (!cone)
			b.Poly(top);
		break;
	}
	case Shape::ArchWall:
		ArchWall(b, x, y, z, OpeningWidth, OpeningHeight, (std::max)(4, seg / 2));
		break;
	case Shape::CurvedWall:
		CurvedWall(b, (std::max)(0.1f, Radius), z, y, XMConvertToRadians(std::clamp(Angle, 1.0f, 360.0f)), seg);
		break;
	default:
		break;
	}
	std::shared_ptr<Mesh> mesh = b.ToMesh(std::string("Prototype ") + ShapeName(Kind));
	if (!mesh)
	{
		EditorLog::Write("Prototype", "'%s': shape has too many vertices — lower Segments or raise Step Height", m_pGameObject->GetName().c_str());
		return;
	}
	mf->SetMesh(mesh, L"", 0);
	MarkPhysicsDirty();   // Mesh Collider 가 새 메시로
}

void PrototypeShape::OnInspectorGUI()
{
	using namespace UnityGUI;
	bool changed = false;
	int kind = (int)Kind;
	if (Dropdown("Shape", &kind, kShapes, (int)Shape::Count)) { Kind = (Shape)kind; changed = true; }
	if (Kind == Shape::CurvedWall)
	{
		changed |= Float("Radius", &Radius);
		changed |= Slider("Angle", &Angle, 1.0f, 360.0f);
		changed |= Float("Height", &Size.y);
		changed |= Float("Thickness", &Size.z);
	}
	else
		changed |= UnityGUI::Vector3("Size", &Size.x);
	if (Kind == Shape::Stairs)
	{
		changed |= Slider("Step Height", &StepHeight, 0.05f, 1.0f);
		char t[64];
		snprintf(t, sizeof(t), "%d steps", std::clamp((int)roundf(Size.y / (std::max)(0.02f, StepHeight)), 1, 400));
		Label(t, 1);
	}
	if (Kind == Shape::ArchWall)
	{
		changed |= Float("Opening Width", &OpeningWidth);
		changed |= Float("Opening Height", &OpeningHeight);
	}
	if (Kind == Shape::Cylinder || Kind == Shape::Cone || Kind == Shape::ArchWall || Kind == Shape::CurvedWall)
		changed |= Int("Segments", &Segments);
	if (changed)
	{
		++Revision;
		MarkPhysicsDirty();
	}
}

GENERATE_COMPONENT_FUNC_TOJSON(PrototypeShape)
{
	json j;
	SERIALIZE_TYPE(j, PrototypeShape);
	j["enabled"] = m_Enabled;
	j["shape"] = (int)Kind;
	j["size"] = { Size.x, Size.y, Size.z };
	j["stepHeight"] = StepHeight;
	j["segments"] = Segments;
	j["openingWidth"] = OpeningWidth;
	j["openingHeight"] = OpeningHeight;
	j["radius"] = Radius;
	j["angle"] = Angle;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(PrototypeShape)
{
	MarkPhysicsDirty();
	m_Enabled = j.value("enabled", true);
	Kind = (Shape)std::clamp(j.value("shape", (int)Kind), 0, (int)Shape::Count - 1);
	if (j.contains("size") && j["size"].is_array() && j["size"].size() >= 3)
		Size = Vec3(j["size"][0].get<float>(), j["size"][1].get<float>(), j["size"][2].get<float>());
	StepHeight = j.value("stepHeight", StepHeight);
	Segments = j.value("segments", Segments);
	OpeningWidth = j.value("openingWidth", OpeningWidth);
	OpeningHeight = j.value("openingHeight", OpeningHeight);
	Radius = j.value("radius", Radius);
	Angle = j.value("angle", Angle);
	++Revision;
}
