#include "pch.h"
#include "LightManager.h"
#include "RenderManager.h"

SINGLE_BODY(LightManager)

namespace
{
	// Forward+: from ë²ˆì§¸ë¶€í„°ì˜ ì ê´‘ Â· ìŠ¤í¬íŠ¸ê´‘ì„ í´ëŸ¬ìŠ¤í„° ë¹›ìœ¼ë¡œ (ê·¸ë¦¼ì ì—†ìŒ)
	void ToAdditional(const vector<shared_ptr<Light>>& lights, size_t from, vector<AdditionalLight>& out)
	{
		for (size_t i = from; i < lights.size() && (int)out.size() < kMaxAdditionalLights; ++i)
		{
			const shared_ptr<Light>& l = lights[i];
			AdditionalLight a;
			a.Mask = l->GetCullingMaskBits();
			if (l->GetLightType() == LightType::Point)
			{
				const PointLight d = l->GetPointLight();
				a.Position = Vec3(d.Position.x, d.Position.y, d.Position.z);
				a.Range = d.Range;
				a.Color = Vec3(d.Diffuse.x, d.Diffuse.y, d.Diffuse.z);
				a.Type = 0;
			}
			else if (l->GetLightType() == LightType::Spot)
			{
				const SpotLight d = l->GetSpotLight();
				a.Position = Vec3(d.Position.x, d.Position.y, d.Position.z);
				a.Range = d.Range;
				a.Color = Vec3(d.Diffuse.x, d.Diffuse.y, d.Diffuse.z);
				a.Type = 1;
				a.Direction = Vec3(d.Direction.x, d.Direction.y, d.Direction.z);
				a.SpotAngle = d.Spot;
			}
			else
				continue;
			out.push_back(a);
		}
	}

	vector<shared_ptr<Light>> NonDirectional(const vector<shared_ptr<Light>>& lights)
	{
		vector<shared_ptr<Light>> out;
		for (const auto& l : lights)
			if (LightType::Directional != l->GetLightType())
				out.push_back(l);
		return out;
	}
}

LightManager::LightManager()
{

}

LightManager::~LightManager()
{

}

void LightManager::Init()
{

}

void LightManager::DeleteLight(InstanceID id)
{
	for (int i = 0; i < m_Lights.size(); i++)
	{
		if (m_Lights[i]->GetInstanceID() == id)
			m_Lights.erase(m_Lights.begin() + i);
	}
}

void LightManager::ViewUpdates()
{
	for (auto& light : m_Lights)
	{
		light->ViewUpdate();
	}
}

void LightManager::EditorViewUpdates()
{
	for (auto& light : m_Lights)
	{
		light->EditorViewUpdate();
	}
}

void LightManager::SortingLights(vector<shared_ptr<Light>> cullingLights, Vec3 cameraPos)
{
	m_DirLights.clear();
	m_PointLights.clear();
	m_SpotLights.clear();
	m_SortedLights.clear();
	m_AdditionalLights.clear();

	// Directional Light´Â ¸Ç ¾ÕÀ¸·Î Á¤·Ä
	for (const auto& light : cullingLights)
	{
		if (LightType::Directional == light->GetLightType())
		{
			m_SortedLights.insert(m_SortedLights.begin(), light);
		}
	}

	if (m_SortedLights.size() >= LIGHT_SIZE) // DirLight°¡ Á¦ÇÑ ¼ö¸¦ ³ÑÀ¸¸é Á¦ÇÑ ¼ö·Î ÁÙÀÌ°í ³ª¸ÓÁö ÄÚµå´Â ¿¬»êX
	{
		m_SortedLights.resize(LIGHT_SIZE);
		m_SortedLightSize = LIGHT_SIZE;
		for (const auto& light : m_SortedLights)
		{
			m_DirLights.push_back(light->GetDirLight());
		}
		ToAdditional(NonDirectional(cullingLights), 0, m_AdditionalLights);   // ë°©í–¥ê´‘ìœ¼ë¡œ ë‹¤ ì°¼ë‹¤: ì ê´‘ Â· ìŠ¤í¬íŠ¸ê´‘ì€ ëª¨ë‘ í´ëŸ¬ìŠ¤í„°ë¡œ
		return;
	}

	if (!m_SortedLights.empty()) // DirLight°¡ ÀÖÀ¸¸é
	{
		for (const auto& light : m_SortedLights)
		{
			m_DirLights.push_back(light->GetDirLight());
		}
	}

	// Point ¹× Spot Light´Â °Å¸® ±â¹İÀ¸·Î Á¤·Ä
	vector<shared_ptr<Light>> pointAndSpotLights;
	for (const auto& light : cullingLights)
	{
		if (LightType::Directional != light->GetLightType())
		{
			pointAndSpotLights.push_back(light);
		}
	}

	// °Å¸® ±â¹İ Á¤·Ä: °Å¸® - ¹üÀ§·Î Á¤·Ä
	std::sort(pointAndSpotLights.begin(), pointAndSpotLights.end(),
		[&cameraPos](const std::shared_ptr<Light>& a, const std::shared_ptr<Light>& b)
		{
			Vec3 posA = a->GetGameObject()->GetTransform()->GetPosition();
			Vec3 posB = b->GetGameObject()->GetTransform()->GetPosition();
			float distanceA = XMVectorGetX(XMVector3Length(XMVectorSubtract(cameraPos, posA))) - a->GetRange();
			float distanceB = XMVectorGetX(XMVector3Length(XMVectorSubtract(cameraPos, posB))) - b->GetRange();
			return distanceA < distanceB; // °Å¸®°¡ ÂªÀº ¼ø¼­·Î Á¤·Ä
		});

	ToAdditional(pointAndSpotLights, LIGHT_SIZE - m_SortedLights.size(), m_AdditionalLights);   // ë‚¨ëŠ” ë¹› = í´ëŸ¬ìŠ¤í„° (Forward+)
	if (pointAndSpotLights.size() >= (LIGHT_SIZE - m_SortedLights.size()))
	{
		pointAndSpotLights.resize(LIGHT_SIZE - m_SortedLights.size());
	}

	// Type º°·Î ´Ù½Ã Á¤·Ä
	vector<shared_ptr<Light>> spotLights;
	vector<shared_ptr<Light>> pointLights;

	for (const auto& light : pointAndSpotLights)
	{
		switch (light->GetLightType())
		{
		case LightType::Spot:
			spotLights.push_back(light);
			break;
		case LightType::Point:
			pointLights.push_back(light);
			break;
		default:
			break;
		}
	}

	// Á¤·ÄµÈ Point ¹× Spot Light Ãß°¡
	if (!spotLights.empty())
	{
		for (const auto& light : spotLights)
		{
			m_SpotLights.push_back(light->GetSpotLight());
		}
		m_SortedLights.insert(m_SortedLights.end(), spotLights.begin(), spotLights.end());
	}
	if (!pointLights.empty())
	{
		for (const auto& light : pointLights)
		{
			m_PointLights.push_back(light->GetPointLight());
		}
		m_SortedLights.insert(m_SortedLights.end(), pointLights.begin(), pointLights.end());
	}

	m_SortedLightSize = m_SortedLights.size();
}

void LightManager::SortingEditorLights(vector<shared_ptr<Light>> cullingLights, Vec3 cameraPos)
{
	m_EditorDirLights.clear();
	m_EditorPointLights.clear();
	m_EditorSpotLights.clear();
	m_SortedEditorLights.clear();
	m_EditorAdditionalLights.clear();

	// Directional Light´Â ¸Ç ¾ÕÀ¸·Î Á¤·Ä
	for (const auto& light : cullingLights)
	{
		if (LightType::Directional == light->GetLightType())
		{
			m_SortedEditorLights.insert(m_SortedEditorLights.begin(), light);
		}
	}

	if (m_SortedEditorLights.size() >= LIGHT_SIZE) // DirLight°¡ Á¦ÇÑ ¼ö¸¦ ³ÑÀ¸¸é Á¦ÇÑ ¼ö·Î ÁÙÀÌ°í ³ª¸ÓÁö ÄÚµå´Â ¿¬»êX
	{
		m_SortedEditorLights.resize(LIGHT_SIZE);
		m_SortedEditorLightSize = LIGHT_SIZE;
		for (const auto& light : m_SortedEditorLights)
		{
			m_EditorDirLights.push_back(light->GetDirLight());
		}
		ToAdditional(NonDirectional(cullingLights), 0, m_EditorAdditionalLights);
		return;
	}

	if (!m_SortedEditorLights.empty()) // DirLight°¡ ÀÖÀ¸¸é
	{
		for (const auto& light : m_SortedEditorLights)
		{
			m_EditorDirLights.push_back(light->GetDirLight());
		}
	}

	// Point ¹× Spot Light´Â °Å¸® ±â¹İÀ¸·Î Á¤·Ä
	vector<shared_ptr<Light>> pointAndSpotLights;
	for (const auto& light : cullingLights)
	{
		if (LightType::Directional != light->GetLightType())
		{
			pointAndSpotLights.push_back(light);
		}
	}

	// °Å¸® ±â¹İ Á¤·Ä: °Å¸® - ¹üÀ§·Î Á¤·Ä
	std::sort(pointAndSpotLights.begin(), pointAndSpotLights.end(),
		[&cameraPos](const std::shared_ptr<Light>& a, const std::shared_ptr<Light>& b)
		{
			Vec3 posA = a->GetGameObject()->GetTransform()->GetPosition();
			Vec3 posB = b->GetGameObject()->GetTransform()->GetPosition();
			float distanceA = XMVectorGetX(XMVector3Length(XMVectorSubtract(cameraPos, posA))) - a->GetRange();
			float distanceB = XMVectorGetX(XMVector3Length(XMVectorSubtract(cameraPos, posB))) - b->GetRange();
			return distanceA < distanceB; // °Å¸®°¡ ÂªÀº ¼ø¼­·Î Á¤·Ä
		});

	ToAdditional(pointAndSpotLights, LIGHT_SIZE - m_SortedEditorLights.size(), m_EditorAdditionalLights);
	if (pointAndSpotLights.size() >= (LIGHT_SIZE - m_SortedEditorLights.size()))
	{
		pointAndSpotLights.resize(LIGHT_SIZE - m_SortedEditorLights.size());
	}

	// Type º°·Î ´Ù½Ã Á¤·Ä
	vector<shared_ptr<Light>> spotLights;
	vector<shared_ptr<Light>> pointLights;

	for (const auto& light : pointAndSpotLights)
	{
		switch (light->GetLightType())
		{
		case LightType::Spot:
			spotLights.push_back(light);
			break;
		case LightType::Point:
			pointLights.push_back(light);
			break;
		default:
			break;
		}
	}

	// Á¤·ÄµÈ Point ¹× Spot Light Ãß°¡
	if (!spotLights.empty())
	{
		for (const auto& light : spotLights)
		{
			m_EditorSpotLights.push_back(light->GetSpotLight());
		}
		m_SortedEditorLights.insert(m_SortedEditorLights.end(), spotLights.begin(), spotLights.end());
	}
	if (!pointLights.empty())
	{
		for (const auto& light : pointLights)
		{
			m_EditorPointLights.push_back(light->GetPointLight());
		}
		m_SortedEditorLights.insert(m_SortedEditorLights.end(), pointLights.begin(), pointLights.end());
	}

	m_SortedEditorLightSize = m_SortedEditorLights.size();
}

bool LightManager::IsLit(const shared_ptr<Light>& light)
{
	if (light == nullptr || !light->IsEnabled() || light->GetGameObject() == nullptr)
		return false;
	for (GameObject* g = light->GetGameObject(); g != nullptr; g = g->GetParent())
		if (!g->IsActive())
			return false;
	return true;
}
