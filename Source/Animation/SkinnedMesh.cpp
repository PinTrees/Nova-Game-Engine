#include "pch.h"
#include "SkinnedMesh.h"
#include "FBXLoader.h"
#include "AssetImportSettings.h"
#include "SkinnedData.h"
#include "LoadM3d.h"
#include "MathHelper.h"
#include "File.h"
#include "MeshUtility.h"
#include "EditorGUI.h"

SkinnedMesh::SkinnedMesh()
{
}

SkinnedMesh::~SkinnedMesh()
{
}

void SkinnedMesh::Setup()
{
	Vec3 minVertex = { +MathHelper::Infinity, +MathHelper::Infinity, +MathHelper::Infinity };
	Vec3 maxVertex = { -MathHelper::Infinity, -MathHelper::Infinity, -MathHelper::Infinity };
	for (int i = 0; i < Vertices.size(); i++)
	{
		if (Vertices[i].pos.x < minVertex.x) minVertex.x = Vertices[i].pos.x;
		if (Vertices[i].pos.y < minVertex.y) minVertex.y = Vertices[i].pos.y;
		if (Vertices[i].pos.z < minVertex.z) minVertex.z = Vertices[i].pos.z;

		if (Vertices[i].pos.x > maxVertex.x) maxVertex.x = Vertices[i].pos.x;
		if (Vertices[i].pos.y > maxVertex.y) maxVertex.y = Vertices[i].pos.y;
		if (Vertices[i].pos.z > maxVertex.z) maxVertex.z = Vertices[i].pos.z;
	}

	Ball.center.x = (minVertex.x + maxVertex.x) / 2;
	Ball.center.y = (minVertex.y + maxVertex.y) / 2;
	Ball.center.z = (minVertex.z + maxVertex.z) / 2;

	Ball.radius = MeshUtility::ComputeBoundingRadius(Ball.center, Vertices);

	ModelMesh.SetVertices(Application::GetI()->GetDevice(), &Vertices[0], Vertices.size()); 
	ModelMesh.SetIndices(Application::GetI()->GetDevice(), &Indices[0], Indices.size()); 
	ModelMesh.SetSubsetTable(Subsets);

	SubsetCount = Subsets.size(); 
}

void SkinnedMesh::from_byte(ifstream& inStream)
{
	if (!inStream.is_open()) 
	{
		std::cerr << "File stream is not open!" << std::endl; 
		return;
	}

	// 1. �̸� �б� (���ڿ�)
	size_t nameLength = 0;
	inStream.read(reinterpret_cast<char*>(&nameLength), sizeof(size_t));

	Name.resize(nameLength);
	inStream.read(&Name[0], nameLength);

	// 2. Vertices �б� (PosNormalTexTanSkinned �迭)
	size_t vertexCount = 0;
	inStream.read(reinterpret_cast<char*>(&vertexCount), sizeof(size_t));

	Vertices.resize(vertexCount);
	inStream.read(reinterpret_cast<char*>(Vertices.data()), vertexCount * sizeof(Vertex::PosNormalTexTanSkinned));

	// 3. Indices �б� (USHORT �迭)
	size_t indexCount = 0;
	inStream.read(reinterpret_cast<char*>(&indexCount), sizeof(size_t));

	Indices.resize(indexCount);
	inStream.read(reinterpret_cast<char*>(Indices.data()), indexCount * sizeof(USHORT));

	// 4. Subsets �б�
	size_t subsetCount = 0;
	inStream.read(reinterpret_cast<char*>(&subsetCount), sizeof(size_t));

	Subsets.resize(subsetCount);
	for (auto& subset : Subsets)
	{
		// �̸� �б� (���ڿ�)
		size_t subsetNameLength = 0;
		inStream.read(reinterpret_cast<char*>(&subsetNameLength), sizeof(size_t));

		subset.Name.resize(subsetNameLength);
		inStream.read(&subset.Name[0], subsetNameLength);

		// �ٸ� �ʵ�� �б� (Id, MaterialIndex, VertexStart, VertexCount, FaceStart, FaceCount)
		inStream.read(reinterpret_cast<char*>(&subset.Id), sizeof(subset.Id));
		inStream.read(reinterpret_cast<char*>(&subset.MaterialIndex), sizeof(subset.MaterialIndex));
		inStream.read(reinterpret_cast<char*>(&subset.VertexStart), sizeof(subset.VertexStart));
		inStream.read(reinterpret_cast<char*>(&subset.VertexCount), sizeof(subset.VertexCount));
		inStream.read(reinterpret_cast<char*>(&subset.FaceStart), sizeof(subset.FaceStart));
		inStream.read(reinterpret_cast<char*>(&subset.FaceCount), sizeof(subset.FaceCount));
	}

	// 5. 본 팔레트 (이름 + 역바인드 행렬)
	uint32_t boneCount = 0;
	inStream.read(reinterpret_cast<char*>(&boneCount), sizeof(boneCount));
	BoneNames.resize(boneCount);
	for (auto& name : BoneNames)
	{
		uint32_t n = 0;
		inStream.read(reinterpret_cast<char*>(&n), sizeof(n));
		name.resize(n);
		if (n) inStream.read(&name[0], n);
	}
	BoneOffsets.resize(boneCount);
	if (boneCount) inStream.read(reinterpret_cast<char*>(BoneOffsets.data()), boneCount * sizeof(XMFLOAT4X4));

	// 6. 재질 (FBX 의 기본 색)
	uint32_t matCount = 0;
	inStream.read(reinterpret_cast<char*>(&matCount), sizeof(matCount));
	Mat.resize(matCount);
	if (matCount) inStream.read(reinterpret_cast<char*>(Mat.data()), matCount * sizeof(Material));

	if (!Vertices.empty() && !Indices.empty())
		Setup();
}

void SkinnedMesh::to_byte(ofstream& outStream)
{
	if (!outStream.is_open())
	{
		std::cerr << "File stream is not open!" << std::endl;
		return;
	}

	// 1. �̸� ���� (���ڿ�) 
	size_t nameLength = Name.size(); 
	outStream.write(reinterpret_cast<const char*>(&nameLength), sizeof(size_t)); 
	outStream.write(Name.c_str(), nameLength); 

	// 2. Vertices ���� (PosNormalTexTan2 �迭)
	size_t vertexCount = Vertices.size(); 
	outStream.write(reinterpret_cast<const char*>(&vertexCount), sizeof(size_t)); 
	outStream.write(reinterpret_cast<const char*>(Vertices.data()), vertexCount * sizeof(Vertex::PosNormalTexTanSkinned));

	// 3. Indices ���� (USHORT �迭)
	size_t indexCount = Indices.size(); 
	outStream.write(reinterpret_cast<const char*>(&indexCount), sizeof(size_t)); 
	outStream.write(reinterpret_cast<const char*>(Indices.data()), indexCount * sizeof(USHORT)); 

	// 4. Subsets ����
	size_t subsetCount = Subsets.size(); 
	outStream.write(reinterpret_cast<const char*>(&subsetCount), sizeof(size_t)); 

	for (const auto& subset : Subsets)
	{
		// �̸� ���� (���ڿ�)
		size_t subsetNameLength = subset.Name.size(); 
		outStream.write(reinterpret_cast<const char*>(&subsetNameLength), sizeof(size_t)); 
		outStream.write(subset.Name.c_str(), subsetNameLength); 

		// �ٸ� �ʵ�� ���� (Id, MaterialIndex, VertexStart, VertexCount, FaceStart, FaceCount)
		outStream.write(reinterpret_cast<const char*>(&subset.Id), sizeof(subset.Id));
		outStream.write(reinterpret_cast<const char*>(&subset.MaterialIndex), sizeof(subset.MaterialIndex));
		outStream.write(reinterpret_cast<const char*>(&subset.VertexStart), sizeof(subset.VertexStart));
		outStream.write(reinterpret_cast<const char*>(&subset.VertexCount), sizeof(subset.VertexCount));
		outStream.write(reinterpret_cast<const char*>(&subset.FaceStart), sizeof(subset.FaceStart));
		outStream.write(reinterpret_cast<const char*>(&subset.FaceCount), sizeof(subset.FaceCount));
	}

	// 5. 본 팔레트
	uint32_t boneCount = (uint32_t)BoneNames.size();
	outStream.write(reinterpret_cast<const char*>(&boneCount), sizeof(boneCount));
	for (const auto& name : BoneNames)
	{
		uint32_t n = (uint32_t)name.size();
		outStream.write(reinterpret_cast<const char*>(&n), sizeof(n));
		outStream.write(name.data(), n);
	}
	if (boneCount) outStream.write(reinterpret_cast<const char*>(BoneOffsets.data()), boneCount * sizeof(XMFLOAT4X4));

	// 6. 재질
	uint32_t matCount = (uint32_t)Mat.size();
	outStream.write(reinterpret_cast<const char*>(&matCount), sizeof(matCount));
	if (matCount) outStream.write(reinterpret_cast<const char*>(Mat.data()), matCount * sizeof(Material));
}


void SkinnedMeshlInstance::Update(float dt)
{
	TimePos += dt;
	Model->SkinnedData.GetFinalTransforms(ClipName, TimePos, FinalTransforms);

	// Loop animation
	if (TimePos > Model->SkinnedData.GetClipEndTime(ClipName))
		TimePos = 0.0f;
}



MeshFile::MeshFile()
{
}

MeshFile::~MeshFile()
{
}

// 캐시(.mesh / .animations / .skeletons) 형식 버전. 구조가 바뀌면 값을 올린다 → 이전 캐시는 자동으로 다시 가져오기.
// NVC8: Unity 와 같은 축(Y 180°)으로 가져오기 (FBXLoader 의 ToEngine) — 이전 캐시는 반대쪽을 본다
// NVC9: 형식 다음에 Import Settings(.meta) 해시 — 설정을 바꾸거나 .meta 를 지우면 다시 가져온다
static const uint32_t kMeshCacheMagic = 0x3943564E;   // "NVC9"

static uint64_t ImportHash(const AssetImport::ModelSettings& settings)
{
	return (uint64_t)std::hash<std::string>{}(settings.ToJson().dump());
}

static bool ReadCacheMagic(ifstream& in, uint64_t expectHash)
{
	uint32_t magic = 0;
	uint64_t hash = 0;
	in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
	in.read(reinterpret_cast<char*>(&hash), sizeof(hash));
	return in.good() && magic == kMeshCacheMagic && hash == expectHash;
}

static void WriteCacheMagic(ofstream& out, uint64_t hash)
{
	out.write(reinterpret_cast<const char*>(&kMeshCacheMagic), sizeof(kMeshCacheMagic));
	out.write(reinterpret_cast<const char*>(&hash), sizeof(hash));
}

static bool IsCacheCurrent(const string& path, uint64_t expectHash)
{
	if (!filesystem::exists(path))
		return false;
	ifstream in(path, ios::binary);
	return ReadCacheMagic(in, expectHash);
}

MeshFile* MeshFile::LoadFromMetaFile(string path)
{
	MeshFile* loadMeshFile = new MeshFile; 

	loadMeshFile->FullPath	= PathManager::GetI()->GetMovePathS(path); 
	loadMeshFile->Path		= string_to_wstring(path); 
	loadMeshFile->Name		= PathManager::GetI()->GetFileName(path); 

	// FBX + glTF / GLB / VRM
	string fileExtension = File::GetExtension(loadMeshFile->FullPath);
	std::transform(fileExtension.begin(), fileExtension.end(), fileExtension.begin(), ::tolower);
	if (fileExtension != "fbx" && fileExtension != "vrm" && fileExtension != "glb" && fileExtension != "gltf")
	{
		delete loadMeshFile;
		return nullptr;
	}

	// Parsing
	string load_path_mesh = loadMeshFile->FullPath + ".mesh";
	string load_path_animations = loadMeshFile->FullPath + ".animations";
	string load_path_skeletone = loadMeshFile->FullPath + ".skeletons";

	// 캐시를 만든 Import Settings 가 지금과 다르면 다시 가져온다
	const wstring assetPath = string_to_wstring(loadMeshFile->FullPath);
	const uint64_t hash = ImportHash(AssetImport::LoadModel(assetPath));
	if (!IsCacheCurrent(load_path_mesh, hash) || !IsCacheCurrent(load_path_animations, hash) || !IsCacheCurrent(load_path_skeletone, hash))
	{
		if (!filesystem::exists(loadMeshFile->FullPath))
		{
			delete loadMeshFile;
			return nullptr;
		}
		loadMeshFile->ImportFile();   // FBX → 캐시 (현재 형식, Import Settings 적용)
		return loadMeshFile;
	}

	if (filesystem::exists(load_path_mesh))
	{
		ifstream mesh_instream(load_path_mesh, ios::binary);
		ReadCacheMagic(mesh_instream, hash);
		loadMeshFile->load_mesh(mesh_instream); 
		mesh_instream.close(); 
	}
	if (filesystem::exists(load_path_animations))
	{
		try
		{
			ifstream animation_instream(load_path_animations, ios::binary);
			ReadCacheMagic(animation_instream, hash);
			loadMeshFile->load_animations(animation_instream);
			animation_instream.close();
		}
		catch (const std::exception&)
		{

		}
	}
	if (filesystem::exists(load_path_skeletone))
	{
		try
		{
			ifstream skeletons_instream(load_path_skeletone, ios::binary);
			ReadCacheMagic(skeletons_instream, hash);
			loadMeshFile->load_skeletone(skeletons_instream);
			skeletons_instream.close(); 
		}
		catch (const std::exception&) 
		{

		}
	}
	for (auto& skeleton : loadMeshFile->Avatas)
		if (skeleton)
			skeleton->SourcePath = assetPath;

	return loadMeshFile;  
}
MeshFile* MeshFile::LoadFromFbxFile(string path)
{
	MeshFile* loadMeshFile = new MeshFile;

	loadMeshFile->FullPath = PathManager::GetI()->GetMovePathS(path);
	loadMeshFile->Path = string_to_wstring(path); 
	loadMeshFile->Name = PathManager::GetI()->GetFileName(path); 

	// FBX + glTF / GLB / VRM (Assimp 가 읽는다 — VRM 은 glTF 기반 아바타)
	string fileExtension = File::GetExtension(loadMeshFile->FullPath);
	std::transform(fileExtension.begin(), fileExtension.end(), fileExtension.begin(), ::tolower);
	if (fileExtension != "fbx" && fileExtension != "vrm" && fileExtension != "glb" && fileExtension != "gltf")
	{
		delete loadMeshFile; 
		return nullptr; 
	}

	// Parsing (Inspector 미리보기 — Scale Factor 적용)
	FBXLoader fbxLoader;
	fbxLoader.Scale = AssetImport::LoadModel(string_to_wstring(loadMeshFile->FullPath)).ScaleFactor;
	fbxLoader.LoadModelFbx(loadMeshFile->FullPath, loadMeshFile);

	return loadMeshFile;
}


void MeshFile::OnInspectorGUI()
{
	EditorGUI::LabelHeader(Name + " Fbx Import Setting");

	ImGui::Dummy(ImVec2(0, 4));
	//EditorGUI::LabelHeader("Vertices", );
	ImGui::Dummy(ImVec2(0, 8));

	EditorGUI::BoolField("Import Animations", UseImportAnimation);
	ImGui::Dummy(ImVec2(0, 4));
	if (EditorGUI::Button("Import Fbx Mesh"))
	{
		ImportFile(); 
	}

}

void MeshFile::ImportFile()
{
	// FBX → 메시 / 스켈레톤 / 애니메이션 (Import Settings: Scale Factor, Import Animation)
	const wstring assetPath = string_to_wstring(FullPath);
	const AssetImport::ModelSettings settings = AssetImport::LoadModel(assetPath);
	UseImportAnimation = settings.ImportAnimation;
	ScaleFactor = settings.ScaleFactor;
	FBXLoader fbxLoader;
	fbxLoader.Scale = settings.ScaleFactor;
	Meshs.clear();
	SkinnedMeshs.clear();
	fbxLoader.LoadModelFbx(FullPath, this);
	Avatas.clear();
	fbxLoader.LoadSkeletonAvata(FullPath, Avatas);
	for (auto& skeleton : Avatas)
		if (skeleton)
			skeleton->SourcePath = assetPath;
	SkinnedData.AnimationClips.clear();
	if (UseImportAnimation)
		fbxLoader.LoadAnimation(FullPath, SkinnedData);

	// 캐시 저장 (FBX 옆에 .mesh / .animations / .skeletons, 머리에 Import Settings 해시)
	const uint64_t hash = ImportHash(settings);
	auto open = [&](const wstring& suffix) { return ofstream(PathManager::GetI()->GetMovePathS(wstring_to_string(Path + suffix)), ios::binary); };
	{
		ofstream out = open(L".mesh");
		WriteCacheMagic(out, hash);
		save_mesh(out);
	}
	{
		ofstream out = open(L".animations");
		WriteCacheMagic(out, hash);
		save_animations(out);
	}
	{
		ofstream out = open(L".skeletons");
		WriteCacheMagic(out, hash);
		save_skeletone(out);
	}
}


void MeshFile::load_mesh(ifstream& inStream)
{
	if (!inStream.is_open())
	{
		std::cerr << "File stream is not open!" << std::endl;
		return;
	}

	size_t nameLength = 0;
	inStream.read(reinterpret_cast<char*>(&nameLength), sizeof(size_t));

	if (nameLength > 0) 
	{
		Name.resize(nameLength); 
		inStream.read(&Name[0], nameLength); 
	} 

	// Meshs ������ �а�, �� ������ŭ Mesh ��ü�� �����ϰ� �ε�
	size_t meshCount = 0;
	inStream.read(reinterpret_cast<char*>(&meshCount), sizeof(size_t));

	Meshs.resize(meshCount);
	for (size_t i = 0; i < meshCount; ++i)
	{
		Mesh* mesh = new Mesh;
		mesh->from_byte(inStream);
		
		shared_ptr<Mesh> mesh_ptr(mesh);
		Meshs[i] = mesh_ptr;
	}

	// SkinnedMeshs ������ �а�, �� ������ŭ SkinnedMesh ��ü�� �����ϰ� �ε�
	size_t skinnedMeshCount = 0;
	inStream.read(reinterpret_cast<char*>(&skinnedMeshCount), sizeof(size_t));

	SkinnedMeshs.resize(skinnedMeshCount);
	for (size_t i = 0; i < skinnedMeshCount; ++i)
	{
		shared_ptr<SkinnedMesh> skinnedMesh = make_shared<SkinnedMesh>();
		SkinnedMeshs[i] = skinnedMesh;
		SkinnedMeshs[i]->from_byte(inStream);  // �� SkinnedMesh ��ü�� from_byte ȣ��
	}
}
void MeshFile::save_mesh(ofstream& outStream)
{
	if (!outStream.is_open())
	{
		std::cerr << "File stream is not open!" << std::endl;
		return;
	}

	size_t nameLength = Name.size();
	outStream.write(reinterpret_cast<const char*>(&nameLength), sizeof(size_t));
	outStream.write(Name.c_str(), nameLength);

	size_t meshCount = Meshs.size();
	outStream.write(reinterpret_cast<char*>(&meshCount), sizeof(size_t));

	for (const auto& mesh : Meshs)
	{
		mesh->to_byte(outStream);
	}

	size_t skinnedMeshCount = SkinnedMeshs.size();
	outStream.write(reinterpret_cast<char*>(&skinnedMeshCount), sizeof(size_t)); 

	for (const auto& skinnedMesh : SkinnedMeshs)
	{
		skinnedMesh->to_byte(outStream);
	}
}


void MeshFile::load_animations(ifstream& inStream)
{
	SkinnedData.from_byte(inStream); 
}
void MeshFile::save_animations(ofstream& outStream)
{
	SkinnedData.to_byte(outStream); 
}


void MeshFile::save_skeletone(ofstream& outStream)
{
	if (!outStream.is_open())
		return;

	size_t avatasSize = Avatas.size();
	outStream.write(reinterpret_cast<const char*>(&avatasSize), sizeof(size_t));

	for (const auto& avata : Avatas)
	{
		avata->to_byte(outStream); 
	}
}
void MeshFile::load_skeletone(ifstream& inStream)
{
	if (!inStream.is_open())
		return;

	try
	{
		size_t avatasSize;
		inStream.read(reinterpret_cast<char*>(&avatasSize), sizeof(size_t));
		Avatas.resize(avatasSize);

		for (size_t i = 0; i < Avatas.size(); ++i)
		{
			shared_ptr<SkeletonAvataData> avata_ptr = make_shared<SkeletonAvataData>();
			Avatas[i] = avata_ptr;
			Avatas[i]->from_byte(inStream);
		}
	}
	catch (const std::exception&)
	{
		// Debug Log
	}
}
