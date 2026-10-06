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
    file(GLOB f CONFIGURE_DEPENDS "${ROOT}/Source/${d}/*.cpp")
    list(APPEND ENGINE_SOURCES ${f})
endforeach()
# Windows 전용 (안드로이드 판은 Android/Source/Engine): D3D11 장치 · Effects11 · 셰이더 컴파일러 · DbgHelp 로그 · psapi · 백엔드 선택 · 경로 ('\' → '/')
list(FILTER ENGINE_SOURCES EXCLUDE REGEX "/(GfxDx11|Dx11Rhi|ShaderCache|EditorLog|MemoryStats|GraphicsBackendFactory|PathManager)\\.cpp$")
file(GLOB ANDROID_ENGINE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Source/Engine/*.cpp")
# 하위 폴더 · 다른 폴더에서 플레이어도 쓰는 것: 오디오 디코더, Application, 컴포넌트 Inspector 의 ImGui 도우미, C# 스크립트 컴포넌트 (런타임은 없음)
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
    Source/ThirdParty/ImGui Source/ThirdParty/ImGui/ImGuiNodeEditor Source/Platform Source/Graphics/ShaderCross Source/Packages Source/Graphics/Vulkan)
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
# 엔진 컴포넌트의 자기 등록 (REGISTER_COMPONENT — Windows 의 NovaCore.dll 과 같게). 없으면 ComponentFactory 의 기본 목록 29 개만 (UI · 오디오 · 물리 … 컴포넌트가 씬에서 빠진다)
target_compile_definitions(nova_engine PRIVATE NOVA_ENGINE_BUILD)
target_compile_options(nova_engine PUBLIC -fexceptions -frtti -Wno-unused-parameter -Wno-unknown-pragmas -Wno-switch -Wno-deprecated-declarations
    -Wno-nonportable-include-path -Wno-microsoft-include -Wno-defaulted-function-deleted -Wno-invalid-offsetof -Wno-macro-redefined
    -Wno-format-security -Wno-error=format-security)
target_link_libraries(nova_engine PUBLIC Jolt box2d imgui)

# ---- 공식 패키지 (Packages/<이름>/Source): Windows 는 DLL 로 불러오지만 안드로이드는 엔진에 함께 넣는다 (Animator · 카메라 · 내비게이션 …)
#  패키지마다 같은 이름인 진입점 (NovaPackage_OnLoad 등) 은 패키지 이름을 붙여 바꾸고, 만든 nova_packages.cpp 가 차례로 부른다
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
    # 패키지 안의 외부 라이브러리 (ThirdParty/<이름>/Source + Include) — Windows 빌드와 같은 규칙
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
    message(STATUS "NOVA package (static): ${PKG_NAME}")
endforeach()
file(WRITE "${CMAKE_BINARY_DIR}/nova_packages.cpp"
    "// 만든 파일 (Android/CMakeLists.txt): 엔진에 함께 넣은 패키지의 진입점을 차례로 — App::Init 이 씬보다 먼저 부른다\n"
    "${PKG_DECLS}void NovaPackageLoaded(const char* name);\nvoid NovaAndroidLoadPackages()\n{\n${PKG_CALLS}}\n")

# ---- 진입점
add_library(nova SHARED
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/AndroidMain.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/AndroidPlatform.cpp"
    "${CMAKE_BINARY_DIR}/nova_packages.cpp"
)
# 엔진 · 패키지 전체를 넣는다 (컴포넌트가 정적 초기화로 스스로 등록 — 참조되지 않아도 남아야 한다)
target_link_libraries(nova PRIVATE -Wl,--whole-archive nova_engine ${NOVA_PACKAGE_LIBS} -Wl,--no-whole-archive native_app_glue android log EGL GLESv3 aaudio)
# NativeActivity 가 찾는 진입점을 지우지 않게
target_link_options(nova PRIVATE "-Wl,-u,ANativeActivity_onCreate" "-Wl,--no-dependent-libraries" "-Wl,--error-limit=0" "$<$<CONFIG:Release>:-Wl,--strip-debug>")   # #pragma comment(lib, ...) (Windows 라이브러리) 무시
