# NOVA 안드로이드 네이티브 라이브러리 (libnova.so — NativeActivity). NDK 툴체인으로 빌드한다 (Android/build.py).
#  엔진 소스는 Source/ 에서 필요한 것만 골라 넣는다. 엔진의 #include "pch.h" 는 Android/Include/pch.h 가 받는다
#  (Windows · D3D11 타입은 WinCompat.h, DirectXMath 는 ThirdParty/DirectXMath)
cmake_minimum_required(VERSION 3.22)
project(NovaAndroid CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

set(NOVA_ANDROID_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/AndroidMain.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/GLESRhi.cpp"
    # 엔진 (API 와 상관없는 것)
    "${ROOT}/Source/Graphics/RHI/Rhi.cpp"
    "${ROOT}/Source/Graphics/RHI/RhiTest.cpp"
    "${ROOT}/Source/Graphics/Common/GeometryGenerator.cpp"
    "${ROOT}/Source/Graphics/Common/FxStates.cpp"
    "${ROOT}/Source/Graphics/ShaderCross/ShaderCrossJson.cpp"
    "${ROOT}/Source/Graphics/ShaderCross/FxParser.cpp"
    "${ROOT}/Source/Math/MathHelper.cpp"
)

add_library(nova SHARED ${NOVA_ANDROID_SOURCES})
target_include_directories(nova PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Include"          # pch.h · Windows.h · PathManager.h 대체 (엔진 폴더보다 먼저)
    "${ROOT}/ThirdParty/DirectXMath/Inc"
    "${ROOT}/Libraries/Include"                    # nlohmann
    "${ROOT}/Source/Graphics/RHI"
    "${ROOT}/Source/Graphics/Common"
    "${ROOT}/Source/Graphics/ShaderCross"
    "${ROOT}/Source/Math"
)
target_compile_definitions(nova PRIVATE NOVA_ANDROID=1)
target_compile_options(nova PRIVATE -fexceptions -frtti -Wno-unused-parameter -Wno-unknown-pragmas -Wno-switch -Wno-deprecated-declarations
    -Wno-nonportable-include-path -Wno-microsoft-include)
target_link_libraries(nova PRIVATE android log EGL GLESv3)
# NativeActivity 가 찾는 진입점을 지우지 않게
target_link_options(nova PRIVATE "-Wl,-u,ANativeActivity_onCreate")
