#pragma once

// 엔진(NovaCore.dll)이 패키지 DLL 에 내보내는 클래스·함수 표시.
//  엔진 빌드(NOVA_ENGINE_BUILD) = dllexport, 패키지 빌드 = dllimport.
//  패키지는 같은 엔진 버전 · 같은 구성(Debug/Release) · 같은 컴파일러(/MD)로 빌드해야 한다 (NOVA_PACKAGE_ABI 로 확인).
#if defined(NOVA_ENGINE_BUILD)
#define NOVA_API __declspec(dllexport)
#else
#define NOVA_API __declspec(dllimport)
#endif

// 패키지 DLL 이 내보내는 진입점 (extern "C")
#define NOVA_PACKAGE_EXPORT extern "C" __declspec(dllexport)

// 패키지와 엔진이 맞는지 보는 문자열: 엔진 버전 + 구성 + 툴셋. 패키지 DLL 의 NovaPackage_Abi() 가 같은 값을 돌려줘야 불러온다
#define NOVA_PACKAGE_ABI_VERSION "nova-0.1-msvc143-" NOVA_PACKAGE_ABI_CONFIG
#if defined(_DEBUG)
#define NOVA_PACKAGE_ABI_CONFIG "debug"
#else
#define NOVA_PACKAGE_ABI_CONFIG "release"
#endif
