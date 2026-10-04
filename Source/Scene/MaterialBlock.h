#pragma once
#include "UMaterial.h"

// MaterialPropertyBlock 상태 (Mesh Renderer · Skinned Mesh Renderer 공용 — C# Renderer.SetPropertyBlock).
//  재질은 공유한 채 렌더러만 값을 바꾼다: (원본 재질, 원본 값, 블록 값) 마다 파생 재질 하나 — 같은 값의 렌더러가 같은 객체를 써서
//  MeshBatcher 가 한 묶음으로 그린다 (값이 렌더러마다 다르면 그만큼 묶음이 나뉜다). 원본 값이 바뀌면 (UMaterial::StateHash) 다시 만든다
class NOVA_API MaterialBlock
{
public:
	struct Value { XMFLOAT4 V = XMFLOAT4(0, 0, 0, 0); bool Color = false; };   // float 은 V.x
	using Values = std::vector<std::pair<std::string, Value>>;

	void Set(Values values);   // 빈 목록 = 블록 없음
	const Values& Get() const { return m_Values; }
	bool Empty() const { return m_Values.empty(); }
	// 그릴 재질: 블록이 없으면 materials 그대로, 있으면 파생 재질 (캐시)
	const std::vector<std::shared_ptr<UMaterial>>& Apply(const std::vector<std::shared_ptr<UMaterial>>& materials);
	// GPU 인스턴싱 속성: 블록이 _BaseColor (_Color) · _EmissionColor (색) · _Metallic · _Smoothness (_Glossiness, 수) 뿐이고
	//  재질이 모두 엔진 Lit · Unlit (Alpha Clipping 없음, Emission 은 CanInstanceEmission) 이면 true + 그 값 —
	//  MeshBatcher 가 파생 재질 대신 인스턴스 값으로 넣어 값이 렌더러마다 달라도 한 묶음으로 그린다 (Unity 의 Per-instance 속성)
	struct InstanceValues
	{
		bool HasBaseColor = false, HasEmission = false, HasMetallic = false, HasSmoothness = false;
		XMFLOAT4 BaseColor = XMFLOAT4(1, 1, 1, 1);
		XMFLOAT3 Emission = XMFLOAT3(0, 0, 0);   // 선형 (셰이더가 쓰는 값 — UMaterial::EmissionToLinear)
		float Metallic = 0.0f, Smoothness = 0.0f;
	};
	bool Instanced(const std::vector<std::shared_ptr<UMaterial>>& materials, InstanceValues& out) const;

private:
	Values m_Values;   // 이름 순
	uint64 m_Hash = 0;
	std::vector<std::shared_ptr<UMaterial>> m_Render;
	uint64 m_Stamp = 0;
};
