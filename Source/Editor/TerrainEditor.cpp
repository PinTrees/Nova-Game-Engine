#include "pch.h"
#include "TerrainEditor.h"
#include "Terrain.h"
#include "TerrainData.h"
#include "UnityGUI.h"
#include "SceneViewOverlay.h"
#include "EditorCamera.h"
#include "UndoSystem.h"
#include "TreeRenderer.h"
#include "DetailRenderer.h"
#include "TerrainGenerator.h"
#include "TerrainBiomes.h"
#include "TerrainStamp.h"
#include "TerrainBiome.h"
#include "TerrainSpline.h"
#include "GameObjectFactory.h"
#include "SelectionManager.h"
#include "PathManager.h"
#include "ImportSettingsInspector.h"
#include <random>

namespace
{
	using namespace TerrainEditor;

	// ---- 도구 상태 (Unity 처럼 마지막에 쓴 도구를 기억) ----
	Tool s_Tool = Tool::PaintTerrain;
	PaintTool s_PaintTool = PaintTool::RaiseLower;
	int s_BrushShape = 0;
	float s_BrushSize = 25.0f;
	float s_Opacity = 30.0f;            // %
	float s_TargetHeight = 10.0f;       // Set Height (미터, 지형 기준)
	int s_SelectedLayer = 0;
	bool s_Painting = false;

	// ---- Undo: 한 획(누르고 뗄 때까지)을 한 단계로. 바뀐 사각형 영역만 저장한다 ----
	struct Rect { int x0 = INT_MAX, z0 = INT_MAX, x1 = -1, z1 = -1; bool Valid() const { return x1 >= x0 && z1 >= z0; } };
	std::shared_ptr<TerrainData> s_StrokeData;
	std::vector<float> s_StrokeHeights;
	std::vector<uint8_t> s_StrokeControl;
	Rect s_HeightRect, s_ControlRect;
	std::string s_StrokeName;

	void Grow(Rect& r, int x0, int z0, int x1, int z1)
	{
		r.x0 = (std::min)(r.x0, x0); r.z0 = (std::min)(r.z0, z0);
		r.x1 = (std::max)(r.x1, x1); r.z1 = (std::max)(r.z1, z1);
	}

	template <typename T>
	std::vector<T> CropOf(const std::vector<T>& src, int width, int channels, const Rect& r)
	{
		std::vector<T> out;
		out.reserve((size_t)(r.x1 - r.x0 + 1) * (r.z1 - r.z0 + 1) * channels);
		for (int z = r.z0; z <= r.z1; ++z)
			out.insert(out.end(), src.begin() + ((size_t)z * width + r.x0) * channels, src.begin() + ((size_t)z * width + r.x1 + 1) * channels);
		return out;
	}

	template <typename T>
	void PasteCrop(std::vector<T>& dst, int width, int channels, const Rect& r, const std::vector<T>& crop)
	{
		const size_t row = (size_t)(r.x1 - r.x0 + 1) * channels;
		for (int z = r.z0; z <= r.z1; ++z)
			std::copy(crop.begin() + (size_t)(z - r.z0) * row, crop.begin() + (size_t)(z - r.z0 + 1) * row, dst.begin() + ((size_t)z * width + r.x0) * channels);
	}

	void BeginStroke(const std::shared_ptr<TerrainData>& data, const char* name)
	{
		s_StrokeData = data;
		s_StrokeHeights = data->Heights;
		s_StrokeControl = data->Control;
		s_HeightRect = Rect();
		s_ControlRect = Rect();
		s_StrokeName = name;
		EditorLog::Write("Terrain", "stroke begin '%s' on %s (brush %d, size %.1f, opacity %.0f)", name, data->Name().c_str(), s_BrushShape, s_BrushSize, s_Opacity);
	}

	void EndStroke()
	{
		auto data = s_StrokeData;
		s_StrokeData = nullptr;
		if (data == nullptr)
			return;
		EditorLog::Write("Terrain", "stroke end '%s' heights rect (%d,%d)-(%d,%d) control rect (%d,%d)-(%d,%d)", s_StrokeName.c_str(),
			s_HeightRect.x0, s_HeightRect.z0, s_HeightRect.x1, s_HeightRect.z1, s_ControlRect.x0, s_ControlRect.z0, s_ControlRect.x1, s_ControlRect.z1);
		const int res = data->HeightmapResolution, cres = data->ControlResolution;
		if (s_HeightRect.Valid() && s_StrokeHeights.size() == data->Heights.size())
		{
			const Rect r = s_HeightRect;
			auto before = std::make_shared<std::vector<float>>(CropOf(s_StrokeHeights, res, 1, r));
			auto after = std::make_shared<std::vector<float>>(CropOf(data->Heights, res, 1, r));
			Undo::Record rec;
			rec.Name = s_StrokeName;
			rec.Bytes = (before->size() + after->size()) * sizeof(float);
			auto apply = [data, r, res](const std::vector<float>& crop) {
				if (data->HeightmapResolution != res) return;
				PasteCrop(data->Heights, res, 1, r, crop);
				data->OnHeightsChanged(r.x0, r.z0, r.x1, r.z1);
			};
			rec.UndoAction = [apply, before]() { apply(*before); };
			rec.RedoAction = [apply, after]() { apply(*after); };
			Undo::Push(std::move(rec));
		}
		if (s_ControlRect.Valid() && s_StrokeControl.size() == data->Control.size())
		{
			const Rect r = s_ControlRect;
			auto before = std::make_shared<std::vector<uint8_t>>(CropOf(s_StrokeControl, cres, 4, r));
			auto after = std::make_shared<std::vector<uint8_t>>(CropOf(data->Control, cres, 4, r));
			Undo::Record rec;
			rec.Name = s_StrokeName;
			rec.Bytes = before->size() + after->size();
			auto apply = [data, r, cres](const std::vector<uint8_t>& crop) {
				if (data->ControlResolution != cres) return;
				PasteCrop(data->Control, cres, 4, r, crop);
				data->OnControlChanged(r.x0, r.z0, r.x1, r.z1);
			};
			rec.UndoAction = [apply, before]() { apply(*before); };
			rec.RedoAction = [apply, after]() { apply(*after); };
			Undo::Push(std::move(rec));
		}
		s_StrokeHeights.clear();
		s_StrokeControl.clear();
	}

	// 전체 상태 기록 (Flatten All, 레이어 추가/제거, 크기/해상도 변경)
	struct TerrainState
	{
		int Res = 0;
		Vec3 Size;
		std::vector<float> Heights;
		std::vector<uint8_t> Control;
		std::vector<std::shared_ptr<TerrainLayer>> Layers;
	};
	std::shared_ptr<TerrainState> Snapshot(const TerrainData& d)
	{
		auto s = std::make_shared<TerrainState>();
		s->Res = d.HeightmapResolution; s->Size = d.Size; s->Heights = d.Heights; s->Control = d.Control; s->Layers = d.Layers;
		return s;
	}
	void PushFullRecord(const char* name, const std::shared_ptr<TerrainData>& data, const std::shared_ptr<TerrainState>& before)
	{
		auto after = Snapshot(*data);
		Undo::Record rec;
		rec.Name = name;
		rec.Bytes = (before->Heights.size() + after->Heights.size()) * sizeof(float) + before->Control.size() + after->Control.size();
		auto apply = [data](const TerrainState& s) { data->RestoreState(s.Res, s.Size, s.Heights, s.Control, s.Layers); };
		rec.UndoAction = [apply, before]() { apply(*before); };
		rec.RedoAction = [apply, after]() { apply(*after); };
		Undo::Push(std::move(rec));
	}

	const ImU32 kText = IM_COL32(210, 210, 210, 255);
	const ImU32 kTextDim = IM_COL32(150, 150, 150, 255);
	const ImU32 kBoxBg = IM_COL32(45, 45, 45, 255);
	const ImU32 kBorder = IM_COL32(30, 30, 30, 255);
	const ImU32 kSelected = IM_COL32(58, 114, 176, 255);

	const char* kBrushNames[] = { "Soft Round", "Hard Round", "Linear", "Noise" };

	float Hash(int x, int z)
	{
		uint32_t h = (uint32_t)x * 374761393u + (uint32_t)z * 668265263u;
		h = (h ^ (h >> 13)) * 1274126177u;
		return ((h ^ (h >> 16)) & 0xFFFF) / 65535.0f;
	}

	// 브러시 세기 (r = 중심에서의 거리 / 반지름, 0~1)
	float Falloff(int shape, float r, int x, int z)
	{
		if (r >= 1.0f)
			return 0.0f;
		switch (shape)
		{
		case 1: return r < 0.85f ? 1.0f : (1.0f - r) / 0.15f;                                   // Hard Round
		case 2: return 1.0f - r;                                                                 // Linear
		case 3: return (0.5f + 0.5f * cosf(XM_PI * r)) * (0.35f + 0.65f * Hash(x, z));          // Noise
		default: return 0.5f + 0.5f * cosf(XM_PI * r);                                           // Soft Round
		}
	}

	const char* PaintToolName(PaintTool t)
	{
		switch (t)
		{
		case PaintTool::RaiseLower: return "Raise or Lower Terrain";
		case PaintTool::PaintHoles: return "Paint Holes";
		case PaintTool::PaintTexture: return "Paint Texture";
		case PaintTool::SetHeight: return "Set Height";
		case PaintTool::SmoothHeight: return "Smooth Height";
		default: return "Stamp Terrain";
		}
	}

	const char* PaintToolHelp(PaintTool t)
	{
		switch (t)
		{
		case PaintTool::RaiseLower: return "Left click to raise.\nHold shift and left click to lower.";
		case PaintTool::PaintTexture: return "Paints the selected material layer onto the terrain texture.";
		case PaintTool::SetHeight: return "Left click to set the height.\nHold shift and left click to sample the target height.";
		case PaintTool::SmoothHeight: return "Click to average out the terrain height.";
		default: return "This tool is not supported yet.";
		}
	}

	bool PaintToolSupported(PaintTool t)
	{
		return t == PaintTool::RaiseLower || t == PaintTool::PaintTexture || t == PaintTool::SetHeight || t == PaintTool::SmoothHeight;
	}

	std::vector<std::string> ScanFiles(const wchar_t* ext)
	{
		std::vector<std::string> out;
		auto scan = [&](const std::wstring& root, const std::wstring& prefix) {
			std::error_code ec;
			if (!std::filesystem::exists(root, ec))
				return;
			for (const auto& e : std::filesystem::recursive_directory_iterator(root, ec))
				if (e.is_regular_file() && e.path().extension() == ext)
					out.push_back(wstring_to_string(prefix + std::filesystem::relative(e.path(), root, ec).wstring()));
		};
		scan(PathManager::GetI()->GetMovePathW(L"Assets\\"), L"Assets\\");
		scan(PathManager::GetI()->GetMovePathW(L"Resources\\Packages\\"), L"Resources\\Packages\\");
		return out;
	}

	std::string UniqueAssetPath(const std::string& baseName, const char* ext)
	{
		std::string base = "Assets\\" + baseName;
		std::string path = base + ext;
		for (int n = 1; std::filesystem::exists(PathManager::GetI()->GetMovePathW(string_to_wstring(path))); ++n)
			path = base + " " + std::to_string(n) + ext;
		return path;
	}

	// ---- 레이어 추가/제거 (컨트롤 맵 채널을 함께 옮긴다) ----
	void AddLayer(TerrainData& data, const std::string& layerPath)
	{
		if ((int)data.Layers.size() >= TerrainData::kMaxLayers)
			return;
		auto layer = TerrainLayer::Load(layerPath);
		if (layer == nullptr)
			return;
		data.Layers.push_back(layer);
		if (data.Layers.size() == 1)
			for (size_t i = 0; i < data.Control.size(); i += 4)
				data.Control[i] = 255, data.Control[i + 1] = data.Control[i + 2] = data.Control[i + 3] = 0;
		data.OnControlChanged(0, 0, data.ControlResolution - 1, data.ControlResolution - 1);
	}

	void RemoveLayer(TerrainData& data, int index)
	{
		if (index < 0 || index >= (int)data.Layers.size())
			return;
		data.Layers.erase(data.Layers.begin() + index);
		for (size_t i = 0; i < data.Control.size(); i += 4)
		{
			for (int c = index; c < 3; ++c)
				data.Control[i + c] = data.Control[i + c + 1];
			data.Control[i + 3] = 0;
		}
		data.OnControlChanged(0, 0, data.ControlResolution - 1, data.ControlResolution - 1);
	}

	// ================================================================ Paint Trees (Unity 의 나무 칠하기)
	void DescriptionBox(const char* text);
	int s_TreeProto = 0;
	float s_TreeDensity = 50.0f;          // 0~100: 나무 사이 최소 간격 8 m → 1.5 m
	float s_TreeHeightMin = 0.8f, s_TreeHeightMax = 1.25f;
	bool s_LockWidth = true;
	float s_TreeWidthMin = 0.8f, s_TreeWidthMax = 1.25f;
	float s_ColorVariation = 0.4f;
	bool s_RandomRotation = true;
	int s_MassPlaceCount = 1000;
	bool s_TreeStroke = false;
	Vec3 s_LastTreePos;
	std::vector<TerrainTreeInstance> s_TreeStrokeBefore;
	std::mt19937 s_TreeRng(1234567u);

	float Rand01() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(s_TreeRng); }
	float TreeSpacing() { return 8.0f + (1.5f - 8.0f) * std::clamp(s_TreeDensity, 0.0f, 100.0f) / 100.0f; }

	TerrainTreeInstance MakeTree(float x, float z, int proto)
	{
		TerrainTreeInstance t;
		t.X = x;
		t.Z = z;
		t.HeightScale = s_TreeHeightMin + (s_TreeHeightMax - s_TreeHeightMin) * Rand01();
		t.WidthScale = s_LockWidth ? t.HeightScale : s_TreeWidthMin + (s_TreeWidthMax - s_TreeWidthMin) * Rand01();
		t.Rotation = s_RandomRotation ? Rand01() * XM_2PI : 0.0f;
		t.Tint = std::clamp(0.5f + (Rand01() - 0.5f) * s_ColorVariation, 0.0f, 1.0f);
		t.Prototype = proto;
		return t;
	}

	// 가까이(m) 다른 나무가 있으면 true
	bool TreeNearby(const TerrainData& data, float lx, float lz, float spacing)
	{
		const float s2 = spacing * spacing;
		for (const TerrainTreeInstance& t : data.TreeInstances)
		{
			const float dx = t.X * data.Size.x - lx, dz = t.Z * data.Size.z - lz;
			if (dx * dx + dz * dz < s2)
				return true;
		}
		return false;
	}

	void PushTreeUndo(const char* name, const std::shared_ptr<TerrainData>& data, std::vector<TerrainTreeInstance> before)
	{
		auto b = std::make_shared<std::vector<TerrainTreeInstance>>(std::move(before));
		auto a = std::make_shared<std::vector<TerrainTreeInstance>>(data->TreeInstances);
		Undo::Record rec;
		rec.Name = name;
		rec.Bytes = (b->size() + a->size()) * sizeof(TerrainTreeInstance);
		rec.UndoAction = [data, b]() { data->TreeInstances = *b; data->OnTreesChanged(); };
		rec.RedoAction = [data, a]() { data->TreeInstances = *a; data->OnTreesChanged(); };
		Undo::Push(std::move(rec));
	}

	// 브러시 한 번: 칠하기(빈 곳에 간격을 지키며 몇 그루) / Shift = 지우기 / Ctrl = 고른 종류만 지우기
	void PaintTreesAt(Terrain* terrain, const Vec3& worldPos, bool erase, bool selectedOnly)
	{
		auto data = terrain->GetTerrainData();
		if (!data)
			return;
		const Vec3 local = worldPos - terrain->GetPosition();
		const float r = s_BrushSize * 0.5f;
		if (erase)
		{
			const size_t before = data->TreeInstances.size();
			auto& v = data->TreeInstances;
			v.erase(std::remove_if(v.begin(), v.end(), [&](const TerrainTreeInstance& t) {
				const float dx = t.X * data->Size.x - local.x, dz = t.Z * data->Size.z - local.z;
				return dx * dx + dz * dz <= r * r && (!selectedOnly || t.Prototype == s_TreeProto);
			}), v.end());
			if (v.size() != before)
				data->OnTreesChanged();
			return;
		}
		if (data->TreePrototypes.empty())
			return;
		const int proto = std::clamp(s_TreeProto, 0, (int)data->TreePrototypes.size() - 1);
		const float spacing = TreeSpacing();
		const float target = XM_PI * r * r / (spacing * spacing) * 0.55f;   // 브러시를 채울 대략의 수
		const int perStep = (std::max)(1, (int)ceilf(target * 0.3f));
		int placed = 0;
		for (int attempt = 0; attempt < perStep * 6 && placed < perStep; ++attempt)
		{
			const float a = Rand01() * XM_2PI, d = sqrtf(Rand01()) * r;
			const float lx = local.x + cosf(a) * d, lz = local.z + sinf(a) * d;
			if (lx < 0 || lz < 0 || lx > data->Size.x || lz > data->Size.z || TreeNearby(*data, lx, lz, spacing))
				continue;
			data->TreeInstances.push_back(MakeTree(lx / data->Size.x, lz / data->Size.z, proto));
			++placed;
		}
		if (placed > 0)
			data->OnTreesChanged();
	}

	void DrawTreeTool(Terrain* terrain, const std::shared_ptr<TerrainData>& data)
	{
		using namespace UnityGUI;
		DescriptionBox("Click to paint trees.\nHold shift and click to erase trees.\nHold Ctrl and click to erase only trees of the selected type.");

		// ---- 나무 종류 (썸네일 = 구운 임포스터 앞면)
		Label("Trees", 0, true);
		{
			ImDrawList* dl = ImGui::GetWindowDrawList();
			const ImVec2 p = ImGui::GetCursorScreenPos();
			const float w = ImGui::GetContentRegionAvail().x;
			const float cell = 64.0f;
			const int perRow = (std::max)(1, (int)((w - 28) / (cell + 6)));
			const int count = (int)data->TreePrototypes.size();
			const int rows = (std::max)(1, (count + perRow - 1) / perRow);
			const ImVec2 box0(p.x + 14, p.y + 2), box1(p.x + w - 10, p.y + rows * (cell + 20) + 10);
			dl->AddRectFilled(box0, box1, kBoxBg, 3.0f);
			dl->AddRect(box0, box1, kBorder, 3.0f);
			int presetCount = 0;
			const char* const* presetNames = TreeParams::PresetNames(presetCount);
			for (int i = 0; i < count; ++i)
			{
				const ImVec2 a(box0.x + 5 + (i % perRow) * (cell + 6), box0.y + 4 + (i / perRow) * (cell + 20)), b(a.x + cell, a.y + cell);
				ImGui::SetCursorScreenPos(a);
				ImGui::PushID(i);
				if (ImGui::InvisibleButton("##proto", ImVec2(cell, cell + 16)))
					s_TreeProto = i;
				ImGui::PopID();
				dl->AddRectFilled(a, b, IM_COL32(30, 32, 34, 255));
				ImVec2 uv0, uv1;
				if (ImTextureID tex = TreeRenderer::Thumbnail(data->TreePrototypes[i], uv0, uv1))
					dl->AddImage(tex, a, b, uv0, uv1);
				const TreeDesc& d = data->TreePrototypes[i];
				char name[48];
				snprintf(name, sizeof(name), "%s %d", d.Preset >= 0 && d.Preset < presetCount ? presetNames[d.Preset] : "Tree", d.Params.Seed);
				dl->AddText(ImVec2(a.x + 2, b.y + 1), kText, name);
				if (i == s_TreeProto)
					dl->AddRect(ImVec2(a.x - 1, a.y - 1), ImVec2(b.x + 1, b.y + 17), kSelected, 0.0f, 0, 2.0f);
			}
			if (count == 0)
				dl->AddText(ImVec2(box0.x + 10, box0.y + 10), kTextDim, "No trees. Add Tree to start painting.");
			ImGui::SetCursorScreenPos(p);
			ImGui::Dummy(ImVec2(w, box1.y - p.y + 4));
		}
		// ---- 추가 / 제거
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 14);
		if (ImGui::Button("Add Tree", ImVec2(100, 0)))
			ImGui::OpenPopup("##addTree");
		ImGui::SameLine();
		const bool hasProto = !data->TreePrototypes.empty();
		ImGui::BeginDisabled(!hasProto);
		if (ImGui::Button("Remove", ImVec2(100, 0)) && hasProto)
		{
			const int idx = std::clamp(s_TreeProto, 0, (int)data->TreePrototypes.size() - 1);
			data->TreePrototypes.erase(data->TreePrototypes.begin() + idx);
			auto& v = data->TreeInstances;
			v.erase(std::remove_if(v.begin(), v.end(), [&](const TerrainTreeInstance& t) { return t.Prototype == idx; }), v.end());
			for (auto& t : v)
				if (t.Prototype > idx)
					--t.Prototype;
			s_TreeProto = (std::max)(0, idx - 1);
			data->OnTreesChanged();
		}
		ImGui::EndDisabled();
		if (ImGui::BeginPopup("##addTree"))
		{
			int presetCount = 0;
			const char* const* names = TreeParams::PresetNames(presetCount);
			for (int i = 0; i < presetCount; ++i)
				if (ImGui::MenuItem(names[i]))
				{
					TerrainEditor::AddTreePrototype(*data, i);
					s_TreeProto = (int)data->TreePrototypes.size() - 1;
				}
			ImGui::EndPopup();
		}

		// ---- 브러시 / 배치 설정
		Spacing(6);
		float maxSize = (std::min)(500.0f, (std::max)(data->Size.x, data->Size.z));
		Slider("Brush Size", &s_BrushSize, 1.0f, maxSize);
		s_BrushSize = std::clamp(s_BrushSize, 1.0f, maxSize);
		Slider("Tree Density", &s_TreeDensity, 0.0f, 100.0f);
		Slider("Tree Height Min", &s_TreeHeightMin, 0.1f, 3.0f);
		Slider("Tree Height Max", &s_TreeHeightMax, 0.1f, 3.0f);
		s_TreeHeightMax = (std::max)(s_TreeHeightMax, s_TreeHeightMin);
		Toggle("Lock Width to Height", &s_LockWidth);
		if (!s_LockWidth)
		{
			Slider("Tree Width Min", &s_TreeWidthMin, 0.1f, 3.0f, 1);
			Slider("Tree Width Max", &s_TreeWidthMax, 0.1f, 3.0f, 1);
			s_TreeWidthMax = (std::max)(s_TreeWidthMax, s_TreeWidthMin);
		}
		Slider("Color Variation", &s_ColorVariation, 0.0f, 1.0f);
		Toggle("Random Tree Rotation", &s_RandomRotation);

		Spacing(4);
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 14);
		if (ImGui::Button("Mass Place Trees", ImVec2(150, 0)) && hasProto)
			ImGui::OpenPopup("##massPlace");
		ImGui::SameLine();
		ImGui::BeginDisabled(data->TreeInstances.empty());
		if (ImGui::Button("Remove All Trees", ImVec2(150, 0)))
		{
			auto before = data->TreeInstances;
			data->TreeInstances.clear();
			data->OnTreesChanged();
			PushTreeUndo("Remove All Trees", data, std::move(before));
		}
		ImGui::EndDisabled();
		if (ImGui::BeginPopup("##massPlace"))
		{
			ImGui::TextUnformatted("Number of Trees");
			ImGui::SetNextItemWidth(160);
			ImGui::InputInt("##count", &s_MassPlaceCount);
			s_MassPlaceCount = std::clamp(s_MassPlaceCount, 1, 200000);
			if (ImGui::Button("Place", ImVec2(160, 0)))
			{
				TerrainEditor::MassPlaceTrees(terrain, s_MassPlaceCount);
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		// ---- 통계
		{
			const TreeRenderer::Stats& st = TreeRenderer::LastStats(true);
			char info[256];
			snprintf(info, sizeof(info), "%d trees on this terrain (%d types).\nScene view: %d visible = %d full mesh, %d mid mesh, %d billboards, %d draw calls.",
				(int)data->TreeInstances.size(), (int)data->TreePrototypes.size(), st.Trees, st.Lod0, st.Lod1, st.Billboards, st.DrawCalls);
			Spacing(4);
			HelpBox(info, false);
		}

		// ---- 고른 나무 종류 설정 (바꾸면 모든 같은 종류가 바로 바뀐다)
		if (hasProto)
		{
			TreeDesc& d = data->TreePrototypes[std::clamp(s_TreeProto, 0, (int)data->TreePrototypes.size() - 1)];
			if (FoldoutPlain("Selected Tree Settings", 0, false))
				if (d.DrawInspector())
					data->OnTreesChanged();
		}
	}

	// ---- 도구 막대 (Unity: 버튼 5개가 붙은 막대) ----
	// ================================================================ Generate (지형 생성기: Base → Stamps → Filters → Materials)
	// 옥타브 막대 (World Creator 의 Base Noise): 위아래로 끌어 옥타브별 세기
	bool OctaveBars(float* values, int count)
	{
		bool changed = false;
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const float w = ImGui::GetContentRegionAvail().x - 28.0f, h = 64.0f;
		const ImVec2 a(p.x + 14.0f, p.y + 2.0f), b(a.x + w, a.y + h);
		dl->AddRectFilled(a, b, IM_COL32(36, 36, 36, 255), 3.0f);
		dl->AddRect(a, b, kBorder, 3.0f);
		const float bw = w / count;
		for (int i = 0; i < count; ++i)
		{
			const ImVec2 c0(a.x + i * bw + 3.0f, a.y + 3.0f), c1(a.x + (i + 1) * bw - 3.0f, b.y - 3.0f);
			ImGui::SetCursorScreenPos(c0);
			ImGui::PushID(i);
			ImGui::InvisibleButton("##oct", ImVec2(c1.x - c0.x, c1.y - c0.y));
			if (ImGui::IsItemActive())
			{
				values[i] = std::clamp((c1.y - ImGui::GetIO().MousePos.y) / (c1.y - c0.y), 0.0f, 1.0f);
				changed = true;
			}
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Octave %d: %.2f", i + 1, values[i]);
			ImGui::PopID();
			const float v = std::clamp(values[i], 0.0f, 1.0f);
			dl->AddRectFilled(ImVec2(c0.x, c1.y - (c1.y - c0.y) * v), c1, IM_COL32(86, 120, 220, 255), 2.0f);
		}
		ImGui::SetCursorScreenPos(ImVec2(p.x, b.y + 4.0f));
		ImGui::Dummy(ImVec2(w, 1.0f));
		return changed;
	}

	// 작은 아이콘 버튼
	bool SmallButton(const char* id, const char* glyph, const char* tip)
	{
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 1));
		const bool clicked = ImGui::Button((std::string(glyph) + id).c_str());
		ImGui::PopStyleVar();
		if (tip && ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", tip);
		return clicked;
	}

	void AddStampToScene(int shape)
	{
		Scene* scene = SceneManager::GetI()->GetCurrentScene();
		if (scene == nullptr)
			return;
		GameObject* obj = GameObjectFactory::CreateTerrainStamp(shape);
		scene->AddRootGameObject(obj);
		SelectionManager::SetSelectedGameObject(obj);
		Undo::SetActionName("Create Terrain Stamp");
		Undo::RequestCheck();
	}

	std::string s_BiomeMenu;   // 프리셋 썸네일 오른쪽 클릭 메뉴의 대상

	void AddSplineToScene(int mode)
	{
		Scene* scene = SceneManager::GetI()->GetCurrentScene();
		if (scene == nullptr)
			return;
		GameObject* obj = GameObjectFactory::CreateTerrainSpline(mode);
		scene->AddRootGameObject(obj);
		SelectionManager::SetSelectedGameObject(obj);
		Undo::SetActionName("Create Terrain Spline");
		Undo::RequestCheck();
	}

	void AddBiomeToScene(const std::string& preset)
	{
		Scene* scene = SceneManager::GetI()->GetCurrentScene();
		if (scene == nullptr)
			return;
		GameObject* obj = GameObjectFactory::CreateTerrainBiome(preset);
		scene->AddRootGameObject(obj);
		SelectionManager::SetSelectedGameObject(obj);
		Undo::SetActionName("Create Terrain Biome");
		Undo::RequestCheck();
	}

	void DrawGenerateTool(Terrain* terrain, const std::shared_ptr<TerrainData>& data)
	{
		using namespace UnityGUI;
		TerrainGenSettings& g = data->Generator;
		// Undo: 설정 전체를 JSON 으로 (되돌리면 다시 생성된다)
		std::weak_ptr<TerrainData> weak = data;
		// 설정 + 레이어 목록 (바이옴 프리셋 적용은 레이어도 바꾼다)
		Undo::WatchAsset("terraingen:" + data->Path, "Terrain Generator",
			[weak]() {
				auto d = weak.lock();
				if (!d) return std::string();
				nlohmann::json j = { { "gen", d->Generator.ToJson() }, { "layers", nlohmann::json::array() } };
				for (const auto& l : d->Layers) j["layers"].push_back(l ? l->Path : std::string());
				return j.dump();
			},
			[weak](const std::string& text) {
				auto d = weak.lock();
				const nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
				if (!d || !j.is_object()) return;
				d->Generator.FromJson(j.value("gen", nlohmann::json::object()));
				if (j.contains("layers"))
				{
					d->Layers.clear();
					for (const auto& p : j["layers"])
						if (auto layer = TerrainLayer::Load(p.get<std::string>())) d->Layers.push_back(layer);
				}
				d->Dirty = true;
			});
		const std::string before = g.ToJson().dump();

		DescriptionBox("Builds the terrain from base noise, Terrain Stamps in the scene, a filter stack and material rules. "
			"Non-destructive: move a stamp or change a value and the terrain regenerates.");
		bool enabled = g.Enabled;
		if (Toggle("Enable Generator", &enabled))
		{
			if (enabled && g.Filters.empty() && g.Materials.empty())
			{
				TerrainGenSettings def = TerrainGenSettings::MakeDefault();
				def.Base.MaxHeight = (std::min)(def.Base.MaxHeight, data->Size.y * 0.6f);
				def.Materials[2].HeightMin = def.Base.MaxHeight * 0.75f;
				g = def;
			}
			g.Enabled = enabled;
		}
		if (!g.Enabled)
		{
			HelpBox("When enabled, the generator owns this terrain's heights (hand-painted heights are replaced).\n"
				"To keep them, choose Base > Current Terrain and capture the heights first.", true);
		}
		else
		{
			Toggle("Auto Update", &g.AutoUpdate);
			const TerrainGenerator::Status st = TerrainGenerator::GetStatus(data.get());
			char status[200];
			if (st.Running)
				snprintf(status, sizeof(status), "Generating...");
			else if (st.LastMs > 0.0)
				snprintf(status, sizeof(status), "%s in %.0f ms  (base %.0f, stamps %.0f, filters %.0f, materials %.0f)  -  %d stamps, %d biomes, %d splines",
					st.LastPreview ? "Preview (erosion skipped)" : "Generated", st.LastMs, st.StageMs[0], st.StageMs[1], st.StageMs[2], st.StageMs[3], st.StampCount, st.BiomeCount, st.SplineCount);
			else
				snprintf(status, sizeof(status), "Waiting...");
			HelpBox(status, false);
			if (CenterButton(g.AutoUpdate ? "Regenerate" : "Generate"))
				TerrainGenerator::Regenerate(data);

			// ---- Biome Presets (패키지): 썸네일을 누르면 지형 특성·재질·레이어를 한 번에
			if (Foldout("Biome Presets", 0, true, false))
			{
				const auto& presets = TerrainBiomes::List();
				if (presets.empty())
					HelpBox("No presets in Resources/Packages/Terrain/Biomes.", true, 1);
				const float avail = ImGui::GetContentRegionAvail().x - 20.0f;
				const float cellW = 96.0f, cellH = 116.0f;
				const int perRow = (std::max)(1, (int)(avail / (cellW + 6.0f)));
				const ImVec2 start = ImGui::GetCursorScreenPos();
				ImDrawList* dl = ImGui::GetWindowDrawList();
				for (int i = 0; i < (int)presets.size(); ++i)
				{
					const auto& p = presets[i];
					const ImVec2 a(start.x + 14.0f + (i % perRow) * (cellW + 6.0f), start.y + 4.0f + (i / perRow) * (cellH + 6.0f));
					ImGui::SetCursorScreenPos(a);
					ImGui::PushID(i);
					const bool clicked = ImGui::InvisibleButton("##biome", ImVec2(cellW, cellH));
					const bool hovered = ImGui::IsItemHovered();
					ImGui::PopID();
					const bool current = g.BiomePreset == p.Name;
					dl->AddRectFilled(a, ImVec2(a.x + cellW, a.y + cellH), current ? IM_COL32(58, 86, 120, 255) : (hovered ? IM_COL32(70, 70, 70, 255) : IM_COL32(48, 48, 48, 255)), 4.0f);
					if (GfxShaderResourceView* thumb = TerrainBiomes::Thumbnail(p))
						dl->AddImage((ImTextureID)thumb, ImVec2(a.x + 4, a.y + 4), ImVec2(a.x + cellW - 4, a.y + cellW - 4));
					else
						dl->AddText(ImVec2(a.x + 18, a.y + 38), IM_COL32(150, 150, 150, 255), "...");
					const ImVec2 ts = ImGui::CalcTextSize(p.Name.c_str());
					ImGui::PushClipRect(a, ImVec2(a.x + cellW, a.y + cellH), true);
					dl->AddText(ImVec2(a.x + (std::max)(4.0f, (cellW - ts.x) * 0.5f), a.y + cellW + 1), IM_COL32(220, 220, 220, 255), p.Name.c_str());
					ImGui::PopClipRect();
					if (hovered && !p.Description.empty())
						ImGui::SetTooltip("%s\n\n%s", p.Name.c_str(), p.Description.c_str());
					if (clicked)
					{
						TerrainBiomes::Apply(*data, p);
						Undo::SetActionName("Apply Biome " + p.Name);
					}
					if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
					{
						s_BiomeMenu = p.Name;
						ImGui::OpenPopup("##biomemenu");
					}
				}
				if (ImGui::BeginPopup("##biomemenu"))
				{
					if (ImGui::MenuItem("Apply to Whole Terrain"))
						if (const TerrainBiomes::Preset* p = TerrainBiomes::Find(s_BiomeMenu))
						{
							TerrainBiomes::Apply(*data, *p);
							Undo::SetActionName("Apply Biome " + p->Name);
						}
					if (ImGui::MenuItem("Add as Biome Area"))
						AddBiomeToScene(s_BiomeMenu);
					ImGui::EndPopup();
				}
				const int rows = ((int)presets.size() + perRow - 1) / perRow;
				ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + 8.0f + rows * (cellH + 6.0f)));
				ImGui::Dummy(ImVec2(avail, 1.0f));
				HelpBox("Click = apply to the whole terrain. Right-click = add as a biome area.", false, 1);
			}

			// ---- Base
			if (Foldout("Base", 0, true, false))
			{
				static const char* kTypes[] = { "Flat", "Classic", "Ridged", "Billow", "Eroded", "Current Terrain", "Dunes" };
				int type = (int)g.Base.NoiseType;
				if (Dropdown("Noise Type", &type, kTypes, 7, 1))
					g.Base.NoiseType = (TerrainGenBase::Type)type;
				if (g.Base.NoiseType == TerrainGenBase::Type::CurrentTerrain)
				{
					HelpBox(data->BaseSnapshot.size() == data->Heights.size() ? "Using captured heights as the base." : "No heights captured yet.", false, 1);
					if (CenterButton("Capture Current Heights"))
					{
						data->BaseSnapshot = data->Heights;
						data->Dirty = true;
					}
				}
				else if (g.Base.NoiseType != TerrainGenBase::Type::Flat)
				{
					Int("Seed", &g.Base.Seed, 1);
					if (CenterButton("New Seed"))
						g.Base.Seed = (g.Base.Seed * 1103515245 + 12345) & 0x7fff;
					Slider("Scale (m)", &g.Base.Scale, 50.0f, 4000.0f, 1);
					Label("Octaves", 1);
					OctaveBars(g.Base.Octaves, TerrainGenBase::kOctaves);
					Slider("Shape Power", &g.Base.ShapePower, 0.3f, 4.0f, 1);
					if (g.Base.NoiseType == TerrainGenBase::Type::Dunes)
						Slider("Wind Angle", &g.Base.WindAngle, 0.0f, 360.0f, 1);
					Slider("Offset X", &g.Base.OffsetX, -5000.0f, 5000.0f, 1);
					Slider("Offset Z", &g.Base.OffsetZ, -5000.0f, 5000.0f, 1);
				}
				if (g.Base.NoiseType != TerrainGenBase::Type::CurrentTerrain)
				{
					Slider("Min Height", &g.Base.MinHeight, 0.0f, data->Size.y, 1);
					if (g.Base.NoiseType != TerrainGenBase::Type::Flat)
						Slider("Max Height", &g.Base.MaxHeight, 0.0f, data->Size.y, 1);
				}
			}

			// ---- Stamps (씬 오브젝트)
			if (Foldout("Stamps", 0, true, false))
			{
				int shown = 0;
				for (TerrainStamp* s : TerrainStamp::All())
				{
					GameObject* go = s->GetGameObject();
					if (go == nullptr)
						continue;
					char label[160];
					snprintf(label, sizeof(label), "%s  -  %s %s%s", go->GetName().c_str(), TerrainStamp::ShapeName(s->StampShape),
						TerrainStamp::OperationName(s->Op), s->IsActiveStamp() ? "" : "  (off)");
					ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 18.0f);
					ImGui::PushID(s);
					if (ImGui::Selectable(label, SelectionManager::GetSelectedGameObject() == go))
						SelectionManager::SetSelectedGameObject(go);
					ImGui::PopID();
					++shown;
				}
				if (shown == 0)
					HelpBox("No stamps in the scene. Add one below or from GameObject > 3D Object > Terrain Stamp.", false, 1);
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 18.0f);
				if (ImGui::Button("Add Stamp", ImVec2(120, 0)))
					ImGui::OpenPopup("##addstamp");
				if (ImGui::BeginPopup("##addstamp"))
				{
					for (int s = 0; s < (int)TerrainStamp::Shape::Count; ++s)
						if (ImGui::MenuItem(TerrainStamp::ShapeName((TerrainStamp::Shape)s)))
							AddStampToScene(s);
					ImGui::EndPopup();
				}
			}

			// ---- Splines (씬 오브젝트: 도로·협곡·능선)
			if (Foldout("Splines", 0, true, false))
			{
				int shown = 0;
				for (TerrainSpline* s : TerrainSpline::All())
				{
					GameObject* go = s->GetGameObject();
					if (go == nullptr)
						continue;
					char label[160];
					snprintf(label, sizeof(label), "%s  -  %s, %d points%s", go->GetName().c_str(), TerrainSpline::ModeName(s->SplineMode),
						(int)s->Points.size(), s->IsActiveSpline() ? "" : "  (off)");
					ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 18.0f);
					ImGui::PushID(s);
					if (ImGui::Selectable(label, SelectionManager::GetSelectedGameObject() == go))
						SelectionManager::SetSelectedGameObject(go);
					ImGui::PopID();
					++shown;
				}
				if (shown == 0)
					HelpBox("No splines. A spline flattens a road, carves a canyon or raises a ridge along a curve.", false, 1);
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 18.0f);
				if (ImGui::Button("Add Spline", ImVec2(120, 0)))
					ImGui::OpenPopup("##addspline");
				if (ImGui::BeginPopup("##addspline"))
				{
					for (int m = 0; m < (int)TerrainSpline::Mode::Count; ++m)
						if (ImGui::MenuItem(TerrainSpline::ModeName((TerrainSpline::Mode)m)))
							AddSplineToScene(m);
					ImGui::EndPopup();
				}
			}

			// ---- Biome Areas (씬 오브젝트: 영역마다 다른 프리셋)
			if (Foldout("Biome Areas", 0, true, false))
			{
				int shown = 0;
				for (TerrainBiome* b : TerrainBiome::All())
				{
					GameObject* go = b->GetGameObject();
					if (go == nullptr)
						continue;
					char label[200];
					snprintf(label, sizeof(label), "%s  -  %s%s%s", go->GetName().c_str(), b->Preset.c_str(),
						TerrainBiomes::Find(b->Preset) ? "" : "  (missing)", b->IsActiveBiome() ? "" : "  (off)");
					ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 18.0f);
					ImGui::PushID(b);
					if (ImGui::Selectable(label, SelectionManager::GetSelectedGameObject() == go))
						SelectionManager::SetSelectedGameObject(go);
					ImGui::PopID();
					++shown;
				}
				if (shown == 0)
					HelpBox("No biome areas. Each area uses a biome preset's shape and materials inside it and blends at the edge.", false, 1);
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 18.0f);
				if (ImGui::Button("Add Biome Area", ImVec2(140, 0)))
					ImGui::OpenPopup("##addbiome");
				if (ImGui::BeginPopup("##addbiome"))
				{
					for (const auto& p : TerrainBiomes::List())
						if (ImGui::MenuItem(p.Name.c_str()))
							AddBiomeToScene(p.Name);
					ImGui::EndPopup();
				}
			}

			// ---- Filters (위에서 아래로)
			if (Foldout("Filters", 0, true, false))
			{
				int remove = -1, up = -1, down = -1;
				for (int i = 0; i < (int)g.Filters.size(); ++i)
				{
					TerrainGenFilter& f = g.Filters[i];
					ImGui::PushID(i);
					ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 14.0f);
					ImGui::Checkbox("##on", &f.Enabled);
					ImGui::SameLine();
					ImGui::TextUnformatted(TerrainGenFilter::Name(f.FilterType));
					ImGui::SameLine();
					ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - 78.0f);
					if (SmallButton("##up", ICON_FA_ARROW_UP, "Move up")) up = i;
					ImGui::SameLine();
					if (SmallButton("##down", ICON_FA_ARROW_DOWN, "Move down")) down = i;
					ImGui::SameLine();
					if (SmallButton("##del", ICON_FA_XMARK, "Remove")) remove = i;
					if (f.Enabled)
					{
						Slider("Strength", &f.Strength, 0.0f, 1.0f, 2);
						const TerrainGenFilter::ParamInfo* info = TerrainGenFilter::Params(f.FilterType);
						for (int k = 0; k < 6; ++k)
						{
							if (info[k].Label == nullptr)
								continue;
							ImGui::PushID(k);
							float maxV = info[k].Max;
							if (f.FilterType == TerrainGenFilter::Type::Terrace && (k == 2 || k == 3))
								maxV = data->Size.y;
							Slider(info[k].Label, &f.P[k], info[k].Min, maxV, 2);
							if (info[k].Integer)
								f.P[k] = roundf(f.P[k]);
							ImGui::PopID();
						}
						if (TerrainGenFilter::IsHeavy(f.FilterType))
							HelpBox("Skipped while dragging (preview), applied when you release.", false, 2);
					}
					ImGui::PopID();
					Spacing(2);
				}
				if (remove >= 0) g.Filters.erase(g.Filters.begin() + remove);
				if (up > 0) std::swap(g.Filters[up], g.Filters[up - 1]);
				if (down >= 0 && down + 1 < (int)g.Filters.size()) std::swap(g.Filters[down], g.Filters[down + 1]);
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 18.0f);
				if (ImGui::Button("Add Filter", ImVec2(120, 0)))
					ImGui::OpenPopup("##addfilter");
				if (ImGui::BeginPopup("##addfilter"))
				{
					for (int t = 0; t < (int)TerrainGenFilter::Type::Count; ++t)
						if (ImGui::MenuItem(TerrainGenFilter::Name((TerrainGenFilter::Type)t)))
							g.Filters.push_back(TerrainGenFilter::Make((TerrainGenFilter::Type)t));
					ImGui::EndPopup();
				}
			}

			// ---- Materials (높이·경사·퇴적으로 레이어 칠하기)
			if (Foldout("Materials", 0, true, false))
			{
				Toggle("Paint Materials", &g.PaintMaterials, 1);
				if (data->Layers.size() < 2)
					HelpBox("Add terrain layers first (Paint Terrain > Paint Texture). Layer 0 is the base; rules paint layers 1-3.", true, 1);
				std::vector<std::string> names;
				for (size_t l = 0; l < data->Layers.size(); ++l)
					names.push_back(std::to_string(l) + ": " + (data->Layers[l] ? data->Layers[l]->Name() : std::string("(none)")));
				std::vector<const char*> namePtrs;
				for (const auto& n : names)
					namePtrs.push_back(n.c_str());
				int remove = -1;
				for (int i = 0; i < (int)g.Materials.size(); ++i)
				{
					TerrainGenMaterialRule& r = g.Materials[i];
					ImGui::PushID(1000 + i);
					ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 14.0f);
					ImGui::Checkbox("##on", &r.Enabled);
					ImGui::SameLine();
					if (r.Name.empty())
						ImGui::Text("Rule %d", i + 1);
					else
						ImGui::Text("%d. %s", i + 1, r.Name.c_str());
					ImGui::SameLine();
					ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - 30.0f);
					if (SmallButton("##del", ICON_FA_XMARK, "Remove")) remove = i;
					if (r.Enabled)
					{
						if (!namePtrs.empty())
						{
							r.Layer = std::clamp(r.Layer, 0, (int)namePtrs.size() - 1);
							Dropdown("Layer", &r.Layer, namePtrs.data(), (int)namePtrs.size(), 2);
						}
						else
							Int("Layer", &r.Layer, 2);
						Slider("Height Min", &r.HeightMin, 0.0f, data->Size.y, 2);
						Slider("Height Max", &r.HeightMax, 0.0f, data->Size.y + 1.0f, 2);
						Slider("Height Blend", &r.HeightBlend, 0.1f, 200.0f, 2);
						Slider("Slope Min", &r.SlopeMin, 0.0f, 90.0f, 2);
						Slider("Slope Max", &r.SlopeMax, 0.0f, 90.0f, 2);
						Slider("Slope Blend", &r.SlopeBlend, 0.1f, 30.0f, 2);
						Slider("Sediment", &r.Sediment, 0.0f, 1.0f, 2);
						Slider("Flow", &r.Flow, 0.0f, 1.0f, 2);
						Slider("Cavity", &r.Cavity, -1.0f, 1.0f, 2);
						Slider("Noise", &r.Noise, 0.0f, 1.0f, 2);
						Slider("Opacity", &r.Opacity, 0.0f, 1.0f, 2);
						// 색: Texture = 레이어 텍스처 색, Color = 단색, Gradient = 값(높이·경사 …)으로 읽는 색 띠
						static const char* kModes[] = { "Texture", "Color", "Gradient" };
						int mode = (int)r.Mode;
						if (Dropdown("Color", &mode, kModes, 3, 2))
							r.Mode = (TerrainGenMaterialRule::ColorMode)mode;
						if (r.Mode == TerrainGenMaterialRule::ColorMode::Color)
						{
							float rgba[4] = { r.Color[0], r.Color[1], r.Color[2], 1.0f };
							if (UnityGUI::Color("Tint", rgba, 3))
								for (int c = 0; c < 3; ++c) r.Color[c] = rgba[c];
						}
						else if (r.Mode == TerrainGenMaterialRule::ColorMode::Gradient)
						{
							static const char* kInputs[] = { "Height", "Slope", "Flow", "Sediment", "Cavity", "Noise" };
							int input = (int)r.GradientInput;
							if (Dropdown("Input", &input, kInputs, 6, 3))
								r.GradientInput = (TerrainGenMaterialRule::Input)input;
							if (r.Gradient.empty())
								r.Gradient = { { 0.0f, { 0.25f, 0.22f, 0.18f } }, { 1.0f, { 0.8f, 0.78f, 0.74f } } };
							int removeStop = -1;
							for (int k = 0; k < (int)r.Gradient.size(); ++k)
							{
								ImGui::PushID(k);
								char label[24];
								snprintf(label, sizeof(label), "Stop %d", k + 1);
								float rgba[4] = { r.Gradient[k].Color[0], r.Gradient[k].Color[1], r.Gradient[k].Color[2], 1.0f };
								if (UnityGUI::Color(label, rgba, 3))
									for (int c = 0; c < 3; ++c) r.Gradient[k].Color[c] = rgba[c];
								Slider("Position", &r.Gradient[k].Pos, 0.0f, 1.0f, 4);
								if (r.Gradient.size() > 2)
								{
									ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 60.0f);
									if (ImGui::SmallButton("Remove Stop")) removeStop = k;
								}
								ImGui::PopID();
							}
							if (removeStop >= 0) r.Gradient.erase(r.Gradient.begin() + removeStop);
							ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 60.0f);
							if (ImGui::SmallButton("Add Stop"))
								r.Gradient.push_back({ 1.0f, { 1.0f, 1.0f, 1.0f } });
							std::sort(r.Gradient.begin(), r.Gradient.end(), [](const auto& a, const auto& b) { return a.Pos < b.Pos; });
						}
						if (r.Mode != TerrainGenMaterialRule::ColorMode::Texture)
							Slider("Variation", &r.ColorVariation, 0.0f, 1.0f, 3);
					}
					ImGui::PopID();
					Spacing(2);
				}
				if (remove >= 0) g.Materials.erase(g.Materials.begin() + remove);
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 18.0f);
				if (ImGui::Button("Add Rule", ImVec2(120, 0)))
					g.Materials.push_back(TerrainGenMaterialRule());
			}
		}
		if (g.ToJson().dump() != before)
			data->Dirty = true;   // 저장 대상 (높이는 생성기가 다시 만든다)
	}

	void DrawToolbar()
	{
		static const char* icons[] = { "terrain_neighbor", "terrain_paint", "terrain_trees", "terrain_details", "terrain_settings", nullptr };
		static const char* tips[] = { "Create Neighbor Terrains", "Paint Terrain", "Paint Trees", "Paint Details", "Terrain Settings", "Generate" };
		constexpr int kTools = 6;
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const float w = ImGui::GetContentRegionAvail().x;
		const float bw = 34.0f, bh = 24.0f;
		const float x0 = p.x + (w - bw * kTools) * 0.5f, y0 = p.y + 6.0f;
		for (int i = 0; i < kTools; ++i)
		{
			const ImVec2 a(x0 + i * bw, y0), b(a.x + bw, a.y + bh);
			ImGui::SetCursorScreenPos(a);
			ImGui::PushID(i);
			if (ImGui::InvisibleButton("##ttool", ImVec2(bw, bh)))
				s_Tool = (Tool)i;
			const bool hovered = ImGui::IsItemHovered();
			if (hovered)
				ImGui::SetTooltip("%s", tips[i]);
			ImGui::PopID();
			const bool active = (int)s_Tool == i;
			const ImDrawFlags corners = i == 0 ? ImDrawFlags_RoundCornersLeft : (i == kTools - 1 ? ImDrawFlags_RoundCornersRight : ImDrawFlags_RoundCornersNone);
			dl->AddRectFilled(a, b, active ? IM_COL32(70, 96, 128, 255) : (hovered ? IM_COL32(88, 88, 88, 255) : IM_COL32(74, 74, 74, 255)), 3.0f, corners);
			dl->AddRect(a, b, kBorder, 3.0f, corners);
			if (icons[i])
				UnityGUI::DrawIcon(dl, icons[i], ImVec2(a.x + (bw - 16) * 0.5f, a.y + (bh - 16) * 0.5f), 16.0f);
			else
			{
				const ImVec2 ts = ImGui::CalcTextSize(ICON_FA_MOUNTAIN_SUN);
				dl->AddText(ImVec2(a.x + (bw - ts.x) * 0.5f, a.y + (bh - ts.y) * 0.5f), IM_COL32(220, 220, 220, 255), ICON_FA_MOUNTAIN_SUN);
			}
		}
		ImGui::SetCursorScreenPos(p);
		ImGui::Dummy(ImVec2(w, bh + 12.0f));
	}

	// 전체 폭 드롭다운 (Unity 도구 선택 상자)
	bool WideDropdown(const char* id, int* current, const char* const* items, int count)
	{
		bool changed = false;
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const float w = ImGui::GetContentRegionAvail().x;
		ImGui::SetCursorScreenPos(ImVec2(p.x + 14, p.y));
		ImGui::SetNextItemWidth(w - 24);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 3));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));
		ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.32f, 0.32f, 0.32f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.19f, 0.19f, 0.19f, 1.0f));
		if (ImGui::BeginCombo(id, items[*current]))
		{
			for (int i = 0; i < count; ++i)
				if (ImGui::Selectable(items[i], i == *current))
				{
					*current = i;
					changed = true;
				}
			ImGui::EndCombo();
		}
		ImGui::PopStyleColor(2);
		ImGui::PopStyleVar(3);
		ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + 26));
		ImGui::Dummy(ImVec2(w, 1));
		return changed;
	}

	// 설명 상자 (Unity 도구 설명)
	void DescriptionBox(const char* text)
	{
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const float w = ImGui::GetContentRegionAvail().x;
		const ImVec2 ts = ImGui::CalcTextSize(text, nullptr, false, w - 40);
		const ImVec2 a(p.x + 14, p.y + 2), b(p.x + w - 10, p.y + ts.y + 14);
		dl->AddRectFilled(a, b, kBoxBg, 3.0f);
		dl->AddRect(a, b, kBorder, 3.0f);
		dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(a.x + 8, a.y + 6), kText, text, nullptr, w - 40);
		ImGui::SetCursorScreenPos(p);
		ImGui::Dummy(ImVec2(w, ts.y + 20));
	}

	// ---- 브러시 목록 + 크기/세기 ----
	void DrawBrushes(Terrain* terrain)
	{
		if (!UnityGUI::FoldoutPlain("Brushes", 0, true))
			return;
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const float w = ImGui::GetContentRegionAvail().x;
		const float cell = 52.0f;
		const ImVec2 box0(p.x + 14, p.y + 2), box1(p.x + w - 10, p.y + cell + 10);
		dl->AddRectFilled(box0, box1, kBoxBg, 3.0f);
		dl->AddRect(box0, box1, kBorder, 3.0f);
		for (int i = 0; i < 4; ++i)
		{
			const ImVec2 a(box0.x + 5 + i * (cell + 4), box0.y + 4), b(a.x + cell, a.y + cell);
			ImGui::SetCursorScreenPos(a);
			ImGui::PushID(i);
			if (ImGui::InvisibleButton("##brush", ImVec2(cell, cell)))
				s_BrushShape = i;
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", kBrushNames[i]);
			ImGui::PopID();
			dl->AddRectFilled(a, b, IM_COL32(20, 20, 20, 255));
			// 브러시 모양 미리보기 (가운데가 밝다)
			const int n = 24;
			const float px = cell / n;
			for (int y = 0; y < n; ++y)
				for (int x = 0; x < n; ++x)
				{
					const float dx = (x + 0.5f) / n * 2 - 1, dy = (y + 0.5f) / n * 2 - 1;
					const float f = Falloff(i, sqrtf(dx * dx + dy * dy) / 0.92f, x * 7, y * 13);
					if (f <= 0.01f)
						continue;
					const int c = (int)(f * 235);
					dl->AddRectFilled(ImVec2(a.x + x * px, a.y + y * px), ImVec2(a.x + (x + 1) * px, a.y + (y + 1) * px), IM_COL32(c, c, c, 255));
				}
			if (i == s_BrushShape)
				dl->AddRect(ImVec2(a.x - 1, a.y - 1), ImVec2(b.x + 1, b.y + 1), kSelected, 0.0f, 0, 2.0f);
		}
		ImGui::SetCursorScreenPos(p);
		ImGui::Dummy(ImVec2(w, cell + 14));
		UnityGUI::Label(kBrushNames[s_BrushShape], 1);

		float maxSize = 500.0f;
		if (terrain && terrain->GetTerrainData())
			maxSize = (std::min)(500.0f, (std::max)(terrain->GetTerrainData()->Size.x, terrain->GetTerrainData()->Size.z));
		UnityGUI::Slider("Brush Size", &s_BrushSize, 0.1f, maxSize);
		s_BrushSize = std::clamp(s_BrushSize, 0.1f, maxSize);
		UnityGUI::Slider("Opacity", &s_Opacity, 0.0f, 100.0f);
		s_Opacity = std::clamp(s_Opacity, 0.0f, 100.0f);
	}

	// ---- Paint Texture: 레이어 목록 ----
	void DrawLayers(TerrainData& data, const std::shared_ptr<TerrainData>& owner)
	{
		UnityGUI::Label("Terrain Layers", 0, true);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const float w = ImGui::GetContentRegionAvail().x;
		const float cell = 64.0f;
		const ImVec2 box0(p.x + 14, p.y + 2), box1(p.x + w - 10, p.y + cell + 26);
		dl->AddRectFilled(box0, box1, kBoxBg, 3.0f);
		dl->AddRect(box0, box1, kBorder, 3.0f);
		if (data.Layers.empty())
			dl->AddText(ImVec2(box0.x + 10, box0.y + 10), kTextDim, "No Terrain Layers. Use \"Edit Terrain Layers...\" to add one.");
		s_SelectedLayer = std::clamp(s_SelectedLayer, 0, (std::max)(0, (int)data.Layers.size() - 1));
		for (int i = 0; i < (int)data.Layers.size(); ++i)
		{
			TerrainLayer* layer = data.Layers[i].get();
			const ImVec2 a(box0.x + 6 + i * (cell + 8), box0.y + 4), b(a.x + cell, a.y + cell);
			ImGui::SetCursorScreenPos(a);
			ImGui::PushID(i);
			if (ImGui::InvisibleButton("##layer", ImVec2(cell, cell + 18)))
				s_SelectedLayer = i;
			ImGui::PopID();
			if (GfxShaderResourceView* srv = layer->DiffuseSRV())
				dl->AddImage((ImTextureID)srv, a, b);
			else
				dl->AddRectFilled(a, b, IM_COL32(128, 128, 128, 255));
			if (i == s_SelectedLayer)
				dl->AddRect(ImVec2(a.x - 2, a.y - 2), ImVec2(b.x + 2, b.y + 18), kSelected, 2.0f, 0, 2.0f);
			const std::string name = layer->Name();
			const ImVec4 clip(a.x, a.y, b.x, b.y + 20);
			dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.9f, ImVec2(a.x, b.y + 2), kText, name.c_str(), nullptr, 0.0f, &clip);
		}
		ImGui::SetCursorScreenPos(p);
		ImGui::Dummy(ImVec2(w, cell + 32));

		// Edit Terrain Layers... (Unity 의 버튼 메뉴)
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + w - 170);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
		if (ImGui::Button("Edit Terrain Layers...", ImVec2(160, 0)))
			ImGui::OpenPopup("##editlayers");
		ImGui::PopStyleVar();
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 5));
		if (ImGui::BeginPopup("##editlayers"))
		{
			const bool canAdd = (int)data.Layers.size() < TerrainData::kMaxLayers;
			if (ImGui::BeginMenu("Add Layer", canAdd))
			{
				for (const std::string& path : TerrainLayer::ListAvailable())
				{
					bool used = false;
					for (auto& l : data.Layers)
						used |= l && l->Path == path;
					if (ImGui::MenuItem(std::filesystem::path(path).stem().string().c_str(), nullptr, false, !used))
					{
						auto before = Snapshot(data);
						AddLayer(data, path);
						PushFullRecord("Add Terrain Layer", owner, before);
					}
				}
				ImGui::EndMenu();
			}
			if (ImGui::MenuItem("Create Layer...", nullptr, false, canAdd))
			{
				// 선택한 레이어의 텍스처로 새 레이어 에셋을 만든다 (텍스처는 아래 Diffuse 에서 바꾼다)
				const std::string diffuse = data.Layers.empty() ? std::string() : data.Layers[s_SelectedLayer]->DiffusePath;
				auto layer = TerrainLayer::Create(UniqueAssetPath("New Layer", ".terrainlayer"), diffuse);
				auto before = Snapshot(data);
				AddLayer(data, layer->Path);
				PushFullRecord("Create Terrain Layer", owner, before);
				s_SelectedLayer = (int)data.Layers.size() - 1;
			}
			if (ImGui::MenuItem("Remove Layer", nullptr, false, !data.Layers.empty()))
			{
				auto before = Snapshot(data);
				RemoveLayer(data, s_SelectedLayer);
				PushFullRecord("Remove Terrain Layer", owner, before);
			}
			ImGui::EndPopup();
		}
		ImGui::PopStyleVar();

		// 선택한 레이어 설정 (Unity 의 TerrainLayer Inspector)
		if (!data.Layers.empty())
		{
			TerrainLayer& layer = *data.Layers[s_SelectedLayer];
			UnityGUI::Spacing(4);
			UnityGUI::Label(layer.Name().c_str(), 0, true);
			const std::string diffuseName = layer.DiffusePath.empty() ? "None (Texture 2D)" : std::filesystem::path(layer.DiffusePath).filename().string();
			if (UnityGUI::ObjectField("Diffuse", diffuseName.c_str(), 1))
				ImGui::OpenPopup("##diffusepick");
			ImGui::SetNextWindowSizeConstraints(ImVec2(280, 0), ImVec2(520, 360));
			if (ImGui::BeginPopup("##diffusepick"))
			{
				static std::vector<std::string> textures;
				if (ImGui::IsWindowAppearing())
				{
					textures = ScanFiles(L".png");
					for (auto& t : ScanFiles(L".jpg")) textures.push_back(t);
				}
				for (const std::string& t : textures)
					if (ImGui::Selectable(t.c_str(), t == layer.DiffusePath))
					{
						layer.SetDiffuse(t);
						layer.Save();
					}
				ImGui::EndPopup();
			}
			if (UnityGUI::Vector2Pair("Tile Size", "X", &layer.TileSize.x, "Y", &layer.TileSize.y, 1)) layer.Save();
			if (UnityGUI::Vector2Pair("Tile Offset", "X", &layer.TileOffset.x, "Y", &layer.TileOffset.y, 1)) layer.Save();
			// 높이 변위 (테셀레이션): Height Map + Amplitude (m) · Base — 바위 · 자갈 · 흙길이 실제 입체로
			const std::string heightName = layer.HeightPath.empty() ? "None (Texture 2D)" : std::filesystem::path(layer.HeightPath).filename().string();
			if (UnityGUI::ObjectField("Height Map", heightName.c_str(), 1))
				ImGui::OpenPopup("##heightpick");
			ImGui::SetNextWindowSizeConstraints(ImVec2(280, 0), ImVec2(520, 360));
			if (ImGui::BeginPopup("##heightpick"))
			{
				static std::vector<std::string> textures;
				if (ImGui::IsWindowAppearing())
				{
					textures = ScanFiles(L".png");
					for (auto& t : ScanFiles(L".jpg")) textures.push_back(t);
				}
				if (ImGui::Selectable("None", layer.HeightPath.empty()))
				{
					layer.SetHeight(std::string());
					layer.Save();
				}
				for (const std::string& t : textures)
					if (ImGui::Selectable(t.c_str(), t == layer.HeightPath))
					{
						layer.SetHeight(t);
						layer.Save();
					}
				ImGui::EndPopup();
			}
			if (!layer.HeightPath.empty())
			{
				if (UnityGUI::Float("Amplitude", &layer.HeightAmplitude, 2)) { layer.HeightAmplitude = std::clamp(layer.HeightAmplitude, 0.0f, 4.0f); layer.Save(); }
				if (UnityGUI::Slider("Base", &layer.HeightBase, 0.0f, 1.0f, 2)) { layer.HeightBase = std::clamp(layer.HeightBase, 0.0f, 1.0f); layer.Save(); }
				const std::wstring full = PathManager::GetI()->GetMovePathW(string_to_wstring(layer.HeightPath));
				if (!ImportSettingsInspector::IsLinearHeightMap(full))
				{
					UnityGUI::HelpBox("This height map is imported as a color (sRGB) texture. Height maps should be linear.", true, 1);
					if (UnityGUI::CenterButton("Fix Now##terrainHeightFix", 120.0f))
						ImportSettingsInspector::MarkAsHeightMap(full);
				}
			}
		}
	}

	// ---- Terrain Settings ----
	void DrawSettings(Terrain* terrain)
	{
		using namespace UnityGUI;
		if (Foldout("Basic Terrain"))
		{
			Int("Grouping ID", &terrain->GroupingIDRef(), 1);
			Toggle("Auto Connect", &terrain->AutoConnectRef(), 1);
			Toggle("Draw", &terrain->DrawRef(), 1);
			float& pe = terrain->PixelErrorRef();
			Slider("Pixel Error", &pe, 1.0f, 200.0f, 1);
			pe = std::clamp(pe, 1.0f, 200.0f);
			Float("Basemap Distance", &terrain->BasemapDistanceRef(), 1);
			static const char* kShadow[] = { "Off", "On", "Two Sided", "Shadows Only" };
			Dropdown("Shadow Casting Mode", &terrain->ShadowCastingRef(), kShadow, 4, 1);
			ObjectField("Material", "Default-Terrain-Standard", 1);
		}

		std::string path = terrain->GetTerrainDataPath();
		if (TerrainDataField("Terrain Data", path, "##terraindata"))
			terrain->SetTerrainData(path);

		auto data = terrain->GetTerrainData();
		if (data == nullptr)
			return;
		if (Foldout("Mesh Resolution (On Terrain Data)"))
		{
			Vec3 size = data->Size;
			bool changed = false;
			changed |= Float("Terrain Width", &size.x, 1);
			changed |= Float("Terrain Length", &size.z, 1);
			changed |= Float("Terrain Height", &size.y, 1);
			if (changed)
			{
				// 크기만 바뀌므로 가벼운 기록 (입력 한 번 = 한 단계)
				const Vec3 before = data->Size;
				data->SetSize(size);
				const Vec3 after = data->Size;
				Undo::Record rec;
				rec.Name = "Terrain Size";
				rec.UndoAction = [data, before]() { data->SetSize(before); };
				rec.RedoAction = [data, after]() { data->SetSize(after); };
				Undo::Push(std::move(rec));
			}
		}
		if (Foldout("Texture Resolutions (On Terrain Data)"))
		{
			static const int kRes[] = { 33, 65, 129, 257, 513, 1025, 2049, 4097 };
			static const char* kResNames[] = { "33 x 33", "65 x 65", "129 x 129", "257 x 257", "513 x 513", "1025 x 1025", "2049 x 2049", "4097 x 4097" };
			int cur = 4;
			for (int i = 0; i < 8; ++i) if (kRes[i] == data->HeightmapResolution) cur = i;
			if (Dropdown("Heightmap Resolution", &cur, kResNames, 8, 1))
			{
				auto before = Snapshot(*data);
				data->SetHeightmapResolution(kRes[cur]);
				PushFullRecord("Heightmap Resolution", data, before);
			}
			char buf[64];
			sprintf_s(buf, "%d x %d", data->ControlResolution, data->ControlResolution);
			ValueLabel("Control Texture Resolution", buf, 1);
		}

		// LOD 상태 (쿼드트리가 지금 고른 노드)
		const auto& st = terrain->GetStats(true);
		char info[256];
		int maxDepthDrawn = 0;
		for (int d = 0; d < 16; ++d) if (st.DepthHistogram[d]) maxDepthDrawn = d;
		sprintf_s(info, "Quadtree LOD (Scene view): %d nodes selected, %d drawn, %d triangles. Depth 0..%d, deepest drawn %d.",
			st.Nodes, st.DrawnNodes, st.Triangles, data->MaxDepth(), maxDepthDrawn);
		if (Foldout("Debug"))
			Toggle("Show LOD Nodes", &terrain->ShowLodNodesRef(), 1);
		Spacing(4);
		HelpBox(info, false);
	}

	// ================================================================ Paint Details (Unity 의 디테일 칠하기: 풀·꽃·작은 돌)
	int s_DetailProto = 0;
	float s_DetailTarget = 100.0f;   // %: 칠하면 다가가는 밀도
	bool s_DetailStroke = false;
	std::shared_ptr<TerrainData> s_DetailStrokeData;
	std::vector<std::vector<uint8_t>> s_DetailStrokeBefore;
	Rect s_DetailRect;
	std::string s_DetailStrokeName;

	using DensityMaps = std::vector<std::vector<uint8_t>>;

	Rect FullRect(int res)
	{
		Rect r;
		Grow(r, 0, 0, res - 1, res - 1);
		return r;
	}

	// 밀도 맵 사각형 영역 Undo (칠하기 한 획, 채우기, 지우기)
	void PushDetailRectUndo(const std::string& name, const std::shared_ptr<TerrainData>& data, const DensityMaps& before, const Rect& r)
	{
		const int res = data->DetailResolution;
		if (!r.Valid())
			return;
		auto b = std::make_shared<DensityMaps>(), a = std::make_shared<DensityMaps>();
		size_t bytes = 0;
		for (size_t i = 0; i < before.size() && i < data->DetailDensity.size(); ++i)
		{
			if (before[i].size() != (size_t)res * res || data->DetailDensity[i].size() != (size_t)res * res)
				return;
			b->push_back(CropOf(before[i], res, 1, r));
			a->push_back(CropOf(data->DetailDensity[i], res, 1, r));
			bytes += b->back().size() * 2;
		}
		Undo::Record rec;
		rec.Name = name;
		rec.Bytes = bytes;
		auto apply = [data, r, res](const DensityMaps& crops) {
			if (data->DetailResolution != res)
				return;
			for (size_t i = 0; i < crops.size() && i < data->DetailDensity.size(); ++i)
				if (data->DetailDensity[i].size() == (size_t)res * res)
					PasteCrop(data->DetailDensity[i], res, 1, r, crops[i]);
			data->OnDetailsChanged();
		};
		rec.UndoAction = [apply, b]() { apply(*b); };
		rec.RedoAction = [apply, a]() { apply(*a); };
		Undo::Push(std::move(rec));
	}

	// 종류 추가·제거: 프로토타입 + 밀도 맵 전체
	struct DetailState
	{
		std::vector<DetailPrototype> Protos;
		DensityMaps Maps;
		int Resolution = 0;
	};
	std::shared_ptr<DetailState> SnapshotDetails(const TerrainData& d)
	{
		auto s = std::make_shared<DetailState>();
		s->Protos = d.DetailPrototypes;
		s->Maps = d.DetailDensity;
		s->Resolution = d.DetailResolution;
		return s;
	}
	void PushDetailFullUndo(const char* name, const std::shared_ptr<TerrainData>& data, const std::shared_ptr<DetailState>& before)
	{
		auto after = SnapshotDetails(*data);
		Undo::Record rec;
		rec.Name = name;
		for (const auto& m : before->Maps) rec.Bytes += m.size();
		for (const auto& m : after->Maps) rec.Bytes += m.size();
		auto apply = [data](const DetailState& s) {
			data->DetailPrototypes = s.Protos;
			data->DetailDensity = s.Maps;
			data->DetailResolution = s.Resolution;
			data->OnDetailsChanged();
		};
		rec.UndoAction = [apply, before]() { apply(*before); };
		rec.RedoAction = [apply, after]() { apply(*after); };
		Undo::Push(std::move(rec));
	}

	void BeginDetailStroke(const std::shared_ptr<TerrainData>& data, const char* name)
	{
		s_DetailStroke = true;
		s_DetailStrokeData = data;
		s_DetailStrokeBefore = data->DetailDensity;
		s_DetailRect = Rect();
		s_DetailStrokeName = name;
	}

	void EndDetailStroke()
	{
		s_DetailStroke = false;
		if (s_DetailStrokeData && s_DetailRect.Valid())
		{
			PushDetailRectUndo(s_DetailStrokeName, s_DetailStrokeData, s_DetailStrokeBefore, s_DetailRect);
			EditorLog::Write("Terrain", "detail stroke '%s' rect (%d,%d)-(%d,%d)", s_DetailStrokeName.c_str(), s_DetailRect.x0, s_DetailRect.z0, s_DetailRect.x1, s_DetailRect.z1);
		}
		s_DetailStrokeData = nullptr;
		s_DetailStrokeBefore.clear();
	}

	// 브러시 한 번: 고른 종류를 목표 밀도로 / erase = 지우기 (selectedOnly 면 고른 종류만, 아니면 전부)
	void PaintDetailsAt(Terrain* terrain, const Vec3& worldPos, float dt, bool erase, bool selectedOnly)
	{
		auto data = terrain->GetTerrainData();
		if (!data || data->DetailPrototypes.empty())
			return;
		const int res = data->DetailResolution;
		const size_t n = (size_t)res * res;
		data->DetailDensity.resize(data->DetailPrototypes.size());
		for (auto& map : data->DetailDensity)
			if (map.size() != n)
				map.assign(n, 0);
		const int proto = std::clamp(s_DetailProto, 0, (int)data->DetailPrototypes.size() - 1);
		const Vec3 local = worldPos - terrain->GetPosition();
		const float cx = local.x / data->Size.x * res - 0.5f, cz = local.z / data->Size.z * res - 0.5f;
		const float rx = (std::max)(0.5f, s_BrushSize * 0.5f / data->Size.x * res), rz = (std::max)(0.5f, s_BrushSize * 0.5f / data->Size.z * res);
		const int x0 = (std::max)(0, (int)floorf(cx - rx)), x1 = (std::min)(res - 1, (int)ceilf(cx + rx));
		const int z0 = (std::max)(0, (int)floorf(cz - rz)), z1 = (std::min)(res - 1, (int)ceilf(cz + rz));
		if (x0 > x1 || z0 > z1)
			return;
		const float opacity = s_Opacity / 100.0f;
		const float target = std::clamp(s_DetailTarget, 0.0f, 100.0f) / 100.0f * 255.0f;
		auto step = [](uint8_t& v, float goal, float k) {
			const float nv = v + (goal - v) * (std::min)(1.0f, k);
			int out = (int)std::lround(nv);
			if (out == v && fabsf(goal - v) >= 1.0f)
				out += goal > v ? 1 : -1;   // 작은 걸음도 멈추지 않게
			v = (uint8_t)std::clamp(out, 0, 255);
		};
		for (int z = z0; z <= z1; ++z)
			for (int x = x0; x <= x1; ++x)
			{
				const float dx = (x - cx) / rx, dz = (z - cz) / rz;
				const float f = Falloff(s_BrushShape, sqrtf(dx * dx + dz * dz), x, z) * opacity;
				if (f <= 0.0f)
					continue;
				const size_t i = (size_t)z * res + x;
				const float k = f * dt * 6.0f;
				if (!erase)
					step(data->DetailDensity[proto][i], target, k);
				else
					for (size_t p = 0; p < data->DetailDensity.size(); ++p)
						if (!selectedOnly || (int)p == proto)
							step(data->DetailDensity[p][i], 0.0f, k);
			}
		Grow(s_DetailRect, x0, z0, x1, z1);
		data->OnDetailsChanged();
	}

	// 썸네일: 종류 모양을 간단히 그린다 (잎 = 곡선, 꽃 = 줄기 끝 원, 돌 = 타원)
	ImU32 Col(const XMFLOAT4& c, float k = 1.0f)
	{
		auto ch = [&](float v) { return (int)std::clamp(v * k * 255.0f, 0.0f, 255.0f); };
		return IM_COL32(ch(c.x), ch(c.y), ch(c.z), 255);
	}

	void DrawDetailIcon(ImDrawList* dl, const DetailPrototype& p, ImVec2 a, ImVec2 b)
	{
		dl->AddRectFilledMultiColor(a, b, IM_COL32(64, 84, 110, 255), IM_COL32(64, 84, 110, 255), IM_COL32(40, 46, 52, 255), IM_COL32(40, 46, 52, 255));
		const float w = b.x - a.x, h = b.y - a.y;
		const float groundY = b.y - h * 0.16f;
		dl->AddRectFilled(ImVec2(a.x, groundY), b, IM_COL32(72, 60, 46, 255));
		uint32_t s = (uint32_t)p.Seed * 2654435761u + 12345u;
		auto rnd = [&]() { s = s * 1664525u + 1013904223u; return ((s >> 8) & 0xFFFF) / 65535.0f; };
		if (p.Type == DetailPrototype::Kind::Pebble)
		{
			const int count = std::clamp(p.Blades, 1, 6);
			for (int i = 0; i < count; ++i)
			{
				const float r = w * (0.08f + 0.08f * rnd()) * (i == 0 ? 1.4f : 1.0f);
				const ImVec2 c(a.x + w * (0.2f + 0.6f * rnd()), groundY + h * 0.02f);
				dl->AddEllipseFilled(c, ImVec2(r, r * 0.6f), Col(i % 2 ? p.DryColor : p.HealthyColor, 0.75f + 0.35f * rnd()));
			}
			return;
		}
		const float scale = std::clamp(p.Height / 0.9f, 0.35f, 1.0f) * h * 0.72f;
		const int blades = std::clamp(p.Type == DetailPrototype::Kind::Flower ? p.Blades : p.Blades, 4, 18);
		for (int i = 0; i < blades; ++i)
		{
			const float x = a.x + w * (0.25f + 0.5f * rnd());
			const float bh = scale * (0.55f + 0.45f * rnd()) * (p.Type == DetailPrototype::Kind::Flower ? 0.5f : 1.0f);
			const float lean = (rnd() - 0.5f) * w * 0.5f * (0.4f + p.Lean + p.Bend);
			const ImVec2 p0(x, groundY + 1), p2(x + lean, groundY - bh), p1(x + lean * 0.2f, groundY - bh * 0.6f);
			const float t = rnd();
			XMFLOAT4 c(p.HealthyColor.x + (p.DryColor.x - p.HealthyColor.x) * t * 0.5f, p.HealthyColor.y + (p.DryColor.y - p.HealthyColor.y) * t * 0.5f,
				p.HealthyColor.z + (p.DryColor.z - p.HealthyColor.z) * t * 0.5f, 1);
			dl->AddBezierQuadratic(p0, p1, p2, Col(c, 0.8f + 0.4f * rnd()), 2.0f);
		}
		if (p.Type == DetailPrototype::Kind::Flower)
			for (int i = 0; i < (std::max)(1, p.Heads); ++i)
			{
				const float x = a.x + w * (0.3f + 0.4f * rnd());
				const float top = groundY - scale * (0.75f + 0.25f * rnd());
				dl->AddLine(ImVec2(x, groundY), ImVec2(x, top), Col(p.HealthyColor, 0.7f), 1.5f);
				const float r = std::clamp(p.HeadSize / 0.04f, 0.6f, 2.0f) * w * 0.07f;
				for (int k = 0; k < (std::max)(3, p.Petals); ++k)
				{
					const float an = XM_2PI * k / (std::max)(3, p.Petals);
					dl->AddCircleFilled(ImVec2(x + cosf(an) * r, top + sinf(an) * r * 0.6f), r * 0.6f, Col(p.FlowerColor));
				}
				dl->AddCircleFilled(ImVec2(x, top), r * 0.45f, Col(p.CenterColor));
			}
	}

	void DrawDetailTool(Terrain* terrain, const std::shared_ptr<TerrainData>& data)
	{
		using namespace UnityGUI;
		DescriptionBox("Click to paint details (grass, flowers, pebbles).\nHold shift and click to erase details.\nHold Ctrl and click to erase only details of the selected type.");

		// 설정·프로토타입 값 편집 Undo (JSON). 종류 수가 바뀌는 추가·제거는 밀도 맵과 함께 따로 기록하므로 키에 수를 넣는다
		std::weak_ptr<TerrainData> weak = data;
		Undo::WatchAsset("terraindetails:" + data->Path + ":" + std::to_string(data->DetailPrototypes.size()), "Terrain Details",
			[weak]() {
				auto d = weak.lock();
				if (!d) return std::string();
				nlohmann::json j = { { "settings", d->Details.ToJson() }, { "prototypes", nlohmann::json::array() } };
				for (const DetailPrototype& p : d->DetailPrototypes) j["prototypes"].push_back(p.ToJson());
				return j.dump();
			},
			[weak](const std::string& text) {
				auto d = weak.lock();
				const nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
				if (!d || !j.is_object() || !j.contains("prototypes") || j["prototypes"].size() != d->DetailPrototypes.size())
					return;
				d->Details.FromJson(j["settings"]);
				for (size_t i = 0; i < d->DetailPrototypes.size(); ++i)
					d->DetailPrototypes[i].FromJson(j["prototypes"][i]);
				d->OnDetailsChanged();
			});

		// ---- 디테일 종류
		Label("Details", 0, true);
		const int count = (int)data->DetailPrototypes.size();
		s_DetailProto = count > 0 ? std::clamp(s_DetailProto, 0, count - 1) : 0;
		{
			ImDrawList* dl = ImGui::GetWindowDrawList();
			const ImVec2 p = ImGui::GetCursorScreenPos();
			const float w = ImGui::GetContentRegionAvail().x;
			const float cell = 64.0f;
			const int perRow = (std::max)(1, (int)((w - 28) / (cell + 6)));
			const int rows = (std::max)(1, (count + perRow - 1) / perRow);
			const ImVec2 box0(p.x + 14, p.y + 2), box1(p.x + w - 10, p.y + rows * (cell + 20) + 10);
			dl->AddRectFilled(box0, box1, kBoxBg, 3.0f);
			dl->AddRect(box0, box1, kBorder, 3.0f);
			for (int i = 0; i < count; ++i)
			{
				const ImVec2 a(box0.x + 5 + (i % perRow) * (cell + 6), box0.y + 4 + (i / perRow) * (cell + 20)), b(a.x + cell, a.y + cell);
				ImGui::SetCursorScreenPos(a);
				ImGui::PushID(i);
				if (ImGui::InvisibleButton("##detail", ImVec2(cell, cell + 16)))
					s_DetailProto = i;
				ImGui::PopID();
				dl->PushClipRect(a, b, true);
				DrawDetailIcon(dl, data->DetailPrototypes[i], a, b);
				dl->PopClipRect();
				const std::string& name = data->DetailPrototypes[i].Name;
				dl->PushClipRect(ImVec2(a.x, b.y), ImVec2(b.x + 4, b.y + 16), true);
				dl->AddText(ImVec2(a.x + 2, b.y + 1), kText, name.c_str());
				dl->PopClipRect();
				if (i == s_DetailProto)
					dl->AddRect(ImVec2(a.x - 1, a.y - 1), ImVec2(b.x + 1, b.y + 17), kSelected, 0.0f, 0, 2.0f);
			}
			if (count == 0)
				dl->AddText(ImVec2(box0.x + 10, box0.y + 10), kTextDim, "No details. Add Detail to start painting.");
			ImGui::SetCursorScreenPos(p);
			ImGui::Dummy(ImVec2(w, box1.y - p.y + 4));
		}
		// ---- 추가 / 제거
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 14);
		if (ImGui::Button("Add Detail", ImVec2(100, 0)))
			ImGui::OpenPopup("##addDetail");
		ImGui::SameLine();
		ImGui::BeginDisabled(count == 0);
		if (ImGui::Button("Remove", ImVec2(100, 0)) && count > 0)
		{
			auto before = SnapshotDetails(*data);
			data->RemoveDetailPrototype(s_DetailProto);
			s_DetailProto = (std::max)(0, s_DetailProto - 1);
			PushDetailFullUndo("Remove Detail", data, before);
		}
		ImGui::EndDisabled();
		if (ImGui::BeginPopup("##addDetail"))
		{
			for (const std::string& path : DetailPrototype::ListPresets())
			{
				const std::string label = std::filesystem::path(path).stem().string();
				if (ImGui::MenuItem(label.c_str()))
					TerrainEditor::AddDetailPrototype(data, path), s_DetailProto = (int)data->DetailPrototypes.size() - 1;
			}
			ImGui::Separator();
			static const char* kEmpty[] = { "Empty Grass", "Empty Flower", "Empty Pebbles" };
			for (int k = 0; k < 3; ++k)
				if (ImGui::MenuItem(kEmpty[k]))
				{
					auto before = SnapshotDetails(*data);
					DetailPrototype p;
					p.Type = (DetailPrototype::Kind)k;
					p.Name = kEmpty[k] + 6;
					p.Seed = 1 + (int)data->DetailPrototypes.size() * 7;
					if (k == 2)
					{
						p.Blades = 3; p.BladeWidth = 0.09f; p.Radius = 0.2f; p.Density = 1.5f; p.GroundAlign = 0.8f; p.WindResponse = 0.0f;
						p.HealthyColor = { 0.5f, 0.48f, 0.45f, 1 }; p.DryColor = { 0.42f, 0.38f, 0.33f, 1 };
					}
					data->AddDetailPrototype(p);
					s_DetailProto = (int)data->DetailPrototypes.size() - 1;
					PushDetailFullUndo("Add Detail", data, before);
				}
			ImGui::EndPopup();
		}

		// ---- 브러시
		Spacing(6);
		DrawBrushes(terrain);
		Slider("Target Strength", &s_DetailTarget, 0.0f, 100.0f);
		s_DetailTarget = std::clamp(s_DetailTarget, 0.0f, 100.0f);
		Spacing(4);
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 14);
		ImGui::BeginDisabled(count == 0);
		if (ImGui::Button("Fill Terrain", ImVec2(150, 0)) && count > 0)
			TerrainEditor::FillDetails(terrain, s_DetailProto, s_DetailTarget / 100.0f);
		ImGui::SameLine();
		if (ImGui::Button("Clear", ImVec2(150, 0)) && count > 0)
			TerrainEditor::FillDetails(terrain, s_DetailProto, 0.0f);
		ImGui::EndDisabled();

		// ---- 통계
		{
			const DetailRenderer::Stats& st = DetailRenderer::LastStats(true);
			char info[256];
			snprintf(info, sizeof(info), "%d detail types, density map %d x %d.\nScene view: %d clumps in %d chunks, %d draw calls (%d chunks cached).",
				count, data->DetailResolution, data->DetailResolution, st.Instances, st.Chunks, st.DrawCalls, st.Cached);
			Spacing(4);
			HelpBox(info, false);
		}

		// ---- 고른 종류 설정 (바꾸면 모든 같은 종류가 바로 바뀐다)
		if (count > 0 && FoldoutPlain("Selected Detail Settings", 0, false))
		{
			DetailPrototype& p = data->DetailPrototypes[s_DetailProto];
			if (p.DrawInspector())
				data->OnDetailsChanged();
			int v = 0, t = 0;
			if (DetailRenderer::GetMeshInfo(p, v, t))
			{
				char info[96];
				snprintf(info, sizeof(info), "Clump mesh: %d vertices, %d triangles.", v, t);
				HelpBox(info, false);
			}
		}
		// ---- 지형 전체 디테일 설정 (Unity Terrain Settings 의 Detail Objects / Wind Settings for Grass)
		if (FoldoutPlain("Detail Settings", 0, false))
		{
			DetailSettings& s = data->Details;
			const std::string before = s.ToJson().dump();
			Slider("Detail Distance", &s.Distance, 10.0f, 400.0f);
			Slider("Detail Density", &s.DensityScale, 0.0f, 2.0f);
			Slider("Shadow Distance", &s.ShadowDistance, 0.0f, 150.0f);
			static const int kRes[] = { 128, 256, 512, 1024, 2048 };
			static const char* kResNames[] = { "128 x 128", "256 x 256", "512 x 512", "1024 x 1024", "2048 x 2048" };
			int cur = 2;
			for (int i = 0; i < 5; ++i)
				if (kRes[i] == data->DetailResolution)
					cur = i;
			if (Dropdown("Detail Resolution", &cur, kResNames, 5))
			{
				auto snap = SnapshotDetails(*data);
				data->SetDetailResolution(kRes[cur]);
				PushDetailFullUndo("Change Detail Resolution", data, snap);
			}
			Label("Wind Settings for Grass", 0, true);
			Slider("Speed", &s.WindSpeed, 0.0f, 3.0f, 1);
			Slider("Size", &s.WindSize, 1.0f, 100.0f, 1);
			Slider("Bending", &s.WindBending, 0.0f, 1.5f, 1);
			Slider("Direction", &s.WindDirection, 0.0f, 360.0f, 1);
			if (s.ToJson().dump() != before)
				data->OnDetailsChanged();
		}
	}
}

namespace TerrainEditor
{
	void AddTreePrototype(TerrainData& data, int preset)
	{
		TreeDesc d;
		d.ApplyPreset(preset);
		d.Params.Seed = 1 + (int)data.TreePrototypes.size() * 7;
		d.Invalidate();
		data.TreePrototypes.push_back(d);
		data.OnTreesChanged();
	}

	int MassPlaceTrees(Terrain* terrain, int count)
	{
		auto data = terrain ? terrain->GetTerrainData() : nullptr;
		if (!data || data->TreePrototypes.empty() || count <= 0)
			return 0;
		auto before = data->TreeInstances;
		const float spacing = TreeSpacing();
		int placed = 0;
		for (int attempt = 0; attempt < count * 4 && placed < count; ++attempt)
		{
			const float x = Rand01(), z = Rand01();
			if (TreeNearby(*data, x * data->Size.x, z * data->Size.z, spacing))
				continue;
			data->TreeInstances.push_back(MakeTree(x, z, (int)(Rand01() * data->TreePrototypes.size()) % (int)data->TreePrototypes.size()));
			++placed;
		}
		data->OnTreesChanged();
		PushTreeUndo("Mass Place Trees", data, std::move(before));
		EditorLog::Write("Terrain", "mass placed %d trees (asked %d, spacing %.1f m) on %s", placed, count, spacing, data->Name().c_str());
		return placed;
	}

	int AddDetailPrototype(const std::shared_ptr<TerrainData>& data, const std::string& presetPath)
	{
		DetailPrototype p;
		p.Seed = 1 + (int)data->DetailPrototypes.size() * 7;
		if (!DetailPrototype::LoadPreset(presetPath, p))
		{
			EditorLog::Write("Terrain", "detail preset not found: %s", presetPath.c_str());
			return -1;
		}
		auto before = SnapshotDetails(*data);
		data->AddDetailPrototype(p);
		PushDetailFullUndo(("Add Detail " + p.Name).c_str(), data, before);
		EditorLog::Write("Terrain", "added detail '%s' (%s) to %s", p.Name.c_str(), presetPath.c_str(), data->Name().c_str());
		return (int)data->DetailPrototypes.size() - 1;
	}

	void FillDetails(Terrain* terrain, int proto, float value)
	{
		auto data = terrain ? terrain->GetTerrainData() : nullptr;
		if (!data || proto < 0 || proto >= (int)data->DetailPrototypes.size())
			return;
		const int res = data->DetailResolution;
		data->DetailDensity.resize(data->DetailPrototypes.size());
		for (auto& map : data->DetailDensity)
			if (map.size() != (size_t)res * res)
				map.assign((size_t)res * res, 0);
		const DensityMaps before = data->DetailDensity;
		std::fill(data->DetailDensity[proto].begin(), data->DetailDensity[proto].end(), (uint8_t)std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
		data->OnDetailsChanged();
		PushDetailRectUndo(value > 0.0f ? "Fill Details" : "Clear Details", data, before, FullRect(res));
		EditorLog::Write("Terrain", "fill detail %d ('%s') = %.2f on %s", proto, data->DetailPrototypes[proto].Name.c_str(), value, data->Name().c_str());
	}

	void PaintDetails(Terrain* terrain, int proto, const Vec3& worldPosition, float target, float deltaTime)
	{
		s_DetailProto = proto;
		s_DetailTarget = target * 100.0f;
		PaintDetailsAt(terrain, worldPosition, deltaTime, false, false);
	}

	void SetTool(Tool tool) { s_Tool = tool; }
	void SetPaintTool(PaintTool tool) { s_PaintTool = tool; }
	void SetBrush(int shape, float size, float opacity) { s_BrushShape = shape; s_BrushSize = size; s_Opacity = opacity; }
	void SetTargetHeight(float meters) { s_TargetHeight = meters; }
	void SetSelectedLayer(int layer) { s_SelectedLayer = layer; }

	bool TerrainDataField(const char* label, std::string& path, const char* popupId)
	{
		bool changed = false;
		const std::string name = path.empty() ? "None (Terrain Data)" : std::filesystem::path(path).stem().string();
		if (UnityGUI::ObjectField(label, name.c_str(), 0, path.empty() ? nullptr : "terrain"))
			ImGui::OpenPopup(popupId);
		ImGui::SetNextWindowSizeConstraints(ImVec2(280, 0), ImVec2(520, 360));
		if (ImGui::BeginPopup(popupId))
		{
			if (ImGui::Selectable("None"))
			{
				path.clear();
				changed = true;
			}
			for (const std::string& file : ScanFiles(L".terraindata"))
				if (ImGui::Selectable(file.c_str(), file == path))
				{
					path = file;
					changed = true;
				}
			ImGui::Separator();
			if (ImGui::Selectable("Create New Terrain Data"))
			{
				path = TerrainData::Create(UniqueAssetPath("New Terrain", ".terraindata"))->Path;
				changed = true;
			}
			ImGui::EndPopup();
		}
		return changed;
	}

	void DrawInspector(Terrain* terrain)
	{
		DrawToolbar();
		auto data = terrain->GetTerrainData();
		switch (s_Tool)
		{
		case Tool::PaintTerrain:
		{
			if (data == nullptr)
			{
				UnityGUI::HelpBox("Terrain has no Terrain Data. Assign one in Terrain Settings.", true);
				break;
			}
			static const char* names[] = { "Raise or Lower Terrain", "Paint Holes", "Paint Texture", "Set Height", "Smooth Height", "Stamp Terrain" };
			int current = (int)s_PaintTool;
			if (WideDropdown("##painttool", &current, names, 6))
				s_PaintTool = (PaintTool)current;
			DescriptionBox(PaintToolHelp(s_PaintTool));
			if (!PaintToolSupported(s_PaintTool))
				break;
			DrawBrushes(terrain);
			if (s_PaintTool == PaintTool::SetHeight)
			{
				UnityGUI::Slider("Height", &s_TargetHeight, 0.0f, data->Size.y);
				s_TargetHeight = std::clamp(s_TargetHeight, 0.0f, data->Size.y);
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 14);
				if (ImGui::Button("Flatten All", ImVec2(120, 0)))
				{
					auto before = Snapshot(*data);
					std::fill(data->Heights.begin(), data->Heights.end(), s_TargetHeight / data->Size.y);
					data->OnHeightsChanged(0, 0, data->HeightmapResolution - 1, data->HeightmapResolution - 1);
					PushFullRecord("Flatten Terrain", data, before);
				}
			}
			if (s_PaintTool == PaintTool::PaintTexture)
			{
				UnityGUI::Spacing(4);
				DrawLayers(*data, data);
			}
			break;
		}
		case Tool::Settings:
			DrawSettings(terrain);
			break;
		case Tool::CreateNeighbor:
			DescriptionBox("Click the edges to create neighbor terrains.\n(Not supported yet.)");
			break;
		case Tool::PaintTrees:
			if (data == nullptr)
			{
				UnityGUI::HelpBox("Terrain has no Terrain Data. Assign one in Terrain Settings.", true);
				break;
			}
			DrawTreeTool(terrain, data);
			break;
		case Tool::PaintDetails:
			if (data == nullptr)
			{
				UnityGUI::HelpBox("Terrain has no Terrain Data. Assign one in Terrain Settings.", true);
				break;
			}
			DrawDetailTool(terrain, data);
			break;
		case Tool::Generate:
			if (data == nullptr)
			{
				UnityGUI::HelpBox("Terrain has no Terrain Data. Assign one in Terrain Settings.", true);
				break;
			}
			DrawGenerateTool(terrain, data);
			break;
		}
	}

	void ApplyBrush(Terrain* terrain, PaintTool tool, const Vec3& worldPosition, float dt, bool shift)
	{
		auto data = terrain ? terrain->GetTerrainData() : nullptr;
		if (data == nullptr || dt <= 0.0f)
			return;
		const Vec3 local = worldPosition - terrain->GetPosition();
		const float opacity = s_Opacity / 100.0f;

		if (tool == PaintTool::PaintTexture)
		{
			if (data->Layers.empty())
				return;
			const int layer = std::clamp(s_SelectedLayer, 0, (int)data->Layers.size() - 1);
			const int cres = data->ControlResolution;
			const float cx = local.x / data->Size.x * (cres - 1), cz = local.z / data->Size.z * (cres - 1);
			const float rx = (std::max)(0.5f, s_BrushSize * 0.5f / data->Size.x * (cres - 1));
			const float rz = (std::max)(0.5f, s_BrushSize * 0.5f / data->Size.z * (cres - 1));
			const int x0 = (std::max)(0, (int)floorf(cx - rx)), x1 = (std::min)(cres - 1, (int)ceilf(cx + rx));
			const int z0 = (std::max)(0, (int)floorf(cz - rz)), z1 = (std::min)(cres - 1, (int)ceilf(cz + rz));
			if (x0 > x1 || z0 > z1)
				return;
			const int channels = (std::min)((int)data->Layers.size(), 4);
			for (int z = z0; z <= z1; ++z)
				for (int x = x0; x <= x1; ++x)
				{
					const float dx = (x - cx) / rx, dz = (z - cz) / rz;
					const float f = Falloff(s_BrushShape, sqrtf(dx * dx + dz * dz), x, z) * opacity;
					if (f <= 0.0f)
						continue;
					uint8_t* px = &data->Control[((size_t)z * cres + x) * 4];
					float w[4];
					for (int c = 0; c < 4; ++c) w[c] = c < channels ? px[c] / 255.0f : 0.0f;
					const float amount = (std::min)(1.0f, f * dt * 4.0f);
					const float target = w[layer] + (1.0f - w[layer]) * amount;
					float others = 0.0f;
					for (int c = 0; c < channels; ++c) if (c != layer) others += w[c];
					for (int c = 0; c < channels; ++c)
						w[c] = c == layer ? target : (others > 1e-5f ? w[c] / others * (1.0f - target) : 0.0f);
					for (int c = 0; c < 4; ++c)
						px[c] = (uint8_t)std::lround(std::clamp(w[c], 0.0f, 1.0f) * 255.0f);
				}
			data->OnControlChanged(x0, z0, x1, z1);
			Grow(s_ControlRect, x0, z0, x1, z1);
			return;
		}

		// 높이 도구
		const int res = data->HeightmapResolution;
		const float cx = local.x / data->CellSizeX(), cz = local.z / data->CellSizeZ();
		const float rx = (std::max)(0.5f, s_BrushSize * 0.5f / data->CellSizeX());
		const float rz = (std::max)(0.5f, s_BrushSize * 0.5f / data->CellSizeZ());
		const int x0 = (std::max)(0, (int)floorf(cx - rx)), x1 = (std::min)(res - 1, (int)ceilf(cx + rx));
		const int z0 = (std::max)(0, (int)floorf(cz - rz)), z1 = (std::min)(res - 1, (int)ceilf(cz + rz));
		if (x0 > x1 || z0 > z1)
			return;

		std::vector<float> copy;
		if (tool == PaintTool::SmoothHeight)
		{
			// 이웃 평균은 이번 프레임 적용 전 값으로
			copy.resize((size_t)(x1 - x0 + 3) * (z1 - z0 + 3));
			for (int z = z0 - 1; z <= z1 + 1; ++z)
				for (int x = x0 - 1; x <= x1 + 1; ++x)
					copy[(size_t)(z - z0 + 1) * (x1 - x0 + 3) + (x - x0 + 1)] = data->GetHeightSample(x, z);
		}
		const float rate = (3.0f + s_BrushSize * 0.25f) / data->Size.y;   // Raise: 불투명도 100%, 중심에서 초당 미터
		const float target = s_TargetHeight / data->Size.y;
		for (int z = z0; z <= z1; ++z)
			for (int x = x0; x <= x1; ++x)
			{
				const float dx = (x - cx) / rx, dz = (z - cz) / rz;
				const float f = Falloff(s_BrushShape, sqrtf(dx * dx + dz * dz), x, z) * opacity;
				if (f <= 0.0f)
					continue;
				float& h = data->Heights[(size_t)z * res + x];
				switch (tool)
				{
				case PaintTool::RaiseLower:
					h += (shift ? -1.0f : 1.0f) * f * dt * rate;
					break;
				case PaintTool::SetHeight:
					h += (target - h) * (std::min)(1.0f, f * dt * 6.0f);
					break;
				case PaintTool::SmoothHeight:
				{
					const int stride = x1 - x0 + 3;
					float sum = 0.0f;
					for (int oz = -1; oz <= 1; ++oz)
						for (int ox = -1; ox <= 1; ++ox)
							sum += copy[(size_t)(z - z0 + 1 + oz) * stride + (x - x0 + 1 + ox)];
					h += (sum / 9.0f - h) * (std::min)(1.0f, f * dt * 12.0f);
					break;
				}
				default:
					break;
				}
				h = std::clamp(h, 0.0f, 1.0f);
			}
		data->OnHeightsChanged(x0, z0, x1, z1);
		Grow(s_HeightRect, x0, z0, x1, z1);
	}

	bool SceneGUI(EditorCamera* camera, const ImVec2& viewMin, const ImVec2& viewMax, bool viewHovered)
	{
		GameObject* selected = SelectionManager::GetSelectedObjectType() == SelectionType::GAMEOBJECT ? SelectionManager::GetSelectedGameObject() : nullptr;
		Terrain* terrain = selected ? selected->GetComponent<Terrain>() : nullptr;
		const bool treeTool = s_Tool == Tool::PaintTrees;
		const bool detailTool = s_Tool == Tool::PaintDetails;
		if (terrain == nullptr || (s_Tool != Tool::PaintTerrain && !treeTool && !detailTool) || terrain->GetTerrainData() == nullptr || camera == nullptr)
		{
			if (s_Painting)
				EndStroke();
			if (s_DetailStroke)
				EndDetailStroke();
			s_Painting = false;
			s_TreeStroke = false;
			return false;
		}
		if (!treeTool && !detailTool && !PaintToolSupported(s_PaintTool))
			return true;

		// 마우스 광선
		ImGuiIO& io = ImGui::GetIO();
		const XMMATRIX viewProj = camera->View() * camera->Proj();
		const float nx = (io.MousePos.x - viewMin.x) / (viewMax.x - viewMin.x) * 2.0f - 1.0f;
		const float ny = 1.0f - (io.MousePos.y - viewMin.y) / (viewMax.y - viewMin.y) * 2.0f;
		const XMMATRIX inv = XMMatrixInverse(nullptr, viewProj);
		const Vec3 nearP = XMVector3TransformCoord(XMVectorSet(nx, ny, 0.0f, 1.0f), inv);
		const Vec3 farP = XMVector3TransformCoord(XMVectorSet(nx, ny, 1.0f, 1.0f), inv);
		Vec3 dir = farP - nearP;
		const float len = dir.Length();
		if (!(len > 1e-6f) || !std::isfinite(len))
			return true;   // 창이 막 열린 첫 프레임 등 카메라 행렬이 아직 유효하지 않을 때
		dir *= 1.0f / len;
		Vec3 hit;
		const bool onTerrain = viewHovered && terrain->Raycast(nearP, dir, len, hit);

		// 브러시 원 (지형 표면을 따라)
		if (onTerrain)
		{
			const float r = s_BrushSize * 0.5f;
			const ImU32 outer = IM_COL32(90, 170, 255, 235), inner = IM_COL32(90, 170, 255, 120);
			const int segments = 64;
			auto ring = [&](float radius, ImU32 color, float thickness) {
				XMFLOAT3 prev;
				for (int k = 0; k <= segments; ++k)
				{
					const float a = XM_2PI * k / segments;
					Vec3 pnt = hit + Vec3(cosf(a) * radius, 0.0f, sinf(a) * radius);
					pnt.y = terrain->GetPosition().y + terrain->SampleHeight(pnt) + 0.05f;
					const XMFLOAT3 cur(pnt.x, pnt.y, pnt.z);
					if (k > 0)
						SceneViewOverlay::DrawLine(prev, cur, color, thickness);
					prev = cur;
				}
			};
			ring(r, outer, 2.0f);
			ring(r * 0.5f, inner, 1.0f);
			const Vec3 n = terrain->GetInterpolatedNormal(hit);
			SceneViewOverlay::DrawLine(XMFLOAT3(hit.x, hit.y, hit.z), XMFLOAT3(hit.x + n.x * r * 0.3f, hit.y + n.y * r * 0.3f, hit.z + n.z * r * 0.3f), outer, 1.5f);
		}

		// 칠하기 (왼쪽 버튼, Alt/오른쪽 버튼은 카메라 조작)
		const bool canStart = viewHovered && onTerrain && !io.KeyAlt && !ImGui::IsMouseDown(ImGuiMouseButton_Right);
		if (treeTool)
		{
			// 나무: 누른 곳에서 한 번, 끌면 브러시 반지름의 1/4 만큼 움직일 때마다 한 번. 한 획 = Undo 한 단계
			if (canStart && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			{
				s_TreeStroke = true;
				s_TreeStrokeBefore = terrain->GetTerrainData()->TreeInstances;
				s_LastTreePos = Vec3(1e30f, 0, 0);
			}
			if (s_TreeStroke && onTerrain && (hit - s_LastTreePos).Length() >= s_BrushSize * 0.25f)
			{
				PaintTreesAt(terrain, hit, io.KeyShift || io.KeyCtrl, io.KeyCtrl);
				s_LastTreePos = hit;
			}
			if (s_TreeStroke && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
			{
				s_TreeStroke = false;
				auto data = terrain->GetTerrainData();
				if (data->TreeInstances.size() != s_TreeStrokeBefore.size())
					PushTreeUndo(io.KeyShift || io.KeyCtrl ? "Erase Trees" : "Paint Trees", data, std::move(s_TreeStrokeBefore));
				s_TreeStrokeBefore.clear();
			}
			return true;
		}
		if (detailTool)
		{
			// 디테일: 누르고 있는 동안 밀도를 목표로 (Shift = 모두 지우기, Ctrl = 고른 종류만 지우기). 한 획 = Undo 한 단계
			if (canStart && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !terrain->GetTerrainData()->DetailPrototypes.empty())
				BeginDetailStroke(terrain->GetTerrainData(), io.KeyShift || io.KeyCtrl ? "Erase Details" : "Paint Details");
			if (s_DetailStroke && onTerrain)
				PaintDetailsAt(terrain, hit, (std::min)(io.DeltaTime, 0.1f), io.KeyShift || io.KeyCtrl, io.KeyCtrl);
			if (s_DetailStroke && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
				EndDetailStroke();
			return true;
		}
		if (canStart && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			if (s_PaintTool == PaintTool::SetHeight && io.KeyShift)
				s_TargetHeight = terrain->SampleHeight(hit);   // Shift + 클릭 = 목표 높이 샘플
			else
			{
				s_Painting = true;
				BeginStroke(terrain->GetTerrainData(), PaintToolName(s_PaintTool));
			}
		}
		if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && s_Painting)
		{
			s_Painting = false;
			EndStroke();
		}
		if (s_Painting && onTerrain)
			ApplyBrush(terrain, s_PaintTool, hit, (std::min)(io.DeltaTime, 0.1f), io.KeyShift);
		return true;
	}
}
