#include "pch.h"
#include "TransformStore.h"
#include "Transform.h"
#include "JobSystem.h"
#include "Profiler.h"
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using namespace DirectX;

// 데이터 지향 Transform — TransformStore.h 의 설명
namespace TransformStore
{
	namespace
	{
		struct alignas(64) WorldMat { XMFLOAT4X4 M; };   // 한 행렬 = 한 캐시 라인

		struct Data
		{
			// SoA: 같은 종류의 값끼리 연속 (계층 갱신은 로컬 TRS + 부모 번호만 읽고 월드 배열만 쓴다)
			std::vector<XMFLOAT3> LocalPos, LocalScale, WorldScale;
			std::vector<XMFLOAT4> LocalRot, WorldRot;
			std::vector<WorldMat> World;
			std::vector<int32_t> Parent;
			std::vector<uint32_t> ParentGen; // 이을 때 부모 자리의 세대 — 부모가 먼저 놓이고 자리가 다시 쓰이면 어긋난다 (그때는 루트로)
			std::vector<uint32_t> Gen;       // 자리를 놓을 때마다 +1
			std::vector<uint8_t> Dirty;     // 월드를 다시 계산해야 한다 (더러우면 그 아래도 모두 더럽다)
			std::vector<uint8_t> InRoots;   // Roots 목록에 있다
			std::vector<uint32_t> Version;
			std::vector<Transform*> Owner;  // 자식 목록 (계층 순회) — 놓인 자리는 nullptr
			std::vector<uint32_t> Free;
			std::vector<uint32_t> Roots;    // 더러움 표시를 시작한 자리 (그때 부모는 깨끗했다)
			uint32_t Live = 0;
			// 통계
			uint64_t Flushes = 0, LazyResolves = 0, PureComputes = 0, Marks = 0;
			uint32_t LastRoots = 0, LastSubtrees = 0, LastResolved = 0;
			double LastFlushMs = 0.0;
		};
		// 끝날 때 지우지 않는다 — 정적 소멸 순서와 상관없이 Transform 이 자리를 놓을 수 있게
		Data& D() { static Data* d = new Data(); return *d; }

		// 부모 자리 (부모가 이미 놓였으면 -1 — 예전 Transform 의 weak_ptr 부모가 사라진 것과 같다)
		inline int32_t ParentOf(const Data& d, uint32_t i)
		{
			const int32_t p = d.Parent[i];
			return (p >= 0 && d.Gen[(size_t)p] == d.ParentGen[i]) ? p : -1;
		}

		std::thread::id s_Main;
		bool s_MainSet = false;
		// 배열을 고쳐도 되는가: 메인 스레드이고 ParallelFor 중이 아닐 때 (그때는 메인도 한 몫을 돌아 일꾼과 같은 사슬을 읽을 수 있다)
		bool OnMain() { return (!s_MainSet || std::this_thread::get_id() == s_Main) && !Jobs::InParallel(); }

		// 로컬 TRS 행렬 (예전 Transform::UpdateTransform 과 같은 순서: S · R · T)
		inline XMMATRIX LocalMatrix(const Data& d, uint32_t i)
		{
			return XMMatrixMultiply(XMMatrixMultiply(XMMatrixScalingFromVector(XMLoadFloat3(&d.LocalScale[i])), XMMatrixRotationQuaternion(XMLoadFloat4(&d.LocalRot[i]))),
				XMMatrixTranslationFromVector(XMLoadFloat3(&d.LocalPos[i])));
		}

		// i 의 월드 값 (부모는 깨끗해야 한다) → 배열에
		inline void Compute(Data& d, uint32_t i)
		{
			const XMMATRIX local = LocalMatrix(d, i);
			const XMVECTOR r = XMLoadFloat4(&d.LocalRot[i]);
			const int32_t p = ParentOf(d, i);
			if (p >= 0)
			{
				XMStoreFloat4x4(&d.World[i].M, XMMatrixMultiply(local, XMLoadFloat4x4(&d.World[(size_t)p].M)));
				// 로컬 회전 → 부모 회전 순서. 크기는 Unity 의 lossyScale 과 같은 근사 (성분끼리 곱)
				XMStoreFloat4(&d.WorldRot[i], XMQuaternionNormalize(XMQuaternionMultiply(r, XMLoadFloat4(&d.WorldRot[(size_t)p]))));
				XMStoreFloat3(&d.WorldScale[i], XMVectorMultiply(XMLoadFloat3(&d.LocalScale[i]), XMLoadFloat3(&d.WorldScale[(size_t)p])));
			}
			else
			{
				XMStoreFloat4x4(&d.World[i].M, local);
				d.WorldRot[i] = d.LocalRot[i];
				d.WorldScale[i] = d.LocalScale[i];
			}
			d.Dirty[i] = 0;
			++d.Version[i];
		}

		// 메인: i 와 더러운 조상 사슬을 위에서 아래로 계산
		void Resolve(Data& d, uint32_t i)
		{
			if (!d.Dirty[i])
				return;
			uint32_t chain[128];
			int n = 0;
			uint32_t cur = i;
			for (;;)
			{
				chain[n++] = cur;
				const int32_t p = ParentOf(d, cur);
				if (p < 0 || !d.Dirty[(size_t)p])
					break;
				if (n == 128)
				{
					Resolve(d, (uint32_t)p);   // 아주 깊은 계층 — 위쪽부터 먼저
					break;
				}
				cur = (uint32_t)p;
			}
			for (int k = n - 1; k >= 0; --k)
				Compute(d, chain[k]);
			++d.LazyResolves;
		}

		struct WorldTRS { XMMATRIX M; XMVECTOR R; XMVECTOR S; };

		// 메인이 아닌 스레드: 배열을 바꾸지 않고 그 자리에서 (더러운 조상 사슬도 임시로)
		WorldTRS ComputePure(const Data& d, uint32_t i)
		{
			uint32_t chain[128];
			int n = 0;
			uint32_t cur = i;
			int32_t top = -1;
			for (;;)
			{
				chain[n++] = cur;
				const int32_t p = ParentOf(d, cur);
				if (p < 0 || !d.Dirty[(size_t)p] || n == 128)
				{
					top = p;
					break;
				}
				cur = (uint32_t)p;
			}
			WorldTRS acc;
			bool have = top >= 0;
			if (have)
			{
				acc.M = XMLoadFloat4x4(&d.World[(size_t)top].M);
				acc.R = XMLoadFloat4(&d.WorldRot[(size_t)top]);
				acc.S = XMLoadFloat3(&d.WorldScale[(size_t)top]);
			}
			for (int k = n - 1; k >= 0; --k)
			{
				const uint32_t s = chain[k];
				const XMMATRIX local = LocalMatrix(d, s);
				const XMVECTOR r = XMLoadFloat4(&d.LocalRot[s]);
				const XMVECTOR sc = XMLoadFloat3(&d.LocalScale[s]);
				if (have)
				{
					acc.M = XMMatrixMultiply(local, acc.M);
					acc.R = XMQuaternionNormalize(XMQuaternionMultiply(r, acc.R));
					acc.S = XMVectorMultiply(sc, acc.S);
				}
				else
				{
					acc.M = local;
					acc.R = r;
					acc.S = sc;
					have = true;
				}
			}
			return acc;
		}

		std::vector<uint32_t>& Stack() { thread_local std::vector<uint32_t> s; return s; }
	}

	void SetMainThread()
	{
		s_Main = std::this_thread::get_id();
		s_MainSet = true;
	}

	uint32_t Allocate(Transform* owner)
	{
		Data& d = D();
		uint32_t i;
		if (!d.Free.empty())
		{
			i = d.Free.back();
			d.Free.pop_back();
		}
		else
		{
			i = (uint32_t)d.Owner.size();
			d.LocalPos.emplace_back(); d.LocalScale.emplace_back(); d.WorldScale.emplace_back();
			d.LocalRot.emplace_back(); d.WorldRot.emplace_back(); d.World.emplace_back();
			d.Parent.push_back(-1); d.ParentGen.push_back(0); d.Gen.push_back(0); d.Dirty.push_back(0); d.InRoots.push_back(0); d.Version.push_back(0); d.Owner.push_back(nullptr);
		}
		d.LocalPos[i] = XMFLOAT3(0, 0, 0);
		d.LocalScale[i] = d.WorldScale[i] = XMFLOAT3(1, 1, 1);
		d.LocalRot[i] = d.WorldRot[i] = XMFLOAT4(0, 0, 0, 1);
		XMStoreFloat4x4(&d.World[i].M, XMMatrixIdentity());
		d.Parent[i] = -1;
		d.Dirty[i] = 0;
		++d.Version[i];   // 자리를 다시 쓰면 번호가 바뀐다 (예전 주인의 번호를 기억한 컬링이 새 것으로 본다)
		d.Owner[i] = owner;
		++d.Live;
		return i;
	}

	void Release(uint32_t slot)
	{
		Data& d = D();
		if (slot >= d.Owner.size() || !d.Owner[slot])
			return;
		d.Owner[slot] = nullptr;
		d.Dirty[slot] = 0;
		d.Parent[slot] = -1;
		++d.Gen[slot];   // 이 자리를 부모로 둔 자식은 이제 루트로 본다
		d.Free.push_back(slot);
		--d.Live;
	}

	void MarkDirty(uint32_t slot)
	{
		Data& d = D();
		if (d.Dirty[slot])
			return;   // 이미 더럽다 (그 아래도)
		++d.Marks;
		const int32_t p = ParentOf(d, slot);
		if ((p < 0 || !d.Dirty[(size_t)p]) && !d.InRoots[slot])
		{
			d.Roots.push_back(slot);
			d.InRoots[slot] = 1;
		}
		std::vector<uint32_t>& stack = Stack();
		stack.clear();
		stack.push_back(slot);
		while (!stack.empty())
		{
			const uint32_t s = stack.back();
			stack.pop_back();
			if (d.Dirty[s])
				continue;
			d.Dirty[s] = 1;
			if (Transform* o = d.Owner[s])
				for (const auto& c : o->GetChildren())
					if (c)
						stack.push_back(c->Slot());
		}
	}

	void SetLocal(uint32_t slot, const XMFLOAT3& position, const XMFLOAT4& rotation, const XMFLOAT3& scale)
	{
		Data& d = D();
		d.LocalPos[slot] = position;
		d.LocalRot[slot] = rotation;
		d.LocalScale[slot] = scale;
		MarkDirty(slot);
	}

	void SetParent(uint32_t slot, int32_t parentSlot)
	{
		Data& d = D();
		d.Parent[slot] = parentSlot;
		d.ParentGen[slot] = parentSlot >= 0 ? d.Gen[(size_t)parentSlot] : 0;
		// 이미 더러운 채로 깨끗한 부모 밑 (또는 루트) 으로 옮겼다 — 새 자리에서는 덮는 조상이 없으니 이 자리부터 순회해야 한다
		//  (깨끗했다면 아래 MarkDirty 가 목록에 넣는다. 부모가 더러우면 부모의 순회가 덮는다)
		if (d.Dirty[slot] && (parentSlot < 0 || !d.Dirty[(size_t)parentSlot]) && !d.InRoots[slot])
		{
			d.Roots.push_back(slot);
			d.InRoots[slot] = 1;
		}
		MarkDirty(slot);
	}

	bool IsDirty(uint32_t slot) { return D().Dirty[slot] != 0; }
	uint32_t Version(uint32_t slot) { return D().Version[slot]; }

	const XMFLOAT4X4& WorldRef(uint32_t slot)
	{
		Data& d = D();
		if (d.Dirty[slot] && OnMain())
			Resolve(d, slot);   // 메인이 아니면 부르지 않는다 (컬링은 깨끗한 자리만 이것으로 읽는다)
		return d.World[slot].M;
	}

	bool OnMainThread() { return OnMain(); }

	XMFLOAT4X4 World(uint32_t slot)
	{
		Data& d = D();
		if (d.Dirty[slot])
		{
			if (OnMain())
				Resolve(d, slot);
			else
			{
				XMFLOAT4X4 m;
				XMStoreFloat4x4(&m, ComputePure(d, slot).M);
				++d.PureComputes;
				return m;
			}
		}
		return d.World[slot].M;
	}

	XMFLOAT3 WorldPosition(uint32_t slot)
	{
		const XMFLOAT4X4 m = World(slot);
		return XMFLOAT3(m._41, m._42, m._43);
	}

	XMFLOAT4 WorldRotation(uint32_t slot)
	{
		Data& d = D();
		if (d.Dirty[slot])
		{
			if (OnMain())
				Resolve(d, slot);
			else
			{
				XMFLOAT4 r;
				XMStoreFloat4(&r, ComputePure(d, slot).R);
				return r;
			}
		}
		return d.WorldRot[slot];
	}

	XMFLOAT3 WorldScale(uint32_t slot)
	{
		Data& d = D();
		if (d.Dirty[slot])
		{
			if (OnMain())
				Resolve(d, slot);
			else
			{
				XMFLOAT3 s;
				XMStoreFloat3(&s, ComputePure(d, slot).S);
				return s;
			}
		}
		return d.WorldScale[slot];
	}

	void Flush()
	{
		Data& d = D();
		if (d.Roots.empty())
			return;
		PROFILE_SCOPE("Transform Flush");
		const auto t0 = std::chrono::steady_clock::now();
		// 1) 조상이 목록에 있는 자리는 뺀다 (그 조상의 순회에 들어 있다) — 남은 것끼리는 하위 계층이 겹치지 않아 나눠 돌려도 된다
		static std::vector<uint32_t> s_Subtrees;
		s_Subtrees.clear();
		for (const uint32_t r : d.Roots)
		{
			if (!d.Owner[r])
				continue;   // 그 사이 놓인 자리
			bool covered = false;
			for (int32_t p = ParentOf(d, r); p >= 0; p = ParentOf(d, (uint32_t)p))
				if (d.InRoots[(size_t)p])
				{
					covered = true;
					break;
				}
			if (!covered)
				s_Subtrees.push_back(r);
		}
		// 2) 하위 계층마다 위에서 아래로 (부모를 먼저) — 잡으로 나눈다. 한 잡은 자기 하위 계층의 자리만 쓴다
		std::atomic<uint32_t> resolved{ 0 };
		Jobs::ParallelFor((int)s_Subtrees.size(), 8, [&](int b, int e) {
			std::vector<uint32_t>& stack = Stack();
			uint32_t n = 0;
			for (int k = b; k < e; ++k)
			{
				stack.clear();
				stack.push_back(s_Subtrees[(size_t)k]);
				while (!stack.empty())
				{
					const uint32_t s = stack.back();
					stack.pop_back();
					if (d.Dirty[s])
					{
						Compute(d, s);
						++n;
					}
					if (Transform* o = d.Owner[s])
						for (const auto& c : o->GetChildren())
							if (c)
								stack.push_back(c->Slot());
				}
			}
			resolved.fetch_add(n, std::memory_order_relaxed);
		}, "Transform Flush");
		for (const uint32_t r : d.Roots)
			d.InRoots[r] = 0;
		d.LastRoots = (uint32_t)d.Roots.size();
		d.LastSubtrees = (uint32_t)s_Subtrees.size();
		d.LastResolved = resolved.load();
		d.Roots.clear();
		++d.Flushes;
		d.LastFlushMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	}

	nlohmann::json Info()
	{
		const Data& d = D();
		return { { "transforms", d.Live }, { "capacity", d.Owner.size() }, { "pendingRoots", d.Roots.size() }, { "flushes", d.Flushes },
			{ "lastFlush", { { "roots", d.LastRoots }, { "subtrees", d.LastSubtrees }, { "resolved", d.LastResolved }, { "ms", d.LastFlushMs } } },
			{ "lazyResolves", d.LazyResolves }, { "pureComputes", d.PureComputes }, { "marks", d.Marks } };
	}
}
