#pragma once
#include "EditorWindow.h"

struct LinkInfo
{
	ed::LinkId Id;
	ed::PinId  InputId;
	ed::PinId  OutputId;
};

class AnimatorEditorWindow
	: public EditorWindow
{
private:
	ed::EditorContext* m_Context = nullptr;

	ImVector<LinkInfo>   m_Links;                
	int                  m_NextLinkId = 100;     


public:
	AnimatorEditorWindow(); 
	~AnimatorEditorWindow(); 

public:
	virtual void OnRender() override;
};

