#include "pch.h"
#include "ShaderCrossJson.h"

namespace ShaderCross::Json
{
	json FxToJson(const FxParser::Effect& e)
	{
		json fx;
		fx["source"] = e.Source;
		json techs = json::array();
		for (const auto& t : e.Techniques)
		{
			json passes = json::array();
			for (const auto& p : t.Passes)
			{
				json shaders = json::array();
				for (const auto& s : p.Shaders)
					shaders.push_back({ (int)s.StageType, s.Profile, s.Entry, s.Original });
				passes.push_back({ { "name", p.Name }, { "shaders", shaders }, { "ds", p.DepthStencilState }, { "rs", p.RasterizerState }, { "bs", p.BlendState },
					{ "stencilRef", p.StencilRef }, { "blendFactor", { p.BlendFactor[0], p.BlendFactor[1], p.BlendFactor[2], p.BlendFactor[3] } }, { "sampleMask", p.SampleMask } });
			}
			techs.push_back({ { "name", t.Name }, { "passes", passes } });
		}
		fx["techniques"] = techs;
		json states = json::object();
		for (const auto& [n, b] : e.States)
			states[n] = { { "type", b.Type }, { "fields", b.Fields } };
		fx["states"] = states;
		fx["defaults"] = e.Defaults;
		fx["warnings"] = e.Warnings;
		return fx;
	}

	void FxFromJson(const json& fx, FxParser::Effect& e)
	{
		e.Source = fx.at("source").get<std::string>();
		for (const auto& t : fx.at("techniques"))
		{
			FxParser::Technique tech;
			tech.Name = t.at("name").get<std::string>();
			for (const auto& p : t.at("passes"))
			{
				FxParser::Pass pass;
				pass.Name = p.at("name").get<std::string>();
				for (const auto& s : p.at("shaders"))
					pass.Shaders.push_back({ (Stage)s[0].get<int>(), s[1].get<std::string>(), s[2].get<std::string>(), s[3].get<std::string>() });
				pass.DepthStencilState = p.at("ds").get<std::string>();
				pass.RasterizerState = p.at("rs").get<std::string>();
				pass.BlendState = p.at("bs").get<std::string>();
				pass.StencilRef = p.at("stencilRef").get<int>();
				for (int i = 0; i < 4; ++i) pass.BlendFactor[i] = p.at("blendFactor")[i].get<float>();
				pass.SampleMask = p.at("sampleMask").get<unsigned>();
				tech.Passes.push_back(pass);
			}
			e.Techniques.push_back(tech);
		}
		for (const auto& [n, b] : fx.at("states").items())
		{
			FxParser::StateBlock block;
			block.Type = b.at("type").get<std::string>();
			block.Fields = b.at("fields").get<std::map<std::string, std::string>>();
			e.States[n] = block;
		}
		e.Defaults = fx.at("defaults").get<std::map<std::string, std::string>>();
		e.Warnings = fx.at("warnings").get<std::vector<std::string>>();
	}

	json BlocksToJson(const std::map<std::string, UniformBlock>& blocks)
	{
		json out = json::object();
		for (const auto& [n, b] : blocks)
		{
			json members = json::array();
			for (const auto& m : b.Members)
				members.push_back({ m.Name, m.Offset, m.Size, m.ArrayCount, m.ArrayStride, m.Rows, m.Columns, m.Transpose, m.Struct, m.Integer });
			out[n] = { { "binding", b.Binding }, { "size", b.Size }, { "members", members } };
		}
		return out;
	}

	void BlocksFromJson(const json& j, std::map<std::string, UniformBlock>& blocks)
	{
		for (const auto& [n, b] : j.items())
		{
			UniformBlock ub;
			ub.Name = n;
			ub.Binding = b.at("binding").get<int>();
			ub.Size = b.at("size").get<int>();
			for (const auto& m : b.at("members"))
			{
				UniformBlock::Member mem;
				mem.Name = m[0].get<std::string>();
				mem.Offset = m[1].get<int>();
				mem.Size = m[2].get<int>();
				mem.ArrayCount = m[3].get<int>();
				mem.ArrayStride = m[4].get<int>();
				mem.Rows = m[5].get<int>();
				mem.Columns = m[6].get<int>();
				mem.Transpose = m[7].get<bool>();
				mem.Struct = m[8].get<bool>();
				mem.Integer = m[9].get<bool>();
				ub.Members.push_back(mem);
			}
			blocks[n] = ub;
		}
	}

	json ToJson(const EffectGlsl& e, int version)
	{
		json fx = FxToJson(e.Fx);
		json passes = json::array();
		for (const auto& p : e.Passes)
		{
			json stages = json::array();
			for (const auto& s : p.Stages)
				stages.push_back({ (int)s.StageType, s.Entry, s.Glsl });
			json inputs = json::array();
			for (const auto& [sem, loc] : p.VertexInputs)
				inputs.push_back({ sem, loc });
			passes.push_back({ { "technique", p.Technique }, { "pass", p.Pass }, { "stages", stages }, { "inputs", inputs }, { "error", p.Error } });
		}
		json blocks = BlocksToJson(e.Blocks);
		json samplers = json::object();
		for (const auto& [n, s] : e.Samplers)
			samplers[n] = { s.Texture, s.Sampler, s.Unit, s.Count };
		return { { "version", version }, { "fx", fx }, { "passes", passes }, { "blocks", blocks }, { "samplers", samplers },
			{ "images", e.Images }, { "buffers", e.Buffers } };
	}

	bool FromJson(const json& j, EffectGlsl& e, int version)
	{
		if (j.value("version", 0) != version) return false;
		FxFromJson(j.at("fx"), e.Fx);
		for (const auto& p : j.at("passes"))
		{
			PassGlsl pg;
			pg.Technique = p.at("technique").get<std::string>();
			pg.Pass = p.at("pass").get<std::string>();
			for (const auto& s : p.at("stages"))
				pg.Stages.push_back({ (Stage)s[0].get<int>(), s[1].get<std::string>(), s[2].get<std::string>() });
			for (const auto& i : p.at("inputs"))
				pg.VertexInputs.push_back({ i[0].get<std::string>(), i[1].get<int>() });
			pg.Error = p.at("error").get<std::string>();
			e.Passes.push_back(pg);
		}
		BlocksFromJson(j.at("blocks"), e.Blocks);
		for (const auto& [n, s] : j.at("samplers").items())
		{
			SamplerBinding sb;
			sb.Name = n;
			sb.Texture = s[0].get<std::string>();
			sb.Sampler = s[1].get<std::string>();
			sb.Unit = s[2].get<int>();
			sb.Count = s[3].get<int>();
			e.Samplers[n] = sb;
		}
		e.Images = j.at("images").get<std::map<std::string, int>>();
		e.Buffers = j.at("buffers").get<std::map<std::string, int>>();
		return true;
	}
}

namespace ShaderCross
{
	int EffectGlsl::PassesOk() const
	{
		int n = 0;
		for (const PassGlsl& p : Passes)
			n += p.Error.empty() ? 1 : 0;
		return n;
	}
}
