#include "pch.h"
#include "CustomShaders.h"
#include "Effects.h"

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
	}

	void Register(const Shader& shader)
	{
		auto& s = Shaders();
		s.erase(std::remove_if(s.begin(), s.end(), [&](const Shader& x) { return x.Name == shader.Name; }), s.end());
		s.push_back(shader);
		EditorLog::Write("Shader", "custom shader '%s' registered by %s", shader.Name.c_str(), shader.Owner.c_str());
	}

	void UnregisterOwner(const std::string& owner)
	{
		auto& s = Shaders();
		s.erase(std::remove_if(s.begin(), s.end(), [&](const Shader& x) { return x.Owner == owner; }), s.end());
		auto& e = Owned();
		e.erase(std::remove_if(e.begin(), e.end(), [&](const OwnedEffect& x) { return x.Owner == owner; }), e.end());
	}

	const Shader* Find(const std::string& name)
	{
		for (const Shader& s : Shaders())
			if (s.Name == name)
				return &s;
		return nullptr;
	}

	std::vector<std::string> Names()
	{
		std::vector<std::string> out;
		for (const Shader& s : Shaders()) out.push_back(s.Name);
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
