#pragma once
// 안드로이드 빌드의 "pch.h" — 엔진 소스의 #include "pch.h" 가 이 파일을 찾는다 (Android/CMakeLists.txt 의 include 순서).
//  Windows 의 Source/Platform/pch.h 와 같은 목록. Windows 전용 헤더(windows.h · d3d11.h · wrl.h · DirectXTex …)는
//  Android/Include 의 대체가 받는다 (WinCompat.h). ImGui 백엔드(dx11 · win32) · Effects11 은 넣지 않는다

// STL
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <functional>
#include <future>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "WinCompat.h"
#include "Types.h"
#include "define.h"
#include "Matrix3.h"
using namespace std;

// DX (대체)
#include <d3d11.h>
#include <wrl.h>
#include <DirectXMath.h>
using namespace DirectX;
using namespace Microsoft::WRL;

// IMGUI (코어만 — 컴포넌트 Inspector 코드가 컴파일되도록. 안드로이드 플레이어는 부르지 않는다)
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "ImGui/imgui.h"
#include "ImGuiNodeEditor/imgui_node_editor.h"
namespace ed = ax::NodeEditor;
#include "IconsFontAwesome/IconsFontAwesome6.h"

// LIB
#include <DirectXTex/DirectXTex.h>
#include "Gfx.h"
#include "RhiFx.h"

// EXTENSIONS
#include "vector_ex.h"

// ENGINE
#include "ComponentFactory.h"
#include "PostProcessingManager.h"
#include "DisplayManager.h"
#include "Task.h"

// EDITOR - ONLY (선언만 — 안드로이드에서는 Android/Source 의 빈 구현)
#include "EditorUtility.h"
#include "EditorWindow.h"
#include "EditorDailog.h"
#include "EditorGUIManager.h"
#include "EditorSettingManager.h"
#include "SelectionManager.h"
#include "SceneViewManager.h"

#include "Application.h"
#include "RenderManager.h"

#include "PathManager.h"
#include "LoadingScreen.h"
#include "EditorLog.h"
#include "ResourceManager.h"
#include "InputManager.h"

#include "Shader.h"
#include "UMaterial.h"

// Data
#include "Mesh.h"
#include "SkinnedMesh.h"

// Component
#include "GameObject.h"
#include "AnimationPlayer.h"
#include "MeshFilter.h"
#include "MeshRenderer.h"
#include "SkinnedMeshRenderer.h"
#include "Camera.h"
#include "RigidBody.h"
#include "SphereCollider.h"
#include "Light.h"
#include "Scene.h"
#include "SceneManager.h"

#include "Transform.h"
#include "Collider.h"
#include "BoxCollider.h"
#include "CapsuleCollider.h"
#include "MeshCollider.h"
#include "TerrainData.h"
#include "Terrain.h"
#include "TerrainCollider.h"
#include "PhysicsManager.h"
#include "TimeManager.h"

#include "Gizmo.h"
#include "VectorUtils.h"

// SHADER
#include "NormalMapSkinnedShader.h"

namespace Constant
{
	// Transform NDC space [-1,+1]^2 to texture space [0,1]^2
	const XMMATRIX toTexSpace(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f);
}
