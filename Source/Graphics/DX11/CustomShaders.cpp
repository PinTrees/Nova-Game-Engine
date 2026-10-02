#include "pch.h"
#include "CustomShaders.h"
#include "Effects.h"
#include "RenderLayers.h"

namespace CustomShaders
{
	namespace
	{
		std::vector<Shader>& Shaders()
		{
			static std::vector<Shader> s;
			return s;
		}
		struct OwnedEffect
		{
			std::string Owner;
			std::shared_ptr<InstancedBasicEffect> Fx;
		};
		std::vector<OwnedEffect>& Owned()
		{
			static std::vector<OwnedEffect> e;
			return e;
		}
		std::vector<Provider>& Providers()
		{
			static std::vector<Provider> p;
			return p;
		}
		std::set<std::string>& Failed()   // 만들기에 실패한 이름 (매 프레임 다시 시도하지 않게 — 다시 등록하면 지운다)
		{
			static std::set<std::string> f;
			return f;
		}
	}

	void RegisterProvider(const Provider& provider)
	{
		auto& p = Providers();
		p.erase(std::remove_if(p.begin(), p.end(), [&](const Provider& x) { return x.Prefix == provider.Prefix; }), p.end());
		p.push_back(provider);
	}

	void Register(const Shader& shader)
	{
		auto& s = Shaders();
		s.erase(std::remove_if(s.begin(), s.end(), [&](const Shader& x) { return x.Name == shader.Name; }), s.end());
		s.push_back(shader);
		Failed().erase(shader.Name);
		EditorLog::Write("Shader", "custom shader '%s' registered by %s", shader.Name.c_str(), shader.Owner.c_str());
	}

	void UnregisterOwner(const std::string& owner)
	{
		auto& s = Shaders();
		for (const Shader& x : s)
			if (x.Owner == owner)
				Failed().erase(x.Name);
		s.erase(std::remove_if(s.begin(), s.end(), [&](const Shader& x) { return x.Owner == owner; }), s.end());
		auto& e = Owned();
		for (const OwnedEffect& x : e)
			if (x.Owner == owner && x.Fx)
				RenderLayers::ForgetEffect(x.Fx.get());
		e.erase(std::remove_if(e.begin(), e.end(), [&](const OwnedEffect& x) { return x.Owner == owner; }), e.end());
	}

	const Shader* Find(const std::string& name)
	{
		for (const Shader& s : Shaders())
			if (s.Name == name)
				return &s;
		// 에셋에서 생기는 셰이더 (Shader Graph): 처음 찾을 때 만든다
		if (name.empty() || Failed().count(name))
			return nullptr;
		for (const Provider& p : Providers())
			if (name.rfind(p.Prefix, 0) == 0 && p.Create)
			{
				if (p.Create(name))
					for (const Shader& s : Shaders())
						if (s.Name == name)
							return &s;
				if (p.Pending && p.Pending(name))
					return nullptr;   // 아직 만드는 중 (그동안 Fallback)
				Failed().insert(name);
				break;
			}
		return nullptr;
	}

	ShadowCaster& CurrentShadow()
	{
		static ShadowCaster s;
		return s;
	}

	void Forget(const std::string& name)
	{
		Failed().erase(name);
	}

	std::vector<std::string> Names()
	{
		std::vector<std::string> out;
		for (const Shader& s : Shaders()) out.push_back(s.Name);
		for (const Provider& p : Providers())
			if (p.List)
				for (const std::string& n : p.List())
					if (std::find(out.begin(), out.end(), n) == out.end())
						out.push_back(n);
		return out;
	}

	InstancedBasicEffect* LoadEffect(const std::string& owner, const std::wstring& fxPath, std::string& error)
	{
		auto device = Application::GetI()->GetDevice();
		if (!device)
		{
			error = "no graphics device";
			return nullptr;
		}
		auto fx = std::make_shared<InstancedBasicEffect>(device, fxPath);
		if (fx->GetFX() == nullptr || !fx->GetFX()->IsValid())
		{
			error = "cannot load " + wstring_to_string(fxPath) + " (see Logs/Editor.log for the shader compiler message)";
			return nullptr;
		}
		Owned().push_back({ owner, fx });
		return fx.get();
	}

	void ForEachEffect(const std::function<void(InstancedBasicEffect*)>& fn)
	{
		for (const OwnedEffect& e : Owned())
			if (e.Fx) fn(e.Fx.get());
	}
}
