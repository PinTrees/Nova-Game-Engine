# NOVA 웹 플레이어 (WebGPU + WebAssembly). Emscripten 툴체인으로 빌드한다 (Web/build.sh — .NET wasm-tools 의 Emscripten 3.1.56).
#  - nova_engine: 엔진 런타임 소스 (Source/ — Windows · 안드로이드와 같은 파일) + 플랫폼 판
#      안드로이드 판 중 플랫폼과 상관없는 것은 그대로 (Android/Source/Engine: 입력 상태 · 경로 · 플레이어 런타임 · 에디터 스텁 · 소프트웨어 오디오 믹서)
#      웹 판 (Web/Source/Engine): 앱 루프 · 로그 · C# (나중) , 그래픽 = WebGPU (Web/Source/GfxWgpu*)
#  - 엔진의 #include "pch.h" 는 Android/Include/pch.h (Windows · D3D11 타입 대체 — 웹과 같이 쓴다)
#  - 컴파일 옵션은 .NET 웹어셈블리 런타임과 같게 (-fwasm-exceptions -msimd128) — C# 런타임과 한 wasm 으로 묶는다
cmake_minimum_required(VERSION 3.22)
project(NovaWeb C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(NOVA_WASM_FLAGS -fwasm-exceptions -msimd128)
add_compile_options(${NOVA_WASM_FLAGS})

# ---- 제삼자 (Windows · 안드로이드 빌드와 같은 소스)
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
set(USE_WASM_SIMD ON CACHE BOOL "" FORCE)
add_subdirectory("${ROOT}/ThirdParty/JoltPhysics/Build" "${CMAKE_BINARY_DIR}/JoltPhysics")

file(GLOB BOX2D_SOURCES "${ROOT}/ThirdParty/box2d/src/*.c")
add_library(box2d STATIC ${BOX2D_SOURCES})
target_include_directories(box2d PUBLIC "${ROOT}/ThirdParty/box2d/include" PRIVATE "${ROOT}/ThirdParty/box2d/src")
set_target_properties(box2d PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS YES)
target_compile_options(box2d PRIVATE -w -msse2)   # box2d 의 SSE2 경로 → wasm SIMD (Emscripten 이 바꾼다)

# ImGui 코어만 — 컴포넌트의 Inspector 코드가 링크되도록, UI 입력 (플레이어는 그리지 않는다)
file(GLOB IMGUI_SOURCES "${ROOT}/Source/ThirdParty/ImGui/imgui*.cpp" "${ROOT}/Source/ThirdParty/ImGui/ImGuiNodeEditor/*.cpp")
list(FILTER IMGUI_SOURCES EXCLUDE REGEX "imgui_impl_")
add_library(imgui STATIC ${IMGUI_SOURCES})
target_include_directories(imgui PUBLIC "${ROOT}/Source/ThirdParty/ImGui" "${ROOT}/Source/ThirdParty/ImGui/ImGuiNodeEditor" "${ROOT}/Libraries/Include")
target_compile_definitions(imgui PUBLIC IMGUI_DEFINE_MATH_OPERATORS)
target_compile_options(imgui PRIVATE -w)

# ---- 엔진 런타임 (안드로이드와 같은 목록)
set(ENGINE_DIRS Core Math Scene Graphics/RHI Graphics/Common Graphics/DX11 Animation Effects UI Water Terrain Physics Physics2D Audio ShaderGraph)
set(ENGINE_SOURCES)
foreach(d ${ENGINE_DIRS})
    file(GLOB f CONFIGURE_DEPENDS "${ROOT}/Source/${d}/*.cpp")
    list(APPEND ENGINE_SOURCES ${f})
endforeach()
list(FILTER ENGINE_SOURCES EXCLUDE REGEX "/(GfxDx11|Dx11Rhi|ShaderCache|EditorLog|MemoryStats|GraphicsBackendFactory|PathManager)\\.cpp$")
file(GLOB AUDIO_CODEC_SOURCES "${ROOT}/Source/Audio/Codecs/*.cpp")
list(APPEND ENGINE_SOURCES ${AUDIO_CODEC_SOURCES}
    "${ROOT}/Source/Editor/EditorApp.cpp"
    "${ROOT}/Source/Build/BuildSettings.cpp"
    "${ROOT}/Source/Editor/UnityGUI.cpp"
    "${ROOT}/Source/Editor/EditorGUI.cpp"
    "${ROOT}/Source/Scripting/CSharpScript.cpp"
    "${ROOT}/Source/Scripting/ScriptBindings.cpp"
    "${ROOT}/Source/Scripting/PlatformBindings.cpp"
    "${ROOT}/Source/Scripting/PlayerPrefsStore.cpp"
    "${ROOT}/Source/Scripting/MonoBehaviour.cpp")
# 안드로이드 판 중 플랫폼과 상관없는 것
set(SHARED_PLATFORM_SOURCES
    "${ROOT}/Android/Source/Engine/AndroidWin32.cpp"         # GetAsyncKeyState · GetCursorPos = 입력 상태
    "${ROOT}/Android/Source/Engine/ApplicationAndroid.cpp"
    "${ROOT}/Android/Source/Engine/EditorStubs.cpp"
    "${ROOT}/Android/Source/Engine/PathManagerAndroid.cpp"   # '/' 경로, 루트 = <파일 폴더>/game
    "${ROOT}/Android/Source/Engine/PlayerRuntimeAndroid.cpp"
    "${ROOT}/Android/Source/Engine/XAudio2Android.cpp"       # 소프트웨어 믹서 (출력은 플랫폼마다)
    "${ROOT}/Android/Source/Engine/AppAndroid.cpp"           # App 한 프레임 (그래픽 장치 = WebGPU)
    "${ROOT}/Android/Source/Engine/EngineAndroid.cpp"        # 로그 (브라우저 콘솔) · 메모리 · 셰이더 캐시 없음
    "${ROOT}/Android/Source/Engine/ScriptEngineAndroid.cpp"  # C# (런타임 = .NET 웹어셈블리 — Web/Host 가 함께 링크할 때만)
    "${ROOT}/Android/Source/DirectXTexLite.cpp")
file(GLOB WEB_ENGINE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Source/Engine/*.cpp")
file(GLOB WEB_GFX_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp")
list(FILTER WEB_GFX_SOURCES EXCLUDE REGEX "/WebMain\\.cpp$")
add_library(nova_engine STATIC
    ${ENGINE_SOURCES}
    ${SHARED_PLATFORM_SOURCES}
    ${WEB_ENGINE_SOURCES}
    ${WEB_GFX_SOURCES}
    "${ROOT}/Source/Graphics/ShaderCross/ShaderCrossJson.cpp"
    "${ROOT}/Source/Graphics/ShaderCross/FxParser.cpp"
)
set(MODULE_DIRS Source Source/Core Source/Math Source/Graphics Source/Graphics/Common Source/Physics2D Source/ShaderGraph Source/Graphics/DX11
    Source/Graphics/OpenGL Source/Graphics/RHI Source/Animation Source/Physics Source/Scene Source/Terrain Source/Water Source/Audio
    Source/Scripting Source/UI Source/Build Source/Effects Source/Editor Source/Editor/Windows Source/Editor/Dialogs Source/Editor/NodeEditor
    Source/ThirdParty/ImGui Source/ThirdParty/ImGui/ImGuiNodeEditor Source/Platform Source/Graphics/ShaderCross Source/Packages Source/Graphics/Vulkan)
list(TRANSFORM MODULE_DIRS PREPEND "${ROOT}/")
target_include_directories(nova_engine PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/Include"          # 웹이 덮어쓰는 대체 헤더 (있으면 먼저)
    "${ROOT}/Android/Include"                      # pch.h · WinCompat.h · d3d11.h … (안드로이드와 같이)
    "${CMAKE_CURRENT_SOURCE_DIR}/Source"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/Engine"
    "${ROOT}/Android/Source/Engine"
    "${ROOT}/Android/Source"
    ${MODULE_DIRS}
    "${ROOT}/ThirdParty/DirectXMath/Inc"
    "${ROOT}/Libraries/Include"
    "${ROOT}/Source/ThirdParty"
)
# NOVA_ANDROID 의 플레이어 코드 (터치 입력 · OpenGL ES 계열 판단) 와 구분: NOVA_WEB. 그래픽 판단은 GfxApi
target_compile_definitions(nova_engine PUBLIC NOVA_WEB=1 _XM_NO_INTRINSICS_=1)
target_compile_definitions(nova_engine PRIVATE NOVA_ENGINE_BUILD)
target_compile_options(nova_engine PUBLIC -fexceptions -frtti -Wno-unused-parameter -Wno-unknown-pragmas -Wno-switch -Wno-deprecated-declarations
    -Wno-nonportable-include-path -Wno-microsoft-include -Wno-defaulted-function-deleted -Wno-invalid-offsetof -Wno-macro-redefined
    -Wno-format-security -Wno-error=format-security -Wno-unused-command-line-argument)
target_link_libraries(nova_engine PUBLIC Jolt box2d imgui)

# ---- 공식 패키지: 안드로이드처럼 엔진에 함께 넣는다 (진입점 이름에 패키지 이름을 붙이고 nova_packages.cpp 가 차례로)
file(GLOB NOVA_PACKAGE_DIRS LIST_DIRECTORIES true "${ROOT}/Packages/*")
set(NOVA_PACKAGE_LIBS)
set(PKG_DECLS "")
set(PKG_CALLS "")
foreach(PKG_DIR ${NOVA_PACKAGE_DIRS})
    file(GLOB PKG_SOURCES "${PKG_DIR}/Source/*.cpp")
    if(NOT PKG_SOURCES)
        continue()
    endif()
    get_filename_component(PKG_NAME "${PKG_DIR}" NAME)
    string(MAKE_C_IDENTIFIER "${PKG_NAME}" PKG_ID)
    add_library(pkg_${PKG_ID} STATIC ${PKG_SOURCES})
    target_include_directories(pkg_${PKG_ID} PRIVATE "${PKG_DIR}/Source")
    target_link_libraries(pkg_${PKG_ID} PUBLIC nova_engine)
    target_compile_definitions(pkg_${PKG_ID} PRIVATE NovaPackage_OnLoad=NovaPackage_OnLoad_${PKG_ID}
        NovaPackage_OnUnload=NovaPackage_OnUnload_${PKG_ID} NovaPackage_Abi=NovaPackage_Abi_${PKG_ID})
    file(GLOB PKG_LIB_DIRS LIST_DIRECTORIES true "${PKG_DIR}/ThirdParty/*")
    foreach(LIB_DIR ${PKG_LIB_DIRS})
        if(IS_DIRECTORY "${LIB_DIR}/Source")
            file(GLOB LIB_SOURCES "${LIB_DIR}/Source/*.cpp" "${LIB_DIR}/Source/*.c")
            target_sources(pkg_${PKG_ID} PRIVATE ${LIB_SOURCES})
            set_source_files_properties(${LIB_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
            target_include_directories(pkg_${PKG_ID} PRIVATE "${LIB_DIR}/Include")
        endif()
    endforeach()
    if(IS_DIRECTORY "${PKG_DIR}/ThirdParty/Detour")
        target_compile_definitions(pkg_${PKG_ID} PRIVATE DT_POLYREF64)
    endif()
    list(APPEND NOVA_PACKAGE_LIBS pkg_${PKG_ID})
    file(GLOB PKG_ENTRY "${PKG_DIR}/Source/Package.cpp")
    if(PKG_ENTRY)
        file(STRINGS "${PKG_ENTRY}" PKG_HAS_ONLOAD REGEX "NovaPackage_OnLoad\\(")
        if(PKG_HAS_ONLOAD)
            string(APPEND PKG_DECLS "extern \"C\" void NovaPackage_OnLoad_${PKG_ID}();\n")
            string(APPEND PKG_CALLS "    NovaPackage_OnLoad_${PKG_ID}();   // ${PKG_NAME}\n    NovaPackageLoaded(\"${PKG_NAME}\");\n")
        endif()
    endif()
endforeach()
file(WRITE "${CMAKE_BINARY_DIR}/nova_packages.cpp"
    "// 만든 파일 (Web/CMakeLists.txt): 엔진에 함께 넣은 패키지의 진입점을 차례로 — App::Init 이 씬보다 먼저 부른다\n"
    "${PKG_DECLS}void NovaPackageLoaded(const char* name);\nvoid NovaAndroidLoadPackages()\n{\n${PKG_CALLS}}\n")

# ---- 실행 파일 (C# 없이 — 엔진만 시험: nova.html · nova.js · nova.wasm). C# 이 있는 게임은 .NET 이 같은 정적 라이브러리를 다시 링크한다
add_executable(nova "${CMAKE_CURRENT_SOURCE_DIR}/Source/WebMain.cpp" "${CMAKE_BINARY_DIR}/nova_packages.cpp")
target_link_libraries(nova PRIVATE -Wl,--whole-archive nova_engine ${NOVA_PACKAGE_LIBS} -Wl,--no-whole-archive)
target_link_options(nova PRIVATE ${NOVA_WASM_FLAGS} -sUSE_WEBGPU=1 -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=268435456 -sMAXIMUM_MEMORY=4294967296
    -sSTACK_SIZE=4194304 -sFORCE_FILESYSTEM=1 -sEXPORTED_RUNTIME_METHODS=FS,ccall,cwrap,UTF8ToString -sEXPORTED_FUNCTIONS=_main,_malloc,_free
    -sENVIRONMENT=web -sERROR_ON_UNDEFINED_SYMBOLS=1 -Wl,--error-limit=0)
set_target_properties(nova PROPERTIES SUFFIX ".js")

# ---- C# 이 있는 게임: .NET 웹어셈블리 런타임 (Web/Host — Mono + BCL) 과 엔진을 wasm 하나로 다시 링크한다 (Web/build.sh host)
#  - nova_web_host: 진입점 (main 없이 nova_web_start — C# 의 Main 이 부른다) + 패키지 목록
#  - NovaWebHost.props: .NET 링크에 넘길 정적 라이브러리 (엔진 · 패키지 = 통째로, 제삼자 = 쓰는 것만)
#  - host-modules/<이름>.c: 엔진 · 패키지 C# 의 DllImport 모듈 이름 ("NovaCore" · "NovaTilemap" …) — .NET 이 이 이름의 네이티브 파일이 있어야 P/Invoke 표에 넣는다
#    (함수는 모두 엔진 쪽에 있고 파일은 비어 있다)
add_library(nova_web_host STATIC "${CMAKE_CURRENT_SOURCE_DIR}/Source/WebMain.cpp" "${CMAKE_BINARY_DIR}/nova_packages.cpp")
target_link_libraries(nova_web_host PUBLIC nova_engine)
target_compile_definitions(nova_web_host PRIVATE NOVA_WEB_DOTNET=1)
add_dependencies(nova_web_host ${NOVA_PACKAGE_LIBS})
set(HOST_WHOLE "$<TARGET_FILE:nova_web_host>;$<TARGET_FILE:nova_engine>")
foreach(PKG ${NOVA_PACKAGE_LIBS})
    string(APPEND HOST_WHOLE ";$<TARGET_FILE:${PKG}>")
endforeach()
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/NovaWebHost.props" CONTENT
"<Project>
  <!-- 만든 파일 (Web/CMakeLists.txt) -->
  <PropertyGroup>
    <NovaWholeLibs>${HOST_WHOLE}</NovaWholeLibs>
    <NovaOtherLibs>$<TARGET_FILE:Jolt>;$<TARGET_FILE:box2d>;$<TARGET_FILE:imgui></NovaOtherLibs>
    <NovaModulesDir>${CMAKE_BINARY_DIR}/host-modules/</NovaModulesDir>
  </PropertyGroup>
</Project>
")
file(REMOVE_RECURSE "${CMAKE_BINARY_DIR}/host-modules")
file(WRITE "${CMAKE_BINARY_DIR}/host-modules/NovaWeb.c" "/* 모듈 이름만 (Web/Host 의 DllImport) */\n")
file(GLOB_RECURSE PKG_CS CONFIGURE_DEPENDS "${ROOT}/Packages/*/Runtime/*.cs" "${ROOT}/ScriptCore/Engine/*.cs" "${ROOT}/ScriptCore/Interop/*.cs")
foreach(CS ${PKG_CS})
    # [DllImport("NovaX")] 또는 const string Dll (Lib) = "NovaX";
    file(STRINGS "${CS}" CS_IMPORTS REGEX "(DllImport\\(|const string (Dll|Lib) *= *)\"[A-Za-z0-9_]+\"")
    foreach(LINE ${CS_IMPORTS})
        string(REGEX MATCH "(DllImport\\(|const string (Dll|Lib) *= *)\"([A-Za-z0-9_]+)\"" _ "${LINE}")
        set(MODULE "${CMAKE_MATCH_3}")
        if(NOT EXISTS "${CMAKE_BINARY_DIR}/host-modules/${MODULE}.c")
            file(WRITE "${CMAKE_BINARY_DIR}/host-modules/${MODULE}.c" "/* 모듈 이름만 (C# 의 DllImport) */\n")
        endif()
    endforeach()
endforeach()
