#pragma once
#include <string>
#include <memory>
#include <map>

class GameObject;
class Mesh;

// 엔진에 내장된 기본 메시(프로젝트 폴더와 무관). 씬에는 "builtin:<이름>" 경로로 저장된다.
enum class PrimitiveType
{
	Cube,
	Sphere,
	Capsule,
	Cylinder,
	Plane,
	Quad
};

class GameObjectFactory
{
public:
	static std::shared_ptr<Mesh> GetPrimitiveMesh(PrimitiveType type);

	// "builtin:Cube" 형태의 경로 <-> 기본 메시 (씬 저장/로드에 사용)
	static std::wstring GetBuiltinMeshPath(PrimitiveType type);
	static std::shared_ptr<Mesh> LoadBuiltinMesh(const std::wstring& path);   // 내장 경로가 아니면 nullptr
	static bool IsBuiltinMeshPath(const std::wstring& path);

	static GameObject* CreateEmpty(const std::string& name = "GameObject");
	static GameObject* CreateCube(const std::string& name = "Cube");
	static GameObject* CreateSphere(const std::string& name = "Sphere");
	static GameObject* CreateCapsule(const std::string& name = "Capsule");
	static GameObject* CreateCylinder(const std::string& name = "Cylinder");
	static GameObject* CreatePlane(const std::string& name = "Plane");
	static GameObject* CreateQuad(const std::string& name = "Quad");

	static GameObject* CreateDirectionalLight(const std::string& name = "Directional Light");
	static GameObject* CreatePointLight(const std::string& name = "Point Light");
	static GameObject* CreateSpotLight(const std::string& name = "Spot Light");
	static GameObject* CreateCamera(const std::string& name = "Main Camera");

private:
	static GameObject* CreatePrimitive(PrimitiveType type, const std::string& name);

public:

private:
	static std::map<PrimitiveType, std::shared_ptr<Mesh>> s_PrimitiveMeshes;
};
