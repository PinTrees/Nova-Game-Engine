#pragma once
#include <array>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>

// 편집용 다각형 메시 (Blender 의 Edit Mesh 와 같은 생각): 점 + 면(n 각형).
//  - 좌표 = 엔진과 같은 왼손 좌표 (Y 위, 캐릭터 앞 = +Z). 면 점 순서는 엔진(DirectX) 과 같다: 법선 = (b-a)×(c-a) 가 바깥
//  - 변은 면에서 만든다 (BuildEdges). 선택은 점 · 변 · 면 셋 다 들고, 모드에 따라 서로 맞춘다 (Flush)
//  - 연산은 선택에 적용하고 결과 수를 돌려준다 (CLI 가 그대로 AI 에게 보여 준다)
//  - 좌표는 오브젝트 로컬 (단위 = 파일 단위 그대로, 보통 미터)
namespace Modeling
{
	// UTF-8 문자열 ↔ 경로 (C++20 의 u8string 은 char8_t)
	inline std::filesystem::path PathU8(const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }
	inline std::string U8String(const std::filesystem::path& p) { const std::u8string u = p.u8string(); return std::string(u.begin(), u.end()); }

	enum class SelectMode { Vertex = 0, Edge = 1, Face = 2 };

	struct Vert
	{
		Vec3 P = Vec3(0, 0, 0);
		bool Sel = false;
		// 버텍스 그룹 (Mesh::Groups 번호, 가중치 0..1) — 이름 있는 선택 집합, 리깅 단계의 스킨 가중치
		std::vector<std::pair<int, float>> W;
	};

	// 새 점 = 두 점 사이 (위치 · 그룹 가중치 보간, 선택됨)
	Vert MixVert(const Vert& a, const Vert& b, float t);
	// 여러 점의 평균 (면 중심 등)
	Vert AverageVert(const std::vector<Vert>& verts, const std::vector<int>& ids);
	// 점을 복사해 위치만 바꾼다 (그룹 유지)
	inline Vert CopyVert(const Vert& v, const Vec3& p, bool sel = true) { Vert c = v; c.P = p; c.Sel = sel; return c; }

	struct Face
	{
		std::vector<int> V;      // 점 번호 (법선 = (V1-V0)×(V2-V0) 쪽이 앞면)
		std::vector<Vec2> UV;    // 모서리마다 (비어 있으면 UV 없음)
		bool Sel = false;
		bool Smooth = false;     // Shade Smooth
		int Material = 0;
		int Origin = -1;         // 모디파이어 결과 면 → 원래 면 번호 (그리기 · 고르기, 저장하지 않음)
	};

	// 비례 편집 감쇠 (Blender 의 Proportional Editing Falloff)
	enum class Falloff { Smooth = 0, Sphere, Root, Sharp, Linear, Constant };
	Falloff FalloffFromName(const std::string& name);

	struct Edge
	{
		int A = 0, B = 0;        // A < B
		std::vector<int> Faces;
	};

	inline uint64 EdgeKey(int a, int b) { if (a > b) std::swap(a, b); return ((uint64)(uint32)a << 32) | (uint32)b; }

	class Mesh
	{
	public:
		std::vector<Vert> Verts;
		std::vector<Face> Faces;
		std::unordered_set<uint64> SelEdges;   // 변 선택 (EdgeKey)
		std::vector<std::string> Groups;       // 버텍스 그룹 이름 (Vert::W 의 번호)

		// ---- 버텍스 그룹 ----
		int FindGroup(const std::string& name) const;
		int AddGroup(const std::string& name);              // 있으면 그 번호
		int AssignGroup(int group, float weight);           // 고른 점을 그룹에 (가중치 덮어씀)
		int RemoveFromGroup(int group);                     // 고른 점을 그룹에서 뺀다
		int SelectGroup(int group, bool select);            // 그룹 점 고르기 / 해제 (가중치 > 0)
		void DeleteGroup(int group);
		int GroupCount(int group) const;
		float Weight(int vert, int group) const;

		// ---- 변 / 이웃 (바뀌면 다시) ----
		const std::vector<Edge>& Edges();
		int FindEdge(int a, int b);            // 변 번호 (없으면 -1)
		void Touch() { m_EdgesDirty = true; }

		// ---- 기하 ----
		Vec3 FaceNormal(int f) const;
		Vec3 FaceCenter(int f) const;
		void Bounds(Vec3& mn, Vec3& mx) const;
		bool Empty() const { return Verts.empty(); }

		// ---- 선택 ----
		void SelectAll(bool on);
		void InvertSelection(SelectMode mode);
		// 모드에 맞춰 나머지를 맞춘다: Vertex = 점 → 변 · 면, Edge = 변 → 점 · 면, Face = 면 → 점 · 변
		void Flush(SelectMode mode);
		std::vector<int> SelectedVerts() const;
		std::vector<int> SelectedFaces() const;
		std::vector<int> SelectedEdgeIndices();
		Vec3 SelectionCenter() const;
		int SelectLinked();                                        // 선택과 이어진 것 모두 (점 기준)
		int SelectByNormal(const Vec3& dir, float maxAngleDeg);    // 면 법선이 dir 과 이 각도 안
		int SelectByPosition(int axis, float minV, float maxV);    // 점 좌표 axis 가 [min, max]
		int SelectEdgeLoop(int edge);                              // 사각형을 가로지르는 변 고리
		int SelectEdgeRing(int edge);
		int GrowSelection();
		int ShrinkSelection();

		// ---- 만들기 (새 것만 선택된다) ----
		int AddCube(const Vec3& size, const Vec3& center);
		int AddPlane(const Vec2& size, const Vec3& center, int subdivX = 1, int subdivY = 1);
		int AddCircle(int verts, float radius, const Vec3& center, bool fill);
		int AddCylinder(int verts, float radius, float depth, const Vec3& center, bool caps = true, float radiusTop = -1.0f);
		int AddUVSphere(int segments, int rings, float radius, const Vec3& center);
		int AddIcoSphere(int subdiv, float radius, const Vec3& center);
		int AddTorus(int major, int minor, float majorRadius, float minorRadius, const Vec3& center);
		int AddFace(const std::vector<Vec3>& points);   // 점들로 면 하나

		// ---- 변환 (선택한 점) ----
		int Translate(const Vec3& d);
		int Rotate(const Quaternion& q, const Vec3& pivot);
		int Scale(const Vec3& s, const Vec3& pivot);
		int SetPositions(const std::vector<std::pair<int, Vec3>>& pos);

		// ---- 편집 연산 (선택에) ----
		int ExtrudeRegion(float distance, const Vec3* direction = nullptr);   // 면 영역 (면이 없으면 경계 변)
		int ExtrudeIndividual(float distance);
		int Inset(float thickness, float depth, bool individual);
		int Subdivide(int cuts);                 // 선택한 면 (이웃 면에도 중점이 들어가 구멍이 생기지 않는다)
		int CatmullClark(int levels);            // 전체 (Subdivision Surface 적용)
		int LoopCut(int edge, int cuts, float factor = 0.5f);   // 변 고리를 따라 사각형을 자른다
		int Bevel(float offset, int segments);   // 선택한 변 (모서리 깎기, segments = 1)
		int DeleteVerts();
		int DeleteFaces(bool onlyFaces);         // onlyFaces = 점 · 변은 남김
		int MergeByDistance(float dist, bool selectedOnly);
		int MergeAtCenter();
		int Fill();                              // 선택한 점으로 면 하나 (평면에 투영해 각도 순)
		int BridgeEdgeLoops();                   // 선택한 두 경계 고리를 잇는다
		int Mirror(int axis, bool merge, float mergeDist = 1e-4f);   // 로컬 axis (0 x, 1 y, 2 z) 로 뒤집은 복제 + 가운데 용접
		int Symmetrize(int axis, bool positiveToNegative);          // 한쪽을 다른 쪽으로 복사 (가운데 점은 0 으로)
		int FlipNormals();
		int RecalculateNormals(bool inside = false);
		int Smooth(float factor, int iterations);
		int SetSmooth(bool smooth);
		int Triangulate();
		int Duplicate();                         // 선택을 복제 (새 것 선택)

		// ---- 비례 편집: 고른 점 = 1, 반경 안 = 감쇠 (가장 가까운 고른 점까지 거리), 밖 = 0
		std::vector<float> ProportionalWeights(float radius, Falloff falloff) const;

		// ---- UV (고른 면, 없으면 전체) — 0..1 안에 맞춘다
		int ProjectUV(int mode);                     // 0 box, 1 cylinder (Y 축), 2 sphere, 3 planar (평균 법선)
		int SmartUV(float angleDeg, float margin);   // Smart UV Project: 법선이 비슷한 면끼리 덩어리 → 평면 투영 → 쌓기. 덩어리 수
		int FacesWithUV() const;

		// ---- 머리카락 (애니메이션풍 머리 다발 · 카드): 점들을 Catmull-Rom 으로 이은 곡선을 따라
		//  sides >= 3 = 단면이 납작한 다발 (width × thickness, 끝으로 가늘어짐, 뿌리는 막힘), sides < 3 = 평평한 카드 (앞면 = center 반대쪽)
		//  tip = 끝 굵기 비율 (0 = 뾰족), UV: u = 둘레, v = 뿌리 0 → 끝 1. 새 것만 선택된다
		int AddStrand(const std::vector<Vec3>& points, float width, float thickness, float tip, int sides, const Vec3& center, int segments);

		// 쓰지 않는 점 지우기 · 빈 면 정리 (연산 뒤 자동)
		void Cleanup();

		// ---- 정보 ----
		int NonManifoldEdges();
		int BoundaryEdges();

		// ---- 저장 ----
		nlohmann::json ToJson(bool withSelection = false) const;   // 선택은 Undo 스냅숏용
		void FromJson(const nlohmann::json& j);
		void Append(const Mesh& other, const Matrix& transform);

	private:
		std::vector<Edge> m_Edges;
		std::unordered_map<uint64, int> m_EdgeIndex;
		bool m_EdgesDirty = true;
		int ExtrudeFaces(const std::vector<int>& faces, float distance, const Vec3* direction, bool alongNormal);
		int ExtrudeBoundaryEdges(const Vec3& offset);
		void DeselectAll();
	};

	// 그리기 · 내보내기용 삼각형 나누기 (오목한 면도: 귀 자르기)
	void Triangulate(const Mesh& m, int face, std::vector<std::array<int, 3>>& outCorners);   // 면 안의 모서리 번호로
}
