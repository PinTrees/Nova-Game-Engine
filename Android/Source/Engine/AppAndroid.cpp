#include "pch.h"
#include "App.h"
#include "AndroidEngine.h"
#include "GfxGLES.h"
#include "Physics2DManager.h"
#include "AudioManager.h"
#include "ShaderGraphRuntime.h"
#include "ScriptEngine.h"
#include "UISystem.h"
#include "ParticleSystem.h"
#include "LineRenderer.h"
#include "Tree.h"
#include "SceneCulling.h"
#include "LightManager.h"
#include "PlayerRuntime.h"
#include "TaskSystem.h"

// Source/Platform/App.cpp 의 안드로이드 판 (Windows 창 · 메시지 루프 · 스왑 체인 대신).
//  - 그래픽: 지금 현재인 EGL 컨텍스트 위의 GfxGLES (데스크톱 OpenGL 경로와 같이 _openGL = true, 백버퍼 = RGBA8 텍스처)
//  - Run() = **한 프레임** (Windows 판의 루프 한 바퀴 — 플레이어 부분만). 안드로이드 루프 (AndroidMain) 가 프레임마다 부르고,
//    그 뒤 GfxGLES::Present + eglSwapBuffers 로 화면에
//  - ImGui 는 입력용으로만 (Windows 플레이어와 같음 — UISystem 이 ImGui IO 의 마우스를 읽는다): 백엔드 없이, 터치 = 마우스
namespace
{
	void ImGuiInit(int width, int height)
	{
		if (ImGui::GetCurrentContext()) return;
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.IniFilename = nullptr;
		io.DisplaySize = ImVec2((float)width, (float)height);
		unsigned char* pixels = nullptr;
		int w = 0, h = 0;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);   // 글꼴 표 (그리지는 않지만 NewFrame 이 요구)
	}

	void ImGuiNewFrame(int width, int height, float dt)
	{
		ImGuiIO& io = ImGui::GetIO();
		io.DisplaySize = ImVec2((float)(std::max)(width, 1), (float)(std::max)(height, 1));
		io.DeltaTime = dt > 0.0f ? dt : 1.0f / 60.0f;
		// 지난 프레임 뒤의 손가락 이벤트를 차례로 (ImGui 가 한 프레임에 하나씩 풀어 빠른 탭도 UI 가 '눌림 → 뗌' 으로 받는다)
		const std::vector<NovaAndroid::PointerEvent> events = NovaAndroid::TakePointerEvents();
		for (const auto& e : events)
		{
			io.AddMousePosEvent(e.X, e.Y);
			io.AddMouseButtonEvent(0, e.Down);
		}
		if (events.empty())
		{
			POINT p;
			GetCursorPos(&p);
			io.AddMousePosEvent((float)p.x, (float)p.y);
		}
		ImGui::NewFrame();
	}
}

App::App(HINSTANCE hInstance)
{
	_hAppInst = hInstance;
	Application::GetI()->SetApp(this);
}

App::~App()
{
	ScriptEngine::Shutdown();
	AudioManager::Shutdown();
	if (_deviceContext)
		_deviceContext->ClearState();
	if (ImGui::GetCurrentContext())
		ImGui::DestroyContext();
}

bool App::InitPlatform()
{
	std::string error;
	GfxDevice* dev = nullptr;
	GfxContext* ctx = nullptr;
	if (!GfxGLES::CreateDevice(&dev, &ctx, error))
	{
		EditorLog::Write("Graphics", "OpenGL ES device failed: %s", error.c_str());
		return false;
	}
	_device.Attach(dev);
	_deviceContext.Attach(ctx);
	Gfx::SetMain(_device.Get(), _deviceContext.Get());
	std::unique_ptr<Rhi::Device> rhi = GfxGLES::CreateRhiDevice(_device.Get(), _deviceContext.Get(), error);
	if (!rhi)
	{
		EditorLog::Write("Graphics", "OpenGL ES RHI device failed: %s", error.c_str());
		return false;
	}
	Rhi::SetMain(std::move(rhi));
	_openGL = true;
	EditorLog::Write("Graphics", "OpenGL ES (Android) %d x %d", _clientWidth, _clientHeight);
	OnResize();
	return true;
}

// 패키지 진입점을 부른 뒤 (nova_packages.cpp) 하나씩 로그로
void NovaPackageLoaded(const char* name)
{
	EditorLog::Write("Packages", "%s (static)", name);
}

// Windows 판 App::Init 과 같은 순서 (에디터 창 · 개발용 환경 변수 빼고, 패키지는 DLL 대신 엔진에 함께 넣은 것)
bool App::Init()
{
	if (!InitPlatform())
		return false;
	PathManager::GetI()->Init();
	RenderManager::GetI()->Init();
	PostProcessingManager::GetI()->Init();
	ImGuiInit(_clientWidth, _clientHeight);
	ResourceManager::GetI()->Init(_device);
	InputManager::GetI()->Init();
	ShaderGraph::InitRuntime();
	{ extern void NovaAndroidLoadPackages(); NovaAndroidLoadPackages(); }   // 엔진에 함께 넣은 패키지 (Windows 의 PackageManager::Init — 씬보다 먼저)
	SceneManager::GetI()->Init();
	PhysicsManager::GetI()->Init();
	SceneManager::GetI()->LoadStartupScene();
	TimeManager::GetI()->Init();
	EditorLog::Write("App", "Android init finished: scene %s", SceneManager::GetI()->GetCurrentScene() ? "loaded" : "none");
	return true;
}

void App::OnResize()
{
	if (!_device || !_deviceContext || _clientWidth <= 0 || _clientHeight <= 0)
		return;
	NovaAndroid::SetGameView(_clientWidth, _clientHeight, true);   // 첫 프레임 그리기 전 (C# Start 의 Screen.width · safeArea) 부터 창 크기
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
	CreateDepthStencilView();
	_deviceContext->OMSetRenderTargets(1, _renderTargetView.GetAddressOf(), _depthStencilView.Get());
	EditorLog::Write("App", "resize backbuffer to %d x %d (OpenGL ES, hr=0x%08X)", _clientWidth, _clientHeight, (unsigned)hr);
}

void App::CreateDepthStencilView()
{
	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = (UINT)_clientWidth;
	desc.Height = (UINT)_clientHeight;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	if (FAILED(_device->CreateTexture2D(&desc, nullptr, _depthStencilBuffer.ReleaseAndGetAddressOf())))
		return;
	D3D11_DEPTH_STENCIL_VIEW_DESC dsv = {};
	dsv.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	dsv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	_device->CreateDepthStencilView(_depthStencilBuffer.Get(), &dsv, _depthStencilView.ReleaseAndGetAddressOf());
}

void App::RenderApplication() {}

void App::SetScreenSize(UINT width, UINT height)
{
	_clientWidth = (int32)width;
	_clientHeight = (int32)height;
}

LRESULT App::MsgProc(HWND, UINT, WPARAM, LPARAM) { return 0; }

// 한 프레임 (Windows 판 App::Run 의 루프 한 바퀴 중 플레이어가 하는 일)
int32 App::Run()
{
	static bool s_Started = false;
	if (!s_Started) { _timer.Reset(); s_Started = true; }
	if (NovaAndroid::TakeTimeReset())   // 뒤에 있던 동안은 시간이 흐르지 않는다 (Unity 와 같이)
	{
		_timer.Reset();
		TimeManager::GetI()->Init();
	}
	_timer.Tick();
	const float dt = _timer.DeltaTime();
	TimeManager::GetI()->Update();
	NovaAndroid::BeginInputFrame();   // 탭 고정 · 터치 (Input.GetTouch)
	InputManager::GetI()->Update();
	for (const Touch& t : InputManager::GetI()->GetTouches())   // 검사 · 진단: 엔진 Input 이 받은 터치
		if (t.phase == TouchPhase::Began || t.phase == TouchPhase::Ended)
			EditorLog::Write("Input", "touch %d %s at %.0f,%.0f (touchCount %d, mouse0 %s)", t.fingerId, t.phase == TouchPhase::Began ? "began" : "ended",
				t.position.x, t.position.y, (int)InputManager::GetI()->GetTouches().size(), (GetAsyncKeyState(VK_LBUTTON) & 0x8000) ? "down" : "up");
	ImGuiNewFrame(_clientWidth, _clientHeight, dt);

	if (Application::ShouldUpdateGame())
	{
		ScriptEngine::BeginFrame();
		UpdateScene(dt);
		SceneManager::GetI()->UpdateScene();
		PhysicsManager::GetI()->Update(dt);
		Physics2DManager::Update(dt);
	}
	AudioManager::Update();
	ShaderGraph::UpdateRuntime();
	ScriptEngine::Update();
	UISystem::Update();
	ParticleSystem::UpdateAll();
	TrailRenderer::UpdateAll();   // Trail Renderer: 점 더하기 · 오래된 점 빼기
	Tree::UpdateAll();

	if (auto activeCamera = DisplayManager::GetI()->GetActiveCamera())
		activeCamera->ViewUpdate();
	LightManager::GetI()->ViewUpdates();
	SceneCulling::Update(SceneManager::GetI()->GetCurrentScene());

	RenderApplication();
	PlayerRuntime::Render(_renderTargetView.Get(), _depthStencilView.Get(), _clientWidth, _clientHeight, GetFocus() != nullptr);
	ImGui::Render();   // 그리지 않는다 (입력용 프레임 마무리)

	if (Scene* scene = SceneManager::GetI()->GetCurrentScene())
		scene->LastFramUpdate();
	SceneManager::GetI()->LastUpdate();
	if (Scene* scene = SceneManager::GetI()->GetCurrentScene())
		scene->FlushDestroyed();
	TaskSystem::ExecuteMainThreadTasks();
	return 0;
}
