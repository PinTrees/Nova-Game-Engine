#include "pch.h"
#include "TerrainSpline.h"
#include "TerrainSplineEditor.h"
#include "Terrain.h"
#include "TerrainData.h"
#include "Transform.h"
#include "UnityGUI.h"
#include "SceneViewOverlay.h"
#include "SelectionManager.h"
#include <filesystem>

namespace
{
	std::vector<TerrainSpline*>& Registry()
	{
		static std::vector<TerrainSpline*> all;
		return all;
	}

	const char* kModes[] = { "Road", "Canyon", "Ridge" };

	float Lerp(float a, float b, float t) { return a + (b - a) * t; }

	Vec3 CatmullRom(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, float t)
	{
		const float t2 = t * t, t3 = t2 * t;
		return (p1 * 2.0f + (p2 - p0) * t + (p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * t2 + (p1 * 3.0f - p0 - p2 * 3.0f + p3) * t3) * 0.5f;
	}

	// 땅 높이. uncarved = 생성기가 도로·물로 바꾸기 전 높이 (점 맞추기가 자기가 깎은 땅을 다시 재지 않게)
	bool GroundHeight(float x, float z, float& y, bool uncarved = false)
	{
		for (Terrain* t : Terrain::GetActiveTerrains())
		{
			const Vec3 p = t->GetPosition();
			auto data = t->GetTerrainData();
			if (data && x >= p.x && z >= p.z && x <= p.x + data->Size.x && z <= p.z + data->Size.z)
			{
				y = p.y + (uncarved ? data->GetUncarvedHeight(x - p.x, z - p.z) : t->SampleHeight(Vec3(x, 0, z)));
				return true;
			}
		}
		return false;
	}
}

TerrainSpline::TerrainSpline()
{
	m_InspectorTitleName = "Terrain Spline";
	Registry().push_back(this);
}

TerrainSpline::~TerrainSpline()
{
	auto& all = Registry();
	all.erase(std::remove(all.begin(), all.end(), this), all.end());
}

const std::vector<TerrainSpline*>& TerrainSpline::All() { return Registry(); }

bool TerrainSpline::IsActiveSpline() const
{
	return m_Enabled && m_pGameObject && m_pGameObject->IsActiveInHierarchy();
}

const char* TerrainSpline::ModeName(Mode m) { return kModes[std::clamp((int)m, 0, (int)Mode::Count - 1)]; }

XMMATRIX TerrainSpline::WorldMatrix() const
{
	return m_pGameObject ? (XMMATRIX)m_pGameObject->GetTransform()->GetWorldMatrix() : XMMatrixIdentity();
}

void TerrainSpline::ApplyModeDefaults()
{
	switch (SplineMode)
	{
	case Mode::Road: BeforeErosion = false; PaintLayer = 2; PaintColor = true; Falloff = 12.0f; break;
	case Mode::Canyon: BeforeErosion = true; PaintLayer = 1; PaintColor = false; Falloff = 30.0f; Depth = 30.0f; break;
	default: BeforeErosion = true; PaintLayer = -1; PaintColor = false; Falloff = 25.0f; Depth = 20.0f; break;
	}
	const float w = SplineMode == Mode::Road ? 8.0f : SplineMode == Mode::Canyon ? 14.0f : 6.0f;
	for (Point& p : Points)
		p.Width = w;
}

void TerrainSpline::ResetShape()
{
	// 240 m, 굽이 두 번
	Points.clear();
	for (int i = 0; i < 5; ++i)
	{
		Point p;
		p.Position = Vec3(-120.0f + i * 60.0f, 0.0f, i == 1 ? 35.0f : i == 3 ? -35.0f : 0.0f);
		Points.push_back(p);
	}
	ApplyModeDefaults();
	++Revision;
}

void TerrainSpline::SnapToGround()
{
	if (m_pGameObject == nullptr || Points.empty())
		return;
	const XMMATRIX world = WorldMatrix();
	const XMMATRIX inv = XMMatrixInverse(nullptr, world);
	std::vector<Vec3> w(Points.size());
	for (size_t i = 0; i < Points.size(); ++i)
	{
		w[i] = XMVector3TransformCoord(Points[i].Position, world);
		float g;
		if (GroundHeight(w[i].x, w[i].z, g, true))
			w[i].y = g;
	}
	// 길은 앞뒤 점과 고르게 (울퉁불퉁하지 않게)
	if (SplineMode == Mode::Road && w.size() > 2)
	{
		std::vector<Vec3> s = w;
		for (size_t i = 1; i + 1 < w.size(); ++i)
			s[i].y = w[i - 1].y * 0.25f + w[i].y * 0.5f + w[i + 1].y * 0.25f;
		w = s;
	}
	for (size_t i = 0; i < Points.size(); ++i)
		Points[i].Position = XMVector3TransformCoord(w[i], inv);
	++Revision;
}

std::vector<TerrainSpline::CurveSample> TerrainSpline::Curve(float step) const
{
	std::vector<CurveSample> out;
	const int n = (int)Points.size();
	if (n < 2 || m_pGameObject == nullptr)
		return out;
	const XMMATRIX world = WorldMatrix();
	std::vector<Vec3> w(n);
	for (int i = 0; i < n; ++i)
		w[i] = XMVector3TransformCoord(Points[i].Position, world);
	auto at = [&](int i) { return std::clamp(i, 0, n - 1); };
	step = (std::max)(0.25f, step);
	for (int s = 0; s + 1 < n; ++s)
	{
		const Vec3 &p0 = w[at(s - 1)], &p1 = w[s], &p2 = w[s + 1], &p3 = w[at(s + 2)];
		const int count = (std::max)(1, (int)ceilf((p2 - p1).Length() / step));
		const bool last = s + 2 == n;
		for (int k = 0; k < count + (last ? 1 : 0); ++k)
		{
			const float t = (float)k / count;
			CurveSample c;
			c.Position = CatmullRom(p0, p1, p2, p3, t);
			Vec3 tan = CatmullRom(p0, p1, p2, p3, (std::min)(1.0f, t + 0.01f)) - CatmullRom(p0, p1, p2, p3, (std::max)(0.0f, t - 0.01f));
			tan.Normalize();
			c.Tangent = tan;
			c.Width = Lerp(Points[s].Width, Points[s + 1].Width, t);
			out.push_back(c);
		}
	}
	return out;
}

void TerrainSpline::OnInspectorGUI()
{
	using namespace UnityGUI;
	int mode = (int)SplineMode;
	if (Dropdown("Mode", &mode, kModes, (int)Mode::Count) && mode != (int)SplineMode)
	{
		SplineMode = (Mode)mode;
		ApplyModeDefaults();
		++Revision;
	}
	Slider("Falloff", &Falloff, 0.5f, 120.0f);
	if (SplineMode != Mode::Road)
		Slider(SplineMode == Mode::Canyon ? "Depth" : "Height", &Depth, 0.5f, 200.0f);
	Toggle("Before Erosion", &BeforeErosion);
	Label("Shape", 0, true);
	TerrainSplineEditor::InspectorPoints(*this);
	Label("Paint", 0, true);
	// 칠할 레이어: 첫 지형의 레이어 이름
	std::vector<std::string> names = { "None" };
	if (!Terrain::GetActiveTerrains().empty())
		if (auto data = Terrain::GetActiveTerrains()[0]->GetTerrainData())
			for (size_t i = 0; i < data->Layers.size(); ++i)
				names.push_back(data->Layers[i] ? std::filesystem::path(data->Layers[i]->Path).stem().string() : "Layer " + std::to_string(i));
	while ((int)names.size() < 5)
		names.push_back("Layer " + std::to_string(names.size() - 1));
	std::vector<const char*> items;
	for (const auto& s : names) items.push_back(s.c_str());
	int layer = PaintLayer + 1;
	if (Dropdown("Layer", &layer, items.data(), (int)items.size()))
		PaintLayer = layer - 1;
	Toggle("Paint Color", &PaintColor);
	if (PaintColor)
	{
		float rgba[4] = { Color[0], Color[1], Color[2], 1.0f };
		if (UnityGUI::Color("Color", rgba, 1))
			for (int i = 0; i < 3; ++i) Color[i] = rgba[i];
	}
	Int("Order", &Order);
	HelpBox(SplineMode == Mode::Road
		? "Flattens the terrain to the curve (point heights) and blends back over Falloff. Use Snap Points To Ground first."
		: "Carves (Canyon) or raises (Ridge) the terrain along the curve. Width = floor/top width, Falloff = wall width.", false);
}

void TerrainSpline::OnDrawGizmos()
{
	if (!SceneViewOverlay::IsActive() || SelectionManager::GetSelectedGameObject() != m_pGameObject || m_pGameObject == nullptr)
		return;
	const auto curve = Curve(3.0f);
	const ImU32 main = IM_COL32(255, 200, 90, 230), edge = IM_COL32(255, 200, 90, 110);
	auto ground = [](Vec3 p) { float g; if (GroundHeight(p.x, p.z, g)) p.y = (std::max)(p.y, g); p.y += 0.3f; return XMFLOAT3(p.x, p.y, p.z); };
	for (size_t i = 0; i + 1 < curve.size(); ++i)
	{
		SceneViewOverlay::DrawLine(ground(curve[i].Position), ground(curve[i + 1].Position), main, 2.0f);
		for (float side : { -1.0f, 1.0f })
		{
			auto off = [&](const CurveSample& c, float extra) {
				const Vec3 right(c.Tangent.z, 0.0f, -c.Tangent.x);
				return c.Position + right * (side * (c.Width * 0.5f + extra));
			};
			SceneViewOverlay::DrawLine(ground(off(curve[i], 0.0f)), ground(off(curve[i + 1], 0.0f)), edge, 1.0f);
			if (Falloff > 0.5f)
				SceneViewOverlay::DrawLine(ground(off(curve[i], Falloff)), ground(off(curve[i + 1], Falloff)), IM_COL32(255, 200, 90, 50), 1.0f);
		}
	}
	TerrainSplineEditor::DrawPoints(*this);
}

GENERATE_COMPONENT_FUNC_TOJSON(TerrainSpline)
{
	json j;
	SERIALIZE_TYPE(j, TerrainSpline);
	j["enabled"] = m_Enabled;
	j["mode"] = (int)SplineMode;
	j["falloff"] = Falloff;
	j["depth"] = Depth;
	j["beforeErosion"] = BeforeErosion;
	j["paintLayer"] = PaintLayer;
	j["paintColor"] = PaintColor;
	j["color"] = { Color[0], Color[1], Color[2] };
	j["order"] = Order;
	json pts = json::array();
	for (const Point& p : Points)
		pts.push_back({ p.Position.x, p.Position.y, p.Position.z, p.Width });
	j["points"] = pts;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(TerrainSpline)
{
	m_Enabled = j.value("enabled", true);
	SplineMode = (Mode)std::clamp(j.value("mode", 0), 0, (int)Mode::Count - 1);
	Falloff = j.value("falloff", Falloff);
	Depth = j.value("depth", Depth);
	BeforeErosion = j.value("beforeErosion", BeforeErosion);
	PaintLayer = j.value("paintLayer", PaintLayer);
	PaintColor = j.value("paintColor", PaintColor);
	if (j.contains("color") && j["color"].is_array() && j["color"].size() >= 3)
		for (int i = 0; i < 3; ++i) Color[i] = j["color"][i].get<float>();
	Order = j.value("order", Order);
	Points.clear();
	if (j.contains("points") && j["points"].is_array())
		for (const auto& p : j["points"])
			if (p.is_array() && p.size() >= 3)
			{
				Point pt;
				pt.Position = Vec3(p[0].get<float>(), p[1].get<float>(), p[2].get<float>());
				if (p.size() >= 4) pt.Width = p[3].get<float>();
				Points.push_back(pt);
			}
	++Revision;
}
