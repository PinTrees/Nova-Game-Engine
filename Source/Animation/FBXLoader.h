#pragma once
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/config.h>

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
    float Scale = 1.0f;   // Import Settings 의 Scale Factor (정적 메시 정점 · 스켈레톤 UnitScale 에 곱한다)

    bool LoadModelFbx( 
        const std::string& filename, 
        MeshFile* skinnedModel);

    bool LoadSkeletonAvata(
        const std::string& filename,
        vector<shared_ptr<SkeletonAvataData>>& skeletones);

    bool LoadAnimation(
        const std::string& filename,
        SkinnedData& skinnedData);

    // FBX 재질 → <파일 이름>_FBX.Materials/<재질 이름>.mat (Unity 의 Extract Materials: Diffuse 색 · 그림 · 발광 · 광택, 이미 있으면 그대로 — 사용자가 고친 값을 지키게).
    //  (_FBX = 같은 이름의 VRM 이 쓰는 <이름>.Materials 와 겹치지 않게) 돌려주는 목록의 번호 = 메시 Subset 의 MaterialIndex. assetPath = 프로젝트 기준 (Assets\...) — 프로젝트 밖 (엔진 Resources 등) 은 빈 목록
    static std::vector<std::wstring> ExtractMaterials(const std::wstring& assetPath);

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

    // 스킨 메시: 정점의 본 인덱스는 SkinnedMesh 의 본 팔레트(boneNames) 인덱스
    void ProcessMeshSkinned(
        aiMesh* mesh,
        const aiScene* scene,
        vector<Vertex::PosNormalTexTanSkinned>& vertices,
        vector<USHORT>& indices, MeshGeometry::Subset& subset,
        vector<string>& boneNames, vector<XMFLOAT4X4>& boneOffsets, vector<BlendShapeData>* blendShapes = nullptr);

    void ParseBonesFromNodes(aiNode* node, std::map<std::string, int>& boneMapping); 
    void ParseBoneOffsets(const aiScene* scene, const std::map<std::string, int>& boneMapping, std::vector<XMFLOAT4X4>& boneOffsets);
    void ParseBoneHierarchy(aiNode* node, std::map<std::string, int>& boneMapping, std::vector<int>& boneHierarchy, int parentIndex);
};