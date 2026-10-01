#include "pch.h"
#include "PackageManager.h"
#include "PackageManagerWindow.h"
#include "App.h"
#include "GraphicsSettings.h"
#include "GfxGL.h"
#include "ImGuiGL.h"
#include "AudioManager.h"
#include "ScriptEngine.h"
#include "UISystem.h"
#include "ParticleSystem.h"
#include "Tree.h"
#include "Terrain.h"
#include "TerrainData.h"
#include "TerrainEditor.h"
#include "TerrainGenerator.h"
#include "SceneCulling.h"
#include "FrameProfiler.h"
#include "PlayerRuntime.h"
#include "IGraphicsBackend.h"
#include "resource.h"
#include <WindowsX.h>
#include <sstream>

#include "SceneHierachyEditorWindow.h"
#include "SceneEditorWindow.h"
#include "ProjectEditorWindow.h"
#include "InspectorEditorWindow.h"
#include "GameViewEditorWindow.h"
#include "ConsoleEditorWindow.h"
#include "AnimatorEditorWindow.h"
#include "NovaCodeWindow.h"
#include "ProfilerEditorWindow.h"
#include "MeshBatcher.h"
#include "TreeRenderer.h"
#include "RockRenderer.h"
#include "DetailRenderer.h"
#include "RenderStats.h"
#include "DisplayManager.h"
#include "GameObjectFactory.h"
#include "PhysicsSelfTest.h"

#include "EditorGUIResourceManager.h"
#include "TaskSystem.h"
#include "UndoSystem.h"
#include "CliServer.h"

namespace
{
	// This is just used to forward Windows messages from a global window
	// procedure to our member function window procedure because we cannot
	// assign a member function to WNDCLASS::lpfnWndProc.
	App* gApp = 0;
}


// 창이 작업 영역보다 커서 최대화로 열어야 하는지
static bool s_StartMaximized = false;

LRESULT CALLBACK
MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	// Forward hwnd on because we can get messages (e.g., WM_CREATE)
	// before CreateWindow returns, and thus before mhMainWnd is valid.
	return gApp->MsgProc(hwnd, msg, wParam, lParam);
}

App::App(HINSTANCE hInstance)
{
	_hAppInst = hInstance;   // 리소스(아이콘) 로드와 윈도우 클래스 등록에 필요
	//ZeroMemory(&_viewport, sizeof(D3D11_VIEWPORT));

	// Get a pointer to the application object so we can forward 
	// Windows messages to the object's window procedure through
	// the global window procedure.
	Application::GetI()->SetApp(this);
	gApp = this;
}

App::~App()
{
	ScriptEngine::Shutdown();
	AudioManager::Shutdown();
	if (_deviceContext)
		_deviceContext->ClearState();
}

int32 App::Run()
{
	MSG msg = {0};
 
	_timer.Reset();

	while (msg.message != WM_QUIT)
	{
		if (::PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
		{
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
		}
		else
        {	
			_timer.Tick();
			EditorLog::Heartbeat();   // 멈춤 감시 ([HANG] 호출 스택)
			// Global Update
			TimeManager::GetI()->Update();
			InputManager::GetI()->Update();

			// Handle
			SceneManager::GetI()->HandleSceneShortcuts();

			// 숨긴 채 첫 프레임을 준비하는 중, NOVA CLI 요청을 처리하는 중에는 멈추지 않는다 (포커스 없이 CLI 로 다룰 수 있게)
			if (!_appPaused || _deferredShow || (!Application::IsPlayer() && CliServer::HasWork()))
			{
				CalculateFrameStats();
				Profiler::BeginFrame();   // Profiler 창 (Window > Analysis > Profiler)

				// Update
				if (Application::ShouldUpdateGame())
				{
					FRAME_PROFILE("GameUpdate");
					{ PROFILE_SCOPE("Scripts.BeginFrame"); ScriptEngine::BeginFrame(); }   // C# 입력 상태 / 시간
					{ PROFILE_SCOPE("Scene.Update"); UpdateScene(_timer.DeltaTime()); SceneManager::GetI()->UpdateScene(); }
					{ PROFILE_SCOPE("Physics.Update"); PhysicsManager::GetI()->Update(_timer.DeltaTime()); }
				}

				// Editor Update
				{
					PROFILE_SCOPE("EditorUpdate");
					SceneViewManager::GetI()->Update();
					EditorGUIManager::GetI()->Update();
				}
				{ PROFILE_SCOPE("Audio.Update"); AudioManager::Update(); }   // 리스너 위치, 일시정지, One Shot 정리, 통계
				{ PROFILE_SCOPE("Scripts.Update"); ScriptEngine::Update(); }   // C# 스크립트 변경 감시 / 컴파일 / 다시 읽기
				{ PROFILE_SCOPE("UI.Update"); UISystem::Update(); }       // UI 레이아웃 (RectTransform), Play 중 버튼 입력
				{ PROFILE_SCOPE("Particles.Update"); ParticleSystem::UpdateAll(); }   // 입자: Play 중이면 게임 시간, 아니면 선택한 시스템 미리보기
				Tree::UpdateAll();             // 나무 바람 시간 (이 프레임의 모든 패스가 같은 값)
				if (!Application::IsPlayer())
				{
					PROFILE_SCOPE("TerrainGenerator");
					TerrainGenerator::Update();   // 지형 생성기: 바뀐 지형을 백그라운드에서 다시 만들고 끝난 결과를 적용
				}

				// (개발/검증용) 입력 없이 확인할 때: NOVA_DEV_SELECT=<GameObject 이름>, NOVA_DEV_FILE=<프로젝트 기준 파일 경로> 를 시작 뒤 한 번 선택
				{
					static bool s_DevSelected = false;
					char devName[260] = {};
					if (!s_DevSelected && ImGui::GetFrameCount() > 90)
					{
						s_DevSelected = true;
						if (::GetEnvironmentVariableA("NOVA_DEV_SELECT", devName, sizeof(devName)) > 0)
							if (Scene* scene = SceneManager::GetI()->GetCurrentScene())
								for (GameObject* go : scene->GetAllGameObjects())
									if (go->GetName() == devName)
									{
										SelectionManager::SetSelectedGameObject(go);
										break;
									}
						if (::GetEnvironmentVariableA("NOVA_DEV_FILE", devName, sizeof(devName)) > 0)
							SelectionManager::SetSelectedFile(PathManager::GetI()->GetMovePathW(string_to_wstring(devName)));
						// NOVA_DEV_TREES=<수>: 첫 지형에 (없으면) Oak/Pine/Birch 프로토타입을 넣고 그만큼 흩뿌린다 (저장은 하지 않음)
						if (::GetEnvironmentVariableA("NOVA_DEV_TREES", devName, sizeof(devName)) > 0 && !Terrain::GetActiveTerrains().empty())
						{
							Terrain* terrain = Terrain::GetActiveTerrains()[0];
							if (auto data = terrain->GetTerrainData())
							{
								if (data->TreePrototypes.empty())
									for (int preset : { 0, 1, 2 })
										TerrainEditor::AddTreePrototype(*data, preset);
								TerrainEditor::MassPlaceTrees(terrain, atoi(devName));
							}
						}
					}
				}

				// OnPreCull, 렌더 직전 매트릭스 연산 등
				
				{
					PROFILE_SCOPE("Camera/Light Update");
					if (auto activeCamera = DisplayManager::GetI()->GetActiveCamera())   // 카메라가 없는 씬도 있다
						activeCamera->ViewUpdate();
					LightManager::GetI()->ViewUpdates();
					LightManager::GetI()->EditorViewUpdates();
				}
				{
					FRAME_PROFILE("CullingUpdate");
					SceneCulling::Update(SceneManager::GetI()->GetCurrentScene());   // 절두체 컬링 옥트리 (움직인 렌더러만 다시 넣음)
				}

				// Render
				{
					FRAME_PROFILE("RenderApplication");
					RenderApplication();
				}

				if (Application::IsPlayer())
				{
					// 빌드된 게임: 에디터 창 대신 창 전체에 카메라 + UI
					PlayerRuntime::Render(_renderTargetView.Get(), _depthStencilView.Get(), _clientWidth, _clientHeight, ::GetForegroundWindow() == _hMainWnd);
				}
				else
				{
					//Editor Render
					{ PROFILE_SCOPE("Editor Windows"); EditorGUIManager::GetI()->RenderEditorWindows(); }
					{ PROFILE_SCOPE("Undo"); Undo::Update(); }   // Ctrl+Z / Ctrl+Y, 조작이 끝난 변경을 기록
				}

				{
					PROFILE_SCOPE("ImGui Render");
					PROFILE_GPU("ImGui");
					ImGui::Render(); 
					// 메인 창 백버퍼를 매번 다시 묶는다: 떠 있는 팝업·툴팁이 창 밖으로 나가 ImGui 가 별도 OS 창(뷰포트)을 만들면
					// RenderPlatformWindowsDefault 가 그 창의 타깃을 묶은 채로 끝나 다음 프레임부터 메인 창이 멈춘 듯 보였다
					_deviceContext->OMSetRenderTargets(1, _renderTargetView.GetAddressOf(), nullptr);
					if (_openGL)
						ImGuiGL::RenderDrawData(ImGui::GetDrawData());
					else
						ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData()); 
					EditorGUIManager::GetI()->RenderAfter();
				}

				// Render End
				{
					FRAME_PROFILE("Present");
					if (!Application::IsPlayer())
						CliServer::PumpBeforePresent();   // NOVA CLI: 에디터 전체 캡처 (백버퍼가 다 그려진 뒤)
					HRESULT presentHr = S_OK;
					if (_openGL)
						GfxGL::Present(_device.Get(), _backBufferTex.Get(), _clientWidth, _clientHeight, 0);
					else
						presentHr = _swapChain->Present(0, 0);
					// 실패(장치 제거 등)는 한 번 기록한다 (Release 에서는 HR 의 assert 가 없다)
					static bool s_PresentFailLogged = false;
					if (FAILED(presentHr) && !s_PresentFailLogged)
					{
						s_PresentFailLogged = true;
						EditorLog::Write("App", "Present failed hr=0x%08X, device removed reason=0x%08X", (unsigned)presentHr, (unsigned)_device->GetDeviceRemovedReason());
					}
				}

				// 첫 프레임(도킹 배치가 잡히도록 두 번째 프레임)이 그려지면 에디터 창을 보이고 로딩 창을 닫는다
				if (_deferredShow && ++_shownFrames >= 2)
				{
					_deferredShow = false;
					::ShowWindow(_hMainWnd, Application::noActivate ? SW_SHOWNOACTIVATE : (s_StartMaximized ? SW_SHOWMAXIMIZED : SW_SHOW));
					::UpdateWindow(_hMainWnd);
					if (!Application::noActivate)
						::SetForegroundWindow(_hMainWnd);
					LoadingScreen::End();
				}

				// Last Frame
				SceneManager::GetI()->GetCurrentScene()->LastFramUpdate();
				SceneManager::GetI()->LastUpdate();
				if (Scene* scene = SceneManager::GetI()->GetCurrentScene())
					scene->FlushDestroyed();   // 이번 프레임에 지운 오브젝트를 delete (렌더·UI 가 다 쓴 뒤)

				{ PROFILE_SCOPE("Main Thread Tasks"); TaskSystem::ExecuteMainThreadTasks(); }
				if (!Application::IsPlayer())
				{
					PROFILE_SCOPE("NOVA CLI");
					CliServer::Pump();
				}
				FrameProfiler::EndFrame();
				RecordProfilerStats();
				Profiler::EndFrame();
				DevGpuProfileLog();
			}
			else
			{
				CliServer::WaitForWork(100);   // CLI 요청이 오면 바로 깨어난다
			}
        }
    }

	CliServer::Stop();   // NOVA CLI: 인스턴스 파일 지우기
	EditorGUIManager::GetI()->Destroy(); 
	EditorGUIManager::GetI()->Dispose(); 

	ResourceManager::GetI()->Destroy();
	ResourceManager::GetI()->Dispose(); 

	return (int)msg.wParam;
}

bool App::InitPlatform()
{
	std::ofstream log(_logFileName, std::ios::app);
	log << "App::Init -> InitMainWindow..." << std::endl; log.flush();
	LoadingScreen::SetProgress(0.02f, L"Initializing graphics device");   // 어떤 API 인지는 아래 [Graphics] 줄에
	if (!InitMainWindow())
		return false;

	GraphicsSettings::Init(Application::IsPlayer() ? PlayerRuntime::GraphicsAPIs() : std::vector<GraphicsAPI>{});
	log << "App::Init -> InitDirect3D (" << GraphicsSettings::SelectionLog() << ")..." << std::endl; log.flush();
	EditorLog::Write("Graphics", "%s", GraphicsSettings::SelectionLog().c_str());
	if (!InitDirect3D())
		return false;

	return true;
}

bool App::Init()
{
	std::ofstream log(_logFileName, std::ios::app);
	if (!InitPlatform())
		return false;

	log << "App::Init -> PathManager..." << std::endl; log.flush();
	PathManager::GetI()->Init();
	LoadingScreen::SetSubtitle(std::filesystem::path(PathManager::GetI()->GetContentPathW()).parent_path().filename().wstring());
	LoadingScreen::SetProgress(0.06f, L"Loading editor settings");

	log << "App::Init -> EditorSettingManager..." << std::endl; log.flush();
	EditorSettingManager::Init();

	log << "App::Init -> RenderManager..." << std::endl; log.flush();
	RenderManager::GetI()->Init();
	PostProcessingManager::GetI()->Init();

	log << "App::Init -> EditorGUIManager..." << std::endl; log.flush();
	LoadingScreen::SetProgress(0.08f, L"Initializing editor UI");
	// 빌드된 게임은 ImGui 를 입력(마우스/키보드 상태)용으로만 쓴다 (도킹/다중 뷰포트 없이 → 마우스 = 창 좌표)
	EditorGUIManager::GetI()->Init(Application::IsPlayer());
	EditorGUIManager::GetI()->OnResize(GetScreenSize());
	if (!Application::IsPlayer())
	{
	EditorGUIManager::GetI()->RegisterWindow(new SceneEditorWindow);
	EditorGUIManager::GetI()->RegisterWindow(new SceneHierachyEditorWindow);
	EditorGUIManager::GetI()->RegisterWindow(new InspectorEditorWindow); 
	EditorGUIManager::GetI()->RegisterWindow(new GameViewEditorWindow);
	EditorGUIManager::GetI()->RegisterWindow(new ProjectEditorWindow);
	EditorGUIManager::GetI()->RegisterWindow(new ConsoleEditorWindow);
	EditorGUIManager::GetI()->RegisterWindow(new AnimatorEditorWindow);
	EditorGUIManager::GetI()->RegisterWindow(new NovaCodeWindow);   // 스크립트를 열 때 나타남 (기본 External Script Editor)
	EditorGUIManager::GetI()->RegisterWindow(new ProfilerEditorWindow);   // Window > Analysis > Profiler (Ctrl+7)
	EditorGUIManager::GetI()->RegisterWindow(new PackageManagerWindow);   // Window > Package Manager
	}

	log << "App::Init -> ResourceManager & InputManager..." << std::endl; log.flush();
	ResourceManager::GetI()->Init(_device);
	InputManager::GetI()->Init();

	log << "App::Init -> PackageManager..." << std::endl; log.flush();
	PackageManager::Init();   // 프로젝트 패키지(DLL) — 씬보다 먼저 (패키지 컴포넌트 등록)

	log << "App::Init -> SceneManager..." << std::endl; log.flush();
	SceneManager::GetI()->Init();
	PhysicsManager::GetI()->Init();

	{ extern void FbxDumpRun(); FbxDumpRun(); }   // (개발/검증용) NOVA_FBX_DUMP
	log << "App::Init -> LoadScene..." << std::endl; log.flush();
	LoadingScreen::SetProgress(0.10f, L"Loading scene");
	SceneManager::GetI()->LoadStartupScene();
	LoadingScreen::SetProgress(0.20f, L"Scene loaded");

	// (개발/검증용) NOVA_DEV_CREATE=cube,sphere,capsule,cylinder,plane,quad 이면 시작 시 기본 도형을 만들어 씬에 저장한다.
	{
		char list[256] = {};
		if (::GetEnvironmentVariableA("NOVA_DEV_CREATE", list, sizeof(list)) > 0 && SceneManager::GetI()->GetCurrentScene())
		{
			Scene* scene = SceneManager::GetI()->GetCurrentScene();
			std::string names = list;
			float x = -4.0f;
			size_t pos = 0;
			while (pos <= names.size())
			{
				size_t next = names.find(',', pos);
				std::string n = names.substr(pos, next == std::string::npos ? std::string::npos : next - pos);
				GameObject* g = nullptr;
				if (n == "cube") g = GameObjectFactory::CreateCube();
				else if (n == "sphere") g = GameObjectFactory::CreateSphere();
				else if (n == "capsule") g = GameObjectFactory::CreateCapsule();
				else if (n == "cylinder") g = GameObjectFactory::CreateCylinder();
				else if (n == "plane") g = GameObjectFactory::CreatePlane();
				else if (n == "quad") g = GameObjectFactory::CreateQuad();
				if (g)
				{
					g->GetTransform()->SetPosition(Vec3(x, 0.5f, 0.0f));
					scene->AddRootGameObject(g);
					x += 1.6f;
				}
				if (next == std::string::npos) break;
				pos = next + 1;
			}
			Scene::Save(scene);
		}
	}

	// (개발/검증용) NOVA_DEV_PHYSICS=1 이면 물리 확인용 오브젝트를 씬에 추가한다 (저장하지 않음).
	if (::GetEnvironmentVariableA("NOVA_DEV_PHYSICS", nullptr, 0) > 0 && SceneManager::GetI()->GetCurrentScene())
	{
		Scene* scene = SceneManager::GetI()->GetCurrentScene();
		auto add = [&](GameObject* g, const Vec3& pos, const std::string& name) {
			g->SetName(name);
			g->GetTransform()->SetPosition(pos);
			scene->AddRootGameObject(g);
			return g;
		};
		GameObject* floor = add(GameObjectFactory::CreatePlane(), Vec3(0.0f, 0.0f, 0.0f), "PhysFloor");
		floor->GetTransform()->SetLocalScale(Vec3(2.0f, 1.0f, 2.0f));
		for (int i = 0; i < 4; ++i)
			add(GameObjectFactory::CreateCube(), Vec3(0.0f, 0.5f + i * 1.05f, 3.0f), "PhysStack" + std::to_string(i))->AddComponent<RigidBody>();
		GameObject* tilted = add(GameObjectFactory::CreateCube(), Vec3(3.0f, 6.0f, 3.0f), "PhysTilted");
		tilted->GetTransform()->SetLocalEulerAngles(Vec3(30.0f, 20.0f, 45.0f));
		tilted->AddComponent<RigidBody>();
		GameObject* ball = add(GameObjectFactory::CreateSphere(), Vec3(-3.0f, 0.5f, 3.0f), "PhysBall");
		ball->AddComponent<RigidBody>()->SetVelocity(Vec3(0.0f, 0.0f, -4.0f));
		add(GameObjectFactory::CreateCapsule(), Vec3(-1.5f, 4.0f, 3.0f), "PhysCapsule")->AddComponent<RigidBody>();
		RigidBody* ghost = add(GameObjectFactory::CreateSphere(), Vec3(3.0f, 3.0f, -2.0f), "PhysGhost")->AddComponent<RigidBody>();
		ghost->SetExcludeLayers(1u << 0);   // Default 레이어 제외 → 바닥을 통과
		GameObject* trigger = add(GameObjectFactory::CreateCube(), Vec3(-3.0f, 0.5f, 0.0f), "PhysTrigger");
		trigger->GetComponent<BoxCollider>()->SetIsTrigger(true);
		trigger->GetTransform()->SetLocalScale(Vec3(2.0f, 1.0f, 1.0f));
	}

	// (개발/검증용) NOVA_PHYSICS_TEST=<로그 파일> 이면 물리 자체 검사 장면을 추가한다 (저장하지 않음)
	if (::GetEnvironmentVariableA("NOVA_PHYSICS_TEST", nullptr, 0) > 0 && SceneManager::GetI()->GetCurrentScene())
		PhysicsSelfTest::Build(SceneManager::GetI()->GetCurrentScene());

	{
		char parentLog[512] = {};
		if (::GetEnvironmentVariableA("NOVA_PARENT_TEST", parentLog, sizeof(parentLog)) > 0 && SceneManager::GetI()->GetCurrentScene())
			PhysicsSelfTest::RunParentTest(SceneManager::GetI()->GetCurrentScene(), parentLog);
		char animLog[512] = {};
		if (::GetEnvironmentVariableA("NOVA_ANIM_TEST", animLog, sizeof(animLog)) > 0 && SceneManager::GetI()->GetCurrentScene())
			PhysicsSelfTest::RunAnimationTest(SceneManager::GetI()->GetCurrentScene(), animLog);
		char animatorLog[512] = {};
		if (::GetEnvironmentVariableA("NOVA_ANIMATOR_TEST", animatorLog, sizeof(animatorLog)) > 0 && SceneManager::GetI()->GetCurrentScene())
			PhysicsSelfTest::RunAnimatorTest(SceneManager::GetI()->GetCurrentScene(), animatorLog);
		char terrainLog[512] = {};
		if (::GetEnvironmentVariableA("NOVA_TERRAIN_TEST", terrainLog, sizeof(terrainLog)) > 0 && SceneManager::GetI()->GetCurrentScene())
			PhysicsSelfTest::RunTerrainTest(SceneManager::GetI()->GetCurrentScene(), terrainLog);
		char prefabLog[512] = {};
		if (::GetEnvironmentVariableA("NOVA_PREFAB_TEST", prefabLog, sizeof(prefabLog)) > 0 && SceneManager::GetI()->GetCurrentScene())
			PhysicsSelfTest::RunPrefabTest(SceneManager::GetI()->GetCurrentScene(), prefabLog);
		char audioLog[512] = {};
		if (::GetEnvironmentVariableA("NOVA_AUDIO_TEST", audioLog, sizeof(audioLog)) > 0 && SceneManager::GetI()->GetCurrentScene())
			PhysicsSelfTest::RunAudioTest(SceneManager::GetI()->GetCurrentScene(), audioLog);
		char scriptLog[512] = {};
		if (::GetEnvironmentVariableA("NOVA_SCRIPT_TEST", scriptLog, sizeof(scriptLog)) > 0 && SceneManager::GetI()->GetCurrentScene())
			PhysicsSelfTest::RunScriptTest(SceneManager::GetI()->GetCurrentScene(), scriptLog);
	}

	// (개발/검증용) NOVA_SELECT=<오브젝트 이름> 이 지정되면 시작 시 해당 오브젝트를 선택해 Inspector 확인을 돕는다.
	{
		char selectName[128] = {};
		if (::GetEnvironmentVariableA("NOVA_SELECT", selectName, sizeof(selectName)) > 0 && SceneManager::GetI()->GetCurrentScene())
		{
			for (GameObject* go : SceneManager::GetI()->GetCurrentScene()->GetAllGameObjects())
			{
				if (go->GetName() == selectName)
				{
					SelectionManager::SetSelectedGameObject(go);
					break;
				}
			}
		}
	}

	// (개발/검증용) NOVA_AUTOPLAY=1 이면 시작 직후 Play 모드로 들어간다.
	if (::GetEnvironmentVariableA("NOVA_AUTOPLAY", nullptr, 0) > 0)
	{
		ScriptEngine::Init();   // 스크립트를 먼저 읽어야 Play 의 Awake/Start 가 C# 까지 간다
		if (ScriptEngine::CanEnterPlayMode())
		{
			Application::SetPlaying(true);
			SceneManager::GetI()->HandlePlay();
		}
	}

	log << "App::Init -> TimeManager..." << std::endl; log.flush();
	TimeManager::GetI()->Init();

	log << "App::Init finished successfully!" << std::endl; log.flush();
	return true;
}
 
void App::OnResize()
{
	assert(_deviceContext);
	assert(_device);
	if (_clientWidth <= 0 || _clientHeight <= 0)
		return;   // 최소화 등
	if (_openGL)
	{
		// OpenGL: 백버퍼 = 창 크기 RGBA8 텍스처 (+ 깊이). Present 가 창으로 위아래 뒤집어 복사
		_deviceContext->OMSetRenderTargets(0, nullptr, nullptr);
		_renderTargetView.Reset();
		_backBufferTex.Reset();
		_depthStencilView.Reset();
		_depthStencilBuffer.Reset();
		D3D11_TEXTURE2D_DESC td = {};
		td.Width = (UINT)_clientWidth;
		td.Height = (UINT)_clientHeight;
		td.MipLevels = 1;
		td.ArraySize = 1;
		td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		td.SampleDesc.Count = 1;
		td.Usage = D3D11_USAGE_DEFAULT;
		td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		const HRESULT hr = _device->CreateTexture2D(&td, nullptr, _backBufferTex.GetAddressOf());
		if (SUCCEEDED(hr))
			_device->CreateRenderTargetView(_backBufferTex.Get(), nullptr, _renderTargetView.GetAddressOf());
		EditorLog::Write("App", "resize backbuffer to %d x %d (OpenGL, hr=0x%08X)", _clientWidth, _clientHeight, (unsigned)hr);
		CreateDepthStencilView();
		_deviceContext->OMSetRenderTargets(1, _renderTargetView.GetAddressOf(), _depthStencilView.Get());
		EditorGUIManager::GetI()->OnResize(Vec2(_clientWidth, _clientHeight));
		return;
	}
	assert(_swapChain);

	// 백버퍼를 실제 창 크기로 바꾼다. 이전에는 처음 크기 그대로 두고 화면에 늘려 그려서,
	// 창 크기가 바뀌면 그려진 위치와 마우스 좌표가 어긋났다 (창 크기에 따라 클릭 위치가 틀어지던 문제).
	_deviceContext->OMSetRenderTargets(0, nullptr, nullptr);
	_renderTargetView.Reset();
	_depthStencilView.Reset();
	_depthStencilBuffer.Reset();
	_deviceContext->Flush();
	const HRESULT resizeHr = _swapChain->ResizeBuffers(0, (UINT)_clientWidth, (UINT)_clientHeight, DXGI_FORMAT_UNKNOWN, 0);
	{
		DXGI_SWAP_CHAIN_DESC scd = {};
		_swapChain->GetDesc(&scd);
		EditorLog::Write("App", "resize backbuffer to %d x %d (hr=0x%08X, now %u x %u)", _clientWidth, _clientHeight, (unsigned)resizeHr, scd.BufferDesc.Width, scd.BufferDesc.Height);
	}

	CreateRenderTargetView();
	CreateDepthStencilView();

	// Bind the render target view and depth/stencil view to the pipeline.`
	_deviceContext->OMSetRenderTargets(1, _renderTargetView.GetAddressOf(), _depthStencilView.Get());

	EditorGUIManager::GetI()->OnResize(Vec2(_clientWidth, _clientHeight));
	
	// Set the viewport transform.
	//_viewport.TopLeftX = 0;
	//_viewport.TopLeftY = 0;
	//_viewport.Width    = static_cast<float>(_clientWidth);
	//_viewport.Height   = static_cast<float>(_clientHeight);
	//_viewport.MinDepth = 0.0f;
	//_viewport.MaxDepth = 1.0f;
	//_deviceContext->RSSetViewports(1, &_viewport); 
}

void App::RenderApplication()
{

}
 
// 개발용: NOVA_DEV_GPUPROFILE 이 있으면 Profiler 를 켜 두고 4 초마다 최근 60 프레임의 GPU 구간 평균(깊이 3 까지)을 Editor.log 에
//  (Profiler 창을 띄우지 않는 자동 검사에서 기능별 GPU 비용을 재기 위함)
void App::DevGpuProfileLog()
{
	static const bool s_On = ::GetEnvironmentVariableA("NOVA_DEV_GPUPROFILE", nullptr, 0) > 0;
	if (!s_On)
		return;
	Profiler::SetCollecting(true);
	static uint64_t s_Frames = 0;
	if (++s_Frames % 240 != 0)
		return;
	const auto& history = Profiler::History();
	std::map<std::string, std::pair<double, int>> sums;
	std::map<std::string, std::pair<double, double>> work;   // 삼각형 / 픽셀 (프레임 평균)
	double frameSum = 0.0;
	int frames = 0;
	for (auto it = history.rbegin(); it != history.rend() && frames < 60; ++it)
	{
		if (it->GpuMs < 0.0f)
			continue;
		++frames;
		frameSum += it->GpuMs;
		for (const Profiler::GpuSample& g : it->Gpu)
			if (g.Depth <= 3)
			{
				const std::string key = std::string(g.Depth, '.') + g.Name;
				auto& s = sums[key];
				s.first += g.Ms;
				++s.second;
				auto& w = work[key];
				w.first += (double)g.Primitives;
				w.second += (double)g.Pixels;
			}
	}
	if (frames == 0)
		return;
	std::string line;
	for (const auto& [name, s] : sums)
	{
		const double ms = s.first / frames;
		if (ms < 0.05)
			continue;
		char buf[160];
		const auto& w = work[name];
		if (w.first > 0.0)
			snprintf(buf, sizeof(buf), " %s=%.2f(%.0fk tri %.0fk px)", name.c_str(), ms, w.first / frames / 1000.0, w.second / frames / 1000.0);
		else
			snprintf(buf, sizeof(buf), " %s=%.2f", name.c_str(), ms);
		line += buf;
	}
	EditorLog::Write("GpuProfile", "%d frames, gpu %.2f ms:%s", frames, frameSum / frames, line.c_str());
}

// Profiler 창의 프레임 통계 (이번 프레임에 그린 화면만)
void App::RecordProfilerStats()
{
	if (!Profiler::Collecting())
		return;
	using Profiler::SetStat;
	if (Profiler::CurrentFrameHas("GameView render"))
	{
		const RenderStats::Frame& rs = RenderStats::Last;
		SetStat("Game View/Draw Calls", rs.DrawCalls);
		SetStat("Game View/Saved by Batching", rs.SavedByBatching);
		SetStat("Game View/Triangles", (double)rs.Triangles);
		SetStat("Game View/Vertices", (double)rs.Vertices);
		SetStat("Game View/Shadow Casters", rs.ShadowCasters);
		const MeshBatcher::Stats& mb = MeshBatcher::LastStats(false);
		SetStat("Game View/Mesh Renderers", mb.Objects);
		SetStat("Game View/Mesh Batches", mb.Batches);
		const SceneCulling::Stats& cs = SceneCulling::LastStats(false);
		SetStat("Game View/Culling Visible", cs.Visible);
		SetStat("Game View/Culling Total", cs.Objects);
		const TreeRenderer::Stats& ts = TreeRenderer::LastStats(false);
		SetStat("Game View/Trees", ts.Trees);
		SetStat("Game View/Trees LOD0", ts.Lod0);
		SetStat("Game View/Trees LOD1", ts.Lod1);
		SetStat("Game View/Trees Billboard", ts.Billboards);
		SetStat("Game View/Tree Draw Calls", ts.DrawCalls);
		const RockRenderer::Stats& rks = RockRenderer::LastStats(false);
		SetStat("Game View/Rocks", rks.Rocks);
		SetStat("Game View/Rock Draw Calls", rks.DrawCalls);
		const DetailRenderer::Stats& ds = DetailRenderer::LastStats(false);
		SetStat("Game View/Details", ds.Instances);
		SetStat("Game View/Detail Draw Calls", ds.DrawCalls);
	}
	if (Profiler::CurrentFrameHas("SceneView render"))
	{
		const MeshBatcher::Stats& mb = MeshBatcher::LastStats(true);
		SetStat("Scene View/Mesh Renderers", mb.Objects);
		SetStat("Scene View/Mesh Batches", mb.Batches);
		const SceneCulling::Stats& cs = SceneCulling::LastStats(true);
		SetStat("Scene View/Culling Visible", cs.Visible);
		SetStat("Scene View/Culling Total", cs.Objects);
		SetStat("Scene View/Octree Nodes Visited", cs.NodesVisited);
		const TreeRenderer::Stats& ts = TreeRenderer::LastStats(true);
		SetStat("Scene View/Trees", ts.Trees);
		SetStat("Scene View/Trees LOD0", ts.Lod0);
		SetStat("Scene View/Trees LOD1", ts.Lod1);
		SetStat("Scene View/Trees Billboard", ts.Billboards);
		SetStat("Scene View/Tree Draw Calls", ts.DrawCalls);
		const RockRenderer::Stats& rks = RockRenderer::LastStats(true);
		SetStat("Scene View/Rocks", rks.Rocks);
		SetStat("Scene View/Rocks LOD0", rks.Lod0);
		SetStat("Scene View/Rocks LOD1", rks.Lod1);
		SetStat("Scene View/Rocks LOD2", rks.Lod2);
		SetStat("Scene View/Rock Draw Calls", rks.DrawCalls);
		const DetailRenderer::Stats& ds = DetailRenderer::LastStats(true);
		SetStat("Scene View/Details", ds.Instances);
		SetStat("Scene View/Detail Chunks", ds.Chunks);
		SetStat("Scene View/Detail Draw Calls", ds.DrawCalls);
	}
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT App::MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
		return true;

	switch( msg )
	{
	case WM_ACTIVATE:
		if (Application::IsPlayer() && PlayerRuntime::RunInBackground())
			return 0;   // Player Settings > Run In Background: 다른 창을 봐도 게임은 계속
		if( LOWORD(wParam) == WA_INACTIVE )
		{
			_appPaused = true;
			_timer.Stop();
		}
		else
		{
			_appPaused = false;
			_timer.Start();
		}
		return 0;

	case WM_SIZE:
	{
		UINT dpi = 0;
		dpi = GetDpiForWindow(hwnd);
		float scaleFactor = dpi / 96.0f;            

		int width = LOWORD(lParam);
		int height = HIWORD(lParam);

		int physicalWidth = static_cast<int>(width * scaleFactor);
		int physicalHeight = static_cast<int>(height * scaleFactor);

		_clientWidth = width;
		_clientHeight = height;

		if( _device )
		{
			if( wParam == SIZE_MINIMIZED )
			{
				_appPaused = true;
				_minimized = true;
				_maximized = false;
			}
			else if( wParam == SIZE_MAXIMIZED )
			{
				_appPaused = false;
				_minimized = false;
				_maximized = true;
				OnResize();
			}
			else if( wParam == SIZE_RESTORED )
			{
				
				if( _minimized )
				{
					_appPaused = false;
					_minimized = false;
					OnResize();
				}

				else if( _maximized )
				{
					_appPaused = false;
					_maximized = false;
					OnResize();
				}
				else if( _resizing )
				{
				}
				else 
				{
					OnResize();
				}
			}
		}
		return 0;
	}
	// WM_EXITSIZEMOVE is sent when the user grabs the resize bars.
	case WM_ENTERSIZEMOVE:
		_appPaused = true;
		_resizing  = true;
		_timer.Stop();
		return 0;

	// WM_EXITSIZEMOVE is sent when the user releases the resize bars.
	// Here we reset everything based on the new window dimensions.
	case WM_EXITSIZEMOVE:
		_appPaused = false;
		_resizing  = false;
		_timer.Start();
		OnResize();
		return 0;
 
	// 창 닫기 (X, Alt+F4, File > Exit, nova quit): 에디터에서 저장 안 한 씬이 있으면 Unity 처럼
	// "Save / Don't Save / Cancel" 을 물은 뒤 닫는다 (예전에는 묻지 않고 꺼져 작업이 사라졌다)
	case WM_CLOSE:
	{
		static bool s_Confirmed = false;
		SceneManager* sm = SceneManager::GetI();
		if (!s_Confirmed && !sm->CloseWithoutPrompt() && !Application::IsPlayer() && sm->GetCurrentScene() != nullptr)
		{
			if (sm->IsScenePromptOpen())
				return 0;   // 이미 묻는 중
			if (Application::IsPlaying())
				sm->TogglePlayFromEditor();   // Play 중이면 먼저 멈춘다 (Play 중에는 저장할 수 없다)
			if (sm->IsCurrentSceneDirty())
			{
				if (::IsIconic(hwnd)) ::ShowWindow(hwnd, SW_RESTORE);
				::SetForegroundWindow(hwnd);   // 비활성 창이면 멈춰 있어 확인 창이 그려지지 않는다
				_appPaused = false;
				sm->RequestSceneChange([hwnd]() { s_Confirmed = true; ::PostMessageW(hwnd, WM_CLOSE, 0, 0); });
				return 0;
			}
		}
		break;   // DefWindowProc → DestroyWindow
	}

	// WM_DESTROY is sent when the window is being destroyed.
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;

	// The WM_MENUCHAR message is sent when a menu is active and the user presses 
	// a key that does not correspond to any mnemonic or accelerator key. 
	case WM_MENUCHAR:
        // Don't beep when we alt-enter.
        return MAKELRESULT(0, MNC_CLOSE);

	// Catch this message so to prevent the window from becoming too small.
	case WM_GETMINMAXINFO:
		((MINMAXINFO*)lParam)->ptMinTrackSize.x = 200;
		((MINMAXINFO*)lParam)->ptMinTrackSize.y = 200; 
		return 0;

	case WM_LBUTTONDOWN:
	case WM_MBUTTONDOWN:
	case WM_RBUTTONDOWN:
		OnMouseDown(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		return 0;
	case WM_LBUTTONUP:
	case WM_MBUTTONUP:
	case WM_RBUTTONUP:
		OnMouseUp(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		return 0;
	case WM_MOUSEMOVE:
		OnMouseMove(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		return 0;
	}

	return DefWindowProc(hwnd, msg, wParam, lParam);
}


void App::SetScreenSize(UINT width, UINT height)
{
	_clientHeight = height;
	_clientWidth = width;

	OnResize();
}

bool App::InitMainWindow()
{
	// DPI 인식을 설정
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2); 

	// 작업 표시줄(큰 아이콘)과 제목 표시줄(작은 아이콘)에 각각 알맞은 크기의 로고 아이콘을 사용한다.
	HICON hIcon = (HICON)LoadImage(_hAppInst, MAKEINTRESOURCE(IDI_MAIN_ICON), IMAGE_ICON,
		GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR);
	HICON hIconSmall = (HICON)LoadImage(_hAppInst, MAKEINTRESOURCE(IDI_MAIN_ICON), IMAGE_ICON,
		GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
	if (!hIcon)
	{
		hIcon = (HICON)LoadImage(NULL, L"ProjectSetting\\icon.ico", IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE);
	}
	if (!hIconSmall)
		hIconSmall = hIcon;
	if (!hIcon)
	{
		hIcon = LoadIcon(0, IDI_APPLICATION);
	}

	WNDCLASS wc;
	wc.style         = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc   = MainWndProc; 
	wc.cbClsExtra    = 0;
	wc.cbWndExtra    = 0;
	wc.hInstance     = _hAppInst;
	wc.hIcon         = hIcon;
	wc.hCursor       = LoadCursor(0, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
	wc.lpszMenuName  = 0;
	wc.lpszClassName = L"NovaEngineWindow";

	if (!RegisterClass(&wc))
	{
		::MessageBox(0, L"RegisterClass Failed.", 0, 0);
		return false;
	}

	// 빌드된 게임: Player Settings 의 창 모드 (Fullscreen Window = 테두리 없이 모니터 전체, Maximized, Windowed)
	if (Application::IsPlayer())
	{
		_mainWindowCaption = PlayerRuntime::ProductName();
		const int mode = PlayerRuntime::FullscreenMode();
		HMONITOR mon = ::MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
		MONITORINFO mi = { sizeof(mi) };
		::GetMonitorInfoW(mon, &mi);
		DWORD style = WS_OVERLAPPEDWINDOW;
		int x = CW_USEDEFAULT, y = CW_USEDEFAULT, w = 1280, h = 720;
		if (mode == 0)
		{
			style = WS_POPUP;
			x = mi.rcMonitor.left; y = mi.rcMonitor.top;
			w = mi.rcMonitor.right - mi.rcMonitor.left;
			h = mi.rcMonitor.bottom - mi.rcMonitor.top;
		}
		else
		{
			if (!PlayerRuntime::Resizable())
				style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
			int cw = 1280, ch = 720;
			PlayerRuntime::WindowSize(cw, ch);
			RECT R = { 0, 0, cw, ch };
			::AdjustWindowRect(&R, style, false);
			w = R.right - R.left;
			h = R.bottom - R.top;
			const RECT& work = mi.rcWork;
			x = work.left + (std::max)(0L, ((work.right - work.left) - w) / 2);
			y = work.top + (std::max)(0L, ((work.bottom - work.top) - h) / 2);
		}
		_hMainWnd = ::CreateWindow(L"NovaEngineWindow", _mainWindowCaption.c_str(), style, x, y, w, h, 0, 0, _hAppInst, 0);
		if (_hMainWnd == nullptr)
			return false;
		RECT client = {};
		::GetClientRect(_hMainWnd, &client);
		_clientWidth = (std::max)(1L, client.right - client.left);
		_clientHeight = (std::max)(1L, client.bottom - client.top);
		SendMessage(_hMainWnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
		SendMessage(_hMainWnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconSmall);
		::ShowWindow(_hMainWnd, mode == 1 ? SW_SHOWMAXIMIZED : SW_SHOW);
		::UpdateWindow(_hMainWnd);
		return true;
	}

	// Compute window rectangle dimensions based on requested client area dimensions.
	RECT R = { 0, 0, _clientWidth, _clientHeight };
	::AdjustWindowRect(&R, WS_OVERLAPPEDWINDOW, false);
	int32 width  = R.right - R.left;
	int32 height = R.bottom - R.top;

	// 요청 크기가 작업 영역(작업 표시줄 제외)보다 크면 창이 작업 표시줄 아래로 들어가 하단 UI 가 가려진다.
	// 그런 경우에는 작업 영역에 맞추고 최대화 상태로 연다 (Unity 에디터와 같음).
	RECT work = {};
	::SystemParametersInfo(SPI_GETWORKAREA, 0, &work, 0);
	const int32 workW = work.right - work.left;
	const int32 workH = work.bottom - work.top;
	if (workW > 0 && workH > 0 && (width > workW || height > workH))
	{
		width = min(width, workW);
		height = min(height, workH);
		s_StartMaximized = true;
	}

	int32 posX = CW_USEDEFAULT;
	int32 posY = CW_USEDEFAULT;
	if (_centerWindow || s_StartMaximized)   // 작업 영역에 맞춘 창은 CW_USEDEFAULT 의 계단식 오프셋을 받으면 다시 밖으로 나간다
	{
		posX = work.left + max(0, (workW - width) / 2);
		posY = work.top + max(0, (workH - height) / 2);
	}

	_hMainWnd = ::CreateWindow(L"NovaEngineWindow", _mainWindowCaption.c_str(), WS_OVERLAPPEDWINDOW, posX, posY, width, height, 0, 0, _hAppInst, 0); 
	
	if (_hMainWnd == nullptr)
	{
		::MessageBox(0, L"CreateWindow Failed.", 0, 0);
		return false;
	}

	SendMessage(_hMainWnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
	SendMessage(_hMainWnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconSmall);

	// 로딩 창이 떠 있으면 첫 프레임이 준비될 때까지 숨겨 둔다 (흰 창이 멈춘 것처럼 보이지 않게)
	_deferredShow = LoadingScreen::IsActive();
	if (!_deferredShow)
	{
		::ShowWindow(_hMainWnd, Application::noActivate ? SW_SHOWNOACTIVATE : (s_StartMaximized ? SW_SHOWMAXIMIZED : SW_SHOW));
		::UpdateWindow(_hMainWnd);
	}

	return true;
}

// OpenGL: 본 창에 GL 4.5 컨텍스트 → Gfx·RHI 장치. 화면 표시는 백버퍼 텍스처를 Present 가 창으로 복사
bool App::InitOpenGL()
{
	std::string error;
	GfxDevice* dev = nullptr;
	GfxContext* ctx = nullptr;
	if (!GfxGL::CreateDevice(_hMainWnd, &dev, &ctx, error))
	{
		EditorLog::Write("Graphics", "OpenGL device failed: %s", error.c_str());
		return false;
	}
	_device.Attach(dev);
	_deviceContext.Attach(ctx);
	Gfx::SetMain(_device.Get(), _deviceContext.Get());
	std::unique_ptr<Rhi::Device> rhi = GfxGL::CreateRhiDevice(_device.Get(), _deviceContext.Get(), error);
	if (!rhi)
	{
		EditorLog::Write("Graphics", "OpenGL RHI device failed: %s", error.c_str());
		return false;
	}
	Rhi::SetMain(std::move(rhi));
	_openGL = true;
	OnResize();
	return true;
}

bool App::InitDirect3D()
{
	if (GraphicsSettings::GetActiveAPI() == GraphicsAPI::OpenGL)
	{
		if (InitOpenGL())
			return true;
		EditorLog::Write("Graphics", "%s", "OpenGL failed to start - using DirectX 11");
		GraphicsSettings::FallBack(GraphicsAPI::DirectX11, "OpenGL could not start");   // 제목줄·설정 창의 "Running With" 가 맞도록
		Rhi::SetMain(nullptr);
		Gfx::SetMain(nullptr, nullptr);
		_deviceContext.Reset();
		_device.Reset();
	}
	std::ofstream log(_logFileName, std::ios::app);
	log << "  InitDirect3D -> CreateDeviceAndSwapChain..." << std::endl; log.flush();
	CreateDeviceAndSwapChain();
	// 렌더러·효과가 쓰는 RHI 장치 (지금은 이 D3D11 장치를 감쌈)
	Rhi::SetMain(Rhi::WrapD3D11(_device.Get(), _deviceContext.Get()));
	log << "  InitDirect3D -> OnResize..." << std::endl; log.flush();
	OnResize();
	log << "  InitDirect3D finished successfully!" << std::endl; log.flush();

	return true;
}

void App::CalculateFrameStats()
{
	// Code computes the average frames per second, and also the 
	// average time it takes to render one frame.  These stats 
	// are appended to the window caption bar.

	static int frameCnt = 0;
	static float timeElapsed = 0.0f;

	frameCnt++;

	// Compute averages over one second period.
	if( (_timer.TotalTime() - timeElapsed) >= 1.0f )
	{
		float fps = (float)frameCnt; // fps = frameCnt / 1
		float mspf = 1000.0f / fps;

		if (Application::IsPlayer())
		{
			// 빌드된 게임: 제목은 제품 이름 그대로 (씬 이름/FPS 를 붙이지 않음)
			frameCnt = 0;
			timeElapsed += 1.0f;
			return;
		}
		std::wostringstream outs;
		outs.precision(6);
		std::wstring caption = _mainWindowCaption;
		if (Scene* scene = SceneManager::GetI()->GetCurrentScene())
			caption = scene->GetName() + (SceneManager::GetI()->IsCurrentSceneDirty() ? L"*" : L"") + L" - " + caption;
		// Unity 처럼 지금 렌더링 중인 그래픽 API 를 제목에 (<DX11>, <OpenGL>)
		caption += GraphicsSettings::GetActiveAPI() == GraphicsAPI::OpenGL ? L" <OpenGL>" : L" <DX11>";
		outs << caption << L"    "  << L"FPS: " << fps << L"    "  << L"Frame Time: " << mspf << L" (ms)";

		::SetWindowText(_hMainWnd, outs.str().c_str());
		
		// Reset for next average.
		frameCnt = 0;
		timeElapsed += 1.0f;
	}
}

void App::CreateDeviceAndSwapChain()
{
	uint32 createDeviceFlags = 0;
#if defined(DEBUG) || defined(_DEBUG)  
	createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	DXGI_SWAP_CHAIN_DESC desc;
	ZeroMemory(&desc, sizeof(desc));
	{
		desc.BufferDesc.Width = _clientWidth;
		desc.BufferDesc.Height = _clientHeight;
		desc.BufferDesc.RefreshRate.Numerator = 60;
		desc.BufferDesc.RefreshRate.Denominator = 1;
		desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
		desc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;

		if (_enable4xMsaa)
		{
			desc.SampleDesc.Count = 4;
			desc.SampleDesc.Quality = _4xMsaaQuality - 1;
		}
		else
		{
			desc.SampleDesc.Count = 1;
			desc.SampleDesc.Quality = 0;
		}
		
		desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		desc.BufferCount = 1;
		desc.OutputWindow = _hMainWnd;
		desc.Windowed = true;
		desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
		desc.Flags = 0;
	}

	ComPtr<ID3D11Device> d3dDevice;
	ComPtr<ID3D11DeviceContext> d3dContext;
	HRESULT hr = ::D3D11CreateDeviceAndSwapChain(
		nullptr, // default adapter
		_driverType,
		nullptr, // no software device
		createDeviceFlags,
		nullptr,
		0,
		D3D11_SDK_VERSION,
		&desc,
		_swapChain.GetAddressOf(),
		d3dDevice.GetAddressOf(),
		nullptr,
		d3dContext.GetAddressOf()
	);

	if (FAILED(hr) && (createDeviceFlags & D3D11_CREATE_DEVICE_DEBUG))
	{
		createDeviceFlags &= ~D3D11_CREATE_DEVICE_DEBUG;
		hr = ::D3D11CreateDeviceAndSwapChain(
			nullptr,
			_driverType,
			nullptr,
			createDeviceFlags,
			nullptr,
			0,
			D3D11_SDK_VERSION,
			&desc,
			_swapChain.GetAddressOf(),
			d3dDevice.GetAddressOf(),
			nullptr,
			d3dContext.GetAddressOf()
		);
	}
	// 엔진은 Gfx 층으로 쓴다 (DirectX 11 = D3D11 객체를 감쌈)
	if (SUCCEEDED(hr))
	{
		_device.Attach(Gfx::WrapD3D11As<GfxDevice>(d3dDevice.Get()));
		_deviceContext.Attach(Gfx::WrapD3D11As<GfxContext>(d3dContext.Get()));
		Gfx::SetMain(_device.Get(), _deviceContext.Get());
	}

	std::ofstream log(_logFileName, std::ios::app);
	log << "    D3D11CreateDeviceAndSwapChain final hr: " << hr << std::endl; log.flush();

	CHECK(hr);
}


void App::CreateRenderTargetView()
{
	std::ofstream log(_logFileName, std::ios::app);
	log << "    CreateRenderTargetView entry..." << std::endl; log.flush();

	HRESULT hr;

	ComPtr<ID3D11Texture2D> d3dBackBuffer;
	hr = _swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)d3dBackBuffer.GetAddressOf());
	log << "    GetBuffer hr: " << hr << std::endl; log.flush();
	CHECK(hr);
	ComPtr<GfxTexture2D> backBuffer;
	backBuffer.Attach(Gfx::WrapD3D11As<GfxTexture2D>(d3dBackBuffer.Get()));

	hr = _device->CreateRenderTargetView(backBuffer.Get(), nullptr, _renderTargetView.ReleaseAndGetAddressOf());
	log << "    CreateRenderTargetView hr: " << hr << std::endl; log.flush();
	CHECK(hr);
}

void App::CreateDepthStencilView()
{
	std::ofstream log(_logFileName, std::ios::app);
	log << "    CreateDepthStencilView entry..." << std::endl; log.flush();

	{
		D3D11_TEXTURE2D_DESC desc = { 0 };
		ZeroMemory(&desc, sizeof(desc));
		desc.Width = _clientWidth;
		desc.Height = _clientHeight;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;

		if (_enable4xMsaa)
		{
			desc.SampleDesc.Count = 4;
			desc.SampleDesc.Quality = _4xMsaaQuality - 1;
		}
		else
		{
			desc.SampleDesc.Count = 1;
			desc.SampleDesc.Quality = 0;
		}
	
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = 0;

		HRESULT hr = _device->CreateTexture2D(&desc, nullptr, _depthStencilBuffer.ReleaseAndGetAddressOf());
		log << "    CreateTexture2D DSV hr: " << hr << std::endl; log.flush();
		CHECK(hr);
	}

	{
		D3D11_DEPTH_STENCIL_VIEW_DESC desc;
		ZeroMemory(&desc, sizeof(desc));
		desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
		desc.Texture2D.MipSlice = 0;

		HRESULT hr = _device->CreateDepthStencilView(_depthStencilBuffer.Get(), nullptr, _depthStencilView.ReleaseAndGetAddressOf());
		log << "    CreateDepthStencilView hr: " << hr << std::endl; log.flush();
		CHECK(hr);
	}
}
