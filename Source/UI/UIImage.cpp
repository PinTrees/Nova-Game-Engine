#include "pch.h"
#include "UIImage.h"
#include "UIRenderer.h"
#include "UISprites.h"
#include "RectTransform.h"
#include "UnityGUI.h"
#include "ObjectPicker.h"

namespace
{
	// 사각형 안의 로컬 점 → 텍스처 UV (전체 스프라이트를 사각형에 펼친 것 기준, v 는 아래로)
	Vec2 UvOf(const Vec2& p, const Vec2& mn, const Vec2& mx)
	{
		const float u = (mx.x - mn.x) > 1e-6f ? (p.x - mn.x) / (mx.x - mn.x) : 0.0f;
		const float v = (mx.y - mn.y) > 1e-6f ? (p.y - mn.y) / (mx.y - mn.y) : 0.0f;
		return Vec2(u, 1.0f - v);
	}

	// center 에서 각도 a(라디안) 방향으로 사각형 [mn, mx] 경계와 만나는 점
	Vec2 RayToRect(const Vec2& c, float a, const Vec2& mn, const Vec2& mx)
	{
		const Vec2 d(cosf(a), sinf(a));
		float t = FLT_MAX;
		if (d.x > 1e-6f) t = (std::min)(t, (mx.x - c.x) / d.x);
		else if (d.x < -1e-6f) t = (std::min)(t, (mn.x - c.x) / d.x);
		if (d.y > 1e-6f) t = (std::min)(t, (mx.y - c.y) / d.y);
		else if (d.y < -1e-6f) t = (std::min)(t, (mn.y - c.y) / d.y);
		if (t == FLT_MAX || t < 0.0f) t = 0.0f;
		return c + d * t;
	}
}

UIImage::UIImage()
{
	m_InspectorTitleName = "Image";
}

void UIImage::SetNativeSize()
{
	UISprites::Info info;
	RectTransform* rt = GetRect();
	if (rt && UISprites::Get(m_Sprite, info) && info.Size.x > 0.0f)
	{
		// 늘어나지 않는 축만 (Unity 와 같음)
		const Vec2 amin = rt->GetAnchorMin(), amax = rt->GetAnchorMax();
		Vec2 sd = rt->GetSizeDelta();
		if (fabsf(amin.x - amax.x) < 1e-6f) sd.x = info.Size.x;
		if (fabsf(amin.y - amax.y) < 1e-6f) sd.y = info.Size.y;
		rt->SetSizeDelta(sd);
	}
}

// ------------------------------------------------------------------ 그리기
void UIImage::PopulateSimple(UIRenderer& r, const Matrix& world, Vec2 mn, Vec2 mx, uint32 color, GfxShaderResourceView* tex)
{
	const Vec3 p[4] = { ToWorld(world, mn.x, mn.y), ToWorld(world, mn.x, mx.y), ToWorld(world, mx.x, mx.y), ToWorld(world, mx.x, mn.y) };
	const Vec2 uv[4] = { Vec2(0, 1), Vec2(0, 0), Vec2(1, 0), Vec2(1, 1) };
	r.AddQuad(p, uv, color, tex);
}

void UIImage::PopulateSliced(UIRenderer& r, const Matrix& world, Vec2 mn, Vec2 mx, uint32 color, GfxShaderResourceView* tex, const Vec4& border, const Vec2& texSize)
{
	// 9 조각: 모서리는 크기 그대로, 가장자리/가운데는 늘인다. 사각형이 작으면 테두리를 비율대로 줄인다
	const float mul = 1.0f / (std::max)(0.01f, m_PixelsPerUnitMultiplier);
	float l = border.x * mul, b = border.y * mul, rr = border.z * mul, t = border.w * mul;
	const float w = mx.x - mn.x, h = mx.y - mn.y;
	if (l + rr > w && l + rr > 0.0f) { const float s = w / (l + rr); l *= s; rr *= s; }
	if (b + t > h && b + t > 0.0f) { const float s = h / (b + t); b *= s; t *= s; }
	const float xs[4] = { mn.x, mn.x + l, mx.x - rr, mx.x };
	const float ys[4] = { mn.y, mn.y + b, mx.y - t, mx.y };
	const float us[4] = { 0.0f, border.x / texSize.x, 1.0f - border.z / texSize.x, 1.0f };
	const float vs[4] = { 1.0f, 1.0f - border.y / texSize.y, border.w / texSize.y, 0.0f };   // 아래 → 위
	for (int iy = 0; iy < 3; ++iy)
		for (int ix = 0; ix < 3; ++ix)
		{
			if (ix == 1 && iy == 1 && !m_FillCenter)
				continue;
			if (xs[ix + 1] - xs[ix] <= 0.0f || ys[iy + 1] - ys[iy] <= 0.0f)
				continue;
			const Vec3 p[4] = { ToWorld(world, xs[ix], ys[iy]), ToWorld(world, xs[ix], ys[iy + 1]), ToWorld(world, xs[ix + 1], ys[iy + 1]), ToWorld(world, xs[ix + 1], ys[iy]) };
			const Vec2 uv[4] = { Vec2(us[ix], vs[iy]), Vec2(us[ix], vs[iy + 1]), Vec2(us[ix + 1], vs[iy + 1]), Vec2(us[ix + 1], vs[iy]) };
			r.AddQuad(p, uv, color, tex);
		}
}

void UIImage::PopulateFilled(UIRenderer& r, const Matrix& world, Vec2 mn, Vec2 mx, uint32 color, GfxShaderResourceView* tex)
{
	const float fill = std::clamp(m_FillAmount, 0.0f, 1.0f);
	if (fill <= 0.0f)
		return;
	if (m_FillMethod == FillMethod::Horizontal || m_FillMethod == FillMethod::Vertical)
	{
		Vec2 a = mn, b = mx;
		if (m_FillMethod == FillMethod::Horizontal)
		{
			if (m_FillOrigin == 0) b.x = mn.x + (mx.x - mn.x) * fill;   // Left
			else a.x = mx.x - (mx.x - mn.x) * fill;                     // Right
		}
		else
		{
			if (m_FillOrigin == 0) b.y = mn.y + (mx.y - mn.y) * fill;   // Bottom
			else a.y = mx.y - (mx.y - mn.y) * fill;                     // Top
		}
		const Vec3 p[4] = { ToWorld(world, a.x, a.y), ToWorld(world, a.x, b.y), ToWorld(world, b.x, b.y), ToWorld(world, b.x, a.y) };
		const Vec2 uv[4] = { UvOf(Vec2(a.x, a.y), mn, mx), UvOf(Vec2(a.x, b.y), mn, mx), UvOf(Vec2(b.x, b.y), mn, mx), UvOf(Vec2(b.x, a.y), mn, mx) };
		r.AddQuad(p, uv, color, tex);
		return;
	}

	// 원형 채우기: 중심에서 부채꼴을 사각형 경계까지 (Unity Radial 90 / 180 / 360)
	const float deg = XM_PI / 180.0f;
	Vec2 c = (mn + mx) * 0.5f;
	float span = 360.0f, startCW = 0.0f;   // 시계 방향일 때 시작 각 (도)
	const int o = m_FillOrigin;
	if (m_FillMethod == FillMethod::Radial360)
	{
		static const float starts[4] = { -90.0f, 0.0f, 90.0f, 180.0f };   // Bottom, Right, Top, Left
		startCW = starts[o & 3];
	}
	else if (m_FillMethod == FillMethod::Radial180)
	{
		span = 180.0f;
		switch (o & 3)
		{
		case 0: c = Vec2((mn.x + mx.x) * 0.5f, mn.y); startCW = 180.0f; break;   // Bottom
		case 1: c = Vec2(mn.x, (mn.y + mx.y) * 0.5f); startCW = 90.0f; break;    // Left
		case 2: c = Vec2((mn.x + mx.x) * 0.5f, mx.y); startCW = 0.0f; break;     // Top
		default: c = Vec2(mx.x, (mn.y + mx.y) * 0.5f); startCW = 270.0f; break;  // Right
		}
	}
	else
	{
		span = 90.0f;
		switch (o & 3)
		{
		case 0: c = mn; startCW = 90.0f; break;                          // Bottom Left
		case 1: c = Vec2(mn.x, mx.y); startCW = 0.0f; break;             // Top Left
		case 2: c = mx; startCW = 270.0f; break;                         // Top Right
		default: c = Vec2(mx.x, mn.y); startCW = 180.0f; break;          // Bottom Right
		}
	}
	const float sweep = span * fill;
	const float a0 = m_Clockwise ? startCW : startCW - span;   // 반시계면 반대쪽 끝에서 시작
	const float dir = m_Clockwise ? -1.0f : 1.0f;
	// 부채꼴 경계의 각도들: 일정 간격 + 사각형 모서리 각 (모서리가 잘리지 않게)
	std::vector<float> angles;
	const int steps = (std::max)(2, (int)ceilf(sweep / 6.0f));
	for (int i = 0; i <= steps; ++i)
		angles.push_back(a0 + dir * sweep * (float)i / (float)steps);
	const Vec2 corners[4] = { mn, Vec2(mn.x, mx.y), mx, Vec2(mx.x, mn.y) };
	for (const Vec2& k : corners)
	{
		const Vec2 d = k - c;
		if (d.LengthSquared() < 1e-6f)
			continue;
		float ang = atan2f(d.y, d.x) / deg;
		// a0 에서 dir 방향으로 몇 도 떨어져 있는지 (0..360)
		float rel = fmodf((ang - a0) * dir + 720.0f, 360.0f);
		if (rel > 0.0f && rel < sweep)
			angles.push_back(a0 + dir * rel);
	}
	std::sort(angles.begin(), angles.end(), [&](float x, float y) { return (x - a0) * dir < (y - a0) * dir; });
	const Vec3 cw = ToWorld(world, c.x, c.y);
	const Vec2 cuv = UvOf(c, mn, mx);
	for (size_t i = 0; i + 1 < angles.size(); ++i)
	{
		const Vec2 p0 = RayToRect(c, angles[i] * deg, mn, mx);
		const Vec2 p1 = RayToRect(c, angles[i + 1] * deg, mn, mx);
		const Vec3 p[3] = { cw, ToWorld(world, p0.x, p0.y), ToWorld(world, p1.x, p1.y) };
		const Vec2 uv[3] = { cuv, UvOf(p0, mn, mx), UvOf(p1, mn, mx) };
		r.AddTriangle(p, uv, color, tex);
	}
}

void UIImage::Populate(UIRenderer& r, float canvasScale)
{
	RectTransform* rt = GetRect();
	if (rt == nullptr || m_pGameObject == nullptr)
		return;
	const Matrix world = m_pGameObject->GetTransform()->GetWorldMatrix();
	Vec2 mn = rt->GetRectMin(), mx = rt->GetRectMin() + rt->GetRectSize();
	UISprites::Info info;
	const bool hasSprite = UISprites::Get(m_Sprite, info);
	GfxShaderResourceView* tex = hasSprite ? info.Texture : r.WhiteTexture();
	const uint32 color = PackedColor();

	// Preserve Aspect: 스프라이트 비율로 사각형 안에 맞춘다 (피벗 쪽 기준)
	if (m_PreserveAspect && hasSprite && info.Size.x > 0.0f && info.Size.y > 0.0f && (m_Type == Type::Simple || m_Type == Type::Filled))
	{
		const Vec2 size = mx - mn;
		const float spriteAspect = info.Size.x / info.Size.y, rectAspect = size.x / (std::max)(1e-6f, size.y);
		const Vec2 pivot = rt->GetPivot();
		if (rectAspect > spriteAspect)
		{
			const float nw = size.y * spriteAspect;
			mn.x += (size.x - nw) * pivot.x;
			mx.x = mn.x + nw;
		}
		else
		{
			const float nh = size.x / spriteAspect;
			mn.y += (size.y - nh) * pivot.y;
			mx.y = mn.y + nh;
		}
	}

	switch (m_Type)
	{
	case Type::Sliced:
		if (hasSprite && (info.Border.x + info.Border.y + info.Border.z + info.Border.w) > 0.0f)
			PopulateSliced(r, world, mn, mx, color, tex, info.Border, info.Size);
		else
			PopulateSimple(r, world, mn, mx, color, tex);
		break;
	case Type::Filled:
		PopulateFilled(r, world, mn, mx, color, tex);
		break;
	default:
		PopulateSimple(r, world, mn, mx, color, tex);
		break;
	}
}

// ------------------------------------------------------------------ Inspector
void UIImage::OnInspectorGUI()
{
	// Source Image [ 이름 ⊙ ] (⊙ = 선택 창, Project 의 이미지를 끌어 놓기)
	const std::string text = m_Sprite.empty() ? "None (Sprite)" : UISprites::DisplayName(m_Sprite);
	ImVec2 fmin, fmax;
	const int pressed = UnityGUI::ObjectFieldButtons("Source Image", text.c_str(), "texture", nullptr, 0, &fmin, &fmax);
	const ImVec2 after = ImGui::GetCursorScreenPos();
	const std::string key = "uiimage:" + std::to_string((uintptr_t)this);
	if (pressed == -1)
	{
		ObjectPicker::Options opt;
		opt.TypeName = "Sprite";
		opt.Icon = "texture";
		opt.Items = UISprites::FindAll();
		opt.Current = m_Sprite;
		opt.Describe = [](const std::string& p) {
			UISprites::Info info;
			if (!UISprites::Get(p, info))
				return std::string();
			char buf[96];
			snprintf(buf, sizeof(buf), "%.0f x %.0f%s", info.Size.x, info.Size.y, info.Border.x > 0 ? "  (9-slice border)" : "");
			return std::string(buf);
		};
		ObjectPicker::Open(key, std::move(opt));
	}
	std::string picked;
	if (ObjectPicker::Poll(key, picked))
		m_Sprite = picked;
	ImGui::SetCursorScreenPos(fmin);
	ImGui::InvisibleButton("##spriteDrop", ImVec2((std::max)(1.0f, fmax.x - fmin.x - 22.0f), fmax.y - fmin.y));
	if (ImGui::BeginDragDropTarget())
	{
		const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_FILE");
		if (payload == nullptr)
			payload = ImGui::AcceptDragDropPayload("PNG_FILE");   // Project 창의 그림 = 절대 경로 → 프로젝트 기준으로
		if (payload)
		{
			std::string dropped(static_cast<const char*>(payload->Data));
			const std::string root = wstring_to_string(PathManager::GetI()->GetContentPathW());
			if (_strnicmp(dropped.c_str(), root.c_str(), root.size()) == 0)
				dropped = dropped.substr(root.size());
			if (UISprites::IsImagePath(dropped))
				m_Sprite = dropped;
		}
		ImGui::EndDragDropTarget();
	}
	ImGui::SetCursorScreenPos(after);

	DrawColorAndRaycast();

	if (!m_Sprite.empty())
	{
		static const char* kTypes[] = { "Simple", "Sliced", "Tiled", "Filled" };
		int type = (int)m_Type;
		if (UnityGUI::Dropdown("Image Type", &type, kTypes, 4))
			m_Type = (Type)type;
		if (m_Type == Type::Simple || m_Type == Type::Tiled)
		{
			if (m_Type == Type::Tiled)
				UnityGUI::HelpBox("Tiled is drawn like Simple in this version.", true, 1);
			UnityGUI::Toggle("Preserve Aspect", &m_PreserveAspect, 1);
		}
		else if (m_Type == Type::Sliced)
		{
			UISprites::Info info;
			if (UISprites::Get(m_Sprite, info) && info.Border.x + info.Border.y + info.Border.z + info.Border.w <= 0.0f)
				UnityGUI::HelpBox("This Image doesn't have a border.", true, 1);
			UnityGUI::Toggle("Fill Center", &m_FillCenter, 1);
			UnityGUI::Float("Pixels Per Unit Multiplier", &m_PixelsPerUnitMultiplier, 1);
		}
	}
	else
	{
		// 스프라이트가 없어도 Filled 는 쓸 수 있게 (체력 바)
		static const char* kTypes[] = { "Simple", "Sliced", "Tiled", "Filled" };
		int type = (int)m_Type;
		if (UnityGUI::Dropdown("Image Type", &type, kTypes, 4))
			m_Type = (Type)type;
	}
	if (m_Type == Type::Filled)
	{
		static const char* kMethods[] = { "Horizontal", "Vertical", "Radial 90", "Radial 180", "Radial 360" };
		int method = (int)m_FillMethod;
		if (UnityGUI::Dropdown("Fill Method", &method, kMethods, 5, 1))
		{
			m_FillMethod = (FillMethod)method;
			m_FillOrigin = 0;
		}
		static const char* kH[] = { "Left", "Right" };
		static const char* kV[] = { "Bottom", "Top" };
		static const char* k90[] = { "Bottom Left", "Top Left", "Top Right", "Bottom Right" };
		static const char* k180[] = { "Bottom", "Left", "Top", "Right" };
		static const char* k360[] = { "Bottom", "Right", "Top", "Left" };
		const char* const* origins = kH;
		int count = 2;
		switch (m_FillMethod)
		{
		case FillMethod::Vertical: origins = kV; break;
		case FillMethod::Radial90: origins = k90; count = 4; break;
		case FillMethod::Radial180: origins = k180; count = 4; break;
		case FillMethod::Radial360: origins = k360; count = 4; break;
		default: break;
		}
		UnityGUI::Dropdown("Fill Origin", &m_FillOrigin, origins, count, 1);
		UnityGUI::Slider("Fill Amount", &m_FillAmount, 0.0f, 1.0f, 1);
		if (m_FillMethod >= FillMethod::Radial90)
			UnityGUI::Toggle("Clockwise", &m_Clockwise, 1);
		UnityGUI::Toggle("Preserve Aspect", &m_PreserveAspect, 1);
	}
	if (!m_Sprite.empty() && m_Type != Type::Sliced && m_Type != Type::Tiled)
	{
		UnityGUI::Spacing(2.0f);
		// Unity: 오른쪽 아래 [Set Native Size]
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const float w = ImGui::GetContentRegionAvail().x;
		ImGui::SetCursorScreenPos(ImVec2(p.x + w - 132.0f, p.y));
		if (ImGui::Button("Set Native Size", ImVec2(120.0f, 20.0f)))
			SetNativeSize();
		ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + 24.0f));
		ImGui::Dummy(ImVec2(w, 0));
	}
}

// ------------------------------------------------------------------ 저장
GENERATE_COMPONENT_FUNC_TOJSON(UIImage)
{
	json j;
	SERIALIZE_TYPE(j, UIImage);
	GraphicToJson(j);
	j["sprite"] = m_Sprite;
	j["imageType"] = (int)m_Type;
	j["preserveAspect"] = m_PreserveAspect;
	j["fillCenter"] = m_FillCenter;
	j["fillMethod"] = (int)m_FillMethod;
	j["fillOrigin"] = m_FillOrigin;
	j["fillAmount"] = m_FillAmount;
	j["clockwise"] = m_Clockwise;
	j["pixelsPerUnitMultiplier"] = m_PixelsPerUnitMultiplier;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(UIImage)
{
	GraphicFromJson(j);
	m_Sprite = j.value("sprite", std::string());
	m_Type = (Type)j.value("imageType", 0);
	m_PreserveAspect = j.value("preserveAspect", false);
	m_FillCenter = j.value("fillCenter", true);
	m_FillMethod = (FillMethod)j.value("fillMethod", 4);
	m_FillOrigin = j.value("fillOrigin", 0);
	m_FillAmount = j.value("fillAmount", 1.0f);
	m_Clockwise = j.value("clockwise", true);
	m_PixelsPerUnitMultiplier = j.value("pixelsPerUnitMultiplier", 1.0f);
}
