#include "pch.h"
#include "UIGraphic.h"
#include "UIRenderer.h"
#include "RectTransform.h"
#include "UnityGUI.h"

std::vector<UIGraphic*> UIGraphic::s_All;

UIGraphic::UIGraphic()
{
	s_All.push_back(this);
}

UIGraphic::~UIGraphic()
{
	s_All.erase(std::remove(s_All.begin(), s_All.end(), this), s_All.end());
}

RectTransform* UIGraphic::GetRect()
{
	return m_pGameObject ? m_pGameObject->GetComponent<RectTransform>() : nullptr;
}

void UIGraphic::FinalColor(float out[4]) const
{
	for (int i = 0; i < 4; ++i)
		out[i] = m_Color[i] * m_Tint[i];
}

uint32 UIGraphic::PackedColor() const
{
	float c[4];
	FinalColor(c);
	return UIRenderer::PackColor(c);
}

void UIGraphic::DrawColorAndRaycast()
{
	UnityGUI::Color("Color", m_Color);
	UnityGUI::ValueLabel("Material", "None (Material)");
	UnityGUI::Toggle("Raycast Target", &m_RaycastTarget);
	UnityGUI::Toggle("Maskable", &m_Maskable);
}

void UIGraphic::GraphicToJson(json& j) const
{
	j["enabled"] = m_Enabled;
	j["color"] = { m_Color[0], m_Color[1], m_Color[2], m_Color[3] };
	j["raycastTarget"] = m_RaycastTarget;
	j["maskable"] = m_Maskable;
}

void UIGraphic::GraphicFromJson(const json& j)
{
	m_Enabled = j.value("enabled", true);
	if (j.contains("color") && j["color"].is_array() && j["color"].size() == 4)
		for (int i = 0; i < 4; ++i)
			m_Color[i] = j["color"][i].get<float>();
	m_RaycastTarget = j.value("raycastTarget", true);
	m_Maskable = j.value("maskable", true);
}
