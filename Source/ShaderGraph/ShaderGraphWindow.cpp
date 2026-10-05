#include "pch.h"
#include "ShaderGraphWindow.h"
#include "ShaderGraphOps.h"
#include "ShaderGraphRuntime.h"
#include "ShaderGraphPreview.h"
#include "CliServer.h"
#include "EditorExtensions.h"
#include "EditorGUIManager.h"
#include "SelectionManager.h"
#include "UndoSystem.h"
#include "MeshRenderer.h"
#include "UnityGUI.h"
#include "ImGui/imgui_internal.h"
#include "IconsFontAwesome/IconsFontAwesome6.h"
#include <filesystem>
#include <functional>
#include <map>
#include <set>

namespace fs = std::filesystem;
using namespace ShaderGraph;

ShaderGraphWindow* ShaderGraphWindow::s_Instance = nullptr;

namespace
{
	// ---- 노드 편집기 id: 노드 = 그래프 id (Master = kMaster), 핀 = 노드 << 9 | 출력 << 8 | 포트 번호, 선 = 입력 핀 | kLinkBit
	constexpr uintptr_t kMaster = 0xFFFFF;
	constexpr uintptr_t kLinkBit = uintptr_t(1) << 40;

	uintptr_t NodeEd(int id) { return id == 0 ? kMaster : (uintptr_t)id; }
	int NodeOf(uintptr_t edId) { return edId == kMaster ? 0 : (int)edId; }
	uintptr_t PinEd(int node, bool out, int index) { return (NodeEd(node) << 9) | (out ? 0x100 : 0) | (uintptr_t)index; }
	struct PinRef { int Node = -1; bool Out = false; int Index = 0; };
	PinRef DecodePin(uintptr_t pin)
	{
		PinRef r;
		r.Node = NodeOf(pin >> 9);
		r.Out = (pin & 0x100) != 0;
		r.Index = (int)(pin & 0xFF);
		return r;
	}

	// Unity Shader Graph 의 포트 색
	ImU32 WidthColor(int w)
	{
		switch (w)
		{
		case 1: return IM_COL32(132, 228, 231, 255);
		case 2: return IM_COL32(154, 239, 146, 255);
		case 3: return IM_COL32(246, 255, 154, 255);
		case 4: return IM_COL32(251, 203, 244, 255);
		case kTexture: return IM_COL32(255, 139, 139, 255);
		default: return IM_COL32(170, 170, 170, 255);
		}
	}

	ImU32 CategoryColor(const std::string& cat)
	{
		if (cat.rfind("Input", 0) == 0) return IM_COL32(150, 60, 60, 255);
		if (cat.rfind("Math", 0) == 0) return IM_COL32(60, 95, 150, 255);
		if (cat.rfind("Procedural", 0) == 0) return IM_COL32(120, 70, 150, 255);
		if (cat.rfind("UV", 0) == 0) return IM_COL32(60, 130, 90, 255);
		if (cat.rfind("Channel", 0) == 0) return IM_COL32(150, 110, 50, 255);
		if (cat.rfind("Artistic", 0) == 0) return IM_COL32(150, 80, 110, 255);
		if (cat == "Property") return IM_COL32(70, 70, 70, 255);
		return IM_COL32(80, 80, 80, 255);
	}

	const char* BindLabel(const std::string& bind)
	{
		if (bind == "uv") return "UV0";
		if (bind == "posW") return "World Space";
		if (bind == "normalW") return "World Normal";
		if (bind == "viewW") return "View Dir";
		if (bind == "screen") return "Screen";
		return "";
	}

	std::vector<PortDef> InputsOf(const Graph& g, int node)
	{
		if (node == 0) return MasterInputs(g);
		if (const Node* n = g.FindNode(node))
			if (const NodeDef* d = DefOf(*n)) return d->In;
		return {};
	}

	std::vector<PortDef> OutputsOf(const Graph& g, const Node& n)
	{
		if (n.Type == "Property")
		{
			PortDef p;
			p.Name = "Out";
			p.Width = OutputWidth(g, n, "Out");
			return { p };
		}
		if (const NodeDef* d = DefOf(n)) return d->Out;
		return {};
	}

	std::string StemOf(const std::string& asset)
	{
		return std::filesystem::path(string_to_wstring(asset)).stem().string();
	}

	// Project 창에서 끌어 온 경로 → 프로젝트 상대 경로
	std::string DroppedPath(const ImGuiPayload* payload)
	{
		std::string dropped(static_cast<const char*>(payload->Data));
		const std::string root = PathManager::GetI()->GetContentPathS();
		if (_strnicmp(dropped.c_str(), root.c_str(), root.size()) == 0) dropped = dropped.substr(root.size());
		while (!dropped.empty() && (dropped[0] == '\\' || dropped[0] == '/')) dropped.erase(0, 1);
		return dropped;
	}

	const char* kOutputTypes[] = { "Float", "Vector2", "Vector3", "Vector4", "Color" };
	const char* kPortTypes[] = { "Float", "Vector2", "Vector3", "Vector4", "Color", "Texture2D" };
	int IndexOf(const char* const* items, int count, const std::string& v)
	{
		for (int i = 0; i < count; ++i) if (v == items[i]) return i;
		return 0;
	}

	bool Linked(const Graph& g, int node, const std::string& port)
	{
		for (const Edge& e : g.Edges)
			if (e.ToNode == node && e.ToPort == port) return true;
		return false;
	}

	int LinkedWidth(const Graph& g, int node, const std::string& port)
	{
		for (const Edge& e : g.Edges)
			if (e.ToNode == node && e.ToPort == port)
				if (const Node* src = g.FindNode(e.FromNode)) return OutputWidth(g, *src, e.FromPort);
		return -1;
	}

	void PinIcon(ImU32 color, bool filled)
	{
		const float s = ImGui::GetTextLineHeight();
		const ImVec2 p = ImGui::GetCursorScreenPos();
		ImGui::Dummy(ImVec2(s * 0.8f, s));
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 c(p.x + s * 0.4f, p.y + s * 0.5f);
		if (filled) dl->AddCircleFilled(c, s * 0.28f, color);
		else dl->AddCircle(c, s * 0.28f, color, 0, 1.5f);
	}

	std::string Utf8Path(const fs::path& p) { return wstring_to_string(p.wstring()); }

	// 노드 종류 → 메뉴 경로 ("Math/Basic" → ["Math", "Basic"])
	std::vector<std::string> SplitCategory(const std::string& c)
	{
		std::vector<std::string> out;
		size_t start = 0;
		while (true)
		{
			const size_t slash = c.find('/', start);
			out.push_back(c.substr(start, slash == std::string::npos ? std::string::npos : slash - start));
			if (slash == std::string::npos) break;
			start = slash + 1;
		}
		return out;
	}
}

ShaderGraphWindow::ShaderGraphWindow()
	: EditorWindow("Shader Graph", ICON_FA_DIAGRAM_PROJECT)
{
	s_Instance = this;
	SetIsOpened(false);
}

ShaderGraphWindow::~ShaderGraphWindow()
{
	if (m_Ctx) ax::NodeEditor::DestroyEditor(m_Ctx);
	if (s_Instance == this) s_Instance = nullptr;
}

void ShaderGraphWindow::Focus()
{
	if (!s_Instance) return;
	s_Instance->SetIsOpened(true);
	ImGui::SetWindowFocus(s_Instance->GetImGuiName().c_str());
}

void ShaderGraphWindow::BeforeBegin()
{
	ImGui::SetNextWindowSize(ImVec2(1280.0f, 760.0f), ImGuiCond_FirstUseEver);
	if (EditorWindow* scene = EditorGUIManager::GetI()->FindWindow("Scene"))
		if (ImGuiWindow* w = ImGui::FindWindowByName(scene->GetImGuiName().c_str()); w && w->DockId)
			ImGui::SetNextWindowDockID(w->DockId, ImGuiCond_FirstUseEver);
}

void ShaderGraphWindow::BeginEdit()
{
	if (!m_EditActive)
	{
		Doc().Snapshot();
		m_EditActive = true;
	}
}

void ShaderGraphWindow::Save()
{
	std::string error;
	Document& d = Doc();
	if (d.Asset.empty())
	{
		SetStatus("This graph has no file yet: make one in the Project window (Create > Shader Graph) or with nova shadergraph new.", true);
		return;
	}
	// 셰이더는 백그라운드에서 만든다 (그동안 예전 셰이더로 그린다) — 끝나면 상태 줄이 오류 / 성공을 보여 준다
	if (SaveDoc(d.Asset, error, false))
		SetStatus(d.G.IsSubGraph() ? "Saved " + d.Asset + " - graphs that use it rebuild" : "Saved " + d.Asset);
	else
		SetStatus(error, true);
}

void ShaderGraphWindow::ApplyToSelection()
{
	Document& d = Doc();
	GameObject* go = SelectionManager::GetSelectedObjectType() == SelectionType::GAMEOBJECT ? SelectionManager::GetSelectedGameObject() : nullptr;
	MeshRenderer* mr = go ? go->GetComponent<MeshRenderer>() : nullptr;
	if (!mr) { SetStatus("Select a GameObject with a Mesh Renderer first.", true); return; }
	if (d.Asset.empty()) { SetStatus("Save the graph first.", true); return; }
	std::string error;
	if (d.Dirty && !SaveDoc(d.Asset, error)) { SetStatus(error, true); return; }
	// 그래프 옆 <이름>.mat 를 (없으면 만들어) 쓴다
	std::wstring matFull = fs::path(FullPath(d.Asset)).replace_extension(L".mat").wstring();
	std::string mat;
	std::error_code ec;
	if (fs::exists(matFull, ec))
		mat = wstring_to_string(PathManager::GetI()->GetCutSolutionPath(matFull));
	else
		mat = MakeMaterial(d.Asset, wstring_to_string(PathManager::GetI()->GetCutSolutionPath(matFull)), error);
	if (mat.empty()) { SetStatus(error, true); return; }
	nlohmann::json j = mr->toJson();
	nlohmann::json paths = nlohmann::json::array({ mat });
	if (j.contains("m_MaterialPaths") && j["m_MaterialPaths"].is_array() && j["m_MaterialPaths"].size() > 1)
	{
		paths = j["m_MaterialPaths"];
		paths[0] = mat;
	}
	j["m_MaterialPaths"] = paths;
	mr->fromJson(j);
	Undo::Touch(go);
	Undo::SetActionName("Apply Shader Graph Material");
	Undo::RequestCheck();
	SetStatus("Applied " + mat + " to " + go->GetName());
}

void ShaderGraphWindow::DrawMenuBar()
{
	// 메뉴 (Anim2D 창과 같은 방식: 메뉴 막대에는 메뉴만, 버튼은 아래 도구 줄)
	Document& d = Doc();
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(12.0f, 4.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 3.0f));
	if (ImGui::BeginMenuBar())
	{
		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("Save Asset", "Ctrl+S")) Save();
			if (ImGui::MenuItem("Apply to Selection")) ApplyToSelection();
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Edit"))
		{
			if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !d.UndoStack.empty())) d.Undo();
			if (ImGui::MenuItem("Redo", "Ctrl+Y", false, !d.RedoStack.empty())) d.Redo();
			ImGui::EndMenu();
		}
		ImGui::EndMenuBar();
	}
	ImGui::PopStyleVar(2);

	// 도구 줄: Save Asset · Apply to Selection · Frame All · 파일 이름 (* = 저장 안 함) · 셰이더 이름
	ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + 6.0f, ImGui::GetCursorPosY() + 3.0f));
	if (ImGui::Button("Save Asset")) Save();
	ImGui::SameLine(0, 6);
	if (ImGui::Button("Apply to Selection")) ApplyToSelection();
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Make <graph>.mat next to the graph (if missing) and put it on the selected Mesh Renderer");
	ImGui::SameLine(0, 6);
	if (ImGui::Button("Frame All") && m_Ctx)
	{
		ax::NodeEditor::SetCurrentEditor(m_Ctx);
		ax::NodeEditor::NavigateToContent();
		ax::NodeEditor::SetCurrentEditor(nullptr);
	}
	ImGui::SameLine(0, 18);
	const std::string title = d.Asset.empty() ? std::string("(no file)") : Utf8Path(fs::path(string_to_wstring(d.Asset)).filename());
	ImGui::AlignTextToFramePadding();
	ImGui::TextDisabled("%s%s   %s", title.c_str(), d.Dirty ? " *" : "", d.Asset.empty() ? "" : ShaderNameOf(d.Asset).c_str());
	ImGui::Dummy(ImVec2(0, 2.0f));
}

void ShaderGraphWindow::DrawBlackboard(float width, float height)
{
	Document& d = Doc();
	Graph& g = d.G;
	ImGui::BeginChild("##blackboard", ImVec2(width, height), true);
	ImGui::PushFont(UnityGUI::BoldFont());
	ImGui::TextUnformatted("Blackboard");
	ImGui::PopFont();
	ImGui::SameLine(width - 34.0f);
	if (ImGui::SmallButton(ICON_FA_PLUS)) ImGui::OpenPopup("##addprop");
	if (ImGui::BeginPopup("##addprop"))
	{
		for (const char* t : { "Float", "Color", "Vector2", "Vector3", "Vector4", "Texture2D" })
			if (ImGui::MenuItem(t))
			{
				nlohmann::json r;
				std::string e;
				const std::string name = std::string(t == std::string("Texture2D") ? "Texture" : t);
				if (RunOp("property.add", { { "name", name }, { "type", t } }, r, e)) m_SelectedProp = r["ref"].get<std::string>(), m_SelectedNode = -1;
				else SetStatus(e, true);
			}
		ImGui::EndPopup();
	}
	// 경로 (Unity 의 Blackboard 부제목): 셰이더 이름 = <경로>/<파일 이름>
	if (m_PathRevision != d.Revision)
	{
		strncpy_s(m_PathBuf, g.Path.c_str(), _TRUNCATE);
		m_PathRevision = d.Revision;
	}
	ImGui::SetNextItemWidth(-1);
	if (ImGui::InputText("##sgpath", m_PathBuf, sizeof(m_PathBuf), ImGuiInputTextFlags_EnterReturnsTrue) || (ImGui::IsItemDeactivatedAfterEdit()))
	{
		nlohmann::json r;
		std::string e;
		if (!RunOp("settings", { { "path", std::string(m_PathBuf) } }, r, e)) SetStatus(e, true);
	}
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Shader path: the material's Shader is <path>/<file name> (save to apply)");
	ImGui::Separator();
	std::string remove;
	for (const Property& p : g.Properties)
	{
		ImGui::PushID(p.Ref.c_str());
		const bool selected = m_SelectedProp == p.Ref;
		const ImVec2 start = ImGui::GetCursorScreenPos();
		if (ImGui::Selectable(("##" + p.Ref).c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, ImGui::GetTextLineHeight() + 4.0f)))
		{
			m_SelectedProp = p.Ref;
			m_SelectedNode = -1;
			if (ImGui::IsMouseDoubleClicked(0))
			{
				// 더블클릭 = 캔버스 가운데에 Property 노드
				d.Snapshot();
				const int id = g.AddNode("Property", m_MouseCanvas.x, m_MouseCanvas.y);
				g.FindNode(id)->Options["ref"] = p.Ref;
				d.Changed();
			}
		}
		if (ImGui::BeginDragDropSource())
		{
			ImGui::SetDragDropPayload("SG_PROPERTY", p.Ref.c_str(), p.Ref.size() + 1);
			ImGui::Text("%s", p.Name.c_str());
			ImGui::EndDragDropSource();
		}
		if (ImGui::BeginPopupContextItem())
		{
			if (ImGui::MenuItem("Delete")) remove = p.Ref;
			ImGui::EndPopup();
		}
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddCircleFilled(ImVec2(start.x + 7.0f, start.y + ImGui::GetTextLineHeight() * 0.5f + 2.0f), 4.0f, WidthColor(p.Width()));
		dl->AddText(ImVec2(start.x + 16.0f, start.y + 2.0f), ImGui::GetColorU32(ImGuiCol_Text), p.Name.c_str());
		const ImVec2 ts = ImGui::CalcTextSize(p.Type.c_str());
		dl->AddText(ImVec2(start.x + width - ts.x - 22.0f, start.y + 2.0f), ImGui::GetColorU32(ImGuiCol_TextDisabled), p.Type.c_str());
		ImGui::PopID();
	}
	if (g.Properties.empty())
		ImGui::TextWrapped("No properties. + adds one; drag it onto the canvas to use it. Each material sets its own value.");
	if (!remove.empty())
	{
		nlohmann::json r;
		std::string e;
		RunOp("property.delete", { { "ref", remove } }, r, e);
		if (m_SelectedProp == remove) m_SelectedProp.clear();
	}
	ImGui::EndChild();
}

bool ShaderGraphWindow::ValueEditor(Node& n, const PortDef& p, float itemWidth)
{
	float v[4] = { p.Default[0], p.Default[1], p.Default[2], p.Default[3] };
	int w = p.Width == kDynamic ? 1 : p.Width;
	if (n.Values.contains(p.Name))
	{
		const nlohmann::json& j = n.Values[p.Name];
		if (j.is_number()) { v[0] = j.get<float>(); if (p.Width != kDynamic) for (int i = 1; i < 4; ++i) v[i] = v[0]; }
		else if (j.is_array())
		{
			for (int i = 0; i < 4 && i < (int)j.size(); ++i) v[i] = j[i].get<float>();
			if (p.Width == kDynamic) w = std::clamp((int)j.size(), 1, 4);
		}
	}
	ImGui::SetNextItemWidth(itemWidth * w);
	bool changed = false;
	switch (w)
	{
	case 1: changed = ImGui::DragFloat("##v", v, 0.01f, 0, 0, "%.3g"); break;
	case 2: changed = ImGui::DragFloat2("##v", v, 0.01f, 0, 0, "%.3g"); break;
	case 3: changed = ImGui::DragFloat3("##v", v, 0.01f, 0, 0, "%.3g"); break;
	default: changed = ImGui::DragFloat4("##v", v, 0.01f, 0, 0, "%.3g"); break;
	}
	if (ImGui::IsItemDeactivated()) m_EditActive = false;
	if (changed)
	{
		BeginEdit();
		if (w == 1) n.Values[p.Name] = v[0];
		else
		{
			nlohmann::json a = nlohmann::json::array();
			for (int i = 0; i < w; ++i) a.push_back(v[i]);
			n.Values[p.Name] = a;
		}
		Doc().Touch();
	}
	return changed;
}

void ShaderGraphWindow::DrawNode(Node& n)
{
	namespace ed = ax::NodeEditor;
	Graph& g = Doc().G;
	const NodeDef* def = DefOf(n);
	ed::PushStyleColor(ed::StyleColor_NodeBorder, ImColor(CategoryColor(def ? def->Category : "")));
	ed::BeginNode(NodeEd(n.Id));
	ImGui::PushID(n.Id);
	std::string title = n.Type;
	if (n.Type == "Property")
	{
		const Property* p = g.FindProperty(n.Options.value("ref", std::string()));
		title = p ? p->Name : "(missing property)";
	}
	else if (n.Type == "Sub Graph")
	{
		const std::string asset = n.Options.value("asset", std::string());
		title = asset.empty() ? std::string("Sub Graph (none)") : StemOf(asset);
	}
	else if (n.Type == "Custom Function")
		title = n.Options.value("name", std::string("Custom Function"));
	ImGui::PushFont(UnityGUI::BoldFont());
	ImGui::TextUnformatted(title.c_str());
	ImGui::PopFont();

	ImGui::BeginGroup();
	if (def)
		for (int i = 0; i < (int)def->In.size(); ++i)
		{
			const PortDef& p = def->In[i];
			const bool linked = Linked(g, n.Id, p.Name);
			int w = p.Width;
			if (linked) w = LinkedWidth(g, n.Id, p.Name);
			ImGui::PushID(i);
			ed::BeginPin(PinEd(n.Id, false, i), ed::PinKind::Input);
			ed::PinPivotAlignment(ImVec2(0.0f, 0.5f));
			PinIcon(WidthColor(w), linked);
			ImGui::SameLine(0, 2);
			ImGui::TextUnformatted(p.Name.c_str());
			ed::EndPin();
			if (!linked)
			{
				ImGui::SameLine(0, 8.0f);
				if (p.Width == kTexture)
				{
					const std::string tex = n.Options.value("texture", std::string());
					ImGui::TextDisabled("%s", tex.empty() ? "(white)" : Utf8Path(fs::path(string_to_wstring(tex)).filename()).c_str());
				}
				else if (!p.Bind.empty())
					ImGui::TextDisabled("%s", BindLabel(p.Bind));
				else
					ValueEditor(n, p, 42.0f);
			}
			ImGui::PopID();
		}
	ImGui::EndGroup();
	ImGui::SameLine(0, 18.0f);
	ImGui::BeginGroup();
	const std::vector<PortDef> outs = OutputsOf(g, n);
	for (int i = 0; i < (int)outs.size(); ++i)
	{
		const PortDef& p = outs[i];
		ed::BeginPin(PinEd(n.Id, true, i), ed::PinKind::Output);
		ed::PinPivotAlignment(ImVec2(1.0f, 0.5f));
		ImGui::TextUnformatted(p.Name.c_str());
		ImGui::SameLine(0, 2);
		const int w = OutputWidth(g, n, p.Name);
		bool used = false;
		for (const Edge& e : g.Edges) used |= e.FromNode == n.Id && e.FromPort == p.Name;
		PinIcon(WidthColor(w), used);
		ed::EndPin();
	}
	ImGui::EndGroup();
	// 미리보기 (첫 출력의 값 — Unity 의 노드 미리보기)
	if (m_ShowPreviews && m_Preview && n.Type != "Property")
	{
		ImVec2 uv0, uv1;
		if (ImTextureID tex = m_Preview->NodeTexture(n.Id, uv0, uv1))
			ImGui::Image(tex, ImVec2((float)ShaderGraphPreview::kTile, (float)ShaderGraphPreview::kTile), uv0, uv1);
		else
			ImGui::Dummy(ImVec2((float)ShaderGraphPreview::kTile, (float)ShaderGraphPreview::kTile));
	}
	ImGui::PopID();
	ed::EndNode();
	ed::PopStyleColor();
}

void ShaderGraphWindow::DrawMaster()
{
	namespace ed = ax::NodeEditor;
	Graph& g = Doc().G;
	ed::PushStyleColor(ed::StyleColor_NodeBorder, ImColor(IM_COL32(200, 200, 200, 255)));
	ed::BeginNode(kMaster);
	const std::vector<PortDef> ins = MasterInputs(g);
	// Unity 의 Master Stack: Vertex 블록 + Fragment 블록 (Sub Graph 는 Output)
	auto header = [](const std::string& text) {
		ImGui::PushFont(UnityGUI::BoldFont());
		ImGui::TextUnformatted(text.c_str());
		ImGui::PopFont();
	};
	if (g.IsSubGraph())
		header("Output");
	for (int i = 0; i < (int)ins.size(); ++i)
	{
		const PortDef& p = ins[i];
		if (!g.IsSubGraph())
		{
			if (i == 0) header("Vertex");
			if (!IsVertexPort(p.Name) && (i == 0 || IsVertexPort(ins[i - 1].Name)))
			{
				ImGui::Dummy(ImVec2(0, 4.0f));
				header("Fragment (" + g.Material + ")");
			}
		}
		const bool linked = Linked(g, 0, p.Name);
		ed::BeginPin(PinEd(0, false, i), ed::PinKind::Input);
		ed::PinPivotAlignment(ImVec2(0.0f, 0.5f));
		PinIcon(WidthColor(p.Width), linked);
		ImGui::SameLine(0, 2);
		ImGui::TextUnformatted(p.Name.c_str());
		ed::EndPin();
		if (!linked)
		{
			ImGui::SameLine(0, 8.0f);
			if (!p.Bind.empty()) ImGui::TextDisabled("Object");
			else if (p.Width == 1) ImGui::TextDisabled("%.2g", p.Default[0]);
			else ImGui::TextDisabled("(%.2g, %.2g, %.2g)", p.Default[0], p.Default[1], p.Default[2]);
		}
	}
	ed::EndNode();
	ed::PopStyleColor();
}

void ShaderGraphWindow::DrawCreatePopup()
{
	namespace ed = ax::NodeEditor;
	Document& d = Doc();
	Graph& g = d.G;
	if (m_OpenCreate)
	{
		ImGui::OpenPopup("Create Node");
		m_Search[0] = 0;
		m_OpenCreate = false;
	}
	ImGui::SetNextWindowSize(ImVec2(260.0f, 0.0f));
	if (!ImGui::BeginPopup("Create Node"))
		return;
	ImGui::TextDisabled("Create Node");
	if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
	ImGui::SetNextItemWidth(-1);
	ImGui::InputTextWithHint("##search", "Search", m_Search, sizeof(m_Search));
	std::string chosen;
	const std::string q = [&] { std::string s = m_Search; for (char& c : s) c = (char)tolower((unsigned char)c); return s; }();
	if (!q.empty())
	{
		ImGui::BeginChild("##results", ImVec2(0, 300.0f));
		for (const NodeDef& def : NodeDefs())
		{
			std::string t = def.Type;
			for (char& c : t) c = (char)tolower((unsigned char)c);
			if (t.find(q) == std::string::npos) continue;
			if (ImGui::Selectable((def.Type + "##" + def.Category).c_str())) chosen = def.Type;
			ImGui::SameLine(170.0f);
			ImGui::TextDisabled("%s", def.Category.c_str());
		}
		// 속성 노드
			for (const Property& p : g.Properties)
		{
			std::string t = p.Name;
			for (char& c : t) c = (char)tolower((unsigned char)c);
			if (t.find(q) != std::string::npos && ImGui::Selectable(("Property: " + p.Name).c_str())) chosen = "@" + p.Ref;
		}
		for (const std::string& s : SubGraphAssets())
		{
			std::string t = StemOf(s);
			for (char& c : t) c = (char)tolower((unsigned char)c);
			if (t.find(q) != std::string::npos && ImGui::Selectable(("Sub Graph: " + StemOf(s)).c_str())) chosen = "#" + s;
		}
		ImGui::EndChild();
		if (ImGui::IsKeyPressed(ImGuiKey_Enter) && chosen.empty())
			for (const NodeDef& def : NodeDefs())
			{
				std::string t = def.Type;
				for (char& c : t) c = (char)tolower((unsigned char)c);
				if (t.find(q) != std::string::npos) { chosen = def.Type; break; }
			}
	}
	else
	{
		// 분류 메뉴 (Unity 처럼 Input / Math / ... 나무)
		std::vector<std::string> open;   // 지금 열린 메뉴 경로
		std::vector<bool> shown;
		std::map<std::string, std::vector<const NodeDef*>> byCat;
		for (const NodeDef& def : NodeDefs())
			if (def.Type != "Property" && def.Type != "Sub Graph") byCat[def.Category].push_back(&def);
		std::function<void(const std::string&)> menu = [&](const std::string& prefix) {
			std::set<std::string> subs;
			for (const auto& [cat, defs] : byCat)
			{
				if (cat.rfind(prefix, 0) != 0) continue;
				const std::string rest = cat.substr(prefix.size());
				const size_t slash = rest.find('/');
				subs.insert(slash == std::string::npos ? rest : rest.substr(0, slash));
			}
			for (const std::string& s : subs)
			{
				if (s.empty()) continue;
				if (ImGui::BeginMenu(s.c_str()))
				{
					menu(prefix + s + "/");
					auto it = byCat.find(prefix + s);
					if (it != byCat.end())
						for (const NodeDef* def : it->second)
							if (ImGui::MenuItem(def->Type.c_str())) chosen = def->Type;
					ImGui::EndMenu();
				}
			}
		};
		menu("");
		if (!g.Properties.empty() && ImGui::BeginMenu("Properties"))
		{
			for (const Property& p : g.Properties)
				if (ImGui::MenuItem(p.Name.c_str())) chosen = "@" + p.Ref;
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Sub Graphs"))
		{
			const std::vector<std::string> subs = SubGraphAssets();
			for (const std::string& s : subs)
				if (ImGui::MenuItem(StemOf(s).c_str())) chosen = "#" + s;
			if (subs.empty()) ImGui::TextDisabled("Create > Shader Sub Graph in the Project window");
			ImGui::EndMenu();
		}
	}
	if (!chosen.empty())
	{
		d.Snapshot();
		int id = 0;
		if (chosen[0] == '@')
		{
			id = g.AddNode("Property", m_CreatePos.x, m_CreatePos.y);
			g.FindNode(id)->Options["ref"] = chosen.substr(1);
		}
		else if (chosen[0] == '#')
		{
			id = g.AddNode("Sub Graph", m_CreatePos.x, m_CreatePos.y);
			g.FindNode(id)->Options["asset"] = chosen.substr(1);
		}
		else
			id = g.AddNode(chosen, m_CreatePos.x, m_CreatePos.y);
		// 핀을 끌어 놓은 자리에서 만들었으면 그 핀과 잇는다 (맞는 첫 포트)
		if (m_CreateFromPin && id)
		{
			const PinRef pin = DecodePin(m_CreateFromPin);
			Node* n = g.FindNode(id);
			std::string err;
			if (pin.Out)
			{
				const Node* src = g.FindNode(pin.Node);
				const std::vector<PortDef> outs = src ? OutputsOf(g, *src) : std::vector<PortDef>();
				if (pin.Index < (int)outs.size())
					for (const PortDef& in : InputsOf(g, id))
						if (g.Connect(pin.Node, outs[pin.Index].Name, id, in.Name, err)) break;
			}
			else
			{
				const std::vector<PortDef> ins = InputsOf(g, pin.Node);
				if (pin.Index < (int)ins.size())
					for (const PortDef& out : OutputsOf(g, *n))
						if (g.Connect(id, out.Name, pin.Node, ins[pin.Index].Name, err)) break;
			}
		}
		m_CreateFromPin = 0;
		m_SelectedNode = id;
		d.Changed();
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

void ShaderGraphWindow::DrawCanvas(float width, float height)
{
	namespace ed = ax::NodeEditor;
	Document& d = Doc();
	Graph& g = d.G;
	if (!m_Ctx)
	{
		ed::Config config;
		config.SettingsFile = nullptr;   // 노드 위치는 .shadergraph 에
		m_Ctx = ed::CreateEditor(&config);
	}
	ImGui::BeginChild("##canvasHost", ImVec2(width, height), false, ImGuiWindowFlags_NoScrollbar);
	const ImVec2 canvasMin = ImGui::GetCursorScreenPos();
	ed::SetCurrentEditor(m_Ctx);
	ed::Begin("##shadergraph", ImVec2(width, height));
	m_MouseCanvas = ImGui::GetMousePos();   // 캔버스 안에서는 캔버스 좌표

	// 문서가 바뀌었으면 (열기 · Undo · CLI) 노드 위치를 다시 넣는다
	const bool resync = m_SeenRevision != d.Revision;
	if (resync)
	{
		for (const Node& n : g.Nodes)
			ed::SetNodePosition(NodeEd(n.Id), ImVec2(n.X, n.Y));
		if (m_SeenRevision == 0 || g.Nodes.empty())
			ed::SetNodePosition(kMaster, ImVec2(420.0f, 0.0f));
	}

	for (Node& n : g.Nodes)
		DrawNode(n);
	DrawMaster();

	// 선
	for (const Edge& e : g.Edges)
	{
		const std::vector<PortDef>& ins = InputsOf(g, e.ToNode);
		int inIndex = -1, outIndex = -1;
		for (int i = 0; i < (int)ins.size(); ++i) if (ins[i].Name == e.ToPort) inIndex = i;
		const Node* src = g.FindNode(e.FromNode);
		if (!src || inIndex < 0) continue;
		const std::vector<PortDef> outs = OutputsOf(g, *src);
		for (int i = 0; i < (int)outs.size(); ++i) if (outs[i].Name == e.FromPort) outIndex = i;
		if (outIndex < 0) continue;
		const uintptr_t inPin = PinEd(e.ToNode, false, inIndex);
		ed::Link(inPin | kLinkBit, PinEd(e.FromNode, true, outIndex), inPin, ImColor(WidthColor(OutputWidth(g, *src, e.FromPort))), 2.0f);
	}

	// 잇기 · 핀에서 빈 곳으로 끌기
	if (ed::BeginCreate(ImVec4(1, 1, 1, 1), 2.0f))
	{
		ed::PinId a, b;
		if (ed::QueryNewLink(&a, &b) && a && b)
		{
			PinRef pa = DecodePin(a.Get()), pb = DecodePin(b.Get());
			if (!pa.Out) std::swap(pa, pb);
			std::string error;
			bool ok = pa.Out && !pb.Out && pa.Node != 0;
			std::string outName, inName;
			if (ok)
			{
				const Node* src = g.FindNode(pa.Node);
				const std::vector<PortDef> outs = src ? OutputsOf(g, *src) : std::vector<PortDef>();
				const std::vector<PortDef>& ins = InputsOf(g, pb.Node);
				ok = pa.Index < (int)outs.size() && pb.Index < (int)ins.size();
				if (ok)
				{
					outName = outs[pa.Index].Name;
					inName = ins[pb.Index].Name;
					Graph test = g;
					ok = test.Connect(pa.Node, outName, pb.Node, inName, error);
				}
			}
			else
				error = "connect an output to an input";
			if (!ok)
			{
				ed::Suspend();
				ImGui::SetTooltip("%s", error.c_str());
				ed::Resume();
				ed::RejectNewItem(ImVec4(1, 0.3f, 0.3f, 1), 2.0f);
			}
			else if (ed::AcceptNewItem(ImVec4(0.5f, 1, 0.5f, 1), 3.0f))
			{
				d.Snapshot();
				g.Connect(pa.Node, outName, pb.Node, inName, error);
				d.Changed();
			}
		}
		ed::PinId pin;
		if (ed::QueryNewNode(&pin) && ed::AcceptNewItem())
		{
			m_CreateFromPin = pin.Get();
			m_CreatePos = ImGui::GetMousePos();
			m_OpenCreate = true;
		}
	}
	ed::EndCreate();

	// 지우기
	if (ed::BeginDelete())
	{
		bool snap = false;
		auto snapshotOnce = [&] { if (!snap) { d.Snapshot(); snap = true; } };
		ed::LinkId link;
		while (ed::QueryDeletedLink(&link))
		{
			if (ed::AcceptDeletedItem())
			{
				const PinRef in = DecodePin(link.Get() & ~kLinkBit);
				const std::vector<PortDef>& ins = InputsOf(g, in.Node);
				if (in.Index < (int)ins.size())
				{
					snapshotOnce();
					g.Disconnect(in.Node, ins[in.Index].Name);
				}
			}
		}
		ed::NodeId node;
		while (ed::QueryDeletedNode(&node))
		{
			if (node.Get() == kMaster) { ed::RejectDeletedItem(); continue; }
			if (ed::AcceptDeletedItem())
			{
				snapshotOnce();
				g.RemoveNode(NodeOf(node.Get()));
				if (m_SelectedNode == NodeOf(node.Get())) m_SelectedNode = -1;
			}
		}
		if (snap) d.Changed();
	}
	ed::EndDelete();

	// 노드 위치 → 문서
	for (Node& n : g.Nodes)
	{
		const ImVec2 p = ed::GetNodePosition(NodeEd(n.Id));
		if (fabsf(p.x - n.X) > 0.5f || fabsf(p.y - n.Y) > 0.5f)
		{
			n.X = p.x;
			n.Y = p.y;
			if (!resync) d.Dirty = true;
		}
	}

	// 고른 노드
	if (ed::HasSelectionChanged())
	{
		ed::NodeId sel[1];
		if (ed::GetSelectedNodes(sel, 1) > 0 && sel[0].Get() != kMaster)
		{
			m_SelectedNode = NodeOf(sel[0].Get());
			m_SelectedProp.clear();
		}
		else if (ed::GetSelectedObjectCount() == 0)
			m_SelectedNode = -1;
	}

	// 오른쪽 클릭 · Space = Create Node
	const ImVec2 mouse = ImGui::GetMousePos();
	ed::Suspend();
	if (ed::ShowBackgroundContextMenu() || (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && !ImGui::IsAnyItemActive() && ImGui::IsKeyPressed(ImGuiKey_Space)))
	{
		m_CreatePos = mouse;
		m_CreateFromPin = 0;
		m_OpenCreate = true;
	}
	DrawCreatePopup();
	ed::Resume();

	ed::End();
	if (resync)
	{
		if (m_SeenRevision == 0) ed::NavigateToContent(0.0f);
		m_SeenRevision = d.Revision;
	}
	ed::SetCurrentEditor(nullptr);

	// Blackboard 의 속성을 끌어 놓기 → Property 노드
	if (ImGui::BeginDragDropTargetCustom(ImRect(canvasMin, ImVec2(canvasMin.x + width, canvasMin.y + height)), ImGui::GetID("##sgdrop")))
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SG_PROPERTY"))
		{
			d.Snapshot();
			const int id = g.AddNode("Property", m_MouseCanvas.x, m_MouseCanvas.y);
			g.FindNode(id)->Options["ref"] = std::string(static_cast<const char*>(payload->Data));
			d.Changed();
		}
		// Project 창의 Sub Graph → Sub Graph 노드
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SHADERSUBGRAPH_FILE"))
		{
			const std::string asset = DroppedPath(payload);
			if (std::filesystem::path(string_to_wstring(asset)) == std::filesystem::path(string_to_wstring(d.Asset)))
				SetStatus("A Sub Graph can not use itself.", true);
			else
			{
				d.Snapshot();
				const int id = g.AddNode("Sub Graph", m_MouseCanvas.x, m_MouseCanvas.y);
				g.FindNode(id)->Options["asset"] = asset;
				d.Changed();
			}
		}
		ImGui::EndDragDropTarget();
	}
	ImGui::EndChild();
}

void ShaderGraphWindow::DrawInspector(float width, float height)
{
	Document& d = Doc();
	Graph& g = d.G;
	ImGui::BeginChild("##graphInspector", ImVec2(width, height), true);
	const float previewH = m_ShowMain ? (std::min)(width, height * 0.5f) + 34.0f : 28.0f;
	ImGui::BeginChild("##inspectorTop", ImVec2(0, (std::max)(80.0f, height - previewH - 12.0f)));
	ImGui::PushFont(UnityGUI::BoldFont());
	ImGui::TextUnformatted("Graph Inspector");
	ImGui::PopFont();
	ImGui::Separator();

	// Graph Settings
	ImGui::TextDisabled("Graph Settings");
	if (g.IsSubGraph())
	{
		// Sub Graph: 출력 목록 (Output 노드의 입력 = Sub Graph 노드의 출력)
		ImGui::TextUnformatted("Outputs");
		std::string removeName;
		for (size_t i = 0; i < g.Outputs.size(); ++i)
		{
			SubOutput& o = g.Outputs[i];
			ImGui::PushID((int)i);
			char buf[64] = {};
			strncpy_s(buf, o.Name.c_str(), _TRUNCATE);
			ImGui::SetNextItemWidth(width * 0.45f);
			if (ImGui::InputText("##oname", buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue) && buf[0] && o.Name != buf)
			{
				nlohmann::json r;
				std::string e;
				if (!RunOp("output.set", { { "name", o.Name }, { "rename", std::string(buf) } }, r, e)) SetStatus(e, true);
				ImGui::PopID();
				break;
			}
			ImGui::SameLine();
			int t = IndexOf(kOutputTypes, 5, o.Type);
			ImGui::SetNextItemWidth(width * 0.32f);
			if (ImGui::Combo("##otype", &t, kOutputTypes, 5))
			{
				nlohmann::json r;
				std::string e;
				RunOp("output.set", { { "name", o.Name }, { "type", kOutputTypes[t] } }, r, e);
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("x")) removeName = o.Name;
			ImGui::PopID();
		}
		if (!removeName.empty())
		{
			nlohmann::json r;
			std::string e;
			RunOp("output.delete", { { "name", removeName } }, r, e);
		}
		if (ImGui::SmallButton("+ Output"))
		{
			std::string n = "Out";
			for (int k = 1; std::any_of(g.Outputs.begin(), g.Outputs.end(), [&](const SubOutput& x) { return x.Name == n; }); ++k) n = "Out" + std::to_string(k);
			nlohmann::json r;
			std::string e;
			RunOp("output.add", { { "name", n }, { "type", "Vector3" } }, r, e);
		}
		ImGui::TextWrapped("Blackboard properties are the Sub Graph node's inputs.");
	}
	else
	{
	int mat = g.Material == "Unlit" ? 1 : (g.Material == "Decal" ? 2 : 0);
	static const char* kMat[] = { "Lit", "Unlit", "Decal" };
	ImGui::SetNextItemWidth(-1);
	if (ImGui::Combo("##material", &mat, kMat, 3))
	{
		nlohmann::json r;
		std::string e;
		RunOp("settings", { { "material", kMat[mat] } }, r, e);
	}
	ImGui::TextUnformatted("Surface Type");
	ImGui::SameLine(110.0f);
	int surface = g.Surface == "Transparent" ? 1 : 0;
	static const char* kSurface[] = { "Opaque", "Transparent" };
	ImGui::SetNextItemWidth(-1);
	if (ImGui::Combo("##surface", &surface, kSurface, 2))
	{
		nlohmann::json r;
		std::string e;
		RunOp("settings", { { "surface", kSurface[surface] } }, r, e);
	}
	bool clip = g.AlphaClip;
	if (ImGui::Checkbox("Alpha Clipping", &clip))
	{
		nlohmann::json r;
		std::string e;
		RunOp("settings", { { "alphaClip", clip } }, r, e);
	}
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Cut away pixels whose Alpha is below Alpha Clip Threshold (also in shadows)");
	// 테셀레이션 (HDRP 처럼): 켜면 Vertex 블록에 Displacement (m, 법선 쪽)
	if (g.Material != "Decal" && g.Surface != "Transparent")
	{
		bool tess = g.Tessellation;
		if (ImGui::Checkbox("Tessellation", &tess))
		{
			nlohmann::json r;
			std::string e;
			RunOp("settings", { { "tessellation", tess } }, r, e);
		}
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Subdivide triangles near the camera and move each new vertex along the normal by the Displacement input (Vertex block)");
		if (g.Tessellation)
		{
			// 끄는 동안은 그래프 값만 (셰이더를 다시 만들지 않는다 — 실행 값), 놓을 때 settings 로 저장 · 되돌리기
			static float s_before = 0.0f;
			auto field = [&](const char* id, float* value, float minV, float maxV, const char* format, const char* key, bool drag) {
				ImGui::SetNextItemWidth(-1);
				const float now = *value;
				if (drag) ImGui::DragFloat(id, value, 0.5f, minV, maxV, format);
				else ImGui::SliderFloat(id, value, minV, maxV, format);
				if (ImGui::IsItemActivated()) s_before = now;
				if (ImGui::IsItemDeactivatedAfterEdit())
				{
					const float v = *value;
					*value = s_before;
					nlohmann::json r;
					std::string e;
					RunOp("settings", { { key, v } }, r, e);
				}
			};
			field("##tessFactor", &g.TessFactor, 1.0f, 64.0f, "Tessellation Factor %.0f", "tessFactor", false);
			field("##tessTri", &g.TessTriangleSize, 2.0f, 100.0f, "Triangle Size %.0f px", "tessTriangleSize", false);
			field("##tessFade", &g.TessFadeDistance, 1.0f, 1000.0f, "Fade Distance %.1f m", "tessFadeDistance", true);
		}
	}
	}
	ImGui::Spacing();
	ImGui::Separator();

	if (Node* n = m_SelectedNode > 0 ? g.FindNode(m_SelectedNode) : nullptr)
	{
		const NodeDef* def = DefOf(*n);
		ImGui::Text("%s  (#%d)", n->Type.c_str(), n->Id);
		if (def && !def->Help.empty()) ImGui::TextWrapped("%s", def->Help.c_str());
		ImGui::Spacing();
		// 이어지지 않은 입력 값
		if (def)
			for (const PortDef& p : def->In)
			{
				if (Linked(g, n->Id, p.Name) || p.Width == kTexture || !p.Bind.empty()) continue;
				ImGui::PushID(p.Name.c_str());
				ImGui::TextUnformatted(p.Name.c_str());
				ImGui::SameLine(100.0f);
				ValueEditor(*n, p, (width - 120.0f) / 4.0f);
				ImGui::PopID();
			}
		// 노드 설정 (options)
		if (n->Type == "Color")
		{
			float c[4] = { 1, 1, 1, 1 };
			const nlohmann::json o = n->Options.value("color", nlohmann::json());
			if (o.is_array()) for (int i = 0; i < 4 && i < (int)o.size(); ++i) c[i] = o[i].get<float>();
			ImGui::SetNextItemWidth(-1);
			if (ImGui::ColorEdit4("##color", c, ImGuiColorEditFlags_Float))
			{
				BeginEdit();
				n->Options["color"] = { c[0], c[1], c[2], c[3] };
				d.Touch();
			}
			if (ImGui::IsItemDeactivated()) m_EditActive = false;
		}
		else if (n->Type == "Swizzle")
		{
			std::string mask = n->Options.value("mask", std::string("xyzw"));
			char buf[8] = {};
			strncpy_s(buf, mask.c_str(), _TRUNCATE);
			ImGui::TextUnformatted("Mask");
			ImGui::SameLine(100.0f);
			ImGui::SetNextItemWidth(-1);
			if (ImGui::InputText("##mask", buf, sizeof(buf)))
			{
				BeginEdit();
				n->Options["mask"] = std::string(buf);
				d.Changed();
			}
			if (ImGui::IsItemDeactivated()) m_EditActive = false;
		}
		else if (n->Type == "Sample Texture 2D")
		{
			int type = n->Options.value("type", std::string("Default")) == "Normal" ? 1 : 0;
			static const char* kType[] = { "Default", "Normal" };
			ImGui::TextUnformatted("Type");
			ImGui::SameLine(100.0f);
			ImGui::SetNextItemWidth(-1);
			if (ImGui::Combo("##type", &type, kType, 2))
			{
				d.Snapshot();
				n->Options["type"] = kType[type];
				d.Changed();
			}
			std::string tex = n->Options.value("texture", std::string());
			UnityGUI::Spacing(2.0f);
			ImGui::TextUnformatted("Texture");
			ImGui::SameLine(100.0f);
			ImGui::SetNextItemWidth(-1);
			char buf[260] = {};
			strncpy_s(buf, tex.c_str(), _TRUNCATE);
			if (ImGui::InputTextWithHint("##tex", "drop a texture", buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue))
			{
				d.Snapshot();
				n->Options["texture"] = std::string(buf);
				d.Changed();
			}
			if (ImGui::BeginDragDropTarget())
			{
				const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_FILE");
				if (!payload) payload = ImGui::AcceptDragDropPayload("PNG_FILE");
				if (payload)
				{
					std::string dropped(static_cast<const char*>(payload->Data));
					const std::string root = PathManager::GetI()->GetContentPathS();
					if (_strnicmp(dropped.c_str(), root.c_str(), root.size()) == 0) dropped = dropped.substr(root.size());
					d.Snapshot();
					n->Options["texture"] = dropped;
					d.Changed();
				}
				ImGui::EndDragDropTarget();
			}
			ImGui::TextWrapped("Or link a Texture2D property to the Texture input (each material sets its own).");
		}
		else if (n->Type == "Position" || n->Type == "Normal Vector")
		{
			static const char* kSpace[] = { "World", "Object" };
			int space = n->Options.value("space", std::string("World")) == "Object" ? 1 : 0;
			ImGui::TextUnformatted("Space");
			ImGui::SameLine(100.0f);
			ImGui::SetNextItemWidth(-1);
			if (ImGui::Combo("##space", &space, kSpace, 2))
			{
				d.Snapshot();
				n->Options["space"] = kSpace[space];
				d.Changed();
			}
		}
		else if (n->Type == "Custom Function")
		{
			json& o = n->Options;
			bool changed = false;
			auto renamePorts = [&](bool output, const std::string& from, const std::string& to) {
				for (Edge& e : g.Edges)
				{
					if (output && e.FromNode == n->Id && e.FromPort == from) e.FromPort = to;
					if (!output && e.ToNode == n->Id && e.ToPort == from) e.ToPort = to;
				}
				if (!output && n->Values.contains(from)) { n->Values[to] = n->Values[from]; n->Values.erase(from); }
			};
			char name[64] = {};
			strncpy_s(name, o.value("name", std::string()).c_str(), _TRUNCATE);
			ImGui::TextUnformatted("Name");
			ImGui::SameLine(100.0f);
			ImGui::SetNextItemWidth(-1);
			if (ImGui::InputText("##cfname", name, sizeof(name))) { BeginEdit(); o["name"] = std::string(name); changed = true; }
			if (ImGui::IsItemDeactivated()) m_EditActive = false;
			static const char* kMode[] = { "String", "File" };
			int mode = o.value("mode", std::string("String")) == "File" ? 1 : 0;
			ImGui::TextUnformatted("Type");
			ImGui::SameLine(100.0f);
			ImGui::SetNextItemWidth(-1);
			if (ImGui::Combo("##cfmode", &mode, kMode, 2)) { d.Snapshot(); o["mode"] = kMode[mode]; changed = true; }
			// 입력 · 출력 목록
			for (int side = 0; side < 2; ++side)
			{
				const char* key = side == 0 ? "inputs" : "outputs";
				if (!o.contains(key) || !o[key].is_array()) o[key] = json::array();
				ImGui::Spacing();
				ImGui::TextUnformatted(side == 0 ? "Inputs" : "Outputs");
				int remove = -1;
				for (int i = 0; i < (int)o[key].size(); ++i)
				{
					json& p = o[key][i];
					ImGui::PushID(side * 1000 + i);
					char pn[48] = {};
					strncpy_s(pn, p.value("name", std::string()).c_str(), _TRUNCATE);
					ImGui::SetNextItemWidth(width * 0.42f);
					if (ImGui::InputText("##pn", pn, sizeof(pn), ImGuiInputTextFlags_EnterReturnsTrue) && pn[0])
					{
						d.Snapshot();
						renamePorts(side == 1, p.value("name", std::string()), pn);
						p["name"] = std::string(pn);
						changed = true;
					}
					ImGui::SameLine();
					const int count = side == 0 ? 6 : 5;
					int t = IndexOf(kPortTypes, count, p.value("type", std::string("Float")));
					ImGui::SetNextItemWidth(width * 0.33f);
					if (ImGui::Combo("##pt", &t, kPortTypes, count)) { d.Snapshot(); p["type"] = kPortTypes[t]; changed = true; }
					ImGui::SameLine();
					if (ImGui::SmallButton("x")) remove = i;
					ImGui::PopID();
				}
				if (remove >= 0) { d.Snapshot(); o[key].erase(o[key].begin() + remove); changed = true; }
				ImGui::PushID(side);
				if (ImGui::SmallButton(side == 0 ? "+ Input" : "+ Output"))
				{
					d.Snapshot();
					o[key].push_back({ { "name", std::string(side == 0 ? "In" : "Out") + std::to_string(o[key].size() + 1) }, { "type", "Float" } });
					changed = true;
				}
				ImGui::PopID();
			}
			ImGui::Spacing();
			if (mode == 0)
			{
				// 본문: void <name>(입력들, out 출력들) { … } 의 안쪽
				std::string body = o.value("body", std::string());
				ImGui::TextUnformatted("Body");
				std::vector<char> buf(body.begin(), body.end());
				buf.resize(body.size() + 4096, 0);
				if (ImGui::InputTextMultiline("##cfbody", buf.data(), buf.size(), ImVec2(-1, 140.0f), ImGuiInputTextFlags_AllowTabInput))
				{
					BeginEdit();
					o["body"] = std::string(buf.data());
					changed = true;
				}
				if (ImGui::IsItemDeactivated()) m_EditActive = false;
			}
			else
			{
				std::string file = o.value("file", std::string());
				char fb[260] = {};
				strncpy_s(fb, file.c_str(), _TRUNCATE);
				ImGui::TextUnformatted("File");
				ImGui::SameLine(100.0f);
				ImGui::SetNextItemWidth(-1);
				if (ImGui::InputTextWithHint("##cffile", "drop a .hlsl", fb, sizeof(fb), ImGuiInputTextFlags_EnterReturnsTrue)) { d.Snapshot(); o["file"] = std::string(fb); changed = true; }
				if (ImGui::BeginDragDropTarget())
				{
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_FILE"))
					{
						d.Snapshot();
						o["file"] = DroppedPath(payload);
						changed = true;
					}
					ImGui::EndDragDropTarget();
				}
				ImGui::TextWrapped("The file defines void %s_float(inputs..., out outputs...) - like Unity.", o.value("name", std::string("MyFunction")).c_str());
			}
			if (changed)
				d.Touch();
		}
		else if (n->Type == "Sub Graph")
		{
			const std::string asset = n->Options.value("asset", std::string());
			ImGui::TextUnformatted("Sub Graph");
			ImGui::SameLine(100.0f);
			ImGui::SetNextItemWidth(-1);
			if (ImGui::BeginCombo("##subasset", asset.empty() ? "(none)" : StemOf(asset).c_str()))
			{
				for (const std::string& s : SubGraphAssets())
					if (ImGui::Selectable(s.c_str(), s == asset) && s != d.Asset)
					{
						d.Snapshot();
						n->Options["asset"] = s;
						d.Changed();
					}
				ImGui::EndCombo();
			}
			if (!asset.empty() && ImGui::Button("Open Sub Graph"))
			{
				std::string err;
				if (d.Dirty) SetStatus("Save this graph first (it has unsaved changes).", true);
				else if (!OpenDoc(asset, err)) SetStatus(err, true);
			}
		}
		else if (n->Type == "Property")
		{
			const std::string ref = n->Options.value("ref", std::string());
			ImGui::TextUnformatted("Property");
			ImGui::SameLine(100.0f);
			ImGui::SetNextItemWidth(-1);
			const Property* cur = g.FindProperty(ref);
			if (ImGui::BeginCombo("##ref", cur ? cur->Name.c_str() : "(none)"))
			{
				for (const Property& p : g.Properties)
					if (ImGui::Selectable(p.Name.c_str(), p.Ref == ref))
					{
						d.Snapshot();
						n->Options["ref"] = p.Ref;
						d.Changed();
					}
				ImGui::EndCombo();
			}
		}
	}
	else if (const Property* cp = g.FindProperty(m_SelectedProp))
	{
		// 속성 설정 (Unity 의 Blackboard 속성 Inspector)
		Property* p = nullptr;
		for (Property& x : g.Properties) if (x.Ref == cp->Ref) p = &x;
		ImGui::Text("%s  (%s)", p->Name.c_str(), p->Type.c_str());
		ImGui::Spacing();
		char name[128] = {};
		strncpy_s(name, p->Name.c_str(), _TRUNCATE);
		ImGui::TextUnformatted("Name");
		ImGui::SameLine(100.0f);
		ImGui::SetNextItemWidth(-1);
		if (ImGui::InputText("##name", name, sizeof(name)))
		{
			BeginEdit();
			p->Name = name;
			d.Touch();
		}
		if (ImGui::IsItemDeactivated()) m_EditActive = false;
		ImGui::TextUnformatted("Reference");
		ImGui::SameLine(100.0f);
		ImGui::TextDisabled("%s", p->Ref.c_str());
		const int w = p->Width();
		if (w != kTexture)
		{
			ImGui::TextUnformatted("Default");
			ImGui::SameLine(100.0f);
			ImGui::SetNextItemWidth(-1);
			bool changed = false;
			if (p->Type == "Color") changed = ImGui::ColorEdit4("##def", p->Value, ImGuiColorEditFlags_Float);
			else if (w == 1 && p->Range) changed = ImGui::SliderFloat("##def", p->Value, p->Min, p->Max);
			else if (w == 1) changed = ImGui::DragFloat("##def", p->Value, 0.01f);
			else if (w == 2) changed = ImGui::DragFloat2("##def", p->Value, 0.01f);
			else if (w == 3) changed = ImGui::DragFloat3("##def", p->Value, 0.01f);
			else changed = ImGui::DragFloat4("##def", p->Value, 0.01f);
			if (changed) { BeginEdit(); d.Touch(); }
			if (ImGui::IsItemDeactivated()) m_EditActive = false;
			if (p->Type == "Float")
			{
				bool range = p->Range;
				if (ImGui::Checkbox("Slider (Range)", &range)) { d.Snapshot(); p->Range = range; d.Touch(); }
				if (p->Range)
				{
					ImGui::SetNextItemWidth(-1);
					float mm[2] = { p->Min, p->Max };
					if (ImGui::DragFloat2("##range", mm, 0.01f)) { BeginEdit(); p->Min = mm[0]; p->Max = mm[1]; d.Touch(); }
					if (ImGui::IsItemDeactivated()) m_EditActive = false;
				}
			}
		}
		else
		{
			char buf[260] = {};
			strncpy_s(buf, p->Texture.c_str(), _TRUNCATE);
			ImGui::TextUnformatted("Default");
			ImGui::SameLine(100.0f);
			ImGui::SetNextItemWidth(-1);
			if (ImGui::InputTextWithHint("##tex", "white", buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue))
			{
				d.Snapshot();
				p->Texture = buf;
				d.Touch();
			}
			if (ImGui::BeginDragDropTarget())
			{
				const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_FILE");
				if (!payload) payload = ImGui::AcceptDragDropPayload("PNG_FILE");
				if (payload)
				{
					std::string dropped(static_cast<const char*>(payload->Data));
					const std::string root = PathManager::GetI()->GetContentPathS();
					if (_strnicmp(dropped.c_str(), root.c_str(), root.size()) == 0) dropped = dropped.substr(root.size());
					d.Snapshot();
					p->Texture = dropped;
					d.Touch();
				}
				ImGui::EndDragDropTarget();
			}
		}
		ImGui::Spacing();
		ImGui::TextWrapped("The default is what a new material starts with; each material can change it in its Inspector.");
	}
	else
		ImGui::TextWrapped("Select a node or a Blackboard property.\n\nRight-click or press Space on the canvas to create a node. Drag from a port to link; drop on empty space to create a linked node.");
	ImGui::EndChild();
	DrawMainPreview(ImGui::GetContentRegionAvail().x);
	ImGui::EndChild();
}

void ShaderGraphWindow::DrawMainPreview(float width)
{
	ImGui::Separator();
	ImGui::Checkbox("Main Preview", &m_ShowMain);
	if (!m_ShowMain || !m_Preview)
		return;
	ImGui::SameLine();
	if (ImGui::SmallButton(m_Preview->Shape == 1 ? "Sphere" : "Cube"))
		m_Preview->Shape = m_Preview->Shape == 1 ? 2 : 1;
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Preview mesh");
	ImGui::SameLine();
	ImGui::Checkbox("Nodes", &m_ShowPreviews);
	const float size = (std::max)(64.0f, (std::min)(width, ImGui::GetContentRegionAvail().y - 4.0f));
	if (ImTextureID tex = m_Preview->MainTexture())
	{
		const ImVec2 p = ImGui::GetCursorScreenPos();
		ImGui::Image(tex, ImVec2(size, size));
		// 끌어 돌리기
		ImGui::SetCursorScreenPos(p);
		ImGui::InvisibleButton("##mainDrag", ImVec2(size, size));
		if (ImGui::IsItemActive())
		{
			const ImVec2 dlt = ImGui::GetIO().MouseDelta;
			m_Preview->Yaw += dlt.x * 0.01f;
			m_Preview->Pitch = std::clamp(m_Preview->Pitch + dlt.y * 0.01f, -1.4f, 1.4f);
		}
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Drag to rotate");
	}
	else
		ImGui::TextDisabled(m_Preview->Compiling() ? "Compiling preview..." : "No preview");
}

void ShaderGraphWindow::OnRender()
{
	Document& d = Doc();
	if (!m_Preview)
		m_Preview = std::make_unique<ShaderGraphPreview>();
	// 값 (Edits) 이나 문서 (Revision: 열기 · Undo · CLI) 가 바뀌면 미리보기를 다시 (같은 코드면 컴파일하지 않는다)
	m_Preview->Update(d.G, (d.Revision << 32) ^ d.Edits, m_ShowPreviews, m_ShowMain);
	DrawMenuBar();
	const ImVec2 avail = ImGui::GetContentRegionAvail();
	const float statusH = ImGui::GetTextLineHeightWithSpacing() + 6.0f;
	const float h = (std::max)(100.0f, avail.y - statusH);
	const float left = 220.0f, right = 280.0f;
	const float mid = (std::max)(200.0f, avail.x - left - right);
	DrawBlackboard(left, h);
	ImGui::SameLine(0, 0);
	DrawCanvas(mid, h);
	ImGui::SameLine(0, 0);
	DrawInspector(right, h);

	// 상태 줄: 저장 결과 · 셰이더 오류
	ImGui::SetCursorPosX(8.0f);
	std::string status = m_Status;
	bool error = m_StatusError;
	if (!d.Asset.empty())
	{
		const std::string name = ShaderNameOf(d.Asset);
		if (IsCompiling(name)) { status = "Compiling " + name + " ..."; error = false; }
		else if (const std::string err = LastError(name); !err.empty()) { status = err; error = true; }
	}
	if (status.empty() && !m_Preview->Error().empty())
	{
		status = "Preview: " + m_Preview->Error();
		error = true;
	}
	if (!status.empty())
	{
		const std::string line = status.substr(0, status.find('\n'));
		ImGui::TextColored(error ? ImVec4(1.0f, 0.45f, 0.4f, 1.0f) : ImVec4(0.6f, 0.85f, 0.6f, 1.0f), "%s", line.c_str());
		if (ImGui::IsItemHovered() && line.size() != status.size()) ImGui::SetTooltip("%s", status.c_str());
	}

	// 단축키 (이 창에 초점이 있고 글자 입력 중이 아닐 때)
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput)
	{
		const bool ctrl = ImGui::GetIO().KeyCtrl;
		if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) Save();
		if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) d.Undo();
		if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) d.Redo();
	}
}

namespace ShaderGraph
{
	void RegisterEditor()
	{
		EditorGUIManager::GetI()->RegisterWindow(new ShaderGraphWindow);

		// Project 창: Create > Shader Graph (.shadergraph), 더블클릭 = 열기
		EditorExtensions::AssetType t;
		t.Owner = "shadergraph";
		t.Extension = kExtension;
		t.Icon = "material_ball";
		t.CreateMenu = "Shader Graph";
		t.DefaultName = "New Shader Graph";
		t.DragPayload = "SHADERGRAPH_FILE";
		t.Create = [](const std::string& path) {
			std::string err;
			NewGraph("Lit").Save(FullPath(path), err);
		};
		t.Open = [](const std::string& path) {
			std::string err;
			if (OpenDoc(path, err)) ShaderGraphWindow::Focus();
			else EditorLog::Write("ShaderGraph", "open %s: %s", path.c_str(), err.c_str());
		};
		t.Inspector = [](const std::string& path) {
			const std::string name = ShaderNameOf(path);
			UnityGUI::ValueLabel("Shader", name.c_str());
			Graph g;
			std::string err;
			if (g.Load(FullPath(path), err))
			{
				UnityGUI::ValueLabel("Material", g.Material.c_str());
				UnityGUI::ValueLabel("Nodes", std::to_string(g.Nodes.size()).c_str());
				UnityGUI::ValueLabel("Properties", std::to_string(g.Properties.size()).c_str());
			}
			const std::string build = LastError(name);
			if (!build.empty()) UnityGUI::HelpBox(build.c_str(), true);
			if (UnityGUI::CenterButton("Open Shader Editor"))
			{
				if (OpenDoc(path, err)) ShaderGraphWindow::Focus();
			}
		};
		EditorExtensions::RegisterAssetType(t);

		// Create > Shader Sub Graph (.shadersubgraph): 노드 묶음을 다른 그래프에서 노드 하나로
		EditorExtensions::AssetType sub = t;
		sub.Extension = kSubExtension;
		sub.CreateMenu = "Shader Sub Graph";
		sub.DefaultName = "New Shader Sub Graph";
		sub.DragPayload = "SHADERSUBGRAPH_FILE";
		sub.Create = [](const std::string& path) {
			Graph g = NewGraph("Lit");
			g.Kind = "SubGraph";
			g.Outputs = { { "Out", "Vector3" } };
			std::string err;
			g.Save(FullPath(path), err);
		};
		sub.Inspector = [](const std::string& path) {
			Graph g;
			std::string err;
			if (g.Load(FullPath(path), err))
			{
				UnityGUI::ValueLabel("Inputs", std::to_string(g.Properties.size()).c_str());
				UnityGUI::ValueLabel("Outputs", std::to_string(g.Outputs.size()).c_str());
				UnityGUI::ValueLabel("Nodes", std::to_string(g.Nodes.size()).c_str());
			}
			if (UnityGUI::CenterButton("Open Shader Editor"))
				if (OpenDoc(path, err)) ShaderGraphWindow::Focus();
		};
		EditorExtensions::RegisterAssetType(sub);

		// CLI: nova shadergraph <op> [경로] [--인자 …]
		CliServer::Register("shadergraph", "shader graph op: {op, ...args} (nova shadergraph help)", [](const nlohmann::json& args, nlohmann::json& result, std::string& error) {
			const std::string op = args.value("op", std::string("help"));
			if (op == "window")
			{
				ShaderGraphWindow::Focus();
				result = Doc().Summary();
				return true;
			}
			nlohmann::json opArgs = args;
			opArgs.erase("op");
			return RunOp(op, opArgs, result, error);
		});
	}
}
