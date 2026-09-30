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


// Áßº¹µÈ Á¤Á¡ Á¦°ÅÇÏ´Â ÇÔ¼ö
void OptimizeVertices(std::vector<Vertex::PosNormalTexTanSkinned>& vertices, std::vector<USHORT>& indices)
{
    // Á¤Á¡ Áßº¹ Ã¼Å©¸¦ À§ÇÑ ¸Ê (VertexKey, UINT)
    std::unordered_map<VertexKey, UINT, VertexKeyHasher> uniqueVertices;
    std::vector<Vertex::PosNormalTexTanSkinned> optimizedVertices;
    std::vector<USHORT> optimizedIndices;

    for (const auto& index : indices)
    {
        // ÇöÀç ÀÎµ¦½ºÀÇ Á¤Á¡ °¡Á®¿À±â
        Vertex::PosNormalTexTanSkinned& vertex = vertices[index];

        // VertexKey »ı¼º - ºñ±³¸¦ À§ÇÑ ÁÖ¿ä ÇÊµå Æ÷ÇÔ
        VertexKey key;
        key.pos = vertex.pos;
        key.normal = vertex.normal;
        key.tex = vertex.tex;
        key.tangent = vertex.tangentU;
        //std::memcpy(key.color, vertex.color, sizeof(BYTE) * 4);  // color º¹»ç

        // Á¤Á¡À» Ã£°Å³ª, Ã£Áö ¸øÇÏ¸é »õ·Î Ãß°¡
        auto it = uniqueVertices.find(key);
        if (it == uniqueVertices.end())
        {
            // Á¤Á¡ÀÌ ¾ø´Ù¸é Ãß°¡ÇÏ°í, »õ·Î¿î ÀÎµ¦½º¸¦ ºÎ¿©
            UINT newIndex = static_cast<UINT>(optimizedVertices.size());
            uniqueVertices[key] = newIndex;
            optimizedVertices.push_back(vertex);
            optimizedIndices.push_back(newIndex);
        }
        else
        {
            // ÀÌ¹Ì ÀÖ´Â Á¤Á¡ÀÌ¶ó¸é ÇØ´ç ÀÎµ¦½º »ç¿ë
            optimizedIndices.push_back(it->second);
        }
    }

    // ÃÖÀûÈ­µÈ Á¤Á¡°ú ÀÎµ¦½º ¸®½ºÆ®·Î ±³Ã¼
    vertices = optimizedVertices;
    indices = optimizedIndices;
}


// ëª¨ë¸/ìŠ¤ì¼ˆë ˆí†¤/ì• ë‹ˆë©”ì´ì…˜ ëª¨ë‘ ê°™ì€ ì„¤ì •ìœ¼ë¡œ ì½ì–´ì•¼ ë…¸ë“œ ì´ë¦„Â·ì¢Œí‘œê³„ê°€ ì¼ì¹˜í•œë‹¤.
//  - ì™¼ì† ì¢Œí‘œê³„(DirectX)ë¡œ ë³€í™˜, ì‚¼ê°í˜•í™”, ì •ì  ê°€ì¤‘ì¹˜ ìµœëŒ€ 4 ê°œ
//  - 16 ë¹„íŠ¸ ì¸ë±ìŠ¤ë¥¼ ì“°ë¯€ë¡œ ë©”ì‹œë¥¼ 65000 ì •ì  ì´í•˜ë¡œ ë‚˜ëˆˆë‹¤
//  - FBX í”¼ë²— ë³´ì¡° ë…¸ë“œ($AssimpFbx$)ë¥¼ ë§Œë“¤ì§€ ì•ŠëŠ”ë‹¤: ë…¸ë“œ ë³€í™˜ê³¼ ì• ë‹ˆë©”ì´ì…˜ í‚¤ê°€ ê°™ì€ ê°’(PreRotation í¬í•¨)ì´ ë˜ì–´ ì´ë¦„ìœ¼ë¡œ ë°”ë¡œ ì—°ê²°ëœë‹¤
static const aiScene* ReadFbxScene(Assimp::Importer& importer, const std::string& path, bool withMeshes)
{
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);   // PreRotation ë“±ì„ ë…¸ë“œ ë³€í™˜Â·ì• ë‹ˆë©”ì´ì…˜ í‚¤ì— í•©ì³ ë„£ëŠ”ë‹¤
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
    // Assimp ëŠ” ì—´ ë²¡í„° ê·œì•½ â†’ DirectX(í–‰ ë²¡í„°) ë¡œ ì „ì¹˜
    XMFLOAT4X4 r;
    XMStoreFloat4x4(&r, XMMatrixTranspose(XMLoadFloat4x4(reinterpret_cast<const XMFLOAT4X4*>(&m))));
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

// ë…¸ë“œ ê³„ì¸µ ì „ì²´ë¥¼ ìŠ¤ì¼ˆë ˆí†¤ìœ¼ë¡œ (ê¹Šì´ ìš°ì„  â†’ ë¶€ëª¨ ì¸ë±ìŠ¤ê°€ í•­ìƒ ìì‹ë³´ë‹¤ ì‘ë‹¤)
static void CollectNodes(const aiNode* node, int parent, SkeletonAvataData& skel)
{
    const int index = (int)skel.NodeNames.size();
    skel.NodeNames.push_back(node->mName.C_Str());
    skel.BoneHierarchy.push_back(parent);
    skel.BindLocal.push_back(ToRowMajor(node->mTransformation));
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

    // íŒŒì¼ ë‹¨ìœ„ â†’ ë¯¸í„° (Unity ì˜ Convert Units). FBX UnitScaleFactor ëŠ” cm ê¸°ì¤€ (cm = 1, m = 100)
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

// í¬í•¨í•œ Assimp í—¤ë”ì™€ DLL ì˜ í‚¤ êµ¬ì¡°ì²´ í¬ê¸°ê°€ ë‹¤ë¥¼ ìˆ˜ ìˆë‹¤ (DLL ìª½ aiQuatKey ì— mInterpolation ì´ ìˆì–´ 32 ë°”ì´íŠ¸).
// í‚¤ ì‹œê°„ì´ ìœ í•œí•˜ê³  ì¦ê°€í•˜ë©°, ì¿¼í„°ë‹ˆì–¸ ê¸¸ì´ê°€ 1 ì¸ ê°„ê²©ì„ ì°¾ì•„ ê·¸ ê°„ê²©ìœ¼ë¡œ ì½ëŠ”ë‹¤.
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
        // "Take 001" ê°™ì€ ì´ë¦„ ëŒ€ì‹  "íŒŒì¼ëª…|í´ë¦½ëª…" í˜•íƒœì˜ ì•ë¶€ë¶„ ì •ë¦¬
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
                channel.Positions.push_back({ (float)(key.mTime / tps), XMFLOAT3(key.mValue.x, key.mValue.y, key.mValue.z) });
            }
            for (unsigned k = 0; k < ch->mNumRotationKeys; ++k)
            {
                const auto& key = KeyAt(ch->mRotationKeys, rotStride, k);
                channel.Rotations.push_back({ (float)(key.mTime / tps), XMFLOAT4(key.mValue.x, key.mValue.y, key.mValue.z, key.mValue.w) });
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
                    continue;   // ë³¸ ì—†ëŠ” ë©”ì‹œëŠ” ì•„ë˜ì—ì„œ ì •ì  ë©”ì‹œë¡œ

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
                    continue;   // ìŠ¤í‚¨ ë©”ì‹œëŠ” ìœ„ì—ì„œ ì²˜ë¦¬

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
        vertex.pos = XMFLOAT3(mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z);

        // Normal
        if (mesh->HasNormals())
            ::memcpy(&vertex.normal, &mesh->mNormals[i], sizeof(XMFLOAT3));  

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
        vertex.pos = XMFLOAT3(mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z);
        if (mesh->HasNormals())
            vertex.normal = XMFLOAT3(mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z);
        if (mesh->HasTangentsAndBitangents())
            vertex.tangentU = XMFLOAT4(mesh->mTangents[i].x, mesh->mTangents[i].y, mesh->mTangents[i].z, 1.0f);
        if (mesh->HasTextureCoords(0))
            vertex.tex = XMFLOAT2(mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y);
        vertices.push_back(vertex);
    }

    // ì •ì ë³„ (ë³¸, ê°€ì¤‘ì¹˜) ëª¨ìœ¼ê¸° â†’ í° ê²ƒ 4 ê°œë§Œ ë‚¨ê¸°ê³  í•©ì´ 1 ì´ ë˜ê²Œ ì •ê·œí™”
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
            boneOffsets.push_back(ToRowMajor(bone->mOffsetMatrix));
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
            wts[0] = 1.0f;   // ê°€ì¤‘ì¹˜ ì—†ëŠ” ì •ì ì€ íŒ”ë ˆíŠ¸ 0 ë²ˆì— ê³ ì •
        v.weights = XMFLOAT3(wts[0], wts[1], wts[2]);   // ë„¤ ë²ˆì§¸ ê°€ì¤‘ì¹˜ = 1 - í•© (ì…°ì´ë”)
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
    boneOffsets.resize(boneMapping.size(), XMFLOAT4X4()); // ±âº» ´ÜÀ§ Çà·Ä·Î ÃÊ±âÈ­

    // ¸ğµç º»¿¡ ´ëÇØ ¿ÀÇÁ¼Â Çà·Ä ¼³Á¤
    for (const auto& [boneName, boneIndex] : boneMapping)
    {
        aiNode* boneNode = scene->mRootNode->FindNode(boneName.c_str());
        if (boneNode)
        {
            aiMatrix4x4 offsetMatrix = boneNode->mTransformation;
            XMMATRIX mat = XMLoadFloat4x4(reinterpret_cast<XMFLOAT4X4*>(&offsetMatrix));

            // Çà·Ä ÀüÈ¯ ÈÄ ÀúÀå
            XMStoreFloat4x4(&boneOffsets[boneIndex], XMMatrixTranspose(mat));
        }
        else
        {
            // º» ³ëµå°¡ ¾ø´Ù¸é ±âº» ´ÜÀ§ Çà·Ä »ç¿ë
            XMStoreFloat4x4(&boneOffsets[boneIndex], XMMatrixIdentity());
        }
    }
}

void FBXLoader::ParseBoneHierarchy(aiNode* node, std::map<std::string, int>& boneMapping, std::vector<int>& boneHierarchy, int parentIndex)
{
    // ÇöÀç ³ëµå°¡ º»ÀÎÁö È®ÀÎ 
    auto it = boneMapping.find(node->mName.C_Str()); 
    int currentIndex = it != boneMapping.end() ? it->second : -1; 

    // º»ÀÌ È®ÀÎµÇ¸é ºÎ¸ğ-ÀÚ½Ä °ü°è ¼³Á¤
    if (currentIndex != -1) 
    {
        boneHierarchy[currentIndex] = parentIndex; 
    }

    // ÀÚ½Ä ³ëµå¿¡ ´ëÇØ Àç±ÍÀûÀ¸·Î È£Ãâ
    for (unsigned int i = 0; i < node->mNumChildren; ++i) 
    {
        ParseBoneHierarchy(node->mChildren[i], boneMapping, boneHierarchy, currentIndex); 
    }
}
