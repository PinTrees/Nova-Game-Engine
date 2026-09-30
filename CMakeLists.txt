cmake_minimum_required(VERSION 3.20)
project(DX11Engine VERSION 1.0.0 LANGUAGES CXX)

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
add_executable(DX11 WIN32 ${ENGINE_SOURCES} ${ENGINE_HEADERS} "${CMAKE_CURRENT_SOURCE_DIR}/Source/Platform/DX11.rc")

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

# Precompiled Header for our engine files
target_precompile_headers(DX11 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source/Platform/pch.h")

# Include directories (match DX11.vcxproj)
# 기존 #include "Xxx.h" 형태를 유지하기 위해 모든 모듈 폴더를 include 경로에 등록
set(ENGINE_MODULE_DIRS
    Source
    Source/Core
    Source/Math
    Source/Graphics
    Source/Animation
    Source/Physics
    Source/Scene
    Source/Scripting
    Source/Editor
    Source/Editor/Windows
    Source/Editor/Dialogs
    Source/Editor/NodeEditor
    Source/ThirdParty/ImGui
    Source/ThirdParty/ImGui/ImGuiNodeEditor
    Source/Platform
)
list(TRANSFORM ENGINE_MODULE_DIRS PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")

target_include_directories(DX11 PRIVATE
    ${ENGINE_MODULE_DIRS}
    "${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Include"
)

# Library search directories
target_link_directories(DX11 PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Lib"
)

# System and Core Libraries
target_link_libraries(DX11 PRIVATE
    d3d11
    dxgi
    d3dcompiler
    dwmapi
    user32
    gdi32
    shell32
    imm32
)

# Post-build copy DLLs
add_custom_command(TARGET DX11 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assimp-vc143-mtd.dll"
    "$<TARGET_FILE_DIR:DX11>/assimp-vc143-mtd.dll"
)
