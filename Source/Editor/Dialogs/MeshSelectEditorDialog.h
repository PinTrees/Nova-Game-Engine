#pragma once
#include "EditorDailog.h"  
#include <filesystem> 

namespace fs = std::filesystem;

enum class MESH_SELECT_DIALOG_TYPE
{
	MESH_STATIC,
	MESH_SKINNED,
};

class MeshSelectEditorDialog : public EditorDialog
{
private:
	vector<shared_ptr<MeshFile>> m_MeshFiles; 

	// Runtime Value
	shared_ptr<Mesh>&			m_SelectMesh;  
	shared_ptr<SkinnedMesh>&	m_SelectSkinnedMesh;
	wstring&					m_SelectMeshFilePath;
	int&						m_SelectSubsetIndex; 
	MESH_SELECT_DIALOG_TYPE		m_OpenMeshType;

	// System Value
	shared_ptr<Mesh>			m_SystemSelectMesh;
	shared_ptr<SkinnedMesh>		m_SystemSelectSkinnedMesh;
	wstring						m_SystemSelectMeshFilePath;
	int							m_SystemSelectMeshIndex;

public:
	MeshSelectEditorDialog(
		shared_ptr<Mesh>&			selectMesh,
		shared_ptr<SkinnedMesh>&	selectSkinnedMesh,
		wstring&					selectMeshFilePath,
		int&						selectSubsetIndex,
		MESH_SELECT_DIALOG_TYPE		openType);
	~MeshSelectEditorDialog();

	static void Open(
		shared_ptr<Mesh>&			selectMesh, 
		shared_ptr<SkinnedMesh>&	selectSkinnedMesh,
		wstring&					meshFilePath, 
		int&						meshIndex, 
		MESH_SELECT_DIALOG_TYPE		openType);

	virtual void Show() override;
	virtual void Close() override;
	virtual void OnRender() override;
};

