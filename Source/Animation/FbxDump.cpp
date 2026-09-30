#include "pch.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/config.h>

// (개발/검증용) FBX 구조를 텍스트로 기록한다: 노드 계층, 메시/본, 애니메이션 채널.
// NOVA_FBX_DUMP=<fbx 경로>;<fbx 경로>...  NOVA_FBX_DUMP_LOG=<로그 파일>
namespace FbxDump
{
	static void DumpNode(FILE* fp, const aiNode* node, int depth, int& count)
	{
		++count;
		if (depth < 12)
		{
			const aiMatrix4x4& m = node->mTransformation;
			aiVector3D sc, ps;
			aiQuaternion rq;
			m.Decompose(sc, rq, ps);
			fprintf(fp, "%*s%s  meshes=%u  T(%.3f %.3f %.3f) Q(w %.3f x %.3f y %.3f z %.3f) S(%.2f)\n", depth * 2, "", node->mName.C_Str(), node->mNumMeshes, m.a4, m.b4, m.c4, rq.w, rq.x, rq.y, rq.z, sc.x);
		}
		for (unsigned i = 0; i < node->mNumChildren; ++i)
			DumpNode(fp, node->mChildren[i], depth + 1, count);
	}

	void Run()
	{
		char list[2048] = {}, logPath[512] = {};
		if (::GetEnvironmentVariableA("NOVA_FBX_DUMP", list, sizeof(list)) == 0 || ::GetEnvironmentVariableA("NOVA_FBX_DUMP_LOG", logPath, sizeof(logPath)) == 0)
			return;
		FILE* fp = nullptr;
		fopen_s(&fp, logPath, "w");
		if (fp == nullptr)
			return;

		std::string all = list;
		size_t pos = 0;
		while (pos < all.size())
		{
			size_t next = all.find(';', pos);
			std::string path = all.substr(pos, next == std::string::npos ? std::string::npos : next - pos);
			pos = next == std::string::npos ? all.size() : next + 1;

			Assimp::Importer importer;
			importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, ::GetEnvironmentVariableA("NOVA_FBX_DUMP_PIVOTS", nullptr, 0) > 0);
			const aiScene* scene = importer.ReadFile(path, aiProcess_ConvertToLeftHanded | aiProcess_Triangulate | aiProcess_LimitBoneWeights);
			fprintf(fp, "===== %s\n", path.c_str());
			if (scene == nullptr)
			{
				fprintf(fp, "  load failed: %s\n", importer.GetErrorString());
				continue;
			}

			if (scene->mMetaData)
			{
				for (unsigned m = 0; m < scene->mMetaData->mNumProperties; ++m)
				{
					const aiString& key = scene->mMetaData->mKeys[m];
					const aiMetadataEntry& e = scene->mMetaData->mValues[m];
					std::string k = key.C_Str();
					if (k.find("Axis") == std::string::npos && k.find("Unit") == std::string::npos && k.find("Frame") == std::string::npos)
						continue;
					if (e.mType == AI_INT32) fprintf(fp, "  meta %s = %d\n", k.c_str(), *(int*)e.mData);
					else if (e.mType == AI_FLOAT) fprintf(fp, "  meta %s = %f\n", k.c_str(), *(float*)e.mData);
					else if (e.mType == AI_DOUBLE) fprintf(fp, "  meta %s = %f\n", k.c_str(), *(double*)e.mData);
					else fprintf(fp, "  meta %s (type %d)\n", k.c_str(), (int)e.mType);
				}
			}
			int nodeCount = 0;
			DumpNode(fp, scene->mRootNode, 0, nodeCount);
			fprintf(fp, "  nodes=%d meshes=%u materials=%u animations=%u\n", nodeCount, scene->mNumMeshes, scene->mNumMaterials, scene->mNumAnimations);
			for (unsigned i = 0; i < scene->mNumMeshes; ++i)
			{
				const aiMesh* m = scene->mMeshes[i];
				aiVector3D mn(1e9f, 1e9f, 1e9f), mx(-1e9f, -1e9f, -1e9f);
				for (unsigned v = 0; v < m->mNumVertices; ++v)
				{
					mn.x = (std::min)(mn.x, m->mVertices[v].x); mn.y = (std::min)(mn.y, m->mVertices[v].y); mn.z = (std::min)(mn.z, m->mVertices[v].z);
					mx.x = (std::max)(mx.x, m->mVertices[v].x); mx.y = (std::max)(mx.y, m->mVertices[v].y); mx.z = (std::max)(mx.z, m->mVertices[v].z);
				}
				fprintf(fp, "  mesh[%u] %s verts=%u faces=%u bones=%u mat=%u  bounds(%.2f %.2f %.2f)-(%.2f %.2f %.2f)\n", i, m->mName.C_Str(), m->mNumVertices, m->mNumFaces, m->mNumBones, m->mMaterialIndex,
					mn.x, mn.y, mn.z, mx.x, mx.y, mx.z);
			}
			for (unsigned a = 0; a < scene->mNumAnimations; ++a)
			{
				const aiAnimation* an = scene->mAnimations[a];
				fprintf(fp, "  anim[%u] '%s' duration=%.2f ticks/s=%.2f channels=%u\n", a, an->mName.C_Str(), an->mDuration, an->mTicksPerSecond, an->mNumChannels);
				for (unsigned c = 0; c < an->mNumChannels && c < 6; ++c)
				{
					const aiNodeAnim* ch = an->mChannels[c];
					fprintf(fp, "    ch %s pos=%u rot=%u scl=%u\n", ch->mNodeName.C_Str(), ch->mNumPositionKeys, ch->mNumRotationKeys, ch->mNumScalingKeys);
				}
			}

			// 채널 첫 키 vs 같은 파일의 노드 변환 (분해) 비교
			for (unsigned a = 0; a < scene->mNumAnimations; ++a)
			{
				const aiAnimation* an = scene->mAnimations[a];
				for (unsigned c = 0; c < an->mNumChannels; ++c)
				{
					const aiNodeAnim* ch = an->mChannels[c];
					const std::string nm = ch->mNodeName.C_Str();
					if (nm != "root" && nm != "pelvis" && nm != "spine_01" && nm != "Bip001" && nm != "Bip001 Spine")
						continue;
					const aiNode* node = scene->mRootNode->FindNode(ch->mNodeName);
					aiVector3D s, p; aiQuaternion q;
					if (node) node->mTransformation.Decompose(s, q, p);
					const aiQuaternion& k = ch->mRotationKeys[0].mValue;
					const aiVector3D& kp = ch->mPositionKeys[0].mValue;
					fprintf(fp, "    Q %-12s node(w %.3f x %.3f y %.3f z %.3f) T(%.2f %.2f %.2f) | key0(w %.3f x %.3f y %.3f z %.3f) T(%.2f %.2f %.2f) t0=%.2f\n", nm.c_str(),
						q.w, q.x, q.y, q.z, p.x, p.y, p.z, k.w, k.x, k.y, k.z, kp.x, kp.y, kp.z, ch->mRotationKeys[0].mTime);
				}
			}
			fflush(fp);
		}
		fclose(fp);
	}
}

void FbxDumpRun() { FbxDump::Run(); }
