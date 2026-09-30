#pragma once
#include "Component.h"

enum class ProjectionType
{
	Perspective, // 원근 투영
	Orthographic, // 직교 투영
	End
};

class Camera : public Component
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

	void FrustumUpdate(); // frustum 범위 계산
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
	void SetAspect(float aspect) { m_aspect = aspect; ProjUpdate(); }
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
	virtual void OnDrawGizmos() override;

	GENERATE_COMPONENT_BODY(Camera)
};

REGISTER_COMPONENT(Camera)
