#include "pch.h"
#include "TerrainRenderer.h"
#include "TerrainData.h"
#include "Effects.h"
#include "RenderStats.h"

namespace
{
	// ---- 인덱스 버퍼: 노드 격자(칸 수) x 가장자리 마스크 16가지 ----
	// 마스크 비트: 1 = -X 가장자리, 2 = +X, 4 = -Z, 8 = +Z (그쪽 이웃 노드가 두 배 크다 = 한 단계 거칠다)
	struct IndexSet
	{
		ComPtr<ID3D11Buffer> Buffers[16];
		UINT Counts[16] = {};
	};

	std::vector<uint16_t> BuildIndices(int cells, int mask)
	{
		const int side = cells + 1;
		auto snap = [&](int x, int z) {
			// 큰 이웃과 닿는 가장자리의 홀수 격자점은 아래쪽 짝수 격자점으로 붙인다 → 이웃의 변과 정확히 같아져 틈이 없다
			if ((mask & 1) && x == 0) z = z / 2 * 2;
			if ((mask & 2) && x == cells) z = z / 2 * 2;
			if ((mask & 4) && z == 0) x = x / 2 * 2;
			if ((mask & 8) && z == cells) x = x / 2 * 2;
			return (uint16_t)(x + z * side);
		};
		std::vector<uint16_t> out;
		out.reserve((size_t)cells * cells * 6);
		auto tri = [&](uint16_t a, uint16_t b, uint16_t c) {
			if (a == b || b == c || a == c)
				return;   // 붙이기로 생긴 넓이 0 삼각형
			out.push_back(a); out.push_back(b); out.push_back(c);
		};
		for (int z = 0; z < cells; ++z)
			for (int x = 0; x < cells; ++x)
			{
				const uint16_t v00 = snap(x, z), v10 = snap(x + 1, z), v01 = snap(x, z + 1), v11 = snap(x + 1, z + 1);
				// 대각선 (0,0)-(1,1), 위에서 볼 때 시계 방향 (TerrainData::GetHeight 와 같은 면)
				tri(v00, v01, v11);
				tri(v00, v11, v10);
			}
		return out;
	}

	IndexSet& GetIndexSet(int cells)
	{
		static std::map<int, IndexSet> sets;
		auto it = sets.find(cells);
		if (it != sets.end())
			return it->second;
		IndexSet& set = sets[cells];
		ID3D11Device* device = Application::GetI()->GetDevice();
		for (int m = 0; m < 16; ++m)
		{
			const std::vector<uint16_t> idx = BuildIndices(cells, m);
			D3D11_BUFFER_DESC desc = {};
			desc.ByteWidth = (UINT)(idx.size() * sizeof(uint16_t));
			desc.Usage = D3D11_USAGE_IMMUTABLE;
			desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
			D3D11_SUBRESOURCE_DATA init = { idx.data(), 0, 0 };
			device->CreateBuffer(&desc, &init, set.Buffers[m].GetAddressOf());
			set.Counts[m] = (UINT)idx.size();
		}
		return set;
	}

	// ---- 효과 변수 (이름으로 찾아 둔다) ----
	struct TerrainVars
	{
		ID3DX11EffectTechnique* Tech = nullptr;
		ID3DX11EffectVectorVariable* Patch = nullptr;
		ID3DX11EffectVectorVariable* Size = nullptr;
		ID3DX11EffectVectorVariable* Origin = nullptr;
		ID3DX11EffectVectorVariable* LayerST = nullptr;
		ID3DX11EffectVectorVariable* LayerTint = nullptr;
		ID3DX11EffectScalarVariable* LayerCount = nullptr;
		ID3DX11EffectShaderResourceVariable* HeightMap = nullptr;
		ID3DX11EffectShaderResourceVariable* Control = nullptr;
		ID3DX11EffectShaderResourceVariable* ColorMap = nullptr;
		ID3DX11EffectScalarVariable* UseColorMap = nullptr;
		ID3DX11EffectShaderResourceVariable* Layers[4] = {};

		void Bind(ID3DX11Effect* fx, const char* tech)
		{
			Tech = fx->GetTechniqueByName(tech);
			Patch = fx->GetVariableByName("gTerrainPatch")->AsVector();
			Size = fx->GetVariableByName("gTerrainSize")->AsVector();
			Origin = fx->GetVariableByName("gTerrainOrigin")->AsVector();
			LayerST = fx->GetVariableByName("gTerrainLayerST")->AsVector();
			LayerTint = fx->GetVariableByName("gTerrainLayerTint")->AsVector();
			LayerCount = fx->GetVariableByName("gTerrainLayerCount")->AsScalar();
			HeightMap = fx->GetVariableByName("gTerrainHeightMap")->AsShaderResource();
			Control = fx->GetVariableByName("gTerrainControl")->AsShaderResource();
			ColorMap = fx->GetVariableByName("gTerrainColorMap")->AsShaderResource();
			UseColorMap = fx->GetVariableByName("gTerrainUseColorMap")->AsScalar();
			const char* names[4] = { "gTerrainLayer0", "gTerrainLayer1", "gTerrainLayer2", "gTerrainLayer3" };
			for (int i = 0; i < 4; ++i)
				Layers[i] = fx->GetVariableByName(names[i])->AsShaderResource();
		}
		bool Valid() const { return Tech && Tech->IsValid(); }
	};

	TerrainVars& Vars(TerrainRenderer::Pass pass)
	{
		static TerrainVars main, shadow, normalDepth;
		static bool bound = false;
		if (!bound)
		{
			bound = true;
			main.Bind(Effects::InstancedBasicFX->GetFX(), "TerrainTech");
			shadow.Bind(Effects::BuildShadowMapFX->GetFX(), "TerrainShadowTech");
			normalDepth.Bind(Effects::SsaoNormalDepthFX->GetFX(), "TerrainNormalDepthTech");
		}
		switch (pass)
		{
		case TerrainRenderer::Pass::Shadow: return shadow;
		case TerrainRenderer::Pass::NormalDepth: return normalDepth;
		default: return main;
		}
	}

	void SetMatrix(ID3DX11Effect* fx, const char* name, CXMMATRIX m)
	{
		if (auto* v = fx->GetVariableByName(name)->AsMatrix(); v && v->IsValid())
			v->SetMatrix(reinterpret_cast<const float*>(&m));
	}

	// 상자가 절두체 밖이면 true (클립 공간에서 8 꼭짓점이 모두 한 평면 바깥)
	bool OutsideFrustum(const Vec3& mn, const Vec3& mx, CXMMATRIX viewProj)
	{
		int outside[6] = {};
		for (int i = 0; i < 8; ++i)
		{
			const XMVECTOR c = XMVector4Transform(XMVectorSet(i & 1 ? mx.x : mn.x, i & 2 ? mx.y : mn.y, i & 4 ? mx.z : mn.z, 1.0f), viewProj);
			const float x = XMVectorGetX(c), y = XMVectorGetY(c), z = XMVectorGetZ(c), w = XMVectorGetW(c);
			outside[0] += x < -w; outside[1] += x > w;
			outside[2] += y < -w; outside[3] += y > w;
			outside[4] += z < 0.0f; outside[5] += z > w;
		}
		for (int k = 0; k < 6; ++k)
			if (outside[k] == 8)
				return true;
		return false;
	}

	struct Leaf { int Depth, X, Z; };

	// ---- 쿼드트리 선택 ----
	//  1) 루트(지형 전체)부터: 노드 오차를 화면에 투영한 픽셀이 Pixel Error 를 넘으면 4등분 (가까운 곳은 계속 쪼개지고 먼 곳은 큰 노드로 남는다)
	//  2) 이웃 노드 크기 차이를 최대 2배로 맞춘다 (더 큰 이웃을 쪼갠다)
	//  3) 두 배 큰 이웃과 닿는 변은 마스크로 표시해 격자점을 붙인다
	//  절두체와 상관없이 지형 전체를 덮도록 고른 뒤 그릴 때만 컬링한다 → 화면 밖 때문에 균형/봉합이 달라지지 않는다.
	void SelectLeaves(const TerrainData& data, const Vec3& origin, const Vec3& cameraPos, float errorPerMeter, std::vector<Leaf>& leaves, std::vector<int>& masks)
	{
		const int maxDepth = data.MaxDepth();
		leaves.clear();
		std::vector<Leaf> stack;
		stack.push_back({ 0, 0, 0 });
		while (!stack.empty())
		{
			const Leaf n = stack.back();
			stack.pop_back();
			const TerrainData::Node& node = data.GetNode(n.Depth, n.X, n.Z);
			bool split = false;
			if (n.Depth < maxDepth)
			{
				const int cells = data.NodeCells(n.Depth);
				const Vec3 mn = origin + Vec3(n.X * cells * data.CellSizeX(), node.MinHeight, n.Z * cells * data.CellSizeZ());
				const Vec3 mx = origin + Vec3((n.X + 1) * cells * data.CellSizeX(), node.MaxHeight, (n.Z + 1) * cells * data.CellSizeZ());
				const Vec3 closest(std::clamp(cameraPos.x, mn.x, mx.x), std::clamp(cameraPos.y, mn.y, mx.y), std::clamp(cameraPos.z, mn.z, mx.z));
				const float dist = (std::max)(0.01f, (closest - cameraPos).Length());
				split = node.Error > errorPerMeter * dist;
			}
			if (split)
				for (int k = 0; k < 4; ++k)
					stack.push_back({ n.Depth + 1, n.X * 2 + (k & 1), n.Z * 2 + (k >> 1) });
			else
				leaves.push_back(n);
		}

		// 가장 깊은 단계 격자에 "이 칸을 덮는 잎 노드" 를 기록해 이웃을 찾는다
		const int N = 1 << maxDepth;
		static std::vector<int> owner;
		owner.assign((size_t)N * N, -1);
		auto fill = [&]() {
			for (int i = 0; i < (int)leaves.size(); ++i)
			{
				const Leaf& l = leaves[i];
				const int size = 1 << (maxDepth - l.Depth);
				for (int z = l.Z * size; z < (l.Z + 1) * size; ++z)
					for (int x = l.X * size; x < (l.X + 1) * size; ++x)
						owner[(size_t)z * N + x] = i;
			}
		};
		// 변 바깥쪽 첫 칸의 잎 (더 큰 이웃이면 그 하나가 변 전체를 덮는다)
		auto neighbor = [&](const Leaf& l, int side) -> int {
			const int size = 1 << (maxDepth - l.Depth);
			const int fx = l.X * size, fz = l.Z * size;
			switch (side)
			{
			case 0: return fx > 0 ? owner[(size_t)fz * N + fx - 1] : -1;
			case 1: return fx + size < N ? owner[(size_t)fz * N + fx + size] : -1;
			case 2: return fz > 0 ? owner[(size_t)(fz - 1) * N + fx] : -1;
			default: return fz + size < N ? owner[(size_t)(fz + size) * N + fx] : -1;
			}
		};

		for (int iter = 0; iter <= maxDepth; ++iter)
		{
			fill();
			std::vector<char> splitMark(leaves.size(), 0);
			bool any = false;
			for (const Leaf& l : leaves)
				for (int side = 0; side < 4; ++side)
				{
					const int j = neighbor(l, side);
					if (j >= 0 && leaves[j].Depth < l.Depth - 1)
					{
						splitMark[j] = 1;
						any = true;
					}
				}
			if (!any)
				break;
			std::vector<Leaf> next;
			next.reserve(leaves.size() + 16);
			for (int i = 0; i < (int)leaves.size(); ++i)
			{
				if (!splitMark[i])
				{
					next.push_back(leaves[i]);
					continue;
				}
				const Leaf& l = leaves[i];
				for (int k = 0; k < 4; ++k)
					next.push_back({ l.Depth + 1, l.X * 2 + (k & 1), l.Z * 2 + (k >> 1) });
			}
			leaves.swap(next);
		}
		fill();

		masks.assign(leaves.size(), 0);
		for (int i = 0; i < (int)leaves.size(); ++i)
			for (int side = 0; side < 4; ++side)
			{
				const int j = neighbor(leaves[i], side);
				if (j >= 0 && leaves[j].Depth < leaves[i].Depth)
					masks[i] |= 1 << side;
			}
	}
}

namespace TerrainRenderer
{
	void Draw(TerrainData& data, const Vec3& origin, Pass pass, float pixelError, Stats* stats)
	{
		TerrainVars& v = Vars(pass);
		if (!v.Valid() || data.Heights.empty())
			return;
		ID3D11ShaderResourceView* heightSRV = data.HeightSRV();
		if (heightSRV == nullptr)
			return;

		RenderManager* rm = RenderManager::GetI();
		const bool editor = rm->RenderingEditorView;
		const XMMATRIX view = editor ? rm->EditorCameraViewMatrix : rm->CameraViewMatrix;
		const XMMATRIX proj = editor ? rm->EditorCameraProjectionMatrix : rm->CameraProjectionMatrix;
		const XMMATRIX viewProj = view * proj;
		const float viewportH = editor ? rm->EditorViewport.Height : rm->Viewport.Height;
		const XMMATRIX invView = XMMatrixInverse(nullptr, view);
		const Vec3 cameraPos(XMVectorGetX(invView.r[3]), XMVectorGetY(invView.r[3]), XMVectorGetZ(invView.r[3]));

		// ---- 쿼드트리 잎 노드 선택 (같은 화면의 모든 패스가 같은 카메라로 고르므로 모양이 일치한다) ----
		XMFLOAT4X4 p;
		XMStoreFloat4x4(&p, proj);
		// 1m 오차가 거리 d 에서 차지하는 화면 픽셀 = K / d
		const float K = (std::max)(1.0f, viewportH) * 0.5f * fabsf(p._22);
		const float errorPerMeter = (std::max)(0.1f, pixelError) / K;
		static std::vector<Leaf> leaves;
		static std::vector<int> masks;
		SelectLeaves(data, origin, cameraPos, errorPerMeter, leaves, masks);

		// ---- 효과 변수 ----
		ID3DX11Effect* fx = nullptr;
		switch (pass)
		{
		case Pass::Main: fx = Effects::InstancedBasicFX->GetFX(); break;
		case Pass::Shadow: fx = Effects::BuildShadowMapFX->GetFX(); break;
		case Pass::NormalDepth: fx = Effects::SsaoNormalDepthFX->GetFX(); break;
		}
		const XMMATRIX cullViewProj = pass == Pass::Shadow ? rm->LightViewProjection : viewProj;
		if (pass == Pass::Main)
		{
			static const XMMATRIX toTexSpace(0.5f, 0.0f, 0.0f, 0.0f, 0.0f, -0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.5f, 0.5f, 0.0f, 1.0f);
			Effects::InstancedBasicFX->SetViewProj(viewProj);
			SetMatrix(fx, "gViewProjTex", viewProj * toTexSpace);
			Material mat;
			mat.Ambient = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
			mat.Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
			mat.Specular = XMFLOAT4(0.08f, 0.08f, 0.08f, 16.0f);
			Effects::InstancedBasicFX->SetMaterial(mat);
			ShaderSetting setting = UMaterial::GetDefault()->GetShaderSetting();
			setting.UseShadowMap = 1;
			Effects::InstancedBasicFX->SetShaderSetting(setting);
		}
		else if (pass == Pass::Shadow)
			SetMatrix(fx, "gViewProj", rm->LightViewProjection);
		else
		{
			SetMatrix(fx, "gView", view);
			SetMatrix(fx, "gProj", proj);
			SetMatrix(fx, "gWorldViewProj", viewProj);   // 지형은 월드 좌표를 바로 쓰므로 World = 단위 행렬
		}

		const XMFLOAT4 sizeVec(data.Size.x, data.Size.y, data.Size.z, (float)data.HeightmapResolution);
		const XMFLOAT4 originVec(origin.x, origin.y, origin.z, 0.0f);
		v.Size->SetFloatVector(reinterpret_cast<const float*>(&sizeVec));
		v.Origin->SetFloatVector(reinterpret_cast<const float*>(&originVec));
		v.HeightMap->SetResource(heightSRV);
		if (pass == Pass::Main)
		{
			XMFLOAT4 st[4] = {}, tint[4] = {};
			const int layerCount = (std::min)((int)data.Layers.size(), TerrainData::kMaxLayers);
			for (int i = 0; i < 4; ++i)
			{
				TerrainLayer* layer = i < layerCount ? data.Layers[i].get() : nullptr;
				const Vec2 tile = layer ? Vec2((std::max)(0.01f, layer->TileSize.x), (std::max)(0.01f, layer->TileSize.y)) : Vec2(15.0f, 15.0f);
				st[i] = XMFLOAT4(1.0f / tile.x, 1.0f / tile.y, layer ? layer->TileOffset.x / tile.x : 0.0f, layer ? layer->TileOffset.y / tile.y : 0.0f);
				tint[i] = layer ? layer->Tint : XMFLOAT4(1, 1, 1, 1);
				v.Layers[i]->SetResource(layer ? layer->DiffuseSRV() : nullptr);
			}
			v.LayerST->SetFloatVectorArray(reinterpret_cast<const float*>(st), 0, 4);
			v.LayerTint->SetFloatVectorArray(reinterpret_cast<const float*>(tint), 0, 4);
			v.LayerCount->SetInt(layerCount);
			v.Control->SetResource(data.ControlSRV());
			// 컬러 맵 (생성기의 색·그라디언트 재질)
			ID3D11ShaderResourceView* colorSRV = data.ColorMapSRV();
			if (v.ColorMap && v.ColorMap->IsValid())
				v.ColorMap->SetResource(colorSRV);
			if (v.UseColorMap && v.UseColorMap->IsValid())
				v.UseColorMap->SetInt(colorSRV ? 1 : 0);
		}

		// ---- 그리기 ----
		ID3D11DeviceContext* dc = Application::GetI()->GetDeviceContext();
		ComPtr<ID3D11DepthStencilState> prevDSS;
		UINT prevRef = 0;
		dc->OMGetDepthStencilState(prevDSS.GetAddressOf(), &prevRef);
		dc->IASetInputLayout(nullptr);
		ID3D11Buffer* nullVB = nullptr;
		UINT zero = 0;
		dc->IASetVertexBuffers(0, 1, &nullVB, &zero, &zero);
		dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		const int grid = data.NodeGrid();
		IndexSet& indices = GetIndexSet(grid);
		ID3DX11EffectPass* fxPass = v.Tech->GetPassByIndex(0);
		if (stats)
		{
			*stats = Stats();
			stats->Nodes = (int)leaves.size();
		}
		for (size_t i = 0; i < leaves.size(); ++i)
		{
			const Leaf& l = leaves[i];
			const TerrainData::Node& node = data.GetNode(l.Depth, l.X, l.Z);
			const int cells = data.NodeCells(l.Depth);
			const Vec3 mn = origin + Vec3(l.X * cells * data.CellSizeX(), node.MinHeight - 0.01f, l.Z * cells * data.CellSizeZ());
			const Vec3 mx = origin + Vec3((l.X + 1) * cells * data.CellSizeX(), node.MaxHeight + 0.01f, (l.Z + 1) * cells * data.CellSizeZ());
			if (OutsideFrustum(mn, mx, cullViewProj))
				continue;
			ID3D11Buffer* ib = indices.Buffers[masks[i]].Get();
			const UINT count = indices.Counts[masks[i]];
			if (ib == nullptr || count == 0)
				continue;
			const XMFLOAT4 patchVec((float)(l.X * cells), (float)(l.Z * cells), (float)data.NodeStep(l.Depth), (float)(grid + 1));
			v.Patch->SetFloatVector(reinterpret_cast<const float*>(&patchVec));
			fxPass->Apply(0, dc);
			dc->IASetIndexBuffer(ib, DXGI_FORMAT_R16_UINT, 0);
			dc->DrawIndexed(count, 0, 0);
			RenderStats::AddDraw(count, count);   // 정점 수는 인덱스 수로 근사
			if (stats)
			{
				++stats->DrawnNodes;
				stats->DrawnLeaves.push_back(XMINT3(l.Depth, l.X, l.Z));
				stats->Triangles += count / 3;
				stats->DepthHistogram[(std::min)(l.Depth, 15)]++;
			}
		}

		// 높이맵 등을 풀어 두고 (다음 그리기에 남지 않게), 본 패스의 다른 물체는 EQUAL 깊이 검사에 기대므로 깊이 상태를 되돌린다
		v.HeightMap->SetResource(nullptr);
		if (pass == Pass::Main)
		{
			v.Control->SetResource(nullptr);
			if (v.ColorMap && v.ColorMap->IsValid())
				v.ColorMap->SetResource(nullptr);
			for (auto* layer : v.Layers)
				layer->SetResource(nullptr);
		}
		fxPass->Apply(0, dc);
		dc->OMSetDepthStencilState(prevDSS.Get(), prevRef);
	}
}
