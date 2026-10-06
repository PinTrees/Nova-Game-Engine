#pragma once
#include <string>

class Tilemap;
class Scene;
class GameObject;

// nova tilemap <op> [--인자 …] — 창 · Scene 뷰 붓과 같은 함수
namespace TilemapOps
{
	nlohmann::json Help();
	bool Run(const std::string& op, const nlohmann::json& args, nlohmann::json& result, std::string& error);
	// GameObject > 2D Object > Tilemap > Rectangular: Grid + 자식 Tilemap (Tilemap · Tilemap Renderer).
	//  parent 에 Grid 가 있으면 그 아래 Tilemap 만. 돌려주는 것 = 새 Tilemap 오브젝트
	GameObject* CreateRectangular(Scene* scene, GameObject* parent, const std::string& name = "Tilemap");
}
