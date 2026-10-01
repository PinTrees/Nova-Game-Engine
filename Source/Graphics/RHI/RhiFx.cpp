#include "pch.h"
#include "RhiFx.h"

HRESULT FxPass::GetDesc(D3DX11_PASS_DESC* desc)
{
	*desc = {};
	const void* data = nullptr;
	size_t size = 0;
	if (IsValid() && _fx->NativeInputSignature(_technique, _pass, &data, &size))
	{
		desc->pIAInputSignature = (BYTE*)data;
		desc->IAInputSignatureSize = size;
	}
	return IsValid() ? S_OK : E_FAIL;
}

FxTechnique::FxTechnique(Rhi::Effect* fx, int technique)
	: _fx(fx), _technique(technique), _invalidPass(fx, -1, -1)
{
	_name = fx && technique >= 0 ? fx->TechniqueName(technique) : std::string();
	if (fx && technique >= 0)
		for (int p = 0; p < fx->PassCount(technique); ++p)
			_passes.emplace_back(fx, technique, p);
}

HRESULT FxTechnique::GetDesc(D3DX11_TECHNIQUE_DESC* desc)
{
	*desc = {};
	desc->Name = _name.c_str();
	desc->Passes = (UINT)_passes.size();
	return IsValid() ? S_OK : E_FAIL;
}

FxPass* FxTechnique::GetPassByIndex(UINT index)
{
	return index < _passes.size() ? &_passes[index] : &_invalidPass;
}

ComPtr<FxEffect> FxEffect::Load(const std::wstring& fxPath, std::string& error)
{
	Rhi::Device* dev = Rhi::Main();
	if (!dev)
	{
		error = "no graphics device";
		return nullptr;
	}
	std::unique_ptr<Rhi::Effect> rhi = dev->LoadEffect(fxPath, error);
	if (!rhi)
		return nullptr;
	ComPtr<FxEffect> fx;
	fx.Attach(new FxEffect());
	fx->AddRef();   // Attach 는 세지 않으므로 1 로
	fx->_rhi = std::move(rhi);
	fx->_invalidVar = std::make_unique<FxVar>(fx->_rhi.get(), -1);
	fx->_invalidTechnique = std::make_unique<FxTechnique>(fx->_rhi.get(), -1);
	return fx;
}

FxVar* FxEffect::GetVariableByName(const char* name)
{
	auto it = _vars.find(name);
	if (it != _vars.end())
		return it->second.get();
	const Rhi::VarId id = _rhi->FindVariable(name);
	if (id < 0)
		return _invalidVar.get();
	return (_vars[name] = std::make_unique<FxVar>(_rhi.get(), id)).get();
}

FxTechnique* FxEffect::GetTechniqueByName(const char* name)
{
	const int t = _rhi->FindTechnique(name);
	return t < 0 ? _invalidTechnique.get() : GetTechniqueByIndex((UINT)t);
}

FxTechnique* FxEffect::GetTechniqueByIndex(UINT index)
{
	if ((int)index >= _rhi->TechniqueCount())
		return _invalidTechnique.get();
	auto& t = _techniques[(int)index];
	if (!t)
		t = std::make_unique<FxTechnique>(_rhi.get(), (int)index);
	return t.get();
}

HRESULT FxEffect::GetDesc(D3DX11_EFFECT_DESC* desc)
{
	*desc = {};
	desc->Techniques = (UINT)_rhi->TechniqueCount();
	return S_OK;
}
