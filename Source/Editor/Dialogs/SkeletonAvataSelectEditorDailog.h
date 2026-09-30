#pragma once
#include "EditorDailog.h"  
#include <filesystem> 

namespace fs = std::filesystem;

class MeshFile;
class SkeletonAvataData;

class SkeletonAvataSelectEditorDailog : public EditorDialog
{
public:
	vector<shared_ptr<MeshFile>> m_MeshFiles;

	// Runtime Value
	shared_ptr<SkeletonAvataData>&	m_SelectSkeletonAvata;
	string&							m_SelectSkeletonAvataFilePath;
	int&							m_SelectSkeletonAvataIndex;

	// System Value
	shared_ptr<SkeletonAvataData>	m_SystemSelectSkeletonAvata;
	string							m_SystemSelectSkeletonAvataFilePath;
	int								m_SystemSelectSkeletonAvataIndex;

public:
	SkeletonAvataSelectEditorDailog(
		shared_ptr<SkeletonAvataData>&	selectSkeletonAvata,
		string&							selectSkeletonAvataFilePath,
		int&							selectSkeletonAvataIndex);
	~SkeletonAvataSelectEditorDailog();

	static void Open(
		shared_ptr<SkeletonAvataData>&	selectSkeletonAvata,
		string&							selectSkeletonAvataFilePath,
		int&							selectSkeletonAvataIndex);

	virtual void Show() override;
	virtual void Close() override;
	virtual void OnRender() override;


private:
	// Custom Imgui Render
	bool RenderSkeletonAvataEntry(const shared_ptr<SkeletonAvataData>& skeletonAvata); 
};

