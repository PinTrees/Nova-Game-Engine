#include "pch.h"
#include "TerrainGraphWindow.h"
#include "TerrainTextureGen.h"
#include "TerrainData.h"
#include "Terrain.h"
#include "WorldTerrain.h"
#include "GameObject.h"
#include "GameObjectFactory.h"
#include "Transform.h"
#include "Scene.h"
#include "SceneManager.h"
#include "SelectionManager.h"
#include "EditorExtensions.h"
#include "EditorGUIManager.h"
#include "PathManager.h"
#include "UnityGUI.h"
#include "EditorLog.h"
#include "JobSystem.h"
#include "WaterBody.h"
#include "ImGui/imgui_internal.h"
#include "IconsFontAwesome/IconsFontAwesome6.h"
#include "ImGuiNodeEditor/imgui_node_editor.h"
#include <thread>

namespace ed = ax::NodeEditor;
namespace TG = TerrainGraph;

TerrainGraphWindow* TerrainGraphWindow::s_Instance = nullptr;

namespace
{
	// 아이디: 노드 = Id, 핀 = (1<<20) + Id*32 + (0..15 출력, 16.. 입력), 선 = (1<<27) + 차례
	ed::NodeId NodeOf(int id) { return ed::NodeId((uintptr_t)id); }
	ed::PinId OutPin(int id, int pin) { return ed::PinId((uintptr_t)((1 << 20) + id * 32 + pin)); }
	ed::PinId InPin(int id, int pin) { return ed::PinId((uintptr_t)((1 << 20) + id * 32 + 16 + pin)); }
	ed::LinkId LinkOf(int index) { return ed::LinkId((uintptr_t)((1 << 27) + index)); }
	bool DecodePin(ed::PinId pin, int& node, int& index, bool& input)
	{
		const uintptr_t v = pin.Get();
		if (v < (1u << 20) || v >= (1u << 27))
			return false;
		const uintptr_t k = v - (1u << 20);
		node = (int)(k / 32);
		const int p = (int)(k % 32);
		input = p >= 16;
		index = input ? p - 16 : p;
		return true;
	}
	ImU32 CategoryColor(const char* cat)
	{
		if (strcmp(cat, "Primitive") == 0) return IM_COL32(80, 150, 90, 255);
		if (strcmp(cat, "Adjust") == 0) return IM_COL32(80, 120, 180, 255);
		if (strcmp(cat, "Simulate") == 0) return IM_COL32(190, 120, 50, 255);
		if (strcmp(cat, "Data") == 0) return IM_COL32(150, 90, 170, 255);
		if (strcmp(cat, "Color") == 0) return IM_COL32(60, 160, 160, 255);
		return IM_COL32(170, 70, 70, 255);
	}
	const char* kCategories[] = { "Primitive", "Adjust", "Simulate", "Data", "Color", "Output" };

	double Now() { return ImGui::GetTime(); }

	bool SaveRgbaPng(const std::vector<uint8_t>& rgba, int w, int h, const std::wstring& file, bool srgb)
	{
		DirectX::Image img = {};
		img.width = w;
		img.height = h;
		img.format = DXGI_FORMAT_R8G8B8A8_UNORM;
		img.rowPitch = (size_t)w * 4;
		img.slicePitch = img.rowPitch * h;
		img.pixels = const_cast<uint8_t*>(rgba.data());
		std::error_code ec;
		std::filesystem::create_directories(std::filesystem::path(file).parent_path(), ec);
		return SUCCEEDED(DirectX::SaveToWICFile(img, srgb ? DirectX::WIC_FLAGS_FORCE_SRGB : DirectX::WIC_FLAGS_NONE, DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), file.c_str()));
	}

	std::string FormatMB(double bytes)
	{
		char b[64];
		if (bytes >= 1024.0 * 1048576.0) snprintf(b, sizeof(b), "%.1f GB", bytes / (1024.0 * 1048576.0));
		else snprintf(b, sizeof(b), "%.0f MB", bytes / 1048576.0);
		return b;
	}
}

TerrainGraphWindow::TerrainGraphWindow()
	: EditorWindow("Terrain Graph", ICON_FA_MOUNTAIN)
{
	s_Instance = this;
	SetIsOpened(false);
	m_Preview = std::make_shared<Preview>();
}

TerrainGraphWindow::~TerrainGraphWindow()
{
	if (m_Preview)
		m_Preview->Cancel = true;
	if (m_Ctx)
		ed::DestroyEditor(m_Ctx);
	if (s_Instance == this)
		s_Instance = nullptr;
}

void TerrainGraphWindow::Open(const std::string& path, int selectNode, int previewPin)
{
	if (!s_Instance)
		return;
	if (!path.empty())
	{
		s_Instance->m_Path = path;
		s_Instance->m_Graph = TG::Load(path);
		s_Instance->m_LayoutPending = true;
		s_Instance->m_Selected = 0;
		s_Instance->m_PreviewRequested = 0;
	}
	if (selectNode > 0)
	{
		s_Instance->m_Selected = selectNode;
		s_Instance->m_SelectPending = selectNode;
		s_Instance->m_PreviewPin = previewPin;
	}
	s_Instance->SetIsOpened(true);
	s_Instance->m_FocusPending = true;
}

void TerrainGraphWindow::BeforeBegin()
{
	if (m_FocusPending)
	{
		ImGui::SetNextWindowFocus();
		m_FocusPending = false;
	}
	ImGui::SetNextWindowSize(ImVec2(1500.0f, 860.0f), ImGuiCond_FirstUseEver);
	if (EditorWindow* scene = EditorGUIManager::GetI()->FindWindow("Scene"))
		if (ImGuiWindow* w = ImGui::FindWindowByName(scene->GetImGuiName().c_str()); w && w->DockId)
			ImGui::SetNextWindowDockID(w->DockId, ImGuiCond_FirstUseEver);
}

void TerrainGraphWindow::Save()
{
	if (m_Graph && !m_Path.empty())
		TG::Save(m_Path, *m_Graph);
	m_SavePending = false;
}

void TerrainGraphWindow::Changed()
{
	if (m_Graph)
		m_Graph->Touch();
	m_SavePending = true;
	m_ChangedAt = Now();
}

void TerrainGraphWindow::OnRender()
{
	// 열린 그래프가 없으면 장면 World Terrain 의 것
	if (!m_Graph)
		if (Scene* scene = SceneManager::GetI()->GetCurrentScene())
			for (GameObject* g : scene->GameObjectsView())
				if (WorldTerrain* wt = g ? g->GetComponent<WorldTerrain>() : nullptr; wt && !wt->GraphPath.empty())
				{
					m_Path = wt->GraphPath;
					m_Graph = TG::Load(m_Path);
					m_LayoutPending = true;
					break;
				}
	if (ImGui::BeginMenuBar())
	{
		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("Save", "Ctrl+S", false, m_Graph != nullptr))
				Save();
			if (ImGui::MenuItem("Reset To Default Graph", nullptr, false, m_Graph != nullptr))
			{
				const TG::WorldSettings keep = m_Graph->World;
				TG::LayerRule layers[4];
				for (int i = 0; i < 4; ++i) layers[i] = m_Graph->Layers[i];
				*m_Graph = TG::MakeDefault(keep.Seed);
				m_Graph->World = keep;
				for (int i = 0; i < 4; ++i) if (!layers[i].Layer.empty()) m_Graph->Layers[i].Layer = layers[i].Layer;
				m_LayoutPending = true;
				Changed();
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Add Node", m_Graph != nullptr))
		{
			for (const char* cat : kCategories)
			{
				ImGui::TextDisabled("%s", cat);
				for (const TG::NodeDesc& d : TG::NodeDescs())
					if (strcmp(d.Category, cat) == 0 && ImGui::MenuItem(d.Label))
					{
						TG::Node& n = m_Graph->Add(d.Type, 0.0f, 0.0f);
						m_Selected = n.Id;
						m_LayoutPending = true;
						Changed();
					}
			}
			ImGui::EndMenu();
		}
		if (m_Graph)
		{
			const TG::BuildStatus st = TG::GetBuildStatus(m_Path);
			if (st.Running)
			{
				ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "  Building %d²: %s %.0f %%", st.Resolution, st.Stage.c_str(), st.Progress * 100.0f);
				if (ImGui::MenuItem("Cancel"))
					TG::CancelBuild(m_Path);
			}
			else if (ImGui::MenuItem(ICON_FA_HAMMER " Build World"))
			{
				Save();
				TG::StartBuild(m_Path);
			}
		}
		ImGui::TextDisabled("  %s", m_Path.empty() ? "(no graph — create one: Project > Create > Terrain Graph, or GameObject > Open World > Terrain World)" : m_Path.c_str());
		ImGui::EndMenuBar();
	}
	if (!m_Graph)
	{
		ImGui::TextDisabled("  Open a .terraingraph asset (double-click in Project) or create GameObject > Open World > Terrain World.");
		return;
	}
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
		Save();
	UpdatePreview();
	const ImVec2 avail = ImGui::GetContentRegionAvail();
	const float sideW = (std::min)(560.0f, (std::max)(380.0f, avail.x * 0.38f));
	DrawCanvas((std::max)(100.0f, avail.x - sideW), avail.y);
	ImGui::SameLine(0.0f, 0.0f);
	DrawSide(sideW, avail.y);
	if (m_SavePending && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
		Save();
}

// ---- 미리 보기: 그래프가 바뀌고 (놓은 뒤) 잠깐 지나면 작업 스레드에서 미리 보기 해상도로 다시 계산
void TerrainGraphWindow::UpdatePreview()
{
	const uint64_t rev = m_Graph->ContentHash() ^ ((uint64_t)m_Graph->World.PreviewResolution << 50);
	if (rev != m_PreviewRequested && !m_Preview->Running && !ImGui::IsMouseDown(ImGuiMouseButton_Left) && Now() - m_ChangedAt > 0.15)
	{
		m_PreviewRequested = rev;
		m_Preview->Running = true;
		m_Preview->Cancel = false;
		auto snapshot = std::make_shared<TG::Graph>(*m_Graph);
		std::shared_ptr<Preview> p = m_Preview;
		std::thread([p, snapshot, rev]() {
			TG::EvalContext ctx;
			ctx.Res = std::clamp(snapshot->World.PreviewResolution, 129, 2049);
			ctx.WorldSize = snapshot->World.WorldSize;
			ctx.MaxHeight = snapshot->World.MaxHeight;
			ctx.Seed = snapshot->World.Seed;
			ctx.KeepAll = true;
			ctx.Cancel = &p->Cancel;
			auto r = std::make_shared<TG::EvalResult>(TG::Evaluate(*snapshot, ctx));
			if (!r->Cancelled)
			{
				std::lock_guard<std::mutex> lock(p->Lock);
				p->Result = r;
				p->ForRevision = rev;
				p->Ms = r->Ms;
			}
			p->Running = false;
		}).detach();
	}
	// 그림 (고른 노드 · 핀) 을 텍스처로 — 결과나 고른 것이 바뀌면
	std::shared_ptr<TG::EvalResult> r;
	{
		std::lock_guard<std::mutex> lock(m_Preview->Lock);
		r = m_Preview->Result;
	}
	if (!r)
		return;
	const uint64_t key = (uint64_t)(uintptr_t)r.get() * 31 + (uint64_t)m_Selected * 1000003ull + (uint64_t)m_PreviewPin + m_Graph->RulesHash();
	if (key == m_ImageKey && m_PreviewSrv)
		return;
	m_ImageKey = key;
	const TG::Node* sel = m_Graph->Find(m_Selected);
	TG::MapPtr map, height = r->Height, color = r->Color;
	TG::PreviewMode mode = r->Color ? TG::PreviewMode::Color : TG::PreviewMode::Splat;
	if (sel && sel->Type != TG::NodeType::Output)
	{
		auto it = r->Outputs.find({ sel->Id, m_PreviewPin });
		if (it == r->Outputs.end())
			it = r->Outputs.find({ sel->Id, 0 });
		if (it != r->Outputs.end())
		{
			map = it->second;
			const TG::NodeDesc* d = TG::FindDesc(sel->Type);
			const int pin = it->first.second;
			const bool mask = d && pin < (int)d->Outputs.size() && d->Outputs[(size_t)pin].second == TG::PinKind::Mask;
			if (map->Channels == 3)
			{
				mode = TG::PreviewMode::Color;
				color = map;
			}
			else
			{
				mode = mask ? TG::PreviewMode::Mask : TG::PreviewMode::Shaded;
				if (!mask)
					height = map;
			}
		}
	}
	if (!height)
		return;
	std::vector<uint8_t> rgba;
	TG::PreviewImage(*height, map.get(), mode, *m_Graph, r->Flow.get(), r->Deposits.get(), rgba, color.get());
	const int res = height->Res;
	// 3D 미리 보기의 재료 (높이 + 이 그림)
	m_3DHeight = std::make_shared<TG::Map>(*height);
	m_3DAlbedo = std::make_shared<std::vector<uint8_t>>(rgba);
	++m_3DSource;
	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = desc.Height = (UINT)res;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	D3D11_SUBRESOURCE_DATA init = { rgba.data(), (UINT)res * 4, 0 };
	ComPtr<GfxTexture2D> tex;
	ComPtr<GfxShaderResourceView> srv;
	auto device = Application::GetI()->GetDevice();
	if (SUCCEEDED(device->CreateTexture2D(&desc, &init, tex.GetAddressOf())) && SUCCEEDED(device->CreateShaderResourceView(tex.Get(), nullptr, srv.GetAddressOf())))
	{
		m_PreviewTex = tex;
		m_PreviewSrv = srv;
		m_PreviewTexRes = res;
	}
}

void TerrainGraphWindow::DrawNode(TG::Node& n)
{
	const TG::NodeDesc* d = TG::FindDesc(n.Type);
	if (!d)
		return;
	ed::PushStyleColor(ed::StyleColor_NodeBorder, n.Enabled ? ImColor(CategoryColor(d->Category)) : ImColor(90, 90, 90, 255));
	ed::BeginNode(NodeOf(n.Id));
	ImGui::PushStyleColor(ImGuiCol_Text, n.Enabled ? IM_COL32(240, 240, 240, 255) : IM_COL32(140, 140, 140, 255));
	ImGui::TextUnformatted(d->Label);
	ImGui::PopStyleColor();
	if (!n.Comment.empty())
		ImGui::TextDisabled("%s", n.Comment.c_str());
	ImGui::Dummy(ImVec2(150.0f, 2.0f));
	const size_t rows = (std::max)(d->Inputs.size(), d->Outputs.size());
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
		if (i < d->Outputs.size())
		{
			ImGui::SameLine(110.0f);
			ed::BeginPin(OutPin(n.Id, (int)i), ed::PinKind::Output);
			const bool mask = d->Outputs[i].second == TG::PinKind::Mask;
			ImGui::TextColored(mask ? ImVec4(0.8f, 0.7f, 1.0f, 1.0f) : ImVec4(1, 1, 1, 1), "%s " ICON_FA_CIRCLE_DOT, d->Outputs[i].first);
			ed::EndPin();
		}
	}
	// 노드 계산 시간 (미리 보기)
	{
		std::lock_guard<std::mutex> lock(m_Preview->Lock);
		if (m_Preview->Result)
		{
			auto it = m_Preview->Result->NodeMs.find(n.Id);
			if (it != m_Preview->Result->NodeMs.end())
				ImGui::TextDisabled("%.0f ms", it->second);
		}
	}
	ed::EndNode();
	ed::PopStyleColor();
}

void TerrainGraphWindow::DrawCanvas(float width, float height)
{
	if (!m_Ctx)
	{
		ed::Config config;
		config.SettingsFile = nullptr;
		m_Ctx = ed::CreateEditor(&config);
	}
	ImGui::BeginChild("##tgcanvas", ImVec2(width, height), false, ImGuiWindowFlags_NoScrollbar);
	ed::SetCurrentEditor(m_Ctx);
	ed::Begin("##terraingraph", ImVec2(width, height));
	TG::Graph& g = *m_Graph;
	const bool layout = m_LayoutPending;
	if (layout)
		for (const TG::Node& n : g.Nodes)
			ed::SetNodePosition(NodeOf(n.Id), ImVec2(n.X, n.Y));
	for (TG::Node& n : g.Nodes)
		DrawNode(n);
	for (size_t i = 0; i < g.Edges.size(); ++i)
	{
		const TG::Edge& e = g.Edges[i];
		const TG::Node* from = g.Find(e.From);
		const TG::NodeDesc* d = from ? TG::FindDesc(from->Type) : nullptr;
		const bool mask = d && e.FromPin < (int)d->Outputs.size() && d->Outputs[(size_t)e.FromPin].second == TG::PinKind::Mask;
		ed::Link(LinkOf((int)i), OutPin(e.From, e.FromPin), InPin(e.To, e.ToPin), mask ? ImColor(200, 170, 255, 255) : ImColor(150, 200, 255, 255), 2.0f);
	}
	if (layout)
	{
		ed::NavigateToContent(0.0f);
		m_LayoutPending = false;
	}
	if (m_SelectPending && !layout)
	{
		ed::SelectNode(NodeOf(m_SelectPending));
		m_SelectPending = 0;
	}
	if (ed::BeginCreate(ImColor(150, 200, 255, 255), 2.0f))
	{
		ed::PinId a, b;
		if (ed::QueryNewLink(&a, &b) && a && b)
		{
			int na, ia, nb, ib;
			bool inA, inB;
			if (DecodePin(a, na, ia, inA) && DecodePin(b, nb, ib, inB) && inA != inB && na != nb)
			{
				const int from = inA ? nb : na, fromPin = inA ? ib : ia, to = inA ? na : nb, toPin = inA ? ia : ib;
				if (ed::AcceptNewItem() && g.Connect(from, fromPin, to, toPin))
					Changed();
			}
			else
				ed::RejectNewItem(ImColor(255, 90, 90, 255), 2.0f);
		}
	}
	ed::EndCreate();
	if (ed::BeginDelete())
	{
		ed::LinkId l;
		std::vector<int> drop;
		while (ed::QueryDeletedLink(&l))
			if (ed::AcceptDeletedItem())
			{
				const int index = (int)(l.Get() - (1u << 27));
				if (index >= 0 && index < (int)g.Edges.size())
					drop.push_back(index);
			}
		std::sort(drop.rbegin(), drop.rend());
		for (int index : drop)
			g.Edges.erase(g.Edges.begin() + index);
		if (!drop.empty())
			Changed();
		ed::NodeId n;
		while (ed::QueryDeletedNode(&n))
			if (ed::AcceptDeletedItem())
			{
				g.Remove((int)n.Get());
				Changed();
			}
	}
	ed::EndDelete();
	for (TG::Node& n : g.Nodes)
	{
		const ImVec2 p = ed::GetNodePosition(NodeOf(n.Id));
		if (fabsf(p.x - n.X) > 0.5f || fabsf(p.y - n.Y) > 0.5f)
		{
			n.X = p.x;
			n.Y = p.y;
			m_SavePending = true;
		}
	}
	{
		ed::NodeId sel[4];
		if (!m_SelectPending && ed::GetSelectedNodes(sel, 4) > 0 && (int)sel[0].Get() != m_Selected)
		{
			m_Selected = (int)sel[0].Get();
			m_PreviewPin = 0;
		}
		else if (!m_SelectPending && ed::IsBackgroundClicked())
			m_Selected = 0;   // 빈 곳 = Output (텍스처 규칙 색) 미리 보기
	}
	ed::Suspend();
	if (ed::ShowBackgroundContextMenu() || (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Space, false)))
	{
		m_AddAt = ed::ScreenToCanvas(ImGui::GetMousePos());
		ImGui::OpenPopup("##tgadd");
	}
	if (ImGui::BeginPopup("##tgadd"))
	{
		for (const char* cat : kCategories)
		{
			ImGui::TextDisabled("%s", cat);
			for (const TG::NodeDesc& d : TG::NodeDescs())
				if (strcmp(d.Category, cat) == 0 && ImGui::MenuItem(d.Label))
				{
					TG::Node& n = g.Add(d.Type, m_AddAt.x, m_AddAt.y);
					m_Selected = n.Id;
					ed::SetNodePosition(NodeOf(n.Id), m_AddAt);
					Changed();
				}
		}
		ImGui::EndPopup();
	}
	ed::Resume();
	ed::End();
	ed::SetCurrentEditor(nullptr);
	ImGui::EndChild();
}

void TerrainGraphWindow::Update3D(int w, int h)
{
	if (!m_3DHeight || !m_3DAlbedo || w < 16 || h < 16)
		return;
	const uint64_t key = m_3DSource * 1000003ull ^ ((uint64_t)(m_Yaw * 10.0f) << 20) ^ ((uint64_t)(m_Pitch * 10.0f) << 36) ^ ((uint64_t)(m_Zoom * 100.0f) << 48) ^ ((uint64_t)w << 8) ^ (uint64_t)h;
	if (key != m_3DRequested && !m_3D->Running)
	{
		m_3DRequested = key;
		m_3D->Running = true;
		auto job = m_3D;
		auto hm = m_3DHeight;
		auto alb = m_3DAlbedo;
		const float maxH = m_Graph->World.MaxHeight, size = m_Graph->World.WorldSize, yaw = m_Yaw, pitch = m_Pitch, zoom = m_Zoom;
		std::thread([job, hm, alb, maxH, size, yaw, pitch, zoom, w, h, key]() {
			std::vector<uint8_t> img;
			TG::Render3D(*hm, *alb, maxH, size, yaw, pitch, zoom, w, h, img);
			std::lock_guard<std::mutex> lock(job->Lock);
			job->Image.swap(img);
			job->W = w;
			job->H = h;
			job->ForKey = key;
			job->Running = false;
		}).detach();
	}
	std::lock_guard<std::mutex> lock(m_3D->Lock);
	if (m_3D->ForKey != m_3DUploaded && !m_3D->Image.empty())
	{
		m_3DUploaded = m_3D->ForKey;
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = (UINT)m_3D->W;
		desc.Height = (UINT)m_3D->H;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		D3D11_SUBRESOURCE_DATA init = { m_3D->Image.data(), (UINT)m_3D->W * 4, 0 };
		ComPtr<GfxTexture2D> tex;
		ComPtr<GfxShaderResourceView> srv;
		auto device = Application::GetI()->GetDevice();
		if (SUCCEEDED(device->CreateTexture2D(&desc, &init, tex.GetAddressOf())) && SUCCEEDED(device->CreateShaderResourceView(tex.Get(), nullptr, srv.GetAddressOf())))
		{
			m_3DTex = tex;
			m_3DSrv = srv;
		}
	}
}

void TerrainGraphWindow::DrawSide(float width, float height)
{
	ImGui::BeginChild("##tgside", ImVec2(width, height), true);
	// ---- 미리 보기: 3D (끌기 = 돌리기, 휠 = 확대) 또는 2D 지도
	const float img = width - 16.0f;
	if (ImGui::RadioButton("3D", m_View3D)) m_View3D = true;
	ImGui::SameLine();
	if (ImGui::RadioButton("2D", !m_View3D)) m_View3D = false;
	if (m_View3D)
	{
		const float h3 = img * 0.62f;
		Update3D((int)img, (int)h3);
		if (m_3DSrv)
			ImGui::Image((ImTextureID)m_3DSrv.Get(), ImVec2(img, h3));
		else
			ImGui::Dummy(ImVec2(img, h3));
		if (ImGui::IsItemHovered())
		{
			const float wheel = ImGui::GetIO().MouseWheel;
			if (wheel != 0.0f)
				m_Zoom = std::clamp(m_Zoom * (wheel > 0.0f ? 1.15f : 1.0f / 1.15f), 0.4f, 6.0f);
		}
		if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f))
		{
			const ImVec2 d = ImGui::GetIO().MouseDelta;
			m_Yaw = fmodf(m_Yaw - d.x * 0.4f + 360.0f, 360.0f);
			m_Pitch = std::clamp(m_Pitch + d.y * 0.3f, 8.0f, 85.0f);
		}
	}
	else if (m_PreviewSrv)
		ImGui::Image((ImTextureID)m_PreviewSrv.Get(), ImVec2(img, img));
	else
		ImGui::Dummy(ImVec2(img, img));
	{
		const TG::Node* sel = m_Graph->Find(m_Selected);
		const TG::NodeDesc* d = sel ? TG::FindDesc(sel->Type) : nullptr;
		if (d && d->Outputs.size() > 1)
		{
			for (int p = 0; p < (int)d->Outputs.size(); ++p)
			{
				if (p) ImGui::SameLine();
				if (ImGui::RadioButton(d->Outputs[(size_t)p].first, m_PreviewPin == p))
					m_PreviewPin = p;
			}
		}
		std::lock_guard<std::mutex> lock(m_Preview->Lock);
		ImGui::TextDisabled("Preview %d² (%.0f m/px) — %s%s, %.0f ms", m_Graph->World.PreviewResolution, m_Graph->World.WorldSize / (m_Graph->World.PreviewResolution - 1),
			d ? d->Label : "Output (texturing)", m_Preview->Running ? " (updating)" : "", m_Preview->Ms);
		if (m_Preview->Result && !m_Preview->Result->Error.empty())
			ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", m_Preview->Result->Error.c_str());
	}
	ImGui::Separator();
	static const char* kTabs[] = { "Node", "World", "Texturing", "Build" };
	for (int t = 0; t < 4; ++t)
	{
		if (t) ImGui::SameLine();
		if (ImGui::Selectable(kTabs[t], m_Tab == t, 0, ImVec2(70.0f, 0.0f)))
			m_Tab = t;
	}
	ImGui::Separator();
	switch (m_Tab)
	{
	case 0: DrawNodeTab(); break;
	case 1: DrawWorldTab(); break;
	case 2: DrawTexturingTab(); break;
	default: DrawBuildTab(); break;
	}
	ImGui::EndChild();
}

void TerrainGraphWindow::DrawNodeTab()
{
	TG::Node* n = m_Graph->Find(m_Selected);
	if (!n)
	{
		ImGui::TextWrapped("Select a node. Right-click (or Space) on the canvas to add nodes, drag from an output to an input to connect, Delete removes the selection.");
		ImGui::TextWrapped("The preview follows the selected node (height = shaded relief, mask = grey). With nothing selected it shows the Output with the texturing rules.");
		return;
	}
	const TG::NodeDesc* d = TG::FindDesc(n->Type);
	ImGui::TextUnformatted(d ? d->Label : "?");
	if (d && d->Help)
		ImGui::TextWrapped("%s", d->Help);
	ImGui::Separator();
	bool changed = ImGui::Checkbox("Enabled", &n->Enabled);
	char comment[128];
	strncpy_s(comment, n->Comment.c_str(), _TRUNCATE);
	if (ImGui::InputText("Name", comment, sizeof(comment)))
	{
		n->Comment = comment;
		m_SavePending = true;
	}
	if (d)
	{
		while (n->Values.size() < d->Params.size())
			n->Values.push_back(d->Params[n->Values.size()].Default);
		for (size_t i = 0; i < d->Params.size(); ++i)
		{
			const TG::ParamDesc& p = d->Params[i];
			float& v = n->Values[i];
			ImGui::PushID((int)i);
			switch (p.Kind)
			{
			case TG::ParamKind::Float:
			{
				const bool logScale = p.Min > 0.0f && p.Max / p.Min > 1000.0f;
				changed |= ImGui::SliderFloat(p.Label, &v, p.Min, p.Max, "%.4g", logScale ? ImGuiSliderFlags_Logarithmic : 0);
				break;
			}
			case TG::ParamKind::Int:
			{
				int iv = (int)v;
				if (ImGui::InputInt(p.Label, &iv))
				{
					v = (float)std::clamp(iv, (int)p.Min, (int)p.Max);
					changed = true;
				}
				break;
			}
			case TG::ParamKind::Bool:
			{
				bool b = v > 0.5f;
				if (ImGui::Checkbox(p.Label, &b))
				{
					v = b ? 1.0f : 0.0f;
					changed = true;
				}
				break;
			}
			case TG::ParamKind::Enum:
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
			if (p.Tooltip && ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", p.Tooltip);
			ImGui::PopID();
		}
	}
	if (changed)
		Changed();
}

void TerrainGraphWindow::DrawWorldTab()
{
	TG::WorldSettings& w = m_Graph->World;
	bool changed = false;
	float km = w.WorldSize / 1000.0f;
	if (ImGui::SliderFloat("World Size (km)", &km, 1.0f, 100.0f, "%.0f"))
	{
		w.WorldSize = km * 1000.0f;
		changed = true;
	}
	changed |= ImGui::SliderFloat("Tile Size (m)", &w.TileSize, 250.0f, 4000.0f, "%.0f");
	auto resCombo = [&](const char* label, int& v, std::initializer_list<int> options) {
		char cur[16];
		snprintf(cur, sizeof(cur), "%d", v);
		if (ImGui::BeginCombo(label, cur))
		{
			for (int o : options)
			{
				char t[16];
				snprintf(t, sizeof(t), "%d", o);
				if (ImGui::Selectable(t, o == v))
				{
					v = o;
					changed = true;
				}
			}
			ImGui::EndCombo();
		}
	};
	resCombo("Tile Heightmap", w.TileResolution, { 513, 1025, 2049, 4097 });
	changed |= ImGui::SliderFloat("Max Height (m)", &w.MaxHeight, 50.0f, 6000.0f, "%.0f");
	changed |= ImGui::InputInt("Seed", &w.Seed);
	resCombo("Build Resolution", w.BuildResolution, { 1025, 2049, 4097, 8193 });
	resCombo("Preview Resolution", w.PreviewResolution, { 257, 513, 1025 });
	ImGui::SeparatorText("Tile Detail (finer than the build)");
	changed |= ImGui::SliderFloat("Amplitude (m)", &w.DetailAmplitude, 0.0f, 40.0f);
	changed |= ImGui::SliderFloat("Scale (m)", &w.DetailScale, 5.0f, 1000.0f);
	changed |= ImGui::SliderFloat("Erosion", &w.DetailErosion, 0.0f, 2.0f);
	changed |= ImGui::SliderFloat("Slope Roughness", &w.DetailSlope, 0.0f, 3.0f);
	changed |= ImGui::SliderFloat("Smooth Deposits", &w.DetailDeposits, 0.0f, 1.0f);
	changed |= ImGui::SliderFloat("Cliff Detail (m)", &w.CliffDetail, 0.0f, 30.0f);
	changed |= ImGui::SliderFloat("Color Map Strength", &w.ColorStrength, 0.0f, 1.0f);
	if (changed)
		Changed();
	// ---- 어림: 타일 · 메모리 · 디스크
	ImGui::SeparatorText("Estimate");
	const int side = (int)std::round(w.WorldSize / (std::max)(w.TileSize, 1.0f));
	const double tileHeights = (double)w.TileResolution * w.TileResolution * 4.0;
	const double tileControl = (double)((w.TileResolution - 1) / 2) * ((w.TileResolution - 1) / 2) * 4.0;
	const double rawAll = (double)side * side * (tileHeights + tileControl);
	const double build = (double)w.BuildResolution * w.BuildResolution;
	ImGui::Text("%d x %d tiles = %d terrains, %.2f m per height sample", side, side, side * side, w.TileSize / (w.TileResolution - 1));
	ImGui::Text("All tiles at %d² if stored: %s (not stored — computed when near)", w.TileResolution, FormatMB(rawAll).c_str());
	ImGui::Text("Build file (%d²): %s on disk, %s in memory", w.BuildResolution, FormatMB(build * 5.0).c_str(), FormatMB(build * 9.0).c_str());
	ImGui::Text("One near tile: %s RAM + %s VRAM", FormatMB(tileHeights + tileControl).c_str(), FormatMB(tileHeights + tileControl).c_str());
	ImGui::TextWrapped("World Terrain keeps only tiles within Near Distance at %d² (others 129 / 33) and a %s cache.", w.TileResolution, FormatMB(384.0 * 1048576.0).c_str());
}

void TerrainGraphWindow::DrawTexturingTab()
{
	bool changed = false;
	ImGui::TextWrapped("Splat rules (layer 0 = base, 1 - 3 painted on top in order). Computed per tile from height, slope, the erosion Flow / Deposits and curvature.");
	for (int i = 0; i < 4; ++i)
	{
		TG::LayerRule& r = m_Graph->Layers[i];
		ImGui::PushID(i);
		char header[128];
		snprintf(header, sizeof(header), "%d  %s", i, r.Name.empty() ? "Layer" : r.Name.c_str());
		if (ImGui::CollapsingHeader(header, i == 0 ? 0 : ImGuiTreeNodeFlags_DefaultOpen))
		{
			char buf[260];
			strncpy_s(buf, r.Name.c_str(), _TRUNCATE);
			if (ImGui::InputText("Name", buf, sizeof(buf))) { r.Name = buf; changed = true; }
			strncpy_s(buf, r.Layer.c_str(), _TRUNCATE);
			if (ImGui::InputText("Terrain Layer", buf, sizeof(buf))) { r.Layer = buf; changed = true; }
			// Project 에서 고른 .terrainlayer 넣기
			const std::wstring sel = SelectionManager::GetSelectedFile();
			if (std::filesystem::path(sel).extension() == L".terrainlayer")
			{
				ImGui::SameLine();
				if (ImGui::SmallButton("Use Selected"))
				{
					r.Layer = wstring_to_string(PathManager::GetI()->GetCutSolutionPath(sel));
					changed = true;
				}
			}
			if (i > 0)
			{
				changed |= ImGui::Checkbox("Enabled", &r.Enabled);
				changed |= ImGui::DragFloatRange2("Height (m)", &r.HeightMin, &r.HeightMax, 5.0f, -1000.0f, 100000.0f, "%.0f", "%.0f");
				changed |= ImGui::SliderFloat("Height Falloff", &r.HeightFalloff, 0.0f, 500.0f);
				changed |= ImGui::DragFloatRange2("Slope (deg)", &r.SlopeMin, &r.SlopeMax, 0.2f, 0.0f, 90.0f, "%.1f", "%.1f");
				changed |= ImGui::SliderFloat("Slope Falloff", &r.SlopeFalloff, 0.0f, 30.0f);
				changed |= ImGui::SliderFloat("Flow", &r.Flow, 0.0f, 2.0f);
				changed |= ImGui::SliderFloat("Deposits", &r.Deposits, 0.0f, 2.0f);
				changed |= ImGui::SliderFloat("Curvature", &r.Curvature, -1.0f, 1.0f);
				changed |= ImGui::SliderFloat("Noise", &r.Noise, 0.0f, 2.0f);
				changed |= ImGui::SliderFloat("Noise Scale (m)", &r.NoiseScale, 1.0f, 500.0f);
				changed |= ImGui::SliderFloat("Strength", &r.Strength, 0.0f, 2.0f);
			}
		}
		ImGui::PopID();
	}
	if (changed)
	{
		m_ImageKey = 0;
		Changed();
	}
	ImGui::Separator();
	ImGui::TextWrapped("Procedural textures: albedo + normal + height maps (tileable), one per rule by its name (Grass / Dirt / Rock / Snow / Sand), saved next to the graph.");
	if (ImGui::Button("Generate Layer Textures (1024)", ImVec2(-1.0f, 0.0f)))
		GenerateLayerTextures(m_Path, 1024, m_Message);
	if (!m_Message.empty())
		ImGui::TextWrapped("%s", m_Message.c_str());
}

void TerrainGraphWindow::DrawBuildTab()
{
	const TG::BuildStatus st = TG::GetBuildStatus(m_Path);
	std::shared_ptr<const TG::Built> built = TG::GetBuilt(m_Path, false);
	if (st.Running)
	{
		ImGui::ProgressBar(st.Progress, ImVec2(-1.0f, 0.0f), st.Stage.c_str());
		if (ImGui::Button("Cancel", ImVec2(-1.0f, 0.0f)))
			TG::CancelBuild(m_Path);
	}
	else
	{
		char label[96];
		snprintf(label, sizeof(label), ICON_FA_HAMMER " Build World (%d²)", m_Graph->World.BuildResolution);
		if (ImGui::Button(label, ImVec2(-1.0f, 0.0f)))
		{
			Save();
			TG::StartBuild(m_Path);
		}
		if (!st.Error.empty())
			ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "Last build: %s", st.Error.c_str());
		else if (st.Done)
			ImGui::TextDisabled("Last build: %.1f s", st.Ms / 1000.0);
	}
	if (built)
	{
		const bool outdated = built->GraphHash != m_Graph->ContentHash();
		ImGui::Text("Built: %d² over %.0f km (%.1f m/px), %s", built->Res, built->World.WorldSize / 1000.0f, built->World.WorldSize / (built->Res - 1), FormatMB((double)built->Bytes()).c_str());
		if (outdated)
			ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "The graph changed since the build — Build again (texturing and tile detail apply without a rebuild).");
	}
	else
		ImGui::TextDisabled("Not built yet.");
	ImGui::Separator();
	ImGui::TextWrapped("World Terrain streams the build as %s tiles: near ones at %d², farther 129² / 33². PCG Volumes read the same heights.",
		(std::to_string((int)std::round(m_Graph->World.WorldSize / m_Graph->World.TileSize)) + " x " + std::to_string((int)std::round(m_Graph->World.WorldSize / m_Graph->World.TileSize))).c_str(),
		m_Graph->World.TileResolution);
	if (ImGui::Button("Create / Update World Terrain In Scene", ImVec2(-1.0f, 0.0f)))
		m_Message = "World: " + AttachToWorld(m_Path);
	if (built && (!built->Rivers.empty() || !built->Lakes.empty()))
	{
		char label[96];
		snprintf(label, sizeof(label), "Create Water (%d rivers, %d lakes)", (int)built->Rivers.size(), (int)built->Lakes.size());
		if (ImGui::Button(label, ImVec2(-1.0f, 0.0f)))
			CreateRivers(m_Path, m_Message);
	}
	if (built)
		ImGui::TextDisabled("Rivers %d · Lakes %d · Roads %d%s", (int)built->Rivers.size(), (int)built->Lakes.size(), (int)built->Roads.size(), built->ColorMap ? " · color map" : "");
	if (!built)
		ImGui::BeginDisabled();
	if (ImGui::Button("Apply To Scene Terrains (multi-terrain)", ImVec2(-1.0f, 0.0f)))
	{
		std::vector<Terrain*> targets;
		for (Terrain* t : Terrain::GetActiveTerrains())
			if (t && t->GetGameObject() && !t->GetGameObject()->IsHideAndDontSave())
				targets.push_back(t);
		const int n = TG::ApplyToTerrains(*built, targets);
		m_Message = "Applied to " + std::to_string(n) + " terrains";
	}
	ImGui::SeparatorText("Terrain Tiles (editable Terrain objects)");
	ImGui::InputInt("First Tile X", &m_TileX);
	ImGui::InputInt("First Tile Z", &m_TileZ);
	ImGui::SliderInt("Count (n x n)", &m_TileCount, 1, 8);
	int resIndex = m_TileRes == 513 ? 0 : m_TileRes == 1025 ? 1 : 2;
	if (ImGui::Combo("Heightmap", &resIndex, "513\0" "1025\0" "2049\0"))
		m_TileRes = resIndex == 0 ? 513 : resIndex == 1 ? 1025 : 2049;
	ImGui::TextDisabled("%d terrains, %s of .terraindata", m_TileCount * m_TileCount, FormatMB((double)m_TileCount * m_TileCount * ((double)m_TileRes * m_TileRes * 4.0 + 4.0 * 1048576.0)).c_str());
	if (ImGui::Button("Create Terrain Tiles", ImVec2(-1.0f, 0.0f)))
		CreateTerrainTiles(m_Path, m_TileX, m_TileZ, m_TileCount, m_TileRes, m_Message);
	if (!built)
		ImGui::EndDisabled();
	if (!m_Message.empty())
		ImGui::TextWrapped("%s", m_Message.c_str());
}

// ================================================================ 기능 (창 · CLI 공용)
bool TerrainGraphWindow::GenerateLayerTextures(const std::string& graphPath, int size, std::string& message)
{
	std::shared_ptr<TG::Graph> g = TG::Load(graphPath);
	if (!g)
	{
		message = "no graph " + graphPath;
		return false;
	}
	const std::filesystem::path gp(graphPath);
	const std::string folder = (gp.parent_path() / (gp.stem().string() + "_Layers")).string();
	int made = 0;
	for (int i = 0; i < 4; ++i)
	{
		TG::LayerRule& r = g->Layers[i];
		const TerrainTextureGen::Kind kind = TerrainTextureGen::KindFromName(r.Name.empty() ? std::to_string(i) : r.Name);
		const char* kname = TerrainTextureGen::KindName(kind);
		TerrainTextureGen::Result t = TerrainTextureGen::Generate(kind, size, g->World.Seed * 13 + i);
		const std::string base = folder + "\\" + kname;
		const std::string albedo = base + "_Albedo.png", normal = base + "_Normal.png", height = base + "_Height.png";
		auto abs = [](const std::string& p) { return PathManager::GetI()->GetMovePathW(string_to_wstring(p)); };
		if (!SaveRgbaPng(t.Albedo, t.Size, t.Size, abs(albedo), true) || !SaveRgbaPng(t.Normal, t.Size, t.Size, abs(normal), false) ||
			!SaveRgbaPng(t.Height, t.Size, t.Size, abs(height), false))
		{
			message = "could not write " + albedo;
			return false;
		}
		const std::string layerPath = base + ".terrainlayer";
		std::shared_ptr<TerrainLayer> layer = TerrainLayer::Load(layerPath);
		if (!layer)
			layer = TerrainLayer::Create(layerPath, albedo);
		if (!layer)
		{
			message = "could not create " + layerPath;
			return false;
		}
		layer->SetDiffuse(albedo);
		layer->SetNormal(normal);
		layer->NormalScale = t.NormalScale;
		layer->SetHeight(height);
		layer->HeightAmplitude = t.HeightAmplitude;
		layer->TileSize = Vec2(t.TileSize, t.TileSize);
		layer->Save();
		r.Layer = layerPath;
		++made;
	}
	g->Touch();
	TG::Save(graphPath, *g);
	message = "Generated " + std::to_string(made) + " layers (" + std::to_string(size) + "²) in " + folder;
	EditorLog::Write("TerrainGraph", "%s", message.c_str());
	return true;
}

std::string TerrainGraphWindow::AttachToWorld(const std::string& graphPath)
{
	Scene* scene = SceneManager::GetI()->GetCurrentScene();
	if (!scene)
		return "no scene";
	std::string name;
	for (GameObject* go : scene->GameObjectsView())
		if (WorldTerrain* wt = go ? go->GetComponent<WorldTerrain>() : nullptr)
		{
			wt->GraphPath = graphPath;
			name = go->GetName();
			break;
		}
	if (name.empty())
	{
		GameObject* go = GameObjectFactory::CreateTerrainWorld(graphPath);
		if (!go)
			return "could not create";
		scene->AddRootGameObject(go);
		SelectionManager::SetSelectedGameObject(go);
		name = go->GetName();
	}
	// 강 (빌드에 강 선이 있으면 Water Body 로)
	if (auto built = TG::GetBuilt(graphPath); built && (!built->Rivers.empty() || !built->Lakes.empty()))
	{
		std::string msg;
		CreateRivers(graphPath, msg);
	}
	return name;
}

int TerrainGraphWindow::CreateRivers(const std::string& graphPath, std::string& message)
{
	std::shared_ptr<const TG::Built> built = TG::GetBuilt(graphPath);
	Scene* scene = SceneManager::GetI()->GetCurrentScene();
	if (!built || !scene)
	{
		message = !built ? "build the graph first" : "no scene";
		return 0;
	}
	// 넣을 곳: World Terrain 오브젝트 아래 "Rivers" (있으면 비우고 다시)
	GameObject* world = nullptr;
	for (GameObject* go : scene->GameObjectsView())
		if (go && go->GetComponent<WorldTerrain>())
		{
			world = go;
			break;
		}
	GameObject* parent = nullptr;
	if (world)
		for (GameObject* ch : world->GetChildren())
			if (ch && ch->GetName() == "Rivers")
				parent = ch;
	if (parent)
		scene->DestroyGameObject(parent);
	parent = new GameObject("Rivers");
	scene->AddRootGameObject(parent);
	if (world)
		parent->SetParent(world, false);
	int made = 0;
	for (const TG::Path& p : built->Rivers)
	{
		if (p.Points.size() < 2)
			continue;
		GameObject* go = new GameObject("River " + std::to_string(made + 1));
		WaterBody* w = go->AddComponent<WaterBody>();
		w->BodyType = WaterBody::Type::River;
		w->Profile = "Mountain River";
		w->CarveTerrain = false;   // 강바닥은 지형 그래프가 이미 팠다
		w->Points.clear();
		// 점 간격 약 40 m (강 메시 · 곡선)
		float run = 1e9f;
		for (size_t i = 0; i < p.Points.size(); ++i)
		{
			const TG::PathPoint& q = p.Points[i];
			if (i > 0)
				run += sqrtf((q.X - p.Points[i - 1].X) * (q.X - p.Points[i - 1].X) + (q.Z - p.Points[i - 1].Z) * (q.Z - p.Points[i - 1].Z));
			if (run < 40.0f && i + 1 < p.Points.size())
				continue;
			run = 0.0f;
			WaterBody::Point wp;
			wp.Position = Vec3(q.X, q.Y, q.Z);
			wp.Width = q.Width;
			wp.Depth = 1.2f + 3.5f * p.Order;
			wp.Speed = 0.6f + 0.8f * p.Order;
			w->Points.push_back(wp);
		}
		++w->Revision;
		scene->AddRootGameObject(go);
		go->SetParent(parent, false);
		++made;
	}
	// 호수: 물가 선 (닫힌) → Water Body (Lake). 수면 = 오브젝트 높이, 점은 가운데 기준 (로컬)
	int lakes = 0;
	for (const TG::Path& p : built->Lakes)
	{
		if (p.Points.size() < 4)
			continue;
		float cx = 0.0f, cz = 0.0f;
		for (const TG::PathPoint& q : p.Points) { cx += q.X; cz += q.Z; }
		cx /= p.Points.size();
		cz /= p.Points.size();
		GameObject* go = new GameObject("Lake " + std::to_string(lakes + 1));
		WaterBody* w = go->AddComponent<WaterBody>();
		w->BodyType = WaterBody::Type::Lake;
		w->Profile = "Calm Lake";
		w->CarveTerrain = false;
		w->Points.clear();
		for (const TG::PathPoint& q : p.Points)
		{
			WaterBody::Point wp;
			wp.Position = Vec3(q.X - cx, 0.0f, q.Z - cz);
			w->Points.push_back(wp);
		}
		++w->Revision;
		scene->AddRootGameObject(go);
		go->SetParent(parent, false);
		go->GetTransform()->SetLocalPosition(Vec3(cx, p.Points[0].Y, cz));
		++lakes;
	}
	message = "Created " + std::to_string(made) + " rivers, " + std::to_string(lakes) + " lakes";
	EditorLog::Write("TerrainGraph", "%s", message.c_str());
	return made + lakes;
}

int TerrainGraphWindow::CreateTerrainTiles(const std::string& graphPath, int x0, int z0, int count, int resolution, std::string& message)
{
	std::shared_ptr<const TG::Built> built = TG::GetBuilt(graphPath);
	Scene* scene = SceneManager::GetI()->GetCurrentScene();
	if (!built || !scene)
	{
		message = !built ? "build the graph first" : "no scene";
		return 0;
	}
	const TG::WorldSettings& w = built->World;
	const int side = (int)std::round(w.WorldSize / w.TileSize);
	const std::filesystem::path gp(graphPath);
	const std::string folder = (gp.parent_path() / (gp.stem().string() + "_Tiles")).string();
	GameObject* parent = new GameObject(gp.stem().string() + " Tiles");
	scene->AddRootGameObject(parent);
	std::vector<Terrain*> made;
	for (int dz = 0; dz < count; ++dz)
		for (int dx = 0; dx < count; ++dx)
		{
			const int tx = std::clamp(x0 + dx, 0, side - 1), tz = std::clamp(z0 + dz, 0, side - 1);
			const std::string name = "Tile_" + std::to_string(tx) + "_" + std::to_string(tz);
			std::shared_ptr<TerrainData> data = TerrainData::Create(folder + "\\" + name + ".terraindata", resolution, Vec3(w.TileSize, w.MaxHeight, w.TileSize));
			if (!data)
				continue;
			data->ControlResolution = (resolution - 1) / 2;
			data->Control.assign((size_t)data->ControlResolution * data->ControlResolution * 4, 0);
			GameObject* go = new GameObject(name);
			Terrain* t = go->AddComponent<Terrain>();
			t->SetTerrainData(data);
			go->SetParent(parent, false);
			go->GetTransform()->SetPosition(Vec3(-w.WorldSize * 0.5f + tx * w.TileSize, 0.0f, -w.WorldSize * 0.5f + tz * w.TileSize));
			made.push_back(t);
		}
	const int n = TG::ApplyToTerrains(*built, made);
	for (Terrain* t : made)
		if (auto d = t->GetTerrainData())
			d->Save();
	message = "Created " + std::to_string(n) + " terrains (" + std::to_string(resolution) + "²) in " + folder;
	EditorLog::Write("TerrainGraph", "%s", message.c_str());
	return n;
}

void TerrainGraphWindow::RegisterEditor()
{
	EditorGUIManager::GetI()->RegisterWindow(new TerrainGraphWindow);
	WorldTerrain::s_OpenGraphWindow = [](const std::string& path) { TerrainGraphWindow::Open(path); };
	EditorExtensions::AssetType t;
	t.Owner = "terraingraph";
	t.Extension = ".terraingraph";
	t.Icon = "terrain";
	t.CreateMenu = "Terrain Graph";
	t.DefaultName = "New Terrain Graph";
	t.Create = [](const std::string& path) { TG::Save(path, TG::MakeDefault(1)); };
	t.Open = [](const std::string& path) { TerrainGraphWindow::Open(path); };
	t.Inspector = [](const std::string& path) {
		auto g = TG::Load(path);
		if (!g)
		{
			UnityGUI::HelpBox("Not a terrain graph", true);
			return;
		}
		UnityGUI::ValueLabel("Nodes", std::to_string(g->Nodes.size()).c_str());
		char text[96];
		snprintf(text, sizeof(text), "%.0f x %.0f km, tiles %.0f m at %d²", g->World.WorldSize / 1000.0f, g->World.WorldSize / 1000.0f, g->World.TileSize, g->World.TileResolution);
		UnityGUI::ValueLabel("World", text);
		UnityGUI::ValueLabel("Built", TG::GetBuilt(path, false) ? "yes" : "no");
		if (UnityGUI::CenterButton("Open Terrain Graph"))
			TerrainGraphWindow::Open(path);
	};
	EditorExtensions::RegisterAssetType(t);
}
