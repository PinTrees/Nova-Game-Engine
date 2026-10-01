#pragma once
#include <string>
#include <vector>
#include <memory>
#include "TreeDesc.h"
#include "DetailPrototype.h"
#include "TerrainGenSettings.h"

// Unity 의 TerrainLayer 에셋 (.terrainlayer, JSON): 지형에 칠하는 텍스처 한 장과 타일 크기
class TerrainLayer
{
public:
	std::string Path;              // 에셋 경로 ("Assets\\..." 또는 엔진 "Resources\\...")
	std::string DiffusePath;       // Diffuse 텍스처 경로
	Vec2 TileSize = Vec2(15.0f, 15.0f);
	Vec2 TileOffset = Vec2(0.0f, 0.0f);
	XMFLOAT4 Tint = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);

	std::string Name() const;
	GfxShaderResourceView* DiffuseSRV();
	void SetDiffuse(const std::string& path);
	bool Save() const;

	static std::shared_ptr<TerrainLayer> Load(const std::string& path);
	static std::shared_ptr<TerrainLayer> Create(const std::string& path, const std::string& diffusePath);
	// 엔진 패키지의 기본 레이어 (Resources/Packages/Terrain/Layers/*.terrainlayer)
	static std::vector<std::string> ListAvailable();

private:
	ComPtr<GfxShaderResourceView> m_Diffuse;
	bool m_DiffuseLoaded = false;
};

// 지형에 칠한 나무 한 그루 (Unity 의 TreeInstance). 위치는 지형 기준 0~1, 높이는 그릴 때 지형에서 읽는다
struct TerrainTreeInstance
{
	float X = 0.0f, Z = 0.0f;
	float HeightScale = 1.0f, WidthScale = 1.0f;
	float Rotation = 0.0f;      // 라디안 (Y 축)
	float Tint = 0.5f;          // 색 변화 (0.5 = 그대로)
	int32_t Prototype = 0;      // TreePrototypes 번호
};
static_assert(sizeof(TerrainTreeInstance) == 28, "저장 형식");

// Unity 의 TerrainData 에셋 (.terraindata, 바이너리).
//  - 높이맵: (해상도 x 해상도) 정규화 높이 0~1, 실제 높이 = 값 * Size.y. 인덱스 [z * 해상도 + x].
//  - 컨트롤(스플랫) 맵: 레이어 4개의 가중치 (RGBA8).
//  - LOD 쿼드트리: 깊이 0 = 지형 전체 한 노드, 깊이가 1 늘 때마다 4등분. 모든 노드는 같은 격자(NodeGrid 칸)로 그려지므로
//    얕은(큰) 노드일수록 높이맵을 성기게 샘플링한다. 노드마다 높이 범위와 기하 오차(그 간격으로 그렸을 때 원래 높이와의 최대 차)를 가진다.
class TerrainData
{
public:
	static constexpr int kMaxLayers = 4;
	static constexpr int kNodeGrid = 32;   // 노드 한 변의 칸 수 (정점 33개)

	struct Node
	{
		float MinHeight = 0.0f;     // 미터 (지형 로컬)
		float MaxHeight = 0.0f;
		float Error = 0.0f;         // 미터. 자식들의 오차를 포함하므로 깊이가 얕을수록 크다
	};

	std::string Path;
	int HeightmapResolution = 513;
	Vec3 Size = Vec3(1000.0f, 600.0f, 1000.0f);   // Unity 기본 지형 크기
	std::vector<float> Heights;
	int ControlResolution = 512;
	std::vector<uint8_t> Control;                 // RGBA
	std::vector<std::shared_ptr<TerrainLayer>> Layers;

	// 나무 (Paint Trees): 프로토타입 = 나무 한 종류의 설정, 인스턴스 = 칠한 나무 한 그루
	std::vector<TreeDesc> TreePrototypes;
	std::vector<TerrainTreeInstance> TreeInstances;
	unsigned TreeRevision = 0;  // 나무가 바뀔 때마다 증가 (TreeRenderer 의 위치 캐시)
	void OnTreesChanged() { ++TreeRevision; Dirty = true; }

	// 디테일 (Paint Details): 프로토타입 = 풀·꽃·돌 한 종류, 종류마다 밀도 맵 (DetailResolution², 0~255, 인덱스 [z * 해상도 + x]).
	//  DetailRenderer 가 밀도만큼 덩어리를 흩뿌린다 (카메라 근처 조각만, 결과는 캐시)
	std::vector<DetailPrototype> DetailPrototypes;
	std::vector<std::vector<uint8_t>> DetailDensity;
	int DetailResolution = 512;
	DetailSettings Details;
	unsigned DetailRevision = 0;   // 밀도·프로토타입·설정이 바뀔 때마다 증가
	void OnDetailsChanged() { ++DetailRevision; Dirty = true; }
	void AddDetailPrototype(const DetailPrototype& proto);   // 빈 밀도 맵과 함께
	void RemoveDetailPrototype(int index);
	void SetDetailResolution(int resolution);                // 기존 밀도를 보간해 옮긴다
	float GetDetailDensity(int proto, float x, float z) const;   // 지형 로컬(m), 쌍선형 0~1
	float GetLayerWeight(uint32_t layerMask, float x, float z) const;   // 레이어 마스크의 컨트롤 비중 합 (0~1)

	// 지형 생성기 (Generate 도구): 켜면 Base 노이즈 + 씬의 TerrainStamp + 필터 + 재질 규칙으로 높이·스플랫을 만든다
	TerrainGenSettings Generator;
	std::vector<float> BaseSnapshot;   // Base = Current Terrain 일 때 기준 높이 (정규화, 해상도²)
	std::vector<float> UncarvedHeights;   // 생성기가 도로(침식 뒤 스플라인)·물(호수·강)로 바꾸기 전 높이 (정규화, 저장 안 함). 점 맞추기용
	// 컬러 맵 (World Creator 식 색 재질): RGBA8, 컨트롤 해상도. rgb = 색(sRGB), a = 색이 레이어 텍스처 색을 대신하는 정도
	//  (텍스처는 명암 디테일만). 비어 있으면 쓰지 않는다. 생성기의 색·그라디언트 규칙이 만든다
	std::vector<uint8_t> ColorMap;
	void SetColorMap(std::vector<uint8_t> colorMap);

	unsigned Revision = 0;      // 높이가 바뀔 때마다 증가 (물리 형상 재생성 판단)
	unsigned ControlRevision = 0;   // 컨트롤(스플랫) 맵이 바뀔 때마다 증가
	bool Dirty = false;         // 저장하지 않은 변경

public:
	std::string Name() const;

	// ---- 크기/해상도 ----
	int NodeGrid() const { return (std::min)(kNodeGrid, HeightmapResolution - 1); }
	int MaxDepth() const;                                   // 가장 깊은 노드 = 높이맵 간격 1
	int NodeStep(int depth) const { return (HeightmapResolution - 1) / (1 << depth) / NodeGrid(); }   // 노드 격자 한 칸 = 높이맵 몇 칸
	int NodeCells(int depth) const { return (HeightmapResolution - 1) >> depth; }
	float CellSizeX() const { return Size.x / (HeightmapResolution - 1); }
	float CellSizeZ() const { return Size.z / (HeightmapResolution - 1); }
	const Node& GetNode(int depth, int x, int z) const { return m_Nodes[depth][(size_t)z * ((size_t)1 << depth) + x]; }
	void SetHeightmapResolution(int resolution);   // 기존 높이를 보간해 옮긴다
	void SetSize(const Vec3& size);
	// Undo: 해상도/크기/높이/컨트롤/레이어를 한꺼번에 되돌린다
	void RestoreState(int resolution, const Vec3& size, const std::vector<float>& heights, const std::vector<uint8_t>& control,
		const std::vector<std::shared_ptr<TerrainLayer>>& layers);

	// ---- 높이 (지형 로컬 좌표, 미터) ----
	float GetHeightSample(int x, int z) const;     // 격자점 (정규화 0~1)
	float GetHeight(float x, float z) const;       // 삼각형 보간 (렌더링 메시와 같은 면)
	float GetUncarvedHeight(float x, float z) const;   // 물로 파기 전 높이 (없으면 GetHeight)
	Vec3 GetNormal(float x, float z) const;
	// 지형 로컬 공간 광선. 맞으면 t(방향 길이 단위) 를 돌려준다
	bool Raycast(const Vec3& origin, const Vec3& dir, float maxDistance, float& outT) const;

	// ---- 편집 후 호출 (격자 범위, 포함) ----
	void OnHeightsChanged(int x0, int z0, int x1, int z1);
	void OnControlChanged(int x0, int z0, int x1, int z1);

	// ---- GPU 자원 (필요할 때 만들고 바뀐 영역만 올린다) ----
	GfxShaderResourceView* HeightSRV();
	GfxShaderResourceView* ControlSRV();
	GfxShaderResourceView* ColorMapSRV();   // 컬러 맵이 없으면 nullptr

	// Profiler 메모리: CPU (높이·컨트롤·쿼드트리·나무 인스턴스) / GPU (높이·컨트롤 텍스처)
	size_t CpuBytes() const;
	size_t GpuBytes() const;

	// ---- 파일 ----
	bool Save();
	static std::shared_ptr<TerrainData> Load(const std::string& path);
	static std::shared_ptr<TerrainData> Create(const std::string& path, int resolution = 513, const Vec3& size = Vec3(1000.0f, 600.0f, 1000.0f));
	// 메모리에 있는 저장 안 된 지형 데이터를 모두 저장 (씬 저장 시)
	static void SaveAllDirty();
	static bool AnyDirty();

private:
	void Allocate();
	void RebuildNodes(int x0, int z0, int x1, int z1);   // 높이맵 격자 범위와 겹치는 노드
	void RebuildAllNodes();

	std::vector<std::vector<Node>> m_Nodes;   // [깊이][z * 2^깊이 + x]

	ComPtr<GfxTexture2D> m_HeightTex;
	ComPtr<GfxShaderResourceView> m_HeightSRV;
	ComPtr<GfxTexture2D> m_ControlTex;
	ComPtr<GfxShaderResourceView> m_ControlSRV;
	ComPtr<GfxTexture2D> m_ColorTex;
	ComPtr<GfxShaderResourceView> m_ColorSRV;
	bool m_ColorDirty = false;
	bool m_HeightDirty = true, m_ControlDirty = true;
	int m_HeightDirtyRect[4] = { 0, 0, 0, 0 };
	int m_ControlDirtyRect[4] = { 0, 0, 0, 0 };
	bool m_HeightFullUpload = true, m_ControlFullUpload = true;
};
