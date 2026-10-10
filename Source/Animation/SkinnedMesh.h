#pragma once
#include "TextureMgr.h"
#include "Vertex.h"
#include "MeshGeometry.h"
#include "SkinnedData.h"
#include "Vertex.h"
#include "LightHelper.h"

class SkinnedMesh;

// ������� ���� - ����
struct SkinnedMeshlInstance
{
	shared_ptr<SkinnedMesh> Model;
	float TimePos;
	std::string ClipName;
	XMFLOAT4X4 World;
	std::vector<XMFLOAT4X4> FinalTransforms;

	void Update(float dt);
};

class SkinnedMesh
{
public: 
	SkinnedMesh();
	~SkinnedMesh();

public:
	void Setup();
	void from_byte(ifstream& inStream);
	void to_byte(ofstream& outStream);

	uint32					SubsetCount;
	std::vector<Material>	Mat;

	// Keep CPU copies of the mesh data to read from.  
	std::vector<Vertex::PosNormalTexTanSkinned> Vertices;
	std::vector<USHORT>							Indices;
	std::vector<MeshGeometry::Subset>			Subsets;

	MeshGeometry	ModelMesh;
	SkinnedData		SkinnedData;

	// 스킨 본 팔레트: 정점의 boneIndices 가 가리키는 본의 이름과 역바인드(Offset) 행렬 (행 벡터)
	// 최종 본 행렬 = BoneOffsets[k] * (본 노드의 전역 행렬)
	std::vector<std::string>	BoneNames;
	std::vector<XMFLOAT4X4>		BoneOffsets;

	// BlendShape (모프 타깃): 순서 = 파일 순서 (glTF targets · FBX 셰이프), 이름 = targetNames
	std::vector<BlendShapeData>	BlendShapes;
	int FindBlendShape(const std::string& name) const
	{
		for (int i = 0; i < (int)BlendShapes.size(); ++i) if (BlendShapes[i].Name == name) return i;
		return -1;
	}

	// 팔레트의 메시 바인드: 최종 본 행렬 = MeshBind * BoneOffsets[k] * 본 전역.
	//  역바인드가 장면 공간 (메시 노드 변환을 빼고 — PreRotation 이 있는 일부 FBX) 이면 메시 노드의 바인드 전역,
	//  메시 노드 공간 (Assimp 의 보통 FBX — 메시 노드 변환 포함, 예: Unreal 내보내기의 ×100 · 회전) 이면 단위 행렬.
	//  바인드 자세에서 어느 쪽이 정점을 메시 노드 자리로 옮기는지 계산해 고른다. chainBind = 메시 노드의 바인드 전역 (바운드용)
	//  mode 0 ~ 4 = 후보를 고정 (Unity 가져오기가 Unity 가 구운 자리와 맞춘 것), -1 = 자동
	static constexpr int kBindBase = 5;                    // 기본 후보 (자동 판정은 이것만)
	static constexpr int kBindCandidates = kBindBase * 4;  // × 바인드 자세 뒤 회전 (없음 · X 180° · X 90° · X -90°) — 가져오기가 정답과 맞출 때
	XMMATRIX PaletteMeshBind(const SkeletonAvataData& skeleton, XMMATRIX* chainBind = nullptr, int mode = -1) const;
	int BindCandidates(const SkeletonAvataData& skeleton, XMMATRIX out[kBindCandidates], XMMATRIX* chainBind, std::vector<XMFLOAT4X4>* global) const;
	// 후보마다 바인드 자세 스키닝 범위 (모델 공간 — 가져오기 대조용, nova modelfile info)
	void BindCandidateBounds(const SkeletonAvataData& skeleton, XMFLOAT3 mn[kBindCandidates], XMFLOAT3 mx[kBindCandidates]) const;
	// 이 메시 바인드로 바인드 자세 스키닝한 범위 (모델 공간, 단위 포함) — 렌더러 경계 상자 (컬링)
	bool BindBounds(const SkeletonAvataData& skeleton, CXMMATRIX meshBind, XMFLOAT3& mn, XMFLOAT3& mx) const;

	BouncingBall	Ball;
	wstring			Path;
	string			Name;
};

class MeshFile
{
public:
	MeshFile();
	~MeshFile();
	static MeshFile* LoadFromMetaFile(string path); 
	static MeshFile* LoadFromFbxFile(string path); 

	vector<shared_ptr<SkinnedMesh>>			SkinnedMeshs;
	vector<shared_ptr<Mesh>>				Meshs; 
	vector<shared_ptr<SkeletonAvataData>>	Avatas; 
	SkinnedData								SkinnedData; 

	wstring				Path;  
	string				FullPath;
	string				Name;

	// metadata setting
	bool				UseImportAnimation = true;
	float				ScaleFactor = 1.0f;

public:
	void OnInspectorGUI();

public:
	void ImportFile(); 

	void load_mesh(ifstream& inStream);
	void save_mesh(ofstream& outStream);

	void load_animations(ifstream& inStream); 
	void save_animations(ofstream& outStream);
	 
	void load_skeletone(ifstream& outStream);
	void save_skeletone(ofstream& inStream);
}; 