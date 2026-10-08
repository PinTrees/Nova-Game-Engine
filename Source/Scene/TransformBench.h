#pragma once

// Transform · 컬링 측정 (CLI: nova transform bench) — 데이터 지향 Transform (docs/TRANSFORM_SOA.md) 의 전후 비교.
//  지금 씬에 큐브 계층 (루트 × 자식) 을 만들어 프레임마다 루트를 움직이고, 계층 갱신 · 월드 행렬 읽기 · 컬링 갱신 · 절두체 검사를 따로 잰 뒤 지운다
namespace TransformBench
{
	void RegisterEditor();
}
