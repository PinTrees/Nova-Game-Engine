#pragma once

class TextureMgr
{
public:
	TextureMgr();
	~TextureMgr();

	void Init(ComPtr<ID3D11Device> device);

	ComPtr<ID3D11ShaderResourceView> CreateTexture(std::wstring filename);

private:
	ComPtr<ID3D11Device> _device;
	std::map<std::wstring, ComPtr<ID3D11ShaderResourceView>> _textureSRV;
};