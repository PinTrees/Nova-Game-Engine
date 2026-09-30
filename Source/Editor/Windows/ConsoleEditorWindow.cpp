#include "pch.h"
#include "ConsoleEditorWindow.h"
#include "Debug.h"
#include "UnityGUI.h"
#include "ScriptEngine.h"

namespace
{
	const ImU32 kBar = IM_COL32(40, 40, 40, 255);
	const ImU32 kRowA = IM_COL32(56, 56, 56, 255);
	const ImU32 kRowB = IM_COL32(60, 60, 60, 255);
	const ImU32 kSel = IM_COL32(44, 93, 135, 255);
	const ImU32 kText = IM_COL32(210, 210, 210, 255);
	const ImU32 kDim = IM_COL32(140, 140, 140, 255);
	const ImU32 kOn = IM_COL32(70, 96, 124, 255);
	constexpr float kBarH = 21.0f;
	constexpr float kRowH = 34.0f;   // Unity: 두 줄 (메시지 + 스택 첫 줄)

	const char* IconOf(LogType t) { return t == LogType::Error ? "console_error" : (t == LogType::Warning ? "warning" : "info"); }

	std::string FirstLine(const std::string& s)
	{
		const size_t n = s.find('\n');
		return n == std::string::npos ? s : s.substr(0, n);
	}

	std::string Lower(std::string s) { std::transform(s.begin(), s.end(), s.begin(), ::tolower); return s; }

	struct Row { int Index; int Count; };

	bool ToolToggle(ImDrawList* dl, const char* id, const char* text, const char* icon, float& x, float y, bool on, const char* tip = nullptr)
	{
		const float w = (icon ? 20.0f : 0.0f) + (text ? ImGui::CalcTextSize(text).x + 12.0f : 4.0f);
		ImGui::SetCursorScreenPos(ImVec2(x, y));
		const bool clicked = ImGui::InvisibleButton(id, ImVec2(w, kBarH));
		const bool hovered = ImGui::IsItemHovered();
		if (tip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
			ImGui::SetTooltip("%s", tip);
		dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + kBarH), on ? kOn : (hovered ? IM_COL32(64, 64, 64, 255) : kBar));
		float tx = x + 6.0f;
		if (icon)
		{
			UnityGUI::DrawIcon(dl, icon, ImVec2(x + 4.0f, y + (kBarH - 14.0f) * 0.5f), 14.0f);
			tx = x + 21.0f;
		}
		if (text)
			dl->AddText(ImVec2(tx, floorf(y + (kBarH - ImGui::GetFontSize()) * 0.5f)), kText, text);
		x += w + 1.0f;
		return clicked;
	}

	void OpenEntry(const LogEntry& e)
	{
		if (!e.File.empty())
			ScriptEngine::OpenInCodeEditor(string_to_wstring(e.File), e.Line);
	}
}

ConsoleEditorWindow::ConsoleEditorWindow()
	: EditorWindow("Console", ICON_FA_FILE_LINES)
{
}

ConsoleEditorWindow::~ConsoleEditorWindow()
{
}

void ConsoleEditorWindow::PushStyle()
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
}

void ConsoleEditorWindow::PopStyle()
{
    ImGui::PopStyleVar();
}

void ConsoleEditorWindow::OnRender()
{
	const ImVec2 p = ImGui::GetCursorScreenPos();
	const ImVec2 avail = ImGui::GetContentRegionAvail();
	if (avail.x < 60.0f || avail.y < 50.0f)
		return;
	ImDrawList* dl = ImGui::GetWindowDrawList();
	const auto& entries = Debug::Entries();

	// ---------------- 툴바
	dl->AddRectFilled(p, ImVec2(p.x + avail.x, p.y + kBarH), kBar);
	float x = p.x;
	if (ToolToggle(dl, "##clear", "Clear", nullptr, x, p.y, false))
	{
		Debug::ClearAll();
		m_Selected = -1;
	}
	if (ToolToggle(dl, "##collapse", "Collapse", nullptr, x, p.y, Debug::Collapse)) Debug::Collapse = !Debug::Collapse;
	if (ToolToggle(dl, "##clearOnPlay", "Clear on Play", nullptr, x, p.y, Debug::ClearOnPlay)) Debug::ClearOnPlay = !Debug::ClearOnPlay;
	if (ToolToggle(dl, "##errorPause", "Error Pause", nullptr, x, p.y, Debug::ErrorPause, "Pause Play mode when an error is logged")) Debug::ErrorPause = !Debug::ErrorPause;
	if (ToolToggle(dl, "##editorLog", "Open Editor Log", nullptr, x, p.y, false, "Logs/Editor.log (editor-only diagnostics)")) EditorLog::OpenInEditor();

	// 오른쪽: 개수 토글 (Unity: ⓘ 12  ⚠ 3  ⛔ 1)
	const std::string nLog = std::to_string(Debug::Count(LogType::Log));
	const std::string nWarn = std::to_string(Debug::Count(LogType::Warning));
	const std::string nErr = std::to_string(Debug::Count(LogType::Error));
	float rx = p.x + avail.x - (60.0f + ImGui::CalcTextSize(nLog.c_str()).x + ImGui::CalcTextSize(nWarn.c_str()).x + ImGui::CalcTextSize(nErr.c_str()).x + 36.0f + 3.0f);
	const float countsX = rx;
	if (ToolToggle(dl, "##showLog", nLog.c_str(), "info", rx, p.y, m_ShowLog)) m_ShowLog = !m_ShowLog;
	if (ToolToggle(dl, "##showWarn", nWarn.c_str(), "warning", rx, p.y, m_ShowWarning)) m_ShowWarning = !m_ShowWarning;
	if (ToolToggle(dl, "##showErr", nErr.c_str(), "console_error", rx, p.y, m_ShowError)) m_ShowError = !m_ShowError;
	// 검색
	{
		const float sw = (std::min)(220.0f, countsX - x - 12.0f);
		if (sw > 60.0f)
		{
			ImGui::SetCursorScreenPos(ImVec2(countsX - sw - 6.0f, p.y + 2.0f));
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(18, 1));
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
			ImGui::SetNextItemWidth(sw);
			ImGui::InputText("##consoleSearch", m_Search, sizeof(m_Search));
			ImGui::PopStyleVar(2);
			UnityGUI::DrawIcon(dl, "search", ImVec2(countsX - sw - 3.0f, p.y + 4.0f), 13.0f);
		}
	}

	// ---------------- 보이는 행 (필터 + 묶기)
	const std::string needle = Lower(m_Search);
	std::vector<Row> rows;
	std::unordered_map<std::string, size_t> collapseIndex;
	for (int i = 0; i < (int)entries.size(); ++i)
	{
		const LogEntry& e = entries[i];
		if ((e.Type == LogType::Log && !m_ShowLog) || (e.Type == LogType::Warning && !m_ShowWarning) || (e.Type == LogType::Error && !m_ShowError))
			continue;
		if (!needle.empty() && Lower(e.Message).find(needle) == std::string::npos)
			continue;
		if (Debug::Collapse)
		{
			const std::string key = std::to_string((int)e.Type) + e.Message + e.StackTrace;
			auto it = collapseIndex.find(key);
			if (it != collapseIndex.end()) { rows[it->second].Count++; continue; }
			collapseIndex[key] = rows.size();
		}
		rows.push_back({ i, 1 });
	}
	if (m_Selected >= (int)rows.size())
		m_Selected = -1;

	// ---------------- 목록
	const float listY = p.y + kBarH;
	const float detailH = std::clamp(m_DetailHeight, 40.0f, (std::max)(40.0f, avail.y - kBarH - 40.0f));
	const float listH = avail.y - kBarH - detailH - 4.0f;
	ImGui::SetCursorScreenPos(ImVec2(p.x, listY));
	ImGui::BeginChild("##consoleList", ImVec2(avail.x, (std::max)(20.0f, listH)), false);
	{
		ImDrawList* ldl = ImGui::GetWindowDrawList();
		const ImVec2 o = ImGui::GetCursorScreenPos();
		const float w = ImGui::GetContentRegionAvail().x;
		ImGuiListClipper clipper;
		clipper.Begin((int)rows.size(), kRowH);
		while (clipper.Step())
			for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r)
			{
				const LogEntry& e = entries[rows[r].Index];
				const float y = o.y + r * kRowH;
				ImGui::SetCursorScreenPos(ImVec2(o.x, y));
				ImGui::PushID(r);
				if (ImGui::InvisibleButton("##row", ImVec2(w, kRowH)))
					m_Selected = r;
				if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
					OpenEntry(e);
				ImGui::PopID();
				ldl->AddRectFilled(ImVec2(o.x, y), ImVec2(o.x + w, y + kRowH), m_Selected == r ? kSel : (r % 2 ? kRowB : kRowA));
				UnityGUI::DrawIcon(ldl, IconOf(e.Type), ImVec2(o.x + 6.0f, y + 4.0f), 24.0f);
				const std::string line1 = e.Time + " " + FirstLine(e.Message);
				std::string line2 = FirstLine(e.StackTrace);
				if (line2.empty() && !e.File.empty())
				{
					// 절대 경로 → Assets/... (Unity 처럼 짧게)
					std::string rel = e.File;
					const size_t a = rel.find("Assets\\");
					if (a != std::string::npos) rel = rel.substr(a);
					std::replace(rel.begin(), rel.end(), '\\', '/');
					line2 = rel + ":" + std::to_string(e.Line);
				}
				ImVec4 clip(o.x, y, o.x + w - (rows[r].Count > 1 ? 44.0f : 4.0f), y + kRowH);
				ldl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(o.x + 38.0f, y + 2.0f), kText, line1.c_str(), nullptr, 0.0f, &clip);
				ldl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(o.x + 38.0f, y + 17.0f), kDim, line2.c_str(), nullptr, 0.0f, &clip);
				if (rows[r].Count > 1)
				{
					const std::string c = std::to_string(rows[r].Count);
					const float cw = ImGui::CalcTextSize(c.c_str()).x + 12.0f;
					const ImVec2 b0(o.x + w - cw - 8.0f, y + 9.0f);
					ldl->AddRectFilled(b0, ImVec2(b0.x + cw, b0.y + 16.0f), IM_COL32(100, 100, 100, 255), 8.0f);
					ldl->AddText(ImVec2(b0.x + 6.0f, b0.y + 1.0f), kText, c.c_str());
				}
			}
		ImGui::SetCursorScreenPos(ImVec2(o.x, o.y + rows.size() * kRowH));
		ImGui::Dummy(ImVec2(1, 1));
		// 새 로그가 오면 맨 아래로 (사용자가 위로 올려 보고 있으면 그대로)
		if (Debug::Version() != m_SeenVersion)
		{
			m_SeenVersion = Debug::Version();
			if (m_AutoScroll)
				ImGui::SetScrollHereY(1.0f);
		}
		m_AutoScroll = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 2.0f;
		// 키보드: 위/아래
		if (ImGui::IsWindowFocused() && !rows.empty())
		{
			if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) m_Selected = (std::min)((int)rows.size() - 1, m_Selected + 1);
			if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) m_Selected = (std::max)(0, m_Selected - 1);
			if (ImGui::IsKeyPressed(ImGuiKey_Enter) && m_Selected >= 0) OpenEntry(entries[rows[m_Selected].Index]);
		}
	}
	ImGui::EndChild();

	// ---------------- 경계 (높이 조절)
	const float splitY = p.y + kBarH + (std::max)(20.0f, listH);
	ImGui::SetCursorScreenPos(ImVec2(p.x, splitY));
	ImGui::InvisibleButton("##consoleSplit", ImVec2(avail.x, 4.0f));
	if (ImGui::IsItemHovered() || ImGui::IsItemActive())
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
	if (ImGui::IsItemActive())
		m_DetailHeight = std::clamp(m_DetailHeight - ImGui::GetIO().MouseDelta.y, 40.0f, avail.y - 80.0f);
	dl->AddLine(ImVec2(p.x, splitY + 2.0f), ImVec2(p.x + avail.x, splitY + 2.0f), IM_COL32(30, 30, 30, 255));

	// ---------------- 선택 항목의 전체 내용 (스택의 "(at 파일:줄)" 을 누르면 열기)
	ImGui::SetCursorScreenPos(ImVec2(p.x, splitY + 4.0f));
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
	ImGui::BeginChild("##consoleDetail", ImVec2(avail.x, (std::max)(20.0f, p.y + avail.y - splitY - 4.0f)), false, ImGuiWindowFlags_HorizontalScrollbar);
	if (m_Selected >= 0 && m_Selected < (int)rows.size())
	{
		const LogEntry& e = entries[rows[m_Selected].Index];
		ImGui::SetCursorPos(ImVec2(6, 4));
		ImGui::PushTextWrapPos(0.0f);
		ImGui::TextUnformatted(e.Message.c_str());
		ImGui::PopTextWrapPos();
		std::istringstream in(e.StackTrace);
		std::string line;
		while (std::getline(in, line))
		{
			if (line.empty()) continue;
			ImGui::SetCursorPosX(6);
			const size_t at = line.find("(at ");
			if (at != std::string::npos && !e.File.empty())
			{
				ImGui::TextUnformatted(line.substr(0, at).c_str());
				ImGui::SameLine(0, 0);
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.65f, 1.0f, 1.0f));
				ImGui::TextUnformatted(line.substr(at).c_str());
				ImGui::PopStyleColor();
				if (ImGui::IsItemHovered())
					ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
				if (ImGui::IsItemClicked())
				{
					// "(at Assets/Scripts/Player.cs:12)" → 그 줄 (파일은 첫 사용자 프레임과 같은 폴더 기준으로 찾는다)
					const size_t colon = line.rfind(':');
					const int ln = colon != std::string::npos ? atoi(line.c_str() + colon + 1) : 0;
					const std::string rel = line.substr(at + 4, colon - at - 4);
					const std::wstring full = PathManager::GetI()->GetMovePathW(string_to_wstring(rel));
					ScriptEngine::OpenInCodeEditor(full, ln);
				}
			}
			else
				ImGui::TextUnformatted(line.c_str());
		}
		if (e.StackTrace.empty() && !e.File.empty())
		{
			ImGui::SetCursorPosX(6);
			ImGui::TextDisabled("%s:%d  (double-click the entry to open)", e.File.c_str(), e.Line);
		}
	}
	ImGui::EndChild();
	ImGui::PopStyleColor();

	ImGui::SetCursorScreenPos(p);
	ImGui::Dummy(avail);
}
