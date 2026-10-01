#include "pch.h"
#include "SceneGrid.h"
#include "Effects.h"

namespace
{
	std::unique_ptr<Effect> s_Effect;
	bool s_Failed = false;

	bool Init()
	{
		if (s_Effect || s_Failed)
			return s_Effect != nullptr;
		s_Effect = std::make_unique<Effect>(Application::GetI()->GetDevice(), L"../Shaders/45. SceneGrid.fx");
		if (s_Effect->GetFX() == nullptr || !s_Effect->GetFX()->GetTechniqueByName("GridTech")->IsValid())
		{
			s_Effect.reset();
			s_Failed = true;
			EditorLog::Write("Grid", "45. SceneGrid.fx failed to load - grid is not drawn");
			return false;
		}
		return true;
	}
}

namespace SceneGrid
{
	void Draw(ID3D11DeviceContext* dc, CXMMATRIX viewProj, const XMFLOAT3& cameraPos)
	{
		if (!Init())
			return;
		FxEffect* fx = s_Effect->GetFX();
		fx->GetVariableByName("gViewProj")->AsMatrix()->SetMatrix(reinterpret_cast<const float*>(&viewProj));
		// 예전 ImGui 격자와 같은 모양: 색 (92, 98, 106), 알파 0.55 / 0.85, 70 유닛에서 사라짐
		const XMFLOAT4 cam(cameraPos.x, cameraPos.y, cameraPos.z, 80.0f);
		const XMFLOAT4 color(92.0f / 255.0f, 98.0f / 255.0f, 106.0f / 255.0f, 1.0f);
		const XMFLOAT4 params(0.55f, 0.85f, 70.0f, 0.0f);
		fx->GetVariableByName("gCameraPos")->AsVector()->SetFloatVector(&cam.x);
		fx->GetVariableByName("gGridColor")->AsVector()->SetFloatVector(&color.x);
		fx->GetVariableByName("gGridParams")->AsVector()->SetFloatVector(&params.x);

		ComPtr<ID3D11BlendState> prevBlend;
		float prevFactor[4];
		UINT prevMask = 0;
		dc->OMGetBlendState(prevBlend.GetAddressOf(), prevFactor, &prevMask);
		ComPtr<ID3D11DepthStencilState> prevDSS;
		UINT prevRef = 0;
		dc->OMGetDepthStencilState(prevDSS.GetAddressOf(), &prevRef);
		ComPtr<ID3D11RasterizerState> prevRS;
		dc->RSGetState(prevRS.GetAddressOf());

		dc->IASetInputLayout(nullptr);
		ID3D11Buffer* nullVB = nullptr;
		UINT zero = 0;
		dc->IASetVertexBuffers(0, 1, &nullVB, &zero, &zero);
		dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		fx->GetTechniqueByName("GridTech")->GetPassByIndex(0)->Apply(0, dc);
		dc->Draw(6, 0);

		dc->OMSetBlendState(prevBlend.Get(), prevFactor, prevMask);
		dc->OMSetDepthStencilState(prevDSS.Get(), prevRef);
		dc->RSSetState(prevRS.Get());
	}
}
