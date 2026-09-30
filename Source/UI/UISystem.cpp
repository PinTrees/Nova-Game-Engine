#include "pch.h"
#include "UISystem.h"
#include "UIRenderer.h"
#include "RectTransform.h"
#include "UICanvas.h"
#include "UIGraphic.h"
#include "UIImage.h"
#include "UIText.h"
#include "UIButton.h"
#include "GameViewEditorWindow.h"
#include "GameObjectFactory.h"

namespace
{
	constexpr uint8 kUILayer = 4;   // UnityGUI::LayerNames() 의 "UI"

	bool s_WasDown = false;
	Button* s_Pressed = nullptr;
	int s_DrawCalls = 0;

	bool ActiveInHierarchy(GameObject* go)
	{
		for (GameObject* g = go; g != nullptr; g = g->GetParent())
			if (!g->IsActive())
				return false;
		return go != nullptr;
	}

	bool InCurrentScene(GameObject* go)
	{
		Scene* scene = SceneManager::GetI()->GetCurrentScene();
		return scene && go && scene->FindByFileID(go->GetFileID()) == go;
	}

	// 부모 쪽에 켜진 Canvas 가 없는 Canvas (= 화면에 그리는 루트 캔버스), Sort Order 순
	std::vector<Canvas*> RootCanvases()
	{
		std::vector<Canvas*> out;
		for (Canvas* c : Canvas::All())
		{
			GameObject* go = c->GetGameObject();
			if (!c->IsEnabled() || !ActiveInHierarchy(go) || !InCurrentScene(go))
				continue;
			bool nested = false;
			for (GameObject* p = go->GetParent(); p && !nested; p = p->GetParent())
				if (Canvas* pc = p->GetComponent<Canvas>(); pc && pc->IsEnabled())
					nested = true;
			if (!nested)
				out.push_back(c);
		}
		std::stable_sort(out.begin(), out.end(), [](Canvas* a, Canvas* b) { return a->GetSortOrder() < b->GetSortOrder(); });
		return out;
	}

	void LayoutTree(GameObject* go, const Vec2& rectMin, const Vec2& rectSize)
	{
		for (GameObject* child : go->GetChildren())
		{
			// Unity 처럼 캔버스 아래의 오브젝트는 모두 RectTransform 을 가진다
			UISystem::EnsureRectTransform(child);
			RectTransform* rt = child->GetComponent<RectTransform>();
			if (rt == nullptr)
				continue;
			rt->Layout(rectMin, rectSize);
			LayoutTree(child, rt->GetRectMin(), rt->GetRectSize());
		}
	}

	// 그리는 순서 (Hierarchy 위 → 아래, 부모 → 자식)
	void CollectGraphics(GameObject* go, std::vector<UIGraphic*>& out)
	{
		if (!go->IsActive())
			return;
		for (auto& c : go->GetComponents())
			if (auto* g = dynamic_cast<UIGraphic*>(c.get()); g && g->IsEnabled())
				out.push_back(g);
		for (GameObject* child : go->GetChildren())
			CollectGraphics(child, out);
	}

	void GameScreenSize(float& w, float& h)
	{
		int gw = 0, gh = 0;
		GameViewEditorWindow::GameSize(gw, gh);
		w = gw > 0 ? (float)gw : 1920.0f;
		h = gh > 0 ? (float)gh : 1080.0f;
	}

	bool IsLiveButton(Button* b)
	{
		return b && std::find(Button::All().begin(), Button::All().end(), b) != Button::All().end();
	}

	Button* ButtonFor(GameObject* go)
	{
		// 맞은 그래픽에서 부모 쪽으로 처음 만나는 Button (Unity 의 이벤트 버블링)
		for (GameObject* g = go; g != nullptr; g = g->GetParent())
			if (Button* b = g->GetComponent<Button>(); b && b->IsEnabled())
				return b;
		return nullptr;
	}

	void ProcessInput(const std::vector<Canvas*>& roots)
	{
		float mx = 0.0f, my = 0.0f;
		const bool inside = GameViewEditorWindow::MouseToGame(mx, my) && GameViewEditorWindow::HasInputFocus();
		Button* hovered = nullptr;
		if (inside)
		{
			// 위에 그려진 캔버스(Sort Order 큰 것)와 나중에 그려진 그래픽부터
			for (auto it = roots.rbegin(); it != roots.rend(); ++it)
			{
				GameObject* cgo = (*it)->GetGameObject();
				GraphicRaycaster* ray = cgo->GetComponent<GraphicRaycaster>();
				if (ray == nullptr || !ray->IsEnabled())
					continue;
				std::vector<UIGraphic*> graphics;
				CollectGraphics(cgo, graphics);
				UIGraphic* hit = nullptr;
				for (auto g = graphics.rbegin(); g != graphics.rend(); ++g)
				{
					if (!(*g)->IsRaycastTarget())
						continue;
					RectTransform* rt = (*g)->GetRect();
					if (rt && rt->ContainsWorldPoint(Vec2(mx, my)))
					{
						hit = *g;
						break;
					}
				}
				if (hit)
				{
					hovered = ButtonFor(hit->GetGameObject());
					break;   // 맞은 그래픽이 아래 캔버스를 가린다
				}
			}
		}
		if (hovered && !hovered->IsInteractable())
			hovered = nullptr;

		const bool down = ImGui::GetIO().MouseDown[0];
		if (!IsLiveButton(s_Pressed))
			s_Pressed = nullptr;
		if (down && !s_WasDown)
			s_Pressed = hovered;   // 누른 버튼
		Button* clicked = nullptr;
		if (!down && s_WasDown)
		{
			if (s_Pressed && s_Pressed == hovered)
				clicked = s_Pressed;   // 같은 버튼 위에서 뗌 = 클릭
			s_Pressed = nullptr;
		}
		s_WasDown = down;
		for (Button* b : Button::All())
			b->SetPointer(b == hovered, b == s_Pressed && down);
		if (clicked)
			clicked->Click();
	}

	// 게임 화면 픽셀(왼쪽 아래 0,0) → NDC
	Matrix ScreenOrtho(float w, float h)
	{
		Matrix m = Matrix::Identity;
		m._11 = 2.0f / (std::max)(1.0f, w);
		m._22 = 2.0f / (std::max)(1.0f, h);
		m._33 = 0.0f;
		m._41 = -1.0f;
		m._42 = -1.0f;
		m._43 = 0.5f;
		return m;
	}

	GameObject* NewUIObject(const std::string& name)
	{
		GameObject* go = GameObjectFactory::CreateEmpty(name);
		go->SetLayerIndex(kUILayer);
		UISystem::EnsureRectTransform(go);
		return go;
	}

	GameObject* CanvasAncestor(GameObject* go)
	{
		for (GameObject* g = go; g != nullptr; g = g->GetParent())
			if (g->GetComponent<Canvas>())
				return g;
		return nullptr;
	}

	GameObject* CreateEventSystemIfMissing(Scene* scene)
	{
		for (GameObject* root : scene->GetRootGameObjects())
			if (root->GetComponent<EventSystem>())
				return nullptr;
		GameObject* es = GameObjectFactory::CreateEmpty("EventSystem");
		es->AddComponent(std::make_shared<EventSystem>());
		scene->AddRootGameObject(es);
		return es;
	}

	GameObject* CreateCanvas(Scene* scene)
	{
		GameObject* go = NewUIObject("Canvas");
		go->AddComponent(std::make_shared<Canvas>());
		go->AddComponent(std::make_shared<CanvasScaler>());
		go->AddComponent(std::make_shared<GraphicRaycaster>());
		scene->AddRootGameObject(go);
		CreateEventSystemIfMissing(scene);
		return go;
	}

	void SetRect(GameObject* go, Vec2 anchorMin, Vec2 anchorMax, Vec2 sizeDelta)
	{
		RectTransform* rt = go->GetComponent<RectTransform>();
		rt->SetAnchorMin(anchorMin);
		rt->SetAnchorMax(anchorMax);
		rt->SetAnchoredPosition(Vec2(0, 0));
		rt->SetSizeDelta(sizeDelta);
	}
}

namespace UISystem
{
	int LastDrawCalls() { return s_DrawCalls; }

	void EnsureRectTransform(GameObject* go)
	{
		if (go == nullptr || go->GetComponent<RectTransform>() != nullptr)
			return;
		auto rt = std::make_shared<RectTransform>();
		go->AddComponent(rt);
		// Unity 처럼 Transform 자리(맨 위)에 보이도록 Transform 바로 다음으로
		auto& comps = go->GetComponents();
		auto it = std::find(comps.begin(), comps.end(), rt);
		if (it != comps.end() && comps.size() > 1)
			std::rotate(comps.begin() + 1, it, it + 1);
	}

	void QueueRectTransformFor(GameObject* go, Component* added)
	{
		if (go == nullptr || added == nullptr || go->GetComponentIncludingPending<RectTransform>() != nullptr)
			return;
		if (dynamic_cast<UIGraphic*>(added) || dynamic_cast<Button*>(added) || dynamic_cast<Canvas*>(added))
			go->QueueComponent(std::make_shared<RectTransform>());
	}

	void Update()
	{
		Scene* scene = SceneManager::GetI()->GetCurrentScene();
		if (scene == nullptr)
			return;
		// UI 컴포넌트가 붙은 오브젝트는 RectTransform 을 갖는다 (Add Component 로 붙였을 때)
		for (UIGraphic* g : UIGraphic::All()) EnsureRectTransform(g->GetGameObject());
		for (Button* b : Button::All()) EnsureRectTransform(b->GetGameObject());
		for (Canvas* c : Canvas::All()) EnsureRectTransform(c->GetGameObject());

		float w, h;
		GameScreenSize(w, h);
		const std::vector<Canvas*> roots = RootCanvases();
		for (Canvas* c : roots)
		{
			GameObject* go = c->GetGameObject();
			CanvasScaler* scaler = go->GetComponent<CanvasScaler>();
			const float scale = scaler ? scaler->ComputeScale(w, h) : 1.0f;
			c->SetScaleFactor(scale);
			RectTransform* rt = go->GetComponent<RectTransform>();
			if (rt == nullptr)
				continue;
			// Screen Space - Overlay: 캔버스는 화면 가운데, 크기 = 화면 / 배율 (월드 1 단위 = 화면 1 픽셀)
			rt->SetDrivenRect(Vec2(w / scale, h / scale));
			Transform* tr = go->GetTransform();
			const Vec3 pos(w * 0.5f, h * 0.5f, 0.0f), s(scale, scale, scale);
			if ((tr->GetLocalPosition() - pos).LengthSquared() > 1e-6f)
				tr->SetLocalPosition(pos);
			if ((tr->GetLocalScale() - s).LengthSquared() > 1e-10f)
				tr->SetLocalScale(s);
			if (fabsf(tr->GetLocalRotation().w) < 0.999999f)
				tr->SetLocalRotation(Quaternion::Identity);
			LayoutTree(go, rt->GetRectMin(), rt->GetRectSize());
		}

		const bool playing = Application::IsPlaying();
		if (playing && !Application::IsPaused() && EventSystem::AnyActive())
			ProcessInput(roots);
		else
		{
			s_Pressed = nullptr;
			s_WasDown = false;
			for (Button* b : Button::All())
				b->SetPointer(false, false);
		}
		const float dt = ImGui::GetIO().DeltaTime;
		for (Button* b : Button::All())
			b->UpdateVisual(dt, playing);
	}

	void RenderGameView(ID3D11RenderTargetView* rtv, UINT width, UINT height, int display)
	{
		UIRenderer& r = UIRenderer::Get();
		r.Begin();
		for (Canvas* c : RootCanvases())
		{
			if (c->GetTargetDisplay() != display)
				continue;
			std::vector<UIGraphic*> graphics;
			CollectGraphics(c->GetGameObject(), graphics);
			for (UIGraphic* g : graphics)
				g->Populate(r, c->GetScaleFactor());
		}
		r.Flush(rtv, width, height, ScreenOrtho((float)width, (float)height));
		s_DrawCalls = r.LastDrawCalls();
	}

	void RenderSceneView(ID3D11RenderTargetView* rtv, UINT width, UINT height, const Matrix& view, const Matrix& proj, const Vec3& cameraPosition)
	{
		UIRenderer& r = UIRenderer::Get();
		r.Begin();
		const std::vector<Canvas*> roots = RootCanvases();
		if (roots.empty())
			return;
		// (캔버스 / 선택 요소의 테두리는 1 픽셀 선이라 Canvas / RectTransform::OnDrawGizmos 가 오버레이로 그린다)
		for (Canvas* c : roots)
		{
			std::vector<UIGraphic*> graphics;
			CollectGraphics(c->GetGameObject(), graphics);
			for (UIGraphic* g : graphics)
				g->Populate(r, c->GetScaleFactor());
		}
		// 캔버스는 1 픽셀 = 1 단위라 매우 크다: 캔버스 전체를 보면 Scene 카메라의 Far 보다 멀어지므로
		// Far 를 무한으로 바꾼 투영으로 그린다 (Unity Scene 뷰의 Dynamic Clipping 과 같은 효과, 깊이 비교는 하지 않음)
		Matrix p = proj;
		if (fabsf(p._44) < 1e-4f && fabsf(p._33) > 1e-6f)
		{
			const float nearZ = -p._43 / p._33;
			p._33 = 1.0f - 1e-6f;
			p._43 = -nearZ * p._33;
		}
		ID3D11DepthStencilView* dsv = nullptr;
		Application::GetI()->GetDeviceContext()->OMGetRenderTargets(0, nullptr, &dsv);
		r.Flush(rtv, width, height, view * p, nullptr);
		if (dsv)
		{
			// 원래 대상 복원 (이후 Scene 뷰 그리기를 위해)
			ID3D11RenderTargetView* rtvs[1] = { rtv };
			Application::GetI()->GetDeviceContext()->OMSetRenderTargets(1, rtvs, dsv);
			dsv->Release();
		}
		(void)cameraPosition;
	}

	GameObject* Create(const std::string& kind, Scene* scene, GameObject* selected)
	{
		if (scene == nullptr)
			return nullptr;
		if (kind == "EventSystem")
		{
			GameObject* es = CreateEventSystemIfMissing(scene);
			if (es)
				SelectionManager::SetSelectedGameObject(es);
			return es;
		}
		if (kind == "Canvas")
		{
			GameObject* canvas = CreateCanvas(scene);
			SelectionManager::SetSelectedGameObject(canvas);
			return canvas;
		}

		// 부모: 선택한 오브젝트가 캔버스 안이면 그 아래, 아니면 씬의 첫 캔버스 (없으면 새로 만든다)
		GameObject* parent = CanvasAncestor(selected) ? selected : nullptr;
		if (parent == nullptr)
			for (GameObject* root : scene->GetRootGameObjects())
				if (root->GetComponent<Canvas>()) { parent = root; break; }
		if (parent == nullptr)
			parent = CreateCanvas(scene);

		GameObject* go = nullptr;
		if (kind == "Image")
		{
			go = NewUIObject("Image");
			go->AddComponent(std::make_shared<UIImage>());
			SetRect(go, Vec2(0.5f, 0.5f), Vec2(0.5f, 0.5f), Vec2(100, 100));
		}
		else if (kind == "Text")
		{
			go = NewUIObject("Text");
			auto text = std::make_shared<Text>();
			text->SetFontSize(36);
			const float white[4] = { 1, 1, 1, 1 };
			text->SetColor(white);
			go->AddComponent(text);
			SetRect(go, Vec2(0.5f, 0.5f), Vec2(0.5f, 0.5f), Vec2(200, 50));
		}
		else if (kind == "Button")
		{
			go = NewUIObject("Button");
			auto img = std::make_shared<UIImage>();
			img->SetSprite("builtin:UISprite");
			img->SetImageType(UIImage::Type::Sliced);
			go->AddComponent(img);
			go->AddComponent(std::make_shared<Button>());
			SetRect(go, Vec2(0.5f, 0.5f), Vec2(0.5f, 0.5f), Vec2(160, 30));
			GameObject* label = NewUIObject("Text");
			auto text = std::make_shared<Text>();
			text->SetText("Button");
			text->SetFontSize(24);
			text->SetAlignment(4);   // Middle Center
			const float dark[4] = { 0.196f, 0.196f, 0.196f, 1.0f };
			text->SetColor(dark);
			label->AddComponent(text);
			SetRect(label, Vec2(0, 0), Vec2(1, 1), Vec2(0, 0));
			label->SetParent(go, false);
		}
		else if (kind == "Panel")
		{
			go = NewUIObject("Panel");
			auto img = std::make_shared<UIImage>();
			img->SetSprite("builtin:Background");
			img->SetImageType(UIImage::Type::Sliced);
			const float c[4] = { 1.0f, 1.0f, 1.0f, 0.392f };
			img->SetColor(c);
			go->AddComponent(img);
			SetRect(go, Vec2(0, 0), Vec2(1, 1), Vec2(0, 0));
		}
		if (go == nullptr)
			return nullptr;
		go->SetParent(parent, false);
		SelectionManager::SetSelectedGameObject(go);
		EditorLog::Write("UI", "created %s under '%s'", kind.c_str(), parent->GetName().c_str());
		return go;
	}
}
