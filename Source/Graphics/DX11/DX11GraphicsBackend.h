#pragma once
#include "IGraphicsBackend.h"

// Direct3D 11 백엔드. 현재 엔진의 모든 렌더링 코드가 이 API를 직접 사용한다.
class DX11GraphicsBackend : public IGraphicsBackend
{
public:
	GraphicsAPI GetAPI() const override { return GraphicsAPI::DirectX11; }
	const char* GetName() const override { return "DirectX 11"; }
	bool IsSupported() const override { return true; }
};
