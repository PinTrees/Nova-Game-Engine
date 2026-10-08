#pragma once
#include <cstdint>
#include <DirectXMath.h>
#include <nlohmann/json.hpp>

class Transform;

// 데이터 지향 Transform (docs/TRANSFORM_SOA.md): 모든 Transform 의 로컬 TRS · 월드 행렬 · 월드 회전 · 월드 크기를 SoA 배열에.
//  - Transform 은 자리 번호 (slot) 만 갖는다. 로컬 값이 바뀌면 배열에 복사하고 그 아래 계층에 '더러움' 표시만 한다 (즉시 다시 계산하지 않는다)
//  - 월드 값은 읽을 때 (그 사슬만) 또는 프레임마다 Flush 에서 한 번에 계산한다. Flush 는 겹치지 않는 하위 계층을 Job System 으로 나눠 돈다
//  - 월드가 다시 계산될 때마다 자리의 Version 이 오른다 — 컬링 · 렌더러는 행렬을 비교하지 않고 번호만 본다
//  - 월드 행렬은 64 바이트 (캐시 라인) 에 맞춘 연속 배열 — 컬링 · 그리기가 차례로 읽는다
//  - 메인 스레드가 아닌 곳 (잡) 에서 더러운 값을 읽으면 배열을 바꾸지 않고 그 자리에서 계산해 돌려준다 (경쟁 없음)
namespace TransformStore
{
	uint32_t Allocate(Transform* owner);
	void Release(uint32_t slot);

	// 로컬 값 → 배열 + 아래 계층 더러움. parentSlot = -1 이면 루트
	void SetLocal(uint32_t slot, const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT4& rotation, const DirectX::XMFLOAT3& scale);
	void SetParent(uint32_t slot, int32_t parentSlot);
	void MarkDirty(uint32_t slot);

	// 월드 값 (더러우면 계산)
	DirectX::XMFLOAT4X4 World(uint32_t slot);
	DirectX::XMFLOAT3 WorldPosition(uint32_t slot);
	DirectX::XMFLOAT4 WorldRotation(uint32_t slot);
	DirectX::XMFLOAT3 WorldScale(uint32_t slot);
	// 메인 스레드: 더러우면 계산해 두고 배열 안의 행렬을 바로 (다음 변경 전까지 유효 — 복사 없이 읽는 컬링용)
	const DirectX::XMFLOAT4X4& WorldRef(uint32_t slot);
	uint32_t Version(uint32_t slot);   // 월드가 다시 계산될 때마다 +1
	bool IsDirty(uint32_t slot);

	// 프레임마다 (컬링 · 그리기 전, 메인): 더러운 계층을 모두 계산한다
	void Flush();

	void SetMainThread();   // App 시작 (메인 스레드 표시 — 이 스레드만 배열을 고친다)
	bool OnMainThread();    // 지금 배열을 고쳐도 되는가 (메인 + ParallelFor 밖)
	nlohmann::json Info();
}
