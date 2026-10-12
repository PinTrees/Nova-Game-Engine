#pragma once
#include "BuildingDesc.h"
#include "Vertex.h"
#include <vector>

// 건축물 메시 생성기: BuildingDesc → 재질 칸 (Slot) 마다 정점 · 삼각형. 엔진 좌표 (왼손 · Y 위 · 앞면 = -Z) 로 바로 짓는다.
//  - 평면 → 사각 날개들 (L · T · U · 안뜰 · 십자) 또는 둥근 몸채, 날개마다 층 · 벽 · 지붕
//  - 벽은 층 · 면마다 띠, 창 · 문 구멍 둘레를 열로 잘라 짓는다 (아치 · 첨두 · 둥근 창도 같은 길 — 틈 없이). 구멍 안쪽 면 · 유리 · 창틀 · 덧창 · 문
//  - UV = 미터 (재질 Tiling 이 재질마다 무늬 크기로), 탄젠트 = 노멀맵 규약 (B = N x T 가 아래 · 지붕은 비탈 아래)
namespace BuildingGenerator
{
	enum Slot { WallLower, WallUpper, Trim, Timber, Roof, Glass, Frame, Shutter, Door, Metal, Fabric, Ornament, Banner, Plants, SlotCount };
	const char* SlotName(int slot);

	struct Part
	{
		std::vector<Vertex::PosNormalTexTan2> V;
		std::vector<uint32_t> I;
	};

	struct Output
	{
		Part Parts[SlotCount];
		XMFLOAT3 Min = { 0, 0, 0 }, Max = { 0, 0, 0 };
		int Windows = 0, Doors = 0, Wings = 0, Towers = 0;
		size_t Vertices() const { size_t n = 0; for (const Part& p : Parts) n += p.V.size(); return n; }
		size_t Triangles() const { size_t n = 0; for (const Part& p : Parts) n += p.I.size() / 3; return n; }
	};

	void Generate(const BuildingDesc& desc, Output& out);
}
