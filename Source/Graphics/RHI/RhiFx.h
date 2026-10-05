#pragma once
#include "Rhi.h"
#include "NovaApi.h"
#include <atomic>
#include <deque>
#include <map>

// Effects11 과 같은 모양의 효과 래퍼 — 기존 렌더러 코드를 거의 그대로 두고 RHI 위로 옮기기 위한 층.
//  ID3DX11Effect → FxEffect, ID3DX11EffectXxxVariable → FxVar, ID3DX11EffectTechnique → FxTechnique, ID3DX11EffectPass → FxPass
//  - 같은 메서드 이름·인자 (GetVariableByName(..)->AsMatrix()->SetMatrix(..), GetPassByIndex(p)->Apply(0, dc) …)
//  - 실제 일은 Rhi::Effect: DirectX 11 = Effects11 그대로, OpenGL = ShaderCross(GLSL)
//  - ComPtr<FxTechnique> 등을 그대로 쓸 수 있게 AddRef/Release 가 있다 (변수·기법·pass 는 효과가 가지므로 세지 않음)
//  - 텍스처: SetResource(GfxShaderResourceView*) = Gfx 층 뷰, Rhi::Texture 는 SetTexture
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

	HRESULT SetRawValue(const void* data, UINT offset, UINT bytes) { if (_id >= 0) _fx->SetRaw(_id, data, bytes, offset); return S_OK; }
	HRESULT SetFloat(float v) { if (_id >= 0) _fx->SetFloat(_id, v); return S_OK; }
	HRESULT SetInt(int v) { if (_id >= 0) _fx->SetInt(_id, v); return S_OK; }
	HRESULT SetBool(bool v) { if (_id >= 0) _fx->SetBool(_id, v); return S_OK; }
	HRESULT SetFloatVector(const float* v) { if (_id >= 0) _fx->SetVector(_id, v); return S_OK; }
	HRESULT SetFloatArray(const float* v, UINT offset, UINT count) { if (_id >= 0) _fx->SetFloatArray(_id, v, offset, count); return S_OK; }
	HRESULT SetFloatVectorArray(const float* v, UINT offset, UINT count) { if (_id >= 0) _fx->SetVectorArray(_id, v, offset, count); return S_OK; }
	HRESULT SetMatrix(const float* m) { if (_id >= 0) _fx->SetMatrix(_id, m); return S_OK; }
	HRESULT SetMatrixArray(const float* m, UINT offset, UINT count) { if (_id >= 0) _fx->SetMatrixArray(_id, m, offset, count); return S_OK; }
	HRESULT SetTexture(Rhi::Texture* texture, UINT index = 0) { if (_id >= 0) _fx->SetTexture(_id, texture, index); return S_OK; }
	HRESULT SetResource(GfxShaderResourceView* srv) { if (_id >= 0) _fx->SetView(_id, srv, 0); return S_OK; }
	HRESULT SetResourceArray(GfxShaderResourceView* const* srvs, UINT offset, UINT count)
	{
		for (UINT i = 0; _id >= 0 && i < count; ++i)
			_fx->SetView(_id, srvs[i], offset + i);
		return S_OK;
	}
	HRESULT SetUnorderedAccessView(GfxUnorderedAccessView* uav) { if (_id >= 0) _fx->SetUav(_id, uav); return S_OK; }
	HRESULT GetFloatVector(float* out) { if (_id >= 0) _fx->GetVector(_id, out); return S_OK; }

private:
	Rhi::Effect* _fx;
	Rhi::VarId _id;
};

class NOVA_API FxPass
{
public:
	FxPass(Rhi::Effect* fx, int technique, int pass) : _fx(fx), _technique(technique), _pass(pass) {}
	ULONG AddRef() { return 1; }
	ULONG Release() { return 1; }
	bool IsValid() const { return _technique >= 0 && _pass >= 0 && _pass < _fx->PassCount(_technique); }
	HRESULT Apply(UINT, GfxContext*) { if (IsValid()) _fx->Apply(_technique, _pass); return S_OK; }
	// 이 기기에서 그릴 수 있는가 (IsValid = 이름이 있다, IsUsable = 셰이더가 만들어졌다 — Rhi::Effect::PassUsable)
	bool IsUsable() const { return IsValid() && _fx->PassUsable(_technique, _pass); }
	HRESULT GetDesc(D3DX11_PASS_DESC* desc);

private:
	Rhi::Effect* _fx;
	int _technique, _pass;
};

class NOVA_API FxTechnique
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

class NOVA_API FxEffect
{
public:
	// Rhi::Main() 장치로 .fx 를 불러온다. 실패하면 nullptr + error
	// device = nullptr 이면 엔진 장치(Rhi::Main), 아니면 그 장치 (검사용 GL 장치 등)
	static ComPtr<FxEffect> Load(const std::wstring& fxPath, std::string& error, Rhi::Device* device = nullptr);
	// 빈 효과: 불러오기에 실패했을 때 대신 (변수·기법이 모두 IsValid() == false, Apply 는 아무 일도 안 함)
	static ComPtr<FxEffect> Empty();

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
