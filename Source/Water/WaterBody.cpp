#include "pch.h"
#include "WaterBody.h"
#include "WaterEditor.h"
#include "Terrain.h"
#include "Transform.h"
#include "UnityGUI.h"
#include "SceneViewOverlay.h"
#include "SelectionManager.h"
#include "Terrain.h"

namespace
{
	float Lerp(float a, float b, float t) { return a + (b - a) * t; }

	std::vector<WaterBody*>& Registry()
	{
		static std::vector<WaterBody*> all;
		return all;
	}

	const char* kTypes[] = { "Ocean", "Lake", "River" };

	Vec3 CatmullRom(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, float t)
	{
		const float t2 = t * t, t3 = t2 * t;
		return (p1 * 2.0f + (p2 - p0) * t + (p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * t2 + (p1 * 3.0f - p0 - p2 * 3.0f + p3) * t3) * 0.5f;
	}

	// 땅 높이 (지형 생성기가 물로 파기 전 높이 → 맞추기를 여러 번 해도 가라앉지 않는다)
	bool GroundHeight(float x, float z, float& y)
	{
		for (Terrain* t : Terrain::GetActiveTerrains())
		{
			const Vec3 p = t->GetPosition();
			auto data = t->GetTerrainData();
			if (data && x >= p.x && z >= p.z && x <= p.x + data->Size.x && z <= p.z + data->Size.z)
			{
				y = p.y + data->GetUncarvedHeight(x - p.x, z - p.z);
				return true;
			}
		}
		return false;
	}

	bool InsidePolygon(const std::vector<WaterBody::CurveSample>& poly, float x, float z)
	{
		bool inside = false;
		for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
		{
			const Vec3& a = poly[i].Position;
			const Vec3& b = poly[j].Position;
			if ((a.z > z) != (b.z > z) && x < (b.x - a.x) * (z - a.z) / (b.z - a.z) + a.x)
				inside = !inside;
		}
		return inside;
	}
}

WaterBody::WaterBody()
{
	m_InspectorTitleName = "Water Body";
	Registry().push_back(this);
}

WaterBody::~WaterBody()
{
	auto& all = Registry();
	all.erase(std::remove(all.begin(), all.end(), this), all.end());
}

const std::vector<WaterBody*>& WaterBody::All() { return Registry(); }

bool WaterBody::IsActiveBody() const
{
	return m_Enabled && m_pGameObject && m_pGameObject->IsActive();
}

const char* WaterBody::TypeName(Type t) { return kTypes[std::clamp((int)t, 0, (int)Type::Count - 1)]; }

void WaterBody::ResetShape()
{
	Points.clear();
	if (BodyType == Type::Lake)
	{
		// 살짝 찌그러진 타원 (반지름 약 60 m)
		for (int i = 0; i < 8; ++i)
		{
			const float a = XM_2PI * i / 8;
			const float r = 60.0f * (1.0f + 0.18f * sinf(a * 2.0f + 0.7f));
			Point p;
			p.Position = Vec3(cosf(a) * r * 1.3f, 0.0f, sinf(a) * r);
			Points.push_back(p);
		}
	}
	else if (BodyType == Type::River)
	{
		// 200 m, 굽이 두 번, 10 m 내려간다
		for (int i = 0; i < 6; ++i)
		{
			Point p;
			p.Position = Vec3(-100.0f + i * 40.0f, -2.0f * i, (i % 2 ? 22.0f : -22.0f) * (i > 0 && i < 5 ? 1.0f : 0.3f));
			p.Width = 14.0f;
			p.Depth = 2.5f;
			Points.push_back(p);
		}
	}
	++Revision;
}

void WaterBody::SnapToGround()
{
	if (m_pGameObject == nullptr || BodyType == Type::Ocean)
		return;
	Transform* tr = m_pGameObject->GetTransform();
	if (BodyType == Type::River)
	{
		const XMMATRIX world = tr->GetWorldMatrix();
		const XMMATRIX inv = XMMatrixInverse(nullptr, world);
		float prevY = FLT_MAX;
		for (Point& p : Points)
		{
			Vec3 w = XMVector3TransformCoord(p.Position, world);
			float g;
			if (GroundHeight(w.x, w.z, g))
				w.y = g - 0.6f;
			w.y = (std::min)(w.y, prevY - 0.05f);
			prevY = w.y;
			p.Position = XMVector3TransformCoord(w, inv);
		}
	}
	else
	{
		const auto curve = Curve(4.0f);
		float lowest = FLT_MAX;
		for (const auto& c : curve)
		{
			float g;
			if (GroundHeight(c.Position.x, c.Position.z, g))
				lowest = (std::min)(lowest, g);
		}
		if (lowest < FLT_MAX)
		{
			Vec3 pos = tr->GetPosition();
			pos.y = lowest - 0.3f;
			tr->SetPosition(pos);
		}
	}
	++Revision;
}

std::vector<WaterBody::CurveSample> WaterBody::Curve(float step) const
{
	std::vector<CurveSample> out;
	const size_t n = Points.size();
	if (BodyType == Type::Ocean || n < 2 || m_pGameObject == nullptr)
		return out;
	const XMMATRIX world = m_pGameObject->GetTransform()->GetWorldMatrix();
	std::vector<Vec3> w(n);
	for (size_t i = 0; i < n; ++i)
		w[i] = XMVector3TransformCoord(Points[i].Position, world);
	const bool closed = BodyType == Type::Lake;
	const size_t segs = closed ? n : n - 1;
	auto at = [&](int i) -> int {
		if (closed) return (int)(((i % (int)n) + (int)n) % (int)n);
		return std::clamp(i, 0, (int)n - 1);
	};
	step = (std::max)(0.25f, step);
	float dist = 0.0f;
	Vec3 prev;
	for (size_t s = 0; s < segs; ++s)
	{
		const int i1 = at((int)s), i2 = at((int)s + 1);
		const Vec3 &p0 = w[at((int)s - 1)], &p1 = w[i1], &p2 = w[i2], &p3 = w[at((int)s + 2)];
		const int count = (std::max)(1, (int)ceilf((p2 - p1).Length() / step));
		const bool last = !closed && s + 1 == segs;
		for (int k = 0; k < count + (last ? 1 : 0); ++k)
		{
			const float t = (float)k / count;
			CurveSample c;
			c.Position = CatmullRom(p0, p1, p2, p3, t);
			const Vec3 ahead = CatmullRom(p0, p1, p2, p3, (std::min)(1.0f, t + 0.01f));
			const Vec3 behind = CatmullRom(p0, p1, p2, p3, (std::max)(0.0f, t - 0.01f));
			c.Tangent = ahead - behind;
			c.Tangent.Normalize();
			c.Width = Lerp(Points[i1].Width, Points[i2].Width, t);
			c.Depth = Lerp(Points[i1].Depth, Points[i2].Depth, t);
			c.Speed = Lerp(Points[i1].Speed, Points[i2].Speed, t);
			if (!out.empty())
				dist += (c.Position - prev).Length();
			c.Distance = dist;
			prev = c.Position;
			out.push_back(c);
		}
	}
	return out;
}

float WaterBody::SurfaceY() const
{
	return m_pGameObject ? m_pGameObject->GetTransform()->GetWorldMatrix()._42 : 0.0f;
}

XMMATRIX WaterBody::WorldMatrix() const
{
	return m_pGameObject ? (XMMATRIX)m_pGameObject->GetTransform()->GetWorldMatrix() : XMMatrixIdentity();
}

const WaterWaves::Set& WaterBody::Waves() const
{
	const WaterProfile& p = WaterProfiles::Get(Profile);
	uint32_t scaleBits;
	memcpy(&scaleBits, &WaveScale, 4);
	const uint64_t key = std::hash<std::string>()(Profile) ^ ((uint64_t)scaleBits << 17) ^ (uint64_t)(uintptr_t)&p ^ (uint64_t)BodyType * 0x9e3779b9ull;
	if (key != m_WavesKey)
	{
		m_WavesKey = key;
		// 강은 Gerstner 파도 대신 흐름 노멀만
		m_Waves = BodyType == Type::River ? WaterWaves::Set() : WaterWaves::Build(p, WaveScale);
	}
	return m_Waves;
}

bool WaterBody::Sample(const Vec3& world, float& height, Vec3* normal, Vec3* flow) const
{
	if (flow) *flow = Vec3(0, 0, 0);
	const float time = WaterWaves::Time();
	if (BodyType == Type::Ocean)
	{
		height = SurfaceY() + WaterWaves::Height(Waves(), world.x, world.z, time, normal);
		return true;
	}
	const std::vector<CurveSample> curve = Curve(4.0f);
	if (curve.size() < 2)
		return false;
	if (BodyType == Type::Lake)
	{
		if (!InsidePolygon(curve, world.x, world.z))
			return false;
		height = SurfaceY() + WaterWaves::Height(Waves(), world.x, world.z, time, normal);
		return true;
	}
	// 강: 가장 가까운 가운데 선 조각
	float best = FLT_MAX;
	for (size_t i = 0; i + 1 < curve.size(); ++i)
	{
		const Vec3 a = curve[i].Position, b = curve[i + 1].Position;
		const float abx = b.x - a.x, abz = b.z - a.z;
		const float len2 = abx * abx + abz * abz;
		const float t = len2 > 1e-6f ? std::clamp(((world.x - a.x) * abx + (world.z - a.z) * abz) / len2, 0.0f, 1.0f) : 0.0f;
		const float px = a.x + abx * t - world.x, pz = a.z + abz * t - world.z;
		const float d = sqrtf(px * px + pz * pz);
		const float halfW = Lerp(curve[i].Width, curve[i + 1].Width, t) * 0.5f;
		if (d <= halfW && d < best)
		{
			best = d;
			height = Lerp(a.y, b.y, t);
			if (normal) *normal = Vec3(0, 1, 0);
			if (flow)
			{
				const float speed = Lerp(curve[i].Speed, curve[i + 1].Speed, t) * FlowSpeed * WaterProfiles::Get(Profile).FlowSpeed;
				const float edge = d / (std::max)(halfW, 0.01f);
				*flow = curve[i].Tangent * (speed * (1.0f - 0.6f * edge * edge));
			}
		}
	}
	return best < FLT_MAX;
}

bool WaterBody::GroundAt(float x, float z, float& y)
{
	return GroundHeight(x, z, y);
}

bool WaterBody::Query(const Vec3& world, float& height, Vec3* normal, Vec3* flow)
{
	bool found = false;
	for (WaterBody* b : All())
	{
		if (!b->IsActiveBody())
			continue;
		float h;
		Vec3 n, f;
		if (b->Sample(world, h, &n, &f) && (!found || h > height))
		{
			found = true;
			height = h;
			if (normal) *normal = n;
			if (flow) *flow = f;
		}
	}
	return found;
}

void WaterBody::OnInspectorGUI()
{
	using namespace UnityGUI;
	int type = (int)BodyType;
	if (Dropdown("Type", &type, kTypes, (int)Type::Count) && type != (int)BodyType)
	{
		BodyType = (Type)type;
		ResetShape();
	}
	// 프로파일
	const auto& profiles = WaterProfiles::List();
	std::vector<const char*> names;
	int current = -1;
	for (int i = 0; i < (int)profiles.size(); ++i)
	{
		names.push_back(profiles[i].Name.c_str());
		if (profiles[i].Name == Profile)
			current = i;
	}
	if (current < 0)
	{
		names.push_back(Profile.c_str());
		current = (int)names.size() - 1;
	}
	int sel = current;
	if (Dropdown("Profile", &sel, names.data(), (int)names.size()) && sel != current && sel < (int)profiles.size())
		Profile = profiles[sel].Name;
	if (const WaterProfile* p = WaterProfiles::Find(Profile))
	{
		if (!p->Description.empty())
		{
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 18.0f);
			ImGui::PushTextWrapPos(ImGui::GetContentRegionMax().x - 8.0f);
			ImGui::TextDisabled("%s", p->Description.c_str());
			ImGui::PopTextWrapPos();
		}
	}
	else
		HelpBox("Profile not found in Resources/Packages/Water/Profiles.", true, 1);
	if (BodyType != Type::River)
		Slider("Wave Scale", &WaveScale, 0.0f, 3.0f);
	else
		Slider("Flow Speed", &FlowSpeed, 0.0f, 4.0f);

	if (BodyType != Type::Ocean)
	{
		Label("Shape", 0, true);
		WaterEditor::InspectorPoints(*this);
		Label("Terrain", 0, true);
		Toggle("Carve Terrain", &CarveTerrain);
		if (BodyType == Type::Lake)
			Slider("Carve Depth", &CarveDepth, 0.5f, 60.0f, 1);
		Slider("Bank Width", &BankWidth, 1.0f, 60.0f, 1);
		HelpBox(BodyType == Type::Lake
			? "Water level = Transform Y. Terrains with Generate enabled are carved to the outline (non-destructive)."
			: "Water level follows each point's height (keep it going downhill). Terrains with Generate enabled get a river bed.", false);
	}
	else
		HelpBox("Endless ocean at the Transform's height. Waves get smaller over shallow terrain.", false);
}

// Scene 뷰: 선택하면 윤곽 / 가운데 선과 강둑, 점
void WaterBody::OnDrawGizmos()
{
	if (!SceneViewOverlay::IsActive() || SelectionManager::GetSelectedGameObject() != m_pGameObject || m_pGameObject == nullptr)
		return;
	if (BodyType == Type::Ocean)
		return;
	const std::vector<CurveSample> curve = Curve(3.0f);
	const ImU32 lineColor = IM_COL32(80, 190, 255, 230), bankColor = IM_COL32(80, 190, 255, 120);
	const float lift = 0.3f;
	auto line = [&](const Vec3& a, const Vec3& b, ImU32 c, float t) {
		SceneViewOverlay::DrawLine(XMFLOAT3(a.x, a.y + lift, a.z), XMFLOAT3(b.x, b.y + lift, b.z), c, t);
	};
	for (size_t i = 0; i + 1 < curve.size(); ++i)
	{
		Vec3 a = curve[i].Position, b = curve[i + 1].Position;
		if (BodyType == Type::Lake)
			a.y = b.y = SurfaceY();
		line(a, b, lineColor, 2.0f);
		if (BodyType == Type::River)
		{
			auto side = [](const CurveSample& c, float s) {
				const Vec3 right(c.Tangent.z, 0.0f, -c.Tangent.x);
				return c.Position + right * (c.Width * 0.5f * s);
			};
			line(side(curve[i], 1.0f), side(curve[i + 1], 1.0f), bankColor, 1.0f);
			line(side(curve[i], -1.0f), side(curve[i + 1], -1.0f), bankColor, 1.0f);
		}
	}
	if (BodyType == Type::Lake && curve.size() > 2)
	{
		Vec3 a = curve.back().Position, b = curve.front().Position;
		a.y = b.y = SurfaceY();
		line(a, b, lineColor, 2.0f);
	}
	WaterEditor::DrawPoints(*this);
}

GENERATE_COMPONENT_FUNC_TOJSON(WaterBody)
{
	json j;
	SERIALIZE_TYPE(j, WaterBody);
	j["enabled"] = m_Enabled;
	j["bodyType"] = (int)BodyType;
	j["profile"] = Profile;
	j["waveScale"] = WaveScale;
	j["flowSpeed"] = FlowSpeed;
	j["carveTerrain"] = CarveTerrain;
	j["carveDepth"] = CarveDepth;
	j["bankWidth"] = BankWidth;
	json pts = json::array();
	for (const Point& p : Points)
		pts.push_back({ p.Position.x, p.Position.y, p.Position.z, p.Width, p.Depth, p.Speed });
	j["points"] = pts;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(WaterBody)
{
	m_Enabled = j.value("enabled", true);
	BodyType = (Type)std::clamp(j.value("bodyType", 1), 0, (int)Type::Count - 1);
	Profile = j.value("profile", Profile);
	WaveScale = j.value("waveScale", WaveScale);
	FlowSpeed = j.value("flowSpeed", FlowSpeed);
	CarveTerrain = j.value("carveTerrain", CarveTerrain);
	CarveDepth = j.value("carveDepth", CarveDepth);
	BankWidth = j.value("bankWidth", BankWidth);
	Points.clear();
	if (j.contains("points") && j["points"].is_array())
		for (const auto& p : j["points"])
			if (p.is_array() && p.size() >= 3)
			{
				Point pt;
				pt.Position = Vec3(p[0].get<float>(), p[1].get<float>(), p[2].get<float>());
				if (p.size() >= 6)
				{
					pt.Width = p[3].get<float>();
					pt.Depth = p[4].get<float>();
					pt.Speed = p[5].get<float>();
				}
				Points.push_back(pt);
			}
	++Revision;
}
