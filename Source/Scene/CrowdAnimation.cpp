#include "pch.h"
#include "CrowdAnimation.h"
#include "SkinnedMesh.h"
#include "SkinnedData.h"
#include "AnimationPose.h"
#include "HumanoidAvatar.h"
#include "ResourceManager.h"
#include "UMaterial.h"
#include "Effects.h"
#include "Vertex.h"
#include "EditorLog.h"

namespace CrowdAnimation
{
	int Baked::FindClip(const std::string& name) const
	{
		for (int i = 0; i < (int)Clips.size(); ++i)
			if (Clips[(size_t)i].Name == name)
				return i;
		return -1;
	}

	std::vector<ClipSource> DefaultBattleClips()
	{
		const std::string basic = "Resources\\Packages\\Character\\Animations\\Nova_Basic.glb";
		return {
			{ "Idle", basic, 0, true, false },
			{ "Walk", basic, 1, true, false },
			{ "Attack", basic, 2, true, false },   // 팔을 휘두르는 Wave — 근접 공격 대신
			{ "Die", basic, 3, false, true },      // 등으로 누운 채 일어나기를 거꾸로 = 뒤로 쓰러지기
		};
	}

	namespace
	{
		std::unordered_map<std::string, std::shared_ptr<Baked>> s_Cache;

		// 루트 (Hips) 의 수평 이동을 바인드 자리로 — 제자리 걷기 (Animator::PinRoot 와 같은 식)
		void PinRoot(const SkeletonAvataData& sk, const std::vector<XMFLOAT4X4>& bindGlobal, int node, std::vector<XMFLOAT4X4>& local)
		{
			if (node < 0 || node >= (int)local.size() || node >= (int)sk.BindLocal.size())
				return;
			const int parent = node < (int)sk.BoneHierarchy.size() ? sk.BoneHierarchy[(size_t)node] : -1;
			const XMMATRIX pg = parent >= 0 && parent < (int)bindGlobal.size() ? XMLoadFloat4x4(&bindGlobal[(size_t)parent])
				: XMMatrixScaling(sk.UnitScale, sk.UnitScale, sk.UnitScale);
			XMVECTOR det;
			const XMMATRIX inv = XMMatrixInverse(&det, pg);
			XMFLOAT3 cur, bind, back;
			XMStoreFloat3(&cur, XMVector3TransformCoord(XMVectorSet(local[(size_t)node]._41, local[(size_t)node]._42, local[(size_t)node]._43, 1.0f), pg));
			const XMFLOAT4X4& b = sk.BindLocal[(size_t)node];
			XMStoreFloat3(&bind, XMVector3TransformCoord(XMVectorSet(b._41, b._42, b._43, 1.0f), pg));
			XMStoreFloat3(&back, XMVector3TransformCoord(XMVectorSet(bind.x, cur.y, bind.z, 1.0f), inv));
			local[(size_t)node]._41 = back.x;
			local[(size_t)node]._42 = back.y;
			local[(size_t)node]._43 = back.z;
		}

		std::string ClipsKey(const std::vector<ClipSource>& clips)
		{
			std::string k;
			for (const ClipSource& c : clips)
				k += "|" + c.Name + ":" + (c.Source ? std::to_string((uintptr_t)c.Source.get()) : c.Path + ":" + std::to_string(c.Index))
					+ (c.Loop ? "L" : "") + (c.Reverse ? "R" : "");
			return k;
		}
		std::shared_ptr<Baked> BakeImpl(const std::string& key, const std::string& label, const std::shared_ptr<SkinnedMesh>& meshPtr,
			const std::shared_ptr<SkeletonAvataData>& skeleton, const std::vector<std::shared_ptr<UMaterial>>* materials, const std::vector<ClipSource>& clips, int bindMode);
	}

	std::shared_ptr<Baked> Bake(const std::string& modelPath, int meshIndex, const std::vector<ClipSource>& clips)
	{
		const std::string key = modelPath + "#" + std::to_string(meshIndex) + ClipsKey(clips);
		if (auto it = s_Cache.find(key); it != s_Cache.end())
			return it->second;
		auto mesh = ResourceManager::GetI()->LoadSkinnedMesh(string_to_wstring(modelPath), meshIndex);
		auto skeleton = ResourceManager::GetI()->LoadSkeletonAvata(modelPath, 0);
		return BakeImpl(key, modelPath, mesh, skeleton, nullptr, clips, -1);
	}

	std::shared_ptr<Baked> Bake(const std::shared_ptr<SkinnedMesh>& mesh, const std::shared_ptr<SkeletonAvataData>& skeleton,
		const std::vector<std::shared_ptr<UMaterial>>& materials, const std::vector<ClipSource>& clips, int bindMode)
	{
		std::string key = std::to_string((uintptr_t)mesh.get()) + "/" + std::to_string((uintptr_t)skeleton.get()) + "/b" + std::to_string(bindMode) + ClipsKey(clips);
		for (const auto& m : materials)
			key += "/m" + std::to_string((uintptr_t)m.get());
		if (auto it = s_Cache.find(key); it != s_Cache.end())
			return it->second;
		return BakeImpl(key, mesh ? mesh->Name : std::string("?"), mesh, skeleton, &materials, clips, bindMode);
	}

	float ImpostorRow(const Baked& baked, int clip, float time)
	{
		if (clip < 0 || clip >= (int)baked.Clips.size())
			return 0.0f;
		const Clip& c = baked.Clips[(size_t)clip];
		const float u = time / (std::max)(c.Duration, 1e-4f);
		int f = (int)floorf(c.Loop ? (u - floorf(u)) * Baked::ImpostorFrames : std::clamp(u, 0.0f, 1.0f) * (Baked::ImpostorFrames - 1) + 0.5f);
		f = std::clamp(f, 0, Baked::ImpostorFrames - 1);
		return (float)(c.ImpostorRow + (uint32_t)f);
	}

	namespace
	{
	std::shared_ptr<Baked> BakeImpl(const std::string& key, const std::string& label, const std::shared_ptr<SkinnedMesh>& meshPtr,
		const std::shared_ptr<SkeletonAvataData>& skeleton, const std::vector<std::shared_ptr<UMaterial>>* materials, const std::vector<ClipSource>& clips, int bindMode)
	{
		const std::string& modelPath = label;
		const auto t0 = std::chrono::steady_clock::now();
		auto baked = std::make_shared<Baked>();
		s_Cache[key] = nullptr;   // 실패해도 다시 시도하지 않는다 (Clear 로)
		baked->Mesh = meshPtr;
		baked->Skeleton = skeleton;
		if (!baked->Mesh || !baked->Skeleton || baked->Mesh->BoneNames.empty() || baked->Mesh->Subsets.empty())
		{
			EditorLog::Write("Crowd", "bake failed: no skinned mesh (%s)", label.c_str());
			return nullptr;
		}
		const SkinnedMesh& mesh = *baked->Mesh;
		const SkeletonAvataData& sk = *baked->Skeleton;
		baked->Bones = (uint32_t)(std::min)(mesh.BoneNames.size(), (size_t)256);

		// 재질 칸: 렌더러의 재질 (자동 임포스터) 또는 서브셋 재질 번호만큼 기본 재질 (SkinnedMeshRenderer::SetSkinnedMesh 와 같다)
		size_t matCount = 1;
		for (const auto& s : mesh.Subsets)
			matCount = (std::max)(matCount, (size_t)s.MaterialIndex + 1);
		if (materials)
			baked->Materials = *materials;
		if (baked->Materials.size() < matCount)
			baked->Materials.resize(matCount);
		for (auto& m : baked->Materials)
			if (!m) m = UMaterial::GetDefault();

		// 팔레트 본 → 노드, 메시 바인드 (SkinnedMeshRenderer::RebuildPalette 와 같다)
		std::vector<int> paletteNode(baked->Bones, -1);
		for (uint32_t k = 0; k < baked->Bones; ++k)
			paletteNode[k] = sk.FindNode(mesh.BoneNames[k]);
		const XMMATRIX meshBind = mesh.PaletteMeshBind(sk, nullptr, bindMode);   // SkinnedMeshRenderer 와 같은 판정

		std::vector<XMFLOAT4X4> bindGlobal;
		AnimationPose::ComputeGlobals(sk, sk.BindLocal, bindGlobal);
		const Humanoid::Avatar& dst = Humanoid::Get(sk);

		std::vector<XMFLOAT4> data;
		std::vector<XMFLOAT4X4> local, global;
		std::vector<std::vector<XMFLOAT4>> frames;
		Vec3 mn(FLT_MAX, FLT_MAX, FLT_MAX), mx(-FLT_MAX, -FLT_MAX, -FLT_MAX);
		for (const ClipSource& src : clips)
		{
			auto clip = src.Source ? src.Source : ResourceManager::GetI()->LoadAnimationClip(src.Path, src.Index);
			Clip c;
			c.Name = src.Name;
			c.Loop = src.Loop;
			c.FirstFrame = baked->TotalFrames;
			c.Duration = clip ? (std::max)(clip->GetClipEndTime(), 0.05f) : 1.0f;
			c.Frames = (uint32_t)(std::max)(2, (int)std::lround(c.Duration * baked->Fps));
			// 이름으로 반도 안 맞고 둘 다 사람 모양이면 Humanoid 리타게팅 (Animator 의 Avatar Auto 와 같다)
			std::vector<int> map;
			bool retarget = false;
			std::shared_ptr<SkeletonAvataData> source;
			std::vector<int> sourceMap;
			if (clip)
			{
				map = AnimationPose::MapChannels(*clip, sk);
				int mapped = 0;
				for (int n : map) mapped += n >= 0;
				source = clip->SourceSkeleton.lock();
				if (mapped * 2 < (int)clip->Channels.size() && source && source.get() != &sk && dst.Valid && Humanoid::Get(*source).Valid)
				{
					retarget = true;
					sourceMap = AnimationPose::MapChannels(*clip, *source);
				}
			}
			const int rootNode = dst.Valid ? dst.Node[Humanoid::Hips] : -1;
			frames.assign(c.Frames, {});
			for (uint32_t f = 0; f < c.Frames; ++f)
			{
				const float t = c.Loop ? c.Duration * f / c.Frames : c.Duration * f / (c.Frames - 1);
				local = sk.BindLocal;
				if (clip && retarget)
					Humanoid::Retarget(Humanoid::Get(*source), dst, *clip, sourceMap, t, local);
				else if (clip)
					AnimationPose::SampleLocal(sk, clip.get(), map, t, local);
				PinRoot(sk, bindGlobal, rootNode, local);
				AnimationPose::ComputeGlobals(sk, local, global);
				std::vector<XMFLOAT4>& out = frames[(size_t)(src.Reverse ? c.Frames - 1 - f : f)];
				out.resize((size_t)baked->Bones * 3);
				for (uint32_t k = 0; k < baked->Bones; ++k)
				{
					XMFLOAT4X4 m;
					const int node = paletteNode[k];
					if (node < 0 || node >= (int)global.size())
						XMStoreFloat4x4(&m, XMMatrixIdentity());
					else
						XMStoreFloat4x4(&m, meshBind * XMLoadFloat4x4(&mesh.BoneOffsets[k]) * XMLoadFloat4x4(&global[(size_t)node]));
					out[k * 3 + 0] = XMFLOAT4(m._11, m._21, m._31, m._41);
					out[k * 3 + 1] = XMFLOAT4(m._12, m._22, m._32, m._42);
					out[k * 3 + 2] = XMFLOAT4(m._13, m._23, m._33, m._43);
				}
				// 크기: 정점 일부를 이 자세로 (임포스터 칸 · 컬링 구)
				for (size_t v = 0; v < mesh.Vertices.size(); v += 37)
				{
					const auto& vx = mesh.Vertices[v];
					const float w[4] = { vx.weights.x, vx.weights.y, vx.weights.z, 1.0f - vx.weights.x - vx.weights.y - vx.weights.z };
					Vec3 p(0, 0, 0);
					for (int i = 0; i < 4; ++i)
					{
						const uint32_t b = (std::min)((uint32_t)vx.boneIndices[i], baked->Bones - 1) * 3;
						const XMFLOAT4 c0 = out[b], c1 = out[b + 1], c2 = out[b + 2];
						p.x += w[i] * (vx.pos.x * c0.x + vx.pos.y * c0.y + vx.pos.z * c0.z + c0.w);
						p.y += w[i] * (vx.pos.x * c1.x + vx.pos.y * c1.y + vx.pos.z * c1.z + c1.w);
						p.z += w[i] * (vx.pos.x * c2.x + vx.pos.y * c2.y + vx.pos.z * c2.z + c2.w);
					}
					mn = Vec3::Min(mn, p);
					mx = Vec3::Max(mx, p);
				}
			}
			for (const auto& fr : frames)
				data.insert(data.end(), fr.begin(), fr.end());
			baked->TotalFrames += c.Frames;
			baked->Clips.push_back(c);
			if (!clip)
				EditorLog::Write("Crowd", "clip %s (%s #%d) not found — bind pose", src.Name.c_str(), src.Path.c_str(), src.Index);
		}
		if (mn.x <= mx.x)
		{
			baked->HalfWidth = (std::max)({ fabsf(mn.x), fabsf(mx.x), fabsf(mn.z), fabsf(mx.z) }) * 1.05f;
			baked->Height = (mx.y - mn.y) * 1.04f;
			baked->CenterY = (mn.y + mx.y) * 0.5f;
		}

		// GPU: 바뀌지 않는 구조 버퍼 (float4)
		GfxDevice* device = Application::GetI()->GetDevice();
		D3D11_BUFFER_DESC d = {};
		d.ByteWidth = (UINT)(data.size() * 16);
		d.Usage = D3D11_USAGE_IMMUTABLE;
		d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		d.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		d.StructureByteStride = 16;
		D3D11_SUBRESOURCE_DATA init = {};
		init.pSysMem = data.data();
		if (device == nullptr || data.empty() || FAILED(device->CreateBuffer(&d, &init, baked->Palettes.GetAddressOf())))
			return nullptr;
		D3D11_SHADER_RESOURCE_VIEW_DESC s = {};
		s.Format = DXGI_FORMAT_UNKNOWN;
		s.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		s.Buffer.NumElements = (UINT)data.size();
		if (FAILED(device->CreateShaderResourceView(baked->Palettes.Get(), &s, baked->PaletteSrv.GetAddressOf())))
			return nullptr;
		s_Cache[key] = baked;
		EditorLog::Write("Crowd", "baked %s: %u bones, %zu clips, %u frames (%.1f MB), size %.2f x %.2f m (%.0f ms)", modelPath.c_str(),
			baked->Bones, baked->Clips.size(), baked->TotalFrames, data.size() * 16 / 1048576.0, baked->HalfWidth * 2.0f, baked->Height,
			std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
		return baked;
	}
	}

	void Clear()
	{
		s_Cache.clear();
	}
}
