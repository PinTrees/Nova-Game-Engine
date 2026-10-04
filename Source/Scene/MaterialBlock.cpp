#include "pch.h"
#include "MaterialBlock.h"

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

void MaterialBlock::Set(Values values)
{
	std::sort(values.begin(), values.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
	m_Values = std::move(values);
	uint64 h = 1469598103934665603ull;
	for (const auto& [name, value] : m_Values)
	{
		h = MixHash(h, std::hash<std::string>()(name));
		h = MixHash(h, value.Color ? 1 : 2);
		uint32_t bits[4];
		memcpy(bits, &value.V, sizeof(bits));
		for (uint32_t b : bits) h = MixHash(h, b);
	}
	m_Hash = h;
	m_Stamp = 0;
}

bool MaterialBlock::InstanceColor(const std::vector<std::shared_ptr<UMaterial>>& materials, XMFLOAT4& color) const
{
	if (m_Values.empty())
		return false;
	const Value* found = nullptr;
	for (const auto& [name, value] : m_Values)   // 이름 순 — 둘 다 있으면 파생 재질과 같이 뒤의 것 (_Color)
	{
		if (!value.Color || !UMaterial::IsBaseColorProperty(name))
			return false;
		found = &value;
	}
	for (const auto& m : materials)
	{
		const UMaterial* u = m ? m.get() : UMaterial::GetDefault().get();
		if (u->IsCustom() || u->GetPbr().AlphaClip)
			return false;   // 패키지 · Shader Graph 셰이더는 인스턴스 값을 읽지 않는다, 잘라내기는 깊이 · 그림자 패스가 재질 알파를 쓴다
	}
	color = found->V;
	return true;
}

const std::vector<std::shared_ptr<UMaterial>>& MaterialBlock::Apply(const std::vector<std::shared_ptr<UMaterial>>& materials)
{
	if (m_Values.empty())
		return materials;
	uint64 stamp = MixHash(m_Hash, materials.size());
	for (const auto& m : materials)
		stamp = MixHash(MixHash(stamp, (uint64)(uintptr_t)m.get()), m ? m->StateHash() : 0);
	if (stamp != m_Stamp || m_Render.size() != materials.size())
	{
		m_Render.resize(materials.size());
		for (size_t i = 0; i < materials.size(); ++i)
			m_Render[i] = VariantOf(materials[i], m_Hash, m_Values);
		m_Stamp = stamp;
	}
	return m_Render;
}
