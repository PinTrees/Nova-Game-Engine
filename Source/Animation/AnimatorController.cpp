#include "pch.h"
#include "AnimatorController.h"
#include "SkinnedData.h"

using namespace AnimatorTypes;

namespace
{
	const char* ParamTypeName(ParamType t)
	{
		switch (t)
		{
		case ParamType::Int: return "Int";
		case ParamType::Bool: return "Bool";
		case ParamType::Trigger: return "Trigger";
		default: return "Float";
		}
	}
	ParamType ParseParamType(const std::string& s)
	{
		if (s == "Int") return ParamType::Int;
		if (s == "Bool") return ParamType::Bool;
		if (s == "Trigger") return ParamType::Trigger;
		return ParamType::Float;
	}
	const char* ModeName(ConditionMode m)
	{
		switch (m)
		{
		case ConditionMode::IfNot: return "IfNot";
		case ConditionMode::Greater: return "Greater";
		case ConditionMode::Less: return "Less";
		case ConditionMode::Equals: return "Equals";
		case ConditionMode::NotEqual: return "NotEqual";
		default: return "If";
		}
	}
	ConditionMode ParseMode(const std::string& s)
	{
		if (s == "IfNot") return ConditionMode::IfNot;
		if (s == "Greater") return ConditionMode::Greater;
		if (s == "Less") return ConditionMode::Less;
		if (s == "Equals") return ConditionMode::Equals;
		if (s == "NotEqual") return ConditionMode::NotEqual;
		return ConditionMode::If;
	}

	std::map<std::string, std::weak_ptr<AnimatorController>>& Cache()
	{
		static std::map<std::string, std::weak_ptr<AnimatorController>> cache;
		return cache;
	}

	// 캐시 키: 구분자를 '\' 로, 앞쪽 구분자 제거 ("Assets/A.controller" == "Assets\A.controller")
	std::string NormalizePath(std::string path)
	{
		std::replace(path.begin(), path.end(), '/', '\\');
		if (!std::filesystem::path(path).is_absolute())
		{
			const size_t first = path.find_first_not_of('\\');
			path.erase(0, first == std::string::npos ? path.size() : first);
		}
		return path;
	}

	std::wstring FilePath(const std::string& path)
	{
		std::filesystem::path p(string_to_wstring(path));
		return p.is_absolute() ? p.wstring() : PathManager::GetI()->GetMovePathW(string_to_wstring(path));
	}
}

void AnimatorState::LoadClip()
{
	Clip = ClipPath.empty() ? nullptr : ResourceManager::GetI()->LoadAnimationClip(ClipPath, ClipIndex);
}

int AnimatorLayer::FindState(const std::string& name) const
{
	for (size_t i = 0; i < States.size(); ++i)
		if (States[i].Name == name)
			return (int)i;
	return -1;
}

int AnimatorController::FindParameter(const std::string& name) const
{
	for (size_t i = 0; i < Parameters.size(); ++i)
		if (Parameters[i].Name == name)
			return (int)i;
	return -1;
}

std::string AnimatorController::Name() const
{
	return std::filesystem::path(Path).stem().string();
}

bool AnimatorController::Save() const
{
	json j;
	json params = json::array();
	for (const auto& p : Parameters)
		params.push_back({ { "name", p.Name }, { "type", ParamTypeName(p.Type) }, { "float", p.DefaultFloat }, { "int", p.DefaultInt }, { "bool", p.DefaultBool } });
	j["parameters"] = params;

	json layers = json::array();
	for (const auto& l : Layers)
	{
		json lj;
		lj["name"] = l.Name;
		lj["weight"] = l.Weight;
		lj["defaultState"] = l.DefaultState;
		lj["entry"] = { l.EntryX, l.EntryY };
		lj["any"] = { l.AnyX, l.AnyY };
		lj["exit"] = { l.ExitX, l.ExitY };
		json states = json::array();
		for (const auto& s : l.States)
			states.push_back({ { "name", s.Name }, { "clipPath", s.ClipPath }, { "clipIndex", s.ClipIndex }, { "speed", s.Speed },
				{ "cycleOffset", s.CycleOffset }, { "loop", s.Loop }, { "writeDefaults", s.WriteDefaults }, { "footIK", s.FootIK }, { "pos", { s.PosX, s.PosY } } });
		lj["states"] = states;
		json transitions = json::array();
		for (const auto& t : l.Transitions)
		{
			json conds = json::array();
			for (const auto& c : t.Conditions)
				conds.push_back({ { "parameter", c.Parameter }, { "mode", ModeName(c.Mode) }, { "threshold", c.Threshold } });
			transitions.push_back({ { "from", t.From }, { "to", t.To }, { "hasExitTime", t.HasExitTime }, { "exitTime", t.ExitTime },
				{ "fixedDuration", t.FixedDuration }, { "duration", t.Duration }, { "offset", t.Offset }, { "canTransitionToSelf", t.CanTransitionToSelf },
				{ "mute", t.Mute }, { "solo", t.Solo }, { "conditions", conds } });
		}
		lj["transitions"] = transitions;
		layers.push_back(lj);
	}
	j["layers"] = layers;

	const std::wstring file = FilePath(Path);
	std::error_code ec;
	std::filesystem::create_directories(std::filesystem::path(file).parent_path(), ec);
	std::ofstream os(file, std::ios::binary | std::ios::trunc);
	if (!os)
		return false;
	os << j.dump(4);
	return true;
}

std::shared_ptr<AnimatorController> AnimatorController::Load(const std::string& rawPath)
{
	const std::string path = NormalizePath(rawPath);
	if (path.empty())
		return nullptr;
	auto it = Cache().find(path);
	if (it != Cache().end())
		if (auto existing = it->second.lock())
			return existing;

	std::ifstream in(FilePath(path), std::ios::binary);
	if (!in)
		return nullptr;
	json j = json::parse(in, nullptr, false);
	if (j.is_discarded())
		return nullptr;

	auto c = std::make_shared<AnimatorController>();
	c->Path = path;
	if (j.contains("parameters"))
		for (const auto& pj : j["parameters"])
		{
			AnimatorParameter p;
			p.Name = pj.value("name", std::string());
			p.Type = ParseParamType(pj.value("type", std::string("Float")));
			p.DefaultFloat = pj.value("float", 0.0f);
			p.DefaultInt = pj.value("int", 0);
			p.DefaultBool = pj.value("bool", false);
			c->Parameters.push_back(p);
		}
	if (j.contains("layers"))
		for (const auto& lj : j["layers"])
		{
			AnimatorLayer l;
			l.Name = lj.value("name", std::string("Base Layer"));
			l.Weight = lj.value("weight", 1.0f);
			l.DefaultState = lj.value("defaultState", std::string());
			if (lj.contains("entry")) { l.EntryX = lj["entry"][0]; l.EntryY = lj["entry"][1]; }
			if (lj.contains("any")) { l.AnyX = lj["any"][0]; l.AnyY = lj["any"][1]; }
			if (lj.contains("exit")) { l.ExitX = lj["exit"][0]; l.ExitY = lj["exit"][1]; }
			if (lj.contains("states"))
				for (const auto& sj : lj["states"])
				{
					AnimatorState s;
					s.Name = sj.value("name", std::string("New State"));
					s.ClipPath = sj.value("clipPath", std::string());
					s.ClipIndex = sj.value("clipIndex", 0);
					s.Speed = sj.value("speed", 1.0f);
					s.CycleOffset = sj.value("cycleOffset", 0.0f);
					s.Loop = sj.value("loop", true);
					s.WriteDefaults = sj.value("writeDefaults", true);
					s.FootIK = sj.value("footIK", false);
					if (sj.contains("pos")) { s.PosX = sj["pos"][0]; s.PosY = sj["pos"][1]; }
					s.LoadClip();
					l.States.push_back(s);
				}
			if (lj.contains("transitions"))
				for (const auto& tj : lj["transitions"])
				{
					AnimatorTransition t;
					t.From = tj.value("from", std::string());
					t.To = tj.value("to", std::string());
					t.HasExitTime = tj.value("hasExitTime", true);
					t.ExitTime = tj.value("exitTime", 0.75f);
					t.FixedDuration = tj.value("fixedDuration", true);
					t.Duration = tj.value("duration", 0.25f);
					t.Offset = tj.value("offset", 0.0f);
					t.CanTransitionToSelf = tj.value("canTransitionToSelf", true);
					t.Mute = tj.value("mute", false);
					t.Solo = tj.value("solo", false);
					if (tj.contains("conditions"))
						for (const auto& cj : tj["conditions"])
						{
							AnimatorCondition cond;
							cond.Parameter = cj.value("parameter", std::string());
							cond.Mode = ParseMode(cj.value("mode", std::string("If")));
							cond.Threshold = cj.value("threshold", 0.0f);
							t.Conditions.push_back(cond);
						}
					l.Transitions.push_back(t);
				}
			c->Layers.push_back(l);
		}
	if (c->Layers.empty())
		c->Layers.push_back(AnimatorLayer());
	Cache()[path] = c;
	return c;
}

std::shared_ptr<AnimatorController> AnimatorController::Create(const std::string& rawPath)
{
	const std::string path = NormalizePath(rawPath);
	auto c = std::make_shared<AnimatorController>();
	c->Path = path;
	c->Layers.push_back(AnimatorLayer());
	c->Save();
	Cache()[path] = c;
	return c;
}

// ------------------------------------------------------------------ 편집
std::string AnimatorController::MakeUniqueStateName(int layer, const std::string& base) const
{
	const AnimatorLayer& l = Layers[layer];
	if (l.FindState(base) < 0)
		return base;
	// Unity: "New State", "New State 0", "New State 1", ...
	for (int n = 0;; ++n)
	{
		std::string name = base + " " + std::to_string(n);
		if (l.FindState(name) < 0)
			return name;
	}
}

int AnimatorController::AddState(int layer, const std::string& name, float x, float y)
{
	AnimatorLayer& l = Layers[layer];
	AnimatorState s;
	s.Name = MakeUniqueStateName(layer, name);
	s.PosX = x;
	s.PosY = y;
	l.States.push_back(s);
	// 첫 상태는 기본 상태가 된다
	if (l.FindState(l.DefaultState) < 0)
		l.DefaultState = s.Name;
	return (int)l.States.size() - 1;
}

void AnimatorController::RemoveState(int layer, int state)
{
	AnimatorLayer& l = Layers[layer];
	if (state < 0 || state >= (int)l.States.size())
		return;
	const std::string name = l.States[state].Name;
	l.States.erase(l.States.begin() + state);
	l.Transitions.erase(std::remove_if(l.Transitions.begin(), l.Transitions.end(),
		[&](const AnimatorTransition& t) { return t.From == name || t.To == name; }), l.Transitions.end());
	if (l.DefaultState == name)
		l.DefaultState = l.States.empty() ? std::string() : l.States[0].Name;
}

bool AnimatorController::RenameState(int layer, int state, const std::string& newName)
{
	AnimatorLayer& l = Layers[layer];
	if (state < 0 || state >= (int)l.States.size() || newName.empty() || newName[0] == '<')
		return false;
	const std::string old = l.States[state].Name;
	if (old == newName)
		return false;
	const std::string name = MakeUniqueStateName(layer, newName);
	l.States[state].Name = name;
	for (auto& t : l.Transitions)
	{
		if (t.From == old) t.From = name;
		if (t.To == old) t.To = name;
	}
	if (l.DefaultState == old)
		l.DefaultState = name;
	return true;
}

int AnimatorController::AddTransition(int layer, const std::string& from, const std::string& to)
{
	AnimatorTransition t;
	t.From = from;
	t.To = to;
	// Any State / Entry 에서 나가는 전이는 Exit Time 없이 조건으로만 (Unity 기본값과 같다)
	if (from == kAnyState || from == kEntry)
		t.HasExitTime = false;
	Layers[layer].Transitions.push_back(t);
	return (int)Layers[layer].Transitions.size() - 1;
}

void AnimatorController::RemoveTransition(int layer, int index)
{
	auto& list = Layers[layer].Transitions;
	if (index >= 0 && index < (int)list.size())
		list.erase(list.begin() + index);
}

std::string AnimatorController::MakeUniqueParameterName(const std::string& base) const
{
	if (FindParameter(base) < 0)
		return base;
	for (int n = 0;; ++n)
	{
		std::string name = base + " " + std::to_string(n);
		if (FindParameter(name) < 0)
			return name;
	}
}

int AnimatorController::AddParameter(ParamType type)
{
	static const char* kBase[] = { "New Float", "New Int", "New Bool", "New Trigger" };
	AnimatorParameter p;
	p.Type = type;
	p.Name = MakeUniqueParameterName(kBase[(int)type]);
	Parameters.push_back(p);
	return (int)Parameters.size() - 1;
}

void AnimatorController::RemoveParameter(int index)
{
	if (index < 0 || index >= (int)Parameters.size())
		return;
	const std::string name = Parameters[index].Name;
	Parameters.erase(Parameters.begin() + index);
	// Unity 처럼 그 파라미터를 쓰던 조건도 지운다
	for (auto& l : Layers)
		for (auto& t : l.Transitions)
			t.Conditions.erase(std::remove_if(t.Conditions.begin(), t.Conditions.end(),
				[&](const AnimatorCondition& c) { return c.Parameter == name; }), t.Conditions.end());
}

bool AnimatorController::RenameParameter(int index, const std::string& newName)
{
	if (index < 0 || index >= (int)Parameters.size() || newName.empty())
		return false;
	const std::string old = Parameters[index].Name;
	if (old == newName)
		return false;
	const std::string name = MakeUniqueParameterName(newName);
	Parameters[index].Name = name;
	for (auto& l : Layers)
		for (auto& t : l.Transitions)
			for (auto& c : t.Conditions)
				if (c.Parameter == old)
					c.Parameter = name;
	return true;
}

int AnimatorController::AddLayer()
{
	AnimatorLayer l;
	l.Name = "New Layer";
	for (int n = 0;; ++n)
	{
		bool used = false;
		for (const auto& e : Layers)
			used |= e.Name == l.Name;
		if (!used)
			break;
		l.Name = "New Layer " + std::to_string(n);
	}
	l.Weight = 0.0f;   // Unity: 새 레이어의 Weight 는 0
	Layers.push_back(l);
	return (int)Layers.size() - 1;
}

void AnimatorController::RemoveLayer(int index)
{
	if (index > 0 && index < (int)Layers.size())
		Layers.erase(Layers.begin() + index);
}
