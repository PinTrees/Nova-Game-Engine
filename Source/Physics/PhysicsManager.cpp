// Jolt Physics 백엔드.
// 이 파일은 미리 컴파일된 헤더(pch)를 쓰지 않는다: Windows.h 의 min/max 매크로가 정의되기 전에 Jolt 헤더를 먼저 포함해야 하기 때문.
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/EmptyShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Geometry/AABox.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>
#include <Jolt/Physics/Constraints/SixDOFConstraint.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>

#include <mutex>
#include <thread>
#include <unordered_set>

#include "pch.h"
#include "PhysicsSettings.h"
#include "PhysicsManager.h"
#include "Profiler.h"
#include "ComponentIndex.h"
#include "MonoBehaviour.h"
#include "RigidBody.h"
#include "BoxCollider.h"
#include "SphereCollider.h"
#include "CapsuleCollider.h"
#include "MeshCollider.h"
#include "TerrainCollider.h"
#include "CharacterController.h"
#include "Joint.h"
#include "TerrainData.h"
#include "TreeDesc.h"
#include "Mesh.h"

SINGLE_BODY(PhysicsManager)

namespace
{
	// ------------------------------------------------------------------ 변환 도우미
	inline JPH::Vec3 ToJ(const Vec3& v) { return JPH::Vec3(v.x, v.y, v.z); }
	inline JPH::RVec3 ToJR(const Vec3& v) { return JPH::RVec3(v.x, v.y, v.z); }
	inline JPH::Quat ToJ(const Quaternion& q) { JPH::Quat r(q.x, q.y, q.z, q.w); return r.Normalized(); }
	inline Vec3 FromJ(JPH::Vec3Arg v) { return Vec3(v.GetX(), v.GetY(), v.GetZ()); }
#ifdef JPH_DOUBLE_PRECISION
	inline Vec3 FromJ(JPH::RVec3Arg v) { return Vec3((float)v.GetX(), (float)v.GetY(), (float)v.GetZ()); }
#endif
	inline Quaternion FromJ(JPH::QuatArg q) { return Quaternion(q.GetX(), q.GetY(), q.GetZ(), q.GetW()); }

	// ------------------------------------------------------------------ 레이어
	namespace Layers
	{
		constexpr JPH::ObjectLayer NonMoving = 0;
		constexpr JPH::ObjectLayer Moving = 1;
		constexpr JPH::ObjectLayer Count = 2;
	}

	class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface
	{
	public:
		JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
		JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
		{
			return JPH::BroadPhaseLayer(layer == Layers::NonMoving ? 0 : 1);
		}
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
		const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
		{
			return (JPH::BroadPhaseLayer::Type)layer == 0 ? "NON_MOVING" : "MOVING";
		}
#endif
	};

	class ObjectVsBroadPhaseFilter final : public JPH::ObjectVsBroadPhaseLayerFilter
	{
	public:
		bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer bp) const override
		{
			return layer == Layers::NonMoving ? (JPH::BroadPhaseLayer::Type)bp == 1 : true;
		}
	};

	class ObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter
	{
	public:
		bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override
		{
			return !(a == Layers::NonMoving && b == Layers::NonMoving);
		}
	};

	// 형상 사용자 데이터 = 콜라이더 컴포넌트 ID (0 과 구분하기 위해 상위 비트 표시)
	constexpr JPH::uint64 kColliderTag = 1ull << 40;
	inline JPH::uint64 EncodeCollider(int id) { return kColliderTag | (JPH::uint64)(JPH::uint32)id; }
	inline bool DecodeCollider(JPH::uint64 data, JPH::uint32& id)
	{
		if ((data & kColliderTag) == 0)
			return false;
		id = (JPH::uint32)(data & 0xffffffffull);
		return true;
	}
	inline JPH::uint64 PairKey(JPH::uint32 a, JPH::uint32 b)
	{
		return a < b ? ((JPH::uint64)a << 32) | b : ((JPH::uint64)b << 32) | a;
	}

	bool IsActiveInHierarchy(GameObject* go)
	{
		for (GameObject* g = go; g != nullptr; g = g->GetParent())
			if (!g->IsActive())
				return false;
		return true;
	}

	void HashCombine(size_t& h, size_t v) { h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2); }
	void HashFloat(size_t& h, float f, float quantum = 0.0f)
	{
		if (quantum > 0.0f)
			f = roundf(f / quantum) * quantum;
		HashCombine(h, std::hash<float>()(f));
	}
	void HashVec(size_t& h, const Vec3& v, float quantum = 0.0f) { HashFloat(h, v.x, quantum); HashFloat(h, v.y, quantum); HashFloat(h, v.z, quantum); }
}

// ====================================================================== JoltWorld
struct PhysicsManager::JoltWorld
{
	// 콜라이더 테이블 (시뮬레이션 중 읽기 전용 → 작업 스레드에서 안전하게 조회)
	struct ColliderEntry
	{
		Collider* collider = nullptr;
		GameObject* owner = nullptr;     // 바디를 소유한 GameObject (Rigidbody 가 있는 부모일 수 있음)
		bool trigger = false;
		int layer = 0;                   // 콜라이더가 붙은 GameObject 의 레이어 (Layer Collision Matrix)
		uint32 layerBit = 1;             // 그 비트
		uint32 excludeMask = 0;          // 콜라이더 + Rigidbody 의 Exclude Layers
		uint32 includeMask = 0;          // 콜라이더 + Rigidbody 의 Include Layers
		int priority = 0;                // Layer Override Priority
		uint8_t kind = 0;                // ColliderKind (바디 형상으로 쓰는 종류 — 0 = 아님: Character Controller 등)
		uint32_t seen = 0;               // 마지막으로 본 동기화 번호 (보지 못한 것은 표에서 뺀다)
	};

	// 바디 동기화 (StepSimulation 2): 스텝마다 할당하지 않게 버퍼를 둔다
	struct ScanEntry { GameObject* owner; Collider* collider; RigidBody* rb; uint8_t kind; };
	std::vector<ScanEntry> scan;                                  // 소유자 · 콜라이더 (nullptr = Rigidbody 만 있는 소유자)
	std::vector<CharacterController*> scanCharacters;             // 활성 오브젝트의 첫 Character Controller
	std::vector<std::pair<GameObject*, Joint*>> scanJoints;       // Rigidbody 가 있는 활성 오브젝트의 Joint
	std::vector<Collider*> groupColliders;
	std::vector<uint8_t> groupKinds;
	uint32_t syncStamp = 0;

	struct BodyRecord
	{
		GameObject* owner = nullptr;
		RigidBody* rb = nullptr;
		JPH::BodyID id;
		size_t signature = 0;
		bool dynamic = false;
		bool kinematic = false;
		bool interpolate = false;
		std::vector<Collider*> colliders;
		uint32_t seen = 0;         // 마지막으로 본 동기화 번호

		Vec3 lastPos;              // 마지막으로 Transform 과 동기화한 값 (사용자가 Transform 을 바꿨는지 판정)
		Quaternion lastRot;
		Vec3 prevPos, curPos;      // Interpolate 용
		Quaternion prevRot, curRot;
	};

	struct TouchInfo
	{
		JPH::uint32 a = 0, b = 0;
		bool trigger = false;
		JPH::BodyID bodyA, bodyB;
	};

	class ContactListener final : public JPH::ContactListener
	{
	public:
		JoltWorld* world = nullptr;

		void Handle(const JPH::Body& b1, const JPH::Body& b2, const JPH::ContactManifold& m, JPH::ContactSettings& s)
		{
			JPH::uint32 c1 = 0, c2 = 0;
			if (!DecodeCollider(b1.GetShape()->GetSubShapeUserData(m.mSubShapeID1), c1) ||
				!DecodeCollider(b2.GetShape()->GetSubShapeUserData(m.mSubShapeID2), c2))
				return;
			auto i1 = world->colliders.find(c1);
			auto i2 = world->colliders.find(c2);
			if (i1 == world->colliders.end() || i2 == world->colliders.end())
				return;

			const bool trigger = i1->second.trigger || i2->second.trigger;
			if (trigger)
				s.mIsSensor = true;   // 트리거: 통과시키고 이벤트만 보낸다
			else
			{
				// Unity: 충돌 메시지는 둘 중 하나가 Dynamic Rigidbody 일 때만
				if (b1.GetMotionType() != JPH::EMotionType::Dynamic && b2.GetMotionType() != JPH::EMotionType::Dynamic)
					return;
			}

			std::lock_guard<std::mutex> lock(world->touchMutex);
			TouchInfo& t = world->touching[PairKey(c1, c2)];
			t.a = c1;
			t.b = c2;
			t.trigger = trigger;
			t.bodyA = b1.GetID();
			t.bodyB = b2.GetID();
		}

		// Layer Overrides: 상대 레이어가 Exclude 에 있으면 접촉을 만들지 않는다.
		// 두 콜라이더의 설정이 충돌하면 Layer Override Priority 가 높은 쪽을 따른다 (Unity 와 동일).
		JPH::ValidateResult OnContactValidate(const JPH::Body& b1, const JPH::Body& b2, JPH::RVec3Arg, const JPH::CollideShapeResult& r) override
		{
			// Joint 로 이은 두 바디 (Enable Collision 꺼짐 — Unity 기본)
			if (!world->jointNoCollide.empty() &&
				world->jointNoCollide.count(PairKey(b1.GetID().GetIndexAndSequenceNumber(), b2.GetID().GetIndexAndSequenceNumber())))
				return JPH::ValidateResult::RejectAllContactsForThisBodyPair;
			JPH::uint32 c1 = 0, c2 = 0;
			if (!DecodeCollider(b1.GetShape()->GetSubShapeUserData(r.mSubShapeID1), c1) ||
				!DecodeCollider(b2.GetShape()->GetSubShapeUserData(r.mSubShapeID2), c2))
				return JPH::ValidateResult::AcceptContact;
			auto i1 = world->colliders.find(c1);
			auto i2 = world->colliders.find(c2);
			if (i1 == world->colliders.end() || i2 == world->colliders.end())
				return JPH::ValidateResult::AcceptContact;
			const ColliderEntry& A = i1->second;
			const ColliderEntry& B = i2->second;

			// 각 콜라이더가 상대를 어떻게 보는지: -1 제외, +1 포함, 0 의견 없음
			auto opinion = [](const ColliderEntry& self, const ColliderEntry& other) {
				if (self.excludeMask & other.layerBit) return -1;
				if (self.includeMask & other.layerBit) return 1;
				return 0;
			};
			const int a = opinion(A, B), b = opinion(B, A);
			int decision = 0;
			if (a != 0 && b != 0 && a != b)
				decision = A.priority >= B.priority ? a : b;
			else
				decision = a != 0 ? a : b;
			// 의견이 없으면 Project Settings > Physics 의 Layer Collision Matrix (트리거도 같음 — Unity)
			if (decision == 0 && !PhysicsSettings::LayersCollide(A.layer, B.layer))
				decision = -1;
			return decision < 0 ? JPH::ValidateResult::RejectContact : JPH::ValidateResult::AcceptContact;
		}

		void OnContactAdded(const JPH::Body& b1, const JPH::Body& b2, const JPH::ContactManifold& m, JPH::ContactSettings& s) override { Handle(b1, b2, m, s); }
		void OnContactPersisted(const JPH::Body& b1, const JPH::Body& b2, const JPH::ContactManifold& m, JPH::ContactSettings& s) override { Handle(b1, b2, m, s); }
	};

	// 캐릭터가 부딪힐 상대를 고른다: 트리거는 막지 않고 기록만(OnTriggerXxx), Layer Overrides 의 제외 레이어는 통과
	struct CharRecord;
	class CharListener final : public JPH::CharacterContactListener
	{
	public:
		JoltWorld* world = nullptr;
		CharRecord* rec = nullptr;          // 지금 Move 중인 캐릭터
		bool OnContactValidate(const JPH::CharacterVirtual* ch, const JPH::CharacterContact& c) override;
	};
	CharListener charListener;

	BroadPhaseLayers bpLayers;
	ObjectVsBroadPhaseFilter objVsBp;
	ObjectLayerPairFilter objPair;
	ContactListener listener;

	std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator;
	std::unique_ptr<JPH::JobSystem> jobSystem;
	std::unique_ptr<JPH::PhysicsSystem> physics;

	std::unordered_map<JPH::uint64, BodyRecord> bodies;              // key: 소유 GameObject 의 InstanceID
	std::unordered_map<JPH::uint32, ColliderEntry> colliders;        // key: 콜라이더 컴포넌트 ID
	std::unordered_map<RigidBody*, JPH::uint64> rigidToOwner;

	// Character Controller: 컴포넌트마다 CharacterVirtual 하나 (안쪽 키네마틱 바디 = 다른 물체·레이캐스트가 보는 캡슐)
	struct CharRecord
	{
		CharacterController* cc = nullptr;
		GameObject* owner = nullptr;
		JPH::Ref<JPH::CharacterVirtual> ch;
		size_t signature = 0;
		Vec3 lastPos;                                  // 마지막으로 맞춘 Transform 위치 (사용자가 옮겼는지)
		std::unordered_set<JPH::uint32> triggers;      // 지금 겹친 트리거 콜라이더 ID
		std::unordered_set<JPH::uint32> moveTriggers;  // 이번 Move 에서 만난 트리거
	};
	std::unordered_map<CharacterController*, CharRecord> characters;

	// Joint: 컴포넌트마다 Jolt Constraint 하나 (Rigidbody 바디 ↔ 이은 바디 또는 월드)
	struct JointRecord
	{
		Joint* joint = nullptr;
		JPH::Ref<JPH::TwoBodyConstraint> c;
		JPH::BodyID a, b;              // b 가 무효 = 월드
		size_t signature = 0;
		bool noCollide = false;        // Enable Collision 꺼짐: 두 바디끼리 접촉하지 않음
	};
	std::unordered_map<Joint*, JointRecord> joints;
	std::unordered_set<JPH::uint64> jointNoCollide;   // 바디 쌍 (PairKey) — Joint (Enable Collision 꺼짐) + IgnoreCollision
	std::vector<std::pair<uint64, uint64>> ignoredPairs;   // Physics.IgnoreCollision (바디 주인 오브젝트)

	std::mutex touchMutex;
	std::unordered_map<JPH::uint64, TouchInfo> touching;             // 이번 스텝에 닿아 있는 콜라이더 쌍
	std::unordered_map<JPH::uint64, TouchInfo> prevTouching;         // 지난 스텝

	JPH::BodyInterface& BI() { return physics->GetBodyInterface(); }

	BodyRecord* Find(RigidBody* rb)
	{
		auto it = rigidToOwner.find(rb);
		if (it == rigidToOwner.end())
			return nullptr;
		auto b = bodies.find(it->second);
		return b == bodies.end() || b->second.id.IsInvalid() ? nullptr : &b->second;
	}
};

// ====================================================================== 형상 만들기
namespace
{
	using World = PhysicsManager::JoltWorld;

	// Rigidbody 가 붙은 가장 가까운 조상(자기 자신 포함). 없으면 nullptr
	GameObject* FindRigidOwner(GameObject* go)
	{
		for (GameObject* g = go; g != nullptr; g = g->GetParent())
			if (ComponentIndex::Of(g).Rigid)   // 기억한 분류 (스텝마다 dynamic_cast 하지 않는다)
				return g;
		return nullptr;
	}

	// 콜라이더 하나를 형상 설정으로 만든다. 위치/회전은 월드 기준으로 돌려준다.
	JPH::Ref<JPH::ShapeSettings> MakeLeaf(Collider* col, bool mustBeConvex, Vec3& outPos, Quaternion& outRot)
	{
		Transform* tr = col->GetGameObject()->GetTransform();
		const Matrix world = tr->GetWorldMatrix();
		const Vec3 scale = tr->GetScale();
		const Vec3 absScale(fabsf(scale.x), fabsf(scale.y), fabsf(scale.z));
		outRot = tr->GetRotation();
		outPos = Vec3::Transform(col->GetCenter(), world);

		JPH::Ref<JPH::ShapeSettings> leaf;
		if (auto* box = dynamic_cast<BoxCollider*>(col))
		{
			Vec3 size = box->GetSize();
			Vec3 half(fabsf(size.x) * absScale.x * 0.5f, fabsf(size.y) * absScale.y * 0.5f, fabsf(size.z) * absScale.z * 0.5f);
			half = Vec3::Max(half, Vec3(0.001f, 0.001f, 0.001f));
			float minHalf = (std::min)((std::min)(half.x, half.y), half.z);
			leaf = new JPH::BoxShapeSettings(ToJ(half), (std::min)(JPH::cDefaultConvexRadius, minHalf * 0.5f));
		}
		else if (auto* sphere = dynamic_cast<SphereCollider*>(col))
		{
			float r = sphere->GetRadius() * (std::max)((std::max)(absScale.x, absScale.y), absScale.z);
			leaf = new JPH::SphereShapeSettings((std::max)(r, 0.001f));
		}
		else if (auto* capsule = dynamic_cast<CapsuleCollider*>(col))
		{
			float r, half;
			capsule->GetScaledDimensions(scale, r, half);
			r = (std::max)(r, 0.001f);
			if (half <= 1e-4f)
				leaf = new JPH::SphereShapeSettings(r);
			else
			{
				leaf = new JPH::CapsuleShapeSettings(half, r);
				// Jolt 캡슐은 Y 축 방향 → Direction 에 맞게 회전
				JPH::Quat extra = JPH::Quat::sIdentity();
				if (capsule->GetDirection() == 0) extra = JPH::Quat::sRotation(JPH::Vec3::sAxisZ(), -0.5f * JPH::JPH_PI);
				if (capsule->GetDirection() == 2) extra = JPH::Quat::sRotation(JPH::Vec3::sAxisX(), 0.5f * JPH::JPH_PI);
				outRot = FromJ((ToJ(outRot) * extra).Normalized());
			}
		}
		else if (auto* meshCol = dynamic_cast<MeshCollider*>(col))
		{
			outPos = tr->GetPosition();   // Mesh Collider 는 Center 가 없다
			Mesh* mesh = meshCol->GetMesh();
			if (mesh == nullptr || mesh->Vertices.empty() || mesh->Indices.size() < 3)
				return nullptr;

			// 사용할 인덱스 범위 (서브셋별 VertexStart 보정)
			std::vector<std::pair<size_t, size_t>> ranges;   // (index start, count)
			std::vector<JPH::uint32> bases;
			if (mesh->Subsets.empty())
			{
				ranges.push_back({ 0, mesh->Indices.size() });
				bases.push_back(0);
			}
			else
				for (const auto& s : mesh->Subsets)
				{
					ranges.push_back({ (size_t)s.FaceStart * 3, (size_t)s.FaceCount * 3 });
					bases.push_back(s.VertexStart);
				}

			auto scaled = [&](size_t vi) {
				const auto& p = mesh->Vertices[(std::min)(vi, mesh->Vertices.size() - 1)].pos;
				return JPH::Vec3(p.x * scale.x, p.y * scale.y, p.z * scale.z);
			};

			if (meshCol->IsConvex() || mustBeConvex)
			{
				JPH::Array<JPH::Vec3> points;
				size_t stride = (std::max)((size_t)1, mesh->Vertices.size() / 1500);
				for (size_t i = 0; i < mesh->Vertices.size(); i += stride)
					points.push_back(scaled(i));
				JPH::Ref<JPH::ConvexHullShapeSettings> hull = new JPH::ConvexHullShapeSettings(points);
				if (!hull->Create().HasError())
					leaf = hull;
				else
				{
					// 평면처럼 납작한 메시: 얇은 상자로 대체
					JPH::AABox box;
					for (const auto& p : points) box.Encapsulate(p);
					JPH::Vec3 half = JPH::Vec3::sMax(box.GetExtent(), JPH::Vec3::sReplicate(0.01f));
					JPH::Ref<JPH::BoxShapeSettings> bs = new JPH::BoxShapeSettings(half, 0.005f);
					JPH::Vec3 c = box.GetCenter();
					leaf = new JPH::RotatedTranslatedShapeSettings(c, JPH::Quat::sIdentity(), bs);
					bs->mUserData = EncodeCollider(col->GetInstanceID());
				}
			}
			else
			{
				JPH::VertexList verts;
				verts.reserve(mesh->Vertices.size());
				for (size_t i = 0; i < mesh->Vertices.size(); ++i)
				{
					JPH::Vec3 p = scaled(i);
					verts.push_back(JPH::Float3(p.GetX(), p.GetY(), p.GetZ()));
				}
				JPH::IndexedTriangleList tris;
				for (size_t r = 0; r < ranges.size(); ++r)
					for (size_t n = ranges[r].first; n + 3 <= ranges[r].first + ranges[r].second && n + 3 <= mesh->Indices.size(); n += 3)
						tris.push_back(JPH::IndexedTriangle(mesh->Indices[n] + bases[r], mesh->Indices[n + 1] + bases[r], mesh->Indices[n + 2] + bases[r]));
				// 음수 스케일이면 삼각형 방향이 뒤집힌다
				if (scale.x * scale.y * scale.z < 0.0f)
					for (auto& t : tris) std::swap(t.mIdx[1], t.mIdx[2]);
				leaf = new JPH::MeshShapeSettings(std::move(verts), std::move(tris));
			}
		}

		else if (auto* terrainCol = dynamic_cast<TerrainCollider*>(col))
		{
			// Unity 지형처럼 위치만 쓰고 회전/크기는 무시. 움직이는(Dynamic) 바디에는 쓸 수 없다.
			std::shared_ptr<TerrainData> data = terrainCol->GetEffectiveData();
			if (data == nullptr || mustBeConvex || data->Heights.empty())
				return nullptr;
			outPos = tr->GetPosition();
			outRot = Quaternion::Identity;
			const int res = data->HeightmapResolution;
			const JPH::Vec3 scale(data->CellSizeX(), data->Size.y, data->CellSizeZ());
			JPH::Ref<JPH::HeightFieldShapeSettings> hf = new JPH::HeightFieldShapeSettings(data->Heights.data(), JPH::Vec3::sZero(), scale, (JPH::uint32)res);
			hf->mBlockSize = 4;
			hf->mBitsPerSample = 16;   // 높이 정밀도 (600m 기준 약 1cm)
			leaf = hf;

			// Enable Tree Colliders: 칠한 나무마다 줄기 캡슐 (Unity 는 나무 프리팹의 Capsule Collider).
			// 반지름 = 종류의 밑동 반지름 × 폭 배율, 높이 = 나무 높이 × 높이 배율, 지형 높이에 세운다. 높이맵과 한 복합 형상으로
			if (terrainCol->GetEnableTreeColliders() && !data->TreeInstances.empty() && !data->TreePrototypes.empty())
			{
				JPH::Ref<JPH::StaticCompoundShapeSettings> compound = new JPH::StaticCompoundShapeSettings();
				hf->mUserData = EncodeCollider(col->GetInstanceID());
				compound->AddShape(JPH::Vec3::sZero(), JPH::Quat::sIdentity(), hf);
				int count = 0;
				for (const TerrainTreeInstance& t : data->TreeInstances)
				{
					const int proto = (std::clamp)(t.Prototype, 0, (int)data->TreePrototypes.size() - 1);
					const TreeParams& p = data->TreePrototypes[proto].Params;
					const float r = (std::max)(0.05f, p.Radius * t.WidthScale);
					const float h = (std::max)(2.0f * r + 0.1f, p.Height * t.HeightScale);
					const float lx = t.X * data->Size.x, lz = t.Z * data->Size.z;
					const float y = data->GetHeight(lx, lz);
					JPH::Ref<JPH::CapsuleShapeSettings> capsule = new JPH::CapsuleShapeSettings(0.5f * h - r, r);
					capsule->mUserData = EncodeCollider(col->GetInstanceID());
					compound->AddShape(JPH::Vec3(lx, y + 0.5f * h, lz), JPH::Quat::sIdentity(), capsule);
					if (count++ == 0)
						EditorLog::Write("Physics", "terrain '%s': %zu tree colliders (first at %.2f %.2f %.2f, radius %.2f, height %.2f)",
							col->GetGameObject()->GetName().c_str(), data->TreeInstances.size(), outPos.x + lx, outPos.y + y, outPos.z + lz, r, h);
				}
				leaf = compound;
			}
		}

		if (leaf != nullptr)
			leaf->mUserData = EncodeCollider(col->GetInstanceID());
		return leaf;
	}

	// 바디의 형상 (여러 콜라이더면 StaticCompound). 콜라이더가 없으면 nullptr
	JPH::RefConst<JPH::Shape> BuildShape(const std::vector<Collider*>& cols, const Vec3& bodyPos, const Quaternion& bodyRot, bool dynamic)
	{
		const JPH::Quat invBody = ToJ(bodyRot).Conjugated();
		struct Part { JPH::Ref<JPH::ShapeSettings> s; JPH::Vec3 pos; JPH::Quat rot; };
		std::vector<Part> parts;
		for (Collider* c : cols)
		{
			Vec3 p;
			Quaternion r;
			JPH::Ref<JPH::ShapeSettings> leaf = MakeLeaf(c, dynamic, p, r);
			if (leaf == nullptr)
				continue;
			Part part;
			part.s = leaf;
			part.pos = invBody * (ToJ(p) - ToJ(bodyPos));
			part.rot = (invBody * ToJ(r)).Normalized();
			parts.push_back(part);
		}
		if (parts.empty())
			return nullptr;

		JPH::Ref<JPH::ShapeSettings> final;
		if (parts.size() == 1)
		{
			const Part& p = parts[0];
			const bool identity = p.pos.LengthSq() < 1e-10f && p.rot.IsClose(JPH::Quat::sIdentity(), 1e-8f);
			final = identity ? p.s : JPH::Ref<JPH::ShapeSettings>(new JPH::RotatedTranslatedShapeSettings(p.pos, p.rot, p.s));
		}
		else
		{
			JPH::Ref<JPH::StaticCompoundShapeSettings> compound = new JPH::StaticCompoundShapeSettings();
			for (const Part& p : parts)
				compound->AddShape(p.pos, p.rot, p.s);
			final = compound;
		}

		JPH::ShapeSettings::ShapeResult result = final->Create();
		if (result.HasError())
		{
			OutputDebugStringA(("[Physics] Shape error: " + std::string(result.GetError().c_str()) + "\n").c_str());
			return nullptr;
		}
		return result.Get();
	}

	// 바디 형상으로 쓰는 콜라이더 종류 (0 = 아님). 동기화가 콜라이더마다 한 번만 판정해 표에 기억한다
	enum ColliderKind : uint8_t { KindNone = 0, KindBox, KindSphere, KindCapsule, KindMesh, KindTerrain };
	uint8_t KindOf(Collider* c)
	{
		if (dynamic_cast<BoxCollider*>(c)) return KindBox;
		if (dynamic_cast<SphereCollider*>(c)) return KindSphere;
		if (dynamic_cast<CapsuleCollider*>(c)) return KindCapsule;
		if (dynamic_cast<MeshCollider*>(c)) return KindMesh;
		if (dynamic_cast<TerrainCollider*>(c)) return KindTerrain;
		return KindNone;
	}

	size_t ComputeSignature(GameObject* owner, RigidBody* rb, const std::vector<Collider*>& cols, const uint8_t* kinds = nullptr)
	{
		size_t h = 0;
		HashVec(h, owner->GetTransform()->GetScale(), 1e-4f);
		if (rb != nullptr)
		{
			HashCombine(h, 1);
			HashFloat(h, rb->GetMass());
			HashFloat(h, rb->GetLinearDamping());
			HashFloat(h, rb->GetAngularDamping());
			HashCombine(h, rb->GetUseGravity());
			HashCombine(h, rb->IsKinematic());
			HashCombine(h, (size_t)rb->GetInterpolation());
			HashCombine(h, (size_t)rb->GetCollisionDetection());
			HashCombine(h, rb->GetExcludeLayers());
			HashCombine(h, rb->GetIncludeLayers());
			for (int i = 0; i < 3; ++i)
			{
				HashCombine(h, rb->IsPositionFrozen(i));
				HashCombine(h, rb->IsRotationFrozen(i));
			}
		}
		for (size_t ci = 0; ci < cols.size(); ++ci)
		{
			Collider* c = cols[ci];
			const uint8_t kind = kinds ? kinds[ci] : KindOf(c);
			HashCombine(h, (size_t)c);
			HashCombine(h, c->IsTrigger());
			HashCombine(h, c->GetExcludeLayers());
			HashCombine(h, c->GetIncludeLayers());
			HashCombine(h, (size_t)c->GetLayerOverridePriority());
			HashCombine(h, c->GetGameObject()->GetLayerIndex());
			HashVec(h, c->GetCenter());
			if (kind == KindBox) HashVec(h, static_cast<BoxCollider*>(c)->GetSize());
			if (kind == KindSphere) HashFloat(h, static_cast<SphereCollider*>(c)->GetRadius());
			if (kind == KindCapsule) { auto* k = static_cast<CapsuleCollider*>(c); HashFloat(h, k->GetRadius()); HashFloat(h, k->GetHeight()); HashCombine(h, k->GetDirection()); }
			if (kind == KindMesh) { auto* m = static_cast<MeshCollider*>(c); HashCombine(h, (size_t)m->GetMesh()); HashCombine(h, m->IsConvex()); }
			if (kind == KindTerrain)
				if (auto data = static_cast<TerrainCollider*>(c)->GetEffectiveData())
				{
					// 지형 높이·나무를 고치거나 나무 충돌을 켜고 끄면 형상을 다시 만든다
					HashCombine(h, (size_t)data.get());
					HashCombine(h, data->Revision);
					HashCombine(h, data->TreeRevision);
					HashCombine(h, static_cast<TerrainCollider*>(c)->GetEnableTreeColliders());
				}
			// 자식 콜라이더: 소유자까지의 로컬 변환 체인 (정확한 값이라 흔들리지 않음)
			for (GameObject* g = c->GetGameObject(); g != nullptr && g != owner; g = g->GetParent())
			{
				Transform* t = g->GetTransform();
				HashVec(h, t->GetLocalPosition());
				HashVec(h, t->GetLocalEulerAngles());
				HashVec(h, t->GetLocalScale());
			}
		}
		return h;
	}

	// (개발/검증용) NOVA_PHYSICS_LOG=<파일 경로> 이면 이벤트와 바디 위치를 기록한다
	FILE* PhysLog()
	{
		static FILE* fp = nullptr;
		static bool checked = false;
		if (!checked)
		{
			checked = true;
			char path[512] = {};
			if (::GetEnvironmentVariableA("NOVA_PHYSICS_LOG", path, sizeof(path)) > 0)
				fopen_s(&fp, path, "w");
		}
		return fp;
	}

	bool NearlyEqual(const Vec3& a, const Vec3& b) { return (a - b).LengthSquared() < 1e-10f; }
	bool NearlyEqual(const Quaternion& a, const Quaternion& b) { return fabsf(a.Dot(b)) > 1.0f - 1e-7f; }

	void DispatchToObject(GameObject* go, Collider* other, bool trigger, int kind)
	{
		if (go == nullptr)
			return;
		for (const auto& component : go->GetComponents())
		{
			MonoBehaviour* script = dynamic_cast<MonoBehaviour*>(component.get());
			if (script == nullptr || !script->IsEnabled())
				continue;
			if (trigger)
			{
				if (kind == 0) script->OnTriggerEnter(other);
				else if (kind == 1) script->OnTriggerStay(other);
				else script->OnTriggerExit(other);
			}
			else
			{
				if (kind == 0) script->OnCollisionEnter(other);
				else if (kind == 1) script->OnCollisionStay(other);
				else script->OnCollisionExit(other);
			}
		}
	}

	// ------------------------------------------------------------------ Character Controller
	World::ColliderEntry CharacterEntry(CharacterController* cc)
	{
		World::ColliderEntry e;
		e.collider = cc;
		e.owner = cc->GetGameObject();
		e.trigger = false;
		e.layer = cc->GetGameObject()->GetLayerIndex() & 31;
		e.layerBit = 1u << e.layer;
		e.excludeMask = cc->GetExcludeLayers();
		e.includeMask = cc->GetIncludeLayers();
		e.priority = cc->GetLayerOverridePriority();
		return e;
	}

	Vec3 CharacterWorldCenter(CharacterController* cc)
	{
		return Vec3::Transform(cc->GetCenter(), cc->GetGameObject()->GetTransform()->GetWorldMatrix());
	}

	// 캐릭터를 만들거나(형상이 바뀌면 다시) 사용자가 Transform 을 옮겼으면 따라간다
	World::CharRecord* EnsureCharacter(World& w, CharacterController* cc)
	{
		GameObject* go = cc->GetGameObject();
		Transform* tr = go->GetTransform();
		float r, half;
		cc->GetScaledDimensions(tr->GetScale(), r, half);
		r = (std::max)(r, 0.01f);
		size_t sig = 0;
		HashFloat(sig, r, 1e-4f);
		HashFloat(sig, half, 1e-4f);
		HashCombine(sig, cc->GetDetectCollisions());
		HashFloat(sig, cc->GetSkinWidth());

		w.colliders[(JPH::uint32)cc->GetInstanceID()] = CharacterEntry(cc);
		auto it = w.characters.find(cc);
		const Vec3 center = CharacterWorldCenter(cc);
		if (it != w.characters.end() && it->second.signature == sig)
		{
			World::CharRecord& rec = it->second;
			if (!NearlyEqual(tr->GetPosition(), rec.lastPos))
			{
				rec.ch->SetPosition(ToJR(center));
				rec.lastPos = tr->GetPosition();
			}
			return &rec;
		}
		if (it != w.characters.end())
			w.characters.erase(it);

		JPH::Ref<JPH::Shape> shape;
		if (half > 1e-4f)
			shape = new JPH::CapsuleShape(half, r);
		else
			shape = new JPH::SphereShape(r);
		shape->SetUserData(EncodeCollider(cc->GetInstanceID()));

		JPH::Ref<JPH::CharacterVirtualSettings> s = new JPH::CharacterVirtualSettings();
		s->mShape = shape;
		s->mUp = JPH::Vec3::sAxisY();
		s->mMaxSlopeAngle = JPH::DegreesToRadians(std::clamp(cc->GetSlopeLimit(), 0.0f, 89.9f));
		// 위치 = 캡슐 중심. 아래 반구(중심에서 half 아래)의 접촉만 발을 받친다
		s->mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), half);
		s->mPredictiveContactDistance = (std::max)(0.1f, cc->GetSkinWidth());
		s->mCharacterPadding = 0.02f;
		s->mMass = 70.0f;
		s->mMaxStrength = 100.0f;   // 다이내믹 바디를 미는 힘 (N)
		if (cc->GetDetectCollisions())
		{
			s->mInnerBodyShape = shape;   // 다른 물체·레이캐스트가 보는 키네마틱 캡슐
			s->mInnerBodyLayer = Layers::Moving;
		}

		World::CharRecord rec;
		rec.cc = cc;
		rec.owner = go;
		rec.signature = sig;
		rec.ch = new JPH::CharacterVirtual(s, ToJR(center), JPH::Quat::sIdentity(), (JPH::uint64)go->GetInstanceID(), w.physics.get());
		rec.ch->SetListener(&w.charListener);
		rec.lastPos = tr->GetPosition();
		return &(w.characters[cc] = std::move(rec));
	}

	// 접촉을 만들지 않을 바디 쌍: Joint (Enable Collision 꺼짐) + Physics.IgnoreCollision. 바디 ID 가 바뀌면 (다시 만들기) 다시 계산
	void RebuildNoCollide(World& w)
	{
		w.jointNoCollide.clear();
		for (auto& kv : w.joints)
			if (kv.second.noCollide)
				w.jointNoCollide.insert(PairKey(kv.second.a.GetIndexAndSequenceNumber(), kv.second.b.GetIndexAndSequenceNumber()));
		for (const auto& [x, y] : w.ignoredPairs)
		{
			auto a = w.bodies.find(x), b = w.bodies.find(y);
			if (a != w.bodies.end() && b != w.bodies.end() && !a->second.id.IsInvalid() && !b->second.id.IsInvalid())
				w.jointNoCollide.insert(PairKey(a->second.id.GetIndexAndSequenceNumber(), b->second.id.GetIndexAndSequenceNumber()));
		}
	}

	// 다이내믹 바디를 계층 깊이 순서로 (부모 먼저)
	std::vector<World::BodyRecord*> DynamicByDepth(World& w)
	{
		std::vector<std::pair<int, World::BodyRecord*>> list;
		bool nested = false;
		for (auto& kv : w.bodies)
		{
			World::BodyRecord& r = kv.second;
			if (!r.dynamic || r.id.IsInvalid())
				continue;
			int depth = 0;
			for (GameObject* p = r.owner->GetParent(); p; p = p->GetParent())
				++depth;
			nested |= depth > 0;
			list.push_back({ depth, &r });
		}
		if (nested)
			std::stable_sort(list.begin(), list.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
		std::vector<World::BodyRecord*> out;
		out.reserve(list.size());
		for (auto& e : list)
			out.push_back(e.second);
		return out;
	}

	// ------------------------------------------------------------------ Joint
	// 바디를 지우기 전에 그 바디를 쓰는 구속을 지운다 (Jolt 구속은 바디 포인터를 들고 있다). 다음 동기화에서 새 바디로 다시 만든다
	void DropJointsOf(World& w, const JPH::BodyID& id)
	{
		for (auto it = w.joints.begin(); it != w.joints.end();)
		{
			if (it->second.a == id || it->second.b == id)
			{
				w.physics->RemoveConstraint(it->second.c);
				it = w.joints.erase(it);
			}
			else
				++it;
		}
	}

	JPH::BodyID BodyOfObject(World& w, GameObject* go)
	{
		if (go == nullptr)
			return JPH::BodyID();
		GameObject* owner = FindRigidOwner(go);
		auto it = w.bodies.find((owner ? owner : go)->GetInstanceID());
		return it == w.bodies.end() ? JPH::BodyID() : it->second.id;
	}

	// 조인트 틀 (월드): 점 · X · Y (서로 수직, 단위)
	struct JointFrame { Vec3 P, X, Y; };

	JointFrame Orthonormal(const Vec3& p, Vec3 x, Vec3 y)
	{
		if (x.LengthSquared() < 1e-10f) x = Vec3(1, 0, 0);
		x.Normalize();
		y -= x * x.Dot(y);
		if (y.LengthSquared() < 1e-10f)
		{
			const JPH::Vec3 perp = JPH::Vec3(x.x, x.y, x.z).GetNormalizedPerpendicular();
			y = Vec3(perp.GetX(), perp.GetY(), perp.GetZ());
		}
		y.Normalize();
		return { p, x, y };
	}

	// 이 바디 쪽 = 지금 앵커 · 축 (로컬 값), 이은 쪽 = 처음 만들 때 기억한 틀 (이은 바디 기준 — 다시 만들어도 쉬는 자세가 같다)
	void JointFrames(Joint* jt, GameObject* go, GameObject* other, const Vec3& secondaryLocal, JointFrame& own, JointFrame& connected)
	{
		const Matrix world = go->GetTransform()->GetWorldMatrix();
		own = Orthonormal(Vec3::Transform(jt->GetAnchor(), world), Vec3::TransformNormal(jt->GetAxis(), world), Vec3::TransformNormal(secondaryLocal, world));
		const Matrix otherWorld = other ? other->GetTransform()->GetWorldMatrix() : Matrix::Identity;
		if (!jt->Rest.Valid)
		{
			const Matrix inv = otherWorld.Invert();
			jt->Rest.Point = jt->GetAutoConfigureConnectedAnchor() ? Vec3::Transform(own.P, inv) : jt->GetConnectedAnchor();
			jt->Rest.X = Vec3::TransformNormal(own.X, inv);
			jt->Rest.Y = Vec3::TransformNormal(own.Y, inv);
			jt->Rest.Valid = true;
		}
		connected = Orthonormal(Vec3::Transform(jt->Rest.Point, otherWorld), Vec3::TransformNormal(jt->Rest.X, otherWorld), Vec3::TransformNormal(jt->Rest.Y, otherWorld));
	}

	float LimitRad(float degrees) { return JPH::DegreesToRadians(std::clamp(degrees, -177.0f, 177.0f)); }

	JPH::MotorSettings DriveMotor(const JointDriveData& d)
	{
		JPH::MotorSettings m(JPH::ESpringMode::StiffnessAndDamping, (std::max)(0.0f, d.PositionSpring), (std::max)(0.0f, d.PositionDamper));
		const float f = std::isinf(d.MaximumForce) ? FLT_MAX : (std::max)(0.0f, d.MaximumForce);
		m.SetForceLimit(f);
		m.SetTorqueLimit(f);
		return m;
	}

	// Configurable Joint 드라이브 목표: Unity 처럼 Target Position · Rotation 은 이은 바디 쪽의 목표라 이 바디는 반대로 간다
	void ApplyConfigurableTargets(ConfigurableJoint* cj, JPH::SixDOFConstraint* c)
	{
		using A = JPH::SixDOFConstraintSettings::EAxis;
		const JointDriveData* lin[3] = { &cj->XDrive, &cj->YDrive, &cj->ZDrive };
		for (int i = 0; i < 3; ++i)
		{
			const JointDriveData& d = *lin[i];
			c->SetMotorState((A)i, d.PositionSpring > 0.0f ? JPH::EMotorState::Position : (d.PositionDamper > 0.0f ? JPH::EMotorState::Velocity : JPH::EMotorState::Off));
		}
		const JointDriveData* ang[3] = { cj->RotationDriveMode == 1 ? &cj->SlerpDrive : &cj->AngularXDrive,
			cj->RotationDriveMode == 1 ? &cj->SlerpDrive : &cj->AngularYZDrive, cj->RotationDriveMode == 1 ? &cj->SlerpDrive : &cj->AngularYZDrive };
		for (int i = 0; i < 3; ++i)
		{
			const JointDriveData& d = *ang[i];
			c->SetMotorState((A)(3 + i), d.PositionSpring > 0.0f ? JPH::EMotorState::Position : (d.PositionDamper > 0.0f ? JPH::EMotorState::Velocity : JPH::EMotorState::Off));
		}
		c->SetTargetPositionCS(JPH::Vec3(-cj->TargetPosition.x, -cj->TargetPosition.y, -cj->TargetPosition.z));
		c->SetTargetVelocityCS(JPH::Vec3(-cj->TargetVelocity.x, -cj->TargetVelocity.y, -cj->TargetVelocity.z));
		Quaternion q = cj->TargetRotation;
		if (q.LengthSquared() < 1e-8f) q = Quaternion::Identity;
		q.Normalize();
		c->SetTargetOrientationCS(JPH::Quat(-q.x, -q.y, -q.z, q.w));   // 역회전
		c->SetTargetAngularVelocityCS(JPH::Vec3(JPH::DegreesToRadians(-cj->TargetAngularVelocity.x), JPH::DegreesToRadians(-cj->TargetAngularVelocity.y),
			JPH::DegreesToRadians(-cj->TargetAngularVelocity.z)));
	}

	JPH::Ref<JPH::TwoBodyConstraint> CreateJointConstraint(World& w, Joint* jt, GameObject* go, GameObject* other, const JPH::BodyID& a, const JPH::BodyID& b)
	{
		const Matrix world = go->GetTransform()->GetWorldMatrix();
		const Vec3 anchor = Vec3::Transform(jt->GetAnchor(), world);
		Vec3 connected = anchor;   // Auto Configure: 지금 앵커 자리
		if (!jt->GetAutoConfigureConnectedAnchor())
			connected = other ? Vec3::Transform(jt->GetConnectedAnchor(), other->GetTransform()->GetWorldMatrix()) : jt->GetConnectedAnchor();

		JPH::Ref<JPH::TwoBodyConstraintSettings> settings;
		HingeJoint* hinge = nullptr;
		switch (jt->JointKind())
		{
		case 0:
		{
			JPH::FixedConstraintSettings* f = new JPH::FixedConstraintSettings();
			f->mAutoDetectPoint = true;   // 지금 상대 위치·방향을 그대로 고정
			settings = f;
			break;
		}
		case 1:
		{
			hinge = static_cast<HingeJoint*>(jt);
			JPH::HingeConstraintSettings* h = new JPH::HingeConstraintSettings();
			Vec3 ax = Vec3::TransformNormal(jt->GetAxis(), world);
			if (ax.LengthSquared() < 1e-10f) ax = Vec3(1, 0, 0);
			ax.Normalize();
			const JPH::Vec3 jax = ToJ(ax);
			const JPH::Vec3 normal = jax.GetNormalizedPerpendicular();
			// body1 = 이은 쪽, body2 = 이 조인트의 바디 → Jolt 의 각도 = 조인트 바디가 상대에 대해 돈 각 (Unity 의 angle·motor·limits 방향)
			h->mPoint1 = ToJR(connected);
			h->mPoint2 = ToJR(anchor);
			h->mHingeAxis1 = h->mHingeAxis2 = jax;
			h->mNormalAxis1 = h->mNormalAxis2 = normal;
			if (hinge->UseLimits)
			{
				h->mLimitsMin = std::clamp(JPH::DegreesToRadians(hinge->Limits.Min), -JPH::JPH_PI, 0.0f);
				h->mLimitsMax = std::clamp(JPH::DegreesToRadians(hinge->Limits.Max), 0.0f, JPH::JPH_PI);
			}
			if (hinge->UseMotor)
			{
				const float force = (std::max)(0.0f, hinge->Motor.Force);
				if (hinge->Motor.FreeSpin)   // 목표 방향으로만 민다 (멈추게 하지 않음)
					h->mMotorSettings.SetTorqueLimits(hinge->Motor.TargetVelocity >= 0.0f ? 0.0f : -force, hinge->Motor.TargetVelocity >= 0.0f ? force : 0.0f);
				else
					h->mMotorSettings.SetTorqueLimit(force);
			}
			else if (hinge->UseSpring)
				h->mMotorSettings = JPH::MotorSettings(JPH::ESpringMode::StiffnessAndDamping, (std::max)(0.0f, hinge->Spring.Spring), (std::max)(0.0f, hinge->Spring.Damper));
			settings = h;
			break;
		}
		case 3:
		{
			// Character Joint → SwingTwist: 비틀기 = Axis (Low ~ High), 흔들기 1 = Swing Axis 둘레, 흔들기 2 = Axis × Swing Axis 둘레
			CharacterJoint* cj = static_cast<CharacterJoint*>(jt);
			JointFrame own, con;
			JointFrames(jt, go, other, cj->SwingAxis, own, con);
			JPH::SwingTwistConstraintSettings* s = new JPH::SwingTwistConstraintSettings();
			s->mPosition1 = ToJR(con.P); s->mTwistAxis1 = ToJ(con.X); s->mPlaneAxis1 = ToJ(con.Y);
			s->mPosition2 = ToJR(own.P); s->mTwistAxis2 = ToJ(own.X); s->mPlaneAxis2 = ToJ(own.Y);
			s->mSwingType = JPH::ESwingType::Cone;
			// Jolt: Plane Half Cone = 비틀기 축이 Plane 축 쪽으로 기우는 각 (= 법선 축 둘레 회전), Normal Half Cone = 법선 쪽 (= Plane 축 둘레)
			s->mNormalHalfConeAngle = LimitRad((std::max)(0.0f, cj->Swing1Limit.Limit));   // Swing Axis 둘레
			s->mPlaneHalfConeAngle = LimitRad((std::max)(0.0f, cj->Swing2Limit.Limit));    // Axis × Swing Axis 둘레
			float lo = cj->LowTwistLimit.Limit, hi = cj->HighTwistLimit.Limit;
			if (lo > hi) std::swap(lo, hi);
			s->mTwistMinAngle = LimitRad(lo);
			s->mTwistMaxAngle = LimitRad(hi);
			settings = s;
			break;
		}
		case 4:
		{
			// Configurable Joint → SixDOF: 축마다 Locked (고정) / Limited (한계 · 스프링) / Free, 드라이브 = 모터
			ConfigurableJoint* cj = static_cast<ConfigurableJoint*>(jt);
			JointFrame own, con;
			JointFrames(jt, go, other, cj->SecondaryAxis, own, con);
			using A = JPH::SixDOFConstraintSettings::EAxis;
			JPH::SixDOFConstraintSettings* s = new JPH::SixDOFConstraintSettings();
			s->mPosition1 = ToJR(con.P); s->mAxisX1 = ToJ(con.X); s->mAxisY1 = ToJ(con.Y);
			s->mPosition2 = ToJR(own.P); s->mAxisX2 = ToJ(own.X); s->mAxisY2 = ToJ(own.Y);
			s->mSwingType = JPH::ESwingType::Pyramid;
			const int lin[3] = { cj->XMotion, cj->YMotion, cj->ZMotion };
			const float limit = (std::max)(0.0f, cj->LinearLimit.Limit);
			for (int i = 0; i < 3; ++i)
			{
				if (lin[i] == ConfigurableJoint::Locked) s->MakeFixedAxis((A)i);
				else if (lin[i] == ConfigurableJoint::Limited) s->SetLimitedAxis((A)i, -limit, limit);
				else s->MakeFreeAxis((A)i);
				if (cj->LinearLimitSpring.Spring > 0.0f)
					s->mLimitsSpringSettings[i] = JPH::SpringSettings(JPH::ESpringMode::StiffnessAndDamping, cj->LinearLimitSpring.Spring, cj->LinearLimitSpring.Damper);
			}
			auto rot = [&](int axis, int motion, float lo, float hi)
			{
				if (motion == ConfigurableJoint::Locked) s->MakeFixedAxis((A)axis);
				else if (motion == ConfigurableJoint::Limited) s->SetLimitedAxis((A)axis, LimitRad((std::min)(lo, hi)), LimitRad((std::max)(lo, hi)));
				else s->MakeFreeAxis((A)axis);
			};
			rot(A::RotationX, cj->AngularXMotion, cj->LowAngularXLimit.Limit, cj->HighAngularXLimit.Limit);
			rot(A::RotationY, cj->AngularYMotion, -std::fabs(cj->AngularYLimit.Limit), std::fabs(cj->AngularYLimit.Limit));
			rot(A::RotationZ, cj->AngularZMotion, -std::fabs(cj->AngularZLimit.Limit), std::fabs(cj->AngularZLimit.Limit));
			s->mMotorSettings[A::TranslationX] = DriveMotor(cj->XDrive);
			s->mMotorSettings[A::TranslationY] = DriveMotor(cj->YDrive);
			s->mMotorSettings[A::TranslationZ] = DriveMotor(cj->ZDrive);
			const bool slerp = cj->RotationDriveMode == 1;
			s->mMotorSettings[A::RotationX] = DriveMotor(slerp ? cj->SlerpDrive : cj->AngularXDrive);
			s->mMotorSettings[A::RotationY] = DriveMotor(slerp ? cj->SlerpDrive : cj->AngularYZDrive);
			s->mMotorSettings[A::RotationZ] = DriveMotor(slerp ? cj->SlerpDrive : cj->AngularYZDrive);
			settings = s;
			break;
		}
		default:
		{
			SpringJoint* sj = static_cast<SpringJoint*>(jt);
			JPH::DistanceConstraintSettings* d = new JPH::DistanceConstraintSettings();
			d->mPoint1 = ToJR(connected);
			d->mPoint2 = ToJR(anchor);
			// Unity: 거리가 [Min, Max] 안이면 힘이 없고, 벗어나면 Spring·Damper 로 당긴다
			d->mMinDistance = (std::max)(0.0f, sj->MinDistance);
			d->mMaxDistance = (std::max)(d->mMinDistance + 1e-3f, sj->MaxDistance);
			d->mLimitsSpringSettings = JPH::SpringSettings(JPH::ESpringMode::StiffnessAndDamping, (std::max)(0.0f, sj->SpringValue), (std::max)(0.0f, sj->Damper));
			settings = d;
			break;
		}
		}
		JPH::TwoBodyConstraint* c = w.BI().CreateConstraint(settings, b, a);   // b 가 무효면 월드
		if (c == nullptr)
			return nullptr;
		if (hinge)
		{
			JPH::HingeConstraint* hc = static_cast<JPH::HingeConstraint*>(c);
			if (hinge->UseMotor)
			{
				hc->SetMotorState(JPH::EMotorState::Velocity);
				hc->SetTargetAngularVelocity(JPH::DegreesToRadians(hinge->Motor.TargetVelocity));
			}
			else if (hinge->UseSpring)
			{
				hc->SetMotorState(JPH::EMotorState::Position);
				hc->SetTargetAngle(JPH::DegreesToRadians(hinge->Spring.TargetPosition));
			}
		}
		if (jt->JointKind() == 4)
			ApplyConfigurableTargets(static_cast<ConfigurableJoint*>(jt), static_cast<JPH::SixDOFConstraint*>(c));
		return c;
	}

	// found = Rigidbody 가 있는 활성 오브젝트의 Joint (StepSimulation 의 동기화가 모은다)
	void SyncJoints(World& w, const std::vector<std::pair<GameObject*, Joint*>>& found)
	{
		Scene* scene = SceneManager::GetI()->GetCurrentScene();
		std::unordered_set<Joint*> alive;
		for (const auto& [go, jt] : found)
		{
			{
				if (jt->IsBroken())
					continue;
				auto own = w.bodies.find(go->GetInstanceID());
				if (own == w.bodies.end() || own->second.id.IsInvalid())
					continue;
				const JPH::BodyID a = own->second.id;
				GameObject* other = jt->GetConnectedBody() != 0 && scene ? scene->FindByFileID(jt->GetConnectedBody()) : nullptr;
				const JPH::BodyID b = other ? BodyOfObject(w, other) : JPH::BodyID();
				if ((other && b.IsInvalid()) || a == b)
					continue;
				size_t sig = jt->ParamsHash();
				HashCombine(sig, a.GetIndexAndSequenceNumber());
				HashCombine(sig, b.IsInvalid() ? 0u : b.GetIndexAndSequenceNumber());
				alive.insert(jt);
				auto it = w.joints.find(jt);
				if (it != w.joints.end() && it->second.signature == sig)
				{
					if (jt->JointKind() == 4)   // 목표 위치 · 회전 · 속도는 Play 중 바뀐다 (구속은 그대로)
					{
						ApplyConfigurableTargets(static_cast<ConfigurableJoint*>(jt), static_cast<JPH::SixDOFConstraint*>(it->second.c.GetPtr()));
						w.BI().ActivateConstraint(it->second.c);
					}
					continue;
				}
				if (it != w.joints.end())
				{
					w.physics->RemoveConstraint(it->second.c);
					w.joints.erase(it);
				}
				JPH::Ref<JPH::TwoBodyConstraint> c = CreateJointConstraint(w, jt, go, other, a, b);
				if (c == nullptr)
					continue;
				w.physics->AddConstraint(c);
				w.BI().ActivateConstraint(c);
				World::JointRecord rec;
				rec.joint = jt;
				rec.c = c;
				rec.a = a;
				rec.b = b;
				rec.signature = sig;
				rec.noCollide = !jt->GetEnableCollision() && !b.IsInvalid();
				w.joints[jt] = rec;
			}
		}
		for (auto it = w.joints.begin(); it != w.joints.end();)
		{
			if (alive.count(it->first))
				++it;
			else
			{
				w.physics->RemoveConstraint(it->second.c);
				it = w.joints.erase(it);
			}
		}
		RebuildNoCollide(w);
	}

	Collider* ColliderOfBody(World& w, const JPH::BodyID& id, const JPH::SubShapeID& sub, JPH::uint32* outId = nullptr)
	{
		if (id.IsInvalid())
			return nullptr;
		JPH::BodyLockRead lock(w.physics->GetBodyLockInterfaceNoLock(), id);
		if (!lock.Succeeded())
			return nullptr;
		JPH::uint32 cid = 0;
		if (!DecodeCollider(lock.GetBody().GetShape()->GetSubShapeUserData(sub), cid))
			return nullptr;
		auto it = w.colliders.find(cid);
		if (it == w.colliders.end())
			return nullptr;
		if (outId)
			*outId = cid;
		return it->second.collider;
	}
}

bool PhysicsManager::JoltWorld::CharListener::OnContactValidate(const JPH::CharacterVirtual* ch, const JPH::CharacterContact& c)
{
	if (rec == nullptr || c.mBodyB.IsInvalid())
		return true;
	JPH::uint32 cid = 0;
	if (ColliderOfBody(*world, c.mBodyB, c.mSubShapeIDB, &cid) == nullptr)
		return true;
	const ColliderEntry& other = world->colliders[cid];
	if (other.trigger)
	{
		rec->moveTriggers.insert(cid);   // 트리거: 통과하고 이벤트만
		return false;
	}
	auto self = world->colliders.find((JPH::uint32)rec->cc->GetInstanceID());
	if (self != world->colliders.end())
	{
		// Layer Overrides: 어느 한쪽이 상대 레이어를 제외하면 부딪히지 않는다
		if ((self->second.excludeMask & other.layerBit) || (other.excludeMask & self->second.layerBit))
			return false;
		// Include 가 없으면 Layer Collision Matrix
		const bool included = (self->second.includeMask & other.layerBit) || (other.includeMask & self->second.layerBit);
		if (!included && !PhysicsSettings::LayersCollide(self->second.layer, other.layer))
			return false;
	}
	return true;
}

// ====================================================================== PhysicsManager
PhysicsManager::PhysicsManager()
{
}

PhysicsManager::~PhysicsManager()
{
	Exit();
}

void PhysicsManager::Init()
{
	if (m_JoltInitialized)
		return;
	JPH::RegisterDefaultAllocator();
#ifdef JPH_ENABLE_ASSERTS
	// Jolt 단언: 중단점(에디터 종료) 대신 Editor.log 에 남기고 계속한다
	JPH::AssertFailed = [](const char* expr, const char* msg, const char* file, JPH::uint line) {
		EditorLog::Write("Physics", "Jolt assert: %s %s (%s:%u)", expr, msg ? msg : "", file, line);
		return false;
	};
#endif
	JPH::Factory::sInstance = new JPH::Factory();
	JPH::RegisterTypes();
	m_JoltInitialized = true;
}

void PhysicsManager::SetGravity(const Vec3& gravity)
{
	m_Gravity = gravity;
	if (m_World)
		m_World->physics->SetGravity(ToJ(gravity));
}

void PhysicsManager::Start()
{
	Init();
	Exit();

	// Project Settings > Physics: 중력 · 레이어 매트릭스 (실행 중 값 = 설정 값으로 시작)
	m_Gravity = PhysicsSettings::Gravity();
	PhysicsSettings::ResetRuntime();
	m_World = std::make_unique<JoltWorld>();
	JoltWorld& w = *m_World;
	w.listener.world = &w;
	w.tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(16 * 1024 * 1024);
#if defined(__EMSCRIPTEN__)
	// 웹: 스레드 없이 (C# 런타임과 같은 단일 스레드 wasm) — 작업을 부른 스레드에서 차례로
	w.jobSystem = std::make_unique<JPH::JobSystemSingleThreaded>(JPH::cMaxPhysicsJobs);
#else
	int threads = (std::max)(1, (int)std::thread::hardware_concurrency() - 1);
	w.jobSystem = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, threads);
#endif
	w.physics = std::make_unique<JPH::PhysicsSystem>();
	w.physics->Init(8192, 0, 8192, 8192, w.bpLayers, w.objVsBp, w.objPair);
	w.physics->SetContactListener(&w.listener);
	w.physics->SetGravity(ToJ(m_Gravity));
	{
		// 정지 접촉 시 허용하는 겹침 (Jolt 기본 2cm → 5mm). Unity 처럼 물체가 바닥에 눈에 띄게 파묻히지 않게 한다
		JPH::PhysicsSettings settings = w.physics->GetPhysicsSettings();
		settings.mPenetrationSlop = 0.005f;
		w.physics->SetPhysicsSettings(settings);
	}
	// Unity(PhysX) 기본값과 같이 마찰/반발 계수는 두 값의 평균으로 합친다 (Jolt 기본: 마찰 = 기하 평균, 반발 = 최댓값)
	w.physics->SetCombineFriction([](const JPH::Body& a, const JPH::SubShapeID&, const JPH::Body& b, const JPH::SubShapeID&) { return 0.5f * (a.GetFriction() + b.GetFriction()); });
	w.physics->SetCombineRestitution([](const JPH::Body& a, const JPH::SubShapeID&, const JPH::Body& b, const JPH::SubShapeID&) { return 0.5f * (a.GetRestitution() + b.GetRestitution()); });
	m_Accumulator = 0.0f;
	m_StepCount = 0;
	m_HeartbeatTime = 0.0f;
	m_HeartbeatSteps = 0;

	StepSimulation(0.0f);   // dt 0: 바디만 만든다
	EditorLog::Write("Physics", "start: fixed %.4f s, gravity %.2f, %zu bodies", m_FixedTimestep, m_Gravity.y, w.bodies.size());
}

void PhysicsManager::Exit()
{
	if (!m_World)
		return;
	EditorLog::Write("Physics", "exit (%zu bodies, playing %d)", m_World->bodies.size(), Application::IsPlaying() ? 1 : 0);
	JoltWorld& w = *m_World;
	JPH::BodyInterface& bi = w.BI();
	w.characters.clear();   // CharacterVirtual 이 안쪽 바디를 지운다 (물리 시스템이 살아 있을 때)
	for (auto& kv : w.joints)
		w.physics->RemoveConstraint(kv.second.c);   // 구속은 바디보다 먼저
	w.joints.clear();
	w.jointNoCollide.clear();
	w.ignoredPairs.clear();
	for (auto& kv : w.bodies)
	{
		if (kv.second.rb)
			kv.second.rb->_SetBodyId(0xffffffff);
		if (!kv.second.id.IsInvalid())
		{
			bi.RemoveBody(kv.second.id);
			bi.DestroyBody(kv.second.id);
		}
	}
	w.bodies.clear();
	m_World.reset();
	PhysicsSettings::ResetRuntime();   // Play 중 Physics.IgnoreLayerCollision 은 설정에 남지 않는다
}

// 매 프레임: 고정 간격으로 나누어 시뮬레이션 (Unity 의 Fixed Timestep)
void PhysicsManager::Update(float deltaTime)
{
	if (!m_World)
		return;

	m_Accumulator += (std::min)(deltaTime, m_MaxAllowedTimestep);
	int steps = 0;
	while (m_Accumulator >= m_FixedTimestep && steps < 8)
	{
		StepSimulation(m_FixedTimestep);
		m_Accumulator -= m_FixedTimestep;
		++steps;
		if (!m_World)
			return;
	}
	// 처음 3 초: 1 초마다 스텝 수 · 바디 수 (물리가 멈춘 세션을 로그로 가린다)
	if (m_HeartbeatTime < 3.0f)
	{
		m_HeartbeatSteps += steps;
		const float before = m_HeartbeatTime;
		m_HeartbeatTime += deltaTime;
		if ((int)m_HeartbeatTime != (int)before || !(m_HeartbeatTime == m_HeartbeatTime))
		{
			size_t dynamicCount = 0;
			for (auto& kv : m_World->bodies)
				dynamicCount += kv.second.dynamic ? 1 : 0;
			EditorLog::Write("Physics", "heartbeat %.1f s: %d steps (dt %.4f, accumulator %.4f, fixed %.4f), %zu bodies, %zu dynamic",
				m_HeartbeatTime, m_HeartbeatSteps, deltaTime, m_Accumulator, m_FixedTimestep, m_World->bodies.size(), dynamicCount);
			m_HeartbeatSteps = 0;
		}
	}
	if (steps == 8)
		m_Accumulator = 0.0f;

	// Interpolate: 두 스텝 사이 위치를 보간해 Transform 에 쓴다
	const float alpha = std::clamp(m_Accumulator / m_FixedTimestep, 0.0f, 1.0f);
	for (JoltWorld::BodyRecord* rp : DynamicByDepth(*m_World))
	{
		JoltWorld::BodyRecord& r = *rp;
		if (!r.interpolate)
			continue;
		Transform* tr = r.owner->GetTransform();
		tr->SetPosition(Vec3::Lerp(r.prevPos, r.curPos, alpha));
		tr->SetRotation(Quaternion::Slerp(r.prevRot, r.curRot, alpha));
		r.lastPos = tr->GetPosition();
		r.lastRot = tr->GetRotation();
	}
}

void PhysicsManager::StepSimulation(float dt)
{
	Scene* scene = SceneManager::GetI()->GetCurrentScene();
	if (scene == nullptr || !m_World)
		return;
	JoltWorld& w = *m_World;
	JPH::BodyInterface& bi = w.BI();
	std::vector<GameObject*> all = scene->GetAllGameObjects();

	// 1) FixedUpdate (Unity: 물리 시뮬레이션 직전)
	if (dt > 0.0f)
	{
		PROFILE_SCOPE("Physics.FixedUpdate");
		for (GameObject* go : all)
		{
			if (!IsActiveInHierarchy(go))
				continue;
			// (ComponentIndex 를 쓰면 표 조회 · 복사가 더 들었다 — 0.13 → 0.19 ms, 도시 PC Release. 여기는 그대로)
			for (const auto& c : go->GetComponents())
			{
				if (MonoBehaviour* mb = dynamic_cast<MonoBehaviour*>(c.get()))
					if (!mb->IsEnabled())
						continue;
				c->FixedUpdate();
			}
		}
		all = scene->GetAllGameObjects();
	}

	// 2) 바디 동기화: 오브젝트마다 컴포넌트를 한 번만 훑어 콜라이더 · Rigidbody · Character Controller · Joint 를 모은다.
	//  버퍼 · 콜라이더 표는 스텝마다 새로 만들지 않고 그 자리에서 고친다 (예전: 스텝마다 소유자 맵 · id 맵 · 표를 새로 —
	//  정적 콜라이더 2200 개의 도시에서 스텝마다 약 3 ms). 바디가 바뀌었는지는 지금처럼 서명으로
	static const char* kSyncName = Profiler::Intern("Physics.Sync");
	const bool profiling = Profiler::Collecting();
	if (profiling) Profiler::Begin(kSyncName);
	const uint32_t stamp = ++w.syncStamp;
	w.scan.clear();
	w.scanCharacters.clear();
	w.scanJoints.clear();
	for (GameObject* go : all)
	{
		if (!IsActiveInHierarchy(go))
			continue;
		const size_t first = w.scan.size();
		// 분류는 ComponentIndex 가 기억한다 (컴포넌트가 그대로면 dynamic_cast 없이)
		const ComponentIndex::Entry& e = ComponentIndex::Of(go);
		RigidBody* ownRb = e.Rigid;
		if (e.Character)
			w.scanCharacters.push_back(e.Character);   // 오브젝트의 첫 Character Controller (예전 GetComponent 와 같음)
		for (Collider* col : e.Colliders)
		{
			if (col == e.Character)
				continue;
			// 형상 종류: 표에 기억한 값, 처음 보는 콜라이더면 판정 (두 번째 Character Controller 등 형상이 아니면 None)
			auto it = w.colliders.find((JPH::uint32)col->GetInstanceID());
			const uint8_t kind = it != w.colliders.end() && it->second.collider == col && it->second.kind != KindNone ? it->second.kind : KindOf(col);
			if (kind != KindNone && col->IsEnabled())
				w.scan.push_back({ nullptr, col, nullptr, kind });
		}
		if (ownRb)
			for (Joint* jt : e.Joints)
				w.scanJoints.push_back({ go, jt });   // Joint 는 Rigidbody 가 있는 오브젝트만
		// 소유자: 자기에게 Rigidbody 가 있으면 자기, 없으면 Rigidbody 가 있는 가장 가까운 조상, 그것도 없으면 자기 (정적)
		if (w.scan.size() == first)
		{
			if (ownRb)
				w.scan.push_back({ go, nullptr, ownRb, KindNone });   // 콜라이더 없는 Rigidbody (빈 형상 바디)
			continue;
		}
		GameObject* owner = go;
		RigidBody* rb = ownRb;
		if (!ownRb)
			if (GameObject* up = FindRigidOwner(go->GetParent()))
			{
				owner = up;
				rb = ComponentIndex::Of(up).Rigid;
			}
		for (size_t i = first; i < w.scan.size(); ++i)
		{
			w.scan[i].owner = owner;
			w.scan[i].rb = rb;
		}
	}
	// 소유자끼리 (같은 소유자 안에서는 훑은 순서 그대로 — 서명 · 형상이 스텝마다 같게)
	std::stable_sort(w.scan.begin(), w.scan.end(), [](const JoltWorld::ScanEntry& a, const JoltWorld::ScanEntry& b) { return a.owner < b.owner; });

	w.rigidToOwner.clear();
	for (size_t g = 0; g < w.scan.size();)
	{
		GameObject* owner = w.scan[g].owner;
		RigidBody* rb = w.scan[g].rb;
		std::vector<Collider*>& cols = w.groupColliders;
		std::vector<uint8_t>& kinds = w.groupKinds;
		cols.clear();
		kinds.clear();
		size_t e = g;
		for (; e < w.scan.size() && w.scan[e].owner == owner; ++e)
			if (w.scan[e].collider)
			{
				cols.push_back(w.scan[e].collider);
				kinds.push_back(w.scan[e].kind);
			}
		g = e;   // 다음 소유자
		const JPH::uint64 key = owner->GetInstanceID();
		const size_t sig = ComputeSignature(owner, rb, cols, kinds.data());

		for (size_t ci = 0; ci < cols.size(); ++ci)
		{
			Collider* c = cols[ci];
			JoltWorld::ColliderEntry& entry = w.colliders[(JPH::uint32)c->GetInstanceID()];
			entry.collider = c;
			entry.owner = owner;
			entry.kind = kinds[ci];
			entry.seen = stamp;
			// Unity: 볼록이 아닌 Mesh Collider 는 트리거가 될 수 없다
			entry.trigger = c->IsTrigger() && !(entry.kind == KindMesh && !static_cast<MeshCollider*>(c)->IsConvex());
			entry.layer = c->GetGameObject()->GetLayerIndex() & 31;
			entry.layerBit = 1u << entry.layer;
			entry.excludeMask = c->GetExcludeLayers() | (rb ? rb->GetExcludeLayers() : 0u);
			entry.includeMask = c->GetIncludeLayers() | (rb ? rb->GetIncludeLayers() : 0u);
			entry.priority = c->GetLayerOverridePriority();
		}

		auto existing = w.bodies.find(key);
		if (existing != w.bodies.end() && existing->second.signature == sig && existing->second.owner == owner)
		{
			existing->second.seen = stamp;
			if (rb) w.rigidToOwner[rb] = key;
			continue;
		}
		Vec3 keepVel = Vec3::Zero, keepAng = Vec3::Zero;
		if (existing != w.bodies.end())
		{
			if (!existing->second.id.IsInvalid())
			{
				keepVel = FromJ(bi.GetLinearVelocity(existing->second.id));
				keepAng = FromJ(bi.GetAngularVelocity(existing->second.id));
				DropJointsOf(w, existing->second.id);
				bi.RemoveBody(existing->second.id);
				bi.DestroyBody(existing->second.id);
			}
			w.bodies.erase(existing);
		}

		Transform* tr = owner->GetTransform();
		const Vec3 pos = tr->GetPosition();
		const Quaternion rot = tr->GetRotation();

		bool frozenAll = false;
		JPH::uint8 dofs = 0x3f;
		if (rb)
		{
			for (int i = 0; i < 3; ++i)
			{
				if (rb->IsPositionFrozen(i)) dofs &= ~(JPH::uint8)(1u << i);
				if (rb->IsRotationFrozen(i)) dofs &= ~(JPH::uint8)(1u << (i + 3));
			}
			frozenAll = dofs == 0;
		}
		const bool kinematic = rb && (rb->IsKinematic() || frozenAll);
		const bool dynamic = rb && !kinematic;

		JPH::RefConst<JPH::Shape> shape = BuildShape(cols, pos, rot, dynamic);
		const bool emptyShape = shape == nullptr;
		if (emptyShape)
		{
			if (rb == nullptr)
				continue;   // 콜라이더 없는 정적 오브젝트는 바디가 필요 없다 (보지 못한 바디로 아래에서 빠진다)
			shape = JPH::EmptyShapeSettings().Create().Get();
		}

		JPH::BodyCreationSettings bcs(shape, ToJR(pos), ToJ(rot),
			rb == nullptr ? JPH::EMotionType::Static : (kinematic ? JPH::EMotionType::Kinematic : JPH::EMotionType::Dynamic),
			rb == nullptr ? Layers::NonMoving : Layers::Moving);
		bcs.mFriction = 0.6f;       // Unity 기본 Physics Material
		bcs.mRestitution = 0.0f;
		bcs.mUserData = key;
		if (rb)
		{
			bcs.mLinearDamping = rb->GetLinearDamping();
			bcs.mAngularDamping = rb->GetAngularDamping();
			bcs.mGravityFactor = rb->GetUseGravity() ? 1.0f : 0.0f;
			bcs.mMaxAngularVelocity = 50.0f;   // Unity Physics.defaultMaxAngularSpeed (rad/s)
			bcs.mCollideKinematicVsNonDynamic = kinematic;   // 키네마틱 ↔ 정적 트리거 이벤트 (Unity 와 동일)
			if (rb->GetCollisionDetection() != RigidBody::CollisionDetection::Discrete)
				bcs.mMotionQuality = JPH::EMotionQuality::LinearCast;
			if (dynamic)
			{
				bcs.mAllowedDOFs = (JPH::EAllowedDOFs)dofs;
				if (emptyShape)
				{
					bcs.mOverrideMassProperties = JPH::EOverrideMassProperties::MassAndInertiaProvided;
					bcs.mMassPropertiesOverride.mMass = rb->GetMass();
					bcs.mMassPropertiesOverride.mInertia = JPH::Mat44::sScale(JPH::Vec3::sReplicate(0.1f * rb->GetMass()));
				}
				else
				{
					bcs.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
					bcs.mMassPropertiesOverride.mMass = rb->GetMass();
				}
			}
		}

		JPH::BodyID id = bi.CreateAndAddBody(bcs, dynamic ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
		if (id.IsInvalid())
			continue;

		JoltWorld::BodyRecord rec;
		rec.owner = owner;
		rec.rb = rb;
		rec.id = id;
		rec.signature = sig;
		rec.dynamic = dynamic;
		rec.kinematic = kinematic;
		rec.interpolate = rb && rb->GetInterpolation() != RigidBody::Interpolation::None;
		rec.colliders = cols;
		rec.lastPos = rec.prevPos = rec.curPos = pos;
		rec.lastRot = rec.prevRot = rec.curRot = rot;
		if (rb)
		{
			rb->_SetBodyId(id.GetIndexAndSequenceNumber());
			if (dynamic)
			{
				Vec3 v = keepVel + rb->_TakePendingVelocity();
				Vec3 a = keepAng + rb->_TakePendingAngularVelocity();
				bi.SetLinearAndAngularVelocity(id, ToJ(v), ToJ(a));
			}
			w.rigidToOwner[rb] = key;
		}
		rec.seen = stamp;
		w.bodies[key] = rec;
	}

	// 이번에 보지 못한 소유자의 바디 · 콜라이더는 뺀다 (지워짐 · 꺼짐 · 콜라이더가 없어짐)
	for (auto it = w.bodies.begin(); it != w.bodies.end();)
	{
		if (it->second.seen == stamp)
		{
			++it;
			continue;
		}
		if (!it->second.id.IsInvalid())
		{
			DropJointsOf(w, it->second.id);
			bi.RemoveBody(it->second.id);
			bi.DestroyBody(it->second.id);
		}
		it = w.bodies.erase(it);
	}
	for (auto it = w.colliders.begin(); it != w.colliders.end();)
		it = it->second.seen == stamp ? std::next(it) : w.colliders.erase(it);   // Character Controller 항목은 아래 EnsureCharacter 가 다시 넣는다
	ComponentIndex::EndPass();
	if (profiling) Profiler::End();   // Physics.Sync

	// Character Controller: 활성인 것만 CharacterVirtual 로 (없어진 것은 지운다 — 안쪽 바디도 같이)
	{
		PROFILE_SCOPE("Physics.Characters");
		std::unordered_set<CharacterController*> alive;
		for (CharacterController* cc : w.scanCharacters)
			if (cc->IsEnabled() && EnsureCharacter(w, cc) != nullptr)
				alive.insert(cc);
		for (auto it = w.characters.begin(); it != w.characters.end();)
			it = alive.count(it->first) ? std::next(it) : w.characters.erase(it);
	}
	{
		PROFILE_SCOPE("Physics.Joints");
		SyncJoints(w, w.scanJoints);
	}

	if (dt <= 0.0f)
		return;

	// 3) Transform → 바디 (사용자가 옮긴 경우 / 키네마틱 / 정적)
	static const char* kPushName = Profiler::Intern("Physics.TransformToBody");
	if (profiling) Profiler::Begin(kPushName);
	for (auto& kv : w.bodies)
	{
		JoltWorld::BodyRecord& r = kv.second;
		if (r.id.IsInvalid())
			continue;
		Transform* tr = r.owner->GetTransform();
		const Vec3 pos = tr->GetPosition();
		const Quaternion rot = tr->GetRotation();
		const bool moved = !NearlyEqual(pos, r.lastPos) || !NearlyEqual(rot, r.lastRot);
		if (r.kinematic)
			bi.MoveKinematic(r.id, ToJR(pos), ToJ(rot), dt);
		else if (moved)
			bi.SetPositionAndRotation(r.id, ToJR(pos), ToJ(rot), r.dynamic ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
		r.lastPos = pos;
		r.lastRot = rot;
		if (moved)
		{
			r.prevPos = r.curPos = pos;
			r.prevRot = r.curRot = rot;
		}
	}

	if (profiling) Profiler::End();   // Physics.TransformToBody

	// 4) 시뮬레이션
	{
		std::lock_guard<std::mutex> lock(w.touchMutex);
		w.touching.clear();
	}
	{
		PROFILE_SCOPE("Physics.Simulate");
		w.physics->Update(dt, 1, w.tempAllocator.get(), w.jobSystem.get());
	}

	// 4.5) Joint 끊어짐: 구속이 쓴 힘(충격량 / dt)이 Break Force / Break Torque 를 넘으면 (Unity: OnJointBreak 후 컴포넌트 삭제)
	if (!w.joints.empty())
	{
		std::vector<std::pair<Joint*, float>> broken;
		for (auto& kv : w.joints)
		{
			Joint* jt = kv.first;
			const float bf = jt->GetBreakForce(), bt = jt->GetBreakTorque();
			if (std::isinf(bf) && std::isinf(bt))
				continue;
			float force = 0.0f, torque = 0.0f;
			if (auto* f = dynamic_cast<JPH::FixedConstraint*>(kv.second.c.GetPtr()))
			{
				force = f->GetTotalLambdaPosition().Length() / dt;
				torque = f->GetTotalLambdaRotation().Length() / dt;
			}
			else if (auto* h = dynamic_cast<JPH::HingeConstraint*>(kv.second.c.GetPtr()))
			{
				force = h->GetTotalLambdaPosition().Length() / dt;
				const JPH::Vector<2> r = h->GetTotalLambdaRotation();
				torque = sqrtf(r[0] * r[0] + r[1] * r[1]) / dt;
			}
			else if (auto* d = dynamic_cast<JPH::DistanceConstraint*>(kv.second.c.GetPtr()))
				force = fabsf(d->GetTotalLambdaPosition()) / dt;
			else if (auto* st = dynamic_cast<JPH::SwingTwistConstraint*>(kv.second.c.GetPtr()))
			{
				force = st->GetTotalLambdaPosition().Length() / dt;
				const float t = st->GetTotalLambdaTwist(), sy = st->GetTotalLambdaSwingY(), sz = st->GetTotalLambdaSwingZ();
				torque = sqrtf(t * t + sy * sy + sz * sz) / dt;
			}
			else if (auto* sd = dynamic_cast<JPH::SixDOFConstraint*>(kv.second.c.GetPtr()))
			{
				force = sd->GetTotalLambdaPosition().Length() / dt;
				torque = sd->GetTotalLambdaRotation().Length() / dt;
			}
			if (force > bf)
				broken.push_back({ jt, force });
			else if (torque > bt)
				broken.push_back({ jt, torque });
		}
		for (const auto& b : broken)
		{
			auto it = w.joints.find(b.first);
			if (it == w.joints.end())
				continue;
			w.physics->RemoveConstraint(it->second.c);
			w.joints.erase(it);
			b.first->_SetBroken();
			EditorLog::Write("Physics", "joint broke on '%s' (%.1f)", b.first->GetGameObject()->GetName().c_str(), b.second);
			if (GameObject* go = b.first->GetGameObject())
				for (const auto& component : go->GetComponents())
					if (MonoBehaviour* script = dynamic_cast<MonoBehaviour*>(component.get()))
						if (script->IsEnabled())
							script->OnJointBreak(b.second);
			GameObject::Destroy(b.first);
		}
		if (!broken.empty())
			RebuildNoCollide(w);
	}

	// 5) 바디 → Transform (Dynamic). 부모 먼저 — 자식 바디를 먼저 쓰면 뒤에 부모가 움직일 때 끌려간다 (래그돌 · 사슬)
	for (JoltWorld::BodyRecord* rp : DynamicByDepth(w))
	{
		JoltWorld::BodyRecord& r = *rp;
		JPH::RVec3 p;
		JPH::Quat q;
		bi.GetPositionAndRotation(r.id, p, q);
		r.prevPos = r.curPos;
		r.prevRot = r.curRot;
		r.curPos = Vec3((float)p.GetX(), (float)p.GetY(), (float)p.GetZ());
		r.curRot = FromJ(q);
		if (!r.interpolate)
		{
			Transform* tr = r.owner->GetTransform();
			tr->SetPosition(r.curPos);
			tr->SetRotation(r.curRot);
			r.lastPos = tr->GetPosition();
			r.lastRot = tr->GetRotation();
		}
	}

	// 6) 충돌 / 트리거 이벤트 (Enter / Stay / Exit)
	PROFILE_SCOPE("Physics.Events");
	std::unordered_map<JPH::uint64, JoltWorld::TouchInfo> current;
	{
		std::lock_guard<std::mutex> lock(w.touchMutex);
		current = w.touching;
	}
	// 잠든 바디끼리 닿아 있던 쌍은 콜백이 오지 않으므로 그대로 유지한다 (Unity 도 잠든 동안 Stay 를 보내지 않음)
	std::vector<JoltWorld::TouchInfo> exits;
	for (auto& kv : w.prevTouching)
	{
		if (current.count(kv.first))
			continue;
		const JoltWorld::TouchInfo& t = kv.second;
		const bool bothExist = w.colliders.count(t.a) && w.colliders.count(t.b);
		const bool asleep = bothExist && !bi.IsActive(t.bodyA) && !bi.IsActive(t.bodyB) && bi.IsAdded(t.bodyA) && bi.IsAdded(t.bodyB);
		if (asleep)
			current[kv.first] = t;
		else
			exits.push_back(t);
	}

	auto dispatch = [&](const JoltWorld::TouchInfo& t, int kind, bool sleepingStay) {
		auto ia = w.colliders.find(t.a);
		auto ib = w.colliders.find(t.b);
		if (ia == w.colliders.end() || ib == w.colliders.end() || sleepingStay)
			return;
		const auto& A = ia->second;
		const auto& B = ib->second;
		if (FILE* fp = PhysLog())
		{
			static const char* kinds[] = { "Enter", "Stay", "Exit" };
			if (kind != 1)
				fprintf(fp, "[step %d] On%s%s  %s <-> %s\n", m_StepCount, t.trigger ? "Trigger" : "Collision", kinds[kind],
					A.collider->GetGameObject()->GetName().c_str(), B.collider->GetGameObject()->GetName().c_str());
		}
		DispatchToObject(A.collider->GetGameObject(), B.collider, t.trigger, kind);
		if (A.owner != A.collider->GetGameObject()) DispatchToObject(A.owner, B.collider, t.trigger, kind);
		DispatchToObject(B.collider->GetGameObject(), A.collider, t.trigger, kind);
		if (B.owner != B.collider->GetGameObject()) DispatchToObject(B.owner, A.collider, t.trigger, kind);
	};

	for (auto& kv : current)
	{
		const bool isNew = w.prevTouching.count(kv.first) == 0;
		const bool sleeping = !isNew && !bi.IsActive(kv.second.bodyA) && !bi.IsActive(kv.second.bodyB);
		dispatch(kv.second, isNew ? 0 : 1, sleeping);
	}
	for (const auto& t : exits)
		dispatch(t, 2, false);

	w.prevTouching = std::move(current);

	++m_StepCount;
	if (FILE* fp = PhysLog())
	{
		if (m_StepCount == 1 || m_StepCount % 50 == 0)
		{
			fprintf(fp, "[step %d] t=%.2fs\n", m_StepCount, m_StepCount * dt);
			for (auto& kv : w.bodies)
			{
				const JoltWorld::BodyRecord& r = kv.second;
				if (!r.dynamic) continue;
				Transform* tr = r.owner->GetTransform();
				Vec3 p = tr->GetPosition();
				Vec3 e = tr->GetEulerAngle();
				fprintf(fp, "   %-12s pos(%7.3f %7.3f %7.3f) rot(%6.1f %6.1f %6.1f) %s\n", r.owner->GetName().c_str(), p.x, p.y, p.z, e.x, e.y, e.z,
					bi.IsActive(r.id) ? "awake" : "asleep");
			}
		}
		fflush(fp);
	}
}

// ====================================================================== Raycast
bool PhysicsManager::Raycast(const Vec3& origin, const Vec3& direction, RaycastHit& hit, float maxDistance, bool hitTriggers, uint32 layerMask)
{
	if (!m_World)
		return false;
	Vec3 dir = direction;
	if (dir.LengthSquared() < 1e-12f)
		return false;
	dir.Normalize();

	JoltWorld& w = *m_World;
	JPH::RRayCast ray{ ToJR(origin), ToJ(dir * maxDistance) };
	JPH::RayCastSettings settings;
	JPH::AllHitCollisionCollector<JPH::CastRayCollector> collector;
	w.physics->GetNarrowPhaseQuery().CastRay(ray, settings, collector);
	if (!collector.HadHit())
		return false;
	collector.Sort();

	for (const JPH::RayCastResult& r : collector.mHits)
	{
		JPH::BodyLockRead lock(w.physics->GetBodyLockInterface(), r.mBodyID);
		if (!lock.Succeeded())
			continue;
		const JPH::Body& body = lock.GetBody();
		JPH::uint32 cid = 0;
		if (!DecodeCollider(body.GetShape()->GetSubShapeUserData(r.mSubShapeID2), cid))
			continue;
		auto it = w.colliders.find(cid);
		if (it == w.colliders.end())
			continue;
		if (it->second.trigger && !hitTriggers)
			continue;
		if (!(layerMask & it->second.layerBit))
			continue;   // Unity 의 layerMask (기본 = Ignore Raycast 를 뺀 모두)

		JPH::RVec3 point = ray.GetPointOnRay(r.mFraction);
		hit.point = Vec3((float)point.GetX(), (float)point.GetY(), (float)point.GetZ());
		hit.normal = FromJ(body.GetWorldSpaceSurfaceNormal(r.mSubShapeID2, point));
		hit.distance = r.mFraction * maxDistance;
		hit.collider = it->second.collider;
		hit.gameObject = it->second.collider->GetGameObject();
		return true;
	}
	return false;
}

// ====================================================================== RigidBody API
bool PhysicsManager::GetLinearVelocity(RigidBody* rb, Vec3& out)
{
	if (!m_World) return false;
	auto* r = m_World->Find(rb);
	if (!r) return false;
	out = FromJ(m_World->BI().GetLinearVelocity(r->id));
	return true;
}

bool PhysicsManager::SetLinearVelocity(RigidBody* rb, const Vec3& v)
{
	if (!m_World) return false;
	auto* r = m_World->Find(rb);
	if (!r) return false;
	if (r->dynamic)
		m_World->BI().SetLinearVelocity(r->id, ToJ(v));
	return true;
}

bool PhysicsManager::GetAngularVelocity(RigidBody* rb, Vec3& out)
{
	if (!m_World) return false;
	auto* r = m_World->Find(rb);
	if (!r) return false;
	out = FromJ(m_World->BI().GetAngularVelocity(r->id));
	return true;
}

bool PhysicsManager::SetAngularVelocity(RigidBody* rb, const Vec3& wv)
{
	if (!m_World) return false;
	auto* r = m_World->Find(rb);
	if (!r) return false;
	if (r->dynamic)
		m_World->BI().SetAngularVelocity(r->id, ToJ(wv));
	return true;
}

bool PhysicsManager::AddForce(RigidBody* rb, const Vec3& force, ForceMode mode)
{
	if (!m_World) return false;
	auto* r = m_World->Find(rb);
	if (!r) return false;
	if (!r->dynamic) return true;
	JPH::BodyInterface& bi = m_World->BI();
	const float mass = rb->GetMass();
	switch (mode)
	{
	case ForceMode::Force:          bi.AddForce(r->id, ToJ(force)); break;
	case ForceMode::Acceleration:   bi.AddForce(r->id, ToJ(force * mass)); break;
	case ForceMode::Impulse:        bi.AddImpulse(r->id, ToJ(force)); break;
	case ForceMode::VelocityChange: bi.AddImpulse(r->id, ToJ(force * mass)); break;
	}
	return true;
}

bool PhysicsManager::AddTorque(RigidBody* rb, const Vec3& torque, ForceMode mode)
{
	if (!m_World) return false;
	auto* r = m_World->Find(rb);
	if (!r) return false;
	if (!r->dynamic) return true;
	JPH::BodyInterface& bi = m_World->BI();
	switch (mode)
	{
	case ForceMode::Force:
		bi.AddTorque(r->id, ToJ(torque));
		break;
	case ForceMode::Impulse:
		bi.AddAngularImpulse(r->id, ToJ(torque));
		break;
	case ForceMode::Acceleration:
	case ForceMode::VelocityChange:
	{
		// 관성 텐서를 곱해 질량과 무관하게 만든다
		JPH::BodyLockRead lock(m_World->physics->GetBodyLockInterface(), r->id);
		if (!lock.Succeeded()) break;
		const JPH::Body& body = lock.GetBody();
		JPH::Mat44 invI = body.GetInverseInertia();
		JPH::Vec3 t = ToJ(torque);
		JPH::Vec3 scaled = invI.Inversed3x3() * t;
		lock.ReleaseLock();
		if (mode == ForceMode::Acceleration) bi.AddTorque(r->id, scaled);
		else bi.AddAngularImpulse(r->id, scaled);
		break;
	}
	}
	return true;
}

float PhysicsManager::GetEffectiveMass(RigidBody* rb, const Vec3& point, const Vec3& dir)
{
	if (!m_World) return 0.0f;
	auto* r = m_World->Find(rb);
	if (!r || !r->dynamic || r->id.IsInvalid()) return 0.0f;
	JPH::BodyLockRead lock(m_World->physics->GetBodyLockInterface(), r->id);
	if (!lock.Succeeded()) return 0.0f;
	const JPH::Body& body = lock.GetBody();
	const JPH::MotionProperties* mp = body.GetMotionProperties();
	if (mp == nullptr) return 0.0f;
	JPH::Vec3 n = ToJ(dir);
	if (n.LengthSq() < 1e-12f) return 0.0f;
	n = n.Normalized();
	const JPH::Vec3 rel = JPH::Vec3(ToJR(point) - body.GetCenterOfMassPosition());
	const JPH::Vec3 rn = rel.Cross(n);
	const float k = mp->GetInverseMass() + rn.Dot(body.GetInverseInertia().Multiply3x3(rn));
	return k > 1e-12f ? 1.0f / k : 0.0f;
}

bool PhysicsManager::AddForceAtPosition(RigidBody* rb, const Vec3& force, const Vec3& position, ForceMode mode)
{
	if (!m_World) return false;
	auto* r = m_World->Find(rb);
	if (!r) return false;
	if (!r->dynamic) return true;
	JPH::BodyInterface& bi = m_World->BI();
	const float mass = rb->GetMass();
	switch (mode)
	{
	case ForceMode::Force:          bi.AddForce(r->id, ToJ(force), ToJR(position)); break;
	case ForceMode::Acceleration:   bi.AddForce(r->id, ToJ(force * mass), ToJR(position)); break;
	case ForceMode::Impulse:        bi.AddImpulse(r->id, ToJ(force), ToJR(position)); break;
	case ForceMode::VelocityChange: bi.AddImpulse(r->id, ToJ(force * mass), ToJR(position)); break;
	}
	return true;
}

bool PhysicsManager::MovePosition(RigidBody* rb, const Vec3& position)
{
	if (!m_World) return false;
	auto* r = m_World->Find(rb);
	if (!r) return false;
	Transform* tr = r->owner->GetTransform();
	if (r->kinematic)
	{
		tr->SetPosition(position);   // 다음 스텝에 MoveKinematic 으로 부드럽게 이동 (접촉 발생)
		return true;
	}
	m_World->BI().SetPosition(r->id, ToJR(position), JPH::EActivation::Activate);
	tr->SetPosition(position);
	r->lastPos = r->prevPos = r->curPos = tr->GetPosition();
	return true;
}

bool PhysicsManager::MoveRotation(RigidBody* rb, const Quaternion& rotation)
{
	if (!m_World) return false;
	auto* r = m_World->Find(rb);
	if (!r) return false;
	Transform* tr = r->owner->GetTransform();
	if (r->kinematic)
	{
		tr->SetRotation(rotation);
		return true;
	}
	m_World->BI().SetRotation(r->id, ToJ(rotation), JPH::EActivation::Activate);
	tr->SetRotation(rotation);
	r->lastRot = r->prevRot = r->curRot = tr->GetRotation();
	return true;
}

bool PhysicsManager::IsSleeping(RigidBody* rb)
{
	if (!m_World) return false;
	auto* r = m_World->Find(rb);
	return r && r->dynamic && !m_World->BI().IsActive(r->id);
}

void PhysicsManager::Sleep(RigidBody* rb)
{
	if (!m_World) return;
	if (auto* r = m_World->Find(rb))
		m_World->BI().DeactivateBody(r->id);
}

void PhysicsManager::WakeUp(RigidBody* rb)
{
	if (!m_World) return;
	if (auto* r = m_World->Find(rb))
		m_World->BI().ActivateBody(r->id);
}

Vec3 PhysicsManager::GetWorldCenterOfMass(RigidBody* rb)
{
	if (m_World)
		if (auto* r = m_World->Find(rb))
		{
			JPH::RVec3 c = m_World->BI().GetCenterOfMassPosition(r->id);
			return Vec3((float)c.GetX(), (float)c.GetY(), (float)c.GetZ());
		}
	return rb && rb->GetGameObject() ? rb->GetGameObject()->GetTransform()->GetPosition() : Vec3::Zero;
}

// ====================================================================== Character Controller
int PhysicsManager::MoveCharacter(CharacterController* cc, const Vec3& motion, float deltaTime)
{
	if (cc == nullptr || cc->GetGameObject() == nullptr)
		return 0;
	Transform* tr = cc->GetGameObject()->GetTransform();
	const float dt = deltaTime > 1e-5f ? deltaTime : 1.0f / 60.0f;
	if (!m_World)
	{
		// Play 가 아님: 충돌 없이 옮기기만
		tr->SetPosition(tr->GetPosition() + motion);
		cc->_SetMoveResult(motion / dt, 0, false);
		cc->_Hits().clear();
		return 0;
	}
	JoltWorld& w = *m_World;
	JoltWorld::CharRecord* rec = EnsureCharacter(w, cc);
	if (rec == nullptr)
		return 0;
	JPH::CharacterVirtual& ch = *rec->ch;
	cc->_Hits().clear();

	const float len = motion.Length();
	if (len < cc->GetMinMoveDistance())
	{
		cc->_SetMoveResult(Vec3::Zero, 0, cc->IsGrounded());
		return 0;
	}

	ch.SetMaxSlopeAngle(JPH::DegreesToRadians(std::clamp(cc->GetSlopeLimit(), 0.0f, 89.9f)));
	const Vec3 offset = CharacterWorldCenter(cc) - tr->GetPosition();   // 캡슐 중심 - Transform 위치
	const JPH::RVec3 before = ch.GetPosition();
	const bool wasGrounded = ch.GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;

	// Move 는 이동량: 이번 시간 동안의 속도로 바꿔 쓸고 지나간다
	ch.SetLinearVelocity(ToJ(motion / dt));
	JPH::CharacterVirtual::ExtendedUpdateSettings es;
	const float step = cc->GetStepOffset();
	es.mWalkStairsStepUp = JPH::Vec3(0.0f, step, 0.0f);
	{
		// 계단 모서리에 둥근 캡슐 바닥이 걸리면 '너무 가파름' 으로 계단 오르기를 거절한다 → 반지름만큼 더 앞을 시험해 평평한 단을 찾게 한다
		float sr, sh;
		cc->GetScaledDimensions(tr->GetScale(), sr, sh);
		es.mWalkStairsStepForwardTest = (std::max)(0.15f, sr);
	}
	// 땅에 있다가 내려가는 중이면 계단·경사 아래로 붙인다 (Unity 처럼 공중에 떴다 떨어지지 않게)
	es.mStickToFloorStepDown = (wasGrounded && motion.y <= 1e-6f) ? JPH::Vec3(0.0f, -(std::max)(step, 0.05f), 0.0f) : JPH::Vec3::sZero();
	JPH::DefaultBroadPhaseLayerFilter bpFilter(w.objVsBp, Layers::Moving);
	JPH::DefaultObjectLayerFilter layerFilter(w.objPair, Layers::Moving);
	JPH::BodyFilter bodyFilter;
	JPH::ShapeFilter shapeFilter;
	w.charListener.world = &w;
	w.charListener.rec = rec;
	rec->moveTriggers.clear();
	ch.ExtendedUpdate(dt, ToJ(m_Gravity), es, bpFilter, layerFilter, bodyFilter, shapeFilter, *w.tempAllocator);
	w.charListener.rec = nullptr;

	const JPH::RVec3 after = ch.GetPosition();
	const Vec3 newCenter((float)after.GetX(), (float)after.GetY(), (float)after.GetZ());
	tr->SetPosition(newCenter - offset);
	rec->lastPos = tr->GetPosition();

	// CollisionFlags: 실제로 부딪힌 접촉이 캡슐의 아래 반구 / 위 반구 / 옆 중 어디인지
	float r, half;
	cc->GetScaledDimensions(tr->GetScale(), r, half);
	int flags = 0;
	const Vec3 dir = motion / len;
	std::vector<ControllerColliderHit>& hits = cc->_Hits();
	for (const auto& c : ch.GetActiveContacts())
	{
		if (!c.mHadCollision || c.mWasDiscarded)
			continue;
		const Vec3 p((float)c.mPosition.GetX(), (float)c.mPosition.GetY(), (float)c.mPosition.GetZ());
		const float rel = p.y - newCenter.y;
		flags |= rel < -half ? (int)CollisionFlags::Below : (rel > half ? (int)CollisionFlags::Above : (int)CollisionFlags::Sides);
		if (Collider* col = ColliderOfBody(w, c.mBodyB, c.mSubShapeIDB))
		{
			ControllerColliderHit h;
			h.collider = col;
			h.gameObject = col->GetGameObject();
			h.point = p;
			h.normal = FromJ(c.mSurfaceNormal);
			h.moveDirection = dir;
			h.moveLength = len;
			hits.push_back(h);
		}
	}
	const bool grounded = (flags & (int)CollisionFlags::Below) != 0 || ch.GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
	if (grounded)
		flags |= (int)CollisionFlags::Below;
	const JPH::Vec3 moved = JPH::Vec3(after - before);
	cc->_SetMoveResult(FromJ(moved) / dt, flags, grounded);

	// 트리거: 이번 Move 에서 만난 것과 지난번 비교 → Enter / Stay / Exit (양쪽 오브젝트에)
	GameObject* self = cc->GetGameObject();
	auto sendTrigger = [&](JPH::uint32 cid, int kind) {
		auto it = w.colliders.find(cid);
		if (it == w.colliders.end())
			return;
		DispatchToObject(self, it->second.collider, true, kind);
		DispatchToObject(it->second.collider->GetGameObject(), cc, true, kind);
	};
	const std::unordered_set<JPH::uint32> now = rec->moveTriggers;
	const std::unordered_set<JPH::uint32> prev = rec->triggers;
	rec->triggers = now;
	for (JPH::uint32 id : now)
		sendTrigger(id, prev.count(id) ? 1 : 0);
	for (JPH::uint32 id : prev)
		if (!now.count(id))
			sendTrigger(id, 2);

	// OnControllerColliderHit (Unity: Move 중 부딪힌 것마다)
	if (!hits.empty())
	{
		const std::vector<ControllerColliderHit> copy = hits;   // 스크립트가 다시 Move 해도 안전하게
		for (size_t i = 0; i < copy.size(); ++i)
			for (const auto& component : self->GetComponents())
				if (MonoBehaviour* script = dynamic_cast<MonoBehaviour*>(component.get()))
					if (script->IsEnabled())
						script->OnControllerColliderHit(copy[i], (int)i);
	}
	return flags;
}

void PhysicsManager::RemoveCharacter(CharacterController* cc)
{
	if (m_World)
		m_World->characters.erase(cc);
}

// ====================================================================== IgnoreCollision
void PhysicsManager::IgnoreCollision(GameObject* a, GameObject* b, bool ignore)
{
	if (m_World == nullptr || a == nullptr || b == nullptr)
		return;
	GameObject* oa = FindRigidOwner(a);
	GameObject* ob = FindRigidOwner(b);
	const uint64 x = (oa ? oa : a)->GetInstanceID(), y = (ob ? ob : b)->GetInstanceID();
	if (x == y)
		return;
	auto& v = m_World->ignoredPairs;
	auto same = [&](const std::pair<uint64, uint64>& p) { return (p.first == x && p.second == y) || (p.first == y && p.second == x); };
	v.erase(std::remove_if(v.begin(), v.end(), same), v.end());
	if (ignore)
		v.push_back({ x, y });
	RebuildNoCollide(*m_World);
}

bool PhysicsManager::GetIgnoreCollision(GameObject* a, GameObject* b)
{
	if (m_World == nullptr || a == nullptr || b == nullptr)
		return false;
	GameObject* oa = FindRigidOwner(a);
	GameObject* ob = FindRigidOwner(b);
	const uint64 x = (oa ? oa : a)->GetInstanceID(), y = (ob ? ob : b)->GetInstanceID();
	for (const auto& p : m_World->ignoredPairs)
		if ((p.first == x && p.second == y) || (p.first == y && p.second == x))
			return true;
	return false;
}

// ====================================================================== Joint
void PhysicsManager::RemoveJoint(Joint* joint)
{
	if (!m_World)
		return;
	auto it = m_World->joints.find(joint);
	if (it == m_World->joints.end())
		return;
	m_World->physics->RemoveConstraint(it->second.c);
	m_World->joints.erase(it);
}

float PhysicsManager::GetHingeAngle(const HingeJoint* joint, bool velocity)
{
	if (!m_World || joint == nullptr)
		return 0.0f;
	auto it = m_World->joints.find(const_cast<HingeJoint*>(joint));
	if (it == m_World->joints.end())
		return 0.0f;
	JPH::HingeConstraint* hc = dynamic_cast<JPH::HingeConstraint*>(it->second.c.GetPtr());
	if (hc == nullptr)
		return 0.0f;
	if (!velocity)
		return JPH::RadiansToDegrees(hc->GetCurrentAngle());
	// 두 바디의 각속도 차이를 힌지 축으로
	JPH::BodyInterface& bi = m_World->BI();
	JPH::Vec3 wa = bi.GetAngularVelocity(it->second.a);
	JPH::Vec3 wb = it->second.b.IsInvalid() ? JPH::Vec3::sZero() : bi.GetAngularVelocity(it->second.b);
	Vec3 ax = Vec3::TransformNormal(joint->GetAxis(), const_cast<HingeJoint*>(joint)->GetGameObject()->GetTransform()->GetWorldMatrix());
	if (ax.LengthSquared() < 1e-10f)
		return 0.0f;
	ax.Normalize();
	return JPH::RadiansToDegrees((wa - wb).Dot(ToJ(ax)));
}

// ====================================================================== 여러 개 레이캐스트 / 편집 중 질의
int PhysicsManager::RaycastAll(const Vec3& origin, const Vec3& direction, float maxDistance, RaycastHit* out, int maxHits, bool staticOnly)
{
	if (!m_World || out == nullptr || maxHits <= 0)
		return 0;
	Vec3 dir = direction;
	if (dir.LengthSquared() < 1e-12f)
		return 0;
	dir.Normalize();
	JoltWorld& w = *m_World;
	JPH::RRayCast ray{ ToJR(origin), ToJ(dir * maxDistance) };
	JPH::RayCastSettings settings;
	JPH::AllHitCollisionCollector<JPH::CastRayCollector> collector;
	w.physics->GetNarrowPhaseQuery().CastRay(ray, settings, collector);
	if (!collector.HadHit())
		return 0;
	collector.Sort();
	int n = 0;
	for (const JPH::RayCastResult& r : collector.mHits)
	{
		if (n >= maxHits)
			break;
		JPH::BodyLockRead lock(w.physics->GetBodyLockInterface(), r.mBodyID);
		if (!lock.Succeeded())
			continue;
		const JPH::Body& body = lock.GetBody();
		if (staticOnly && body.GetMotionType() != JPH::EMotionType::Static)
			continue;
		JPH::uint32 cid = 0;
		if (!DecodeCollider(body.GetShape()->GetSubShapeUserData(r.mSubShapeID2), cid))
			continue;
		auto it = w.colliders.find(cid);
		if (it == w.colliders.end() || it->second.trigger)
			continue;
		JPH::RVec3 point = ray.GetPointOnRay(r.mFraction);
		RaycastHit& hit = out[n++];
		hit.point = Vec3((float)point.GetX(), (float)point.GetY(), (float)point.GetZ());
		hit.normal = FromJ(body.GetWorldSpaceSurfaceNormal(r.mSubShapeID2, point));
		hit.distance = r.mFraction * maxDistance;
		hit.collider = it->second.collider;
		hit.gameObject = it->second.collider->GetGameObject();
	}
	return n;
}

bool PhysicsManager::BeginEditQueries()
{
	if (m_World)
	{
		// 편집 중에도 씬을 열 때(Scene::Enter) 만든 월드가 있다 — 그 뒤 붙이거나 옮긴 콜라이더를 반영한다 (dt 0: 바디만 맞춤)
		if (!Application::IsPlaying())
		{
			StepSimulation(0.0f);
			m_World->physics->OptimizeBroadPhase();
		}
		return true;
	}
	Start();   // 바디만 만든다 (StepSimulation(0) — FixedUpdate·시뮬레이션 없음)
	if (m_World)
		m_World->physics->OptimizeBroadPhase();   // 한꺼번에 넣은 바디: 질의 전에 트리를 다시 짓는다
	m_EditQueryWorld = m_World != nullptr;
	return m_EditQueryWorld;
}

void PhysicsManager::EndEditQueries()
{
	if (!m_EditQueryWorld)
		return;
	m_EditQueryWorld = false;
	Exit();
}

bool PhysicsManager::GetWorldBounds(Vec3& outMin, Vec3& outMax)
{
	if (!m_World)
	{
		EditorLog::Write("Physics", "world bounds: no physics world");
		return false;
	}
	// 바디마다 월드 상자를 합친다 (브로드페이즈 전체 상자는 시뮬레이션 스텝 전에는 갱신되지 않는다)
	JPH::AABox b;
	const JPH::BodyLockInterface& locks = m_World->physics->GetBodyLockInterface();
	for (const auto& kv : m_World->bodies)
	{
		if (kv.second.id.IsInvalid())
			continue;
		JPH::BodyLockRead lock(locks, kv.second.id);
		if (lock.Succeeded())
			b.Encapsulate(lock.GetBody().GetWorldSpaceBounds());
	}
	if (!b.IsValid())
	{
		EditorLog::Write("Physics", "world bounds: no body with a collider (%zu records)", m_World->bodies.size());
		return false;
	}
	outMin = FromJ(JPH::Vec3(b.mMin));
	outMax = FromJ(JPH::Vec3(b.mMax));
	return true;
}

int PhysicsManager::CollectStaticTriangles(const Vec3& boundsMin, const Vec3& boundsMax, std::vector<float>& verts, std::vector<int>& tris)
{
	verts.clear();
	tris.clear();
	if (!m_World)
		return 0;
	const JPH::AABox box(ToJ(boundsMin), ToJ(boundsMax));
	const JPH::BodyLockInterface& locks = m_World->physics->GetBodyLockInterface();
	constexpr int kBatch = 256;
	std::vector<JPH::Float3> buf(kBatch * 3);
	for (const auto& kv : m_World->bodies)
	{
		const JoltWorld::BodyRecord& r = kv.second;
		if (r.id.IsInvalid() || r.rb != nullptr)
			continue;   // Rigidbody 가 있는 것(움직이는 것)은 빼고 정적 콜라이더만
		bool solid = false;
		for (Collider* c : r.colliders)
			solid = solid || !c->IsTrigger();
		if (!solid)
			continue;   // 트리거만 있는 바디
		JPH::BodyLockRead lock(locks, r.id);
		if (!lock.Succeeded())
			continue;
		const JPH::TransformedShape body = lock.GetBody().GetTransformedShape();
		if (!body.GetWorldSpaceBounds().Overlaps(box))
			continue;
		// 복합 형상(지형 + 나무 캡슐, 여러 콜라이더)은 잎 형상부터 모은다 — GetTriangles 는 잎에서만 된다
		JPH::AllHitCollisionCollector<JPH::TransformedShapeCollector> leaves;
		body.CollectTransformedShapes(box, leaves);
		for (const JPH::TransformedShape& ts : leaves.mHits)
		{
			JPH::TransformedShape::GetTrianglesContext ctx;
			ts.GetTrianglesStart(ctx, box, JPH::RVec3::sZero());
			for (;;)
			{
				const int n = ts.GetTrianglesNext(ctx, kBatch, buf.data());
				if (n <= 0)
					break;
				for (int i = 0; i < n * 3; ++i)
				{
					verts.push_back(buf[i].x);
					verts.push_back(buf[i].y);
					verts.push_back(buf[i].z);
					tris.push_back((int)(verts.size() / 3) - 1);
				}
			}
		}
	}
	return (int)(tris.size() / 3);
}
