#include "pch.h"
#include "TerrainEditor.h"
#include "Terrain.h"
#include "TerrainData.h"
#include "UnityGUI.h"
#include "SceneViewOverlay.h"
#include "EditorCamera.h"
#include "UndoSystem.h"

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

	// ---- 도구 막대 (Unity: 버튼 5개가 붙은 막대) ----
	void DrawToolbar()
	{
		static const char* icons[] = { "terrain_neighbor", "terrain_paint", "terrain_trees", "terrain_details", "terrain_settings" };
		static const char* tips[] = { "Create Neighbor Terrains", "Paint Terrain", "Paint Trees", "Paint Details", "Terrain Settings" };
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const float w = ImGui::GetContentRegionAvail().x;
		const float bw = 34.0f, bh = 24.0f;
		const float x0 = p.x + (w - bw * 5) * 0.5f, y0 = p.y + 6.0f;
		for (int i = 0; i < 5; ++i)
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
			const ImDrawFlags corners = i == 0 ? ImDrawFlags_RoundCornersLeft : (i == 4 ? ImDrawFlags_RoundCornersRight : ImDrawFlags_RoundCornersNone);
			dl->AddRectFilled(a, b, active ? IM_COL32(70, 96, 128, 255) : (hovered ? IM_COL32(88, 88, 88, 255) : IM_COL32(74, 74, 74, 255)), 3.0f, corners);
			dl->AddRect(a, b, kBorder, 3.0f, corners);
			UnityGUI::DrawIcon(dl, icons[i], ImVec2(a.x + (bw - 16) * 0.5f, a.y + (bh - 16) * 0.5f), 16.0f);
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
			if (ID3D11ShaderResourceView* srv = layer->DiffuseSRV())
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
}

namespace TerrainEditor
{
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
			DescriptionBox("Click to paint trees.\n(Not supported yet.)");
			break;
		case Tool::PaintDetails:
			DescriptionBox("Click to paint details (grass, flowers).\n(Not supported yet.)");
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
		if (terrain == nullptr || s_Tool != Tool::PaintTerrain || terrain->GetTerrainData() == nullptr || camera == nullptr)
		{
			if (s_Painting)
				EndStroke();
			s_Painting = false;
			return false;
		}
		if (!PaintToolSupported(s_PaintTool))
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
