#include "pch.h"
#include "SpriteRenderer.h"
#include "LineRenderer.h"
#include "ParticleSystemEditor.h"
#include "RenderLayers.h"
#include "UnityGUI.h"
#include "ScriptBindings.h"
#include <chrono>

namespace
{
	std::vector<LineRendererBase*>& Registry()
	{
		static std::vector<LineRendererBase*> r;
		return r;
	}

	json Vec3Json(const Vec3& v) { return json::array({ v.x, v.y, v.z }); }
	bool ReadVec3(const json& j, Vec3& v)
	{
		if (!j.is_array() || j.size() != 3)
			return false;
		v = Vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
		return true;
	}

	const char* const kAlignment[] = { "View", "Transform Z" };
	const char* const kTextureMode[] = { "Stretch", "Tile", "Distribute Per Segment", "Repeat Per Segment" };
	const char* const kBlend[] = { "Alpha Blended", "Additive" };
}

// ------------------------------------------------------------------ 공통
LineRendererBase::LineRendererBase()
{
	ColorGradient.Mode = ParticleGradientMode::Gradient;   // Unity: Color = 그라디언트 (흰색)
	Registry().push_back(this);
}

LineRendererBase::~LineRendererBase()
{
	auto& r = Registry();
	r.erase(std::remove(r.begin(), r.end(), this), r.end());
}

const std::vector<LineRendererBase*>& LineRendererBase::All() { return Registry(); }

bool LineRendererBase::IsVisible() const
{
	if (m_pGameObject == nullptr || !m_Enabled)
		return false;
	for (GameObject* g = m_pGameObject; g != nullptr; g = g->GetParent())
		if (!g->IsActive())
			return false;
	return RenderLayers::Visible(m_pGameObject);
}

float LineRendererBase::WidthAt(float t) const
{
	return (std::max)(0.0f, WidthCurve.Evaluate(std::clamp(t, 0.0f, 1.0f), 0.5f) * WidthMultiplier);
}

Vec4 LineRendererBase::ColorAt(float t) const
{
	return ColorGradient.Evaluate(std::clamp(t, 0.0f, 1.0f), 0.5f);
}

// Unity 의 startWidth · endWidth: 곡선의 처음 · 끝 키 (Width Multiplier 로 나눈 값)
void LineRendererBase::SetEndWidth(bool end, float width)
{
	if (WidthMultiplier <= 0.0f)
		WidthMultiplier = 1.0f;
	const float v = (std::max)(0.0f, width) / WidthMultiplier;
	if (WidthCurve.Mode == ParticleCurveMode::Constant)
	{
		const float c = WidthCurve.ConstantMax;
		WidthCurve = MinMaxCurve::Curve(ParticleCurve::Constant(c));
	}
	ParticleCurve& c = WidthCurve.CurveMax;
	if (c.Keys.empty())
		c = ParticleCurve::Constant(1.0f);
	WidthCurve.Multiplier = 1.0f;
	(end ? c.Keys.back() : c.Keys.front()).Value = v;
}

void LineRendererBase::SetEndColor(bool end, const Vec4& color)
{
	ColorGradient.Mode = ParticleGradientMode::Gradient;
	ParticleGradient& g = ColorGradient.GradientMax;
	if (g.Colors.empty()) g.Colors = { { 0.0f, 1, 1, 1 }, { 1.0f, 1, 1, 1 } };
	if (g.Alphas.empty()) g.Alphas = { { 0.0f, 1.0f }, { 1.0f, 1.0f } };
	auto& ck = end ? g.Colors.back() : g.Colors.front();
	ck.R = color.x; ck.G = color.y; ck.B = color.z;
	(end ? g.Alphas.back() : g.Alphas.front()).A = color.w;
}

void LineRendererBase::StyleToJson(json& j) const
{
	j["enabled"] = m_Enabled;
	if (SortingLayerId != 0) j["sortingLayerID"] = SortingLayerId;
	if (SortingOrder != 0) j["sortingOrder"] = SortingOrder;
	j["widthCurve"] = WidthCurve;
	j["widthMultiplier"] = WidthMultiplier;
	j["colorGradient"] = ColorGradient;
	j["numCornerVertices"] = CornerVertices;
	j["numCapVertices"] = CapVertices;
	j["alignment"] = AlignmentMode;
	j["textureMode"] = TextureMode;
	j["textureScale"] = { TextureScale[0], TextureScale[1] };
	j["texture"] = Texture;
	j["blend"] = Blend;
}

void LineRendererBase::StyleFromJson(const json& j)
{
	m_Enabled = j.value("enabled", true);
	SortingLayerId = j.value("sortingLayerID", 0);
	SortingOrder = j.value("sortingOrder", 0);
	if (j.contains("widthCurve")) WidthCurve = j["widthCurve"].get<MinMaxCurve>();
	WidthMultiplier = j.value("widthMultiplier", 1.0f);
	if (j.contains("colorGradient")) ColorGradient = j["colorGradient"].get<MinMaxGradient>();
	CornerVertices = std::clamp(j.value("numCornerVertices", 0), 0, 90);
	CapVertices = std::clamp(j.value("numCapVertices", 0), 0, 90);
	AlignmentMode = std::clamp(j.value("alignment", 0), 0, 1);
	TextureMode = std::clamp(j.value("textureMode", 0), 0, 3);
	if (j.contains("textureScale") && j["textureScale"].is_array() && j["textureScale"].size() == 2)
	{
		TextureScale[0] = j["textureScale"][0].get<float>();
		TextureScale[1] = j["textureScale"][1].get<float>();
	}
	Texture = j.value("texture", std::string());
	Blend = std::clamp(j.value("blend", 0), 0, 1);
}

void LineRendererBase::DrawStyleInspector()
{
	ParticleSystemEditor::CurveField("Width", &WidthCurve);
	UnityGUI::Float("Width Multiplier", &WidthMultiplier);
	WidthMultiplier = (std::max)(0.0f, WidthMultiplier);
	ParticleSystemEditor::GradientField("Color", &ColorGradient, 0, false);
	UnityGUI::Int("Corner Vertices", &CornerVertices);
	UnityGUI::Int("End Cap Vertices", &CapVertices);
	CornerVertices = std::clamp(CornerVertices, 0, 90);
	CapVertices = std::clamp(CapVertices, 0, 90);
	UnityGUI::Dropdown("Alignment", &AlignmentMode, kAlignment, 2);
	UnityGUI::Dropdown("Texture Mode", &TextureMode, kTextureMode, 4);
	UnityGUI::Vector2Pair("Texture Scale", "X", &TextureScale[0], "Y", &TextureScale[1]);
	UnityGUI::Label("Materials", 0, true);
	ParticleSystemEditor::TexturePicker("Texture", &Texture, "line_texture");
	UnityGUI::Dropdown("Blend Mode", &Blend, kBlend, 2);
	if (UnityGUI::FoldoutPlain("Additional Settings"))
		SpriteRenderer::SortingFields(SortingLayerId, SortingOrder);
}

// ------------------------------------------------------------------ Line Renderer
void LineRenderer::WorldPoints(std::vector<Vec3>& out) const
{
	out.clear();
	if (m_pGameObject == nullptr)
		return;
	if (UseWorldSpace)
	{
		out = Positions;
		return;
	}
	const Matrix w = m_pGameObject->GetTransform()->GetWorldMatrix();
	out.reserve(Positions.size());
	for (const Vec3& p : Positions)
		out.push_back(Vec3::Transform(p, w));
}

void LineRenderer::OnInspectorGUI()
{
	UnityGUI::Toggle("Loop", &Loop);
	int count = (int)Positions.size();
	if (UnityGUI::FoldoutPlain("Positions"))
	{
		if (UnityGUI::Int("Size", &count, 1))
			Positions.resize((size_t)std::clamp(count, 0, 10000), Vec3(0, 0, 0));
		for (size_t i = 0; i < Positions.size() && i < 64; ++i)
		{
			float xyz[3] = { Positions[i].x, Positions[i].y, Positions[i].z };
			const std::string label = "Index " + std::to_string(i);
			if (UnityGUI::Vector3(label.c_str(), xyz, false, 1))
				Positions[i] = Vec3(xyz[0], xyz[1], xyz[2]);
		}
		if (Positions.size() > 64)
			UnityGUI::Label(("… " + std::to_string(Positions.size() - 64) + " more").c_str(), 1);
	}
	DrawStyleInspector();
	UnityGUI::Toggle("Use World Space", &UseWorldSpace);
}

GENERATE_COMPONENT_FUNC_TOJSON(LineRenderer)
{
	json j;
	SERIALIZE_TYPE(j, LineRenderer);
	StyleToJson(j);
	json pts = json::array();
	for (const Vec3& p : Positions)
		pts.push_back(Vec3Json(p));
	j["positions"] = pts;
	j["useWorldSpace"] = UseWorldSpace;
	j["loop"] = Loop;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(LineRenderer)
{
	StyleFromJson(j);
	if (j.contains("positions") && j["positions"].is_array())
	{
		Positions.clear();
		for (const json& p : j["positions"])
		{
			Vec3 v;
			if (ReadVec3(p, v))
				Positions.push_back(v);
		}
	}
	UseWorldSpace = j.value("useWorldSpace", true);
	Loop = j.value("loop", false);
}

// ------------------------------------------------------------------ Trail Renderer
namespace
{
	double EditorSeconds()
	{
		static const auto s_Start = std::chrono::steady_clock::now();
		return std::chrono::duration<double>(std::chrono::steady_clock::now() - s_Start).count();
	}
	double s_GameClock = 0.0;   // Play 중 게임 시간 (일시 정지 · 시간 배율을 따른다)
	bool s_WasPlaying = false;

	double Now() { return Application::IsPlaying() ? s_GameClock : EditorSeconds(); }
}

void TrailRenderer::Start()
{
	m_Points.clear();   // Play 시작: 에디터에서 남은 꼬리를 지운다
	m_HadPoints = false;
}

void TrailRenderer::AddPosition(const Vec3& p)
{
	m_Points.push_back({ p, Now() });
	m_HadPoints = true;
}

void TrailRenderer::SetPosition(int index, const Vec3& p)
{
	if (index >= 0 && index < (int)m_Points.size())
		m_Points[(size_t)index].Position = p;
}

void TrailRenderer::Tick(double now)
{
	while (!m_Points.empty() && now - m_Points.front().Time > (double)Time)
		m_Points.pop_front();
	if (m_pGameObject == nullptr)
		return;
	if (Emitting && IsVisible())
	{
		const Vec3 head = m_pGameObject->GetTransform()->GetPosition();
		if (m_Points.empty() || (head - m_Points.back().Position).Length() >= (std::max)(MinVertexDistance, 1e-5f))
			AddPosition(head);
	}
	// Autodestruct: Play 중 점이 모두 사라지면 (Time 동안 가만히 있었다) GameObject 를 지운다
	if (Autodestruct && Application::IsPlaying() && m_HadPoints && m_Points.empty())
	{
		m_HadPoints = false;
		GameObject::Destroy(m_pGameObject);
	}
}

void TrailRenderer::UpdateAll()
{
	const bool playing = Application::IsPlaying();
	if (playing != s_WasPlaying)
	{
		s_WasPlaying = playing;
		s_GameClock = 0.0;
		for (LineRendererBase* l : LineRendererBase::All())
			if (auto* t = dynamic_cast<TrailRenderer*>(l))
				t->Clear();   // Play 시작 · 끝: 시간 기준이 바뀐다
	}
	if (playing)
	{
		if (!Application::ShouldUpdateGame())
			return;   // 일시 정지: 꼬리도 멈춘다
		s_GameClock += (std::min)((double)DT, 0.25);
	}
	const double now = Now();
	const std::vector<LineRendererBase*> all = LineRendererBase::All();   // Autodestruct 가 목록을 바꿀 수 있다
	for (LineRendererBase* l : all)
		if (std::find(LineRendererBase::All().begin(), LineRendererBase::All().end(), l) != LineRendererBase::All().end())
			if (auto* t = dynamic_cast<TrailRenderer*>(l))
				t->Tick(now);
}

void TrailRenderer::WorldPoints(std::vector<Vec3>& out) const
{
	out.clear();
	if (m_pGameObject == nullptr)
		return;
	// 물체 (지금 위치) 부터 오래된 점으로
	if (Emitting)
		out.push_back(m_pGameObject->GetTransform()->GetPosition());
	for (auto it = m_Points.rbegin(); it != m_Points.rend(); ++it)
		out.push_back(it->Position);
}

void TrailRenderer::OnInspectorGUI()
{
	UnityGUI::Float("Time", &Time);
	Time = (std::max)(0.0f, Time);
	UnityGUI::Float("Min Vertex Distance", &MinVertexDistance);
	MinVertexDistance = (std::max)(0.0f, MinVertexDistance);
	UnityGUI::Toggle("AutoDestruct", &Autodestruct);
	UnityGUI::Toggle("Emitting", &Emitting);
	DrawStyleInspector();
}

GENERATE_COMPONENT_FUNC_TOJSON(TrailRenderer)
{
	json j;
	SERIALIZE_TYPE(j, TrailRenderer);
	StyleToJson(j);
	j["time"] = Time;
	j["minVertexDistance"] = MinVertexDistance;
	j["autodestruct"] = Autodestruct;
	j["emitting"] = Emitting;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(TrailRenderer)
{
	StyleFromJson(j);
	Time = (std::max)(0.0f, j.value("time", 5.0f));
	MinVertexDistance = (std::max)(0.0f, j.value("minVertexDistance", 0.1f));
	Autodestruct = j.value("autodestruct", false);
	Emitting = j.value("emitting", true);
}

// ------------------------------------------------------------------ 띠 만들기
namespace LineGeometry
{
	void Build(const LineRendererBase& line, const Vec3& cam, std::vector<Vertex>& out)
	{
		std::vector<Vec3> raw;
		line.WorldPoints(raw);
		// 겹친 점은 하나로 (방향을 정할 수 없다)
		std::vector<Vec3> p;
		p.reserve(raw.size() + 1);
		for (const Vec3& v : raw)
			if (p.empty() || (v - p.back()).LengthSquared() > 1e-10f)
				p.push_back(v);
		const bool loop = line.IsLoop() && p.size() >= 3;
		if (loop)
			p.push_back(p.front());
		const int n = (int)p.size();
		if (n < 2)
			return;

		// 길이 비율 t (너비 · 색), u (텍스처)
		std::vector<float> len(n, 0.0f);
		for (int i = 1; i < n; ++i)
			len[i] = len[i - 1] + (p[i] - p[i - 1]).Length();
		const float total = len[n - 1];
		std::vector<float> t(n), w(n), u(n);
		std::vector<XMFLOAT4> col(n);
		for (int i = 0; i < n; ++i)
		{
			t[i] = total > 1e-8f ? len[i] / total : (float)i / (n - 1);
			w[i] = line.WidthAt(t[i]) * 0.5f;
			const Vec4 c = line.ColorAt(t[i]);
			col[i] = XMFLOAT4(c.x, c.y, c.z, c.w);
			switch (line.TextureMode)
			{
			case LineRendererBase::Tile: u[i] = len[i]; break;
			case LineRendererBase::DistributePerSegment: u[i] = (float)i / (n - 1); break;
			case LineRendererBase::RepeatPerSegment: u[i] = (float)i; break;
			default: u[i] = t[i]; break;
			}
			u[i] *= line.TextureScale[0];
		}
		const float vScale = line.TextureScale[1];

		// 띠의 면 방향: View = 카메라 쪽, Transform Z = 물체의 Z 축
		Vec3 zAxis(0, 0, 1);
		if (line.AlignmentMode == LineRendererBase::AlignTransformZ && line.Owner())
		{
			const Matrix m = line.Owner()->GetTransform()->GetWorldMatrix();
			zAxis = Vec3(m._31, m._32, m._33);
			if (zAxis.LengthSquared() > 1e-12f) zAxis.Normalize(); else zAxis = Vec3(0, 0, 1);
		}
		auto normalAt = [&](const Vec3& at) {
			if (line.AlignmentMode == LineRendererBase::AlignTransformZ)
				return zAxis;
			Vec3 v = cam - at;
			return v.LengthSquared() > 1e-12f ? (v.Normalize(), v) : Vec3(0, 0, 1);
		};
		// 선분마다 방향 · 옆 방향
		std::vector<Vec3> dir(n - 1), side(n - 1);
		for (int s = 0; s < n - 1; ++s)
		{
			Vec3 d = p[s + 1] - p[s];
			d.Normalize();
			dir[s] = d;
			Vec3 sd = d.Cross(normalAt((p[s] + p[s + 1]) * 0.5f));
			if (sd.LengthSquared() < 1e-12f)
				sd = d.Cross(std::fabs(d.y) < 0.99f ? Vec3(0, 1, 0) : Vec3(1, 0, 0));
			sd.Normalize();
			side[s] = sd;
		}

		auto vert = [](const Vec3& pos, float uu, float vv, const XMFLOAT4& c) { return Vertex{ XMFLOAT3(pos.x, pos.y, pos.z), XMFLOAT2(uu, vv), c }; };
		auto quad = [&](const Vec3& a0, const Vec3& b0, const Vec3& a1, const Vec3& b1, int i0, int i1) {
			const Vertex A = vert(a0, u[i0], 0.0f, col[i0]), B = vert(b0, u[i0], vScale, col[i0]);
			const Vertex C = vert(a1, u[i1], 0.0f, col[i1]), D = vert(b1, u[i1], vScale, col[i1]);
			out.insert(out.end(), { A, B, C, B, D, C });
		};
		// 점 i 를 중심으로 from → to 방향 (단위 벡터) 사이를 k + 1 조각 부채꼴로
		auto fan = [&](int i, const Vec3& from, const Vec3& to, const Vec3& axisA, const Vec3& axisB, int k, float vFrom, float vTo) {
			const Vertex center = vert(p[i], u[i], vScale * 0.5f, col[i]);
			Vec3 prev = from;
			float vPrev = vFrom;
			for (int j = 1; j <= k + 1; ++j)
			{
				const float a = (float)j / (k + 1);
				Vec3 cur = axisA * std::cos(a * XM_PI) + axisB * std::sin(a * XM_PI);
				if (axisB.LengthSquared() < 1e-12f)   // 꺾인 곳: from → to 를 고르게
				{
					cur = from * (1.0f - a) + to * a;
					if (cur.LengthSquared() > 1e-12f) cur.Normalize();
				}
				const float vCur = vFrom + (vTo - vFrom) * a;
				out.insert(out.end(), { center, vert(p[i] + prev * w[i], u[i], vPrev, col[i]), vert(p[i] + cur * w[i], u[i], vCur, col[i]) });
				prev = cur;
				vPrev = vCur;
			}
		};

		if (line.CornerVertices <= 0)
		{
			// 뾰족한 이음: 점마다 앞뒤 선분의 옆 방향 평균 (미터, 4 배까지)
			std::vector<Vec3> ps(n);
			std::vector<float> miter(n, 1.0f);
			for (int i = 0; i < n; ++i)
			{
				const bool first = i == 0, last = i == n - 1;
				Vec3 a = first ? (loop ? side[n - 2] : side[0]) : side[i - 1];
				Vec3 b = last ? (loop ? side[0] : side[n - 2]) : side[i];
				Vec3 s = a + b;
				if (s.LengthSquared() < 1e-8f)
					s = b;
				s.Normalize();
				ps[i] = s;
				miter[i] = 1.0f / (std::max)(s.Dot(b), 0.25f);
			}
			for (int s = 0; s < n - 1; ++s)
				quad(p[s] - ps[s] * w[s] * miter[s], p[s] + ps[s] * w[s] * miter[s], p[s + 1] - ps[s + 1] * w[s + 1] * miter[s + 1], p[s + 1] + ps[s + 1] * w[s + 1] * miter[s + 1], s, s + 1);
		}
		else
		{
			// 선분마다 따로 + 꺾인 바깥쪽을 부채꼴로 채운다
			for (int s = 0; s < n - 1; ++s)
				quad(p[s] - side[s] * w[s], p[s] + side[s] * w[s], p[s + 1] - side[s] * w[s + 1], p[s + 1] + side[s] * w[s + 1], s, s + 1);
			const int joints = loop ? n - 1 : n - 2;
			for (int k = 1; k <= joints; ++k)
			{
				const int i = k % (n - 1) == 0 && loop ? 0 : k;   // 닫힌 선의 첫 점
				const int sp = (k - 1), sn = k % (n - 1);
				const float sign = dir[sn].Dot(side[sp]) > 0.0f ? -1.0f : 1.0f;   // 다음 선분이 휘는 반대쪽이 벌어진다
				fan(i == 0 ? n - 1 : i, side[sp] * sign, side[sn] * sign, Vec3(), Vec3(), line.CornerVertices, sign > 0 ? vScale : 0.0f, sign > 0 ? vScale : 0.0f);
			}
		}
		// 끝 모양: 반원 (+옆 → 뒤 → -옆)
		if (!loop && line.CapVertices > 0)
		{
			fan(0, side[0], -side[0], side[0], -dir[0], line.CapVertices, vScale, 0.0f);
			fan(n - 1, side[n - 2], -side[n - 2], side[n - 2], dir[n - 2], line.CapVertices, vScale, 0.0f);
		}
	}
}

// ------------------------------------------------------------------ C# (ScriptCore 의 LineRenderer.cs — DllImport("NovaCore"))
namespace
{
	LineRendererBase* FindLine(uint64 id, int kind)
	{
		GameObject* go = ScriptBindings::FindObject(id);
		if (go == nullptr)
			return nullptr;
		// 같은 프레임에 AddComponent 한 것은 아직 대기 목록에 있다 (C# 이 바로 값을 넣는다)
		return kind == 1 ? static_cast<LineRendererBase*>(go->GetComponentIncludingPending<TrailRenderer>()) : static_cast<LineRendererBase*>(go->GetComponentIncludingPending<LineRenderer>());
	}
}

// prop: 0 widthMultiplier, 1 startWidth, 2 endWidth, 3 loop | time, 4 useWorldSpace | minVertexDistance, 5 numCornerVertices,
//       6 numCapVertices, 7 alignment, 8 textureMode, 9 emitting, 10 autodestruct, 11 enabled
NOVA_PACKAGE_EXPORT float NovaLine_GetFloat(uint64 id, int kind, int prop)
{
	LineRendererBase* l = FindLine(id, kind);
	if (l == nullptr)
		return 0.0f;
	auto* line = dynamic_cast<LineRenderer*>(l);
	auto* trail = dynamic_cast<TrailRenderer*>(l);
	switch (prop)
	{
	case 0: return l->WidthMultiplier;
	case 1: return l->WidthAt(0.0f);
	case 2: return l->WidthAt(1.0f);
	case 3: return line ? (line->Loop ? 1.0f : 0.0f) : trail->Time;
	case 4: return line ? (line->UseWorldSpace ? 1.0f : 0.0f) : trail->MinVertexDistance;
	case 5: return (float)l->CornerVertices;
	case 6: return (float)l->CapVertices;
	case 7: return (float)l->AlignmentMode;
	case 8: return (float)l->TextureMode;
	case 9: return trail && trail->Emitting ? 1.0f : 0.0f;
	case 10: return trail && trail->Autodestruct ? 1.0f : 0.0f;
	case 11: return l->IsEnabled() ? 1.0f : 0.0f;
	default: return 0.0f;
	}
}

NOVA_PACKAGE_EXPORT void NovaLine_SetFloat(uint64 id, int kind, int prop, float v)
{
	LineRendererBase* l = FindLine(id, kind);
	if (l == nullptr)
		return;
	auto* line = dynamic_cast<LineRenderer*>(l);
	auto* trail = dynamic_cast<TrailRenderer*>(l);
	switch (prop)
	{
	case 0: l->WidthMultiplier = (std::max)(0.0f, v); break;
	case 1: l->SetEndWidth(false, v); break;
	case 2: l->SetEndWidth(true, v); break;
	case 3: if (line) line->Loop = v != 0.0f; else trail->Time = (std::max)(0.0f, v); break;
	case 4: if (line) line->UseWorldSpace = v != 0.0f; else trail->MinVertexDistance = (std::max)(0.0f, v); break;
	case 5: l->CornerVertices = std::clamp((int)v, 0, 90); break;
	case 6: l->CapVertices = std::clamp((int)v, 0, 90); break;
	case 7: l->AlignmentMode = std::clamp((int)v, 0, 1); break;
	case 8: l->TextureMode = std::clamp((int)v, 0, 3); break;
	case 9: if (trail) trail->Emitting = v != 0.0f; break;
	case 10: if (trail) trail->Autodestruct = v != 0.0f; break;
	case 11: l->SetEnabled(v != 0.0f); break;
	default: break;
	}
}

// which: 0 = startColor, 1 = endColor (rgba)
NOVA_PACKAGE_EXPORT void NovaLine_GetColor(uint64 id, int kind, int which, float* rgba)
{
	LineRendererBase* l = FindLine(id, kind);
	const Vec4 c = l ? l->ColorAt(which ? 1.0f : 0.0f) : Vec4(0, 0, 0, 0);
	if (rgba) { rgba[0] = c.x; rgba[1] = c.y; rgba[2] = c.z; rgba[3] = c.w; }
}

NOVA_PACKAGE_EXPORT void NovaLine_SetColor(uint64 id, int kind, int which, const float* rgba)
{
	if (LineRendererBase* l = FindLine(id, kind); l && rgba)
		l->SetEndColor(which != 0, Vec4(rgba[0], rgba[1], rgba[2], rgba[3]));
}

NOVA_PACKAGE_EXPORT int NovaLine_GetCount(uint64 id, int kind)
{
	LineRendererBase* l = FindLine(id, kind);
	if (auto* line = dynamic_cast<LineRenderer*>(l)) return (int)line->Positions.size();
	if (auto* trail = dynamic_cast<TrailRenderer*>(l)) return (int)trail->Points().size();
	return 0;
}

NOVA_PACKAGE_EXPORT void NovaLine_SetCount(uint64 id, int count)
{
	if (auto* line = dynamic_cast<LineRenderer*>(FindLine(id, 0)))
		line->Positions.resize((size_t)std::clamp(count, 0, 1000000), Vec3(0, 0, 0));
}

NOVA_PACKAGE_EXPORT void NovaLine_GetPosition(uint64 id, int kind, int index, float* xyz)
{
	Vec3 v(0, 0, 0);
	LineRendererBase* l = FindLine(id, kind);
	if (auto* line = dynamic_cast<LineRenderer*>(l); line && index >= 0 && index < (int)line->Positions.size()) v = line->Positions[(size_t)index];
	if (auto* trail = dynamic_cast<TrailRenderer*>(l); trail && index >= 0 && index < (int)trail->Points().size()) v = trail->Points()[(size_t)index].Position;
	if (xyz) { xyz[0] = v.x; xyz[1] = v.y; xyz[2] = v.z; }
}

NOVA_PACKAGE_EXPORT void NovaLine_SetPosition(uint64 id, int kind, int index, const float* xyz)
{
	if (!xyz)
		return;
	LineRendererBase* l = FindLine(id, kind);
	const Vec3 v(xyz[0], xyz[1], xyz[2]);
	if (auto* line = dynamic_cast<LineRenderer*>(l); line && index >= 0 && index < (int)line->Positions.size()) line->Positions[(size_t)index] = v;
	if (auto* trail = dynamic_cast<TrailRenderer*>(l)) trail->SetPosition(index, v);
}

// xyz = count * 3 (LineRenderer.SetPositions — positionCount 도 바꾼다)
NOVA_PACKAGE_EXPORT void NovaLine_SetPositions(uint64 id, const float* xyz, int count)
{
	auto* line = dynamic_cast<LineRenderer*>(FindLine(id, 0));
	if (line == nullptr || (xyz == nullptr && count > 0))
		return;
	line->Positions.resize((size_t)(std::max)(count, 0));
	for (int i = 0; i < count; ++i)
		line->Positions[(size_t)i] = Vec3(xyz[i * 3], xyz[i * 3 + 1], xyz[i * 3 + 2]);
}

NOVA_PACKAGE_EXPORT void NovaTrail_Clear(uint64 id)
{
	if (auto* trail = dynamic_cast<TrailRenderer*>(FindLine(id, 1)))
		trail->Clear();
}

NOVA_PACKAGE_EXPORT void NovaTrail_AddPosition(uint64 id, const float* xyz)
{
	if (auto* trail = dynamic_cast<TrailRenderer*>(FindLine(id, 1)); trail && xyz)
		trail->AddPosition(Vec3(xyz[0], xyz[1], xyz[2]));
}
