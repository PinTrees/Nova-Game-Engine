#include "pch.h"
#include "UIScriptBindings.h"
#include "ScriptBindings.h"
#include "RectTransform.h"
#include "UICanvas.h"
#include "UIImage.h"
#include "UIText.h"
#include "UIButton.h"
#include "UIToggle.h"
#include "UISlider.h"
#include "UIInputField.h"
#include "UIMask.h"
#include "UIRenderer.h"
#include "UILayout.h"

namespace
{
	GameObject* Find(uint64 id) { return ScriptBindings::FindObject(id); }

	UIGraphic* GraphicOf(GameObject* g, int kind)
	{
		if (g == nullptr)
			return nullptr;
		if (kind == 1)
			return g->GetComponentIncludingPending<Text>();
		return g->GetComponentIncludingPending<UIImage>();
	}
}

namespace UIScriptBindings
{
	int GetVec(uint64 id, int prop, Vec4* out)
	{
		if (out == nullptr)
			return 0;
		*out = Vec4(0, 0, 0, 0);
		GameObject* g = Find(id);
		if (g == nullptr)
			return 0;
		if (prop < 10)
		{
			RectTransform* rt = g->GetComponentIncludingPending<RectTransform>();
			if (rt == nullptr)
				return 0;
			Vec2 v;
			switch (prop)
			{
			case 0: v = rt->GetAnchoredPosition(); break;
			case 1: v = rt->GetSizeDelta(); break;
			case 2: v = rt->GetAnchorMin(); break;
			case 3: v = rt->GetAnchorMax(); break;
			case 4: v = rt->GetPivot(); break;
			case 5: *out = Vec4(rt->GetRectMin().x, rt->GetRectMin().y, rt->GetRectSize().x, rt->GetRectSize().y); return 1;
			case 6: v = rt->GetOffsetMin(); break;
			case 7: v = rt->GetOffsetMax(); break;
			default: return 0;
			}
			*out = Vec4(v.x, v.y, 0, 0);
			return 1;
		}
		if (prop >= 230 && prop < 300)
		{
			// 레이아웃 (230 그룹, 240 Grid, 250 Content Size Fitter, 260 Layout Element, 270 Aspect Ratio Fitter)
			if (prop < 240)
			{
				LayoutGroup* lg = g->GetComponentIncludingPending<LayoutGroup>();
				if (lg == nullptr) return 0;
				auto* hv = dynamic_cast<HorizontalOrVerticalLayoutGroup*>(lg);
				auto* grid = dynamic_cast<GridLayoutGroup*>(lg);
				switch (prop)
				{
				case 230: *out = Vec4((float)lg->PaddingLeft, (float)lg->PaddingRight, (float)lg->PaddingTop, (float)lg->PaddingBottom); break;
				case 231: out->x = (float)lg->ChildAlignment; break;
				case 232: if (hv) out->x = hv->Spacing; else if (grid) *out = Vec4(grid->Spacing.x, grid->Spacing.y, 0, 0); break;
				case 233: if (hv) *out = Vec4(hv->ChildControlWidth ? 1.0f : 0.0f, hv->ChildControlHeight ? 1.0f : 0.0f, 0, 0); break;
				case 234: if (hv) *out = Vec4(hv->ChildForceExpandWidth ? 1.0f : 0.0f, hv->ChildForceExpandHeight ? 1.0f : 0.0f, 0, 0); break;
				case 235: if (hv) *out = Vec4(hv->ChildScaleWidth ? 1.0f : 0.0f, hv->ChildScaleHeight ? 1.0f : 0.0f, 0, 0); break;
				case 236: if (hv) out->x = hv->ReverseArrangement ? 1.0f : 0.0f; break;
				case 237: *out = Vec4(lg->TotalMin(0), lg->TotalMin(1), lg->TotalPreferred(0), lg->TotalPreferred(1)); break;
				default: return 0;
				}
				return 1;
			}
			if (prop < 250)
			{
				GridLayoutGroup* grid = g->GetComponentIncludingPending<GridLayoutGroup>();
				if (grid == nullptr) return 0;
				if (prop == 240) *out = Vec4(grid->CellSize.x, grid->CellSize.y, 0, 0);
				else if (prop == 241) *out = Vec4((float)grid->ConstraintMode, (float)grid->ConstraintCount, 0, 0);
				else if (prop == 242) *out = Vec4((float)grid->StartCorner, (float)grid->StartAxis, 0, 0);
				else return 0;
				return 1;
			}
			if (prop < 260)
			{
				ContentSizeFitter* f = g->GetComponentIncludingPending<ContentSizeFitter>();
				if (f == nullptr) return 0;
				*out = Vec4((float)f->HorizontalFit, (float)f->VerticalFit, 0, 0);
				return 1;
			}
			if (prop < 270)
			{
				LayoutElement* le = g->GetComponentIncludingPending<LayoutElement>();
				if (le == nullptr) return 0;
				switch (prop)
				{
				case 260: *out = Vec4(le->MinWidth, le->MinHeight, 0, 0); break;
				case 261: *out = Vec4(le->PreferredWidth, le->PreferredHeight, 0, 0); break;
				case 262: *out = Vec4(le->FlexibleWidth, le->FlexibleHeight, 0, 0); break;
				case 263: out->x = le->IgnoreLayout ? 1.0f : 0.0f; break;
				case 264: out->x = (float)le->LayoutPriority; break;
				default: return 0;
				}
				return 1;
			}
			if (prop == 270)
			{
				AspectRatioFitter* a = g->GetComponentIncludingPending<AspectRatioFitter>();
				if (a == nullptr) return 0;
				*out = Vec4((float)a->AspectMode, a->AspectRatio, 0, 0);
				return 1;
			}
			if (prop == 271)
			{
				// LayoutUtility.GetMin/Preferred/FlexibleSize (x min w, y min h, z pref w, w pref h)
				RectTransform* rt = RectTransform::Of(g);
				if (rt == nullptr) return 0;
				*out = Vec4(UILayout::GetMinSize(rt, 0), UILayout::GetMinSize(rt, 1), UILayout::GetPreferredSize(rt, 0), UILayout::GetPreferredSize(rt, 1));
				return 1;
			}
			return 0;
		}
		if (prop >= 130 && prop < 200)
		{
			// Text (TextMeshPro 기능)
			Text* t = g->GetComponentIncludingPending<Text>();
			if (t == nullptr)
				return 0;
			const UIRenderer::TextMaterial& m = t->Material;
			switch (prop)
			{
			case 130: out->x = t->CharacterSpacing; break;
			case 131: out->x = t->WordSpacing; break;
			case 132: out->x = t->ParagraphSpacing; break;
			case 133: *out = t->Margin; break;
			case 134: out->x = (float)t->MaxVisibleCharacters; break;
			case 135: out->x = t->GetWrap() ? 1.0f : 0.0f; break;
			case 136: out->x = (float)t->GetVOverflow(); break;
			case 137: out->x = t->GetRichText() ? 1.0f : 0.0f; break;
			case 138: out->x = t->GetAutoSize() ? 1.0f : 0.0f; break;
			case 139: *out = Vec4((float)t->GetMinSize(), (float)t->GetMaxSize(), 0, 0); break;
			case 140: out->x = (float)t->GetStyleBits(); break;
			case 141: out->x = m.OutlineWidth; break;
			case 142: *out = Vec4(m.OutlineColor[0], m.OutlineColor[1], m.OutlineColor[2], m.OutlineColor[3]); break;
			case 143: *out = Vec4(m.FaceDilate, m.Softness, 0, 0); break;
			case 144: *out = Vec4(m.UnderlayColor[0], m.UnderlayColor[1], m.UnderlayColor[2], m.UnderlayColor[3]); break;
			case 145: *out = Vec4(t->Underlay ? 1.0f : 0.0f, m.UnderlayOffsetX, m.UnderlayOffsetY, m.UnderlaySoftness); break;
			case 146: *out = Vec4(t->GetPreferredWidth(), t->GetPreferredHeight(), 0, 0); break;
			case 147: out->x = t->IsOverflowing() ? 1.0f : 0.0f; break;
			case 148: out->x = (float)t->GetUsedFontSize(); break;
			default: return 0;
			}
			return 1;
		}
		const int local = prop % 100;
		if (local >= 10 && local <= 12)
		{
			UIGraphic* gr = GraphicOf(g, prop / 100);
			if (gr == nullptr)
				return 0;
			if (local == 10) { const float* c = gr->GetColor(); *out = Vec4(c[0], c[1], c[2], c[3]); }
			else if (local == 11) out->x = gr->IsRaycastTarget() ? 1.0f : 0.0f;
			else out->x = gr->IsEnabled() ? 1.0f : 0.0f;
			return 1;
		}
		if (prop >= 20 && prop <= 25)
		{
			UIImage* img = g->GetComponentIncludingPending<UIImage>();
			if (img == nullptr)
				return 0;
			switch (prop)
			{
			case 20: out->x = img->GetFillAmount(); break;
			case 21: out->x = (float)img->GetImageType(); break;
			case 22: out->x = (float)img->GetFillMethod(); break;
			case 23: out->x = (float)img->GetFillOrigin(); break;
			case 24: out->x = img->GetPreserveAspect() ? 1.0f : 0.0f; break;
			default: break;
			}
			return 1;
		}
		if (prop >= 30 && prop <= 33)
		{
			Text* t = g->GetComponentIncludingPending<Text>();
			if (t == nullptr)
				return 0;
			switch (prop)
			{
			case 30: out->x = (float)t->GetFontSize(); break;
			case 31: out->x = (float)t->GetAlignment(); break;
			case 32: out->x = t->GetLineSpacing(); break;
			case 33: out->x = (float)t->GetStyle(); break;
			}
			return 1;
		}
		if (prop == 40 || prop == 41)
		{
			UISelectable* b = g->GetComponentIncludingPending<UISelectable>();
			if (b == nullptr)
				return 0;
			out->x = (prop == 40 ? b->IsInteractable() : b->IsEnabled()) ? 1.0f : 0.0f;
			return 1;
		}
		if (prop == 50 || prop == 51)
		{
			Canvas* c = g->GetComponentIncludingPending<Canvas>();
			if (c == nullptr)
				return 0;
			out->x = prop == 50 ? (float)c->GetSortOrder() : c->GetScaleFactor();
			return 1;
		}
		if (prop >= 60 && prop <= 65)
		{
			Slider* s = g->GetComponentIncludingPending<Slider>();
			if (s == nullptr)
				return 0;
			switch (prop)
			{
			case 60: case 65: out->x = s->GetValue(); break;
			case 61: out->x = s->GetMin(); break;
			case 62: out->x = s->GetMax(); break;
			case 63: out->x = s->GetWholeNumbers() ? 1.0f : 0.0f; break;
			case 64: out->x = s->GetNormalized(); break;
			}
			return 1;
		}
		if (prop == 70 || prop == 71)
		{
			Toggle* t = g->GetComponentIncludingPending<Toggle>();
			if (t == nullptr)
				return 0;
			out->x = t->IsOn() ? 1.0f : 0.0f;
			return 1;
		}
		if (prop == 75 || prop == 76)
		{
			InputField* f = g->GetComponentIncludingPending<InputField>();
			if (f == nullptr)
				return 0;
			out->x = prop == 75 ? (float)f->GetCharacterLimit() : (f->IsFocused() ? 1.0f : 0.0f);
			return 1;
		}
		if (prop == 80)
		{
			ScrollRect* s = g->GetComponentIncludingPending<ScrollRect>();
			if (s == nullptr)
				return 0;
			const Vec2 n = s->GetNormalizedPosition();
			*out = Vec4(n.x, n.y, 0, 0);
			return 1;
		}
		return 0;
	}

	void SetVec(uint64 id, int prop, Vec4* in)
	{
		GameObject* g = Find(id);
		if (g == nullptr || in == nullptr)
			return;
		const Vec4 v = *in;
		if (prop < 10)
		{
			RectTransform* rt = g->GetComponentIncludingPending<RectTransform>();
			if (rt == nullptr)
				return;
			const Vec2 v2(v.x, v.y);
			switch (prop)
			{
			case 0: rt->SetAnchoredPosition(v2); break;
			case 1: rt->SetSizeDelta(v2); break;
			case 2: rt->SetAnchorMin(v2); break;
			case 3: rt->SetAnchorMax(v2); break;
			case 4: rt->SetPivot(v2); break;
			case 6: rt->SetOffsets(v2, rt->GetOffsetMax()); break;
			case 7: rt->SetOffsets(rt->GetOffsetMin(), v2); break;
			default: break;
			}
			return;
		}
		if (prop >= 230 && prop < 300)
		{
			if (prop < 240)
			{
				LayoutGroup* lg = g->GetComponentIncludingPending<LayoutGroup>();
				if (lg == nullptr) return;
				auto* hv = dynamic_cast<HorizontalOrVerticalLayoutGroup*>(lg);
				auto* grid = dynamic_cast<GridLayoutGroup*>(lg);
				switch (prop)
				{
				case 230: lg->PaddingLeft = (int)v.x; lg->PaddingRight = (int)v.y; lg->PaddingTop = (int)v.z; lg->PaddingBottom = (int)v.w; break;
				case 231: lg->ChildAlignment = std::clamp((int)v.x, 0, 8); break;
				case 232: if (hv) hv->Spacing = v.x; else if (grid) grid->Spacing = Vec2(v.x, v.y); break;
				case 233: if (hv) { hv->ChildControlWidth = v.x != 0.0f; hv->ChildControlHeight = v.y != 0.0f; } break;
				case 234: if (hv) { hv->ChildForceExpandWidth = v.x != 0.0f; hv->ChildForceExpandHeight = v.y != 0.0f; } break;
				case 235: if (hv) { hv->ChildScaleWidth = v.x != 0.0f; hv->ChildScaleHeight = v.y != 0.0f; } break;
				case 236: if (hv) hv->ReverseArrangement = v.x != 0.0f; break;
				}
				return;
			}
			if (prop < 250)
			{
				if (GridLayoutGroup* grid = g->GetComponentIncludingPending<GridLayoutGroup>())
				{
					if (prop == 240) grid->CellSize = Vec2(v.x, v.y);
					else if (prop == 241) { grid->ConstraintMode = std::clamp((int)v.x, 0, 2); grid->ConstraintCount = (std::max)(1, (int)v.y); }
					else if (prop == 242) { grid->StartCorner = std::clamp((int)v.x, 0, 3); grid->StartAxis = std::clamp((int)v.y, 0, 1); }
				}
				return;
			}
			if (prop < 260)
			{
				if (ContentSizeFitter* f = g->GetComponentIncludingPending<ContentSizeFitter>())
				{
					f->HorizontalFit = std::clamp((int)v.x, 0, 2);
					f->VerticalFit = std::clamp((int)v.y, 0, 2);
				}
				return;
			}
			if (prop < 270)
			{
				if (LayoutElement* le = g->GetComponentIncludingPending<LayoutElement>())
					switch (prop)
					{
					case 260: le->MinWidth = v.x; le->MinHeight = v.y; break;
					case 261: le->PreferredWidth = v.x; le->PreferredHeight = v.y; break;
					case 262: le->FlexibleWidth = v.x; le->FlexibleHeight = v.y; break;
					case 263: le->IgnoreLayout = v.x != 0.0f; break;
					case 264: le->LayoutPriority = (int)v.x; break;
					}
				return;
			}
			if (prop == 270)
			{
				if (AspectRatioFitter* a = g->GetComponentIncludingPending<AspectRatioFitter>())
				{
					a->AspectMode = std::clamp((int)v.x, 0, 4);
					a->AspectRatio = (std::max)(0.001f, v.y);
				}
				return;
			}
			if (prop == 280)
				UILayout::ForceRebuild(g);   // LayoutRebuilder.ForceRebuildLayoutImmediate
			return;
		}
		if (prop >= 130 && prop < 200)
		{
			Text* t = g->GetComponentIncludingPending<Text>();
			if (t == nullptr)
				return;
			UIRenderer::TextMaterial& m = t->Material;
			switch (prop)
			{
			case 130: t->CharacterSpacing = v.x; break;
			case 131: t->WordSpacing = v.x; break;
			case 132: t->ParagraphSpacing = v.x; break;
			case 133: t->Margin = v; break;
			case 134: t->MaxVisibleCharacters = (std::max)(-1, (int)v.x); break;
			case 135: t->SetWrap(v.x != 0.0f); break;
			case 136: t->SetOverflow(t->GetWrap() ? Text::HOverflow::Wrap : Text::HOverflow::Overflow, (Text::VOverflow)std::clamp((int)v.x, 0, 2)); break;
			case 137: t->SetRichText(v.x != 0.0f); break;
			case 138: t->SetAutoSize(v.x != 0.0f); break;
			case 139: t->SetMinMaxSize((int)v.x, (int)v.y); break;
			case 140: t->SetStyleBits((int)v.x); break;
			case 141: m.OutlineWidth = std::clamp(v.x, 0.0f, 1.0f); break;
			case 142: m.OutlineColor[0] = v.x; m.OutlineColor[1] = v.y; m.OutlineColor[2] = v.z; m.OutlineColor[3] = v.w; break;
			case 143: m.FaceDilate = std::clamp(v.x, -1.0f, 1.0f); m.Softness = std::clamp(v.y, 0.0f, 1.0f); break;
			case 144: m.UnderlayColor[0] = v.x; m.UnderlayColor[1] = v.y; m.UnderlayColor[2] = v.z; m.UnderlayColor[3] = v.w; break;
			case 145: t->Underlay = v.x != 0.0f; m.UnderlayOffsetX = v.y; m.UnderlayOffsetY = v.z; m.UnderlaySoftness = v.w; break;
			default: break;
			}
			return;
		}
		const int local = prop % 100;
		if (local >= 10 && local <= 12)
		{
			UIGraphic* gr = GraphicOf(g, prop / 100);
			if (gr == nullptr)
				return;
			if (local == 10) { const float c[4] = { v.x, v.y, v.z, v.w }; gr->SetColor(c); }
			else if (local == 11) gr->SetRaycastTarget(v.x != 0.0f);
			else gr->SetEnabled(v.x != 0.0f);
			return;
		}
		if (prop >= 20 && prop <= 25)
		{
			if (UIImage* img = g->GetComponentIncludingPending<UIImage>())
				switch (prop)
				{
				case 20: img->SetFillAmount(v.x); break;
				case 21: img->SetImageType((UIImage::Type)std::clamp((int)v.x, 0, 3)); break;
				case 22: img->SetFillMethod((UIImage::FillMethod)std::clamp((int)v.x, 0, 4)); break;
				case 23: img->SetFillOrigin((int)v.x); break;
				case 24: img->SetPreserveAspect(v.x != 0.0f); break;
				case 25: img->SetNativeSize(); break;
				}
			return;
		}
		if (prop >= 30 && prop <= 33)
		{
			if (Text* t = g->GetComponentIncludingPending<Text>())
				switch (prop)
				{
				case 30: t->SetFontSize((int)v.x); break;
				case 31: t->SetAlignment((int)v.x); break;
				case 32: t->SetLineSpacing(v.x); break;
				case 33: t->SetStyle((Text::Style)std::clamp((int)v.x, 0, 3)); break;
				}
			return;
		}
		if (prop == 40 || prop == 41)
		{
			if (UISelectable* b = g->GetComponentIncludingPending<UISelectable>())
			{
				if (prop == 40) b->SetInteractable(v.x != 0.0f);
				else b->SetEnabled(v.x != 0.0f);
			}
			return;
		}
		if (prop == 50)
			if (Canvas* c = g->GetComponentIncludingPending<Canvas>())
				c->SetSortOrder((int)v.x);
		if (prop >= 60 && prop <= 65)
			if (Slider* s = g->GetComponentIncludingPending<Slider>())
				switch (prop)
				{
				case 60: s->SetValue(v.x, true); break;
				case 61: s->SetMin(v.x); break;
				case 62: s->SetMax(v.x); break;
				case 63: s->SetWholeNumbers(v.x != 0.0f); break;
				case 64: s->SetNormalized(v.x, true); break;
				case 65: s->SetValue(v.x, false); break;
				}
		if (prop == 70 || prop == 71)
			if (Toggle* t = g->GetComponentIncludingPending<Toggle>())
				t->SetIsOn(v.x != 0.0f, prop == 70);
		if (prop == 75)
			if (InputField* f = g->GetComponentIncludingPending<InputField>())
				f->SetCharacterLimit((int)v.x);
		if (prop == 80)
			if (ScrollRect* s = g->GetComponentIncludingPending<ScrollRect>())
				s->SetNormalizedPosition(Vec2(v.x, v.y));
	}

	// textInfo · 링크 · 글자 애니메이션 (kind 번호는 UIScriptBindings.h)
	int TextInfo(uint64 id, int kind, int index, float* out, int max)
	{
		GameObject* g = Find(id);
		Text* t = g ? g->GetComponentIncludingPending<Text>() : nullptr;
		if (t == nullptr)
			return 0;
		auto put = [&](std::initializer_list<float> v) {
			int i = 0;
			for (float f : v) { if (out && i < max) out[i] = f; ++i; }
			return 1;
		};
		switch (kind)
		{
		case 0: return (int)t->GetCharacters().size();
		case 1:
		{
			const auto& cs = t->GetCharacters();
			if (index < 0 || index >= (int)cs.size()) return 0;
			const Text::CharInfo& c = cs[index];
			return put({ (float)c.Char, (float)c.Source, c.Visible ? 1.0f : 0.0f, (float)c.Line, c.BottomLeft.x, c.BottomLeft.y, c.TopRight.x, c.TopRight.y, (float)c.Link, c.Size });
		}
		case 2: return (int)t->GetLines().size();
		case 3:
		{
			const auto& ls = t->GetLines();
			if (index < 0 || index >= (int)ls.size()) return 0;
			return put({ (float)ls[index].First, (float)ls[index].Count, ls[index].Top, ls[index].Height });
		}
		case 4: return (int)t->GetLinks().size();
		case 5:
		{
			const auto& ls = t->GetLinks();
			if (index < 0 || index >= (int)ls.size()) return 0;
			return put({ (float)ls[index].First, (float)ls[index].Count });
		}
		case 6: return out ? t->FindLinkAt(Vec2(out[0], out[1])) : -1;
		case 7: if (out) t->SetCharacterOffset(index, Vec2(out[0], out[1])); return 1;
		case 8: if (out) t->SetCharacterScale(index, out[0]); return 1;
		case 9: if (out) t->SetCharacterColor(index, out); return 1;
		case 10: t->ClearCharacterModifiers(); return 1;
		case 11: t->ForceMeshUpdate(); return 1;
		}
		return 0;
	}

	const char* TextLink(uint64 id, int index, int which)
	{
		GameObject* g = Find(id);
		Text* t = g ? g->GetComponentIncludingPending<Text>() : nullptr;
		if (t == nullptr || index < 0 || index >= (int)t->GetLinks().size())
			return ScriptBindings::ReturnString("");
		const Text::LinkInfo& l = t->GetLinks()[index];
		return ScriptBindings::ReturnString(which == 0 ? l.Id : l.Text);
	}

	const char* GetString(uint64 id, int prop)
	{
		GameObject* g = Find(id);
		if (g == nullptr)
			return ScriptBindings::ReturnString("");
		switch (prop)
		{
		case 0: if (Text* t = g->GetComponentIncludingPending<Text>()) return ScriptBindings::ReturnString(t->GetText()); break;
		case 1: if (Text* t = g->GetComponentIncludingPending<Text>()) return ScriptBindings::ReturnString(t->GetFont()); break;
		case 2: if (UIImage* i = g->GetComponentIncludingPending<UIImage>()) return ScriptBindings::ReturnString(i->GetSprite()); break;
		case 3: case 4: if (InputField* f = g->GetComponentIncludingPending<InputField>()) return ScriptBindings::ReturnString(f->GetText()); break;
		}
		return ScriptBindings::ReturnString("");
	}

	void SetString(uint64 id, int prop, const char* value)
	{
		GameObject* g = Find(id);
		if (g == nullptr)
			return;
		const std::string s = value ? value : "";
		switch (prop)
		{
		case 0: if (Text* t = g->GetComponentIncludingPending<Text>()) t->SetText(s); break;
		case 1: if (Text* t = g->GetComponentIncludingPending<Text>()) t->SetFont(s); break;
		case 2: if (UIImage* i = g->GetComponentIncludingPending<UIImage>()) i->SetSprite(s); break;
		case 3: case 4: if (InputField* f = g->GetComponentIncludingPending<InputField>()) f->SetText(s, prop == 3); break;
		}
	}
}
