#include "pch.h"
#include "UIMask.h"
#include "RectTransform.h"
#include "UnityGUI.h"

std::vector<ScrollRect*> ScrollRect::s_All;

namespace
{
	GameObject* FindObject(uint64 id)
	{
		Scene* scene = SceneManager::GetI()->GetCurrentScene();
		return (scene && id) ? scene->FindByFileID(id) : nullptr;
	}

	// Unity ScrollRect.RubberDelta: 끝을 넘어 끌수록 덜 움직인다
	float RubberDelta(float overStretching, float viewSize)
	{
		if (viewSize <= 0.0f)
			return 0.0f;
		return (1.0f - (1.0f / ((fabsf(overStretching) * 0.55f / viewSize) + 1.0f))) * viewSize * (overStretching < 0.0f ? -1.0f : 1.0f);
	}

	void RefField(const char* label, uint64& id, const char* key, const char* icon)
	{
		GameObject* g = FindObject(id);
		const std::string text = g ? g->GetName() + " (Rect Transform)" : "None (Rect Transform)";
		ImVec2 fmin, fmax;
		const int pressed = UnityGUI::ObjectFieldButtons(label, text.c_str(), icon, nullptr, 0, &fmin, &fmax);
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
	}
}

// ================================================================== Mask
Mask::Mask() { m_InspectorTitleName = "Mask"; }

void Mask::OnInspectorGUI()
{
	UnityGUI::Toggle("Show Mask Graphic", &m_ShowMaskGraphic);
	UnityGUI::HelpBox("Children are clipped to this element's rectangle (sprite-shaped masking is not supported yet).", false);
}

GENERATE_COMPONENT_FUNC_TOJSON(Mask)
{
	json j;
	SERIALIZE_TYPE(j, Mask);
	j["enabled"] = m_Enabled;
	j["showMaskGraphic"] = m_ShowMaskGraphic;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(Mask)
{
	m_Enabled = j.value("enabled", true);
	m_ShowMaskGraphic = j.value("showMaskGraphic", true);
}

// ================================================================== RectMask2D
RectMask2D::RectMask2D() { m_InspectorTitleName = "Rect Mask 2D"; }

void RectMask2D::OnInspectorGUI()
{
	if (UnityGUI::FoldoutPlain("Padding", 0, true))
	{
		UnityGUI::Float("Left", &m_Padding.x, 1);
		UnityGUI::Float("Bottom", &m_Padding.y, 1);
		UnityGUI::Float("Right", &m_Padding.z, 1);
		UnityGUI::Float("Top", &m_Padding.w, 1);
	}
}

GENERATE_COMPONENT_FUNC_TOJSON(RectMask2D)
{
	json j;
	SERIALIZE_TYPE(j, RectMask2D);
	j["enabled"] = m_Enabled;
	j["padding"] = { m_Padding.x, m_Padding.y, m_Padding.z, m_Padding.w };
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(RectMask2D)
{
	m_Enabled = j.value("enabled", true);
	if (j.contains("padding") && j["padding"].is_array() && j["padding"].size() == 4)
		m_Padding = Vec4(j["padding"][0].get<float>(), j["padding"][1].get<float>(), j["padding"][2].get<float>(), j["padding"][3].get<float>());
}

// ================================================================== ScrollRect
ScrollRect::ScrollRect()
{
	m_InspectorTitleName = "Scroll Rect";
	s_All.push_back(this);
}

ScrollRect::~ScrollRect()
{
	s_All.erase(std::remove(s_All.begin(), s_All.end(), this), s_All.end());
}

RectTransform* ScrollRect::ContentRect()
{
	GameObject* go = FindObject(m_Content);
	return go ? go->GetComponent<RectTransform>() : nullptr;
}

RectTransform* ScrollRect::ViewportRect()
{
	GameObject* go = m_Viewport ? FindObject(m_Viewport) : m_pGameObject;
	return go ? go->GetComponent<RectTransform>() : nullptr;
}

Vec2 ScrollRect::WorldToContentParent(const Vec3& worldDelta)
{
	RectTransform* content = ContentRect();
	GameObject* parent = content && content->GetGameObject() ? content->GetGameObject()->GetParent() : nullptr;
	if (parent == nullptr)
		return Vec2(worldDelta.x, worldDelta.y);
	const Matrix inv = parent->GetTransform()->GetWorldMatrix().Invert();
	const Vec3 d = Vec3::TransformNormal(worldDelta, inv);
	return Vec2(d.x, d.y);
}

Vec2 ScrollRect::CalculateOffset(const Vec2& extraDelta)
{
	Vec2 offset(0, 0);
	RectTransform* content = ContentRect();
	RectTransform* view = ViewportRect();
	if (content == nullptr || view == nullptr || m_Movement == Movement::Unrestricted)
		return offset;
	// Content 의 사각형을 Viewport 로컬 공간으로
	Vec3 corners[4];
	content->GetWorldCorners(corners);
	const Matrix inv = view->GetGameObject()->GetTransform()->GetWorldMatrix().Invert();
	Vec2 cmin(FLT_MAX, FLT_MAX), cmax(-FLT_MAX, -FLT_MAX);
	for (const Vec3& c : corners)
	{
		const Vec3 l = Vec3::Transform(c, inv);
		cmin = Vec2::Min(cmin, Vec2(l.x, l.y));
		cmax = Vec2::Max(cmax, Vec2(l.x, l.y));
	}
	cmin += extraDelta;
	cmax += extraDelta;
	const Vec2 vmin = view->GetRectMin(), vmax = view->GetRectMin() + view->GetRectSize();
	for (int axis = 0; axis < 2; ++axis)
	{
		if ((axis == 0 && !m_Horizontal) || (axis == 1 && !m_Vertical))
			continue;
		const float cMin = (&cmin.x)[axis], cMax = (&cmax.x)[axis], vMin = (&vmin.x)[axis], vMax = (&vmax.x)[axis];
		float& o = (&offset.x)[axis];
		if (cMax - cMin <= vMax - vMin)
			o = axis == 1 ? vMax - cMax : vMin - cMin;   // 작으면 위/왼쪽에 붙인다
		else if (cMin > vMin)
			o = vMin - cMin;
		else if (cMax < vMax)
			o = vMax - cMax;
	}
	return offset;
}

void ScrollRect::OnBeginDrag(const Vec3& point)
{
	RectTransform* content = ContentRect();
	if (content == nullptr)
		return;
	m_Dragging = true;
	m_DragStartPointer = point;
	m_ContentStart = content->GetAnchoredPosition();
	m_Velocity = Vec2(0, 0);
}

void ScrollRect::OnDrag(const Vec3& point, const Vec3&)
{
	RectTransform* content = ContentRect();
	RectTransform* view = ViewportRect();
	if (!m_Dragging || content == nullptr || view == nullptr)
		return;
	Vec2 delta = WorldToContentParent(point - m_DragStartPointer);
	if (!m_Horizontal) delta.x = 0.0f;
	if (!m_Vertical) delta.y = 0.0f;
	Vec2 pos = m_ContentStart + delta;
	const Vec2 offset = CalculateOffset(pos - content->GetAnchoredPosition());
	if (m_Movement == Movement::Clamped)
		pos += offset;
	else if (m_Movement == Movement::Elastic)
	{
		// 끝을 넘은 만큼 고무줄처럼 덜 따라온다
		const Vec2 size = view->GetRectSize();
		if (offset.x != 0.0f) pos.x = pos.x + offset.x - RubberDelta(offset.x, size.x);
		if (offset.y != 0.0f) pos.y = pos.y + offset.y - RubberDelta(offset.y, size.y);
	}
	content->SetAnchoredPosition(pos);
}

void ScrollRect::OnEndDrag(const Vec3&)
{
	m_Dragging = false;
}

void ScrollRect::OnScroll(float wheel)
{
	RectTransform* content = ContentRect();
	if (content == nullptr)
		return;
	// 휠 위 = 위쪽 내용을 보여 준다 (Content 를 아래로). 가로만 켜져 있으면 가로로
	Vec2 delta(0, 0);
	const float step = wheel * m_Sensitivity * 30.0f;
	if (m_Vertical) delta.y = -step;
	else if (m_Horizontal) delta.x = step;
	Vec2 pos = content->GetAnchoredPosition() + delta;
	if (m_Movement != Movement::Unrestricted)
		pos += CalculateOffset(delta);
	content->SetAnchoredPosition(pos);
	m_Velocity = Vec2(0, 0);
}

void ScrollRect::UpdateBeforeLayout(float dt, bool playing)
{
	RectTransform* content = ContentRect();
	if (!playing || content == nullptr || !m_Enabled || dt <= 0.0f)
	{
		m_Velocity = Vec2(0, 0);
		m_Dragging = false;
		return;
	}
	Vec2 pos = content->GetAnchoredPosition();
	if (m_Dragging)
	{
		// 놓았을 때 관성으로 이어갈 속도
		const Vec2 v = (pos - m_PrevPosition) / dt;
		m_Velocity = m_Velocity + (v - m_Velocity) * (std::min)(1.0f, dt * 10.0f);
	}
	else
	{
		const Vec2 offset = CalculateOffset(Vec2(0, 0));
		for (int axis = 0; axis < 2; ++axis)
		{
			float& p = (&pos.x)[axis];
			float& v = (&m_Velocity.x)[axis];
			const float o = (&offset.x)[axis];
			if (m_Movement == Movement::Elastic && o != 0.0f)
			{
				// 제자리로 부드럽게 (Unity 의 SmoothDamp 와 비슷한 감쇠)
				p += o * (1.0f - expf(-dt * 4.0f / (std::max)(0.01f, m_Elasticity * 4.0f)));
				v = 0.0f;
			}
			else if (m_Inertia)
			{
				v *= powf(m_Deceleration, dt);
				if (fabsf(v) < 1.0f) v = 0.0f;
				p += v * dt;
			}
			else
				v = 0.0f;
		}
		if (m_Movement == Movement::Clamped)
		{
			const Vec2 o = CalculateOffset(pos - content->GetAnchoredPosition());
			pos += o;
			if (o.x != 0.0f) m_Velocity.x = 0.0f;
			if (o.y != 0.0f) m_Velocity.y = 0.0f;
		}
		content->SetAnchoredPosition(pos);
	}
	m_PrevPosition = pos;
}

Vec2 ScrollRect::GetNormalizedPosition()
{
	RectTransform* content = ContentRect();
	RectTransform* view = ViewportRect();
	if (content == nullptr || view == nullptr)
		return Vec2(0, 0);
	Vec3 corners[4];
	content->GetWorldCorners(corners);
	const Matrix inv = view->GetGameObject()->GetTransform()->GetWorldMatrix().Invert();
	Vec2 cmin(FLT_MAX, FLT_MAX), cmax(-FLT_MAX, -FLT_MAX);
	for (const Vec3& c : corners)
	{
		const Vec3 l = Vec3::Transform(c, inv);
		cmin = Vec2::Min(cmin, Vec2(l.x, l.y));
		cmax = Vec2::Max(cmax, Vec2(l.x, l.y));
	}
	const Vec2 vmin = view->GetRectMin(), vsize = view->GetRectSize();
	Vec2 n(0, 0);
	for (int axis = 0; axis < 2; ++axis)
	{
		const float range = ((&cmax.x)[axis] - (&cmin.x)[axis]) - (&vsize.x)[axis];
		(&n.x)[axis] = range > 1e-4f ? std::clamp(((&vmin.x)[axis] - (&cmin.x)[axis]) / range, 0.0f, 1.0f) : 0.0f;
	}
	return n;
}

void ScrollRect::SetNormalizedPosition(const Vec2& target)
{
	RectTransform* content = ContentRect();
	if (content == nullptr)
		return;
	const Vec2 cur = GetNormalizedPosition();
	RectTransform* view = ViewportRect();
	Vec3 corners[4];
	content->GetWorldCorners(corners);
	const Matrix inv = view->GetGameObject()->GetTransform()->GetWorldMatrix().Invert();
	Vec2 cmin(FLT_MAX, FLT_MAX), cmax(-FLT_MAX, -FLT_MAX);
	for (const Vec3& c : corners)
	{
		const Vec3 l = Vec3::Transform(c, inv);
		cmin = Vec2::Min(cmin, Vec2(l.x, l.y));
		cmax = Vec2::Max(cmax, Vec2(l.x, l.y));
	}
	const Vec2 range = (cmax - cmin) - view->GetRectSize();
	Vec2 pos = content->GetAnchoredPosition();
	if (m_Horizontal && range.x > 0.0f) pos.x -= (std::clamp(target.x, 0.0f, 1.0f) - cur.x) * range.x;
	if (m_Vertical && range.y > 0.0f) pos.y -= (std::clamp(target.y, 0.0f, 1.0f) - cur.y) * range.y;
	content->SetAnchoredPosition(pos);
}

void ScrollRect::OnInspectorGUI()
{
	RefField("Content", m_Content, "##srContent", "rect_transform");
	UnityGUI::Toggle("Horizontal", &m_Horizontal);
	UnityGUI::Toggle("Vertical", &m_Vertical);
	static const char* kMove[] = { "Unrestricted", "Elastic", "Clamped" };
	int mv = (int)m_Movement;
	if (UnityGUI::Dropdown("Movement Type", &mv, kMove, 3))
		m_Movement = (Movement)mv;
	if (m_Movement == Movement::Elastic)
		UnityGUI::Float("Elasticity", &m_Elasticity, 1);
	UnityGUI::Toggle("Inertia", &m_Inertia);
	if (m_Inertia)
		UnityGUI::Float("Deceleration Rate", &m_Deceleration, 1);
	UnityGUI::Float("Scroll Sensitivity", &m_Sensitivity);
	RefField("Viewport", m_Viewport, "##srViewport", "rect_transform");
	UnityGUI::ValueLabel("Horizontal Scrollbar", "None (Scrollbar)");
	UnityGUI::ValueLabel("Vertical Scrollbar", "None (Scrollbar)");
}

GENERATE_COMPONENT_FUNC_TOJSON(ScrollRect)
{
	json j;
	SERIALIZE_TYPE(j, ScrollRect);
	j["enabled"] = m_Enabled;
	j["content"] = m_Content;
	j["viewport"] = m_Viewport;
	j["horizontal"] = m_Horizontal;
	j["vertical"] = m_Vertical;
	j["movementType"] = (int)m_Movement;
	j["elasticity"] = m_Elasticity;
	j["inertia"] = m_Inertia;
	j["decelerationRate"] = m_Deceleration;
	j["scrollSensitivity"] = m_Sensitivity;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(ScrollRect)
{
	m_Enabled = j.value("enabled", true);
	m_Content = j.value("content", (uint64)0);
	m_Viewport = j.value("viewport", (uint64)0);
	m_Horizontal = j.value("horizontal", true);
	m_Vertical = j.value("vertical", true);
	m_Movement = (Movement)j.value("movementType", 1);
	m_Elasticity = j.value("elasticity", 0.1f);
	m_Inertia = j.value("inertia", true);
	m_Deceleration = j.value("decelerationRate", 0.135f);
	m_Sensitivity = j.value("scrollSensitivity", 1.0f);
}
