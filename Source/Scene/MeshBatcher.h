#pragma once

class Scene;

// Mesh Renderer 묶어 그리기 (인스턴싱).
//  - 보이는(SceneCulling) Mesh Renderer 를 (메시, 서브셋, 재질)로 묶고 월드 행렬 배열 하나로 DrawIndexedInstanced 한 번
//    그림자·깊이 패스는 재질이 필요 없어 (메시, 서브셋)만으로 묶는다
//  - 예전에는 물체마다 효과 Apply(상수 버퍼·텍스처 바인딩) + Draw 를 패스마다 했다 → 물체 수 × 패스 수만큼 CPU 비용
//  - 본 패스(BatchTech)와 깊이 사전 패스(NormalDepthBatchTech)는 같은 식(월드 위치 × ViewProj)이라 EQUAL 깊이 검사가 맞는다
namespace MeshBatcher
{
	enum class Pass { Main, Shadow, NormalDepth };

	void Draw(Scene* scene, Pass pass, bool editor);

	struct Stats
	{
		int Objects = 0;     // 그린 Mesh Renderer
		int Batches = 0;     // 그리기 호출 수
	};
	const Stats& LastStats(bool editor);   // 마지막 본 패스
}
