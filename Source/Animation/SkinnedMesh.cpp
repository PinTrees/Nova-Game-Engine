#include "pch.h"
#include "SkinnedMesh.h"
#include "AnimationPose.h"
#include "FBXLoader.h"
#include "AssetImportSettings.h"
#include "SkinnedData.h"
#include "LoadM3d.h"
#include "MathHelper.h"
#include "File.h"
#include "MeshUtility.h"
#include "EditorGUI.h"

// 메시 바인드 후보: 내보내기마다 정점 · 역바인드 · 메시 노드 변환의 기준이 다르다 (장면 공간 역바인드, 메시 노드 공간 역바인드,
//  정점이 이미 모델 공간, 소켓에 붙인 단단한 부위의 본 공간 정점, 크기 ×100 이 한쪽에만 …)
//  0 메시 노드 전역 (예전), 1 단위 행렬, 2 메시 노드 전역 · 단위 · (첫 본 Offset · 전역)⁻¹, 3 (첫 본 Offset · 전역)⁻¹, 4 첫 본 Offset⁻¹
int SkinnedMesh::BindCandidates(const SkeletonAvataData& sk, XMMATRIX out[kBindCandidates], XMMATRIX* chainOut, std::vector<XMFLOAT4X4>* globalOut) const
{
	XMMATRIX chain = XMMatrixIdentity();
	for (int node = sk.FindNode(Name); node >= 0 && node < (int)sk.BindLocal.size(); node = sk.BoneHierarchy[(size_t)node])
		chain = chain * XMLoadFloat4x4(&sk.BindLocal[(size_t)node]);
	if (chainOut)
		*chainOut = chain;
	for (int c = 0; c < kBindCandidates; ++c)
		out[c] = chain;
	out[1] = XMMatrixIdentity();
	int bone = -1, node = -1;
	for (size_t k = 0; k < BoneNames.size() && k < BoneOffsets.size(); ++k)
		if ((node = sk.FindNode(BoneNames[k])) >= 0) { bone = (int)k; break; }
	if (bone < 0)
		return 0;
	std::vector<XMFLOAT4X4> local;
	std::vector<XMFLOAT4X4>& global = globalOut ? *globalOut : local;
	AnimationPose::ComputeGlobals(sk, sk.BindLocal, global);
	if (node >= (int)global.size())
		return 0;
	const XMMATRIX unit = XMMatrixScaling(sk.UnitScale, sk.UnitScale, sk.UnitScale);
	const XMMATRIX off = XMLoadFloat4x4(&BoneOffsets[(size_t)bone]), g = XMLoadFloat4x4(&global[(size_t)node]);
	XMVECTOR det;
	const XMMATRIX inv = XMMatrixInverse(&det, off * g);
	if (fabsf(XMVectorGetX(det)) > 1e-20f)
	{
		out[2] = chain * unit * inv;
		out[3] = inv;
	}
	const XMMATRIX invOff = XMMatrixInverse(&det, off);
	if (fabsf(XMVectorGetX(det)) > 1e-20f)
		out[4] = invOff;
	// 바인드 자세 뒤 회전: X · (Off · 전역) · Rot · (Off · 전역)⁻¹ — 바인드 자세 결과를 모델 공간에서 돌린 것 (단단한 부위는 정확,
	//  여러 본 부위는 Off · 전역이 같을 때). Unreal 내보내기의 소품 (모자 · 머리카락) 이 X 180° 뒤집혀 있었다
	const XMMATRIX bind = off * g;
	const XMMATRIX invBind = XMMatrixInverse(&det, bind);
	const bool ok = fabsf(XMVectorGetX(det)) > 1e-20f;
	const XMMATRIX rots[3] = { XMMatrixRotationX(XM_PI), XMMatrixRotationX(XM_PIDIV2), XMMatrixRotationX(-XM_PIDIV2) };
	for (int r = 0; r < 3; ++r)
		for (int c = 0; c < kBindBase; ++c)
			out[kBindBase * (r + 1) + c] = ok ? out[c] * bind * rots[r] * invBind : out[c];
	return kBindCandidates;
}

// 바인드 자세 스키닝 (가중치 넷) 한 정점 — 후보 X
static XMVECTOR SkinBind(const SkinnedMesh& m, const std::vector<int>& nodeOf, const std::vector<XMFLOAT4X4>& global, size_t i, CXMMATRIX x, int* heavy)
{
	const auto& v = m.Vertices[i];
	const float w[4] = { v.weights.x, v.weights.y, v.weights.z, 1.0f - v.weights.x - v.weights.y - v.weights.z };
	XMVECTOR p = XMVectorZero();
	float used = 0.0f;
	*heavy = -1;
	for (int j = 0; j < 4; ++j)
	{
		const int k = v.boneIndices[j];
		if (w[j] <= 0.0f || k >= (int)m.BoneOffsets.size() || nodeOf[(size_t)k] < 0 || nodeOf[(size_t)k] >= (int)global.size())
			continue;
		if (*heavy < 0)
			*heavy = nodeOf[(size_t)k];
		const XMMATRIX gk = XMLoadFloat4x4(&global[(size_t)nodeOf[(size_t)k]]);
		p += XMVector3TransformCoord(XMLoadFloat3(&v.pos), x * XMLoadFloat4x4(&m.BoneOffsets[(size_t)k]) * gk) * w[j];
		used += w[j];
	}
	return used > 1e-6f ? p / used : p;
}

void SkinnedMesh::BindCandidateBounds(const SkeletonAvataData& sk, XMFLOAT3 mn[kBindCandidates], XMFLOAT3 mx[kBindCandidates]) const
{
	XMMATRIX cand[kBindCandidates];
	std::vector<XMFLOAT4X4> global;
	BindCandidates(sk, cand, nullptr, &global);
	std::vector<int> nodeOf(BoneNames.size(), -1);
	for (size_t k = 0; k < BoneNames.size(); ++k)
		nodeOf[k] = sk.FindNode(BoneNames[k]);
	const size_t step = (std::max)((size_t)1, Vertices.size() / 512);
	for (int c = 0; c < kBindCandidates; ++c)
	{
		XMVECTOR lo = XMVectorReplicate(FLT_MAX), hi = XMVectorReplicate(-FLT_MAX);
		for (size_t i = 0; i < Vertices.size(); i += step)
		{
			int heavy;
			const XMVECTOR p = SkinBind(*this, nodeOf, global, i, cand[c], &heavy);
			lo = XMVectorMin(lo, p);
			hi = XMVectorMax(hi, p);
		}
		XMStoreFloat3(&mn[c], lo);
		XMStoreFloat3(&mx[c], hi);
	}
}

bool SkinnedMesh::BindBounds(const SkeletonAvataData& sk, CXMMATRIX meshBind, XMFLOAT3& mn, XMFLOAT3& mx) const
{
	std::vector<XMFLOAT4X4> global;
	AnimationPose::ComputeGlobals(sk, sk.BindLocal, global);
	std::vector<int> nodeOf(BoneNames.size(), -1);
	for (size_t k = 0; k < BoneNames.size(); ++k)
		nodeOf[k] = sk.FindNode(BoneNames[k]);
	const size_t step = (std::max)((size_t)1, Vertices.size() / 2048);
	XMVECTOR lo = XMVectorReplicate(FLT_MAX), hi = XMVectorReplicate(-FLT_MAX);
	bool any = false;
	for (size_t i = 0; i < Vertices.size(); i += step)
	{
		int heavy;
		const XMVECTOR p = SkinBind(*this, nodeOf, global, i, meshBind, &heavy);
		if (heavy < 0)
			continue;
		lo = XMVectorMin(lo, p);
		hi = XMVectorMax(hi, p);
		any = true;
	}
	XMStoreFloat3(&mn, lo);
	XMStoreFloat3(&mx, hi);
	return any;
}

XMMATRIX SkinnedMesh::PaletteMeshBind(const SkeletonAvataData& sk, XMMATRIX* chainOut, int mode) const
{
	XMMATRIX cand[kBindCandidates];
	std::vector<XMFLOAT4X4> global;
	if (BindCandidates(sk, cand, chainOut, &global) == 0)
		return cand[0];
	if (mode >= 0 && mode < kBindCandidates)
		return cand[mode];   // 가져오기가 정답 (Unity 가 구운 자리) 과 맞춰 고정한 것
	// 자동: 바인드 자세에서 "정점이 자기 본 가까이 오는" 기본 후보 (같으면 앞 후보 — 예전 방식을 지킨다)
	std::vector<int> nodeOf(BoneNames.size(), -1);
	for (size_t k = 0; k < BoneNames.size(); ++k)
		nodeOf[k] = sk.FindNode(BoneNames[k]);
	const size_t step = (std::max)((size_t)1, Vertices.size() / 96);
	float best = FLT_MAX;
	int pick = 0;
	for (int c = 0; c < kBindBase; ++c)
	{
		double sum = 0.0;
		int n = 0;
		for (size_t i = 0; i < Vertices.size(); i += step)
		{
			int heavy;
			const XMVECTOR p = SkinBind(*this, nodeOf, global, i, cand[c], &heavy);
			if (heavy < 0)
				continue;
			sum += XMVectorGetX(XMVector3Length(p - XMLoadFloat4x4(&global[(size_t)heavy]).r[3]));
			++n;
		}
		const float avg = n > 0 ? (float)(sum / n) : FLT_MAX;
		if (avg < best * 0.98f)
		{
			best = avg;
			pick = c;
		}
	}
	return cand[pick];
}

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
	uint64_t nameLength = 0;
	inStream.read(reinterpret_cast<char*>(&nameLength), sizeof(uint64_t));

	Name.resize(nameLength);
	inStream.read(&Name[0], nameLength);

	// 2. Vertices �б� (PosNormalTexTanSkinned �迭)
	uint64_t vertexCount = 0;
	inStream.read(reinterpret_cast<char*>(&vertexCount), sizeof(uint64_t));

	Vertices.resize(vertexCount);
	inStream.read(reinterpret_cast<char*>(Vertices.data()), vertexCount * sizeof(Vertex::PosNormalTexTanSkinned));

	// 3. Indices �б� (USHORT �迭)
	uint64_t indexCount = 0;
	inStream.read(reinterpret_cast<char*>(&indexCount), sizeof(uint64_t));

	Indices.resize(indexCount);
	inStream.read(reinterpret_cast<char*>(Indices.data()), indexCount * sizeof(USHORT));

	// 4. Subsets �б�
	uint64_t subsetCount = 0;
	inStream.read(reinterpret_cast<char*>(&subsetCount), sizeof(uint64_t));

	Subsets.resize(subsetCount);
	for (auto& subset : Subsets)
	{
		// �̸� �б� (���ڿ�)
		uint64_t subsetNameLength = 0;
		inStream.read(reinterpret_cast<char*>(&subsetNameLength), sizeof(uint64_t));

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

	// 7. BlendShape (이름 + 정점 번호 · 위치 차이 · 법선 차이)
	uint32_t shapeCount = 0;
	inStream.read(reinterpret_cast<char*>(&shapeCount), sizeof(shapeCount));
	BlendShapes.resize(inStream.good() ? shapeCount : 0);
	for (BlendShapeData& s : BlendShapes)
	{
		uint32_t n = 0, count = 0;
		inStream.read(reinterpret_cast<char*>(&n), sizeof(n));
		s.Name.resize(n);
		if (n) inStream.read(&s.Name[0], n);
		inStream.read(reinterpret_cast<char*>(&count), sizeof(count));
		s.Index.resize(count);
		s.DPos.resize(count);
		s.DNrm.resize(count);
		if (count)
		{
			inStream.read(reinterpret_cast<char*>(s.Index.data()), count * sizeof(uint32));
			inStream.read(reinterpret_cast<char*>(s.DPos.data()), count * sizeof(XMFLOAT3));
			inStream.read(reinterpret_cast<char*>(s.DNrm.data()), count * sizeof(XMFLOAT3));
		}
	}

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
	uint64_t nameLength = Name.size(); 
	outStream.write(reinterpret_cast<const char*>(&nameLength), sizeof(uint64_t)); 
	outStream.write(Name.c_str(), nameLength); 

	// 2. Vertices ���� (PosNormalTexTan2 �迭)
	uint64_t vertexCount = Vertices.size(); 
	outStream.write(reinterpret_cast<const char*>(&vertexCount), sizeof(uint64_t)); 
	outStream.write(reinterpret_cast<const char*>(Vertices.data()), vertexCount * sizeof(Vertex::PosNormalTexTanSkinned));

	// 3. Indices ���� (USHORT �迭)
	uint64_t indexCount = Indices.size(); 
	outStream.write(reinterpret_cast<const char*>(&indexCount), sizeof(uint64_t)); 
	outStream.write(reinterpret_cast<const char*>(Indices.data()), indexCount * sizeof(USHORT)); 

	// 4. Subsets ����
	uint64_t subsetCount = Subsets.size(); 
	outStream.write(reinterpret_cast<const char*>(&subsetCount), sizeof(uint64_t)); 

	for (const auto& subset : Subsets)
	{
		// �̸� ���� (���ڿ�)
		uint64_t subsetNameLength = subset.Name.size(); 
		outStream.write(reinterpret_cast<const char*>(&subsetNameLength), sizeof(uint64_t)); 
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

	// 7. BlendShape
	const uint32_t shapeCount = (uint32_t)BlendShapes.size();
	outStream.write(reinterpret_cast<const char*>(&shapeCount), sizeof(shapeCount));
	for (const BlendShapeData& sh : BlendShapes)
	{
		const uint32_t n = (uint32_t)sh.Name.size(), count = (uint32_t)sh.Index.size();
		outStream.write(reinterpret_cast<const char*>(&n), sizeof(n));
		if (n) outStream.write(sh.Name.data(), n);
		outStream.write(reinterpret_cast<const char*>(&count), sizeof(count));
		if (count)
		{
			outStream.write(reinterpret_cast<const char*>(sh.Index.data()), count * sizeof(uint32));
			outStream.write(reinterpret_cast<const char*>(sh.DPos.data()), count * sizeof(XMFLOAT3));
			outStream.write(reinterpret_cast<const char*>(sh.DNrm.data()), count * sizeof(XMFLOAT3));
		}
	}
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
// NVCC: 본과 이름이 같은 메시 노드를 "<이름>_Mesh" 로 (FBXLoader 의 RenameClashingMeshNodes)
static const uint32_t kMeshCacheMagic = 0x4343564E;   // "NVCC"

// FNV-1a 64 비트 — std::hash 는 구현마다 값이 달라 (MSVC = FNV-1a, 안드로이드 libc++ = 다른 함수) PC 가 구운 캐시를 기기가 버린다.
//  MSVC 의 std::hash<std::string> 과 같은 값이라 이미 만든 캐시도 그대로 맞는다
static uint64_t ImportHash(const AssetImport::ModelSettings& settings)
{
	uint64_t h = 14695981039346656037ull;
	for (const unsigned char c : settings.ToJson().dump())
	{
		h ^= c;
		h *= 1099511628211ull;
	}
	return h;
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

// 캐시가 지금 Import Settings 로 만들어졌고, 원본 (FBX · VRM) 보다 나중이면 그대로 쓴다 (Unity 도 원본이 바뀌면 다시 가져온다)
static bool IsCacheCurrent(const string& path, uint64_t expectHash, const string& sourcePath)
{
	if (!filesystem::exists(path))
		return false;
	std::error_code ec1, ec2;
	const auto cacheTime = filesystem::last_write_time(path, ec1), sourceTime = filesystem::last_write_time(sourcePath, ec2);
	if (!ec1 && !ec2 && cacheTime < sourceTime)
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
	const string& source = loadMeshFile->FullPath;
	if (!IsCacheCurrent(load_path_mesh, hash, source) || !IsCacheCurrent(load_path_animations, hash, source) || !IsCacheCurrent(load_path_skeletone, hash, source))
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

	uint64_t nameLength = 0;
	inStream.read(reinterpret_cast<char*>(&nameLength), sizeof(uint64_t));

	if (nameLength > 0) 
	{
		Name.resize(nameLength); 
		inStream.read(&Name[0], nameLength); 
	} 

	// Meshs ������ �а�, �� ������ŭ Mesh ��ü�� �����ϰ� �ε�
	uint64_t meshCount = 0;
	inStream.read(reinterpret_cast<char*>(&meshCount), sizeof(uint64_t));

	Meshs.resize(meshCount);
	for (size_t i = 0; i < meshCount; ++i)
	{
		Mesh* mesh = new Mesh;
		mesh->from_byte(inStream);
		
		shared_ptr<Mesh> mesh_ptr(mesh);
		Meshs[i] = mesh_ptr;
	}

	// SkinnedMeshs ������ �а�, �� ������ŭ SkinnedMesh ��ü�� �����ϰ� �ε�
	uint64_t skinnedMeshCount = 0;
	inStream.read(reinterpret_cast<char*>(&skinnedMeshCount), sizeof(uint64_t));

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

	uint64_t nameLength = Name.size();
	outStream.write(reinterpret_cast<const char*>(&nameLength), sizeof(uint64_t));
	outStream.write(Name.c_str(), nameLength);

	uint64_t meshCount = Meshs.size();
	outStream.write(reinterpret_cast<char*>(&meshCount), sizeof(uint64_t));

	for (const auto& mesh : Meshs)
	{
		mesh->to_byte(outStream);
	}

	uint64_t skinnedMeshCount = SkinnedMeshs.size();
	outStream.write(reinterpret_cast<char*>(&skinnedMeshCount), sizeof(uint64_t)); 

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

	uint64_t avatasSize = Avatas.size();
	outStream.write(reinterpret_cast<const char*>(&avatasSize), sizeof(uint64_t));

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
		uint64_t avatasSize;
		inStream.read(reinterpret_cast<char*>(&avatasSize), sizeof(uint64_t));
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
