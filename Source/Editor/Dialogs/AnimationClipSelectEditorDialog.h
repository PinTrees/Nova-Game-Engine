#pragma once
#include "EditorDailog.h"  
#include <filesystem> 

namespace fs = std::filesystem;

class AnimationClipSelectEditorDialog : public EditorDialog
{
public:
	vector<shared_ptr<MeshFile>> m_MeshFiles; 

	// Runtime Value
	shared_ptr<AnimationClip>&	m_SelectAnimationClip;
	string&						m_SelectAnimationClipFilePath;
	int&						m_SelectAnimationClipIndex; 

	// System Value
	shared_ptr<AnimationClip>	m_SystemSelectAnimationClip;
	string						m_SystemSelectAnimationClipFilePath;
	int							m_SystemSelectAnimationClipIndex;

public:
	AnimationClipSelectEditorDialog(
		shared_ptr<AnimationClip>& selectAnimationClip,
		string& selectAnimationClipFilePath,
		int& selectAnimationClipIndex);
	~AnimationClipSelectEditorDialog();

	static void Open(
		shared_ptr<AnimationClip>& selectAnimationClip,
		string& selectAnimationClipFilePath,
		int& selectAnimationClipIndex);

	virtual void Show() override;
	virtual void Close() override;
	virtual void OnRender() override;

private:
	// Custom Imgui Render
	bool RenderAnimationClipEntry(const shared_ptr<AnimationClip>& selectAnimationClip);  
};

