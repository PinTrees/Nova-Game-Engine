#pragma once
#include "Component.h"

enum class ProjectionType
{
	Perspective, // ���� ����
	Orthographic, // ���� ����
	End
};

class NOVA_API Camera : public Component
{
private:
	ProjectionType m_cameraType = ProjectionType::Perspective;
	// Camera coordinate system with coordinates relative to world space.
	XMFLOAT3 _right = { 1, 0, 0 };
	XMFLOAT3 _up = { 0, 1, 0 };
	XMFLOAT3 _look = { 0, 0, 1 };

	// Cache frustum properties.
	float m_nearZ;
	float m_farZ;
	float m_aspect;
	float m_fovY;

	float m_nearWindowHeight;
	float m_farWindowHeight;

	// Cache View/Proj matrices.
	XMFLOAT4X4 _view;
	XMFLOAT4X4 _proj;

	Frustum m_Frustum;

	// Unity(URP) 카메라 Inspector 항목. Projection/FOV/Size/Clipping/Background 는 실제로 동작하고,
	// Post Processing / Anti-aliasing(FXAA) / Stop NaNs / Dithering 은 Volume 후처리 패스가 쓴다.
	// 나머지(Stack, Volume Mask, Output 등)는 값 저장만 하는 UI 용 항목이다.
	float m_orthoSize = 5.0f;
	bool  m_fovVerticalAxis = true;
	bool  m_physicalCamera = false;
	int   m_renderType = 0;          // Base / Overlay
	int   m_renderer = 0;
	bool  m_postProcessing = true;   // 새 카메라는 켬 (Unity URP 템플릿의 Main Camera 와 같음)
	int   m_antiAliasing = 0;          // 0 없음, 1 FXAA, 2 SMAA, 3 TAA (URP 와 같은 순서)
	int   m_smaaQuality = 2;           // Low · Medium · High
	int   m_taaQuality = 3;            // Very Low · Low · Medium · High · Very High
	float m_taaBaseBlend = 0.875f;     // Base Blend Factor (히스토리 비중)
	float m_taaJitterScale = 1.0f;     // Jitter Scale
	float m_taaVarianceClamp = 0.9f;   // Variance Clamp Scale
	float m_taaSharpening = 0.0f;      // Contrast Adaptive Sharpening
	bool  m_stopNaNs = false;
	bool  m_dithering = false;
	bool  m_renderShadows = true;
	int   m_priority = 0;
	int   m_opaqueTexture = 2;       // 0 Off, 1 On, 2 Use settings from Render Pipeline Asset
	int   m_depthTexture = 2;
	uint32 m_cullingMask = 0xFFFFFFFFu;   // Unity Camera.cullingMask: 그리는 레이어 (비트 = 레이어)
	bool  m_occlusionCulling = true;
	int   m_backgroundType = 0;      // 0 Skybox, 1 Solid Color, 2 Uninitialized
	float m_backgroundColor[4] = { 49.0f / 255.0f, 77.0f / 255.0f, 121.0f / 255.0f, 1.0f };
	int   m_volumeUpdateMode = 0;
	int   m_volumeMask = 0;
	int   m_targetDisplay = 0;
	int   m_targetEye = 0;
	float m_viewportRect[4] = { 0.0f, 0.0f, 1.0f, 1.0f };   // x, y, w, h

	void FrustumUpdate(); // frustum ���� ���
	void GetFrustumCulling();
	void ProjUpdate();
	string GetStringCameraType(ProjectionType type);
	//::XMMatrixPerspectiveFovLH
	//::XMMatrixOrthographicLH
public:
	Camera();
	~Camera();

	void ViewUpdate();

	// Get/Set world camera position.
	XMVECTOR GetPositionXM();
	XMFLOAT3 GetPosition()const;

	// Get camera basis vectors.
	XMVECTOR GetRightXM()const;
	XMFLOAT3 GetRight()const;
	XMVECTOR GetUpXM()const;
	XMFLOAT3 GetUp()const;
	XMVECTOR GetLookXM()const;
	XMFLOAT3 GetLook()const;

	// Get frustum properties.
	float GetNearZ()const;
	float GetFarZ()const;
	float GetAspect()const;
	float GetFovY()const;
	float GetFovX()const;

	void SetFovY(float fovY) { m_fovY = fovY; ProjUpdate(); }
	void SetAspect(float aspect) { if (aspect > 1e-4f && std::isfinite(aspect)) { m_aspect = aspect; ProjUpdate(); } }
	void SetNearZ(float nearZ) { m_nearZ = nearZ; ProjUpdate(); }
	void SetFarZ(float farZ) { m_farZ = farZ; ProjUpdate(); }

	// Get near and far plane dimensions in view space coordinates.
	float GetNearWindowWidth()const;
	float GetNearWindowHeight()const;
	float GetFarWindowWidth()const;
	float GetFarWindowHeight()const;

	// Set frustum.
	void SetLens(float fovY, float aspect, float zn, float zf);

	// Define camera space via LookAt parameters.
	void LookAt(FXMVECTOR pos, FXMVECTOR target, FXMVECTOR worldUp);
	void LookAt(const XMFLOAT3& pos, const XMFLOAT3& target, const XMFLOAT3& up);

	// Get View/Proj matrices.
	XMMATRIX View()const;
	XMMATRIX Proj()const;
	XMMATRIX ViewProj()const;

	// Strafe/Walk the camera a distance d.
	void Strafe(float d);
	void Walk(float d);

	// Rotate the camera.
	void Pitch(float angle);
	void RotateY(float angle);

	// After modifying camera position/orientation, call to rebuild the view matrix.
	void UpdateViewMatrix();

public:
	virtual void Update() override;
	virtual void LateUpdate() override;
	
	virtual void OnInspectorGUI() override;
	virtual bool UsesUnityInspector() const override { return true; }

	// 배경 (Game 뷰: 0 Skybox 면 하늘을 그리고, 1 Solid Color 면 그 색으로 지운다)
	bool UsesSolidBackground() const { return m_backgroundType == 1; }
	int  GetBackgroundType() const { return m_backgroundType; }
	bool PostProcessingEnabled() const { return m_postProcessing; }
	int AntiAliasingMode() const { return m_antiAliasing; }   // 0 없음, 1 FXAA, 2 SMAA, 3 TAA
	int SmaaQuality() const { return m_smaaQuality; }
	int TaaQuality() const { return m_taaQuality; }
	float TaaBaseBlend() const { return m_taaBaseBlend; }
	float TaaJitterScale() const { return m_taaJitterScale; }
	float TaaVarianceClamp() const { return m_taaVarianceClamp; }
	float TaaSharpening() const { return m_taaSharpening; }
	bool DitheringEnabled() const { return m_dithering; }
	bool StopNaNsEnabled() const { return m_stopNaNs; }
	int GetTargetDisplay() const { return m_targetDisplay; }   // 0 = Display 1
	int GetPriority() const { return m_priority; }
	const float* GetBackgroundColor() const { return m_backgroundColor; }
	float GetOrthoSize() const { return m_orthoSize; }
	uint32 GetCullingMask() const { return m_cullingMask; }
	void SetCullingMask(uint32 m) { m_cullingMask = m; }
	bool UsesOcclusionCulling() const { return m_occlusionCulling; }   // Unity Camera.useOcclusionCulling (NOVA = 굽기 없는 GPU Hi-Z)
	bool IsOrthographic() const { return m_cameraType == ProjectionType::Orthographic; }
	virtual void OnDrawGizmos() override;

	GENERATE_COMPONENT_BODY(Camera)
};

REGISTER_COMPONENT(Camera)
