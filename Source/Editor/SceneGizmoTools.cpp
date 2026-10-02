#include "pch.h"
#include "SpriteBatch.h"
#include "SceneGizmoTools.h"
#include "UndoSystem.h"
#include "SceneToolbar.h"
#include "EditorCamera.h"
#include "SelectionManager.h"
#include "MeshFilter.h"
#include "MeshRenderer.h"
#include "Mesh.h"
#include "RectTransform.h"
#include "Tree.h"
#include "Rock.h"
#include <unordered_map>

// Unity Scene 뷰 조작 핸들 구현.
//  - 모든 핸들은 ImGui DrawList 로 화면 공간에 그리고, 화면 공간 거리로 호버를 판정한다.
//  - 호버 색은 한 프레임 전 결과를 쓴다 (그리기와 판정을 한 번에 하기 위해).
//  - 드래그 중에는 시작 시점의 Transform 을 기준으로 매 프레임 새 값을 계산한다 (오차 누적 없음).
namespace
{
	using namespace SceneToolbar;

	enum class Kind { None, MoveAxis, MovePlane, MoveView, RotAxis, RotView, ScaleAxis, ScaleUniform, RectCorner, RectEdge, RectBody };

	struct Handle
	{
		Kind kind = Kind::None;
		int index = -1;   // 축 번호(0 X, 1 Y, 2 Z) / 평면의 법선 축 / 사각형의 모서리·변 번호
		bool operator==(const Handle& o) const { return kind == o.kind && index == o.index; }
		bool operator!=(const Handle& o) const { return !(*this == o); }
	};

	// ---- Unity 핸들 색 ----
	const ImU32 kAxisColor[3] = { IM_COL32(219, 62, 29, 255), IM_COL32(139, 220, 0, 255), IM_COL32(58, 122, 248, 255) };
	const ImU32 kHot = IM_COL32(246, 242, 50, 255);
	const ImU32 kViewRing = IM_COL32(210, 210, 210, 255);
	const ImU32 kCenter = IM_COL32(204, 204, 204, 255);

	const float kHandlePx = 88.0f;      // 핸들 크기(화면 px 기준)
	const float kPickPx = 7.0f;         // 선 핸들을 잡을 수 있는 거리

	// ---- 프레임 정보 ----
	struct Frame
	{
		XMMATRIX viewProj;
		ImVec2 vmin, vmax;
		Vec3 camPos, camRight, camUp, camLook;
		float fovY = 1.0f;
		ImVec2 mouse;
		ImDrawList* dl = nullptr;
	} f;

	// ---- 호버 판정 (이번 프레임 후보 / 지난 프레임 결과) ----
	Handle s_PrevHot;
	Handle s_BestHot;
	float s_BestDist = FLT_MAX;
	Vec3 s_BestGrab;

	// ---- 드래그 상태 ----
	struct DragState
	{
		bool active = false;
		Handle h;
		GameObject* target = nullptr;
		ImVec2 mouseStart;

		Vec3 startPos;             // 오브젝트 월드 위치
		Quaternion startRot;       // 월드 회전
		Vec3 startLocalScale;
		Vec2 startSizeDelta;   // UI(RectTransform): Rect 도구는 크기를 바꾼다
		Matrix startWorld;
		Vec3 handlePos;            // 핸들 위치 (Pivot 이면 startPos, Center 면 바운드 중심)

		Vec3 axis;                 // 이동/회전 축 (월드)
		Vec3 planeNormal;
		Vec3 hitStart;
		float t0 = 0.0f;

		ImVec2 tangent;            // 회전: 잡은 지점의 화면 접선
		float radiusPx = 1.0f;
		Vec3 grabVec;              // 회전: 핸들 중심 → 잡은 지점
		float angle = 0.0f;

		// Rect 도구
		Vec3 bmin, bmax;
		int ri = 0, rj = 1, rk = 2;
	} d;

	// 카메라 궤도/패닝
	bool s_Orbiting = false;
	bool s_ClickCandidate = false;
	ImVec2 s_ClickPos;

	// ------------------------------------------------------------------ 수학 도우미
	bool Project(const Vec3& w, ImVec2& out)
	{
		XMVECTOR c = XMVector4Transform(XMVectorSet(w.x, w.y, w.z, 1.0f), f.viewProj);
		float cw = XMVectorGetW(c);
		if (cw <= 1e-4f)
			return false;
		float x = XMVectorGetX(c) / cw, y = XMVectorGetY(c) / cw;
		out = ImVec2(f.vmin.x + (x * 0.5f + 0.5f) * (f.vmax.x - f.vmin.x), f.vmin.y + (0.5f - y * 0.5f) * (f.vmax.y - f.vmin.y));
		return true;
	}

	void MouseRay(const ImVec2& m, Vec3& origin, Vec3& dir)
	{
		float nx = (m.x - f.vmin.x) / (f.vmax.x - f.vmin.x) * 2.0f - 1.0f;
		float ny = 1.0f - (m.y - f.vmin.y) / (f.vmax.y - f.vmin.y) * 2.0f;
		XMMATRIX inv = XMMatrixInverse(nullptr, f.viewProj);
		Vec3 n = XMVector3TransformCoord(XMVectorSet(nx, ny, 0.0f, 1.0f), inv);
		Vec3 fa = XMVector3TransformCoord(XMVectorSet(nx, ny, 1.0f, 1.0f), inv);
		origin = n;
		dir = fa - n;
		dir.Normalize();
	}

	// 직선(p, a) 위에서 광선(o, r)에 가장 가까운 점의 매개변수
	bool ClosestOnAxis(const Vec3& p, const Vec3& a, const Vec3& o, const Vec3& r, float& t)
	{
		Vec3 w0 = p - o;
		float A = a.Dot(a), B = a.Dot(r), C = r.Dot(r), D = a.Dot(w0), E = r.Dot(w0);
		float denom = A * C - B * B;
		if (fabsf(denom) < 1e-6f)
			return false;
		t = (B * E - C * D) / denom;
		return true;
	}

	bool RayPlane(const Vec3& o, const Vec3& r, const Vec3& p, const Vec3& n, Vec3& hit)
	{
		float dn = r.Dot(n);
		if (fabsf(dn) < 1e-5f)
			return false;
		float t = (p - o).Dot(n) / dn;
		if (t < 0.0f)
			return false;
		hit = o + r * t;
		return true;
	}

	float Len2(const ImVec2& v) { return sqrtf(v.x * v.x + v.y * v.y); }

	float DistSeg(const ImVec2& p, const ImVec2& a, const ImVec2& b)
	{
		ImVec2 ab(b.x - a.x, b.y - a.y), ap(p.x - a.x, p.y - a.y);
		float l = ab.x * ab.x + ab.y * ab.y;
		float t = l > 1e-6f ? std::clamp((ap.x * ab.x + ap.y * ab.y) / l, 0.0f, 1.0f) : 0.0f;
		return Len2(ImVec2(a.x + ab.x * t - p.x, a.y + ab.y * t - p.y));
	}

	bool InQuad(const ImVec2& p, const ImVec2 q[4])
	{
		int pos = 0, neg = 0;
		for (int i = 0; i < 4; ++i)
		{
			const ImVec2& a = q[i];
			const ImVec2& b = q[(i + 1) % 4];
			float c = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
			(c >= 0 ? pos : neg)++;
		}
		return pos == 0 || neg == 0;
	}

	float Snap(float v, float inc) { return inc > 0.0f ? roundf(v / inc) * inc : v; }

	float HandleSize(const Vec3& p)
	{
		float dist = (p - f.camPos).Dot(f.camLook);
		dist = (std::max)(dist, 0.01f);
		return dist * 2.0f * tanf(f.fovY * 0.5f) * (kHandlePx / (std::max)(1.0f, f.vmax.y - f.vmin.y));
	}

	void Perp(const Vec3& a, Vec3& u, Vec3& v)
	{
		Vec3 ref = fabsf(a.y) > 0.9f ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
		u = a.Cross(ref);
		u.Normalize();
		v = a.Cross(u);
		v.Normalize();
	}

	void Candidate(const Handle& h, float dist, const Vec3& grab = Vec3::Zero)
	{
		if (dist < s_BestDist)
		{
			s_BestDist = dist;
			s_BestHot = h;
			s_BestGrab = grab;
		}
	}

	bool IsHot(const Handle& h) { return d.active ? d.h == h : s_PrevHot == h; }
	ImU32 Col(const Handle& h, ImU32 base) { return IsHot(h) ? kHot : base; }
	ImU32 WithAlpha(ImU32 c, int a) { return (c & ~IM_COL32_A_MASK) | ((ImU32)a << IM_COL32_A_SHIFT); }

	// ------------------------------------------------------------------ 오브젝트 정보
	Mesh* ObjectMesh(GameObject* go)
	{
		if (MeshFilter* mf = go->GetComponent<MeshFilter>())
			if (mf->GetMesh())
				return mf->GetMesh().get();
		if (MeshRenderer* mr = go->GetComponent<MeshRenderer>())
			if (auto m = mr->GetMesh())
				return m.get();
		return nullptr;
	}

	// 메시 로컬 AABB (없으면 단위 큐브)
	void LocalBounds(GameObject* go, Vec3& bmin, Vec3& bmax)
	{
		// UI 요소: RectTransform 사각형 (F 포커스, Rect 도구가 UI 크기에 맞게)
		if (RectTransform* rt = go ? go->GetComponent<RectTransform>() : nullptr)
		{
			const Vec2 mn = rt->GetRectMin(), mx = rt->GetRectMin() + rt->GetRectSize();
			bmin = Vec3(mn.x, mn.y, -0.5f);
			bmax = Vec3(mx.x, mx.y, 0.5f);
			return;
		}
		// 절차적 나무: 생성된 메시의 범위
		if (Tree* tree = go ? go->GetComponent<Tree>() : nullptr)
			if (tree->GetLocalBounds(bmin, bmax))
				return;
		// 절차적 바위
		if (Rock* rock = go ? go->GetComponent<Rock>() : nullptr)
			if (rock->GetLocalBounds(bmin, bmax))
				return;
		// 2D 스프라이트 (SpriteRenderer · 패키지의 2D 렌더러)
		if (go)
			for (SpriteSource* src : SpriteSource::All())
				if (src->SpriteOwner() == go && src->SpriteLocalBounds(bmin, bmax))
					return;
		static std::unordered_map<const Mesh*, std::pair<Vec3, Vec3>> cache;
		Mesh* mesh = ObjectMesh(go);
		if (mesh == nullptr || mesh->Vertices.empty())
		{
			bmin = Vec3(-0.5f, -0.5f, -0.5f);
			bmax = Vec3(0.5f, 0.5f, 0.5f);
			return;
		}
		auto it = cache.find(mesh);
		if (it == cache.end() || it->second.first.x > it->second.second.x)
		{
			Vec3 mn(FLT_MAX, FLT_MAX, FLT_MAX), mx(-FLT_MAX, -FLT_MAX, -FLT_MAX);
			for (const auto& v : mesh->Vertices)
			{
				mn = Vec3::Min(mn, Vec3(v.pos));
				mx = Vec3::Max(mx, Vec3(v.pos));
			}
			it = cache.insert_or_assign(mesh, std::make_pair(mn, mx)).first;
		}
		bmin = it->second.first;
		bmax = it->second.second;
	}

	void Axes(Transform* tr, bool local, Vec3 out[3])
	{
		if (!local)
		{
			out[0] = Vec3(1, 0, 0); out[1] = Vec3(0, 1, 0); out[2] = Vec3(0, 0, 1);
			return;
		}
		Matrix w = tr->GetWorldMatrix();
		out[0] = w.Right(); out[1] = w.Up(); out[2] = w.Backward();
		for (int i = 0; i < 3; ++i)
		{
			if (out[i].LengthSquared() < 1e-10f)
				out[i] = i == 0 ? Vec3(1, 0, 0) : (i == 1 ? Vec3(0, 1, 0) : Vec3(0, 0, 1));
			out[i].Normalize();
		}
	}

	// 부모가 있어도 월드 회전을 지정할 수 있게 한다 (Transform::SetRotation 은 부모가 있으면 무시됨)
	void SetWorldRotation(Transform* tr, const Quaternion& q)
	{
		if (tr->HasParent())
		{
			Quaternion parentInv;
			tr->GetParent()->GetRotation().Inverse(parentInv);
			Quaternion local = q * parentInv;
			local.Normalize();
			tr->SetLocalRotation(local);
		}
		else
		{
			Quaternion n = q;
			n.Normalize();
			tr->SetLocalRotation(n);
		}
	}

	// ------------------------------------------------------------------ 그리기 도우미
	void Line(const Vec3& a, const Vec3& b, ImU32 c, float th)
	{
		ImVec2 pa, pb;
		if (Project(a, pa) && Project(b, pb))
			f.dl->AddLine(pa, pb, c, th);
	}

	void Cone(const Vec3& base, const Vec3& tip, float radius, ImU32 c)
	{
		Vec3 a = tip - base;
		a.Normalize();
		Vec3 u, v;
		Perp(a, u, v);
		const int N = 12;
		ImVec2 pts[N], pt, pb;
		if (!Project(tip, pt) || !Project(base, pb))
			return;
		for (int i = 0; i < N; ++i)
		{
			float th = XM_2PI * i / N;
			if (!Project(base + (u * cosf(th) + v * sinf(th)) * radius, pts[i]))
				return;
		}
		for (int i = 0; i < N; ++i)
		{
			f.dl->AddTriangleFilled(pt, pts[i], pts[(i + 1) % N], c);
			f.dl->AddTriangleFilled(pb, pts[i], pts[(i + 1) % N], c);
		}
	}

	void ScreenSquare(const ImVec2& c, float half, ImU32 fill)
	{
		f.dl->AddRectFilled(ImVec2(c.x - half, c.y - half), ImVec2(c.x + half, c.y + half), fill);
	}

	// ------------------------------------------------------------------ Move
	void DrawMove(const Vec3& P, const Vec3 ax[3], float s, bool withPlanes, bool withViewCenter)
	{
		ImVec2 pc;
		if (!Project(P, pc))
			return;

		Vec3 toCam = f.camPos - P;
		float sign[3];
		for (int i = 0; i < 3; ++i)
			sign[i] = toCam.Dot(ax[i]) >= 0.0f ? 1.0f : -1.0f;

		// 평면 핸들 (카메라 쪽 사분면에 배치, 색 = 법선 축 색)
		if (withPlanes)
		{
			for (int k = 0; k < 3; ++k)
			{
				int i = (k + 1) % 3, j = (k + 2) % 3;
				Vec3 a = ax[i] * sign[i] * s, b = ax[j] * sign[j] * s;
				const float lo = 0.0f, hi = 0.28f;
				Vec3 q[4] = { P + a * lo + b * lo, P + a * hi + b * lo, P + a * hi + b * hi, P + a * lo + b * hi };
				ImVec2 sq[4];
				bool ok = true;
				for (int n = 0; n < 4; ++n) ok &= Project(q[n], sq[n]);
				if (!ok)
					continue;
				// 평면이 시선과 거의 평행하면 숨긴다
				Vec3 nrm = ax[k];
				Vec3 view = P - f.camPos;
				view.Normalize();
				if (fabsf(nrm.Dot(view)) < 0.15f)
					continue;
				Handle h{ Kind::MovePlane, k };
				ImU32 c = Col(h, kAxisColor[k]);
				f.dl->AddQuadFilled(sq[0], sq[1], sq[2], sq[3], WithAlpha(c, IsHot(h) ? 140 : 70));
				f.dl->AddQuad(sq[0], sq[1], sq[2], sq[3], c, 1.0f);
				if (InQuad(f.mouse, sq))
					Candidate(h, 0.5f);
			}
		}

		// 축 화살표
		for (int i = 0; i < 3; ++i)
		{
			Vec3 tip = P + ax[i] * s;
			ImVec2 pt;
			if (!Project(tip, pt))
				continue;
			if (Len2(ImVec2(pt.x - pc.x, pt.y - pc.y)) < 8.0f)
				continue;   // 시선 방향과 평행한 축은 숨김
			Handle h{ Kind::MoveAxis, i };
			ImU32 c = Col(h, kAxisColor[i]);
			Vec3 coneBase = P + ax[i] * (s * 0.8f);
			Line(P, coneBase, c, 2.0f);
			Cone(coneBase, tip, s * 0.07f, c);
			Candidate(h, (std::min)(DistSeg(f.mouse, pc, pt), Len2(ImVec2(f.mouse.x - pt.x, f.mouse.y - pt.y)) - 2.0f));
		}

		// 가운데 사각형 (카메라 평면 이동)
		if (withViewCenter)
		{
			Handle h{ Kind::MoveView, 0 };
			ImU32 c = Col(h, kCenter);
			f.dl->AddRect(ImVec2(pc.x - 5, pc.y - 5), ImVec2(pc.x + 5, pc.y + 5), c, 0.0f, 0, 1.5f);
			if (fabsf(f.mouse.x - pc.x) <= 6 && fabsf(f.mouse.y - pc.y) <= 6)
				Candidate(h, 0.0f);
		}
	}

	// ------------------------------------------------------------------ Rotate
	void RingPoint(const Vec3& P, const Vec3& u, const Vec3& v, float r, float th, Vec3& out)
	{
		out = P + (u * cosf(th) + v * sinf(th)) * r;
	}

	void DrawRotate(const Vec3& P, const Vec3 ax[3], float r)
	{
		ImVec2 pc;
		if (!Project(P, pc))
			return;
		Vec3 toCam = f.camPos - P;
		toCam.Normalize();
		const int N = 72;

		// 구 외곽선 (반투명 회색)
		{
			ImVec2 prev;
			bool hasPrev = false;
			for (int n = 0; n <= N; ++n)
			{
				Vec3 w;
				RingPoint(P, f.camRight, f.camUp, r, XM_2PI * n / N, w);
				ImVec2 p;
				if (Project(w, p))
				{
					if (hasPrev) f.dl->AddLine(prev, p, IM_COL32(160, 160, 160, 70), 1.0f);
					prev = p;
					hasPrev = true;
				}
			}
		}

		// 축 링: 카메라 쪽 절반만 표시
		for (int i = 0; i < 3; ++i)
		{
			Vec3 u = ax[(i + 1) % 3], v = ax[(i + 2) % 3];
			Handle h{ Kind::RotAxis, i };
			ImU32 c = Col(h, kAxisColor[i]);
			for (int n = 0; n < N; ++n)
			{
				Vec3 a, b;
				RingPoint(P, u, v, r, XM_2PI * n / N, a);
				RingPoint(P, u, v, r, XM_2PI * (n + 1) / N, b);
				Vec3 mid = (a + b) * 0.5f - P;
				mid.Normalize();
				if (mid.Dot(toCam) < -0.02f)
					continue;
				ImVec2 pa, pb;
				if (!Project(a, pa) || !Project(b, pb))
					continue;
				f.dl->AddLine(pa, pb, c, 2.0f);
				Candidate(h, DistSeg(f.mouse, pa, pb), (a + b) * 0.5f);
			}
		}

		// 바깥쪽 화면 평면 링 (시선 축 회전)
		{
			Handle h{ Kind::RotView, 0 };
			ImU32 c = Col(h, kViewRing);
			const float rr = r * 1.15f;
			for (int n = 0; n < N; ++n)
			{
				Vec3 a, b;
				RingPoint(P, f.camRight, f.camUp, rr, XM_2PI * n / N, a);
				RingPoint(P, f.camRight, f.camUp, rr, XM_2PI * (n + 1) / N, b);
				ImVec2 pa, pb;
				if (!Project(a, pa) || !Project(b, pb))
					continue;
				f.dl->AddLine(pa, pb, c, 1.5f);
				Candidate(h, DistSeg(f.mouse, pa, pb) + 0.5f, (a + b) * 0.5f);
			}
		}

		// 드래그 중: 회전한 만큼 부채꼴 표시
		if (d.active && (d.h.kind == Kind::RotAxis || d.h.kind == Kind::RotView) && fabsf(d.angle) > 1e-4f)
		{
			Vec3 g = d.grabVec;
			g.Normalize();
			const float rr = d.h.kind == Kind::RotView ? r * 1.15f : r;
			int steps = (std::max)(2, (int)(fabsf(d.angle) / XM_2PI * 72.0f));
			ImVec2 prev;
			Project(P + g * rr, prev);
			ImU32 fill = WithAlpha(d.h.kind == Kind::RotView ? kViewRing : kAxisColor[d.h.index], 60);
			for (int n = 1; n <= steps; ++n)
			{
				Quaternion q = Quaternion::CreateFromAxisAngle(d.axis, d.angle * n / steps);
				ImVec2 p;
				if (!Project(P + Vec3::Transform(g, q) * rr, p))
					break;
				f.dl->AddTriangleFilled(pc, prev, p, fill);
				prev = p;
			}
		}
	}

	// ------------------------------------------------------------------ Scale
	void DrawScale(const Vec3& P, const Vec3 ax[3], float s, bool axesToo)
	{
		ImVec2 pc;
		if (!Project(P, pc))
			return;

		if (axesToo)
		{
			for (int i = 0; i < 3; ++i)
			{
				Handle h{ Kind::ScaleAxis, i };
				float len = s;
				if (d.active && d.h == h)
				{
					float start = (&d.startLocalScale.x)[i];
					const Vec3 cur = d.target->GetTransform()->GetLocalScale();
					float now = (&cur.x)[i];
					if (fabsf(start) > 1e-5f)
						len = s * now / start;
				}
				else if (d.active && d.h.kind == Kind::ScaleUniform)
				{
					Transform* tr = d.target->GetTransform();
					if (fabsf(d.startLocalScale.x) > 1e-5f)
						len = s * tr->GetLocalScale().x / d.startLocalScale.x;
				}
				Vec3 end = P + ax[i] * len;
				ImVec2 pe;
				if (!Project(end, pe))
					continue;
				ImVec2 pe0;
				Project(P + ax[i] * s, pe0);
				if (Len2(ImVec2(pe0.x - pc.x, pe0.y - pc.y)) < 8.0f)
					continue;
				ImU32 c = Col(h, kAxisColor[i]);
				f.dl->AddLine(pc, pe, c, 2.0f);
				ScreenSquare(pe, 4.5f, c);
				Candidate(h, (std::min)(DistSeg(f.mouse, pc, pe), (std::max)(fabsf(f.mouse.x - pe.x), fabsf(f.mouse.y - pe.y)) - 3.0f));
			}
		}

		Handle h{ Kind::ScaleUniform, 0 };
		ScreenSquare(pc, 5.5f, Col(h, kCenter));
		if (fabsf(f.mouse.x - pc.x) <= 7 && fabsf(f.mouse.y - pc.y) <= 7)
			Candidate(h, 0.0f);
	}

	// ------------------------------------------------------------------ Rect
	void RectPlane(Transform* tr, const Vec3& bmin, const Vec3& bmax, int& i, int& j, int& k)
	{
		Vec3 ax[3];
		Axes(tr, true, ax);
		Vec3 view = tr->GetPosition() - f.camPos;
		view.Normalize();
		k = 2;
		float best = -1.0f;
		for (int n = 0; n < 3; ++n)
		{
			float dd = fabsf(ax[n].Dot(view));
			if (dd > best) { best = dd; k = n; }
		}
		i = (k + 1) % 3;
		j = (k + 2) % 3;
		if (i > j) std::swap(i, j);
		(void)bmin; (void)bmax;
	}

	// Rect 모서리의 메시 로컬 좌표 (c: 0 = (min,min), 1 = (max,min), 2 = (max,max), 3 = (min,max))
	Vec3 RectCornerLocal(const Vec3& bmin, const Vec3& bmax, int i, int j, int k, int c)
	{
		Vec3 p = (bmin + bmax) * 0.5f;
		(&p.x)[i] = (c == 0 || c == 3) ? (&bmin.x)[i] : (&bmax.x)[i];
		(&p.x)[j] = (c == 0 || c == 1) ? (&bmin.x)[j] : (&bmax.x)[j];
		(void)k;
		return p;
	}

	void DrawRect(GameObject* go)
	{
		Transform* tr = go->GetTransform();
		Vec3 bmin, bmax;
		LocalBounds(go, bmin, bmax);
		int i, j, k;
		if (d.active && d.h.kind >= Kind::RectCorner)
		{
			i = d.ri; j = d.rj; k = d.rk;
		}
		else
			RectPlane(tr, bmin, bmax, i, j, k);

		Matrix w = tr->GetWorldMatrix();
		ImVec2 q[4];
		for (int c = 0; c < 4; ++c)
			if (!Project(Vec3::Transform(RectCornerLocal(bmin, bmax, i, j, k, c), w), q[c]))
				return;

		// 몸통 (평면 이동)
		Handle body{ Kind::RectBody, 0 };
		if (InQuad(f.mouse, q))
			Candidate(body, 4.5f);

		const ImU32 line = IM_COL32(230, 230, 230, 220);
		for (int e = 0; e < 4; ++e)
		{
			Handle h{ Kind::RectEdge, e };
			f.dl->AddLine(q[e], q[(e + 1) % 4], IsHot(h) ? kHot : line, IsHot(h) ? 2.0f : 1.0f);
			Candidate(h, DistSeg(f.mouse, q[e], q[(e + 1) % 4]) + 0.5f);
		}
		for (int c = 0; c < 4; ++c)
		{
			Handle h{ Kind::RectCorner, c };
			f.dl->AddCircleFilled(q[c], 4.5f, IsHot(h) ? kHot : IM_COL32(64, 148, 255, 255));
			f.dl->AddCircle(q[c], 4.5f, IM_COL32(255, 255, 255, 255), 0, 1.2f);
			Candidate(h, Len2(ImVec2(f.mouse.x - q[c].x, f.mouse.y - q[c].y)) - 3.0f);
		}

		// 피벗 표시
		ImVec2 pp;
		if (Project(tr->GetPosition(), pp))
			f.dl->AddCircle(pp, 5.0f, IM_COL32(64, 148, 255, 255), 0, 1.5f);
	}

	// ------------------------------------------------------------------ 드래그 시작 / 적용
	void BeginDrag(GameObject* go, const Handle& h, const Vec3& handlePos, const Vec3 ax[3], const Vec3& grab)
	{
		Transform* tr = go->GetTransform();
		switch (h.kind)
		{
		case Kind::RotAxis: case Kind::RotView: Undo::SetActionName("Rotate"); break;
		case Kind::ScaleAxis: case Kind::ScaleUniform: Undo::SetActionName("Scale"); break;
		case Kind::RectCorner: case Kind::RectEdge: case Kind::RectBody: Undo::SetActionName("Rect Transform"); break;
		default: Undo::SetActionName("Move"); break;
		}
		d = DragState();
		d.active = true;
		d.h = h;
		d.target = go;
		d.mouseStart = f.mouse;
		d.startPos = tr->GetPosition();
		d.startRot = tr->GetRotation();
		d.startLocalScale = tr->GetLocalScale();
		if (RectTransform* rt = tr->GetGameObject()->GetComponent<RectTransform>())
			d.startSizeDelta = rt->GetSizeDelta();
		d.startWorld = tr->GetWorldMatrix();
		d.handlePos = handlePos;

		Vec3 ro, rd;
		MouseRay(f.mouse, ro, rd);

		switch (h.kind)
		{
		case Kind::MoveAxis:
			d.axis = ax[h.index];
			ClosestOnAxis(handlePos, d.axis, ro, rd, d.t0);
			break;
		case Kind::MovePlane:
		case Kind::MoveView:
		case Kind::RectBody:
			d.planeNormal = h.kind == Kind::MovePlane ? ax[h.index] : f.camLook;
			if (h.kind == Kind::RectBody)
			{
				LocalBounds(go, d.bmin, d.bmax);
				RectPlane(tr, d.bmin, d.bmax, d.ri, d.rj, d.rk);
				Vec3 lax[3];
				Axes(tr, true, lax);
				d.planeNormal = lax[d.rk];
			}
			if (!RayPlane(ro, rd, handlePos, d.planeNormal, d.hitStart))
				d.active = false;
			break;
		case Kind::RotAxis:
		case Kind::RotView:
		{
			d.axis = h.kind == Kind::RotAxis ? ax[h.index] : f.camLook;
			d.grabVec = grab - handlePos;
			// 잡은 지점에서 +회전 방향의 화면 접선을 수치적으로 구한다 (좌표계 규약에 의존하지 않음)
			Vec3 moved = handlePos + Vec3::Transform(d.grabVec, Quaternion::CreateFromAxisAngle(d.axis, 0.05f));
			ImVec2 a, b, c;
			if (Project(grab, a) && Project(moved, b) && Project(handlePos, c))
			{
				ImVec2 t(b.x - a.x, b.y - a.y);
				float l = Len2(t);
				d.tangent = l > 1e-4f ? ImVec2(t.x / l, t.y / l) : ImVec2(1, 0);
				d.radiusPx = (std::max)(20.0f, Len2(ImVec2(a.x - c.x, a.y - c.y)));
			}
			else
				d.active = false;
			break;
		}
		case Kind::RectCorner:
		case Kind::RectEdge:
		{
			LocalBounds(go, d.bmin, d.bmax);
			RectPlane(tr, d.bmin, d.bmax, d.ri, d.rj, d.rk);
			Vec3 lax[3];
			Axes(tr, true, lax);
			d.planeNormal = lax[d.rk];
			d.hitStart = Vec3::Transform((d.bmin + d.bmax) * 0.5f, d.startWorld);
			break;
		}
		default:
			break;
		}
	}

	void ApplyDrag()
	{
		Transform* tr = d.target->GetTransform();
		Vec3 ro, rd;
		MouseRay(f.mouse, ro, rd);
		const bool snap = SnapEnabled();
		const float inc = SnapIncrement();
		const bool global = Space() == HandleSpace::Global;
		ImVec2 total(f.mouse.x - d.mouseStart.x, f.mouse.y - d.mouseStart.y);

		switch (d.h.kind)
		{
		case Kind::MoveAxis:
		{
			float t;
			if (!ClosestOnAxis(d.handlePos, d.axis, ro, rd, t))
				return;
			float delta = t - d.t0;
			Vec3 pos = d.startPos + d.axis * delta;
			if (snap)
			{
				if (global)
					(&pos.x)[d.h.index] = Snap((&pos.x)[d.h.index], inc);
				else
					pos = d.startPos + d.axis * Snap(delta, inc);
			}
			tr->SetPosition(pos);
			break;
		}
		case Kind::MovePlane:
		case Kind::MoveView:
		case Kind::RectBody:
		{
			Vec3 hit;
			if (!RayPlane(ro, rd, d.handlePos, d.planeNormal, hit))
				return;
			Vec3 off = hit - d.hitStart;
			Vec3 pos = d.startPos + off;
			if (snap && d.h.kind == Kind::MovePlane)
			{
				int i = (d.h.index + 1) % 3, j = (d.h.index + 2) % 3;
				if (global)
				{
					(&pos.x)[i] = Snap((&pos.x)[i], inc);
					(&pos.x)[j] = Snap((&pos.x)[j], inc);
				}
			}
			tr->SetPosition(pos);
			break;
		}
		case Kind::RotAxis:
		case Kind::RotView:
		{
			float angle = (total.x * d.tangent.x + total.y * d.tangent.y) / d.radiusPx;
			if (snap)
				angle = Snap(angle, XMConvertToRadians(15.0f));
			d.angle = angle;
			Quaternion q = Quaternion::CreateFromAxisAngle(d.axis, angle);
			SetWorldRotation(tr, d.startRot * q);
			// Center 모드: 핸들 중심을 기준으로 회전하므로 위치도 함께 움직인다
			if (Pivot() == PivotMode::Center)
				tr->SetPosition(d.handlePos + Vec3::Transform(d.startPos - d.handlePos, q));
			break;
		}
		case Kind::ScaleAxis:
		case Kind::ScaleUniform:
		{
			float factor = 1.0f;
			if (d.h.kind == Kind::ScaleAxis)
			{
				Vec3 ax[3];
				Vec3 lax[3];
				(void)ax;
				// 시작 시점의 로컬 축 (d.startWorld 기준)
				lax[0] = d.startWorld.Right(); lax[1] = d.startWorld.Up(); lax[2] = d.startWorld.Backward();
				Vec3 a = lax[d.h.index];
				a.Normalize();
				ImVec2 p0, p1;
				if (!Project(d.handlePos, p0) || !Project(d.handlePos + a * HandleSize(d.handlePos), p1))
					return;
				ImVec2 s2(p1.x - p0.x, p1.y - p0.y);
				float L = Len2(s2);
				if (L < 5.0f)
					return;
				factor = 1.0f + (total.x * s2.x + total.y * s2.y) / (L * L);
			}
			else
				factor = 1.0f + (total.x - total.y) * 0.01f;

			Vec3 sc = d.startLocalScale;
			for (int n = 0; n < 3; ++n)
			{
				if (d.h.kind == Kind::ScaleAxis && n != d.h.index)
					continue;
				float v = (&sc.x)[n] * factor;
				if (snap)
					v = Snap(v, 0.1f);
				if (fabsf(v) < 1e-3f)
					v = v < 0.0f ? -1e-3f : 1e-3f;
				(&sc.x)[n] = v;
			}
			tr->SetLocalScale(sc);
			break;
		}
		case Kind::RectCorner:
		case Kind::RectEdge:
		{
			Vec3 hit;
			if (!RayPlane(ro, rd, d.hitStart, d.planeNormal, hit))
				return;
			Vec3 local = Vec3::Transform(hit, d.startWorld.Invert());
			const int i = d.ri, j = d.rj;
			// 움직이는 변: 모서리는 두 축, 변은 한 축
			bool moveI = false, moveJ = false, maxI = false, maxJ = false;
			if (d.h.kind == Kind::RectCorner)
			{
				int c = d.h.index;
				moveI = moveJ = true;
				maxI = (c == 1 || c == 2);
				maxJ = (c == 2 || c == 3);
			}
			else
			{
				int e = d.h.index;   // 0: 모서리0-1 (min j), 1: 1-2 (max i), 2: 2-3 (max j), 3: 3-0 (min i)
				if (e == 0) { moveJ = true; maxJ = false; }
				if (e == 1) { moveI = true; maxI = true; }
				if (e == 2) { moveJ = true; maxJ = true; }
				if (e == 3) { moveI = true; maxI = false; }
			}
			Vec3 g(1, 1, 1);
			Vec3 fixedP = (d.bmin + d.bmax) * 0.5f;
			auto axisScale = [&](int ax, bool isMax) {
				float lo = (&d.bmin.x)[ax], hi = (&d.bmax.x)[ax];
				float fixedV = isMax ? lo : hi;
				float movingV = isMax ? hi : lo;
				float denom = movingV - fixedV;
				if (fabsf(denom) < 1e-6f)
					return;
				float gv = ((&local.x)[ax] - fixedV) / denom;
				gv = (std::max)(gv, 0.01f);
				(&g.x)[ax] = gv;
				(&fixedP.x)[ax] = fixedV;
			};
			if (moveI) axisScale(i, maxI);
			if (moveJ) axisScale(j, maxJ);

			// UI 요소: Unity 처럼 크기(Width/Height)를 바꾸고 반대쪽 변을 고정
			if (RectTransform* rt = tr->GetGameObject()->GetComponent<RectTransform>(); rt && !rt->IsDrivenByCanvas())
			{
				const Vec2 startSize(d.bmax.x - d.bmin.x, d.bmax.y - d.bmin.y);
				const Vec2 newSize(startSize.x * g.x, startSize.y * g.y);
				rt->SetSizeDelta(d.startSizeDelta + (newSize - startSize));
				Vec3 fixedScaled(fixedP.x * g.x, fixedP.y * g.y, fixedP.z * g.z);
				tr->SetPosition(d.startPos + Vec3::TransformNormal(fixedP - fixedScaled, d.startWorld));
				break;
			}

			Vec3 sc = d.startLocalScale;
			sc.x *= g.x; sc.y *= g.y; sc.z *= g.z;
			tr->SetLocalScale(sc);
			// 반대쪽 모서리/변이 제자리에 있도록 위치 보정
			Vec3 fixedScaled(fixedP.x * g.x, fixedP.y * g.y, fixedP.z * g.z);
			tr->SetPosition(d.startPos + Vec3::TransformNormal(fixedP - fixedScaled, d.startWorld));
			break;
		}
		default:
			break;
		}
	}

	// ------------------------------------------------------------------ 클릭 선택 (레이캐스트)
	bool RayTriangle(const Vec3& o, const Vec3& r, const Vec3& a, const Vec3& b, const Vec3& c, float& t)
	{
		Vec3 e1 = b - a, e2 = c - a;
		Vec3 p = r.Cross(e2);
		float det = e1.Dot(p);
		if (fabsf(det) < 1e-9f)
			return false;
		float inv = 1.0f / det;
		Vec3 s = o - a;
		float u = s.Dot(p) * inv;
		if (u < 0.0f || u > 1.0f)
			return false;
		Vec3 q = s.Cross(e1);
		float v = r.Dot(q) * inv;
		if (v < 0.0f || u + v > 1.0f)
			return false;
		t = e2.Dot(q) * inv;
		return t > 0.0f;
	}

	bool RayAabb(const Vec3& o, const Vec3& r, const Vec3& mn, const Vec3& mx)
	{
		float tmin = -FLT_MAX, tmax = FLT_MAX;
		for (int i = 0; i < 3; ++i)
		{
			float oi = (&o.x)[i], ri = (&r.x)[i];
			float lo = (&mn.x)[i], hi = (&mx.x)[i];
			if (fabsf(ri) < 1e-9f)
			{
				if (oi < lo || oi > hi) return false;
				continue;
			}
			float t1 = (lo - oi) / ri, t2 = (hi - oi) / ri;
			if (t1 > t2) std::swap(t1, t2);
			tmin = (std::max)(tmin, t1);
			tmax = (std::min)(tmax, t2);
			if (tmin > tmax) return false;
		}
		return tmax > 0.0f;
	}

	void PickRecursive(GameObject* go, const Vec3& ro, const Vec3& rd, GameObject*& best, float& bestT)
	{
		if (go == nullptr)
			return;
		Transform* tr = go->GetTransform();
		// 절차적 나무: 로컬 공간 광선으로 수피·잎 카드 삼각형 검사 (방향은 정규화하지 않아 t 가 월드와 같다)
		if (Tree* tree = go->GetComponent<Tree>(); tree && tr)
		{
			const Matrix inv = tr->GetWorldMatrix().Invert();
			float t = bestT;
			if (tree->RaycastLocal(Vec3::Transform(ro, inv), Vec3::TransformNormal(rd, inv), t) && t < bestT)
			{
				bestT = t;
				best = go;
			}
		}
		// 절차적 바위: 로컬 공간 광선으로 메시 삼각형
		if (Rock* rock = go->GetComponent<Rock>(); rock && tr)
		{
			const Matrix inv = tr->GetWorldMatrix().Invert();
			float t = bestT;
			if (rock->RaycastLocal(Vec3::Transform(ro, inv), Vec3::TransformNormal(rd, inv), t) && t < bestT)
			{
				bestT = t;
				best = go;
			}
		}
		if (Terrain* terrain = go->GetComponent<Terrain>())
		{
			Vec3 hit;
			if (terrain->Raycast(ro, rd, bestT, hit))
			{
				const float t = (hit - ro).Length();
				if (t < bestT)
				{
					bestT = t;
					best = go;
				}
			}
		}
		// 2D 스프라이트: 로컬 사각형 (얇은 상자) 과 광선
		bool spriteHit = false;
		if (tr != nullptr)
			for (SpriteSource* src : SpriteSource::All())
			{
				Vec3 smin, smax;
				if (src->SpriteOwner() != go || !src->ActiveInHierarchy() || !src->SpriteLocalBounds(smin, smax))
					continue;
				spriteHit = true;   // 스프라이트가 있으면 위치 근처 클릭으로 고르지 않는다
				const Matrix inv = tr->GetWorldMatrix().Invert();
				const Vec3 lo = Vec3::Transform(ro, inv), ld = Vec3::TransformNormal(rd, inv);
				// z = 0 평면과 만나는 곳이 사각형 안인가 (방향은 정규화하지 않아 t 가 월드와 같다)
				if (fabsf(ld.z) > 1e-8f)
				{
					const float t = -lo.z / ld.z;
					const Vec3 hit = lo + ld * t;
					if (t > 0.0f && t < bestT && hit.x >= smin.x && hit.x <= smax.x && hit.y >= smin.y && hit.y <= smax.y)
					{
						bestT = t;
						best = go;
					}
				}
			}
		Mesh* mesh = ObjectMesh(go);
		if (spriteHit)
		{
		}
		else if (tr != nullptr && mesh != nullptr && !mesh->Vertices.empty() && !mesh->Indices.empty())
		{
			// 광선을 메시 로컬 공간으로 (방향은 정규화하지 않아 t 가 월드와 같다)
			Matrix w = tr->GetWorldMatrix();
			Matrix inv = w.Invert();
			Vec3 lo = Vec3::Transform(ro, inv);
			Vec3 ld = Vec3::TransformNormal(rd, inv);
			Vec3 bmin, bmax;
			LocalBounds(go, bmin, bmax);
			if (RayAabb(lo, ld, bmin, bmax))
			{
				auto testRange = [&](size_t start, size_t count, uint32 base) {
					for (size_t n = start; n + 3 <= start + count && n + 3 <= mesh->Indices.size(); n += 3)
					{
						const auto& A = mesh->Vertices[(std::min)((size_t)mesh->Indices[n] + base, mesh->Vertices.size() - 1)].pos;
						const auto& B = mesh->Vertices[(std::min)((size_t)mesh->Indices[n + 1] + base, mesh->Vertices.size() - 1)].pos;
						const auto& C = mesh->Vertices[(std::min)((size_t)mesh->Indices[n + 2] + base, mesh->Vertices.size() - 1)].pos;
						float t;
						if (RayTriangle(lo, ld, Vec3(A), Vec3(B), Vec3(C), t) && t < bestT)
						{
							bestT = t;
							best = go;
						}
					}
				};
				if (mesh->Subsets.empty())
					testRange(0, mesh->Indices.size(), 0);
				else
					for (const auto& sub : mesh->Subsets)
						testRange((size_t)sub.FaceStart * 3, (size_t)sub.FaceCount * 3, sub.VertexStart);
			}
		}
		else if (tr != nullptr)
		{
			// 메시가 없는 오브젝트(카메라, 라이트 등): 화면에서 위치 근처를 클릭하면 선택
			ImVec2 p;
			if (Project(tr->GetPosition(), p) && Len2(ImVec2(p.x - f.mouse.x, p.y - f.mouse.y)) < 12.0f)
			{
				float t = (tr->GetPosition() - ro).Length();
				if (t < bestT)
				{
					bestT = t;
					best = go;
				}
			}
		}
		for (GameObject* child : go->GetChildren())
			PickRecursive(child, ro, rd, best, bestT);
	}

	void PickAtMouse()
	{
		Scene* scene = SceneManager::GetI()->GetCurrentScene();
		if (scene == nullptr)
			return;
		Vec3 ro, rd;
		MouseRay(f.mouse, ro, rd);
		GameObject* best = nullptr;
		float bestT = FLT_MAX;
		for (GameObject* go : scene->GetRootGameObjects())
			PickRecursive(go, ro, rd, best, bestT);
		if (best != nullptr)
			SelectionManager::SetSelectedGameObject(best);
		else
			SelectionManager::ClearSelection();
	}

	// ------------------------------------------------------------------ 카메라 조작 (Hand / 휠 / Alt 궤도 / F 포커스)
	void Navigate(EditorCamera* camera, bool viewHovered, GameObject* selected)
	{
		ImGuiIO& io = ImGui::GetIO();
		const bool rightHeld = ImGui::IsMouseDown(ImGuiMouseButton_Right);

		// 패닝: Hand 도구 좌클릭 드래그 또는 가운데 버튼 드래그
		static bool panning = false;
		if (viewHovered && !rightHeld && !io.KeyAlt &&
			((CurrentTool() == Tool::View && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle)))
			panning = true;
		if (panning)
		{
			if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Middle))
				panning = false;
			else
			{
				// 선택/피벗 거리 기준으로 화면 이동량이 마우스와 맞도록
				float dist = 10.0f;
				float perPx = dist * 2.0f * tanf(f.fovY * 0.5f) / (std::max)(1.0f, f.vmax.y - f.vmin.y);
				camera->Strafe(-io.MouseDelta.x * perPx);
				camera->Pedestal(io.MouseDelta.y * perPx);
			}
		}

		// Alt + 좌클릭 드래그: 궤도 회전
		if (viewHovered && io.KeyAlt && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			s_Orbiting = true;
		if (s_Orbiting)
		{
			if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
				s_Orbiting = false;
			else
			{
				Vec3 pivot = selected ? selected->GetTransform()->GetPosition() : f.camPos + f.camLook * 10.0f;
				float dist = (pivot - f.camPos).Length();
				camera->RotateY(XMConvertToRadians(io.MouseDelta.x * 0.3f));
				camera->Pitch(XMConvertToRadians(io.MouseDelta.y * 0.3f));
				XMFLOAT3 look = camera->GetLook();
				camera->SetPosition(pivot.x - look.x * dist, pivot.y - look.y * dist, pivot.z - look.z * dist);
			}
		}

		// 휠 줌
		if (viewHovered && io.MouseWheel != 0.0f && !rightHeld)
			camera->Walk(io.MouseWheel * 2.0f);

		// F: 선택 오브젝트로 포커스
		if (viewHovered && selected != nullptr && !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F, false))
		{
			Vec3 bmin, bmax;
			LocalBounds(selected, bmin, bmax);
			Matrix w = selected->GetTransform()->GetWorldMatrix();
			Vec3 center = Vec3::Transform((bmin + bmax) * 0.5f, w);
			Vec3 ext = Vec3::TransformNormal(bmax - bmin, w);
			float radius = (std::max)(0.5f, ext.Length() * 0.5f);
			float dist = radius / tanf(f.fovY * 0.5f) * 1.2f;
			XMFLOAT3 look = camera->GetLook();
			XMFLOAT3 pos(center.x - look.x * dist, center.y - look.y * dist, center.z - look.z * dist);
			camera->LookAt(pos, XMFLOAT3(center.x, center.y, center.z), XMFLOAT3(0, 1, 0));
		}

		// (개발/검증용) NOVA_DEV_SCENECAM="px,py,pz,tx,ty,tz": 시작 뒤 한 번 Scene 카메라를 그 위치에서 목표를 보게 (입력 없이 캡처할 때)
		{
			static bool s_DevCam = false;
			char devCam[128] = {};
			if (!s_DevCam && ImGui::GetFrameCount() > 60 && ::GetEnvironmentVariableA("NOVA_DEV_SCENECAM", devCam, sizeof(devCam)) > 0)
			{
				s_DevCam = true;
				float v[6] = {};
				if (sscanf_s(devCam, "%f,%f,%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) == 6)
					camera->LookAt(XMFLOAT3(v[0], v[1], v[2]), XMFLOAT3(v[3], v[4], v[5]), XMFLOAT3(0, 1, 0));
			}
		}

		camera->UpdateViewMatrix();
	}
}

namespace SceneGizmoTools
{
	bool IsDragging() { return d.active || s_Orbiting; }

	static bool s_Suppressed = false;
	void SetSuppressed(bool suppressed) { s_Suppressed = suppressed; }

	void Update(EditorCamera* camera, const ImVec2& viewMin, const ImVec2& viewMax, bool viewHovered)
	{
		if (camera == nullptr)
			return;

		f.viewProj = camera->View() * camera->Proj();
		f.vmin = viewMin;
		f.vmax = viewMax;
		f.camPos = camera->GetPosition();
		f.camRight = camera->GetRight();
		f.camUp = camera->GetUp();
		f.camLook = camera->GetLook();
		f.fovY = camera->GetFovY();
		f.mouse = ImGui::GetIO().MousePos;
		f.dl = ImGui::GetWindowDrawList();

		GameObject* selected = SelectionManager::GetSelectedObjectType() == SelectionType::GAMEOBJECT ? SelectionManager::GetSelectedGameObject() : nullptr;
		if (d.active && d.target != selected)
			d.active = false;

		Navigate(camera, viewHovered && !d.active, selected);

		s_BestHot = Handle();
		s_BestDist = kPickPx;

		f.dl->PushClipRect(viewMin, viewMax, true);

		const Tool tool = CurrentTool();
		if (s_Suppressed)
		{
			d.active = false;
			s_ClickCandidate = false;
		}
		if (!s_Suppressed && selected != nullptr && selected->GetTransform() != nullptr && tool != Tool::View)
		{
			Transform* tr = selected->GetTransform();

			// 핸들 위치: Pivot = 오브젝트 원점, Center = 메시 바운드 중심
			Vec3 P = d.active ? d.handlePos : tr->GetPosition();
			if (!d.active && Pivot() == PivotMode::Center)
			{
				Vec3 bmin, bmax;
				LocalBounds(selected, bmin, bmax);
				P = Vec3::Transform((bmin + bmax) * 0.5f, tr->GetWorldMatrix());
			}
			if (d.active && (d.h.kind == Kind::MoveAxis || d.h.kind == Kind::MovePlane || d.h.kind == Kind::MoveView))
				P = d.handlePos + (tr->GetPosition() - d.startPos);   // 이동 중에는 핸들이 따라온다

			const bool local = Space() == HandleSpace::Local;
			Vec3 ax[3], lax[3];
			if (d.active && d.h.kind == Kind::RotAxis)
			{
				// 회전 중에는 시작 시점의 축을 유지 (Local 이면 축이 계속 돌아가 버리므로)
				Matrix w = d.startWorld;
				if (local) { ax[0] = w.Right(); ax[1] = w.Up(); ax[2] = w.Backward(); for (auto& a : ax) a.Normalize(); }
				else Axes(tr, false, ax);
			}
			else
				Axes(tr, local, ax);
			Axes(tr, true, lax);
			const float s = HandleSize(P);

			switch (tool)
			{
			case Tool::Move:      DrawMove(P, ax, s, true, true); break;
			case Tool::Rotate:    DrawRotate(P, ax, s * 0.95f); break;
			case Tool::Scale:     DrawScale(P, lax, s, true); break;
			case Tool::Rect:      DrawRect(selected); break;
			case Tool::Transform:
				DrawRotate(P, ax, s * 0.8f);
				DrawMove(P, ax, s * 1.25f, true, false);
				DrawScale(P, lax, s, false);
				break;
			default: break;
			}

			// 드래그 시작
			ImGuiIO& io = ImGui::GetIO();
			if (!d.active && viewHovered && !io.KeyAlt && s_BestHot.kind != Kind::None &&
				ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Right))
			{
				const Vec3* axesForDrag = (s_BestHot.kind == Kind::ScaleAxis || s_BestHot.kind == Kind::ScaleUniform) ? lax : ax;
				BeginDrag(selected, s_BestHot, P, axesForDrag, s_BestGrab);
			}
		}

		f.dl->PopClipRect();

		// 드래그 진행 / 종료
		if (d.active)
		{
			if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
				ApplyDrag();
			else
				d.active = false;
		}
		s_PrevHot = d.active ? d.h : s_BestHot;

		// 빈 곳 클릭 → 오브젝트 선택 (드래그하지 않고 뗐을 때)
		ImGuiIO& io = ImGui::GetIO();
		if (!s_Suppressed && viewHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !d.active && !io.KeyAlt && tool != Tool::View && s_BestHot.kind == Kind::None)
		{
			s_ClickCandidate = true;
			s_ClickPos = f.mouse;
		}
		if (s_ClickCandidate && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
		{
			s_ClickCandidate = false;
			if (Len2(ImVec2(f.mouse.x - s_ClickPos.x, f.mouse.y - s_ClickPos.y)) < 4.0f)
				PickAtMouse();
		}
	}
}
