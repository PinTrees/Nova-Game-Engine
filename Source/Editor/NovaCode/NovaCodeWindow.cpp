#include "pch.h"
#include "NovaCodeWindow.h"
#include "CSharpLanguage.h"
#include "ScriptEngine.h"
#include "Debug.h"
#include "UnityGUI.h"
#include "EditorPrefs.h"
#include "EditorGUIManager.h"
#include "PreferencesWindow.h"
#include "imgui_internal.h"
#include <fstream>
#include <shellapi.h>

namespace fs = std::filesystem;

namespace
{
	NovaCodeWindow* s_Instance = nullptr;
	int s_FocusedFrame = -100;

	const ImU32 kPanel      = IM_COL32(37, 37, 38, 255);
	const ImU32 kBar        = IM_COL32(45, 45, 45, 255);
	const ImU32 kRowHover   = IM_COL32(55, 55, 58, 255);
	const ImU32 kRowActive  = IM_COL32(4, 57, 94, 255);
	const ImU32 kText       = IM_COL32(204, 204, 204, 255);
	const ImU32 kDim        = IM_COL32(140, 140, 140, 255);
	const ImU32 kStatus     = IM_COL32(0, 122, 204, 255);
	const ImU32 kStatusErr  = IM_COL32(170, 50, 50, 255);
	constexpr float kToolbarH = 30.0f;
	constexpr float kStatusH = 22.0f;
	constexpr float kRowH = 22.0f;

	const char* kPrefFontSize = "NovaCode.FontSize";
	const char* kPrefExplorer = "NovaCode.ShowExplorer";
	const char* kPrefExplorerW = "NovaCode.ExplorerWidth";

	double Now() { return ImGui::GetTime(); }

	std::wstring Key(const fs::path& p)
	{
		std::wstring s = p.lexically_normal().wstring();
		std::replace(s.begin(), s.end(), L'/', L'\\');
		std::transform(s.begin(), s.end(), s.begin(), ::towlower);
		return s;
	}

	// 끝의 '\' 를 뗀 Assets 폴더 (filename() 이 "Assets" 가 되도록)
	fs::path AssetsDir()
	{
		fs::path p = fs::path(PathManager::GetI()->GetMovePathW(L"Assets\\")).lexically_normal();
		return p.has_filename() ? p : p.parent_path();
	}

	std::string ToUtf8(const fs::path& p) { return wstring_to_string(p.wstring()); }

	// 엔진 API (NovaScriptCore 리플렉션, 한 번 읽어 둔다)
	std::vector<CSharpLanguage::ApiType> s_EngineApi;

	// 도구 모음 버튼 (아이콘 글자)
	bool ToolButton(const char* id, const char* icon, const char* tip, bool enabled = true, bool on = false)
	{
		ImGui::PushStyleColor(ImGuiCol_Button, on ? ImVec4(0.25f, 0.33f, 0.43f, 1.0f) : ImVec4(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.08f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.14f));
		ImGui::PushStyleColor(ImGuiCol_Text, enabled ? ImVec4(0.85f, 0.85f, 0.85f, 1.0f) : ImVec4(0.45f, 0.45f, 0.45f, 1.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
		ImGui::PushID(id);
		const bool pressed = ImGui::Button(icon, ImVec2(26.0f, 22.0f)) && enabled;
		ImGui::PopID();
		ImGui::PopStyleVar();
		ImGui::PopStyleColor(4);
		if (tip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("%s", tip);
		return pressed;
	}

	void Separator()
	{
		ImGui::SameLine(0, 6);
		const ImVec2 p = ImGui::GetCursorScreenPos();
		ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x, p.y + 4), ImVec2(p.x, p.y + 18), IM_COL32(80, 80, 80, 255));
		ImGui::Dummy(ImVec2(1, 22));
		ImGui::SameLine(0, 6);
	}

	// "Assets/Scripts/Player.cs(12,5): error CS1002: ; expected" → 열 5, "error CS1002: ; expected"
	void SplitCompileMessage(const std::string& msg, int& column, std::string& text)
	{
		column = 0;
		text = msg;
		const size_t close = msg.find("): ");
		const size_t open = close == std::string::npos ? std::string::npos : msg.rfind('(', close);
		if (open == std::string::npos)
			return;
		const std::string inside = msg.substr(open + 1, close - open - 1);
		const size_t comma = inside.find(',');
		if (comma != std::string::npos)
			column = atoi(inside.c_str() + comma + 1);
		text = msg.substr(close + 3);
	}
}

// ==================================================================== 생성 / 정적
NovaCodeWindow::NovaCodeWindow()
	: EditorWindow("NOVA Code", ICON_FA_CODE)
{
	s_Instance = this;
	SetIsOpened(false);   // 스크립트를 열 때 나타난다
	m_ShowExplorer = EditorPrefs::GetBool(kPrefExplorer, true);
	m_ExplorerWidth = std::clamp(EditorPrefs::GetFloat(kPrefExplorerW, 210.0f), 120.0f, 600.0f);
}

NovaCodeWindow::~NovaCodeWindow()
{
	if (s_Instance == this)
		s_Instance = nullptr;
}

void NovaCodeWindow::Open(const std::wstring& file, int line)
{
	if (s_Instance == nullptr)
	{
		::ShellExecuteW(nullptr, L"open", file.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
		return;
	}
	NovaCodeWindow& w = *s_Instance;
	Document* doc = w.OpenDocument(fs::path(file));
	if (doc == nullptr)
		return;
	if (line > 0)
		doc->Editor.GoToLine(line);
	w.SetIsOpened(true);
	w.m_FocusRequest = true;
	w.m_FocusEditor = true;
	EditorLog::Write("NovaCode", "open %s:%d", ToUtf8(file).c_str(), line);
}

bool NovaCodeWindow::IsFocused()
{
	return s_FocusedFrame >= ImGui::GetFrameCount() - 1;
}

ImGuiWindowFlags NovaCodeWindow::ExtraWindowFlags() const
{
	return ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
}

void NovaCodeWindow::BeforeBegin()
{
	// 처음 열 때: Scene/Game 과 같은 자리에 탭으로 (Unity 에서 창을 열면 가운데 영역에 붙는 것과 같이)
	ImGui::SetNextWindowSize(ImVec2(1100.0f, 720.0f), ImGuiCond_FirstUseEver);
	if (EditorWindow* scene = EditorGUIManager::GetI()->FindWindow("Scene"))
		if (ImGuiWindow* sw = ImGui::FindWindowByName(scene->GetImGuiName().c_str()))
			if (sw->DockId != 0)
				ImGui::SetNextWindowDockID(sw->DockId, ImGuiCond_FirstUseEver);
	if (m_FocusRequest)
	{
		ImGui::SetNextWindowFocus();
		m_FocusRequest = false;
	}
}

// ==================================================================== 문서
NovaCodeWindow::Document* NovaCodeWindow::OpenDocument(const fs::path& path)
{
	const std::wstring key = Key(path);
	for (int i = 0; i < (int)m_Docs.size(); ++i)
		if (Key(m_Docs[i]->Path) == key)
		{
			m_Docs[i]->SelectTab = true;
			m_ActiveIndex = i;
			return m_Docs[i].get();
		}
	std::ifstream in(path, std::ios::binary);
	if (!in)
	{
		Debug::LogWarning("NOVA Code: cannot open " + ToUtf8(path));
		return nullptr;
	}
	std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	auto doc = std::make_unique<Document>();
	doc->Path = path.lexically_normal();
	doc->Name = ToUtf8(path.filename());
	doc->Bom = text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF;
	doc->Editor.SetText(text);
	std::error_code ec;
	doc->LastWrite = fs::last_write_time(path, ec);
	doc->SelectTab = true;
	m_Docs.push_back(std::move(doc));
	m_ActiveIndex = (int)m_Docs.size() - 1;
	m_MarkerVersion = ~0ull;   // 이 파일의 컴파일 오류 표시

	// Explorer 에서 그 파일이 있는 폴더들을 펼친다
	const std::wstring assetsKey = Key(AssetsDir());
	for (fs::path dir = path.parent_path(); !dir.empty(); dir = dir.parent_path())
	{
		const std::wstring k = Key(dir);
		if (k.size() < assetsKey.size())
			break;
		m_Expanded.insert(k);
		if (dir == dir.parent_path())
			break;
	}
	return m_Docs.back().get();
}

NovaCodeWindow::Document* NovaCodeWindow::Active()
{
	return m_ActiveIndex >= 0 && m_ActiveIndex < (int)m_Docs.size() ? m_Docs[m_ActiveIndex].get() : nullptr;
}

bool NovaCodeWindow::Save(Document& doc)
{
	std::string text = doc.Editor.GetText();
	if (doc.Bom)
		text = "\xEF\xBB\xBF" + text;
	std::ofstream out(doc.Path, std::ios::binary | std::ios::trunc);
	if (!out)
	{
		Debug::LogError("NOVA Code: cannot save " + ToUtf8(doc.Path));
		return false;
	}
	out << text;
	out.close();
	std::error_code ec;
	doc.LastWrite = fs::last_write_time(doc.Path, ec);
	doc.ChangedOnDisk = false;
	doc.Missing = false;
	doc.Editor.MarkSaved();
	ScriptEngine::RequestRecompile();   // 저장하면 바로 다시 컴파일 (Unity 가 포커스를 받을 때 하는 것)
	EditorLog::Write("NovaCode", "saved %s (%d lines)", ToUtf8(doc.Path).c_str(), doc.Editor.LineCount());
	return true;
}

void NovaCodeWindow::SaveAll()
{
	for (auto& d : m_Docs)
		if (d->Editor.IsDirty())
			Save(*d);
}

void NovaCodeWindow::RequestClose(int index)
{
	if (index < 0 || index >= (int)m_Docs.size())
		return;
	if (m_Docs[index]->Editor.IsDirty())
		m_PendingClose = index;   // "저장할까요?"
	else
		CloseNow(index);
}

void NovaCodeWindow::CloseNow(int index)
{
	if (index < 0 || index >= (int)m_Docs.size())
		return;
	m_Docs.erase(m_Docs.begin() + index);
	if (m_ActiveIndex >= (int)m_Docs.size())
		m_ActiveIndex = (int)m_Docs.size() - 1;
	if (Document* a = Active())
		a->SelectTab = true;
}

void NovaCodeWindow::Reload(Document& doc)
{
	std::ifstream in(doc.Path, std::ios::binary);
	if (!in)
		return;
	std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	const int line = doc.Editor.CursorLine(), col = doc.Editor.CursorColumn();
	doc.Bom = text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF;
	doc.Editor.SetText(text);
	doc.Editor.GoToLine(line, col);
	std::error_code ec;
	doc.LastWrite = fs::last_write_time(doc.Path, ec);
	doc.ChangedOnDisk = false;
	m_MarkerVersion = ~0ull;
	EditorLog::Write("NovaCode", "reloaded %s (changed on disk)", ToUtf8(doc.Path).c_str());
}

// ==================================================================== 매 프레임
void NovaCodeWindow::Update()
{
	if (!GetIsOpened())
		return;
	const double now = Now();
	if (now >= m_NextDiskCheck)
	{
		m_NextDiskCheck = now + 1.0;
		CheckFilesOnDisk();
	}
	if (now >= m_NextScan)
	{
		m_NextScan = now + 2.0;
		ScanAssets();
	}
	UpdateMarkers();
	UpdateApi();
}

void NovaCodeWindow::CheckFilesOnDisk()
{
	for (auto& d : m_Docs)
	{
		std::error_code ec;
		if (!fs::exists(d->Path, ec))
		{
			d->Missing = true;
			continue;
		}
		d->Missing = false;
		const auto t = fs::last_write_time(d->Path, ec);
		if (ec || t == d->LastWrite)
			continue;
		if (!d->Editor.IsDirty())
			Reload(*d);
		else
			d->ChangedOnDisk = true;
	}
}

void NovaCodeWindow::UpdateMarkers()
{
	if (Debug::Version() == m_MarkerVersion)
		return;
	m_MarkerVersion = Debug::Version();
	for (auto& d : m_Docs)
	{
		const std::wstring key = Key(d->Path);
		std::vector<CodeEditor::Marker> markers;
		for (const LogEntry& e : Debug::Entries())
		{
			if (!e.Compile || e.File.empty() || e.Line <= 0 || Key(fs::path(string_to_wstring(e.File))) != key)
				continue;
			CodeEditor::Marker m;
			m.Line = e.Line;
			SplitCompileMessage(e.Message, m.Column, m.Message);
			m.Error = e.Type == LogType::Error;
			markers.push_back(std::move(m));
		}
		d->Editor.SetMarkers(std::move(markers));
	}
}

void NovaCodeWindow::UpdateApi()
{
	if (!ScriptEngine::IsAvailable() || ScriptEngine::IsCompiling())
		return;
	if (!m_ApiLoaded)
	{
		m_ApiLoaded = true;
		const std::string text = ScriptEngine::GetApiJson();
		json j = json::parse(text, nullptr, false);
		if (j.is_object() && j.contains("types"))
			for (const json& t : j["types"])
			{
				CSharpLanguage::ApiType type;
				type.Name = t.value("name", std::string());
				if (t.contains("members"))
					for (const json& m : t["members"])
						type.Members.push_back({ m.value("n", std::string()), m.value("t", std::string()) });
				s_EngineApi.push_back(std::move(type));
			}
		EditorLog::Write("NovaCode", "engine API for completion: %d types", (int)s_EngineApi.size());
	}
	// 프로젝트 스크립트 클래스가 바뀌면 (컴파일 후) 다시
	const auto& classes = ScriptEngine::Classes();
	if (classes.size() != m_ApiClassCount)
	{
		m_ApiClassCount = classes.size();
		std::vector<std::string> names;
		for (const auto& c : classes)
			names.push_back(c.Name);
		CSharpLanguage::SetApi(s_EngineApi, names);
	}
}

void NovaCodeWindow::ScanAssets()
{
	std::function<void(Node&)> scan = [&](Node& node) {
		std::error_code ec;
		std::vector<Node> dirs, files;
		for (const auto& e : fs::directory_iterator(node.Path, fs::directory_options::skip_permission_denied, ec))
		{
			Node child;
			child.Path = e.path();
			if (e.is_directory(ec))
			{
				child.IsDir = true;
				scan(child);
				dirs.push_back(std::move(child));
			}
			else if (_wcsicmp(e.path().extension().c_str(), L".cs") == 0)
				files.push_back(std::move(child));
		}
		auto byName = [](const Node& a, const Node& b) { return _wcsicmp(a.Path.filename().c_str(), b.Path.filename().c_str()) < 0; };
		std::sort(dirs.begin(), dirs.end(), byName);
		std::sort(files.begin(), files.end(), byName);
		node.Children = std::move(dirs);
		for (Node& f : files)
			node.Children.push_back(std::move(f));
	};
	m_Root = Node();
	m_Root.Path = AssetsDir();
	m_Root.IsDir = true;
	std::error_code ec;
	if (fs::exists(m_Root.Path, ec))
		scan(m_Root);
	if (m_Expanded.empty())
		m_Expanded.insert(Key(m_Root.Path));
}

// ==================================================================== 단축키
void NovaCodeWindow::HandleShortcuts()
{
	ImGuiIO& io = ImGui::GetIO();
	if (!io.KeyCtrl || io.KeyAlt)
		return;
	Document* doc = Active();
	if (ImGui::IsKeyPressed(ImGuiKey_S, false))
	{
		if (io.KeyShift) SaveAll();
		else if (doc) Save(*doc);
	}
	if (ImGui::IsKeyPressed(ImGuiKey_W, false) && doc)
		RequestClose(m_ActiveIndex);
	if (ImGui::IsKeyPressed(ImGuiKey_B, false))
	{
		m_ShowExplorer = !m_ShowExplorer;
		EditorPrefs::SetBool(kPrefExplorer, m_ShowExplorer);
	}
	auto zoom = [&](float d) {
		m_FontSize = std::clamp(m_FontSize + d, 10.0f, 32.0f);
		EditorPrefs::SetFloat(kPrefFontSize, m_FontSize);
	};
	if (ImGui::IsKeyPressed(ImGuiKey_Equal) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd)) zoom(1.0f);
	if (ImGui::IsKeyPressed(ImGuiKey_Minus) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract)) zoom(-1.0f);
	if (io.MouseWheel != 0.0f && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
		zoom(io.MouseWheel > 0 ? 1.0f : -1.0f);
}

// ==================================================================== 그리기
void NovaCodeWindow::OnRender()
{
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
	{
		s_FocusedFrame = ImGui::GetFrameCount();
		HandleShortcuts();
	}
	if (m_Root.Path.empty())
		ScanAssets();
	// Preferences > NOVA Code 설정 (바꾸면 바로 반영)
	m_FontSize = std::clamp(EditorPrefs::GetFloat(kPrefFontSize, 16.0f), 10.0f, 32.0f);
	if (Document* d = Active())
	{
		d->Editor.TabSize = EditorPrefs::GetInt("NovaCode.TabSize", 4);
		d->Editor.AutoComplete = EditorPrefs::GetBool("NovaCode.AutoComplete", true);
		d->Editor.AutoCloseBrackets = EditorPrefs::GetBool("NovaCode.AutoCloseBrackets", true);
	}

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));
	DrawToolbar();

	const ImVec2 origin = ImGui::GetCursorScreenPos();
	const ImVec2 avail = ImGui::GetContentRegionAvail();
	const float bodyH = (std::max)(40.0f, avail.y - kStatusH);

	// ---- Explorer | 편집 영역
	float editorX = origin.x;
	if (m_ShowExplorer)
	{
		const float w = (std::min)(m_ExplorerWidth, avail.x * 0.6f);
		ImGui::SetCursorScreenPos(origin);
		DrawExplorer(w, bodyH);
		// 경계를 끌어 너비 조절
		ImGui::SetCursorScreenPos(ImVec2(origin.x + w - 2.0f, origin.y));
		ImGui::InvisibleButton("##explorerSplit", ImVec2(5.0f, bodyH));
		if (ImGui::IsItemHovered() || ImGui::IsItemActive())
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
		if (ImGui::IsItemActive())
			m_ExplorerWidth = std::clamp(m_ExplorerWidth + ImGui::GetIO().MouseDelta.x, 120.0f, 600.0f);
		if (ImGui::IsItemDeactivated())
			EditorPrefs::SetFloat(kPrefExplorerW, m_ExplorerWidth);
		ImGui::GetWindowDrawList()->AddLine(ImVec2(origin.x + w, origin.y), ImVec2(origin.x + w, origin.y + bodyH), IM_COL32(25, 25, 25, 255));
		editorX = origin.x + w + 1.0f;
	}

	ImGui::SetCursorScreenPos(ImVec2(editorX, origin.y));
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.118f, 0.118f, 0.118f, 1.0f));
	ImGui::BeginChild("##ncBody", ImVec2(origin.x + avail.x - editorX, bodyH), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	DrawTabs();
	Document* doc = Active();
	if (doc)
	{
		if (doc->Missing || doc->ChangedOnDisk)
		{
			// 경고 줄
			const ImVec2 p = ImGui::GetCursorScreenPos();
			const float w = ImGui::GetContentRegionAvail().x;
			ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + w, p.y + 28.0f), IM_COL32(90, 72, 20, 255));
			ImGui::SetCursorScreenPos(ImVec2(p.x + 8.0f, p.y + 4.0f));
			ImGui::AlignTextToFramePadding();
			if (doc->Missing)
				ImGui::TextUnformatted(ICON_FA_TRIANGLE_EXCLAMATION "  This file was deleted on disk. Save to create it again.");
			else
			{
				ImGui::TextUnformatted(ICON_FA_TRIANGLE_EXCLAMATION "  The file was changed outside NOVA Code.");
				ImGui::SameLine();
				if (ImGui::SmallButton("Reload")) Reload(*doc);
				ImGui::SameLine();
				if (ImGui::SmallButton("Keep Mine")) { std::error_code ec; doc->LastWrite = fs::last_write_time(doc->Path, ec); doc->ChangedOnDisk = false; }
			}
			ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + 28.0f));
		}
		if (m_FocusEditor)
		{
			doc->Editor.Focus();
			m_FocusEditor = false;
		}
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
		doc->Editor.Render("##code", m_FontSize);
		ImGui::PopStyleVar();
	}
	else
	{
		// 빈 화면: 안내
		const ImVec2 p = ImGui::GetWindowPos();
		const ImVec2 s = ImGui::GetWindowSize();
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const char* lines[] = {
			"NOVA Code",
			"Open a script from the Explorer, or double-click a .cs file in the Project window.",
			"",
			"Save                 Ctrl+S          Find            Ctrl+F",
			"Save All             Ctrl+Shift+S    Replace         Ctrl+H",
			"Close Tab            Ctrl+W          Go to Line      Ctrl+G",
			"Toggle Explorer      Ctrl+B          Comment         Ctrl+/",
			"Zoom                 Ctrl+Wheel      Complete        Ctrl+Space" };
		ImFont* mono = ImGui::GetIO().Fonts->Fonts.Size > 2 ? ImGui::GetIO().Fonts->Fonts[2] : ImGui::GetFont();
		float y = p.y + s.y * 0.32f;
		for (int i = 0; i < IM_ARRAYSIZE(lines); ++i)
		{
			ImFont* f = i == 0 ? UnityGUI::BoldFont() : (i >= 3 ? mono : ImGui::GetFont());
			const float size = i == 0 ? 26.0f : (i >= 3 ? 14.0f : ImGui::GetFontSize());
			const ImVec2 ts = f->CalcTextSizeA(size, FLT_MAX, 0.0f, lines[i]);
			dl->AddText(f, size, ImVec2(floorf(p.x + (s.x - ts.x) * 0.5f), y), i == 0 ? IM_COL32(180, 180, 180, 255) : kDim, lines[i]);
			y += size + (i == 0 ? 14.0f : 6.0f);
		}
	}
	ImGui::EndChild();
	ImGui::PopStyleColor();

	ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + bodyH));
	DrawStatusBar();
	ImGui::PopStyleVar();

	DrawClosePrompt();
}

void NovaCodeWindow::DrawToolbar()
{
	const ImVec2 p = ImGui::GetCursorScreenPos();
	const float w = ImGui::GetContentRegionAvail().x;
	ImDrawList* dl = ImGui::GetWindowDrawList();
	dl->AddRectFilled(p, ImVec2(p.x + w, p.y + kToolbarH), kBar);
	dl->AddLine(ImVec2(p.x, p.y + kToolbarH - 1), ImVec2(p.x + w, p.y + kToolbarH - 1), IM_COL32(25, 25, 25, 255));
	ImGui::SetCursorScreenPos(ImVec2(p.x + 6.0f, p.y + 4.0f));

	Document* doc = Active();
	if (ToolButton("explorer", ICON_FA_FOLDER_TREE, "Toggle Explorer (Ctrl+B)", true, m_ShowExplorer))
	{
		m_ShowExplorer = !m_ShowExplorer;
		EditorPrefs::SetBool(kPrefExplorer, m_ShowExplorer);
	}
	Separator();
	if (ToolButton("save", ICON_FA_FLOPPY_DISK, "Save (Ctrl+S)", doc && doc->Editor.IsDirty()) && doc) Save(*doc);
	ImGui::SameLine();
	bool anyDirty = false;
	for (auto& d : m_Docs) anyDirty |= d->Editor.IsDirty();
	if (ToolButton("saveAll", ICON_FA_COPY, "Save All (Ctrl+Shift+S)", anyDirty)) SaveAll();
	Separator();
	if (ToolButton("undo", ICON_FA_ROTATE_LEFT, "Undo (Ctrl+Z)", doc && doc->Editor.CanUndo()) && doc) doc->Editor.Undo();
	ImGui::SameLine();
	if (ToolButton("redo", ICON_FA_ROTATE_RIGHT, "Redo (Ctrl+Y)", doc && doc->Editor.CanRedo()) && doc) doc->Editor.Redo();
	Separator();
	if (ToolButton("find", ICON_FA_MAGNIFYING_GLASS, "Find (Ctrl+F)", doc != nullptr) && doc) { doc->Editor.OpenFind(false); }
	ImGui::SameLine();
	if (ToolButton("replace", ICON_FA_RIGHT_LEFT, "Replace (Ctrl+H)", doc != nullptr) && doc) { doc->Editor.OpenFind(true); }
	ImGui::SameLine();
	if (ToolButton("goto", ICON_FA_ARROW_DOWN_1_9, "Go to Line (Ctrl+G)", doc != nullptr) && doc) { doc->Editor.OpenGoToLine(); }

	// 오른쪽: 컴파일 상태 + 설정
	std::string status;
	ImU32 statusCol = kDim;
	if (!ScriptEngine::IsAvailable())
		status = ICON_FA_CIRCLE_XMARK "  C# scripting unavailable";
	else if (ScriptEngine::IsCompiling())
	{
		status = ICON_FA_SPINNER "  " + ScriptEngine::StatusText();
		statusCol = IM_COL32(220, 200, 120, 255);
	}
	else if (ScriptEngine::HasCompileErrors())
	{
		status = ICON_FA_CIRCLE_XMARK "  Compile errors";
		statusCol = IM_COL32(244, 110, 110, 255);
	}
	else
	{
		status = ICON_FA_CIRCLE_CHECK "  Scripts compiled";
		statusCol = IM_COL32(120, 200, 140, 255);
	}
	const float sw = ImGui::CalcTextSize(status.c_str()).x;
	const float gearX = p.x + w - 34.0f;
	dl->AddText(ImVec2(gearX - sw - 12.0f, p.y + (kToolbarH - ImGui::GetFontSize()) * 0.5f), statusCol, status.c_str());
	ImGui::SetCursorScreenPos(ImVec2(gearX, p.y + 4.0f));
	if (ToolButton("settings", ICON_FA_GEAR, "Settings"))
		ImGui::OpenPopup("##ncSettings");
	if (ImGui::BeginPopup("##ncSettings"))
	{
		ImGui::TextDisabled("NOVA Code");
		ImGui::SetNextItemWidth(160);
		if (ImGui::SliderFloat("Font Size", &m_FontSize, 10.0f, 32.0f, "%.0f"))
			EditorPrefs::SetFloat(kPrefFontSize, m_FontSize = floorf(m_FontSize + 0.5f));
		ImGui::Separator();
		if (ImGui::MenuItem("External Script Editor..."))
			PreferencesWindow::Open("External Tools");
		if (ImGui::MenuItem("Regenerate Project Files"))
			ScriptEngine::RegenerateProjectFiles();
		if (ImGui::MenuItem("Recompile Scripts"))
			ScriptEngine::RequestRecompile();
		ImGui::EndPopup();
	}
	ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + kToolbarH));
}

void NovaCodeWindow::DrawExplorer(float width, float height)
{
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(kPanel));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::BeginChild("##ncExplorer", ImVec2(width, height), ImGuiChildFlags_None);
	ImDrawList* dl = ImGui::GetWindowDrawList();

	// 머리글: EXPLORER  [+]
	const ImVec2 p = ImGui::GetCursorScreenPos();
	dl->AddText(ImVec2(p.x + 12.0f, p.y + 6.0f), kDim, "EXPLORER");
	ImGui::SetCursorScreenPos(ImVec2(p.x + width - 32.0f, p.y + 2.0f));
	if (ToolButton("newScript", ICON_FA_PLUS, "New C# Script"))
	{
		m_NewScriptDir = AssetsDir();
		strcpy_s(m_NewScriptName, "NewMonoBehaviourScript");
		ImGui::OpenPopup("##ncNewScript");
	}
	ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + 28.0f));

	if (!m_Root.Path.empty())
		DrawExplorerFolder(m_Root.Path, 0);

	// 새 스크립트 이름
	if (m_OpenNewScript)
	{
		m_OpenNewScript = false;
		ImGui::OpenPopup("##ncNewScript");
	}
	if (ImGui::BeginPopup("##ncNewScript"))
	{
		ImGui::TextDisabled("New C# Script in %s", ToUtf8(m_NewScriptDir.filename()).c_str());
		ImGui::SetNextItemWidth(220);
		if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
		const bool enter = ImGui::InputText("##name", m_NewScriptName, sizeof(m_NewScriptName), ImGuiInputTextFlags_EnterReturnsTrue);
		ImGui::SameLine();
		if ((ImGui::Button("Create") || enter) && m_NewScriptName[0])
		{
			const std::wstring file = ScriptEngine::CreateScriptAsset(m_NewScriptDir.wstring(), m_NewScriptName);
			ImGui::CloseCurrentPopup();
			if (!file.empty())
			{
				ScanAssets();
				Open(file, 8);
				if (Document* d = Active())
					d->Editor.GoToLine(8, 9);   // 템플릿의 Start() 본문 들여쓰기 끝
			}
		}
		ImGui::EndPopup();
	}
	ImGui::EndChild();
	ImGui::PopStyleVar();
	ImGui::PopStyleColor();
}

void NovaCodeWindow::DrawExplorerFolder(const fs::path& dir, int depth)
{
	// m_Root 에서 dir 노드 찾기 (트리는 2초마다 새로 만든다)
	std::function<const Node*(const Node&)> find = [&](const Node& n) -> const Node* {
		if (n.Path == dir) return &n;
		for (const Node& c : n.Children)
			if (c.IsDir)
				if (const Node* r = find(c)) return r;
		return nullptr;
	};
	const Node* node = find(m_Root);
	if (node == nullptr)
		return;

	ImDrawList* dl = ImGui::GetWindowDrawList();
	const float w = ImGui::GetContentRegionAvail().x;
	const Document* active = Active();
	const std::wstring activeKey = active ? Key(active->Path) : std::wstring();

	auto row = [&](const Node& n, int d) {
		const std::wstring key = Key(n.Path);
		const bool expanded = n.IsDir && m_Expanded.count(key) > 0;
		const ImVec2 p = ImGui::GetCursorScreenPos();
		ImGui::PushID(key.c_str());
		ImGui::InvisibleButton("##row", ImVec2((std::max)(1.0f, w), kRowH));
		const bool hovered = ImGui::IsItemHovered();
		if (!n.IsDir && key == activeKey)
			dl->AddRectFilled(p, ImVec2(p.x + w, p.y + kRowH), kRowActive);
		else if (hovered)
			dl->AddRectFilled(p, ImVec2(p.x + w, p.y + kRowH), kRowHover);
		float x = p.x + 8.0f + d * 14.0f;
		const float cy = p.y + kRowH * 0.5f;
		if (n.IsDir)
		{
			ImGui::RenderArrow(dl, ImVec2(x, cy - 5.0f), kDim, expanded ? ImGuiDir_Down : ImGuiDir_Right, 0.7f);
			x += 14.0f;
		}
		else
			x += 14.0f;
		UnityGUI::DrawIcon(dl, n.IsDir ? (expanded ? "folder_open" : "folder") : "script_cs", ImVec2(x, cy - 8.0f), 16.0f);
		const std::string name = ToUtf8(n.Path.filename());
		const Document* open = nullptr;
		for (auto& doc : m_Docs) if (Key(doc->Path) == key) open = doc.get();
		dl->AddText(ImVec2(x + 21.0f, cy - ImGui::GetFontSize() * 0.5f), kText, name.c_str());
		if (open && open->Editor.IsDirty())
			dl->AddCircleFilled(ImVec2(p.x + w - 12.0f, cy), 3.5f, kText);   // 저장 안 함

		if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
		{
			if (n.IsDir)
			{
				if (expanded) m_Expanded.erase(key);
				else m_Expanded.insert(key);
			}
			else
				Open(n.Path.wstring(), 0);
		}
		if (ImGui::BeginPopupContextItem("##rowMenu"))
		{
			if (n.IsDir)
			{
				if (ImGui::MenuItem("New C# Script..."))
				{
					m_NewScriptDir = n.Path;
					strcpy_s(m_NewScriptName, "NewMonoBehaviourScript");
					m_Expanded.insert(key);
					m_OpenNewScript = true;   // 이 메뉴가 닫힌 뒤 Explorer 에서 연다
				}
			}
			else if (ImGui::MenuItem("Open"))
				Open(n.Path.wstring(), 0);
			if (ImGui::MenuItem("Reveal in File Explorer"))
			{
				const std::wstring args = L"/select,\"" + n.Path.wstring() + L"\"";
				::ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
			}
			ImGui::EndPopup();
		}
		if (hovered && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
			ImGui::SetTooltip("%s", ToUtf8(n.Path).c_str());
		ImGui::PopID();
		return expanded;
	};

	std::function<void(const Node&, int)> draw = [&](const Node& n, int d) {
		if (!row(n, d))
			return;
		for (const Node& c : n.Children)
		{
			if (c.IsDir) draw(c, d + 1);
			else row(c, d + 1);
		}
	};
	draw(*node, depth);
}

void NovaCodeWindow::DrawTabs()
{
	if (m_Docs.empty())
		return;
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12, 6));
	ImGui::PushStyleVar(ImGuiStyleVar_TabRounding, 0.0f);
	ImGui::PushStyleColor(ImGuiCol_Tab, ImVec4(0.176f, 0.176f, 0.176f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_TabHovered, ImVec4(0.22f, 0.22f, 0.22f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_TabSelected, ImVec4(0.118f, 0.118f, 0.118f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_TabSelectedOverline, ImVec4(0.0f, 0.48f, 0.8f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_TabDimmed, ImVec4(0.176f, 0.176f, 0.176f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_TabDimmedSelected, ImVec4(0.118f, 0.118f, 0.118f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_TabDimmedSelectedOverline, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
	const ImVec2 p = ImGui::GetCursorScreenPos();
	ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + ImGui::GetContentRegionAvail().x, p.y + ImGui::GetFrameHeight()), IM_COL32(37, 37, 38, 255));
	if (ImGui::BeginTabBar("##ncTabs", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_FittingPolicyScroll | ImGuiTabBarFlags_TabListPopupButton))
	{
		int closeIndex = -1;
		for (int i = 0; i < (int)m_Docs.size(); ++i)
		{
			Document& d = *m_Docs[i];
			ImGuiTabItemFlags flags = d.Editor.IsDirty() ? ImGuiTabItemFlags_UnsavedDocument : 0;
			if (d.SelectTab)
			{
				flags |= ImGuiTabItemFlags_SetSelected;
				d.SelectTab = false;
			}
			bool open = true;
			const std::string label = "  " + d.Name + "##" + ToUtf8(d.Path);
			if (ImGui::BeginTabItem(label.c_str(), &open, flags))
			{
				if (m_ActiveIndex != i)
					m_FocusEditor = true;
				m_ActiveIndex = i;
				ImGui::EndTabItem();
			}
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
				ImGui::SetTooltip("%s", ToUtf8(d.Path).c_str());
			if (ImGui::IsItemClicked(ImGuiMouseButton_Middle))
				open = false;
			if (!open)
				closeIndex = i;
		}
		ImGui::EndTabBar();
		if (closeIndex >= 0)
			RequestClose(closeIndex);
	}
	ImGui::PopStyleColor(7);
	ImGui::PopStyleVar(2);
}

void NovaCodeWindow::DrawStatusBar()
{
	const ImVec2 p = ImGui::GetCursorScreenPos();
	const float w = ImGui::GetContentRegionAvail().x;
	ImDrawList* dl = ImGui::GetWindowDrawList();
	const bool errors = ScriptEngine::HasCompileErrors();
	dl->AddRectFilled(p, ImVec2(p.x + w, p.y + kStatusH), errors ? kStatusErr : kStatus);
	const float ty = p.y + (kStatusH - ImGui::GetFontSize()) * 0.5f;
	const ImU32 white = IM_COL32(255, 255, 255, 235);

	// 왼쪽: ⛔ 오류  ⚠ 경고 (컴파일) — 누르면 첫 오류로
	int nErr = 0, nWarn = 0;
	const LogEntry* first = nullptr;
	for (const LogEntry& e : Debug::Entries())
		if (e.Compile)
		{
			(e.Type == LogType::Error ? nErr : nWarn)++;
			if (!first && e.Type == LogType::Error && !e.File.empty()) first = &e;
		}
	char left[96];
	snprintf(left, sizeof(left), ICON_FA_CIRCLE_XMARK " %d   " ICON_FA_TRIANGLE_EXCLAMATION " %d", nErr, nWarn);
	const float lw = ImGui::CalcTextSize(left).x + 20.0f;
	ImGui::SetCursorScreenPos(p);
	if (ImGui::InvisibleButton("##ncProblems", ImVec2(lw, kStatusH)) && first)
		Open(string_to_wstring(first->File), first->Line);
	if (ImGui::IsItemHovered())
	{
		dl->AddRectFilled(p, ImVec2(p.x + lw, p.y + kStatusH), IM_COL32(255, 255, 255, 30));
		ImGui::SetTooltip(first ? "Go to the first compile error" : "No compile errors");
	}
	dl->AddText(ImVec2(p.x + 10.0f, ty), white, left);
	if (ScriptEngine::IsCompiling())
		dl->AddText(ImVec2(p.x + lw + 8.0f, ty), white, ScriptEngine::StatusText().c_str());

	// 오른쪽: Ln, Col / Spaces / UTF-8 / CRLF / C#
	if (const Document* doc = Active())
	{
		char right[160];
		snprintf(right, sizeof(right), "Ln %d, Col %d     Spaces: %d     UTF-8%s     %s     C#",
			doc->Editor.CursorLine(), doc->Editor.CursorColumn(), doc->Editor.TabSize, doc->Bom ? " with BOM" : "", doc->Editor.UsesCRLF() ? "CRLF" : "LF");
		const float rw = ImGui::CalcTextSize(right).x;
		dl->AddText(ImVec2(p.x + w - rw - 12.0f, ty), white, right);
	}
	ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + kStatusH));
}

void NovaCodeWindow::DrawClosePrompt()
{
	if (m_PendingClose >= 0 && !ImGui::IsPopupOpen("Save Changes?"))
		ImGui::OpenPopup("Save Changes?");
	ImGui::SetNextWindowSize(ImVec2(420, 0));
	if (ImGui::BeginPopupModal("Save Changes?", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings))
	{
		if (m_PendingClose < 0 || m_PendingClose >= (int)m_Docs.size())
		{
			m_PendingClose = -1;
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			return;
		}
		Document& d = *m_Docs[m_PendingClose];
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));
		ImGui::TextWrapped("Do you want to save the changes you made to %s?", d.Name.c_str());
		ImGui::TextDisabled("Your changes will be lost if you don't save them.");
		ImGui::Spacing();
		if (ImGui::Button("Save", ImVec2(110, 0)))
		{
			if (Save(d)) CloseNow(m_PendingClose);
			m_PendingClose = -1;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Don't Save", ImVec2(110, 0)))
		{
			CloseNow(m_PendingClose);
			m_PendingClose = -1;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(110, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
		{
			m_PendingClose = -1;
			ImGui::CloseCurrentPopup();
		}
		ImGui::PopStyleVar();
		ImGui::EndPopup();
	}
}
