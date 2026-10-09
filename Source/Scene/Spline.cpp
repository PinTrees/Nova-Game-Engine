#include "pch.h"
#include "Spline.h"
#include "GameObject.h"
#include "Transform.h"
#include "MeshFilter.h"
#include "MeshRenderer.h"
#include "MeshCollider.h"
#include "Mesh.h"
#include "PrefabUtility.h"
#include "SceneManager.h"
#include "Scene.h"
#include "UnityGUI.h"
#include "SceneViewOverlay.h"
#include "SelectionManager.h"
#include "SplineEditor.h"
#include "EditorLog.h"
#include "UndoSystem.h"
#include <random>
#include <chrono>

namespace
{
	const char* kInterp[] = { "Smooth", "Linear" };
	const char* kMethods[] = { "Repeat", "Deform" };
	const char* kSpacing[] = { "Item Length", "Distance", "Count" };
	const char* kAxes[] = { "X", "Z", "-X", "-Z" };

	// centripetal Catmull-Rom (alpha 0.5): 점 간격이 고르지 않아도 고리 · 뾰족함이 생기지 않는다
	Vec3 CentripetalCR(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, float t)
	{
		auto knot = [](float ti, const Vec3& a, const Vec3& b) { return ti + (std::max)(1e-4f, sqrtf((b - a).Length())); };
		const float t0 = 0.0f, t1 = knot(t0, p0, p1), t2 = knot(t1, p1, p2), t3 = knot(t2, p2, p3);
		const float u = t1 + (t2 - t1) * t;
		const Vec3 a1 = p0 * ((t1 - u) / (t1 - t0)) + p1 * ((u - t0) / (t1 - t0));
		const Vec3 a2 = p1 * ((t2 - u) / (t2 - t1)) + p2 * ((u - t1) / (t2 - t1));
		const Vec3 a3 = p2 * ((t3 - u) / (t3 - t2)) + p3 * ((u - t2) / (t3 - t2));
		const Vec3 b1 = a1 * ((t2 - u) / (t2 - t0)) + a2 * ((u - t0) / (t2 - t0));
		const Vec3 b2 = a2 * ((t3 - u) / (t3 - t1)) + a3 * ((u - t1) / (t3 - t1));
		return b1 * ((t2 - u) / (t2 - t1)) + b2 * ((u - t1) / (t2 - t1));
	}

	double NowSec() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

	// 프리팹 공간의 축: 앞 (곡선 진행) · 위 · 옆. 음의 축은 프리팹을 Y 로 180 도 돌린 것과 같다
	struct AxisMap
	{
		int Fwd = 0;          // 0 = X, 2 = Z
		float Sign = 1.0f;    // -X / -Z
		float Along(const Vec3& p) const { return Sign * (Fwd == 0 ? p.x : p.z); }
		float Side(const Vec3& p) const { return Sign * (Fwd == 0 ? p.z : p.x); }
	};
	AxisMap MapOf(SplineInstantiate::Axis a)
	{
		AxisMap m;
		m.Fwd = (a == SplineInstantiate::Axis::X || a == SplineInstantiate::Axis::NegX) ? 0 : 2;
		m.Sign = (a == SplineInstantiate::Axis::NegX || a == SplineInstantiate::Axis::NegZ) ? -1.0f : 1.0f;
		return m;
	}

	// 곡선 위 한 점의 틀: F = 앞, U = 위, S = 옆 (프리팹의 옆 축이 갈 곳 — 앞이 X 면 S = F x U (Z = X x Y), 앞이 Z 면 S = U x F (X = Y x Z))
	struct Frame { Vec3 P, F, U, S; };
	Frame FrameAt(SplineContainer* c, float d, bool upright, int fwdAxis)
	{
		Frame f;
		c->Evaluate(d, f.P, f.F);
		const Vec3 up(0, 1, 0);
		Vec3 fwd = f.F;
		if (upright)
		{
			fwd.y = 0.0f;
			if (fwd.LengthSquared() < 1e-8f) fwd = Vec3(1, 0, 0);
			fwd.Normalize();
			f.U = up;
		}
		else
		{
			Vec3 u = up - f.F * f.F.Dot(up);
			if (u.LengthSquared() < 1e-8f) u = Vec3(0, 0, 1);
			u.Normalize();
			f.U = u;
		}
		f.S = fwdAxis == 0 ? fwd.Cross(f.U) : f.U.Cross(fwd);
		f.S.Normalize();
		if (upright) f.F = fwd;   // 회전 (노멀) 은 수평 앞 — 위치는 곡선 (경사) 그대로
		return f;
	}

	// 프리팹 축 → 틀 (행 = 프리팹 X · Y · Z 가 갈 방향, DirectX 행 벡터 규약)
	Matrix RotationOf(const Frame& f, const AxisMap& m, float extraYawDeg)
	{
		Vec3 ax, az;
		if (m.Fwd == 0) { ax = f.F * m.Sign; az = f.S * m.Sign; }
		else { az = f.F * m.Sign; ax = f.S * m.Sign; }
		Matrix r(ax.x, ax.y, ax.z, 0, f.U.x, f.U.y, f.U.z, 0, az.x, az.y, az.z, 0, 0, 0, 0, 1);
		if (extraYawDeg != 0.0f)
			r = Matrix::CreateRotationY(XMConvertToRadians(extraYawDeg)) * r;
		return r;
	}
}

// ====================================================================== Spline Container
SplineContainer::SplineContainer()
{
	m_InspectorTitleName = "Spline Container";
	ResetShape();
}

SplineContainer::~SplineContainer() {}

void SplineContainer::ResetShape()
{
	Knots = { Vec3(-6, 0, 0), Vec3(-2, 0, 3), Vec3(2, 0, -3), Vec3(6, 0, 0) };
	++Revision;
}

XMMATRIX SplineContainer::WorldMatrix() const
{
	if (m_pGameObject) return m_pGameObject->GetTransform()->GetWorldMatrix();
	return XMMatrixIdentity();
}

void SplineContainer::Resample()
{
	m_Samples.clear();
	const int n = (int)Knots.size();
	m_SampledRevision = Revision;
	if (n == 0)
		return;
	if (n == 1)
	{
		m_Samples.push_back({ Knots[0], 0.0f });
		return;
	}
	const int segments = Closed ? n : n - 1;
	auto at = [&](int i) { return Closed ? Knots[((i % n) + n) % n] : Knots[std::clamp(i, 0, n - 1)]; };
	float dist = 0.0f;
	Vec3 prev = Knots[0];
	m_Samples.push_back({ prev, 0.0f });
	for (int s = 0; s < segments; ++s)
	{
		const Vec3 p0 = at(s - 1), p1 = at(s), p2 = at(s + 1), p3 = at(s + 2);
		// 끝 점은 바깥으로 이어 (열린 곡선의 첫 · 끝 구간이 바르게)
		const Vec3 q0 = (!Closed && s == 0) ? p1 * 2.0f - p2 : p0;
		const Vec3 q3 = (!Closed && s == segments - 1) ? p2 * 2.0f - p1 : p3;
		const int steps = Mode == Interpolation::Linear ? 1 : (std::max)(8, (int)ceilf((p2 - p1).Length() / 0.25f));
		for (int k = 1; k <= steps; ++k)
		{
			const float t = (float)k / steps;
			const Vec3 p = Mode == Interpolation::Linear ? Vec3::Lerp(p1, p2, t) : CentripetalCR(q0, p1, p2, q3, t);
			dist += (p - prev).Length();
			m_Samples.push_back({ p, dist });
			prev = p;
		}
	}
}

float SplineContainer::Length()
{
	if (m_SampledRevision != Revision || m_Samples.empty())
		Resample();
	return m_Samples.empty() ? 0.0f : m_Samples.back().D;
}

void SplineContainer::Evaluate(float d, Vec3& position, Vec3& tangent)
{
	const float len = Length();
	position = Vec3::Zero;
	tangent = Vec3(1, 0, 0);
	if (m_Samples.size() < 2)
	{
		if (!m_Samples.empty()) position = m_Samples[0].P;
		return;
	}
	if (Closed && len > 0.0f)
		d = fmodf(fmodf(d, len) + len, len);
	d = std::clamp(d, 0.0f, len);
	auto it = std::lower_bound(m_Samples.begin(), m_Samples.end(), d, [](const Sample& s, float v) { return s.D < v; });
	size_t i = (size_t)(it - m_Samples.begin());
	if (i == 0) i = 1;
	if (i >= m_Samples.size()) i = m_Samples.size() - 1;
	const Sample& a = m_Samples[i - 1];
	const Sample& b = m_Samples[i];
	const float span = (std::max)(1e-6f, b.D - a.D);
	position = Vec3::Lerp(a.P, b.P, (d - a.D) / span);
	// 진행 방향: 둘레 표본 (Linear 의 꺾인 곳은 그 구간의 방향)
	Vec3 t = b.P - a.P;
	if (Mode == Interpolation::Smooth)
	{
		const size_t i0 = i >= 2 ? i - 2 : 0, i1 = (std::min)(m_Samples.size() - 1, i + 1);
		t = m_Samples[i1].P - m_Samples[i0].P;
	}
	if (t.LengthSquared() > 1e-12f)
	{
		t.Normalize();
		tangent = t;
	}
}

void SplineContainer::OnInspectorGUI()
{
	using namespace UnityGUI;
	int mode = (int)Mode;
	if (Dropdown("Interpolation", &mode, kInterp, (int)Interpolation::Count) && mode != (int)Mode)
	{
		Mode = (Interpolation)mode;
		++Revision;
	}
	if (Toggle("Closed", &Closed))
		++Revision;
	char len[64];
	snprintf(len, sizeof(len), "Length  %.1f m  (%d knots)", Length(), (int)Knots.size());
	Label(len);
	SplineEditor::InspectorKnots(*this);
}

void SplineContainer::OnDrawGizmos()
{
	if (!SceneViewOverlay::IsActive() || m_pGameObject == nullptr || SelectionManager::GetSelectedGameObject() != m_pGameObject)
		return;
	const float len = Length();
	const XMMATRIX world = WorldMatrix();
	const int steps = (std::max)(2, (int)(len / 0.5f));
	Vec3 prev, tan;
	Evaluate(0.0f, prev, tan);
	for (int i = 1; i <= steps; ++i)
	{
		Vec3 p;
		Evaluate(len * i / steps, p, tan);
		const Vec3 a = XMVector3TransformCoord(prev, world), b = XMVector3TransformCoord(p, world);
		SceneViewOverlay::DrawLine(XMFLOAT3(a.x, a.y, a.z), XMFLOAT3(b.x, b.y, b.z), IM_COL32(90, 200, 255, 230), 2.0f);
		prev = p;
	}
	SplineEditor::DrawKnots(*this);
}

GENERATE_COMPONENT_FUNC_TOJSON(SplineContainer)
{
	json j;
	SERIALIZE_TYPE(j, SplineContainer);
	j["enabled"] = m_Enabled;
	j["closed"] = Closed;
	j["interpolation"] = (int)Mode;
	json k = json::array();
	for (const Vec3& p : Knots)
		k.push_back({ p.x, p.y, p.z });
	j["knots"] = k;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(SplineContainer)
{
	m_Enabled = j.value("enabled", true);
	Closed = j.value("closed", false);
	Mode = (Interpolation)std::clamp(j.value("interpolation", 0), 0, (int)Interpolation::Count - 1);
	if (j.contains("knots") && j["knots"].is_array())
	{
		Knots.clear();
		for (const auto& p : j["knots"])
			if (p.is_array() && p.size() >= 3)
				Knots.push_back(Vec3(p[0].get<float>(), p[1].get<float>(), p[2].get<float>()));
	}
	++Revision;
}

// ====================================================================== Spline Instantiate
SplineInstantiate::SplineInstantiate()
{
	m_InspectorTitleName = "Spline Instantiate";
}

SplineInstantiate::~SplineInstantiate()
{
}

SplineContainer* SplineInstantiate::Container()
{
	return m_pGameObject ? m_pGameObject->GetComponent<SplineContainer>() : nullptr;
}

void SplineInstantiate::OnDestroy()
{
	Clear();
}

void SplineInstantiate::Clear()
{
	Scene* scene = SceneManager::GetI()->GetCurrentScene();
	for (GameObject* g : m_Generated)
		if (scene && GameObject::IsAlive(g))
			scene->DestroyGameObject(g);
	m_Generated.clear();
	m_Vertices = 0;
}

bool SplineInstantiate::LoadSources()
{
	// 프리팹마다 한 번: 임시 인스턴스를 만들어 메시 · 재질 · 범위를 읽고 지운다 (Deform 은 이 메시를 휜다)
	m_Sources.clear();
	Scene* scene = SceneManager::GetI()->GetCurrentScene();
	if (!scene)
		return false;
	for (const Item& item : Items)
	{
		SourceItem src;
		src.Prefab = item.Prefab;
		if (!item.Prefab.empty())
			if (GameObject* tmp = PrefabUtility::InstantiatePrefab(item.Prefab, scene, nullptr, false))
			{
				const Matrix rootInv = tmp->GetTransform()->GetWorldMatrix().Invert();
				Vec3 mn(FLT_MAX, FLT_MAX, FLT_MAX), mx(-FLT_MAX, -FLT_MAX, -FLT_MAX);
				std::function<void(GameObject*)> visit = [&](GameObject* g) {
					MeshFilter* mf = g->GetComponent<MeshFilter>();
					MeshRenderer* mr = g->GetComponent<MeshRenderer>();
					if (mf && mr && mf->GetMesh())
					{
						SourcePart part;
						part.Source = mf->GetMesh();
						part.Renderer = mr->toJson();
						part.ToItem = g->GetTransform()->GetWorldMatrix() * rootInv;
						for (const auto& v : part.Source->Vertices)
						{
							const Vec3 p = Vec3::Transform(Vec3(v.pos), part.ToItem);
							mn = Vec3::Min(mn, p);
							mx = Vec3::Max(mx, p);
						}
						src.Parts.push_back(std::move(part));
					}
					for (GameObject* c : g->Children())
						visit(c);
				};
				visit(tmp);
				if (!src.Parts.empty())
				{
					src.Min = mn;
					src.Max = mx;
					src.Valid = true;
				}
				scene->DestroyGameObject(tmp);
			}
		if (!src.Valid && !item.Prefab.empty())
			EditorLog::Write("Spline", "Spline Instantiate on '%s': cannot use '%s' (no prefab or no mesh)", m_pGameObject ? m_pGameObject->GetName().c_str() : "?", item.Prefab.c_str());
		m_Sources.push_back(std::move(src));
	}
	return true;
}

void SplineInstantiate::EnsureBuilt()
{
	SplineContainer* c = Container();
	if (!c || !m_pGameObject)
		return;
	const uint64_t key = ((uint64_t)(uint32_t)c->Revision << 32) ^ (uint64_t)(uint32_t)Revision ^ ((uint64_t)m_Enabled << 31);
	if (key == m_BuiltKey && !(m_Generated.empty() && !Items.empty() && m_BuiltKey == 0))
		return;
	// 점을 끄는 동안 프레임마다 다시 만들지 않게 (편집기 — 0.08 초에 한 번)
	const double now = NowSec();
	if (!Application::IsPlaying() && m_LastBuild >= 0.0 && now - m_LastBuild < 0.08)
		return;
	m_LastBuild = now;
	m_BuiltKey = key;
	Rebuild();
}

void SplineInstantiate::Rebuild()
{
	Clear();
	SplineContainer* c = Container();
	Scene* scene = SceneManager::GetI()->GetCurrentScene();
	if (!c || !scene || !m_pGameObject || !m_Enabled || Items.empty())
		return;
	// 프리팹이 바뀌었거나 처음이면 다시 읽는다
	bool reload = m_Sources.size() != Items.size();
	for (size_t i = 0; !reload && i < Items.size(); ++i)
		reload = m_Sources[i].Prefab != Items[i].Prefab;
	if (reload)
		LoadSources();
	std::vector<int> usable;
	float weightSum = 0.0f;
	for (size_t i = 0; i < m_Sources.size(); ++i)
		if (m_Sources[i].Valid && Items[i].Weight > 0.0f)
		{
			usable.push_back((int)i);
			weightSum += Items[i].Weight;
		}
	if (usable.empty())
		return;

	const float len = c->Length();
	m_Length = len;
	if (len < 1e-3f)
		return;
	const AxisMap am = MapOf(ForwardAxis);
	auto itemLen = [&](int s) { const SourceItem& it = m_Sources[s]; return (std::max)(0.01f, am.Fwd == 0 ? it.Max.x - it.Min.x : it.Max.z - it.Min.z); };
	auto itemMinAlong = [&](int s) { const SourceItem& it = m_Sources[s]; return am.Sign > 0 ? (am.Fwd == 0 ? it.Min.x : it.Min.z) : -(am.Fwd == 0 ? it.Max.x : it.Max.z); };

	std::mt19937 rng((uint32_t)Seed * 2654435761u + 7u);
	std::uniform_real_distribution<float> unit(0.0f, 1.0f);
	auto pick = [&]() {
		float r = unit(rng) * weightSum;
		for (int s : usable)
		{
			r -= Items[s].Weight;
			if (r <= 0.0f) return s;
		}
		return usable.back();
	};

	// 조각 놓을 자리: (시작 거리, 차지할 길이, 조각) — 간격 방식
	struct Slot { float Start, Span; int Source; };
	std::vector<Slot> slots;
	if (Spacing == SpacingMode::ItemLength)
	{
		// 조각을 차례로 이어 붙인다 (여러 조각이면 길이가 다를 수 있다)
		std::vector<int> seq;
		float total = 0.0f;
		for (int guard = 0; guard < 10000; ++guard)
		{
			const int s = pick();
			const float l = itemLen(s) + Gap;
			if (total + l > len + (FitToLength ? l * 0.5f : 0.001f))
				break;
			seq.push_back(s);
			total += l;
		}
		if (seq.empty())
			seq.push_back(usable.front());
		float scale = 1.0f, sum = 0.0f;
		for (int s : seq) sum += itemLen(s) + Gap;
		if (FitToLength && sum > 0.0f)
			scale = len / sum;
		float d = 0.0f;
		for (int s : seq)
		{
			const float span = (itemLen(s) + Gap) * scale;
			slots.push_back({ d, span, s });
			d += span;
		}
	}
	else
	{
		int n;
		float step;
		if (Spacing == SpacingMode::Count)
		{
			n = (std::max)(1, Count);
			step = len / (c->Closed ? n : (std::max)(1, n - (Placement == Method::Repeat ? 1 : 0)));
		}
		else
		{
			step = (std::max)(0.05f, Distance);
			n = (int)floorf(len / step + 1e-3f) + ((c->Closed || Placement == Method::Deform) ? 0 : 1);
			if (FitToLength && n > 1)
				step = len / (c->Closed || Placement == Method::Deform ? n : n - 1);
		}
		n = (std::min)(n, 5000);
		for (int i = 0; i < n; ++i)
			slots.push_back({ i * step, Placement == Method::Deform ? step : 0.0f, pick() });
	}

	int made = 0;
	const float minScale = (std::min)(RandomScale[0], RandomScale[1]), maxScale = (std::max)(RandomScale[0], RandomScale[1]);
	for (const Slot& slot : slots)
	{
		const SourceItem& src = m_Sources[slot.Source];
		if (Placement == Method::Repeat)
		{
			// 프리팹 인스턴스 그대로 (휘지 않음): 조각의 가운데를 자리 가운데에 (Distance · Count 는 자리 = 점)
			const float l = itemLen(slot.Source);
			const float center = Spacing == SpacingMode::ItemLength ? slot.Start + slot.Span * 0.5f : slot.Start;
			Frame f = FrameAt(c, center, KeepUpright, am.Fwd);
			GameObject* go = PrefabUtility::InstantiatePrefab(src.Prefab, scene, m_pGameObject, false);
			if (!go)
				continue;
			const float yaw = RotationY + (RandomYaw > 0.0f ? (unit(rng) * 2.0f - 1.0f) * RandomYaw : 0.0f);
			const Matrix rot = RotationOf(f, am, yaw);
			const float sc = minScale + (maxScale - minScale) * unit(rng);
			// 프리팹의 앞 축 가운데가 자리 가운데에 오게 (프리팹 원점이 가운데가 아닐 수 있다)
			const float midAlong = itemMinAlong(slot.Source) + l * 0.5f;
			Vec3 local(0, 0, 0);
			if (am.Fwd == 0) local.x = midAlong * am.Sign; else local.z = midAlong * am.Sign;
			const Vec3 shift = Vec3::TransformNormal(local, rot) * sc;
			const Vec3 pos = f.P + f.S * Offset.x + f.U * Offset.y + f.F * Offset.z - shift;
			go->GetTransform()->SetLocalPosition(pos);
			go->GetTransform()->SetLocalRotation(Quaternion::CreateFromRotationMatrix(rot));
			if (sc != 1.0f) go->GetTransform()->SetLocalScale(go->GetTransform()->GetLocalScale() * sc);
			go->SetHideAndDontSave(true);
			go->SetName(go->GetName() + " (Spline " + std::to_string(made) + ")");
			m_Generated.push_back(go);
			++made;
			continue;
		}
		// Deform: 조각 메시를 곡선을 따라 휜다. 진행 방향 [min, max] → [Start, Start + Span] (늘이기 · 줄이기)
		const float l = itemLen(slot.Source);
		const float a0 = itemMinAlong(slot.Source);
		const float k = slot.Span / l;
		const float step = (std::max)(0.05f, DeformResolution);
		for (const SourcePart& part : src.Parts)
		{
			const Mesh& m = *part.Source;
			auto out = std::make_shared<Mesh>(m);   // 재질 · 서브셋 표를 이어받는다
			out->Vertices.clear();
			out->Indices.clear();
			out->Subsets.clear();
			auto mapVertex = [&](const Vertex::PosNormalTexTan2& v) {
				Vertex::PosNormalTexTan2 o = v;
				const Vec3 p = Vec3::Transform(Vec3(v.pos), part.ToItem);
				const float along = am.Along(p) - a0;
				const float side = am.Side(p);
				Frame f = FrameAt(c, slot.Start + along * k, KeepUpright, am.Fwd);
				const Vec3 w = f.P + f.S * (side + Offset.x) + f.U * (p.y + Offset.y);
				o.pos = XMFLOAT3(w.x, w.y, w.z);
				const Matrix rot = RotationOf(f, am, 0.0f);
				Vec3 n = Vec3::TransformNormal(Vec3::TransformNormal(Vec3(v.normal), part.ToItem), rot);
				n.Normalize();
				o.normal = XMFLOAT3(n.x, n.y, n.z);
				Vec3 t = Vec3::TransformNormal(Vec3::TransformNormal(Vec3(v.tangentU.x, v.tangentU.y, v.tangentU.z), part.ToItem), rot);
				t.Normalize();
				o.tangentU = XMFLOAT4(t.x, t.y, t.z, v.tangentU.w);
				return o;
			};
			auto lerpV = [](const Vertex::PosNormalTexTan2& a, const Vertex::PosNormalTexTan2& b, const Vertex::PosNormalTexTan2& c3, float wa, float wb, float wc) {
				Vertex::PosNormalTexTan2 o;
				auto L3 = [&](const XMFLOAT3& x, const XMFLOAT3& y, const XMFLOAT3& z) { return XMFLOAT3(x.x * wa + y.x * wb + z.x * wc, x.y * wa + y.y * wb + z.y * wc, x.z * wa + y.z * wb + z.z * wc); };
				o.pos = L3(a.pos, b.pos, c3.pos);
				o.normal = L3(a.normal, b.normal, c3.normal);
				o.tex = XMFLOAT2(a.tex.x * wa + b.tex.x * wb + c3.tex.x * wc, a.tex.y * wa + b.tex.y * wb + c3.tex.y * wc);
				o.tangentU = XMFLOAT4(a.tangentU.x * wa + b.tangentU.x * wb + c3.tangentU.x * wc, a.tangentU.y * wa + b.tangentU.y * wb + c3.tangentU.y * wc,
					a.tangentU.z * wa + b.tangentU.z * wb + c3.tangentU.z * wc, a.tangentU.w);
				return o;
			};
			bool overflow = false;
			for (const auto& sub : m.Subsets)
			{
				MeshGeometry::Subset ns = sub;
				ns.VertexStart = (uint32)out->Vertices.size();
				ns.FaceStart = (uint32)(out->Indices.size() / 3);
				for (uint32 f3 = 0; f3 < sub.FaceCount && !overflow; ++f3)
				{
					const size_t base = (size_t)(sub.FaceStart + f3) * 3;
					if (base + 2 >= m.Indices.size())
						break;
					const auto& va = m.Vertices[sub.VertexStart + m.Indices[base]];
					const auto& vb = m.Vertices[sub.VertexStart + m.Indices[base + 1]];
					const auto& vc = m.Vertices[sub.VertexStart + m.Indices[base + 2]];
					// 진행 방향 길이만큼 잘게 (같은 면을 이루는 이웃 세모는 길이가 같아 같은 수로 나뉜다)
					const float ea = am.Along(Vec3::Transform(Vec3(va.pos), part.ToItem));
					const float eb = am.Along(Vec3::Transform(Vec3(vb.pos), part.ToItem));
					const float ec = am.Along(Vec3::Transform(Vec3(vc.pos), part.ToItem));
					const float extent = ((std::max)({ ea, eb, ec }) - (std::min)({ ea, eb, ec })) * k;
					const int div = std::clamp((int)ceilf(extent / step), 1, 32);
					const uint32 start = (uint32)out->Vertices.size() - ns.VertexStart;
					// 무게중심 격자 (i, j): i + j <= div
					auto idx = [&](int i, int j) { return start + (uint32)(i * (div + 1) - i * (i - 1) / 2 + j); };
					for (int i = 0; i <= div; ++i)
						for (int j = 0; j <= div - i; ++j)
						{
							const float wb = (float)i / div, wc = (float)j / div;
							out->Vertices.push_back(mapVertex(lerpV(va, vb, vc, 1.0f - wb - wc, wb, wc)));
						}
					for (int i = 0; i < div; ++i)
						for (int j = 0; j < div - i; ++j)
						{
							out->Indices.push_back((USHORT)idx(i, j)); out->Indices.push_back((USHORT)idx(i + 1, j)); out->Indices.push_back((USHORT)idx(i, j + 1));
							if (j + 1 <= div - i - 1)
							{
								out->Indices.push_back((USHORT)idx(i + 1, j)); out->Indices.push_back((USHORT)idx(i + 1, j + 1)); out->Indices.push_back((USHORT)idx(i, j + 1));
							}
						}
					if (out->Vertices.size() - ns.VertexStart > 65000)
						overflow = true;
				}
				ns.VertexCount = (uint32)out->Vertices.size() - ns.VertexStart;
				ns.FaceCount = (uint32)(out->Indices.size() / 3) - ns.FaceStart;
				out->Subsets.push_back(ns);
			}
			if (overflow)
			{
				EditorLog::Write("Spline", "Spline Instantiate on '%s': a deformed piece has too many vertices — raise Deform Resolution", m_pGameObject->GetName().c_str());
				continue;
			}
			if (out->Vertices.empty())
				continue;
			out->Name = m.Name + " (deformed)";
			out->Setup();
			GameObject* go = new GameObject(std::filesystem::path(src.Prefab).stem().string() + " (Spline " + std::to_string(made) + ")");
			go->SetHideAndDontSave(true);
			MeshFilter* mf = go->AddComponent<MeshFilter>();
			mf->SetMesh(out, L"", 0);
			MeshRenderer* mr = go->AddComponent<MeshRenderer>();
			json rj = part.Renderer;
			rj["meshPath"] = "";
			mr->fromJson(rj);
			if (GenerateColliders)
				go->AddComponent<MeshCollider>();   // 메시는 같은 오브젝트의 Mesh Filter (휜 메시)
			scene->AddRootGameObject(go);
			go->SetParent(m_pGameObject, false);
			m_Generated.push_back(go);
			m_Vertices += (int)out->Vertices.size();
		}
		++made;
	}
}

int SplineInstantiate::Bake()
{
	if (Placement != Method::Repeat)
		return -1;
	EnsureBuilt();
	const int n = (int)m_Generated.size();
	for (GameObject* g : m_Generated)
		if (GameObject::IsAlive(g))
			g->SetHideAndDontSave(false);
	m_Generated.clear();
	m_Enabled = false;   // 구운 뒤에는 다시 만들지 않는다 (Unity 도 Bake Instances 뒤 컴포넌트를 끈다)
	++Revision;
	m_BuiltKey = 0;
	return n;
}

nlohmann::json SplineInstantiate::Info()
{
	EnsureBuilt();
	int valid = 0;
	for (const SourceItem& s : m_Sources) valid += s.Valid ? 1 : 0;
	return { { "length", m_Length }, { "instances", (int)m_Generated.size() }, { "deformedVertices", m_Vertices }, { "items", (int)Items.size() },
		{ "validItems", valid }, { "method", kMethods[(int)Placement] }, { "spacing", kSpacing[(int)Spacing] } };
}

void SplineInstantiate::OnInspectorGUI()
{
	using namespace UnityGUI;
	bool changed = false;
	if (!Container())
		HelpBox("Needs a Spline Container on the same GameObject.", true);
	Label("Items To Instantiate", 0, true);
	for (size_t i = 0; i < Items.size(); ++i)
	{
		ImGui::PushID((int)i);
		const std::string name = Items[i].Prefab.empty() ? "None (Prefab)" : std::filesystem::path(Items[i].Prefab).stem().string();
		ObjectField("Prefab", name.c_str(), 1, "prefab");
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET_FILE"))
			{
				std::string path((const char*)p->Data, p->DataSize);
				while (!path.empty() && path.back() == '\0') path.pop_back();
				if (std::filesystem::path(path).extension() == ".prefab")
				{
					Items[i].Prefab = wstring_to_string(PathManager::GetI()->GetCutSolutionPath(string_to_wstring(path)));
					changed = true;
				}
			}
			ImGui::EndDragDropTarget();
		}
		changed |= Slider("Weight", &Items[i].Weight, 0.0f, 10.0f, 1);
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 18.0f);
		if (ImGui::SmallButton("Remove"))
		{
			Items.erase(Items.begin() + i);
			changed = true;
			ImGui::PopID();
			break;
		}
		ImGui::PopID();
	}
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 18.0f);
	if (ImGui::SmallButton("+ Add Item"))
	{
		Items.push_back(Item());
		changed = true;
	}
	int method = (int)Placement;
	if (Dropdown("Method", &method, kMethods, (int)Method::Count)) { Placement = (Method)method; changed = true; }
	int spacing = (int)Spacing;
	if (Dropdown("Spacing", &spacing, kSpacing, (int)SpacingMode::ModeCount)) { Spacing = (SpacingMode)spacing; changed = true; }
	if (Spacing == SpacingMode::ItemLength) changed |= Float("Gap", &Gap, 1);
	if (Spacing == SpacingMode::Distance) changed |= Float("Distance", &Distance, 1);
	if (Spacing == SpacingMode::Count) changed |= Int("Count", &Count, 1);
	changed |= Toggle("Fit To Length", &FitToLength);
	int axis = (int)ForwardAxis;
	if (Dropdown("Forward Axis", &axis, kAxes, (int)Axis::Count)) { ForwardAxis = (Axis)axis; changed = true; }
	changed |= Toggle("Keep Upright", &KeepUpright);
	if (Placement == Method::Deform)
	{
		changed |= Slider("Deform Resolution", &DeformResolution, 0.1f, 4.0f);
		changed |= Toggle("Generate Colliders", &GenerateColliders);
	}
	changed |= UnityGUI::Vector3("Offset", &Offset.x);
	changed |= Float("Rotation Y", &RotationY);
	if (Placement == Method::Repeat)
	{
		changed |= Slider("Random Yaw", &RandomYaw, 0.0f, 180.0f);
		changed |= Vector2Pair("Random Scale", "Min", &RandomScale[0], "Max", &RandomScale[1]);
	}
	changed |= Int("Seed", &Seed);
	if (changed)
		++Revision;
	char info[96];
	snprintf(info, sizeof(info), "%d pieces along %.1f m%s", (int)m_Generated.size(), m_Length, Placement == Method::Deform ? " (deformed)" : "");
	Label(info, 1);
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 18.0f);
	if (ImGui::Button("Regenerate", ImVec2(110, 0)))
	{
		m_Sources.clear();   // 프리팹을 고쳤을 수 있다
		m_BuiltKey = 0;
	}
	if (Placement == Method::Repeat)
	{
		ImGui::SameLine();
		if (ImGui::Button("Bake Instances", ImVec2(130, 0)))
		{
			Bake();
			Undo::SetActionName("Bake Spline Instances");
			Undo::RequestCheck();
		}
	}
}

GENERATE_COMPONENT_FUNC_TOJSON(SplineInstantiate)
{
	json j;
	SERIALIZE_TYPE(j, SplineInstantiate);
	j["enabled"] = m_Enabled;
	json items = json::array();
	for (const Item& it : Items)
		items.push_back({ { "prefab", it.Prefab }, { "weight", it.Weight } });
	j["items"] = items;
	j["method"] = (int)Placement;
	j["spacing"] = (int)Spacing;
	j["gap"] = Gap;
	j["distance"] = Distance;
	j["count"] = Count;
	j["fitToLength"] = FitToLength;
	j["forwardAxis"] = (int)ForwardAxis;
	j["keepUpright"] = KeepUpright;
	j["deformResolution"] = DeformResolution;
	j["generateColliders"] = GenerateColliders;
	j["offset"] = { Offset.x, Offset.y, Offset.z };
	j["rotationY"] = RotationY;
	j["randomYaw"] = RandomYaw;
	j["randomScale"] = { RandomScale[0], RandomScale[1] };
	j["seed"] = Seed;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(SplineInstantiate)
{
	m_Enabled = j.value("enabled", true);
	if (j.contains("items") && j["items"].is_array())
	{
		Items.clear();
		for (const auto& it : j["items"])
		{
			Item item;
			if (it.is_string()) item.Prefab = it.get<std::string>();
			else if (it.is_object()) { item.Prefab = it.value("prefab", std::string()); item.Weight = it.value("weight", 1.0f); }
			Items.push_back(item);
		}
	}
	Placement = (Method)std::clamp(j.value("method", (int)Placement), 0, (int)Method::Count - 1);
	Spacing = (SpacingMode)std::clamp(j.value("spacing", (int)Spacing), 0, (int)SpacingMode::ModeCount - 1);
	Gap = j.value("gap", Gap);
	Distance = j.value("distance", Distance);
	Count = j.value("count", Count);
	FitToLength = j.value("fitToLength", FitToLength);
	ForwardAxis = (Axis)std::clamp(j.value("forwardAxis", (int)ForwardAxis), 0, (int)Axis::Count - 1);
	KeepUpright = j.value("keepUpright", KeepUpright);
	DeformResolution = j.value("deformResolution", DeformResolution);
	GenerateColliders = j.value("generateColliders", GenerateColliders);
	if (j.contains("offset") && j["offset"].is_array() && j["offset"].size() >= 3)
		Offset = Vec3(j["offset"][0].get<float>(), j["offset"][1].get<float>(), j["offset"][2].get<float>());
	RotationY = j.value("rotationY", RotationY);
	RandomYaw = j.value("randomYaw", RandomYaw);
	if (j.contains("randomScale") && j["randomScale"].is_array() && j["randomScale"].size() >= 2)
	{
		RandomScale[0] = j["randomScale"][0].get<float>();
		RandomScale[1] = j["randomScale"][1].get<float>();
	}
	Seed = j.value("seed", Seed);
	++Revision;
}
