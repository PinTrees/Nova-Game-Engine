cmake_minimum_required(VERSION 3.20)
project(NovaEngine VERSION 0.1.0 LANGUAGES CXX)

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

# Create executable (Windows GUI Subsystem with Application Icon)
add_executable(${ENGINE_NAME} WIN32 ${ENGINE_SOURCES} ${ENGINE_HEADERS} "${CMAKE_CURRENT_SOURCE_DIR}/Source/Platform/NovaEngine.rc")

# Visual Studio 솔루션 탐색기에서 폴더 트리 유지
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/Source" PREFIX "Source" FILES ${ENGINE_SOURCES} ${ENGINE_HEADERS})

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

# Jolt 헤더를 Windows.h(min/max 매크로)보다 먼저 포함해야 하므로 물리 백엔드는 PCH 를 쓰지 않는다
set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/Source/Physics/PhysicsManager.cpp" PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

# Precompiled Header for our engine files
target_precompile_headers(${ENGINE_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source/Platform/pch.h")

# Include directories (match DX11.vcxproj)
# 기존 #include "Xxx.h" 형태를 유지하기 위해 모든 모듈 폴더를 include 경로에 등록
set(ENGINE_MODULE_DIRS
    Source
    Source/Core
    Source/Math
    Source/Graphics
    Source/Graphics/Common
    Source/Graphics/DX11
    Source/Graphics/OpenGL
    Source/Animation
    Source/Physics
    Source/Scene
    Source/Terrain
    Source/Audio
    Source/Scripting
    Source/UI
    Source/Editor
    Source/Editor/Windows
    Source/Editor/Dialogs
    Source/Editor/NodeEditor
    Source/Editor/NovaCode
    Source/ThirdParty/ImGui
    Source/ThirdParty/ImGui/ImGuiNodeEditor
    Source/Hub
    Source/Platform
)
list(TRANSFORM ENGINE_MODULE_DIRS PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")

target_include_directories(${ENGINE_NAME} PRIVATE
    ${ENGINE_MODULE_DIRS}
    "${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Include"
)

# Library search directories
target_link_directories(${ENGINE_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Lib"
)

# System and Core Libraries
target_link_libraries(${ENGINE_NAME} PRIVATE
    Jolt
    d3d11
    dxgi
    d3dcompiler
    dwmapi
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
    add_dependencies(${ENGINE_NAME} NovaScriptCore)
else()
    message(WARNING ".NET SDK (dotnet) not found - C# scripting will be disabled")
endif()

# Post-build copy DLLs
add_custom_command(TARGET ${ENGINE_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assimp-vc143-mtd.dll"
    "$<TARGET_FILE_DIR:${ENGINE_NAME}>/assimp-vc143-mtd.dll"
)
