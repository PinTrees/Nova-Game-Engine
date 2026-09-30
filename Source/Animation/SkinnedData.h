#pragma once

///<summary>
/// A Keyframe defines the bone transformation at an instant in time.
///</summary>
struct Keyframe
{
	Keyframe();
	~Keyframe();

public:
	float TimePos;				// 파싱됨
	XMFLOAT3 Translation;		// 파싱됨
	XMFLOAT3 Scale;				// 파싱됨
	XMFLOAT4 RotationQuat;		// 파싱됨
};




///<summary>
/// BoneAnimation은 키프레임 목록으로 정의됩니다. 두 개의 키프레임 사이의 시간 값에 대해서는, 
/// 그 시간을 포함하는 두 가장 가까운 키프레임을 이용해 보간합니다. 
///
/// 애니메이션은 항상 최소 두 개의 키프레임을 가집니다.
///</summary>
struct BoneAnimation
{
	float GetStartTime()const;
	float GetEndTime()const;

	void Interpolate(float t, XMFLOAT4X4& M)const;

public:
	std::vector<Keyframe> Keyframes;		// 파싱됨

public:
	// Editor
	void to_byte(std::ofstream& outStream) const;
	void from_byte(std::ifstream& inStream);
};




///<summary>
/// "Walk", "Run", "Attack", "Defend"와 같은 애니메이션 클립을 예로 들 수 있습니다.
/// AnimationClip은 애니메이션 클립을 구성하기 위해 모든 뼈에 대한 BoneAnimation을 필요로 합니다.
///</summary>
struct AnimationClip
{
	float GetClipStartTime()const;
	float GetClipEndTime()const;

	void Interpolate(float t, vector<XMFLOAT4X4>& boneTransforms)const;

public:
	vector<BoneAnimation>	BoneAnimations;			// 파싱됨
	string					Name;					// 파싱됨


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
	vector<shared_ptr<AnimationClip>>	AnimationClips;		// 파싱됨
	vector<int>							BoneHierarchy;		// 파싱됨
	vector<XMFLOAT4X4>					BoneOffsets;		// 파싱됨

private:
	// Gives parentIndex of ith bone.
	std::map<std::string, AnimationClip> _animations;

public:
	// Editor
	void from_byte(ifstream& inStream);
	void to_byte(ofstream& outStream);
};



class SkeletonAvataData
{
public:
	string								Name;
	vector<int>							BoneHierarchy;		
	vector<XMFLOAT4X4>					BoneOffsets;		

public:
	void from_byte(ifstream& inStream); 
	void to_byte(ofstream& outStream); 
};