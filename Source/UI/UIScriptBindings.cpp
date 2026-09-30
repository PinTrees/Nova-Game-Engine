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
