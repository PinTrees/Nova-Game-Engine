#include "pch.h"
#include "SpriteRenderer.h"
#include "UnityGUI.h"
#include "UISprites.h"
#include "ObjectPicker.h"
#include "AssetImportSettings.h"
#include "TagsAndLayers.h"
#include "ProjectSettingsWindow.h"

SpriteRenderer::SpriteRenderer()
{
	m_InspectorTitleName = "Sprite Renderer";
}

bool SpriteRenderer::Resolve()
{
	const unsigned long long now = GetTickCount64();
	if (m_Resolved && now - m_ResolvedAt < 500)
		return m_Texture != nullptr;
	m_Resolved = true;
	m_ResolvedAt = now;
	m_Texture = nullptr;
	if (m_Sprite.empty())
		return false;
	UISprites::Info info;
	if (!UISprites::Get(m_Sprite, info) || info.Texture == nullptr || info.Size.x <= 0 || info.Size.y <= 0)
		return false;
	m_Texture = info.Texture;
	m_SizePx = info.Size;
	m_Pivot = info.Pivot;
	m_UV = info.UV;
	m_Point = false;
	if (m_Sprite.rfind("builtin:", 0) == 0)
	{
		m_PixelsPerUnit = info.Size.x;   // 내장 도형: 너비 = 1 단위
		return true;
	}
	// 파일: 가져오기 설정 (Sprite 의 Pixels Per Unit · Pivot · Filter Mode). 크기는 원본 픽셀 (Max Size 로 줄어도 같은 크기)
	const std::string file = m_Sprite.substr(0, m_Sprite.find('#'));
	const std::wstring full = PathManager::GetI()->GetMovePathW(string_to_wstring(file));
	const AssetImport::TextureSettings ts = AssetImport::LoadTexture(full);
	m_PixelsPerUnit = (std::max)(0.01f, ts.PixelsPerUnit);
	m_Point = ts.FilterMode == AssetImport::TextureSettings::Point;
	if (info.SubSprite)
		return true;   // 크기 · 기준점 · UV = 그 사각형
	m_Pivot = Vec2(ts.PivotX, ts.PivotY);
	AssetImport::TextureInfo ti;
	if (AssetImport::GetTextureInfo(full, ti) && ti.SourceWidth > 0 && ti.SourceHeight > 0)
		m_SizePx = Vec2((float)ti.SourceWidth, (float)ti.SourceHeight);
	return true;
}

bool SpriteRenderer::GetSpriteSize(Vec2& size, Vec2& pivot)
{
	if (!Resolve())
		return false;
	size = m_SizePx / m_PixelsPerUnit;
	pivot = m_Pivot;
	return true;
}

// 로컬 네 점 (왼쪽 아래 · 오른쪽 아래 · 오른쪽 위 · 왼쪽 위). Flip = 기준점을 중심으로 뒤집기
void SpriteRenderer::LocalCorners(Vec3 out[4])
{
	const float w = m_SizePx.x / m_PixelsPerUnit, h = m_SizePx.y / m_PixelsPerUnit;
	float x0 = -m_Pivot.x * w, x1 = x0 + w, y0 = -m_Pivot.y * h, y1 = y0 + h;
	if (m_FlipX) { x0 = -x0; x1 = -x1; }
	if (m_FlipY) { y0 = -y0; y1 = -y1; }
	out[0] = Vec3(x0, y0, 0.0f);
	out[1] = Vec3(x1, y0, 0.0f);
	out[2] = Vec3(x1, y1, 0.0f);
	out[3] = Vec3(x0, y1, 0.0f);
}

void SpriteRenderer::CollectSprites(SpriteBatch& batch)
{
	if (m_pGameObject == nullptr || !Resolve())
		return;
	const Matrix world = m_pGameObject->GetTransform()->GetWorldMatrix();
	Vec3 p[4];
	LocalCorners(p);
	for (Vec3& v : p)
		v = Vec3::Transform(v, world);
	const Vec2 uv[4] = { Vec2(m_UV.x, m_UV.w), Vec2(m_UV.z, m_UV.w), Vec2(m_UV.z, m_UV.y), Vec2(m_UV.x, m_UV.y) };
	batch.Begin(m_SortingLayerId, m_SortingOrder, m_pGameObject->GetTransform()->GetPosition());
	batch.Quad(p, uv, SpriteBatch::PackColor(m_Color), m_Texture, m_Point);
}

bool SpriteRenderer::SpriteLocalBounds(Vec3& bmin, Vec3& bmax)
{
	if (!Resolve())
		return false;
	Vec3 p[4];
	LocalCorners(p);
	bmin = Vec3(FLT_MAX, FLT_MAX, -0.01f);
	bmax = Vec3(-FLT_MAX, -FLT_MAX, 0.01f);
	for (const Vec3& v : p)
	{
		bmin.x = (std::min)(bmin.x, v.x); bmin.y = (std::min)(bmin.y, v.y);
		bmax.x = (std::max)(bmax.x, v.x); bmax.y = (std::max)(bmax.y, v.y);
	}
	return true;
}

// ------------------------------------------------------------------ Inspector (Unity 와 같은 순서)
void SpriteRenderer::SortingFields(int& layerId, int& order)
{
	// Sorting Layer: Tags and Layers 의 목록 (+ Add Sorting Layer...)
	const auto& layers = TagsAndLayers::SortingLayers();
	std::vector<const char*> names;
	int cur = 0;
	for (int i = 0; i < (int)layers.size(); ++i)
	{
		names.push_back(layers[i].Name.c_str());
		if (layers[i].Id == layerId) cur = i;
	}
	names.push_back("Add Sorting Layer...");
	if (UnityGUI::Dropdown("Sorting Layer", &cur, names.data(), (int)names.size(), 1))
	{
		if (cur == (int)layers.size())
			ProjectSettingsWindow::Open("Tags and Layers");
		else
			layerId = layers[cur].Id;
	}
	UnityGUI::Int("Order in Layer", &order, 1);
}

void SpriteRenderer::OnInspectorGUI()
{
	// Sprite [ 이름 ⊙ ] — ⊙ = 선택 창, Project 의 그림을 끌어 놓기
	const std::string text = m_Sprite.empty() ? "None (Sprite)" : UISprites::DisplayName(m_Sprite);
	ImVec2 fmin, fmax;
	const int pressed = UnityGUI::ObjectFieldButtons("Sprite", text.c_str(), "texture", nullptr, 0, &fmin, &fmax);
	const ImVec2 after = ImGui::GetCursorScreenPos();
	const std::string key = "sprite:" + std::to_string((uintptr_t)this);
	if (pressed == -1)
	{
		ObjectPicker::Options opt;
		opt.TypeName = "Sprite";
		opt.Icon = "texture";
		opt.Items = UISprites::FindAll2D();
		opt.Current = m_Sprite;
		opt.Describe = [](const std::string& p) {
			UISprites::Info info;
			if (!UISprites::Get(p, info))
				return std::string();
			char buf[64];
			snprintf(buf, sizeof(buf), "%.0f x %.0f", info.Size.x, info.Size.y);
			return std::string(buf);
		};
		ObjectPicker::Open(key, std::move(opt));
	}
	std::string picked;
	if (ObjectPicker::Poll(key, picked))
		SetSprite(picked);
	ImGui::SetCursorScreenPos(fmin);
	ImGui::InvisibleButton("##spriteDrop", ImVec2((std::max)(1.0f, fmax.x - fmin.x - 22.0f), fmax.y - fmin.y));
	if (ImGui::BeginDragDropTarget())
	{
		const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_FILE");
		if (payload == nullptr)
			payload = ImGui::AcceptDragDropPayload("PNG_FILE");   // Project 창의 그림 = 절대 경로
		if (payload)
		{
			std::string dropped(static_cast<const char*>(payload->Data));
			const std::string root = wstring_to_string(PathManager::GetI()->GetContentPathW());
			if (_strnicmp(dropped.c_str(), root.c_str(), root.size()) == 0)
				dropped = dropped.substr(root.size());
			if (UISprites::IsImagePath(dropped))
				SetSprite(dropped);
		}
		ImGui::EndDragDropTarget();
	}
	ImGui::SetCursorScreenPos(after);

	UnityGUI::Color("Color", m_Color);
	UnityGUI::Toggle("Flip X", &m_FlipX);
	UnityGUI::Toggle("Flip Y", &m_FlipY);
	UnityGUI::ValueLabel("Draw Mode", "Simple");
	UnityGUI::ValueLabel("Material", "Sprites-Default");
	if (UnityGUI::FoldoutPlain("Additional Settings"))
		SortingFields(m_SortingLayerId, m_SortingOrder);
	Vec2 size, pivot;
	if (GetSpriteSize(size, pivot))
	{
		char buf[96];
		snprintf(buf, sizeof(buf), "%.0f x %.0f px  =  %.2f x %.2f units", m_SizePx.x, m_SizePx.y, size.x, size.y);
		UnityGUI::ValueLabel("Size", buf);
	}
}

// ------------------------------------------------------------------ 저장
GENERATE_COMPONENT_FUNC_TOJSON(SpriteRenderer)
{
	json j;
	SERIALIZE_TYPE(j, SpriteRenderer);
	j["enabled"] = m_Enabled;
	j["sprite"] = m_Sprite;
	j["color"] = { m_Color[0], m_Color[1], m_Color[2], m_Color[3] };
	j["flipX"] = m_FlipX;
	j["flipY"] = m_FlipY;
	j["sortingOrder"] = m_SortingOrder;
	j["sortingLayerID"] = m_SortingLayerId;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(SpriteRenderer)
{
	m_Enabled = j.value("enabled", true);
	SetSprite(j.value("sprite", std::string()));
	if (j.contains("color") && j["color"].is_array() && j["color"].size() == 4)
		for (int i = 0; i < 4; ++i)
			m_Color[i] = j["color"][i].get<float>();
	m_FlipX = j.value("flipX", false);
	m_FlipY = j.value("flipY", false);
	m_SortingOrder = j.value("sortingOrder", 0);
	m_SortingLayerId = j.value("sortingLayerID", 0);
}
