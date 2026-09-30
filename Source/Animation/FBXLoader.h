#pragma once
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include "Vertex.h"
#include "MeshGeometry.h"
#include "SkinnedData.h"

class MeshFile;

struct FbxMaterial
{
    Material Mat;

    XMFLOAT3 ambient;
    XMFLOAT3 diffuse;
    XMFLOAT3 specular;
    XMFLOAT3 reflect;
    float shininess;

    wstring DiffuseMapName;
    wstring NormalMapName;
};

struct Subset
{
    UINT id;
    UINT vertexStart;
    UINT vertexCount;
    UINT faceStart;
    UINT faceCount;
};

class FBXLoader
{
public:
    bool LoadModelFbx( 
        const std::string& filename, 
        MeshFile* skinnedModel);

    bool LoadSkeletonAvata(
        const std::string& filename,
        vector<shared_ptr<SkeletonAvataData>>& skeletones);

    bool LoadAnimation(
        const std::string& filename,
        SkinnedData& skinnedData); 

private:
    void ParsingMeshNode(
        aiNode* node,
        const aiScene* scene,
        MeshFile* model); 

    void ProcessMesh(
        aiMesh* mesh,
        const aiScene* scene,
        vector<Vertex::PosNormalTexTan2>& vertices,
        vector<USHORT>& indices, MeshGeometry::Subset& subset);

    void ProcessMeshSkinned(
        aiMesh* mesh,
        const aiScene* scene,
        vector<Vertex::PosNormalTexTanSkinned>& vertices, 
        vector<USHORT>& indices, MeshGeometry::Subset& subset);

    void ParseBonesFromNodes(aiNode* node, std::map<std::string, int>& boneMapping); 
    void ParseBoneOffsets(const aiScene* scene, const std::map<std::string, int>& boneMapping, std::vector<XMFLOAT4X4>& boneOffsets);
    void ParseBoneHierarchy(aiNode* node, std::map<std::string, int>& boneMapping, std::vector<int>& boneHierarchy, int parentIndex);
};