#include "pch.h"
#include "FBXLoader.h"
struct VertexKey
{
    XMFLOAT3 pos;       // Position
    XMFLOAT3 normal;    // Normal
    XMFLOAT2 tex;       // Texture coordinates
    XMFLOAT4 tangent;   // Tangent
    BYTE color[4];      // Color (R, G, B, A)

    bool operator==(const VertexKey& other) const
    {
        return pos.x == other.pos.x && pos.y == other.pos.y && pos.z == other.pos.z &&
            normal.x == other.normal.x && normal.y == other.normal.y && normal.z == other.normal.z &&
            tex.x == other.tex.x && tex.y == other.tex.y &&
            tangent.x == other.tangent.x && tangent.y == other.tangent.y &&
            tangent.z == other.tangent.z && tangent.w == other.tangent.w &&
            color[0] == other.color[0] && color[1] == other.color[1] &&
            color[2] == other.color[2] && color[3] == other.color[3];
    }
};

struct VertexKeyHasher
{
    std::size_t operator()(const VertexKey& key) const
    {
        std::size_t h1 = std::hash<float>()(key.pos.x) ^ std::hash<float>()(key.pos.y) ^ std::hash<float>()(key.pos.z);
        std::size_t h2 = std::hash<float>()(key.normal.x) ^ std::hash<float>()(key.normal.y) ^ std::hash<float>()(key.normal.z);
        std::size_t h3 = std::hash<float>()(key.tex.x) ^ std::hash<float>()(key.tex.y);
        std::size_t h4 = std::hash<float>()(key.tangent.x) ^ std::hash<float>()(key.tangent.y) ^ std::hash<float>()(key.tangent.z) ^ std::hash<float>()(key.tangent.w);
        std::size_t h5 = std::hash<BYTE>()(key.color[0]) ^ std::hash<BYTE>()(key.color[1]) ^ std::hash<BYTE>()(key.color[2]) ^ std::hash<BYTE>()(key.color[3]);

        return h1 ^ h2 ^ h3 ^ h4 ^ h5;
    }
};


// �ߺ��� ���� �����ϴ� �Լ�
void OptimizeVertices(std::vector<Vertex::PosNormalTexTanSkinned>& vertices, std::vector<USHORT>& indices)
{
    // ���� �ߺ� üũ�� ���� �� (VertexKey, UINT)
    std::unordered_map<VertexKey, UINT, VertexKeyHasher> uniqueVertices;
    std::vector<Vertex::PosNormalTexTanSkinned> optimizedVertices;
    std::vector<USHORT> optimizedIndices;

    for (const auto& index : indices)
    {
        // ���� �ε����� ���� ��������
        Vertex::PosNormalTexTanSkinned& vertex = vertices[index];

        // VertexKey ���� - �񱳸� ���� �ֿ� �ʵ� ����
        VertexKey key;
        key.pos = vertex.pos;
        key.normal = vertex.normal;
        key.tex = vertex.tex;
        key.tangent = vertex.tangentU;
        //std::memcpy(key.color, vertex.color, sizeof(BYTE) * 4);  // color ����

        // ������ ã�ų�, ã�� ���ϸ� ���� �߰�
        auto it = uniqueVertices.find(key);
        if (it == uniqueVertices.end())
        {
            // ������ ���ٸ� �߰��ϰ�, ���ο� �ε����� �ο�
            UINT newIndex = static_cast<UINT>(optimizedVertices.size());
            uniqueVertices[key] = newIndex;
            optimizedVertices.push_back(vertex);
            optimizedIndices.push_back(newIndex);
        }
        else
        {
            // �̹� �ִ� �����̶�� �ش� �ε��� ���
            optimizedIndices.push_back(it->second);
        }
    }

    // ����ȭ�� ������ �ε��� ����Ʈ�� ��ü
    vertices = optimizedVertices;
    indices = optimizedIndices;
}


// 모델/스켈레톤/애니메이션 모두 같은 설정으로 읽어야 노드 이름·좌표계가 일치한다.
//  - 왼손 좌표계(DirectX)로 변환, 삼각형화, 정점 가중치 최대 4 개
//  - 16 비트 인덱스를 쓰므로 메시를 65000 정점 이하로 나눈다
//  - FBX 피벗 보조 노드($AssimpFbx$)를 만들지 않는다: 노드 변환과 애니메이션 키가 같은 값(PreRotation 포함)이 되어 이름으로 바로 연결된다
static const aiScene* ReadFbxScene(Assimp::Importer& importer, const std::string& path, bool withMeshes)
{
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);   // PreRotation 등을 노드 변환·애니메이션 키에 합쳐 넣는다
    importer.SetPropertyInteger(AI_CONFIG_PP_SLM_VERTEX_LIMIT, 65000);
    importer.SetPropertyInteger(AI_CONFIG_PP_SLM_TRIANGLE_LIMIT, 1000000);
    importer.SetPropertyInteger(AI_CONFIG_PP_LBW_MAX_WEIGHTS, 4);
    unsigned flags = aiProcess_ConvertToLeftHanded | aiProcess_Triangulate | aiProcess_LimitBoneWeights;
    if (withMeshes)
        flags |= aiProcess_GenSmoothNormals | aiProcess_CalcTangentSpace | aiProcess_JoinIdenticalVertices |
                 aiProcess_SplitLargeMeshes | aiProcess_ImproveCacheLocality | aiProcess_SortByPType | aiProcess_ValidateDataStructure;
    const aiScene* scene = importer.ReadFile(path, flags);
    if (!scene || !scene->mRootNode)
    {
        printf("ERROR::ASSIMP:: %s\n", importer.GetErrorString());
        return nullptr;
    }
    return scene;
}

static XMFLOAT4X4 ToRowMajor(const aiMatrix4x4& m)
{
    // Assimp 는 열 벡터 규약 → DirectX(행 벡터) 로 전치
    XMFLOAT4X4 r;
    XMStoreFloat4x4(&r, XMMatrixTranspose(XMLoadFloat4x4(reinterpret_cast<const XMFLOAT4X4*>(&m))));
    return r;
}

// Unity 와 같은 축: Assimp 의 ConvertToLeftHanded 는 Z 를 뒤집지만 Unity 는 X 를 뒤집는다 → 둘은 Y 축 180° 차이.
// 모든 정점·노드·본·애니메이션 키에 같은 회전 R = diag(-1, 1, -1) 을 걸어 Unity 에서 +Z 를 보던 캐릭터가 여기서도 +Z 를 보게 한다
// (R 은 회전이라 삼각형 감기 방향은 그대로). 행렬은 R·M·R = 원소 (i, j) 에 r_i·r_j 를 곱한 것.
static aiVector3D ToEngine(const aiVector3D& v) { return aiVector3D(-v.x, v.y, -v.z); }

static XMFLOAT4X4 ToEngineMatrix(const aiMatrix4x4& m)
{
    XMFLOAT4X4 r = ToRowMajor(m);
    const float s[4] = { -1.0f, 1.0f, -1.0f, 1.0f };
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            r.m[i][j] *= s[i] * s[j];
    return r;
}

bool FBXLoader::LoadModelFbx(const std::string& filename, MeshFile* model)
{
    Assimp::Importer importer;
    const aiScene* scene = ReadFbxScene(importer, filename, true);
    if (scene == nullptr)
        return false;
    ParsingMeshNode(scene->mRootNode, scene, model);
    return true;
}

// 노드 계층 전체를 스켈레톤으로 (깊이 우선 → 부모 인덱스가 항상 자식보다 작다)
static void CollectNodes(const aiNode* node, int parent, SkeletonAvataData& skel)
{
    const int index = (int)skel.NodeNames.size();
    skel.NodeNames.push_back(node->mName.C_Str());
    skel.BoneHierarchy.push_back(parent);
    skel.BindLocal.push_back(ToEngineMatrix(node->mTransformation));
    for (unsigned i = 0; i < node->mNumChildren; ++i)
        CollectNodes(node->mChildren[i], index, skel);
}

bool FBXLoader::LoadSkeletonAvata(const std::string& filepath, vector<shared_ptr<SkeletonAvataData>>& skeletones)
{
    Assimp::Importer importer;
    const aiScene* scene = ReadFbxScene(importer, filepath, false);
    if (scene == nullptr)
        return false;

    shared_ptr<SkeletonAvataData> skeletonData = make_shared<SkeletonAvataData>();
    skeletonData->Name = filesystem::path(filepath).filename().string();
    CollectNodes(scene->mRootNode, -1, *skeletonData);

    // 파일 단위 → 미터 (Unity 의 Convert Units). FBX UnitScaleFactor 는 cm 기준 (cm = 1, m = 100)
    double unit = 1.0;
    if (scene->mMetaData != nullptr)
    {
        float f = 0.0f;
        double d = 0.0;
        if (scene->mMetaData->Get("UnitScaleFactor", d) && d > 0.0) unit = d;
        else if (scene->mMetaData->Get("UnitScaleFactor", f) && f > 0.0f) unit = f;
    }
    skeletonData->UnitScale = (float)(unit * 0.01);

    skeletones.push_back(skeletonData);
    return true;
}

// 포함한 Assimp 헤더와 DLL 의 키 구조체 크기가 다를 수 있다 (DLL 쪽 aiQuatKey 에 mInterpolation 이 있어 32 바이트).
// 키 시간이 유한하고 증가하며, 쿼터니언 길이가 1 인 간격을 찾아 그 간격으로 읽는다.
static size_t DetectQuatKeyStride(const aiQuatKey* keys, unsigned count)
{
    const size_t candidates[] = { sizeof(aiQuatKey), 32, 40 };
    if (count < 2)
        return sizeof(aiQuatKey);
    for (size_t stride : candidates)
    {
        bool ok = true;
        double prev = -1e300;
        for (unsigned k = 0; k < (std::min)(count, 6u) && ok; ++k)
        {
            const aiQuatKey* key = reinterpret_cast<const aiQuatKey*>(reinterpret_cast<const unsigned char*>(keys) + k * stride);
            const double t = key->mTime;
            const aiQuaternion& q = key->mValue;
            const double len = q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z;
            ok = std::isfinite(t) && t >= prev && fabs(len - 1.0) < 0.05;
            prev = t;
        }
        if (ok)
            return stride;
    }
    return sizeof(aiQuatKey);
}

static size_t DetectVectorKeyStride(const aiVectorKey* keys, unsigned count)
{
    const size_t candidates[] = { sizeof(aiVectorKey), 24, 32 };
    if (count < 2)
        return sizeof(aiVectorKey);
    for (size_t stride : candidates)
    {
        bool ok = true;
        double prev = -1e300;
        for (unsigned k = 0; k < (std::min)(count, 6u) && ok; ++k)
        {
            const aiVectorKey* key = reinterpret_cast<const aiVectorKey*>(reinterpret_cast<const unsigned char*>(keys) + k * stride);
            ok = std::isfinite(key->mTime) && key->mTime >= prev && std::isfinite(key->mValue.x) && std::isfinite(key->mValue.y) && std::isfinite(key->mValue.z);
            prev = key->mTime;
        }
        if (ok)
            return stride;
    }
    return sizeof(aiVectorKey);
}

template <class K>
static const K& KeyAt(const K* keys, size_t stride, unsigned index)
{
    return *reinterpret_cast<const K*>(reinterpret_cast<const unsigned char*>(keys) + index * stride);
}

bool FBXLoader::LoadAnimation(const std::string& filename, SkinnedData& skinnedData)
{
    Assimp::Importer importer;
    const aiScene* scene = ReadFbxScene(importer, filename, false);
    if (scene == nullptr || !scene->HasAnimations())
        return false;

    for (unsigned a = 0; a < scene->mNumAnimations; ++a)
    {
        const aiAnimation* anim = scene->mAnimations[a];
        const double tps = anim->mTicksPerSecond > 0.0 ? anim->mTicksPerSecond : 25.0;
        shared_ptr<AnimationClip> clip = make_shared<AnimationClip>();
        clip->Name = anim->mName.length > 0 ? anim->mName.C_Str() : filesystem::path(filename).stem().string();
        // "Take 001" 같은 이름 대신 "파일명|클립명" 형태의 앞부분 정리
        size_t bar = clip->Name.find('|');
        if (bar != string::npos) clip->Name = clip->Name.substr(bar + 1);
        clip->Duration = (float)(anim->mDuration / tps);

        for (unsigned c = 0; c < anim->mNumChannels; ++c)
        {
            const aiNodeAnim* ch = anim->mChannels[c];
            AnimationChannel channel;
            channel.NodeName = ch->mNodeName.C_Str();
            const size_t posStride = DetectVectorKeyStride(ch->mPositionKeys, ch->mNumPositionKeys);
            const size_t rotStride = DetectQuatKeyStride(ch->mRotationKeys, ch->mNumRotationKeys);
            const size_t sclStride = DetectVectorKeyStride(ch->mScalingKeys, ch->mNumScalingKeys);
            for (unsigned k = 0; k < ch->mNumPositionKeys; ++k)
            {
                const auto& key = KeyAt(ch->mPositionKeys, posStride, k);
                channel.Positions.push_back({ (float)(key.mTime / tps), XMFLOAT3(-key.mValue.x, key.mValue.y, -key.mValue.z) });   // ToEngine
            }
            for (unsigned k = 0; k < ch->mNumRotationKeys; ++k)
            {
                const auto& key = KeyAt(ch->mRotationKeys, rotStride, k);
                // R·q·R (Y 축 180°): 회전축의 X·Z 를 뒤집는다
                channel.Rotations.push_back({ (float)(key.mTime / tps), XMFLOAT4(-key.mValue.x, key.mValue.y, -key.mValue.z, key.mValue.w) });
            }
            for (unsigned k = 0; k < ch->mNumScalingKeys; ++k)
            {
                const auto& key = KeyAt(ch->mScalingKeys, sclStride, k);
                channel.Scales.push_back({ (float)(key.mTime / tps), XMFLOAT3(key.mValue.x, key.mValue.y, key.mValue.z) });
            }
            clip->Channels.push_back(std::move(channel));
        }
        skinnedData.AnimationClips.push_back(clip);
    }
    return true;
}

void FBXLoader::ParsingMeshNode(aiNode* node, const aiScene* scene, MeshFile* model)
{
    if (node->mNumMeshes > 0) 
    {
        bool anySkinned = false, anyStatic = false;
        for (UINT i = 0; i < node->mNumMeshes; ++i)
            (scene->mMeshes[node->mMeshes[i]]->HasBones() ? anySkinned : anyStatic) = true;
        if (anySkinned)
        {
            shared_ptr<SkinnedMesh> mesh = make_shared<SkinnedMesh>(); 
            model->SkinnedMeshs.push_back(mesh); 
            mesh->Name = node->mName.C_Str();   
            mesh->Mat.resize(scene->mNumMaterials);  
            for (UINT i = 0; i < scene->mNumMaterials; ++i) 
            {
                aiMaterial* mat = scene->mMaterials[i]; 
                aiString name; 
            
                mesh->Mat[i].Ambient = XMFLOAT4(0.2f, 0.2f, 0.2f, 1.0f);
                mesh->Mat[i].Diffuse = XMFLOAT4(0.8f, 0.8f, 0.8f, 1.0f);
                mesh->Mat[i].Specular = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
                mesh->Mat[i].Reflect = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
            
                aiColor3D color(0.f, 0.f, 0.f);
                if (AI_SUCCESS == mat->Get(AI_MATKEY_COLOR_AMBIENT, color))
                    mesh->Mat[i].Ambient = XMFLOAT4(color.r, color.g, color.b, 1.0f);
                if (AI_SUCCESS == mat->Get(AI_MATKEY_COLOR_DIFFUSE, color))
                    mesh->Mat[i].Diffuse = XMFLOAT4(color.r, color.g, color.b, 1.0f);
                if (AI_SUCCESS == mat->Get(AI_MATKEY_COLOR_SPECULAR, color))
                    mesh->Mat[i].Specular = XMFLOAT4(color.r, color.g, color.b, 1.0f);
            }

            mesh->Vertices.clear();
            for (UINT i = 0; i < node->mNumMeshes; ++i)
            {
                aiMesh* aiMesh = scene->mMeshes[node->mMeshes[i]];
                if (!aiMesh->HasBones())
                    continue;   // 본 없는 메시는 아래에서 정적 메시로

                MeshGeometry::Subset subset; 
                subset.Id = i;
                ProcessMeshSkinned(aiMesh, scene, mesh->Vertices, mesh->Indices, subset, mesh->BoneNames, mesh->BoneOffsets);
                mesh->Subsets.push_back(subset);
            }
            //OptimizeVertices(mesh->Vertices, mesh->Indices); 
            mesh->Setup(); 
        }
        if (anyStatic)
        {
            shared_ptr<Mesh> mesh_ptr = make_shared<Mesh>(); 

            model->Meshs.push_back(mesh_ptr); 
            mesh_ptr->Name = node->mName.C_Str();
            mesh_ptr->Mat.resize(scene->mNumMaterials);
            for (UINT i = 0; i < scene->mNumMaterials; ++i)
            {
                aiMaterial* mat = scene->mMaterials[i];
                aiString name;

                mesh_ptr->Mat[i].Ambient = XMFLOAT4(0.2f, 0.2f, 0.2f, 1.0f);
                mesh_ptr->Mat[i].Diffuse = XMFLOAT4(0.8f, 0.8f, 0.8f, 1.0f);
                mesh_ptr->Mat[i].Specular = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
                mesh_ptr->Mat[i].Reflect = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);

                aiColor3D color(0.f, 0.f, 0.f);
                if (AI_SUCCESS == mat->Get(AI_MATKEY_COLOR_AMBIENT, color))
                    mesh_ptr->Mat[i].Ambient = XMFLOAT4(color.r, color.g, color.b, 1.0f);
                if (AI_SUCCESS == mat->Get(AI_MATKEY_COLOR_DIFFUSE, color))
                    mesh_ptr->Mat[i].Diffuse = XMFLOAT4(color.r, color.g, color.b, 1.0f);
                if (AI_SUCCESS == mat->Get(AI_MATKEY_COLOR_SPECULAR, color))
                    mesh_ptr->Mat[i].Specular = XMFLOAT4(color.r, color.g, color.b, 1.0f);
            }

            for (uint32 i = 0; i < node->mNumMeshes; ++i) 
            {
                uint32 index = node->mMeshes[i];
                aiMesh* aiMesh = scene->mMeshes[index]; 
                if (aiMesh->HasBones())
                    continue;   // 스킨 메시는 위에서 처리

                MeshGeometry::Subset subset; 
                ProcessMesh(aiMesh, scene, mesh_ptr->Vertices, mesh_ptr->Indices, subset); 
                mesh_ptr->Subsets.push_back(subset); 
            }
            mesh_ptr->Setup(); 
        }
    }

    for (UINT i = 0; i < node->mNumChildren; ++i)
    {
        ParsingMeshNode(node->mChildren[i], scene, model);
    }
}

void FBXLoader::ProcessMesh(
    aiMesh* mesh,
    const aiScene* scene,
    vector<Vertex::PosNormalTexTan2>& vertices,
    vector<USHORT>& indices,
    MeshGeometry::Subset& subset)
{
    subset.Name = mesh->mName.C_Str();
    subset.VertexStart = vertices.size();
    subset.FaceStart = indices.size() / 3;
    subset.VertexCount = mesh->mNumVertices;
    subset.FaceCount = mesh->mNumFaces;
    subset.MaterialIndex = mesh->mMaterialIndex;


    for (UINT i = 0; i < mesh->mNumVertices; ++i)
    {
        Vertex::PosNormalTexTan2 vertex;
        const aiVector3D p = ToEngine(mesh->mVertices[i]);
        vertex.pos = XMFLOAT3(p.x, p.y, p.z);

        // Normal
        if (mesh->HasNormals())
        {
            const aiVector3D n = ToEngine(mesh->mNormals[i]);
            vertex.normal = XMFLOAT3(n.x, n.y, n.z);
        }

        // UV
        if (mesh->HasTextureCoords(0))
            ::memcpy(&vertex.tex, &mesh->mTextureCoords[0][i], sizeof(Vec2));

        //vertex.tangentU = XMFLOAT4(mesh->mTangents[i].x, mesh->mTangents[i].y, mesh->mTangents[i].z, 1.0f);

        vertices.push_back(vertex);
    }

    for (UINT i = 0; i < mesh->mNumFaces; ++i)
    {
        aiFace face = mesh->mFaces[i];
        for (UINT j = 0; j < face.mNumIndices; ++j)
        {
            indices.push_back(face.mIndices[j]);
        }
    }
}

void FBXLoader::ProcessMeshSkinned(
    aiMesh* mesh,
    const aiScene* scene,
    vector<Vertex::PosNormalTexTanSkinned>& vertices,
    vector<USHORT>& indices,
    MeshGeometry::Subset& subset,
    vector<string>& boneNames, vector<XMFLOAT4X4>& boneOffsets)
{
    subset.Name = mesh->mName.C_Str();
    subset.VertexStart = (UINT)vertices.size();
    subset.FaceStart = (UINT)(indices.size() / 3);
    subset.VertexCount = mesh->mNumVertices;
    subset.FaceCount = mesh->mNumFaces;
    subset.MaterialIndex = mesh->mMaterialIndex;

    const size_t startVertex = vertices.size();
    for (uint32 i = 0; i < mesh->mNumVertices; ++i)
    {
        Vertex::PosNormalTexTanSkinned vertex = {};
        const aiVector3D p = ToEngine(mesh->mVertices[i]);
        vertex.pos = XMFLOAT3(p.x, p.y, p.z);
        if (mesh->HasNormals())
        {
            const aiVector3D n = ToEngine(mesh->mNormals[i]);
            vertex.normal = XMFLOAT3(n.x, n.y, n.z);
        }
        if (mesh->HasTangentsAndBitangents())
        {
            const aiVector3D t = ToEngine(mesh->mTangents[i]);
            vertex.tangentU = XMFLOAT4(t.x, t.y, t.z, 1.0f);
        }
        if (mesh->HasTextureCoords(0))
            vertex.tex = XMFLOAT2(mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y);
        vertices.push_back(vertex);
    }

    // 정점별 (본, 가중치) 모으기 → 큰 것 4 개만 남기고 합이 1 이 되게 정규화
    std::vector<std::vector<std::pair<float, int>>> influences(mesh->mNumVertices);
    for (UINT b = 0; b < mesh->mNumBones; ++b)
    {
        const aiBone* bone = mesh->mBones[b];
        const std::string name = bone->mName.C_Str();
        int palette = -1;
        for (size_t k = 0; k < boneNames.size(); ++k)
            if (boneNames[k] == name) { palette = (int)k; break; }
        if (palette < 0)
        {
            palette = (int)boneNames.size();
            boneNames.push_back(name);
            boneOffsets.push_back(ToEngineMatrix(bone->mOffsetMatrix));
        }
        for (UINT w = 0; w < bone->mNumWeights; ++w)
        {
            const aiVertexWeight& vw = bone->mWeights[w];
            if (vw.mVertexId < mesh->mNumVertices && vw.mWeight > 0.0f)
                influences[vw.mVertexId].push_back({ vw.mWeight, palette });
        }
    }
    for (uint32 i = 0; i < mesh->mNumVertices; ++i)
    {
        auto& inf = influences[i];
        std::sort(inf.begin(), inf.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        if (inf.size() > 4) inf.resize(4);
        float sum = 0.0f;
        for (const auto& p : inf) sum += p.first;
        Vertex::PosNormalTexTanSkinned& v = vertices[startVertex + i];
        float wts[4] = { 0, 0, 0, 0 };
        for (size_t k = 0; k < inf.size(); ++k)
        {
            wts[k] = sum > 0.0f ? inf[k].first / sum : 0.0f;
            v.boneIndices[k] = (BYTE)(std::min)(inf[k].second, 255);
        }
        if (inf.empty())
            wts[0] = 1.0f;   // 가중치 없는 정점은 팔레트 0 번에 고정
        v.weights = XMFLOAT3(wts[0], wts[1], wts[2]);   // 네 번째 가중치 = 1 - 합 (셰이더)
    }

    for (UINT i = 0; i < mesh->mNumFaces; ++i)
    {
        const aiFace& face = mesh->mFaces[i];
        for (UINT j = 0; j < face.mNumIndices; ++j)
            indices.push_back((USHORT)face.mIndices[j]);
    }
}

void FBXLoader::ParseBonesFromNodes(aiNode* node, std::map<std::string, int>& boneMapping)
{
    std::string nodeName(node->mName.C_Str());

    if (boneMapping.find(nodeName) == boneMapping.end())
    {
        int boneIndex = static_cast<int>(boneMapping.size());  
        boneMapping[nodeName] = boneIndex;
    }

    for (unsigned int i = 0; i < node->mNumChildren; ++i)
    {
        ParseBonesFromNodes(node->mChildren[i], boneMapping); 
    }
}

void FBXLoader::ParseBoneOffsets(const aiScene* scene, const std::map<std::string, int>& boneMapping, std::vector<XMFLOAT4X4>& boneOffsets)
{
    boneOffsets.resize(boneMapping.size(), XMFLOAT4X4()); // �⺻ ���� ��ķ� �ʱ�ȭ

    // ��� ���� ���� ������ ��� ����
    for (const auto& [boneName, boneIndex] : boneMapping)
    {
        aiNode* boneNode = scene->mRootNode->FindNode(boneName.c_str());
        if (boneNode)
        {
            boneOffsets[boneIndex] = ToEngineMatrix(boneNode->mTransformation);
        }
        else
        {
            // �� ��尡 ���ٸ� �⺻ ���� ��� ���
            XMStoreFloat4x4(&boneOffsets[boneIndex], XMMatrixIdentity());
        }
    }
}

void FBXLoader::ParseBoneHierarchy(aiNode* node, std::map<std::string, int>& boneMapping, std::vector<int>& boneHierarchy, int parentIndex)
{
    // ���� ��尡 ������ Ȯ�� 
    auto it = boneMapping.find(node->mName.C_Str()); 
    int currentIndex = it != boneMapping.end() ? it->second : -1; 

    // ���� Ȯ�εǸ� �θ�-�ڽ� ���� ����
    if (currentIndex != -1) 
    {
        boneHierarchy[currentIndex] = parentIndex; 
    }

    // �ڽ� ��忡 ���� ��������� ȣ��
    for (unsigned int i = 0; i < node->mNumChildren; ++i) 
    {
        ParseBoneHierarchy(node->mChildren[i], boneMapping, boneHierarchy, currentIndex); 
    }
}
