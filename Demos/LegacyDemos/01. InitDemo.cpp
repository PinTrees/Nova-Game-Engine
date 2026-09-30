#include "pch.h"
#include "01. InitDemo.h"


InitDemo::InitDemo(HINSTANCE hInstance)
	: App(hInstance)
{
}

InitDemo::~InitDemo()
{
}

bool InitDemo::Init()
{
	if (!App::Init())
		return false;

	return true;
}

void InitDemo::OnResize()
{
	App::OnResize();
}

void InitDemo::UpdateScene(float dt)
{

}

void InitDemo::RenderApplication()
{
	_deviceContext->ClearRenderTargetView(_renderTargetView.Get(), reinterpret_cast<const float*>(&Colors::Blue));
	_deviceContext->ClearDepthStencilView(_depthStencilView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

	HR(_swapChain->Present(0, 0));
}
