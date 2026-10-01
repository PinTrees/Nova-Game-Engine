#pragma once
#include "EditorWindow.h"

class EditorCamera;

class SceneEditorWindow
	: public EditorWindow
{
private:
	// DirectX 11 ·»´õ Å¸°Ù ¹× ºäÆ÷Æ® ¼³Á¤
	GfxTexture2D* renderTargetTexture = nullptr;
	GfxRenderTargetView* renderTargetView = nullptr;
	GfxShaderResourceView* shaderResourceView = nullptr;

	GfxRenderTargetView* oldRenderTarget;

	// Ã¢ Å©±â ÀúÀå
	UINT windowWidth;
	UINT windowHeight;

	EditorCamera* m_Camera;

public:
	SceneEditorWindow();
	~SceneEditorWindow();

public:
	EditorCamera* GetSceneCamera() { return m_Camera; }
	GfxRenderTargetView* GetRenderTargetView() const { return renderTargetView; }
	GfxTexture2D* GetRenderTexture() const { return renderTargetTexture; }   // NOVA CLI screenshot  

public:
	virtual void Update() override;

protected:

	virtual void PushStyle() override;
	virtual void PopStyle() override;
	virtual void OnRender() override;

private:
	void InitRenderTarget(UINT width, UINT height);
	void CleanUpRenderTarget();
	void RenderScene();
};

