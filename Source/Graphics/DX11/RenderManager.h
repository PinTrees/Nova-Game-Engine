#pragma once

class ShadowMap;

class RenderManager
{
	SINGLE_HEADER(RenderManager)

public:
	// Common
	shared_ptr<class ShadowMap> BaseShadowMap;
	shared_ptr<class ShadowMap> EditorShadowMap;

	// Editor Only
	XMMATRIX EditorCameraViewMatrix;
	XMMATRIX EditorCameraProjectionMatrix;
	XMMATRIX EditorCameraViewProjectionMatrix;
	D3D11_VIEWPORT EditorViewport;

	// Game Only
	XMMATRIX CameraViewMatrix;
	XMMATRIX CameraProjectionMatrix;
	XMMATRIX CameraViewProjectionMatrix;
	D3D11_VIEWPORT Viewport; 

	// ShaodwTransform -> VPT, ·»´õ¸¶´Ù clear ÇÊ¼ö
	vector<XMMATRIX> ShadowTransformArray[(uint32)LightType::End];
	vector<XMMATRIX> EditorShadowTransformArray[(uint32)LightType::End];
	XMMATRIX LightViewProjection; // shadowMap Render¿ë º¯¼ö, Editor and Game µÑ ´Ù »ç¿ë

	// Editor Only
public:
	static const int SMapSize = 2048;

	bool WireFrameMode = false;
	bool InstancingMode = false;
	// ì§€ê¸ˆ ê·¸ë¦¬ëŠ” í™”ë©´ì´ Scene ë·°(ì—ë””í„° ì¹´ë©”ë¼)ì¸ì§€ Game ë·°ì¸ì§€ (ê·¸ë¦¼ì íŒ¨ìŠ¤ì—ì„œ ì§€í˜• LOD ê¸°ì¤€ ì¹´ë©”ë¼ë¥¼ ê³ ë¥¼ ë•Œ)
	bool RenderingEditorView = false;
public:
	void Init();

	// Editor Only
public:
	void SetEditorViewport(UINT width, UINT height);
	void SetViewport(UINT width, UINT height);
};

