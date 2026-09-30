#include "pch.h"
#include "ConsoleEditorWindow.h"
#include "Debug.h"

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
    // Create the control bar at the top
    ImGui::BeginChild("ControlBar", ImVec2(0, ImGui::GetFrameHeightWithSpacing()), false);

    // Add control buttons
    if (ImGui::Button("Clear"))
    {
        Debug::ClearAll();
    }

    // Add other control buttons here as needed
    // e.g., if (ImGui::Button("AnotherButton")) { ... }

    ImGui::EndChild();

    // Create a scrollable child for the logs
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("LogArea", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar | ImGuiWindowFlags_NoDecoration);

    // Get logs and display them
    auto logs = Debug::GetAllLogs();
    bool alternateColor = false;
    ImVec4 textColor = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    for (const auto& log : logs)
    {
        if (alternateColor)
        {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1.0f, 1.0f, 1.0f, 0.0f)); // Light grey background
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1.0f, 1.0f, 1.0f, 0.1f)); // White background
        }

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4)); // Add padding to each log message
        ImGui::PushStyleColor(ImGuiCol_Text, textColor);

        float logHeight = ImGui::GetTextLineHeightWithSpacing() + 10; // Total height with padding
        ImGui::BeginChild(ImGui::GetID((void*)log.c_str()), ImVec2(0, logHeight), false, ImGuiWindowFlags_NoDecoration);

        // Calculate vertical center position
        float yOffset = (logHeight - ImGui::GetTextLineHeightWithSpacing()) * 0.5f;

        // Add left and vertical padding 
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + yOffset); 
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4); 

        ImGui::TextWrapped("%s", log.c_str());

        ImGui::EndChild();

        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar();

        alternateColor = !alternateColor;
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();
}
