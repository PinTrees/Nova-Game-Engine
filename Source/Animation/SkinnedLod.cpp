#include "pch.h"
#include "SkinnedLod.h"
#include "SkinnedMesh.h"
#include "JobSystem.h"
#include "Application.h"
#include "meshoptimizer.h"
#include "EditorLog.h"
#include "SkinnedData.h"
#include "HumanoidAvatar.h"
#include "AnimationPose.h"

namespace SkinnedLod
{
	float Bias = 1.0f;
	int ForceLevel = -1;
	bool Impostors = true;
	float ImpostorScreen = 0.035f;

	namespace
	{
		struct Level
		{
			std::vector<USHORT> Indices;                 // 서브셋마다 VertexStart 기준
			std::vector<MeshGeometry::Subset> Subsets;
			std::vector<Vertex::PosNormalTexTanSkinned> Vertices;   // 이 단계가 쓰는 정점만 (본 줄이기 — 가중치를 남는 본으로)
			int Triangles = 0;
			int Bones = 0;                               // 정점이 가리키는 팔레트 본 수
			std::vector<int> Used;                       // 그 팔레트 본 번호
		};
		struct Built
		{
			Level Levels[kLevels];
		};
		struct Entry
		{
			std::weak_ptr<SkinnedMesh> Base;
			Jobs::Future<std::shared_ptr<Built>> Job;
			std::unique_ptr<MeshGeometry> Geo[kLevels];
			int Triangles[kLevels] = { -1, -1, -1, -1 };
			int Bones[kLevels] = { -1, -1, -1, -1 };
			std::vector<int> Used[kLevels];
			bool Uploaded = false;
			bool Failed = false;
		};
		std::unordered_map<const SkinnedMesh*, Entry> s_Entries;


		// 본 줄이기 (Skeletal LOD — 먼 사람은 본 수를 줄인다): 단계마다 남기는 사람 본 (Humanoid). 나머지 본 (손가락 · 트위스트 · 천 · 얼굴 …)
		//  의 가중치는 메시 팔레트에 있는 가장 가까운 "남는 조상 본" 으로. 가장 먼 단계는 정점마다 본 하나
		using HB = Humanoid::Bone;
		const std::vector<int>& KeptBones(int level)
		{
			static const std::vector<int> l2 = { HB::Hips, HB::Spine, HB::Chest, HB::Head, HB::LeftUpperArm, HB::LeftLowerArm, HB::RightUpperArm,
				HB::RightLowerArm, HB::LeftUpperLeg, HB::LeftLowerLeg, HB::RightUpperLeg, HB::RightLowerLeg };
			static const std::vector<int> l3 = { HB::Hips, HB::Chest, HB::Head, HB::LeftUpperArm, HB::RightUpperArm, HB::LeftUpperLeg, HB::RightUpperLeg };
			// 정적 초기화 한 번 (여러 LOD 만들기 잡이 동시에 부른다 — 예전에는 처음 부른 잡들이 함께 채우다 깨졌다)
			static const std::vector<int> l1 = [] {
				std::vector<int> all;
				for (int b = 0; b < HB::BoneCount; ++b)
					all.push_back(b);
				return all;
			}();
			return level <= 1 ? l1 : level == 2 ? l2 : l3;
		}

		// 팔레트 본 k → 이 단계에서 쓸 팔레트 본 (없으면 그대로)
		std::vector<int> BoneRemap(const SkinnedMesh& mesh, const SkeletonAvataData* sk, int level)
		{
			std::vector<int> remap(mesh.BoneNames.size());
			for (size_t k = 0; k < remap.size(); ++k)
				remap[k] = (int)k;
			if (sk == nullptr)
				return remap;
			const Humanoid::Avatar& av = Humanoid::Get(*sk);
			if (!av.Valid)
				return remap;
			std::vector<uint8_t> kept(sk->BoneHierarchy.size(), 0);
			for (int b : KeptBones(level))
				if (av.Node[b] >= 0 && (size_t)av.Node[b] < kept.size())
					kept[(size_t)av.Node[b]] = 1;
			// 같은 노드라도 팔레트 칸이 여럿일 수 있다 (합친 메시 — 부위마다 메시 바인드가 다르다) → 바인드 자세에서 같은 변환 (Offset · 전역) 을
			//  주는 칸으로만 옮긴다 (정점이 다른 부위의 공간으로 가지 않게)
			std::vector<XMFLOAT4X4> global;
			AnimationPose::ComputeGlobals(*sk, sk->BindLocal, global);
			std::unordered_multimap<int, int> paletteOfNode;
			std::vector<int> nodeOf(mesh.BoneNames.size(), -1);
			std::vector<XMFLOAT4X4> bind(mesh.BoneNames.size());
			for (size_t k = 0; k < mesh.BoneNames.size(); ++k)
			{
				nodeOf[k] = sk->FindNode(mesh.BoneNames[k]);
				if (nodeOf[k] >= 0 && (size_t)nodeOf[k] < global.size() && k < mesh.BoneOffsets.size())
				{
					paletteOfNode.emplace(nodeOf[k], (int)k);
					XMStoreFloat4x4(&bind[k], XMLoadFloat4x4(&mesh.BoneOffsets[k]) * XMLoadFloat4x4(&global[(size_t)nodeOf[k]]));
				}
			}
			auto same = [&](size_t a, size_t b) {
				float d = 0.0f, m = 1e-6f;
				for (int i = 0; i < 16; ++i)
				{
					d += fabsf((&bind[a]._11)[i] - (&bind[b]._11)[i]);
					m += fabsf((&bind[a]._11)[i]);
				}
				return d < m * 1e-3f;
			};
			for (size_t k = 0; k < remap.size(); ++k)
			{
				bool done = false;
				for (int n = nodeOf[k]; n >= 0 && (size_t)n < kept.size() && !done; n = sk->BoneHierarchy[(size_t)n])
				{
					if (!kept[(size_t)n])
						continue;
					auto range = paletteOfNode.equal_range(n);
					for (auto it = range.first; it != range.second; ++it)
						if (same(k, (size_t)it->second))
						{
							remap[k] = it->second;
							done = true;
							break;
						}
				}
			}
			return remap;
		}

		Vertex::PosNormalTexTanSkinned RemapVertex(const Vertex::PosNormalTexTanSkinned& v, const std::vector<int>& remap, bool single)
		{
			const float w[4] = { v.weights.x, v.weights.y, v.weights.z, 1.0f - v.weights.x - v.weights.y - v.weights.z };
			int bone[4] = { -1, -1, -1, -1 };
			float wt[4] = { 0, 0, 0, 0 };
			int n = 0;
			for (int j = 0; j < 4; ++j)
			{
				if (w[j] <= 0.0f)
					continue;
				const int k = v.boneIndices[j] < remap.size() ? remap[v.boneIndices[j]] : v.boneIndices[j];
				int slot = -1;
				for (int i = 0; i < n; ++i)
					if (bone[i] == k) { slot = i; break; }
				if (slot < 0) { slot = n++; bone[slot] = k; }
				wt[slot] += w[j];
			}
			// 무거운 순서
			for (int i = 0; i < n; ++i)
				for (int j = i + 1; j < n; ++j)
					if (wt[j] > wt[i]) { std::swap(wt[i], wt[j]); std::swap(bone[i], bone[j]); }
			if (single && n > 1)
				n = 1;
			float sum = 0.0f;
			for (int i = 0; i < n; ++i) sum += wt[i];
			Vertex::PosNormalTexTanSkinned o = v;
			float ow[4] = { 0, 0, 0, 0 };
			for (int i = 0; i < 4; ++i)
			{
				o.boneIndices[i] = (BYTE)(i < n ? bone[i] : (n > 0 ? bone[0] : 0));
				ow[i] = i < n && sum > 0.0f ? wt[i] / sum : 0.0f;
			}
			if (n == 0) ow[0] = 1.0f;
			o.weights = XMFLOAT3(ow[0], ow[1], ow[2]);   // 네 번째 = 1 − 합 (셰이더)
			return o;
		}

		// 작업 스레드: 서브셋마다 위치만 보고 줄인다 (UV · 법선 이음매는 meshoptimizer 가 같은 위치로 묶어 따라간다)
		std::shared_ptr<Built> Build(std::shared_ptr<SkinnedMesh> mesh, std::shared_ptr<SkeletonAvataData> skeleton, std::chrono::steady_clock::time_point queued)
		{
			const auto t0 = std::chrono::steady_clock::now();
			auto out = std::make_shared<Built>();
			const auto& verts = mesh->Vertices;
			const auto& idx = mesh->Indices;
			const size_t stride = sizeof(Vertex::PosNormalTexTanSkinned);
			for (int l = 0; l < kLevels; ++l)
				out->Levels[l].Subsets = mesh->Subsets;
			std::vector<int> remaps[kLevels];
			for (int l = 1; l < kLevels; ++l)
				remaps[l] = BoneRemap(*mesh, skeleton.get(), l);
			std::vector<unsigned int> src, dst;
			for (size_t s = 0; s < mesh->Subsets.size(); ++s)
			{
				const MeshGeometry::Subset& sub = mesh->Subsets[s];
				const size_t first = (size_t)sub.FaceStart * 3, count = (size_t)sub.FaceCount * 3;
				if (first + count > idx.size() || (size_t)sub.VertexStart + sub.VertexCount > verts.size() || sub.VertexCount == 0)
				{
					for (int l = 0; l < kLevels; ++l)
						out->Levels[l].Subsets[s].FaceStart = (uint32)(out->Levels[l].Indices.size() / 3), out->Levels[l].Subsets[s].FaceCount = 0;
					continue;
				}
				src.assign(idx.begin() + first, idx.begin() + first + count);
				bool inRange = true;   // VertexStart 기준 번호여야 한다 (아니면 줄이지 않고 원본 그대로)
				for (unsigned int v : src)
					inRange = inRange && v < sub.VertexCount;
				const float* pos = &verts[sub.VertexStart].pos.x;
				std::vector<unsigned int> prev = src;
				for (int l = 0; l < kLevels; ++l)
				{
					Level& lv = out->Levels[l];
					if (l > 0 && inRange)
					{
						// 단계마다 원본에서 줄인다 (앞 단계에서 이어 줄이면 오차가 쌓여 팔다리가 가늘어지고 머리가 뾰족해진다).
						//  법선도 함께 본다 — 굽은 면 (팔다리 · 머리) 의 부피를 지킨다. 오차 = 메시 크기에 대한 비율
						//  먼 두 단계는 이음매 · 조각 경계도 넘어 줄인다 (Permissive — 조각으로 된 몸은 경계에 막혀 6 천에서 멈추고 경계를 지키느라 머리가 뾰족해졌다)
						static const float kError[kLevels] = { 0.0f, 0.01f, 0.06f, 0.12f };
						static const unsigned kOptions[kLevels] = { 0, 0, meshopt_SimplifyPermissive, meshopt_SimplifyPermissive };
						static const float kNormalWeight[3] = { 0.5f, 0.5f, 0.5f };
						const size_t target = (std::min)((std::max)((size_t)(count * kRatio[l]) / 3 * 3, (size_t)36), src.size());
						dst.resize(src.size());
						float err = 0.0f;
						size_t n = meshopt_simplifyWithAttributes(dst.data(), src.data(), src.size(), pos, sub.VertexCount, stride,
							&verts[sub.VertexStart].normal.x, stride, kNormalWeight, 3, nullptr,
							target, kError[l], kOptions[l], &err);
						// 앞 단계보다 많으면 앞 단계 (오차 한도에 막힘), 다 없어지면 앞 단계 그대로 (작은 조각이 사라지지 않게)
						if (n == 0 || n > prev.size())
							n = prev.size(), dst = prev;
						dst.resize(n);
						meshopt_optimizeVertexCache(dst.data(), dst.data(), dst.size(), sub.VertexCount);
						prev = dst;
					}
					lv.Subsets[s].FaceStart = (uint32)(lv.Indices.size() / 3);
					lv.Subsets[s].FaceCount = (uint32)(prev.size() / 3);
					lv.Triangles += (int)(prev.size() / 3);
					if (l == 0)
					{
						for (unsigned int v : prev)
							lv.Indices.push_back((USHORT)v);
						continue;
					}
					// 이 단계가 쓰는 정점만 모아 (본 줄이기로 가중치가 달라져 원본 정점 버퍼를 같이 쓰지 않는다)
					std::unordered_map<unsigned int, USHORT> local;
					const uint32 start = (uint32)lv.Vertices.size();
					for (unsigned int v : prev)
					{
						auto it = local.find(v);
						if (it == local.end())
						{
							it = local.emplace(v, (USHORT)(lv.Vertices.size() - start)).first;
							lv.Vertices.push_back(RemapVertex(verts[sub.VertexStart + v], remaps[l], l == kLevels - 1));
						}
						lv.Indices.push_back(it->second);
					}
					lv.Subsets[s].VertexStart = start;
					lv.Subsets[s].VertexCount = (uint32)(lv.Vertices.size() - start);
				}
			}
			for (int l = 0; l < kLevels; ++l)
			{
				std::vector<uint8_t> used(256, 0);
				for (const auto& v : (l == 0 ? mesh->Vertices : out->Levels[l].Vertices))
					for (int j = 0; j < 4; ++j) used[v.boneIndices[j]] = 1;
				for (int k = 0; k < 256; ++k)
					if (used[(size_t)k])
						out->Levels[l].Used.push_back(k);
				out->Levels[l].Bones = (int)out->Levels[l].Used.size();
			}
			const auto t1 = std::chrono::steady_clock::now();
			EditorLog::Write("SkinnedLod", "%s: %d / %d / %d / %d triangles, %d / %d / %d / %d bones (waited %.0f ms, built %.0f ms)", mesh->Name.c_str(),
				out->Levels[0].Triangles, out->Levels[1].Triangles, out->Levels[2].Triangles, out->Levels[3].Triangles,
				out->Levels[0].Bones, out->Levels[1].Bones, out->Levels[2].Bones, out->Levels[3].Bones,
				std::chrono::duration<double, std::milli>(t0 - queued).count(), std::chrono::duration<double, std::milli>(t1 - t0).count());
			return out;
		}

		Entry* Find(const std::shared_ptr<SkinnedMesh>& base, bool start, const std::shared_ptr<SkeletonAvataData>& skeleton = nullptr)
		{
			if (!base || base->Vertices.empty() || base->Indices.empty())
				return nullptr;
			auto it = s_Entries.find(base.get());
			if (it != s_Entries.end() && it->second.Base.lock() != base)   // 같은 주소에 다른 메시 (예전 것이 지워짐)
			{
				s_Entries.erase(it);
				it = s_Entries.end();
			}
			if (it == s_Entries.end())
			{
				if (!start)
					return nullptr;
				Entry& e = s_Entries[base.get()];
				e.Base = base;
				// Normal: 백그라운드 줄 (셰이더 변형 컴파일 등) 뒤에서 기다리지 않게
				e.Job = Jobs::Async([base, skeleton, q = std::chrono::steady_clock::now()]() { return Build(base, skeleton, q); }, Jobs::Priority::Normal, "SkinnedLod");
				return &e;
			}
			return &it->second;
		}

		// 메인: 작업이 끝났으면 GPU 인덱스 버퍼를 만든다 (정점 버퍼는 원본 것을 같이)
		void Upload(Entry& e, const std::shared_ptr<SkinnedMesh>& base)
		{
			if (e.Uploaded || e.Failed || !e.Job.Ready())
				return;
			std::shared_ptr<Built> b;
			try { b = e.Job.get(); }
			catch (...) { b = nullptr; }
			GfxDevice* device = Application::GetI()->GetDevice();
			if (!b || device == nullptr)
			{
				e.Failed = true;
				return;
			}
			for (int l = 0; l < kLevels; ++l)
			{
				e.Triangles[l] = b->Levels[l].Triangles;
				e.Bones[l] = b->Levels[l].Bones;
				e.Used[l] = b->Levels[l].Used;
				if (l == 0 || b->Levels[l].Indices.empty() || b->Levels[l].Vertices.empty())
					continue;
				auto g = std::make_unique<MeshGeometry>();
				g->SetVertices(device, b->Levels[l].Vertices.data(), (uint32)b->Levels[l].Vertices.size());
				g->SetIndices(device, b->Levels[l].Indices.data(), (uint32)b->Levels[l].Indices.size());
				g->SetSubsetTable(b->Levels[l].Subsets);
				e.Geo[l] = std::move(g);
			}
			e.Uploaded = true;
		}
	}

	MeshGeometry* Get(const std::shared_ptr<SkinnedMesh>& base, int level, const std::shared_ptr<SkeletonAvataData>& skeleton)
	{
		if (level <= 0)
			return nullptr;
		Entry* e = Find(base, true, skeleton);
		if (e == nullptr)
			return nullptr;
		Upload(*e, base);
		if (!e->Uploaded)
			return nullptr;
		for (int l = (std::min)(level, kLevels - 1); l > 0; --l)
			if (e->Geo[l])
				return e->Geo[l].get();
		return nullptr;
	}

	void Request(const std::shared_ptr<SkinnedMesh>& base, const std::shared_ptr<SkeletonAvataData>& skeleton)
	{
		Find(base, true, skeleton);
	}

	const std::vector<int>* UsedBones(const SkinnedMesh* base, int level)
	{
		auto it = s_Entries.find(base);
		if (it == s_Entries.end() || !it->second.Uploaded || level < 0 || level >= kLevels)
			return nullptr;
		return &it->second.Used[level];
	}

	int Bones(const std::shared_ptr<SkinnedMesh>& base, int level)
	{
		Entry* e = Find(base, false);
		if (e == nullptr || level < 0 || level >= kLevels)
			return -1;
		Upload(*e, base);
		return e->Bones[level];
	}

	bool WaitReady(const std::shared_ptr<SkinnedMesh>& base)
	{
		Entry* e = Find(base, true);
		if (e == nullptr)
			return false;
		e->Job.wait();
		Upload(*e, base);
		return e->Uploaded;
	}

	int Triangles(const std::shared_ptr<SkinnedMesh>& base, int level)
	{
		Entry* e = Find(base, false);
		if (e == nullptr || level < 0 || level >= kLevels)
			return -1;
		Upload(*e, base);
		return e->Triangles[level];
	}

	void Clear()
	{
		s_Entries.clear();   // Future 는 지울 때 끝나기를 기다린다
	}

	int SelectLevel(float screenHeight, int previous)
	{
		// 화면 높이 비율 경계 (Unity LOD Group 의 Screen Relative Height 와 같은 뜻): 위로 갈수록 자세히
		static const float kEdge[kLevels - 1] = { 0.25f, 0.10f, 0.04f };
		if (ForceLevel >= 0)
			return (std::min)(ForceLevel, Impostors ? kImpostorLevel : kLevels - 1);
		const float edges[kLevels] = { kEdge[0], kEdge[1], kEdge[2], Impostors ? ImpostorScreen : 0.0f };
		const float h = screenHeight * Bias;
		int level = Impostors ? kImpostorLevel : kLevels - 1;
		for (int l = 0; l < kLevels; ++l)
			if (h >= edges[l]) { level = l; break; }
		// 경계 근처 (± 10 %) 에서는 앞 단계를 지킨다
		if (previous >= 0 && previous <= kImpostorLevel && previous != level)
		{
			const int edge = (std::min)(previous, level);   // 둘 사이의 경계 edges[edge]
			if (std::abs(level - previous) == 1 && edge < kLevels && std::abs(h - edges[edge]) < edges[edge] * 0.1f)
				return previous;
		}
		return level;
	}
}
