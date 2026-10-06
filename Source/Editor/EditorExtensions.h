#pragma once
#include <functional>
#include <string>
#include <vector>

class GameObject;
class Scene;

// 패키지가 편집기에 붙이는 에셋 종류 (예: Animation 패키지의 .controller).
//  - Project 창: 아이콘 · Create 메뉴 항목 · 더블클릭
//  - Inspector: Project 에서 골랐을 때 그리는 함수
//  패키지를 내리면 UnregisterOwner(패키지 이름) 로 한꺼번에 뺀다.
namespace EditorExtensions
{
	struct AssetType
	{
		std::string Owner;            // 패키지 이름
		std::string Extension;        // ".controller" (소문자)
		std::string Icon;             // ProjectSetting/icons 이름
		std::string CreateMenu;       // Project 창 Create 메뉴 이름 (빈 문자열 = 없음)
		std::string DefaultName;      // 새 파일 이름 ("New Animator Controller")
		std::string DragPayload;      // 끌어 놓기 종류 ("CONTROLLER_FILE", 빈 문자열 = 기본 ASSET_FILE)
		std::function<void(const std::string& relPath)> Create;      // 새 파일을 만든다 (Assets\...)
		std::function<void(const std::string& relPath)> Open;        // 더블클릭
		std::function<void(const std::string& relPath)> Inspector;   // Project 에서 골랐을 때
	};

	NOVA_API void RegisterAssetType(const AssetType& type);
	NOVA_API void UnregisterOwner(const std::string& owner);
	NOVA_API const AssetType* FindAssetType(const std::string& extension);   // 대소문자 무시
	NOVA_API std::vector<const AssetType*> AssetTypes();

	// Scene 뷰 도구 (예: Tilemap 패키지의 Tile Palette 붓). Scene 뷰가 매 프레임 지형 · 물 도구 옆에서 부른다
	struct SceneViewContext
	{
		Matrix View, Proj;
		Vec3 RayOrigin, RayDir;   // 마우스 광선 (월드, 방향은 단위 길이)
		float RayLength = 0.0f;   // 먼 평면까지
		bool RayValid = false;    // 창이 막 열린 첫 프레임 등 카메라 행렬이 아직 유효하지 않으면 false
		ImVec2 ViewMin, ViewMax;  // Scene 뷰 그림 영역 (화면 픽셀)
		bool Hovered = false;     // 마우스가 Scene 뷰 위 (도구 팔레트 · 오버레이 제외)
	};
	// true 를 돌려주면 이번 프레임 이동 핸들 · 클릭 선택을 쉰다 (도구가 마우스를 쓴다)
	using SceneTool = std::function<bool(const SceneViewContext&)>;
	NOVA_API void RegisterSceneTool(const std::string& owner, SceneTool tool);
	bool RunSceneTools(const SceneViewContext& context);

	// GameObject 메뉴 (Hierarchy + 와 GameObject 메뉴) 의 항목. Path = "2D Object/Tilemap/Rectangular"
	//  Create(scene, parent) 가 오브젝트를 만들어 씬에 넣고 (parent 가 있으면 그 아래) 고를 오브젝트를 돌려준다
	struct CreateMenuItem
	{
		std::string Owner;
		std::string Path;
		std::function<GameObject*(Scene* scene, GameObject* parent)> Create;
	};
	NOVA_API void RegisterCreateMenu(const CreateMenuItem& item);
	std::vector<const CreateMenuItem*> CreateMenuItems(const std::string& folder);   // folder = "2D Object/Tilemap" → 그 바로 아래 항목
}
