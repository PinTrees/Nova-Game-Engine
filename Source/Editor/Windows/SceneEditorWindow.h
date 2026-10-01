#pragma once
#include "EditorWindow.h"

class EditorCamera;

class SceneEditorWindow
	: public EditorWindow
{
private:
	// DirectX 11 ·»´õ Å¸°Ù ¹× ºäÆ÷Æ® ¼³Á¤
	ID3D11Texture2D* renderTargetTexture = nullptr;
	ID3D11RenderTargetView* renderTargetView = nullptr;
	ID3D11ShaderResourceView* shaderResourceView = nullptr;

	ID3D11RenderTargetView* oldRenderTarget;

	// Ã¢ Å©±â ÀúÀå
	UINT windowWidth;
	UINT windowHeight;

	EditorCamera* m_Camera;

public:
	SceneEditorWindow();
	~SceneEditorWindow();

public:
	EditorCamera* GetSceneCamera() { return m_Camera; }
	ID3D11RenderTargetView* GetRenderTargetView() const { return renderTargetView; }
	ID3D11Texture2D* GetRenderTexture() const { return renderTargetTexture; }   // NOVA CLI screenshot  

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

