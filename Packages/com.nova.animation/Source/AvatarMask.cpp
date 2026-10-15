#include "pch.h"
#include "AvatarMask.h"
#include "SkinnedData.h"
#include "HumanoidAvatar.h"

namespace
{
	const char* kPartNames[AvatarMask::PartCount] = {
		"Root", "Body", "Head", "LeftLeg", "RightLeg", "LeftArm", "RightArm", "LeftFingers", "RightFingers",
		"LeftFootIK", "RightFootIK", "LeftHandIK", "RightHandIK" };

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

	long long Stamp(const std::wstring& file)
	{
		std::error_code ec;
		const auto t = std::filesystem::last_write_time(file, ec);
		return ec ? 0 : (long long)t.time_since_epoch().count();
	}

	std::map<std::string, std::shared_ptr<AvatarMask>>& Cache()
	{
		static std::map<std::string, std::shared_ptr<AvatarMask>> cache;
		return cache;
	}

	// 사람 본 → 몸 부위
	int PartOfHuman(int bone)
	{
		using namespace Humanoid;
		switch (bone)
		{
		// Hips 는 Root 쪽 (상체 마스크가 엉덩이를 돌려 다리까지 흔들지 않게 — 몸통 비틀기는 Spine 부터)
		case Hips: return AvatarMask::Root;
		case Spine: case Chest: case UpperChest: return AvatarMask::Body;
		case Neck: case Head: return AvatarMask::Head;
		case LeftShoulder: case LeftUpperArm: case LeftLowerArm: case LeftHand: return AvatarMask::LeftArm;
		case RightShoulder: case RightUpperArm: case RightLowerArm: case RightHand: return AvatarMask::RightArm;
		case LeftUpperLeg: case LeftLowerLeg: case LeftFoot: case LeftToes: return AvatarMask::LeftLeg;
		case RightUpperLeg: case RightLowerLeg: case RightFoot: case RightToes: return AvatarMask::RightLeg;
		default: return AvatarMask::Root;
		}
	}

	std::string LastName(const std::string& path)
	{
		const size_t s = path.find_last_of("/\\");
		return s == std::string::npos ? path : path.substr(s + 1);
	}
}

const char* AvatarMask::PartName(int part) { return part >= 0 && part < PartCount ? kPartNames[part] : "?"; }

AvatarMask::AvatarMask()
{
	for (bool& p : Parts)
		p = true;   // Unity: 새 마스크는 모두 켜짐
}

void AvatarMask::SetUpperBody(AvatarMask& mask)
{
	for (int i = 0; i < PartCount; ++i)
		mask.Parts[i] = false;
	for (int p : { Body, Head, LeftArm, RightArm, LeftFingers, RightFingers, LeftHandIK, RightHandIK })
		mask.Parts[p] = true;
}

std::string AvatarMask::ToJsonString() const
{
	json h = json::object();
	for (int i = 0; i < PartCount; ++i)
		h[kPartNames[i]] = Parts[i];
	json t = json::array();
	for (const auto& e : Transforms)
		t.push_back({ { "path", e.Path }, { "active", e.Active } });
	json j = { { "humanoid", h }, { "transforms", t } };
	return j.dump(4);
}

void AvatarMask::ApplyJson(const std::string& text)
{
	json j = json::parse(text, nullptr, false);
	if (j.is_discarded())
		return;
	if (j.contains("humanoid") && j["humanoid"].is_object())
		for (int i = 0; i < PartCount; ++i)
			Parts[i] = j["humanoid"].value(kPartNames[i], true);
	Transforms.clear();
	if (j.contains("transforms") && j["transforms"].is_array())
		for (const auto& e : j["transforms"])
			Transforms.push_back({ e.value("path", std::string()), e.value("active", true) });
	++Revision;
}

bool AvatarMask::Save() const
{
	const std::wstring file = FilePath(Path);
	std::error_code ec;
	std::filesystem::create_directories(std::filesystem::path(file).parent_path(), ec);
	std::ofstream os(file, std::ios::binary | std::ios::trunc);
	if (!os)
		return false;
	os << ToJsonString();
	const_cast<AvatarMask*>(this)->m_FileStamp = 0;   // 저장한 것은 다시 읽지 않는다 (아래 Load 가 스탬프를 맞춘다)
	return true;
}

int AvatarMask::PartOfNode(const SkeletonAvataData& skeleton, int node)
{
	const Humanoid::Avatar& avatar = Humanoid::Get(skeleton);
	// 사람 본 조상을 찾는다. 손 아래 사람 본이 아닌 노드 = 손가락 (무기 소켓도 손을 따른다)
	bool belowHand = false;
	for (int n = node; n >= 0 && n < (int)skeleton.BoneHierarchy.size(); n = skeleton.BoneHierarchy[(size_t)n])
	{
		const int human = n < (int)avatar.HumanOf.size() ? avatar.HumanOf[(size_t)n] : -1;
		if (human < 0)
		{
			belowHand = true;   // 사람 본이 아닌 노드를 지났다
			continue;
		}
		if (human == Humanoid::LeftHand && belowHand && n != node) return LeftFingers;
		if (human == Humanoid::RightHand && belowHand && n != node) return RightFingers;
		return PartOfHuman(human);
	}
	return Root;
}

const std::vector<float>& AvatarMask::NodeWeights(const SkeletonAvataData& skeleton) const
{
	std::lock_guard<std::mutex> lock(m_Lock);
	NodeCache& c = m_Cache[&skeleton];
	if (c.Revision == Revision && c.Weights.size() == skeleton.BoneHierarchy.size())
		return c.Weights;
	c.Revision = Revision;
	const size_t n = skeleton.BoneHierarchy.size();
	c.Weights.assign(n, 1.0f);
	const Humanoid::Avatar& avatar = Humanoid::Get(skeleton);
	const bool humanoid = avatar.Valid && !avatar.Generic;
	std::unordered_map<std::string, bool> byName;
	for (const auto& e : Transforms)
		byName[LastName(e.Path)] = e.Active;
	for (size_t i = 0; i < n; ++i)
	{
		bool on = true;
		if (humanoid)
			on = Parts[PartOfNode(skeleton, (int)i)];
		if (!byName.empty() && i < skeleton.NodeNames.size())
		{
			auto it = byName.find(skeleton.NodeNames[i]);
			if (it != byName.end())
				on = it->second;
		}
		c.Weights[i] = on ? 1.0f : 0.0f;
	}
	return c.Weights;
}

std::shared_ptr<AvatarMask> AvatarMask::Load(const std::string& rawPath)
{
	const std::string path = NormalizePath(rawPath);
	if (path.empty())
		return nullptr;
	const std::wstring file = FilePath(path);
	auto it = Cache().find(path);
	const unsigned long long now = ::GetTickCount64();
	if (it != Cache().end() && now - it->second->m_CheckedTick < 500)
		return it->second;   // Animator 가 프레임마다 부른다 — 파일 시각은 가끔만
	const long long stamp = Stamp(file);
	if (it != Cache().end())
		it->second->m_CheckedTick = now;
	if (it != Cache().end() && (stamp == 0 || it->second->m_FileStamp == 0 || it->second->m_FileStamp == stamp))
	{
		if (it->second->m_FileStamp == 0)
			it->second->m_FileStamp = stamp;
		return it->second;
	}
	std::ifstream in(file, std::ios::binary);
	if (!in)
		return nullptr;
	std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	std::shared_ptr<AvatarMask> m = it != Cache().end() ? it->second : std::make_shared<AvatarMask>();
	m->Path = path;
	m->ApplyJson(text);
	m->m_FileStamp = stamp;
	Cache()[path] = m;
	return m;
}

std::shared_ptr<AvatarMask> AvatarMask::Create(const std::string& rawPath)
{
	auto m = std::make_shared<AvatarMask>();
	m->Path = NormalizePath(rawPath);
	m->Save();
	Cache()[m->Path] = m;
	return m;
}
