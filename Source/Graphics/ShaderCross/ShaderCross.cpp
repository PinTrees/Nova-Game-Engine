// SPIRV-Cross 헤더는 windows.h 의 min/max 매크로보다 먼저 (이 파일은 PCH 를 쓰지 않는다: CMakeLists)
#include "spirv_glsl.hpp"
#include "pch.h"
#include "ShaderCross.h"
#include <dxcapi.h>
#include <fstream>
#include <regex>
#include <set>
#include <nlohmann/json.hpp>
#include "ShaderCrossJson.h"

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
	// es = OpenGL ES 변환: NOVA_GLES 를 정의한다 (셰이더가 ES 에 없는 기능을 피해 가는 #ifdef)
	bool Preprocess(const std::wstring& file, std::string& out, std::string& error, bool es = false)
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
		std::error_code ec;
		const std::wstring engineArg = L"-I" + std::filesystem::absolute(L"../Shaders", ec).wstring();   // 패키지 셰이더 → 엔진 셰이더
		LPCWSTR args[] = { incArg.c_str(), engineArg.c_str(), L"-HV", L"2018" };
		ComPtr<IDxcOperationResult> result;
		DxcDefine define = { L"NOVA_GLES", L"1" };
		if (FAILED(s_Legacy->Preprocess(source.Get(), file.c_str(), args, _countof(args), es ? &define : nullptr, es ? 1 : 0, include.Get(), &result)))
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

	bool CompileSpirv(const std::string& source, const std::wstring& name, const FxParser::ShaderRef& ref, bool invertY, std::vector<uint32_t>& spirv, std::string& error, bool vulkan = false)
	{
		DxcBuffer buf = { source.data(), source.size(), DXC_CP_UTF8 };
		const std::wstring entry = string_to_wstring(ref.Entry);
		const std::wstring profile = ProfileFor(ref);
		std::vector<LPCWSTR> args = { name.c_str(), L"-E", entry.c_str(), L"-T", profile.c_str(), L"-spirv", L"-fspv-reflect", L"-fvk-use-dx-layout",
			L"-HV", L"2018", L"-O3", L"-Wno-ignored-attributes", L"-Wno-conversion", L"-Wno-parentheses-equality", L"-Wno-unused-value" };
		if (invertY)
			args.push_back(L"-fvk-invert-y");
		// 픽셀 셰이더의 SV_Position.w: D3D = 클립 w (시야 깊이), GL/Vulkan gl_FragCoord.w = 1/w → D3D 와 같게 뒤집는다
		//  (없으면 물이 waterZ = PosH.w 로 깊이를 잘못 계산해 굴절·흡수가 틀렸다)
		if (ref.StageType == Stage::Pixel)
			args.push_back(L"-fvk-use-dx-position-w");
		// Vulkan: gl_InstanceIndex 는 시작 인스턴스를 더한다 → D3D SV_InstanceID 처럼 뺀다 (GL 은 SPIRV_Cross_BaseInstance = 0 으로)
		if (vulkan && ref.StageType == Stage::Vertex)
			args.push_back(L"-fvk-support-nonzero-base-instance");
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

	// SampleCmpLevelZero(그림자 PCF)는 SPIRV-Cross 가 textureGrad(그림자 샘플러, 좌표, 0, 0) 으로 옮긴다
	// (GLSL 4.5 에는 sampler2DArrayShadow 용 textureLod 가 없어서). NVIDIA 에서 기울기를 주는 표본은 훨씬 느려
	// GL 본 패스(PCF 루프)가 DX11 의 5~6 배였다. 그림자 맵은 밉이 하나라 texture() 와 결과가 같다 → 바꾼다
	void FastShadowSamples(std::string& src)
	{
		std::set<std::string> shadow;   // sampler*Shadow 로 선언된 이름
		{
			static const std::regex decl(R"(uniform\s+sampler\w*Shadow\s+(\w+))");
			for (std::sregex_iterator it(src.begin(), src.end(), decl), end; it != end; ++it)
				shadow.insert((*it)[1]);
		}
		if (shadow.empty())
			return;
		const std::string key = "textureGrad(";
		std::string out;
		out.reserve(src.size());
		size_t pos = 0;
		for (size_t at; (at = src.find(key, pos)) != std::string::npos;)
		{
			// 맨 위 수준의 인자 나누기 (괄호 깊이)
			const size_t open = at + key.size();
			std::vector<std::string> args;
			int depth = 0;
			size_t start = open, i = open;
			for (; i < src.size(); ++i)
			{
				const char c = src[i];
				if (c == '(' || c == '[') ++depth;
				else if ((c == ')' || c == ']') && depth > 0) --depth;
				else if (c == ')' && depth == 0) { args.push_back(src.substr(start, i - start)); break; }
				else if (c == ',' && depth == 0) { args.push_back(src.substr(start, i - start)); start = i + 1; }
			}
			auto trim = [](std::string s) { s.erase(0, s.find_first_not_of(' ')); s.erase(s.find_last_not_of(' ') + 1); return s; };
			auto zero = [&](const std::string& a) { const std::string t = trim(a); return t == "vec2(0.0)" || t == "vec3(0.0)"; };
			std::string sampler = args.empty() ? std::string() : trim(args[0]);
			sampler = sampler.substr(0, sampler.find('['));
			out.append(src, pos, at - pos);
			if (i < src.size() && args.size() == 4 && zero(args[2]) && zero(args[3]) && shadow.count(sampler))
				out += "texture(" + trim(args[0]) + ", " + trim(args[1]) + ")";
			else
				out.append(src, at, (i < src.size() ? i + 1 : src.size()) - at);
			pos = i < src.size() ? i + 1 : src.size();
		}
		out.append(src, pos, std::string::npos);
		src.swap(out);
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

	// stageIndex = pass 안에서 이 단계의 순서 (VS 0, 다음 단계 1 …). 단계 사이 값의 이름 = v<경계>_<의미>
	//  (경계 k = k 번째 단계의 출력 = k+1 번째 단계의 입력). 경계 번호가 없으면 지오메트리·테셀레이션 단계의 입력과 출력이
	//  같은 이름(v_POSITION0)이 되어 SPIRV-Cross 가 출력을 v_POSITION0_1 로 바꾸고 다음 단계와 맞물리지 않았다
	// es = OpenGL ES 3.20 (안드로이드): glClipControl 이 없어 깊이 0..1 → -1..1 을 셰이더가 바꾼다 (fixup_clipspace — 깊이 값은 같다)
	bool ToGlsl(const std::vector<uint32_t>& spirv, Stage stage, int stageIndex, EffectGlsl& fx, PassGlsl& pass, StageGlsl& out, std::string& error, bool es = false)
	{
		try
		{
			spirv_cross::CompilerGLSL glsl(spirv);
			spirv_cross::CompilerGLSL::Options opt;
			opt.version = es ? 320 : 450;
			opt.es = es;
			opt.vulkan_semantics = false;
			opt.enable_420pack_extension = !es;
			if (es)
			{
				opt.vertex.fixup_clipspace = true;
				opt.fragment.default_float_precision = spirv_cross::CompilerGLSL::Options::Highp;
				opt.fragment.default_int_precision = spirv_cross::CompilerGLSL::Options::Highp;
			}
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
					glsl.set_name(r.id, "v" + std::to_string(stageIndex - 1) + "_" + sem);
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
					glsl.set_name(r.id, "v" + std::to_string(stageIndex) + "_" + sem);
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
			FastShadowSamples(out.Glsl);
			// 테셀레이션 (Domain = GL 의 TES): 깊이 프리패스와 본 패스의 gl_Position 이 비트까지 같게. GLSL 은 invariant 가 없으면
			//  같은 식이라도 프로그램마다 다르게 계산해도 된다 (재질 테셀레이션의 EQUAL 깊이 검사가 얼룩졌다 — 정점 셰이더는 식이 짧아 같았다)
			if (stage == Stage::Domain)
			{
				size_t at = 0;
				while (at < out.Glsl.size() && out.Glsl[at] == '#')
				{
					const size_t eol = out.Glsl.find('\n', at);
					at = eol == std::string::npos ? out.Glsl.size() : eol + 1;
				}
				out.Glsl.insert(at, "invariant gl_Position;\n");
			}
			if (es)
			{
				// GLSL ES 는 int · uint 를 저절로 바꾸지 않는다: SPIRV-Cross 가 gl_InvocationID(int) 를 uint 상수와 비교 → uint 로 감싼다
				static const std::regex inv(R"(gl_InvocationID(\s*[=!<>]=?\s*\d+u))");
				out.Glsl = std::regex_replace(out.Glsl, inv, "uint(gl_InvocationID)$1");
			}
			return true;
		}
		catch (const std::exception& e)
		{
			error = std::string("SPIRV-Cross: ") + e.what();
			return false;
		}
	}
}

namespace
{
	// ---- 변환 결과 캐시 (전처리한 소스의 해시가 같으면 디스크의 결과를 쓴다: 효과 하나 변환이 1~3 초)
	//  ShaderCache/GLSL/<이름>_<해시>.json — 변환기·이름 규칙이 바뀌면 kCacheVersion 을 올린다
	constexpr int kCacheVersion = 7;   // 2: 픽셀 셰이더 -fvk-use-dx-position-w, 3: 단계 사이 값 이름에 경계 번호 (v0_, v1_ …), 4: 그림자 표본 texture() (FastShadowSamples), 5: OpenGL ES (NOVA_GLES · gl_InvocationID), 6: gl_InvocationID 를 고치기 전에 만든 ES 캐시 버리기, 7: TES 의 invariant gl_Position
	using json = nlohmann::json;
	using namespace ShaderCross::Json;

	uint64_t Fnv1a(const std::string& s)
	{
		uint64_t h = 1469598103934665603ull;
		for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
		return h;
	}

	std::filesystem::path CachePath(const std::wstring& fxPath, uint64_t hash, bool es = false)
	{
		char hex[32];
		snprintf(hex, sizeof(hex), "%016llx", (unsigned long long)hash);
		return std::filesystem::path(L"ShaderCache") / (es ? L"GLES" : L"GLSL") / (std::filesystem::path(fxPath).stem().wstring() + L"_" + string_to_wstring(hex) + L".json");
	}
}

namespace ShaderCross
{
	bool Available(std::string* error)
	{
		const bool ok = Load();
		if (!ok && error)
			*error = s_LoadError;
		return ok;
	}

	bool CompileEffect(const std::wstring& fxPath, EffectGlsl& out)
	{
		return CompileEffectAs(fxPath, out, false);
	}

	bool CompileEffectGles(const std::wstring& fxPath, EffectGlsl& out)
	{
		return CompileEffectAs(fxPath, out, true);
	}

	bool CompileEffectAs(const std::wstring& fxPath, EffectGlsl& out, bool es)
	{
		out = EffectGlsl();
		out.File = fxPath;
		if (!Load())
		{
			out.Error = s_LoadError;
			return false;
		}
		std::string pre;
		if (!Preprocess(fxPath, pre, out.Error, es))
			return false;
		// 캐시: 전처리 결과(#include 까지 펼친 소스)가 같으면 변환 결과도 같다
		const uint64_t hash = Fnv1a(pre) ^ (uint64_t)kCacheVersion;
		const std::filesystem::path cacheFile = CachePath(fxPath, hash, es);
		{
			std::ifstream in(cacheFile, std::ios::binary);
			if (in)
			{
				try
				{
					const json j = json::parse(in);
					EffectGlsl cached;
					cached.File = fxPath;
					if (FromJson(j, cached, kCacheVersion))
					{
						out = std::move(cached);
						EditorLog::Write("ShaderCross", "cache hit %s", Narrow(std::filesystem::path(fxPath).filename().wstring()).c_str());
						return true;
					}
				}
				catch (const std::exception&)
				{
					// 깨진 캐시 → 다시 변환
				}
			}
		}
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
				// 단계 순서 (Stage 값 = 파이프라인 순서): 경계 번호용
				std::vector<Stage> order;
				for (const auto& s : pass.Shaders) order.push_back(s.StageType);
				std::sort(order.begin(), order.end());
				for (const FxParser::ShaderRef& ref : pass.Shaders)
				{
					const int stageIndex = (int)(std::find(order.begin(), order.end(), ref.StageType) - order.begin());
					EditorLog::Write("ShaderCross", "%s %s/%s %s %s", Narrow(std::filesystem::path(fxPath).filename().wstring()).c_str(), tech.Name.c_str(), pass.Name.c_str(),
						FxParser::StageName(ref.StageType), ref.Entry.c_str());
					std::vector<uint32_t> spirv;
					std::string error;
					StageGlsl sg;
					sg.StageType = ref.StageType;
					sg.Entry = ref.Entry;
					if (!CompileSpirv(out.Fx.Source, name, ref, ref.StageType == lastGeom, spirv, error) || !ToGlsl(spirv, ref.StageType, stageIndex, out, pg, sg, error, es))
					{
						pg.Error = std::string(FxParser::StageName(ref.StageType)) + " " + ref.Entry + ": " + error;
						break;
					}
					pg.Stages.push_back(std::move(sg));
				}
				out.Passes.push_back(std::move(pg));
			}
		// 캐시에 저장 (같은 이름의 예전 캐시 파일은 지운다)
		try
		{
			std::error_code ec;
			std::filesystem::create_directories(cacheFile.parent_path(), ec);
			const std::wstring prefix = std::filesystem::path(fxPath).stem().wstring() + L"_";
			for (const auto& f : std::filesystem::directory_iterator(cacheFile.parent_path(), ec))
				if (f.path().filename().wstring().rfind(prefix, 0) == 0 && f.path() != cacheFile)
					std::filesystem::remove(f.path(), ec);
			std::ofstream(cacheFile, std::ios::binary | std::ios::trunc) << ToJson(out, kCacheVersion).dump();
		}
		catch (const std::exception&)
		{
		}
		return true;
	}
}

// ============================================================================ Vulkan: SPIR-V 그대로 (장식만 고침)
namespace
{
	using namespace ShaderCross::Json;

	// ShaderCache/SPIRV/<이름>_<해시>.json — 규칙이 바뀌면 올린다
	constexpr int kSpirvCacheVersion = 3;

	const char* kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

	std::string ToBase64(const std::vector<uint32_t>& words)
	{
		const uint8_t* p = reinterpret_cast<const uint8_t*>(words.data());
		const size_t n = words.size() * 4;
		std::string out;
		out.reserve((n + 2) / 3 * 4);
		for (size_t i = 0; i < n; i += 3)
		{
			const uint32_t v = (uint32_t)p[i] << 16 | (i + 1 < n ? (uint32_t)p[i + 1] << 8 : 0) | (i + 2 < n ? p[i + 2] : 0);
			out += kB64[(v >> 18) & 63];
			out += kB64[(v >> 12) & 63];
			out += i + 1 < n ? kB64[(v >> 6) & 63] : '=';
			out += i + 2 < n ? kB64[v & 63] : '=';
		}
		return out;
	}

	bool FromBase64(const std::string& s, std::vector<uint32_t>& words)
	{
		int map[256];
		for (int& m : map) m = -1;
		for (int i = 0; i < 64; ++i) map[(uint8_t)kB64[i]] = i;
		std::vector<uint8_t> bytes;
		bytes.reserve(s.size() / 4 * 3);
		uint32_t acc = 0;
		int bits = 0;
		for (char c : s)
		{
			if (c == '=') break;
			const int v = map[(uint8_t)c];
			if (v < 0) return false;
			acc = acc << 6 | (uint32_t)v;
			bits += 6;
			if (bits >= 8)
			{
				bits -= 8;
				bytes.push_back((uint8_t)(acc >> bits));
			}
		}
		if (bytes.size() % 4 != 0) return false;
		words.resize(bytes.size() / 4);
		memcpy(words.data(), bytes.data(), bytes.size());
		return true;
	}

	std::string SpvName(spirv_cross::Compiler& c, const spirv_cross::Resource& r)
	{
		std::string name = c.get_name(r.id);
		if (name.empty())
			name = c.get_name(r.base_type_id);
		if (name.rfind("type_", 0) == 0)
			name = name.substr(5);
		if (name == "_Globals" || name == "$Globals")
			name = "$Globals";
		return name;
	}

	// DXC 가 늘 적는 장식 (Binding · DescriptorSet · Location) 의 값 워드를 바꾼다
	bool PatchDecoration(const spirv_cross::Compiler& c, std::vector<uint32_t>& code, uint32_t id, spv::Decoration dec, uint32_t value)
	{
		uint32_t offset = 0;
		if (!c.get_binary_offset_for_decoration(id, dec, offset) || offset >= code.size())
			return false;
		code[offset] = value;
		return true;
	}

	// cbuffer 반사 (GL 쪽 ToGlsl 과 같은 값)
	UniformBlock ReflectBlock(spirv_cross::Compiler& c, const spirv_cross::Resource& r, const std::string& name)
	{
		UniformBlock b;
		b.Name = name;
		const spirv_cross::SPIRType& type = c.get_type(r.base_type_id);
		b.Size = (int)c.get_declared_struct_size(type);
		for (uint32_t m = 0; m < (uint32_t)type.member_types.size(); ++m)
		{
			UniformBlock::Member mem;
			mem.Name = c.get_member_name(r.base_type_id, m);
			mem.Offset = (int)c.get_member_decoration(r.base_type_id, m, spv::DecorationOffset);
			mem.Size = (int)c.get_declared_struct_member_size(type, m);
			const spirv_cross::SPIRType& mt = c.get_type(type.member_types[m]);
			if (!mt.array.empty())
			{
				mem.ArrayCount = (int)mt.array[0];
				mem.ArrayStride = (int)c.type_struct_member_array_stride(type, m);
			}
			mem.Struct = mt.basetype == spirv_cross::SPIRType::Struct;
			mem.Integer = mt.basetype == spirv_cross::SPIRType::Int || mt.basetype == spirv_cross::SPIRType::UInt || mt.basetype == spirv_cross::SPIRType::Boolean;
			mem.Rows = (int)mt.vecsize;
			mem.Columns = (int)mt.columns;
			mem.Transpose = mt.columns > 1 && c.has_member_decoration(r.base_type_id, m, spv::DecorationRowMajor);
			b.Members.push_back(mem);
		}
		return b;
	}

	// DXC -fspv-reflect 가 넣은 HLSL 정보 (의미 이름 · 사용자 타입 · 카운터 버퍼) 지우기. 반사(PatchStage)가 끝난 뒤에.
	//  남겨 두면 장치에 VK_GOOGLE_hlsl_functionality1 · VK_GOOGLE_user_type 이 있어야 한다 (안드로이드 등 없는 GPU 가 많다)
	void StripHlslDecorations(std::vector<uint32_t>& code)
	{
		if (code.size() < 5) return;
		std::vector<uint32_t> out(code.begin(), code.begin() + 5);
		out.reserve(code.size());
		size_t i = 5;
		while (i < code.size())
		{
			const uint32_t count = code[i] >> 16, op = code[i] & 0xFFFF;
			if (count == 0 || i + count > code.size()) { out.insert(out.end(), code.begin() + i, code.end()); break; }
			bool drop = false;
			if (op == 10 /* OpExtension */)
			{
				const char* name = reinterpret_cast<const char*>(&code[i + 1]);
				drop = strcmp(name, "SPV_GOOGLE_hlsl_functionality1") == 0 || strcmp(name, "SPV_GOOGLE_user_type") == 0;
			}
			else if (op == 5632 /* OpDecorateString */ && count >= 3)
				drop = code[i + 2] == 5635 /* UserSemantic */ || code[i + 2] == 5636 /* UserTypeGOOGLE */;
			else if (op == 5633 /* OpMemberDecorateString */ && count >= 4)
				drop = code[i + 3] == 5635 || code[i + 3] == 5636;
			else if (op == 332 /* OpDecorateId */ && count >= 3)
				drop = code[i + 2] == 5634 /* HlslCounterBufferGOOGLE */;
			if (!drop) out.insert(out.end(), code.begin() + i, code.begin() + i + count);
			i += count;
		}
		code.swap(out);
	}

	// 단계 하나의 SPIR-V 장식 고치기: 자원 = set 0 · 효과 안 고정 바인딩, 단계 사이 입력 location = 앞 단계 출력
	//  prevOutputs = 앞 단계의 (의미 → location), outputs = 이 단계의 것 (다음 단계가 쓴다)
	bool PatchStage(std::vector<uint32_t>& code, Stage stage, const std::map<std::string, int>& prevOutputs, std::map<std::string, int>& outputs,
		EffectSpirv& fx, PassSpirv& pass, std::string& error)
	{
		try
		{
			spirv_cross::Compiler c(code);
			const spirv_cross::ShaderResources res = c.get_shader_resources();
			for (const auto& r : res.stage_inputs)
			{
				if (!c.has_decoration(r.id, spv::DecorationLocation))
					continue;   // SV_ 내장 값
				const std::string sem = NormSemantic(c.get_decoration_string(r.id, spv::DecorationUserSemantic));
				if (stage == Stage::Vertex)
				{
					pass.VertexInputs.push_back({ sem, (int)c.get_decoration(r.id, spv::DecorationLocation) });
					continue;
				}
				auto it = prevOutputs.find(sem);
				if (it == prevOutputs.end())
				{
					error = "input " + sem + " is not written by the previous stage";
					return false;
				}
				PatchDecoration(c, code, r.id, spv::DecorationLocation, (uint32_t)it->second);
			}
			for (const auto& r : res.stage_outputs)
				if (c.has_decoration(r.id, spv::DecorationLocation))
				{
					const int loc = (int)c.get_decoration(r.id, spv::DecorationLocation);
					outputs[NormSemantic(c.get_decoration_string(r.id, spv::DecorationUserSemantic))] = loc;
					if (stage == Stage::Pixel && loc < 32)
					{
						// SV_Target 배열 (float4 o[2] : SV_Target0) 이면 원소마다
						const spirv_cross::SPIRType& t = c.get_type(r.type_id);
						const int n = t.array.empty() ? 1 : (int)t.array[0];
						for (int i = 0; i < n && loc + i < 32; ++i)
							pass.PixelOutputs |= 1u << (loc + i);
					}
				}

			auto bind = [&](const spirv_cross::Resource& r, int binding) {
				PatchDecoration(c, code, r.id, spv::DecorationDescriptorSet, 0);
				PatchDecoration(c, code, r.id, spv::DecorationBinding, (uint32_t)binding);
				if (std::find(pass.Bindings.begin(), pass.Bindings.end(), binding) == pass.Bindings.end())
					pass.Bindings.push_back(binding);
			};
			for (const auto& r : res.uniform_buffers)
			{
				const std::string name = SpvName(c, r);
				auto it = fx.Blocks.find(name);
				if (it == fx.Blocks.end())
				{
					UniformBlock b = ReflectBlock(c, r, name);
					b.Binding = fx.BindingCount++;
					it = fx.Blocks.emplace(name, b).first;
				}
				bind(r, it->second.Binding);
			}
			// 텍스처 · 샘플러 · 버퍼: 이름마다 바인딩 하나 (배열이면 원소 수)
			auto resource = [&](const spirv_cross::Resource& r, ResourceBinding::Kind kind) -> ResourceBinding& {
				const std::string name = SpvName(c, r);
				auto it = fx.Resources.find(name);
				if (it == fx.Resources.end())
				{
					ResourceBinding rb;
					rb.Name = name;
					rb.Type = kind;
					const spirv_cross::SPIRType& t = c.get_type(r.type_id);
					rb.Count = t.array.empty() ? 1 : (int)t.array[0];
					if (t.basetype == spirv_cross::SPIRType::Image || t.basetype == spirv_cross::SPIRType::SampledImage)
					{
						rb.Dim = (int)t.image.dim;
						rb.Arrayed = t.image.arrayed;
						const spirv_cross::SPIRType& sampled = c.get_type(t.image.type);
						rb.Integer = sampled.basetype == spirv_cross::SPIRType::Int || sampled.basetype == spirv_cross::SPIRType::UInt;
						if (t.image.dim == spv::DimBuffer && kind == ResourceBinding::Kind::SampledImage)
							rb.Type = ResourceBinding::Kind::TexelBuffer;
					}
					if (kind == ResourceBinding::Kind::Sampler)
					{
						auto st = fx.Fx.States.find(name);
						rb.Comparison = st != fx.Fx.States.end() && st->second.Type == "SamplerComparisonState";
					}
					rb.Binding = fx.BindingCount++;
					it = fx.Resources.emplace(name, rb).first;
				}
				bind(r, it->second.Binding);
				return it->second;
			};
			for (const auto& r : res.separate_images) resource(r, ResourceBinding::Kind::SampledImage);
			for (const auto& r : res.separate_samplers) resource(r, ResourceBinding::Kind::Sampler);
			for (const auto& r : res.storage_images) resource(r, ResourceBinding::Kind::StorageImage);
			for (const auto& r : res.storage_buffers) resource(r, ResourceBinding::Kind::StorageBuffer);
			// 비교 샘플러와 같이 쓰는 텍스처 = 깊이 텍스처 (빈 칸에 깊이 더미를 묶는다): 결합 쌍을 따로 분석
			{
				spirv_cross::CompilerGLSL pairs(code);
				pairs.build_dummy_sampler_for_combined_images();   // Load 만 쓰는 텍스처
				pairs.build_combined_image_samplers();
				for (const auto& p : pairs.get_combined_image_samplers())
				{
					auto img = fx.Resources.find(pairs.get_name(p.image_id));
					auto smp = fx.Resources.find(pairs.get_name(p.sampler_id));
					if (img != fx.Resources.end() && smp != fx.Resources.end() && smp->second.Comparison)
						img->second.Depth = true;
				}
			}
			StripHlslDecorations(code);
			return true;
		}
		catch (const std::exception& e)
		{
			error = std::string("SPIRV-Cross: ") + e.what();
			return false;
		}
	}

	json SpirvToJson(const EffectSpirv& e)
	{
		json passes = json::array();
		for (const auto& p : e.Passes)
		{
			json stages = json::array();
			for (const auto& s : p.Stages)
				stages.push_back({ (int)s.StageType, s.Entry, ToBase64(s.Code) });
			json inputs = json::array();
			for (const auto& [sem, loc] : p.VertexInputs)
				inputs.push_back({ sem, loc });
			passes.push_back({ { "technique", p.Technique }, { "pass", p.Pass }, { "stages", stages }, { "inputs", inputs }, { "psOut", p.PixelOutputs }, { "bindings", p.Bindings }, { "error", p.Error } });
		}
		json resources = json::object();
		for (const auto& [n, r] : e.Resources)
			resources[n] = { (int)r.Type, r.Binding, r.Count, r.Dim, r.Arrayed, r.Depth, r.Integer, r.Comparison };
		return { { "version", kSpirvCacheVersion }, { "fx", FxToJson(e.Fx) }, { "passes", passes }, { "blocks", BlocksToJson(e.Blocks) },
			{ "resources", resources }, { "bindingCount", e.BindingCount } };
	}

	bool SpirvFromJson(const json& j, EffectSpirv& e)
	{
		if (j.value("version", 0) != kSpirvCacheVersion) return false;
		FxFromJson(j.at("fx"), e.Fx);
		for (const auto& p : j.at("passes"))
		{
			PassSpirv ps;
			ps.Technique = p.at("technique").get<std::string>();
			ps.Pass = p.at("pass").get<std::string>();
			for (const auto& s : p.at("stages"))
			{
				StageSpirv st;
				st.StageType = (Stage)s[0].get<int>();
				st.Entry = s[1].get<std::string>();
				if (!FromBase64(s[2].get<std::string>(), st.Code)) return false;
				ps.Stages.push_back(std::move(st));
			}
			for (const auto& i : p.at("inputs"))
				ps.VertexInputs.push_back({ i[0].get<std::string>(), i[1].get<int>() });
			ps.PixelOutputs = p.value("psOut", 0u);
			ps.Bindings = p.value("bindings", std::vector<int>());
			ps.Error = p.at("error").get<std::string>();
			e.Passes.push_back(std::move(ps));
		}
		BlocksFromJson(j.at("blocks"), e.Blocks);
		for (const auto& [n, r] : j.at("resources").items())
		{
			ResourceBinding rb;
			rb.Name = n;
			rb.Type = (ResourceBinding::Kind)r[0].get<int>();
			rb.Binding = r[1].get<int>();
			rb.Count = r[2].get<int>();
			rb.Dim = r[3].get<int>();
			rb.Arrayed = r[4].get<bool>();
			rb.Depth = r[5].get<bool>();
			rb.Integer = r[6].get<bool>();
			rb.Comparison = r[7].get<bool>();
			e.Resources[n] = rb;
		}
		e.BindingCount = j.at("bindingCount").get<int>();
		return true;
	}
}

namespace ShaderCross
{
	int EffectSpirv::PassesOk() const
	{
		int n = 0;
		for (const PassSpirv& p : Passes)
			n += p.Error.empty() ? 1 : 0;
		return n;
	}

	bool CompileEffectSpirv(const std::wstring& fxPath, EffectSpirv& out)
	{
		out = EffectSpirv();
		out.File = fxPath;
		if (!Load())
		{
			out.Error = s_LoadError;
			return false;
		}
		std::string pre;
		if (!Preprocess(fxPath, pre, out.Error))
			return false;
		const uint64_t hash = Fnv1a(pre) ^ ((uint64_t)kSpirvCacheVersion << 32);
		char hex[32];
		snprintf(hex, sizeof(hex), "%016llx", (unsigned long long)hash);
		const std::wstring stem = std::filesystem::path(fxPath).stem().wstring();
		const std::filesystem::path cacheFile = std::filesystem::path(L"ShaderCache") / L"SPIRV" / (stem + L"_" + string_to_wstring(hex) + L".json");
		{
			std::ifstream in(cacheFile, std::ios::binary);
			if (in)
			{
				try
				{
					const json j = json::parse(in);
					EffectSpirv cached;
					cached.File = fxPath;
					if (SpirvFromJson(j, cached))
					{
						out = std::move(cached);
						EditorLog::Write("ShaderCross", "SPIR-V cache hit %s", Narrow(std::filesystem::path(fxPath).filename().wstring()).c_str());
						return true;
					}
				}
				catch (const std::exception&)
				{
				}
			}
		}
		if (!FxParser::Parse(pre, out.Fx, out.Error))
			return false;
		const std::wstring name = std::filesystem::path(fxPath).filename().wstring() + L".hlsl";
		for (const FxParser::Technique& tech : out.Fx.Techniques)
			for (const FxParser::Pass& pass : tech.Passes)
			{
				PassSpirv ps;
				ps.Technique = tech.Name;
				ps.Pass = pass.Name;
				Stage lastGeom = Stage::Vertex;
				for (const auto& s : pass.Shaders)
					if (s.StageType == Stage::Geometry || (s.StageType == Stage::Domain && lastGeom != Stage::Geometry))
						lastGeom = s.StageType;
				// 파이프라인 순서로 (단계 사이 location 을 앞 단계에서 받는다)
				std::vector<const FxParser::ShaderRef*> refs;
				for (const auto& s : pass.Shaders) refs.push_back(&s);
				std::sort(refs.begin(), refs.end(), [](const FxParser::ShaderRef* a, const FxParser::ShaderRef* b) { return a->StageType < b->StageType; });
				std::map<std::string, int> prev;
				for (const FxParser::ShaderRef* ref : refs)
				{
					StageSpirv st;
					st.StageType = ref->StageType;
					st.Entry = ref->Entry;
					std::string error;
					std::map<std::string, int> outputs;
					if (!CompileSpirv(out.Fx.Source, name, *ref, ref->StageType == lastGeom, st.Code, error, true) ||
						!PatchStage(st.Code, ref->StageType, prev, outputs, out, ps, error))
					{
						ps.Error = std::string(FxParser::StageName(ref->StageType)) + " " + ref->Entry + ": " + error;
						break;
					}
					prev = std::move(outputs);
					ps.Stages.push_back(std::move(st));
				}
				out.Passes.push_back(std::move(ps));
			}
		try
		{
			std::error_code ec;
			std::filesystem::create_directories(cacheFile.parent_path(), ec);
			const std::wstring prefix = stem + L"_";
			for (const auto& f : std::filesystem::directory_iterator(cacheFile.parent_path(), ec))
				if (f.path().filename().wstring().rfind(prefix, 0) == 0 && f.path() != cacheFile)
					std::filesystem::remove(f.path(), ec);
			std::ofstream(cacheFile, std::ios::binary | std::ios::trunc) << SpirvToJson(out).dump();
		}
		catch (const std::exception&)
		{
		}
		return true;
	}
}
