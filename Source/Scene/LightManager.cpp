#include "pch.h"
#include "LightManager.h"
#include "RenderManager.h"

SINGLE_BODY(LightManager)

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

	// Directional Light는 맨 앞으로 정렬
	for (const auto& light : cullingLights)
	{
		if (LightType::Directional == light->GetLightType())
		{
			m_SortedLights.insert(m_SortedLights.begin(), light);
		}
	}

	if (m_SortedLights.size() >= LIGHT_SIZE) // DirLight가 제한 수를 넘으면 제한 수로 줄이고 나머지 코드는 연산X
	{
		m_SortedLights.resize(LIGHT_SIZE);
		m_SortedLightSize = LIGHT_SIZE;
		for (const auto& light : m_SortedLights)
		{
			m_DirLights.push_back(light->GetDirLight());
		}
		return;
	}

	if (!m_SortedLights.empty()) // DirLight가 있으면
	{
		for (const auto& light : m_SortedLights)
		{
			m_DirLights.push_back(light->GetDirLight());
		}
	}

	// Point 및 Spot Light는 거리 기반으로 정렬
	vector<shared_ptr<Light>> pointAndSpotLights;
	for (const auto& light : cullingLights)
	{
		if (LightType::Directional != light->GetLightType())
		{
			pointAndSpotLights.push_back(light);
		}
	}

	// 거리 기반 정렬: 거리 - 범위로 정렬
	std::sort(pointAndSpotLights.begin(), pointAndSpotLights.end(),
		[&cameraPos](const std::shared_ptr<Light>& a, const std::shared_ptr<Light>& b)
		{
			Vec3 posA = a->GetGameObject()->GetTransform()->GetPosition();
			Vec3 posB = b->GetGameObject()->GetTransform()->GetPosition();
			float distanceA = XMVectorGetX(XMVector3Length(XMVectorSubtract(cameraPos, posA))) - a->GetRange();
			float distanceB = XMVectorGetX(XMVector3Length(XMVectorSubtract(cameraPos, posB))) - b->GetRange();
			return distanceA < distanceB; // 거리가 짧은 순서로 정렬
		});

	if (pointAndSpotLights.size() >= (LIGHT_SIZE - m_SortedLights.size()))
	{
		pointAndSpotLights.resize(LIGHT_SIZE - m_SortedLights.size());
	}

	// Type 별로 다시 정렬
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

	// 정렬된 Point 및 Spot Light 추가
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

	// Directional Light는 맨 앞으로 정렬
	for (const auto& light : cullingLights)
	{
		if (LightType::Directional == light->GetLightType())
		{
			m_SortedEditorLights.insert(m_SortedEditorLights.begin(), light);
		}
	}

	if (m_SortedEditorLights.size() >= LIGHT_SIZE) // DirLight가 제한 수를 넘으면 제한 수로 줄이고 나머지 코드는 연산X
	{
		m_SortedEditorLights.resize(LIGHT_SIZE);
		m_SortedEditorLightSize = LIGHT_SIZE;
		for (const auto& light : m_SortedEditorLights)
		{
			m_EditorDirLights.push_back(light->GetDirLight());
		}
		return;
	}

	if (!m_SortedEditorLights.empty()) // DirLight가 있으면
	{
		for (const auto& light : m_SortedEditorLights)
		{
			m_EditorDirLights.push_back(light->GetDirLight());
		}
	}

	// Point 및 Spot Light는 거리 기반으로 정렬
	vector<shared_ptr<Light>> pointAndSpotLights;
	for (const auto& light : cullingLights)
	{
		if (LightType::Directional != light->GetLightType())
		{
			pointAndSpotLights.push_back(light);
		}
	}

	// 거리 기반 정렬: 거리 - 범위로 정렬
	std::sort(pointAndSpotLights.begin(), pointAndSpotLights.end(),
		[&cameraPos](const std::shared_ptr<Light>& a, const std::shared_ptr<Light>& b)
		{
			Vec3 posA = a->GetGameObject()->GetTransform()->GetPosition();
			Vec3 posB = b->GetGameObject()->GetTransform()->GetPosition();
			float distanceA = XMVectorGetX(XMVector3Length(XMVectorSubtract(cameraPos, posA))) - a->GetRange();
			float distanceB = XMVectorGetX(XMVector3Length(XMVectorSubtract(cameraPos, posB))) - b->GetRange();
			return distanceA < distanceB; // 거리가 짧은 순서로 정렬
		});

	if (pointAndSpotLights.size() >= (LIGHT_SIZE - m_SortedEditorLights.size()))
	{
		pointAndSpotLights.resize(LIGHT_SIZE - m_SortedEditorLights.size());
	}

	// Type 별로 다시 정렬
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

	// 정렬된 Point 및 Spot Light 추가
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