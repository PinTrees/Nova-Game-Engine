#include "pch.h"
#include "BuildingGeneratorWindow.h"
#include "Building.h"
#include "BuildingGenerator.h"
#include "BuildingTextures.h"
#include "GameObject.h"
#include "TextureLibrary.h"
#include "Transform.h"
#include "Scene.h"
#include "SceneManager.h"
#include "SelectionManager.h"
#include "EditorExtensions.h"
#include "EditorGUIManager.h"
#include "PathManager.h"
#include "UnityGUI.h"
#include "UndoSystem.h"
#include "EditorLog.h"
#include "Vertex.h"
#include "Mesh.h"
#include "UMaterial.h"
#include "ImGui/imgui_internal.h"
#include "IconsFontAwesome/IconsFontAwesome6.h"
#include <fstream>
#include <filesystem>
#include <chrono>

BuildingGeneratorWindow* BuildingGeneratorWindow::s_Instance = nullptr;

namespace
{
	using BD = BuildingDesc;
	const char* kSteps[] = { "Style", "Shape & Size", "Walls", "Windows & Doors", "Roof", "Details", "Colors", "Finish" };
	constexpr int kStepCount = 8;
	const char* kPlans[] = { "Rectangle", "L-Shape", "T-Shape", "U-Shape", "Courtyard", "Cross (church)", "Round (tower)" };
	const char* kRoofs[] = { "Gable", "Hip", "Mansard", "Gambrel", "Flat", "Shed", "Pyramid", "Cone", "Dome", "Onion", "Tiered (stave)" };
	const char* kWallMats[] = { "Rubble Stone", "Ashlar (cut stone)", "Brick", "Plaster", "Stucco", "Planks", "Logs", "Marble" };
	const char* kRoofMats[] = { "Clay Tiles", "Slate", "Thatch", "Wood Shingles", "Copper", "Lead", "Turf (grass)" };
	const char* kWindows[] = { "None", "Rectangular", "Arched", "Gothic (pointed)", "Round (oculus)", "Mullioned", "Baroque (segmental)", "Arrow Slit" };
	const char* kDoors[] = { "Plank", "Arched", "Double", "Grand" };
	const char* kTimber[] = { "None", "Grid", "Cross (X)", "Chevron", "Tudor (curved)" };
	const char* kTowerShapes[] = { "Round", "Octagon", "Square" };
	const char* kTowerCounts[] = { "None", "1 (front left)", "2 (front corners)", "4 (all corners)" };
	const int kTowerCountValues[] = { 0, 1, 2, 4 };

	double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

	template <typename E>
	bool EnumCombo(const char* label, E* value, const char* const* items, int count)
	{
		int v = (int)*value;
		ImGui::SetNextItemWidth(-150.0f);
		if (ImGui::Combo(label, &v, items, count))
		{
			*value = (E)v;
			return true;
		}
		return false;
	}
	bool SliderF(const char* label, float* v, float lo, float hi, const char* fmt = "%.2f")
	{
		ImGui::SetNextItemWidth(-150.0f);
		return ImGui::SliderFloat(label, v, lo, hi, fmt);
	}
	bool SliderI(const char* label, int* v, int lo, int hi)
	{
		ImGui::SetNextItemWidth(-150.0f);
		return ImGui::SliderInt(label, v, lo, hi);
	}
	void Hint(const char* text)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.62f, 0.62f, 0.62f, 1.0f));
		ImGui::TextWrapped("%s", text);
		ImGui::PopStyleColor();
	}
	void Section(const char* title)
	{
		ImGui::Spacing();
		ImGui::SeparatorText(title);
	}
	void Rgb(float* c, float r, float g, float b) { c[0] = r; c[1] = g; c[2] = b; }

	// 색 묶음 (벽 · 위층 · 장식 돌 · 들보 · 지붕 · 창틀 · 덧창 · 문 · 유리 · 금속)
	struct Palette { const char* Name; float C[10][3]; };
	const Palette kPalettes[] = {
		{ "Medieval Village", { { 0.6f, 0.56f, 0.5f }, { 0.9f, 0.85f, 0.72f }, { 0.7f, 0.66f, 0.58f }, { 0.26f, 0.18f, 0.12f }, { 0.62f, 0.33f, 0.22f },
			{ 0.3f, 0.22f, 0.15f }, { 0.24f, 0.38f, 0.24f }, { 0.38f, 0.25f, 0.15f }, { 0.36f, 0.44f, 0.44f }, { 0.3f, 0.3f, 0.3f } } },
		{ "Dark Fantasy", { { 0.36f, 0.35f, 0.36f }, { 0.42f, 0.4f, 0.4f }, { 0.4f, 0.39f, 0.4f }, { 0.13f, 0.1f, 0.09f }, { 0.26f, 0.27f, 0.31f },
			{ 0.14f, 0.11f, 0.1f }, { 0.25f, 0.12f, 0.12f }, { 0.2f, 0.13f, 0.1f }, { 0.55f, 0.4f, 0.2f }, { 0.25f, 0.25f, 0.26f } } },
		{ "Nordic", { { 0.5f, 0.47f, 0.42f }, { 0.52f, 0.2f, 0.15f }, { 0.62f, 0.6f, 0.56f }, { 0.18f, 0.13f, 0.1f }, { 0.45f, 0.55f, 0.32f },
			{ 0.88f, 0.86f, 0.8f }, { 0.2f, 0.26f, 0.32f }, { 0.25f, 0.18f, 0.13f }, { 0.34f, 0.42f, 0.46f }, { 0.28f, 0.28f, 0.28f } } },
		{ "Mediterranean", { { 0.92f, 0.9f, 0.85f }, { 0.94f, 0.92f, 0.86f }, { 0.85f, 0.8f, 0.7f }, { 0.36f, 0.26f, 0.18f }, { 0.72f, 0.38f, 0.24f },
			{ 0.2f, 0.36f, 0.58f }, { 0.18f, 0.38f, 0.62f }, { 0.22f, 0.34f, 0.52f }, { 0.3f, 0.38f, 0.42f }, { 0.3f, 0.3f, 0.3f } } },
		{ "Baroque Pastel", { { 0.86f, 0.78f, 0.56f }, { 0.9f, 0.82f, 0.6f }, { 0.95f, 0.93f, 0.88f }, { 0.4f, 0.3f, 0.2f }, { 0.42f, 0.44f, 0.48f },
			{ 0.94f, 0.92f, 0.88f }, { 0.4f, 0.48f, 0.42f }, { 0.4f, 0.3f, 0.2f }, { 0.38f, 0.44f, 0.48f }, { 0.4f, 0.62f, 0.52f } } },
		{ "Elven", { { 0.86f, 0.86f, 0.82f }, { 0.9f, 0.9f, 0.86f }, { 0.94f, 0.94f, 0.9f }, { 0.5f, 0.42f, 0.3f }, { 0.36f, 0.56f, 0.5f },
			{ 0.8f, 0.7f, 0.42f }, { 0.4f, 0.55f, 0.4f }, { 0.6f, 0.5f, 0.32f }, { 0.5f, 0.62f, 0.6f }, { 0.85f, 0.72f, 0.36f } } },
		{ "Royal", { { 0.92f, 0.9f, 0.86f }, { 0.94f, 0.92f, 0.88f }, { 0.96f, 0.95f, 0.92f }, { 0.35f, 0.25f, 0.18f }, { 0.36f, 0.4f, 0.48f },
			{ 0.92f, 0.9f, 0.86f }, { 0.22f, 0.3f, 0.52f }, { 0.28f, 0.2f, 0.42f }, { 0.36f, 0.42f, 0.5f }, { 0.86f, 0.7f, 0.3f } } },
		{ "Desert Fort", { { 0.8f, 0.68f, 0.5f }, { 0.84f, 0.72f, 0.54f }, { 0.78f, 0.66f, 0.48f }, { 0.42f, 0.3f, 0.2f }, { 0.7f, 0.5f, 0.32f },
			{ 0.4f, 0.28f, 0.18f }, { 0.32f, 0.44f, 0.5f }, { 0.42f, 0.28f, 0.16f }, { 0.36f, 0.42f, 0.42f }, { 0.55f, 0.45f, 0.3f } } },
	};
	float* ColorRef(BD& d, int i)
	{
		float* refs[12] = { d.WallColor, d.UpperColor, d.TrimColor, d.TimberColor, d.RoofColor, d.FrameColor, d.ShutterColor, d.DoorColor, d.GlassColor, d.MetalColor,
			d.OrnamentColor, d.BannerColor };
		return refs[i];
	}
	const char* kColorNames[12] = { "Walls (ground floor)", "Walls (upper floors)", "Stone Trim", "Timber Beams", "Roof", "Window Frames", "Shutters", "Door", "Glass", "Metal (copper / lead)",
		"Painted Ornament (white = original)", "Banners" };
	const char* kBargeboards[] = { "None", "Plain", "Carved (crossed horns)" };
	const char* kProps[] = { "None", "Firewood & barrels", "Busy (+ crates)" };
}

// ------------------------------------------------------------------ 렌더 타깃
// ------------------------------------------------------------------ 창
BuildingGeneratorWindow::BuildingGeneratorWindow()
	: EditorWindow("Building Generator", ICON_FA_CHURCH)
{
	s_Instance = this;
	SetIsOpened(false);
	m_Desc.ApplyStyle(0);
}

BuildingGeneratorWindow::~BuildingGeneratorWindow()
{
	if (s_Instance == this)
		s_Instance = nullptr;
}

void BuildingGeneratorWindow::Open(Building* target, int step)
{
	if (!s_Instance)
		return;
	if (target)
	{
		s_Instance->m_Desc = target->Desc;
		s_Instance->m_Live = true;
		if (GameObject* go = target->GetGameObject())
		{
			s_Instance->m_Name = go->GetName();
			SelectionManager::SetSelectedGameObject(go);
		}
		s_Instance->m_Step = 1;
		s_Instance->Changed();
		s_Instance->m_LiveRevision = s_Instance->m_Revision;   // 연 순간에는 고치지 않는다
	}
	if (step >= 0)
		s_Instance->m_Step = std::clamp(step, 0, kStepCount - 1);
	s_Instance->SetIsOpened(true);
	s_Instance->m_FocusFrames = 3;
}

void BuildingGeneratorWindow::BeforeBegin()
{
	if (m_FocusFrames > 0)
	{
		ImGui::SetNextWindowFocus();
		--m_FocusFrames;
	}
	ImGui::SetNextWindowSize(ImVec2(1280.0f, 800.0f), ImGuiCond_FirstUseEver);
	if (EditorWindow* scene = EditorGUIManager::GetI()->FindWindow("Scene"))
		if (ImGuiWindow* w = ImGui::FindWindowByName(scene->GetImGuiName().c_str()); w && w->DockId)
			ImGui::SetNextWindowDockID(w->DockId, ImGuiCond_FirstUseEver);
}

Building* BuildingGeneratorWindow::SelectedBuilding() const
{
	GameObject* go = SelectionManager::GetSelectedGameObject();
	return go ? go->GetComponent<Building>() : nullptr;
}

void BuildingGeneratorWindow::EnsureGround()
{
	if (m_Ground)
		return;
	// 땅 (200 m 판) — 잔디 재질
	BuildingGenerator::Output out;
	auto& part = out.Parts[0];
	const float s = 100.0f;
	const XMFLOAT3 p[4] = { { -s, 0, -s }, { s, 0, -s }, { s, 0, s }, { -s, 0, s } };
	for (const XMFLOAT3& q : p)
	{
		Vertex::PosNormalTexTan2 v;
		v.pos = q;
		v.normal = XMFLOAT3(0, 1, 0);
		v.tex = XMFLOAT2(q.x, -q.z);
		v.tangentU = XMFLOAT4(1, 0, 0, 1);
		part.V.push_back(v);
	}
	part.I = { 0, 2, 1, 0, 3, 2 };
	m_Ground = Building::MakeMesh(out, "Building Preview Ground");
	const float grass[3] = { 0.42f, 0.5f, 0.3f };
	if (const TextureLibrary::Material* lib = TextureLibrary::Find("ground/wild-grass"))
		m_GroundMat = BuildingTextures::MaterialById(*lib, BuildingTextures::Kind::Turf, grass, 0, 0.0f);
	if (!m_GroundMat)
		m_GroundMat = BuildingTextures::Material(BuildingTextures::Kind::Turf, grass);
}

void BuildingGeneratorWindow::Rebuild()
{
	const auto t0 = std::chrono::steady_clock::now();
	BuildingGenerator::Output out;
	BuildingGenerator::Generate(m_Desc, out);
	m_Mesh = Building::MakeMesh(out, "Building Preview");
	m_Mats = Building::MakeMaterials(m_Desc);
	m_Min = out.Min;
	m_Max = out.Max;
	m_Tris = (int)out.Triangles();
	m_Windows = out.Windows;
	m_Doors = out.Doors;
	m_BuildMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
	m_BuiltRevision = m_Revision;
	m_LastBuild = Now();
}

void BuildingGeneratorWindow::ApplyLive()
{
	m_LiveRevision = m_Revision;
	Building* b = SelectedBuilding();
	if (b == nullptr)
		return;
	Undo::SetActionName("Building Generator");
	b->SetDesc(m_Desc);
	Undo::Touch(b->GetGameObject());
}

void BuildingGeneratorWindow::RenderTo(MeshPreview::Target& t, const std::shared_ptr<Mesh>& mesh, const std::vector<std::shared_ptr<UMaterial>>& mats,
	const XMFLOAT3& mn, const XMFLOAT3& mx, float yaw, float pitch, float zoom)
{
	if (!mesh)
		return;
	EnsureGround();
	std::vector<MeshPreview::Item> items;
	if (m_Ground && m_GroundMat)
		items.push_back({ m_Ground, { m_GroundMat }, MeshPreview::Identity() });
	items.push_back({ mesh, mats, MeshPreview::Identity() });
	MeshPreview::View v;
	v.Min = mn;
	v.Max = mx;
	v.Yaw = yaw;
	v.Pitch = pitch;
	v.Zoom = zoom;
	MeshPreview::Render(t, items, v);
}

// ------------------------------------------------------------------ 프리셋
bool BuildingGeneratorWindow::SavePreset(const std::string& name, const BuildingDesc& d, std::string& path)
{
	std::string clean;
	for (char ch : name)
		clean += (std::isalnum((unsigned char)ch) || ch == ' ' || ch == '-' || ch == '_') ? ch : '_';
	if (clean.empty())
		clean = "Building";
	const std::string rel = "Assets\\Buildings\\" + clean + ".building";
	const std::string full = PathManager::GetI()->GetContentPathS() + rel;
	std::error_code ec;
	std::filesystem::create_directories(std::filesystem::path(full).parent_path(), ec);
	std::ofstream os(full);
	if (!os)
		return false;
	json j = d.ToJson();
	j["name"] = name;
	os << j.dump(2);
	path = rel;
	return true;
}

bool BuildingGeneratorWindow::LoadPreset(const std::string& relPath, BuildingDesc& d)
{
	std::ifstream is(PathManager::GetI()->GetContentPathS() + relPath);
	if (!is)
		return false;
	json j = json::parse(is, nullptr, false);
	if (!j.is_object())
		return false;
	d.FromJson(j);
	return true;
}

std::vector<std::string> BuildingGeneratorWindow::ListPresets()
{
	std::vector<std::string> out;
	const std::string root = PathManager::GetI()->GetContentPathS();
	std::error_code ec;
	const std::filesystem::path dir = std::filesystem::path(root) / "Assets" / "Buildings";
	if (!std::filesystem::exists(dir, ec))
		return out;
	for (auto& e : std::filesystem::directory_iterator(dir, ec))
		if (e.is_regular_file() && _stricmp(e.path().extension().string().c_str(), ".building") == 0)
			out.push_back("Assets\\Buildings\\" + e.path().filename().string());
	std::sort(out.begin(), out.end());
	return out;
}

// ------------------------------------------------------------------ 그리기
void BuildingGeneratorWindow::OnRender()
{
	const size_t before = m_Desc.Hash();
	const ImVec2 avail = ImGui::GetContentRegionAvail();
	const float left = std::clamp(avail.x * 0.36f, 360.0f, 520.0f);
	DrawSteps(left, avail.y);
	ImGui::SameLine(0.0f, 0.0f);
	if (m_Desc.Hash() != before)
		Changed();
	// 다시 짓기 (끄는 동안엔 1 초에 15 번까지)
	if (m_BuiltRevision != m_Revision && (Now() - m_LastBuild > 0.066 || !m_Mesh))
		Rebuild();
	if (m_Live && m_LiveRevision != m_Revision && m_BuiltRevision == m_Revision)
		ApplyLive();
	DrawPreview(avail.x - left, avail.y);
}

void BuildingGeneratorWindow::DrawSteps(float width, float height)
{
	ImGui::BeginChild("##bgsteps", ImVec2(width, height), true);
	// 단계 표시 (번호 단추)
	for (int i = 0; i < kStepCount; ++i)
	{
		if (i)
			ImGui::SameLine(0.0f, 3.0f);
		char label[16];
		snprintf(label, sizeof(label), "%d##step%d", i + 1, i);
		const bool cur = i == m_Step;
		if (cur)
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.17f, 0.4f, 0.68f, 1.0f));
		if (ImGui::Button(label, ImVec2((width - 16.0f - 3.0f * (kStepCount - 1)) / kStepCount, 24.0f)))
			m_Step = i;
		if (cur)
			ImGui::PopStyleColor();
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", kSteps[i]);
	}
	ImGui::Spacing();
	ImGui::SetWindowFontScale(1.25f);
	ImGui::Text("%d. %s", m_Step + 1, kSteps[m_Step]);
	ImGui::SetWindowFontScale(1.0f);
	ImGui::Separator();

	const float footer = 40.0f;
	ImGui::BeginChild("##bgstep", ImVec2(0.0f, ImGui::GetContentRegionAvail().y - footer), false);
	ImGui::PushItemWidth(-150.0f);
	switch (m_Step)
	{
	case 0: DrawStyleStep(); break;
	case 1: DrawShapeStep(); break;
	case 2: DrawWallsStep(); break;
	case 3: DrawWindowsStep(); break;
	case 4: DrawRoofStep(); break;
	case 5: DrawDetailsStep(); break;
	case 6: DrawColorsStep(); break;
	default: DrawFinishStep(); break;
	}
	ImGui::PopItemWidth();
	ImGui::EndChild();

	// Back / Next
	ImGui::Separator();
	const float bw = 110.0f;
	ImGui::BeginDisabled(m_Step == 0);
	if (ImGui::Button(ICON_FA_ARROW_LEFT " Back", ImVec2(bw, 26.0f)))
		--m_Step;
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::TextDisabled("%s", m_Live && SelectedBuilding() ? "Live: editing the selected building" : "");
	ImGui::SameLine(ImGui::GetContentRegionMax().x - bw);
	if (m_Step < kStepCount - 1)
	{
		if (ImGui::Button("Next " ICON_FA_ARROW_RIGHT, ImVec2(bw, 26.0f)))
			++m_Step;
	}
	else if (ImGui::Button(ICON_FA_PLUS " Create", ImVec2(bw, 26.0f)))
	{
		GameObject* go = Building::CreateObject(m_Desc, m_Name);
		if (Scene* scene = SceneManager::GetI()->GetCurrentScene(); scene && go)
		{
			go->SetName(m_Name);
			go->GetTransform()->SetPosition(Vec3(m_PlaceX, 0.0f, 0.0f));
			m_PlaceX += (m_Max.x - m_Min.x) + 6.0f;
			Undo::SetActionName("Create Building");
			scene->AddRootGameObject(go);
			SelectionManager::SetSelectedGameObject(go);
			m_Message = "Created '" + m_Name + "'";
		}
	}
	ImGui::EndChild();
}

void BuildingGeneratorWindow::DrawStyleStep()
{
	Hint("Pick a style to start from - every value can be changed in the next steps. Medieval fantasy (cottage, tavern, castle, wizard tower) and European styles (Roman, Gothic, Baroque, Nordic, Alpine, Mediterranean).");
	int count = 0;
	const char* const* names = BD::StyleNames(count);
	if ((int)m_Thumbs.size() != count)
		m_Thumbs.resize(count);
	// 보일 때 한 프레임에 썸네일 하나 (그 양식 그대로 지어 굽는다)
	for (int s = 0; s < count; ++s)
		if (!m_Thumbs[s].Done)
		{
			BD d;
			d.ApplyStyle(s);
			BuildingGenerator::Output out;
			BuildingGenerator::Generate(d, out);
			auto mesh = Building::MakeMesh(out, "Building Thumbnail");
			if (mesh && m_Thumbs[s].T.Ensure(256, 192))
				RenderTo(m_Thumbs[s].T, mesh, Building::MakeMaterials(d), out.Min, out.Max, -32.0f, 16.0f, 1.0f);
			m_Thumbs[s].Done = true;
			break;
		}
	const float cardW = 128.0f, cardH = 96.0f;
	const int cols = (std::max)(1, (int)((ImGui::GetContentRegionAvail().x + 8.0f) / (cardW + 8.0f)));
	for (int s = 0; s < count; ++s)
	{
		if (s % cols)
			ImGui::SameLine(0.0f, 8.0f);
		ImGui::BeginGroup();
		ImGui::PushID(s);
		const bool cur = m_Desc.Style == s;
		const ImVec2 p = ImGui::GetCursorScreenPos();
		bool clicked;
		if (m_Thumbs[s].Done && m_Thumbs[s].T.Srv)
			clicked = ImGui::ImageButton("##thumb", (ImTextureID)m_Thumbs[s].T.Srv.Get(), ImVec2(cardW, cardH));
		else
			clicked = ImGui::Button("...", ImVec2(cardW + 8.0f, cardH + 6.0f));
		if (cur)
			ImGui::GetWindowDrawList()->AddRect(ImVec2(p.x - 1, p.y - 1), ImVec2(p.x + cardW + 9, p.y + cardH + 7), IM_COL32(80, 160, 255, 255), 3.0f, 0, 2.5f);
		if (clicked)
		{
			m_Desc.ApplyStyle(s);
			m_Name = names[s];
		}
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("%s\n%s", names[s], BD::StyleDescription(s));
		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cardW + 8.0f);
		ImGui::TextUnformatted(names[s]);
		ImGui::PopTextWrapPos();
		ImGui::PopID();
		ImGui::EndGroup();
	}
	ImGui::Spacing();
	if (m_Desc.Style >= 0 && m_Desc.Style < count)
	{
		Section(names[m_Desc.Style]);
		ImGui::TextWrapped("%s", BD::StyleDescription(m_Desc.Style));
	}
}

void BuildingGeneratorWindow::DrawShapeStep()
{
	BD& d = m_Desc;
	Hint("Floor plan and size. The front (door side) faces -Z. L / T / U plans add wings, Courtyard builds four ranges around a yard, Cross is a church plan, Round makes a tower or windmill.");
	Section("Plan");
	EnumCombo("Plan", &d.Shape, kPlans, (int)BD::Plan::Count);
	SliderF("Width (m)", &d.Width, 3.0f, 60.0f, "%.1f");
	if (d.Shape != BD::Plan::Round)
		SliderF("Depth (m)", &d.Depth, 3.0f, 60.0f, "%.1f");
	if (d.Shape == BD::Plan::LShape || d.Shape == BD::Plan::TShape || d.Shape == BD::Plan::UShape || d.Shape == BD::Plan::Cross)
	{
		SliderF("Wing Width", &d.WingWidth, 0.3f, 1.2f);
		SliderF("Wing Length", &d.WingLength, 0.3f, 1.5f);
	}
	if (d.Shape == BD::Plan::Round)
		SliderF("Taper", &d.Taper, 0.0f, 0.5f);
	Section("Floors");
	SliderI("Floors", &d.Floors, 1, 8);
	SliderF("Floor Height (m)", &d.FloorHeight, 2.2f, 8.0f, "%.2f");
	SliderF("Plinth (m)", &d.Plinth, 0.0f, 2.5f);
	SliderF("Jetty (m)", &d.Jetty, 0.0f, 0.8f);
	Hint("Jetty: each upper floor overhangs the one below (Tudor and medieval town houses).");
	Section("Corner Towers");
	int tc = 0;
	for (int i = 0; i < 4; ++i)
		if (kTowerCountValues[i] == d.Towers) tc = i;
	ImGui::SetNextItemWidth(-150.0f);
	if (ImGui::Combo("Towers", &tc, kTowerCounts, 4))
		d.Towers = kTowerCountValues[tc];
	if (d.Towers > 0)
	{
		EnumCombo("Tower Shape", &d.TowerKind, kTowerShapes, (int)BD::TowerShape::Count);
		SliderF("Tower Radius", &d.TowerRadius, 1.0f, 6.0f);
		SliderF("Extra Height", &d.TowerExtra, 0.0f, 20.0f, "%.1f");
		EnumCombo("Tower Roof", &d.TowerRoof, kRoofs, (int)BD::Roof::Count);
	}
	ImGui::Spacing();
	ImGui::TextDisabled("Wall height %.1f m", d.WallHeight());
}

void BuildingGeneratorWindow::DrawWallsStep()
{
	BD& d = m_Desc;
	Hint("Wall materials per floor. Plaster upper floors can carry half-timbering (Fachwerk). Materials are procedural textures with normal maps - the colors are set in step 7.");
	Section("Materials");
	EnumCombo("Ground Floor", &d.GroundWall, kWallMats, (int)BD::WallMat::Count);
	EnumCombo("Upper Floors", &d.UpperWall, kWallMats, (int)BD::WallMat::Count);
	Section("Half-Timbering");
	EnumCombo("Pattern", &d.Frame, kTimber, (int)BD::Timber::Count);
	if (d.Frame != BD::Timber::None)
	{
		SliderF("Beam Width", &d.TimberWidth, 0.1f, 0.4f);
		if (d.UpperWall != BD::WallMat::Plaster && d.UpperWall != BD::WallMat::Stucco && d.GroundWall != BD::WallMat::Plaster)
			Hint("Timbering shows on Plaster or Stucco walls.");
	}
	Section("Stonework");
	ImGui::Checkbox("Quoins (corner stones)", &d.Quoins);
	ImGui::Checkbox("String Course (band between floors)", &d.StringCourse);
	ImGui::Checkbox("Cornice (eaves moulding)", &d.Cornice);
	ImGui::Checkbox("Pilasters (flat columns)", &d.Pilasters);
	ImGui::Checkbox("Buttresses (Gothic)", &d.Buttresses);
}

void BuildingGeneratorWindow::DrawWindowsStep()
{
	BD& d = m_Desc;
	Hint("Window and door shapes. Openings are cut into the walls with depth, frames, sills and glass.");
	Section("Windows");
	EnumCombo("Upper Windows", &d.WindowStyle, kWindows, (int)BD::Window::Count);
	EnumCombo("Ground Windows", &d.GroundWindowStyle, kWindows, (int)BD::Window::Count);
	SliderF("Width (m)", &d.WindowWidth, 0.3f, 3.0f);
	SliderF("Height (m)", &d.WindowHeight, 0.4f, 6.0f);
	SliderF("Spacing (m)", &d.WindowSpacing, 1.2f, 8.0f);
	SliderF("Sill Height (m)", &d.SillHeight, 0.2f, 2.5f);
	ImGui::Checkbox("Shutters", &d.Shutters);
	ImGui::SameLine(200.0f);
	ImGui::Checkbox("Leaded Glass", &d.Lattice);
	ImGui::Checkbox("Pediments (Baroque)", &d.WindowPediments);
	ImGui::SameLine(200.0f);
	ImGui::Checkbox("Rose Window", &d.RoseWindow);
	Section("Door");
	EnumCombo("Door", &d.DoorStyle, kDoors, (int)BD::Door::Count);
	SliderF("Door Width (m)", &d.DoorWidth, 0.8f, 4.0f);
	SliderF("Door Height (m)", &d.DoorHeight, 1.8f, 6.0f);
}

void BuildingGeneratorWindow::DrawRoofStep()
{
	BD& d = m_Desc;
	Hint("Roof shape and covering. Round plans use the cone, dome, onion, pyramid or flat (battlements) roof.");
	Section("Shape");
	EnumCombo("Roof", &d.RoofStyle, kRoofs, (int)BD::Roof::Count);
	EnumCombo("Covering", &d.RoofMaterial, kRoofMats, (int)BD::RoofMat::Count);
	SliderF("Pitch (deg)", &d.RoofPitch, 5.0f, 75.0f, "%.0f");
	SliderF("Overhang (m)", &d.Overhang, 0.0f, 1.5f);
	Section("Roof Details");
	SliderI("Dormers", &d.Dormers, 0, 8);
	SliderI("Chimneys", &d.Chimneys, 0, 4);
	ImGui::Checkbox("Crow-Stepped Gables", &d.CrowSteps);
	ImGui::Checkbox("Dragon Heads (Nordic)", &d.DragonHeads);
	ImGui::Checkbox("Central Dome", &d.CentralDome);
	SliderF("Spire (m)", &d.SpireHeight, 0.0f, 30.0f, "%.1f");
}

void BuildingGeneratorWindow::DrawDetailsStep()
{
	BD& d = m_Desc;
	Hint("Classical and fantasy features.");
	Section("Classical");
	ImGui::Checkbox("Portico (front columns + pediment)", &d.Portico);
	if (d.Portico)
		SliderI("Columns", &d.PorticoColumns, 2, 10);
	ImGui::Checkbox("Peristyle (columns all around - temple)", &d.Peristyle);
	ImGui::Checkbox("Open Arcade (ground floor arches)", &d.Arcade);
	Section("Medieval");
	ImGui::Checkbox("Battlements (crenellations)", &d.Crenellations);
	ImGui::Checkbox("Wooden Balcony", &d.Balcony);
	ImGui::Checkbox("Windmill Sails", &d.Sails);
	Section("Timber Craft");
	ImGui::Checkbox("Eave Brackets & Rafter Ends", &d.Brackets);
	ImGui::SetNextItemWidth(-150.0f);
	ImGui::Combo("Bargeboards", &d.Bargeboards, kBargeboards, 3);
	ImGui::Checkbox("Gable Finials", &d.Finials);
	ImGui::SameLine(200.0f);
	ImGui::Checkbox("Painted Gables", &d.PaintedGables);
	ImGui::Checkbox("Heavy Corner Posts", &d.CornerPosts);
	Section("Entrance & Terrace");
	ImGui::Checkbox("Porch (roof on posts)", &d.Porch);
	ImGui::SameLine(200.0f);
	ImGui::Checkbox("Covered Terrace", &d.Terrace);
	ImGui::Checkbox("Door Lanterns", &d.Lanterns);
	ImGui::SameLine(200.0f);
	ImGui::Checkbox("Hanging Shop Sign", &d.ShopSign);
	Section("Decoration");
	SliderI("Banners", &d.Banners, 0, 6);
	ImGui::Checkbox("Flower Boxes", &d.FlowerBoxes);
	ImGui::SetNextItemWidth(-150.0f);
	ImGui::Combo("Props", &d.Props, kProps, 3);
	Section("Age & Character");
	SliderF("Weathering", &d.Weathering, 0.0f, 1.0f);
	Hint("Dirt, water stains and moss rising from the ground on the ground-floor walls.");
	SliderF("Irregularity", &d.Irregularity, 0.0f, 1.0f);
	Hint("Hand-built look: upper floors lean, the ridge sags, walls bow a little, windows and the door sit off the grid.");
	ImGui::Checkbox("Side Annex (lean-to shed)", &d.Annex);
	Section("Variation");
	ImGui::SetNextItemWidth(-150.0f);
	ImGui::InputInt("Seed", &d.Seed);
	if (ImGui::Button(ICON_FA_DICE " Randomize within style", ImVec2(-1.0f, 0.0f)))
		d.Randomize(d.Seed + 1);
	Hint("Randomize keeps the style and varies size, floors, pitch, dormers and colors - use it to fill a village with houses that do not repeat.");
}

void BuildingGeneratorWindow::DrawColorsStep()
{
	BD& d = m_Desc;
	Hint("Colors multiply the procedural material textures. Start from a palette, then adjust each part.");
	Section("Palettes");
	const float bw = (ImGui::GetContentRegionAvail().x - 8.0f) * 0.5f;
	for (int i = 0; i < (int)(sizeof(kPalettes) / sizeof(kPalettes[0])); ++i)
	{
		if (i % 2)
			ImGui::SameLine(0.0f, 8.0f);
		ImGui::PushID(i);
		const ImVec2 p = ImGui::GetCursorScreenPos();
		if (ImGui::Button("##pal", ImVec2(bw, 34.0f)))
			for (int k = 0; k < 10; ++k)
				Rgb(ColorRef(d, k), kPalettes[i].C[k][0], kPalettes[i].C[k][1], kPalettes[i].C[k][2]);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const int order[6] = { 0, 1, 4, 3, 6, 7 };
		for (int k = 0; k < 6; ++k)
		{
			const float* c = kPalettes[i].C[order[k]];
			dl->AddRectFilled(ImVec2(p.x + 6 + k * 14.0f, p.y + 18), ImVec2(p.x + 18 + k * 14.0f, p.y + 30), ImGui::ColorConvertFloat4ToU32(ImVec4(c[0], c[1], c[2], 1.0f)), 2.0f);
		}
		dl->AddText(ImVec2(p.x + 6, p.y + 2), IM_COL32(220, 220, 220, 255), kPalettes[i].Name);
		ImGui::PopID();
	}
	Section("Parts");
	for (int k = 0; k < 12; ++k)
	{
		ImGui::SetNextItemWidth(-150.0f);
		ImGui::ColorEdit3(kColorNames[k], ColorRef(d, k), ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);
	}
	DrawMaterials();
}

// 칸마다 텍스처 팩 재질 (어울리는 분류 먼저, 그 밖은 아래). "Style default" = 벽 · 지붕 재질 종류의 기본 id
void BuildingGeneratorWindow::DrawMaterials()
{
	BD& d = m_Desc;
	Section("Materials (Texture Pack)");
	if (TextureLibrary::All().empty())
	{
		Hint("No texture pack in the project — the built-in procedural materials are used. Add com.nova.textures in Window > Package Manager.");
		return;
	}
	Hint("Photo-scanned CC0 materials (ambientCG) from the texture pack, used as they are (their own color, normal, roughness, AO and height - close-up stone, tiles and thatch are displaced by tessellation). The colors above tint only the built-in materials (glass, banners, painted ornament, flowers). The id is what the CLI and presets store.");
	const int slots[] = { BuildingGenerator::WallLower, BuildingGenerator::WallUpper, BuildingGenerator::Trim, BuildingGenerator::Roof, BuildingGenerator::Timber,
		BuildingGenerator::Frame, BuildingGenerator::Door, BuildingGenerator::Shutter, BuildingGenerator::Metal };
	for (int slot : slots)
	{
		const std::string key = BuildingGenerator::SlotKey(slot);
		auto it = d.Materials.find(key);
		const bool overridden = it != d.Materials.end() && !it->second.empty();
		const std::string cur = BuildingTextures::SlotMaterialId(d, slot);
		const TextureLibrary::Material* curLib = TextureLibrary::Find(cur);
		std::string preview = curLib ? curLib->Name : cur;
		if (!overridden)
			preview = "Style default: " + preview;
		ImGui::SetNextItemWidth(-150.0f);
		if (ImGui::BeginCombo(BuildingGenerator::SlotName(slot), preview.c_str()))
		{
			if (ImGui::Selectable("Style default", !overridden))
				d.Materials.erase(key);
			if (ImGui::Selectable("Procedural (built-in)", overridden && it->second == "procedural"))
				d.Materials[key] = "procedural";
			const std::vector<std::string> cats = BuildingTextures::SlotCategories(slot);
			for (int pass = 0; pass < 2; ++pass)
			{
				ImGui::Separator();
				for (const TextureLibrary::Material& m : TextureLibrary::All())
				{
					const bool suited = std::find(cats.begin(), cats.end(), m.Category) != cats.end();
					if (suited != (pass == 0))
						continue;
					const std::string label = m.Name + "  (" + m.Id + ")";
					if (ImGui::Selectable(label.c_str(), overridden && it->second == m.Id))
						d.Materials[key] = m.Id;
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("%s", m.Description.c_str());
				}
			}
			ImGui::EndCombo();
		}
	}
}

void BuildingGeneratorWindow::DrawFinishStep()
{
	Hint("Create the building in the open scene, or apply these values to the selected building. Presets are saved to Assets/Buildings as .building files.");
	Section("Scene");
	ImGui::SetNextItemWidth(-150.0f);
	char name[128];
	strncpy_s(name, m_Name.c_str(), _TRUNCATE);
	if (ImGui::InputText("Name", name, sizeof(name)))
		m_Name = name;
	if (ImGui::Button(ICON_FA_PLUS " Create in Scene", ImVec2(-1.0f, 30.0f)))
	{
		GameObject* go = Building::CreateObject(m_Desc, m_Name);
		if (Scene* scene = SceneManager::GetI()->GetCurrentScene(); scene && go)
		{
			go->SetName(m_Name);
			go->GetTransform()->SetPosition(Vec3(m_PlaceX, 0.0f, 0.0f));
			m_PlaceX += (m_Max.x - m_Min.x) + 6.0f;
			Undo::SetActionName("Create Building");
			scene->AddRootGameObject(go);
			SelectionManager::SetSelectedGameObject(go);
			m_Message = "Created '" + m_Name + "'";
		}
	}
	Building* sel = SelectedBuilding();
	ImGui::BeginDisabled(sel == nullptr);
	if (ImGui::Button(ICON_FA_CHECK " Apply to Selected Building", ImVec2(-1.0f, 26.0f)) && sel)
	{
		ApplyLive();
		m_Message = "Applied to '" + sel->GetGameObject()->GetName() + "'";
	}
	ImGui::EndDisabled();
	ImGui::Checkbox("Live (edit the selected building while changing values)", &m_Live);
	Section("Presets");
	ImGui::SetNextItemWidth(-150.0f);
	char preset[128];
	strncpy_s(preset, m_PresetName.c_str(), _TRUNCATE);
	if (ImGui::InputText("Preset Name", preset, sizeof(preset)))
		m_PresetName = preset;
	if (ImGui::Button(ICON_FA_FLOPPY_DISK " Save Preset", ImVec2(-1.0f, 0.0f)))
	{
		std::string path;
		m_Message = SavePreset(m_PresetName, m_Desc, path) ? "Saved " + path : "Could not save the preset";
	}
	const std::vector<std::string> presets = ListPresets();
	if (presets.empty())
		ImGui::TextDisabled("No presets in Assets/Buildings yet.");
	for (const std::string& p : presets)
	{
		ImGui::PushID(p.c_str());
		if (ImGui::Selectable(std::filesystem::path(p).stem().string().c_str()))
		{
			BD d = m_Desc;
			if (LoadPreset(p, d))
			{
				m_Desc = d;
				m_Message = "Loaded " + p;
			}
		}
		ImGui::PopID();
	}
	if (!m_Message.empty())
	{
		ImGui::Spacing();
		ImGui::TextWrapped("%s", m_Message.c_str());
	}
}

void BuildingGeneratorWindow::DrawPreview(float width, float height)
{
	ImGui::BeginChild("##bgpreview", ImVec2(width, height), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	// 위 도구 줄
	ImGui::SetCursorPos(ImVec2(8.0f, 6.0f));
	if (ImGui::Button(ICON_FA_DICE " Randomize"))
		m_Desc.Randomize(m_Desc.Seed + 1);
	ImGui::SameLine();
	if (ImGui::Button(ICON_FA_ROTATE_LEFT " Reset to Style"))
		m_Desc.ApplyStyle(m_Desc.Style);
	ImGui::SameLine();
	if (ImGui::Button("Front")) { m_Yaw = 0.0f; m_Pitch = 6.0f; }
	ImGui::SameLine();
	if (ImGui::Button("Corner")) { m_Yaw = -32.0f; m_Pitch = 18.0f; }
	ImGui::SameLine();
	if (ImGui::Button("Top")) { m_Yaw = -20.0f; m_Pitch = 62.0f; }
	ImGui::SameLine();
	ImGui::Checkbox("Turntable", &m_AutoRotate);
	const float top = 36.0f, bottom = 26.0f;
	const float w = (std::max)(64.0f, width - 16.0f), h = (std::max)(64.0f, height - top - bottom);
	ImGui::SetCursorPos(ImVec2(8.0f, top));
	// 2 배로 그려 줄여 보인다 (가장자리 계단 없이)
	const int tw = ((int)(w * 2.0f) + 31) / 32 * 32, th = ((int)(h * 2.0f) + 31) / 32 * 32;
	if (m_AutoRotate)
		m_Yaw = fmodf(m_Yaw + ImGui::GetIO().DeltaTime * 12.0f + 540.0f, 360.0f) - 180.0f;
	if (m_Mesh && m_View.Ensure(tw, th))
	{
		RenderTo(m_View, m_Mesh, m_Mats, m_Min, m_Max, m_Yaw, m_Pitch, m_Zoom);
		ImGui::Image((ImTextureID)m_View.Srv.Get(), ImVec2(w, h), ImVec2(0, 0), ImVec2(w * 2.0f / tw, h * 2.0f / th));
	}
	else
		ImGui::Dummy(ImVec2(w, h));
	if (ImGui::IsItemHovered())
	{
		const float wheel = ImGui::GetIO().MouseWheel;
		if (wheel != 0.0f)
			m_Zoom = std::clamp(m_Zoom * (wheel > 0.0f ? 1.12f : 1.0f / 1.12f), 0.3f, 8.0f);
		if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f))
		{
			const ImVec2 d = ImGui::GetIO().MouseDelta;
			m_Yaw = fmodf(m_Yaw + d.x * 0.4f + 540.0f, 360.0f) - 180.0f;
			m_Pitch = std::clamp(m_Pitch + d.y * 0.3f, -5.0f, 85.0f);
		}
	}
	// 아래 정보 줄
	int count = 0;
	const char* const* names = BD::StyleNames(count);
	ImGui::SetCursorPos(ImVec2(10.0f, top + h + 5.0f));
	ImGui::TextDisabled("%s  |  %.1f x %.1f x %.1f m  |  %d triangles  |  %d windows, %d doors  |  built in %.1f ms",
		m_Desc.Style >= 0 && m_Desc.Style < count ? names[m_Desc.Style] : "Custom",
		m_Max.x - m_Min.x, m_Max.z - m_Min.z, m_Max.y - m_Min.y, m_Tris, m_Windows, m_Doors, m_BuildMs);
	ImGui::EndChild();
}

void BuildingGeneratorWindow::UnregisterEditor()
{
	if (s_Instance)
	{
		BuildingGeneratorWindow* w = s_Instance;
		EditorGUIManager::GetI()->UnregisterWindow(w);
		delete w;
	}
}

void BuildingGeneratorWindow::RegisterEditor(const char* owner)
{
	EditorGUIManager::GetI()->RegisterWindow(new BuildingGeneratorWindow);
	Building::s_OpenGenerator = [](Building* b) { BuildingGeneratorWindow::Open(b); };
	EditorExtensions::AssetType t;
	t.Owner = owner;
	t.Extension = ".building";
	t.Icon = "prefab";
	t.CreateMenu = "Building Preset";
	t.DefaultName = "New Building";
	t.Create = [](const std::string& path) {
		BuildingDesc d;
		d.ApplyStyle(0);
		std::ofstream os(PathManager::GetI()->GetContentPathS() + path);
		os << d.ToJson().dump(2);
	};
	t.Open = [](const std::string& path) {
		if (!s_Instance)
			return;
		BuildingDesc d;
		if (LoadPreset(path, d))
		{
			s_Instance->m_Desc = d;
			s_Instance->m_Live = false;
			s_Instance->m_PresetName = std::filesystem::path(path).stem().string();
		}
		Open(nullptr, 1);
	};
	t.Inspector = [](const std::string& path) {
		BuildingDesc d;
		if (!LoadPreset(path, d))
		{
			UnityGUI::HelpBox("Not a building preset", true);
			return;
		}
		int count = 0;
		const char* const* names = BuildingDesc::StyleNames(count);
		UnityGUI::ValueLabel("Style", d.Style >= 0 && d.Style < count ? names[d.Style] : "Custom");
		char text[96];
		snprintf(text, sizeof(text), "%.1f x %.1f m, %d floors", d.Width, d.Depth, d.Floors);
		UnityGUI::ValueLabel("Size", text);
		if (UnityGUI::CenterButton("Open Building Generator"))
		{
			if (s_Instance)
			{
				s_Instance->m_Desc = d;
				s_Instance->m_Live = false;
			}
			Open(nullptr, 1);
		}
		if (UnityGUI::CenterButton("Create in Scene"))
			if (Scene* scene = SceneManager::GetI()->GetCurrentScene())
				if (GameObject* go = Building::CreateObject(d, std::filesystem::path(path).stem().string()))
				{
					go->SetName(std::filesystem::path(path).stem().string());
					Undo::SetActionName("Create Building");
					scene->AddRootGameObject(go);
					SelectionManager::SetSelectedGameObject(go);
				}
	};
	EditorExtensions::RegisterAssetType(t);
}
