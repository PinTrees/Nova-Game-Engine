#pragma once


#include "Types.h"
#include "define.h"
#include "Matrix3.h"

// STL
#include <memory>
#include <memory.h>
#include <iostream>
#include <array>
#include <vector>
#include <list>
#include <map>
#include <deque>
#include <queue>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>

#include <thread>
#include <future>		// std::async, std::future
#include <chrono>		// std::chrono::seconds
#include <coroutine>
using namespace std;

// WIN
#include <windows.h>
#include <assert.h>
#include <optional>

// DX
#include <d3d11.h>
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <d3d11.h>
#include <wrl.h>
#include <DirectXMath.h>
using namespace DirectX;
using namespace Microsoft::WRL;

// IMGUI
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "ImGui/imgui.h"
#include "ImGui/imgui_impl_dx11.h" 
#include "ImGui/imgui_impl_win32.h" 
#include "ImGuiNodeEditor/imgui_node_editor.h" 
namespace ed = ax::NodeEditor;  

// FONT AWSEAM
#include "IconsFontAwesome/IconsFontAwesome6.h"

// LIB
#include <DirectXTex/DirectXTex.h>
#include <DirectXTex/DirectXTex.inl>
#include <FX11/d3dx11effect.h>
#include "Gfx.h"     // 그래픽 API 층 (D3D11 모양) — 엔진 코드는 ID3D11Xxx 대신 GfxXxx
#include "RhiFx.h"   // Effects11 모양 래퍼 (RHI 위) — 엔진 코드는 Effects11 타입 대신 FxEffect·FxVar·FxTechnique 를 쓴다

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

#ifdef _DEBUG
#pragma comment(lib, "DirectXTex/DirectXTex_debug.lib")
#pragma comment(lib, "FX11/Effects11d.lib")
#pragma comment(lib, "Assimp/assimp-vc143-mtd.lib")
#else
#pragma comment(lib, "DirectXTex/DirectXTex.lib")
#pragma comment(lib, "FX11/Effects11.lib")
// Release: 저장소 루트에 assimp-vc143-mt.dll(5.4.1, /MD)이 있으면 CMake 가 NOVA_ASSIMP_RELEASE_DLL 을 정의해 그것을 링크한다
// (없으면 디버그 런타임 DLL(mtd) — 엔진은 Assimp::Importer(구현은 포인터 하나 뒤) + C 구조체(aiScene)만 주고받아 런타임이 달라도 안전)
#ifdef NOVA_ASSIMP_RELEASE_DLL
#pragma comment(lib, "Assimp/assimp-vc143-mt.lib")
#else
#pragma comment(lib, "Assimp/assimp-vc143-mtd.lib")
#endif
#endif

// EXTENSIONS
#include "vector_ex.h"

// ENGINE
#include "ComponentFactory.h"
#include "PostProcessingManager.h"
#include "DisplayManager.h"
#include "Task.h"

// EDITOR - ONLY
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
#include "Animator.h"
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

