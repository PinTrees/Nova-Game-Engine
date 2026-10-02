#include "pch.h"
#include "UICanvas.h"
#include "UnityGUI.h"
#include "RectTransform.h"
#include "SceneViewOverlay.h"
#include "Camera.h"

std::vector<Canvas*> Canvas::s_All;
std::vector<EventSystem*> EventSystem::s_All;

namespace
{
	bool ActiveInHierarchy(GameObject* go)
	{
		for (GameObject* g = go; g != nullptr; g = g->GetParent())
			if (!g->IsActive())
				return false;
		return go != nullptr;
	}
}

// ================================================================== Canvas
Canvas::Canvas()
{
	m_InspectorTitleName = "Canvas";
	s_All.push_back(this);
}

Canvas::~Canvas()
{
	s_All.erase(std::remove(s_All.begin(), s_All.end(), this), s_All.end());
}

void Canvas::OnDrawGizmos()
{
	if (!SceneViewOverlay::IsActive() || !m_Enabled || !ActiveInHierarchy(m_pGameObject))
		return;
	RectTransform* rt = m_pGameObject->GetComponent<RectTransform>();
	if (rt == nullptr)
		return;
	Vec3 k[4];
	rt->GetWorldCorners(k);
	for (int i = 0; i < 4; ++i)
		SceneViewOverlay::DrawLine(k[i], k[(i + 1) % 4], IM_COL32(255, 255, 255, 150));
}

void Canvas::OnInspectorGUI()
{
	static const char* kModes[] = { "Screen Space - Overlay", "Screen Space - Camera", "World Space" };
	int mode = (int)m_RenderMode;
	if (UnityGUI::Dropdown("Render Mode", &mode, kModes, 3) && mode != (int)m_RenderMode)
	{
		// World Space 로 바꾸면: 화면 크기 그대로, 1 단위 = 1 cm 정도로 줄여 원점 근처에 (Unity 는 그대로 두지만 픽셀 단위라 너무 크다)
		if ((RenderMode)mode == RenderMode::WorldSpace)
			if (RectTransform* rt = m_pGameObject ? m_pGameObject->GetComponent<RectTransform>() : nullptr)
			{
				rt->SetAnchorMin(Vec2(0.5f, 0.5f));
				rt->SetAnchorMax(Vec2(0.5f, 0.5f));
				rt->SetSizeDelta(rt->GetRectSize());
				rt->SetAnchoredPosition(Vec2(0.0f, 1.5f));
				m_pGameObject->GetTransform()->SetLocalPosition(Vec3(0.0f, 1.5f, 0.0f));
				m_pGameObject->GetTransform()->SetLocalScale(Vec3(0.01f, 0.01f, 0.01f));
			}
		m_RenderMode = (RenderMode)mode;
	}
	if (m_RenderMode == RenderMode::ScreenSpaceCamera)
	{
		UnityGUI::GameObjectField("Render Camera", &m_WorldCamera, 1);
		if (m_WorldCamera == 0 || FindWorldCamera() == nullptr)
			UnityGUI::HelpBox("A Screen Space Canvas with no specified camera acts like an Overlay Canvas.", false);
		if (UnityGUI::Float("Plane Distance", &m_PlaneDistance, 1))
			m_PlaneDistance = (std::max)(0.01f, m_PlaneDistance);
	}
	else if (m_RenderMode == RenderMode::WorldSpace)
	{
		UnityGUI::GameObjectField("Event Camera", &m_WorldCamera, 1);
		if (m_WorldCamera == 0)
			UnityGUI::HelpBox("No Event Camera: the Game view camera receives clicks.", false);
	}
	if (m_RenderMode != RenderMode::WorldSpace)
		UnityGUI::Toggle("Pixel Perfect", &m_PixelPerfect);
	UnityGUI::Int(m_RenderMode == RenderMode::ScreenSpaceOverlay ? "Sort Order" : "Order in Layer", &m_SortOrder);
	static const char* kDisplays[] = { "Display 1", "Display 2", "Display 3", "Display 4", "Display 5", "Display 6", "Display 7", "Display 8" };
	if (m_RenderMode == RenderMode::ScreenSpaceOverlay)
		UnityGUI::Dropdown("Target Display", &m_TargetDisplay, kDisplays, 8);
	UnityGUI::ValueLabel("Additional Shader Channels", "Nothing");
	UnityGUI::Toggle("Vertex Color Always In Gamma Space", &m_VertexColorGamma);
}

GENERATE_COMPONENT_FUNC_TOJSON(Canvas)
{
	json j;
	SERIALIZE_TYPE(j, Canvas);
	j["enabled"] = m_Enabled;
	j["renderMode"] = (int)m_RenderMode;
	j["pixelPerfect"] = m_PixelPerfect;
	j["sortOrder"] = m_SortOrder;
	j["targetDisplay"] = m_TargetDisplay;
	j["vertexColorGamma"] = m_VertexColorGamma;
	if (m_WorldCamera) j["worldCamera"] = m_WorldCamera;
	if (m_PlaneDistance != 100.0f) j["planeDistance"] = m_PlaneDistance;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(Canvas)
{
	m_Enabled = j.value("enabled", true);
	m_RenderMode = (RenderMode)j.value("renderMode", 0);
	m_PixelPerfect = j.value("pixelPerfect", false);
	m_SortOrder = j.value("sortOrder", 0);
	m_TargetDisplay = j.value("targetDisplay", 0);
	m_VertexColorGamma = j.value("vertexColorGamma", false);
	m_WorldCamera = j.value("worldCamera", (uint64)0);
	m_PlaneDistance = (std::max)(0.01f, j.value("planeDistance", 100.0f));
}

Camera* Canvas::FindWorldCamera() const
{
	if (m_WorldCamera == 0)
		return nullptr;
	Scene* scene = SceneManager::GetI()->GetCurrentScene();
	GameObject* go = scene ? scene->FindByFileID(m_WorldCamera) : nullptr;
	if (go == nullptr || !go->IsActive())
		return nullptr;
	Camera* cam = go->GetComponent<Camera>();
	return cam && cam->IsEnabled() ? cam : nullptr;
}

void Canvas::RemapFileIDs(const std::unordered_map<uint64, uint64>& map)
{
	if (auto it = map.find(m_WorldCamera); it != map.end())
		m_WorldCamera = it->second;
}

// ================================================================== Canvas Scaler
CanvasScaler::CanvasScaler()
{
	m_InspectorTitleName = "Canvas Scaler";
}

float CanvasScaler::ComputeScale(float screenW, float screenH) const
{
	if (!m_Enabled)
		return 1.0f;
	switch (m_Mode)
	{
	case ScaleMode::ScaleWithScreenSize:
	{
		// Unity CanvasScaler.HandleScaleWithScreenSize 와 같은 식
		const float rw = (std::max)(1.0f, m_RefW), rh = (std::max)(1.0f, m_RefH);
		switch (m_MatchMode)
		{
		case MatchMode::Expand: return (std::min)(screenW / rw, screenH / rh);
		case MatchMode::Shrink: return (std::max)(screenW / rw, screenH / rh);
		default:
		{
			const float logW = log2f((std::max)(1.0f, screenW) / rw);
			const float logH = log2f((std::max)(1.0f, screenH) / rh);
			return powf(2.0f, logW + (logH - logW) * std::clamp(m_Match, 0.0f, 1.0f));
		}
		}
	}
	case ScaleMode::ConstantPhysicalSize:
		return 1.0f;
	default:
		return (std::max)(0.01f, m_ScaleFactor);
	}
}

void CanvasScaler::OnInspectorGUI()
{
	static const char* kModes[] = { "Constant Pixel Size", "Scale With Screen Size", "Constant Physical Size" };
	int mode = (int)m_Mode;
	if (UnityGUI::Dropdown("UI Scale Mode", &mode, kModes, 3))
		m_Mode = (ScaleMode)mode;
	if (m_Mode == ScaleMode::ConstantPixelSize)
	{
		if (UnityGUI::Float("Scale Factor", &m_ScaleFactor))
			m_ScaleFactor = (std::max)(0.01f, m_ScaleFactor);
	}
	else if (m_Mode == ScaleMode::ScaleWithScreenSize)
	{
		UnityGUI::Vector2Pair("Reference Resolution", "X", &m_RefW, "Y", &m_RefH);
		static const char* kMatch[] = { "Match Width Or Height", "Expand", "Shrink" };
		int mm = (int)m_MatchMode;
		if (UnityGUI::Dropdown("Screen Match Mode", &mm, kMatch, 3))
			m_MatchMode = (MatchMode)mm;
		if (m_MatchMode == MatchMode::MatchWidthOrHeight)
		{
			UnityGUI::Slider("Match", &m_Match, 0.0f, 1.0f);
			UnityGUI::SliderCaptions("Width", "Height");
		}
	}
	else
		UnityGUI::HelpBox("Constant Physical Size is treated as Constant Pixel Size.", true);
	UnityGUI::Float("Reference Pixels Per Unit", &m_RefPixelsPerUnit);
}

GENERATE_COMPONENT_FUNC_TOJSON(CanvasScaler)
{
	json j;
	SERIALIZE_TYPE(j, CanvasScaler);
	j["enabled"] = m_Enabled;
	j["mode"] = (int)m_Mode;
	j["scaleFactor"] = m_ScaleFactor;
	j["referenceResolution"] = { m_RefW, m_RefH };
	j["matchMode"] = (int)m_MatchMode;
	j["match"] = m_Match;
	j["referencePixelsPerUnit"] = m_RefPixelsPerUnit;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(CanvasScaler)
{
	m_Enabled = j.value("enabled", true);
	m_Mode = (ScaleMode)j.value("mode", 0);
	m_ScaleFactor = j.value("scaleFactor", 1.0f);
	if (j.contains("referenceResolution") && j["referenceResolution"].is_array() && j["referenceResolution"].size() == 2)
	{
		m_RefW = j["referenceResolution"][0].get<float>();
		m_RefH = j["referenceResolution"][1].get<float>();
	}
	m_MatchMode = (MatchMode)j.value("matchMode", 0);
	m_Match = j.value("match", 0.0f);
	m_RefPixelsPerUnit = j.value("referencePixelsPerUnit", 100.0f);
}

// ================================================================== Graphic Raycaster
GraphicRaycaster::GraphicRaycaster()
{
	m_InspectorTitleName = "Graphic Raycaster";
}

void GraphicRaycaster::OnInspectorGUI()
{
	UnityGUI::Toggle("Ignore Reversed Graphics", &m_IgnoreReversed);
	static const char* kBlocking[] = { "None", "Two D", "Three D", "All" };
	UnityGUI::Dropdown("Blocking Objects", &m_BlockingObjects, kBlocking, 4);
	UnityGUI::MaskField("Blocking Mask", &m_BlockingMask);
}

GENERATE_COMPONENT_FUNC_TOJSON(GraphicRaycaster)
{
	json j;
	SERIALIZE_TYPE(j, GraphicRaycaster);
	j["enabled"] = m_Enabled;
	j["ignoreReversed"] = m_IgnoreReversed;
	j["blockingObjects"] = m_BlockingObjects;
	j["blockingMask"] = m_BlockingMask;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(GraphicRaycaster)
{
	m_Enabled = j.value("enabled", true);
	m_IgnoreReversed = j.value("ignoreReversed", true);
	m_BlockingObjects = j.value("blockingObjects", 0);
	m_BlockingMask = j.value("blockingMask", 0xFFFFFFFFu);
}

// ================================================================== Event System
EventSystem::EventSystem()
{
	m_InspectorTitleName = "Event System";
	s_All.push_back(this);
}

EventSystem::~EventSystem()
{
	s_All.erase(std::remove(s_All.begin(), s_All.end(), this), s_All.end());
}

bool EventSystem::AnyActive()
{
	for (EventSystem* e : s_All)
		if (e->IsEnabled() && ActiveInHierarchy(e->GetGameObject()))
			return true;
	return false;
}

void EventSystem::OnInspectorGUI()
{
	std::string first = "None (Game Object)";
	if (m_FirstSelected)
		if (Scene* scene = SceneManager::GetI()->GetCurrentScene())
			if (GameObject* go = scene->FindByFileID(m_FirstSelected))
				first = go->GetName();
	ImVec2 fmin, fmax;
	const int pressed = UnityGUI::ObjectFieldButtons("First Selected", first.c_str(), "gameobject", nullptr, 0, &fmin, &fmax);
	const ImVec2 after = ImGui::GetCursorScreenPos();
	ImGui::SetCursorScreenPos(fmin);
	ImGui::InvisibleButton("##firstSel", ImVec2((std::max)(1.0f, fmax.x - fmin.x - 22.0f), fmax.y - fmin.y));
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("GAME_OBJECT"))
			if (GameObject* go = *(GameObject**)payload->Data)
				m_FirstSelected = go->GetFileID();
		ImGui::EndDragDropTarget();
	}
	if (pressed == -1)
		m_FirstSelected = 0;
	ImGui::SetCursorScreenPos(after);
	UnityGUI::Toggle("Send Navigation Events", &m_SendNavigationEvents);
	UnityGUI::Int("Drag Threshold", &m_DragThreshold);
	UnityGUI::Spacing(4.0f);
	UnityGUI::HelpBox("Mouse input for the UI is read from the Game view (hover, press, click).", false);
}

GENERATE_COMPONENT_FUNC_TOJSON(EventSystem)
{
	json j;
	SERIALIZE_TYPE(j, EventSystem);
	j["enabled"] = m_Enabled;
	j["firstSelected"] = m_FirstSelected;
	j["sendNavigationEvents"] = m_SendNavigationEvents;
	j["dragThreshold"] = m_DragThreshold;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(EventSystem)
{
	m_Enabled = j.value("enabled", true);
	m_FirstSelected = j.value("firstSelected", (uint64)0);
	m_SendNavigationEvents = j.value("sendNavigationEvents", true);
	m_DragThreshold = j.value("dragThreshold", 10);
}
