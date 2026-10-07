#include "pch.h"
#include "RenderingDebug.h"
#include "MotionVectors.h"
#include "ProbeVolumes.h"
#include "EditorLog.h"
#include "CliServer.h"

namespace RenderingDebug
{
	namespace
	{
		State s_State;
		ComPtr<FxEffect> s_Fx;
		bool s_LoadFailed = false;
		int s_Drawn[2] = {};   // 마지막으로 그린 모드 (0 Game, 1 Scene) — 정보

		const char* kNames[] = { "None", "Depth", "Normals (World)", "Ambient Occlusion", "Motion Vectors", "Probe Volume: Lighting", "Probe Volume: Sampling" };
		const char* kKeys[] = { "none", "depth", "normals", "ao", "motion", "apv", "apv-sampling" };

		FxVar* Var(const char* name)
		{
			FxVar* v = s_Fx ? s_Fx->GetVariableByName(name) : nullptr;
			return v && v->IsValid() ? v : nullptr;
		}

		bool Load()
		{
			if (s_Fx || s_LoadFailed)
				return s_Fx != nullptr;
			std::string error;
			s_Fx = FxEffect::Load(L"../Shaders/64. RenderingDebug.fx", error);
			if (!s_Fx)
			{
				s_LoadFailed = true;
				EditorLog::Write("RenderingDebug", "64. RenderingDebug.fx failed to load: %s", error.c_str());
			}
			return s_Fx != nullptr;
		}

		bool IsFullscreen(int mode) { return mode >= Depth && mode <= MotionVectors; }
		bool IsProbeVolume(int mode) { return mode == ProbeVolumeLighting || mode == ProbeVolumeSampling; }
	}

	const char* const* ModeNames() { return kNames; }
	const char* ModeKey(int mode) { return kKeys[std::clamp(mode, 0, (int)ModeCount - 1)]; }
	State& Get() { return s_State; }

	void SetMode(int mode)
	{
		mode = std::clamp(mode, 0, (int)ModeCount - 1);
		const int old = s_State.Mode;
		s_State.Mode = mode;
		// APV: 물체 셰이더의 진단 보기 (1 프로브 빛만, 2 섞은 방법). 다른 모드로 나가면 끈다 (CLI probevolume debug 로 켠 것은 그대로)
		if (IsProbeVolume(mode))
			ProbeVolumes::SetDebugView(mode == ProbeVolumeLighting ? 1 : 2);
		else if (IsProbeVolume(old))
			ProbeVolumes::SetDebugView(0);
	}

	bool NeedsMotionVectors() { return s_State.Mode == MotionVectors; }

	bool Draw(const Inputs& in, GfxRenderTargetView* target, const D3D11_VIEWPORT& viewport)
	{
		const int mode = s_State.Mode;
		s_Drawn[in.SceneView ? 1 : 0] = mode;
		if (!IsFullscreen(mode) || target == nullptr || in.NormalDepth == nullptr || !Load())
			return false;
		FxTechnique* tech = s_Fx->GetTechniqueByName("DebugTech");
		if (tech == nullptr || !tech->IsValid())
			return false;
		GfxContext* ctx = Gfx::Context();

		const float params[4] = { (float)mode, s_State.DepthRange, s_State.MotionScale, 0.0f };
		const float size[4] = { (float)in.Width, (float)in.Height, 0.0f, 0.0f };
		XMFLOAT4X4 invView;
		XMStoreFloat4x4(&invView, XMMatrixInverse(nullptr, XMLoadFloat4x4(&in.View)));
		if (FxVar* v = Var("gDebugParams")) v->SetFloatVector(params);
		if (FxVar* v = Var("gDebugSize")) v->SetFloatVector(size);
		if (FxVar* v = Var("gInvView")) v->SetMatrix(&invView._11);
		if (FxVar* v = Var("gNormalDepth")) v->SetResource(in.NormalDepth);
		if (FxVar* v = Var("gAo")) v->SetResource(in.Ao);
		if (FxVar* v = Var("gMotion")) v->SetResource(in.Motion);

		ctx->OMSetRenderTargets(1, &target, nullptr);
		ctx->RSSetViewports(1, &viewport);
		ctx->OMSetDepthStencilState(nullptr, 0);
		ctx->OMSetBlendState(nullptr, nullptr, 0xffffffff);
		ctx->RSSetState(nullptr);
		ctx->IASetInputLayout(nullptr);
		ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		tech->GetPassByIndex(0)->Apply(0, ctx);
		ctx->Draw(3, 0);

		// 묶은 텍스처를 푼다 (다음 패스가 같은 텍스처를 타깃으로 쓸 수 있게)
		if (FxVar* v = Var("gNormalDepth")) v->SetResource(nullptr);
		if (FxVar* v = Var("gAo")) v->SetResource(nullptr);
		if (FxVar* v = Var("gMotion")) v->SetResource(nullptr);
		tech->GetPassByIndex(0)->Apply(0, ctx);
		return true;
	}

	nlohmann::json Info()
	{
		const int mode = s_State.Mode;
		return {
			{ "mode", ModeKey(mode) }, { "modeName", kNames[mode] }, { "depthRange", s_State.DepthRange }, { "motionScale", s_State.MotionScale },
			{ "fullscreen", IsFullscreen(mode) }, { "probeVolumeDebugView", ProbeVolumes::DebugView() },
			{ "drawnGame", ModeKey(s_Drawn[0]) }, { "drawnScene", ModeKey(s_Drawn[1]) },
			{ "sceneMotionVectors", MotionVectors::Info(true) }, { "loaded", s_Fx != nullptr },
		};
	}

	void RegisterEditor()
	{
		CliServer::Register("debugview", "Rendering Debugger: {op: info | none | depth | normals | ao | motion | apv | apv-sampling, range, scale}",
			[](const nlohmann::json& args, nlohmann::json& result, std::string& error) {
				const std::string op = args.value("op", std::string("info"));
				if (op != "info")
				{
					int mode = -1;
					for (int i = 0; i < ModeCount; ++i)
						if (op == kKeys[i])
							mode = i;
					if (mode < 0)
					{
						error = "mode: none | depth | normals | ao | motion | apv | apv-sampling (or info)";
						return false;
					}
					SetMode(mode);
				}
				if (args.contains("range") && args["range"].is_number())
					s_State.DepthRange = (std::max)(0.01f, args["range"].get<float>());
				if (args.contains("scale") && args["scale"].is_number())
					s_State.MotionScale = (std::max)(0.0f, args["scale"].get<float>());
				result = Info();
				return true;
			});
	}
}
