#include "pch.h"
#include "Physics2DComponents.h"
#include "Physics2DManager.h"
#include "SpriteRenderer.h"
#include "SpriteSlicer.h"
#include "UnityGUI.h"
#include "UISprites.h"
#include "SceneViewOverlay.h"

namespace
{
	constexpr float kPi = 3.14159265358979f;

	float Cross(const Vec2& a, const Vec2& b) { return a.x * b.y - a.y * b.x; }

	float SignedArea(const std::vector<Vec2>& p)
	{
		float a = 0.0f;
		for (size_t i = 0; i < p.size(); ++i)
			a += Cross(p[i], p[(i + 1) % p.size()]);
		return a * 0.5f;
	}

	// 같은 점 · 일직선 점을 뺀다
	std::vector<Vec2> Clean(std::vector<Vec2> p, float eps)
	{
		bool changed = true;
		while (changed && p.size() > 3)
		{
			changed = false;
			for (size_t i = 0; i < p.size() && p.size() > 3; ++i)
			{
				const Vec2& a = p[(i + p.size() - 1) % p.size()];
				const Vec2& b = p[i];
				const Vec2& c = p[(i + 1) % p.size()];
				if ((b - a).LengthSquared() < eps * eps || fabsf(Cross(b - a, c - b)) < eps * eps)
				{
					p.erase(p.begin() + i);
					changed = true;
					break;
				}
			}
		}
		return p;
	}

	bool IsConvex(const std::vector<Vec2>& p)
	{
		for (size_t i = 0; i < p.size(); ++i)
			if (Cross(p[(i + 1) % p.size()] - p[i], p[(i + 2) % p.size()] - p[(i + 1) % p.size()]) < -1e-7f)
				return false;
		return true;
	}

	bool PointInTriangle(const Vec2& p, const Vec2& a, const Vec2& b, const Vec2& c)
	{
		return Cross(b - a, p - a) >= -1e-9f && Cross(c - b, p - b) >= -1e-9f && Cross(a - c, p - c) >= -1e-9f;
	}

	// 오목 다각형 → 볼록 조각 (귀 자르기 삼각형 → 모서리를 나누는 이웃끼리 합치기, 조각마다 8 점까지 — Box2D)
	std::vector<std::vector<Vec2>> Decompose(std::vector<Vec2> poly)
	{
		std::vector<std::vector<Vec2>> out;
		poly = Clean(poly, 1e-4f);
		if (poly.size() < 3)
			return out;
		if (SignedArea(poly) < 0.0f)
			std::reverse(poly.begin(), poly.end());
		if (poly.size() <= 8 && IsConvex(poly))
		{
			out.push_back(poly);
			return out;
		}
		// 귀 자르기
		std::vector<int> idx(poly.size());
		for (size_t i = 0; i < poly.size(); ++i) idx[i] = (int)i;
		std::vector<std::array<int, 3>> tris;
		int guard = 0;
		while (idx.size() > 3 && guard++ < 10000)
		{
			bool clipped = false;
			for (size_t i = 0; i < idx.size(); ++i)
			{
				const int ia = idx[(i + idx.size() - 1) % idx.size()], ib = idx[i], ic = idx[(i + 1) % idx.size()];
				const Vec2 &a = poly[ia], &b = poly[ib], &c = poly[ic];
				if (Cross(b - a, c - b) <= 1e-9f)
					continue;   // 오목한 꼭짓점
				bool inside = false;
				for (int k : idx)
					if (k != ia && k != ib && k != ic && PointInTriangle(poly[k], a, b, c))
					{
						inside = true;
						break;
					}
				if (inside)
					continue;
				tris.push_back({ ia, ib, ic });
				idx.erase(idx.begin() + i);
				clipped = true;
				break;
			}
			if (!clipped)
				break;   // 꼬인 윤곽: 남은 부분은 아래에서 볼록 껍질로
		}
		if (idx.size() == 3)
			tris.push_back({ idx[0], idx[1], idx[2] });
		// 합치기: 공유하는 모서리 (a → b 와 b → a)
		std::vector<std::vector<int>> pieces;
		for (const auto& t : tris)
			pieces.push_back({ t[0], t[1], t[2] });
		auto points = [&](const std::vector<int>& piece) {
			std::vector<Vec2> p;
			for (int i : piece) p.push_back(poly[i]);
			return p;
		};
		for (bool merged = true; merged;)
		{
			merged = false;
			for (size_t a = 0; a < pieces.size() && !merged; ++a)
				for (size_t b = a + 1; b < pieces.size() && !merged; ++b)
				{
					const auto& pa = pieces[a];
					const auto& pb = pieces[b];
					if (pa.size() + pb.size() - 2 > 8)
						continue;
					for (size_t i = 0; i < pa.size() && !merged; ++i)
					{
						const int u = pa[i], v = pa[(i + 1) % pa.size()];
						for (size_t j = 0; j < pb.size(); ++j)
							if (pb[j] == v && pb[(j + 1) % pb.size()] == u)
							{
								// pa 의 u → v 자리에 pb 의 나머지 (v 다음부터 u 전까지) 를 끼운다
								std::vector<int> m;
								for (size_t k = 0; k <= i; ++k) m.push_back(pa[k]);
								for (size_t k = 2; k < pb.size(); ++k) m.push_back(pb[(j + k) % pb.size()]);
								for (size_t k = i + 1; k < pa.size(); ++k) m.push_back(pa[k]);
								if (IsConvex(points(m)))
								{
									pieces[a] = m;
									pieces.erase(pieces.begin() + b);
									merged = true;
								}
								break;
							}
					}
				}
		}
		for (const auto& piece : pieces)
			out.push_back(points(piece));
		if (idx.size() > 3)
		{
			// 귀를 못 찾은 남은 부분: 볼록 껍질 (8 점까지 줄이기)
			std::vector<Vec2> rest;
			for (int k : idx) rest.push_back(poly[k]);
			if (rest.size() > 8)
			{
				std::vector<Vec2> few;
				for (int i = 0; i < 8; ++i) few.push_back(rest[i * rest.size() / 8]);
				rest = few;
			}
			out.push_back(rest);
		}
		return out;
	}

	// ---- 스프라이트 윤곽: 투명하지 않은 픽셀의 경계 (solid 왼쪽 = 반시계) → 덩어리마다 고리 → Douglas-Peucker
	std::vector<std::vector<Vec2>> TraceOutlines(const std::vector<uint8>& solid, int W, int H)
	{
		struct Edge { int From, To; bool Used = false; };
		std::vector<Edge> edges;
		std::unordered_map<int, std::vector<int>> starts;
		auto at = [&](int x, int y) { return x >= 0 && y >= 0 && x < W && y < H && solid[(size_t)y * W + x]; };
		auto vid = [&](int x, int y) { return y * (W + 1) + x; };
		auto add = [&](int x0, int y0, int x1, int y1) {
			starts[vid(x0, y0)].push_back((int)edges.size());
			edges.push_back({ vid(x0, y0), vid(x1, y1) });
		};
		for (int y = 0; y < H; ++y)
			for (int x = 0; x < W; ++x)
			{
				if (!at(x, y)) continue;
				if (!at(x, y - 1)) add(x, y, x + 1, y);
				if (!at(x + 1, y)) add(x + 1, y, x + 1, y + 1);
				if (!at(x, y + 1)) add(x + 1, y + 1, x, y + 1);
				if (!at(x - 1, y)) add(x, y + 1, x, y);
			}
		auto pos = [&](int v) { return Vec2((float)(v % (W + 1)), (float)(v / (W + 1))); };
		std::vector<std::vector<Vec2>> loops;
		for (size_t e0 = 0; e0 < edges.size(); ++e0)
		{
			if (edges[e0].Used) continue;
			std::vector<Vec2> loop;
			int e = (int)e0;
			for (int guard = 0; guard < (int)edges.size() + 2; ++guard)
			{
				edges[e].Used = true;
				loop.push_back(pos(edges[e].From));
				const Vec2 dir = pos(edges[e].To) - pos(edges[e].From);
				int next = -1;
				float best = -10.0f;
				for (int cand : starts[edges[e].To])
				{
					if (edges[cand].Used) continue;
					const Vec2 nd = pos(edges[cand].To) - pos(edges[cand].From);
					const float turn = Cross(dir, nd) * 2.0f + dir.Dot(nd);   // 왼쪽 > 직진 > 오른쪽
					if (turn > best) { best = turn; next = cand; }
				}
				if (next < 0) break;
				e = next;
			}
			if (loop.size() >= 4 && SignedArea(loop) > 2.0f)   // 반시계 = 바깥 윤곽 (구멍 · 아주 작은 점은 버린다)
				loops.push_back(loop);
		}
		return loops;
	}

	void DouglasPeucker(const std::vector<Vec2>& p, size_t a, size_t b, float tol, std::vector<bool>& keep)
	{
		if (b <= a + 1) return;
		float worst = -1.0f;
		size_t wi = a;
		const Vec2 ab = p[b % p.size()] - p[a];
		const float len = (std::max)(ab.Length(), 1e-6f);
		for (size_t i = a + 1; i < b; ++i)
		{
			const float d = fabsf(Cross(ab, p[i] - p[a])) / len;
			if (d > worst) { worst = d; wi = i; }
		}
		if (worst > tol)
		{
			keep[wi] = true;
			DouglasPeucker(p, a, wi, tol, keep);
			DouglasPeucker(p, wi, b, tol, keep);
		}
	}

	std::vector<Vec2> Simplify(const std::vector<Vec2>& p, float tol)
	{
		if (p.size() < 4) return p;
		// 닫힌 고리: 첫 점과 가장 먼 점에서 둘로
		size_t farIdx = 0;
		float fd = -1.0f;
		for (size_t i = 1; i < p.size(); ++i)
			if ((p[i] - p[0]).LengthSquared() > fd) { fd = (p[i] - p[0]).LengthSquared(); farIdx = i; }
		std::vector<bool> keep(p.size(), false);
		keep[0] = keep[farIdx] = true;
		DouglasPeucker(p, 0, farIdx, tol, keep);
		DouglasPeucker(p, farIdx, p.size(), tol, keep);
		std::vector<Vec2> out;
		for (size_t i = 0; i < p.size(); ++i)
			if (keep[i]) out.push_back(p[i]);
		return out.size() >= 3 ? out : p;
	}

	std::vector<Vec2> Ellipse(Vec2 c, float rx, float ry, int n)
	{
		std::vector<Vec2> p;
		for (int i = 0; i < n; ++i)
		{
			const float a = 2.0f * kPi * i / n;
			p.push_back(c + Vec2(cosf(a) * rx, sinf(a) * ry));
		}
		return p;
	}

	// 고리 (로컬) → 선 (Scene 뷰)
	void DrawLoops(GameObject* go, const std::vector<std::vector<Vec2>>& loops, bool closed, ImU32 color)
	{
		const Matrix m = go->GetTransform()->GetWorldMatrix();
		for (const auto& loop : loops)
			for (size_t i = 0; i + (closed ? 0 : 1) < loop.size(); ++i)
			{
				const Vec3 a = Vec3::Transform(Vec3(loop[i].x, loop[i].y, 0), m);
				const Vec3 b = Vec3::Transform(Vec3(loop[(i + 1) % loop.size()].x, loop[(i + 1) % loop.size()].y, 0), m);
				SceneViewOverlay::DrawLine(XMFLOAT3(a.x, a.y, a.z), XMFLOAT3(b.x, b.y, b.z), color, 1.5f);
			}
	}

	json V2(const Vec2& v) { return json::array({ v.x, v.y }); }
	Vec2 ReadV2(const json& j, const char* key, Vec2 def)
	{
		if (j.contains(key) && j[key].is_array() && j[key].size() == 2)
			return Vec2(j[key][0].get<float>(), j[key][1].get<float>());
		return def;
	}
	bool Vec2Field(const char* label, Vec2& v)
	{
		return UnityGUI::Vector2Pair(label, "X", &v.x, "Y", &v.y);
	}
}

// ------------------------------------------------------------------ Rigidbody2D
Rigidbody2D::Rigidbody2D()
{
	m_InspectorTitleName = "Rigidbody 2D";
}

void Rigidbody2D::OnInspectorGUI()
{
	static const char* types[] = { "Dynamic", "Kinematic", "Static" };
	int t = (int)Type;
	bool changed = false;
	if (UnityGUI::Dropdown("Body Type", &t, types, 3)) { Type = (BodyType)t; changed = true; }
	if (Type == BodyType::Dynamic)
	{
		changed |= UnityGUI::Float("Mass", &Mass);
		Mass = (std::max)(0.0001f, Mass);
		changed |= UnityGUI::Float("Linear Damping", &LinearDamping);
		changed |= UnityGUI::Float("Angular Damping", &AngularDamping);
		changed |= UnityGUI::Float("Gravity Scale", &GravityScale);
		static const char* modes[] = { "Discrete", "Continuous" };
		changed |= UnityGUI::Dropdown("Collision Detection", &CollisionDetection, modes, 2);
	}
	if (Type != BodyType::Static && UnityGUI::FoldoutPlain("Constraints"))
	{
		changed |= UnityGUI::Toggle("Freeze Position X", &FreezePositionX, 1);
		changed |= UnityGUI::Toggle("Freeze Position Y", &FreezePositionY, 1);
		changed |= UnityGUI::Toggle("Freeze Rotation Z", &FreezeRotation, 1);
	}
	if (changed)
		ApplyDynamicSettings();
	if (Application::IsPlaying() && Body)
	{
		char buf[96];
		const Vec2 v = GetVelocity();
		snprintf(buf, sizeof(buf), "%.2f, %.2f  (angular %.1f deg/s)", v.x, v.y, GetAngularVelocity());
		UnityGUI::ValueLabel("Velocity", buf);
	}
}

GENERATE_COMPONENT_FUNC_TOJSON(Rigidbody2D)
{
	json j;
	SERIALIZE_TYPE(j, Rigidbody2D);
	j["enabled"] = m_Enabled;
	j["bodyType"] = (int)Type;
	j["mass"] = Mass;
	j["linearDamping"] = LinearDamping;
	j["angularDamping"] = AngularDamping;
	j["gravityScale"] = GravityScale;
	j["collisionDetection"] = CollisionDetection;
	j["freezePositionX"] = FreezePositionX;
	j["freezePositionY"] = FreezePositionY;
	j["freezeRotation"] = FreezeRotation;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(Rigidbody2D)
{
	m_Enabled = j.value("enabled", true);
	Type = (BodyType)std::clamp(j.value("bodyType", 0), 0, 2);
	Mass = (std::max)(0.0001f, j.value("mass", 1.0f));
	LinearDamping = j.value("linearDamping", 0.0f);
	AngularDamping = j.value("angularDamping", 0.05f);
	GravityScale = j.value("gravityScale", 1.0f);
	CollisionDetection = j.value("collisionDetection", 0);
	FreezePositionX = j.value("freezePositionX", false);
	FreezePositionY = j.value("freezePositionY", false);
	FreezeRotation = j.value("freezeRotation", false);
	ApplyDynamicSettings();
}

// ------------------------------------------------------------------ Collider2D (공통)
Collider2D::Collider2D() {}

Rigidbody2D* Collider2D::AttachedRigidbody()
{
	for (GameObject* g = m_pGameObject; g; g = g->GetParent())
		if (Rigidbody2D* rb = g->GetComponent<Rigidbody2D>(); rb && rb->IsEnabled())
			return rb;
	return nullptr;
}

void Collider2D::EnsureFitted()
{
	if (!m_NeedsFit || m_pGameObject == nullptr)
		return;
	m_NeedsFit = false;
	if (m_pGameObject->GetComponent<SpriteRenderer>())
	{
		FitToSprite();
		Touch();
	}
}

bool Collider2D::CommonInspector()
{
	bool changed = false;
	changed |= UnityGUI::Toggle("Is Trigger", &IsTrigger);
	changed |= Vec2Field("Offset", Offset);
	if (UnityGUI::FoldoutPlain("Material (Physics Material 2D)", 0, false))
	{
		changed |= UnityGUI::Float("Friction", &Friction, 1);
		changed |= UnityGUI::Float("Bounciness", &Bounciness, 1);
		Bounciness = std::clamp(Bounciness, 0.0f, 1.0f);
		Friction = (std::max)(0.0f, Friction);
	}
	return changed;
}

void Collider2D::CommonToJson(json& j) const
{
	j["enabled"] = m_Enabled;
	j["isTrigger"] = IsTrigger;
	j["offset"] = V2(Offset);
	j["friction"] = Friction;
	j["bounciness"] = Bounciness;
}

void Collider2D::CommonFromJson(const json& j)
{
	m_Enabled = j.value("enabled", true);
	IsTrigger = j.value("isTrigger", false);
	Offset = ReadV2(j, "offset", Vec2(0, 0));
	Friction = j.value("friction", 0.4f);
	Bounciness = j.value("bounciness", 0.0f);
	m_NeedsFit = false;   // 저장된 값 그대로
	Touch();
}

void Collider2D::OnDrawGizmos()
{
	// Unity: 고른 오브젝트의 2D 콜라이더 윤곽 (초록)
	if (m_pGameObject == nullptr || !SceneViewOverlay::IsActive())
		return;
	EnsureFitted();
	GameObject* sel = SelectionManager::GetSelectedGameObject();
	bool selected = false;
	for (GameObject* g = m_pGameObject; g && !selected; g = g->GetParent())
		selected = g == sel;
	if (!selected)
		return;
	std::vector<std::vector<Vec2>> loops;
	Outline(loops);
	DrawLoops(m_pGameObject, loops, dynamic_cast<EdgeCollider2D*>(this) == nullptr, IsTrigger ? IM_COL32(120, 220, 255, 230) : IM_COL32(120, 255, 120, 230));
}

// ------------------------------------------------------------------ Box
void BoxCollider2D::BuildShapes(std::vector<Shape2D>& out)
{
	EnsureFitted();
	const float r = (std::max)(0.0f, EdgeRadius);
	const float hx = (std::max)(0.0005f, fabsf(Size.x) * 0.5f), hy = (std::max)(0.0005f, fabsf(Size.y) * 0.5f);
	Shape2D s;
	s.K = Shape2D::Polygon;
	s.Points = { Offset + Vec2(-hx, -hy), Offset + Vec2(hx, -hy), Offset + Vec2(hx, hy), Offset + Vec2(-hx, hy) };
	s.Radius = r;
	out.push_back(s);
}

void BoxCollider2D::Outline(std::vector<std::vector<Vec2>>& loops)
{
	const float hx = fabsf(Size.x) * 0.5f + EdgeRadius, hy = fabsf(Size.y) * 0.5f + EdgeRadius;
	loops.push_back({ Offset + Vec2(-hx, -hy), Offset + Vec2(hx, -hy), Offset + Vec2(hx, hy), Offset + Vec2(-hx, hy) });
}

void BoxCollider2D::FitToSprite()
{
	SpriteRenderer* sr = m_pGameObject ? m_pGameObject->GetComponent<SpriteRenderer>() : nullptr;
	Vec2 size, pivot;
	if (sr && sr->GetSpriteSize(size, pivot))
	{
		Size = size;
		Offset = Vec2((0.5f - pivot.x) * size.x, (0.5f - pivot.y) * size.y);
	}
}

void BoxCollider2D::OnInspectorGUI()
{
	EnsureFitted();
	bool changed = CommonInspector();
	changed |= Vec2Field("Size", Size);
	changed |= UnityGUI::Float("Edge Radius", &EdgeRadius);
	EdgeRadius = (std::max)(0.0f, EdgeRadius);
	if (UnityGUI::CenterButton("Fit to Sprite", 160.0f)) { FitToSprite(); changed = true; }
	if (changed) Touch();
}

GENERATE_COMPONENT_FUNC_TOJSON(BoxCollider2D)
{
	json j;
	SERIALIZE_TYPE(j, BoxCollider2D);
	CommonToJson(j);
	j["size"] = V2(Size);
	j["edgeRadius"] = EdgeRadius;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(BoxCollider2D)
{
	CommonFromJson(j);
	Size = ReadV2(j, "size", Vec2(1, 1));
	EdgeRadius = j.value("edgeRadius", 0.0f);
}

// ------------------------------------------------------------------ Circle
void CircleCollider2D::BuildShapes(std::vector<Shape2D>& out)
{
	EnsureFitted();
	Shape2D s;
	s.K = Shape2D::Circle;
	s.Points = { Offset };
	s.Radius = (std::max)(0.0005f, fabsf(Radius));
	out.push_back(s);
}

void CircleCollider2D::Outline(std::vector<std::vector<Vec2>>& loops)
{
	loops.push_back(Ellipse(Offset, Radius, Radius, 32));
}

void CircleCollider2D::FitToSprite()
{
	SpriteRenderer* sr = m_pGameObject ? m_pGameObject->GetComponent<SpriteRenderer>() : nullptr;
	Vec2 size, pivot;
	if (sr && sr->GetSpriteSize(size, pivot))
	{
		Radius = (std::max)(size.x, size.y) * 0.5f;
		Offset = Vec2((0.5f - pivot.x) * size.x, (0.5f - pivot.y) * size.y);
	}
}

void CircleCollider2D::OnInspectorGUI()
{
	EnsureFitted();
	bool changed = CommonInspector();
	changed |= UnityGUI::Float("Radius", &Radius);
	Radius = (std::max)(0.0001f, Radius);
	if (UnityGUI::CenterButton("Fit to Sprite", 160.0f)) { FitToSprite(); changed = true; }
	if (changed) Touch();
}

GENERATE_COMPONENT_FUNC_TOJSON(CircleCollider2D)
{
	json j;
	SERIALIZE_TYPE(j, CircleCollider2D);
	CommonToJson(j);
	j["radius"] = Radius;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(CircleCollider2D)
{
	CommonFromJson(j);
	Radius = j.value("radius", 0.5f);
}

// ------------------------------------------------------------------ Capsule
void CapsuleCollider2D::BuildShapes(std::vector<Shape2D>& out)
{
	EnsureFitted();
	const float w = fabsf(Size.x), h = fabsf(Size.y);
	Shape2D s;
	s.K = Shape2D::Capsule;
	if (Direction == 0)
	{
		s.Radius = (std::max)(0.0005f, w * 0.5f);
		const float half = (std::max)(0.0f, h * 0.5f - s.Radius);
		s.Points = { Offset + Vec2(0, -half), Offset + Vec2(0, half) };
	}
	else
	{
		s.Radius = (std::max)(0.0005f, h * 0.5f);
		const float half = (std::max)(0.0f, w * 0.5f - s.Radius);
		s.Points = { Offset + Vec2(-half, 0), Offset + Vec2(half, 0) };
	}
	if ((s.Points[1] - s.Points[0]).LengthSquared() < 1e-8f)
	{
		s.K = Shape2D::Circle;   // 짧으면 원
		s.Points = { Offset };
	}
	out.push_back(s);
}

void CapsuleCollider2D::Outline(std::vector<std::vector<Vec2>>& loops)
{
	std::vector<Shape2D> s;
	BuildShapes(s);
	if (s.empty()) return;
	if (s[0].K == Shape2D::Circle) { loops.push_back(Ellipse(Offset, s[0].Radius, s[0].Radius, 32)); return; }
	const Vec2 a = s[0].Points[0], b = s[0].Points[1];
	const float r = s[0].Radius;
	const Vec2 d = b - a;
	const float base = atan2f(d.y, d.x);
	std::vector<Vec2> loop;
	for (int i = 0; i <= 16; ++i) { const float t = base - kPi * 0.5f + kPi * i / 16; loop.push_back(b + Vec2(cosf(t), sinf(t)) * r); }
	for (int i = 0; i <= 16; ++i) { const float t = base + kPi * 0.5f + kPi * i / 16; loop.push_back(a + Vec2(cosf(t), sinf(t)) * r); }
	loops.push_back(loop);
}

void CapsuleCollider2D::FitToSprite()
{
	SpriteRenderer* sr = m_pGameObject ? m_pGameObject->GetComponent<SpriteRenderer>() : nullptr;
	Vec2 size, pivot;
	if (sr && sr->GetSpriteSize(size, pivot))
	{
		Size = size;
		Direction = size.x > size.y ? 1 : 0;
		Offset = Vec2((0.5f - pivot.x) * size.x, (0.5f - pivot.y) * size.y);
	}
}

void CapsuleCollider2D::OnInspectorGUI()
{
	EnsureFitted();
	bool changed = CommonInspector();
	changed |= Vec2Field("Size", Size);
	static const char* dirs[] = { "Vertical", "Horizontal" };
	changed |= UnityGUI::Dropdown("Direction", &Direction, dirs, 2);
	if (UnityGUI::CenterButton("Fit to Sprite", 160.0f)) { FitToSprite(); changed = true; }
	if (changed) Touch();
}

GENERATE_COMPONENT_FUNC_TOJSON(CapsuleCollider2D)
{
	json j;
	SERIALIZE_TYPE(j, CapsuleCollider2D);
	CommonToJson(j);
	j["size"] = V2(Size);
	j["direction"] = Direction;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(CapsuleCollider2D)
{
	CommonFromJson(j);
	Size = ReadV2(j, "size", Vec2(0.5f, 1.0f));
	Direction = j.value("direction", 0);
}

// ------------------------------------------------------------------ Polygon
void PolygonCollider2D::BuildShapes(std::vector<Shape2D>& out)
{
	EnsureFitted();
	if (Paths.empty())
		Paths.push_back(Ellipse(Vec2(0, 0), 0.5f, 0.5f, 5));   // 기본 = 오각형 (Unity 와 같음)
	if (m_CacheRevision != Revision)
	{
		m_Cache.clear();
		for (const auto& path : Paths)
			for (auto& piece : Decompose(path))
			{
				Shape2D s;
				s.K = Shape2D::Polygon;
				s.Points = piece;
				m_Cache.push_back(s);
			}
		m_CacheRevision = Revision;
	}
	for (Shape2D s : m_Cache)
	{
		for (Vec2& p : s.Points) p += Offset;
		out.push_back(s);
	}
}

void PolygonCollider2D::Outline(std::vector<std::vector<Vec2>>& loops)
{
	EnsureFitted();
	for (auto path : Paths)
	{
		for (Vec2& p : path) p += Offset;
		loops.push_back(path);
	}
}

void PolygonCollider2D::FitToSprite()
{
	SpriteRenderer* sr = m_pGameObject ? m_pGameObject->GetComponent<SpriteRenderer>() : nullptr;
	Vec2 size, pivot;
	if (!sr || !sr->GetSpriteSize(size, pivot))
		return;
	const std::string sprite = sr->GetSprite();
	auto toLocal = [&](const Vec2& uv) { return Vec2((uv.x - pivot.x) * size.x, (uv.y - pivot.y) * size.y); };
	Paths.clear();
	Offset = Vec2(0, 0);
	// 내장 도형: 모양 그대로
	if (sprite == "builtin:Circle") { std::vector<Vec2> p; for (const Vec2& v : Ellipse(Vec2(0.5f, 0.5f), 0.5f, 0.5f, 20)) p.push_back(toLocal(v)); Paths.push_back(p); return; }
	if (sprite == "builtin:Triangle") { Paths.push_back({ toLocal(Vec2(0, 0)), toLocal(Vec2(1, 0)), toLocal(Vec2(0.5f, 1)) }); return; }
	if (sprite.rfind("builtin:", 0) == 0) { Paths.push_back({ toLocal(Vec2(0, 0)), toLocal(Vec2(1, 0)), toLocal(Vec2(1, 1)), toLocal(Vec2(0, 1)) }); return; }
	// 그림: 투명하지 않은 픽셀의 윤곽 (잘라 놓은 스프라이트면 그 사각형만)
	const std::string file = sprite.substr(0, sprite.find('#'));
	const std::wstring full = PathManager::GetI()->GetMovePathW(string_to_wstring(file));
	SpriteSlicer::Pixels px;
	if (!px.Load(full))
		return;
	int rx = 0, ry = 0, rw = px.W, rh = px.H;   // ry = 아래 기준
	if (const size_t hash = sprite.find('#'); hash != std::string::npos)
	{
		const AssetImport::TextureSettings ts = AssetImport::LoadTexture(full);
		if (const AssetImport::SpriteRect* r = ts.FindSprite(sprite.substr(hash + 1)))
		{
			rx = (int)r->X; ry = (int)r->Y; rw = (int)r->W; rh = (int)r->H;
		}
	}
	// 큰 그림은 칸을 묶어서 (긴 쪽 128 칸 이하)
	const int step = (std::max)(1, (std::max)(rw, rh) / 128);
	const int gw = (rw + step - 1) / step, gh = (rh + step - 1) / step;
	std::vector<uint8> solid((size_t)gw * gh, 0);
	for (int gy = 0; gy < gh; ++gy)
		for (int gx = 0; gx < gw; ++gx)
		{
			bool any = false;
			for (int y = gy * step; y < (std::min)(rh, (gy + 1) * step) && !any; ++y)
				for (int x = gx * step; x < (std::min)(rw, (gx + 1) * step) && !any; ++x)
				{
					const int sx = rx + x, syTop = px.H - 1 - (ry + y);   // y 위 → 그림 줄
					if (sx >= 0 && sx < px.W && syTop >= 0 && syTop < px.H && px.Alpha(sx, syTop) > 24)
						any = true;
				}
			solid[(size_t)gy * gw + gx] = any;
		}
	for (const auto& loop : TraceOutlines(solid, gw, gh))
	{
		std::vector<Vec2> simple = Simplify(loop, 1.0f);
		std::vector<Vec2> path;
		for (const Vec2& p : simple)
			path.push_back(toLocal(Vec2(p.x * step / (float)rw, p.y * step / (float)rh)));
		if (path.size() >= 3)
			Paths.push_back(path);
	}
	if (Paths.empty())
		Paths.push_back({ toLocal(Vec2(0, 0)), toLocal(Vec2(1, 0)), toLocal(Vec2(1, 1)), toLocal(Vec2(0, 1)) });
}

void PolygonCollider2D::OnInspectorGUI()
{
	EnsureFitted();
	bool changed = CommonInspector();
	size_t points = 0;
	for (const auto& p : Paths) points += p.size();
	char buf[96];
	std::vector<Shape2D> shapes;
	BuildShapes(shapes);
	snprintf(buf, sizeof(buf), "%d paths, %d points  (%d convex pieces)", (int)Paths.size(), (int)points, (int)shapes.size());
	UnityGUI::ValueLabel("Points", buf);
	if (UnityGUI::CenterButton("Regenerate from Sprite", 200.0f)) { FitToSprite(); changed = true; }
	if (changed) Touch();
}

GENERATE_COMPONENT_FUNC_TOJSON(PolygonCollider2D)
{
	json j;
	SERIALIZE_TYPE(j, PolygonCollider2D);
	CommonToJson(j);
	json paths = json::array();
	for (const auto& path : Paths)
	{
		json pts = json::array();
		for (const Vec2& p : path) pts.push_back(V2(p));
		paths.push_back(pts);
	}
	j["paths"] = paths;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(PolygonCollider2D)
{
	CommonFromJson(j);
	Paths.clear();
	if (j.contains("paths") && j["paths"].is_array())
		for (const json& path : j["paths"])
		{
			std::vector<Vec2> pts;
			for (const json& p : path)
				if (p.is_array() && p.size() == 2)
					pts.push_back(Vec2(p[0].get<float>(), p[1].get<float>()));
			if (pts.size() >= 3)
				Paths.push_back(pts);
		}
	if (Paths.empty())
		m_NeedsFit = true;   // 경로 없이 붙였으면 스프라이트에 맞춘다
}

// ------------------------------------------------------------------ Edge
void EdgeCollider2D::BuildShapes(std::vector<Shape2D>& out)
{
	for (size_t i = 0; i + 1 < Points.size(); ++i)
	{
		Shape2D s;
		s.K = Shape2D::Segment;
		s.Points = { Offset + Points[i], Offset + Points[i + 1] };
		s.Radius = (std::max)(0.0f, EdgeRadius);
		out.push_back(s);
	}
}

void EdgeCollider2D::Outline(std::vector<std::vector<Vec2>>& loops)
{
	std::vector<Vec2> line;
	for (const Vec2& p : Points) line.push_back(Offset + p);
	loops.push_back(line);
}

void EdgeCollider2D::OnInspectorGUI()
{
	bool changed = CommonInspector();
	changed |= UnityGUI::Float("Edge Radius", &EdgeRadius);
	EdgeRadius = (std::max)(0.0f, EdgeRadius);
	UnityGUI::Label("Points", 0, true);
	int remove = -1;
	for (int i = 0; i < (int)Points.size(); ++i)
	{
		ImGui::PushID(i);
		char label[32];
		snprintf(label, sizeof(label), "Point %d", i);
		changed |= UnityGUI::Vector2Pair(label, "X", &Points[i].x, "Y", &Points[i].y, 1);
		ImGui::PopID();
	}
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 18.0f);
	if (ImGui::SmallButton(ICON_FA_PLUS " Add Point")) { Points.push_back(Points.empty() ? Vec2(0, 0) : Points.back() + Vec2(1, 0)); changed = true; }
	ImGui::SameLine();
	if (Points.size() > 2 && ImGui::SmallButton(ICON_FA_MINUS " Remove Last")) remove = (int)Points.size() - 1;
	if (remove >= 0) { Points.erase(Points.begin() + remove); changed = true; }
	if (changed) Touch();
}

GENERATE_COMPONENT_FUNC_TOJSON(EdgeCollider2D)
{
	json j;
	SERIALIZE_TYPE(j, EdgeCollider2D);
	CommonToJson(j);
	json pts = json::array();
	for (const Vec2& p : Points) pts.push_back(V2(p));
	j["points"] = pts;
	j["edgeRadius"] = EdgeRadius;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(EdgeCollider2D)
{
	CommonFromJson(j);
	EdgeRadius = j.value("edgeRadius", 0.0f);
	if (j.contains("points") && j["points"].is_array() && j["points"].size() >= 2)
	{
		Points.clear();
		for (const json& p : j["points"])
			if (p.is_array() && p.size() == 2)
				Points.push_back(Vec2(p[0].get<float>(), p[1].get<float>()));
	}
}
