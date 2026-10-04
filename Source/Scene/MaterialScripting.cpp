#include "pch.h"
#include "MeshRenderer.h"
#include "ResourceManager.h"
#include "ScriptBindings.h"

// C# (ScriptCore 의 Material.cs — DllImport("NovaCore")): Unity 의 Material · Renderer.material · sharedMaterial · MaterialPropertyBlock.
//  재질 = 핸들 (UMaterial 주소). 스크립트에 건넨 재질은 여기서 잡아 둔다 (C# 이 들고 있는 동안 지워지지 않게 — 런타임 사본은 플레이가 끝날 때까지)
namespace
{
	std::unordered_map<uint64, shared_ptr<UMaterial>> s_Handles;

	uint64 HandleOf(const shared_ptr<UMaterial>& m)
	{
		if (!m) return 0;
		const uint64 h = (uint64)(uintptr_t)m.get();
		s_Handles[h] = m;
		return h;
	}

	UMaterial* FromHandle(uint64 h)
	{
		auto it = s_Handles.find(h);
		return it != s_Handles.end() ? it->second.get() : nullptr;
	}

	MeshRenderer* FindRenderer(uint64 id)
	{
		GameObject* go = ScriptBindings::FindObject(id);
		return go ? go->GetComponentIncludingPending<MeshRenderer>() : nullptr;   // 같은 프레임에 AddComponent 한 것도
	}

	// C# 에 돌려줄 문자열 (다음 호출까지 유효)
	const char* Keep(std::string s)
	{
		static thread_local std::string t;
		t = std::move(s);
		return t.c_str();
	}
}

// 재질 칸 수 (Unity 의 sharedMaterials.Length)
NOVA_PACKAGE_EXPORT int NovaMat_Count(uint64 id)
{
	MeshRenderer* r = FindRenderer(id);
	return r ? (int)r->GetMaterials().size() : 0;
}

// 재질 칸 index: instance 0 = sharedMaterial (공유 재질 그대로), 1 = material (처음 부르면 이 렌더러만의 사본으로 바꾼다 — Unity 와 같음)
NOVA_PACKAGE_EXPORT uint64 NovaMat_Get(uint64 id, int index, int instance)
{
	MeshRenderer* r = FindRenderer(id);
	if (r == nullptr || index < 0)
		return 0;
	const auto& mats = r->GetMaterials();
	shared_ptr<UMaterial> m = index < (int)mats.size() ? mats[index] : nullptr;
	if (!m && instance == 0)
		return HandleOf(UMaterial::GetDefault());
	if (instance && !(m && m->IsInstance()))
	{
		m = (m ? m : UMaterial::GetDefault())->CloneInstance();
		r->SetMaterialAt(index, m, L"");   // 씬에 저장하는 경로는 원본 그대로 (사본은 저장되지 않는다)
	}
	return HandleOf(m);
}

// sharedMaterial = 재질 (파일 재질이면 씬에 저장하는 경로도)
NOVA_PACKAGE_EXPORT int NovaMat_Set(uint64 id, int index, uint64 handle)
{
	MeshRenderer* r = FindRenderer(id);
	auto it = s_Handles.find(handle);
	if (r == nullptr || index < 0 || it == s_Handles.end())
		return 0;
	const shared_ptr<UMaterial>& m = it->second;
	const std::string& path = m->GetPath();
	r->SetMaterialAt(index, m, m->IsInstance() || path.empty() ? std::wstring() : string_to_wstring(path));
	return 1;
}

// new Material(source) — 사본 (source 0 = 기본 재질)
NOVA_PACKAGE_EXPORT uint64 NovaMat_Clone(uint64 source)
{
	UMaterial* src = FromHandle(source);
	return HandleOf(src ? src->CloneInstance() : UMaterial::GetDefault()->CloneInstance());
}

// 프로젝트의 .mat (Assets/... — Resources.Load 처럼). 없으면 0
NOVA_PACKAGE_EXPORT uint64 NovaMat_Load(const char* path)
{
	if (path == nullptr || !*path)
		return 0;
	return HandleOf(UMaterial::IsBuiltinPath(path) ? UMaterial::GetDefault() : ResourceManager::GetI()->LoadMaterial(path));
}

NOVA_PACKAGE_EXPORT const char* NovaMat_Name(uint64 handle)
{
	UMaterial* m = FromHandle(handle);
	return Keep(m ? m->ScriptName() : std::string());
}

NOVA_PACKAGE_EXPORT const char* NovaMat_Shader(uint64 handle)
{
	UMaterial* m = FromHandle(handle);
	return Keep(m ? m->ShaderName() : std::string());
}

NOVA_PACKAGE_EXPORT int NovaMat_SetColor(uint64 handle, const char* name, const float* rgba)
{
	UMaterial* m = FromHandle(handle);
	return m && name && rgba && m->SetColorProperty(name, XMFLOAT4(rgba[0], rgba[1], rgba[2], rgba[3])) ? 1 : 0;
}

NOVA_PACKAGE_EXPORT int NovaMat_GetColor(uint64 handle, const char* name, float* rgba)
{
	UMaterial* m = FromHandle(handle);
	XMFLOAT4 c;
	if (!m || !name || !rgba || !m->GetColorProperty(name, c))
		return 0;
	rgba[0] = c.x; rgba[1] = c.y; rgba[2] = c.z; rgba[3] = c.w;
	return 1;
}

NOVA_PACKAGE_EXPORT int NovaMat_SetFloat(uint64 handle, const char* name, float v)
{
	UMaterial* m = FromHandle(handle);
	return m && name && m->SetFloatProperty(name, v) ? 1 : 0;
}

NOVA_PACKAGE_EXPORT int NovaMat_GetFloat(uint64 handle, const char* name, float* v)
{
	UMaterial* m = FromHandle(handle);
	return m && name && v && m->GetFloatProperty(name, *v) ? 1 : 0;
}

NOVA_PACKAGE_EXPORT int NovaMat_HasProperty(uint64 handle, const char* name)
{
	UMaterial* m = FromHandle(handle);
	return m && name && m->HasProperty(name) ? 1 : 0;
}

// MaterialPropertyBlock → 렌더러 (SetPropertyBlock). names = '\n' 으로 이은 이름, values = 이름마다 float4, colors = 이름마다 1 (색) · 0 (수)
NOVA_PACKAGE_EXPORT int NovaMat_SetBlock(uint64 id, int count, const char* names, const float* values, const int* colors)
{
	MeshRenderer* r = FindRenderer(id);
	if (r == nullptr || count < 0)
		return 0;
	std::vector<std::pair<std::string, MeshRenderer::BlockValue>> block;
	const char* p = names ? names : "";
	for (int i = 0; i < count; ++i)
	{
		const char* e = strchr(p, '\n');
		std::string name = e ? std::string(p, e) : std::string(p);
		p = e ? e + 1 : p + strlen(p);
		MeshRenderer::BlockValue v;
		v.V = XMFLOAT4(values[i * 4], values[i * 4 + 1], values[i * 4 + 2], values[i * 4 + 3]);
		v.Color = colors[i] != 0;
		block.push_back({ name, v });
	}
	r->SetPropertyBlock(std::move(block));
	return 1;
}

// GetPropertyBlock: 값 수, index 번째 이름 · 값
NOVA_PACKAGE_EXPORT int NovaMat_BlockCount(uint64 id)
{
	MeshRenderer* r = FindRenderer(id);
	return r ? (int)r->GetPropertyBlock().size() : 0;
}

NOVA_PACKAGE_EXPORT const char* NovaMat_BlockEntry(uint64 id, int index, float* value, int* color)
{
	MeshRenderer* r = FindRenderer(id);
	if (r == nullptr || index < 0 || index >= (int)r->GetPropertyBlock().size())
		return Keep(std::string());
	const auto& [name, v] = r->GetPropertyBlock()[index];
	value[0] = v.V.x; value[1] = v.V.y; value[2] = v.V.z; value[3] = v.V.w;
	*color = v.Color ? 1 : 0;
	return Keep(name);
}
