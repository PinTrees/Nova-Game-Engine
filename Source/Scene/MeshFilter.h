#pragma once
#include "Component.h"

class Mesh;

// Unity 의 Mesh Filter: 렌더링할 메시를 보관한다. MeshRenderer 는 같은 GameObject 의 MeshFilter 에서 메시를 가져온다.
class MeshFilter : public Component
{
private:
	shared_ptr<Mesh> m_Mesh;
	wstring m_MeshPath;          // "builtin:Cube" 또는 프로젝트 안의 fbx 경로
	int m_MeshSubsetIndex = 0;

public:
	MeshFilter();
	~MeshFilter();

	shared_ptr<Mesh> GetMesh() { return m_Mesh; }
	const wstring& GetMeshPath() const { return m_MeshPath; }
	int GetSubsetIndex() const { return m_MeshSubsetIndex; }
	void SetMesh(shared_ptr<Mesh> mesh, const wstring& path, int subsetIndex = 0)
	{
		m_Mesh = mesh;
		++s_BindingSerial;   // 컬링이 Mesh Renderer 의 메시를 다시 본다
		m_MeshPath = path;
		m_MeshSubsetIndex = subsetIndex;
	}

public:
	virtual void OnInspectorGUI() override;
	virtual bool UsesUnityInspector() const override { return true; }
	virtual bool HasEnabledToggle() const override { return false; }
	virtual const char* InspectorIconName() const override { return "mesh_filter"; }
	virtual std::string InspectorTitle() const override;

	GENERATE_COMPONENT_BODY(MeshFilter)
};

REGISTER_COMPONENT(MeshFilter)
