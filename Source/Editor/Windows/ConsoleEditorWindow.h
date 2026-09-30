#pragma once
#include "EditorWindow.h"

class ConsoleEditorWindow
	: public EditorWindow
{
public: 
	ConsoleEditorWindow();
	~ConsoleEditorWindow();

public:
	virtual void PushStyle() override;
	virtual void PopStyle() override;
	virtual void OnRender() override;
};

