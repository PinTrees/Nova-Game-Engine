#include "pch.h"
#include "ShadowRenderer.h"
#include "ShadowMap.h"
#include "Light.h"
#include "Transform.h"
#include "VolumeProfile.h"
#include "Effects.h"
#include "RenderManager.h"
#include "Profiler.h"

namespace
{
	const char* const kCascadeNames[4] = { "Cascade 0", "Cascade 1", "Cascade 2", "Cascade 3" };
	// NDC → 텍스처 좌표
	const XMMATRIX kToTex(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f);

	// 방향광 상자를 구 뒤(빛 쪽)로 더 늘리는 거리: 구 밖에 있는 높은 물체의 그림자도 담는다
	constexpr float kCasterPad = 100.0f;

	XMVECTOR SafeUp(FXMVECTOR dir)
	{
		return fabsf(XMVectorGetY(dir)) > 0.99f ? XMVectorSet(0, 0, 1, 0) : XMVectorSet(0, 1, 0, 0);
	}

	// Soft 필터는 한 텍셀보다 멀리 샘플하므로 바이어스를 키운다 (URP 와 같은 2.5 배)
	float BiasScale(int filter) { return filter > 0 ? 2.5f : 1.0f; }

	void ResolveBias(const Light& light, const ShadowRenderer::Settings& s, float& depth, float& normal)
	{
		Light& l = const_cast<Light&>(light);
		depth = l.UsesCustomShadowBias() ? l.GetShadowDepthBias() : s.DepthBias;
		normal = l.UsesCustomShadowBias() ? l.GetShadowNormalBias() : s.NormalBias;
	}

	int FilterOf(const Light& light, const ShadowRenderer::Settings& s)
	{
		return (light.SoftShadows() && s.SoftShadows) ? std::clamp(s.SoftQuality, 0, 2) + 1 : 0;
	}
}

namespace ShadowRenderer
{
	Settings Settings::FromStack(const VolumeStack& stack)
	{
		Settings s;
		const VolumeComponent* c = stack.Get("Shadows");
		if (c == nullptr)
			return s;
		static const uint32 kRes[] = { 512, 1024, 2048, 4096 };
		s.MaxDistance = (std::max)(c->F("maxDistance"), 0.0f);
		s.CascadeCount = std::clamp(c->I("cascadeCount"), 1, 4);
		s.Splits[0] = c->F("split1");
		s.Splits[1] = c->F("split2");
		s.Splits[2] = c->F("split3");
		s.LastBorder = std::clamp(c->F("lastBorder"), 0.0f, 1.0f);
		s.Resolution = kRes[std::clamp(c->I("resolution"), 0, 3)];
		s.DepthBias = c->F("depthBias");
		s.NormalBias = c->F("normalBias");
		s.SoftShadows = c->B("softShadows");
		s.SoftQuality = std::clamp(c->I("softQuality"), 0, 2);
		s.FarCascadeUpdate = std::clamp(c->I("farCascadeUpdate"), 0, 2);
		return s;
	}

	void Render(GfxContext* dc, ShadowMap& maps, const vector<shared_ptr<Light>>& sortedLights,
		int dirCount, int spotCount, int pointCount,
		const XMFLOAT3& eye, CXMMATRIX view, CXMMATRIX proj, const Settings& s,
		FrameData& out, const std::function<void()>& drawCasters)
	{
		auto fx = Effects::BuildShadowMapFX;
		fx->SetEyePosW(eye);
		dirCount = (std::min)(dirCount, LIGHT_SIZE);
		spotCount = (std::min)(spotCount, LIGHT_SIZE);
		pointCount = (std::min)(pointCount, LIGHT_SIZE);
		out.DirCount = dirCount;
		out.SpotCount = spotCount;
		out.PointCount = pointCount;
		for (int i = 0; i < LIGHT_SIZE; ++i)
			out.DirData[i] = out.SpotData[i] = out.PointData[i] = XMFLOAT4(0, 0, 0, 0);

		// ---- 캐스케이드 구: 절두체 조각 [n, f] 의 8 모서리를 감싸는 가장 작은 구 (시선 축 위)
		const int count = std::clamp(s.CascadeCount, 1, 4);
		const float maxDist = (std::max)(s.MaxDistance, 0.01f);
		float ends[4];
		float prev = 0.0f;
		for (int i = 0; i < count - 1; ++i)
		{
			ends[i] = std::clamp(s.Splits[i], prev + 0.001f, 1.0f);
			prev = ends[i];
		}
		for (int i = 0; i < count; ++i)
			ends[i] = (i == count - 1 ? 1.0f : ends[i]) * maxDist;

		const XMMATRIX invView = XMMatrixInverse(nullptr, view);
		const XMVECTOR camPos = invView.r[3];
		const XMVECTOR camFwd = XMVector3Normalize(invView.r[2]);
		XMFLOAT4X4 p;
		XMStoreFloat4x4(&p, proj);
		const float tanX = 1.0f / p._11, tanY = 1.0f / p._22;
		const float k2 = tanX * tanX + tanY * tanY;

		XMVECTOR centers[4];
		float radii[4];
		for (int i = 0; i < 4; ++i)
		{
			if (i >= count)
			{
				out.Spheres[i] = XMFLOAT4(0, 0, 0, -1.0f);   // 쓰지 않음
				continue;
			}
			const float n = i == 0 ? 0.0f : ends[i - 1];
			const float f = ends[i];
			float c = (f + n) * (1.0f + k2) * 0.5f;
			float r;
			if (c >= f) { c = f; r = f * sqrtf(k2); }
			else r = sqrtf((f - c) * (f - c) + f * f * k2);
			r = ceilf(r * 16.0f) / 16.0f;   // 부동소수 잡음으로 크기가 흔들리지 않게
			centers[i] = XMVectorAdd(camPos, XMVectorScale(camFwd, c));
			radii[i] = r;
			XMFLOAT3 cc;
			XMStoreFloat3(&cc, centers[i]);
			out.Spheres[i] = XMFLOAT4(cc.x, cc.y, cc.z, r * r);
		}
		// 설정이 바뀔 때만 한 번 기록 (진단용)
		{
			static float s_LastKey = -1.0f;
			const float key = count * 1000003.0f + maxDist * 101.0f + (float)s.Resolution + ends[0] * 7.0f + radii[0] * 13.0f;
			if (key != s_LastKey)
			{
				s_LastKey = key;
				EditorLog::Write("Shadow", "cascades=%d maxDistance=%.1f resolution=%u ends=[%.2f %.2f %.2f %.2f] radii=[%.2f %.2f %.2f %.2f] soft=%d quality=%d",
					count, maxDist, s.Resolution, ends[0], count > 1 ? ends[1] : 0.0f, count > 2 ? ends[2] : 0.0f, count > 3 ? ends[3] : 0.0f,
					radii[0], count > 1 ? radii[1] : 0.0f, count > 2 ? radii[2] : 0.0f, count > 3 ? radii[3] : 0.0f, s.SoftShadows ? 1 : 0, s.SoftQuality);
			}
		}
		const float fadeRange = maxDist * s.LastBorder;
		out.Params = XMFLOAT4((float)count, maxDist, maxDist - fadeRange, fadeRange > 0.0001f ? 1.0f / fadeRange : 1.0e6f);

		// ---- 먼 캐스케이드 캐시: 캐스케이드마다 이번 프레임에 다시 그릴지 (모든 방향광이 같이 정한다)
		//  가까운 두 캐스케이드는 매 프레임 (바람에 흔들리는 잎 그림자가 보인다), 3·4 번째만 몇 프레임마다 돌아가며.
		//  빛 방향·설정·맵이 바뀌거나 카메라가 구 반지름의 5% 넘게 움직이면 바로 다시 그린다
		++out.FrameCounter;
		out.CascadesDrawn = 0;
		bool redraw[4] = { true, true, true, true };
		{
			uint64_t key = (uint64_t)count * 1000003ull + (uint64_t)s.Resolution;
			auto mixF = [&](float f) { uint32_t u; memcpy(&u, &f, 4); key = key * 1099511628211ull ^ u; };
			mixF(maxDist); mixF(s.Splits[0]); mixF(s.Splits[1]); mixF(s.Splits[2]); mixF(s.DepthBias); mixF(s.NormalBias);
			key = key * 31 + (uint64_t)(s.SoftShadows ? 1 : 0) * 7 + (uint64_t)s.SoftQuality;
			const uint64_t f = out.FrameCounter;
			for (int i = 0; i < count; ++i)
			{
				int interval = 1, phase = 0;
				if (i >= 2 && count >= 3 && s.FarCascadeUpdate > 0)
				{
					interval = (i == 2 ? 2 : 4) * (s.FarCascadeUpdate == 2 ? 2 : 1);
					phase = i == 2 ? 0 : 1;   // 3·4 번째가 같은 프레임에 겹치지 않게
				}
				const auto& c = out.Cache[i];
				bool need = interval == 1 || !c.Valid || c.SettingsKey != key || (f % (uint64_t)interval) != (uint64_t)phase % interval;
				if (!need)
				{
					const float dx = c.Sphere.x - out.Spheres[i].x, dy = c.Sphere.y - out.Spheres[i].y, dz = c.Sphere.z - out.Spheres[i].z;
					const float moveLimit = 0.05f * 0.05f * out.Spheres[i].w;   // (5% 반지름)²
					need = fabsf(c.Sphere.w - out.Spheres[i].w) > 1e-4f || dx * dx + dy * dy + dz * dz > moveLimit;
				}
				for (int d = 0; d < dirCount && !need; ++d)
				{
					Light& light = *sortedLights[d];
					if (!light.CastsShadows())
						continue;
					XMFLOAT3 look;
					XMStoreFloat3(&look, XMVector3Normalize(light.GetGameObject()->GetTransform()->GetLook()));
					need = c.Lights[d] != &light || c.Generation[d] != maps.Generation(LightType::Directional, d) ||
						look.x * c.LightDir[d].x + look.y * c.LightDir[d].y + look.z * c.LightDir[d].z < 0.99999f;
				}
				redraw[i] = need;
				out.Cache[i].SettingsKey = key;
				if (!need)
					out.Spheres[i] = c.Sphere;   // 맵을 그린 때의 구로 캐스케이드를 고른다 (맵 밖을 읽지 않게)
			}
		}

		// ---- 방향광: 캐스케이드마다 정사영 맵
		for (int d = 0; d < dirCount; ++d)
		{
			Light& light = *sortedLights[d];
			if (!light.CastsShadows())
				continue;
			const int filter = FilterOf(light, s);
			out.DirData[d] = XMFLOAT4(light.GetShadowStrength(), (float)filter, 1.0f / (float)s.Resolution, 0);   // z = 1 / 맵 크기 (ShadowPCF)
			float depthBias, normalBias;
			ResolveBias(light, s, depthBias, normalBias);

			const XMVECTOR dir = XMVector3Normalize(light.GetGameObject()->GetTransform()->GetLook());
			const XMVECTOR up = SafeUp(dir);
			const XMMATRIX lightRot = XMMatrixLookToLH(XMVectorZero(), dir, up);
			const XMMATRIX invRot = XMMatrixInverse(nullptr, lightRot);
			XMFLOAT3 toLight;
			XMStoreFloat3(&toLight, XMVectorNegate(dir));
			fx->SetShadowLight(XMFLOAT4(toLight.x, toLight.y, toLight.z, 0.0f));

			XMFLOAT3 lookDir;
			XMStoreFloat3(&lookDir, dir);
			for (int i = 0; i < count; ++i)
			{
				auto& cache = out.Cache[i];
				if (!redraw[i])
				{
					out.Dir[d * 4 + i] = cache.Dir[d];   // 캐시한 맵 + 그린 때의 행렬
					continue;
				}
				const float r = radii[i];
				const float texel = 2.0f * r / (float)s.Resolution;
				// 구 중심을 빛 공간에서 텍셀 단위로 맞춘다 → 카메라가 움직여도 텍셀 격자가 그대로
				XMVECTOR ls = XMVector3TransformCoord(centers[i], lightRot);
				ls = XMVectorSet(floorf(XMVectorGetX(ls) / texel) * texel, floorf(XMVectorGetY(ls) / texel) * texel, XMVectorGetZ(ls), 1.0f);
				const XMVECTOR center = XMVector3TransformCoord(ls, invRot);
				const XMVECTOR lightEye = XMVectorSubtract(center, XMVectorScale(dir, r + kCasterPad));
				const XMMATRIX lightView = XMMatrixLookToLH(lightEye, dir, up);
				const XMMATRIX lightProj = XMMatrixOrthographicLH(2.0f * r, 2.0f * r, 0.0f, 2.0f * r + kCasterPad);
				const XMMATRIX vp = lightView * lightProj;
				out.Dir[d * 4 + i] = vp * kToTex;

				const float scale = BiasScale(filter);
				fx->SetShadowBias(depthBias * texel * scale, normalBias * texel * 1.4142136f * scale);
				fx->SetViewProj(vp);
				RenderManager::GetI()->LightViewProjection = vp;
				RenderManager::GetI()->ShadowTexelWorld = texel;
				maps.BindSlice(dc, LightType::Directional, d, i, s.Resolution);
				{
					PROFILE_GPU(kCascadeNames[i]);   // Profiler: 캐스케이드마다 GPU 시간·픽셀
					drawCasters();
				}
				cache.Dir[d] = out.Dir[d * 4 + i];
				cache.Lights[d] = &light;
				cache.LightDir[d] = lookDir;
				cache.Generation[d] = maps.Generation(LightType::Directional, d);
			}
		}
		for (int i = 0; i < count; ++i)
			if (redraw[i])
			{
				out.Cache[i].Valid = true;
				out.Cache[i].Sphere = out.Spheres[i];
				++out.CascadesDrawn;
			}

		// ---- 스포트광: 원근 맵 하나
		for (int k = 0; k < spotCount; ++k)
		{
			Light& light = *sortedLights[dirCount + k];
			if (!light.CastsShadows())
				continue;
			const int filter = FilterOf(light, s);
			out.SpotData[k] = XMFLOAT4(light.GetShadowStrength(), (float)filter, 1.0f / (float)s.Resolution, 0);
			float depthBias, normalBias;
			ResolveBias(light, s, depthBias, normalBias);

			Transform* t = light.GetGameObject()->GetTransform();
			const Vec3 pos = t->GetPosition();
			const XMVECTOR dir = XMVector3Normalize(t->GetLook());
			const float fov = XMConvertToRadians(std::clamp(light.GetSpotAngle(), 1.0f, 179.0f));
			const float nearZ = (std::max)(light.GetShadowNearPlane(), 0.01f);
			const float farZ = (std::max)(light.GetRange(), nearZ + 0.1f);
			const XMMATRIX vp = XMMatrixLookToLH(pos, dir, SafeUp(dir)) * XMMatrixPerspectiveFovLH(fov, 1.0f, nearZ, farZ);
			out.Spot[k] = vp * kToTex;

			const float texelPerDist = 2.0f * tanf(fov * 0.5f) / (float)s.Resolution;
			const float scale = BiasScale(filter);
			fx->SetShadowLight(XMFLOAT4(pos.x, pos.y, pos.z, 1.0f));
			fx->SetShadowBias(depthBias * texelPerDist * scale, normalBias * texelPerDist * 1.4142136f * scale);
			fx->SetViewProj(vp);
			RenderManager::GetI()->LightViewProjection = vp;
			RenderManager::GetI()->ShadowTexelWorld = 0.0f;
			maps.BindSlice(dc, LightType::Spot, k, 0, s.Resolution);
			drawCasters();
		}

		// ---- 점광: 큐브 6 면 (월드 축, 셰이더의 PointFace 와 같은 순서)
		static const XMVECTORF32 kFaceDir[6] = { { 1, 0, 0, 0 }, { -1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, -1, 0, 0 }, { 0, 0, 1, 0 }, { 0, 0, -1, 0 } };
		static const XMVECTORF32 kFaceUp[6] = { { 0, 1, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, -1, 0 }, { 0, 0, 1, 0 }, { 0, 1, 0, 0 }, { 0, 1, 0, 0 } };
		const uint32 pointRes = (std::max)(256u, s.Resolution / 2);
		for (int k = 0; k < pointCount; ++k)
		{
			Light& light = *sortedLights[dirCount + spotCount + k];
			if (!light.CastsShadows())
				continue;
			const int filter = FilterOf(light, s);
			out.PointData[k] = XMFLOAT4(light.GetShadowStrength(), (float)filter, 1.0f / (float)pointRes, 0);
			float depthBias, normalBias;
			ResolveBias(light, s, depthBias, normalBias);

			const Vec3 pos = light.GetGameObject()->GetTransform()->GetPosition();
			const float nearZ = (std::max)(light.GetShadowNearPlane(), 0.01f);
			const float farZ = (std::max)(light.GetRange(), nearZ + 0.1f);
			// 면 경계에서 필터가 밖(= 빛)을 읽지 않도록 90 도보다 몇 텍셀 넓게
			const float tanHalf = 1.0f + 4.0f / (float)pointRes;
			const float fov = 2.0f * atanf(tanHalf);
			const XMMATRIX faceProj = XMMatrixPerspectiveFovLH(fov, 1.0f, nearZ, farZ);
			const float texelPerDist = 2.0f * tanHalf / (float)pointRes;
			const float scale = BiasScale(filter);
			fx->SetShadowLight(XMFLOAT4(pos.x, pos.y, pos.z, 1.0f));
			fx->SetShadowBias(depthBias * texelPerDist * scale, normalBias * texelPerDist * 1.4142136f * scale);
			for (int f = 0; f < 6; ++f)
			{
				const XMMATRIX vp = XMMatrixLookToLH(pos, kFaceDir[f], kFaceUp[f]) * faceProj;
				out.Point[k * 6 + f] = vp * kToTex;
				fx->SetViewProj(vp);
				RenderManager::GetI()->LightViewProjection = vp;
				RenderManager::GetI()->ShadowTexelWorld = 0.0f;
				maps.BindSlice(dc, LightType::Point, k, f, pointRes);
				drawCasters();
			}
		}
	}

	void Bind(InstancedBasicEffect* fx, ShadowMap& maps, const FrameData& data)
	{
		fx->SetDirShadowMaps(maps.DepthMapSRVArray(LightType::Directional).data(), LIGHT_SIZE);
		fx->SetSpotShadowMaps(maps.DepthMapSRVArray(LightType::Spot).data(), LIGHT_SIZE);
		fx->SetPointShadowMaps(maps.DepthMapSRVArray(LightType::Point).data(), LIGHT_SIZE);
		fx->SetDirShadowTransforms(data.Dir, LIGHT_SIZE * 4);
		fx->SetSpotShadowTransforms(data.Spot, LIGHT_SIZE);
		fx->SetPointShadowTransforms(data.Point, LIGHT_MAX_SIZE);
		fx->SetCascadeSpheres(data.Spheres, 4);
		fx->SetShadowParams(data.Params);
		fx->SetDirShadowData(data.DirData, LIGHT_SIZE);
		fx->SetSpotShadowData(data.SpotData, LIGHT_SIZE);
		fx->SetPointShadowData(data.PointData, LIGHT_SIZE);
	}
}
