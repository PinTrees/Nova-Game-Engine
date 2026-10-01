#include "pch.h"
#include "ParticleRenderer.h"
#include "ParticleSystem.h"
#include "ParticleTextures.h"
#include "Effects.h"

namespace
{
	// 43. Particle.fx 의 InstanceIn 과 같은 배치 (68 바이트)
	struct Instance
	{
		XMFLOAT3 Pos;
		float Rot;
		XMFLOAT2 Size;
		XMFLOAT3 Vel;
		XMFLOAT4 Color;
		XMFLOAT4 UV;
	};

	// 꼬리 정점 (43. Particle.fx 의 TrailIn)
	struct TrailVertex
	{
		XMFLOAT3 Pos;
		XMFLOAT2 UV;
		XMFLOAT4 Color;
	};

	struct Batch
	{
		ParticleSystem* System;
		UINT Start;
		UINT Count;
		UINT TrailStart = 0;
		UINT TrailCount = 0;   // 정점 수 (삼각형 목록)
		float Distance;
	};

	std::unique_ptr<Effect> s_Effect;
	bool s_Failed = false;
	ComPtr<ID3D11InputLayout> s_Layout;
	ComPtr<ID3D11Buffer> s_Buffer;
	UINT s_Capacity = 0;
	std::vector<Instance> s_Instances;
	ComPtr<ID3D11InputLayout> s_TrailLayout;
	ComPtr<ID3D11Buffer> s_TrailBuffer;
	UINT s_TrailCapacity = 0;
	std::vector<TrailVertex> s_TrailVertices;
	int s_LastDrawCalls = 0;
	int s_LastParticles = 0;

	bool Init()
	{
		if (s_Effect || s_Failed)
			return s_Effect != nullptr;
		auto device = Application::GetI()->GetDevice();
		s_Effect = std::make_unique<Effect>(device, L"../Shaders/43. Particle.fx");
		if (s_Effect->GetFX() == nullptr)
		{
			s_Effect.reset();
			s_Failed = true;
			EditorLog::Write("Particles", "43. Particle.fx failed to load - particles are not drawn");
			return false;
		}
		D3DX11_PASS_DESC pass = {};
		s_Effect->GetFX()->GetTechniqueByName("AlphaTech")->GetPassByIndex(0)->GetDesc(&pass);
		const D3D11_INPUT_ELEMENT_DESC desc[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
			{ "ROTATION", 0, DXGI_FORMAT_R32_FLOAT, 0, 12, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
			{ "SIZE", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
			{ "VELOCITY", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
			{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 36, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 52, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
		};
		if (FAILED(device->CreateInputLayout(desc, _countof(desc), pass.pIAInputSignature, pass.IAInputSignatureSize, s_Layout.GetAddressOf())))
		{
			s_Effect.reset();
			s_Failed = true;
			EditorLog::Write("Particles", "particle input layout failed");
			return false;
		}
		D3DX11_PASS_DESC trailPass = {};
		s_Effect->GetFX()->GetTechniqueByName("TrailAlphaTech")->GetPassByIndex(0)->GetDesc(&trailPass);
		const D3D11_INPUT_ELEMENT_DESC trailDesc[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};
		if (FAILED(device->CreateInputLayout(trailDesc, _countof(trailDesc), trailPass.pIAInputSignature, trailPass.IAInputSignatureSize, s_TrailLayout.GetAddressOf())))
			EditorLog::Write("Particles", "trail input layout failed - trails are not drawn");
		return true;
	}

	bool EnsureTrailBuffer(UINT count)
	{
		if (count <= s_TrailCapacity && s_TrailBuffer)
			return true;
		s_TrailCapacity = (std::max)(count, (std::max)(s_TrailCapacity * 2u, 4096u));
		D3D11_BUFFER_DESC bd = {};
		bd.ByteWidth = s_TrailCapacity * sizeof(TrailVertex);
		bd.Usage = D3D11_USAGE_DYNAMIC;
		bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		s_TrailBuffer.Reset();
		return SUCCEEDED(Application::GetI()->GetDevice()->CreateBuffer(&bd, nullptr, s_TrailBuffer.GetAddressOf()));
	}

	// 한 시스템의 꼬리를 카메라를 향한 띠(삼각형 목록)로. 머리(입자 위치)에서 꼬리 끝으로 u = 0 → 1
	void BuildTrails(ParticleSystem* ps, const Matrix& toWorld, float sizeScale, const Vec3& camPos)
	{
		const bool world = ps->TrailsInWorld();
		std::vector<Vec3> pts;
		for (const ParticleSystem::Trail& tr : ps->Trails())
		{
			if (!tr.Used || tr.Points.empty())
				continue;
			pts.clear();
			auto toW = [&](const Vec3& p) { return world ? p : Vec3::Transform(p, toWorld); };
			pts.push_back(toW(tr.Head));
			for (int i = (int)tr.Points.size() - 1; i >= 0; --i)
			{
				const Vec3 p = toW(tr.Points[i].Position);
				if ((p - pts.back()).LengthSquared() > 1e-8f)
					pts.push_back(p);
			}
			if (pts.size() < 2)
				continue;
			const int n = (int)pts.size();
			const float width = tr.Width * (world ? 1.0f : sizeScale);
			std::vector<Vec3> left(n), right(n);
			std::vector<XMFLOAT4> colors(n);
			for (int i = 0; i < n; ++i)
			{
				const float u = (float)i / (n - 1);
				Vec3 dir = pts[(std::min)(i + 1, n - 1)] - pts[(std::max)(i - 1, 0)];
				Vec3 side = dir.Cross(camPos - pts[i]);
				if (side.LengthSquared() < 1e-12f)
					side = Vec3(0, 1, 0).Cross(dir);
				side.Normalize();
				const float w = (std::max)(0.0f, width * ps->TrailWidthOverTrail.Evaluate(u, 0.5f)) * 0.5f;
				left[i] = pts[i] - side * w;
				right[i] = pts[i] + side * w;
				const Vec4 c = ps->TrailColorOverTrail.Evaluate(u, 0.5f);
				colors[i] = XMFLOAT4(tr.Color.x * c.x, tr.Color.y * c.y, tr.Color.z * c.z, tr.Color.w * c.w);
			}
			for (int i = 0; i + 1 < n; ++i)
			{
				const float u0 = (float)i / (n - 1), u1 = (float)(i + 1) / (n - 1);
				const TrailVertex a{ XMFLOAT3(left[i].x, left[i].y, left[i].z), XMFLOAT2(u0, 0), colors[i] };
				const TrailVertex b{ XMFLOAT3(right[i].x, right[i].y, right[i].z), XMFLOAT2(u0, 1), colors[i] };
				const TrailVertex c{ XMFLOAT3(left[i + 1].x, left[i + 1].y, left[i + 1].z), XMFLOAT2(u1, 0), colors[i + 1] };
				const TrailVertex d{ XMFLOAT3(right[i + 1].x, right[i + 1].y, right[i + 1].z), XMFLOAT2(u1, 1), colors[i + 1] };
				s_TrailVertices.insert(s_TrailVertices.end(), { a, b, c, b, d, c });
			}
		}
	}

	bool EnsureBuffer(UINT count)
	{
		if (count <= s_Capacity && s_Buffer)
			return true;
		s_Capacity = (std::max)(count, (std::max)(s_Capacity * 2u, 1024u));
		D3D11_BUFFER_DESC bd = {};
		bd.ByteWidth = s_Capacity * sizeof(Instance);
		bd.Usage = D3D11_USAGE_DYNAMIC;
		bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		s_Buffer.Reset();
		return SUCCEEDED(Application::GetI()->GetDevice()->CreateBuffer(&bd, nullptr, s_Buffer.GetAddressOf()));
	}
}

namespace ParticleRenderer
{
	int LastDrawCalls() { return s_LastDrawCalls; }
	int LastParticleCount() { return s_LastParticles; }

	void Render(const Matrix& view, const Matrix& proj, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv)
	{
		s_LastDrawCalls = 0;
		s_LastParticles = 0;
		if (rtv == nullptr)
			return;
		Scene* scene = SceneManager::GetI()->GetCurrentScene();
		if (scene == nullptr)
			return;

		// 카메라 축 (뷰 행렬의 역 = 카메라의 월드 변환, 행 벡터)
		const Matrix camWorld = view.Invert();
		const Vec3 camRight(camWorld._11, camWorld._12, camWorld._13);
		const Vec3 camUp(camWorld._21, camWorld._22, camWorld._23);
		const Vec3 camForward(camWorld._31, camWorld._32, camWorld._33);
		const Vec3 camPos(camWorld._41, camWorld._42, camWorld._43);

		std::vector<Batch> batches;
		s_Instances.clear();
		s_TrailVertices.clear();
		for (ParticleSystem* ps : ParticleSystem::All())
		{
			// 입자가 없어도 남은 꼬리(Die with Particles 꺼짐)는 그린다
			if ((ps->ParticleCount() == 0 && ps->Trails().empty()) || !ps->RendererEnabled || !ps->ActiveInHierarchy())
				continue;
			const Matrix toWorld = ps->SimulationToWorld();
			// Local 공간: 오브젝트 크기도 입자 크기에 곱한다 (Unity 의 Scaling Mode = Local 과 비슷하게)
			float sizeScale = 1.0f;
			if (ps->SimulationSpace == 0)
				sizeScale = Vec3(toWorld._11, toWorld._12, toWorld._13).Length();
			const int tilesX = ps->SheetEnabled ? (std::max)(1, ps->SheetTilesX) : 1;
			const int tilesY = ps->SheetEnabled ? (std::max)(1, ps->SheetTilesY) : 1;

			Batch b;
			b.System = ps;
			b.Start = (UINT)s_Instances.size();
			b.Count = 0;
			const Vec3 origin = ps->GetGameObject()->GetTransform()->GetPosition();
			b.Distance = (origin - camPos).Dot(camForward) - ps->SortingFudge;

			const auto& particles = ps->Particles();
			std::vector<uint32> order(particles.size());
			for (uint32 i = 0; i < order.size(); ++i)
				order[i] = i;
			std::vector<float> depth;
			if (ps->Blend == ParticleSystem::BlendMode::AlphaBlended && ps->Sort != ParticleSystem::SortMode::None)
			{
				depth.resize(particles.size());
				for (size_t i = 0; i < particles.size(); ++i)
				{
					switch (ps->Sort)
					{
					case ParticleSystem::SortMode::ByDistance: depth[i] = (Vec3::Transform(particles[i].Position, toWorld) - camPos).Dot(camForward); break;
					case ParticleSystem::SortMode::OldestInFront: depth[i] = -particles[i].Age; break;   // 나중에 그린 것이 앞 → 나이 많은 것을 마지막에
					default: depth[i] = particles[i].Age; break;
					}
				}
				std::sort(order.begin(), order.end(), [&](uint32 a, uint32 b) { return depth[a] > depth[b]; });
			}

			for (uint32 idx : order)
			{
				const ParticleSystem::Particle& p = particles[idx];
				if (p.Size <= 0.0f || p.Color.w <= 0.0f)
					continue;
				Instance inst;
				const Vec3 wp = Vec3::Transform(p.Position, toWorld);
				const Vec3 wv = Vec3::TransformNormal(p.Velocity + p.AnimatedVelocity, toWorld);
				inst.Pos = XMFLOAT3(wp.x, wp.y, wp.z);
				inst.Rot = p.Rotation * (3.14159265f / 180.0f);
				inst.Size = XMFLOAT2(p.Size * sizeScale, p.Size * sizeScale);
				inst.Vel = XMFLOAT3(wv.x, wv.y, wv.z);
				inst.Color = XMFLOAT4(p.Color.x, p.Color.y, p.Color.z, p.Color.w);
				if (ps->SheetEnabled && tilesX * tilesY > 1)
				{
					const int frame = (int)p.SheetFrame;
					int col, row;
					if (ps->SheetAnimation == 1)
					{
						col = frame % tilesX;
						row = std::clamp(p.SheetRow, 0, tilesY - 1);
					}
					else
					{
						col = frame % tilesX;
						row = (frame / tilesX) % tilesY;
					}
					inst.UV = XMFLOAT4((float)col / tilesX, (float)row / tilesY, (float)(col + 1) / tilesX, (float)(row + 1) / tilesY);
				}
				else
					inst.UV = XMFLOAT4(0, 0, 1, 1);
				s_Instances.push_back(inst);
				++b.Count;
			}
			b.TrailStart = (UINT)s_TrailVertices.size();
			if (!ps->Trails().empty())
				BuildTrails(ps, toWorld, sizeScale, camPos);
			b.TrailCount = (UINT)s_TrailVertices.size() - b.TrailStart;
			if (b.Count > 0 || b.TrailCount > 0)
				batches.push_back(b);
		}
		if (batches.empty() || !Init() || !EnsureBuffer((UINT)(std::max)((size_t)1, s_Instances.size())))
			return;
		const bool drawTrails = !s_TrailVertices.empty() && s_TrailLayout && EnsureTrailBuffer((UINT)s_TrailVertices.size());

		// 먼 시스템부터 (투명 물체끼리의 순서)
		std::stable_sort(batches.begin(), batches.end(), [](const Batch& a, const Batch& b) { return a.Distance > b.Distance; });

		auto ctx = Application::GetI()->GetDeviceContext();
		D3D11_MAPPED_SUBRESOURCE mapped;
		if (FAILED(ctx->Map(s_Buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
			return;
		memcpy(mapped.pData, s_Instances.data(), s_Instances.size() * sizeof(Instance));
		ctx->Unmap(s_Buffer.Get(), 0);
		if (drawTrails && SUCCEEDED(ctx->Map(s_TrailBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
		{
			memcpy(mapped.pData, s_TrailVertices.data(), s_TrailVertices.size() * sizeof(TrailVertex));
			ctx->Unmap(s_TrailBuffer.Get(), 0);
		}

		ID3D11RenderTargetView* rtvs[1] = { rtv };
		ctx->OMSetRenderTargets(1, rtvs, dsv);
		ctx->IASetInputLayout(s_Layout.Get());
		ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		const UINT stride = sizeof(Instance), offset = 0;
		ID3D11Buffer* vb = s_Buffer.Get();
		ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);

		FxEffect* fx = s_Effect->GetFX();
		XMFLOAT4X4 vp;
		XMStoreFloat4x4(&vp, view * proj);
		fx->GetVariableByName("gViewProj")->AsMatrix()->SetMatrix(&vp._11);
		fx->GetVariableByName("gCamRight")->AsVector()->SetFloatVector(&camRight.x);
		fx->GetVariableByName("gCamUp")->AsVector()->SetFloatVector(&camUp.x);
		fx->GetVariableByName("gCamPos")->AsVector()->SetFloatVector(&camPos.x);
		FxVar* texVar = fx->GetVariableByName("gTexture")->AsShaderResource();
		FxVar* modeVar = fx->GetVariableByName("gRenderMode")->AsScalar();
		FxVar* speedVar = fx->GetVariableByName("gSpeedScale")->AsScalar();
		FxVar* lengthVar = fx->GetVariableByName("gLengthScale")->AsScalar();
		FxPass* alphaPass = fx->GetTechniqueByName("AlphaTech")->GetPassByIndex(0);
		FxPass* addPass = fx->GetTechniqueByName("AdditiveTech")->GetPassByIndex(0);
		FxPass* trailAlphaPass = fx->GetTechniqueByName("TrailAlphaTech")->GetPassByIndex(0);
		FxPass* trailAddPass = fx->GetTechniqueByName("TrailAdditiveTech")->GetPassByIndex(0);

		for (const Batch& b : batches)
		{
			ParticleSystem* ps = b.System;
			// 꼬리 먼저 (입자가 꼬리 위에 보이도록)
			if (drawTrails && b.TrailCount > 0)
			{
				const UINT tstride = sizeof(TrailVertex), toffset = 0;
				ID3D11Buffer* tvb = s_TrailBuffer.Get();
				ctx->IASetInputLayout(s_TrailLayout.Get());
				ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				ctx->IASetVertexBuffers(0, 1, &tvb, &tstride, &toffset);
				texVar->SetResource(ParticleTextures::Get(ps->TrailTexture));
				(ps->Blend == ParticleSystem::BlendMode::Additive ? trailAddPass : trailAlphaPass)->Apply(0, ctx);
				ctx->Draw(b.TrailCount, b.TrailStart);
				++s_LastDrawCalls;
				ctx->IASetInputLayout(s_Layout.Get());
				ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
				ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
			}
			if (b.Count == 0)
				continue;
			texVar->SetResource(ParticleTextures::Get(ps->Texture));
			modeVar->SetInt((int)ps->Render);
			speedVar->SetFloat(ps->SpeedScale);
			lengthVar->SetFloat(ps->LengthScale);
			(ps->Blend == ParticleSystem::BlendMode::Additive ? addPass : alphaPass)->Apply(0, ctx);
			ctx->DrawInstanced(4, b.Count, 0, b.Start);
			++s_LastDrawCalls;
			s_LastParticles += (int)b.Count;
		}
		texVar->SetResource(nullptr);
		ID3D11ShaderResourceView* nullSRV[1] = {};
		ctx->PSSetShaderResources(0, 1, nullSRV);
		ctx->OMSetBlendState(nullptr, nullptr, 0xffffffff);
		ctx->OMSetDepthStencilState(nullptr, 0);
		ctx->RSSetState(nullptr);
		ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	}
}
