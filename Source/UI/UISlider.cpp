#include "pch.h"
#include "UISlider.h"
#include "RectTransform.h"
#include "UnityGUI.h"
#include "ScriptEngine.h"

Slider::Slider()
{
	m_InspectorTitleName = "Slider";
}

float Slider::GetNormalized() const
{
	if (fabsf(m_Max - m_Min) < 1e-6f)
		return 0.0f;
	return std::clamp((m_Value - m_Min) / (m_Max - m_Min), 0.0f, 1.0f);
}

void Slider::SetValue(float value, bool notify)
{
	float v = std::clamp(value, (std::min)(m_Min, m_Max), (std::max)(m_Min, m_Max));
	if (m_WholeNumbers)
		v = roundf(v);
	if (v == m_Value)
		return;
	m_Value = v;
	if (notify && m_pGameObject && Application::IsPlaying())
	{
		char buf[32];
		snprintf(buf, sizeof(buf), "%g", m_Value);
		m_OnValueChanged.Invoke(m_pGameObject, "On Value Changed", buf);
		ScriptEngine::InvokeUIEvent(m_pGameObject->GetFileID(), 1, m_Value);
	}
}

void Slider::SetNormalized(float t, bool notify)
{
	SetValue(m_Min + (m_Max - m_Min) * std::clamp(t, 0.0f, 1.0f), notify);
}

void Slider::SetFromPoint(const Vec2& point)
{
	if (!m_Interactable)
		return;
	// 범위 = Handle 의 부모 (Unity 의 Handle Slide Area), 없으면 Fill 의 부모, 없으면 자기
	RectTransform* area = nullptr;
	for (uint64 id : { m_HandleRect, m_FillRect })
		if (GameObject* go = FindObject(id))
			if (GameObject* parent = go->GetParent())
				if ((area = parent->GetComponent<RectTransform>()) != nullptr)
					break;
	if (area == nullptr)
		area = m_pGameObject ? m_pGameObject->GetComponent<RectTransform>() : nullptr;
	if (area == nullptr || area->GetGameObject() == nullptr)
		return;
	const Matrix inv = area->GetGameObject()->GetTransform()->GetWorldMatrix().Invert();
	const Vec3 local = Vec3::Transform(Vec3(point.x, point.y, 0.0f), inv);
	const Vec2 mn = area->GetRectMin(), size = area->GetRectSize();
	float t = Vertical() ? (size.y > 1e-4f ? (local.y - mn.y) / size.y : 0.0f) : (size.x > 1e-4f ? (local.x - mn.x) / size.x : 0.0f);
	if (Reversed())
		t = 1.0f - t;
	SetNormalized(t);
}

void Slider::OnPointerDown(const Vec2& point) { SetFromPoint(point); }
void Slider::OnDrag(const Vec2& point, const Vec2&) { SetFromPoint(point); }

void Slider::UpdateBeforeLayout(float, bool)
{
	// Unity Slider.UpdateVisuals: Fill 은 기준점을 값만큼 늘이고, Handle 은 기준점을 값 위치로
	const float n = GetNormalized();
	const int axis = Vertical() ? 1 : 0;
	if (GameObject* go = FindObject(m_FillRect))
		if (RectTransform* rt = go->GetComponent<RectTransform>())
		{
			Vec2 mn(0, 0), mx(1, 1);
			if (Reversed()) (&mn.x)[axis] = 1.0f - n;
			else (&mx.x)[axis] = n;
			rt->SetAnchorMin(mn);
			rt->SetAnchorMax(mx);
		}
	if (GameObject* go = FindObject(m_HandleRect))
		if (RectTransform* rt = go->GetComponent<RectTransform>())
		{
			Vec2 mn(0, 0), mx(1, 1);
			const float pos = Reversed() ? 1.0f - n : n;
			(&mn.x)[axis] = pos;
			(&mx.x)[axis] = pos;
			rt->SetAnchorMin(mn);
			rt->SetAnchorMax(mx);
		}
}

void Slider::OnInspectorGUI()
{
	DrawSelectableInspector();
	auto refField = [&](const char* label, uint64& id, const char* key) {
		GameObject* g = FindObject(id);
		const std::string text = g ? g->GetName() + " (Rect Transform)" : "None (Rect Transform)";
		ImVec2 fmin, fmax;
		const int pressed = UnityGUI::ObjectFieldButtons(label, text.c_str(), "rect_transform", nullptr, 0, &fmin, &fmax);
		const ImVec2 after = ImGui::GetCursorScreenPos();
		ImGui::SetCursorScreenPos(fmin);
		ImGui::InvisibleButton(key, ImVec2((std::max)(1.0f, fmax.x - fmin.x - 22.0f), fmax.y - fmin.y));
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("GAME_OBJECT"))
				if (GameObject* go = *(GameObject**)payload->Data)
					id = go->GetFileID();
			ImGui::EndDragDropTarget();
		}
		if (pressed == -1)
			id = 0;
		ImGui::SetCursorScreenPos(after);
	};
	refField("Fill Rect", m_FillRect, "##fillRect");
	refField("Handle Rect", m_HandleRect, "##handleRect");
	static const char* kDirs[] = { "Left To Right", "Right To Left", "Bottom To Top", "Top To Bottom" };
	int dir = (int)m_Direction;
	if (UnityGUI::Dropdown("Direction", &dir, kDirs, 4))
		m_Direction = (Direction)dir;
	float mn = m_Min, mx = m_Max;
	if (UnityGUI::Float("Min Value", &mn)) SetMin(mn);
	if (UnityGUI::Float("Max Value", &mx)) SetMax(mx);
	bool whole = m_WholeNumbers;
	if (UnityGUI::Toggle("Whole Numbers", &whole)) SetWholeNumbers(whole);
	float v = m_Value;
	if (UnityGUI::Slider("Value", &v, (std::min)(m_Min, m_Max), (std::max)(m_Min, m_Max)))
		SetValue(v, Application::IsPlaying());
	UnityGUI::Spacing(6.0f);
	m_OnValueChanged.Draw("On Value Changed (Single)", "float");
}

GENERATE_COMPONENT_FUNC_TOJSON(Slider)
{
	json j;
	SERIALIZE_TYPE(j, Slider);
	SelectableToJson(j);
	j["fillRect"] = m_FillRect;
	j["handleRect"] = m_HandleRect;
	j["direction"] = (int)m_Direction;
	j["minValue"] = m_Min;
	j["maxValue"] = m_Max;
	j["wholeNumbers"] = m_WholeNumbers;
	j["value"] = m_Value;
	j["onValueChanged"] = m_OnValueChanged.ToJson();
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(Slider)
{
	SelectableFromJson(j);
	m_FillRect = j.value("fillRect", (uint64)0);
	m_HandleRect = j.value("handleRect", (uint64)0);
	m_Direction = (Direction)j.value("direction", 0);
	m_Min = j.value("minValue", 0.0f);
	m_Max = j.value("maxValue", 1.0f);
	m_WholeNumbers = j.value("wholeNumbers", false);
	m_Value = j.value("value", 0.0f);
	m_OnValueChanged.FromJson(j.contains("onValueChanged") ? j["onValueChanged"] : json::array());
}
