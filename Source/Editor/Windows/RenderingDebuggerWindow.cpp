#include "pch.h"
#include "RenderingDebuggerWindow.h"
#include "RenderingDebug.h"
#include "MotionVectors.h"
#include "ProbeVolumes.h"
#include "UnityGUI.h"

namespace
{
	RenderingDebuggerWindow* s_Instance = nullptr;
}

RenderingDebuggerWindow::RenderingDebuggerWindow()
	: EditorWindow("Rendering Debugger", ICON_FA_BUG)
{
	s_Instance = this;
	SetIsOpened(false);
}

void RenderingDebuggerWindow::Toggle()
{
	if (s_Instance == nullptr)
		return;
	const bool open = !s_Instance->GetIsOpened();
	s_Instance->SetIsOpened(open);
	s_Instance->m_FocusNext = open;
}

bool RenderingDebuggerWindow::IsOpen()
{
	return s_Instance && s_Instance->GetIsOpened();
}

void RenderingDebuggerWindow::BeforeBegin()
{
	const ImGuiViewport* vp = ImGui::GetMainViewport();
	ImGui::SetNextWindowSize(ImVec2(440.0f, 330.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.4f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
	if (m_FocusNext)
	{
		ImGui::SetNextWindowFocus();
		m_FocusNext = false;
	}
}

void RenderingDebuggerWindow::OnRender()
{
	RenderingDebug::State& s = RenderingDebug::Get();
	UnityGUI::Label("Rendering", 0, true);
	int mode = s.Mode;
	if (UnityGUI::Dropdown("Fullscreen Debug Mode", &mode, RenderingDebug::ModeNames(), RenderingDebug::ModeCount, 1))
		RenderingDebug::SetMode(mode);
	switch (s.Mode)
	{
	case RenderingDebug::Depth:
		if (UnityGUI::Float("Depth Range (m)", &s.DepthRange, 1))
			s.DepthRange = (std::max)(0.01f, s.DepthRange);
		UnityGUI::HelpBox("View depth: black = near, white = Depth Range and beyond (sky = white).", false, 1);
		break;
	case RenderingDebug::Normals:
		UnityGUI::HelpBox("World-space normals as color (x, y, z -> red, green, blue; facing up = light green).", false, 1);
		break;
	case RenderingDebug::AmbientOcclusion:
		UnityGUI::HelpBox("The SSAO map: white = open, dark = occluded. Turn SSAO on in a Volume (Add Override > Lighting > Screen Space Ambient Occlusion).", false, 1);
		break;
	case RenderingDebug::MotionVectors:
		if (UnityGUI::Float("Motion Vector Scale", &s.MotionScale, 1))
			s.MotionScale = (std::max)(0.0f, s.MotionScale);
		UnityGUI::HelpBox("Screen velocity: color = direction (red right, green down, cyan left, blue up), brightness = speed. Black = no motion. Objects move in Play mode.", false, 1);
		break;
	case RenderingDebug::ProbeVolumeLighting:
		UnityGUI::HelpBox("Adaptive Probe Volume: only the light from the probes (x6). Needs an Adaptive Probe Volume in the scene.", false, 1);
		break;
	case RenderingDebug::AdditionalLightCount:
		UnityGUI::HelpBox("Forward+: how many additional (clustered) lights each pixel's cluster has — black 0, blue 1, green 4, yellow 8, red 16+. The first 4 lights (with shadows) are not counted.", false, 1);
		break;
	case RenderingDebug::ProbeVolumeSampling:
		UnityGUI::HelpBox("Adaptive Probe Volume sampling: green = wall test + normal, yellow = normal only, red = trilinear only, blue = larger cascade.", false, 1);
		break;
	default:
		break;
	}
	UnityGUI::Spacing(6.0f);
	if (s.Mode != RenderingDebug::None && UnityGUI::CenterButton("Reset", 120.0f))
		RenderingDebug::SetMode(RenderingDebug::None);
	UnityGUI::HelpBox("Applies to the Scene view and the Game view. Also in the Scene view toolbar (bug icon > Debug View) and the CLI: nova debugview.", false);
}
