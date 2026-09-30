#include "pch.h"
#include "App.h"
#include "GraphicsSettings.h"
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
#include "DisplayManager.h"
#include "GameObjectFactory.h"
#include "PhysicsSelfTest.h"

#include "EditorGUIResourceManager.h"
#include "TaskSystem.h"

namespace
{
	// This is just used to forward Windows messages from a global window
	// procedure to our member function window procedure because we cannot
	// assign a member function to WNDCLASS::lpfnWndProc.
	App* gApp = 0;
}

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
			// Global Update
			TimeManager::GetI()->Update();
			InputManager::GetI()->Update();

			// Handle
			SceneManager::GetI()->HandleSaveScene();

			if (!_appPaused)
			{
				CalculateFrameStats();

				// Update
				if (Application::ShouldUpdateGame())
				{
					UpdateScene(_timer.DeltaTime()); 
					SceneManager::GetI()->UpdateScene(); 
					PhysicsManager::GetI()->Update(_timer.DeltaTime()); 
				}

				// Editor Update
				SceneViewManager::GetI()->Update();
				EditorGUIManager::GetI()->Update();

				// OnPreCull, 렌더 직전 매트릭스 연산 등
				
				DisplayManager::GetI()->GetActiveCamera()->ViewUpdate();
				LightManager::GetI()->ViewUpdates();
				LightManager::GetI()->EditorViewUpdates();

				// Render
				RenderApplication();

				//Editor Render
				EditorGUIManager::GetI()->RenderEditorWindows();

				ImGui::Render(); 
				ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData()); 

				EditorGUIManager::GetI()->RenderAfter();

				// Render End
				HR(_swapChain->Present(0, 0)); 

				// Last Frame
				SceneManager::GetI()->GetCurrentScene()->LastFramUpdate();
				SceneManager::GetI()->LastUpdate();

				TaskSystem::ExecuteMainThreadTasks();
			}
			else
			{
				::Sleep(100);
			}
        }
    }

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
	if (!InitMainWindow())
		return false;

	GraphicsSettings::Init();
	log << "App::Init -> InitDirect3D (backend: " << GraphicsSettings::GetBackend()->GetName() << ", requested: " << GraphicsAPIToString(GraphicsSettings::GetRequestedAPI()) << ")..." << std::endl; log.flush();
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

	log << "App::Init -> EditorSettingManager..." << std::endl; log.flush();
	EditorSettingManager::Init();

	log << "App::Init -> RenderManager..." << std::endl; log.flush();
	RenderManager::GetI()->Init();
	PostProcessingManager::GetI()->Init();

	log << "App::Init -> EditorGUIManager..." << std::endl; log.flush();
	EditorGUIManager::GetI()->Init();
	EditorGUIManager::GetI()->OnResize(GetScreenSize());
	EditorGUIManager::GetI()->RegisterWindow(new SceneEditorWindow);  
	EditorGUIManager::GetI()->RegisterWindow(new SceneHierachyEditorWindow);
	EditorGUIManager::GetI()->RegisterWindow(new InspectorEditorWindow); 
	EditorGUIManager::GetI()->RegisterWindow(new GameViewEditorWindow);
	EditorGUIManager::GetI()->RegisterWindow(new ProjectEditorWindow);
	EditorGUIManager::GetI()->RegisterWindow(new ConsoleEditorWindow);
	EditorGUIManager::GetI()->RegisterWindow(new AnimatorEditorWindow);

	log << "App::Init -> ResourceManager & InputManager..." << std::endl; log.flush();
	ResourceManager::GetI()->Init(_device);
	InputManager::GetI()->Init();

	log << "App::Init -> SceneManager..." << std::endl; log.flush();
	SceneManager::GetI()->Init();
	PhysicsManager::GetI()->Init();

	log << "App::Init -> LoadScene..." << std::endl; log.flush();
	SceneManager::GetI()->LoadStartupScene();

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

	// (개발/검증용) NOVA_PHYSICS_TEST=<로그 파일> 이면 물리 자체 검사 장면을 추가한다 (저장하지 않음)
	if (::GetEnvironmentVariableA("NOVA_PHYSICS_TEST", nullptr, 0) > 0 && SceneManager::GetI()->GetCurrentScene())
		PhysicsSelfTest::Build(SceneManager::GetI()->GetCurrentScene());

	{
		char parentLog[512] = {};
		if (::GetEnvironmentVariableA("NOVA_PARENT_TEST", parentLog, sizeof(parentLog)) > 0 && SceneManager::GetI()->GetCurrentScene())
			PhysicsSelfTest::RunParentTest(SceneManager::GetI()->GetCurrentScene(), parentLog);
	}

	// (개발/검증용) NOVA_AUTOPLAY=1 이면 시작 직후 Play 모드로 들어간다.
	if (::GetEnvironmentVariableA("NOVA_AUTOPLAY", nullptr, 0) > 0)
	{
		Application::SetPlaying(true);
		SceneManager::GetI()->HandlePlay();
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
	assert(_swapChain);

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
 
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT App::MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
		return true;

	switch( msg )
	{
	case WM_ACTIVATE:
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

	// Compute window rectangle dimensions based on requested client area dimensions.
	RECT R = { 0, 0, _clientWidth, _clientHeight };
	::AdjustWindowRect(&R, WS_OVERLAPPEDWINDOW, false);
	int32 width  = R.right - R.left;
	int32 height = R.bottom - R.top;

	int32 posX = CW_USEDEFAULT;
	int32 posY = CW_USEDEFAULT;
	if (_centerWindow)
	{
		posX = max(0, (::GetSystemMetrics(SM_CXSCREEN) - width) / 2);
		posY = max(0, (::GetSystemMetrics(SM_CYSCREEN) - height) / 2);
	}

	_hMainWnd = ::CreateWindow(L"NovaEngineWindow", _mainWindowCaption.c_str(), WS_OVERLAPPEDWINDOW, posX, posY, width, height, 0, 0, _hAppInst, 0); 
	
	if (_hMainWnd == nullptr)
	{
		::MessageBox(0, L"CreateWindow Failed.", 0, 0);
		return false;
	}

	SendMessage(_hMainWnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
	SendMessage(_hMainWnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconSmall);

	::ShowWindow(_hMainWnd, SW_SHOW);
	::UpdateWindow(_hMainWnd);

	return true;
}

bool App::InitDirect3D()
{
	std::ofstream log(_logFileName, std::ios::app);
	log << "  InitDirect3D -> CreateDeviceAndSwapChain..." << std::endl; log.flush();
	CreateDeviceAndSwapChain();	
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

		std::wostringstream outs;   
		outs.precision(6);
		std::wstring caption = _mainWindowCaption;
		if (Scene* scene = SceneManager::GetI()->GetCurrentScene())
			caption = scene->GetName() + (SceneManager::GetI()->IsCurrentSceneDirty() ? L"*" : L"") + L" - " + caption;
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
		_device.GetAddressOf(),
		nullptr,
		_deviceContext.GetAddressOf()
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
			_device.GetAddressOf(),
			nullptr,
			_deviceContext.GetAddressOf()
		);
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

	ComPtr<ID3D11Texture2D> backBuffer = nullptr;
	hr = _swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)backBuffer.GetAddressOf());
	log << "    GetBuffer hr: " << hr << std::endl; log.flush();
	CHECK(hr);

	hr = _device->CreateRenderTargetView(backBuffer.Get(), nullptr, _renderTargetView.GetAddressOf());
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

		HRESULT hr = _device->CreateTexture2D(&desc, nullptr, _depthStencilBuffer.GetAddressOf());
		log << "    CreateTexture2D DSV hr: " << hr << std::endl; log.flush();
		CHECK(hr);
	}

	{
		D3D11_DEPTH_STENCIL_VIEW_DESC desc;
		ZeroMemory(&desc, sizeof(desc));
		desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
		desc.Texture2D.MipSlice = 0;

		HRESULT hr = _device->CreateDepthStencilView(_depthStencilBuffer.Get(), nullptr, _depthStencilView.GetAddressOf());
		log << "    CreateDepthStencilView hr: " << hr << std::endl; log.flush();
		CHECK(hr);
	}
}
