#include "pch.h"
#include "MaterialInspector.h"
#include "UMaterial.h"
#include "Effects.h"
#include "Vertex.h"
#include "MathHelper.h"
#include "GameObjectFactory.h"
#include "Mesh.h"
#include "UnityGUI.h"
#include "ObjectPicker.h"
#include "UISprites.h"
#include "UndoSystem.h"
#include "PathManager.h"
#include "ResourceManager.h"

namespace
{
	const ImU32 kText = IM_COL32(196, 196, 196, 255);
	const ImU32 kTextBright = IM_COL32(230, 230, 230, 255);
	const ImU32 kFieldBg = IM_COL32(42, 42, 42, 255);
	const ImU32 kFieldBorder = IM_COL32(26, 26, 26, 255);

	// ---------------------------------------------------------------- 미리보기 렌더 타깃
	struct PreviewTarget
	{
		int Size = 0;
		ComPtr<ID3D11Texture2D> Color, Depth;
		ComPtr<ID3D11RenderTargetView> Rtv;
		ComPtr<ID3D11ShaderResourceView> Srv;
		ComPtr<ID3D11DepthStencilView> Dsv;
	};
	std::map<int, PreviewTarget> s_Targets;

	PreviewTarget* EnsureTarget(int size)
	{
		PreviewTarget& t = s_Targets[size];
		if (t.Size == size && t.Rtv)
			return &t;
		auto device = Application::GetI()->GetDevice();
		D3D11_TEXTURE2D_DESC td = {};
		td.Width = td.Height = size;
		td.MipLevels = td.ArraySize = 1;
		td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		td.SampleDesc.Count = 1;
		td.Usage = D3D11_USAGE_DEFAULT;
		td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		if (FAILED(device->CreateTexture2D(&td, nullptr, t.Color.ReleaseAndGetAddressOf())))
			return nullptr;
		device->CreateRenderTargetView(t.Color.Get(), nullptr, t.Rtv.ReleaseAndGetAddressOf());
		device->CreateShaderResourceView(t.Color.Get(), nullptr, t.Srv.ReleaseAndGetAddressOf());
		td.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		if (FAILED(device->CreateTexture2D(&td, nullptr, t.Depth.ReleaseAndGetAddressOf())))
			return nullptr;
		device->CreateDepthStencilView(t.Depth.Get(), nullptr, t.Dsv.ReleaseAndGetAddressOf());
		t.Size = size;
		return &t;
	}

	// ---------------------------------------------------------------- 위젯
	std::vector<std::string> TextureItems()
	{
		std::vector<std::string> out;
		for (const std::string& s : UISprites::FindAll())
			if (s.rfind("builtin:", 0) != 0)
				out.push_back(s);
		return out;
	}

	// 텍스처 행: [■ 썸네일] 레이블 | (필드 열은 호출자가 채운다). 썸네일 클릭 = 선택 창, Project 의 이미지를 끌어 놓기
	// 주의: ComPtr 의 & 는 포인터를 놓아 버린다(ReleaseAndGetAddressOf) → 호출할 때 std::addressof 로 넘길 것
	bool TextureRow(const char* label, std::wstring* path, ComPtr<ID3D11ShaderResourceView>* srv, const std::string& key, UnityGUI::FieldRow& row, int indent = 0, bool disabled = false)
	{
		bool changed = false;
		row = UnityGUI::BeginFieldRow("", indent);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const float x = row.p.x + UnityGUI::kBaseIndent + indent * UnityGUI::kNestIndent;
		const ImVec2 box(x, row.p.y + 1.0f), boxEnd(x + 16.0f, row.p.y + 17.0f);
		ImGui::PushID(label);
		ImGui::SetCursorScreenPos(box);
		if (disabled) ImGui::BeginDisabled();
		const bool clicked = ImGui::InvisibleButton("##tex", ImVec2(16.0f, 16.0f));
		if (disabled) ImGui::EndDisabled();
		dl->AddRectFilled(box, boxEnd, kFieldBg, 2.0f);
		if (srv && srv->Get())
			dl->AddImage((ImTextureID)srv->Get(), ImVec2(box.x + 1, box.y + 1), ImVec2(boxEnd.x - 1, boxEnd.y - 1));
		dl->AddRect(box, boxEnd, ImGui::IsItemHovered() ? IM_COL32(120, 120, 120, 255) : kFieldBorder, 2.0f);
		dl->AddText(ImVec2(x + 22.0f, row.p.y + 2.0f), disabled ? IM_COL32(110, 110, 110, 255) : kText, label);
		if (!disabled && path)
		{
			if (clicked)
			{
				ObjectPicker::Options opt;
				opt.TypeName = "Texture2D";
				opt.Icon = "texture";
				opt.Items = TextureItems();
				opt.Current = wstring_to_string(*path);
				ObjectPicker::Open(key, std::move(opt));
			}
			std::string picked;
			if (ObjectPicker::Poll(key, picked))
			{
				*path = string_to_wstring(picked);
				*srv = picked.empty() ? nullptr : ResourceManager::GetI()->LoadTexture(*path);
				changed = true;
			}
			ImGui::SetCursorScreenPos(box);
			ImGui::InvisibleButton("##texDrop", ImVec2(row.fieldX - box.x - 8.0f, 18.0f));
			if (ImGui::BeginDragDropTarget())
			{
				const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_FILE");
				if (payload == nullptr)
					payload = ImGui::AcceptDragDropPayload("PNG_FILE");
				if (payload)
				{
					std::string dropped(static_cast<const char*>(payload->Data));
					const std::string root = wstring_to_string(PathManager::GetI()->GetContentPathW());
					if (_strnicmp(dropped.c_str(), root.c_str(), root.size()) == 0)
						dropped = dropped.substr(root.size());
					if (UISprites::IsImagePath(dropped))
					{
						*path = string_to_wstring(dropped);
						*srv = ResourceManager::GetI()->LoadTexture(*path);
						changed = true;
					}
				}
				ImGui::EndDragDropTarget();
			}
		}
		ImGui::PopID();
		return changed;
	}

	// 필드 열 안의 색 칸 (클릭 = 색 선택 팝업)
	bool ColorSwatch(const char* id, float* rgba, ImVec2 pos, float width, bool alpha)
	{
		bool changed = false;
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 end(pos.x + width, pos.y + UnityGUI::kRowHeight);
		ImGui::PushID(id);
		ImGui::SetCursorScreenPos(pos);
		if (ImGui::InvisibleButton("##sw", ImVec2(width, UnityGUI::kRowHeight)))
			ImGui::OpenPopup("##picker");
		dl->AddRectFilled(pos, end, kFieldBg, 3.0f);
		dl->AddRectFilled(ImVec2(pos.x + 2, pos.y + 2), ImVec2(end.x - 2, end.y - 5), ImGui::ColorConvertFloat4ToU32(ImVec4(rgba[0], rgba[1], rgba[2], 1.0f)), 2.0f);
		if (alpha)
		{
			dl->AddRectFilled(ImVec2(pos.x + 2, end.y - 4), ImVec2(end.x - 2, end.y - 2), IM_COL32(0, 0, 0, 255));
			dl->AddRectFilled(ImVec2(pos.x + 2, end.y - 4), ImVec2(pos.x + 2 + (width - 4) * std::clamp(rgba[3], 0.0f, 1.0f), end.y - 2), IM_COL32(255, 255, 255, 255));
		}
		dl->AddRect(pos, end, kFieldBorder, 3.0f);
		if (ImGui::BeginPopup("##picker"))
		{
			const ImGuiColorEditFlags flags = ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview | (alpha ? ImGuiColorEditFlags_AlphaBar : ImGuiColorEditFlags_NoAlpha);
			changed = alpha ? ImGui::ColorPicker4("##c", rgba, flags) : ImGui::ColorPicker3("##c", rgba, flags);
			ImGui::EndPopup();
		}
		ImGui::PopID();
		return changed;
	}

	std::string ShortName(const std::string& path)
	{
		if (path.rfind("builtin:", 0) == 0)
			return path.substr(8);
		return std::filesystem::path(path).stem().string();
	}
}

// ------------------------------------------------------------------ 미리보기
ImTextureID MaterialInspector::RenderPreview(UMaterial& material, int size, float yaw)
{
	PreviewTarget* t = EnsureTarget(size);
	auto mesh = GameObjectFactory::GetPrimitiveMesh(PrimitiveType::Sphere);
	if (t == nullptr || mesh == nullptr || Effects::InstancedBasicFX == nullptr)
		return nullptr;
	auto ctx = Application::GetI()->GetDeviceContext();

	// 지금 묶인 타깃/뷰포트 보관
	ComPtr<ID3D11RenderTargetView> oldRtv;
	ComPtr<ID3D11DepthStencilView> oldDsv;
	ctx->OMGetRenderTargets(1, oldRtv.GetAddressOf(), oldDsv.GetAddressOf());
	UINT vpCount = 1;
	D3D11_VIEWPORT oldVp = {};
	ctx->RSGetViewports(&vpCount, &oldVp);

	ID3D11RenderTargetView* rtvs[1] = { t->Rtv.Get() };
	ctx->OMSetRenderTargets(1, rtvs, t->Dsv.Get());
	const D3D11_VIEWPORT vp = { 0.0f, 0.0f, (float)size, (float)size, 0.0f, 1.0f };
	ctx->RSSetViewports(1, &vp);
	const float clear[4] = { 0.19f, 0.19f, 0.19f, 1.0f };
	ctx->ClearRenderTargetView(t->Rtv.Get(), clear);
	ctx->ClearDepthStencilView(t->Dsv.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
	ctx->OMSetDepthStencilState(nullptr, 0);
	ctx->OMSetBlendState(nullptr, nullptr, 0xffffffff);
	ctx->RSSetState(nullptr);

	// 카메라 (구 정면), 빛 2개 (왼쪽 위 주광 + 오른쪽 뒤 보조광) — Unity 재질 미리보기와 비슷하게
	const XMFLOAT3 eye(0.0f, 0.0f, -2.9f);
	const XMMATRIX view = XMMatrixLookAtLH(XMLoadFloat3(&eye), XMVectorZero(), XMVectorSet(0, 1, 0, 0));
	const XMMATRIX proj = XMMatrixPerspectiveFovLH(XMConvertToRadians(30.0f), 1.0f, 0.1f, 100.0f);
	const XMMATRIX world = XMMatrixRotationY(yaw);
	const XMMATRIX toTex(0.5f, 0.0f, 0.0f, 0.0f, 0.0f, -0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.5f, 0.5f, 0.0f, 1.0f);
	auto fx = Effects::InstancedBasicFX;
	fx->SetWorld(world);
	fx->SetWorldInvTranspose(MathHelper::InverseTranspose(world));
	fx->SetWorldViewProj(world * view * proj);
	fx->SetWorldViewProjTex(world * view * proj * toTex);
	fx->SetViewProj(view * proj);
	fx->SetTexTransform(XMMatrixIdentity());
	fx->SetEyePosW(eye);
	DirectionalLight lights[2];
	lights[0].Ambient = XMFLOAT4(0.32f, 0.33f, 0.36f, 1.0f);
	lights[0].Diffuse = XMFLOAT4(1.0f, 0.97f, 0.92f, 1.0f);
	lights[0].Specular = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	XMStoreFloat3(&lights[0].Direction, XMVector3Normalize(XMVectorSet(0.7f, -0.6f, 0.6f, 0)));
	lights[1].Ambient = XMFLOAT4(0, 0, 0, 1);
	lights[1].Diffuse = XMFLOAT4(0.28f, 0.3f, 0.38f, 1.0f);
	lights[1].Specular = XMFLOAT4(0.3f, 0.3f, 0.3f, 1.0f);
	XMStoreFloat3(&lights[1].Direction, XMVector3Normalize(XMVectorSet(-0.8f, 0.2f, -0.5f, 0)));
	PointLight noPoint;
	SpotLight noSpot;
	fx->SetDirLights(lights, 2);
	fx->SetPointLights(&noPoint, 0);
	fx->SetSpotLights(&noSpot, 0);
	material.Apply(fx.get(), true);

	ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	ctx->IASetInputLayout(InputLayouts::PosNormalTexTan.Get());
	fx->Tech->GetPassByIndex(0)->Apply(0, ctx);
	for (int i = 0; i < (int)mesh->Subsets.size(); ++i)
		mesh->ModelMesh.Draw(ctx, i);

	ID3D11ShaderResourceView* nullSRV[16] = {};
	ctx->PSSetShaderResources(0, 16, nullSRV);
	ID3D11RenderTargetView* restore[1] = { oldRtv.Get() };
	ctx->OMSetRenderTargets(1, restore, oldDsv.Get());
	if (vpCount > 0)
		ctx->RSSetViewports(1, &oldVp);
	return (ImTextureID)t->Srv.Get();
}

void MaterialInspector::WatchUndo(const std::shared_ptr<UMaterial>& material)
{
	if (material == nullptr || UMaterial::IsBuiltinPath(material->GetPath()))
		return;
	std::weak_ptr<UMaterial> weak = material;
	Undo::WatchAsset("material:" + std::to_string((uintptr_t)material.get()), "Material",
		[weak]() { auto m = weak.lock(); if (!m) return std::string(); json j = *m; return j.dump(); },
		[weak](const std::string& text) { if (auto m = weak.lock()) { from_json(json::parse(text), *m); m->ReloadTextures(); UMaterial::Save(m.get()); } });
}

// ------------------------------------------------------------------ 재질 슬롯 (Renderer 의 Materials 목록)
std::vector<std::string> MaterialInspector::FindAllMaterials()
{
	std::vector<std::string> out;
	auto scan = [&](const std::wstring& root) {
		std::error_code ec;
		if (!std::filesystem::exists(root, ec))
			return;
		for (const auto& e : std::filesystem::recursive_directory_iterator(root, std::filesystem::directory_options::skip_permission_denied, ec))
			if (e.is_regular_file(ec) && _wcsicmp(e.path().extension().c_str(), L".mat") == 0)
				out.push_back(wstring_to_string(PathManager::GetI()->GetCutSolutionPath(e.path().wstring())));
	};
	scan(PathManager::GetI()->GetMovePathW(L"Assets\\"));
	scan(PathManager::GetI()->GetMovePathW(L"Resources\\Packages\\"));
	std::sort(out.begin(), out.end());
	out.insert(out.begin(), "builtin:Default-Material");
	return out;
}

bool MaterialInspector::MaterialSlot(const char* label, const std::string& key, std::shared_ptr<UMaterial>& material, std::wstring& path)
{
	std::string name = "None (Material)";
	if (material)
	{
		name = ShortName(material->GetName());
		if (name.empty())
			name = "None (Material)";
	}

	// ElementRow 와 같은 배치로 오브젝트 필드 영역을 계산해 끌어 놓기 영역을 겹친다
	const ImVec2 p = ImGui::GetCursorScreenPos();
	const float w = ImGui::GetContentRegionAvail().x;
	const float x0 = p.x + 14.0f, x1 = p.x + w - 12.0f;
	const ImVec2 f0(x0 + (x1 - x0) * 0.40f, p.y + 4.0f), f1(x1 - 6.0f, p.y + 23.0f);
	const bool pick = UnityGUI::ElementRow(label, name.c_str(), material ? "material_ball" : nullptr);
	const ImVec2 after = ImGui::GetCursorScreenPos();

	bool changed = false;
	auto assign = [&](const std::string& picked) {
		if (picked.empty())
		{
			material = nullptr;
			path.clear();
		}
		else
		{
			auto loaded = ResourceManager::GetI()->LoadMaterial(picked);
			if (loaded == nullptr)
				return;
			material = loaded;
			path = string_to_wstring(picked);
		}
		changed = true;
	};

	if (pick)
	{
		ObjectPicker::Options opt;
		opt.TypeName = "Material";
		opt.Icon = "material_ball";
		opt.Items = FindAllMaterials();
		opt.Current = material ? (path.empty() ? material->GetName() : wstring_to_string(path)) : std::string();
		opt.Describe = [](const std::string& p) {
			if (UMaterial::IsBuiltinPath(p))
				return std::string("Universal Render Pipeline/Lit (built-in)");
			auto m = ResourceManager::GetI()->LoadMaterial(p);
			return m ? std::string(m->GetShader() == UMaterial::ShaderKind::Unlit ? "Universal Render Pipeline/Unlit" : "Universal Render Pipeline/Lit") : std::string("(cannot load)");
		};
		ObjectPicker::Open(key, std::move(opt));
	}
	std::string picked;
	if (ObjectPicker::Poll(key, picked))
		assign(picked);

	// Project 창에서 .mat 끌어 놓기 (⊙ 버튼 칸은 빼고)
	ImGui::PushID(key.c_str());
	ImGui::SetCursorScreenPos(f0);
	ImGui::InvisibleButton("##matDrop", ImVec2((std::max)(1.0f, f1.x - f0.x - 22.0f), f1.y - f0.y));
	if (ImGui::BeginDragDropTarget())
	{
		// Project 창은 .mat 를 "MAT_FILE"(프로젝트 기준 경로)로 보낸다
		const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MAT_FILE");
		if (payload == nullptr)
			payload = ImGui::AcceptDragDropPayload("ASSET_FILE");
		if (payload != nullptr)
		{
			const std::string dropped(static_cast<const char*>(payload->Data));
			if (_stricmp(std::filesystem::path(dropped).extension().string().c_str(), ".mat") == 0)
				assign(wstring_to_string(PathManager::GetI()->GetCutSolutionPath(string_to_wstring(dropped))));
		}
		ImGui::EndDragDropTarget();
	}
	ImGui::PopID();
	ImGui::SetCursorScreenPos(after);
	return changed;
}

// ------------------------------------------------------------------ Inspector

void MaterialInspector::Draw(UMaterial& m, bool embedded)
{
	const bool builtin = UMaterial::IsBuiltinPath(m.m_ResourcePath);
	const std::string name = ShortName(m.m_ResourcePath);
	const std::string key = std::to_string((uintptr_t)&m);
	ImGui::PushID(&m);
	ImDrawList* dl = ImGui::GetWindowDrawList();
	bool changed = false;

	// ---- 머리글: [미리보기] 이름 / Shader [ ▾ ]
	{
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const float w = ImGui::GetContentRegionAvail().x;
		const float h = 48.0f;
		bool open = true;
		ImGuiStorage* st = ImGui::GetStateStorage();
		const ImGuiID openId = ImGui::GetID("##matOpen");
		if (embedded)
		{
			open = st->GetBool(openId, true);
			ImGui::SetNextItemAllowOverlap();
			if (ImGui::InvisibleButton("##matHeader", ImVec2(w, 22.0f)))
				st->SetBool(openId, open = !open);
		}
		dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), IM_COL32(62, 62, 62, 255));
		dl->AddLine(p, ImVec2(p.x + w, p.y), IM_COL32(30, 30, 30, 255));
		dl->AddLine(ImVec2(p.x, p.y + h - 1), ImVec2(p.x + w, p.y + h - 1), IM_COL32(30, 30, 30, 255));
		if (embedded)
			UnityGUI::DrawIcon(dl, open ? "arrow_down" : "arrow_right", ImVec2(p.x + 6.0f, p.y + 6.0f), 10.0f);
		const float ix = p.x + (embedded ? 20.0f : 6.0f);
		if (ImTextureID icon = RenderPreview(m, 128, 0.6f))
			dl->AddImage(icon, ImVec2(ix, p.y + 4.0f), ImVec2(ix + 40.0f, p.y + 44.0f));
		ImFont* bold = UnityGUI::BoldFont();
		dl->AddText(bold, bold->FontSize, ImVec2(ix + 48.0f, p.y + 5.0f), kTextBright, (name + (builtin ? "  (Default)" : "")).c_str());
		dl->AddText(ImVec2(ix + 48.0f, p.y + 26.0f), kText, "Shader");
		// Shader: Lit / Unlit
		static const char* kShaders[] = { "Universal Render Pipeline/Lit", "Universal Render Pipeline/Unlit" };
		ImGui::SetCursorScreenPos(ImVec2(ix + 100.0f, p.y + 24.0f));
		ImGui::SetNextItemWidth((std::max)(80.0f, p.x + w - (ix + 100.0f) - 10.0f));
		if (builtin) ImGui::BeginDisabled();
		int shader = (int)m.m_Shader;
		if (ImGui::Combo("##shader", &shader, kShaders, 2))
		{
			m.m_Shader = (UMaterial::ShaderKind)shader;
			changed = true;
		}
		if (builtin) ImGui::EndDisabled();
		ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h + 2.0f));
		if (!open)
		{
			ImGui::PopID();
			return;
		}
	}

	if (builtin)
	{
		UnityGUI::HelpBox("Default-Material 은 엔진에 들어 있는 재질이라 바꿀 수 없습니다. Project 창에서 Create > Material 로 새 재질을 만들어 끌어 놓으세요.", false);
		ImGui::BeginDisabled();
	}

	PbrMaterial& p = m.m_Pbr;
	const bool lit = m.m_Shader == UMaterial::ShaderKind::Lit;

	// ---- Surface Options
	if (UnityGUI::Foldout("Surface Options", 0, true, false))
	{
		static const char* kWorkflow[] = { "Metallic" };
		int workflow = 0;
		if (lit)
			UnityGUI::Dropdown("Workflow Mode", &workflow, kWorkflow, 1, 0, true);
		static const char* kSurface[] = { "Opaque", "Transparent" };
		int surface = 0;
		UnityGUI::Dropdown("Surface Type", &surface, kSurface, 2, 0, true);
		bool clip = p.AlphaClip != 0;
		if (UnityGUI::Toggle("Alpha Clipping", &clip)) { p.AlphaClip = clip ? 1 : 0; changed = true; }
		if (clip && UnityGUI::Slider("Threshold", &p.Cutoff, 0.0f, 1.0f, 1)) { p.Cutoff = std::clamp(p.Cutoff, 0.0f, 1.0f); changed = true; }
		if (lit)
		{
			bool receive = p.ReceiveShadows != 0;
			if (UnityGUI::Toggle("Receive Shadows", &receive)) { p.ReceiveShadows = receive ? 1 : 0; changed = true; }
		}
	}

	// ---- Surface Inputs
	if (UnityGUI::Foldout("Surface Inputs", 0, true, false))
	{
		UnityGUI::FieldRow row;
		// Base Map + 색
		changed |= TextureRow("Base Map", &m.m_BaseMapPath, std::addressof(m.BaseMapSRV), "matBase:" + key, row);
		changed |= ColorSwatch("##baseColor", &p.BaseColor.x, ImVec2(row.fieldX, row.p.y), row.fieldW, true);
		UnityGUI::EndFieldRow(row);
		if (lit)
		{
			// Metallic Map + Metallic (맵이 있으면 맵 값)
			changed |= TextureRow("Metallic Map", &m.m_MetallicMapPath, std::addressof(m.MetallicMapSRV), "matMetal:" + key, row);
			UnityGUI::EndFieldRow(row);
			if (!m.MetallicMapSRV)
			{
				if (UnityGUI::Slider("Metallic", &p.Metallic, 0.0f, 1.0f, 1)) { p.Metallic = std::clamp(p.Metallic, 0.0f, 1.0f); changed = true; }
			}
			if (UnityGUI::Slider("Smoothness", &p.Smoothness, 0.0f, 1.0f, 1)) { p.Smoothness = std::clamp(p.Smoothness, 0.0f, 1.0f); changed = true; }
			static const char* kSource[] = { "Metallic Alpha", "Albedo Alpha" };
			if (UnityGUI::Dropdown("Source", &p.SmoothnessFromAlbedo, kSource, 2, 2)) changed = true;
			// Normal Map + Scale
			changed |= TextureRow("Normal Map", &m.m_NormalMapPath, std::addressof(m.NormalMapSRV), "matNormal:" + key, row);
			if (m.NormalMapSRV)
				changed |= UnityGUI::FloatBox("##normalScale", &p.NormalScale, ImVec2(row.fieldX, row.p.y), 60.0f);
			UnityGUI::EndFieldRow(row);
			TextureRow("Height Map", nullptr, nullptr, "", row, 0, true);
			UnityGUI::EndFieldRow(row);
			// Occlusion Map + Strength
			changed |= TextureRow("Occlusion Map", &m.m_OcclusionMapPath, std::addressof(m.OcclusionMapSRV), "matOcc:" + key, row);
			UnityGUI::EndFieldRow(row);
			if (m.OcclusionMapSRV && UnityGUI::Slider("Strength", &p.OcclusionStrength, 0.0f, 1.0f, 1)) { p.OcclusionStrength = std::clamp(p.OcclusionStrength, 0.0f, 1.0f); changed = true; }
		}
		// Emission: 켜면 Map + HDR 색 + Intensity
		if (UnityGUI::Toggle("Emission", &m.m_EmissionEnabled)) changed = true;
		if (m.m_EmissionEnabled)
		{
			changed |= TextureRow("Emission Map", &m.m_EmissionMapPath, std::addressof(m.EmissionMapSRV), "matEmit:" + key, row, 1);
			changed |= ColorSwatch("##emitColor", &m.m_EmissionColor.x, ImVec2(row.fieldX, row.p.y), row.fieldW, false);
			UnityGUI::EndFieldRow(row);
			if (UnityGUI::Float("Intensity", &m.m_EmissionIntensity, 1)) { m.m_EmissionIntensity = (std::max)(0.0f, m.m_EmissionIntensity); changed = true; }
		}
		UnityGUI::Spacing(4.0f);
		if (UnityGUI::Vector2Pair("Tiling", "X", &p.Tiling.x, "Y", &p.Tiling.y)) changed = true;
		if (UnityGUI::Vector2Pair("Offset", "X", &p.Offset.x, "Y", &p.Offset.y)) changed = true;
	}

	// ---- Advanced Options
	if (UnityGUI::Foldout("Advanced Options", 0, false, false))
	{
		if (lit)
		{
			bool spec = p.SpecularHighlights != 0, env = p.EnvironmentReflections != 0;
			if (UnityGUI::Toggle("Specular Highlights", &spec)) { p.SpecularHighlights = spec ? 1 : 0; changed = true; }
			if (UnityGUI::Toggle("Environment Reflections", &env)) { p.EnvironmentReflections = env ? 1 : 0; changed = true; }
		}
		bool instancing = true;
		ImGui::BeginDisabled();
		UnityGUI::Toggle("Enable GPU Instancing", &instancing);
		ImGui::EndDisabled();
		if (UnityGUI::Int("Priority", &m.m_Priority)) changed = true;
	}

	if (builtin)
		ImGui::EndDisabled();

	if (changed && !builtin)
	{
		m.SyncLegacy();
		UMaterial::Save(&m);
	}

	// ---- 아래쪽 Preview (재질 에셋을 골랐을 때만, 끌어서 돌리기)
	if (!embedded)
	{
		UnityGUI::Spacing(8.0f);
		const ImVec2 p0 = ImGui::GetCursorScreenPos();
		const float w = ImGui::GetContentRegionAvail().x;
		dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + 20.0f), IM_COL32(50, 50, 50, 255));
		ImFont* bold = UnityGUI::BoldFont();
		dl->AddText(bold, bold->FontSize, ImVec2(p0.x + 8.0f, p0.y + 3.0f), kTextBright, "Preview");
		static float s_Yaw = 0.6f;
		const float size = (std::min)(w - 16.0f, 256.0f);
		const ImVec2 ip(p0.x + (w - size) * 0.5f, p0.y + 26.0f);
		ImGui::SetCursorScreenPos(ip);
		ImGui::InvisibleButton("##preview", ImVec2(size, size));
		if (ImGui::IsItemActive())
			s_Yaw -= ImGui::GetIO().MouseDelta.x * 0.01f;
		if (ImTextureID tex = RenderPreview(m, 256, s_Yaw))
			dl->AddImage(tex, ip, ImVec2(ip.x + size, ip.y + size));
		ImGui::SetCursorScreenPos(ImVec2(p0.x, ip.y + size + 6.0f));
		ImGui::Dummy(ImVec2(w, 1.0f));
	}
	ImGui::PopID();
}
