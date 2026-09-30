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