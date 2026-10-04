cmake_minimum_required(VERSION 3.20)
project(NovaEngine VERSION 0.1.0 LANGUAGES CXX C)   # C: 패키지 ThirdParty 의 C 라이브러리 (FastLZ)

# 엔진/실행 파일 이름 (변경 시 Source/Core/EngineInfo.h 도 함께 수정)
set(ENGINE_NAME NovaEngine)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Unicode Character Set & Preprocessor Definitions
add_compile_definitions(
    UNICODE
    _UNICODE
    IMGUI_DEFINE_MATH_OPERATORS
    _CONSOLE
    WIN32
    _WINDOWS
)

# Debug 빌드도 최적화(/O2)를 사용해 초기 로딩 속도를 확보 (런타임 체크 /RTC1 은 /O2 와 충돌하므로 제거)
if(MSVC)
    string(REPLACE "/Od" "/O2" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REPLACE "/JMC" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    # Release (build.bat release): 기본 /O2 /DNDEBUG 에 디버그 정보(/Zi)를 더해 멈춤·예외 호출 스택(EditorLog)이 이름을 보이게
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF")
endif()

# ---------------------------------------------------------------------------
# Jolt Physics (ThirdParty/JoltPhysics, MIT) - Rigidbody / Collider 시뮬레이션
# 엔진과 같은 런타임(/MD)과 위에서 지정한 Debug 최적화 플래그를 그대로 쓰도록 옵션을 맞춘다.
# ---------------------------------------------------------------------------
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
set(OVERRIDE_CXX_FLAGS OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
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
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/JoltPhysics/Build" "${CMAKE_BINARY_DIR}/JoltPhysics")

# ---------------------------------------------------------------------------
# Box2D v3 (ThirdParty/box2d, MIT) - Rigidbody2D / Collider2D (Unity 의 2D 물리도 Box2D)
# C17 정적 라이브러리. 경고는 끈다 (원본 그대로 두기 위해)
# ---------------------------------------------------------------------------
file(GLOB BOX2D_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/box2d/src/*.c")
add_library(box2d STATIC ${BOX2D_SOURCES})
target_include_directories(box2d
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/box2d/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/box2d/src")
set_target_properties(box2d PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS YES FOLDER "ThirdParty")
if(MSVC)
    target_compile_options(box2d PRIVATE /W0)
endif()

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/Binaries")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/Binaries")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/Binaries")

# Source files (all engine and editor files)
file(GLOB_RECURSE ENGINE_SOURCES 
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp"
)
file(GLOB_RECURSE ENGINE_HEADERS 
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h"
)

# Exclude legacy demo files if any remain in the Source folder
list(FILTER ENGINE_SOURCES EXCLUDE REGEX ".*[0-9][0-9]\\..*\\.cpp$")
list(FILTER ENGINE_HEADERS EXCLUDE REGEX ".*[0-9][0-9]\\..*\\.h$")
# 실행 파일(Launcher)은 엔진 DLL 밖
list(FILTER ENGINE_SOURCES EXCLUDE REGEX ".*/Source/Launcher/.*")

# ---------------------------------------------------------------------------
# 엔진 = NovaCore.dll (에디터·Hub·플레이어 전부), 실행 파일 = 작은 Launcher (Unity 의 UnityPlayer.dll + <게임>.exe 구조).
# 패키지(DLL, Packages/)가 엔진 클래스를 링크해 쓴다 — 빌드한 게임에서 exe 이름이 <제품>.exe 로 바뀌어도 패키지가 찾는 것은 NovaCore.dll.
# 패키지가 쓰는 클래스·함수에는 NOVA_API (Source/Core/NovaApi.h), ImGui 는 IMGUI_API 로 내보낸다.
set(ENGINE_CORE NovaCore)
add_library(${ENGINE_CORE} SHARED ${ENGINE_SOURCES} ${ENGINE_HEADERS})
target_compile_definitions(${ENGINE_CORE} PRIVATE NOVA_ENGINE_BUILD "IMGUI_API=__declspec(dllexport)")
target_compile_definitions(${ENGINE_CORE} INTERFACE "IMGUI_API=__declspec(dllimport)")   # 패키지 쪽
if(MSVC)
    # 내보낸 클래스의 STL 멤버(C4251) / 내보내지 않은 기반 클래스(C4275): 엔진과 패키지는 같은 컴파일러·런타임(/MD)이라 안전
    target_compile_options(${ENGINE_CORE} PUBLIC /wd4251 /wd4275)
    set_target_properties(${ENGINE_CORE} PROPERTIES LINK_FLAGS_RELEASE "/DEBUG /OPT:REF /OPT:ICF")
endif()

# 실행 파일 (Windows GUI, 아이콘) — NovaMain(NovaCore.dll) 만 부른다
add_executable(${ENGINE_NAME} WIN32 "${CMAKE_CURRENT_SOURCE_DIR}/Source/Launcher/Launcher.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/Source/Platform/NovaEngine.rc")
target_include_directories(${ENGINE_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source/Platform")
target_link_libraries(${ENGINE_NAME} PRIVATE ${ENGINE_CORE})

# Visual Studio 솔루션 탐색기에서 폴더 트리 유지
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/Source" PREFIX "Source" FILES ${ENGINE_SOURCES} ${ENGINE_HEADERS})
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${ENGINE_NAME})

# 오디오 디코더 (stb_vorbis · dr_mp3 를 감싼 파일): 엔진 PCH 없이, 경고 없이
file(GLOB NOVA_AUDIO_CODECS "${CMAKE_CURRENT_SOURCE_DIR}/Source/Audio/Codecs/*.cpp")
set_source_files_properties(${NOVA_AUDIO_CODECS} PROPERTIES SKIP_PRECOMPILE_HEADERS ON COMPILE_OPTIONS "/W0")

# Disable PCH for ImGui and ImGuiNodeEditor third-party files
set_source_files_properties(
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/ThirdParty/ImGui/imgui.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/ThirdParty/ImGui/imgui_draw.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/ThirdParty/ImGui/imgui_widgets.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/ThirdParty/ImGui/imgui_tables.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/ThirdParty/ImGui/imgui_demo.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/ThirdParty/ImGui/imgui_impl_dx11.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/ThirdParty/ImGui/imgui_impl_win32.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/ThirdParty/ImGui/ImGuiNodeEditor/crude_json.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/ThirdParty/ImGui/ImGuiNodeEditor/imgui_canvas.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/ThirdParty/ImGui/ImGuiNodeEditor/imgui_node_editor.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/ThirdParty/ImGui/ImGuiNodeEditor/imgui_node_editor_api.cpp"
    PROPERTIES SKIP_PRECOMPILE_HEADERS ON
)

# 셰이더 변환기: SPIRV-Cross 헤더를 Windows.h(min/max 매크로)보다 먼저 포함해야 하므로 PCH 를 쓰지 않는다
set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/Source/Graphics/ShaderCross/ShaderCross.cpp" PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

# Jolt 헤더를 Windows.h(min/max 매크로)보다 먼저 포함해야 하므로 물리 백엔드는 PCH 를 쓰지 않는다
set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/Source/Physics/PhysicsManager.cpp" PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

# Precompiled Header for our engine files
target_precompile_headers(${ENGINE_CORE} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source/Platform/pch.h")

# Include directories (match DX11.vcxproj)
# 기존 #include "Xxx.h" 형태를 유지하기 위해 모든 모듈 폴더를 include 경로에 등록
set(ENGINE_MODULE_DIRS
    Source
    Source/Core
    Source/Math
    Source/Graphics
    Source/Graphics/Common
    Source/Physics2D
    Source/ShaderGraph
    Source/Graphics/DX11
    Source/Graphics/OpenGL
    Source/Graphics/Vulkan
    Source/Graphics/RHI
    Source/Animation
    Source/Physics
    Source/Scene
    Source/Terrain
    Source/Water
    Source/Audio
    Source/Scripting
    Source/UI
    Source/Build
    Source/Effects
    Source/Editor
    Source/Editor/Windows
    Source/Editor/Dialogs
    Source/Editor/NodeEditor
    Source/Editor/NovaCode
    Source/ThirdParty/ImGui
    Source/ThirdParty/ImGui/ImGuiNodeEditor
    Source/Hub
    Source/Platform
    Source/Graphics/ShaderCross
    Source/Packages
)
list(TRANSFORM ENGINE_MODULE_DIRS PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")

target_include_directories(${ENGINE_CORE} PUBLIC
    ${ENGINE_MODULE_DIRS}
    "${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Include"
    "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/SPIRV-Cross"
    "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/DXC/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/Vulkan/include"   # Vulkan 헤더 (Khronos, 함수는 vulkan-1.dll 에서 실행 중에)
)

# Library search directories
target_link_directories(${ENGINE_CORE} PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Lib"
)

# System and Core Libraries
target_link_libraries(${ENGINE_CORE} PRIVATE
    Jolt
    box2d
    d3d11
    dxgi
    d3dcompiler
    dwmapi
    opengl32
    user32
    gdi32
    shell32
    imm32
)

# ---------------------------------------------------------------------------
# C# 스크립팅 API (ScriptCore → Binaries/Scripting/NovaScriptCore.dll). .NET SDK(8+) 가 있으면 함께 빌드한다.
find_program(DOTNET_EXE dotnet)
if(DOTNET_EXE)
    add_custom_target(NovaScriptCore ALL
        COMMAND "${DOTNET_EXE}" build "${CMAKE_CURRENT_SOURCE_DIR}/ScriptCore/NovaScriptCore.csproj" -c Release --nologo -v q -nodeReuse:false
                -o "${CMAKE_CURRENT_SOURCE_DIR}/Binaries/Scripting"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/ScriptCore"
        COMMENT "Building NovaScriptCore (C# scripting API)"
        VERBATIM)
    add_dependencies(${ENGINE_CORE} NovaScriptCore)
else()
    message(WARNING ".NET SDK (dotnet) not found - C# scripting will be disabled")
endif()

# Post-build copy DLLs
add_custom_command(TARGET ${ENGINE_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assimp-vc143-mtd.dll"
    "$<TARGET_FILE_DIR:${ENGINE_NAME}>/assimp-vc143-mtd.dll"
)
# Release: assimp 릴리스 DLL (assimp 5.4.1 을 /MD Release 로 빌드해 저장소 루트에 둠, 가져오기 라이브러리는 Libraries/Lib/Assimp).
# 있으면 Release 는 이것을 링크한다 → 디버그 C 런타임(재배포 불가)이 없는 PC 에서도 실행 (Tools/package_release.ps1)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assimp-vc143-mt.dll")
    target_compile_definitions(${ENGINE_CORE} PUBLIC $<$<CONFIG:Release>:NOVA_ASSIMP_RELEASE_DLL>)
    add_custom_command(TARGET ${ENGINE_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/assimp-vc143-mt.dll"
        "$<TARGET_FILE_DIR:${ENGINE_NAME}>/assimp-vc143-mt.dll"
    )
endif()

# ---------------------------------------------------------------------------
# NOVA CLI (nova.exe): 터미널·AI 가 실행 중인 에디터를 다루는 콘솔 도구 (Tools/NovaCli, 에디터 쪽 = Source/Editor/CliServer).
# 정적 런타임(/MT)으로 만들어 Hub 가 다른 폴더에 복사해도 혼자 돈다. 엔진이 빌드될 때 같이 빌드된다.
add_executable(nova "${CMAKE_CURRENT_SOURCE_DIR}/Tools/NovaCli/main.cpp")
target_include_directories(nova PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Include")
set_property(TARGET nova PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
target_compile_options(nova PRIVATE /utf-8)
add_dependencies(${ENGINE_NAME} nova)

# ---------------------------------------------------------------------------
# 셰이더 자동 변환 (HLSL → SPIR-V → GLSL): SPIRV-Cross (ThirdParty, Apache-2.0) 핵심 + GLSL 출력만 정적 라이브러리로,
# DXC (dxcompiler.dll + dxil.dll, ThirdParty/DXC/runtime) 는 실행 중에 불러온다 (Source/Graphics/ShaderCross)
add_library(SPIRVCross STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/SPIRV-Cross/spirv_cross.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/SPIRV-Cross/spirv_cfg.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/SPIRV-Cross/spirv_cross_parsed_ir.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/SPIRV-Cross/spirv_cross_util.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/SPIRV-Cross/spirv_parser.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/SPIRV-Cross/spirv_glsl.cpp"
)
target_include_directories(SPIRVCross PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/SPIRV-Cross")
target_compile_options(SPIRVCross PRIVATE /W0 /bigobj)
target_link_libraries(${ENGINE_CORE} PRIVATE SPIRVCross)

# 안드로이드 텍스처 압축 ASTC: ARM astc-encoder 5.7.0 (ThirdParty/astcenc, Apache-2.0) — 에디터가 nova android export 때만 쓴다 (APK 에는 들어가지 않음).
#  SSE4.1 + POPCNT (MSVC 는 자동 판별 매크로가 없어 직접 정한다)
file(GLOB ASTCENC_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/astcenc/astcenc_*.cpp")
add_library(astcenc STATIC ${ASTCENC_SOURCES})
target_include_directories(astcenc PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/astcenc")
target_compile_definitions(astcenc PUBLIC ASTCENC_SSE=41 ASTCENC_AVX=0 ASTCENC_POPCNT=1 ASTCENC_F16C=0 ASTCENC_NEON=0 ASTCENC_SVE=0)
target_compile_options(astcenc PRIVATE /W0 /utf-8)
target_link_libraries(${ENGINE_CORE} PRIVATE astcenc)
add_custom_command(TARGET ${ENGINE_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/DXC/runtime/dxcompiler.dll"
        "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/DXC/runtime/dxil.dll"
        "$<TARGET_FILE_DIR:${ENGINE_NAME}>"
)

# ---------------------------------------------------------------------------
# NOVA 패키지 (Packages/<이름>/): Source/*.cpp → Packages/<이름>/Plugins/<DLL>.dll (NovaCore.dll 링크) + <DLL>.dll.abi
# .abi = 엔진 ABI 문자열 (Source/Core/NovaApi.h 의 NOVA_PACKAGE_ABI_VERSION 과 같아야 한다 — PackageManager 가 불러오기 전에 비교)
set(NOVA_PACKAGE_ABI_BASE "nova-0.1-msvc143")
function(nova_add_package PKG_NAME DLL_NAME)
    set(PKG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Packages/${PKG_NAME}")
    file(GLOB_RECURSE PKG_SOURCES "${PKG_DIR}/Source/*.cpp" "${PKG_DIR}/Source/*.h")
    # SimpleMath 의 정적 상수(Vector3::Zero 등)는 엔진 DLL 밖으로 내보내지 않으므로 패키지에도 같이 컴파일한다
    list(APPEND PKG_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Source/Math/SimpleMath.cpp")
    add_library(${DLL_NAME} SHARED ${PKG_SOURCES})
    target_link_libraries(${DLL_NAME} PRIVATE ${ENGINE_CORE})
    target_include_directories(${DLL_NAME} PRIVATE "${PKG_DIR}/Source")
    target_precompile_headers(${DLL_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source/Platform/pch.h")
    # 패키지 안의 외부 라이브러리 (ThirdParty/<이름>/Source/*.cpp, *.c + Include/) — 엔진 PCH 없이, 경고 없이
    file(GLOB PKG_LIB_DIRS LIST_DIRECTORIES true "${PKG_DIR}/ThirdParty/*")
    foreach(LIB_DIR ${PKG_LIB_DIRS})
        if(IS_DIRECTORY "${LIB_DIR}/Source")
            file(GLOB LIB_SOURCES "${LIB_DIR}/Source/*.cpp" "${LIB_DIR}/Source/*.c")
            target_sources(${DLL_NAME} PRIVATE ${LIB_SOURCES})
            set_source_files_properties(${LIB_SOURCES} PROPERTIES SKIP_PRECOMPILE_HEADERS ON COMPILE_OPTIONS "/W0")
            target_include_directories(${DLL_NAME} PRIVATE "${LIB_DIR}/Include")
        endif()
    endforeach()
    if(IS_DIRECTORY "${PKG_DIR}/ThirdParty/Detour")
        target_compile_definitions(${DLL_NAME} PRIVATE DT_POLYREF64)   # 큰 월드: 64 비트 다각형 참조 (타일 수 제한 없음)
    endif()
    set_target_properties(${DLL_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PKG_DIR}/Plugins"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${PKG_DIR}/Plugins"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${PKG_DIR}/Plugins"
        FOLDER "Packages")
    file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/PackageAbi/${DLL_NAME}_$<CONFIG>.abi"
        CONTENT "${NOVA_PACKAGE_ABI_BASE}-$<IF:$<CONFIG:Debug>,debug,release>")
    add_custom_command(TARGET ${DLL_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/PackageAbi/${DLL_NAME}_$<CONFIG>.abi" "${PKG_DIR}/Plugins/${DLL_NAME}.dll.abi")
    add_dependencies(${ENGINE_NAME} ${DLL_NAME})   # 엔진을 빌드하면 공식 패키지도 같이
endfunction()

# Packages/<이름>/ 에 Source/ 와 package.json 의 "native" 가 있으면 자동으로 패키지 DLL 을 만든다 (DLL 이름 = native 첫 항목)
file(GLOB NOVA_PACKAGE_DIRS LIST_DIRECTORIES true "${CMAKE_CURRENT_SOURCE_DIR}/Packages/*")
foreach(PKG_PATH ${NOVA_PACKAGE_DIRS})
    if(IS_DIRECTORY "${PKG_PATH}/Source" AND EXISTS "${PKG_PATH}/package.json")
        file(READ "${PKG_PATH}/package.json" PKG_JSON)
        string(JSON PKG_NATIVE_COUNT ERROR_VARIABLE PKG_JSON_ERR LENGTH "${PKG_JSON}" native)
        if(NOT PKG_JSON_ERR AND PKG_NATIVE_COUNT GREATER 0)
            string(JSON PKG_DLL GET "${PKG_JSON}" native 0)
            get_filename_component(PKG_DLL_NAME "${PKG_DLL}" NAME_WE)
            get_filename_component(PKG_NAME "${PKG_PATH}" NAME)
            nova_add_package(${PKG_NAME} ${PKG_DLL_NAME})
            message(STATUS "NOVA package: ${PKG_NAME} -> Plugins/${PKG_DLL_NAME}.dll")
        endif()
    endif()
endforeach()