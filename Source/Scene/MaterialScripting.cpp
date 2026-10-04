#include "pch.h"
#include "MeshRenderer.h"
#include "SkinnedMeshRenderer.h"
#include "SpriteRenderer.h"
#include "SceneCulling.h"
#include "LineRenderer.h"
#include "TagsAndLayers.h"
#include "ResourceManager.h"
#include "ScriptBindings.h"

// C# (ScriptCore 의 Material.cs · Renderer.cs — DllImport("NovaCore")): Unity 의 Material · Renderer (material · sharedMaterial ·
//  MaterialPropertyBlock · enabled · bounds · shadowCastingMode · sorting). 렌더러 함수의 kind = C# 클래스 (0 Mesh, 1 Skinned, 2 Sprite, 3 Line, 4 Trail).
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

	// 재질을 쓰는 렌더러 (Mesh · Skinned). Sprite Renderer 는 재질이 없다 (null — 문서 MATERIAL_SCRIPTING.md)
	struct RendererRef
	{
		MeshRenderer* Mesh = nullptr;
		SkinnedMeshRenderer* Skinned = nullptr;
		explicit operator bool() const { return Mesh || Skinned; }
		const vector<shared_ptr<UMaterial>>& GetMaterials() const { return Mesh ? Mesh->GetMaterials() : Skinned->GetMaterials(); }
		void SetMaterialAt(int index, shared_ptr<UMaterial> m, const wstring& path) const
		{
			if (Mesh) Mesh->SetMaterialAt(index, std::move(m), path);
			else Skinned->SetMaterialAt(index, std::move(m), path);
		}
		MaterialBlock& Block() const { return Mesh ? Mesh->PropertyBlock() : Skinned->PropertyBlock(); }
	};

	Component* FindComponent(uint64 id, int kind)
	{
		GameObject* go = ScriptBindings::FindObject(id);
		if (go == nullptr)
			return nullptr;
		switch (kind)   // 같은 프레임에 AddComponent 한 것도
		{
		case 0: return go->GetComponentIncludingPending<MeshRenderer>();
		case 1: return go->GetComponentIncludingPending<SkinnedMeshRenderer>();
		case 2: return go->GetComponentIncludingPending<SpriteRenderer>();
		case 3: return go->GetComponentIncludingPending<LineRenderer>();
		case 4: return go->GetComponentIncludingPending<TrailRenderer>();
		}
		return nullptr;
	}

	RendererRef FindRenderer(uint64 id, int kind)
	{
		RendererRef r;
		Component* c = FindComponent(id, kind);
		if (kind == 0) r.Mesh = static_cast<MeshRenderer*>(c);
		if (kind == 1) r.Skinned = static_cast<SkinnedMeshRenderer*>(c);
		return r;
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
NOVA_PACKAGE_EXPORT int NovaMat_Count(uint64 id, int kind)
{
	RendererRef r = FindRenderer(id, kind);
	return r ? (int)r.GetMaterials().size() : 0;
}

// 재질 칸 index: instance 0 = sharedMaterial (공유 재질 그대로), 1 = material (처음 부르면 이 렌더러만의 사본으로 바꾼다 — Unity 와 같음)
NOVA_PACKAGE_EXPORT uint64 NovaMat_Get(uint64 id, int kind, int index, int instance)
{
	RendererRef r = FindRenderer(id, kind);
	if (!r || index < 0)
		return 0;
	const auto& mats = r.GetMaterials();
	shared_ptr<UMaterial> m = index < (int)mats.size() ? mats[index] : nullptr;
	if (!m && instance == 0)
		return HandleOf(UMaterial::GetDefault());
	if (instance && !(m && m->IsInstance()))
	{
		m = (m ? m : UMaterial::GetDefault())->CloneInstance();
		r.SetMaterialAt(index, m, L"");   // 씬에 저장하는 경로는 원본 그대로 (사본은 저장되지 않는다)
	}
	return HandleOf(m);
}

// sharedMaterial = 재질 (파일 재질이면 씬에 저장하는 경로도)
NOVA_PACKAGE_EXPORT int NovaMat_Set(uint64 id, int kind, int index, uint64 handle)
{
	RendererRef r = FindRenderer(id, kind);
	auto it = s_Handles.find(handle);
	if (!r || index < 0 || it == s_Handles.end())
		return 0;
	const shared_ptr<UMaterial>& m = it->second;
	const std::string& path = m->GetPath();
	r.SetMaterialAt(index, m, m->IsInstance() || path.empty() ? std::wstring() : string_to_wstring(path));
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

// MaterialPropertyBlock → 렌더러 (SetPropertyBlock). materialIndex < 0 = 렌더러 전체, 아니면 그 재질 칸만.
//  names = '\n' 으로 이은 이름, values = 이름마다 float4, colors = 이름마다 1 (색) · 0 (수)
NOVA_PACKAGE_EXPORT int NovaMat_SetBlock(uint64 id, int kind, int materialIndex, int count, const char* names, const float* values, const int* colors)
{
	RendererRef r = FindRenderer(id, kind);
	if (!r || count < 0)
		return 0;
	MaterialBlock::Values block;
	const char* p = names ? names : "";
	for (int i = 0; i < count; ++i)
	{
		const char* e = strchr(p, '\n');
		std::string name = e ? std::string(p, e) : std::string(p);
		p = e ? e + 1 : p + strlen(p);
		MaterialBlock::Value v;
		v.V = XMFLOAT4(values[i * 4], values[i * 4 + 1], values[i * 4 + 2], values[i * 4 + 3]);
		v.Color = colors[i] != 0;
		block.push_back({ name, v });
	}
	r.Block().SetAt(materialIndex, std::move(block));
	return 1;
}

// GetPropertyBlock: 값 수, index 번째 이름 · 값
NOVA_PACKAGE_EXPORT int NovaMat_BlockCount(uint64 id, int kind, int materialIndex)
{
	RendererRef r = FindRenderer(id, kind);
	return r ? (int)r.Block().GetAt(materialIndex).size() : 0;
}

// HasPropertyBlock: 렌더러 블록이나 재질 칸 블록이 하나라도
NOVA_PACKAGE_EXPORT int NovaMat_HasBlock(uint64 id, int kind)
{
	RendererRef r = FindRenderer(id, kind);
	return r && !r.Block().Empty() ? 1 : 0;
}

NOVA_PACKAGE_EXPORT const char* NovaMat_BlockEntry(uint64 id, int kind, int materialIndex, int index, float* value, int* color)
{
	RendererRef r = FindRenderer(id, kind);
	if (!r || index < 0 || index >= (int)r.Block().GetAt(materialIndex).size())
		return Keep(std::string());
	const auto& [name, v] = r.Block().GetAt(materialIndex)[index];
	value[0] = v.V.x; value[1] = v.V.y; value[2] = v.V.z; value[3] = v.V.w;
	*color = v.Color ? 1 : 0;
	return Keep(name);
}

// ---- Renderer 공통 (Sprite Renderer 포함)

// Renderer.enabled (Inspector 의 체크 상자 — 꺼지면 그리지 않는다)
NOVA_PACKAGE_EXPORT int NovaRenderer_GetEnabled(uint64 id, int kind)
{
	Component* c = FindComponent(id, kind);
	return c && c->IsEnabled() ? 1 : 0;
}

NOVA_PACKAGE_EXPORT void NovaRenderer_SetEnabled(uint64 id, int kind, int enabled)
{
	if (Component* c = FindComponent(id, kind))
		c->SetEnabled(enabled != 0);
}

// Renderer.bounds: 월드 상자 (최소 · 최대). Mesh · Skinned = 컬링이 마지막 Update 에 잰 상자 (Skinned 는 애니메이션 여유를 더한 상자),
//  아직 재지 않았으면 (같은 프레임에 만든 것) 위치에 크기 0. Sprite = 그림 사각형 × 월드 행렬, Line · Trail = 점들 + 가장 넓은 너비의 반
NOVA_PACKAGE_EXPORT int NovaRenderer_Bounds(uint64 id, int kind, float* mn, float* mx)
{
	Component* c = FindComponent(id, kind);
	if (c == nullptr || mn == nullptr || mx == nullptr)
		return 0;
	Transform* tr = c->GetGameObject() ? c->GetGameObject()->GetTransform() : nullptr;
	Vec3 a, b;
	bool ok = false;
	if (kind == 2)
	{
		Vec3 la, lb;
		if (tr && static_cast<SpriteRenderer*>(c)->SpriteLocalBounds(la, lb))
		{
			const Matrix w = tr->GetWorldMatrix();
			a = Vec3(FLT_MAX, FLT_MAX, FLT_MAX);
			b = Vec3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
			for (int i = 0; i < 8; ++i)
			{
				const Vec3 p = Vec3::Transform(Vec3(i & 1 ? lb.x : la.x, i & 2 ? lb.y : la.y, i & 4 ? lb.z : la.z), w);
				a = Vec3::Min(a, p);
				b = Vec3::Max(b, p);
			}
			ok = true;
		}
	}
	else if (kind == 3 || kind == 4)
	{
		auto* line = static_cast<LineRendererBase*>(c);
		std::vector<Vec3> points;
		line->WorldPoints(points);
		if (!points.empty())
		{
			float half = 0.0f;
			for (int i = 0; i <= 8; ++i)
				half = (std::max)(half, line->WidthAt(i / 8.0f) * 0.5f);
			a = b = points[0];
			for (const Vec3& p : points)
			{
				a = Vec3::Min(a, p);
				b = Vec3::Max(b, p);
			}
			a -= Vec3(half, half, half);
			b += Vec3(half, half, half);
			ok = true;
		}
	}
	else
		ok = SceneCulling::TrackedBounds(c, a, b);
	if (!ok)
	{
		a = b = tr ? tr->GetWorldMatrix().Translation() : Vec3::Zero;
	}
	mn[0] = a.x; mn[1] = a.y; mn[2] = a.z;
	mx[0] = b.x; mx[1] = b.y; mx[2] = b.z;
	return ok ? 1 : 0;
}

// Renderer.shadowCastingMode: Unity 값 (Off 0, On 1, TwoSided 2, ShadowsOnly 3) ↔ 엔진 (On 0, Off 1, Two Sided 2, Shadows Only 3).
//  Sprite · Line · Trail Renderer 는 그림자를 드리우지 않는다 (Off, 바꿔도 무시)
namespace
{
	int ToUnityShadows(int native) { return native == 0 ? 1 : native == 1 ? 0 : native; }
}

NOVA_PACKAGE_EXPORT int NovaRenderer_GetShadows(uint64 id, int kind)
{
	Component* c = FindComponent(id, kind);
	if (kind == 0 && c) return ToUnityShadows(static_cast<MeshRenderer*>(c)->GetCastShadows());
	if (kind == 1 && c) return ToUnityShadows(static_cast<SkinnedMeshRenderer*>(c)->GetCastShadows());
	return 0;
}

NOVA_PACKAGE_EXPORT void NovaRenderer_SetShadows(uint64 id, int kind, int mode)
{
	if (mode < 0 || mode > 3)
		return;
	Component* c = FindComponent(id, kind);
	const int native = ToUnityShadows(mode);   // 0 ↔ 1 맞바꿈이라 같은 함수
	if (kind == 0 && c) static_cast<MeshRenderer*>(c)->SetCastShadows(native);
	if (kind == 1 && c) static_cast<SkinnedMeshRenderer*>(c)->SetCastShadows(native);
}

// Renderer.sortingLayerID · sortingOrder (which 0 = 레이어 id, 1 = 순서). 투명 순서에 쓰는 렌더러: Sprite · Mesh (투명 재질) · Line · Trail,
//  Skinned 는 값만 (Unity 처럼 불투명에는 영향 없음)
NOVA_PACKAGE_EXPORT int NovaRenderer_GetSorting(uint64 id, int kind, int which)
{
	Component* c = FindComponent(id, kind);
	if (c == nullptr) return 0;
	switch (kind)
	{
	case 0: { auto* r = static_cast<MeshRenderer*>(c); return which == 0 ? r->GetSortingLayerId() : r->GetSortingOrder(); }
	case 1: { auto* r = static_cast<SkinnedMeshRenderer*>(c); return which == 0 ? r->GetSortingLayerId() : r->GetSortingOrder(); }
	case 2: { auto* r = static_cast<SpriteRenderer*>(c); return which == 0 ? r->GetSortingLayerId() : r->GetSortingOrder(); }
	case 3: case 4: { auto* l = static_cast<LineRendererBase*>(c); return which == 0 ? l->SortingLayerId : l->SortingOrder; }
	}
	return 0;
}

NOVA_PACKAGE_EXPORT void NovaRenderer_SetSorting(uint64 id, int kind, int which, int value)
{
	Component* c = FindComponent(id, kind);
	if (c == nullptr) return;
	switch (kind)
	{
	case 0: { auto* r = static_cast<MeshRenderer*>(c); if (which == 0) r->SetSortingLayerId(value); else r->SetSortingOrder(value); break; }
	case 1: { auto* r = static_cast<SkinnedMeshRenderer*>(c); if (which == 0) r->SetSortingLayerId(value); else r->SetSortingOrder(value); break; }
	case 2: { auto* r = static_cast<SpriteRenderer*>(c); if (which == 0) r->SetSortingLayerId(value); else r->SetSortingOrder(value); break; }
	case 3: case 4: { auto* l = static_cast<LineRendererBase*>(c); if (which == 0) l->SortingLayerId = value; else l->SortingOrder = value; break; }
	}
}

// Sorting Layer 이름 ↔ id (Project Settings > Tags and Layers). 없는 이름 = -1 (C# 이 무시)
NOVA_PACKAGE_EXPORT const char* NovaRenderer_SortingLayerName(int layerId)
{
	return Keep(TagsAndLayers::SortingLayerName(layerId));
}

NOVA_PACKAGE_EXPORT int NovaRenderer_SortingLayerId(const char* name)
{
	return name ? TagsAndLayers::SortingLayerIdFromName(name) : -1;
}
