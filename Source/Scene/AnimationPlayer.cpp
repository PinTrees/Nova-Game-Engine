#include "pch.h"
#include "AnimationPlayer.h"
#include "UnityGUI.h"
#include "AnimationPose.h"
#include "SkinnedMeshRenderer.h"
#include "SkinnedMesh.h"
#include "PhysicsManager.h"

namespace
{
	// ---- 클립 선택 팝업용 목록 (엔진 패키지 + 프로젝트 Assets 의 FBX 안에 있는 클립) ----
	struct ClipEntry
	{
		std::string Path;
		int Index;
		std::string Name;
	};
	std::vector<ClipEntry> s_ClipList;
	bool s_ClipListReady = false;

	void ScanFolder(const std::wstring& root, const std::wstring& relativePrefix)
	{
		std::error_code ec;
		if (!std::filesystem::exists(root, ec))
			return;
		for (const auto& entry : std::filesystem::recursive_directory_iterator(root, ec))
		{
			if (!entry.is_regular_file())
				continue;
			std::wstring ext = entry.path().extension().wstring();
			std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
			if (ext != L".fbx")
				continue;
			const std::wstring rel = relativePrefix + std::filesystem::relative(entry.path(), root, ec).wstring();
			auto file = ResourceManager::GetI()->LoadMeshFile(wstring_to_string(rel));
			if (file == nullptr)
				continue;
			for (int i = 0; i < (int)file->SkinnedData.AnimationClips.size(); ++i)
				s_ClipList.push_back({ wstring_to_string(rel), i, file->SkinnedData.AnimationClips[i]->Name });
		}
	}

	void RefreshClipList()
	{
		s_ClipList.clear();
		ScanFolder(PathManager::GetI()->GetMovePathW(L"Resources\\Packages\\"), L"Resources\\Packages\\");
		ScanFolder(PathManager::GetI()->GetMovePathW(L"Assets\\"), L"Assets\\");
		s_ClipListReady = true;
	}

	void LoadRef(AnimationPlayer::ClipRef& ref)
	{
		ref.Clip = ref.Path.empty() ? nullptr : ResourceManager::GetI()->LoadAnimationClip(ref.Path, ref.Index);
	}

	std::string RefName(const AnimationPlayer::ClipRef& ref)
	{
		return ref.Clip ? ref.Clip->Name : "None (Animation Clip)";
	}
}

AnimationPlayer::AnimationPlayer()
{
	m_InspectorTitleName = "Animation";
}

AnimationPlayer::~AnimationPlayer()
{
}

// ------------------------------------------------------------------ Unity API
void AnimationPlayer::SetClip(const std::string& fbxPath, int clipIndex)
{
	m_DefaultClip.Path = fbxPath;
	m_DefaultClip.Index = clipIndex;
	LoadRef(m_DefaultClip);
	bool listed = false;
	for (const auto& c : m_Clips)
		if (c.Path == fbxPath && c.Index == clipIndex)
			listed = true;
	if (!listed && m_DefaultClip.Clip)
		m_Clips.push_back(m_DefaultClip);
}

bool AnimationPlayer::Play()
{
	if (m_DefaultClip.Clip == nullptr)
		return false;
	m_Playing = m_DefaultClip.Clip;
	m_TimePos = 0.0f;
	m_IsPlaying = true;
	Sample();
	return true;
}

bool AnimationPlayer::Play(const std::string& clipName)
{
	for (const auto& c : m_Clips)
		if (c.Clip && c.Clip->Name == clipName)
		{
			m_Playing = c.Clip;
			m_TimePos = 0.0f;
			m_IsPlaying = true;
			Sample();
			return true;
		}
	return false;
}

void AnimationPlayer::Stop()
{
	m_IsPlaying = false;
	m_TimePos = 0.0f;
}

// ------------------------------------------------------------------ 재생
void AnimationPlayer::Start()
{
	if (m_PlayAutomatically)
		Play();
}

void AnimationPlayer::Update()
{
	if (m_UpdateMode == UpdateMode::Fixed || m_AnimatePhysics)
		return;
	Advance(m_UpdateMode == UpdateMode::UnScale ? TimeManager::GetI()->GetfDT() : DT);
}

void AnimationPlayer::FixedUpdate()
{
	if (m_UpdateMode == UpdateMode::Fixed || m_AnimatePhysics)
		Advance(PhysicsManager::GetI()->GetFixedTimestep());
}

void AnimationPlayer::Advance(float dt)
{
	if (!m_IsPlaying || m_Playing == nullptr)
		return;
	m_TimePos += dt;
	const float duration = m_Playing->GetClipEndTime();
	if (duration > 0.0f)
		m_TimePos = fmodf(m_TimePos, duration);   // Wrap Mode: Loop
	Sample();
}

void AnimationPlayer::CollectRenderers(GameObject* go, std::vector<SkinnedMeshRenderer*>& out)
{
	if (go == nullptr)
		return;
	if (SkinnedMeshRenderer* r = go->GetComponent<SkinnedMeshRenderer>())
		out.push_back(r);
	for (GameObject* child : go->GetChildren())
		CollectRenderers(child, out);
}

void AnimationPlayer::Sample()
{
	std::vector<SkinnedMeshRenderer*> renderers;
	CollectRenderers(m_pGameObject, renderers);
	if (renderers.empty())
		return;

	// 렌더러마다 스켈레톤이 같으면 포즈를 한 번만 계산한다
	const SkeletonAvataData* computedFor = nullptr;
	for (SkinnedMeshRenderer* r : renderers)
	{
		shared_ptr<SkeletonAvataData> skeleton = r->GetSkeleton();
		if (skeleton == nullptr)
			continue;
		if (computedFor != skeleton.get())
		{
			if (m_Playing && (m_MappedClip != m_Playing.get() || m_MappedSkeleton != skeleton.get()))
			{
				m_ChannelToNode = AnimationPose::MapChannels(*m_Playing, *skeleton);
				m_MappedClip = m_Playing.get();
				m_MappedSkeleton = skeleton.get();
			}
			AnimationPose::SampleLocal(*skeleton, m_Playing.get(), m_ChannelToNode, m_TimePos, m_Local);
			AnimationPose::ComputeGlobals(*skeleton, m_Local, m_Global);
			computedFor = skeleton.get();
		}
		r->ApplyPose(m_Global);
	}
}

// ------------------------------------------------------------------ Inspector (Unity 6 Animation)
void AnimationPlayer::OnInspectorGUI()
{
	using namespace UnityGUI;
	static int s_PickTarget = -2;   // -1 = 기본 클립, 0.. = 목록 원소
	static AnimationPlayer* s_PickOwner = nullptr;

	std::string defaultName = RefName(m_DefaultClip);
	if (ObjectField("Animation", defaultName.c_str(), 0, m_DefaultClip.Clip ? "animation_clip" : nullptr))
	{
		s_PickTarget = -1;
		s_PickOwner = this;
		if (!s_ClipListReady) RefreshClipList();
		ImGui::OpenPopup("##ClipPicker");
	}

	int count = (int)m_Clips.size();
	bool open = MaterialsHeader("Animations", &count);
	if (count != (int)m_Clips.size())
		m_Clips.resize((std::max)(0, count));
	if (open)
	{
		if (m_Clips.empty())
			Label("List is Empty", 1);
		for (int i = 0; i < (int)m_Clips.size(); ++i)
		{
			std::string label = "Element " + std::to_string(i);
			std::string name = RefName(m_Clips[i]);
			ImGui::PushID(i);
			if (ElementRow(label.c_str(), name.c_str(), m_Clips[i].Clip ? "animation_clip" : nullptr))
			{
				s_PickTarget = i;
				s_PickOwner = this;
				if (!s_ClipListReady) RefreshClipList();
				ImGui::OpenPopup("##ClipPicker");
			}
			ImGui::PopID();
		}
		bool plus = false, minus = false;
		PlusMinus(&plus, &minus);
		if (plus) m_Clips.push_back({});
		if (minus && !m_Clips.empty()) m_Clips.pop_back();
	}

	Toggle("Play Automatically", &m_PlayAutomatically);
	Toggle("Animate Physics", &m_AnimatePhysics);
	static const char* kUpdate[] = { "Normal", "Fixed", "Unscaled Time" };
	int update = (int)m_UpdateMode;
	if (Dropdown("Update Mode", &update, kUpdate, 3))
		m_UpdateMode = (UpdateMode)update;
	static const char* kCulling[] = { "Always Animate", "Based On Renderers" };
	Dropdown("Culling Type", &m_CullingType, kCulling, 2);

	if (m_IsPlaying && m_Playing)
	{
		char buf[128];
		sprintf_s(buf, "%s  %.2f / %.2f s", m_Playing->Name.c_str(), m_TimePos, m_Playing->GetClipEndTime());
		ValueLabel("Playing", buf);
	}

	// ---- 클립 선택 팝업 ----
	ImGui::SetNextWindowSizeConstraints(ImVec2(320, 0), ImVec2(520, 420));
	if (ImGui::BeginPopup("##ClipPicker"))
	{
		if (ImGui::Selectable("None"))
		{
			if (s_PickTarget == -1) m_DefaultClip = {};
			else if (s_PickTarget >= 0 && s_PickTarget < (int)m_Clips.size()) m_Clips[s_PickTarget] = {};
		}
		for (const ClipEntry& e : s_ClipList)
		{
			std::string text = e.Name + "   (" + std::filesystem::path(e.Path).filename().string() + ")";
			if (ImGui::Selectable(text.c_str()) && s_PickOwner == this)
			{
				if (s_PickTarget == -1)
					SetClip(e.Path, e.Index);
				else if (s_PickTarget >= 0 && s_PickTarget < (int)m_Clips.size())
				{
					m_Clips[s_PickTarget].Path = e.Path;
					m_Clips[s_PickTarget].Index = e.Index;
					LoadRef(m_Clips[s_PickTarget]);
				}
			}
		}
		ImGui::Separator();
		if (ImGui::Selectable("Refresh"))
			RefreshClipList();
		ImGui::EndPopup();
	}
}

GENERATE_COMPONENT_FUNC_TOJSON(AnimationPlayer)
{
	json j;
	SERIALIZE_TYPE(j, AnimationPlayer);
	j["enabled"] = m_Enabled;
	j["clipPath"] = m_DefaultClip.Path;
	j["clipIndex"] = m_DefaultClip.Index;
	json list = json::array();
	for (const auto& c : m_Clips)
		list.push_back({ { "path", c.Path }, { "index", c.Index } });
	j["clips"] = list;
	j["playAutomatically"] = m_PlayAutomatically;
	j["animatePhysics"] = m_AnimatePhysics;
	j["updateMode"] = (int)m_UpdateMode;
	j["cullingType"] = m_CullingType;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(AnimationPlayer)
{
	m_Enabled = j.value("enabled", true);
	// 이전 형식: m_AnimationFilePath / m_AnimationClipIndex / m_PlayAutomatically
	m_DefaultClip.Path = j.value("clipPath", j.value("m_AnimationFilePath", std::string()));
	m_DefaultClip.Index = j.value("clipIndex", j.value("m_AnimationClipIndex", 0));
	LoadRef(m_DefaultClip);
	m_Clips.clear();
	if (j.contains("clips") && j["clips"].is_array())
		for (const auto& c : j["clips"])
		{
			ClipRef ref;
			ref.Path = c.value("path", std::string());
			ref.Index = c.value("index", 0);
			LoadRef(ref);
			m_Clips.push_back(ref);
		}
	else if (m_DefaultClip.Clip)
		m_Clips.push_back(m_DefaultClip);
	m_PlayAutomatically = j.value("playAutomatically", j.value("m_PlayAutomatically", true));
	m_AnimatePhysics = j.value("animatePhysics", false);
	m_UpdateMode = (UpdateMode)j.value("updateMode", 0);
	m_CullingType = j.value("cullingType", 0);
}
