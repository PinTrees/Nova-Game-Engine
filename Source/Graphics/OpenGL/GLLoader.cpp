#include "pch.h"
#include "GLLoader.h"

#define NOVA_GL_DEFINE(ret, name, params) PFN_##name name = nullptr;
NOVA_GL_FUNCTIONS(NOVA_GL_DEFINE)
#undef NOVA_GL_DEFINE

namespace GLLoader
{
	static void* Proc(const char* name)
	{
		void* p = (void*)::wglGetProcAddress(name);
		// 드라이버에 따라 실패를 1, 2, 3, -1 로 돌려준다
		if (p == nullptr || p == (void*)0x1 || p == (void*)0x2 || p == (void*)0x3 || p == (void*)-1)
		{
			static HMODULE lib = ::LoadLibraryA("opengl32.dll");
			p = lib ? (void*)::GetProcAddress(lib, name) : nullptr;
		}
		return p;
	}

	bool Load(std::string& missing)
	{
		missing.clear();
#define NOVA_GL_LOAD(ret, name, params) \
		name = reinterpret_cast<PFN_##name>(Proc(#name)); \
		if (!name) missing += std::string(missing.empty() ? "" : ", ") + #name;
		NOVA_GL_FUNCTIONS(NOVA_GL_LOAD)
#undef NOVA_GL_LOAD
		return missing.empty();
	}
}
