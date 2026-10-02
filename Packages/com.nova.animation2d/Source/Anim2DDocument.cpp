#include "pch.h"
#include "Anim2DDocument.h"
#include <filesystem>
#include <fstream>

namespace Anim2D
{
	using json = nlohmann::json;

	namespace
	{
		constexpr float kDeg = 3.14159265358979f / 180.0f;
		constexpr size_t kUndoLimit = 100;
		const char* CurveName(Curve c) { return c == Curve::Stepped ? "stepped" : (c == Curve::Smooth ? "smooth" : "linear"); }
		Curve CurveOf(const json& j)
		{
			const std::string s = j.is_string() ? j.get<std::string>() : std::string();
			return s == "stepped" ? Curve::Stepped : (s == "smooth" ? Curve::Smooth : Curve::Linear);
		}

		// 키 사이 값: 앞 키의 곡선 (계단 = 앞 값, 부드럽게 = smoothstep). 루프면 끝 → 처음
		void SampleKeys(const std::vector<Key>& keys, float t, float length, bool loop, int n, float* out)
		{
			if (keys.empty()) return;
			if (keys.size() == 1) { for (int i = 0; i < n; ++i) out[i] = keys[0].V[i]; return; }
			if (loop && length > 0.0f) { t = fmodf(t, length); if (t < 0.0f) t += length; }
			auto mix = [&](const Key& a, const Key& b, float span, float at) {
				float f = span > 1e-6f ? std::clamp(at / span, 0.0f, 1.0f) : 0.0f;
				if (a.Ease == Curve::Stepped) f = 0.0f;
				else if (a.Ease == Curve::Smooth) f = f * f * (3.0f - 2.0f * f);
				for (int i = 0; i < n; ++i) out[i] = a.V[i] + (b.V[i] - a.V[i]) * f;
			};
			if (t < keys.front().Time)
			{
				if (!loop) { for (int i = 0; i < n; ++i) out[i] = keys.front().V[i]; return; }
				mix(keys.back(), keys.front(), keys.front().Time + length - keys.back().Time, t + length - keys.back().Time);
				return;
			}
			for (size_t k = 0; k + 1 < keys.size(); ++k)
				if (t < keys[k + 1].Time) { mix(keys[k], keys[k + 1], keys[k + 1].Time - keys[k].Time, t - keys[k].Time); return; }
			if (!loop) { for (int i = 0; i < n; ++i) out[i] = keys.back().V[i]; return; }
			mix(keys.back(), keys.front(), length - keys.back().Time + keys.front().Time, t - keys.back().Time);
		}

		void PutKey(std::vector<Key>& keys, const Key& k)
		{
			keys.erase(std::remove_if(keys.begin(), keys.end(), [&](const Key& x) { return fabsf(x.Time - k.Time) < 1e-4f; }), keys.end());
			keys.push_back(k);
			std::sort(keys.begin(), keys.end(), [](const Key& a, const Key& b) { return a.Time < b.Time; });
		}

		json KeysJson(const std::vector<Key>& keys, int n)
		{
			json a = json::array();
			for (const Key& k : keys)
			{
				json e = json::array({ k.Time });
				for (int i = 0; i < n; ++i) e.push_back(k.V[i]);
				if (k.Ease != Curve::Linear) e.push_back(CurveName(k.Ease));
				a.push_back(e);
			}
			return a;
		}

		std::vector<Key> KeysFrom(const json& a, int n)
		{
			std::vector<Key> keys;
			if (!a.is_array()) return keys;
			for (const json& e : a)
			{
				if (!e.is_array() || (int)e.size() < n + 1) continue;
				Key k;
				k.Time = e[0].get<float>();
				for (int i = 0; i < n; ++i) k.V[i] = e[i + 1].get<float>();
				if ((int)e.size() > n + 1) k.Ease = CurveOf(e[n + 1]);
				keys.push_back(k);
			}
			std::sort(keys.begin(), keys.end(), [](const Key& x, const Key& y) { return x.Time < y.Time; });
			return keys;
		}
	}

	Document& Doc()
	{
		static Document s_Doc;
		return s_Doc;
	}

	std::string FullPath(const std::string& path)
	{
		std::filesystem::path p = std::filesystem::path(std::u8string(path.begin(), path.end()));
		if (!p.is_absolute()) p = PathManager::GetI()->GetMovePathW(p.wstring());
		const std::u8string u = p.u8string();
		return std::string(u.begin(), u.end());
	}

	int Animation::KeyCount() const
	{
		int n = 0;
		for (const auto& [b, t] : Bones) n += (int)(t.Rotate.size() + t.Translate.size() + t.Scale.size());
		for (const auto& [s, t] : Slots) n += (int)(t.Attach.size() + t.Color.size());
		return n;
	}

	// ------------------------------------------------------------------ 기본
	void Document::New()
	{
		Bones.clear();
		Slots.clear();
		Animations.clear();
		ActiveAnim = -1;
		Time = 0.0f;
		AnimateMode = false;
		SelectedBone = SelectedSlot = -1;
		Path.clear();
		Name = "skeleton";
		m_Undo.clear();
		m_Redo.clear();
		Bone root;
		root.Name = "root";
		Bones.push_back(root);
		ResetPose();
		Dirty = false;
		++Revision;
	}

	int Document::FindBone(const std::string& n) const { for (int i = 0; i < (int)Bones.size(); ++i) if (Bones[i].Name == n) return i; return -1; }
	int Document::FindSlot(const std::string& n) const { for (int i = 0; i < (int)Slots.size(); ++i) if (Slots[i].Name == n) return i; return -1; }
	int Document::FindAnim(const std::string& n) const { for (int i = 0; i < (int)Animations.size(); ++i) if (Animations[i].Name == n) return i; return -1; }

	std::string Document::UniqueBoneName(const std::string& base) const
	{
		if (FindBone(base) < 0) return base;
		for (int k = 2;; ++k) { const std::string n = base + std::to_string(k); if (FindBone(n) < 0) return n; }
	}

	std::string Document::UniqueSlotName(const std::string& base) const
	{
		if (FindSlot(base) < 0) return base;
		for (int k = 2;; ++k) { const std::string n = base + std::to_string(k); if (FindSlot(n) < 0) return n; }
	}

	void Document::SortBones()
	{
		std::vector<int> order;
		std::vector<uint8_t> done(Bones.size(), 0);
		std::function<void(int)> visit = [&](int i) {
			if (done[i]) return;
			done[i] = 1;
			if (Bones[i].Parent >= 0 && Bones[i].Parent < (int)Bones.size()) visit(Bones[i].Parent);
			order.push_back(i);
		};
		for (int i = 0; i < (int)Bones.size(); ++i) visit(i);
		std::vector<int> idx(Bones.size());
		for (int k = 0; k < (int)order.size(); ++k) idx[order[k]] = k;
		std::vector<Bone> sorted;
		for (int i : order) { Bone b = Bones[i]; if (b.Parent >= 0) b.Parent = idx[b.Parent]; sorted.push_back(b); }
		Bones = sorted;
		for (Slot& s : Slots) s.Bone = s.Bone >= 0 && s.Bone < (int)idx.size() ? idx[s.Bone] : 0;
		if (SelectedBone >= 0 && SelectedBone < (int)idx.size()) SelectedBone = idx[SelectedBone];
	}

	void Document::DeleteBone(int b)
	{
		if (b < 0 || b >= (int)Bones.size()) return;
		const int parent = Bones[b].Parent;
		const std::string name = Bones[b].Name;
		for (Bone& c : Bones) if (c.Parent == b) c.Parent = parent;
		for (Slot& s : Slots) if (s.Bone == b) s.Bone = (std::max)(0, parent);
		Bones.erase(Bones.begin() + b);
		for (Bone& c : Bones) if (c.Parent > b) --c.Parent;
		for (Slot& s : Slots) if (s.Bone > b) --s.Bone;
		for (Animation& a : Animations) a.Bones.erase(name);
		SelectedBone = -1;
	}

	// ------------------------------------------------------------------ 자세
	void Document::ResetPose()
	{
		for (Bone& b : Bones) { b.PX = b.X; b.PY = b.Y; b.PR = b.Rotation; b.PSX = b.ScaleX; b.PSY = b.ScaleY; }
		for (Slot& s : Slots) { s.Current = s.SetupAttachment; for (int i = 0; i < 4; ++i) s.PColor[i] = s.Color[i]; }
		UpdateWorld();
	}

	void Document::ApplyAnimation(const Animation& anim, float t)
	{
		ResetPose();
		for (Bone& b : Bones)
		{
			auto it = anim.Bones.find(b.Name);
			if (it == anim.Bones.end()) continue;
			const BoneTimeline& tl = it->second;
			float v[4] = {};
			if (!tl.Rotate.empty()) { SampleKeys(tl.Rotate, t, anim.Length, anim.Loop, 1, v); b.PR = b.Rotation + v[0]; }
			if (!tl.Translate.empty()) { SampleKeys(tl.Translate, t, anim.Length, anim.Loop, 2, v); b.PX = b.X + v[0]; b.PY = b.Y + v[1]; }
			if (!tl.Scale.empty()) { SampleKeys(tl.Scale, t, anim.Length, anim.Loop, 2, v); b.PSX = b.ScaleX * v[0]; b.PSY = b.ScaleY * v[1]; }
		}
		for (Slot& s : Slots)
		{
			auto it = anim.Slots.find(s.Name);
			if (it == anim.Slots.end()) continue;
			const SlotTimeline& tl = it->second;
			if (!tl.Attach.empty())
			{
				float tt = t;
				if (anim.Loop && anim.Length > 0.0f) { tt = fmodf(tt, anim.Length); if (tt < 0.0f) tt += anim.Length; }
				// 그 시각 전의 마지막 키 (처음 키 전이면 루프 = 마지막 키, 아니면 셋업)
				const std::string* cur = anim.Loop ? &tl.Attach.back().second : nullptr;
				for (const auto& [kt, name] : tl.Attach) if (kt <= tt + 1e-5f) cur = &name;
				if (cur) s.Current = *cur;
			}
			if (!tl.Color.empty()) SampleKeys(tl.Color, t, anim.Length, anim.Loop, 4, s.PColor);
		}
		UpdateWorld();
	}

	void Document::UpdateWorld()
	{
		for (Bone& b : Bones)
		{
			const float r = b.PR * kDeg;
			const float la = cosf(r) * b.PSX, lb = -sinf(r) * b.PSY, lc = sinf(r) * b.PSX, ld = cosf(r) * b.PSY;
			if (b.Parent < 0 || b.Parent >= (int)Bones.size())
			{
				b.A = la; b.B = lb; b.C = lc; b.D = ld; b.WX = b.PX; b.WY = b.PY;
				continue;
			}
			const Bone& p = Bones[b.Parent];
			b.WX = p.A * b.PX + p.B * b.PY + p.WX;
			b.WY = p.C * b.PX + p.D * b.PY + p.WY;
			b.A = p.A * la + p.B * lc; b.B = p.A * lb + p.B * ld;
			b.C = p.C * la + p.D * lc; b.D = p.C * lb + p.D * ld;
		}
	}

	void Document::Pose()
	{
		Animation* a = Active();
		if (AnimateMode && a)
		{
			if (Posed) UpdateWorld();
			else ApplyAnimation(*a, Time);
		}
		else
		{
			Posed = false;
			ResetPose();
		}
	}

	Document::PoseState Document::SavePose() const
	{
		PoseState s;
		for (const Bone& b : Bones) s.B.push_back({ b.PX, b.PY, b.PR, b.PSX, b.PSY });
		for (const Slot& sl : Slots) { s.Cur.push_back(sl.Current); s.Col.push_back({ sl.PColor[0], sl.PColor[1], sl.PColor[2], sl.PColor[3] }); }
		s.Posed = Posed;
		s.Animate = AnimateMode;
		s.Time = Time;
		return s;
	}

	void Document::RestorePose(const PoseState& s)
	{
		AnimateMode = s.Animate;
		Time = s.Time;
		Posed = s.Posed;
		if (s.B.size() == Bones.size() && s.Cur.size() == Slots.size())
		{
			for (size_t i = 0; i < Bones.size(); ++i) { Bone& b = Bones[i]; b.PX = s.B[i][0]; b.PY = s.B[i][1]; b.PR = s.B[i][2]; b.PSX = s.B[i][3]; b.PSY = s.B[i][4]; }
			for (size_t i = 0; i < Slots.size(); ++i) { Slots[i].Current = s.Cur[i]; for (int k = 0; k < 4; ++k) Slots[i].PColor[k] = s.Col[i][k]; }
			UpdateWorld();
		}
		else
			Repose();
	}

	void Document::WorldToParent(int bone, float wx, float wy, float& lx, float& ly) const
	{
		const int p = Bones[bone].Parent;
		if (p < 0) { lx = wx; ly = wy; return; }
		const Bone& pb = Bones[p];
		const float det = pb.A * pb.D - pb.B * pb.C;
		const float x = wx - pb.WX, y = wy - pb.WY;
		if (fabsf(det) < 1e-9f) { lx = x; ly = y; return; }
		lx = (pb.D * x - pb.B * y) / det;
		ly = (pb.A * y - pb.C * x) / det;
	}

	float Document::WorldRotation(int bone) const { return atan2f(Bones[bone].C, Bones[bone].A) / kDeg; }

	int Document::KeyPose(Animation& anim, float t, const std::vector<std::string>& only, Curve ease, bool slots)
	{
		int n = 0;
		for (const Bone& b : Bones)
		{
			if (!only.empty() && std::find(only.begin(), only.end(), b.Name) == only.end()) continue;
			BoneTimeline& tl = anim.Bones[b.Name];
			Key k;
			k.Time = t;
			k.Ease = ease;
			k.V[0] = b.PR - b.Rotation;
			PutKey(tl.Rotate, k);
			k.V[0] = b.PX - b.X; k.V[1] = b.PY - b.Y;
			PutKey(tl.Translate, k);
			k.V[0] = fabsf(b.ScaleX) > 1e-6f ? b.PSX / b.ScaleX : 1.0f;
			k.V[1] = fabsf(b.ScaleY) > 1e-6f ? b.PSY / b.ScaleY : 1.0f;
			PutKey(tl.Scale, k);
			n += 3;
		}
		if (slots)
			for (const Slot& s : Slots)
			{
				if (!only.empty() && std::find(only.begin(), only.end(), s.Name) == only.end() && std::find(only.begin(), only.end(), Bones[s.Bone].Name) == only.end()) continue;
				auto it = anim.Slots.find(s.Name);
				const bool attachChanged = s.Current != s.SetupAttachment || (it != anim.Slots.end() && !it->second.Attach.empty());
				bool colorChanged = it != anim.Slots.end() && !it->second.Color.empty();
				for (int i = 0; i < 4; ++i) colorChanged = colorChanged || fabsf(s.PColor[i] - s.Color[i]) > 1e-4f;
				if (!attachChanged && !colorChanged) continue;
				SlotTimeline& tl = anim.Slots[s.Name];
				if (attachChanged)
				{
					tl.Attach.erase(std::remove_if(tl.Attach.begin(), tl.Attach.end(), [&](const auto& x) { return fabsf(x.first - t) < 1e-4f; }), tl.Attach.end());
					tl.Attach.push_back({ t, s.Current });
					std::sort(tl.Attach.begin(), tl.Attach.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
					++n;
				}
				if (colorChanged)
				{
					Key k;
					k.Time = t;
					k.Ease = ease;
					for (int i = 0; i < 4; ++i) k.V[i] = s.PColor[i];
					PutKey(tl.Color, k);
					++n;
				}
			}
		return n;
	}

	// ------------------------------------------------------------------ 저장
	json Document::ToJson() const
	{
		json bones = json::array();
		for (const Bone& b : Bones)
		{
			json j = { { "name", b.Name } };
			if (b.Parent >= 0) j["parent"] = Bones[b.Parent].Name;
			if (b.X != 0) j["x"] = b.X;
			if (b.Y != 0) j["y"] = b.Y;
			if (b.Rotation != 0) j["rotation"] = b.Rotation;
			if (b.ScaleX != 1) j["scaleX"] = b.ScaleX;
			if (b.ScaleY != 1) j["scaleY"] = b.ScaleY;
			if (b.Length != 0) j["length"] = b.Length;
			bones.push_back(j);
		}
		json slots = json::array();
		for (const Slot& s : Slots)
		{
			json atts = json::array();
			for (const Attachment& a : s.Attachments)
			{
				json aj = { { "name", a.Name }, { "image", a.Image } };
				if (a.X != 0) aj["x"] = a.X;
				if (a.Y != 0) aj["y"] = a.Y;
				if (a.Rotation != 0) aj["rotation"] = a.Rotation;
				if (a.ScaleX != 1) aj["scaleX"] = a.ScaleX;
				if (a.ScaleY != 1) aj["scaleY"] = a.ScaleY;
				if (a.Width != 0) aj["width"] = a.Width;
				if (a.Height != 0) aj["height"] = a.Height;
				atts.push_back(aj);
			}
			json sj = { { "name", s.Name }, { "bone", Bones[s.Bone].Name }, { "attachment", s.SetupAttachment }, { "attachments", atts } };
			if (s.Color[0] != 1 || s.Color[1] != 1 || s.Color[2] != 1 || s.Color[3] != 1) sj["color"] = { s.Color[0], s.Color[1], s.Color[2], s.Color[3] };
			slots.push_back(sj);
		}
		json anims = json::array();
		for (const Animation& a : Animations)
		{
			json bj = json::object(), sj = json::object();
			for (const auto& [name, tl] : a.Bones)
			{
				json t = json::object();
				if (!tl.Rotate.empty()) t["rotate"] = KeysJson(tl.Rotate, 1);
				if (!tl.Translate.empty()) t["translate"] = KeysJson(tl.Translate, 2);
				if (!tl.Scale.empty()) t["scale"] = KeysJson(tl.Scale, 2);
				bj[name] = t;
			}
			for (const auto& [name, tl] : a.Slots)
			{
				json t = json::object();
				if (!tl.Attach.empty()) { json at = json::array(); for (const auto& [kt, n] : tl.Attach) at.push_back({ kt, n }); t["attachment"] = at; }
				if (!tl.Color.empty()) t["color"] = KeysJson(tl.Color, 4);
				sj[name] = t;
			}
			anims.push_back({ { "name", a.Name }, { "length", a.Length }, { "loop", a.Loop }, { "bones", bj }, { "slots", sj } });
		}
		return { { "format", "nova-skel2d" }, { "version", 1 }, { "name", Name }, { "bones", bones }, { "slots", slots }, { "animations", anims } };
	}

	bool Document::FromJson(const json& j, std::string& error)
	{
		if (!j.is_object() || !j.contains("bones")) { error = "not a 2D skeleton (.skel2d)"; return false; }
		Bones.clear(); Slots.clear(); Animations.clear();
		Name = j.value("name", std::string("skeleton"));
		std::vector<std::string> parents;
		for (const json& bj : j["bones"])
		{
			Bone b;
			b.Name = bj.value("name", std::string("bone"));
			b.X = bj.value("x", 0.0f); b.Y = bj.value("y", 0.0f); b.Rotation = bj.value("rotation", 0.0f);
			b.ScaleX = bj.value("scaleX", 1.0f); b.ScaleY = bj.value("scaleY", 1.0f); b.Length = bj.value("length", 0.0f);
			parents.push_back(bj.value("parent", std::string()));
			Bones.push_back(b);
		}
		for (size_t i = 0; i < Bones.size(); ++i) Bones[i].Parent = parents[i].empty() ? -1 : FindBone(parents[i]);
		SortBones();
		for (const json& sj : j.value("slots", json::array()))
		{
			Slot s;
			s.Name = sj.value("name", std::string("slot"));
			s.Bone = (std::max)(0, FindBone(sj.value("bone", std::string())));
			s.SetupAttachment = sj.value("attachment", std::string());
			if (sj.contains("color") && sj["color"].size() == 4) for (int i = 0; i < 4; ++i) s.Color[i] = sj["color"][i].get<float>();
			for (const json& aj : sj.value("attachments", json::array()))
			{
				Attachment a;
				a.Name = aj.value("name", std::string("image"));
				a.Image = aj.value("image", std::string());
				a.X = aj.value("x", 0.0f); a.Y = aj.value("y", 0.0f); a.Rotation = aj.value("rotation", 0.0f);
				a.ScaleX = aj.value("scaleX", 1.0f); a.ScaleY = aj.value("scaleY", 1.0f);
				a.Width = aj.value("width", 0.0f); a.Height = aj.value("height", 0.0f);
				s.Attachments.push_back(a);
			}
			Slots.push_back(s);
		}
		for (const json& aj : j.value("animations", json::array()))
		{
			Animation a;
			a.Name = aj.value("name", std::string("animation"));
			a.Length = aj.value("length", 1.0f);
			a.Loop = aj.value("loop", true);
			if (aj.contains("bones"))
				for (auto it = aj["bones"].begin(); it != aj["bones"].end(); ++it)
				{
					BoneTimeline tl;
					tl.Rotate = KeysFrom(it.value().value("rotate", json()), 1);
					tl.Translate = KeysFrom(it.value().value("translate", json()), 2);
					tl.Scale = KeysFrom(it.value().value("scale", json()), 2);
					a.Bones[it.key()] = tl;
				}
			if (aj.contains("slots"))
				for (auto it = aj["slots"].begin(); it != aj["slots"].end(); ++it)
				{
					SlotTimeline tl;
					for (const json& k : it.value().value("attachment", json::array()))
						if (k.is_array() && k.size() == 2) tl.Attach.push_back({ k[0].get<float>(), k[1].is_string() ? k[1].get<std::string>() : std::string() });
					tl.Color = KeysFrom(it.value().value("color", json()), 4);
					a.Slots[it.key()] = tl;
				}
			Animations.push_back(a);
		}
		ActiveAnim = Animations.empty() ? -1 : 0;
		Time = 0.0f;
		SelectedBone = SelectedSlot = -1;
		Pose();
		++Revision;
		return true;
	}

	bool Document::Save(const std::string& path, std::string& error)
	{
		const std::filesystem::path p = std::filesystem::path(std::u8string(path.begin(), path.end()));
		std::error_code ec;
		if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
		std::ofstream f(p, std::ios::binary);
		if (!f) { error = "cannot write " + path; return false; }
		f << ToJson().dump(2);
		Path = path;
		Dirty = false;
		return true;
	}

	bool Document::Load(const std::string& path, std::string& error)
	{
		std::ifstream f(std::filesystem::path(std::u8string(path.begin(), path.end())), std::ios::binary);
		if (!f) { error = "cannot read " + path; return false; }
		const json j = json::parse(f, nullptr, false);
		if (j.is_discarded()) { error = "bad JSON in " + path; return false; }
		if (!FromJson(j, error)) return false;
		Path = path;
		Dirty = false;
		m_Undo.clear();
		m_Redo.clear();
		return true;
	}

	// ------------------------------------------------------------------ Undo
	std::string Document::Serialize() const
	{
		json j = ToJson();
		j["_anim"] = ActiveAnim;
		j["_time"] = Time;
		j["_animate"] = AnimateMode;
		j["_bone"] = SelectedBone;
		j["_slot"] = SelectedSlot;
		j["_posed"] = Posed;
		// 지금 자세 (Animate 모드에서 키 전 고친 것)
		json pose = json::array();
		for (const Bone& b : Bones) pose.push_back({ b.PX, b.PY, b.PR, b.PSX, b.PSY });
		j["_pose"] = pose;
		json cur = json::array();
		for (const Slot& s : Slots) cur.push_back({ s.Current, s.PColor[0], s.PColor[1], s.PColor[2], s.PColor[3] });
		j["_cur"] = cur;
		return j.dump();
	}

	void Document::Deserialize(const std::string& s)
	{
		const json j = json::parse(s, nullptr, false);
		std::string err;
		if (j.is_discarded() || !FromJson(j, err)) return;
		ActiveAnim = j.value("_anim", -1);
		Time = j.value("_time", 0.0f);
		AnimateMode = j.value("_animate", false);
		SelectedBone = j.value("_bone", -1);
		SelectedSlot = j.value("_slot", -1);
		Posed = false;
		Pose();
		Posed = j.value("_posed", false);
		if (j.contains("_pose") && j["_pose"].size() == Bones.size())
			for (size_t i = 0; i < Bones.size(); ++i)
			{
				const json& p = j["_pose"][i];
				Bones[i].PX = p[0]; Bones[i].PY = p[1]; Bones[i].PR = p[2]; Bones[i].PSX = p[3]; Bones[i].PSY = p[4];
			}
		if (j.contains("_cur") && j["_cur"].size() == Slots.size())
			for (size_t i = 0; i < Slots.size(); ++i)
			{
				const json& c = j["_cur"][i];
				Slots[i].Current = c[0].get<std::string>();
				for (int k = 0; k < 4; ++k) Slots[i].PColor[k] = c[k + 1];
			}
		UpdateWorld();
		Changed();
	}

	void Document::PushUndo(const std::string& label)
	{
		m_Undo.push_back({ label, Serialize() });
		m_Redo.clear();
		if (m_Undo.size() > kUndoLimit) m_Undo.erase(m_Undo.begin());
	}

	bool Document::Undo()
	{
		if (m_Undo.empty()) return false;
		Snap s = m_Undo.back();
		m_Undo.pop_back();
		m_Redo.push_back({ s.Label, Serialize() });
		Deserialize(s.Data);
		return true;
	}

	bool Document::Redo()
	{
		if (m_Redo.empty()) return false;
		Snap s = m_Redo.back();
		m_Redo.pop_back();
		m_Undo.push_back({ s.Label, Serialize() });
		Deserialize(s.Data);
		return true;
	}

	void Document::CancelUndo()
	{
		if (m_Undo.empty()) return;
		Deserialize(m_Undo.back().Data);
		m_Undo.pop_back();
	}

	json Document::Summary()
	{
		json anims = json::array();
		for (const Animation& a : Animations) anims.push_back({ { "name", a.Name }, { "length", a.Length }, { "loop", a.Loop }, { "keys", a.KeyCount() } });
		json r = { { "name", Name }, { "bones", (int)Bones.size() }, { "slots", (int)Slots.size() }, { "animations", anims },
			{ "mode", AnimateMode ? "animate" : "setup" }, { "time", Time }, { "dirty", Dirty }, { "undo", m_Undo.empty() ? "" : m_Undo.back().Label } };
		if (Active()) r["animation"] = Active()->Name;
		if (Posed) r["unkeyedPose"] = true;   // Animate 모드에서 고친 자세가 아직 키 안 됨 (anim2d anim.key)
		if (SelectedBone >= 0 && SelectedBone < (int)Bones.size()) r["selectedBone"] = Bones[SelectedBone].Name;
		if (SelectedSlot >= 0 && SelectedSlot < (int)Slots.size()) r["selectedSlot"] = Slots[SelectedSlot].Name;
		if (!Path.empty()) r["path"] = Path;
		return r;
	}
}
