#include "pch.h"
#include "Tree.h"
#include "Transform.h"
#include "Effects.h"
#include "RenderManager.h"
#include "UMaterial.h"
#include "UnityGUI.h"
#include "TreeTextures.h"
#include <chrono>
#include <map>

float Tree::s_WindTime = 0.0f;

// ================================================================== GPU 메시 (같은 모양은 같이 쓴다)
struct Tree::GpuMesh
{
	ComPtr<ID3D11Buffer> VB, IB;
	uint32 BarkIndexCount = 0, LeafIndexCount = 0;
	int VertexCount = 0, BranchCount = 0, LeafCardCount = 0;
	Vec3 BoundsMin, BoundsMax;
	std::vector<XMFLOAT3> Positions;   // 선택용 (바람 전 위치)
	std::vector<uint32_t> Indices;
};

namespace
{
	std::map<std::string, std::weak_ptr<Tree::GpuMesh>>& MeshCache()
	{
		static std::map<std::string, std::weak_ptr<Tree::GpuMesh>> cache;
		return cache;
	}

	const D3D11_INPUT_ELEMENT_DESC kTreeLayout[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "WIND", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "AXIS", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "PHASE", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 64, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};
	static_assert(sizeof(TreeVertex) == 80, "TreeVertex 배치가 셰이더 TreeVertexIn 과 달라졌다");

	// ---- 효과 변수 (이름으로 찾아 둔다)
	struct TreeVars
	{
		ID3DX11EffectTechnique* Bark = nullptr;
		ID3DX11EffectTechnique* Leaf = nullptr;
		ID3DX11EffectMatrixVariable* World = nullptr;
		ID3DX11EffectMatrixVariable* WorldInvTranspose = nullptr;
		ID3DX11EffectVectorVariable* Wind = nullptr;
		ID3DX11EffectVectorVariable* WindParams = nullptr;
		ID3DX11EffectVectorVariable* BarkColor = nullptr;
		ID3DX11EffectVectorVariable* BarkParams = nullptr;
		ID3DX11EffectVectorVariable* MossColor = nullptr;
		ID3DX11EffectVectorVariable* LeafColor = nullptr;
		ID3DX11EffectVectorVariable* LeafColor2 = nullptr;
		ID3DX11EffectVectorVariable* LeafParams = nullptr;
		ID3DX11EffectShaderResourceVariable* LeafTex = nullptr;
		ID3DX11EffectShaderResourceVariable* BarkTex = nullptr;

		void Bind(ID3DX11Effect* fx, const char* bark, const char* leaf)
		{
			Bark = fx->GetTechniqueByName(bark);
			Leaf = fx->GetTechniqueByName(leaf);
			World = fx->GetVariableByName("gTreeWorld")->AsMatrix();
			WorldInvTranspose = fx->GetVariableByName("gTreeWorldInvTranspose")->AsMatrix();
			Wind = fx->GetVariableByName("gTreeWind")->AsVector();
			WindParams = fx->GetVariableByName("gTreeWindParams")->AsVector();
			BarkColor = fx->GetVariableByName("gTreeBarkColor")->AsVector();
			BarkParams = fx->GetVariableByName("gTreeBarkParams")->AsVector();
			MossColor = fx->GetVariableByName("gTreeMossColor")->AsVector();
			LeafColor = fx->GetVariableByName("gTreeLeafColor")->AsVector();
			LeafColor2 = fx->GetVariableByName("gTreeLeafColor2")->AsVector();
			LeafParams = fx->GetVariableByName("gTreeLeafParams")->AsVector();
			LeafTex = fx->GetVariableByName("gTreeLeafTex")->AsShaderResource();
			BarkTex = fx->GetVariableByName("gTreeBarkTex")->AsShaderResource();
		}
		bool Valid() const { return Bark && Bark->IsValid() && Leaf && Leaf->IsValid(); }
	};

	enum { kMain, kShadow, kNormalDepth };
	TreeVars& Vars(int pass)
	{
		static TreeVars vars[3];
		static bool bound = false;
		if (!bound)
		{
			bound = true;
			vars[kMain].Bind(Effects::InstancedBasicFX->GetFX(), "TreeBarkTech", "TreeLeafTech");
			vars[kShadow].Bind(Effects::BuildShadowMapFX->GetFX(), "TreeShadowBarkTech", "TreeShadowLeafTech");
			vars[kNormalDepth].Bind(Effects::SsaoNormalDepthFX->GetFX(), "TreeNormalDepthBarkTech", "TreeNormalDepthLeafTech");
			if (!vars[kMain].Valid() || !vars[kShadow].Valid() || !vars[kNormalDepth].Valid())
				EditorLog::Write("Tree", "tree techniques missing (main %d, shadow %d, normalDepth %d)", vars[kMain].Valid(), vars[kShadow].Valid(), vars[kNormalDepth].Valid());
		}
		return vars[pass];
	}

	ID3D11InputLayout* TreeInputLayout()
	{
		static ComPtr<ID3D11InputLayout> layout;
		static bool tried = false;
		if (!tried)
		{
			tried = true;
			TreeVars& v = Vars(kMain);
			if (v.Valid())
			{
				D3DX11_PASS_DESC pd;
				v.Bark->GetPassByIndex(0)->GetDesc(&pd);
				const HRESULT hr = Application::GetI()->GetDevice()->CreateInputLayout(kTreeLayout, _countof(kTreeLayout), pd.pIAInputSignature, pd.IAInputSignatureSize, layout.GetAddressOf());
				if (FAILED(hr))
					EditorLog::Write("Tree", "input layout failed hr=0x%08X", (unsigned)hr);
			}
		}
		return layout.Get();
	}

	void SetMatrix(ID3DX11Effect* fx, const char* name, CXMMATRIX m)
	{
		if (auto* v = fx->GetVariableByName(name)->AsMatrix(); v && v->IsValid())
			v->SetMatrix(reinterpret_cast<const float*>(&m));
	}

	void SetVec(ID3DX11EffectVectorVariable* v, const XMFLOAT4& f)
	{
		if (v && v->IsValid())
			v->SetFloatVector(reinterpret_cast<const float*>(&f));
	}

	float Hash2(float x, float z)
	{
		const float h = sinf(x * 12.9898f + z * 78.233f) * 43758.5453f;
		return h - floorf(h);
	}
}

// ================================================================== 컴포넌트
Tree::Tree()
{
	m_InspectorTitleName = "Tree";
}

Tree::~Tree()
{
}

void Tree::UpdateAll()
{
	static const auto start = std::chrono::steady_clock::now();
	s_WindTime = std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
}

void Tree::ApplyPreset(int preset)
{
	Preset = preset;
	Params.ApplyPreset(preset);
	switch (preset)
	{
	default:
	case 0:   // Oak
		BarkColor = { 0.36f, 0.29f, 0.23f, 1 }; MossColor = { 0.33f, 0.42f, 0.16f, 1 }; Moss = 0.35f;
		RidgeDepth = 0.6f; RidgeFrequency = 16.0f; BarkFleck = 0.2f;
		LeafColor = { 0.24f, 0.42f, 0.11f, 1 }; LeafColor2 = { 0.45f, 0.53f, 0.13f, 1 }; LeafVariation = 0.6f; LeafTransmission = 0.6f; LeafLength = 0.34f;
		break;
	case 1:   // Pine
		BarkColor = { 0.38f, 0.25f, 0.18f, 1 }; MossColor = { 0.3f, 0.36f, 0.16f, 1 }; Moss = 0.12f;
		RidgeDepth = 0.8f; RidgeFrequency = 10.0f; BarkFleck = 0.1f;
		LeafColor = { 0.12f, 0.26f, 0.11f, 1 }; LeafColor2 = { 0.2f, 0.33f, 0.13f, 1 }; LeafVariation = 0.35f; LeafTransmission = 0.3f; LeafLength = 0.3f;
		break;
	case 2:   // Birch: 흰 수피 + 어두운 반점
		BarkColor = { 0.86f, 0.84f, 0.79f, 1 }; MossColor = { 0.4f, 0.45f, 0.2f, 1 }; Moss = 0.05f;
		RidgeDepth = 0.15f; RidgeFrequency = 14.0f; BarkFleck = -0.7f;
		LeafColor = { 0.4f, 0.58f, 0.16f, 1 }; LeafColor2 = { 0.62f, 0.66f, 0.2f, 1 }; LeafVariation = 0.5f; LeafTransmission = 0.75f; LeafLength = 0.3f;
		break;
	case 3:   // Bush
		BarkColor = { 0.35f, 0.28f, 0.22f, 1 }; MossColor = { 0.33f, 0.42f, 0.16f, 1 }; Moss = 0.1f;
		RidgeDepth = 0.3f; RidgeFrequency = 24.0f; BarkFleck = 0.1f;
		LeafColor = { 0.2f, 0.38f, 0.11f, 1 }; LeafColor2 = { 0.34f, 0.48f, 0.13f, 1 }; LeafVariation = 0.5f; LeafTransmission = 0.55f; LeafLength = 0.36f;
		break;
	}
	m_Dirty = true;
}

void Tree::EnsureMesh()
{
	if (!m_Dirty && m_Mesh)
		return;
	m_Dirty = false;
	const std::string key = Params.ToJson().dump();
	if (m_Mesh && key == m_MeshKey)
		return;
	m_MeshKey = key;

	auto& cache = MeshCache();
	if (auto it = cache.find(key); it != cache.end())
		if (auto shared = it->second.lock())
		{
			m_Mesh = shared;
			return;
		}

	const auto t0 = std::chrono::steady_clock::now();
	TreeMeshData data;
	TreeGenerator::Generate(Params, data);
	auto mesh = std::make_shared<GpuMesh>();
	mesh->BarkIndexCount = data.BarkIndexCount;
	mesh->LeafIndexCount = data.LeafIndexCount;
	mesh->VertexCount = (int)data.Vertices.size();
	mesh->BranchCount = data.BranchCount;
	mesh->LeafCardCount = data.LeafCardCount;
	mesh->BoundsMin = Vec3(data.BoundsMin.x, data.BoundsMin.y, data.BoundsMin.z);
	mesh->BoundsMax = Vec3(data.BoundsMax.x, data.BoundsMax.y, data.BoundsMax.z);
	mesh->Positions.reserve(data.Vertices.size());
	for (const TreeVertex& v : data.Vertices)
		mesh->Positions.push_back(v.Pos);
	mesh->Indices = data.Indices;

	if (!data.Vertices.empty() && !data.Indices.empty())
	{
		auto device = Application::GetI()->GetDevice();
		D3D11_BUFFER_DESC bd = {};
		bd.Usage = D3D11_USAGE_IMMUTABLE;
		bd.ByteWidth = (UINT)(sizeof(TreeVertex) * data.Vertices.size());
		bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		D3D11_SUBRESOURCE_DATA init = { data.Vertices.data(), 0, 0 };
		device->CreateBuffer(&bd, &init, mesh->VB.GetAddressOf());
		bd.ByteWidth = (UINT)(sizeof(uint32_t) * data.Indices.size());
		bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
		init.pSysMem = data.Indices.data();
		device->CreateBuffer(&bd, &init, mesh->IB.GetAddressOf());
	}
	const float ms = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
	EditorLog::Write("Tree", "generated seed %d: %d vertices, %u triangles, %d branches, %d leaf cards (%.1f ms)",
		Params.Seed, mesh->VertexCount, (unsigned)(data.Indices.size() / 3), mesh->BranchCount, mesh->LeafCardCount, ms);
	cache[key] = mesh;
	m_Mesh = mesh;
}

bool Tree::GetLocalBounds(Vec3& bmin, Vec3& bmax)
{
	EnsureMesh();
	if (!m_Mesh || m_Mesh->VertexCount == 0)
		return false;
	bmin = m_Mesh->BoundsMin;
	bmax = m_Mesh->BoundsMax;
	return true;
}

bool Tree::RaycastLocal(const Vec3& o, const Vec3& r, float& bestT)
{
	EnsureMesh();
	if (!m_Mesh)
		return false;
	bool hit = false;
	const auto& P = m_Mesh->Positions;
	const auto& I = m_Mesh->Indices;
	for (size_t n = 0; n + 3 <= I.size(); n += 3)
	{
		const Vec3 a(P[I[n]]), b(P[I[n + 1]]), c(P[I[n + 2]]);
		const Vec3 e1 = b - a, e2 = c - a;
		const Vec3 p = r.Cross(e2);
		const float det = e1.Dot(p);
		if (fabsf(det) < 1e-12f)
			continue;
		const float inv = 1.0f / det;
		const Vec3 s = o - a;
		const float u = s.Dot(p) * inv;
		if (u < 0.0f || u > 1.0f)
			continue;
		const Vec3 q = s.Cross(e1);
		const float v = r.Dot(q) * inv;
		if (v < 0.0f || u + v > 1.0f)
			continue;
		const float t = e2.Dot(q) * inv;
		if (t > 0.0f && t < bestT)
		{
			bestT = t;
			hit = true;
		}
	}
	return hit;
}

int Tree::VertexCount() { EnsureMesh(); return m_Mesh ? m_Mesh->VertexCount : 0; }
int Tree::TriangleCount() { EnsureMesh(); return m_Mesh ? (int)((m_Mesh->BarkIndexCount + m_Mesh->LeafIndexCount) / 3) : 0; }

// ================================================================== 그리기
void Tree::DrawPass(Pass pass, bool editor)
{
	if (!m_Enabled || (m_pGameObject && !m_pGameObject->IsActive()))
		return;
	if (pass == Pass::Shadow && !CastShadows)
		return;
	EnsureMesh();
	if (!m_Mesh || !m_Mesh->VB || !m_Mesh->IB)
		return;
	const int passIndex = pass == Pass::Main ? kMain : (pass == Pass::Shadow ? kShadow : kNormalDepth);
	TreeVars& v = Vars(passIndex);
	ID3D11InputLayout* layout = TreeInputLayout();
	if (!v.Valid() || layout == nullptr)
		return;

	ID3DX11Effect* fx = pass == Pass::Main ? Effects::InstancedBasicFX->GetFX()
		: (pass == Pass::Shadow ? Effects::BuildShadowMapFX->GetFX() : Effects::SsaoNormalDepthFX->GetFX());
	RenderManager* rm = RenderManager::GetI();
	if (pass == Pass::Main)
	{
		static const XMMATRIX toTex(0.5f, 0.0f, 0.0f, 0.0f, 0.0f, -0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.5f, 0.5f, 0.0f, 1.0f);
		const XMMATRIX vp = editor ? rm->EditorCameraViewProjectionMatrix : rm->CameraViewProjectionMatrix;
		Effects::InstancedBasicFX->SetViewProj(vp);
		SetMatrix(fx, "gViewProjTex", vp * toTex);
		ShaderSetting setting = UMaterial::GetDefault()->GetShaderSetting();
		setting.UseShadowMap = 1;
		Effects::InstancedBasicFX->SetShaderSetting(setting);
	}
	else if (pass == Pass::NormalDepth)
	{
		SetMatrix(fx, "gView", editor ? rm->EditorCameraViewMatrix : rm->CameraViewMatrix);
		SetMatrix(fx, "gWorldViewProj", editor ? rm->EditorCameraViewProjectionMatrix : rm->CameraViewProjectionMatrix);
	}
	// Shadow: gViewProj 는 ShadowRenderer 가 조각마다 설정해 둔다

	// ---- 나무 값
	Transform* tr = m_pGameObject->GetTransform();
	const XMMATRIX world = tr->GetWorldMatrix();
	v.World->SetMatrix(reinterpret_cast<const float*>(&world));
	const XMMATRIX invT = XMMatrixTranspose(XMMatrixInverse(nullptr, world));
	v.WorldInvTranspose->SetMatrix(reinterpret_cast<const float*>(&invT));

	const float yaw = XMConvertToRadians(WindDirection);
	const float strength = std::clamp(WindStrength, 0.0f, 2.0f);
	const float h = (std::max)(Params.Height, 0.1f);
	const Vec3 pos = tr->GetPosition();
	SetVec(v.Wind, XMFLOAT4(sinf(yaw) * strength, 0.0f, cosf(yaw) * strength, s_WindTime));
	SetVec(v.WindParams, XMFLOAT4(TrunkSway * h / 9.0f, BranchSway * std::clamp(h / 9.0f, 0.2f, 1.5f), LeafFlutter, Hash2(pos.x, pos.z) * XM_2PI));
	SetVec(v.BarkColor, XMFLOAT4(BarkColor.x, BarkColor.y, BarkColor.z, Moss));
	SetVec(v.BarkParams, XMFLOAT4(RidgeDepth * 0.04f, RidgeFrequency, BarkSmoothness, BarkFleck));
	SetVec(v.MossColor, MossColor);
	SetVec(v.LeafColor, XMFLOAT4(LeafColor.x, LeafColor.y, LeafColor.z, LeafVariation));
	SetVec(v.LeafColor2, XMFLOAT4(LeafColor2.x, LeafColor2.y, LeafColor2.z, (float)(int)Params.Leaf));
	SetVec(v.LeafParams, XMFLOAT4(LeafTransmission, LeafSmoothness, (float)std::clamp(Params.LeavesPerCard, 1, 16), LeafLength));
	// 실행 중에 식으로 구운 텍스처 (같은 조합은 한 번만 굽는다)
	if (v.LeafTex && v.LeafTex->IsValid())
		v.LeafTex->SetResource(TreeTextures::Leaf((int)Params.Leaf, Params.LeavesPerCard, LeafLength));
	if (v.BarkTex && v.BarkTex->IsValid())
		v.BarkTex->SetResource(TreeTextures::Bark());

	// ---- 그리기 (수피 → 잎). 기법이 바꾼 깊이/래스터 상태는 되돌린다
	ID3D11DeviceContext* dc = Application::GetI()->GetDeviceContext();
	ComPtr<ID3D11DepthStencilState> prevDSS;
	UINT prevRef = 0;
	dc->OMGetDepthStencilState(prevDSS.GetAddressOf(), &prevRef);
	ComPtr<ID3D11RasterizerState> prevRS;
	dc->RSGetState(prevRS.GetAddressOf());

	const UINT stride = sizeof(TreeVertex), offset = 0;
	dc->IASetInputLayout(layout);
	dc->IASetVertexBuffers(0, 1, m_Mesh->VB.GetAddressOf(), &stride, &offset);
	dc->IASetIndexBuffer(m_Mesh->IB.Get(), DXGI_FORMAT_R32_UINT, 0);
	dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	if (m_Mesh->BarkIndexCount > 0)
	{
		v.Bark->GetPassByIndex(0)->Apply(0, dc);
		dc->DrawIndexed(m_Mesh->BarkIndexCount, 0, 0);
	}
	if (m_Mesh->LeafIndexCount > 0)
	{
		v.Leaf->GetPassByIndex(0)->Apply(0, dc);
		dc->DrawIndexed(m_Mesh->LeafIndexCount, m_Mesh->BarkIndexCount, 0);
	}
	dc->OMSetDepthStencilState(prevDSS.Get(), prevRef);
	dc->RSSetState(prevRS.Get());
}

void Tree::Render() { DrawPass(Pass::Main, false); }
void Tree::_Editor_Render() { DrawPass(Pass::Main, true); }
void Tree::RenderShadow() { DrawPass(Pass::Shadow, RenderManager::GetI()->RenderingEditorView); }
void Tree::RenderShadowNormal() { DrawPass(Pass::NormalDepth, false); }
void Tree::_Editor_RenderShadowNormal() { DrawPass(Pass::NormalDepth, true); }

// ================================================================== Inspector
void Tree::OnInspectorGUI()
{
	using namespace UnityGUI;
	bool shape = false;

	int presetCount = 0;
	const char* const* presets = TreeParams::PresetNames(presetCount);
	int preset = Preset;
	if (Dropdown("Preset", &preset, presets, presetCount))
		ApplyPreset(preset);
	if (Int("Seed", &Params.Seed))
		shape = true;
	if (CenterButton("New Seed"))
	{
		Params.Seed = (Params.Seed * 1103515245 + 12345) & 0x7fffff;
		shape = true;
	}
	{
		EnsureMesh();
		char stats[160];
		snprintf(stats, sizeof(stats), "%d vertices, %d triangles, %d branches, %d leaf cards", VertexCount(), TriangleCount(),
			m_Mesh ? m_Mesh->BranchCount : 0, m_Mesh ? m_Mesh->LeafCardCount : 0);
		HelpBox(stats, false, 0);
	}

	if (Foldout("Trunk", 0, true, false))
	{
		static const char* kCrown[] = { "Conical", "Spherical", "Hemispherical", "Cylindrical", "Tapered Cylindrical", "Flame", "Inverse Conical" };
		shape |= Slider("Height", &Params.Height, 0.5f, 40.0f, 1);
		shape |= Slider("Radius", &Params.Radius, 0.01f, 2.0f, 1);
		shape |= Slider("Tip Radius", &Params.TipRadius, 0.01f, 1.0f, 1);
		shape |= Slider("Root Flare", &Params.Flare, 0.0f, 3.0f, 1);
		shape |= Slider("Gnarl", &Params.Gnarl, 0.0f, 1.0f, 1);
		shape |= Slider("Lean", &Params.Lean, -45.0f, 45.0f, 1);
		shape |= Int("Radial Segments", &Params.RadialSegments, 1);
		int crown = (int)Params.CrownShape;
		if (Dropdown("Crown Shape", &crown, kCrown, 7, 1)) { Params.CrownShape = (TreeParams::Crown)crown; shape = true; }
	}
	if (Foldout("Branches", 0, true, false))
	{
		shape |= Int("Levels", &Params.Levels, 1);
		Params.Levels = std::clamp(Params.Levels, 0, 3);
		for (int i = 0; i < Params.Levels; ++i)
		{
			TreeParams::Level& l = Params.L[i];
			ImGui::PushID(i);
			char title[32];
			snprintf(title, sizeof(title), "Level %d", i + 1);
			Label(title, 1, true);
			shape |= Int("Count", &l.Count, 2);
			shape |= Slider("Start", &l.Start, 0.0f, 0.95f, 2);
			shape |= Slider("Angle", &l.Angle, 0.0f, 150.0f, 2);
			shape |= Slider("Angle Variance", &l.AngleVariance, 0.0f, 60.0f, 2);
			shape |= Slider("Length", &l.Length, 0.05f, 2.0f, 2);
			shape |= Slider("Length Variance", &l.LengthVariance, 0.0f, 1.0f, 2);
			shape |= Slider("Gravity", &l.Gravity, -1.0f, 1.5f, 2);
			shape |= Slider("Up", &l.Up, 0.0f, 1.0f, 2);
			shape |= Slider("Radius", &l.Radius, 0.1f, 1.0f, 2);
			shape |= Slider("Gnarl", &l.Gnarl, 0.0f, 1.0f, 2);
			ImGui::PopID();
		}
	}
	if (Foldout("Leaves", 0, true, false))
	{
		static const char* kShape[] = { "Broad", "Oval", "Needle" };
		int leaf = (int)Params.Leaf;
		if (Dropdown("Shape", &leaf, kShape, 3, 1)) { Params.Leaf = (TreeParams::LeafShape)leaf; shape = true; }
		shape |= Int("Cards per Branch", &Params.LeafCards, 1);
		shape |= Int("Leaves per Card", &Params.LeavesPerCard, 1);
		shape |= Slider("Card Size", &Params.LeafCardSize, 0.1f, 3.0f, 1);
		shape |= Slider("Start", &Params.LeafStart, 0.0f, 1.0f, 1);
		Slider("Leaf Length", &LeafLength, 0.1f, 0.5f, 1);
		UnityGUI::Color("Color", &LeafColor.x, 1);
		UnityGUI::Color("Color 2", &LeafColor2.x, 1);
		Slider("Variation", &LeafVariation, 0.0f, 1.0f, 1);
		Slider("Transmission", &LeafTransmission, 0.0f, 1.5f, 1);
		Slider("Smoothness", &LeafSmoothness, 0.0f, 1.0f, 1);
	}
	if (Foldout("Bark", 0, true, false))
	{
		UnityGUI::Color("Color", &BarkColor.x, 1);
		Slider("Ridge Depth", &RidgeDepth, 0.0f, 2.0f, 1);
		Slider("Ridge Frequency", &RidgeFrequency, 1.0f, 40.0f, 1);
		Slider("Fleck", &BarkFleck, -1.0f, 1.0f, 1);
		Slider("Smoothness", &BarkSmoothness, 0.0f, 1.0f, 1);
		UnityGUI::Color("Moss Color", &MossColor.x, 1);
		Slider("Moss", &Moss, 0.0f, 1.0f, 1);
	}
	if (Foldout("Wind", 0, true, false))
	{
		Slider("Strength", &WindStrength, 0.0f, 2.0f, 1);
		Slider("Direction", &WindDirection, -180.0f, 180.0f, 1);
		Slider("Trunk Sway", &TrunkSway, 0.0f, 2.0f, 1);
		Slider("Branch Sway", &BranchSway, 0.0f, 1.0f, 1);
		Slider("Leaf Flutter", &LeafFlutter, 0.0f, 0.2f, 1);
	}
	if (Foldout("Lighting", 0, true, false))
		Toggle("Cast Shadows", &CastShadows, 1);

	if (shape)
		m_Dirty = true;
}

// ================================================================== 저장
static nlohmann::json Color4(const XMFLOAT4& c) { return { c.x, c.y, c.z, c.w }; }
static void ReadColor(const nlohmann::json& j, const char* key, XMFLOAT4& c)
{
	if (j.contains(key) && j[key].is_array() && j[key].size() >= 3)
	{
		c.x = j[key][0].get<float>(); c.y = j[key][1].get<float>(); c.z = j[key][2].get<float>();
	}
}

GENERATE_COMPONENT_FUNC_TOJSON(Tree)
{
	json j;
	SERIALIZE_TYPE(j, Tree);
	j["enabled"] = m_Enabled;
	j["preset"] = Preset;
	j["shape"] = Params.ToJson();
	j["bark"] = { { "color", Color4(BarkColor) }, { "mossColor", Color4(MossColor) }, { "moss", Moss }, { "ridgeDepth", RidgeDepth },
		{ "ridgeFrequency", RidgeFrequency }, { "smoothness", BarkSmoothness }, { "fleck", BarkFleck } };
	j["leaves"] = { { "color", Color4(LeafColor) }, { "color2", Color4(LeafColor2) }, { "variation", LeafVariation },
		{ "transmission", LeafTransmission }, { "smoothness", LeafSmoothness }, { "length", LeafLength } };
	j["wind"] = { { "strength", WindStrength }, { "direction", WindDirection }, { "trunkSway", TrunkSway },
		{ "branchSway", BranchSway }, { "leafFlutter", LeafFlutter } };
	j["castShadows"] = CastShadows;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(Tree)
{
	m_Enabled = j.value("enabled", true);
	Preset = j.value("preset", 0);
	// 모양 값이 없으면(손으로 쓴 씬 등) 프리셋 값 + 저장된 seed
	if (!j.contains("shape"))
	{
		ApplyPreset(Preset);
		Params.Seed = j.value("seed", Params.Seed);
	}
	else
		Params.FromJson(j["shape"]);
	if (j.contains("bark"))
	{
		const json& b = j["bark"];
		ReadColor(b, "color", BarkColor); ReadColor(b, "mossColor", MossColor);
		Moss = b.value("moss", Moss); RidgeDepth = b.value("ridgeDepth", RidgeDepth); RidgeFrequency = b.value("ridgeFrequency", RidgeFrequency);
		BarkSmoothness = b.value("smoothness", BarkSmoothness); BarkFleck = b.value("fleck", BarkFleck);
	}
	if (j.contains("leaves"))
	{
		const json& l = j["leaves"];
		ReadColor(l, "color", LeafColor); ReadColor(l, "color2", LeafColor2);
		LeafVariation = l.value("variation", LeafVariation); LeafTransmission = l.value("transmission", LeafTransmission);
		LeafSmoothness = l.value("smoothness", LeafSmoothness); LeafLength = l.value("length", LeafLength);
	}
	if (j.contains("wind"))
	{
		const json& w = j["wind"];
		WindStrength = w.value("strength", WindStrength); WindDirection = w.value("direction", WindDirection);
		TrunkSway = w.value("trunkSway", TrunkSway); BranchSway = w.value("branchSway", BranchSway); LeafFlutter = w.value("leafFlutter", LeafFlutter);
	}
	CastShadows = j.value("castShadows", true);
	m_Dirty = true;
}
