#include "pch.h"
#include "PlantLibrary.h"
#include <mutex>

// 기후대별 식생 종 — 기후 범위는 연평균 기온 (°C) · 습도 (0 ~ 1). 모양 · 색은 실제 종을 보고 맞춘 근사값.
//  틀 (Deciduous · Conifer · Palm · Shrub · Grass · Fern · Rosette · Flower · Cactus) 에서 시작해 종마다 몇 값을 바꾼다
namespace
{
	using LS = TreeParams::LeafShape;
	using CR = TreeParams::Crown;
	using FM = TreeParams::Form;
	using L = PlantLibrary::Layer;
	using Climate = PlantLibrary::Climate;
	using Species = PlantLibrary::Species;

	XMFLOAT4 C3(float r, float g, float b) { return { r, g, b, 1.0f }; }
	TreeParams::Level Lv(int count, float start, float angle, float angleVar, float length, float lengthVar, float gravity, float up, float radius, float gnarl)
	{
		TreeParams::Level l;
		l.Count = count; l.Start = start; l.Angle = angle; l.AngleVariance = angleVar; l.Length = length; l.LengthVariance = lengthVar;
		l.Gravity = gravity; l.Up = up; l.Radius = radius; l.Gnarl = gnarl;
		return l;
	}

	// 크기로 LOD · 그리는 거리 · 그림자: 키 큰 나무는 멀리 임포스터, 작은 풀 · 꽃은 임포스터 없이 짧게 (그림자 없음)
	void SizeLod(TreeDesc& d)
	{
		const float h = d.Params.Height;
		if (h >= 6.0f) { d.LodDistance = 40.0f; d.BillboardDistance = 100.0f; d.CullDistance = 2500.0f; }
		else if (h >= 2.5f) { d.LodDistance = 22.0f; d.BillboardDistance = 55.0f; d.CullDistance = 1200.0f; }
		else if (h >= 0.9f) { d.LodDistance = 12.0f; d.BillboardDistance = 32.0f; d.CullDistance = 400.0f; }
		else { d.LodDistance = 7.0f; d.BillboardDistance = 10000.0f; d.CullDistance = 90.0f; d.CastShadows = false; }
	}

	// ---- 틀
	void Deciduous(TreeDesc& d, float h, float r, CR crown, LS leaf)
	{
		TreeParams& p = d.Params;
		p = TreeParams();
		p.Height = h; p.Radius = r; p.TipRadius = 0.28f; p.Flare = 0.55f; p.Gnarl = 0.18f; p.CrownShape = crown;
		p.Levels = 2;
		// 수관이 성기지 않게: 굵은 가지 15 · 잔가지 8 · 잎 카드 12, 큰 나무일수록 카드도 크게
		p.L[0] = Lv(15, 0.3f, 50.0f, 12.0f, 0.55f, 0.2f, 0.12f, 0.1f, 0.45f, 0.25f);
		p.L[1] = Lv(8, 0.15f, 45.0f, 15.0f, 0.52f, 0.2f, 0.18f, 0.05f, 0.5f, 0.3f);
		p.Leaf = leaf; p.LeafCards = 12; p.LeavesPerCard = leaf == LS::Oval || leaf == LS::Round ? 6 : 5;
		p.LeafCardSize = std::clamp(0.6f + h * 0.03f, 0.6f, 1.4f); p.LeafStart = 0.15f;
		d.BarkColor = C3(0.36f, 0.3f, 0.24f); d.MossColor = C3(0.33f, 0.42f, 0.16f); d.Moss = 0.25f;
		d.RidgeDepth = 0.5f; d.RidgeFrequency = 14.0f; d.BarkFleck = 0.15f;
		d.LeafColor = C3(0.22f, 0.4f, 0.11f); d.LeafColor2 = C3(0.4f, 0.52f, 0.14f); d.LeafVariation = 0.5f; d.LeafTransmission = 0.6f;
		d.LeafLength = leaf == LS::Round ? 0.25f : 0.3f;
	}

	void Conifer(TreeDesc& d, float h, float r, int branches)
	{
		TreeParams& p = d.Params;
		p = TreeParams();
		p.Height = h; p.Radius = r; p.TipRadius = 0.05f; p.Flare = 0.45f; p.Gnarl = 0.03f; p.CrownShape = CR::Conical;
		p.Levels = 2;
		p.L[0] = Lv(branches, 0.08f, 90.0f, 8.0f, 0.28f, 0.12f, 0.45f, 0.0f, 0.3f, 0.05f);
		p.L[1] = Lv(7, 0.08f, 52.0f, 10.0f, 0.45f, 0.2f, 0.3f, 0.0f, 0.5f, 0.1f);
		// 잎 카드 크기는 키를 따라 (30 ~ 58 m 나무에 0.75 m 카드면 앙상한 뼈대로 보였다). 카드 수는 그대로 두면 큰 나무가
		//  LOD0 10 만 삼각형 (카드 6900 장) 이 되어 — 적게, 크게
		p.Leaf = LS::Needle; p.LeafCards = 10; p.LeavesPerCard = 16; p.LeafCardSize = std::clamp(0.5f + h * 0.035f, 0.8f, 2.2f); p.LeafStart = 0.03f;
		d.BarkColor = C3(0.36f, 0.25f, 0.19f); d.MossColor = C3(0.3f, 0.36f, 0.16f); d.Moss = 0.15f;
		d.RidgeDepth = 0.6f; d.RidgeFrequency = 14.0f; d.BarkFleck = 0.12f;
		d.LeafColor = C3(0.1f, 0.22f, 0.11f); d.LeafColor2 = C3(0.17f, 0.3f, 0.13f); d.LeafVariation = 0.3f; d.LeafTransmission = 0.25f; d.LeafLength = 0.3f;
	}

	void Palm(TreeDesc& d, float h, float r, int fronds, float frondLength, LS leaf = LS::Frond)
	{
		TreeParams& p = d.Params;
		p = TreeParams();
		p.Shape = FM::Palm;
		p.Height = h; p.Radius = r; p.TipRadius = 0.75f; p.Flare = 0.25f; p.Gnarl = 0.08f; p.Lean = 6.0f; p.CrownShape = CR::Cylindrical;
		p.Levels = 0; p.RadialSegments = 10;
		p.Leaf = leaf; p.LeafCards = fronds; p.LeavesPerCard = 14; p.LeafCardSize = frondLength;
		p.CardSegments = 4; p.Droop = 1.0f; p.CardSpread = 55.0f;
		d.BarkColor = C3(0.45f, 0.38f, 0.3f); d.MossColor = C3(0.35f, 0.4f, 0.2f); d.Moss = 0.05f;
		d.RidgeDepth = 0.25f; d.RidgeFrequency = 30.0f; d.BarkFleck = 0.1f;
		d.LeafColor = C3(0.18f, 0.38f, 0.12f); d.LeafColor2 = C3(0.32f, 0.48f, 0.15f); d.LeafVariation = 0.4f; d.LeafTransmission = 0.55f; d.LeafLength = 0.3f;
		d.TrunkSway = 0.35f; d.BranchSway = 0.35f;
		d.Pattern = TreeDesc::BarkPattern::Rings; d.PatternSpacing = 0.12f; d.PatternDepth = 0.55f; d.PatternColor = C3(0.6f, 0.56f, 0.48f);
	}

	void Shrub(TreeDesc& d, float h, LS leaf)
	{
		TreeParams& p = d.Params;
		p = TreeParams();
		p.Height = h; p.Radius = (std::max)(0.03f, h * 0.04f); p.TipRadius = 0.5f; p.Flare = 0.2f; p.Gnarl = 0.25f; p.CrownShape = CR::Hemispherical;
		p.Levels = 2;
		// 덤불은 수만 그루 — 카드 적게, 크게 (예전 틀은 1.2 m 덤불이 4 ~ 6 천 삼각형)
		p.L[0] = Lv(7, 0.05f, 48.0f, 15.0f, 1.3f, 0.25f, 0.1f, 0.15f, 0.8f, 0.4f);
		p.L[1] = Lv(4, 0.25f, 45.0f, 15.0f, 0.5f, 0.2f, 0.15f, 0.1f, 0.6f, 0.4f);
		p.Leaf = leaf; p.LeafCards = 5; p.LeavesPerCard = leaf == LS::Needle || leaf == LS::Scale ? 12 : 6;
		p.LeafCardSize = std::clamp(0.32f + h * 0.18f, 0.3f, 0.85f); p.LeafStart = 0.2f; p.RadialSegments = 6;
		d.BarkColor = C3(0.35f, 0.28f, 0.22f); d.MossColor = C3(0.33f, 0.42f, 0.16f); d.Moss = 0.1f;
		d.RidgeDepth = 0.3f; d.RidgeFrequency = 24.0f; d.BarkFleck = 0.1f;
		d.LeafColor = C3(0.2f, 0.38f, 0.11f); d.LeafColor2 = C3(0.34f, 0.48f, 0.13f); d.LeafVariation = 0.5f; d.LeafTransmission = 0.55f; d.LeafLength = 0.32f;
	}

	void Grass(TreeDesc& d, float h, int cards, int blades, float width = 0.3f)
	{
		TreeParams& p = d.Params;
		p = TreeParams();
		p.Shape = FM::Grass;
		p.Height = h; p.Radius = 0.005f; p.Levels = 0;
		p.Leaf = LS::Blade; p.LeafCards = cards; p.LeavesPerCard = blades; p.LeafCardSize = h;
		p.CardSegments = 3; p.Droop = 0.55f; p.CardSpread = 22.0f;
		d.BarkColor = C3(0.3f, 0.35f, 0.15f); d.Moss = 0.0f;
		d.LeafColor = C3(0.3f, 0.46f, 0.14f); d.LeafColor2 = C3(0.5f, 0.56f, 0.2f); d.LeafVariation = 0.6f; d.LeafTransmission = 0.75f; d.LeafSmoothness = 0.3f;
		d.LeafLength = width;
		d.WindStrength = 0.6f; d.TrunkSway = 3.0f; d.BranchSway = 0.1f; d.LeafFlutter = 0.04f;
	}

	void Fern(TreeDesc& d, float size, int fronds)
	{
		TreeParams& p = d.Params;
		p = TreeParams();
		p.Shape = FM::Fern;
		p.Height = size * 0.8f; p.Radius = 0.005f; p.Levels = 0;
		p.Leaf = LS::Frond; p.LeafCards = fronds; p.LeavesPerCard = 14; p.LeafCardSize = size;
		p.CardSegments = 3; p.Droop = 0.95f; p.CardSpread = 48.0f;
		d.BarkColor = C3(0.28f, 0.3f, 0.14f); d.Moss = 0.0f;
		d.LeafColor = C3(0.2f, 0.4f, 0.1f); d.LeafColor2 = C3(0.36f, 0.52f, 0.14f); d.LeafVariation = 0.45f; d.LeafTransmission = 0.8f; d.LeafLength = 0.3f;
		d.WindStrength = 0.5f; d.TrunkSway = 1.5f; d.LeafFlutter = 0.03f;
	}

	void Rosette(TreeDesc& d, float size, int leaves, LS leaf = LS::Blade, float width = 0.35f)
	{
		TreeParams& p = d.Params;
		p = TreeParams();
		p.Shape = FM::Rosette;
		p.Height = size * 0.7f; p.Radius = 0.01f; p.Levels = 0;
		p.Leaf = leaf; p.LeafCards = leaves; p.LeavesPerCard = 1; p.LeafCardSize = size;
		p.CardSegments = 2; p.Droop = 0.25f; p.CardSpread = 62.0f;
		d.BarkColor = C3(0.3f, 0.32f, 0.2f); d.Moss = 0.0f;
		d.LeafColor = C3(0.3f, 0.45f, 0.32f); d.LeafColor2 = C3(0.42f, 0.52f, 0.38f); d.LeafVariation = 0.3f; d.LeafTransmission = 0.3f; d.LeafSmoothness = 0.45f;
		d.LeafLength = width;
		d.WindStrength = 0.2f; d.TrunkSway = 0.3f; d.LeafFlutter = 0.005f;
	}

	void Flower(TreeDesc& d, float h, int stems, int petals, float flowerSize, XMFLOAT4 petal, XMFLOAT4 center)
	{
		TreeParams& p = d.Params;
		p = TreeParams();
		p.Shape = FM::Flower;
		p.Height = h; p.Radius = 0.004f; p.Levels = 0;
		p.Leaf = LS::Blade; p.LeafCards = 5; p.LeavesPerCard = 3; p.LeafCardSize = (std::max)(0.08f, h * 0.45f);
		p.CardSegments = 2; p.Droop = 0.5f; p.CardSpread = 35.0f;
		p.Stems = stems; p.FlowerPetals = petals; p.FlowerCards = 1; p.FlowerSize = flowerSize;
		d.BarkColor = C3(0.28f, 0.42f, 0.16f); d.Moss = 0.0f;
		d.LeafColor = C3(0.24f, 0.42f, 0.13f); d.LeafColor2 = C3(0.36f, 0.5f, 0.16f); d.LeafVariation = 0.4f; d.LeafTransmission = 0.7f; d.LeafLength = 0.35f;
		d.FlowerColor = petal; d.FlowerCenter = center;
		d.WindStrength = 0.6f; d.TrunkSway = 3.0f; d.BranchSway = 0.4f; d.LeafFlutter = 0.03f;
	}

	void Cactus(TreeDesc& d, float h, float r, int arms)
	{
		TreeParams& p = d.Params;
		p = TreeParams();
		p.Height = h; p.Radius = r; p.TipRadius = 0.8f; p.Flare = 0.05f; p.Gnarl = 0.02f; p.CrownShape = CR::Cylindrical;
		p.RadialSegments = 12;
		p.Levels = arms > 0 ? 1 : 0;
		p.L[0] = Lv(arms, 0.35f, 85.0f, 10.0f, 0.4f, 0.15f, 0.0f, 1.0f, 0.75f, 0.05f);
		p.LeafCards = 0; p.LeavesPerCard = 1; p.LeafCardSize = 0.1f;
		d.BarkColor = C3(0.3f, 0.43f, 0.24f); d.MossColor = C3(0.6f, 0.58f, 0.4f); d.Moss = 0.0f;
		d.RidgeDepth = 1.4f; d.RidgeFrequency = 6.0f; d.BarkSmoothness = 0.3f; d.BarkFleck = 0.25f;
		d.LeafColor = C3(0.3f, 0.43f, 0.24f); d.LeafColor2 = C3(0.3f, 0.43f, 0.24f);
		d.WindStrength = 0.05f; d.TrunkSway = 0.02f; d.BranchSway = 0.02f; d.LeafFlutter = 0.0f;
		d.Pattern = TreeDesc::BarkPattern::Ribs; d.PatternSpacing = 11.0f; d.PatternDepth = 0.8f; d.PatternColor = C3(0.42f, 0.52f, 0.32f);
	}

	struct Builder
	{
		std::vector<Species> List;

		Species& Add(const char* name, const char* zone, L kind, Climate c, float abundance)
		{
			Species s;
			s.Name = name;
			s.Zone = zone;
			s.Kind = kind;
			s.Range = c;
			s.Abundance = abundance;
			s.Desc.Preset = (int)List.size();
			List.push_back(s);
			return List.back();
		}
	};

	std::vector<Species> Build()
	{
		Builder b;
		// ================================================================ 0 ~ 11: 예전 프리셋 (값 그대로)
		{
			Species& s = b.Add("Oak", "Temperate Forest", L::Canopy, { 6.0f, 16.0f, 0.4f, 0.85f, 30.0f, 0.0f }, 1.0f);
			TreeParams& p = s.Desc.Params; TreeDesc& d = s.Desc;
			p.Height = 9.0f; p.Radius = 0.34f; p.TipRadius = 0.3f; p.Flare = 0.7f; p.Gnarl = 0.25f; p.CrownShape = CR::Spherical;
			p.Levels = 2;
			p.L[0] = Lv(11, 0.32f, 55.0f, 15.0f, 0.58f, 0.2f, 0.12f, 0.1f, 0.45f, 0.3f);
			p.L[1] = Lv(7, 0.2f, 45.0f, 15.0f, 0.5f, 0.2f, 0.2f, 0.05f, 0.5f, 0.35f);
			p.Leaf = LS::Broad; p.LeafCards = 10; p.LeavesPerCard = 5; p.LeafCardSize = 0.9f; p.LeafStart = 0.2f;
			d.BarkColor = C3(0.36f, 0.29f, 0.23f); d.MossColor = C3(0.33f, 0.42f, 0.16f); d.Moss = 0.35f;
			d.RidgeDepth = 0.6f; d.RidgeFrequency = 16.0f; d.BarkFleck = 0.2f;
			d.LeafColor = C3(0.24f, 0.42f, 0.11f); d.LeafColor2 = C3(0.45f, 0.53f, 0.13f); d.LeafVariation = 0.6f; d.LeafTransmission = 0.6f; d.LeafLength = 0.34f;
			d.LodDistance = 35.0f; d.BillboardDistance = 90.0f;
		}
		{
			Species& s = b.Add("Pine", "Boreal Forest", L::Canopy, { 0.0f, 13.0f, 0.25f, 0.7f, 35.0f, 0.0f }, 1.0f);
			TreeParams& p = s.Desc.Params; TreeDesc& d = s.Desc;
			p.Height = 13.0f; p.Radius = 0.28f; p.TipRadius = 0.08f; p.Flare = 0.45f; p.Gnarl = 0.04f; p.CrownShape = CR::Conical;
			p.Levels = 2;
			p.L[0] = Lv(30, 0.18f, 82.0f, 8.0f, 0.34f, 0.12f, 0.35f, 0.0f, 0.35f, 0.08f);
			p.L[1] = Lv(8, 0.15f, 52.0f, 10.0f, 0.42f, 0.2f, 0.25f, 0.05f, 0.5f, 0.1f);
			p.Leaf = LS::Needle; p.LeafCards = 12; p.LeavesPerCard = 16; p.LeafCardSize = 0.75f; p.LeafStart = 0.05f;
			d.BarkColor = C3(0.38f, 0.25f, 0.18f); d.MossColor = C3(0.3f, 0.36f, 0.16f); d.Moss = 0.12f;
			d.RidgeDepth = 0.8f; d.RidgeFrequency = 10.0f; d.BarkFleck = 0.1f;
			d.LeafColor = C3(0.12f, 0.26f, 0.11f); d.LeafColor2 = C3(0.2f, 0.33f, 0.13f); d.LeafVariation = 0.35f; d.LeafTransmission = 0.3f; d.LeafLength = 0.3f;
			d.LodDistance = 35.0f; d.BillboardDistance = 90.0f;
		}
		{
			Species& s = b.Add("Birch", "Boreal Forest", L::Canopy, { -3.0f, 12.0f, 0.4f, 0.9f, 30.0f, 0.1f }, 0.8f);
			TreeParams& p = s.Desc.Params; TreeDesc& d = s.Desc;
			p.Height = 11.0f; p.Radius = 0.17f; p.TipRadius = 0.2f; p.Flare = 0.25f; p.Gnarl = 0.12f; p.CrownShape = CR::Flame;
			p.Levels = 2;
			p.L[0] = Lv(15, 0.38f, 38.0f, 10.0f, 0.42f, 0.2f, 0.3f, 0.25f, 0.45f, 0.2f);
			p.L[1] = Lv(6, 0.2f, 40.0f, 12.0f, 0.55f, 0.2f, 0.65f, 0.0f, 0.5f, 0.3f);
			p.Leaf = LS::Oval; p.LeafCards = 8; p.LeavesPerCard = 6; p.LeafCardSize = 0.55f; p.LeafStart = 0.2f;
			d.BarkColor = C3(0.86f, 0.84f, 0.79f); d.MossColor = C3(0.4f, 0.45f, 0.2f); d.Moss = 0.05f;
			d.RidgeDepth = 0.15f; d.RidgeFrequency = 14.0f; d.BarkFleck = -0.7f;
			d.LeafColor = C3(0.4f, 0.58f, 0.16f); d.LeafColor2 = C3(0.62f, 0.66f, 0.2f); d.LeafVariation = 0.5f; d.LeafTransmission = 0.75f; d.LeafLength = 0.3f;
			d.LodDistance = 35.0f; d.BillboardDistance = 90.0f;
		}
		{
			Species& s = b.Add("Bush", "Temperate Forest", L::Shrub, { 3.0f, 20.0f, 0.35f, 0.9f, 35.0f, 0.0f }, 1.0f);
			TreeParams& p = s.Desc.Params; TreeDesc& d = s.Desc;
			p.Height = 1.2f; p.Radius = 0.06f; p.TipRadius = 0.5f; p.Flare = 0.2f; p.Gnarl = 0.2f; p.CrownShape = CR::Hemispherical;
			p.Levels = 2;
			p.L[0] = Lv(9, 0.05f, 48.0f, 15.0f, 1.3f, 0.25f, 0.1f, 0.15f, 0.8f, 0.4f);
			p.L[1] = Lv(5, 0.25f, 45.0f, 15.0f, 0.5f, 0.2f, 0.15f, 0.1f, 0.6f, 0.4f);
			p.Leaf = LS::Broad; p.LeafCards = 7; p.LeavesPerCard = 5; p.LeafCardSize = 0.45f; p.LeafStart = 0.2f;
			d.BarkColor = C3(0.35f, 0.28f, 0.22f); d.MossColor = C3(0.33f, 0.42f, 0.16f); d.Moss = 0.1f;
			d.RidgeDepth = 0.3f; d.RidgeFrequency = 24.0f; d.BarkFleck = 0.1f;
			d.LeafColor = C3(0.2f, 0.38f, 0.11f); d.LeafColor2 = C3(0.34f, 0.48f, 0.13f); d.LeafVariation = 0.5f; d.LeafTransmission = 0.55f; d.LeafLength = 0.36f;
			d.LodDistance = 18.0f; d.BillboardDistance = 45.0f;
		}
		{
			Species& s = b.Add("Spruce", "Boreal Forest", L::Canopy, { -5.0f, 8.0f, 0.4f, 0.95f, 38.0f, 0.0f }, 1.4f);
			TreeParams& p = s.Desc.Params; TreeDesc& d = s.Desc;
			p.Height = 22.0f; p.Radius = 0.36f; p.TipRadius = 0.05f; p.Flare = 0.5f; p.Gnarl = 0.03f; p.CrownShape = CR::Conical;
			p.Levels = 2;
			p.L[0] = Lv(46, 0.07f, 96.0f, 6.0f, 0.25f, 0.12f, 0.55f, 0.0f, 0.3f, 0.05f);
			p.L[1] = Lv(8, 0.1f, 55.0f, 10.0f, 0.42f, 0.2f, 0.4f, 0.0f, 0.5f, 0.1f);
			p.Leaf = LS::Needle; p.LeafCards = 14; p.LeavesPerCard = 18; p.LeafCardSize = 0.8f; p.LeafStart = 0.02f;
			d.BarkColor = C3(0.33f, 0.22f, 0.17f); d.MossColor = C3(0.3f, 0.36f, 0.16f); d.Moss = 0.2f;
			d.RidgeDepth = 0.5f; d.RidgeFrequency = 18.0f; d.BarkFleck = 0.15f;
			d.LeafColor = C3(0.08f, 0.2f, 0.11f); d.LeafColor2 = C3(0.14f, 0.28f, 0.14f); d.LeafVariation = 0.3f; d.LeafTransmission = 0.25f; d.LeafLength = 0.3f;
			d.LodDistance = 45.0f; d.BillboardDistance = 110.0f; d.CullDistance = 2500.0f;
		}
		{
			Species& s = b.Add("Fir", "Montane", L::Canopy, { -1.0f, 10.0f, 0.45f, 0.95f, 38.0f, 0.0f }, 1.0f);
			TreeParams& p = s.Desc.Params; TreeDesc& d = s.Desc;
			p.Height = 18.0f; p.Radius = 0.32f; p.TipRadius = 0.06f; p.Flare = 0.45f; p.Gnarl = 0.03f; p.CrownShape = CR::Conical;
			p.Levels = 2;
			p.L[0] = Lv(38, 0.1f, 88.0f, 5.0f, 0.3f, 0.1f, 0.25f, 0.05f, 0.32f, 0.05f);
			p.L[1] = Lv(7, 0.12f, 50.0f, 10.0f, 0.45f, 0.2f, 0.2f, 0.05f, 0.5f, 0.1f);
			p.Leaf = LS::Needle; p.LeafCards = 13; p.LeavesPerCard = 16; p.LeafCardSize = 0.8f; p.LeafStart = 0.03f;
			d.BarkColor = C3(0.45f, 0.43f, 0.4f); d.MossColor = C3(0.33f, 0.4f, 0.18f); d.Moss = 0.25f;
			d.RidgeDepth = 0.25f; d.RidgeFrequency = 12.0f; d.BarkFleck = 0.3f;
			d.LeafColor = C3(0.1f, 0.23f, 0.1f); d.LeafColor2 = C3(0.17f, 0.31f, 0.13f); d.LeafVariation = 0.3f; d.LeafTransmission = 0.25f; d.LeafLength = 0.3f;
			d.LodDistance = 45.0f; d.BillboardDistance = 110.0f; d.CullDistance = 2500.0f;
		}
		{
			Species& s = b.Add("Larch", "Boreal Forest", L::Canopy, { -8.0f, 6.0f, 0.3f, 0.85f, 38.0f, 0.0f }, 0.6f);
			TreeParams& p = s.Desc.Params; TreeDesc& d = s.Desc;
			p.Height = 20.0f; p.Radius = 0.3f; p.TipRadius = 0.06f; p.Flare = 0.4f; p.Gnarl = 0.08f; p.CrownShape = CR::Conical;
			p.Levels = 2;
			p.L[0] = Lv(26, 0.15f, 75.0f, 10.0f, 0.32f, 0.15f, 0.4f, 0.0f, 0.3f, 0.1f);
			p.L[1] = Lv(7, 0.15f, 50.0f, 12.0f, 0.5f, 0.2f, 0.45f, 0.0f, 0.5f, 0.15f);
			p.Leaf = LS::Needle; p.LeafCards = 9; p.LeavesPerCard = 12; p.LeafCardSize = 0.7f; p.LeafStart = 0.1f;
			d.BarkColor = C3(0.4f, 0.28f, 0.2f); d.MossColor = C3(0.33f, 0.4f, 0.18f); d.Moss = 0.1f;
			d.RidgeDepth = 0.7f; d.RidgeFrequency = 12.0f; d.BarkFleck = 0.1f;
			d.LeafColor = C3(0.32f, 0.45f, 0.14f); d.LeafColor2 = C3(0.48f, 0.55f, 0.18f); d.LeafVariation = 0.4f; d.LeafTransmission = 0.5f; d.LeafLength = 0.28f;
			d.LodDistance = 40.0f; d.BillboardDistance = 100.0f; d.CullDistance = 2500.0f;
		}
		{
			Species& s = b.Add("Beech", "Temperate Forest", L::Canopy, { 6.0f, 14.0f, 0.55f, 1.0f, 32.0f, 0.0f }, 1.3f);
			TreeParams& p = s.Desc.Params; TreeDesc& d = s.Desc;
			p.Height = 16.0f; p.Radius = 0.3f; p.TipRadius = 0.25f; p.Flare = 0.5f; p.Gnarl = 0.1f; p.CrownShape = CR::Spherical;
			p.Levels = 2;
			p.L[0] = Lv(12, 0.42f, 42.0f, 12.0f, 0.5f, 0.2f, 0.08f, 0.15f, 0.4f, 0.15f);
			p.L[1] = Lv(7, 0.2f, 40.0f, 15.0f, 0.5f, 0.2f, 0.12f, 0.08f, 0.5f, 0.25f);
			p.Leaf = LS::Broad; p.LeafCards = 11; p.LeavesPerCard = 6; p.LeafCardSize = 0.85f; p.LeafStart = 0.2f;
			d.BarkColor = C3(0.55f, 0.53f, 0.5f); d.MossColor = C3(0.35f, 0.45f, 0.18f); d.Moss = 0.3f;
			d.RidgeDepth = 0.08f; d.RidgeFrequency = 10.0f; d.BarkFleck = 0.25f;
			d.LeafColor = C3(0.25f, 0.45f, 0.12f); d.LeafColor2 = C3(0.42f, 0.55f, 0.15f); d.LeafVariation = 0.5f; d.LeafTransmission = 0.7f; d.LeafLength = 0.32f;
			d.LodDistance = 40.0f; d.BillboardDistance = 100.0f; d.CullDistance = 2500.0f;
		}
		{
			Species& s = b.Add("Dwarf Pine", "Montane", L::Shrub, { -6.0f, 3.0f, 0.25f, 0.9f, 40.0f, 0.0f }, 1.0f);
			TreeParams& p = s.Desc.Params; TreeDesc& d = s.Desc;
			p.Height = 3.2f; p.Radius = 0.12f; p.TipRadius = 0.4f; p.Flare = 0.4f; p.Gnarl = 0.45f; p.Lean = 8.0f; p.CrownShape = CR::Hemispherical;
			p.Levels = 2;
			p.L[0] = Lv(14, 0.08f, 72.0f, 20.0f, 0.75f, 0.3f, 0.3f, 0.0f, 0.6f, 0.45f);
			p.L[1] = Lv(6, 0.2f, 50.0f, 15.0f, 0.5f, 0.2f, 0.2f, 0.05f, 0.6f, 0.35f);
			p.Leaf = LS::Needle; p.LeafCards = 9; p.LeavesPerCard = 14; p.LeafCardSize = 0.6f; p.LeafStart = 0.15f;
			d.BarkColor = C3(0.3f, 0.24f, 0.2f); d.MossColor = C3(0.4f, 0.42f, 0.25f); d.Moss = 0.3f;
			d.RidgeDepth = 0.6f; d.RidgeFrequency = 20.0f; d.BarkFleck = 0.2f;
			d.LeafColor = C3(0.1f, 0.22f, 0.1f); d.LeafColor2 = C3(0.18f, 0.3f, 0.12f); d.LeafVariation = 0.35f; d.LeafTransmission = 0.3f; d.LeafLength = 0.3f;
			d.LodDistance = 20.0f; d.BillboardDistance = 50.0f; d.CullDistance = 900.0f;
		}
		{
			Species& s = b.Add("Dead Pine", "Montane", L::Canopy, { -4.0f, 12.0f, 0.2f, 0.9f, 35.0f, 0.0f }, 0.04f);
			TreeParams& p = s.Desc.Params; TreeDesc& d = s.Desc;
			p.Height = 12.0f; p.Radius = 0.25f; p.TipRadius = 0.15f; p.Flare = 0.35f; p.Gnarl = 0.2f; p.CrownShape = CR::Cylindrical;
			p.Levels = 1;
			p.L[0] = Lv(16, 0.3f, 78.0f, 18.0f, 0.18f, 0.35f, 0.15f, 0.0f, 0.35f, 0.3f);
			p.Leaf = LS::Needle; p.LeafCards = 1; p.LeavesPerCard = 1; p.LeafCardSize = 0.02f; p.LeafStart = 0.95f;
			d.BarkColor = C3(0.52f, 0.49f, 0.45f); d.MossColor = C3(0.4f, 0.42f, 0.3f); d.Moss = 0.2f;
			d.RidgeDepth = 0.5f; d.RidgeFrequency = 9.0f; d.BarkFleck = 0.4f;
			d.LeafColor = C3(0.4f, 0.35f, 0.25f); d.LeafColor2 = C3(0.45f, 0.4f, 0.3f); d.LeafVariation = 0.2f; d.LeafTransmission = 0.1f; d.LeafLength = 0.2f;
			d.LodDistance = 40.0f; d.BillboardDistance = 100.0f; d.CullDistance = 2000.0f;
		}
		{
			Species& s = b.Add("Alder", "Wetland", L::Canopy, { 2.0f, 16.0f, 0.5f, 1.0f, 25.0f, 0.8f }, 1.0f);
			TreeParams& p = s.Desc.Params; TreeDesc& d = s.Desc;
			p.Height = 10.0f; p.Radius = 0.2f; p.TipRadius = 0.2f; p.Flare = 0.3f; p.Gnarl = 0.15f; p.CrownShape = CR::TaperedCylindrical;
			p.Levels = 2;
			p.L[0] = Lv(14, 0.3f, 45.0f, 12.0f, 0.45f, 0.2f, 0.25f, 0.15f, 0.45f, 0.2f);
			p.L[1] = Lv(6, 0.2f, 42.0f, 12.0f, 0.5f, 0.2f, 0.5f, 0.0f, 0.5f, 0.25f);
			p.Leaf = LS::Oval; p.LeafCards = 9; p.LeavesPerCard = 6; p.LeafCardSize = 0.6f; p.LeafStart = 0.2f;
			d.BarkColor = C3(0.3f, 0.27f, 0.24f); d.MossColor = C3(0.33f, 0.42f, 0.16f); d.Moss = 0.4f;
			d.RidgeDepth = 0.4f; d.RidgeFrequency = 14.0f; d.BarkFleck = 0.25f;
			d.LeafColor = C3(0.15f, 0.32f, 0.1f); d.LeafColor2 = C3(0.26f, 0.42f, 0.12f); d.LeafVariation = 0.45f; d.LeafTransmission = 0.6f; d.LeafLength = 0.3f;
			d.LodDistance = 35.0f; d.BillboardDistance = 90.0f; d.CullDistance = 2000.0f;
		}
		{
			Species& s = b.Add("Juniper", "Grassland", L::Shrub, { 0.0f, 16.0f, 0.15f, 0.6f, 40.0f, 0.0f }, 0.8f);
			TreeParams& p = s.Desc.Params; TreeDesc& d = s.Desc;
			p.Height = 2.0f; p.Radius = 0.07f; p.TipRadius = 0.2f; p.Flare = 0.2f; p.Gnarl = 0.2f; p.CrownShape = CR::Flame;
			p.Levels = 2;
			p.L[0] = Lv(12, 0.05f, 30.0f, 12.0f, 0.9f, 0.25f, 0.05f, 0.2f, 0.7f, 0.3f);
			p.L[1] = Lv(5, 0.2f, 40.0f, 15.0f, 0.5f, 0.2f, 0.1f, 0.1f, 0.6f, 0.3f);
			p.Leaf = LS::Needle; p.LeafCards = 8; p.LeavesPerCard = 12; p.LeafCardSize = 0.45f; p.LeafStart = 0.1f;
			d.BarkColor = C3(0.38f, 0.28f, 0.22f); d.MossColor = C3(0.33f, 0.42f, 0.16f); d.Moss = 0.05f;
			d.RidgeDepth = 0.4f; d.RidgeFrequency = 22.0f; d.BarkFleck = 0.1f;
			d.LeafColor = C3(0.12f, 0.22f, 0.16f); d.LeafColor2 = C3(0.2f, 0.3f, 0.22f); d.LeafVariation = 0.3f; d.LeafTransmission = 0.3f; d.LeafLength = 0.3f;
			d.LodDistance = 18.0f; d.BillboardDistance = 45.0f; d.CullDistance = 600.0f;
		}

		// 새 종: 틀 → 값 몇 개 → 크기로 LOD (값을 따로 정한 것은 그대로 둔다)
		auto add = [&](const char* name, const char* zone, L kind, Climate c, float abundance, auto make) -> Species& {
			Species& s = b.Add(name, zone, kind, c, abundance);
			make(s.Desc, s.Desc.Params);
			SizeLod(s.Desc);
			s.Desc.Invalidate();
			return s;
		};

		// ================================================================ 열대 우림 (연평균 22 ~ 30 °C, 습도 0.7 ~ 1)
		const char* TR = "Tropical Rainforest";
		add("Kapok", TR, L::Canopy, { 22, 30, 0.7f, 1.0f, 30, 0.1f }, 0.35f, [](TreeDesc& d, TreeParams& p) {
			Deciduous(d, 38.0f, 0.9f, CR::InverseConical, LS::Oval);
			p.Flare = 2.2f; p.TipRadius = 0.2f; p.Gnarl = 0.05f;
			p.L[0] = Lv(9, 0.7f, 70.0f, 12.0f, 0.42f, 0.2f, 0.05f, 0.12f, 0.4f, 0.15f);
			p.LeafCardSize = 1.2f; p.LeafCards = 12;
			d.BarkColor = C3(0.55f, 0.52f, 0.45f); d.RidgeDepth = 0.2f; d.Moss = 0.45f; d.MossColor = C3(0.3f, 0.42f, 0.18f);
			d.LeafColor = C3(0.16f, 0.36f, 0.09f); d.LeafColor2 = C3(0.3f, 0.46f, 0.12f);
		});
		add("Mahogany", TR, L::Canopy, { 21, 30, 0.6f, 1.0f, 30, 0.0f }, 0.8f, [](TreeDesc& d, TreeParams& p) {
			Deciduous(d, 28.0f, 0.6f, CR::Spherical, LS::Oval);
			p.L[0] = Lv(13, 0.5f, 55.0f, 12.0f, 0.45f, 0.2f, 0.08f, 0.1f, 0.42f, 0.18f);
			p.LeafCardSize = 1.1f;
			d.BarkColor = C3(0.4f, 0.28f, 0.22f); d.RidgeDepth = 0.9f; d.RidgeFrequency = 10.0f; d.Moss = 0.35f;
			d.LeafColor = C3(0.14f, 0.32f, 0.08f); d.LeafColor2 = C3(0.24f, 0.42f, 0.1f);
		});
		add("Strangler Fig", TR, L::Canopy, { 22, 30, 0.65f, 1.0f, 30, 0.0f }, 0.6f, [](TreeDesc& d, TreeParams& p) {
			Deciduous(d, 24.0f, 0.8f, CR::Hemispherical, LS::Broad);
			p.Flare = 1.6f; p.Gnarl = 0.4f;
			p.L[0] = Lv(14, 0.45f, 62.0f, 15.0f, 0.55f, 0.25f, 0.15f, 0.1f, 0.5f, 0.35f);
			d.BarkColor = C3(0.48f, 0.45f, 0.38f); d.RidgeDepth = 0.9f; d.RidgeFrequency = 6.0f; d.Moss = 0.5f;
			d.LeafColor = C3(0.12f, 0.3f, 0.08f); d.LeafColor2 = C3(0.22f, 0.4f, 0.1f); d.LeafSmoothness = 0.55f;
		});
		add("Rubber Tree", TR, L::Canopy, { 22, 30, 0.7f, 1.0f, 30, 0.1f }, 0.7f, [](TreeDesc& d, TreeParams& p) {
			Deciduous(d, 20.0f, 0.45f, CR::Spherical, LS::Oval);
			p.LeafCardSize = 1.0f; p.LeavesPerCard = 4;
			d.BarkColor = C3(0.5f, 0.47f, 0.42f); d.RidgeDepth = 0.15f; d.Moss = 0.4f;
			d.LeafColor = C3(0.1f, 0.28f, 0.08f); d.LeafColor2 = C3(0.18f, 0.36f, 0.1f); d.LeafSmoothness = 0.6f; d.LeafLength = 0.4f;
		});
		add("Cecropia", TR, L::SmallTree, { 22, 30, 0.65f, 1.0f, 32, 0.2f }, 0.6f, [](TreeDesc& d, TreeParams& p) {
			Deciduous(d, 14.0f, 0.22f, CR::InverseConical, LS::Lobed);
			p.TipRadius = 0.5f; p.Gnarl = 0.08f;
			p.L[0] = Lv(6, 0.7f, 55.0f, 15.0f, 0.38f, 0.2f, 0.05f, 0.3f, 0.6f, 0.15f);
			p.L[1] = Lv(4, 0.5f, 40.0f, 15.0f, 0.4f, 0.2f, 0.1f, 0.2f, 0.6f, 0.2f);
			p.LeafCards = 6; p.LeavesPerCard = 3; p.LeafCardSize = 1.3f; p.LeafStart = 0.6f;
			d.BarkColor = C3(0.66f, 0.64f, 0.58f); d.RidgeDepth = 0.15f; d.RidgeFrequency = 26.0f;
			d.LeafColor = C3(0.28f, 0.42f, 0.2f); d.LeafColor2 = C3(0.5f, 0.56f, 0.42f); d.LeafLength = 0.42f;
		});
		add("Cacao", TR, L::SmallTree, { 22, 30, 0.7f, 1.0f, 25, 0.0f }, 0.5f, [](TreeDesc& d, TreeParams& p) {
			Deciduous(d, 6.0f, 0.12f, CR::Spherical, LS::Oval);
			p.LeafCardSize = 0.8f; p.LeavesPerCard = 4; p.FlowerPetals = 5; p.FlowerCards = 2; p.FlowerSize = 0.2f;
			d.BarkColor = C3(0.42f, 0.34f, 0.26f); d.LeafColor = C3(0.16f, 0.34f, 0.08f); d.LeafColor2 = C3(0.5f, 0.32f, 0.16f); d.LeafVariation = 0.5f; d.LeafLength = 0.4f;
			d.FlowerColor = C3(0.75f, 0.45f, 0.15f); d.FlowerCenter = C3(0.55f, 0.3f, 0.1f);
		});
		add("Banana", TR, L::SmallTree, { 20, 30, 0.6f, 1.0f, 25, 0.3f }, 0.6f, [](TreeDesc& d, TreeParams& p) {
			Palm(d, 4.0f, 0.15f, 9, 2.6f, LS::Blade);
			p.LeavesPerCard = 1; p.Lean = 2.0f; p.TipRadius = 0.8f; p.CardSpread = 65.0f; p.Droop = 0.7f; p.CardSegments = 3;
			d.BarkColor = C3(0.42f, 0.48f, 0.26f); d.RidgeDepth = 0.1f; d.RidgeFrequency = 8.0f;
			d.PatternSpacing = 0.5f; d.PatternDepth = 0.2f; d.PatternColor = C3(0.5f, 0.42f, 0.28f);
			d.LeafColor = C3(0.28f, 0.48f, 0.12f); d.LeafColor2 = C3(0.42f, 0.56f, 0.16f); d.LeafLength = 0.6f; d.LeafTransmission = 0.8f;
		});
		add("Tree Fern", TR, L::SmallTree, { 12, 28, 0.75f, 1.0f, 35, 0.2f }, 0.7f, [](TreeDesc& d, TreeParams& p) {
			Palm(d, 5.0f, 0.13f, 14, 2.2f, LS::Frond);
			p.Droop = 1.2f; p.CardSpread = 40.0f; p.Lean = 4.0f;
			d.BarkColor = C3(0.25f, 0.2f, 0.16f); d.RidgeDepth = 1.0f; d.RidgeFrequency = 20.0f; d.Moss = 0.3f;
			d.Pattern = TreeDesc::BarkPattern::LeafBases; d.PatternSpacing = 0.09f; d.PatternDepth = 0.8f; d.PatternColor = C3(0.32f, 0.24f, 0.17f);
			d.LeafColor = C3(0.2f, 0.4f, 0.1f); d.LeafColor2 = C3(0.34f, 0.5f, 0.14f); d.LeafTransmission = 0.8f;
		});
		add("Heliconia", TR, L::Flower, { 20, 30, 0.65f, 1.0f, 30, 0.2f }, 0.6f, [](TreeDesc& d, TreeParams& p) {
			Flower(d, 1.6f, 3, 6, 0.32f, C3(0.85f, 0.18f, 0.08f), C3(0.95f, 0.75f, 0.1f));
			p.Leaf = LS::Blade; p.LeafCards = 7; p.LeavesPerCard = 1; p.LeafCardSize = 1.3f; p.CardSpread = 30.0f; d.LeafLength = 0.55f;
			d.LeafColor = C3(0.16f, 0.36f, 0.08f); d.LeafColor2 = C3(0.26f, 0.44f, 0.1f);
		});
		add("Bird of Paradise", TR, L::Flower, { 18, 30, 0.5f, 1.0f, 30, 0.0f }, 0.5f, [](TreeDesc& d, TreeParams& p) {
			Flower(d, 1.3f, 3, 4, 0.3f, C3(0.98f, 0.5f, 0.08f), C3(0.2f, 0.25f, 0.6f));
			p.Leaf = LS::Blade; p.LeafCards = 6; p.LeavesPerCard = 1; p.LeafCardSize = 1.0f; d.LeafLength = 0.5f;
			d.LeafColor = C3(0.2f, 0.35f, 0.18f); d.LeafColor2 = C3(0.28f, 0.42f, 0.22f); d.LeafSmoothness = 0.5f;
		});
		add("Monstera", TR, L::Shrub, { 20, 30, 0.7f, 1.0f, 35, 0.0f }, 0.8f, [](TreeDesc& d, TreeParams& p) {
			Rosette(d, 1.4f, 9, LS::Lobed, 0.5f);
			p.LeavesPerCard = 1; p.CardSpread = 50.0f; p.Droop = 0.55f; p.Height = 1.2f;
			d.LeafColor = C3(0.1f, 0.3f, 0.08f); d.LeafColor2 = C3(0.16f, 0.38f, 0.1f); d.LeafSmoothness = 0.6f; d.LeafTransmission = 0.5f;
		});
		add("Bromeliad", TR, L::Succulent, { 20, 30, 0.6f, 1.0f, 40, 0.0f }, 0.8f, [](TreeDesc& d, TreeParams& p) {
			Rosette(d, 0.6f, 16, LS::Blade, 0.3f);
			p.CardSpread = 55.0f; p.Droop = 0.6f; p.FlowerPetals = 8; p.Stems = 1; p.FlowerCards = 1; p.FlowerSize = 0.2f; p.Shape = FM::Flower; p.Height = 0.5f; p.LeafCards = 14; p.LeafCardSize = 0.6f; p.LeavesPerCard = 1;
			d.LeafColor = C3(0.22f, 0.4f, 0.12f); d.LeafColor2 = C3(0.6f, 0.2f, 0.18f); d.LeafVariation = 0.8f;
			d.FlowerColor = C3(0.9f, 0.15f, 0.25f); d.FlowerCenter = C3(0.9f, 0.8f, 0.2f);
		});
		add("Philodendron", TR, L::Shrub, { 20, 30, 0.7f, 1.0f, 35, 0.0f }, 0.7f, [](TreeDesc& d, TreeParams& p) {
			Rosette(d, 1.1f, 8, LS::Broad, 0.5f);
			p.LeavesPerCard = 1; p.CardSpread = 45.0f; p.Droop = 0.7f;
			d.LeafColor = C3(0.12f, 0.32f, 0.08f); d.LeafColor2 = C3(0.2f, 0.42f, 0.1f); d.LeafSmoothness = 0.6f;
		});
		add("Wild Ginger", TR, L::Grass, { 20, 30, 0.65f, 1.0f, 30, 0.2f }, 0.8f, [](TreeDesc& d, TreeParams& p) {
			Grass(d, 1.4f, 6, 3, 0.55f);
			p.Droop = 0.4f; p.CardSpread = 25.0f;
			d.LeafColor = C3(0.2f, 0.4f, 0.1f); d.LeafColor2 = C3(0.3f, 0.48f, 0.12f);
		});
		add("Jungle Grass", TR, L::Grass, { 20, 30, 0.6f, 1.0f, 35, 0.0f }, 1.2f, [](TreeDesc& d, TreeParams& p) {
			Grass(d, 0.55f, 6, 7, 0.4f);
			d.LeafColor = C3(0.2f, 0.4f, 0.09f); d.LeafColor2 = C3(0.32f, 0.48f, 0.12f);
		});
		add("Orchid", TR, L::Flower, { 18, 30, 0.7f, 1.0f, 40, 0.0f }, 0.4f, [](TreeDesc& d, TreeParams& p) {
			Flower(d, 0.45f, 2, 5, 0.1f, C3(0.88f, 0.55f, 0.85f), C3(0.95f, 0.85f, 0.3f));
			p.FlowerCards = 4; p.Leaf = LS::Oval; p.LeafCards = 4; p.LeavesPerCard = 2; p.LeafCardSize = 0.22f;
		});

		// ================================================================ 열대 건조림 (계절풍 — 20 ~ 30 °C, 습도 0.45 ~ 0.7)
		const char* TD = "Tropical Dry Forest";
		add("Teak", TD, L::Canopy, { 20, 30, 0.4f, 0.75f, 30, 0.0f }, 1.0f, [](TreeDesc& d, TreeParams& p) {
			Deciduous(d, 22.0f, 0.45f, CR::TaperedCylindrical, LS::Broad);
			p.LeafCardSize = 1.2f; p.LeavesPerCard = 4;
			d.BarkColor = C3(0.5f, 0.42f, 0.34f); d.RidgeDepth = 0.6f; d.RidgeFrequency = 12.0f; d.Moss = 0.1f;
			d.LeafColor = C3(0.3f, 0.42f, 0.13f); d.LeafColor2 = C3(0.48f, 0.5f, 0.18f); d.LeafLength = 0.42f;
		});
		add("Rosewood", TD, L::Canopy, { 20, 30, 0.35f, 0.7f, 30, 0.0f }, 0.7f, [](TreeDesc& d, TreeParams& p) {
			Deciduous(d, 16.0f, 0.35f, CR::Spherical, LS::Oval);
			p.LeavesPerCard = 8; p.LeafCardSize = 0.6f;
			d.BarkColor = C3(0.36f, 0.28f, 0.22f); d.RidgeDepth = 0.8f; d.LeafColor = C3(0.2f, 0.38f, 0.1f); d.LeafColor2 = C3(0.34f, 0.46f, 0.12f); d.LeafLength = 0.22f;
		});
		add("Frangipani", TD, L::SmallTree, { 20, 30, 0.3f, 0.8f, 30, 0.0f }, 0.5f, [](TreeDesc& d, TreeParams& p) {
			Deciduous(d, 5.0f, 0.16f, CR::Hemispherical, LS::Oval);
			p.TipRadius = 0.6f; p.Gnarl = 0.3f;
			p.L[0] = Lv(5, 0.35f, 40.0f, 15.0f, 0.6f, 0.2f, 0.0f, 0.3f, 0.75f, 0.3f);
			p.L[1] = Lv(3, 0.5f, 35.0f, 15.0f, 0.5f, 0.2f, 0.0f, 0.3f, 0.75f, 0.3f);
			p.LeafCards = 4; p.LeavesPerCard = 6; p.LeafCardSize = 0.8f; p.LeafStart = 0.7f;
			p.FlowerPetals = 5; p.FlowerCards = 3; p.FlowerSize = 0.18f;
			d.BarkColor = C3(0.55f, 0.52f, 0.45f); d.RidgeDepth = 0.1f; d.LeafLength = 0.45f;
			d.FlowerColor = C3(0.97f, 0.94f, 0.88f); d.FlowerCenter = C3(0.98f, 0.82f, 0.25f);
		});
		add("Bougainvillea", TD, L::Shrub, { 18, 30, 0.2f, 0.7f, 35, 0.0f }, 0.5f, [](TreeDesc& d, TreeParams& p) {
			Shrub(d, 2.4f, LS::Oval);
			p.FlowerPetals = 3; p.FlowerCards = 4; p.FlowerSize = 0.16f;
			d.FlowerColor = C3(0.85f, 0.15f, 0.55f); d.FlowerCenter = C3(0.95f, 0.9f, 0.85f);
		});
		add("Coconut Palm", TD, L::Canopy, { 22, 30, 0.4f, 1.0f, 20, 0.6f }, 0.8f, [](TreeDesc& d, TreeParams& p) {
			Palm(d, 18.0f, 0.2f, 18, 4.6f);
			p.Lean = 14.0f; p.Gnarl = 0.15f; p.TipRadius = 0.7f;
			p.FlowerPetals = 1; p.FlowerCards = 3; p.FlowerSize = 0.5f;
			d.FlowerColor = C3(0.45f, 0.4f, 0.18f); d.FlowerCenter = C3(0.4f, 0.36f, 0.15f);
		});
		add("Oil Palm", TR, L::Canopy, { 22, 30, 0.6f, 1.0f, 25, 0.1f }, 0.6f, [](TreeDesc& d, TreeParams& p) {
			Palm(d, 12.0f, 0.32f, 30, 4.8f);
			p.Lean = 3.0f; p.CardSpread = 60.0f; p.Droop = 0.9f;
			p.FlowerPetals = 1; p.FlowerCards = 4; p.FlowerSize = 0.5f;
			d.BarkColor = C3(0.36f, 0.32f, 0.24f);
			d.Pattern = TreeDesc::BarkPattern::LeafBases; d.PatternSpacing = 0.3f; d.PatternDepth = 1.0f; d.PatternColor = C3(0.42f, 0.38f, 0.26f);
			d.LeafColor = C3(0.16f, 0.34f, 0.1f); d.LeafColor2 = C3(0.26f, 0.42f, 0.12f);
			d.FlowerColor = C3(0.65f, 0.18f, 0.08f); d.FlowerCenter = C3(0.4f, 0.1f, 0.05f);
		});
		add("Pygmy Date Palm", TD, L::Shrub, { 18, 30, 0.4f, 1.0f, 30, 0.1f }, 0.5f, [](TreeDesc& d, TreeParams& p) {
			Palm(d, 2.6f, 0.09f, 18, 1.4f);
			p.Lean = 8.0f; p.CardSpread = 65.0f; p.Droop = 0.9f; p.LeavesPerCard = 16;
			d.Pattern = TreeDesc::BarkPattern::LeafBases; d.PatternSpacing = 0.07f; d.PatternDepth = 0.9f; d.PatternColor = C3(0.45f, 0.38f, 0.28f);
			d.LeafColor = C3(0.16f, 0.34f, 0.12f); d.LeafColor2 = C3(0.24f, 0.4f, 0.14f);
		});
		add("Royal Palm", TD, L::Canopy, { 21, 30, 0.45f, 1.0f, 25, 0.2f }, 0.5f, [](TreeDesc& d, TreeParams& p) {
			Palm(d, 20.0f, 0.3f, 16, 4.0f);
			p.Lean = 1.0f; p.Gnarl = 0.02f; p.Flare = 0.4f;
			d.BarkColor = C3(0.65f, 0.63f, 0.58f); d.RidgeDepth = 0.15f;
			d.PatternSpacing = 0.07f; d.PatternDepth = 0.3f; d.PatternColor = C3(0.55f, 0.53f, 0.48f);
		});
		add("Dry Forest Scrub", TD, L::Shrub, { 20, 30, 0.35f, 0.7f, 35, 0.0f }, 0.7f, [](TreeDesc& d, TreeParams& p) {
			Shrub(d, 1.8f, LS::Broad);
			p.LeafCardSize = 0.6f; d.LeafColor = C3(0.3f, 0.42f, 0.13f); d.LeafColor2 = C3(0.46f, 0.48f, 0.18f);
		});

		// ================================================================ 사바나 (20 ~ 30 °C, 습도 0.2 ~ 0.5)
		const char* SV = "Savanna";
		add("Umbrella Thorn", SV, L::Canopy, { 18, 30, 0.15f, 0.5f, 25, 0.0f }, 1.0f, [](TreeDesc& d, TreeParams& p) {
			Deciduous(d, 9.0f, 0.24f, CR::InverseConical, LS::Needle);
			p.Gnarl = 0.3f; p.Lean = 6.0f;
			p.L[0] = Lv(8, 0.45f, 62.0f, 12.0f, 0.75f, 0.2f, -0.05f, 0.18f, 0.55f, 0.3f);
			p.L[1] = Lv(7, 0.3f, 60.0f, 12.0f, 0.55f, 0.2f, -0.1f, 0.15f, 0.55f, 0.3f);
			p.LeafCards = 9; p.LeavesPerCard = 10; p.LeafCardSize = 0.7f; p.LeafStart = 0.4f;
			d.BarkColor = C3(0.34f, 0.28f, 0.22f); d.RidgeDepth = 0.8f; d.Moss = 0.0f;
			d.LeafColor = C3(0.3f, 0.4f, 0.14f); d.LeafColor2 = C3(0.44f, 0.46f, 0.18f); d.LeafTransmission = 0.4f; d.LeafLength = 0.22f;
		});
		add("Baobab", SV, L::Canopy, { 20, 30, 0.15f, 0.55f, 20, 0.0f }, 0.25f, [](TreeDesc& d, TreeParams& p) {
			Deciduous(d, 16.0f, 1.6f, CR::InverseConical, LS::Oval);
			p.TipRadius = 0.45f; p.Flare = 0.4f; p.Gnarl = 0.05f;
			p.L[0] = Lv(9, 0.8f, 58.0f, 15.0f, 0.38f, 0.25f, 0.0f, 0.15f, 0.35f, 0.45f);
			p.L[1] = Lv(4, 0.4f, 45.0f, 15.0f, 0.5f, 0.2f, 0.0f, 0.1f, 0.5f, 0.4f);
			p.LeafCards = 5; p.LeafCardSize = 0.7f;
			d.BarkColor = C3(0.6f, 0.52f, 0.46f); d.RidgeDepth = 0.35f; d.RidgeFrequency = 5.0f; d.Moss = 0.0f;
			d.LeafColor = C3(0.28f, 0.42f, 0.14f); d.LeafColor2 = C3(0.4f, 0.5f, 0.16f);
		});
		add("Marula", SV, L::Canopy, { 20, 30, 0.2f, 0.55f, 25, 0.0f }, 0.6f, [](TreeDesc& d, TreeParams& p) {
			Deciduous(d, 12.0f, 0.4f, CR::Hemispherical, LS::Oval);
			p.L[0] = Lv(10, 0.4f, 58.0f, 12.0f, 0.6f, 0.2f, 0.05f, 0.12f, 0.45f, 0.3f);
			d.BarkColor = C3(0.48f, 0.44f, 0.4f); d.RidgeDepth = 0.6f; d.LeafColor = C3(0.26f, 0.4f, 0.12f); d.LeafColor2 = C3(0.42f, 0.48f, 0.16f);
		});
		add("Sausage Tree", SV, L::Canopy, { 20, 30, 0.3f, 0.65f, 25, 0.3f }, 0.4f, [](TreeDesc& d, TreeParams& p) {
			Deciduous(d, 13.0f, 0.45f, CR::Spherical, LS::Oval);
			p.FlowerPetals = 5; p.FlowerCards = 1; p.FlowerSize = 0.25f;
			d.BarkColor = C3(0.5f, 0.45f, 0.38f); d.RidgeDepth = 0.3f;
			d.FlowerColor = C3(0.55f, 0.08f, 0.1f); d.FlowerCenter = C3(0.85f, 0.65f, 0.2f);
		});
		add("Whistling Thorn", SV, L::Shrub, { 18, 30, 0.15f, 0.45f, 30, 0.0f }, 0.9f, [](TreeDesc& d, TreeParams& p) {
			Shrub(d, 2.2f, LS::Needle);
			p.Gnarl = 0.4f; p.LeafCardSize = 0.5f; p.LeavesPerCard = 8;
			d.BarkColor = C3(0.3f, 0.24f, 0.2f); d.LeafColor = C3(0.32f, 0.42f, 0.16f); d.LeafColor2 = C3(0.46f, 0.46f, 0.2f); d.LeafLength = 0.2f;
		});
		add("Elephant Grass", SV, L::Grass, { 18, 30, 0.35f, 0.75f, 30, 0.3f }, 0.9f, [](TreeDesc& d, TreeParams& p) {
			Grass(d, 2.4f, 7, 5, 0.32f);
			p.Droop = 0.7f;
			d.LeafColor = C3(0.36f, 0.46f, 0.15f); d.LeafColor2 = C3(0.55f, 0.55f, 0.22f);
		});
		add("Red Oat Grass", SV, L::Grass, { 16, 30, 0.15f, 0.5f, 35, 0.0f }, 1.6f, [](TreeDesc& d, TreeParams& p) {
			Grass(d, 0.75f, 6, 7, 0.25f);
			d.LeafColor = C3(0.62f, 0.52f, 0.26f); d.LeafColor2 = C3(0.72f, 0.48f, 0.3f); d.LeafVariation = 0.8f;
		});
		add("Aloe", SV, L::Succulent, { 16, 30, 0.08f, 0.45f, 40, 0.0f }, 0.6f, [](TreeDesc& d, TreeParams& p) {
			Rosette(d, 0.7f, 14, LS::Blade, 0.3f);
			p.Shape = FM::Flower; p.Stems = 1; p.FlowerPetals = 6; p.FlowerCards = 6; p.FlowerSize = 0.09f; p.Height = 1.2f; p.LeafCardSize = 0.7f; p.LeavesPerCard = 1;
			d.LeafColor = C3(0.34f, 0.46f, 0.32f); d.LeafColor2 = C3(0.5f, 0.42f, 0.32f);
			d.FlowerColor = C3(0.95f, 0.38f, 0.12f); d.FlowerCenter = C3(0.95f, 0.65f, 0.15f);
		});
		add("Savanna Shrub", SV, L::Shrub, { 18, 30, 0.2f, 0.55f, 35, 0.0f }, 1.0f, [](TreeDesc& d, TreeParams& p) {
			Shrub(d, 1.4f, LS::Oval);
			d.LeafColor = C3(0.34f, 0.44f, 0.16f); d.LeafColor2 = C3(0.5f, 0.5f, 0.2f); d.LeafLength = 0.24f;
		});

		// ================================================================ 더운 사막 (18 ~ 30 °C, 습도 0 ~ 0.25)
		const char* DS = "Hot Desert";
		add("Saguaro", DS, L::Canopy, { 18, 30, 0.0f, 0.22f, 25, 0.0f }, 0.8f, [](TreeDesc& d, TreeParams& p) {
			Cactus(d, 10.0f, 0.3f, 3);
		});
		add("Organ Pipe Cactus", DS, L::SmallTree, { 20, 30, 0.0f, 0.2f, 25, 0.0f }, 0.5f, [](TreeDesc& d, TreeParams& p) {
			Cactus(d, 5.0f, 0.12f, 9);
			p.L[0] = Lv(9, 0.0f, 25.0f, 10.0f, 0.85f, 0.15f, 0.0f, 0.8f, 0.9f, 0.05f);
			d.BarkColor = C3(0.32f, 0.42f, 0.26f);
		});
		add("Barrel Cactus", DS, L::Succulent, { 18, 30, 0.0f, 0.2f, 40, 0.0f }, 1.0f, [](TreeDesc& d, TreeParams& p) {
			Cactus(d, 0.75f, 0.32f, 0);
			p.TipRadius = 0.75f; p.RadialSegments = 14;
			d.BarkColor = C3(0.36f, 0.46f, 0.26f); d.RidgeFrequency = 9.0f;
		});
		add("Prickly Pear", DS, L::Succulent, { 14, 30, 0.0f, 0.35f, 40, 0.0f }, 1.0f, [](TreeDesc& d, TreeParams& p) {
			// 납작한 마디 (패드) 가 이어진다: 짧은 줄기 · 가지마다 큰 둥근 잎 카드가 줄기를 덮는다
			Cactus(d, 0.5f, 0.07f, 3);
			p.L[0] = Lv(3, 0.6f, 35.0f, 20.0f, 0.7f, 0.2f, 0.0f, 0.4f, 0.8f, 0.2f);
			p.Leaf = LS::Round; p.LeafCards = 2; p.LeavesPerCard = 1; p.LeafCardSize = 0.42f; p.LeafStart = 0.35f;
			p.FlowerPetals = 8; p.FlowerCards = 1; p.FlowerSize = 0.1f;
			d.LeafColor = C3(0.34f, 0.46f, 0.24f); d.LeafColor2 = C3(0.42f, 0.5f, 0.28f); d.LeafLength = 0.5f; d.LeafTransmission = 0.2f;
			d.FlowerColor = C3(0.98f, 0.82f, 0.2f); d.FlowerCenter = C3(0.75f, 0.6f, 0.15f);
		});
		add("Cholla", DS, L::Shrub, { 16, 30, 0.0f, 0.22f, 35, 0.0f }, 0.7f, [](TreeDesc& d, TreeParams& p) {
			Cactus(d, 1.6f, 0.07f, 7);
			p.Levels = 2; p.Gnarl = 0.3f;
			p.L[0] = Lv(7, 0.3f, 55.0f, 20.0f, 0.5f, 0.3f, 0.1f, 0.4f, 0.8f, 0.3f);
			p.L[1] = Lv(3, 0.4f, 50.0f, 20.0f, 0.5f, 0.3f, 0.1f, 0.3f, 0.8f, 0.3f);
			d.BarkColor = C3(0.6f, 0.6f, 0.45f); d.RidgeFrequency = 30.0f; d.RidgeDepth = 1.0f;
		});
		add("Joshua Tree", DS, L::SmallTree, { 12, 24, 0.0f, 0.25f, 25, 0.0f }, 0.6f, [](TreeDesc& d, TreeParams& p) {
			Deciduous(d, 7.0f, 0.3f, CR::InverseConical, LS::Blade);
			p.Gnarl = 0.35f; p.TipRadius = 0.6f;
			p.L[0] = Lv(5, 0.5f, 50.0f, 20.0f, 0.55f, 0.3f, 0.1f, 0.3f, 0.7f, 0.4f);
			p.L[1] = Lv(3, 0.5f, 45.0f, 20.0f, 0.5f, 0.3f, 0.1f, 0.3f, 0.75f, 0.4f);
			p.LeafCards = 6; p.LeavesPerCard = 9; p.LeafCardSize = 0.55f; p.LeafStart = 0.85f; p.CardSegments = 1;
			d.BarkColor = C3(0.42f, 0.36f, 0.28f); d.RidgeDepth = 1.0f; d.RidgeFrequency = 20.0f; d.Moss = 0.0f;
			d.LeafColor = C3(0.3f, 0.42f, 0.24f); d.LeafColor2 = C3(0.42f, 0.46f, 0.3f); d.LeafLength = 0.2f;
		});
		add("Agave", DS, L::Succulent, { 14, 30, 0.0f, 0.35f, 40, 0.0f }, 1.0f, [](TreeDesc& d, TreeParams& p) {
			Rosette(d, 1.0f, 18, LS::Blade, 0.32f);
			p.CardSpread = 58.0f; p.Droop = 0.12f;
			d.LeafColor = C3(0.36f, 0.48f, 0.42f); d.LeafColor2 = C3(0.46f, 0.52f, 0.44f);
		});
		add("Yucca", DS, L::Succulent, { 10, 28, 0.0f, 0.35f, 40, 0.0f }, 0.7f, [](TreeDesc& d, TreeParams& p) {
			Rosette(d, 0.8f, 22, LS::Blade, 0.18f);
			p.Shape = FM::Flower; p.Stems = 1; p.FlowerPetals = 6; p.FlowerCards = 7; p.FlowerSize = 0.1f; p.Height = 1.8f; p.LeafCardSize = 0.8f; p.LeavesPerCard = 1; p.CardSpread = 40.0f;
			d.LeafColor = C3(0.3f, 0.42f, 0.28f); d.LeafColor2 = C3(0.4f, 0.46f, 0.3f);
			d.FlowerColor = C3(0.95f, 0.94f, 0.85f); d.FlowerCenter = C3(0.85f, 0.85f, 0.6f);
		});
		add("Ocotillo", DS, L::Shrub, { 16, 30, 0.0f, 0.25f, 35, 0.0f }, 0.6f, [](TreeDesc& d, TreeParams& p) {
			Shrub(d, 4.0f, LS::Oval);
			p.Radius = 0.08f; p.CrownShape = CR::InverseConical; p.Levels = 1;
			p.L[0] = Lv(14, 0.0f, 18.0f, 10.0f, 1.0f, 0.2f, 0.0f, 0.25f, 0.55f, 0.15f);
			p.LeafCards = 10; p.LeavesPerCard = 4; p.LeafCardSize = 0.18f; p.LeafStart = 0.1f;
			p.FlowerPetals = 5; p.FlowerCards = 2; p.FlowerSize = 0.14f;
			d.BarkColor = C3(0.45f, 0.4f, 0.3f); d.RidgeFrequency = 30.0f;
			d.LeafColor = C3(0.3f, 0.42f, 0.14f); d.FlowerColor = C3(0.95f, 0.22f, 0.1f); d.FlowerCenter = C3(0.9f, 0.4f, 0.1f);
		});
		add("Creosote Bush", DS, L::Shrub, { 16, 30, 0.0f, 0.25f, 35, 0.0f }, 1.4f, [](TreeDesc& d, TreeParams& p) {
			Shrub(d, 1.3f, LS::Round);
			p.LeavesPerCard = 8; p.LeafCardSize = 0.3f;
			d.LeafColor = C3(0.28f, 0.36f, 0.14f); d.LeafColor2 = C3(0.4f, 0.42f, 0.16f); d.LeafLength = 0.15f; d.LeafSmoothness = 0.55f;
		});
		add("Brittlebush", DS, L::Shrub, { 16, 30, 0.0f, 0.3f, 35, 0.0f }, 0.9f, [](TreeDesc& d, TreeParams& p) {
			Shrub(d, 0.9f, LS::Oval);
			p.FlowerPetals = 12; p.FlowerCards = 3; p.FlowerSize = 0.06f;
			d.LeafColor = C3(0.55f, 0.58f, 0.5f); d.LeafColor2 = C3(0.62f, 0.62f, 0.55f);
			d.FlowerColor = C3(0.98f, 0.82f, 0.12f); d.FlowerCenter = C3(0.75f, 0.55f, 0.1f);
		});
		add("Date Palm", DS, L::Canopy, { 18, 30, 0.0f, 0.6f, 20, 0.9f }, 1.0f, [](TreeDesc& d, TreeParams& p) {
			Palm(d, 14.0f, 0.3f, 24, 3.6f);
			p.Lean = 4.0f; p.CardSpread = 70.0f; p.Droop = 0.7f;
			p.FlowerPetals = 1; p.FlowerCards = 4; p.FlowerSize = 0.5f;
			d.BarkColor = C3(0.5f, 0.4f, 0.3f); d.RidgeDepth = 0.2f; d.RidgeFrequency = 16.0f;
			d.Pattern = TreeDesc::BarkPattern::LeafBases; d.PatternSpacing = 0.22f; d.PatternDepth = 0.9f; d.PatternColor = C3(0.6f, 0.48f, 0.34f);
			d.LeafColor = C3(0.3f, 0.42f, 0.24f); d.LeafColor2 = C3(0.38f, 0.48f, 0.3f);
			d.FlowerColor = C3(0.7f, 0.42f, 0.12f); d.FlowerCenter = C3(0.6f, 0.35f, 0.1f);
		});
		add("Fan Palm", DS, L::Canopy, { 15, 28, 0.05f, 0.6f, 25, 0.5f }, 0.7f, [](TreeDesc& d, TreeParams& p) {
			Palm(d, 20.0f, 0.38f, 24, 1.9f, LS::Blade);
			p.LeavesPerCard = 16; p.CardSegments = 2; p.Droop = 0.35f; p.CardSpread = 50.0f; p.TipRadius = 0.7f; p.Lean = 2.0f;
			d.BarkColor = C3(0.46f, 0.4f, 0.32f); d.PatternSpacing = 0.09f; d.PatternDepth = 0.45f; d.PatternColor = C3(0.56f, 0.5f, 0.4f);
			d.LeafColor = C3(0.24f, 0.4f, 0.18f); d.LeafColor2 = C3(0.36f, 0.46f, 0.24f); d.LeafLength = 0.22f;
		});
		add("Desert Marigold", DS, L::Flower, { 14, 30, 0.0f, 0.3f, 35, 0.0f }, 0.9f, [](TreeDesc& d, TreeParams& p) {
			Flower(d, 0.4f, 7, 14, 0.06f, C3(0.98f, 0.85f, 0.1f), C3(0.9f, 0.7f, 0.1f));
			d.LeafColor = C3(0.5f, 0.55f, 0.45f); d.LeafColor2 = C3(0.56f, 0.58f, 0.5f);
		});
		add("Desert Grass", DS, L::Grass, { 14, 30, 0.02f, 0.3f, 35, 0.0f }, 1.0f, [](TreeDesc& d, TreeParams& p) {
			Grass(d, 0.45f, 5, 6, 0.22f);
			d.LeafColor = C3(0.68f, 0.6f, 0.38f); d.LeafColor2 = C3(0.6f, 0.52f, 0.3f);
		});

		// ================================================================ 지중해 (13 ~ 20 °C, 습도 0.2 ~ 0.55)
		const char* MD = "Mediterranean";
		add("Olive", MD, L::Canopy, { 13, 21, 0.2f, 0.5f, 30, 0.0f }, 1.0f, [](TreeDesc& d, TreeParams& p) {
			Deciduous(d, 7.0f, 0.35f, CR::Spherical, LS::Needle);
			p.Gnarl = 0.55f; p.Flare = 0.9f; p.Lean = 6.0f;
			p.L[0] = Lv(8, 0.3f, 50.0f, 18.0f, 0.65f, 0.25f, 0.15f, 0.1f, 0.55f, 0.5f);
			p.LeavesPerCard = 10; p.LeafCardSize = 0.6f;
			d.BarkColor = C3(0.48f, 0.45f, 0.4f); d.RidgeDepth = 1.2f; d.RidgeFrequency = 8.0f; d.Moss = 0.1f;
			d.LeafColor = C3(0.36f, 0.42f, 0.32f); d.LeafColor2 = C3(0.52f, 0.56f, 0.46f); d.LeafLength = 0.22f; d.LeafTransmission = 0.4f;
		});
		add("Cork Oak", MD, L::Canopy, { 13, 20, 0.3f, 0.6f, 30, 0.0f }, 0.8f, [](TreeDesc& d, TreeParams& p) {
			Deciduous(d, 12.0f, 0.45f, CR::Hemispherical, LS::Oval);
			p.Gnarl = 0.3f;
			d.BarkColor = C3(0.5f, 0.42f, 0.34f); d.RidgeDepth = 1.6f; d.RidgeFrequency = 6.0f; d.Moss = 0.15f;
			d.LeafColor = C3(0.16f, 0.3f, 0.12f); d.LeafColor2 = C3(0.26f, 0.38f, 0.16f); d.LeafLength = 0.24f;
		});
		add("Stone Pine", MD, L::Canopy, { 13, 20, 0.2f, 0.55f, 30, 0.0f }, 0.8f, [](TreeDesc& d, TreeParams& p) {
			Conifer(d, 16.0f, 0.45f, 9);
			p.CrownShape = CR::InverseConical; p.TipRadius = 0.15f; p.Gnarl = 0.1f;
			p.L[0] = Lv(9, 0.62f, 55.0f, 12.0f, 0.55f, 0.2f, 0.0f, 0.2f, 0.5f, 0.2f);
			p.L[1] = Lv(7, 0.3f, 50.0f, 12.0f, 0.5f, 0.2f, 0.05f, 0.15f, 0.5f, 0.2f);
			p.LeafCards = 10; p.LeafStart = 0.4f;
			d.BarkColor = C3(0.48f, 0.32f, 0.24f); d.RidgeDepth = 1.0f; d.RidgeFrequency = 8.0f;
			d.LeafColor = C3(0.16f, 0.3f, 0.12f); d.LeafColor2 = C3(0.24f, 0.36f, 0.14f);
		});
		add("Italian Cypress", MD, L::Canopy, { 12, 22, 0.15f, 0.55f, 30, 0.0f }, 0.7f, [](TreeDesc& d, TreeParams& p) {
			Conifer(d, 16.0f, 0.25f, 34);
			p.CrownShape = CR::TaperedCylindrical;
			p.L[0] = Lv(34, 0.03f, 18.0f, 6.0f, 0.18f, 0.1f, 0.0f, 0.4f, 0.4f, 0.05f);
			p.L[1] = Lv(5, 0.2f, 25.0f, 10.0f, 0.5f, 0.2f, 0.0f, 0.3f, 0.5f, 0.1f);
			p.Leaf = LS::Scale; p.LeavesPerCard = 12; p.LeafCardSize = 0.6f;
			d.LeafColor = C3(0.1f, 0.2f, 0.1f); d.LeafColor2 = C3(0.16f, 0.26f, 0.12f);
		});
		add("Holm Oak", MD, L::Canopy, { 12, 20, 0.25f, 0.6f, 32, 0.0f }, 0.9f, [](TreeDesc& d, TreeParams& p) {
			Deciduous(d, 13.0f, 0.4f, CR::Spherical, LS::Oval);
			p.LeavesPerCard = 7; p.LeafCardSize = 0.6f;
			d.BarkColor = C3(0.32f, 0.28f, 0.24f); d.RidgeDepth = 0.9f; d.RidgeFrequency = 18.0f;
			d.LeafColor = C3(0.14f, 0.26f, 0.1f); d.LeafColor2 = C3(0.3f, 0.36f, 0.24f); d.LeafLength = 0.22f; d.LeafSmoothness = 0.5f;
		});
		add("Carob", MD, L::SmallTree, { 14, 22, 0.2f, 0.5f, 30, 0.0f }, 0.5f, [](TreeDesc& d, TreeParams& p) {
			Deciduous(d, 8.0f, 0.3f, CR::Hemispherical, LS::Round);
			p.LeavesPerCard = 6; p.LeafCardSize = 0.6f;
			d.BarkColor = C3(0.4f, 0.34f, 0.28f); d.LeafColor = C3(0.14f, 0.3f, 0.1f); d.LeafColor2 = C3(0.24f, 0.38f, 0.12f); d.LeafSmoothness = 0.6f;
		});
		add("Canary Date Palm", MD, L::Canopy, { 14, 26, 0.2f, 0.65f, 25, 0.2f }, 0.6f, [](TreeDesc& d, TreeParams& p) {
			Palm(d, 12.0f, 0.55f, 42, 4.6f);
			p.Lean = 1.0f; p.CardSpread = 62.0f; p.Droop = 0.85f; p.Flare = 0.15f; p.TipRadius = 0.85f; p.LeavesPerCard = 16;
			p.FlowerPetals = 1; p.FlowerCards = 6; p.FlowerSize = 0.6f;
			d.BarkColor = C3(0.46f, 0.38f, 0.28f);
			d.Pattern = TreeDesc::BarkPattern::LeafBases; d.PatternSpacing = 0.26f; d.PatternDepth = 1.0f; d.PatternColor = C3(0.58f, 0.46f, 0.32f);
			d.LeafColor = C3(0.2f, 0.36f, 0.14f); d.LeafColor2 = C3(0.3f, 0.44f, 0.18f);
			d.FlowerColor = C3(0.85f, 0.5f, 0.1f); d.FlowerCenter = C3(0.75f, 0.4f, 0.08f);
		});
		add("Lavender", MD, L::Flower, { 10, 22, 0.15f, 0.5f, 35, 0.0f }, 1.0f, [](TreeDesc& d, TreeParams& p) {
			Flower(d, 0.6f, 14, 6, 0.045f, C3(0.55f, 0.45f, 0.82f), C3(0.45f, 0.35f, 0.7f));
			p.FlowerCards = 6; p.Leaf = LS::Needle; p.LeafCards = 8; p.LeavesPerCard = 10; p.LeafCardSize = 0.25f; p.CardSegments = 1;
			d.LeafColor = C3(0.4f, 0.48f, 0.4f); d.LeafColor2 = C3(0.48f, 0.54f, 0.46f);
		});
		add("Rosemary", MD, L::Shrub, { 12, 22, 0.15f, 0.5f, 35, 0.0f }, 0.9f, [](TreeDesc& d, TreeParams& p) {
			Shrub(d, 1.0f, LS::Needle);
			p.LeavesPerCard = 12; p.LeafCardSize = 0.3f;
			p.FlowerPetals = 4; p.FlowerCards = 2; p.FlowerSize = 0.03f;
			d.LeafColor = C3(0.2f, 0.32f, 0.2f); d.LeafColor2 = C3(0.3f, 0.38f, 0.26f);
			d.FlowerColor = C3(0.6f, 0.65f, 0.92f); d.FlowerCenter = C3(0.5f, 0.55f, 0.85f);
		});
		add("Oleander", MD, L::Shrub, { 14, 26, 0.2f, 0.6f, 30, 0.4f }, 0.7f, [](TreeDesc& d, TreeParams& p) {
			Shrub(d, 2.8f, LS::Blade);
			p.LeavesPerCard = 5; p.LeafCardSize = 0.5f; p.CrownShape = CR::Spherical;
			p.FlowerPetals = 5; p.FlowerCards = 3; p.FlowerSize = 0.09f;
			d.LeafColor = C3(0.14f, 0.3f, 0.12f); d.LeafColor2 = C3(0.2f, 0.36f, 0.14f); d.LeafLength = 0.2f;
			d.FlowerColor = C3(0.95f, 0.45f, 0.6f); d.FlowerCenter = C3(0.95f, 0.75f, 0.8f);
		});
		add("Rockrose", MD, L::Shrub, { 12, 22, 0.15f, 0.5f, 38, 0.0f }, 1.0f, [](TreeDesc& d, TreeParams& p) {
			Shrub(d, 0.8f, LS::Oval);
			p.FlowerPetals = 5; p.FlowerCards = 3; p.FlowerSize = 0.07f;
			d.LeafColor = C3(0.25f, 0.34f, 0.18f); d.LeafColor2 = C3(0.34f, 0.4f, 0.22f);
			d.FlowerColor = C3(0.97f, 0.95f, 0.92f); d.FlowerCenter = C3(0.95f, 0.8f, 0.2f);
		});
		add("Mediterranean Grass", MD, L::Grass, { 12, 22, 0.15f, 0.5f, 38, 0.0f }, 1.0f, [](TreeDesc& d, TreeParams& p) {
			Grass(d, 0.5f, 5, 6, 0.24f);
			d.LeafColor = C3(0.56f, 0.54f, 0.3f); d.LeafColor2 = C3(0.46f, 0.5f, 0.24f);
		});

		// ================================================================ 아열대 습윤 (15 ~ 22 °C, 습도 0.55 ~ 1)
		const char* ST = "Subtropical Forest";
		add("Bamboo", ST, L::SmallTree, { 14, 26, 0.6f, 1.0f, 35, 0.2f }, 1.0f, [](TreeDesc& d, TreeParams& p) {
			Deciduous(d, 12.0f, 0.06f, CR::Cylindrical, LS::Blade);
			p.TipRadius = 0.6f; p.Flare = 0.0f; p.Gnarl = 0.05f; p.Lean = 6.0f;
			p.L[0] = Lv(26, 0.4f, 50.0f, 15.0f, 0.15f, 0.3f, 0.3f, 0.0f, 0.4f, 0.1f);
			p.L[1] = Lv(4, 0.2f, 45.0f, 15.0f, 0.55f, 0.25f, 0.4f, 0.0f, 0.5f, 0.15f);
			p.Levels = 2; p.LeafCards = 6; p.LeavesPerCard = 7; p.LeafCardSize = 0.6f; p.LeafStart = 0.1f; p.CardSegments = 2; p.Droop = 0.5f;
			d.BarkColor = C3(0.42f, 0.5f, 0.22f); d.RidgeDepth = 0.05f; d.RidgeFrequency = 40.0f; d.BarkSmoothness = 0.55f; d.Moss = 0.0f; d.BarkFleck = 0.0f;
			d.Pattern = TreeDesc::BarkPattern::Rings; d.PatternSpacing = 0.32f; d.PatternDepth = 0.9f; d.PatternColor = C3(0.72f, 0.74f, 0.58f);
			d.LeafColor = C3(0.26f, 0.42f, 0.12f); d.LeafColor2 = C3(0.38f, 0.5f, 0.15f); d.LeafLength = 0.4f;
			d.TrunkSway = 0.6f;
		});
		add("Moso Bamboo", ST, L::Canopy, { 12, 22, 0.6f, 1.0f, 35, 0.1f }, 0.9f, [](TreeDesc& d, TreeParams& p) {
			Deciduous(d, 20.0f, 0.09f, CR::Cylindrical, LS::Blade);
			p.TipRadius = 0.45f; p.Flare = 0.0f; p.Gnarl = 0.04f; p.Lean = 8.0f;
			p.Levels = 2;
			p.L[0] = Lv(34, 0.42f, 55.0f, 15.0f, 0.13f, 0.3f, 0.35f, 0.0f, 0.35f, 0.1f);
			p.L[1] = Lv(4, 0.2f, 45.0f, 15.0f, 0.55f, 0.25f, 0.4f, 0.0f, 0.5f, 0.15f);
			p.LeafCards = 6; p.LeavesPerCard = 7; p.LeafCardSize = 0.75f; p.LeafStart = 0.1f; p.CardSegments = 2; p.Droop = 0.55f;
			d.BarkColor = C3(0.4f, 0.48f, 0.22f); d.RidgeDepth = 0.04f; d.BarkSmoothness = 0.6f; d.Moss = 0.0f; d.BarkFleck = 0.0f;
			d.Pattern = TreeDesc::BarkPattern::Rings; d.PatternSpacing = 0.4f; d.PatternDepth = 0.9f; d.PatternColor = C3(0.75f, 0.76f, 0.62f);
			d.LeafColor = C3(0.28f, 0.44f, 0.12f); d.LeafColor2 = C3(0.4f, 0.52f, 0.16f); d.LeafLength = 0.42f; d.TrunkSway = 0.8f;
		});
		add("Black Bamboo", ST, L::SmallTree, { 12, 24, 0.6f, 1.0f, 35, 0.0f }, 0.5f, [](TreeDesc& d, TreeParams& p) {
			Deciduous(d, 8.0f, 0.04f, CR::Cylindrical, LS::Blade);
			p.TipRadius = 0.6f; p.Flare = 0.0f; p.Gnarl = 0.05f; p.Lean = 7.0f;
			p.Levels = 2;
			p.L[0] = Lv(24, 0.35f, 50.0f, 15.0f, 0.16f, 0.3f, 0.3f, 0.0f, 0.4f, 0.1f);
			p.L[1] = Lv(4, 0.2f, 45.0f, 15.0f, 0.55f, 0.25f, 0.4f, 0.0f, 0.5f, 0.15f);
			p.LeafCards = 5; p.LeavesPerCard = 7; p.LeafCardSize = 0.55f; p.CardSegments = 2; p.Droop = 0.55f;
			d.BarkColor = C3(0.1f, 0.09f, 0.08f); d.RidgeDepth = 0.04f; d.BarkSmoothness = 0.7f; d.Moss = 0.0f; d.BarkFleck = 0.0f;
			d.Pattern = TreeDesc::BarkPattern::Rings; d.PatternSpacing = 0.28f; d.PatternDepth = 0.9f; d.PatternColor = C3(0.32f, 0.3f, 0.24f);
			d.LeafColor = C3(0.26f, 0.42f, 0.12f); d.LeafColor2 = C3(0.36f, 0.48f, 0.15f); d.LeafLength = 0.4f; d.TrunkSway = 0.7f;
		});
		add("Dwarf Bamboo", ST, L::Shrub, { 6, 22, 0.55f, 1.0f, 40, 0.0f }, 0.9f, [](TreeDesc& d, TreeParams& p) {
			Shrub(d, 1.3f, LS::Blade);
			p.Gnarl = 0.08f; p.CrownShape = CR::Cylindrical;
			p.L[0] = Lv(12, 0.0f, 12.0f, 8.0f, 1.0f, 0.2f, 0.1f, 0.3f, 0.7f, 0.1f);
			p.L[1] = Lv(3, 0.5f, 55.0f, 15.0f, 0.3f, 0.2f, 0.2f, 0.0f, 0.6f, 0.1f);
			p.LeavesPerCard = 3; p.LeafCards = 4; p.LeafCardSize = 0.35f; p.CardSegments = 2; p.Droop = 0.6f;
			d.BarkColor = C3(0.4f, 0.48f, 0.22f); d.Pattern = TreeDesc::BarkPattern::Rings; d.PatternSpacing = 0.12f; d.PatternDepth = 0.8f; d.PatternColor = C3(0.6f, 0.62f, 0.45f);
			d.LeafColor = C3(0.24f, 0.42f, 0.12f); d.LeafColor2 = C3(0.5f, 0.52f, 0.28f); d.LeafLength = 0.5f;
		});
		add("Southern Magnolia", ST, L::Canopy, { 14, 22, 0.6f, 1.0f, 30, 0.0f }, 0.7f, [](TreeDesc& d, TreeParams& p) {
			Deciduous(d, 18.0f, 0.4f, CR::Conical, LS::Oval);
			p.LeavesPerCard = 4; p.LeafCardSize = 0.9f; p.FlowerPetals = 6; p.FlowerCards = 1; p.FlowerSize = 0.28f;
			d.BarkColor = C3(0.45f, 0.42f, 0.38f); d.RidgeDepth = 0.15f;
			d.LeafColor = C3(0.08f, 0.24f, 0.08f); d.LeafColor2 = C3(0.14f, 0.3f, 0.1f); d.LeafSmoothness = 0.7f; d.LeafLength = 0.42f;
			d.FlowerColor = C3(0.97f, 0.95f, 0.88f); d.FlowerCenter = C3(0.85f, 0.75f, 0.45f);
		});
		add("Camellia", ST, L::Shrub, { 12, 22, 0.55f, 1.0f, 30, 0.0f }, 0.6f, [](TreeDesc& d, TreeParams& p) {
			Shrub(d, 2.5f, LS::Oval);
			p.FlowerPetals = 7; p.FlowerCards = 2; p.FlowerSize = 0.1f;
			d.LeafColor = C3(0.08f, 0.24f, 0.08f); d.LeafColor2 = C3(0.12f, 0.3f, 0.1f); d.LeafSmoothness = 0.7f;
			d.FlowerColor = C3(0.85f, 0.12f, 0.2f); d.FlowerCenter = C3(0.95f, 0.8f, 0.2f);
		});
		add("Live Oak", ST, L::Canopy, { 15, 24, 0.45f, 0.95f, 28, 0.0f }, 0.9f, [](TreeDesc& d, TreeParams& p) {
			Deciduous(d, 14.0f, 0.6f, CR::Hemispherical, LS::Oval);
			p.Gnarl = 0.4f;
			p.L[0] = Lv(9, 0.25f, 72.0f, 15.0f, 0.85f, 0.25f, 0.25f, 0.05f, 0.5f, 0.45f);
			p.LeavesPerCard = 7; p.LeafCardSize = 0.7f;
			d.BarkColor = C3(0.3f, 0.27f, 0.24f); d.RidgeDepth = 1.0f; d.Moss = 0.5f; d.MossColor = C3(0.45f, 0.48f, 0.38f);
			d.LeafColor = C3(0.14f, 0.28f, 0.1f); d.LeafColor2 = C3(0.24f, 0.36f, 0.14f); d.LeafLength = 0.22f;
		});
		add("Sweetgum", ST, L::Canopy, { 12, 22, 0.5f, 1.0f, 30, 0.1f }, 0.8f, [](TreeDesc& d, TreeParams& p) {
			Deciduous(d, 20.0f, 0.38f, CR::Conical, LS::Lobed);
			p.LeavesPerCard = 4; p.LeafCardSize = 0.8f;
			d.BarkColor = C3(0.36f, 0.3f, 0.26f); d.RidgeDepth = 1.0f; d.RidgeFrequency = 12.0f;
			d.LeafColor = C3(0.2f, 0.38f, 0.1f); d.LeafColor2 = C3(0.55f, 0.32f, 0.12f); d.LeafVariation = 0.45f; d.LeafLength = 0.38f;
		});
		add("Azalea", ST, L::Shrub, { 10, 22, 0.55f, 1.0f, 35, 0.0f }, 0.7f, [](TreeDesc& d, TreeParams& p) {
			Shrub(d, 1.2f, LS::Oval);
			p.FlowerPetals = 5; p.FlowerCards = 4; p.FlowerSize = 0.08f;
			d.LeafColor = C3(0.16f, 0.32f, 0.1f); d.FlowerColor = C3(0.95f, 0.45f, 0.65f); d.FlowerCenter = C3(0.9f, 0.3f, 0.5f);
		});
		add("Saw Palmetto", ST, L::Shrub, { 16, 26, 0.4f, 0.95f, 25, 0.1f }, 0.9f, [](TreeDesc& d, TreeParams& p) {
			Rosette(d, 1.3f, 12, LS::Blade, 0.25f);
			p.LeavesPerCard = 9; p.CardSpread = 50.0f; p.Droop = 0.35f; p.Shape = FM::Fern;
			d.LeafColor = C3(0.22f, 0.36f, 0.22f); d.LeafColor2 = C3(0.32f, 0.44f, 0.28f);
		});
		add("Bald Cypress", ST, L::Canopy, { 10, 24, 0.6f, 1.0f, 15, 1.0f }, 1.0f, [](TreeDesc& d, TreeParams& p) {
			Conifer(d, 24.0f, 0.55f, 26);
			p.Flare = 2.0f; p.CrownShape = CR::TaperedCylindrical;
			p.L[0] = Lv(26, 0.3f, 80.0f, 10.0f, 0.32f, 0.15f, 0.25f, 0.05f, 0.3f, 0.15f);
			p.LeavesPerCard = 12; p.LeafCards = 10;
			d.BarkColor = C3(0.5f, 0.42f, 0.36f); d.Moss = 0.45f; d.MossColor = C3(0.46f, 0.48f, 0.4f);
			d.LeafColor = C3(0.28f, 0.42f, 0.14f); d.LeafColor2 = C3(0.4f, 0.48f, 0.18f);
		});
		add("Spider Lily", ST, L::Flower, { 12, 26, 0.55f, 1.0f, 30, 0.4f }, 0.5f, [](TreeDesc& d, TreeParams& p) {
			Flower(d, 0.5f, 3, 6, 0.16f, C3(0.88f, 0.12f, 0.12f), C3(0.9f, 0.2f, 0.15f));
		});

		// ================================================================ 온대 낙엽수림 (6 ~ 15 °C, 습도 0.45 ~ 0.85)
		const char* TF = "Temperate Forest";
		add("Maple", TF, L::Canopy, { 4, 14, 0.45f, 0.9f, 32, 0.0f }, 1.0f, [](TreeDesc& d, TreeParams& p) {
			Deciduous(d, 15.0f, 0.34f, CR::Spherical, LS::Lobed);
			p.LeavesPerCard = 4; p.LeafCardSize = 0.85f;
			d.BarkColor = C3(0.4f, 0.36f, 0.32f); d.RidgeDepth = 0.6f;
			d.LeafColor = C3(0.22f, 0.42f, 0.1f); d.LeafColor2 = C3(0.42f, 0.5f, 0.14f); d.LeafLength = 0.4f;
		});
		add("Red Maple", TF, L::Canopy, { 4, 16, 0.5f, 1.0f, 32, 0.3f }, 0.5f, [](TreeDesc& d, TreeParams& p) {
			Deciduous(d, 14.0f, 0.3f, CR::Spherical, LS::Lobed);
			p.LeavesPerCard = 4; p.LeafCardSize = 0.8f;
			d.BarkColor = C3(0.45f, 0.42f, 0.4f); d.RidgeDepth = 0.4f;
			d.LeafColor = C3(0.62f, 0.14f, 0.08f); d.LeafColor2 = C3(0.82f, 0.36f, 0.1f); d.LeafLength = 0.4f;
		});
		add("Linden", TF, L::Canopy, { 5, 14, 0.45f, 0.9f, 30, 0.0f }, 0.8f, [](TreeDesc& d, TreeParams& p) {
			Deciduous(d, 18.0f, 0.4f, CR::Conical, LS::Round);
			p.LeavesPerCard = 5; p.LeafCardSize = 0.8f;
			d.BarkColor = C3(0.38f, 0.34f, 0.3f); d.RidgeDepth = 0.7f;
			d.LeafColor = C3(0.25f, 0.44f, 0.12f); d.LeafColor2 = C3(0.42f, 0.54f, 0.16f); d.LeafLength = 0.32f;
		});
		add("Chestnut", TF, L::Canopy, { 7, 15, 0.45f, 0.85f, 30, 0.0f }, 0.7f, [](TreeDesc& d, TreeParams& p) {
			Deciduous(d, 17.0f, 0.5f, CR::Spherical, LS::Blade);
			p.LeavesPerCard = 7; p.LeafCardSize = 0.8f; p.CardSegments = 1;
			d.BarkColor = C3(0.36f, 0.3f, 0.26f); d.RidgeDepth = 1.1f; d.RidgeFrequency = 10.0f;
			d.LeafColor = C3(0.2f, 0.38f, 0.1f); d.LeafColor2 = C3(0.34f, 0.46f, 0.12f); d.LeafLength = 0.3f;
		});
		add("Ash", TF, L::Canopy, { 4, 14, 0.45f, 0.95f, 30, 0.2f }, 0.8f, [](TreeDesc& d, TreeParams& p) {
			Deciduous(d, 20.0f, 0.36f, CR::TaperedCylindrical, LS::Frond);
			p.LeavesPerCard = 10; p.LeafCardSize = 0.85f;
			d.BarkColor = C3(0.46f, 0.44f, 0.4f); d.RidgeDepth = 0.6f; d.RidgeFrequency = 20.0f;
			d.LeafColor = C3(0.24f, 0.42f, 0.11f); d.LeafColor2 = C3(0.36f, 0.5f, 0.14f); d.LeafLength = 0.28f;
		});
		add("Hornbeam", TF, L::Canopy, { 6, 14, 0.45f, 0.9f, 30, 0.0f }, 0.7f, [](TreeDesc& d, TreeParams& p) {
			Deciduous(d, 14.0f, 0.3f, CR::Spherical, LS::Oval);
			d.BarkColor = C3(0.5f, 0.5f, 0.47f); d.RidgeDepth = 0.12f; d.LeafColor = C3(0.22f, 0.42f, 0.12f); d.LeafColor2 = C3(0.36f, 0.5f, 0.15f);
		});
		add("Elm", TF, L::Canopy, { 4, 16, 0.45f, 0.95f, 30, 0.2f }, 0.6f, [](TreeDesc& d, TreeParams& p) {
			Deciduous(d, 20.0f, 0.42f, CR::InverseConical, LS::Oval);
			p.L[0] = Lv(10, 0.4f, 35.0f, 12.0f, 0.6f, 0.2f, 0.15f, 0.2f, 0.5f, 0.2f);
			p.L[1] = Lv(7, 0.2f, 45.0f, 15.0f, 0.5f, 0.2f, 0.35f, 0.0f, 0.5f, 0.25f);
			d.BarkColor = C3(0.36f, 0.32f, 0.28f); d.RidgeDepth = 1.0f;
		});
		add("Walnut", TF, L::Canopy, { 7, 16, 0.4f, 0.85f, 28, 0.0f }, 0.5f, [](TreeDesc& d, TreeParams& p) {
			Deciduous(d, 16.0f, 0.42f, CR::Hemispherical, LS::Frond);
			p.LeavesPerCard = 8; p.LeafCardSize = 0.9f;
			d.BarkColor = C3(0.42f, 0.38f, 0.34f); d.RidgeDepth = 1.2f; d.RidgeFrequency = 9.0f;
			d.LeafColor = C3(0.22f, 0.38f, 0.1f); d.LeafColor2 = C3(0.34f, 0.46f, 0.13f); d.LeafLength = 0.32f;
		});
		add("Cherry Blossom", TF, L::SmallTree, { 6, 18, 0.45f, 0.9f, 30, 0.0f }, 0.35f, [](TreeDesc& d, TreeParams& p) {
			Deciduous(d, 7.0f, 0.22f, CR::Hemispherical, LS::Oval);
			p.Gnarl = 0.3f; p.LeafCards = 6;
			p.FlowerPetals = 5; p.FlowerCards = 10; p.FlowerSize = 0.14f;
			d.BarkColor = C3(0.32f, 0.24f, 0.22f); d.RidgeDepth = 0.3f; d.RidgeFrequency = 28.0f;
			d.LeafColor = C3(0.36f, 0.5f, 0.16f); d.LeafColor2 = C3(0.5f, 0.5f, 0.2f);
			d.FlowerColor = C3(0.98f, 0.76f, 0.84f); d.FlowerCenter = C3(0.9f, 0.5f, 0.6f);
		});
		add("Dogwood", TF, L::SmallTree, { 6, 18, 0.5f, 0.95f, 30, 0.0f }, 0.5f, [](TreeDesc& d, TreeParams& p) {
			Deciduous(d, 6.0f, 0.16f, CR::Hemispherical, LS::Oval);
			p.FlowerPetals = 4; p.FlowerCards = 4; p.FlowerSize = 0.12f;
			d.BarkColor = C3(0.36f, 0.32f, 0.28f); d.FlowerColor = C3(0.97f, 0.96f, 0.92f); d.FlowerCenter = C3(0.7f, 0.8f, 0.4f);
		});
		add("Rowan", TF, L::SmallTree, { 0, 12, 0.4f, 0.95f, 35, 0.0f }, 0.5f, [](TreeDesc& d, TreeParams& p) {
			Deciduous(d, 8.0f, 0.18f, CR::Spherical, LS::Frond);
			p.LeavesPerCard = 10; p.LeafCardSize = 0.6f; p.FlowerPetals = 12; p.FlowerCards = 3; p.FlowerSize = 0.12f;
			d.BarkColor = C3(0.44f, 0.42f, 0.38f); d.RidgeDepth = 0.15f; d.LeafLength = 0.25f;
			d.FlowerColor = C3(0.85f, 0.2f, 0.1f); d.FlowerCenter = C3(0.75f, 0.15f, 0.08f);
		});
		add("Hazel", TF, L::Shrub, { 4, 15, 0.45f, 0.9f, 35, 0.0f }, 1.0f, [](TreeDesc& d, TreeParams& p) {
			Shrub(d, 3.0f, LS::Round);
			p.CrownShape = CR::InverseConical; p.LeafCardSize = 0.55f;
			d.BarkColor = C3(0.42f, 0.34f, 0.28f); d.LeafColor = C3(0.25f, 0.44f, 0.12f); d.LeafColor2 = C3(0.4f, 0.52f, 0.15f);
		});
		add("Hawthorn", TF, L::Shrub, { 4, 15, 0.35f, 0.85f, 35, 0.0f }, 0.8f, [](TreeDesc& d, TreeParams& p) {
			Shrub(d, 3.5f, LS::Lobed);
			p.Gnarl = 0.4f; p.LeafCardSize = 0.45f; p.FlowerPetals = 5; p.FlowerCards = 4; p.FlowerSize = 0.06f;
			d.FlowerColor = C3(0.97f, 0.96f, 0.93f); d.FlowerCenter = C3(0.8f, 0.6f, 0.65f);
		});
		add("Elderberry", TF, L::Shrub, { 3, 15, 0.5f, 0.95f, 35, 0.3f }, 0.7f, [](TreeDesc& d, TreeParams& p) {
			Shrub(d, 2.6f, LS::Frond);
			p.LeavesPerCard = 7; p.FlowerPetals = 14; p.FlowerCards = 2; p.FlowerSize = 0.16f;
			d.FlowerColor = C3(0.96f, 0.94f, 0.82f); d.FlowerCenter = C3(0.9f, 0.88f, 0.7f);
		});
		add("Bluebell", TF, L::Flower, { 5, 14, 0.55f, 0.95f, 35, 0.0f }, 1.0f, [](TreeDesc& d, TreeParams& p) {
			Flower(d, 0.3f, 3, 6, 0.035f, C3(0.4f, 0.42f, 0.88f), C3(0.35f, 0.38f, 0.8f));
			p.FlowerCards = 5; p.LeafCards = 5; p.LeavesPerCard = 1; d.LeafLength = 0.3f;
		});
		add("Foxglove", TF, L::Flower, { 4, 15, 0.5f, 0.95f, 35, 0.0f }, 0.5f, [](TreeDesc& d, TreeParams& p) {
			Flower(d, 1.2f, 2, 5, 0.06f, C3(0.82f, 0.38f, 0.66f), C3(0.6f, 0.2f, 0.45f));
			p.FlowerCards = 9; p.Leaf = LS::Broad; p.LeafCards = 6; p.LeavesPerCard = 2; p.LeafCardSize = 0.4f; p.CardSegments = 1;
		});
		add("Wood Anemone", TF, L::Flower, { 3, 14, 0.5f, 0.95f, 35, 0.0f }, 1.0f, [](TreeDesc& d, TreeParams& p) {
			Flower(d, 0.18f, 4, 6, 0.04f, C3(0.97f, 0.96f, 0.95f), C3(0.95f, 0.85f, 0.3f));
			p.Leaf = LS::Lobed; p.LeafCards = 4; p.LeavesPerCard = 3; p.LeafCardSize = 0.12f; p.CardSegments = 1;
		});
		add("Bracken", TF, L::Fern, { 2, 18, 0.4f, 0.95f, 40, 0.0f }, 1.4f, [](TreeDesc& d, TreeParams& p) {
			Fern(d, 1.0f, 7);
			p.CardSpread = 35.0f; p.Droop = 0.8f;
			d.LeafColor = C3(0.28f, 0.44f, 0.12f); d.LeafColor2 = C3(0.46f, 0.48f, 0.16f);
		});
		add("Lady Fern", TF, L::Fern, { 2, 16, 0.55f, 1.0f, 40, 0.2f }, 1.0f, [](TreeDesc& d, TreeParams& p) {
			Fern(d, 0.7f, 10);
			d.LeafColor = C3(0.28f, 0.48f, 0.12f); d.LeafColor2 = C3(0.4f, 0.56f, 0.16f);
		});
		add("Woodland Grass", TF, L::Grass, { 3, 16, 0.4f, 0.95f, 40, 0.0f }, 1.2f, [](TreeDesc& d, TreeParams& p) {
			Grass(d, 0.4f, 6, 6, 0.24f);
			d.LeafColor = C3(0.24f, 0.42f, 0.1f); d.LeafColor2 = C3(0.38f, 0.5f, 0.14f);
		});
		add("Ivy Groundcover", TF, L::Grass, { 5, 18, 0.5f, 1.0f, 45, 0.0f }, 0.8f, [](TreeDesc& d, TreeParams& p) {
			Rosette(d, 0.25f, 10, LS::Lobed, 0.4f);
			p.Shape = FM::Grass; p.LeavesPerCard = 3; p.CardSpread = 75.0f; p.Droop = 0.6f;
			d.LeafColor = C3(0.1f, 0.26f, 0.08f); d.LeafColor2 = C3(0.18f, 0.34f, 0.1f); d.LeafSmoothness = 0.6f;
			SizeLod(d);
		});

		// ================================================================ 온대 우림 (7 ~ 14 °C, 습도 0.75 ~ 1)
		const char* TRF = "Temperate Rainforest";
		add("Douglas Fir", TRF, L::Canopy, { 5, 14, 0.6f, 1.0f, 38, 0.0f }, 1.2f, [](TreeDesc& d, TreeParams& p) {
			Conifer(d, 40.0f, 0.75f, 52);
			p.L[0] = Lv(52, 0.25f, 88.0f, 8.0f, 0.22f, 0.12f, 0.4f, 0.0f, 0.3f, 0.06f);
			d.BarkColor = C3(0.36f, 0.26f, 0.2f); d.RidgeDepth = 1.4f; d.RidgeFrequency = 8.0f; d.Moss = 0.35f;
			d.LeafColor = C3(0.1f, 0.24f, 0.1f); d.LeafColor2 = C3(0.16f, 0.3f, 0.12f);
		});
		add("Western Red Cedar", TRF, L::Canopy, { 5, 14, 0.7f, 1.0f, 35, 0.3f }, 1.0f, [](TreeDesc& d, TreeParams& p) {
			Conifer(d, 34.0f, 0.8f, 46);
			p.Flare = 1.2f; p.Leaf = LS::Scale; p.LeavesPerCard = 12; p.LeafCardSize = 0.9f;
			p.L[0] = Lv(46, 0.12f, 95.0f, 8.0f, 0.25f, 0.12f, 0.55f, 0.1f, 0.3f, 0.08f);
			d.BarkColor = C3(0.48f, 0.34f, 0.28f); d.RidgeDepth = 1.0f; d.RidgeFrequency = 22.0f; d.Moss = 0.3f;
			d.LeafColor = C3(0.12f, 0.28f, 0.1f); d.LeafColor2 = C3(0.2f, 0.36f, 0.12f);
		});
		add("Sitka Spruce", TRF, L::Canopy, { 4, 12, 0.7f, 1.0f, 35, 0.2f }, 0.8f, [](TreeDesc& d, TreeParams& p) {
			Conifer(d, 38.0f, 0.8f, 50);
			d.BarkColor = C3(0.4f, 0.34f, 0.3f); d.RidgeDepth = 0.6f; d.RidgeFrequency = 22.0f; d.Moss = 0.45f;
			d.LeafColor = C3(0.12f, 0.24f, 0.16f); d.LeafColor2 = C3(0.2f, 0.32f, 0.2f);
		});
		add("Western Hemlock", TRF, L::Canopy, { 4, 13, 0.7f, 1.0f, 38, 0.0f }, 0.9f, [](TreeDesc& d, TreeParams& p) {
			Conifer(d, 30.0f, 0.6f, 44);
			p.L[0] = Lv(44, 0.12f, 100.0f, 8.0f, 0.26f, 0.12f, 0.8f, 0.0f, 0.3f, 0.08f);
			p.L[1] = Lv(8, 0.1f, 55.0f, 10.0f, 0.42f, 0.2f, 0.6f, 0.0f, 0.5f, 0.1f);
			d.BarkColor = C3(0.38f, 0.3f, 0.26f); d.Moss = 0.45f;
			d.LeafColor = C3(0.12f, 0.26f, 0.1f); d.LeafColor2 = C3(0.2f, 0.34f, 0.12f);
		});
		add("Redwood", TRF, L::Canopy, { 9, 16, 0.75f, 1.0f, 30, 0.0f }, 0.5f, [](TreeDesc& d, TreeParams& p) {
			Conifer(d, 58.0f, 1.4f, 40);
			p.Flare = 0.9f; p.L[0] = Lv(40, 0.45f, 92.0f, 8.0f, 0.18f, 0.12f, 0.3f, 0.05f, 0.25f, 0.06f);
			d.BarkColor = C3(0.55f, 0.32f, 0.22f); d.RidgeDepth = 1.6f; d.RidgeFrequency = 7.0f; d.Moss = 0.15f;
			d.LeafColor = C3(0.12f, 0.26f, 0.1f); d.LeafColor2 = C3(0.2f, 0.32f, 0.12f);
		});
		add("Bigleaf Maple", TRF, L::Canopy, { 6, 14, 0.65f, 1.0f, 32, 0.3f }, 0.7f, [](TreeDesc& d, TreeParams& p) {
			Deciduous(d, 22.0f, 0.5f, CR::Spherical, LS::Lobed);
			p.LeavesPerCard = 3; p.LeafCardSize = 1.1f;
			d.BarkColor = C3(0.4f, 0.37f, 0.33f); d.Moss = 0.7f; d.MossColor = C3(0.3f, 0.44f, 0.14f);
			d.LeafColor = C3(0.24f, 0.44f, 0.12f); d.LeafColor2 = C3(0.4f, 0.52f, 0.15f); d.LeafLength = 0.45f;
		});
		add("Sword Fern", TRF, L::Fern, { 4, 16, 0.65f, 1.0f, 45, 0.0f }, 1.5f, [](TreeDesc& d, TreeParams& p) {
			Fern(d, 0.9f, 14);
			p.CardSpread = 40.0f; d.LeafColor = C3(0.12f, 0.3f, 0.08f); d.LeafColor2 = C3(0.18f, 0.36f, 0.1f); d.LeafSmoothness = 0.5f;
		});
		add("Salal", TRF, L::Shrub, { 4, 14, 0.6f, 1.0f, 40, 0.0f }, 1.0f, [](TreeDesc& d, TreeParams& p) {
			Shrub(d, 1.0f, LS::Oval);
			d.LeafColor = C3(0.12f, 0.28f, 0.08f); d.LeafColor2 = C3(0.2f, 0.34f, 0.1f); d.LeafSmoothness = 0.6f;
		});
		add("Moss Carpet", TRF, L::Grass, { 2, 16, 0.75f, 1.0f, 50, 0.0f }, 0.9f, [](TreeDesc& d, TreeParams& p) {
			Rosette(d, 0.12f, 9, LS::Scale, 0.3f);
			p.Shape = FM::Grass; p.LeavesPerCard = 12; p.CardSpread = 80.0f; p.Droop = 0.3f; p.CardSegments = 1;
			d.LeafColor = C3(0.22f, 0.4f, 0.1f); d.LeafColor2 = C3(0.36f, 0.48f, 0.12f); d.LeafTransmission = 0.4f;
			SizeLod(d);
		});

		// ================================================================ 초원 · 스텝 (2 ~ 16 °C, 습도 0.15 ~ 0.5)
		const char* GL = "Grassland";
		add("Feather Grass", GL, L::Grass, { 2, 16, 0.15f, 0.45f, 35, 0.0f }, 1.4f, [](TreeDesc& d, TreeParams& p) {
			Grass(d, 0.7f, 6, 8, 0.15f);
			p.Droop = 0.9f;
			d.LeafColor = C3(0.6f, 0.6f, 0.42f); d.LeafColor2 = C3(0.75f, 0.72f, 0.55f); d.LeafTransmission = 0.9f;
		});
		add("Fescue", GL, L::Grass, { 0, 16, 0.25f, 0.65f, 40, 0.0f }, 1.6f, [](TreeDesc& d, TreeParams& p) {
			Grass(d, 0.35f, 6, 8, 0.18f);
			d.LeafColor = C3(0.3f, 0.44f, 0.18f); d.LeafColor2 = C3(0.46f, 0.52f, 0.24f);
		});
		add("Big Bluestem", GL, L::Grass, { 4, 18, 0.3f, 0.6f, 35, 0.0f }, 1.0f, [](TreeDesc& d, TreeParams& p) {
			Grass(d, 1.5f, 6, 5, 0.25f);
			d.LeafColor = C3(0.34f, 0.44f, 0.3f); d.LeafColor2 = C3(0.55f, 0.38f, 0.3f); d.LeafVariation = 0.7f;
		});
		add("Meadow Grass", GL, L::Grass, { 2, 18, 0.3f, 0.8f, 40, 0.0f }, 1.6f, [](TreeDesc& d, TreeParams& p) {
			Grass(d, 0.5f, 7, 7, 0.24f);
		});
		add("Sunflower", GL, L::Flower, { 8, 22, 0.3f, 0.65f, 25, 0.0f }, 0.3f, [](TreeDesc& d, TreeParams& p) {
			Flower(d, 1.8f, 1, 16, 0.32f, C3(0.98f, 0.78f, 0.08f), C3(0.3f, 0.18f, 0.08f));
			p.Leaf = LS::Broad; p.LeafCards = 6; p.LeavesPerCard = 2; p.LeafCardSize = 0.4f; p.CardSegments = 1; p.Radius = 0.02f;
		});
		add("Purple Coneflower", GL, L::Flower, { 4, 18, 0.25f, 0.6f, 35, 0.0f }, 0.8f, [](TreeDesc& d, TreeParams& p) {
			Flower(d, 0.8f, 4, 12, 0.1f, C3(0.85f, 0.45f, 0.65f), C3(0.6f, 0.32f, 0.12f));
		});
		add("Red Poppy", GL, L::Flower, { 6, 20, 0.2f, 0.6f, 35, 0.0f }, 1.0f, [](TreeDesc& d, TreeParams& p) {
			Flower(d, 0.5f, 4, 4, 0.08f, C3(0.92f, 0.12f, 0.06f), C3(0.1f, 0.08f, 0.08f));
		});
		add("Cornflower", GL, L::Flower, { 4, 18, 0.25f, 0.65f, 35, 0.0f }, 0.9f, [](TreeDesc& d, TreeParams& p) {
			Flower(d, 0.5f, 5, 9, 0.05f, C3(0.28f, 0.42f, 0.92f), C3(0.3f, 0.3f, 0.75f));
		});
		add("Yarrow", GL, L::Flower, { 0, 18, 0.2f, 0.65f, 40, 0.0f }, 0.9f, [](TreeDesc& d, TreeParams& p) {
			Flower(d, 0.55f, 4, 14, 0.09f, C3(0.95f, 0.94f, 0.88f), C3(0.88f, 0.85f, 0.7f));
			p.Leaf = LS::Frond; p.LeavesPerCard = 12; p.LeafCards = 5; p.LeafCardSize = 0.22f;
		});
		add("Lupine", GL, L::Flower, { 2, 16, 0.3f, 0.75f, 35, 0.0f }, 0.7f, [](TreeDesc& d, TreeParams& p) {
			Flower(d, 0.8f, 4, 5, 0.05f, C3(0.45f, 0.38f, 0.85f), C3(0.9f, 0.9f, 0.95f));
			p.FlowerCards = 8; p.Leaf = LS::Lobed; p.LeafCards = 6; p.LeavesPerCard = 4; p.LeafCardSize = 0.25f; p.CardSegments = 1;
		});
		add("Sagebrush", GL, L::Shrub, { 0, 14, 0.05f, 0.35f, 35, 0.0f }, 1.3f, [](TreeDesc& d, TreeParams& p) {
			Shrub(d, 1.1f, LS::Oval);
			p.Gnarl = 0.45f; p.LeavesPerCard = 8; p.LeafCardSize = 0.3f;
			d.BarkColor = C3(0.42f, 0.38f, 0.32f); d.LeafColor = C3(0.5f, 0.55f, 0.48f); d.LeafColor2 = C3(0.6f, 0.62f, 0.55f); d.LeafLength = 0.2f;
		});
		add("Wild Rose", GL, L::Shrub, { 2, 16, 0.3f, 0.7f, 35, 0.0f }, 0.6f, [](TreeDesc& d, TreeParams& p) {
			Shrub(d, 1.5f, LS::Frond);
			p.LeavesPerCard = 7; p.FlowerPetals = 5; p.FlowerCards = 3; p.FlowerSize = 0.07f;
			d.FlowerColor = C3(0.95f, 0.58f, 0.68f); d.FlowerCenter = C3(0.95f, 0.85f, 0.3f);
		});
		add("Tumbleweed", GL, L::Shrub, { 6, 24, 0.0f, 0.3f, 30, 0.0f }, 0.4f, [](TreeDesc& d, TreeParams& p) {
			Shrub(d, 0.8f, LS::Needle);
			p.CrownShape = CR::Spherical; p.Levels = 3;
			p.L[2] = Lv(4, 0.2f, 50.0f, 20.0f, 0.5f, 0.3f, 0.0f, 0.1f, 0.6f, 0.5f);
			p.LeafCards = 2; p.LeavesPerCard = 4; p.LeafCardSize = 0.15f;
			d.BarkColor = C3(0.62f, 0.54f, 0.4f); d.LeafColor = C3(0.58f, 0.52f, 0.36f); d.LeafColor2 = C3(0.6f, 0.55f, 0.4f);
		});
		add("Clover", GL, L::Grass, { 0, 18, 0.4f, 0.9f, 35, 0.0f }, 0.9f, [](TreeDesc& d, TreeParams& p) {
			Flower(d, 0.15f, 3, 14, 0.035f, C3(0.95f, 0.92f, 0.95f), C3(0.9f, 0.85f, 0.88f));
			p.Leaf = LS::Round; p.LeafCards = 6; p.LeavesPerCard = 3; p.LeafCardSize = 0.07f; p.CardSegments = 1; p.CardSpread = 70.0f;
		});
		add("Dandelion", GL, L::Flower, { 0, 20, 0.3f, 0.85f, 40, 0.0f }, 1.0f, [](TreeDesc& d, TreeParams& p) {
			Flower(d, 0.22f, 2, 16, 0.045f, C3(0.98f, 0.82f, 0.08f), C3(0.95f, 0.7f, 0.05f));
			p.Leaf = LS::Lobed; p.LeafCards = 6; p.LeavesPerCard = 1; p.LeafCardSize = 0.18f; p.CardSpread = 70.0f;
		});

		// ================================================================ 냉대 (타이가, -6 ~ 6 °C, 습도 0.35 ~ 0.9)
		const char* BF = "Boreal Forest";
		add("Aspen", BF, L::Canopy, { -4, 10, 0.35f, 0.85f, 30, 0.0f }, 1.0f, [](TreeDesc& d, TreeParams& p) {
			Deciduous(d, 16.0f, 0.22f, CR::TaperedCylindrical, LS::Round);
			p.L[0] = Lv(14, 0.45f, 40.0f, 10.0f, 0.38f, 0.2f, 0.2f, 0.2f, 0.4f, 0.15f);
			p.LeavesPerCard = 6; p.LeafCardSize = 0.55f;
			d.BarkColor = C3(0.78f, 0.78f, 0.7f); d.RidgeDepth = 0.1f; d.BarkFleck = -0.5f; d.Moss = 0.05f;
			d.LeafColor = C3(0.36f, 0.52f, 0.14f); d.LeafColor2 = C3(0.6f, 0.6f, 0.16f); d.LeafFlutter = 0.08f; d.LeafLength = 0.25f;
		});
		add("Black Spruce", BF, L::Canopy, { -8, 4, 0.45f, 1.0f, 30, 0.3f }, 1.0f, [](TreeDesc& d, TreeParams& p) {
			Conifer(d, 12.0f, 0.18f, 40);
			p.CrownShape = CR::TaperedCylindrical;
			p.L[0] = Lv(40, 0.05f, 96.0f, 10.0f, 0.18f, 0.2f, 0.65f, 0.0f, 0.3f, 0.1f);
			d.LeafColor = C3(0.07f, 0.16f, 0.1f); d.LeafColor2 = C3(0.12f, 0.22f, 0.12f);
		});
		add("Jack Pine", BF, L::Canopy, { -6, 6, 0.25f, 0.7f, 30, 0.0f }, 0.7f, [](TreeDesc& d, TreeParams& p) {
			Conifer(d, 16.0f, 0.26f, 22);
			p.Gnarl = 0.2f; p.CrownShape = CR::Spherical;
			p.L[0] = Lv(22, 0.4f, 70.0f, 15.0f, 0.3f, 0.2f, 0.2f, 0.05f, 0.35f, 0.25f);
			d.BarkColor = C3(0.36f, 0.28f, 0.22f); d.LeafColor = C3(0.16f, 0.28f, 0.12f); d.LeafColor2 = C3(0.22f, 0.34f, 0.14f);
		});
		add("Willow Shrub", BF, L::Shrub, { -8, 10, 0.5f, 1.0f, 30, 0.6f }, 0.9f, [](TreeDesc& d, TreeParams& p) {
			Shrub(d, 1.8f, LS::Blade);
			p.LeavesPerCard = 6; p.LeafCardSize = 0.4f; p.CardSegments = 1;
			d.BarkColor = C3(0.5f, 0.32f, 0.22f); d.LeafColor = C3(0.36f, 0.46f, 0.3f); d.LeafColor2 = C3(0.48f, 0.54f, 0.4f); d.LeafLength = 0.18f;
		});
		add("Blueberry", BF, L::Shrub, { -6, 10, 0.4f, 0.95f, 35, 0.0f }, 1.3f, [](TreeDesc& d, TreeParams& p) {
			Shrub(d, 0.45f, LS::Oval);
			p.FlowerPetals = 12; p.FlowerCards = 2; p.FlowerSize = 0.02f; p.LeafCardSize = 0.16f;
			d.LeafColor = C3(0.2f, 0.38f, 0.12f); d.FlowerColor = C3(0.25f, 0.3f, 0.6f); d.FlowerCenter = C3(0.2f, 0.22f, 0.45f);
		});
		add("Fireweed", BF, L::Flower, { -6, 12, 0.35f, 0.9f, 35, 0.0f }, 0.9f, [](TreeDesc& d, TreeParams& p) {
			Flower(d, 1.2f, 3, 4, 0.05f, C3(0.88f, 0.35f, 0.62f), C3(0.75f, 0.25f, 0.5f));
			p.FlowerCards = 10; p.Leaf = LS::Blade; p.LeafCards = 6; p.LeavesPerCard = 4; p.LeafCardSize = 0.35f;
		});
		add("Taiga Grass", BF, L::Grass, { -8, 8, 0.35f, 0.9f, 40, 0.0f }, 1.0f, [](TreeDesc& d, TreeParams& p) {
			Grass(d, 0.35f, 6, 6, 0.2f);
			d.LeafColor = C3(0.34f, 0.42f, 0.18f); d.LeafColor2 = C3(0.5f, 0.48f, 0.26f);
		});
		add("Reindeer Lichen", BF, L::Grass, { -12, 6, 0.2f, 0.8f, 40, 0.0f }, 0.8f, [](TreeDesc& d, TreeParams& p) {
			Rosette(d, 0.1f, 10, LS::Scale, 0.3f);
			p.Shape = FM::Grass; p.LeavesPerCard = 10; p.CardSpread = 50.0f; p.CardSegments = 1;
			d.LeafColor = C3(0.75f, 0.78f, 0.68f); d.LeafColor2 = C3(0.68f, 0.7f, 0.6f); d.LeafTransmission = 0.2f;
			SizeLod(d);
		});

		// ================================================================ 고산 · 아고산 (-6 ~ 6 °C)
		const char* MT = "Montane";
		add("Swiss Stone Pine", MT, L::Canopy, { -6, 4, 0.3f, 0.9f, 40, 0.0f }, 0.8f, [](TreeDesc& d, TreeParams& p) {
			Conifer(d, 14.0f, 0.4f, 30);
			p.Gnarl = 0.15f; p.CrownShape = CR::TaperedCylindrical;
			p.L[0] = Lv(30, 0.15f, 70.0f, 12.0f, 0.32f, 0.2f, 0.15f, 0.1f, 0.35f, 0.2f);
			d.BarkColor = C3(0.4f, 0.34f, 0.3f); d.LeafColor = C3(0.12f, 0.24f, 0.14f); d.LeafColor2 = C3(0.2f, 0.32f, 0.18f);
		});
		add("Alpine Rose", MT, L::Shrub, { -6, 6, 0.4f, 0.95f, 40, 0.0f }, 1.0f, [](TreeDesc& d, TreeParams& p) {
			Shrub(d, 0.8f, LS::Oval);
			p.FlowerPetals = 5; p.FlowerCards = 3; p.FlowerSize = 0.05f;
			d.LeafColor = C3(0.12f, 0.26f, 0.1f); d.LeafSmoothness = 0.6f;
			d.FlowerColor = C3(0.88f, 0.22f, 0.45f); d.FlowerCenter = C3(0.75f, 0.15f, 0.35f);
		});
		add("Edelweiss", MT, L::Flower, { -8, 4, 0.2f, 0.7f, 45, 0.0f }, 0.5f, [](TreeDesc& d, TreeParams& p) {
			Flower(d, 0.15f, 3, 9, 0.05f, C3(0.92f, 0.92f, 0.88f), C3(0.88f, 0.85f, 0.6f));
			d.LeafColor = C3(0.58f, 0.6f, 0.55f); d.LeafColor2 = C3(0.65f, 0.66f, 0.6f);
		});
		add("Gentian", MT, L::Flower, { -8, 6, 0.35f, 0.9f, 45, 0.0f }, 0.7f, [](TreeDesc& d, TreeParams& p) {
			Flower(d, 0.12f, 3, 5, 0.05f, C3(0.12f, 0.22f, 0.85f), C3(0.3f, 0.5f, 0.75f));
		});
		add("Alpine Grass", MT, L::Grass, { -8, 6, 0.25f, 0.9f, 45, 0.0f }, 1.5f, [](TreeDesc& d, TreeParams& p) {
			Grass(d, 0.25f, 6, 7, 0.2f);
			d.LeafColor = C3(0.34f, 0.44f, 0.2f); d.LeafColor2 = C3(0.52f, 0.5f, 0.3f);
		});
		add("Mountain Avens", MT, L::Flower, { -10, 4, 0.2f, 0.8f, 45, 0.0f }, 0.8f, [](TreeDesc& d, TreeParams& p) {
			Flower(d, 0.1f, 3, 8, 0.04f, C3(0.97f, 0.96f, 0.92f), C3(0.95f, 0.8f, 0.2f));
			p.Leaf = LS::Lobed; p.LeafCards = 6; p.LeavesPerCard = 2; p.LeafCardSize = 0.08f; p.CardSpread = 75.0f;
		});

		// ================================================================ 툰드라 (-12 ~ 0 °C)
		const char* TU = "Tundra";
		add("Dwarf Birch", TU, L::Shrub, { -14, 2, 0.3f, 0.9f, 35, 0.0f }, 1.2f, [](TreeDesc& d, TreeParams& p) {
			Shrub(d, 0.5f, LS::Round);
			p.Gnarl = 0.5f; p.LeafCardSize = 0.14f;
			d.BarkColor = C3(0.42f, 0.3f, 0.26f); d.LeafColor = C3(0.3f, 0.42f, 0.14f); d.LeafColor2 = C3(0.6f, 0.34f, 0.12f); d.LeafVariation = 0.6f;
		});
		add("Arctic Willow", TU, L::Shrub, { -16, 0, 0.3f, 0.95f, 35, 0.3f }, 1.0f, [](TreeDesc& d, TreeParams& p) {
			Shrub(d, 0.25f, LS::Oval);
			p.CrownShape = CR::Hemispherical; p.LeafCardSize = 0.1f;
			p.L[0] = Lv(9, 0.05f, 78.0f, 15.0f, 1.6f, 0.25f, 0.3f, 0.0f, 0.8f, 0.4f);
			d.LeafColor = C3(0.4f, 0.48f, 0.3f); d.LeafColor2 = C3(0.5f, 0.54f, 0.38f);
		});
		add("Cotton Grass", TU, L::Flower, { -16, 4, 0.5f, 1.0f, 30, 0.5f }, 1.0f, [](TreeDesc& d, TreeParams& p) {
			Flower(d, 0.35f, 4, 16, 0.05f, C3(0.98f, 0.98f, 0.97f), C3(0.95f, 0.95f, 0.93f));
			p.Leaf = LS::Blade; p.LeavesPerCard = 5; p.LeafCards = 4; p.LeafCardSize = 0.25f;
		});
		add("Moss Campion", TU, L::Flower, { -16, 2, 0.2f, 0.8f, 45, 0.0f }, 0.8f, [](TreeDesc& d, TreeParams& p) {
			Rosette(d, 0.12f, 12, LS::Needle, 0.3f);
			p.Shape = FM::Flower; p.LeavesPerCard = 10; p.Stems = 8; p.FlowerPetals = 5; p.FlowerCards = 1; p.FlowerSize = 0.03f; p.Height = 0.06f; p.CardSpread = 80.0f;
			d.LeafColor = C3(0.2f, 0.36f, 0.12f); d.FlowerColor = C3(0.92f, 0.45f, 0.7f); d.FlowerCenter = C3(0.9f, 0.6f, 0.75f);
		});
		add("Arctic Poppy", TU, L::Flower, { -16, 2, 0.2f, 0.8f, 45, 0.0f }, 0.7f, [](TreeDesc& d, TreeParams& p) {
			Flower(d, 0.18f, 3, 4, 0.05f, C3(0.98f, 0.88f, 0.2f), C3(0.75f, 0.7f, 0.2f));
		});
		add("Tundra Sedge", TU, L::Grass, { -16, 2, 0.3f, 1.0f, 40, 0.0f }, 1.5f, [](TreeDesc& d, TreeParams& p) {
			Grass(d, 0.22f, 5, 6, 0.18f);
			d.LeafColor = C3(0.5f, 0.46f, 0.26f); d.LeafColor2 = C3(0.42f, 0.42f, 0.22f); d.LeafVariation = 0.7f;
		});

		// ================================================================ 물가 · 습지 (기후 넓게, 물가 정도로)
		const char* WL = "Wetland";
		add("Weeping Willow", WL, L::Canopy, { 4, 22, 0.4f, 1.0f, 20, 0.95f }, 1.0f, [](TreeDesc& d, TreeParams& p) {
			Deciduous(d, 14.0f, 0.5f, CR::Spherical, LS::Blade);
			p.L[0] = Lv(10, 0.4f, 45.0f, 15.0f, 0.55f, 0.2f, 0.1f, 0.2f, 0.45f, 0.25f);
			p.L[1] = Lv(9, 0.2f, 40.0f, 15.0f, 0.9f, 0.2f, 1.2f, 0.0f, 0.4f, 0.1f);
			p.LeafCards = 12; p.LeavesPerCard = 7; p.LeafCardSize = 0.7f; p.CardSegments = 1;
			d.BarkColor = C3(0.38f, 0.34f, 0.3f); d.RidgeDepth = 1.0f;
			d.LeafColor = C3(0.36f, 0.5f, 0.18f); d.LeafColor2 = C3(0.5f, 0.56f, 0.22f); d.LeafLength = 0.2f; d.BranchSway = 0.45f;
		});
		add("Poplar", WL, L::Canopy, { 2, 18, 0.4f, 1.0f, 25, 0.6f }, 0.8f, [](TreeDesc& d, TreeParams& p) {
			Deciduous(d, 24.0f, 0.4f, CR::TaperedCylindrical, LS::Round);
			p.L[0] = Lv(18, 0.2f, 22.0f, 8.0f, 0.3f, 0.2f, 0.0f, 0.4f, 0.4f, 0.1f);
			d.BarkColor = C3(0.5f, 0.48f, 0.42f); d.LeafColor = C3(0.26f, 0.44f, 0.12f); d.LeafColor2 = C3(0.46f, 0.52f, 0.2f); d.LeafFlutter = 0.07f;
		});
		add("Mangrove", WL, L::SmallTree, { 22, 30, 0.6f, 1.0f, 15, 1.0f }, 1.0f, [](TreeDesc& d, TreeParams& p) {
			Deciduous(d, 8.0f, 0.2f, CR::Hemispherical, LS::Oval);
			p.Flare = 2.6f; p.Gnarl = 0.4f;
			d.BarkColor = C3(0.42f, 0.38f, 0.32f); d.LeafColor = C3(0.14f, 0.3f, 0.1f); d.LeafColor2 = C3(0.22f, 0.38f, 0.12f); d.LeafSmoothness = 0.6f;
		});
		add("Common Reed", WL, L::Grass, { 0, 28, 0.5f, 1.0f, 20, 0.95f }, 1.4f, [](TreeDesc& d, TreeParams& p) {
			Grass(d, 2.4f, 6, 4, 0.3f);
			p.Droop = 0.5f; p.CardSpread = 12.0f;
			p.Shape = FM::Flower; p.Stems = 4; p.FlowerPetals = 14; p.FlowerCards = 1; p.FlowerSize = 0.25f; p.Radius = 0.008f;
			d.LeafColor = C3(0.36f, 0.44f, 0.2f); d.LeafColor2 = C3(0.5f, 0.48f, 0.26f);
			d.FlowerColor = C3(0.5f, 0.4f, 0.36f); d.FlowerCenter = C3(0.45f, 0.36f, 0.32f);
		});
		add("Cattail", WL, L::Grass, { 0, 28, 0.55f, 1.0f, 15, 1.0f }, 1.2f, [](TreeDesc& d, TreeParams& p) {
			Flower(d, 1.6f, 3, 2, 0.12f, C3(0.35f, 0.22f, 0.12f), C3(0.3f, 0.2f, 0.1f));
			p.Leaf = LS::Blade; p.LeafCards = 6; p.LeavesPerCard = 3; p.LeafCardSize = 1.5f; p.CardSpread = 12.0f; d.LeafLength = 0.3f;
			d.LeafColor = C3(0.3f, 0.44f, 0.16f); d.LeafColor2 = C3(0.4f, 0.48f, 0.2f);
		});
		add("Bulrush", WL, L::Grass, { 0, 28, 0.55f, 1.0f, 15, 1.0f }, 1.0f, [](TreeDesc& d, TreeParams& p) {
			Grass(d, 1.4f, 6, 6, 0.12f);
			p.Droop = 0.2f; p.CardSpread = 8.0f;
			d.LeafColor = C3(0.28f, 0.42f, 0.16f); d.LeafColor2 = C3(0.34f, 0.46f, 0.18f);
		});
		add("Papyrus", WL, L::Flower, { 18, 30, 0.5f, 1.0f, 15, 1.0f }, 0.8f, [](TreeDesc& d, TreeParams& p) {
			Flower(d, 2.6f, 6, 16, 0.5f, C3(0.4f, 0.55f, 0.2f), C3(0.35f, 0.5f, 0.18f));
			p.LeafCards = 0; p.Radius = 0.012f; p.CardSpread = 40.0f;
		});
		add("Yellow Iris", WL, L::Flower, { 2, 22, 0.5f, 1.0f, 20, 0.9f }, 0.8f, [](TreeDesc& d, TreeParams& p) {
			Flower(d, 0.9f, 3, 3, 0.11f, C3(0.98f, 0.85f, 0.12f), C3(0.6f, 0.4f, 0.1f));
			p.Leaf = LS::Blade; p.LeafCards = 6; p.LeavesPerCard = 2; p.LeafCardSize = 0.9f; p.CardSpread = 15.0f; d.LeafLength = 0.35f;
		});
		add("Marsh Marigold", WL, L::Flower, { 0, 16, 0.6f, 1.0f, 20, 0.9f }, 0.7f, [](TreeDesc& d, TreeParams& p) {
			Flower(d, 0.3f, 4, 5, 0.06f, C3(0.98f, 0.82f, 0.08f), C3(0.9f, 0.7f, 0.05f));
			p.Leaf = LS::Round; p.LeafCards = 6; p.LeavesPerCard = 1; p.LeafCardSize = 0.16f; p.CardSegments = 1;
		});
		add("Horsetail", WL, L::Grass, { -4, 22, 0.55f, 1.0f, 30, 0.7f }, 0.7f, [](TreeDesc& d, TreeParams& p) {
			Grass(d, 0.5f, 5, 9, 0.1f);
			p.Droop = 0.15f; p.CardSpread = 10.0f; p.Leaf = LS::Needle; p.LeavesPerCard = 14; p.CardSegments = 2;
			d.LeafColor = C3(0.3f, 0.46f, 0.18f); d.LeafColor2 = C3(0.38f, 0.5f, 0.2f);
		});

		return std::move(b.List);
	}

	const std::vector<Species>& Library()
	{
		static const std::vector<Species> list = Build();
		return list;
	}

	float Window(float x, float lo, float hi, float margin)
	{
		if (x < lo)
			return std::clamp(1.0f - (lo - x) / margin, 0.0f, 1.0f);
		if (x > hi)
			return std::clamp(1.0f - (x - hi) / margin, 0.0f, 1.0f);
		return 1.0f;
	}
}

namespace PlantLibrary
{
	const char* LayerName(Layer layer)
	{
		static const char* const names[] = { "Canopy", "Small Tree", "Shrub", "Grass", "Flower", "Fern", "Succulent" };
		const int i = (int)layer;
		return i >= 0 && i < (int)Layer::Count ? names[i] : "";
	}

	const std::vector<Species>& All() { return Library(); }

	const Species* At(int index)
	{
		const auto& all = Library();
		return index >= 0 && index < (int)all.size() ? &all[(size_t)index] : nullptr;
	}

	int IndexOf(const std::string& name)
	{
		const auto& all = Library();
		for (size_t i = 0; i < all.size(); ++i)
			if (_stricmp(all[i].Name.c_str(), name.c_str()) == 0)
				return (int)i;
		return -1;
	}

	const char* const* Names(int& count)
	{
		static std::vector<const char*> names;
		static std::once_flag once;
		std::call_once(once, []() {
			for (const Species& s : Library())
				names.push_back(s.Name.c_str());
		});
		count = (int)names.size();
		return names.data();
	}

	float Suitability(const Species& s, float temperatureC, float moisture, float slopeDeg, float water)
	{
		const Climate& c = s.Range;
		float k = Window(temperatureC, c.TempMin, c.TempMax, 3.0f) * Window(moisture, c.MoistMin, c.MoistMax, 0.08f);
		if (slopeDeg > c.SlopeMax)
			k *= std::clamp(1.0f - (slopeDeg - c.SlopeMax) / 8.0f, 0.0f, 1.0f);
		// 물가 종: 물에서 멀면 줄고 (Water 1 이면 물가에만), 물가에서는 늘어난다
		if (c.Water > 0.0f)
			k *= (1.0f - c.Water) + c.Water * std::clamp(water * 1.5f, 0.0f, 1.0f) + water * c.Water * 1.5f;
		return k;
	}
}
