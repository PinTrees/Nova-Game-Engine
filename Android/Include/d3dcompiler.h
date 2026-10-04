#pragma once
// 안드로이드: 셰이더 컴파일러가 없다 (셰이더는 PC 에서 미리 GLSL ES 로). ShaderCache 선언이 컴파일되도록 ID3DBlob 모양만
#include "WinCompat.h"
struct ID3DBlob : IUnknown
{
	virtual void* GetBufferPointer() = 0;
	virtual SIZE_T GetBufferSize() = 0;
};
typedef ID3DBlob ID3D10Blob;
enum D3D_INCLUDE_TYPE { D3D_INCLUDE_LOCAL = 0, D3D_INCLUDE_SYSTEM = 1 };
