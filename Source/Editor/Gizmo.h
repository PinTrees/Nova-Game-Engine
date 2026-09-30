#pragma once

class Transform;

class Gizmo
{
public:
	static bool isDragging; 
	static ImVec2 lastMousePos; 
	static int activeAxis;

public:
	static void DrawVector(const Matrix worldMatrix, const Vec3 vector);
	static void DrawCube(const XMMATRIX& worldMatrix, const Vec3& size);
	static void DrawSphere(const XMMATRIX& worldMatrix, float radius);
	static void DrawArrow(Vector3 position, Vec3 dir, ImVec4 color=ImVec4(255, 0, 0, 255));
	static void DrawFrustum(const XMMATRIX& worldMatrix, float _near, float _far, float fieldOfView); 
	static void DrawTransformHandler(Transform* transform);

private:
	static bool IsMouseOverHandle(const ImVec2& handlePos);
	static void DrawAxisHandle(const ImVec2& pos, const ImVec2& dir, const ImVec4& color);
	static void DrawwPoinVector(const Vec2& pos, const Vec2& dir, const ImVec4& color);
};

