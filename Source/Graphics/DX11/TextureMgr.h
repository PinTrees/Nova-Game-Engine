#pragma once

class TextureMgr
{
public:
	TextureMgr();
	~TextureMgr();

	void Init(ComPtr<GfxDevice> device);

	ComPtr<GfxShaderResourceView> CreateTexture(std::wstring filename);

private:
	ComPtr<GfxDevice> _device;
	std::map<std::wstring, ComPtr<GfxShaderResourceView>> _textureSRV;
};