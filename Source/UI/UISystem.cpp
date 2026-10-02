#include "pch.h"
#include "TagsAndLayers.h"
#include "UISystem.h"
#include "UIRenderer.h"
#include "RectTransform.h"
#include "UICanvas.h"
#include "UIGraphic.h"
#include "UIImage.h"
#include "UIText.h"
#include "UIButton.h"
#include "UIToggle.h"
#include "UISlider.h"
#include "UIInputField.h"
#include "UIMask.h"
#include "UILayout.h"
#include "UIScrollbar.h"
#include "UIDropdown.h"
#include "GameViewEditorWindow.h"
#include "GameObjectFactory.h"
#include "ScriptEngine.h"
#include "Camera.h"
#include "DisplayManager.h"

namespace
{
	constexpr uint8 kUILayer = TagsAndLayers::UI;   // Unity 와 같은 5
	constexpr float kDragThreshold = 10.0f;   // EventSystem.Drag Threshold (화면 픽셀)

	int s_DrawCalls = 0;

	// ---------------------------------------------------------------- 입력 상태 (EventSystem)
	struct PointerState
	{
		bool WasDown = false;
		UISelectable* Pressed = nullptr;         // 누른 Selectable (같은 곳에서 떼면 Click)
		IUIDragHandler* DragTarget = nullptr;    // 끌기를 받을 것 (Slider, ScrollRect)
		Component* DragComponent = nullptr;      // DragTarget 의 컴포넌트 (살아 있는지 확인용)
		bool Dragging = false;
		Vec3 DownPos = Vec3(0, 0, 0), LastPos = Vec3(0, 0, 0);   // 캔버스 월드 점 (World 캔버스면 3D)
		Vec2 DownScreen = Vec2(0, 0);            // 끌기 문턱은 화면 픽셀로
		Canvas* PressCanvas = nullptr;           // 누른 캔버스 (끄는 동안 이 평면으로 마우스를 옮긴다)
		UISelectable* Selected = nullptr;        // 키보드 포커스 (InputField)
	} s_Pointer;

	struct DrawItem
	{
		UIGraphic* Graphic;
		bool Clip;
		Vec4 ClipRect;   // 캔버스 월드 minX, minY, maxX, maxY
		bool Visible;    // Mask 의 Show Mask Graphic 이 꺼져 있으면 그리지 않고 클릭만
	};

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

	template <typename T>
	bool IsLive(T* p, const std::vector<T*>& all) { return p && std::find(all.begin(), all.end(), p) != all.end(); }

	// 실제로 쓰는 모드: Screen Space - Camera 인데 카메라가 없으면 Overlay (Unity 와 같음)
	enum class Space { Overlay, CameraSpace, World };
	Space SpaceOf(Canvas* c)
	{
		if (c->GetRenderMode() == Canvas::RenderMode::WorldSpace) return Space::World;
		if (c->GetRenderMode() == Canvas::RenderMode::ScreenSpaceCamera && c->FindWorldCamera()) return Space::CameraSpace;
		return Space::Overlay;
	}

	// 입력 카메라: Camera 모드 = Render Camera, World 모드 = Event Camera (없으면 Game 뷰 카메라)
	Camera* EventCameraOf(Canvas* c)
	{
		if (Camera* cam = c->FindWorldCamera())
			return cam;
		static std::shared_ptr<Camera> s_Main;
		s_Main = DisplayManager::GetI()->GetCameraForDisplay(DisplayManager::GetI()->GetActiveDisplay());
		return s_Main.get();
	}

	void GameScreenSize(float& w, float& h);

	// 마우스(게임 화면 픽셀) 가 가리키는 이 캔버스의 점. Overlay = 화면 픽셀 그대로, World / Camera = 카메라 광선이 캔버스 평면과 만나는 3D 점
	bool PointerOnCanvas(Canvas* c, float mx, float my, Vec3& out, float* distance = nullptr)
	{
		if (distance) *distance = 0.0f;
		if (SpaceOf(c) == Space::Overlay)
		{
			out = Vec3(mx, my, 0.0f);
			return true;
		}
		Camera* cam = EventCameraOf(c);
		if (cam == nullptr || c->GetGameObject() == nullptr)
			return false;
		float w, h;
		GameScreenSize(w, h);
		const Matrix inv = (Matrix(cam->View()) * Matrix(cam->Proj())).Invert();
		const float nx = mx / w * 2.0f - 1.0f, ny = my / h * 2.0f - 1.0f;
		const Vec3 p0 = Vec3::Transform(Vec3(nx, ny, 0.0f), inv), p1 = Vec3::Transform(Vec3(nx, ny, 1.0f), inv);
		Vec3 dir = p1 - p0;
		if (dir.LengthSquared() < 1e-12f)
			return false;
		dir.Normalize();
		const Matrix world = c->GetGameObject()->GetTransform()->GetWorldMatrix();
		Vec3 n = Vec3::TransformNormal(Vec3(0, 0, 1), world);
		if (n.LengthSquared() < 1e-12f)
			return false;
		n.Normalize();
		const float denom = dir.Dot(n);
		if (fabsf(denom) < 1e-6f)
			return false;
		const float t = (world.Translation() - p0).Dot(n) / denom;
		if (t < 0.0f)
			return false;
		out = p0 + dir * t;
		if (distance) *distance = t;
		return true;
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

	// 사각형의 캔버스 월드 경계 상자 (Mask 잘라내기)
	Vec4 WorldBounds(RectTransform* rt, const Vec4& padding = Vec4(0, 0, 0, 0))
	{
		Vec3 k[4];
		rt->GetWorldCorners(k);
		Vec4 r(FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX);
		for (const Vec3& p : k)
		{
			r.x = (std::min)(r.x, p.x); r.y = (std::min)(r.y, p.y);
			r.z = (std::max)(r.z, p.x); r.w = (std::max)(r.w, p.y);
		}
		return Vec4(r.x + padding.x, r.y + padding.y, r.z - padding.z, r.w - padding.w);
	}

	Vec4 Intersect(const Vec4& a, const Vec4& b)
	{
		return Vec4((std::max)(a.x, b.x), (std::max)(a.y, b.y), (std::min)(a.z, b.z), (std::min)(a.w, b.w));
	}

	// 그리는 순서 (Hierarchy 위 → 아래, 부모 → 자식), Mask / RectMask2D 는 자식을 자기 사각형으로 자른다
	void CollectGraphics(GameObject* go, std::vector<DrawItem>& out, bool clip, const Vec4& clipRect, bool allowClip = true)
	{
		if (!go->IsActive())
			return;
		Mask* mask = go->GetComponent<Mask>();
		if (mask && !mask->IsEnabled()) mask = nullptr;
		for (auto& c : go->GetComponents())
			if (auto* g = dynamic_cast<UIGraphic*>(c.get()); g && g->IsEnabled())
				out.push_back({ g, clip, clipRect, !(mask && !mask->ShowMaskGraphic()) });
		bool childClip = clip;
		Vec4 childRect = clipRect;
		RectTransform* rt = go->GetComponent<RectTransform>();
		RectMask2D* rm = go->GetComponent<RectMask2D>();
		if (allowClip && rt && ((mask) || (rm && rm->IsEnabled())))
		{
			const Vec4 own = WorldBounds(rt, rm && rm->IsEnabled() ? rm->GetPadding() : Vec4(0, 0, 0, 0));
			childRect = clip ? Intersect(clipRect, own) : own;
			childClip = true;
		}
		for (GameObject* child : go->GetChildren())
			CollectGraphics(child, out, childClip, childRect, allowClip);
	}

	void GameScreenSize(float& w, float& h)
	{
		int gw = 0, gh = 0;
		GameViewEditorWindow::GameSize(gw, gh);
		w = gw > 0 ? (float)gw : 1920.0f;
		h = gh > 0 ? (float)gh : 1080.0f;
	}

	// 맞은 그래픽에서 부모 쪽으로: 처음 만나는 Selectable / 끌기 / 휠 대상 (Unity 이벤트 버블링)
	template <typename T>
	T* FindUp(GameObject* go)
	{
		for (GameObject* g = go; g != nullptr; g = g->GetParent())
			for (auto& c : g->GetComponents())
				if (T* t = dynamic_cast<T*>(c.get()); t && c->IsEnabled())
					return t;
		return nullptr;
	}

	bool DragTargetAlive()
	{
		if (s_Pointer.DragComponent == nullptr)
			return false;
		if (auto* s = dynamic_cast<UISelectable*>(s_Pointer.DragComponent))
			return IsLive(s, UISelectable::All());
		if (auto* sr = dynamic_cast<ScrollRect*>(s_Pointer.DragComponent))
			return IsLive(sr, ScrollRect::All());
		return false;
	}

	void Select(UISelectable* s)
	{
		if (!IsLive(s_Pointer.Selected, UISelectable::All()))
			s_Pointer.Selected = nullptr;
		if (s == s_Pointer.Selected)
			return;
		UISelectable* old = s_Pointer.Selected;
		s_Pointer.Selected = s;
		if (old)
			old->OnDeselect();
		if (s)
			s->OnSelect();
	}

	// 화면 점(게임 화면 픽셀, 왼쪽 아래 0,0) 에 맞는 맨 위 그래픽 (Raycast Target, GraphicRaycaster 가 있는 캔버스만)
	GameObject* HitTest(const std::vector<Canvas*>& roots, float mx, float my, Canvas*& hitCanvas, Vec3& hitPoint)
	{
		GameObject* hitObject = nullptr;
		hitCanvas = nullptr;
		hitPoint = Vec3(mx, my, 0.0f);
		{
			// 맞춰 볼 순서: Overlay (Sort Order 큰 것 = 위) → World / Camera 캔버스 (가까운 것부터)
			struct Candidate { Canvas* C; Vec3 P; float Dist; bool Overlay; };
			std::vector<Candidate> order;
			for (auto it = roots.rbegin(); it != roots.rend(); ++it)
			{
				Vec3 p;
				float d = 0.0f;
				if (PointerOnCanvas(*it, mx, my, p, &d))
					order.push_back({ *it, p, d, SpaceOf(*it) == Space::Overlay });
			}
			std::stable_sort(order.begin(), order.end(), [](const Candidate& a, const Candidate& b) {
				if (a.Overlay != b.Overlay) return a.Overlay;
				return !a.Overlay && a.Dist < b.Dist;
			});
			for (const Candidate& cand : order)
			{
				if (hitObject)
					break;
				GameObject* cgo = cand.C->GetGameObject();
				GraphicRaycaster* ray = cgo->GetComponent<GraphicRaycaster>();
				if (ray == nullptr || !ray->IsEnabled())
					continue;
				std::vector<DrawItem> items;
				CollectGraphics(cgo, items, false, Vec4());
				for (auto g = items.rbegin(); g != items.rend(); ++g)
				{
					if (!g->Graphic->IsRaycastTarget())
						continue;
					// Mask 밖은 맞지 않음 (Overlay 의 잘라내기 사각형은 화면 픽셀)
					if (cand.Overlay && g->Clip && (mx < g->ClipRect.x || mx > g->ClipRect.z || my < g->ClipRect.y || my > g->ClipRect.w))
						continue;
					RectTransform* rt = g->Graphic->GetRect();
					if (rt && rt->ContainsWorldPoint(cand.P))
					{
						hitObject = g->Graphic->GetGameObject();
						hitCanvas = cand.C;
						hitPoint = cand.P;
						break;
					}
				}
			}
		}
		return hitObject;
	}

	void ProcessInput(const std::vector<Canvas*>& roots)
	{
		float mx = 0.0f, my = 0.0f;
		const bool inside = GameViewEditorWindow::MouseToGame(mx, my) && GameViewEditorWindow::HasInputFocus();
		const Vec2 screen(mx, my);
		Canvas* hitCanvas = nullptr;
		Vec3 hitPoint(mx, my, 0.0f);
		GameObject* hitObject = inside ? HitTest(roots, mx, my, hitCanvas, hitPoint) : nullptr;
		UISelectable* hovered = hitObject ? FindUp<UISelectable>(hitObject) : nullptr;
		if (hovered && !hovered->IsInteractable())
			hovered = nullptr;

		const ImGuiIO& io = ImGui::GetIO();
		const bool down = io.MouseDown[0] && (inside || s_Pointer.WasDown);
		if (!IsLive(s_Pointer.Pressed, UISelectable::All()))
			s_Pointer.Pressed = nullptr;
		if (!IsLive(s_Pointer.PressCanvas, Canvas::All()))
			s_Pointer.PressCanvas = nullptr;
		if (!DragTargetAlive())
		{
			s_Pointer.DragTarget = nullptr;
			s_Pointer.DragComponent = nullptr;
			s_Pointer.Dragging = false;
		}
		// 누르고 있는 동안: 누른 캔버스 평면 위의 점 (그래픽 밖으로 나가도 Slider · ScrollRect 가 따라온다)
		Vec3 point = hitPoint;
		if (s_Pointer.WasDown && s_Pointer.PressCanvas)
		{
			Vec3 p;
			if (PointerOnCanvas(s_Pointer.PressCanvas, mx, my, p))
				point = p;
			else
				point = s_Pointer.LastPos;
		}

		UISelectable* clicked = nullptr;
		if (down && !s_Pointer.WasDown)
		{
			// 누름: Selectable, 끌기 대상, 선택(포커스) 결정
			s_Pointer.Pressed = hovered;
			s_Pointer.PressCanvas = hitCanvas;
			s_Pointer.DownPos = s_Pointer.LastPos = hitPoint;
			s_Pointer.DownScreen = screen;
			s_Pointer.Dragging = false;
			s_Pointer.DragTarget = hitObject ? FindUp<IUIDragHandler>(hitObject) : nullptr;
			s_Pointer.DragComponent = s_Pointer.DragTarget ? dynamic_cast<Component*>(s_Pointer.DragTarget) : nullptr;
			if (auto* s = dynamic_cast<UISelectable*>(s_Pointer.DragComponent); s && !s->IsInteractable())
				s_Pointer.DragTarget = nullptr, s_Pointer.DragComponent = nullptr;
			Select(hovered);   // 빈 곳을 누르면 선택 해제 (InputField 입력 끝)
			if (hovered)
				hovered->OnPointerDown(hitPoint);
			if (s_Pointer.DragTarget && s_Pointer.DragTarget->DragsImmediately())
			{
				s_Pointer.Dragging = true;
				s_Pointer.DragTarget->OnBeginDrag(hitPoint);
			}
		}
		else if (down && s_Pointer.WasDown)
		{
			// 끄는 중: 조금 움직이면 끌기 시작 (ScrollRect 안의 버튼은 클릭 취소)
			if (s_Pointer.DragTarget && !s_Pointer.Dragging && (screen - s_Pointer.DownScreen).Length() > kDragThreshold)
			{
				s_Pointer.Dragging = true;
				s_Pointer.DragTarget->OnBeginDrag(s_Pointer.DownPos);
				if (s_Pointer.Pressed && dynamic_cast<Component*>(s_Pointer.Pressed) != s_Pointer.DragComponent)
					s_Pointer.Pressed = nullptr;
			}
			if (s_Pointer.Dragging && s_Pointer.DragTarget && (point - s_Pointer.LastPos).LengthSquared() > 0.0f)
				s_Pointer.DragTarget->OnDrag(point, point - s_Pointer.LastPos);
			s_Pointer.LastPos = point;
		}
		else if (!down && s_Pointer.WasDown)
		{
			// <link> 클릭: 누른 곳과 뗀 곳이 같은 링크면 Text 의 On Link Clicked (Selectable 이 아니어도)
			if (hitObject && !s_Pointer.Dragging)
				if (Text* t = hitObject->GetComponent<Text>())
				{
					const int up = t->FindLinkAt(hitPoint);
					if (up >= 0 && up == t->FindLinkAt(s_Pointer.DownPos))
					{
						const std::string id = t->GetLinks()[up].Id;
						t->OnLinkClicked.Invoke(hitObject, "OnLinkClicked", id);
						ScriptEngine::InvokeUIEvent(hitObject->GetFileID(), 5, (float)up, id);   // C# text.onLinkClicked
					}
				}
			// 뗌: 같은 Selectable 위면 클릭
			if (s_Pointer.Dragging && s_Pointer.DragTarget)
				s_Pointer.DragTarget->OnEndDrag(point);
			if (s_Pointer.Pressed && s_Pointer.Pressed == hovered)
				clicked = s_Pointer.Pressed;
			s_Pointer.Pressed = nullptr;
			s_Pointer.PressCanvas = nullptr;
			s_Pointer.DragTarget = nullptr;
			s_Pointer.DragComponent = nullptr;
			s_Pointer.Dragging = false;
		}
		s_Pointer.WasDown = down;

		// 휠 → ScrollRect
		const float wheel = GameViewEditorWindow::ScrollDelta();
		if (wheel != 0.0f && hitObject)
			if (IUIScrollHandler* sh = FindUp<IUIScrollHandler>(hitObject))
				sh->OnScroll(wheel);

		for (UISelectable* s : UISelectable::All())
			s->SetPointer(s == hovered, s == s_Pointer.Pressed && down);
		if (clicked)
			clicked->OnClick();
		if (IsLive(s_Pointer.Selected, UISelectable::All()))
			s_Pointer.Selected->OnUpdateSelected();
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

	void DrawItems(UIRenderer& r, const std::vector<DrawItem>& items, float scale)
	{
		for (const DrawItem& item : items)
		{
			if (!item.Visible)
				continue;
			r.SetClip(item.Clip, item.ClipRect);
			item.Graphic->Populate(r, scale);
		}
		r.SetClip(false);
	}

	// ---------------------------------------------------------------- 만들기 (GameObject > UI)
	GameObject* NewUIObject(const std::string& name, GameObject* parent = nullptr)
	{
		GameObject* go = GameObjectFactory::CreateEmpty(name);
		go->SetLayerIndex(kUILayer);
		UISystem::EnsureRectTransform(go);
		if (parent)
			go->SetParent(parent, false);
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

	RectTransform* RT(GameObject* go) { return go->GetComponent<RectTransform>(); }

	void SetRect(GameObject* go, Vec2 anchorMin, Vec2 anchorMax, Vec2 sizeDelta, Vec2 pos = Vec2(0, 0), Vec2 pivot = Vec2(0.5f, 0.5f))
	{
		RectTransform* rt = RT(go);
		rt->SetAnchorMin(anchorMin);
		rt->SetAnchorMax(anchorMax);
		rt->SetPivot(pivot);
		rt->SetAnchoredPosition(pos);
		rt->SetSizeDelta(sizeDelta);
	}

	// 늘어나는 사각형: 여백(왼, 아래, 오른, 위)
	void SetStretch(GameObject* go, Vec2 anchorMin, Vec2 anchorMax, float left, float bottom, float right, float top)
	{
		RectTransform* rt = RT(go);
		rt->SetAnchorMin(anchorMin);
		rt->SetAnchorMax(anchorMax);
		rt->SetPivot(Vec2(0.5f, 0.5f));
		rt->SetOffsets(Vec2(left, bottom), Vec2(-right, -top));
	}

	std::shared_ptr<UIImage> AddImage(GameObject* go, const char* sprite, bool sliced, const float color[4] = nullptr)
	{
		auto img = std::make_shared<UIImage>();
		if (sprite)
			img->SetSprite(sprite);
		if (sliced)
			img->SetImageType(UIImage::Type::Sliced);
		if (color)
			img->SetColor(color);
		go->AddComponent(img);
		return img;
	}

	std::shared_ptr<Text> AddText(GameObject* go, const char* s, int size, const float color[4], int alignment)
	{
		auto text = std::make_shared<Text>();
		text->SetText(s);
		text->SetFontSize(size);
		text->SetColor(color);
		text->SetAlignment(alignment);
		go->AddComponent(text);
		return text;
	}
}

namespace UISystem
{
	int LastDrawCalls() { return s_DrawCalls; }

	void ClearSelection() { Select(nullptr); }

	void OnSceneUnloading()
	{
		// 씬을 지우기 전에: 선택(InputField 는 여기서 On End Edit)과 누르고 있던/끌던 대상을 놓는다
		Select(nullptr);
		s_Pointer.Pressed = nullptr;
		s_Pointer.DragTarget = nullptr;
		s_Pointer.DragComponent = nullptr;
		s_Pointer.Dragging = false;
	}

	void EnsureRectTransform(GameObject* go)
	{
		// 스크립트가 AddComponent 로 대기열에 넣은 RectTransform 이 있으면 그것을 쓴다 (빌드된 게임은 첫 프레임 전에 Start 가 돌아 여기서 먼저 만나고,
		// 새로 만들면 스크립트가 값을 넣은 쪽이 가려진다)
		if (go == nullptr || go->GetComponentIncludingPending<RectTransform>() != nullptr)
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
		if (dynamic_cast<UIGraphic*>(added) || dynamic_cast<UISelectable*>(added) || dynamic_cast<Canvas*>(added) ||
			dynamic_cast<Mask*>(added) || dynamic_cast<RectMask2D*>(added) || dynamic_cast<ScrollRect*>(added))
			go->QueueComponent(std::make_shared<RectTransform>());
	}

	void Update()
	{
		Scene* scene = SceneManager::GetI()->GetCurrentScene();
		if (scene == nullptr)
			return;
		// UI 컴포넌트가 붙은 오브젝트는 RectTransform 을 갖는다 (Add Component 로 붙였을 때)
		for (UIGraphic* g : UIGraphic::All()) EnsureRectTransform(g->GetGameObject());
		for (UISelectable* s : UISelectable::All()) EnsureRectTransform(s->GetGameObject());
		for (Canvas* c : Canvas::All()) EnsureRectTransform(c->GetGameObject());
		for (ScrollRect* s : ScrollRect::All()) EnsureRectTransform(s->GetGameObject());

		const bool playing = Application::IsPlaying();
		const bool running = playing && !Application::IsPaused();
		const float dt = ImGui::GetIO().DeltaTime;

		// 레이아웃 전: Slider 의 Fill/Handle, Toggle 체크 표시, InputField 글자, ScrollRect 관성/복귀
		for (UISelectable* s : UISelectable::All())
			s->UpdateBeforeLayout(dt, playing);
		for (ScrollRect* s : ScrollRect::All())
			s->UpdateBeforeLayout(running ? dt : 0.0f, running);

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
			Transform* tr = go->GetTransform();
			const Space space = SpaceOf(c);
			if (space == Space::World)
			{
				// World Space: Transform 위치 · 회전 · 크기 그대로, 사각형 = Width / Height (부모 사각형 없음)
				c->SetScaleFactor(1.0f);
				rt->AdoptTransformPosition();
				rt->Layout(Vec2(0, 0), Vec2(0, 0));
			}
			else if (space == Space::CameraSpace)
			{
				// Screen Space - Camera: Render Camera 앞 Plane Distance, 화면 크기 (그 거리에서 1 캔버스 단위 = 화면 1 픽셀 / 배율)
				Camera* cam = c->FindWorldCamera();
				rt->SetDrivenRect(Vec2(w / scale, h / scale));
				Transform* ct = cam->GetGameObject()->GetTransform();
				const float d = c->GetPlaneDistance();
				const float worldH = cam->IsOrthographic() ? 2.0f * cam->GetOrthoSize() : 2.0f * d * tanf(cam->GetFovY() * 0.5f);
				const float k = worldH / (std::max)(1.0f, h / scale);
				Vec3 fwd = Vec3::TransformNormal(Vec3(0, 0, 1), ct->GetWorldMatrix());
				fwd.Normalize();
				const Vec3 pos = ct->GetPosition() + fwd * d;
				if ((tr->GetPosition() - pos).LengthSquared() > 1e-8f)
					tr->SetPosition(pos);
				const Quaternion rot = ct->GetRotation();
				if (fabsf(tr->GetRotation().Dot(rot)) < 0.999999f)
					tr->SetRotation(rot);
				const Vec3 s(k, k, k);
				if ((tr->GetLocalScale() - s).LengthSquared() > 1e-14f)
					tr->SetLocalScale(s);
			}
			else
			{
				// Screen Space - Overlay: 캔버스는 화면 가운데, 크기 = 화면 / 배율 (월드 1 단위 = 화면 1 픽셀)
				rt->SetDrivenRect(Vec2(w / scale, h / scale));
				const Vec3 pos(w * 0.5f, h * 0.5f, 0.0f), s(scale, scale, scale);
				if ((tr->GetLocalPosition() - pos).LengthSquared() > 1e-6f)
					tr->SetLocalPosition(pos);
				if ((tr->GetLocalScale() - s).LengthSquared() > 1e-10f)
					tr->SetLocalScale(s);
				if (fabsf(tr->GetLocalRotation().w) < 0.999999f)
					tr->SetLocalRotation(Quaternion::Identity);
			}
			LayoutTree(go, rt->GetRectMin(), rt->GetRectSize());
			UILayout::Apply(go);   // Layout Group · Content Size Fitter · Aspect Ratio Fitter (RectTransform 레이아웃 뒤에)
		}

		if (running && EventSystem::AnyActive())
			ProcessInput(roots);
		else
		{
			if (!playing)
				Select(nullptr);
			s_Pointer.Pressed = nullptr;
			s_Pointer.DragTarget = nullptr;
			s_Pointer.DragComponent = nullptr;
			s_Pointer.Dragging = false;
			s_Pointer.WasDown = false;
			for (UISelectable* s : UISelectable::All())
				s->SetPointer(false, false);
		}
		for (UISelectable* s : UISelectable::All())
			s->UpdateVisual(dt, playing);
	}

	GameObject* RaycastScreen(float x, float y)
	{
		Canvas* c = nullptr;
		Vec3 p;
		return HitTest(RootCanvases(), x, y, c, p);
	}

	void RenderGameView(GfxRenderTargetView* rtv, UINT width, UINT height, int display, Camera* camera, GfxDepthStencilView* depth)
	{
		UIRenderer& r = UIRenderer::Get();
		const std::vector<Canvas*> roots = RootCanvases();
		int drawCalls = 0;
		// 1) World / Camera 캔버스: 게임 카메라로, 씬 깊이로 가려지게 (Order in Layer 순, 같으면 먼 것부터)
		if (camera)
		{
			const Vec3 eye = camera->GetGameObject() ? camera->GetGameObject()->GetTransform()->GetPosition() : Vec3(0, 0, 0);
			std::vector<std::pair<Canvas*, float>> worlds;
			for (Canvas* c : roots)
			{
				const Space space = SpaceOf(c);
				if (space == Space::Overlay || (space == Space::CameraSpace && c->FindWorldCamera() != camera))
					continue;
				if (!((camera->GetCullingMask() >> (c->GetGameObject()->GetLayerIndex() & 31)) & 1u))
					continue;   // Camera.cullingMask (World / Camera 캔버스 — Overlay 는 Unity 처럼 늘 그린다)
				worlds.push_back({ c, (c->GetGameObject()->GetTransform()->GetPosition() - eye).LengthSquared() });
			}
			std::stable_sort(worlds.begin(), worlds.end(), [](const auto& a, const auto& b) {
				if (a.first->GetSortOrder() != b.first->GetSortOrder()) return a.first->GetSortOrder() < b.first->GetSortOrder();
				return a.second > b.second;
			});
			if (!worlds.empty())
			{
				r.Begin();
				for (const auto& [c, dist] : worlds)
				{
					std::vector<DrawItem> items;
					CollectGraphics(c->GetGameObject(), items, false, Vec4(), false);
					DrawItems(r, items, 1.0f);
				}
				r.Flush(rtv, width, height, Matrix(camera->View()) * Matrix(camera->Proj()), depth);
				drawCalls += r.LastDrawCalls();
			}
		}
		// 2) Screen Space - Overlay (맨 위)
		r.Begin();
		for (Canvas* c : roots)
		{
			if (SpaceOf(c) != Space::Overlay || c->GetTargetDisplay() != display)
				continue;
			std::vector<DrawItem> items;
			CollectGraphics(c->GetGameObject(), items, false, Vec4());
			DrawItems(r, items, c->GetScaleFactor());
		}
		r.Flush(rtv, width, height, ScreenOrtho((float)width, (float)height));
		s_DrawCalls = drawCalls + r.LastDrawCalls();
	}

	void RenderSceneView(GfxRenderTargetView* rtv, UINT width, UINT height, const Matrix& view, const Matrix& proj, const Vec3& cameraPosition)
	{
		UIRenderer& r = UIRenderer::Get();
		r.Begin();
		const std::vector<Canvas*> roots = RootCanvases();
		if (roots.empty())
			return;
		// (캔버스 / 선택 요소의 테두리는 1 픽셀 선이라 Canvas / RectTransform::OnDrawGizmos 가 오버레이로 그린다)
		// World / Camera 캔버스는 씬 깊이로 가려지게 따로 (아래에서)
		for (Canvas* c : roots)
		{
			if (SpaceOf(c) != Space::Overlay)
				continue;
			std::vector<DrawItem> items;
			CollectGraphics(c->GetGameObject(), items, false, Vec4());
			DrawItems(r, items, c->GetScaleFactor());
		}
		// 캔버스는 1 픽셀 = 1 단위라 매우 크다: 캔버스 전체를 보면 Scene 카메라의 Far 보다 멀어질 수 있으므로
		// Far 를 무한으로 바꾼 투영으로 그린다 (Unity Scene 뷰의 Dynamic Clipping 과 같은 효과, 깊이 비교는 하지 않음)
		Matrix p = proj;
		if (fabsf(p._44) < 1e-4f && fabsf(p._33) > 1e-6f)
		{
			const float nearZ = -p._43 / p._33;
			p._33 = 1.0f - 1e-6f;
			p._43 = -nearZ * p._33;
		}
		GfxDepthStencilView* dsv = nullptr;
		Application::GetI()->GetDeviceContext()->OMGetRenderTargets(0, nullptr, &dsv);
		r.Flush(rtv, width, height, view * p, nullptr);
		// World / Camera 캔버스: Scene 카메라 + 씬 깊이
		r.Begin();
		bool anyWorld = false;
		for (Canvas* c : roots)
		{
			if (SpaceOf(c) == Space::Overlay)
				continue;
			std::vector<DrawItem> items;
			CollectGraphics(c->GetGameObject(), items, false, Vec4(), false);
			DrawItems(r, items, 1.0f);
			anyWorld = true;
		}
		if (anyWorld)
			r.Flush(rtv, width, height, view * proj, dsv);
		if (dsv)
		{
			// 원래 대상 복원 (이후 Scene 뷰 그리기를 위해)
			GfxRenderTargetView* rtvs[1] = { rtv };
			Application::GetI()->GetDeviceContext()->OMSetRenderTargets(1, rtvs, dsv);
			dsv->Release();
		}
		(void)cameraPosition;
	}

	// Unity: Scrollbar > Sliding Area > Handle
	GameObject* MakeScrollbar(const char* name, GameObject* parent, bool vertical)
	{
		GameObject* go = NewUIObject(name, parent);
		AddImage(go, "builtin:Background", true);
		auto bar = std::make_shared<Scrollbar>();
		go->AddComponent(bar);
		GameObject* area = NewUIObject("Sliding Area", go);
		SetStretch(area, Vec2(0, 0), Vec2(1, 1), 10, 10, 10, 10);
		GameObject* handle = NewUIObject("Handle", area);
		AddImage(handle, "builtin:UISprite", true);
		SetStretch(handle, Vec2(0, 0), Vec2(0.2f, 1), -10, -10, -10, -10);
		bar->fromJson(json{ { "targetGraphic", handle->GetFileID() }, { "handleRect", handle->GetFileID() }, { "direction", vertical ? 2 : 0 }, { "size", 0.2f } });
		return go;
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

		const float white[4] = { 1, 1, 1, 1 };
		const float dark[4] = { 0.196f, 0.196f, 0.196f, 1.0f };
		GameObject* go = nullptr;
		if (kind == "Image")
		{
			go = NewUIObject("Image");
			AddImage(go, nullptr, false);
			SetRect(go, Vec2(0.5f, 0.5f), Vec2(0.5f, 0.5f), Vec2(100, 100));
		}
		else if (kind == "Text")
		{
			go = NewUIObject("Text");
			AddText(go, "New Text", 36, white, 0);
			SetRect(go, Vec2(0.5f, 0.5f), Vec2(0.5f, 0.5f), Vec2(200, 50));
		}
		else if (kind == "Button")
		{
			go = NewUIObject("Button");
			AddImage(go, "builtin:UISprite", true);
			go->AddComponent(std::make_shared<Button>());
			SetRect(go, Vec2(0.5f, 0.5f), Vec2(0.5f, 0.5f), Vec2(160, 30));
			GameObject* label = NewUIObject("Text", go);
			AddText(label, "Button", 24, dark, 4);
			SetStretch(label, Vec2(0, 0), Vec2(1, 1), 0, 0, 0, 0);
		}
		else if (kind == "Panel")
		{
			go = NewUIObject("Panel");
			const float c[4] = { 1.0f, 1.0f, 1.0f, 0.392f };
			AddImage(go, "builtin:Background", true, c);
			SetStretch(go, Vec2(0, 0), Vec2(1, 1), 0, 0, 0, 0);
		}
		else if (kind == "Toggle")
		{
			// Unity: Toggle > Background > Checkmark, Label
			go = NewUIObject("Toggle");
			auto toggle = std::make_shared<Toggle>();
			go->AddComponent(toggle);
			SetRect(go, Vec2(0.5f, 0.5f), Vec2(0.5f, 0.5f), Vec2(160, 20));
			GameObject* bg = NewUIObject("Background", go);
			AddImage(bg, "builtin:UISprite", true);
			SetRect(bg, Vec2(0, 1), Vec2(0, 1), Vec2(20, 20), Vec2(10, -10));
			GameObject* check = NewUIObject("Checkmark", bg);
			AddImage(check, "builtin:Checkmark", false, dark);
			SetRect(check, Vec2(0.5f, 0.5f), Vec2(0.5f, 0.5f), Vec2(20, 20));
			GameObject* label = NewUIObject("Label", go);
			AddText(label, "Toggle", 14, dark, 3);
			SetStretch(label, Vec2(0, 0), Vec2(1, 1), 23, 1, 5, 3);
			// Target Graphic = Background, Graphic = Checkmark (Unity 와 같음)
			toggle->fromJson(json{ { "targetGraphic", bg->GetFileID() }, { "graphic", check->GetFileID() }, { "isOn", true } });
		}
		else if (kind == "Slider")
		{
			// Unity: Slider > Background, Fill Area > Fill, Handle Slide Area > Handle
			go = NewUIObject("Slider");
			auto slider = std::make_shared<Slider>();
			go->AddComponent(slider);
			SetRect(go, Vec2(0.5f, 0.5f), Vec2(0.5f, 0.5f), Vec2(160, 20));
			GameObject* bg = NewUIObject("Background", go);
			const float bgColor[4] = { 0.8f, 0.8f, 0.8f, 1.0f };
			AddImage(bg, "builtin:Background", true, bgColor);
			SetStretch(bg, Vec2(0, 0.25f), Vec2(1, 0.75f), 0, 0, 0, 0);
			GameObject* fillArea = NewUIObject("Fill Area", go);
			SetStretch(fillArea, Vec2(0, 0.25f), Vec2(1, 0.75f), 5, 0, 15, 0);
			GameObject* fill = NewUIObject("Fill", fillArea);
			AddImage(fill, "builtin:UISprite", true);
			SetRect(fill, Vec2(0, 0), Vec2(0, 1), Vec2(10, 0));
			GameObject* handleArea = NewUIObject("Handle Slide Area", go);
			SetStretch(handleArea, Vec2(0, 0), Vec2(1, 1), 10, 0, 10, 0);
			GameObject* handle = NewUIObject("Handle", handleArea);
			AddImage(handle, "builtin:Knob", false);
			SetRect(handle, Vec2(0, 0), Vec2(0, 1), Vec2(20, 0));
			slider->fromJson(json{ { "targetGraphic", handle->GetFileID() }, { "fillRect", fill->GetFileID() }, { "handleRect", handle->GetFileID() }, { "value", 0.0f } });
		}
		else if (kind == "InputField")
		{
			// Unity: InputField > Text Area(RectMask2D) > Placeholder, Text
			go = NewUIObject("InputField");
			AddImage(go, "builtin:InputFieldBackground", true);
			auto input = std::make_shared<InputField>();
			go->AddComponent(input);
			SetRect(go, Vec2(0.5f, 0.5f), Vec2(0.5f, 0.5f), Vec2(160, 30));
			GameObject* area = NewUIObject("Text Area", go);
			area->AddComponent(std::make_shared<RectMask2D>());
			SetStretch(area, Vec2(0, 0), Vec2(1, 1), 10, 6, 10, 7);
			GameObject* ph = NewUIObject("Placeholder", area);
			const float phColor[4] = { 0.196f, 0.196f, 0.196f, 0.5f };
			auto phText = AddText(ph, "Enter text...", 14, phColor, 3);
			phText->SetStyle(Text::Style::Italic);
			phText->SetOverflow(Text::HOverflow::Overflow, Text::VOverflow::Overflow);
			SetStretch(ph, Vec2(0, 0), Vec2(1, 1), 0, 0, 0, 0);
			GameObject* textGo = NewUIObject("Text", area);
			auto text = AddText(textGo, "", 14, dark, 3);
			text->SetOverflow(Text::HOverflow::Overflow, Text::VOverflow::Overflow);
			text->SetRaycastTarget(false);
			phText->SetRaycastTarget(false);
			SetStretch(textGo, Vec2(0, 0), Vec2(1, 1), 0, 0, 0, 0);
			input->SetTextComponents(textGo->GetFileID(), ph->GetFileID());
		}
		else if (kind == "ScrollView")
		{
			// Unity: Scroll View(ScrollRect, Image) > Viewport(RectMask2D) > Content
			go = NewUIObject("Scroll View");
			const float c[4] = { 1.0f, 1.0f, 1.0f, 0.392f };
			AddImage(go, "builtin:Background", true, c);
			auto scroll = std::make_shared<ScrollRect>();
			go->AddComponent(scroll);
			SetRect(go, Vec2(0.5f, 0.5f), Vec2(0.5f, 0.5f), Vec2(200, 200));
			GameObject* viewport = NewUIObject("Viewport", go);
			viewport->AddComponent(std::make_shared<RectMask2D>());
			SetStretch(viewport, Vec2(0, 0), Vec2(1, 1), 0, 0, 0, 0);
			GameObject* content = NewUIObject("Content", viewport);
			SetRect(content, Vec2(0, 1), Vec2(1, 1), Vec2(0, 300), Vec2(0, 0), Vec2(0, 1));
			scroll->SetContent(content->GetFileID(), viewport->GetFileID());
			// Scrollbar Horizontal (아래) · Vertical (오른쪽)
			GameObject* hbar = MakeScrollbar("Scrollbar Horizontal", go, false);
			SetRect(hbar, Vec2(0, 0), Vec2(1, 0), Vec2(-17, 20), Vec2(-8.5f, 0), Vec2(0.5f, 0));
			GameObject* vbar = MakeScrollbar("Scrollbar Vertical", go, true);
			SetRect(vbar, Vec2(1, 0), Vec2(1, 1), Vec2(20, -17), Vec2(0, 8.5f), Vec2(1, 0.5f));
			SetStretch(viewport, Vec2(0, 0), Vec2(1, 1), 0, 17, 17, 0);
			scroll->SetScrollbars(hbar->GetFileID(), vbar->GetFileID());
		}
		else if (kind == "Scrollbar")
		{
			go = MakeScrollbar("Scrollbar", nullptr, false);
			SetRect(go, Vec2(0.5f, 0.5f), Vec2(0.5f, 0.5f), Vec2(160, 20));
		}
		else if (kind == "Dropdown")
		{
			// Unity: Dropdown > Label, Arrow, Template(꺼 둠: Scroll Rect) > Viewport(RectMask2D) > Content > Item(Toggle) > Item Background, Item Checkmark, Item Label
			go = NewUIObject("Dropdown");
			AddImage(go, "builtin:UISprite", true);
			auto dd = std::make_shared<Dropdown>();
			go->AddComponent(dd);
			SetRect(go, Vec2(0.5f, 0.5f), Vec2(0.5f, 0.5f), Vec2(160, 30));
			GameObject* label = NewUIObject("Label", go);
			AddText(label, "Option A", 14, dark, 3);
			SetStretch(label, Vec2(0, 0), Vec2(1, 1), 10, 6, 25, 7);
			GameObject* arrow = NewUIObject("Arrow", go);
			AddText(arrow, "\xE2\x96\xBC", 12, dark, 4);   // \u25BC (UTF-8)
			SetRect(arrow, Vec2(1, 0.5f), Vec2(1, 0.5f), Vec2(20, 20), Vec2(-15, 0));
			GameObject* templ = NewUIObject("Template", go);
			AddImage(templ, "builtin:UISprite", true);
			auto tscroll = std::make_shared<ScrollRect>();
			templ->AddComponent(tscroll);
			SetRect(templ, Vec2(0, 0), Vec2(1, 0), Vec2(0, 150), Vec2(0, 2), Vec2(0.5f, 1));
			GameObject* tview = NewUIObject("Viewport", templ);
			tview->AddComponent(std::make_shared<RectMask2D>());
			SetStretch(tview, Vec2(0, 0), Vec2(1, 1), 0, 0, 0, 0);
			GameObject* tcontent = NewUIObject("Content", tview);
			SetRect(tcontent, Vec2(0, 1), Vec2(1, 1), Vec2(0, 28), Vec2(0, 0), Vec2(0.5f, 1));
			GameObject* item = NewUIObject("Item", tcontent);
			auto itemToggle = std::make_shared<Toggle>();
			item->AddComponent(itemToggle);
			SetRect(item, Vec2(0, 1), Vec2(1, 1), Vec2(0, 20), Vec2(0, -14));
			GameObject* itemBg = NewUIObject("Item Background", item);
			const float itemBgColor[4] = { 0.961f, 0.961f, 0.961f, 1.0f };
			AddImage(itemBg, nullptr, false, itemBgColor);
			SetStretch(itemBg, Vec2(0, 0), Vec2(1, 1), 0, 0, 0, 0);
			GameObject* itemCheck = NewUIObject("Item Checkmark", item);
			AddImage(itemCheck, "builtin:Checkmark", false, dark);
			SetRect(itemCheck, Vec2(0, 0.5f), Vec2(0, 0.5f), Vec2(20, 20), Vec2(10, 0));
			GameObject* itemLabel = NewUIObject("Item Label", item);
			auto itemText = AddText(itemLabel, "Option A", 14, dark, 3);
			itemText->SetRaycastTarget(false);
			SetStretch(itemLabel, Vec2(0, 0), Vec2(1, 1), 20, 1, 10, 2);
			itemToggle->fromJson(json{ { "targetGraphic", itemBg->GetFileID() }, { "graphic", itemCheck->GetFileID() }, { "isOn", true } });
			tscroll->SetContent(tcontent->GetFileID(), tview->GetFileID());
			tscroll->fromJson(json{ { "content", tcontent->GetFileID() }, { "viewport", tview->GetFileID() }, { "horizontal", false }, { "movementType", 2 } });
			templ->SetActive(false);
			dd->Options = { { "Option A", "" }, { "Option B", "" }, { "Option C", "" } };
			dd->SetParts(templ->GetFileID(), label->GetFileID(), itemLabel->GetFileID());
		}
		if (go == nullptr)
			return nullptr;
		go->SetParent(parent, false);
		SelectionManager::SetSelectedGameObject(go);
		EditorLog::Write("UI", "created %s under '%s'", kind.c_str(), parent->GetName().c_str());
		return go;
	}
}
