#pragma once

// 나중에 메타데이터파일 생성 시 프로젝트 설정에서 조정, 카메라 범위에서 렌더할 수 있는 최대 사이즈
// 정렬된 벡터 라이트들에서 최대 사이즈까지만 걸러낼 것
#define LIGHT_SIZE 4 // Light Size는 이 곳에서만 설정가능
#define LIGHT_MULTYPLE 6
#define LIGHT_MAX_SIZE (LIGHT_SIZE * LIGHT_MULTYPLE)

class Light;

class LightManager
{
	SINGLE_HEADER(LightManager)

private:
	vector<shared_ptr<Light>> m_Lights; // 씬의 모든 빛들

	int m_SortedLightSize = 0;
	int m_SortedEditorLightSize = 0;

	// Frustum Culling, 거리 기반 정렬, Type별 정렬이 완료된 Light벡터
	vector<shared_ptr<Light>> m_SortedEditorLights;
	vector<shared_ptr<Light>> m_SortedLights;

	// Frustum Culling, 거리 기반 정렬 후 Type별로 모아둔 Light벡터, RenderEffect에 넘기기 용도
	vector<DirectionalLight> m_EditorDirLights;
	vector<PointLight> m_EditorPointLights;
	vector<SpotLight> m_EditorSpotLights;

	vector<DirectionalLight> m_DirLights;
	vector<PointLight> m_PointLights;
	vector<SpotLight> m_SpotLights;
public:
	void Init();

	void SetLight(shared_ptr<Light> light) { m_Lights.push_back(light); }
	void DeleteLight(InstanceID id);

	void ViewUpdates();
	void EditorViewUpdates();

	// Camera 클래스에서 Frustum 컬링 된 라이트들만 정렬
	void SortingLights(vector<shared_ptr<Light>> cullingLights, Vec3 cameraPos);
	void SortingEditorLights(vector<shared_ptr<Light>> cullingLights, Vec3 cameraPos);

	int GetSortedLightSize() { return m_SortedLightSize; }
	int GetSortedEditorLightSize() { return m_SortedEditorLightSize; }

	vector<shared_ptr<Light>> GetLights() { return m_Lights; }
	vector<shared_ptr<Light>> GetSortedLights() { return m_SortedLights; }
	vector<shared_ptr<Light>> GetSortedEditorLights() { return m_SortedEditorLights; }

	// 타입별 Lights 반환
	vector<DirectionalLight> GetDirLights() { return m_DirLights; }
	vector<PointLight> GetPointLights() { return m_PointLights; }
	vector<SpotLight> GetSpotLights() { return m_SpotLights; }

	vector<DirectionalLight> GetEditorDirLights() { return m_EditorDirLights; }
	vector<PointLight> GetEditorPointLights() { return m_EditorPointLights; }
	vector<SpotLight> GetEditorSpotLights() { return m_EditorSpotLights; }
};

