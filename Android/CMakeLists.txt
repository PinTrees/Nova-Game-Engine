# NOVA 안드로이드 네이티브 라이브러리 (libnova.so — NativeActivity). NDK 툴체인으로 빌드한다 (Android/build.py).
#  - nova_engine: 엔진 런타임 소스 (Source/ 의 Windows 와 같은 파일) + Windows 전용 파일의 안드로이드 판 (Android/Source/Engine)
#  - nova: 진입점 · 플랫폼 (AndroidMain · AndroidPlatform)
#  엔진의 #include "pch.h" 는 Android/Include/pch.h 가 받는다 (Windows · D3D11 타입은 WinCompat.h, DirectXMath 는 ThirdParty/DirectXMath)
cmake_minimum_required(VERSION 3.22)
project(NovaAndroid C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

# ---- 제삼자 (Windows 빌드와 같은 소스)
set(CPP_RTTI_ENABLED ON CACHE BOOL "" FORCE)
set(CPP_EXCEPTIONS_ENABLED ON CACHE BOOL "" FORCE)
set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(PROFILER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(JPH_USE_DX12 OFF CACHE BOOL "" FORCE)
set(JPH_USE_VK OFF CACHE BOOL "" FORCE)
set(JPH_USE_MTL OFF CACHE BOOL "" FORCE)
set(JPH_USE_CPU_COMPUTE OFF CACHE BOOL "" FORCE)
set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
add_subdirectory("${ROOT}/ThirdParty/JoltPhysics/Build" "${CMAKE_BINARY_DIR}/JoltPhysics")

file(GLOB BOX2D_SOURCES "${ROOT}/ThirdParty/box2d/src/*.c")
add_library(box2d STATIC ${BOX2D_SOURCES})
target_include_directories(box2d PUBLIC "${ROOT}/ThirdParty/box2d/include" PRIVATE "${ROOT}/ThirdParty/box2d/src")
set_target_properties(box2d PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS YES)
target_compile_options(box2d PRIVATE -w)

# ImGui 코어만 (dx11 · win32 백엔드 없음) — 컴포넌트의 Inspector 코드가 링크되도록. 플레이어는 그리지 않는다
file(GLOB IMGUI_SOURCES "${ROOT}/Source/ThirdParty/ImGui/imgui*.cpp" "${ROOT}/Source/ThirdParty/ImGui/ImGuiNodeEditor/*.cpp")
list(FILTER IMGUI_SOURCES EXCLUDE REGEX "imgui_impl_")
add_library(imgui STATIC ${IMGUI_SOURCES})
target_include_directories(imgui PUBLIC "${ROOT}/Source/ThirdParty/ImGui" "${ROOT}/Source/ThirdParty/ImGui/ImGuiNodeEditor" "${ROOT}/Libraries/Include")
target_compile_definitions(imgui PUBLIC IMGUI_DEFINE_MATH_OPERATORS)
target_compile_options(imgui PRIVATE -w)

# NDK 의 native_app_glue: 이벤트 루프 (ALooper) · 생명 주기 명령 · 입력 큐를 앱 스레드로
set(GLUE_DIR "${ANDROID_NDK}/sources/android/native_app_glue")
add_library(native_app_glue STATIC "${GLUE_DIR}/android_native_app_glue.c")
target_include_directories(native_app_glue PUBLIC "${GLUE_DIR}")

# ---- 엔진 런타임
set(ENGINE_DIRS Core Math Scene Graphics/RHI Graphics/Common Graphics/DX11 Animation Effects UI Water Terrain Physics Physics2D Audio ShaderGraph)
set(ENGINE_SOURCES)
foreach(d ${ENGINE_DIRS})
    file(GLOB f "${ROOT}/Source/${d}/*.cpp")
    list(APPEND ENGINE_SOURCES ${f})
endforeach()
# Windows 전용 (안드로이드 판은 Android/Source/Engine): D3D11 장치 · Effects11 · 셰이더 컴파일러 · DbgHelp 로그 · psapi · 백엔드 선택 · 경로 ('\' → '/')
list(FILTER ENGINE_SOURCES EXCLUDE REGEX "/(GfxDx11|Dx11Rhi|ShaderCache|EditorLog|MemoryStats|GraphicsBackendFactory|PathManager)\\.cpp$")
file(GLOB ANDROID_ENGINE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Source/Engine/*.cpp")
# 하위 폴더 · 다른 폴더에서 플레이어도 쓰는 것: 오디오 디코더, Application, 컴포넌트 Inspector 의 ImGui 도우미, C# 스크립트 컴포넌트 (런타임은 없음)
file(GLOB AUDIO_CODEC_SOURCES "${ROOT}/Source/Audio/Codecs/*.cpp")
list(APPEND ENGINE_SOURCES ${AUDIO_CODEC_SOURCES}
    "${ROOT}/Source/Editor/UnityGUI.cpp"
    "${ROOT}/Source/Editor/EditorGUI.cpp"
    "${ROOT}/Source/Scripting/CSharpScript.cpp"
    "${ROOT}/Source/Scripting/MonoBehaviour.cpp")
add_library(nova_engine STATIC
    ${ENGINE_SOURCES}
    ${ANDROID_ENGINE_SOURCES}
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/GLESRhi.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/GLESState.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/GfxGLES.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/DirectXTexLite.cpp"
    "${ROOT}/Source/Graphics/ShaderCross/ShaderCrossJson.cpp"
    "${ROOT}/Source/Graphics/ShaderCross/FxParser.cpp"
)
# Windows 빌드처럼 모든 모듈 폴더를 include 경로에 (기존 #include "Xxx.h" 그대로). Android/Include 가 먼저 (pch.h · Windows 헤더 대체)
set(MODULE_DIRS Source Source/Core Source/Math Source/Graphics Source/Graphics/Common Source/Physics2D Source/ShaderGraph Source/Graphics/DX11
    Source/Graphics/OpenGL Source/Graphics/RHI Source/Animation Source/Physics Source/Scene Source/Terrain Source/Water Source/Audio
    Source/Scripting Source/UI Source/Build Source/Effects Source/Editor Source/Editor/Windows Source/Editor/Dialogs Source/Editor/NodeEditor
    Source/ThirdParty/ImGui Source/ThirdParty/ImGui/ImGuiNodeEditor Source/Platform Source/Graphics/ShaderCross Source/Packages)
list(TRANSFORM MODULE_DIRS PREPEND "${ROOT}/")
target_include_directories(nova_engine PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/Include"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/Engine"
    ${MODULE_DIRS}
    "${ROOT}/ThirdParty/DirectXMath/Inc"
    "${ROOT}/Libraries/Include"
    "${ROOT}/Source/ThirdParty"
)
target_compile_definitions(nova_engine PUBLIC NOVA_ANDROID=1)
target_compile_options(nova_engine PUBLIC -fexceptions -frtti -Wno-unused-parameter -Wno-unknown-pragmas -Wno-switch -Wno-deprecated-declarations
    -Wno-nonportable-include-path -Wno-microsoft-include -Wno-defaulted-function-deleted -Wno-invalid-offsetof -Wno-macro-redefined
    -Wno-format-security -Wno-error=format-security)
target_link_libraries(nova_engine PUBLIC Jolt box2d imgui)

# ---- 진입점
add_library(nova SHARED
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/AndroidMain.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/AndroidPlatform.cpp"
)
# 엔진 전체를 넣는다 (컴포넌트가 정적 초기화로 스스로 등록 — 참조되지 않아도 남아야 한다)
target_link_libraries(nova PRIVATE -Wl,--whole-archive nova_engine -Wl,--no-whole-archive native_app_glue android log EGL GLESv3)
# NativeActivity 가 찾는 진입점을 지우지 않게
target_link_options(nova PRIVATE "-Wl,-u,ANativeActivity_onCreate" "-Wl,--no-dependent-libraries" "-Wl,--error-limit=0" "$<$<CONFIG:Release>:-Wl,--strip-debug>")   # #pragma comment(lib, ...) (Windows 라이브러리) 무시
