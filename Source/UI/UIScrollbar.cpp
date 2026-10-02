#include "pch.h"
#include "UIScrollbar.h"
#include "RectTransform.h"
#include "UnityGUI.h"
#include "ScriptEngine.h"

Scrollbar::Scrollbar()
{
	m_InspectorTitleName = "Scrollbar";
}

void Scrollbar::SetValue(float v, bool notify)
{
	v = std::clamp(v, 0.0f, 1.0f);
	if (m_Steps > 1)
		v = roundf(v * (m_Steps - 1)) / (m_Steps - 1);
	if (fabsf(v - m_Value) < 1e-6f)
		return;
	m_Value = v;
	if (notify && m_pGameObject && Application::IsPlaying())
	{
		char buf[32];
		snprintf(buf, sizeof(buf), "%g", v);
		m_OnValueChanged.Invoke(m_pGameObject, "On Value Changed", buf);
		ScriptEngine::InvokeUIEvent(m_pGameObject->GetFileID(), 6, v);   // C# scrollbar.onValueChanged
	}
}

RectTransform* Scrollbar::Container()
{
	GameObject* handle = FindObject(m_HandleRect);
	GameObject* area = handle ? handle->GetParent() : nullptr;
	return area ? area->GetComponent<RectTransform>() : nullptr;
}

bool Scrollbar::LocalInContainer(const Vec3& point, Vec2& local)
{
	RectTransform* c = Container();
	if (c == nullptr || c->GetGameObject() == nullptr)
		return false;
	const Vec3 l = Vec3::Transform(point, c->GetGameObject()->GetTransform()->GetWorldMatrix().Invert());
	local = Vec2(l.x, l.y);
	return true;
}

void Scrollbar::UpdateBeforeLayout(float, bool)
{
	// 손잡이 기준점: 움직이는 축은 [value × (1 − size), + size], 다른 축은 꽉 차게
	GameObject* handle = FindObject(m_HandleRect);
	RectTransform* rt = handle ? handle->GetComponent<RectTransform>() : nullptr;
	if (rt == nullptr)
		return;
	const int axis = Vertical() ? 1 : 0;
	float start = m_Value * (1.0f - m_Size);
	if (Reversed())
		start = 1.0f - start - m_Size;
	Vec2 aMin(0, 0), aMax(1, 1);
	(&aMin.x)[axis] = start;
	(&aMax.x)[axis] = start + m_Size;
	if (rt->GetAnchorMin() != aMin) rt->SetAnchorMin(aMin);
	if (rt->GetAnchorMax() != aMax) rt->SetAnchorMax(aMax);
}

void Scrollbar::OnPointerDown(const Vec3& point)
{
	m_DraggingHandle = false;
	if (!m_Interactable)
		return;
	RectTransform* c = Container();
	Vec2 local;
	if (c == nullptr || !LocalInContainer(point, local))
		return;
	const int axis = Vertical() ? 1 : 0;
	const Vec2 cm = c->GetRectMin(), cs = c->GetRectSize();
	const float cMin = axis ? cm.y : cm.x, cSize = axis ? cs.y : cs.x;
	const float handleSize = cSize * m_Size;
	float start = m_Value * (1.0f - m_Size);
	if (Reversed()) start = 1.0f - start - m_Size;
	const float hMin = cMin + start * cSize, hMax = hMin + handleSize;
	const float p = (&local.x)[axis];
	if (p >= hMin && p <= hMax)
	{
		// 손잡이를 잡았다: 잡은 자리를 기억해 끄는 동안 그대로
		m_DraggingHandle = true;
		m_DragOffset = p - (hMin + hMax) * 0.5f;
		return;
	}
	// 트랙: 그쪽으로 한 쪽(Size) 만큼 (Unity 의 Click Repeat 한 번)
	const bool towardMax = (p > hMax) != Reversed();
	SetValue(m_Value + (towardMax ? 1.0f : -1.0f) * (std::max)(m_Size, 0.05f));
}

void Scrollbar::OnDrag(const Vec3& point, const Vec3&)
{
	if (!m_Interactable || !m_DraggingHandle)
		return;
	RectTransform* c = Container();
	Vec2 local;
	if (c == nullptr || !LocalInContainer(point, local))
		return;
	const int axis = Vertical() ? 1 : 0;
	const Vec2 cm = c->GetRectMin(), cs = c->GetRectSize();
	const float cMin = axis ? cm.y : cm.x, cSize = axis ? cs.y : cs.x;
	const float travel = cSize * (1.0f - m_Size);
	if (travel <= 1e-4f)
		return;
	const float center = (&local.x)[axis] - m_DragOffset;
	float t = (center - cMin - cSize * m_Size * 0.5f) / travel;
	if (Reversed())
		t = 1.0f - t;
	SetValue(t);
}

void Scrollbar::RemapFileIDs(const std::unordered_map<uint64, uint64>& map)
{
	UISelectable::RemapFileIDs(map);
	if (auto it = map.find(m_HandleRect); it != map.end())
		m_HandleRect = it->second;
}

void Scrollbar::OnInspectorGUI()
{
	DrawSelectableInspector();
	GameObject* h = FindObject(m_HandleRect);
	UnityGUI::ValueLabel("Handle Rect", h ? (h->GetName() + " (Rect Transform)").c_str() : "None (Rect Transform)");
	static const char* kDir[] = { "Left To Right", "Right To Left", "Bottom To Top", "Top To Bottom" };
	int d = (int)m_Direction;
	if (UnityGUI::Dropdown("Direction", &d, kDir, 4))
		m_Direction = (Direction)d;
	float v = m_Value;
	if (UnityGUI::Slider("Value", &v, 0.0f, 1.0f))
		SetValue(v);
	UnityGUI::Slider("Size", &m_Size, 0.0f, 1.0f);
	if (UnityGUI::Int("Number Of Steps", &m_Steps))
		m_Steps = std::clamp(m_Steps, 0, 11);
	m_OnValueChanged.Draw("On Value Changed (Single)", "float");
}

GENERATE_COMPONENT_FUNC_TOJSON(Scrollbar)
{
	json j;
	SERIALIZE_TYPE(j, Scrollbar);
	SelectableToJson(j);
	j["handleRect"] = m_HandleRect;
	j["direction"] = (int)m_Direction;
	j["value"] = m_Value;
	j["size"] = m_Size;
	j["numberOfSteps"] = m_Steps;
	j["onValueChanged"] = m_OnValueChanged.ToJson();
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(Scrollbar)
{
	SelectableFromJson(j);
	m_HandleRect = j.value("handleRect", (uint64)0);
	m_Direction = (Direction)std::clamp(j.value("direction", 0), 0, 3);
	m_Value = std::clamp(j.value("value", 0.0f), 0.0f, 1.0f);
	m_Size = std::clamp(j.value("size", 0.2f), 0.0f, 1.0f);
	m_Steps = (std::max)(0, j.value("numberOfSteps", 0));
	m_OnValueChanged = UIEventList();
	if (j.contains("onValueChanged"))
		m_OnValueChanged.FromJson(j["onValueChanged"]);
}
