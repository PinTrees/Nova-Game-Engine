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

void TextureMgr::Init(ComPtr<GfxDevice> device)
{
	_device = device;
}

ComPtr<GfxShaderResourceView> TextureMgr::CreateTexture(std::wstring filename)
{
	ComPtr<GfxShaderResourceView> srv;

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

