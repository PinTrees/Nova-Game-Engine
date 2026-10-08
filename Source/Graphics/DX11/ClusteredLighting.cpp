#include "pch.h"
#include "ClusteredLighting.h"
#include "LightManager.h"
#include "Effects.h"
#include "CliServer.h"
#include <chrono>
#include "JobSystem.h"

namespace ClusteredLighting
{
	namespace
	{
		constexpr int kClusters = kTilesX * kTilesY * kSlices;
		// 텍스처 하나 (RGBA32F, 가로 1024): OpenGL 은 프로그램마다 샘플러가 32 개까지 — 무거운 셰이더 (lilToon 지형 · 테셀레이션) 가 넘지 않게 하나로 묶는다
		//  텍셀 i → (i % 1024, i / 1024). 빛 = 0 부터 (빛마다 4), 클러스터 표 = 4096 부터 (시작, 개수), 번호 목록 = 8192 부터 (텍셀마다 4 개)
		constexpr int kWidth = 1024;
		constexpr int kLightBase = 0, kGridBase = 4096, kIndexBase = 8192;
		constexpr int kRows = (kIndexBase + (kMaxIndices + 4) / 4 + kWidth - 1) / kWidth;   // 24

		struct Tex
		{
			ComPtr<GfxTexture2D> T;
			ComPtr<GfxShaderResourceView> S;
			UINT W = 0, H = 0;
		};
		Tex s_Data;
		Stats s_Stats;
		bool s_Enabled = true;
		int s_LightCount = 0;
		float s_DepthScale = 0.0f, s_DepthBias = 0.0f;
		Vec3 s_Forward = Vec3(0.0f, 0.0f, 1.0f);

		std::vector<XMFLOAT4> s_LightData;      // 빛마다 4 텍셀
		std::vector<XMFLOAT4> s_Packed;         // 올릴 텍스처 내용 (kWidth x kRows)
		std::vector<uint32> s_Pairs;            // (클러스터 << 10 | 빛) — 개수 세기 정렬

		bool Ensure(Tex& t, UINT w, UINT h, DXGI_FORMAT format)
		{
			if (t.S && t.W == w && t.H == h)
				return true;
			t.T.Reset();
			t.S.Reset();
			D3D11_TEXTURE2D_DESC d = {};
			d.Width = w;
			d.Height = h;
			d.MipLevels = 1;
			d.ArraySize = 1;
			d.Format = format;
			d.SampleDesc.Count = 1;
			d.Usage = D3D11_USAGE_DEFAULT;
			d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			auto device = Gfx::Device();
			if (FAILED(device->CreateTexture2D(&d, nullptr, t.T.GetAddressOf())) || FAILED(device->CreateShaderResourceView(t.T.Get(), nullptr, t.S.GetAddressOf())))
			{
				t.T.Reset();
				t.S.Reset();
				return false;
			}
			t.W = w;
			t.H = h;
			return true;
		}

		int SliceOf(float z)
		{
			const float s = logf((std::max)(z, 1e-4f)) * s_DepthScale + s_DepthBias;
			return std::clamp((int)floorf(s), 0, kSlices - 1);
		}

		// 조각 s 가 시작하는 뷰 깊이 (SliceOf 의 반대)
		float SliceNear(int s) { return expf(((float)s - s_DepthBias) / s_DepthScale); }

		void SetVars(FxEffect* fx)
		{
			if (fx == nullptr)
				return;
			auto vec = [fx](const char* n, float x, float y, float z, float w) {
				if (FxVar* v = fx->GetVariableByName(n); v && v->IsValid())
				{
					const float f[4] = { x, y, z, w };
					v->SetFloatVector(f);
				}
			};
			auto res = [fx](const char* n, GfxShaderResourceView* srv) {
				if (FxVar* v = fx->GetVariableByName(n); v && v->IsValid())
					v->SetResource(srv);
			};
			vec("gClusterParams", (float)kTilesX, (float)kTilesY, (float)kSlices, (float)s_LightCount);
			vec("gClusterDepth", s_DepthScale, s_DepthBias, 0.0f, 0.0f);
			vec("gClusterView", s_Forward.x, s_Forward.y, s_Forward.z, 0.0f);
			res("gClusterData", s_Data.S.Get());
		}
	}

	void Build(const std::vector<AdditionalLight>& lights, const XMFLOAT4X4& view, const XMFLOAT4X4& proj)
	{
		const auto t0 = std::chrono::steady_clock::now();
		s_Stats = Stats();
		const int n = s_Enabled ? (int)(std::min)(lights.size(), (size_t)kMaxAdditionalLights) : 0;
		s_LightCount = 0;

		// 왼손 투영: 원근 P33 = f / (f − n), P43 = −n · P33 / 직교 P33 = 1 / (f − n), P43 = −n · P33
		const bool orthoProj = fabsf(proj._34) < 1e-6f;
		float nearZ = fabsf(proj._33) > 1e-8f ? -proj._43 / proj._33 : 0.1f;
		float farZ = orthoProj ? nearZ + 1.0f / (std::max)(fabsf(proj._33), 1e-8f) : (fabsf(proj._33 - 1.0f) > 1e-8f ? proj._33 * nearZ / (proj._33 - 1.0f) : 1000.0f);
		if (!std::isfinite(nearZ) || nearZ <= 0.0f) nearZ = 0.1f;
		if (!std::isfinite(farZ)) farZ = 1000.0f;
		const float zNear = (std::max)(nearZ, 0.01f);
		const float zFar = std::clamp(farZ, zNear * 2.0f, 5000.0f);
		const float logRange = logf(zFar / zNear);
		s_DepthScale = (float)kSlices / logRange;
		s_DepthBias = -(float)kSlices * logf(zNear) / logRange;
		s_Forward = Vec3(view._13, view._23, view._33);   // 뷰 행렬의 세 번째 열 = 카메라 앞 (월드)
		s_Forward.Normalize();

		s_LightData.assign((size_t)(std::max)(n, 1) * 4, XMFLOAT4(0, 0, 0, 0));
		s_Pairs.clear();
		const XMMATRIX V = XMLoadFloat4x4(&view);
		const XMFLOAT4X4& P = proj;

		// 빛마다 (클러스터 칸) 짝은 서로 기대지 않는다 → 묶음마다 일꾼에서 (Job System), 번호 매기기 · 모으기는 빛 순서대로 (차례로 짓는 것과 같은 결과)
		struct LightOut
		{
			uint32 First = 0, Count = 0;   // 묶음 짝 목록 안의 범위 (번호 자리는 빛 i — 아래에서 바꾼다)
			bool Kept = false;
			float CosOuter = -2.0f, CosInner = -1.0f;
		};
		static std::vector<LightOut> s_Out;
		static std::vector<std::vector<uint32>> s_BatchPairs;
		constexpr int kLightBatch = 16;
		const int batches = (n + kLightBatch - 1) / kLightBatch;
		s_Out.assign((size_t)n, LightOut());
		if ((int)s_BatchPairs.size() < batches)
			s_BatchPairs.resize((size_t)batches);
		Jobs::ParallelFor(batches, 1, [&](int b0, int b1) {
		for (int bi = b0; bi < b1; ++bi)
		{
		std::vector<uint32>& pairs = s_BatchPairs[(size_t)bi];
		pairs.clear();
		const int iEnd = (std::min)(n, (bi + 1) * kLightBatch);
		for (int i = bi * kLightBatch; i < iEnd; ++i)
		{
			const AdditionalLight& l = lights[i];
			// 빛의 경계 구: 점광 = 자리 · 범위, 스포트광 = 원뿔을 감싸는 더 작은 구 (넓은 원뿔이면 점광과 같게)
			Vec3 center = l.Position;
			float radius = l.Range;
			float cosOuter = -2.0f, cosInner = -1.0f;
			if (l.Type == 1)
			{
				const float half = XMConvertToRadians(std::clamp(l.SpotAngle, 1.0f, 179.0f) * 0.5f);
				cosOuter = cosf(half);
				cosInner = cosf(XMConvertToRadians(std::clamp(l.SpotAngle, 1.0f, 179.0f) * 0.4f));
				const float tight = sqrtf(0.25f + tanf(half) * tanf(half)) * l.Range;
				if (tight < l.Range)
				{
					Vec3 dir = l.Direction;
					dir.Normalize();
					center = l.Position + dir * (l.Range * 0.5f);
					radius = tight;
				}
			}
			const Vec3 c = Vec3::Transform(center, V);   // 뷰 공간 (왼손, +z 앞)
			LightOut& lo = s_Out[(size_t)i];
			lo.CosOuter = cosOuter;
			lo.CosInner = cosInner;
			if (radius <= 0.0f || c.z + radius < zNear || c.z - radius > zFar)
				continue;   // 잘림 (아래에서 센다)

			// 깊이 조각마다: 그 조각 안에서의 구 단면 (가장 넓은 원) 을 감싸는 상자를 투영 → 타일 사각형 (멀리 있는 빛이 화면을 넓게 차지하지 않게)
			//  상자는 가까운 면 앞으로 자른다 (z ≥ near > 0 이라 투영이 늘 맞다)
			const size_t firstPair = pairs.size();
			const int index = i;   // 임시 (빛 번호) — 모을 때 실제 번호로
			const int s0 = SliceOf((std::max)(c.z - radius, zNear));
			const int s1 = SliceOf((std::min)(c.z + radius, zFar));
			for (int s = s0; s <= s1; ++s)
			{
				const float za = (std::max)({ SliceNear(s), c.z - radius, zNear });
				const float zb = (std::min)({ SliceNear(s + 1), c.z + radius, zFar });
				if (za > zb)
					continue;
				const float dz = c.z < za ? za - c.z : (c.z > zb ? c.z - zb : 0.0f);
				const float rs = sqrtf((std::max)(radius * radius - dz * dz, 0.0f));
				float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
				for (int k = 0; k < 8; ++k)
				{
					const float x = c.x + ((k & 1) ? rs : -rs);
					const float y = c.y + ((k & 2) ? rs : -rs);
					const float z = (k & 4) ? zb : za;
					const float cx = x * P._11 + y * P._21 + z * P._31 + P._41;
					const float cy = x * P._12 + y * P._22 + z * P._32 + P._42;
					const float cw = x * P._14 + y * P._24 + z * P._34 + P._44;
					const float w = (std::max)(cw, 1e-5f);
					x0 = (std::min)(x0, cx / w); x1 = (std::max)(x1, cx / w);
					y0 = (std::min)(y0, cy / w); y1 = (std::max)(y1, cy / w);
				}
				if (x1 < -1.0f || x0 > 1.0f || y1 < -1.0f || y0 > 1.0f)
					continue;
				const int tx0 = std::clamp((int)floorf((x0 * 0.5f + 0.5f) * kTilesX), 0, kTilesX - 1);
				const int tx1 = std::clamp((int)floorf((x1 * 0.5f + 0.5f) * kTilesX), 0, kTilesX - 1);
				const int ty0 = std::clamp((int)floorf((0.5f - y1 * 0.5f) * kTilesY), 0, kTilesY - 1);   // 화면 y 아래 +
				const int ty1 = std::clamp((int)floorf((0.5f - y0 * 0.5f) * kTilesY), 0, kTilesY - 1);
				for (int ty = ty0; ty <= ty1; ++ty)
					for (int tx = tx0; tx <= tx1; ++tx)
						pairs.push_back((uint32)(((s * kTilesY + ty) * kTilesX + tx) << 10) | (uint32)(index & 1023));
			}
			lo.First = (uint32)firstPair;
			lo.Count = (uint32)(pairs.size() - firstPair);
			lo.Kept = lo.Count > 0;   // 0 = 화면 밖
		}
		}
		}, "Forward+ Cluster Build");

		// 모으기: 빛 순서대로 번호를 매기고 짝의 번호 칸을 바꾼다 (차례로 지을 때와 같은 순서 · 같은 값)
		for (int i = 0; i < n; ++i)
		{
			const LightOut& lo = s_Out[(size_t)i];
			if (!lo.Kept)
			{
				++s_Stats.Culled;
				continue;
			}
			const int index = s_LightCount++;
			const std::vector<uint32>& pairs = s_BatchPairs[(size_t)(i / kLightBatch)];
			for (uint32 k = lo.First; k < lo.First + lo.Count; ++k)
				s_Pairs.push_back((pairs[k] & ~1023u) | (uint32)index);
			const AdditionalLight& l = lights[i];
			const float cosOuter = lo.CosOuter, cosInner = lo.CosInner;
			XMFLOAT4* d = &s_LightData[(size_t)index * 4];
			d[0] = XMFLOAT4(l.Position.x, l.Position.y, l.Position.z, l.Range);
			d[1] = XMFLOAT4(l.Color.x, l.Color.y, l.Color.z, (float)l.Type);
			Vec3 dir = l.Direction;
			if (dir.LengthSquared() > 1e-8f)
				dir.Normalize();
			d[2] = XMFLOAT4(dir.x, dir.y, dir.z, cosOuter);
			// 레이어 마스크는 16 비트씩 (float 에 정확히 담긴다)
			d[3] = XMFLOAT4(cosInner, (float)(l.Mask & 0xFFFFu), (float)(l.Mask >> 16), 0.0f);
		}
		s_Stats.Lights = s_LightCount;

		// 클러스터마다 개수 → 시작 번호 → 목록 (개수 세기 정렬)
		s_Packed.assign((size_t)kWidth * kRows, XMFLOAT4(0, 0, 0, 0));
		for (int i = 0; i < s_LightCount * 4; ++i)
			s_Packed[kLightBase + i] = s_LightData[i];
		std::vector<uint32> counts(kClusters, 0u);
		for (uint32 p : s_Pairs)
			++counts[p >> 10];
		uint32 offset = 0;
		std::vector<uint32> starts(kClusters, 0u);
		for (int c = 0; c < kClusters; ++c)
		{
			uint32 cnt = counts[c];
			if (offset + cnt > (uint32)kMaxIndices)
			{
				s_Stats.Dropped += (int)(offset + cnt - kMaxIndices);
				cnt = offset >= (uint32)kMaxIndices ? 0u : (uint32)kMaxIndices - offset;
				counts[c] = cnt;
			}
			starts[c] = offset;
			s_Packed[kGridBase + c] = XMFLOAT4((float)offset, (float)cnt, 0.0f, 0.0f);
			offset += cnt;
			if (cnt)
				++s_Stats.NonEmptyClusters;
			s_Stats.MaxPerCluster = (std::max)(s_Stats.MaxPerCluster, (int)cnt);
		}
		s_Stats.Indices = (int)offset;
		std::vector<uint32> fill(kClusters, 0u);
		for (uint32 p : s_Pairs)
		{
			const uint32 c = p >> 10;
			if (fill[c] >= counts[c])
				continue;
			const uint32 at = starts[c] + fill[c]++;
			(&s_Packed[kIndexBase + at / 4].x)[at % 4] = (float)(p & 1023u);
		}

		// 올리기: 쓴 줄만 (빛 · 클러스터 표 · 번호 목록)
		GfxContext* ctx = Gfx::Context();
		if (Ensure(s_Data, kWidth, kRows, DXGI_FORMAT_R32G32B32A32_FLOAT))
		{
			auto upload = [&](int firstTexel, int texels) {
				if (texels <= 0)
					return;
				const UINT r0 = (UINT)(firstTexel / kWidth), r1 = (UINT)((firstTexel + texels - 1) / kWidth) + 1;
				const D3D11_BOX box = { 0, r0, 0, (UINT)kWidth, r1, 1 };
				ctx->UpdateSubresource(s_Data.T.Get(), 0, &box, &s_Packed[(size_t)r0 * kWidth], kWidth * sizeof(XMFLOAT4), 0);
			};
			upload(kLightBase, s_LightCount * 4);
			upload(kGridBase, kClusters);
			upload(kIndexBase, ((int)offset + 3) / 4);
		}
		else
			s_LightCount = 0;   // 텍스처를 못 만들었다 — 셰이더는 클러스터 빛 없이

		s_Stats.BuildMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
	}

	void Bind(InstancedBasicEffect* fx)
	{
		if (fx)
			SetVars(fx->GetFX());
	}

	void BindFx(FxEffect* fx) { SetVars(fx); }

	const Stats& LastStats() { return s_Stats; }
	void SetEnabled(bool enabled) { s_Enabled = enabled; }
	bool Enabled() { return s_Enabled; }

	nlohmann::json Info()
	{
		const Stats& s = s_Stats;
		return {
			{ "enabled", s_Enabled }, { "grid", { kTilesX, kTilesY, kSlices } }, { "lights", s.Lights }, { "culled", s.Culled }, { "indices", s.Indices }, { "dropped", s.Dropped },
			{ "maxPerCluster", s.MaxPerCluster }, { "nonEmptyClusters", s.NonEmptyClusters }, { "buildMs", s.BuildMs },
			{ "maxLights", kMaxAdditionalLights }, { "mainLights", LIGHT_SIZE },
			{ "sceneAdditional", (int)LightManager::GetI()->GetEditorAdditionalLights().size() },
			{ "gameAdditional", (int)LightManager::GetI()->GetAdditionalLights().size() },
		};
	}

	void RegisterEditor()
	{
		CliServer::Register("forwardplus", "Forward+ clustered lights: {op: info | set, enabled} — last built view: lights, clusters, indices, build ms",
			[](const nlohmann::json& args, nlohmann::json& result, std::string&) {
				if (args.contains("enabled") && args["enabled"].is_boolean())
					s_Enabled = args["enabled"].get<bool>();
				result = Info();
				return true;
			});
	}
}
