#pragma  once
#include "Utils.h"
#include "GameTimer.h"
#include <string>

class EditorCamera;
class Camera;

class App
{
public:
	App(HINSTANCE hInstance);
	virtual ~App();
	
	HINSTANCE AppInst() { return _hAppInst; }
	HWND      MainWnd() { return _hMainWnd; }
	ID3D11Device* GetDevice() { return  _device.Get(); }
	ID3D11DeviceContext* GetDeviceContext() { return  _deviceContext.Get(); }
	//D3D11_VIEWPORT GetViewport() { return _viewport; }
	
	float     AspectRatio() { return static_cast<float>(_clientWidth) / _clientHeight; }
	
	virtual int32 Run();

	virtual bool Init();
	virtual void OnResize(); 
	virtual void UpdateScene(float dt) = 0;
	virtual void RenderApplication();

	virtual void OnSceneRender(ID3D11RenderTargetView* renderTargetView, Camera* camera) { } 
	virtual void _Editor_OnSceneRender(ID3D11RenderTargetView* renderTargetView, EditorCamera* camera) { }

	virtual LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

	virtual void OnMouseDown(WPARAM btnState, int32 x, int32 y){ }
	virtual void OnMouseUp(WPARAM btnState, int32 x, int32 y)  { }
	virtual void OnMouseMove(WPARAM btnState, int32 x, int32 y){ }

	void SetScreenSize(UINT width, UINT height);
	Vec2 GetScreenSize() { return Vec2(_clientWidth, _clientHeight); }
protected:
	// 창 생성 + 그래픽 백엔드/디바이스 초기화까지만 수행 (Hub처럼 가벼운 앱에서 사용)
	bool InitPlatform();
	bool InitMainWindow();
	bool InitDirect3D();
	void CalculateFrameStats();

private:
	void CreateDeviceAndSwapChain();
	void CreateRenderTargetView();
	void CreateDepthStencilView();

protected:
	HINSTANCE _hAppInst = 0;
	HWND      _hMainWnd = 0;
	bool      _appPaused = false;
	bool      _minimized = false;
	bool      _maximized = false;
	bool      _resizing = false;
	uint32    _4xMsaaQuality = 0;
	GameTimer _timer;

protected:
	// Device & SwapChain
	ComPtr<ID3D11Device> _device;
	ComPtr<ID3D11DeviceContext> _deviceContext;
	ComPtr<IDXGISwapChain> _swapChain;

	// DSV
	ComPtr<ID3D11Texture2D> _depthStencilBuffer;
	ComPtr<ID3D11DepthStencilView>  _depthStencilView;

	// RTV
	ComPtr<ID3D11RenderTargetView> _renderTargetView;
	
	// Viewport
	D3D11_VIEWPORT _viewport;

	// Etc
	std::wstring _mainWindowCaption = L"NOVA Game Engine";
	std::string _logFileName = "run_log.txt";
	bool _centerWindow = false;
	D3D_DRIVER_TYPE _driverType = D3D_DRIVER_TYPE_HARDWARE;
	int32 _clientWidth = 1920;
	int32 _clientHeight = 1080;
	bool _enable4xMsaa = false;
};