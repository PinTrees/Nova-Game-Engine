#include "pch.h"
#include "SkinnedData.h"
#include "MathHelper.h"

Keyframe::Keyframe()
	: TimePos(0.0f),
	Translation(0.0f, 0.0f, 0.0f),
	Scale(1.0f, 1.0f, 1.0f),
	RotationQuat(0.0f, 0.0f, 0.0f, 1.0f)
{
}

Keyframe::~Keyframe()
{
}

float BoneAnimation::GetStartTime()const
{
	// Keyframes are sorted by time, so first keyframe gives start time.
	return Keyframes.front().TimePos;
}

float BoneAnimation::GetEndTime()const
{
	// Keyframes are sorted by time, so last keyframe gives end time.
	float f = Keyframes.back().TimePos;

	return f;
}

void BoneAnimation::Interpolate(float t, XMFLOAT4X4& M)const
{
	if (t <= Keyframes.front().TimePos)
	{
		XMVECTOR S = XMLoadFloat3(&Keyframes.front().Scale);
		XMVECTOR P = XMLoadFloat3(&Keyframes.front().Translation);
		XMVECTOR Q = XMLoadFloat4(&Keyframes.front().RotationQuat);

		XMVECTOR zero = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
		XMStoreFloat4x4(&M, XMMatrixAffineTransformation(S, zero, Q, P));
	}
	else if (t >= Keyframes.back().TimePos)
	{
		XMVECTOR S = XMLoadFloat3(&Keyframes.back().Scale);
		XMVECTOR P = XMLoadFloat3(&Keyframes.back().Translation);
		XMVECTOR Q = XMLoadFloat4(&Keyframes.back().RotationQuat);

		XMVECTOR zero = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
		XMStoreFloat4x4(&M, XMMatrixAffineTransformation(S, zero, Q, P));
	}
	else
	{
		for (UINT i = 0; i < Keyframes.size() - 1; ++i)
		{
			if (t >= Keyframes[i].TimePos && t <= Keyframes[i + 1].TimePos)
			{
				float lerpPercent = (t - Keyframes[i].TimePos) / (Keyframes[i + 1].TimePos - Keyframes[i].TimePos);

				XMVECTOR s0 = XMLoadFloat3(&Keyframes[i].Scale);
				XMVECTOR s1 = XMLoadFloat3(&Keyframes[i + 1].Scale);

				XMVECTOR p0 = XMLoadFloat3(&Keyframes[i].Translation);
				XMVECTOR p1 = XMLoadFloat3(&Keyframes[i + 1].Translation);

				XMVECTOR q0 = XMLoadFloat4(&Keyframes[i].RotationQuat);
				XMVECTOR q1 = XMLoadFloat4(&Keyframes[i + 1].RotationQuat);

				XMVECTOR S = XMVectorLerp(s0, s1, lerpPercent);
				XMVECTOR P = XMVectorLerp(p0, p1, lerpPercent);
				XMVECTOR Q = XMQuaternionSlerp(q0, q1, lerpPercent);

				XMVECTOR zero = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
				XMStoreFloat4x4(&M, XMMatrixAffineTransformation(S, zero, Q, P));

				break;
			}
		}
	}
}



void BoneAnimation::to_byte(std::ofstream& outStream) const
{
	// Keyframes Å©±â ¾²±â
	uint32_t keyframeCount = Keyframes.size();
	outStream.write(reinterpret_cast<const char*>(&keyframeCount), sizeof(keyframeCount));

	// °¢ Keyframe ¾²±â
	for (const auto& keyframe : Keyframes)
	{
		// KeyframeÀÇ °¢ ¸â¹ö º¯¼ö Á÷·ÄÈ­
		outStream.write(reinterpret_cast<const char*>(&keyframe.TimePos), sizeof(keyframe.TimePos));
		outStream.write(reinterpret_cast<const char*>(&keyframe.Translation), sizeof(keyframe.Translation));
		outStream.write(reinterpret_cast<const char*>(&keyframe.Scale), sizeof(keyframe.Scale));
		outStream.write(reinterpret_cast<const char*>(&keyframe.RotationQuat), sizeof(keyframe.RotationQuat));
	}
}

void BoneAnimation::from_byte(std::ifstream& inStream)
{
	// Keyframes Å©±â ÀÐ±â
	uint32_t keyframeCount;
	inStream.read(reinterpret_cast<char*>(&keyframeCount), sizeof(keyframeCount));
	Keyframes.resize(keyframeCount);

	// °¢ Keyframe ÀÐ±â
	for (auto& keyframe : Keyframes)
	{
		// KeyframeÀÇ °¢ ¸â¹ö º¯¼ö ¿ªÁ÷·ÄÈ­
		inStream.read(reinterpret_cast<char*>(&keyframe.TimePos), sizeof(keyframe.TimePos));
		inStream.read(reinterpret_cast<char*>(&keyframe.Translation), sizeof(keyframe.Translation));
		inStream.read(reinterpret_cast<char*>(&keyframe.Scale), sizeof(keyframe.Scale));
		inStream.read(reinterpret_cast<char*>(&keyframe.RotationQuat), sizeof(keyframe.RotationQuat));
	}
}



float AnimationClip::GetClipStartTime()const
{
	// Find smallest start time over all bones in this clip.
	float t = MathHelper::Infinity;
	for (UINT i = 0; i < BoneAnimations.size(); ++i)
	{
		t = MathHelper::Min(t, BoneAnimations[i].GetStartTime());
	}

	return t;
}

float AnimationClip::GetClipEndTime()const
{
	if (!Channels.empty())
		return Duration;
	// Find largest end time over all bones in this clip.
	float t = 0.0f;
	for (UINT i = 0; i < BoneAnimations.size(); ++i)
	{
		t = MathHelper::Max(t, BoneAnimations[i].GetEndTime());
	}

	return t;
}

void AnimationClip::Interpolate(float t, std::vector<XMFLOAT4X4>& boneTransforms)const
{
	for (UINT i = 0; i < BoneAnimations.size(); ++i)
	{
		BoneAnimations[i].Interpolate(t, boneTransforms[i]);
	}
}

namespace
{
	template <class K>
	size_t FindKey(const std::vector<K>& keys, float t)
	{
		// t ì´í•˜ì¸ ë§ˆì§€ë§‰ í‚¤ (keys ëŠ” ì‹œê°„ìˆœ)
		size_t lo = 0, hi = keys.size();
		while (hi - lo > 1)
		{
			size_t mid = (lo + hi) / 2;
			if (keys[mid].Time <= t) lo = mid; else hi = mid;
		}
		return lo;
	}
}

void AnimationChannel::Sample(float t, XMFLOAT4X4& outLocal) const
{
	if (!std::isfinite(t))
		t = 0.0f;
	// ë‘ í‚¤ ì‚¬ì´ ë³´ê°„ ë¹„ìœ¨ (NaN/ë¬´í•œëŒ€ ë°©ì§€)
	auto factor = [t](float a, float b) {
		const float f = (t - a) / (b - a);
		return std::isfinite(f) ? std::clamp(f, 0.0f, 1.0f) : 0.0f;
	};
	XMVECTOR S = XMVectorSet(1, 1, 1, 0), R = XMQuaternionIdentity(), T = XMVectorZero();
	if (!Scales.empty())
	{
		size_t i = FindKey(Scales, t);
		if (i + 1 < Scales.size() && Scales[i + 1].Time > Scales[i].Time)
		{
			float f = factor(Scales[i].Time, Scales[i + 1].Time);
			S = XMVectorLerp(XMLoadFloat3(&Scales[i].Value), XMLoadFloat3(&Scales[i + 1].Value), f);
		}
		else S = XMLoadFloat3(&Scales[i].Value);
	}
	if (!Rotations.empty())
	{
		size_t i = FindKey(Rotations, t);
		if (i + 1 < Rotations.size() && Rotations[i + 1].Time > Rotations[i].Time)
		{
			float f = factor(Rotations[i].Time, Rotations[i + 1].Time);
			R = XMQuaternionSlerp(XMLoadFloat4(&Rotations[i].Value), XMLoadFloat4(&Rotations[i + 1].Value), f);
		}
		else R = XMLoadFloat4(&Rotations[i].Value);
	}
	if (!Positions.empty())
	{
		size_t i = FindKey(Positions, t);
		if (i + 1 < Positions.size() && Positions[i + 1].Time > Positions[i].Time)
		{
			float f = factor(Positions[i].Time, Positions[i + 1].Time);
			T = XMVectorLerp(XMLoadFloat3(&Positions[i].Value), XMLoadFloat3(&Positions[i + 1].Value), f);
		}
		else T = XMLoadFloat3(&Positions[i].Value);
	}
	XMStoreFloat4x4(&outLocal, XMMatrixScalingFromVector(S) * XMMatrixRotationQuaternion(XMQuaternionNormalize(R)) * XMMatrixTranslationFromVector(T));
}

namespace
{
	void WriteString(std::ofstream& o, const std::string& s)
	{
		uint32_t n = (uint32_t)s.size();
		o.write(reinterpret_cast<const char*>(&n), sizeof(n));
		o.write(s.data(), n);
	}
	void ReadString(std::ifstream& in, std::string& s)
	{
		uint32_t n = 0;
		in.read(reinterpret_cast<char*>(&n), sizeof(n));
		s.resize(n);
		if (n) in.read(&s[0], n);
	}
	template <class T>
	void WriteVec(std::ofstream& o, const std::vector<T>& v)
	{
		uint32_t n = (uint32_t)v.size();
		o.write(reinterpret_cast<const char*>(&n), sizeof(n));
		if (n) o.write(reinterpret_cast<const char*>(v.data()), n * sizeof(T));
	}
	template <class T>
	void ReadVec(std::ifstream& in, std::vector<T>& v)
	{
		uint32_t n = 0;
		in.read(reinterpret_cast<char*>(&n), sizeof(n));
		v.resize(n);
		if (n) in.read(reinterpret_cast<char*>(v.data()), n * sizeof(T));
	}
}

void AnimationChannel::to_byte(std::ofstream& o) const
{
	WriteString(o, NodeName);
	WriteVec(o, Positions);
	WriteVec(o, Rotations);
	WriteVec(o, Scales);
}

void AnimationChannel::from_byte(std::ifstream& in)
{
	ReadString(in, NodeName);
	ReadVec(in, Positions);
	ReadVec(in, Rotations);
	ReadVec(in, Scales);
}

int SkeletonAvataData::FindNode(const string& name) const
{
	for (size_t i = 0; i < NodeNames.size(); ++i)
		if (NodeNames[i] == name)
			return (int)i;
	return -1;
}

void AnimationClip::to_byte(std::ofstream& outStream) const
{
	WriteString(outStream, Name);
	outStream.write(reinterpret_cast<const char*>(&Duration), sizeof(Duration));
	uint32_t n = (uint32_t)Channels.size();
	outStream.write(reinterpret_cast<const char*>(&n), sizeof(n));
	for (const auto& c : Channels)
		c.to_byte(outStream);
}

void AnimationClip::from_byte(std::ifstream& inStream)
{
	ReadString(inStream, Name);
	inStream.read(reinterpret_cast<char*>(&Duration), sizeof(Duration));
	uint32_t n = 0;
	inStream.read(reinterpret_cast<char*>(&n), sizeof(n));
	Channels.resize(n);
	for (auto& c : Channels)
		c.from_byte(inStream);
}






float SkinnedData::GetClipStartTime(const std::string& clipName)const
{
	auto clip = _animations.find(clipName);
	return clip->second.GetClipStartTime();
}

float SkinnedData::GetClipEndTime(const std::string& clipName)const
{
	auto clip = _animations.find(clipName);
	return clip->second.GetClipEndTime();
}

SkinnedData::SkinnedData()
{
}

SkinnedData::~SkinnedData()
{
}

uint32 SkinnedData::BoneCount()const
{
	return BoneHierarchy.size();
}

void SkinnedData::Set(std::vector<int>& boneHierarchy,
	std::vector<XMFLOAT4X4>& boneOffsets,
	std::map<std::string, AnimationClip>& animations)
{
	BoneHierarchy = boneHierarchy;
	BoneOffsets = boneOffsets;
	_animations = animations;
}

void SkinnedData::GetFinalTransforms(const std::string& clipName, float timePos, std::vector<XMFLOAT4X4>& finalTransforms)const
{
	uint32 numBones = BoneOffsets.size();

	std::vector<XMFLOAT4X4> toParentTransforms(numBones);

	// Interpolate all the bones of this clip at the given time instance.
	auto clip = _animations.find(clipName);
	clip->second.Interpolate(timePos, toParentTransforms);

	//
	// Traverse the hierarchy and transform all the bones to the root space.
	//

	std::vector<XMFLOAT4X4> toRootTransforms(numBones);

	// The root bone has index 0.  The root bone has no parent, so its toRootTransform
	// is just its local bone transform.
	toRootTransforms[0] = toParentTransforms[0];

	// Now find the toRootTransform of the children.
	for (uint32 i = 1; i < numBones; ++i)
	{
		XMMATRIX toParent = XMLoadFloat4x4(&toParentTransforms[i]);

		int parentIndex = BoneHierarchy[i];
		XMMATRIX parentToRoot = XMLoadFloat4x4(&toRootTransforms[parentIndex]);

		XMMATRIX toRoot = XMMatrixMultiply(toParent, parentToRoot);

		XMStoreFloat4x4(&toRootTransforms[i], toRoot);
	}

	// Premultiply by the bone offset transform to get the final transform.
	for (uint32 i = 0; i < numBones; ++i)
	{
		XMMATRIX offset = XMLoadFloat4x4(&BoneOffsets[i]);
		XMMATRIX toRoot = XMLoadFloat4x4(&toRootTransforms[i]);
		XMStoreFloat4x4(&finalTransforms[i], XMMatrixMultiply(offset, toRoot));
	}
}

void SkinnedData::from_byte(ifstream& inStream)
{
	// _boneHierarchy ÀÐ±â
	uint32_t boneCount;
	inStream.read(reinterpret_cast<char*>(&boneCount), sizeof(boneCount));  
	BoneHierarchy.resize(boneCount); 
	inStream.read(reinterpret_cast<char*>(BoneHierarchy.data()), boneCount * sizeof(int)); 

	// _boneOffsets ÀÐ±â
	uint32_t offsetCount;
	inStream.read(reinterpret_cast<char*>(&offsetCount), sizeof(offsetCount));
	BoneOffsets.resize(offsetCount);
	inStream.read(reinterpret_cast<char*>(BoneOffsets.data()), offsetCount * sizeof(XMFLOAT4X4));

	// _animations ÀÐ±â
	uint32_t animCount;
	inStream.read(reinterpret_cast<char*>(&animCount), sizeof(animCount));
	for (uint32_t i = 0; i < animCount; ++i)
	{
		uint32_t nameLength;
		inStream.read(reinterpret_cast<char*>(&nameLength), sizeof(nameLength));

		std::string clipName(nameLength, ' ');
		inStream.read(clipName.data(), nameLength);

		AnimationClip* animation = new AnimationClip;
		animation->from_byte(inStream);		
		shared_ptr<AnimationClip> animation_ptr(animation);

		AnimationClips.push_back(animation_ptr); 
	}
}

void SkinnedData::to_byte(ofstream& outStream)
{
	// _boneHierarchy ¾²±â
	uint32_t boneCount = BoneHierarchy.size();
	outStream.write(reinterpret_cast<const char*>(&boneCount), sizeof(boneCount));
	outStream.write(reinterpret_cast<const char*>(BoneHierarchy.data()), boneCount * sizeof(int));

	// _boneOffsets ¾²±â
	uint32_t offsetCount = BoneOffsets.size();
	outStream.write(reinterpret_cast<const char*>(&offsetCount), sizeof(offsetCount));
	outStream.write(reinterpret_cast<const char*>(BoneOffsets.data()), offsetCount * sizeof(XMFLOAT4X4));

	// _animations ¾²±â
	uint32_t animCount = AnimationClips.size();
	outStream.write(reinterpret_cast<const char*>(&animCount), sizeof(animCount));
	for (const auto& animation : AnimationClips) 
	{
		uint32_t nameLength = animation->Name.size();
		outStream.write(reinterpret_cast<const char*>(&nameLength), sizeof(nameLength));
		outStream.write(animation->Name.c_str(), nameLength);  

		// AnimationClip Å¬·¡½º¿¡ to_byte°¡ ÀÖ¾î¾ß ÇÔ
		animation->to_byte(outStream);
	}
}





void SkeletonAvataData::from_byte(ifstream& inStream)
{
	if (!inStream.is_open())
		return;
	ReadString(inStream, Name);
	ReadVec(inStream, BoneHierarchy);
	uint32_t n = 0;
	inStream.read(reinterpret_cast<char*>(&n), sizeof(n));
	NodeNames.resize(n);
	for (auto& name : NodeNames)
		ReadString(inStream, name);
	ReadVec(inStream, BindLocal);
	inStream.read(reinterpret_cast<char*>(&UnitScale), sizeof(UnitScale));
}

void SkeletonAvataData::to_byte(ofstream& outStream)
{
	if (!outStream.is_open())
		return;
	WriteString(outStream, Name);
	WriteVec(outStream, BoneHierarchy);
	uint32_t n = (uint32_t)NodeNames.size();
	outStream.write(reinterpret_cast<const char*>(&n), sizeof(n));
	for (const auto& name : NodeNames)
		WriteString(outStream, name);
	WriteVec(outStream, BindLocal);
	outStream.write(reinterpret_cast<const char*>(&UnitScale), sizeof(UnitScale));
}
