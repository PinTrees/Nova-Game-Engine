#include "pch.h"
#include "TextureMgr.h"
#include "Utils.h"

TextureMgr::TextureMgr()
{
}

TextureMgr::~TextureMgr()
{
	_textureSRV.clear();
}

void TextureMgr::Init(ComPtr<ID3D11Device> device)
{
	_device = device;
}

ComPtr<ID3D11ShaderResourceView> TextureMgr::CreateTexture(std::wstring filename)
{
	ComPtr<ID3D11ShaderResourceView> srv;

	wstring path = PathManager::GetI()->GetCutSolutionPath(filename);

	// Does it already exist?
	if (_textureSRV.find(path) != _textureSRV.end())
	{
		srv = _textureSRV[path];
	}
	else
	{
		srv = Utils::LoadTexture(_device, filename.c_str());

		_textureSRV[path] = srv;
	}

	return srv;
}

