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

	// ShaodwTransform -> VPT, �������� clear �ʼ�
	vector<XMMATRIX> ShadowTransformArray[(uint32)LightType::End];
	vector<XMMATRIX> EditorShadowTransformArray[(uint32)LightType::End];
	XMMATRIX LightViewProjection; // shadowMap Render�� ����, Editor and Game �� �� ���
	// 지금 그리는 그림자 조각의 텍셀 하나 = 월드 몇 m (방향광 캐스케이드만, 0 = 원근 맵). 나무가 먼 캐스케이드에서 낮은 LOD 를 쓴다
	float ShadowTexelWorld = 0.0f;
	// 화면(Game / Scene 뷰) 그리기마다 1 씩 (EditorApp). 화면 하나 동안만 쓰는 목록의 유효성 검사용
	uint32 ViewSerial = 0;

	// Editor Only
public:
	static const int SMapSize = 2048;

	bool WireFrameMode = false;
	bool InstancingMode = false;
	// 지금 그리는 화면이 Scene 뷰(에디터 카메라)인지 Game 뷰인지 (그림자 패스에서 지형 LOD 기준 카메라를 고를 때)
	bool RenderingEditorView = false;
public:
	void Init();

	// Editor Only
public:
	void SetEditorViewport(UINT width, UINT height);
	void SetViewport(UINT width, UINT height);
};

