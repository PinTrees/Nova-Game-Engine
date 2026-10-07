#include "pch.h"
#include "AndroidEngine.h"
#include "AddComponentMenu.h"
#include "AnimationClipSelectEditorDialog.h"
#include "MeshSelectEditorDialog.h"
#include "SkeletonAvataSelectEditorDailog.h"
#include "AutoSave.h"
#include "CliServer.h"
#include "EditorCamera.h"
#include "EditorExtensions.h"
#include "EditorGUIResourceManager.h"
#include "GameViewEditorWindow.h"
#include "GraphicsBackendFactory.h"
#include "MaterialInspector.h"
#include "NovaCodeWindow.h"
#include "ObjectPicker.h"
#include "PackageManager.h"
#include "ProjectSettingsWindow.h"
#include "SceneViewOverlay.h"
#include "ScriptBindings.h"
#include "ScriptEngine.h"
#include "SpriteSlicer.h"
#include "TerrainEditor.h"
#include "TerrainSplineEditor.h"
#include "UndoSystem.h"
#include "VolumeEditor.h"
#include "WaterEditor.h"
#include "PlayerRuntime.h"
#include <Assimp/Importer.hpp>
#include <Assimp/scene.h>
#include <Assimp/material.h>

// 안드로이드 플레이어에는 에디터가 없다: 런타임 코드가 부르는 에디터 함수 (Inspector · 선택 · Undo · 대화 상자 · 창) 의 빈 구현.
//  C# 스크립트는 ScriptEngineAndroid.cpp (Mono — 게임 데이터에 런타임이 없으면 IsAvailable = false, 스크립트 컴포넌트는 조용히 아무것도 하지 않는다).
//  게임 뷰의 마우스 · 크기 (GameViewEditorWindow 의 정적 함수) 는 진짜 구현: 터치 = 마우스, 게임 화면 = 창 전체

// ---- 게임 뷰 (UI 입력 · 스크립트 입력이 쓴다)
namespace { int s_GameW = 1, s_GameH = 1; bool s_GameFocus = true; }
namespace NovaAndroid
{
	void SetGameView(int width, int height, bool focused) { s_GameW = (std::max)(1, width); s_GameH = (std::max)(1, height); s_GameFocus = focused; }
}
bool GameViewEditorWindow::HasInputFocus() { return s_GameFocus; }
// 게임 화면 = 앱 창 전체. Windows 의 Game 뷰와 같이 ImGui 마우스 (터치 이벤트 큐를 따른다) 를 Unity 화면 좌표 (왼쪽 아래 0,0) 로
bool GameViewEditorWindow::MouseToGame(float& x, float& y)
{
	const ImVec2 m = ImGui::GetIO().MousePos;
	x = m.x;
	y = (float)s_GameH - m.y;
	return m.x >= 0 && m.y >= 0 && m.x < s_GameW && m.y < s_GameH;
}
ImVec2 GameViewEditorWindow::GameToScreen(float x, float y) { return ImVec2(x, (float)s_GameH - y); }
void GameViewEditorWindow::GameSize(int& w, int& h) { w = s_GameW; h = s_GameH; }
float GameViewEditorWindow::ScrollDelta() { return 0.0f; }
void GameViewEditorWindow::SetPlayerView(int width, int height, bool focused) { NovaAndroid::SetGameView(width, height, focused); }

// ---- 에디터 싱글턴 · 정적 멤버
SINGLE_BODY(EditorGUIManager)
EditorGUIManager::EditorGUIManager() {}
EditorGUIManager::~EditorGUIManager() {}
void EditorGUIManager::RegisterWindow(EditorWindow*) {}
void EditorGUIManager::UnregisterWindow(EditorWindow*) {}
EditorWindow* EditorGUIManager::FindWindow(const std::string&) const { return nullptr; }

SINGLE_BODY(EditorGUIResourceManager)
EditorGUIResourceManager::EditorGUIResourceManager() {}
EditorGUIResourceManager::~EditorGUIResourceManager() {}
ImFont* EditorGUIResourceManager::LoadFont(EditorTextStyle) { return nullptr; }

SINGLE_BODY(SceneViewManager)
SceneViewManager::SceneViewManager() {}
SceneViewManager::~SceneViewManager() {}

EditorSetting* EditorSettingManager::m_pSetting = new EditorSetting();
void EditorSettingManager::SetLastOpenedScenePath(wstring) {}

SelectionType SelectionManager::m_SelectedType = SelectionType{};
GameObject* SelectionManager::m_SelectedGameObject = nullptr;
void SelectionManager::ClearSelection() { m_SelectedGameObject = nullptr; }
void SelectionManager::SetSelectedFile(const std::wstring&) {}
void SelectionManager::SetSelectedGameObject(GameObject* go) { m_SelectedGameObject = go; }
SelectionSubType SelectionManager::m_SelectedSubType = SelectionSubType{};
std::wstring SelectionManager::m_SelectedFilePath;
CustomSelection SelectionManager::m_Custom;
void SelectionManager::SetCustomSelection(const std::string&, std::shared_ptr<void>, std::function<void()>) {}

EditorWindow::EditorWindow(const string&, const string&) {}
EditorWindow::~EditorWindow() {}
string EditorWindow::GetImGuiName() const { return {}; }

bool SceneViewOverlay::s_Active = false;
ImVec2 SceneViewOverlay::s_Min;
ImVec2 SceneViewOverlay::s_Max;
void SceneViewOverlay::DrawLine(const XMFLOAT3&, const XMFLOAT3&, ImU32, float) {}
void SceneViewOverlay::DrawFrustum(const XMMATRIX&, float, float, float, ImU32) {}
void SceneViewOverlay::DrawLightGizmo(const XMFLOAT3&, const XMFLOAT3&, int, bool) {}
bool SceneViewOverlay::Project(const XMFLOAT3&, ImVec2&) { return false; }

// 에디터 카메라 (렌더 경로가 Scene 뷰를 그릴 때만 쓴다 — 플레이어는 Game 카메라)
float EditorCamera::GetAspect() const { return 1.0f; }
float EditorCamera::GetFarZ() const { return 1000.0f; }
float EditorCamera::GetFovY() const { return XM_PIDIV4; }
XMFLOAT3 EditorCamera::GetLook() const { return XMFLOAT3(0, 0, 1); }
XMFLOAT3 EditorCamera::GetPosition() const { return XMFLOAT3(0, 0, 0); }
XMMATRIX EditorCamera::Proj() const { return XMMatrixIdentity(); }

bool NovaCodeWindow::IsFocused() { return false; }

std::wstring EditorUtility::OpenFileDialog(const std::wstring&, const wstring, const vector<wstring>) { return {}; }
std::wstring EditorUtility::SaveFileDialog(const std::wstring&, const std::wstring&, const std::wstring&) { return {}; }

namespace LoadingScreen { void SetStatus(const std::wstring&) {} }
namespace AutoSave { void OnEnterPlay() {} }
namespace CliServer { void Register(const std::string&, const std::string&, Handler, int, bool) {} void Unregister(const std::string&) {} }
namespace EditorExtensions
{
	void RegisterAssetType(const AssetType&) {}
	void UnregisterOwner(const std::string&) {}
	void RegisterSceneTool(const std::string&, SceneTool) {}      // 패키지의 Scene 뷰 도구 (Tile Palette 붓) — 플레이어에는 Scene 뷰가 없다
	void RegisterCreateMenu(const CreateMenuItem&) {}             // GameObject 메뉴 항목
}
namespace ProjectSettingsWindow { void Open(const char*) {} }
namespace ObjectPicker
{
	void Open(const std::string&, Options) {}
	bool Poll(const std::string&, std::string&) { return false; }
}
namespace AddComponentMenu
{
	void Open(const ImVec2&, const ImVec2&) {}
	void DevPreset(const char*) {}
	void Draw(GameObject*, const std::function<void(std::shared_ptr<Component>)>&) {}
}
namespace Undo
{
	void SetActionName(const std::string&) {}
	void RequestCheck() {}
	void Touch(GameObject*) {}
	bool CommittedSceneHash(size_t&) { return false; }
	void BlockShortcuts() {}
	void WatchAsset(const std::string&, const std::string&, std::function<std::string()>, std::function<void(const std::string&)>) {}
}
namespace TerrainEditor
{
	void DrawInspector(Terrain*) {}
	bool TerrainDataField(const char*, std::string&, const char*) { return false; }
}
namespace TerrainSplineEditor
{
	void InspectorPoints(TerrainSpline&) {}
	void DrawPoints(const TerrainSpline&) {}
}
namespace VolumeEditor
{
	void DrawProfile(const std::shared_ptr<VolumeProfile>&, bool) {}
	bool ProfileField(const char*, std::string&, const std::string&, bool) { return false; }
}
namespace WaterEditor
{
	void InspectorPoints(WaterBody&) {}
	void DrawPoints(const WaterBody&) {}
}
void MaterialInspector::Draw(UMaterial&, bool) {}
void MaterialInspector::WatchUndo(const std::shared_ptr<UMaterial>&) {}
bool MaterialInspector::MaterialSlot(const char*, const std::string&, std::shared_ptr<UMaterial>&, std::wstring&) { return false; }
void AnimationClipSelectEditorDialog::Open(shared_ptr<AnimationClip>&, string&, int&) {}
void MeshSelectEditorDialog::Open(shared_ptr<Mesh>&, shared_ptr<SkinnedMesh>&, wstring&, int&, MESH_SELECT_DIALOG_TYPE) {}
void SkeletonAvataSelectEditorDailog::Open(shared_ptr<SkeletonAvataData>&, string&, int&) {}
namespace SpriteSlicer { bool Pixels::Load(const std::wstring&) { return false; } }

// ---- 그래픽 백엔드 선택 (안드로이드는 늘 OpenGL ES — GfxGLES)
std::unique_ptr<IGraphicsBackend> GraphicsBackendFactory::Create(GraphicsAPI) { return nullptr; }

// ---- 패키지 DLL (안드로이드에는 아직 없다)
namespace PackageManager
{
	const PackageInfo* Find(const std::string&) { return nullptr; }
	bool IsInProject(const std::string&) { return false; }
	std::string LoadError(const std::string&) { return "packages are not available on Android"; }
	bool Add(const std::string&, std::string& error) { error = "packages are not available on Android"; return false; }
	std::string PackageForComponent(const std::string&) { return {}; }
	bool AddForComponent(const std::string&) { return false; }
}

// ---- C# 스크립트 런타임: ScriptEngineAndroid.cpp (Mono), 엔진 API 표: Source/Scripting/ScriptBindings.cpp

// ---- Assimp (Windows 전용 미리 빌드된 라이브러리) — 안드로이드는 PC 가 구운 메시 캐시(.mesh)만 읽는다
namespace Assimp
{
	Importer::Importer() : pimpl(nullptr) {}
	Importer::~Importer() {}
	const aiScene* Importer::ReadFile(const char*, unsigned int) { return nullptr; }
	const char* Importer::GetErrorString() const { return "model import (Assimp) is not available on Android - bake the .mesh cache on the PC"; }
	bool Importer::SetPropertyInteger(const char*, int) { return false; }
}
aiReturn aiGetMaterialColor(const aiMaterial*, const char*, unsigned int, unsigned int, aiColor4D*) { return aiReturn_FAILURE; }
aiReturn aiGetMaterialString(const aiMaterial*, const char*, unsigned int, unsigned int, aiString*) { return aiReturn_FAILURE; }
// FBX 재질 꺼내기 (FBXLoader::ExtractMaterials — 편집기 전용, 플레이어는 ReadFile 이 없어 부르지 않는다)
aiReturn aiGetMaterialFloatArray(const aiMaterial*, const char*, unsigned int, unsigned int, ai_real*, unsigned int*) { return aiReturn_FAILURE; }
aiReturn aiGetMaterialTexture(const aiMaterial*, aiTextureType, unsigned int, aiString*, aiTextureMapping*, unsigned int*, ai_real*, aiTextureOp*, aiTextureMapMode*, unsigned int*) { return aiReturn_FAILURE; }
aiString aiMaterial::GetName() const { return aiString(); }
aiNode* aiNode::FindNode(const char*) { return nullptr; }

// ---- 정적 상수의 정의 (MSVC 는 없어도 되지만 clang 은 주소를 쓰면 필요)
const int RenderManager::SMapSize;

// ---- EditorApp 이 부르는 에디터 쪽 (플레이어에서는 Scene 뷰 · 툴바 · CLI · 로딩 창이 없다)
#include "SceneToolbar.h"
#include "SceneGrid.h"
#include "CliCommands.h"
#include "ModelPlacement.h"
#include "VulkanTools.h"
#include "AndroidTools.h"
#include "WebTools.h"
namespace SceneToolbar
{
	bool GridVisible() { return false; }
	bool PostProcessingVisible() { return true; }
	bool ParticlesVisible() { return true; }
	bool FogVisible() { return true; }
	bool SkyboxVisible() { return true; }
}
namespace SceneGrid { void Draw(GfxContext*, CXMMATRIX, const XMFLOAT3&) {} }
namespace CliCommands { void RegisterAll() {} }
namespace CliServer { void Start() {} }
namespace ModelPlacement { void RegisterEditor() {} }
namespace VulkanTools { void RegisterEditor() {} }
namespace AndroidTools { void RegisterEditor() {} }
namespace WebTools { void RegisterEditor() {} }
namespace LoadingScreen
{
	void Begin(const std::wstring&) {}
	void End() {}
	void SetProgress(float, const std::wstring&) {}
	void BeginShaderPhase(float, float, int) {}
}
XMMATRIX EditorCamera::View() const { return XMMatrixIdentity(); }
