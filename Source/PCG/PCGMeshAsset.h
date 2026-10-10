#pragma once
#include <memory>
#include <string>
#include <vector>

class Mesh;
class UMaterial;

// PCG 스포너가 놓는 모델 (FBX · GLB): 노드 이름 _LOD0 · _LOD1 … 이 LOD (Unity 프리팹 · SpeedTree 식), 없으면 LOD 하나.
//  모델 파일의 노드 변환 · 단위 (cm → m) 를 메시마다 담는다 (Model Placement 와 같은 결과). 재질 = FBX 재질 칸 (<파일>_FBX.Materials)
//  LOD 전환 화면 높이 = 모델 옆 "<파일>.lod.json" ([LOD0, LOD1, …], Unity 의 Screen Relative Transition Height) 또는 기본 (반씩)
namespace PCG
{
	struct MeshPart
	{
		std::shared_ptr<Mesh> MeshPtr;
		XMFLOAT4X4 Model;                                   // 메시 → 모델 루트 (노드 변환 · 단위)
		std::vector<std::shared_ptr<UMaterial>> Materials;  // 서브셋마다
		std::vector<bool> TwoSided;                         // 서브셋마다 (Alpha Clipping = 잎 — 뒷면도)
	};
	struct MeshLevel
	{
		std::vector<MeshPart> Parts;
		float ScreenHeight = 0.0f;   // 화면 높이 비율이 이 이상이면 이 LOD
		int Triangles = 0;
	};
	struct MeshAsset
	{
		std::string Path;
		std::vector<MeshLevel> Levels;
		float Height = 1.0f;         // 모델 높이 (m) — 화면 높이 계산
		float Radius = 1.0f;         // 바닥 반지름 (m)
		bool Failed = false;
		// 읽기 단계: 0 = 텍스처 · 모델 캐시를 작업 스레드에서 미리 (Utils::PrefetchTexture), 1 = 다 읽음
		int Stage = 0;
		uint32_t RequestFrame = 0;
		std::vector<std::wstring> Pending;   // 미리 디코드 중인 텍스처 (디스크 경로)
	};

	// 메인 스레드. 처음 부르면 텍스처 디코드 · 모델 캐시 읽기를 작업 스레드에 맡기고 nullptr, 끝나면 (allowLoad 일 때) GPU 로 올려 돌려준다.
	//  loaded = 이번에 실제로 읽었나 (프레임마다 몇 개만 — 부른 쪽 예산)
	MeshAsset* GetMeshAsset(const std::string& path, bool allowLoad, bool* loaded = nullptr);
	int LoadedMeshAssets();
	void ClearMeshAssets();
}
