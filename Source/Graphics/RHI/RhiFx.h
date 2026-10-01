#pragma once
#include "Rhi.h"
#include <atomic>
#include <deque>
#include <map>

// Effects11 과 같은 모양의 효과 래퍼 — 기존 렌더러 코드를 거의 그대로 두고 RHI 위로 옮기기 위한 층.
//  ID3DX11Effect → FxEffect, ID3DX11EffectXxxVariable → FxVar, ID3DX11EffectTechnique → FxTechnique, ID3DX11EffectPass → FxPass
//  - 같은 메서드 이름·인자 (GetVariableByName(..)->AsMatrix()->SetMatrix(..), GetPassByIndex(p)->Apply(0, dc) …)
//  - 실제 일은 Rhi::Effect: DirectX 11 = Effects11 그대로, OpenGL = ShaderCross(GLSL)
//  - ComPtr<FxTechnique> 등을 그대로 쓸 수 있게 AddRef/Release 가 있다 (변수·기법·pass 는 효과가 가지므로 세지 않음)
//  - SetResource(ID3D11ShaderResourceView*) 는 옮기는 동안만 (DirectX 11 에서만 뜻이 있음). RHI 텍스처는 SetTexture
class FxEffect;

class FxVar
{
public:
	FxVar(Rhi::Effect* fx, Rhi::VarId id) : _fx(fx), _id(id) {}
	ULONG AddRef() { return 1; }
	ULONG Release() { return 1; }
	bool IsValid() const { return _id >= 0; }

	FxVar* AsMatrix() { return this; }
	FxVar* AsVector() { return this; }
	FxVar* AsScalar() { return this; }
	FxVar* AsShaderResource() { return this; }
	FxVar* AsUnorderedAccessView() { return this; }

	HRESULT SetRawValue(const void* data, UINT offset, UINT bytes) { _fx->SetRaw(_id, data, bytes, offset); return S_OK; }
	HRESULT SetFloat(float v) { _fx->SetFloat(_id, v); return S_OK; }
	HRESULT SetInt(int v) { _fx->SetInt(_id, v); return S_OK; }
	HRESULT SetBool(bool v) { _fx->SetBool(_id, v); return S_OK; }
	HRESULT SetFloatVector(const float* v) { _fx->SetVector(_id, v); return S_OK; }
	HRESULT SetFloatArray(const float* v, UINT offset, UINT count) { _fx->SetFloatArray(_id, v, offset, count); return S_OK; }
	HRESULT SetFloatVectorArray(const float* v, UINT offset, UINT count) { _fx->SetVectorArray(_id, v, offset, count); return S_OK; }
	HRESULT SetMatrix(const float* m) { _fx->SetMatrix(_id, m); return S_OK; }
	HRESULT SetMatrixArray(const float* m, UINT offset, UINT count) { _fx->SetMatrixArray(_id, m, offset, count); return S_OK; }
	HRESULT SetTexture(Rhi::Texture* texture, UINT index = 0) { _fx->SetTexture(_id, texture, index); return S_OK; }
	HRESULT SetResource(ID3D11ShaderResourceView* srv) { _fx->SetNativeTexture(_id, srv, 0); return S_OK; }
	HRESULT SetResourceArray(ID3D11ShaderResourceView* const* srvs, UINT offset, UINT count)
	{
		for (UINT i = 0; i < count; ++i)
			_fx->SetNativeTexture(_id, srvs[i], offset + i);
		return S_OK;
	}
	HRESULT SetUnorderedAccessView(ID3D11UnorderedAccessView* uav) { _fx->SetNativeUav(_id, uav); return S_OK; }
	HRESULT GetFloatVector(float* out) { _fx->GetVector(_id, out); return S_OK; }

private:
	Rhi::Effect* _fx;
	Rhi::VarId _id;
};

class FxPass
{
public:
	FxPass(Rhi::Effect* fx, int technique, int pass) : _fx(fx), _technique(technique), _pass(pass) {}
	ULONG AddRef() { return 1; }
	ULONG Release() { return 1; }
	bool IsValid() const { return _technique >= 0 && _pass >= 0 && _pass < _fx->PassCount(_technique); }
	HRESULT Apply(UINT, ID3D11DeviceContext*) { if (IsValid()) _fx->Apply(_technique, _pass); return S_OK; }
	HRESULT GetDesc(D3DX11_PASS_DESC* desc);

private:
	Rhi::Effect* _fx;
	int _technique, _pass;
};

class FxTechnique
{
public:
	FxTechnique(Rhi::Effect* fx, int technique);
	ULONG AddRef() { return 1; }
	ULONG Release() { return 1; }
	bool IsValid() const { return _technique >= 0; }
	int Index() const { return _technique; }
	HRESULT GetDesc(D3DX11_TECHNIQUE_DESC* desc);
	FxPass* GetPassByIndex(UINT index);

private:
	Rhi::Effect* _fx;
	int _technique;
	std::string _name;
	std::deque<FxPass> _passes;
	FxPass _invalidPass;
};

class FxEffect
{
public:
	// Rhi::Main() 장치로 .fx 를 불러온다. 실패하면 nullptr + error
	static ComPtr<FxEffect> Load(const std::wstring& fxPath, std::string& error);

	ULONG AddRef() { return ++_refs; }
	ULONG Release()
	{
		const ULONG n = --_refs;
		if (n == 0) delete this;
		return n;
	}
	bool IsValid() const { return _rhi != nullptr; }

	FxVar* GetVariableByName(const char* name);
	FxTechnique* GetTechniqueByName(const char* name);
	FxTechnique* GetTechniqueByIndex(UINT index);
	HRESULT GetDesc(D3DX11_EFFECT_DESC* desc);
	Rhi::Effect* Rhi() const { return _rhi.get(); }

private:
	FxEffect() = default;
	std::atomic<ULONG> _refs{ 0 };
	std::unique_ptr<Rhi::Effect> _rhi;
	std::map<std::string, std::unique_ptr<FxVar>> _vars;
	std::map<int, std::unique_ptr<FxTechnique>> _techniques;
	std::unique_ptr<FxVar> _invalidVar;
	std::unique_ptr<FxTechnique> _invalidTechnique;
};
