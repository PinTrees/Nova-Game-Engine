#pragma once

///<summary>
/// A Keyframe defines the bone transformation at an instant in time.
///</summary>
struct Keyframe
{
	Keyframe();
	~Keyframe();

public:
	float TimePos;				// ÆÄ½ÌµÊ
	XMFLOAT3 Translation;		// ÆÄ½ÌµÊ
	XMFLOAT3 Scale;				// ÆÄ½ÌµÊ
	XMFLOAT4 RotationQuat;		// ÆÄ½ÌµÊ
};




///<summary>
/// BoneAnimationÀº Å°ÇÁ·¹ÀÓ ¸ñ·ÏÀ¸·Î Á¤ÀÇµË´Ï´Ù. µÎ °³ÀÇ Å°ÇÁ·¹ÀÓ »çÀÌÀÇ ½Ã°£ °ª¿¡ ´ëÇØ¼­´Â, 
/// ±× ½Ã°£À» Æ÷ÇÔÇÏ´Â µÎ °¡Àå °¡±î¿î Å°ÇÁ·¹ÀÓÀ» ÀÌ¿ëÇØ º¸°£ÇÕ´Ï´Ù. 
///
/// ¾Ö´Ï¸ŞÀÌ¼ÇÀº Ç×»ó ÃÖ¼Ò µÎ °³ÀÇ Å°ÇÁ·¹ÀÓÀ» °¡Áı´Ï´Ù.
///</summary>
struct BoneAnimation
{
	float GetStartTime()const;
	float GetEndTime()const;

	void Interpolate(float t, XMFLOAT4X4& M)const;

public:
	std::vector<Keyframe> Keyframes;		// ÆÄ½ÌµÊ

public:
	// Editor
	void to_byte(std::ofstream& outStream) const;
	void from_byte(std::ifstream& inStream);
};




///<summary>
/// "Walk", "Run", "Attack", "Defend"¿Í °°Àº ¾Ö´Ï¸ŞÀÌ¼Ç Å¬¸³À» ¿¹·Î µé ¼ö ÀÖ½À´Ï´Ù.
/// AnimationClipÀº ¾Ö´Ï¸ŞÀÌ¼Ç Å¬¸³À» ±¸¼ºÇÏ±â À§ÇØ ¸ğµç »À¿¡ ´ëÇÑ BoneAnimationÀ» ÇÊ¿ä·Î ÇÕ´Ï´Ù.
///</summary>
// ë…¸ë“œ í•˜ë‚˜ì˜ ì• ë‹ˆë©”ì´ì…˜ íŠ¸ë™ (ìœ„ì¹˜/íšŒì „/í¬ê¸° í‚¤ë¥¼ ë”°ë¡œ ê°€ì§„ë‹¤, ì‹œê°„ ë‹¨ìœ„: ì´ˆ)
struct VecKey { float Time; XMFLOAT3 Value; };
struct QuatKey { float Time; XMFLOAT4 Value; };

struct AnimationChannel
{
	std::string				NodeName;		// ëŒ€ìƒ ë…¸ë“œ(ë³¸) ì´ë¦„ - ì¬ìƒí•  ë•Œ ìŠ¤ì¼ˆë ˆí†¤ì˜ ê°™ì€ ì´ë¦„ ë…¸ë“œì— ì ìš©
	std::vector<VecKey>		Positions;
	std::vector<QuatKey>	Rotations;
	std::vector<VecKey>		Scales;

	// ì‹œê°„ t ì˜ ë¡œì»¬ í–‰ë ¬ (Scale * Rotation * Translation, í–‰ ë²¡í„°)
	void Sample(float t, XMFLOAT4X4& outLocal) const;

	void to_byte(std::ofstream& outStream) const;
	void from_byte(std::ifstream& inStream);
};

struct AnimationClip
{
	float GetClipStartTime()const;
	float GetClipEndTime()const;

	void Interpolate(float t, vector<XMFLOAT4X4>& boneTransforms)const;

public:
	vector<AnimationChannel> Channels;				// ë…¸ë“œ ì´ë¦„ë³„ íŠ¸ë™
	float					Duration = 0.0f;		// ê¸¸ì´ (ì´ˆ)
	vector<BoneAnimation>	BoneAnimations;			// (ì´ì „ í˜•ì‹, ì‚¬ìš© ì•ˆ í•¨) ÆÄ½ÌµÊ
	string					Name;					// ÆÄ½ÌµÊ


public:
	// Editor
	void to_byte(std::ofstream& outStream) const; 
	void from_byte(std::ifstream& inStream); 
};






class SkinnedData
{
public:
	SkinnedData(); 
	~SkinnedData(); 

	uint32 BoneCount() const;

	float GetClipStartTime(const std::string& clipName) const;
	float GetClipEndTime(const std::string& clipName) const;

	void Set(
		std::vector<int>& boneHierarchy,
		std::vector<XMFLOAT4X4>& boneOffsets,
		std::map<std::string, AnimationClip>& animations);

	// In a real project, you'd want to cache the result if there was a chance
	// that you were calling this several times with the same clipName at 
	// the same timePos.
	void GetFinalTransforms(const std::string& clipName, float timePos,
		std::vector<XMFLOAT4X4>& finalTransforms)const;

public:
	vector<shared_ptr<AnimationClip>>	AnimationClips;		// ÆÄ½ÌµÊ
	vector<int>							BoneHierarchy;		// ÆÄ½ÌµÊ
	vector<XMFLOAT4X4>					BoneOffsets;		// ÆÄ½ÌµÊ

private:
	// Gives parentIndex of ith bone.
	std::map<std::string, AnimationClip> _animations;

public:
	// Editor
	void from_byte(ifstream& inStream);
	void to_byte(ofstream& outStream);
};



// ëª¨ë¸ì˜ ë…¸ë“œ ê³„ì¸µ (ìŠ¤ì¼ˆë ˆí†¤). ì¸ë±ìŠ¤ ìˆœì„œëŠ” ë¶€ëª¨ê°€ í•­ìƒ ìì‹ë³´ë‹¤ ì•ì„ ë‹¤ (ê¹Šì´ ìš°ì„ ).
class SkeletonAvataData
{
public:
	string								Name;
	vector<int>							BoneHierarchy;		// ë¶€ëª¨ ë…¸ë“œ ì¸ë±ìŠ¤ (-1 = ë£¨íŠ¸)
	vector<XMFLOAT4X4>					BoneOffsets;		// (ì´ì „ í˜•ì‹, ì‚¬ìš© ì•ˆ í•¨)
	vector<string>						NodeNames;			// ë…¸ë“œ ì´ë¦„
	vector<XMFLOAT4X4>					BindLocal;			// ë°”ì¸ë“œ í¬ì¦ˆì˜ ë¡œì»¬ í–‰ë ¬ (í–‰ ë²¡í„°)
	float								UnitScale = 1.0f;	// íŒŒì¼ ë‹¨ìœ„ â†’ ë¯¸í„° (FBX cm ì´ë©´ 0.01, Unity ì˜ Convert Units)

	int FindNode(const string& name) const;

public:
	void from_byte(ifstream& inStream); 
	void to_byte(ofstream& outStream); 
};