#include "pch.h"
#include "MaterialBlock.h"
#include "ShaderGraphRuntime.h"

namespace
{
	uint64 MixHash(uint64 h, uint64 v)
	{
		h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
		return h;
	}

	// 블록 파생 재질: (원본 재질, 원본 값, 블록 값) 마다 하나
	struct Variant { std::shared_ptr<UMaterial> Base, Material; };
	std::map<std::tuple<const UMaterial*, uint64, uint64>, Variant> s_Variants;

	std::shared_ptr<UMaterial> VariantOf(const std::shared_ptr<UMaterial>& base, uint64 blockHash, const MaterialBlock::Values& block)
	{
		const std::shared_ptr<UMaterial> src = base ? base : UMaterial::GetDefault();
		const auto key = std::make_tuple(src.get(), src->StateHash(), blockHash);
		auto it = s_Variants.find(key);
		if (it != s_Variants.end())
			return it->second.Material;
		if (s_Variants.size() > 4096)
			for (auto v = s_Variants.begin(); v != s_Variants.end();)   // 아무도 쓰지 않는 파생 재질 (캐시만 잡고 있다)
				v = v->second.Material.use_count() == 1 ? s_Variants.erase(v) : std::next(v);
		std::shared_ptr<UMaterial> m = src->CloneInstance();
		for (const auto& [name, value] : block)
		{
			if (value.Color) m->SetColorProperty(name, value.V);
			else m->SetFloatProperty(name, value.V.x);
		}
		s_Variants[key] = { src, m };
		return m;
	}
}

namespace
{
	// 이름 순으로 정렬하고 해시 (같은 값 = 같은 해시 → 같은 파생 재질)
	uint64 SortAndHash(MaterialBlock::Values& values)
	{
		std::sort(values.begin(), values.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
		uint64 h = 1469598103934665603ull;
		for (const auto& [name, value] : values)
		{
			h = MixHash(h, std::hash<std::string>()(name));
			h = MixHash(h, value.Color ? 1 : 2);
			uint32_t bits[4];
			memcpy(bits, &value.V, sizeof(bits));
			for (uint32_t b : bits) h = MixHash(h, b);
		}
		return h;
	}

	const MaterialBlock::Values s_NoValues;
}

void MaterialBlock::Set(Values values)
{
	m_Hash = SortAndHash(values);
	m_Values = std::move(values);
	m_Stamp = 0;
}

void MaterialBlock::SetAt(int materialIndex, Values values)
{
	if (materialIndex < 0)
	{
		Set(std::move(values));
		return;
	}
	if (values.empty())
		m_PerIndex.erase(materialIndex);
	else
	{
		IndexBlock& b = m_PerIndex[materialIndex];
		b.Hash = SortAndHash(values);
		b.V = std::move(values);
	}
	m_Stamp = 0;
}

const MaterialBlock::Values& MaterialBlock::GetAt(int materialIndex) const
{
	if (materialIndex < 0)
		return m_Values;
	auto it = m_PerIndex.find(materialIndex);
	return it != m_PerIndex.end() ? it->second.V : s_NoValues;
}

namespace
{
	// 엔진 Lit · Unlit: 이름 → 칸 (UMaterial 의 Unity URP 이름), 값은 엔진이 쓰는 형태로 (Emission = 선형)
	bool PutEngine(MaterialBlock::InstanceValues& v, const std::string& name, const MaterialBlock::Value& value)
	{
		switch (UMaterial::InstancePropOf(name))
		{
		case UMaterial::InstanceProp::BaseColor:
			if (!value.Color) return false;
			v.HasBaseColor = true;
			v.BaseColor = value.V;
			return true;
		case UMaterial::InstanceProp::Emission:
			if (!value.Color) return false;
			v.HasEmission = true;
			v.Emission = UMaterial::EmissionToLinear(value.V);
			return true;
		case UMaterial::InstanceProp::Metallic:
			if (value.Color) return false;   // 재질에서도 색으로는 넣을 수 없다 (SetColorProperty 가 무시)
			v.HasMetallic = true;
			v.Metallic = value.V.x;
			return true;
		case UMaterial::InstanceProp::Smoothness:
			if (value.Color) return false;
			v.HasSmoothness = true;
			v.Smoothness = value.V.x;
			return true;
		default:
			return false;   // 그 밖의 속성은 파생 재질
		}
	}

	// Shader Graph: 그래프 속성 (Reference) 이 인스턴스 칸이면 그 값 그대로 (그래프가 쓰는 값 — 변환 없음). 모든 재질의 그래프에서 같은 칸이어야 한다
	bool PutGraph(MaterialBlock::InstanceValues& v, const std::string& name, const MaterialBlock::Value& value, const std::vector<std::shared_ptr<UMaterial>>& materials)
	{
		ShaderGraph::InstanceSlot slot = ShaderGraph::InstanceSlot::None;
		for (const auto& m : materials)
		{
			bool isColor = false;
			const ShaderGraph::InstanceSlot s = ShaderGraph::InstanceSlotFor(m->CustomShader(), name, isColor);
			if (s == ShaderGraph::InstanceSlot::None || isColor != value.Color || (slot != ShaderGraph::InstanceSlot::None && s != slot))
				return false;
			slot = s;
		}
		switch (slot)
		{
		case ShaderGraph::InstanceSlot::BaseColor: v.HasBaseColor = true; v.BaseColor = value.V; return true;
		case ShaderGraph::InstanceSlot::Emission: v.HasEmission = true; v.Emission = XMFLOAT3(value.V.x, value.V.y, value.V.z); return true;
		case ShaderGraph::InstanceSlot::Metallic: v.HasMetallic = true; v.Metallic = value.V.x; return true;
		case ShaderGraph::InstanceSlot::Smoothness: v.HasSmoothness = true; v.Smoothness = value.V.x; return true;
		default: return false;
		}
	}
}

bool MaterialBlock::Instanced(const std::vector<std::shared_ptr<UMaterial>>& materials, InstanceValues& out) const
{
	if (m_Values.empty() || !m_PerIndex.empty())
		return false;   // 재질 칸 블록은 칸마다 값이 달라 파생 재질
	// 재질이 모두 엔진 Lit · Unlit 이거나 모두 Shader Graph 여야 한다 (같은 칸이라도 값의 뜻이 다르다 — 엔진 Emission 은 선형)
	bool engine = false, graph = false;
	for (const auto& m : materials)
	{
		const UMaterial* u = m ? m.get() : UMaterial::GetDefault().get();
		if (!u->IsCustom())
		{
			if (u->GetPbr().AlphaClip)
				return false;   // 잘라내기는 깊이 · 그림자 패스가 재질 알파를 쓴다
			engine = true;
		}
		else if (ShaderGraph::IsGraphShader(u->CustomShader()))
			graph = true;
		else
			return false;   // 패키지 셰이더는 인스턴스 값을 읽지 않는다
	}
	if (engine == graph)
		return false;   // 섞였거나 재질이 없다
	InstanceValues v;
	for (const auto& [name, value] : m_Values)   // 이름 순 — 같은 뜻의 이름이 둘이면 파생 재질과 같이 뒤의 것 (_Color · _Glossiness)
		if (!(engine ? PutEngine(v, name, value) : PutGraph(v, name, value, materials)))
			return false;
	if (engine && v.HasEmission)
		for (const auto& m : materials)
			if (!(m ? m.get() : UMaterial::GetDefault().get())->CanInstanceEmission())
				return false;
	out = v;
	return true;
}

const std::vector<std::shared_ptr<UMaterial>>& MaterialBlock::Apply(const std::vector<std::shared_ptr<UMaterial>>& materials)
{
	if (Empty())
		return materials;
	uint64 stamp = MixHash(m_Hash, materials.size());
	for (const auto& [index, b] : m_PerIndex)
		stamp = MixHash(MixHash(stamp, (uint64)index), b.Hash);
	for (const auto& m : materials)
		stamp = MixHash(MixHash(stamp, (uint64)(uintptr_t)m.get()), m ? m->StateHash() : 0);
	if (stamp != m_Stamp || m_Render.size() != materials.size())
	{
		m_Render.resize(materials.size());
		for (size_t i = 0; i < materials.size(); ++i)
		{
			auto it = m_PerIndex.find((int)i);
			if (it == m_PerIndex.end())
			{
				m_Render[i] = m_Values.empty() ? materials[i] : VariantOf(materials[i], m_Hash, m_Values);
				continue;
			}
			// 칸 블록을 렌더러 블록 위에 (같은 이름이면 칸 쪽)
			Values merged = it->second.V;
			for (const auto& [name, value] : m_Values)
				if (std::none_of(merged.begin(), merged.end(), [&](const auto& e) { return e.first == name; }))
					merged.push_back({ name, value });
			const uint64 hash = SortAndHash(merged);
			m_Render[i] = VariantOf(materials[i], hash, merged);
		}
		m_Stamp = stamp;
	}
	return m_Render;
}
