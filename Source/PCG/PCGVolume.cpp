#include "pch.h"
#include "PCGVolume.h"
#include "PCGExecutor.h"
#include "PCGMeshAsset.h"
#include "WorldTerrain.h"
#include "MeshBatcher.h"
#include "Mesh.h"
#include "UMaterial.h"
#include "Transform.h"
#include "GameObject.h"
#include "RenderManager.h"
#include "JobSystem.h"
#include "UnityGUI.h"
#include "Profiler.h"
#include "SceneViewOverlay.h"
#include "SelectionManager.h"
#include "EditorLog.h"
#include "OcclusionCulling.h"
#include "Application.h"
#include <chrono>
#include <map>
#include <mutex>

// 작업 스레드 → 메인: 만든 셀
struct PCGQueue
{
	std::mutex Lock;
	struct Item { PCGVolume::CellKey Key; uint64_t Hash; PCG::CellOutput Out; };
	std::vector<Item> Done;
	int Running = 0;
};

struct PCGVolume::Cell
{
	uint64_t Hash = 0;          // 만든 규칙 · 월드 · Seed
	bool Ready = false, Pending = false;
	uint32_t LastNeeded = 0;
	float X0 = 0, Z0 = 0, X1 = 0, Z1 = 0;
	float MinY = 0, MaxY = 0;
	int Points = 0;
	std::vector<std::vector<XMFLOAT4X4>> PerMesh;
	// GPU 인스턴스 버퍼: [메시][LOD][파트] (그 LOD 를 처음 그릴 때 만든다 — 모델 변환을 곱해서)
	std::vector<std::vector<std::vector<ComPtr<GfxBuffer>>>> Buffers;
};

namespace
{
	std::vector<PCGVolume*>& Registry()
	{
		static std::vector<PCGVolume*> all;
		return all;
	}

	Vec3 EyeOf(CXMMATRIX view)
	{
		const XMMATRIX inv = XMMatrixInverse(nullptr, view);
		return Vec3(XMVectorGetX(inv.r[3]), XMVectorGetY(inv.r[3]), XMVectorGetZ(inv.r[3]));
	}

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

	float RectDistance(float x0, float z0, float x1, float z1, const Vec3& p)
	{
		const float dx = (std::max)({ x0 - p.x, 0.0f, p.x - x1 });
		const float dz = (std::max)({ z0 - p.z, 0.0f, p.z - z1 });
		return sqrtf(dx * dx + dz * dz);
	}

	// 그리기 프레임마다 모델 읽기 예산 (GPU 자원 — 메인)
	uint32_t s_LoadFrame = ~0u;
	int s_LoadsThisFrame = 0;

	bool s_SourceRegistered = false;

	// 셀 버퍼를 (메시 · LOD · 파트) 마다 하나로 모은다 (GPU 복사 — CPU 는 인스턴스를 만지지 않는다) → 그리기 한 번
	struct Segment { GfxBuffer* Buffer; uint32_t Count; };
	struct Group
	{
		const PCG::MeshPart* Part = nullptr;
		std::vector<Segment> Segments;
		uint32_t Total = 0;
		uint64_t Signature = 1469598103934665603ull;
	};
	struct Merged
	{
		ComPtr<GfxBuffer> Buffer;
		uint32_t Capacity = 0;
		uint32_t FilledFrame = ~0u;
		uint64_t FilledSignature = 0;
		uint32_t LastUsed = 0;
	};
	struct MergedKey
	{
		const PCG::MeshPart* Part; bool Editor; bool Shadow;
		bool operator==(const MergedKey& o) const { return Part == o.Part && Editor == o.Editor && Shadow == o.Shadow; }
	};
	struct MergedKeyHash { size_t operator()(const MergedKey& k) const { return std::hash<const void*>()(k.Part) ^ (k.Editor ? 0x9e37u : 0) ^ (k.Shadow ? 0x7f4au : 0); } };
	std::unordered_map<MergedKey, Merged, MergedKeyHash> s_Merged;
}

std::function<void(const std::string&)> PCGVolume::s_OpenGraphWindow;

PCGVolume::PCGVolume()
{
	m_InspectorTitleName = "PCG Volume";
	m_Queue = std::make_shared<PCGQueue>();
	Registry().push_back(this);
	if (!s_SourceRegistered)
	{
		s_SourceRegistered = true;
		MeshBatcher::SetExternalSource("PCG", [](MeshBatcher::Pass pass, bool editor, std::vector<MeshBatcher::ExternalBatch>& out) {
			PCGVolume::EmitBatches((int)pass, editor, &out);
		});
	}
}

PCGVolume::~PCGVolume()
{
	auto& r = Registry();
	r.erase(std::remove(r.begin(), r.end(), this), r.end());
}

const std::vector<PCGVolume*>& PCGVolume::All() { return Registry(); }

void PCGVolume::OnDestroy() { Clear(); }

void PCGVolume::Clear()
{
	m_Cells.clear();
}

void PCGVolume::Regenerate()
{
	m_Cells.clear();
	m_GraphRevision = 0;
}

std::shared_ptr<PCG::Graph> PCGVolume::GetGraph() const
{
	return PCG::LoadGraph(GraphPath);
}

bool PCGVolume::InBounds(float x0, float z0, float x1, float z1) const
{
	return x1 > m_BoundsX0 && x0 < m_BoundsX1 && z1 > m_BoundsZ0 && z0 < m_BoundsZ1;
}

void PCGVolume::ApplyResults()
{
	std::vector<PCGQueue::Item> done;
	{
		std::lock_guard<std::mutex> lock(m_Queue->Lock);
		done.swap(m_Queue->Done);
	}
	for (PCGQueue::Item& it : done)
	{
		auto c = m_Cells.find(it.Key);
		if (c == m_Cells.end())
			continue;   // 그사이 버렸다
		Cell& cell = *c->second;
		cell.Pending = false;
		if (it.Hash != m_Key)
			continue;   // 그사이 규칙이 바뀌었다 — 다음 Tick 이 다시
		cell.Hash = it.Hash;
		cell.Ready = true;
		cell.PerMesh = std::move(it.Out.PerMesh);
		cell.MinY = it.Out.MinY;
		cell.MaxY = it.Out.MaxY;
		cell.Points = it.Out.Points;
		cell.Buffers.clear();
		m_GenMs += it.Out.Ms;
		++m_Generated;
	}
}

void PCGVolume::Tick()
{
	const uint32_t frame = (uint32_t)ImGui::GetFrameCount();
	if (frame == m_LastTickFrame)
		return;
	m_LastTickFrame = frame;
	m_Frame = frame;
	if (!IsEnabled() || m_pGameObject == nullptr || !m_pGameObject->IsActiveInHierarchy())
		return;
	PROFILE_SCOPE("PCG.Update");
	if (m_GraphFor != GraphPath)
	{
		m_Graph = PCG::LoadGraph(GraphPath);
		m_GraphFor = GraphPath;
		m_GraphRevision = 0;
	}
	if (!m_Graph)
	{
		Clear();
		return;
	}
	ApplyResults();

	// 규칙이 바뀌었으면 스냅숏 (작업 스레드가 읽는 복사본) · 스포너 목록
	if (m_Graph->Revision != m_GraphRevision)
	{
		m_GraphRevision = m_Graph->Revision;
		m_Snapshot = std::make_shared<const PCG::Graph>(*m_Graph);
		m_Spawners.clear();
		for (int id : m_Snapshot->Spawners())
		{
			const PCG::Node* n = m_Snapshot->Find(id);
			SpawnerInfo s;
			s.Id = id;
			s.Name = n->Comment.empty() ? "Spawner " + std::to_string(id) : n->Comment;
			s.Cull = (std::max)(10.0f, n->Get("cullDistance"));
			const float cs = n->Get("cellSize");
			// 자동: Cull Distance / 8 (32 ~ 128 m) — 셀 거리로 LOD 를 고르므로 작게 (그리기는 셀을 모아 한 번이라 셀 수와 상관없다)
			s.CellSize = cs > 0.0f ? cs : std::clamp(s.Cull / 8.0f, 32.0f, 128.0f);
			s.Shadows = n->Get("castShadows") > 0.5f;
			s.ShadowDistance = n->Get("shadowDistance");
			s.LodBias = (std::max)(0.1f, n->Get("lodBias"));
			for (const PCG::MeshEntry& m : n->Meshes)
				s.Meshes.push_back(m.Path);
			m_Spawners.push_back(std::move(s));
		}
	}

	// 월드 (World Terrain 이 없으면 기본 값)
	WorldTerrain* wt = WorldTerrain::Active();
	static const WorldGen::Settings kDefault;
	const WorldGen::Settings& world = wt ? wt->Settings : kDefault;
	PCG::TerrainGrid grid;
	const float half = world.WorldSize * 0.5f;
	const float tile = wt ? wt->TileSize : 1024.0f;
	grid.OriginX = grid.OriginZ = -(std::max)(1.0f, std::round(world.WorldSize / tile)) * tile * 0.5f;
	grid.Spacing = tile / 512.0f;
	if (WholeWorld)
	{
		m_BoundsX0 = m_BoundsZ0 = -half;
		m_BoundsX1 = m_BoundsZ1 = half;
	}
	else
	{
		const Vec3 p = m_pGameObject->GetTransform()->GetPosition();
		const Vec3 s = m_pGameObject->GetTransform()->GetScale();
		m_BoundsX0 = p.x - fabsf(s.x) * 0.5f; m_BoundsX1 = p.x + fabsf(s.x) * 0.5f;
		m_BoundsZ0 = p.z - fabsf(s.z) * 0.5f; m_BoundsZ1 = p.z + fabsf(s.z) * 0.5f;
	}
	uint64_t key = m_Snapshot->ContentHash() ^ (world.Hash() * 0x9E3779B97F4A7C15ull) ^ ((uint64_t)(uint32_t)Seed << 17);
	for (float f : { m_BoundsX0, m_BoundsZ0, m_BoundsX1, m_BoundsZ1, grid.Spacing })
	{
		uint32_t u;
		memcpy(&u, &f, 4);
		key = (key ^ u) * 1099511628211ull;
	}
	m_Key = key;

	// 필요한 셀: 카메라 둘 (Scene 뷰 · Game 뷰) 에서 Cull Distance 안
	RenderManager* rm = RenderManager::GetI();
	const Vec3 eyes[2] = { EyeOf(rm->EditorCameraViewMatrix), EyeOf(rm->CameraViewMatrix) };
	struct Want { CellKey Key; float Dist; };
	static std::vector<Want> wants;
	wants.clear();
	for (const SpawnerInfo& s : m_Spawners)
	{
		const float range = s.Cull * DistanceScale;
		for (const Vec3& e : eyes)
		{
			const int cx0 = (int)floorf((e.x - range) / s.CellSize), cx1 = (int)floorf((e.x + range) / s.CellSize);
			const int cz0 = (int)floorf((e.z - range) / s.CellSize), cz1 = (int)floorf((e.z + range) / s.CellSize);
			for (int cz = cz0; cz <= cz1; ++cz)
				for (int cx = cx0; cx <= cx1; ++cx)
				{
					const float x0 = cx * s.CellSize, z0 = cz * s.CellSize, x1 = x0 + s.CellSize, z1 = z0 + s.CellSize;
					const float d = RectDistance(x0, z0, x1, z1, e);
					if (d > range || !InBounds(x0, z0, x1, z1))
						continue;
					const CellKey k{ s.Id, cx, cz };
					auto it = m_Cells.find(k);
					if (it == m_Cells.end())
					{
						auto cell = std::make_unique<Cell>();
						cell->X0 = (std::max)(x0, m_BoundsX0); cell->Z0 = (std::max)(z0, m_BoundsZ0);
						cell->X1 = (std::min)(x1, m_BoundsX1); cell->Z1 = (std::min)(z1, m_BoundsZ1);
						it = m_Cells.emplace(k, std::move(cell)).first;
					}
					Cell& c = *it->second;
					c.LastNeeded = frame;
					if (!c.Pending && c.Hash != key)
						wants.push_back({ k, d });
				}
		}
	}
	// 오래 안 쓴 셀은 버린다 (GPU 버퍼도)
	for (auto it = m_Cells.begin(); it != m_Cells.end();)
	{
		if (!it->second->Pending && frame - it->second->LastNeeded > 90u)
			it = m_Cells.erase(it);
		else
			++it;
	}
	// 가까운 것부터, 동시에 16 개까지
	std::sort(wants.begin(), wants.end(), [](const Want& a, const Want& b) { return a.Dist < b.Dist; });
	int running;
	{
		std::lock_guard<std::mutex> lock(m_Queue->Lock);
		running = m_Queue->Running;
	}
	const std::shared_ptr<const PCG::Graph> graph = m_Snapshot;
	const int seed = Seed;
	for (const Want& w : wants)
	{
		if (running >= 16)
			break;
		Cell& c = *m_Cells[w.Key];
		if (c.Pending)
			continue;
		c.Pending = true;
		++running;
		const PCG::Rect rect{ c.X0, c.Z0, c.X1, c.Z1 };
		std::shared_ptr<PCGQueue> q = m_Queue;
		{
			std::lock_guard<std::mutex> lock(q->Lock);
			++q->Running;
		}
		const CellKey ck = w.Key;
		const WorldGen::Settings ws = world;
		Jobs::Run([q, graph, ck, ws, grid, seed, rect, key]() {
			PCGQueue::Item item;
			item.Key = ck;
			item.Hash = key;
			PCG::ExecuteSpawner(*graph, ck.Spawner, ws, grid, seed, rect, item.Out);
			std::lock_guard<std::mutex> lock(q->Lock);
			q->Done.push_back(std::move(item));
			--q->Running;
		}, nullptr, Jobs::Priority::Background, "PCG Cell");
	}
}

void PCGVolume::EmitBatches(int passIndex, bool editor, void* outPtr)
{
	auto& out = *static_cast<std::vector<MeshBatcher::ExternalBatch>*>(outPtr);
	const MeshBatcher::Pass pass = (MeshBatcher::Pass)passIndex;
	if (pass == MeshBatcher::Pass::Transparent)
		return;
	RenderManager* rm = RenderManager::GetI();
	const XMMATRIX view = editor ? rm->EditorCameraViewMatrix : rm->CameraViewMatrix;
	const XMMATRIX proj = editor ? rm->EditorCameraProjectionMatrix : rm->CameraProjectionMatrix;
	const XMMATRIX viewProj = view * proj;
	const bool shadow = pass == MeshBatcher::Pass::Shadow;
	const XMMATRIX cullVP = shadow ? rm->LightViewProjection : viewProj;
	const Vec3 eye = EyeOf(view);
	XMFLOAT4X4 p;
	XMStoreFloat4x4(&p, proj);
	const float invTanHalf = fabsf(p._22) > 1e-4f ? fabsf(p._22) : 1.0f;   // 1 / tan(fov/2)
	const uint32_t frame = (uint32_t)ImGui::GetFrameCount();
	if (frame != s_LoadFrame)
	{
		s_LoadFrame = frame;
		s_LoadsThisFrame = 0;
	}
	static std::unordered_map<const PCG::MeshPart*, Group> groups;
	groups.clear();
	// 가까운 무거운 모델 (나무 · 큰 바위): 인스턴스마다 LOD (셀 전체가 LOD 0 이 되지 않게) — 이번 패스의 CPU 행렬
	static std::unordered_map<const PCG::MeshPart*, std::vector<XMFLOAT4X4>> nearWorlds;
	for (auto& kv : nearWorlds)
		kv.second.clear();
	for (PCGVolume* v : Registry())
	{
		if (!v->IsEnabled() || v->m_pGameObject == nullptr || !v->m_pGameObject->IsActiveInHierarchy())
			continue;
		int drawn = 0, batches = 0;
		for (const SpawnerInfo& s : v->m_Spawners)
		{
			if (shadow && (!s.Shadows || s.ShadowDistance <= 0.0f))
				continue;
			// 모델 (없으면 읽는다 — 프레임마다 두 개까지)
			std::vector<PCG::MeshAsset*> assets(s.Meshes.size(), nullptr);
			float maxHeight = 1.0f;
			for (size_t m = 0; m < s.Meshes.size(); ++m)
			{
				bool loaded = false;
				PCG::MeshAsset* a = PCG::GetMeshAsset(s.Meshes[m], s_LoadsThisFrame < 2, &loaded);
				if (loaded)
					++s_LoadsThisFrame;
				assets[m] = a;
				if (a)
					maxHeight = (std::max)(maxHeight, a->Height * 1.5f);
			}
			const float range = (shadow ? (std::min)(s.ShadowDistance, s.Cull) : s.Cull) * v->DistanceScale;
			for (auto& kv : v->m_Cells)
			{
				if (kv.first.Spawner != s.Id)
					continue;
				Cell& c = *kv.second;
				if (!c.Ready)
					continue;
				const float d = RectDistance(c.X0, c.Z0, c.X1, c.Z1, eye);
				if (d > range)
					continue;
				if (OutsideFrustum(Vec3(c.X0, c.MinY - 2.0f, c.Z0), Vec3(c.X1, c.MaxY + maxHeight, c.Z1), cullVP))
					continue;
				if (c.Buffers.size() != c.PerMesh.size())
					c.Buffers.assign(c.PerMesh.size(), {});
				for (size_t m = 0; m < c.PerMesh.size() && m < assets.size(); ++m)
				{
					PCG::MeshAsset* a = assets[m];
					const std::vector<XMFLOAT4X4>& inst = c.PerMesh[m];
					if (a == nullptr || inst.empty())
						continue;
					auto pickLevel = [&](float screenHeight) {
						int level = (int)a->Levels.size() - 1;
						for (int l = 0; l < (int)a->Levels.size(); ++l)
							if (screenHeight >= a->Levels[(size_t)l].ScreenHeight)
							{
								level = l;
								break;
							}
						return shadow ? (std::min)(level + 1, (int)a->Levels.size() - 1) : level;
					};
					// 가까운 셀의 무거운 모델 (LOD 0 이 1500 삼각형 넘게): LOD 2 가 되는 거리 안이면 인스턴스마다
					if (a->Levels[0].Triangles > 1500 && a->Levels.size() > 1)
					{
						const float h2 = a->Levels[(std::min)((size_t)2, a->Levels.size() - 1)].ScreenHeight;
						const float nearDist = h2 > 0.0f ? a->Height * invTanHalf * s.LodBias / (2.0f * h2) : 0.0f;
						if (d < nearDist)
						{
							for (const XMFLOAT4X4& w : inst)
							{
								const float dx = w._41 - eye.x, dy = w._42 - eye.y, dz = w._43 - eye.z;
								const float dist = (std::max)(1.0f, sqrtf(dx * dx + dy * dy + dz * dz));
								if (dist > range)
									continue;
								const float scale = sqrtf(w._11 * w._11 + w._12 * w._12 + w._13 * w._13);
								const PCG::MeshLevel& lv = a->Levels[(size_t)pickLevel(a->Height * scale / (2.0f * dist) * invTanHalf * s.LodBias)];
								const XMMATRIX iw = XMLoadFloat4x4(&w);
								for (const PCG::MeshPart& part : lv.Parts)
								{
									XMFLOAT4X4 out;
									XMStoreFloat4x4(&out, XMLoadFloat4x4(&part.Model) * iw);
									nearWorlds[&part].push_back(out);
								}
							}
							drawn += (int)inst.size();
							continue;
						}
					}
					// LOD: 셀의 가장 가까운 점까지 거리로 화면 높이 (Unity 의 Screen Relative Transition Height)
					const float screen = a->Height / (2.0f * (std::max)(d, 1.0f)) * invTanHalf * s.LodBias;
					int level = (int)a->Levels.size() - 1;
					for (int l = 0; l < (int)a->Levels.size(); ++l)
						if (screen >= a->Levels[(size_t)l].ScreenHeight)
						{
							level = l;
							break;
						}
					if (shadow)
						level = (std::min)(level + 1, (int)a->Levels.size() - 1);   // 그림자는 한 단계 거칠게
					auto& perLevel = c.Buffers[m];
					if (perLevel.size() != a->Levels.size())
						perLevel.assign(a->Levels.size(), {});
					const PCG::MeshLevel& lv = a->Levels[(size_t)level];
					std::vector<ComPtr<GfxBuffer>>& bufs = perLevel[(size_t)level];
					if (bufs.size() != lv.Parts.size())
					{
						// 이 LOD 를 처음 그린다: 파트마다 (모델 변환 × 인스턴스) 버퍼
						bufs.clear();
						std::vector<XMFLOAT4X4> worlds(inst.size());
						for (const PCG::MeshPart& part : lv.Parts)
						{
							const XMMATRIX model = XMLoadFloat4x4(&part.Model);
							for (size_t i = 0; i < inst.size(); ++i)
								XMStoreFloat4x4(&worlds[i], model * XMLoadFloat4x4(&inst[i]));
							bufs.push_back(MeshBatcher::CreateInstanceBuffer(worlds.data(), (uint32_t)worlds.size()));
						}
					}
					for (size_t pi = 0; pi < lv.Parts.size(); ++pi)
					{
						const PCG::MeshPart& part = lv.Parts[pi];
						if (!bufs[pi] || part.MeshPtr == nullptr)
							continue;
						Group& g = groups[&part];
						g.Part = &part;
						g.Segments.push_back({ bufs[pi].Get(), (uint32_t)inst.size() });
						g.Total += (uint32_t)inst.size();
						g.Signature = (g.Signature ^ (uint64_t)(uintptr_t)bufs[pi].Get()) * 1099511628211ull;
					}
					drawn += (int)inst.size();
				}
			}
		}
		if (pass == MeshBatcher::Pass::Main)
		{
			v->m_LastDrawn = drawn;
			v->m_LastBatches = (int)groups.size();
		}
		(void)batches;
	}

	// 묶음마다: 셀 버퍼들을 큰 버퍼 하나로 (GPU 복사, 같은 프레임 · 같은 셀 묶음이면 다시 복사하지 않는다 — 깊이 프리패스 → 본 패스, 그림자 조각들)
	GfxContext* dc = Application::GetI()->GetDeviceContext();
	GfxDevice* device = Application::GetI()->GetDevice();
	const UINT stride = OcclusionCulling::InstanceBytes;
	for (auto& kv : groups)
	{
		Group& g = kv.second;
		if (g.Total == 0)
			continue;
		const PCG::MeshPart& part = *g.Part;
		GfxBuffer* buffer = nullptr;
		if (g.Segments.size() == 1)
			buffer = g.Segments[0].Buffer;   // 셀 하나 — 복사 없이
		else
		{
			Merged& m = s_Merged[{ &part, editor, shadow }];
			m.LastUsed = frame;
			if (m.Capacity < g.Total)
			{
				m.Capacity = (std::max)(g.Total, m.Capacity + m.Capacity / 2);
				D3D11_BUFFER_DESC bd = {};
				bd.Usage = D3D11_USAGE_DEFAULT;
				bd.ByteWidth = m.Capacity * stride;
				bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
				m.Buffer.Reset();
				device->CreateBuffer(&bd, nullptr, m.Buffer.GetAddressOf());
				m.FilledFrame = ~0u;
			}
			if (!m.Buffer)
				continue;
			if (m.FilledFrame != frame || m.FilledSignature != g.Signature)
			{
				UINT offset = 0;
				for (const Segment& seg : g.Segments)
				{
					const D3D11_BOX box = { 0, 0, 0, seg.Count * stride, 1, 1 };
					dc->CopySubresourceRegion(m.Buffer.Get(), 0, offset, 0, 0, seg.Buffer, 0, &box);
					offset += seg.Count * stride;
				}
				m.FilledFrame = frame;
				m.FilledSignature = g.Signature;
			}
			buffer = m.Buffer.Get();
		}
		for (size_t k = 0; k < part.MeshPtr->Subsets.size(); ++k)
		{
			MeshBatcher::ExternalBatch b;
			b.MeshPtr = part.MeshPtr.get();
			b.Subset = (int)k;
			b.Material = k < part.Materials.size() ? part.Materials[k] : UMaterial::GetDefault();
			b.Buffer = buffer;
			b.Count = g.Total;
			b.TwoSided = k < part.TwoSided.size() && part.TwoSided[k];
			out.push_back(std::move(b));
		}
	}
	// 가까운 무거운 모델 (인스턴스마다 LOD) — CPU 행렬, 이번 패스에 올린다
	for (auto& kv : nearWorlds)
	{
		if (kv.second.empty())
			continue;
		const PCG::MeshPart& part = *kv.first;
		for (size_t k = 0; k < part.MeshPtr->Subsets.size(); ++k)
		{
			MeshBatcher::ExternalBatch b;
			b.MeshPtr = part.MeshPtr.get();
			b.Subset = (int)k;
			b.Material = k < part.Materials.size() ? part.Materials[k] : UMaterial::GetDefault();
			b.Worlds = kv.second.data();
			b.Count = (uint32_t)kv.second.size();
			b.TwoSided = k < part.TwoSided.size() && part.TwoSided[k];
			out.push_back(std::move(b));
		}
	}
	// 오래 안 쓴 묶음 버퍼는 놓는다
	if ((frame & 127u) == 0)
		for (auto it = s_Merged.begin(); it != s_Merged.end();)
			it = frame - it->second.LastUsed > 300u ? s_Merged.erase(it) : std::next(it);
}

nlohmann::json PCGVolume::Info() const
{
	int ready = 0, pending = 0;
	size_t instances = 0;
	std::map<int, std::pair<int, size_t>> perSpawner;
	for (const auto& kv : m_Cells)
	{
		ready += kv.second->Ready;
		pending += kv.second->Pending;
		size_t n = 0;
		for (const auto& v : kv.second->PerMesh)
			n += v.size();
		instances += n;
		perSpawner[kv.first.Spawner].first += kv.second->Ready;
		perSpawner[kv.first.Spawner].second += n;
	}
	nlohmann::json spawners = nlohmann::json::array();
	for (const SpawnerInfo& s : m_Spawners)
		spawners.push_back({ { "id", s.Id }, { "name", s.Name }, { "cull", s.Cull }, { "cellSize", s.CellSize }, { "meshes", s.Meshes.size() },
			{ "cells", perSpawner[s.Id].first }, { "instances", perSpawner[s.Id].second } });
	return { { "graph", GraphPath }, { "seed", Seed }, { "cells", m_Cells.size() }, { "ready", ready }, { "pending", pending },
		{ "instances", instances }, { "drawn", m_LastDrawn }, { "batches", m_LastBatches }, { "generated", m_Generated },
		{ "avgCellMs", m_Generated ? m_GenMs / m_Generated : 0.0 }, { "meshAssets", PCG::LoadedMeshAssets() }, { "spawners", spawners } };
}

void PCGVolume::OnInspectorGUI()
{
	using namespace UnityGUI;
	char buf[260];
	TextField("Graph", &GraphPath);
	Int("Seed", &Seed);
	Toggle("Whole World", &WholeWorld);
	Slider("Distance Scale", &DistanceScale, 0.1f, 3.0f);
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 18.0f);
	if (ImGui::Button("Open Graph", ImVec2(110, 0)) && s_OpenGraphWindow)
		s_OpenGraphWindow(GraphPath);
	ImGui::SameLine();
	if (ImGui::Button("Regenerate", ImVec2(110, 0)))
		Regenerate();
	const nlohmann::json info = Info();
	snprintf(buf, sizeof(buf), "%d cells (%d generating), %zu instances, drawn %d in %d batches, %.1f ms per cell",
		(int)info["cells"], (int)info["pending"], (size_t)info["instances"], (int)info["drawn"], (int)info["batches"], (double)info["avgCellMs"]);
	HelpBox(buf, false);
	for (const auto& s : info["spawners"])
	{
		snprintf(buf, sizeof(buf), "%s: %d cells, %zu instances (cull %.0f m, cell %.0f m)", s["name"].get<std::string>().c_str(), (int)s["cells"],
			(size_t)s["instances"], (float)s["cull"], (float)s["cellSize"]);
		ImGui::TextDisabled("%s", buf);
	}
}

void PCGVolume::OnDrawGizmos()
{
	if (WholeWorld || !SceneViewOverlay::IsActive() || SelectionManager::GetSelectedGameObject() != m_pGameObject)
		return;
	const float y = m_pGameObject->GetTransform()->GetPosition().y;
	const XMFLOAT3 c[4] = { { m_BoundsX0, y, m_BoundsZ0 }, { m_BoundsX1, y, m_BoundsZ0 }, { m_BoundsX1, y, m_BoundsZ1 }, { m_BoundsX0, y, m_BoundsZ1 } };
	for (int i = 0; i < 4; ++i)
		SceneViewOverlay::DrawLine(c[i], c[(i + 1) % 4], IM_COL32(90, 220, 120, 230), 2.0f);
}

GENERATE_COMPONENT_FUNC_TOJSON(PCGVolume)
{
	json j;
	SERIALIZE_TYPE(j, PCGVolume);
	j["enabled"] = m_Enabled;
	j["graph"] = GraphPath;
	j["seed"] = Seed;
	j["wholeWorld"] = WholeWorld;
	j["distanceScale"] = DistanceScale;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(PCGVolume)
{
	m_Enabled = j.value("enabled", true);
	GraphPath = j.value("graph", GraphPath);
	Seed = j.value("seed", Seed);
	WholeWorld = j.value("wholeWorld", WholeWorld);
	DistanceScale = j.value("distanceScale", DistanceScale);
}
