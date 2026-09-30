#pragma once
#include "EditorWindow.h"

class InspectorEditorWindow
	: public EditorWindow
{
public:
	InspectorEditorWindow();
	~InspectorEditorWindow();

public:
	virtual void OnRender() override;
};

