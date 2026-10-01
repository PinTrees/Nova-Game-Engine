#include "pch.h"
#include "TerrainStamp.h"
#include "Terrain.h"
#include "Transform.h"
#include "UnityGUI.h"
#include "SceneViewOverlay.h"
#include "SelectionManager.h"

namespace
{
	std::vector<TerrainStamp*>& Registry()
	{
		static std::vector<TerrainStamp*> all;
		return all;
	}

	const char* kShapes[] = { "Mountain", "Hill", "Crater", "Volcano", "Mesa", "Ridge", "Canyon", "Dunes", "Island", "Heightmap" };
	const char* kOps[] = { "Add", "Subtract", "Max", "Min", "Blend" };
}

TerrainStamp::TerrainStamp()
{
	m_InspectorTitleName = "Terrain Stamp";
	Registry().push_back(this);
}

TerrainStamp::~TerrainStamp()
{
	auto& all = Registry();
	all.erase(std::remove(all.begin(), all.end(), this), all.end());
}

const std::vector<TerrainStamp*>& TerrainStamp::All() { return Registry(); }

bool TerrainStamp::IsActiveStamp() const
{
	return m_Enabled && m_pGameObject && m_pGameObject->IsActive();
}

const char* TerrainStamp::ShapeName(Shape s) { return kShapes[std::clamp((int)s, 0, (int)Shape::Count - 1)]; }
const char* TerrainStamp::OperationName(Operation o) { return kOps[std::clamp((int)o, 0, (int)Operation::Count - 1)]; }

void TerrainStamp::OnInspectorGUI()
{
	using namespace UnityGUI;
	int shape = (int)StampShape;
	if (Dropdown("Shape", &shape, kShapes, (int)Shape::Count))
		StampShape = (Shape)shape;
	if (StampShape == Shape::Heightmap)
	{
		TextField("Heightmap", &HeightmapPath, 1);
		HelpBox("Project path of a grayscale image (PNG / TGA / DDS). White = top.", false, 1);
	}
	int op = (int)Op;
	if (Dropdown("Operation", &op, kOps, (int)Operation::Count))
		Op = (Operation)op;
	if (Op == Operation::Max || Op == Operation::Min || Op == Operation::Blend)
		HelpBox("Max / Min / Blend use the stamp's Y position as the base level.", false, 1);
	Slider("Height", &Height, -500.0f, 1000.0f);
	Slider("Opacity", &Opacity, 0.0f, 1.0f);
	Label("Area", 0, true);
	Slider("Blend Size", &BlendSize, 0.01f, 1.0f, 1);
	Slider("Roundness", &Roundness, 0.0f, 1.0f, 1);
	Label("Shape", 0, true);
	Slider("Power", &Power, 0.2f, 4.0f, 1);
	Slider("Detail", &Detail, 0.0f, 1.5f, 1);
	Slider("Detail Scale", &DetailScale, 0.02f, 1.0f, 1);
	Int("Seed", &Seed, 1);
	if (CenterButton("New Seed"))
		Seed = (Seed * 1103515245 + 12345) & 0x7fff;
	Int("Order", &Order);
	HelpBox("Area = Transform scale X / Z (m), height multiplier = scale Y, direction = rotation Y.\nTerrains with Generate enabled rebuild automatically.", false);
}

// Scene 뷰: 선택하면 영역(바깥) + 섞기 시작(안쪽) 경계를 지형 표면을 따라 그린다
void TerrainStamp::OnDrawGizmos()
{
	if (!SceneViewOverlay::IsActive() || SelectionManager::GetSelectedGameObject() != m_pGameObject || m_pGameObject == nullptr)
		return;
	XMFLOAT4X4 w;
	XMStoreFloat4x4(&w, m_pGameObject->GetTransform()->GetWorldMatrix());
	const float sx = sqrtf(w._11 * w._11 + w._12 * w._12 + w._13 * w._13) * 0.5f;
	const float sz = sqrtf(w._31 * w._31 + w._32 * w._32 + w._33 * w._33) * 0.5f;
	const float len = sqrtf(w._11 * w._11 + w._13 * w._13);
	const float c = len > 1e-6f ? w._11 / len : 1.0f, s = len > 1e-6f ? w._13 / len : 0.0f;
	auto ground = [&](float x, float z) {
		for (Terrain* t : Terrain::GetActiveTerrains())
		{
			const Vec3 p = t->GetPosition();
			auto data = t->GetTerrainData();
			if (data && x >= p.x && z >= p.z && x <= p.x + data->Size.x && z <= p.z + data->Size.z)
				return p.y + t->SampleHeight(Vec3(x, 0, z)) + 0.5f;
		}
		return w._42;
	};
	auto ring = [&](float scale, ImU32 color, float thickness) {
		const int n = 96;
		XMFLOAT3 prev = {};
		for (int i = 0; i <= n; ++i)
		{
			const float a = XM_2PI * i / n;
			const float du = cosf(a), dv = sinf(a);
			// 경계: lerp(max(|u|,|v|), 반지름, Roundness) = scale
			const float m = (std::max)(fabsf(du), fabsf(dv));
			const float t = scale / (m + (1.0f - m) * std::clamp(Roundness, 0.0f, 1.0f));
			const float u = du * t * sx, v = dv * t * sz;
			const float x = w._41 + u * c - v * s, z = w._43 + u * s + v * c;
			const XMFLOAT3 cur(x, ground(x, z), z);
			if (i > 0)
				SceneViewOverlay::DrawLine(prev, cur, color, thickness);
			prev = cur;
		}
	};
	ring(1.0f, IM_COL32(255, 170, 60, 230), 2.0f);
	ring(1.0f - std::clamp(BlendSize, 0.01f, 1.0f), IM_COL32(255, 230, 120, 160), 1.0f);
	// 가운데 → 앞(로컬 +X) 방향 표시
	const XMFLOAT3 center(w._41, ground(w._41, w._43), w._43);
	const float fx = w._41 + c * sx * 0.3f, fz = w._43 + s * sx * 0.3f;
	SceneViewOverlay::DrawLine(center, XMFLOAT3(fx, ground(fx, fz), fz), IM_COL32(255, 170, 60, 230), 2.0f);
}

GENERATE_COMPONENT_FUNC_TOJSON(TerrainStamp)
{
	json j;
	SERIALIZE_TYPE(j, TerrainStamp);
	j["enabled"] = m_Enabled;
	j["shape"] = (int)StampShape;
	j["operation"] = (int)Op;
	j["height"] = Height;
	j["opacity"] = Opacity;
	j["blendSize"] = BlendSize;
	j["roundness"] = Roundness;
	j["power"] = Power;
	j["detail"] = Detail;
	j["detailScale"] = DetailScale;
	j["seed"] = Seed;
	j["order"] = Order;
	j["heightmap"] = HeightmapPath;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(TerrainStamp)
{
	m_Enabled = j.value("enabled", true);
	StampShape = (Shape)std::clamp(j.value("shape", 0), 0, (int)Shape::Count - 1);
	Op = (Operation)std::clamp(j.value("operation", 0), 0, (int)Operation::Count - 1);
	Height = j.value("height", Height);
	Opacity = j.value("opacity", Opacity);
	BlendSize = j.value("blendSize", BlendSize);
	Roundness = j.value("roundness", Roundness);
	Power = j.value("power", Power);
	Detail = j.value("detail", Detail);
	DetailScale = j.value("detailScale", DetailScale);
	Seed = j.value("seed", Seed);
	Order = j.value("order", Order);
	HeightmapPath = j.value("heightmap", std::string());
}
