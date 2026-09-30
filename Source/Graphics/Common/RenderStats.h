#pragma once
#include <cstdint>

// Game 뷰 Stats 창(Unity 의 Statistics)에 쓰는 한 프레임 렌더링 통계.
// Game 뷰를 그리는 동안(Begin ~ End)만 센다. Scene 뷰 그리기는 세지 않는다.
namespace RenderStats
{
	struct Frame
	{
		int DrawCalls = 0;          // Batches (그림자·깊이 패스 포함)
		int SavedByBatching = 0;    // 인스턴싱으로 합쳐진 오브젝트 수
		int64_t Triangles = 0;
		int64_t Vertices = 0;
		int ShadowCasters = 0;
		int VisibleSkinnedMeshes = 0;
		double RenderMs = 0.0;      // Game 뷰 그리기에 걸린 CPU 시간
	};

	inline bool Counting = false;
	inline Frame Current;
	inline Frame Last;

	inline void Begin() { Current = Frame(); Counting = true; }
	inline void End() { Counting = false; Last = Current; }

	inline void AddDraw(uint32_t indexCount, uint32_t vertexCount, uint32_t instances = 1)
	{
		if (!Counting)
			return;
		++Current.DrawCalls;
		Current.Triangles += (int64_t)(indexCount / 3) * instances;
		Current.Vertices += (int64_t)vertexCount * instances;
		if (instances > 1)
			Current.SavedByBatching += (int)instances - 1;
	}
	inline void AddShadowCaster() { if (Counting) ++Current.ShadowCasters; }
	inline void AddSkinnedMesh() { if (Counting) ++Current.VisibleSkinnedMeshes; }
}
