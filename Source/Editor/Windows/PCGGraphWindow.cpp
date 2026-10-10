#include "pch.h"
#include "PCGGraphWindow.h"
#include "PCGVolume.h"
#include "EditorExtensions.h"
#include "EditorGUIManager.h"
#include "SelectionManager.h"
#include "PathManager.h"
#include "UnityGUI.h"
#include "EditorLog.h"
#include "ImGui/imgui_internal.h"
#include "IconsFontAwesome/IconsFontAwesome6.h"
#include "ImGuiNodeEditor/imgui_node_editor.h"

namespace ed = ax::NodeEditor;

PCGGraphWindow* PCGGraphWindow::s_Instance = nullptr;

namespace
{
	// 아이디: 노드 = Id, 핀 = (1<<20) + Id*16 + (0 출력, 1.. 입력), 선 = (1<<26) + 차례
	ed::NodeId NodeOf(int id) { return ed::NodeId((uintptr_t)id); }
	ed::PinId OutPin(int id) { return ed::PinId((uintptr_t)((1 << 20) + id * 16)); }
	ed::PinId InPin(int id, int pin) { return ed::PinId((uintptr_t)((1 << 20) + id * 16 + 1 + pin)); }
	ed::LinkId LinkOf(int index) { return ed::LinkId((uintptr_t)((1 << 26) + index)); }
	// 핀 → (노드, 입력 핀 번호 — 출력이면 -1)
	bool DecodePin(ed::PinId pin, int& node, int& input)
	{
		const uintptr_t v = pin.Get();
		if (v < (1u << 20))
			return false;
		const uintptr_t k = v - (1u << 20);
		node = (int)(k / 16);
		input = (int)(k % 16) - 1;
		return true;
	}

	ImU32 CategoryColor(const char* cat)
	{
		if (strcmp(cat, "Sampler") == 0) return IM_COL32(70, 140, 90, 255);
		if (strcmp(cat, "Filter") == 0) return IM_COL32(70, 110, 170, 255);
		if (strcmp(cat, "Spatial") == 0) return IM_COL32(150, 110, 60, 255);
		return IM_COL32(160, 70, 70, 255);   // Spawner
	}

	PCG::Graph DefaultGraph()
	{
		PCG::Graph g;
		PCG::Node& s = g.Add(PCG::NodeType::SurfaceSampler, 0.0f, 0.0f);
		const int sid = s.Id;
		PCG::Node& f = g.Add(PCG::NodeType::DensityFilter, 280.0f, 0.0f);
		const int fid = f.Id;
		PCG::Node& t = g.Add(PCG::NodeType::TransformPoints, 560.0f, 0.0f);
		const int tid = t.Id;
		PCG::Node& sp = g.Add(PCG::NodeType::StaticMeshSpawner, 840.0f, 0.0f);
		const int spid = sp.Id;
		g.Connect(sid, fid, 0);
		g.Connect(fid, tid, 0);
		g.Connect(tid, spid, 0);
		return g;
	}
}

PCGGraphWindow::PCGGraphWindow()
	: EditorWindow("PCG Graph", ICON_FA_SEEDLING)
{
	s_Instance = this;
	SetIsOpened(false);
}

PCGGraphWindow::~PCGGraphWindow()
{
	if (m_Ctx)
		ed::DestroyEditor(m_Ctx);
	if (s_Instance == this)
		s_Instance = nullptr;
}

void PCGGraphWindow::Open(const std::string& path)
{
	if (!s_Instance)
		return;
	if (!path.empty())
	{
		s_Instance->m_Path = path;
		s_Instance->m_Graph = PCG::LoadGraph(path);
		s_Instance->m_LayoutPending = true;
		s_Instance->m_Selected = 0;
	}
	s_Instance->SetIsOpened(true);
	s_Instance->m_FocusPending = true;
}

void PCGGraphWindow::BeforeBegin()
{
	if (m_FocusPending)
	{
		ImGui::SetNextWindowFocus();
		m_FocusPending = false;
	}
	ImGui::SetNextWindowSize(ImVec2(1400.0f, 820.0f), ImGuiCond_FirstUseEver);
	if (EditorWindow* scene = EditorGUIManager::GetI()->FindWindow("Scene"))
		if (ImGuiWindow* w = ImGui::FindWindowByName(scene->GetImGuiName().c_str()); w && w->DockId)
			ImGui::SetNextWindowDockID(w->DockId, ImGuiCond_FirstUseEver);
}

void PCGGraphWindow::Save()
{
	if (m_Graph && !m_Path.empty())
		PCG::SaveGraph(m_Path, *m_Graph);
	m_SavePending = false;
}

void PCGGraphWindow::OnRender()
{
	// 열린 그래프가 없으면 장면의 첫 PCG Volume 것
	if (!m_Graph)
		for (PCGVolume* v : PCGVolume::All())
			if (!v->GraphPath.empty())
			{
				m_Path = v->GraphPath;
				m_Graph = PCG::LoadGraph(m_Path);
				m_LayoutPending = true;
				break;
			}
	if (ImGui::BeginMenuBar())
	{
		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("Save", "Ctrl+S", false, m_Graph != nullptr))
				Save();
			if (ImGui::MenuItem("Regenerate World"))
				for (PCGVolume* v : PCGVolume::All())
					v->Regenerate();
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Add Node", m_Graph != nullptr))
		{
			for (const char* cat : { "Sampler", "Filter", "Spatial", "Spawner" })
			{
				ImGui::TextDisabled("%s", cat);
				for (const PCG::NodeDesc& d : PCG::NodeDescs())
					if (strcmp(d.Category, cat) == 0 && ImGui::MenuItem(d.Label))
					{
						PCG::Node& n = m_Graph->Add(d.Type, 0.0f, 0.0f);
						m_Selected = n.Id;
						m_LayoutPending = true;
						m_SavePending = true;
					}
			}
			ImGui::EndMenu();
		}
		ImGui::TextDisabled("  %s", m_Path.empty() ? "(no graph — select a PCG Volume or a .pcg asset)" : m_Path.c_str());
		ImGui::EndMenuBar();
	}
	if (!m_Graph)
	{
		ImGui::TextDisabled("  Open a .pcg asset (double-click in Project) or create GameObject > Open World.");
		return;
	}
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
		Save();

	const ImVec2 avail = ImGui::GetContentRegionAvail();
	const float detailsW = 340.0f;
	DrawCanvas((std::max)(100.0f, avail.x - detailsW), avail.y);
	ImGui::SameLine(0.0f, 0.0f);
	DrawDetails(detailsW, avail.y);

	// 끌기 · 값 바꾸기가 끝나면 저장 (마우스를 놓았을 때)
	if (m_SavePending && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
		Save();
}

void PCGGraphWindow::DrawNode(PCG::Node& n)
{
	const PCG::NodeDesc* d = PCG::FindDesc(n.Type);
	if (d == nullptr)
		return;
	ed::PushStyleColor(ed::StyleColor_NodeBorder, n.Enabled ? ImColor(CategoryColor(d->Category)) : ImColor(90, 90, 90, 255));
	ed::BeginNode(NodeOf(n.Id));
	const ImVec2 start = ImGui::GetCursorScreenPos();
	ImGui::PushStyleColor(ImGuiCol_Text, n.Enabled ? IM_COL32(240, 240, 240, 255) : IM_COL32(140, 140, 140, 255));
	ImGui::TextUnformatted(d->Label);
	ImGui::PopStyleColor();
	if (!n.Comment.empty())
		ImGui::TextDisabled("%s", n.Comment.c_str());
	ImGui::Dummy(ImVec2(170.0f, 2.0f));
	// 핀: 입력 (왼쪽) · 출력 (오른쪽)
	const size_t rows = (std::max)(d->Inputs.size(), (size_t)(d->Output ? 1 : 0));
	for (size_t i = 0; i < rows; ++i)
	{
		if (i < d->Inputs.size())
		{
			ed::BeginPin(InPin(n.Id, (int)i), ed::PinKind::Input);
			ImGui::Text(ICON_FA_CIRCLE_DOT " %s", d->Inputs[i]);
			ed::EndPin();
		}
		else
			ImGui::Dummy(ImVec2(60.0f, ImGui::GetTextLineHeight()));
		if (i == 0 && d->Output)
		{
			ImGui::SameLine(130.0f);
			ed::BeginPin(OutPin(n.Id), ed::PinKind::Output);
			ImGui::Text("Out " ICON_FA_CIRCLE_DOT);
			ed::EndPin();
		}
	}
	// 요약 (한두 줄)
	switch (n.Type)
	{
	case PCG::NodeType::SurfaceSampler: ImGui::TextDisabled("%.4g / m2", n.Get("pointsPerSquaredMeter")); break;
	case PCG::NodeType::BiomeFilter:
	{
		static const char* kB[] = { "Forest", "Meadow", "Desert", "Rock" };
		ImGui::TextDisabled("%s >= %.2f", kB[std::clamp((int)n.Get("biome"), 0, 3)], n.Get("minWeight"));
		break;
	}
	case PCG::NodeType::SlopeFilter: ImGui::TextDisabled("%.0f - %.0f deg", n.Get("minAngle"), n.Get("maxAngle")); break;
	case PCG::NodeType::HeightFilter: ImGui::TextDisabled("%.0f - %.0f m", n.Get("minHeight"), n.Get("maxHeight")); break;
	case PCG::NodeType::SelfPruning: ImGui::TextDisabled("radius %.1f m", n.Get("radius")); break;
	case PCG::NodeType::Difference: ImGui::TextDisabled("radius %.1f m", n.Get("radius")); break;
	case PCG::NodeType::StaticMeshSpawner:
	{
		size_t instances = 0;
		for (PCGVolume* v : PCGVolume::All())
			for (const auto& s : v->Info()["spawners"])
				if (s["id"].get<int>() == n.Id)
					instances += s["instances"].get<size_t>();
		ImGui::TextDisabled("%zu meshes, cull %.0f m", n.Meshes.size(), n.Get("cullDistance"));
		ImGui::TextDisabled("%zu instances", instances);
		break;
	}
	default: break;
	}
	ed::EndNode();
	ed::PopStyleColor();
	(void)start;
}

void PCGGraphWindow::DrawCanvas(float width, float height)
{
	if (!m_Ctx)
	{
		ed::Config config;
		config.SettingsFile = nullptr;   // 노드 자리는 .pcg 에
		m_Ctx = ed::CreateEditor(&config);
	}
	ImGui::BeginChild("##pcgcanvas", ImVec2(width, height), false, ImGuiWindowFlags_NoScrollbar);
	ed::SetCurrentEditor(m_Ctx);
	ed::Begin("##pcggraph", ImVec2(width, height));
	PCG::Graph& g = *m_Graph;
	const bool layout = m_LayoutPending;
	if (layout)
		for (const PCG::Node& n : g.Nodes)
			ed::SetNodePosition(NodeOf(n.Id), ImVec2(n.X, n.Y));
	for (PCG::Node& n : g.Nodes)
		DrawNode(n);
	for (size_t i = 0; i < g.Edges.size(); ++i)
		ed::Link(LinkOf((int)i), OutPin(g.Edges[i].From), InPin(g.Edges[i].To, g.Edges[i].Pin), ImColor(150, 200, 255, 255), 2.0f);
	if (layout)
	{
		ed::NavigateToContent(0.0f);
		m_LayoutPending = false;
	}

	// 잇기
	if (ed::BeginCreate(ImColor(150, 200, 255, 255), 2.0f))
	{
		ed::PinId a, b;
		if (ed::QueryNewLink(&a, &b) && a && b)
		{
			int na, ia, nb, ib;
			if (DecodePin(a, na, ia) && DecodePin(b, nb, ib) && ((ia < 0) != (ib < 0)) && na != nb)
			{
				const int from = ia < 0 ? na : nb, to = ia < 0 ? nb : na, pin = ia < 0 ? ib : ia;
				if (ed::AcceptNewItem())
					if (g.Connect(from, to, pin))
						m_SavePending = true;
			}
			else
				ed::RejectNewItem(ImColor(255, 90, 90, 255), 2.0f);
		}
	}
	ed::EndCreate();
	// 지우기 (Delete · 선 끊기)
	if (ed::BeginDelete())
	{
		ed::LinkId l;
		std::vector<int> drop;
		while (ed::QueryDeletedLink(&l))
			if (ed::AcceptDeletedItem())
			{
				const int index = (int)(l.Get() - (1u << 26));
				if (index >= 0 && index < (int)g.Edges.size())
					drop.push_back(index);
			}
		std::sort(drop.rbegin(), drop.rend());
		for (int index : drop)
			g.Edges.erase(g.Edges.begin() + index);
		if (!drop.empty())
		{
			g.Touch();
			m_SavePending = true;
		}
		ed::NodeId n;
		while (ed::QueryDeletedNode(&n))
			if (ed::AcceptDeletedItem())
			{
				g.Remove((int)n.Get());
				m_SavePending = true;
			}
	}
	ed::EndDelete();

	// 노드 자리 (끌었으면 그래프에 — 다시 만들지는 않는다)
	for (PCG::Node& n : g.Nodes)
	{
		const ImVec2 p = ed::GetNodePosition(NodeOf(n.Id));
		if (fabsf(p.x - n.X) > 0.5f || fabsf(p.y - n.Y) > 0.5f)
		{
			n.X = p.x;
			n.Y = p.y;
			m_SavePending = true;
		}
	}
	// 고르기
	{
		ed::NodeId sel[4];
		const int count = ed::GetSelectedNodes(sel, 4);
		if (count > 0)
			m_Selected = (int)sel[0].Get();
	}
	// 노드 추가 (오른쪽 클릭 · Space)
	ed::Suspend();
	if (ed::ShowBackgroundContextMenu() || (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Space, false)))
	{
		m_AddAt = ed::ScreenToCanvas(ImGui::GetMousePos());
		ImGui::OpenPopup("##pcgadd");
	}
	if (ImGui::BeginPopup("##pcgadd"))
	{
		for (const char* cat : { "Sampler", "Filter", "Spatial", "Spawner" })
		{
			ImGui::TextDisabled("%s", cat);
			for (const PCG::NodeDesc& d : PCG::NodeDescs())
				if (strcmp(d.Category, cat) == 0 && ImGui::MenuItem(d.Label))
				{
					PCG::Node& n = g.Add(d.Type, m_AddAt.x, m_AddAt.y);
					m_Selected = n.Id;
					ed::SetNodePosition(NodeOf(n.Id), m_AddAt);
					m_SavePending = true;
				}
		}
		ImGui::EndPopup();
	}
	ed::Resume();
	ed::End();
	ed::SetCurrentEditor(nullptr);
	ImGui::EndChild();
}

void PCGGraphWindow::DrawDetails(float width, float height)
{
	ImGui::BeginChild("##pcgdetails", ImVec2(width, height), true);
	PCG::Node* n = m_Graph->Find(m_Selected);
	if (n == nullptr)
	{
		ImGui::TextDisabled("Select a node.");
		ImGui::Separator();
		ImGui::TextWrapped("Right-click (or Space) on the canvas to add nodes. Drag from Out to In to connect. Delete removes the selection.");
		ImGui::Separator();
		ImGui::TextWrapped("Every change regenerates the affected cells of the PCG Volumes in the scene right away.");
		ImGui::EndChild();
		return;
	}
	const PCG::NodeDesc* d = PCG::FindDesc(n->Type);
	ImGui::TextUnformatted(d ? d->Label : "?");
	if (d && d->Help)
		ImGui::TextWrapped("%s", d->Help);
	ImGui::Separator();
	bool changed = false;
	if (ImGui::Checkbox("Enabled", &n->Enabled))
		changed = true;
	char comment[128];
	strncpy_s(comment, n->Comment.c_str(), _TRUNCATE);
	if (ImGui::InputText("Name", comment, sizeof(comment)))
	{
		n->Comment = comment;
		m_SavePending = true;
	}
	if (d)
	{
		if (n->Values.size() < d->Params.size())
			for (size_t i = n->Values.size(); i < d->Params.size(); ++i)
				n->Values.push_back(d->Params[i].Default);
		for (size_t i = 0; i < d->Params.size(); ++i)
		{
			const PCG::ParamDesc& p = d->Params[i];
			float& v = n->Values[i];
			ImGui::PushID((int)i);
			switch (p.Kind)
			{
			case PCG::ParamKind::Float:
			{
				const bool logScale = p.Max / (std::max)(p.Min, 1e-6f) > 1000.0f;
				changed |= ImGui::SliderFloat(p.Label, &v, p.Min, p.Max, "%.4g", logScale ? ImGuiSliderFlags_Logarithmic : 0);
				break;
			}
			case PCG::ParamKind::Int:
			{
				int iv = (int)v;
				if (ImGui::InputInt(p.Label, &iv))
				{
					v = (float)std::clamp(iv, (int)p.Min, (int)p.Max);
					changed = true;
				}
				break;
			}
			case PCG::ParamKind::Bool:
			{
				bool b = v > 0.5f;
				if (ImGui::Checkbox(p.Label, &b))
				{
					v = b ? 1.0f : 0.0f;
					changed = true;
				}
				break;
			}
			case PCG::ParamKind::Enum:
			{
				std::vector<std::string> items;
				std::string all = p.Options ? p.Options : "";
				for (size_t s = 0, e; s <= all.size(); s = e + 1)
				{
					e = all.find('|', s);
					if (e == std::string::npos) e = all.size();
					items.push_back(all.substr(s, e - s));
				}
				int idx = std::clamp((int)v, 0, (int)items.size() - 1);
				if (ImGui::BeginCombo(p.Label, items[(size_t)idx].c_str()))
				{
					for (int k = 0; k < (int)items.size(); ++k)
						if (ImGui::Selectable(items[(size_t)k].c_str(), k == idx))
						{
							v = (float)k;
							changed = true;
						}
					ImGui::EndCombo();
				}
				break;
			}
			}
			if (p.Tooltip && p.Tooltip[0] && ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", p.Tooltip);
			ImGui::PopID();
		}
	}
	if (n->Type == PCG::NodeType::StaticMeshSpawner)
	{
		ImGui::Separator();
		ImGui::Text("Meshes (%zu)", n->Meshes.size());
		for (size_t i = 0; i < n->Meshes.size();)
		{
			ImGui::PushID((int)i + 1000);
			const std::string name = std::filesystem::path(n->Meshes[i].Path).stem().string();
			ImGui::SetNextItemWidth(70.0f);
			changed |= ImGui::DragFloat("##w", &n->Meshes[i].Weight, 0.02f, 0.0f, 100.0f, "%.2f");
			ImGui::SameLine();
			ImGui::TextUnformatted(name.c_str());
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", n->Meshes[i].Path.c_str());
			ImGui::SameLine(width - 40.0f);
			const bool remove = ImGui::SmallButton(ICON_FA_XMARK);
			ImGui::PopID();
			if (remove)
			{
				n->Meshes.erase(n->Meshes.begin() + (long long)i);
				changed = true;
				continue;
			}
			++i;
		}
		// Project 에서 고른 모델 넣기
		const std::wstring sel = SelectionManager::GetSelectedFile();
		std::wstring ext = std::filesystem::path(sel).extension().wstring();
		std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
		const bool model = ext == L".fbx" || ext == L".glb" || ext == L".gltf";
		if (!model)
			ImGui::BeginDisabled();
		if (ImGui::Button("Add Selected Model", ImVec2(-1.0f, 0.0f)))
		{
			n->Meshes.push_back({ wstring_to_string(PathManager::GetI()->GetCutSolutionPath(sel)), 1.0f });
			changed = true;
		}
		if (!model)
			ImGui::EndDisabled();
		// 역할로 한꺼번에 (프로젝트의 모델을 이름으로 나눈 것)
		if (--m_ModelsAge <= 0)
		{
			m_Models = PCG::ScanProjectModels();
			m_ModelsAge = 600;
		}
		if (ImGui::BeginCombo("Add By Role", "..."))
		{
			for (const char* role : { "BigTree", "SmallTree", "Bush", "Fern", "Grass", "ForestCover", "Rock", "Debris", "Mushroom", "Desert", "DesertGrass" })
			{
				int count = 0;
				for (const auto& m : m_Models)
					count += m.first == role;
				char label[64];
				snprintf(label, sizeof(label), "%s (%d)", role, count);
				if (count > 0 && ImGui::Selectable(label))
				{
					for (const auto& m : m_Models)
						if (m.first == role)
							n->Meshes.push_back({ m.second, 1.0f });
					changed = true;
				}
			}
			ImGui::EndCombo();
		}
		if (!n->Meshes.empty() && ImGui::Button("Clear Meshes", ImVec2(-1.0f, 0.0f)))
		{
			n->Meshes.clear();
			changed = true;
		}
	}
	if (changed)
	{
		m_Graph->Touch();   // PCG Volume 이 이 규칙의 셀을 다시 만든다
		m_SavePending = true;
	}
	ImGui::EndChild();
}

void PCGGraphWindow::RegisterEditor()
{
	EditorGUIManager::GetI()->RegisterWindow(new PCGGraphWindow);
	PCGVolume::s_OpenGraphWindow = [](const std::string& path) { PCGGraphWindow::Open(path); };
	EditorExtensions::AssetType t;
	t.Owner = "pcg";
	t.Extension = ".pcg";
	t.Icon = "terrain_paint";
	t.CreateMenu = "PCG Graph";
	t.DefaultName = "New PCG Graph";
	t.Create = [](const std::string& path) { PCG::SaveGraph(path, DefaultGraph()); };
	t.Open = [](const std::string& path) { PCGGraphWindow::Open(path); };
	t.Inspector = [](const std::string& path) {
		auto g = PCG::LoadGraph(path);
		if (!g)
		{
			UnityGUI::HelpBox("Not a PCG graph", true);
			return;
		}
		UnityGUI::ValueLabel("Nodes", std::to_string(g->Nodes.size()).c_str());
		UnityGUI::ValueLabel("Spawners", std::to_string(g->Spawners().size()).c_str());
		if (UnityGUI::CenterButton("Open PCG Graph"))
			PCGGraphWindow::Open(path);
	};
	EditorExtensions::RegisterAssetType(t);
}
