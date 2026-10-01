// SPIRV-Cross 헤더는 windows.h 의 min/max 매크로보다 먼저 (이 파일은 PCH 를 쓰지 않는다: CMakeLists)
#include "spirv_glsl.hpp"
#include "pch.h"
#include "ShaderCross.h"
#include <dxcapi.h>

namespace
{
	using namespace ShaderCross;

	HMODULE s_Dll = nullptr;
	bool s_Tried = false;
	std::string s_LoadError;
	ComPtr<IDxcUtils> s_Utils;
	ComPtr<IDxcCompiler3> s_Compiler;
	ComPtr<IDxcCompiler> s_Legacy;   // Preprocess

	bool Load()
	{
		if (s_Tried)
			return s_Compiler != nullptr;
		s_Tried = true;
		s_Dll = ::LoadLibraryW(L"dxcompiler.dll");
		if (!s_Dll)
		{
			s_LoadError = "dxcompiler.dll not found next to the engine";
			return false;
		}
		auto create = reinterpret_cast<DxcCreateInstanceProc>(::GetProcAddress(s_Dll, "DxcCreateInstance"));
		if (!create || FAILED(create(CLSID_DxcUtils, IID_PPV_ARGS(&s_Utils))) || FAILED(create(CLSID_DxcCompiler, IID_PPV_ARGS(&s_Compiler))) ||
			FAILED(create(CLSID_DxcCompiler, IID_PPV_ARGS(&s_Legacy))))
		{
			s_LoadError = "DxcCreateInstance failed";
			s_Compiler.Reset();
			return false;
		}
		return true;
	}

	std::string Narrow(const std::wstring& w) { return wstring_to_string(w); }

	// 전처리 (#include, 매크로): IDxcCompiler::Preprocess
	bool Preprocess(const std::wstring& file, std::string& out, std::string& error)
	{
		ComPtr<IDxcBlobEncoding> source;
		if (FAILED(s_Utils->LoadFile(file.c_str(), nullptr, &source)))
		{
			error = "cannot read " + Narrow(file);
			return false;
		}
		ComPtr<IDxcIncludeHandler> include;
		s_Utils->CreateDefaultIncludeHandler(&include);
		const std::wstring dir = std::filesystem::path(file).parent_path().wstring();
		const std::wstring incArg = L"-I" + dir;
		LPCWSTR args[] = { incArg.c_str(), L"-HV", L"2018" };
		ComPtr<IDxcOperationResult> result;
		if (FAILED(s_Legacy->Preprocess(source.Get(), file.c_str(), args, _countof(args), nullptr, 0, include.Get(), &result)))
		{
			error = "preprocess call failed";
			return false;
		}
		HRESULT status = E_FAIL;
		result->GetStatus(&status);
		if (FAILED(status))
		{
			ComPtr<IDxcBlobEncoding> errs;
			result->GetErrorBuffer(&errs);
			error = errs ? std::string((const char*)errs->GetBufferPointer(), errs->GetBufferSize()) : "preprocess failed";
			return false;
		}
		ComPtr<IDxcBlob> text;
		result->GetResult(&text);
		out.assign((const char*)text->GetBufferPointer(), text->GetBufferSize());
		while (!out.empty() && out.back() == '\0')
			out.pop_back();
		return true;
	}

	std::wstring ProfileFor(const FxParser::ShaderRef& ref)
	{
		static const wchar_t* prefix[] = { L"vs", L"hs", L"ds", L"gs", L"ps", L"cs" };
		return std::wstring(prefix[(int)ref.StageType]) + L"_6_0";
	}

	// DXC 가 특정 입력에서 접근 위반으로 죽는 경우가 있다 → 에디터까지 죽지 않게 SEH 로 막는다 (C++ 객체 없는 함수여야 한다)
	HRESULT GuardedCompile(IDxcCompiler3* compiler, const DxcBuffer* buf, LPCWSTR* args, UINT32 count, IDxcResult** result, DWORD* exception)
	{
		__try
		{
			return compiler->Compile(buf, args, count, nullptr, __uuidof(IDxcResult), reinterpret_cast<void**>(result));
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			*exception = GetExceptionCode();
			return E_FAIL;
		}
	}

	bool CompileSpirv(const std::string& source, const std::wstring& name, const FxParser::ShaderRef& ref, bool invertY, std::vector<uint32_t>& spirv, std::string& error)
	{
		DxcBuffer buf = { source.data(), source.size(), DXC_CP_UTF8 };
		const std::wstring entry = string_to_wstring(ref.Entry);
		const std::wstring profile = ProfileFor(ref);
		std::vector<LPCWSTR> args = { name.c_str(), L"-E", entry.c_str(), L"-T", profile.c_str(), L"-spirv", L"-fspv-reflect", L"-fvk-use-dx-layout",
			L"-HV", L"2018", L"-O3", L"-Wno-ignored-attributes", L"-Wno-conversion", L"-Wno-parentheses-equality", L"-Wno-unused-value" };
		if (invertY)
			args.push_back(L"-fvk-invert-y");
		ComPtr<IDxcResult> result;
		DWORD exception = 0;
		if (FAILED(GuardedCompile(s_Compiler.Get(), &buf, args.data(), (UINT32)args.size(), result.GetAddressOf(), &exception)) || !result)
		{
			char buf2[64];
			snprintf(buf2, sizeof(buf2), "DXC crashed (exception 0x%08X)", (unsigned)exception);
			error = exception ? buf2 : "compile call failed";
			return false;
		}
		HRESULT status = E_FAIL;
		result->GetStatus(&status);
		ComPtr<IDxcBlobUtf8> errs;
		result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errs), nullptr);
		if (FAILED(status))
		{
			error = errs && errs->GetStringLength() ? errs->GetStringPointer() : "compile failed";
			return false;
		}
		ComPtr<IDxcBlob> obj;
		result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&obj), nullptr);
		if (!obj || obj->GetBufferSize() % 4 != 0)
		{
			error = "no SPIR-V";
			return false;
		}
		spirv.resize(obj->GetBufferSize() / 4);
		memcpy(spirv.data(), obj->GetBufferPointer(), obj->GetBufferSize());
		return true;
	}

	// 의미 이름 정리: 대문자, 숫자가 없으면 0 (TEXCOORD = TEXCOORD0)
	std::string NormSemantic(std::string s)
	{
		for (char& c : s) c = (char)toupper((unsigned char)c);
		if (s.empty() || !isdigit((unsigned char)s.back()))
			s += "0";
		return s;
	}

	std::string BlockName(spirv_cross::CompilerGLSL& glsl, const spirv_cross::Resource& r)
	{
		std::string name = glsl.get_name(r.id);
		if (name.empty())
			name = glsl.get_name(r.base_type_id);
		if (name.rfind("type_", 0) == 0)
			name = name.substr(5);
		if (name == "_Globals" || name == "$Globals")
			name = "$Globals";
		return name;
	}

	bool ToGlsl(const std::vector<uint32_t>& spirv, Stage stage, EffectGlsl& fx, PassGlsl& pass, StageGlsl& out, std::string& error)
	{
		try
		{
			spirv_cross::CompilerGLSL glsl(spirv);
			spirv_cross::CompilerGLSL::Options opt;
			opt.version = 450;
			opt.es = false;
			opt.vulkan_semantics = false;
			opt.enable_420pack_extension = true;
			glsl.set_common_options(opt);

			spirv_cross::ShaderResources res = glsl.get_shader_resources();
			// 단계 입출력: 의미 이름으로 (정점 입력은 location 유지, 픽셀 출력도 유지)
			for (const auto& r : res.stage_inputs)
			{
				const std::string sem = NormSemantic(glsl.get_decoration_string(r.id, spv::DecorationUserSemantic));
				if (stage == Stage::Vertex)
				{
					glsl.set_name(r.id, "in_" + sem);
					pass.VertexInputs.push_back({ sem, (int)glsl.get_decoration(r.id, spv::DecorationLocation) });
				}
				else
				{
					glsl.set_name(r.id, "v_" + sem);
					glsl.unset_decoration(r.id, spv::DecorationLocation);
				}
			}
			for (const auto& r : res.stage_outputs)
			{
				const std::string sem = NormSemantic(glsl.get_decoration_string(r.id, spv::DecorationUserSemantic));
				if (stage == Stage::Pixel)
					glsl.set_name(r.id, "out_" + sem);
				else
				{
					glsl.set_name(r.id, "v_" + sem);
					glsl.unset_decoration(r.id, spv::DecorationLocation);
				}
			}
			// uniform 블록: 효과 안에서 이름마다 같은 바인딩
			for (const auto& r : res.uniform_buffers)
			{
				const std::string name = BlockName(glsl, r);
				auto it = fx.Blocks.find(name);
				if (it == fx.Blocks.end())
				{
					UniformBlock b;
					b.Name = name;
					b.Binding = (int)fx.Blocks.size();
					const spirv_cross::SPIRType& type = glsl.get_type(r.base_type_id);
					b.Size = (int)glsl.get_declared_struct_size(type);
					for (uint32_t m = 0; m < (uint32_t)type.member_types.size(); ++m)
					{
						UniformBlock::Member mem;
						mem.Name = glsl.get_member_name(r.base_type_id, m);
						mem.Offset = (int)glsl.get_member_decoration(r.base_type_id, m, spv::DecorationOffset);
						mem.Size = (int)glsl.get_declared_struct_member_size(type, m);
						const spirv_cross::SPIRType& mt = glsl.get_type(type.member_types[m]);
						if (!mt.array.empty())
						{
							mem.ArrayCount = (int)mt.array[0];
							mem.ArrayStride = (int)glsl.type_struct_member_array_stride(type, m);
						}
						mem.Struct = mt.basetype == spirv_cross::SPIRType::Struct;
						mem.Integer = mt.basetype == spirv_cross::SPIRType::Int || mt.basetype == spirv_cross::SPIRType::UInt || mt.basetype == spirv_cross::SPIRType::Boolean;
						mem.Rows = (int)mt.vecsize;
						mem.Columns = (int)mt.columns;
						// DXC 는 HLSL column_major(기본)를 SPIR-V RowMajor 로 적는다 → 같은 바이트 = Effects11 이 전치해 넣은 값
						mem.Transpose = mt.columns > 1 && glsl.has_member_decoration(r.base_type_id, m, spv::DecorationRowMajor);
						b.Members.push_back(mem);
					}
					it = fx.Blocks.emplace(name, b).first;
				}
				glsl.unset_decoration(r.id, spv::DecorationDescriptorSet);
				glsl.set_decoration(r.id, spv::DecorationBinding, it->second.Binding);
				glsl.set_name(r.base_type_id, "cb_" + std::string(name == "$Globals" ? "Globals" : name));
			}
			// 텍스처 + 샘플러 → 결합 샘플러 (Load 만 쓰는 텍스처는 빈 샘플러와)
			const spirv_cross::VariableID dummySampler = glsl.build_dummy_sampler_for_combined_images();
			glsl.build_combined_image_samplers();
			for (const auto& c : glsl.get_combined_image_samplers())
			{
				const std::string tex = glsl.get_name(c.image_id);
				std::string smp = c.sampler_id == dummySampler ? std::string() : glsl.get_name(c.sampler_id);
				if (smp.empty()) smp = "nosampler";
				const std::string name = tex + "_" + smp;
				glsl.set_name(c.combined_id, name);
				auto it = fx.Samplers.find(name);
				if (it == fx.Samplers.end())
				{
					SamplerBinding s;
					s.Name = name;
					s.Texture = tex;
					s.Sampler = smp;
					const spirv_cross::SPIRType& ct = glsl.get_type_from_variable(c.combined_id);
					s.Count = ct.array.empty() ? 1 : (int)ct.array[0];
					s.Unit = 0;
					for (const auto& [n, other] : fx.Samplers)
						s.Unit = (std::max)(s.Unit, other.Unit + other.Count);
					it = fx.Samplers.emplace(name, s).first;
				}
				glsl.unset_decoration(c.combined_id, spv::DecorationDescriptorSet);
				glsl.set_decoration(c.combined_id, spv::DecorationBinding, it->second.Unit);
			}
			// RWTexture → image 유닛, (RW)StructuredBuffer → SSBO 바인딩: 효과 안에서 이름마다 고정
			auto mapByName = [&](const spirv_cross::SmallVector<spirv_cross::Resource>& list, std::map<std::string, int>& table, bool block)
			{
				for (const auto& r : list)
				{
					std::string name = block ? BlockName(glsl, r) : glsl.get_name(r.id);
					auto it = table.find(name);
					if (it == table.end())
						it = table.emplace(name, (int)table.size()).first;
					glsl.unset_decoration(r.id, spv::DecorationDescriptorSet);
					glsl.set_decoration(r.id, spv::DecorationBinding, it->second);
				}
			};
			mapByName(res.storage_images, fx.Images, false);
			mapByName(res.storage_buffers, fx.Buffers, true);
			out.Glsl = glsl.compile();
			return true;
		}
		catch (const std::exception& e)
		{
			error = std::string("SPIRV-Cross: ") + e.what();
			return false;
		}
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

	bool Available(std::string* error)
	{
		const bool ok = Load();
		if (!ok && error)
			*error = s_LoadError;
		return ok;
	}

	bool CompileEffect(const std::wstring& fxPath, EffectGlsl& out)
	{
		out = EffectGlsl();
		out.File = fxPath;
		if (!Load())
		{
			out.Error = s_LoadError;
			return false;
		}
		std::string pre;
		if (!Preprocess(fxPath, pre, out.Error))
			return false;
		if (!FxParser::Parse(pre, out.Fx, out.Error))
			return false;
		const std::wstring name = std::filesystem::path(fxPath).filename().wstring() + L".hlsl";
		for (const FxParser::Technique& tech : out.Fx.Techniques)
			for (const FxParser::Pass& pass : tech.Passes)
			{
				PassGlsl pg;
				pg.Technique = tech.Name;
				pg.Pass = pass.Name;
				// 래스터 전 마지막 단계에서 y 뒤집기 (도메인/지오메트리가 있으면 그 단계)
				Stage lastGeom = Stage::Vertex;
				for (const auto& s : pass.Shaders)
					if (s.StageType == Stage::Geometry || (s.StageType == Stage::Domain && lastGeom != Stage::Geometry))
						lastGeom = s.StageType;
				for (const FxParser::ShaderRef& ref : pass.Shaders)
				{
					EditorLog::Write("ShaderCross", "%s %s/%s %s %s", Narrow(std::filesystem::path(fxPath).filename().wstring()).c_str(), tech.Name.c_str(), pass.Name.c_str(),
						FxParser::StageName(ref.StageType), ref.Entry.c_str());
					std::vector<uint32_t> spirv;
					std::string error;
					StageGlsl sg;
					sg.StageType = ref.StageType;
					sg.Entry = ref.Entry;
					if (!CompileSpirv(out.Fx.Source, name, ref, ref.StageType == lastGeom, spirv, error) || !ToGlsl(spirv, ref.StageType, out, pg, sg, error))
					{
						pg.Error = std::string(FxParser::StageName(ref.StageType)) + " " + ref.Entry + ": " + error;
						break;
					}
					pg.Stages.push_back(std::move(sg));
				}
				out.Passes.push_back(std::move(pg));
			}
		return true;
	}
}
