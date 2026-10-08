#include "pch.h"
#include "RenderGraphViewerWindow.h"
#include "RenderGraph.h"
#include "UnityGUI.h"

namespace
{
	RenderGraphViewerWindow* s_Instance = nullptr;
}

RenderGraphViewerWindow::RenderGraphViewerWindow()
	: EditorWindow("Render Graph Viewer", ICON_FA_DIAGRAM_PROJECT)
{
	s_Instance = this;
	SetIsOpened(false);
}

void RenderGraphViewerWindow::Toggle()
{
	if (s_Instance == nullptr)
		return;
	const bool open = !s_Instance->GetIsOpened();
	s_Instance->SetIsOpened(open);
	s_Instance->m_FocusNext = open;
}

bool RenderGraphViewerWindow::IsOpen()
{
	return s_Instance && s_Instance->GetIsOpened();
}

void RenderGraphViewerWindow::BeforeBegin()
{
	const ImGuiViewport* vp = ImGui::GetMainViewport();
	ImGui::SetNextWindowSize(ImVec2(980.0f, 520.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.45f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
	if (m_FocusNext)
	{
		ImGui::SetNextWindowFocus();
		m_FocusNext = false;
	}
}

void RenderGraphViewerWindow::OnRender()
{
	const std::vector<std::string> names = RenderGraph::GraphNames();
	if (names.empty())
	{
		UnityGUI::HelpBox("No Render Graph has run yet (open the Scene or Game view).", false);
		return;
	}
	m_Graph = std::clamp(m_Graph, 0, (int)names.size() - 1);
	std::vector<const char*> labels;
	for (const std::string& n : names)
		labels.push_back(n.c_str());
	UnityGUI::Dropdown("Graph", &m_Graph, labels.data(), (int)labels.size());

	const nlohmann::json info = RenderGraph::LastInfo(names[m_Graph]);
	if (!info.is_object() || !info.contains("passes"))
		return;
	const nlohmann::json& passes = info["passes"];
	const nlohmann::json& resources = info["resources"];
	char line[256];
	snprintf(line, sizeof(line), "%d passes, %d culled, %d transient textures", info.value("passCount", 0), info.value("culledCount", 0), info.value("transientCount", 0));
	UnityGUI::ValueLabel("Passes", line);

	// 패스 (열) x 자원 (행): R 읽기, W 쓰기, RW 둘 다 — 빠진 패스는 흐리게
	const int cols = (int)passes.size() + 1;
	if (cols > 1 && ImGui::BeginTable("##rg", cols, ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit,
		ImVec2(0.0f, ImGui::GetContentRegionAvail().y * 0.62f)))
	{
		ImGui::TableSetupScrollFreeze(1, 1);
		ImGui::TableSetupColumn("Resource");
		for (const auto& p : passes)
		{
			const std::string n = p.value("name", std::string());
			ImGui::TableSetupColumn(n.c_str(), ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize(n.c_str()).x + 10.0f);   // 패스 이름이 다 보이게
		}
		ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
		ImGui::TableSetColumnIndex(0);
		ImGui::TextUnformatted("Resource \\ Pass");
		for (int c = 0; c < (int)passes.size(); ++c)
		{
			ImGui::TableSetColumnIndex(c + 1);
			const bool culled = passes[c].value("culled", false);
			ImGui::PushStyleColor(ImGuiCol_Text, culled ? IM_COL32(120, 120, 120, 255) : IM_COL32(230, 230, 230, 255));
			ImGui::TextUnformatted(passes[c].value("name", std::string()).c_str());
			ImGui::PopStyleColor();
		}
		for (const auto& r : resources)
		{
			const std::string name = r.value("name", std::string());
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted((name + (r.value("imported", false) ? "" : " (transient)")).c_str());
			for (int c = 0; c < (int)passes.size(); ++c)
			{
				bool rd = false, wr = false;
				for (const auto& t : passes[c]["reads"])
					if (t.get<std::string>().rfind(name + "@", 0) == 0) rd = true;
				for (const auto& t : passes[c]["writes"])
					if (t.get<std::string>().rfind(name + "@", 0) == 0) wr = true;
				if (!rd && !wr)
					continue;
				ImGui::TableSetColumnIndex(c + 1);
				const bool culled = passes[c].value("culled", false);
				const ImU32 color = culled ? IM_COL32(110, 110, 110, 255) : (wr ? IM_COL32(240, 120, 90, 255) : IM_COL32(110, 190, 250, 255));
				ImGui::PushStyleColor(ImGuiCol_Text, color);
				ImGui::TextUnformatted(rd && wr ? "RW" : wr ? "W" : "R");
				ImGui::PopStyleColor();
			}
		}
		ImGui::EndTable();
	}

	// 패스 목록: CPU 시간 · 빠짐 · 결과 밖
	for (const auto& p : passes)
	{
		const bool culled = p.value("culled", false);
		const bool compute = p.value("queue", std::string()) == "compute";
		snprintf(line, sizeof(line), "%s%s%s%s%s  %.3f ms", p.value("name", std::string()).c_str(), culled ? "  (culled)" : "",
			p.value("sideEffect", false) ? "  (side effect)" : "", compute ? "  [async compute queue]" : p.value("async", false) ? "  [async - same queue]" : "",
			p.value("waitsAsync", false) ? "  (waits for async compute)" : "", p.value("cpuMs", 0.0));
		ImGui::PushStyleColor(ImGuiCol_Text, culled ? IM_COL32(120, 120, 120, 255) : compute ? IM_COL32(150, 220, 120, 255) : IM_COL32(220, 220, 220, 255));
		ImGui::TextUnformatted(line);
		ImGui::PopStyleColor();
	}
	const nlohmann::json pool = RenderGraph::PoolInfo();
	snprintf(line, sizeof(line), "%d textures (%.1f MB), created %d", pool.value("count", 0), pool.value("megabytes", 0.0), pool.value("created", 0));
	UnityGUI::ValueLabel("Transient Pool", line);
}
