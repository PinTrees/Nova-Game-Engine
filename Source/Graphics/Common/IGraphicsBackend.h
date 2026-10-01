#pragma once
#include "GraphicsAPI.h"

// 렌더링 API 별 백엔드가 구현하는 공통 인터페이스.
// 1단계: 백엔드 식별/지원 여부만 다룬다. (디바이스/버퍼/텍스처/셰이더 RHI 추상화는 후속 단계)
class IGraphicsBackend
{
public:
	virtual ~IGraphicsBackend() = default;

	virtual GraphicsAPI GetAPI() const = 0;
	virtual const char* GetName() const = 0;

	// 현재 빌드에서 사용 가능한지
	virtual bool IsSupported() const = 0;
	virtual const char* GetUnsupportedReason() const { return ""; }
	// 쓸 수는 있지만 아직 시험 단계 (설정 UI 에 표시)
	virtual bool IsExperimental() const { return false; }
};
