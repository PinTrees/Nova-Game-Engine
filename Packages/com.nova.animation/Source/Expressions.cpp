#include "pch.h"
#include "Expressions.h"
#include "UnityGUI.h"
#include "SkinnedMeshRenderer.h"
#include "EngineTime.h"

namespace
{
	SkinnedMeshRenderer* FindByName(GameObject* go, const std::string& name)
	{
		if (go == nullptr) return nullptr;
		if (go->GetName() == name)
			if (auto* r = go->GetComponent<SkinnedMeshRenderer>()) return r;
		for (GameObject* c : go->GetChildren())
			if (auto* r = FindByName(c, name)) return r;
		return nullptr;
	}

	bool IsBlink(const std::string& n) { return n == "blink" || n == "blinkLeft" || n == "blinkRight"; }
	bool IsMouth(const std::string& n) { return n == "aa" || n == "ih" || n == "ou" || n == "ee" || n == "oh"; }
}

Expressions::Expressions()
{
	m_InspectorTitleName = "Expressions";
}

int Expressions::Find(const std::string& name) const
{
	for (int i = 0; i < (int)List.size(); ++i) if (List[i].Name == name) return i;
	return -1;
}

bool Expressions::SetWeight(const std::string& name, float value)
{
	const int i = Find(name);
	if (i < 0) return false;
	List[i].Value = std::clamp(value, 0.0f, 1.0f);
	Apply();
	return true;
}

float Expressions::GetWeight(const std::string& name) const
{
	const int i = Find(name);
	return i >= 0 ? List[i].Value : 0.0f;
}

SkinnedMeshRenderer* Expressions::FindRenderer(const std::string& name) const
{
	if (!m_pGameObject) return nullptr;
	for (GameObject* c : m_pGameObject->GetChildren())
		if (auto* r = FindByName(c, name)) return r;
	return nullptr;
}

void Expressions::Update()
{
	if (!m_Enabled)
		return;   // Unity: 끄면 멈춘다 (깜빡임 · 표정 적용)
	// 자동 깜빡임: 다음 시각이 되면 닫고 (40 %) 잠깐 멈췄다 (20 %) 연다 (40 %)
	const float dt = std::clamp(Time::DeltaTime(), 0.0f, 0.1f);
	if (AutoBlink && Find("blink") >= 0)
	{
		if (m_NextBlink < 0.0f)
			m_NextBlink = (std::max)(0.5f, BlinkInterval * 0.75f);   // 첫 깜빡임 (Blink Interval 기준)
		m_BlinkTimer += dt;
		if (m_BlinkTime < 0.0f && m_BlinkTimer >= m_NextBlink)
		{
			m_BlinkTime = 0.0f;
			m_BlinkTimer = 0.0f;
			m_Seed = m_Seed * 1664525u + 1013904223u;
			const float r = (m_Seed >> 8) / 16777216.0f;   // 0..1
			m_NextBlink = (std::max)(0.5f, BlinkInterval * (0.5f + r));
		}
		if (m_BlinkTime >= 0.0f)
		{
			m_BlinkTime += dt;
			const float d = (std::max)(0.05f, BlinkDuration), t = m_BlinkTime / d;
			m_Blink = t < 0.4f ? t / 0.4f : (t < 0.6f ? 1.0f : (std::max)(0.0f, 1.0f - (t - 0.6f) / 0.4f));
			if (t >= 1.0f) { m_BlinkTime = -1.0f; m_Blink = 0.0f; }
		}
	}
	else
		m_Blink = 0.0f;
	Apply();
}

// 표정 값 → BlendShape: 묶음마다 값 × 가중치를 더하고 (0..100), 깜빡임 · 입 모양은 block 표정이 켜지면 막는다
void Expressions::Apply()
{
	float blockBlink = 0.0f, blockMouth = 0.0f;
	for (const Expression& e : List)
	{
		const float v = e.Binary ? (e.Value > 0.5f ? 1.0f : 0.0f) : e.Value;
		if (v <= 0.0f) continue;
		if (!IsBlink(e.Name) && e.OverrideBlink == "block") blockBlink = 1.0f;
		else if (!IsBlink(e.Name) && e.OverrideBlink == "blend") blockBlink = (std::max)(blockBlink, v);
		if (!IsMouth(e.Name) && e.OverrideMouth == "block") blockMouth = 1.0f;
		else if (!IsMouth(e.Name) && e.OverrideMouth == "blend") blockMouth = (std::max)(blockMouth, v);
	}
	std::map<std::pair<std::string, int>, float> sum;
	std::map<std::string, SkinnedMeshRenderer*> renderers;
	auto renderer = [&](const std::string& n) {
		auto it = renderers.find(n);
		if (it == renderers.end()) it = renderers.emplace(n, FindRenderer(n)).first;
		return it->second;
	};
	for (const Expression& e : List)
	{
		float v = e.Binary ? (e.Value > 0.5f ? 1.0f : 0.0f) : e.Value;
		if (e.Name == "blink") v = (std::max)(v, m_Blink);
		if (IsBlink(e.Name)) v *= 1.0f - blockBlink;
		if (IsMouth(e.Name)) v *= 1.0f - blockMouth;
		for (const Bind& b : e.Binds)
		{
			SkinnedMeshRenderer* r = renderer(b.Renderer);
			if (!r) continue;
			int idx = b.Shape.empty() ? -1 : r->BlendShapeIndex(b.Shape);
			if (idx < 0) idx = b.Index;
			if (idx < 0 || idx >= r->BlendShapeCount()) continue;
			sum[{ b.Renderer, idx }] += v * b.Weight;
		}
	}
	// 지난번에 썼는데 이번엔 없는 셰이프 = 0
	for (const auto& t : m_Touched)
		if (!sum.count(t))
			if (SkinnedMeshRenderer* r = renderer(t.first)) r->SetBlendShapeWeight(t.second, 0.0f);
	m_Touched.clear();
	for (const auto& [key, w] : sum)
	{
		if (SkinnedMeshRenderer* r = renderer(key.first)) r->SetBlendShapeWeight(key.second, std::clamp(w, 0.0f, 100.0f));
		m_Touched.push_back(key);
	}
}

void Expressions::OnInspectorGUI()
{
	UnityGUI::Toggle("Auto Blink", &AutoBlink);
	UnityGUI::Slider("Blink Interval", &BlinkInterval, 0.5f, 10.0f, 1);
	UnityGUI::Slider("Blink Duration", &BlinkDuration, 0.05f, 0.5f, 1);
	char buf[96];
	snprintf(buf, sizeof(buf), "Expressions (%d)", (int)List.size());
	if (UnityGUI::FoldoutPlain(buf, 0, true))
		for (size_t i = 0; i < List.size(); ++i)
		{
			ImGui::PushID((int)i);
			Expression& e = List[i];
			if (UnityGUI::Slider(e.Name.c_str(), &e.Value, 0.0f, 1.0f, 1))
				Apply();
			ImGui::PopID();
		}
	if (List.empty())
		UnityGUI::HelpBox("VRM characters get their expressions (happy, angry, sad, blink, aa ...) here automatically. C#: GetComponent<Expressions>().SetWeight(\"happy\", 1).", false);
}

GENERATE_COMPONENT_FUNC_TOJSON(Expressions)
{
	json j;
	j["type"] = "Expressions";
	j["enabled"] = m_Enabled;
	j["autoBlink"] = AutoBlink;
	j["blinkInterval"] = BlinkInterval;
	j["blinkDuration"] = BlinkDuration;
	json list = json::array();
	for (const Expression& e : List)
	{
		json binds = json::array();
		for (const Bind& b : e.Binds)
			binds.push_back({ { "renderer", b.Renderer }, { "shape", b.Shape }, { "index", b.Index }, { "weight", b.Weight } });
		json ej = { { "name", e.Name }, { "binds", binds } };
		if (e.Value != 0.0f) ej["value"] = e.Value;
		if (e.Binary) ej["binary"] = true;
		if (e.OverrideBlink != "none") ej["overrideBlink"] = e.OverrideBlink;
		if (e.OverrideMouth != "none") ej["overrideMouth"] = e.OverrideMouth;
		list.push_back(ej);
	}
	j["expressions"] = list;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(Expressions)
{
	m_Enabled = j.value("enabled", true);
	AutoBlink = j.value("autoBlink", true);
	BlinkInterval = j.value("blinkInterval", 4.0f);
	BlinkDuration = j.value("blinkDuration", 0.15f);
	List.clear();
	for (const json& ej : j.value("expressions", json::array()))
	{
		Expression e;
		e.Name = ej.value("name", std::string("Expression"));
		e.Value = ej.value("value", 0.0f);
		e.Binary = ej.value("binary", false);
		e.OverrideBlink = ej.value("overrideBlink", std::string("none"));
		e.OverrideMouth = ej.value("overrideMouth", std::string("none"));
		for (const json& bj : ej.value("binds", json::array()))
			e.Binds.push_back({ bj.value("renderer", std::string()), bj.value("shape", std::string()), bj.value("index", -1), bj.value("weight", 100.0f) });
		List.push_back(e);
	}
	m_Touched.clear();
}
