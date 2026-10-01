#pragma once
#include "GraphicsAPI.h"
#include <memory>
#include <string>
#include <vector>

class IGraphicsBackend;

// 이번 실행이 쓸 그래픽 API 고르기 (디바이스 만들기 전, 다른 매니저와 독립적으로).
//  우선순위: 실행 인자(-force-d3d11 / -force-opengl) → Hub 고정 → 플레이어 = player.json 의 graphicsAPIs 순서
//            → 에디터 = 에디터 설정(Preferences > Graphics, ../ProjectSetting/GraphicsSettings.json).
//  목록에서 이 PC·이 빌드가 쓸 수 있는 첫 API 를 쓰고, 하나도 없으면 DirectX 11. 고른 이유는 SelectionLog().
class GraphicsSettings
{
public:
	// playerList: 플레이어면 player.json 의 목록 (비면 에디터 설정을 쓴다)
	static void Init(const std::vector<GraphicsAPI>& playerList = {});
	static void ForceForThisProcess(GraphicsAPI api);   // Init 전에 (Hub = DirectX 11)

	static GraphicsAPI GetEditorAPI();                   // 에디터 설정 (저장된 값)
	static void SetEditorAPI(GraphicsAPI api);           // 저장만, 다음 실행부터
	static GraphicsAPI GetRequestedAPI() { return s_Requested; }   // 이번 실행에 처음 원한 API
	static IGraphicsBackend* GetBackend() { return s_Backend.get(); }
	static GraphicsAPI GetActiveAPI();
	static const std::string& SelectionLog() { return s_Log; }

	// 이 빌드에서 쓸 수 있는지 (안 되면 이유)
	static bool IsSupported(GraphicsAPI api, std::string* reason = nullptr);
	static bool IsExperimental(GraphicsAPI api);
	// 고른 API 로 장치를 만들지 못했을 때 (예: OpenGL 4.5 드라이버 없음) → 이 API 로 바꾸고 기록에 이유를 남긴다
	static void FallBack(GraphicsAPI api, const std::string& why);
	static std::vector<GraphicsAPI> AllAPIs();

	// 하위 호환 (예전 메뉴)
	static void SetRequestedAPI(GraphicsAPI api) { SetEditorAPI(api); }

private:
	static GraphicsAPI s_Requested;
	static std::unique_ptr<IGraphicsBackend> s_Backend;
	static std::string s_Log;
};
