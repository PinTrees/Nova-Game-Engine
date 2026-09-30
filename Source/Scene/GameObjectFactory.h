#pragma once
#include <string>
#include <memory>
#include <map>

class GameObject;
class Mesh;

enum class PrimitiveType
{
	Cube,
	Sphere,
	Cylinder,
	Plane
};

class GameObjectFactory
{
public:
	static std::shared_ptr<Mesh> GetPrimitiveMesh(PrimitiveType type);

	static GameObject* CreateEmpty(const std::string& name = "GameObject");
	static GameObject* CreateCube(const std::string& name = "Cube");
	static GameObject* CreateSphere(const std::string& name = "Sphere");
	static GameObject* CreateCylinder(const std::string& name = "Cylinder");
	static GameObject* CreatePlane(const std::string& name = "Plane");

	static GameObject* CreateDirectionalLight(const std::string& name = "Directional Light");
	static GameObject* CreatePointLight(const std::string& name = "Point Light");
	static GameObject* CreateSpotLight(const std::string& name = "Spot Light");
	static GameObject* CreateCamera(const std::string& name = "Main Camera");

private:
	static std::map<PrimitiveType, std::shared_ptr<Mesh>> s_PrimitiveMeshes;
};
