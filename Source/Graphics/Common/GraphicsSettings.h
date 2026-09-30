#pragma once
#include "GraphicsAPI.h"
#include <memory>

class IGraphicsBackend;

// 사용자가 선택한 렌더링 API 설정 (ProjectSetting/GraphicsSettings.json 에 저장).
// 디바이스 생성 이전에 필요하므로 다른 매니저(PathManager 등)와 독립적으로 동작한다.
class GraphicsSettings
{
public:
	// 파일에서 요청 API를 읽고 백엔드를 생성한다. 지원되지 않으면 DirectX11로 대체한다.
	static void Init();

	static GraphicsAPI GetRequestedAPI() { return s_Requested; }
	static void SetRequestedAPI(GraphicsAPI api);   // 저장만 하며 다음 실행부터 적용된다.

	static IGraphicsBackend* GetBackend() { return s_Backend.get(); }
	static GraphicsAPI GetActiveAPI();

private:
	static GraphicsAPI s_Requested;
	static std::unique_ptr<IGraphicsBackend> s_Backend;
};
