#pragma once
#include "TextureMgr.h"
#include "Vertex.h"
#include "MeshGeometry.h"
#include "SkinnedData.h"
#include "Vertex.h"
#include "LightHelper.h"

class SkinnedMesh;

// 사용하지 않음 - 제거
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
	bool				UseImportAnimation;  
	float				ScaleFactor; 

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