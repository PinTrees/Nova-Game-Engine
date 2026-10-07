#include "pch.h"
#include "LODGroup.h"
#include "Scene.h"
#include "SceneManager.h"
#include "SceneCulling.h"
#include "MeshRenderer.h"
#include "SkinnedMeshRenderer.h"
#include "Mesh.h"
#include "SkinnedMesh.h"
#include "Transform.h"
#include "RenderManager.h"
#include "UnityGUI.h"
#include "CliServer.h"
#include <chrono>
#include <unordered_map>

namespace
{
	std::vector<LODGroup*>& Registry()
	{
		static std::vector<LODGroup*> r;
		return r;
	}

	double Seconds()
	{
		static const auto s_Start = std::chrono::steady_clock::now();
		return std::chrono::duration<double>(std::chrono::steady_clock::now() - s_Start).count();
	}

	// GameObject 의 렌더러 (LOD 가 가리키는 것)
	template <typename F>
	void ForRenderers(GameObject* go, F&& f)
	{
		if (MeshRenderer* mr = go->GetComponent<MeshRenderer>())
			f(static_cast<Component*>(mr), false);
		if (SkinnedMeshRenderer* sr = go->GetComponent<SkinnedMeshRenderer>())
			f(static_cast<Component*>(sr), true);
	}

	// 렌더러의 월드 상자 (메시 정점 범위 → 월드 AABB)
	bool RendererBounds(GameObject* go, Vec3& mn, Vec3& mx)
	{
		bool any = false;
		auto add = [&](const std::vector<Vec3>& points) {
			const Matrix w = go->GetTransform()->GetWorldMatrix();
			for (const Vec3& p : points)
			{
				const Vec3 v = Vec3::Transform(p, w);
				mn = any ? Vec3::Min(mn, v) : v;
				mx = any ? Vec3::Max(mx, v) : v;
				any = true;
			}
		};
		auto corners = [](const Vec3& a, const Vec3& b) {
			std::vector<Vec3> c(8);
			for (int i = 0; i < 8; ++i)
				c[i] = Vec3((i & 1) ? b.x : a.x, (i & 2) ? b.y : a.y, (i & 4) ? b.z : a.z);
			return c;
		};
		auto range = [&](const auto& vertices) {
			if (vertices.empty())
				return;
			Vec3 a(vertices[0].pos), b(vertices[0].pos);
			for (const auto& v : vertices)
			{
				a = Vec3::Min(a, Vec3(v.pos));
				b = Vec3::Max(b, Vec3(v.pos));
			}
			add(corners(a, b));
		};
		if (MeshRenderer* mr = go->GetComponent<MeshRenderer>())
			if (auto mesh = mr->GetMesh())
				range(mesh->Vertices);
		if (SkinnedMeshRenderer* sr = go->GetComponent<SkinnedMeshRenderer>())
			if (auto mesh = sr->GetMesh())
				range(mesh->Vertices);
		return any;
	}

	float MaxScale(const Matrix& w)
	{
		return (std::max)({ Vec3(w._11, w._12, w._13).Length(), Vec3(w._21, w._22, w._23).Length(), Vec3(w._31, w._32, w._33).Length() });
	}

	// 막대 위치 (0 = 왼쪽 100 %) ↔ 화면 높이. Unity 처럼 제곱근 눈금 — 작은 값이 넓게
	float BarPos(float h) { return 1.0f - sqrtf(std::clamp(h, 0.0f, 1.0f)); }
	float BarHeight(float x) { const float s = 1.0f - std::clamp(x, 0.0f, 1.0f); return s * s; }

	const ImU32 kLodColors[LODGroup::kMaxLODs] = {
		IM_COL32(60, 92, 43, 255), IM_COL32(47, 87, 104, 255), IM_COL32(44, 63, 113, 255), IM_COL32(85, 59, 117, 255),
		IM_COL32(110, 56, 98, 255), IM_COL32(112, 76, 44, 255), IM_COL32(100, 96, 40, 255), IM_COL32(64, 64, 64, 255) };
	const ImU32 kCulledColor = IM_COL32(105, 38, 38, 255);

	std::string NameOf(const LODGroup* g)
	{
		GameObject* go = const_cast<LODGroup*>(g)->GetGameObject();
		return go ? go->GetName() : std::string();
	}
}

LODGroup::LODGroup()
{
	m_InspectorTitleName = "LOD Group";
	// Unity 기본: LOD 0 · 1 · 2 = 60 · 30 · 10 %, 그 아래 Culled
	m_LODs = { { 0.6f, 0.0f, {} }, { 0.3f, 0.0f, {} }, { 0.1f, 0.0f, {} } };
	Registry().push_back(this);
}

LODGroup::~LODGroup()
{
	auto& r = Registry();
	r.erase(std::remove(r.begin(), r.end(), this), r.end());
}

const std::vector<LODGroup*>& LODGroup::All()
{
	return Registry();
}

void LODGroup::SetLODs(const std::vector<LOD>& lods)
{
	m_LODs = lods;
	if ((int)m_LODs.size() > kMaxLODs)
		m_LODs.resize(kMaxLODs);
	// 높이는 앞에서 뒤로 줄어야 한다
	float prev = 1.0f;
	for (LOD& l : m_LODs)
	{
		l.ScreenRelativeTransitionHeight = std::clamp(l.ScreenRelativeTransitionHeight, 0.0f, prev);
		l.FadeTransitionWidth = std::clamp(l.FadeTransitionWidth, 0.0f, 1.0f);
		prev = l.ScreenRelativeTransitionHeight;
	}
	m_SelectedLOD = std::clamp(m_SelectedLOD, 0, (std::max)(0, (int)m_LODs.size() - 1));
}

int LODGroup::CurrentLOD(bool editor) const
{
	const int c = m_View[editor ? 1 : 0].Current;
	return c == -2 ? -2 : (c >= (int)m_LODs.size() ? -1 : c);
}

float LODGroup::ScreenRelativeHeight(CXMMATRIX view, CXMMATRIX proj) const
{
	if (m_pGameObject == nullptr)
		return 0.0f;
	const Matrix w = m_pGameObject->GetTransform()->GetWorldMatrix();
	const float size = m_Size * MaxScale(w);
	XMFLOAT4X4 p;
	XMStoreFloat4x4(&p, proj);
	if (fabsf(p._34) < 1e-6f)
		return size * p._22 * 0.5f;   // 직교: _22 = 1 / Orthographic Size
	const Vec3 ref = Vec3::Transform(m_LocalReferencePoint, w);
	XMFLOAT4X4 inv;
	XMStoreFloat4x4(&inv, XMMatrixInverse(nullptr, view));
	const float dist = (std::max)((ref - Vec3(inv._41, inv._42, inv._43)).Length(), 1e-4f);
	return size * p._22 / (2.0f * dist);   // _22 = 1 / tan(FOV / 2)
}

void LODGroup::RecalculateBounds()
{
	if (m_pGameObject == nullptr)
		return;
	Scene* scene = SceneManager::GetI()->GetCurrentScene();
	Vec3 mn, mx;
	bool any = false;
	for (const LOD& l : m_LODs)
		for (uint64 id : l.Renderers)
			if (GameObject* go = scene ? scene->FindByFileID(id) : nullptr)
			{
				Vec3 a, b;
				if (!RendererBounds(go, a, b))
					continue;
				mn = any ? Vec3::Min(mn, a) : a;
				mx = any ? Vec3::Max(mx, b) : b;
				any = true;
			}
	if (!any)
		return;
	// Unity: 중심 = 상자 중심 (로컬), 크기 = 가장 긴 변 / 가장 큰 축 배율
	const Matrix w = m_pGameObject->GetTransform()->GetWorldMatrix();
	m_LocalReferencePoint = Vec3::Transform((mn + mx) * 0.5f, w.Invert());
	const Vec3 ext = mx - mn;
	m_Size = (std::max)({ ext.x, ext.y, ext.z }) / (std::max)(MaxScale(w), 1e-6f);
	m_Size = (std::max)(m_Size, 1e-4f);
}

void LODGroup::SelectForView(Scene* scene, bool editor, bool capture)
{
	auto& all = Registry();
	if (all.empty() || scene == nullptr)
		return;
	const uint32_t stamp = ++SceneCulling::LodStamp;
	RenderManager* rm = RenderManager::GetI();
	const XMMATRIX view = editor ? rm->EditorCameraViewMatrix : rm->CameraViewMatrix;
	const XMMATRIX proj = editor ? rm->EditorCameraProjectionMatrix : rm->CameraProjectionMatrix;
	// fileID → GameObject (이 뷰에서 한 번)
	std::unordered_map<uint64, GameObject*> byID;
	for (GameObject* go : scene->GetAllGameObjects())
		if (go)
			byID.emplace(go->GetFileID(), go);
	const double now = Seconds();

	struct Item { Component* C; bool Skinned; uint32_t Mask; };
	std::vector<Item> items;
	for (LODGroup* g : all)
	{
		GameObject* owner = g->m_pGameObject;
		if (owner == nullptr || !g->m_Enabled || !owner->IsActiveInHierarchy() || g->m_LODs.empty() || byID.find(owner->GetFileID()) == byID.end())
			continue;   // 꺼진 LOD Group = 모든 LOD 를 그린다 (Unity)
		const int n = (int)g->m_LODs.size();
		const float h = g->ScreenRelativeHeight(view, proj);
		int target = n;   // n = Culled
		for (int i = 0; i < n; ++i)
			if (h >= g->m_LODs[i].ScreenRelativeTransitionHeight)
			{
				target = i;
				break;
			}

		// a 를 t 만큼, b 를 1 - t 만큼 보인다 (디더가 서로 겹치지 않는다)
		int a = target, b = -1;
		float t = 1.0f;
		ViewState& vs = g->m_View[editor ? 1 : 0];
		if (g->m_FadeMode == FadeCrossFade && !capture)
		{
			if (g->m_AnimateCrossFading)
			{
				if (vs.Current == -2 || vs.Current > n)
					vs.Previous = vs.Current = target;
				else if (target != vs.Current)
				{
					// 섞는 중에 또 바뀌면 지금 보이는 쪽에서 새로 시작
					vs.Previous = vs.Current;
					vs.Current = target;
					vs.FadeStart = now;
				}
				const float e = (float)((now - vs.FadeStart) / kCrossFadeAnimationDuration);
				if (vs.Previous != vs.Current && e < 1.0f)
				{
					a = vs.Current;
					b = vs.Previous;
					t = (std::max)(e, 0.0f);
				}
			}
			else if (target < n)
			{
				// 이 LOD 범위의 아래 끝 Fade Transition Width 비율에서 다음 LOD (또는 Culled) 와 섞는다
				const float lower = g->m_LODs[target].ScreenRelativeTransitionHeight;
				const float upper = target == 0 ? 1.0f : g->m_LODs[target - 1].ScreenRelativeTransitionHeight;
				const float band = g->m_LODs[target].FadeTransitionWidth * (upper - lower);
				if (band > 1e-6f && h < lower + band)
				{
					b = target + 1;
					t = std::clamp((h - lower) / band, 0.0f, 1.0f);
				}
			}
		}
		if (!capture)
		{
			if (!(g->m_FadeMode == FadeCrossFade && g->m_AnimateCrossFading))
				vs.Current = vs.Previous = target;
			vs.Height = h;
		}

		// 렌더러마다 어느 LOD 에 들었는지 (한 렌더러가 여러 LOD 에 들 수 있다)
		items.clear();
		for (int i = 0; i < n; ++i)
			for (uint64 id : g->m_LODs[i].Renderers)
			{
				const auto it = byID.find(id);
				if (it == byID.end())
					continue;
				ForRenderers(it->second, [&](Component* c, bool skinned) {
					for (Item& x : items)
						if (x.C == c) { x.Mask |= 1u << i; return; }
					items.push_back({ c, skinned, 1u << i });
				});
			}
		for (const Item& x : items)
		{
			const bool inA = a < n && (x.Mask & (1u << a));
			const bool inB = b >= 0 && b < n && (x.Mask & (1u << b));
			Component* c = x.C;
			c->LodStamp = stamp;
			c->LodFade = 0.0f;
			c->LodFadeBelow = false;
			const bool full = (inA && inB) || (inA && t >= 1.0f) || (inB && t <= 0.0f);
			const bool partial = !full && ((inA && t > 0.0f) || (inB && t < 1.0f));
			// 그림자 = 더 많이 보이는 쪽
			const bool dominant = full || (inA && t >= 0.5f) || (inB && t < 0.5f);
			c->LodShadowHidden = !dominant;
			if (x.Skinned || !partial)
			{
				c->LodHidden = x.Skinned ? !dominant : !full;
				continue;
			}
			c->LodHidden = false;
			c->LodFade = t;
			c->LodFadeBelow = inA;   // a: 무늬 < t 인 픽셀 (t 만큼), b: 무늬 ≥ t (1 - t 만큼)
		}
	}
}

void LODGroup::AddRenderers(int lod, GameObject* go)
{
	if (go == nullptr || lod < 0 || lod >= (int)m_LODs.size())
		return;
	// 끌어 놓은 GameObject 와 자식 중 렌더러가 있는 것 (Unity 처럼)
	std::function<void(GameObject*)> visit = [&](GameObject* o) {
		bool has = false;
		ForRenderers(o, [&](Component*, bool) { has = true; });
		auto& list = m_LODs[lod].Renderers;
		if (has && std::find(list.begin(), list.end(), o->GetFileID()) == list.end())
			list.push_back(o->GetFileID());
		for (GameObject* c : o->Children())
			visit(c);
	};
	visit(go);
	RecalculateBounds();
}

void LODGroup::DrawLODBar()
{
	const int n = (int)m_LODs.size();
	UnityGUI::Spacing(18.0f);   // 위에 카메라 표시 자리
	const ImVec2 p = ImGui::GetCursorScreenPos();
	const float w = ImGui::GetContentRegionAvail().x - 28.0f;
	const float x0 = p.x + 18.0f, h = 34.0f;
	ImDrawList* dl = ImGui::GetWindowDrawList();
	const ImVec2 mouse = ImGui::GetIO().MousePos;
	auto xOf = [&](float height) { return x0 + w * BarPos(height); };

	// 마우스: 경계 끌기 · 칸 고르기 · 오른쪽 클릭 메뉴 · Hierarchy 에서 끌어 놓기
	ImGui::SetCursorScreenPos(ImVec2(x0, p.y));
	ImGui::InvisibleButton("##lodbar", ImVec2((std::max)(w, 1.0f), h), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
	const bool hovered = ImGui::IsItemHovered();
	int hoverSeg = -1, hoverEdge = -1;
	if (hovered)
	{
		for (int i = 0; i < n; ++i)
			if (fabsf(mouse.x - xOf(m_LODs[i].ScreenRelativeTransitionHeight)) < 5.0f)
				hoverEdge = i;
		float start = 1.0f;
		for (int i = 0; i <= n; ++i)
		{
			const float end = i < n ? m_LODs[i].ScreenRelativeTransitionHeight : 0.0f;
			if (mouse.x >= xOf(start) && mouse.x < xOf(end) + (i == n ? 1.0f : 0.0f))
				hoverSeg = i;
			start = end;
		}
	}
	if (ImGui::IsItemActivated() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		if (hoverEdge >= 0)
			m_DragBoundary = hoverEdge;
		else if (hoverSeg >= 0 && hoverSeg < n)
			m_SelectedLOD = hoverSeg;
	}
	if (m_DragBoundary >= 0 && m_DragBoundary < n)
	{
		if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			// 이웃 경계 사이로
			const float hi = m_DragBoundary == 0 ? 1.0f : m_LODs[m_DragBoundary - 1].ScreenRelativeTransitionHeight - 0.001f;
			const float lo = m_DragBoundary + 1 < n ? m_LODs[m_DragBoundary + 1].ScreenRelativeTransitionHeight + 0.001f : 0.0f;
			m_LODs[m_DragBoundary].ScreenRelativeTransitionHeight = std::clamp(BarHeight((mouse.x - x0) / (std::max)(w, 1.0f)), lo, (std::max)(hi, lo));
		}
		else
			m_DragBoundary = -1;
	}
	if (hoverEdge >= 0 || m_DragBoundary >= 0)
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
	static int s_MenuSeg = -1;
	if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && hoverSeg >= 0)
	{
		s_MenuSeg = hoverSeg;
		ImGui::OpenPopup("##lodmenu");
	}
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("GAME_OBJECT"))
			if (GameObject* dropped = *static_cast<GameObject* const*>(pl->Data))
				if (hoverSeg >= 0 && hoverSeg < n)
				{
					AddRenderers(hoverSeg, dropped);
					m_SelectedLOD = hoverSeg;
				}
		ImGui::EndDragDropTarget();
	}
	if (ImGui::BeginPopup("##lodmenu"))
	{
		const int seg = s_MenuSeg;
		if (ImGui::MenuItem("Insert Before", nullptr, false, n < kMaxLODs && seg >= 0 && seg <= n))
		{
			// 이 칸의 앞 경계와 끝 경계 가운데
			const float upper = seg == 0 ? 1.0f : m_LODs[seg - 1].ScreenRelativeTransitionHeight;
			const float lower = seg < n ? m_LODs[seg].ScreenRelativeTransitionHeight : 0.0f;
			LOD l;
			l.ScreenRelativeTransitionHeight = (upper + lower) * 0.5f;
			m_LODs.insert(m_LODs.begin() + seg, l);
			m_SelectedLOD = seg;
		}
		if (ImGui::MenuItem("Delete", nullptr, false, n > 1 && seg >= 0 && seg < n))
		{
			m_LODs.erase(m_LODs.begin() + seg);
			m_SelectedLOD = std::clamp(m_SelectedLOD, 0, (int)m_LODs.size() - 1);
		}
		ImGui::EndPopup();
	}

	// 칸: LOD i (전환 높이 %), 끝에 Culled
	const int count = (int)m_LODs.size();
	float start = 1.0f;
	for (int i = 0; i <= count; ++i)
	{
		const float end = i < count ? m_LODs[i].ScreenRelativeTransitionHeight : 0.0f;
		const float a = xOf(start), b = xOf(end);
		ImU32 col = i < count ? kLodColors[i] : kCulledColor;
		if (i == hoverSeg && m_DragBoundary < 0)
		{
			ImVec4 c = ImGui::ColorConvertU32ToFloat4(col);
			col = ImGui::ColorConvertFloat4ToU32(ImVec4((std::min)(c.x * 1.2f, 1.0f), (std::min)(c.y * 1.2f, 1.0f), (std::min)(c.z * 1.2f, 1.0f), 1.0f));
		}
		dl->AddRectFilled(ImVec2(a, p.y), ImVec2(b, p.y + h), col);
		if (i < count && i == m_SelectedLOD)
			dl->AddRect(ImVec2(a + 1.0f, p.y + 1.0f), ImVec2(b - 1.0f, p.y + h - 1.0f), IM_COL32(58, 121, 187, 255), 0.0f, 0, 2.0f);
		char name[24], pct[24];
		if (i < count)
		{
			snprintf(name, sizeof(name), "LOD %d", i);
			snprintf(pct, sizeof(pct), "%.0f%%", m_LODs[i].ScreenRelativeTransitionHeight * 100.0f);
		}
		else
		{
			snprintf(name, sizeof(name), "Culled");
			pct[0] = 0;
		}
		dl->PushClipRect(ImVec2(a, p.y), ImVec2(b, p.y + h), true);
		dl->AddText(ImVec2(a + 4.0f, p.y + 2.0f), IM_COL32(230, 230, 230, 255), name);
		if (pct[0])
			dl->AddText(ImVec2(a + 4.0f, p.y + 17.0f), IM_COL32(200, 200, 200, 255), pct);
		dl->PopClipRect();
		if (i > 0)
			dl->AddLine(ImVec2(a, p.y), ImVec2(a, p.y + h), IM_COL32(20, 20, 20, 255), 2.0f);
		start = end;
	}
	dl->AddRect(ImVec2(x0, p.y), ImVec2(x0 + w, p.y + h), IM_COL32(26, 26, 26, 255));

	// Scene 뷰 카메라가 보는 화면 높이 (Unity 의 카메라 표시)
	if (m_View[1].Current != -2)
	{
		const float cx = x0 + w * BarPos(m_View[1].Height);
		dl->AddLine(ImVec2(cx, p.y - 2.0f), ImVec2(cx, p.y + h), IM_COL32(240, 240, 240, 220), 1.0f);
		dl->AddTriangleFilled(ImVec2(cx - 5.0f, p.y - 9.0f), ImVec2(cx + 5.0f, p.y - 9.0f), ImVec2(cx, p.y - 2.0f), IM_COL32(240, 240, 240, 255));
		char text[24];
		snprintf(text, sizeof(text), "%.1f%%", m_View[1].Height * 100.0f);
		const ImVec2 ts = ImGui::CalcTextSize(text);
		const float tx = std::clamp(cx + 7.0f, x0, x0 + w - ts.x);
		dl->AddText(ImVec2(tx, p.y - 17.0f), IM_COL32(220, 220, 220, 255), text);
	}
	ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h + 4.0f));
	ImGui::Dummy(ImVec2(w, 1.0f));
}

void LODGroup::OnInspectorGUI()
{
	using namespace UnityGUI;
	static const char* kFadeModes[] = { "None", "Cross Fade" };
	Dropdown("Fade Mode", &m_FadeMode, kFadeModes, 2);
	if (m_FadeMode == FadeCrossFade)
		Toggle("Animate Cross-fading", &m_AnimateCrossFading);
	if (Float("Object Size", &m_Size))
		m_Size = (std::max)(m_Size, 1e-4f);

	DrawLODBar();
	ImGui::TextDisabled("  Drag the edges to change, right-click to insert / delete, drop objects on a LOD.");

	// 고른 LOD: 전환 높이 · Fade Transition Width · 렌더러
	if (m_SelectedLOD >= 0 && m_SelectedLOD < (int)m_LODs.size())
	{
		LOD& l = m_LODs[m_SelectedLOD];
		char title[32];
		snprintf(title, sizeof(title), "LOD %d", m_SelectedLOD);
		Spacing(4.0f);
		Label(title, 0, true);
		float pct = l.ScreenRelativeTransitionHeight * 100.0f;
		if (Float("Screen Relative Height (%)", &pct))
		{
			const float hi = m_SelectedLOD == 0 ? 100.0f : m_LODs[m_SelectedLOD - 1].ScreenRelativeTransitionHeight * 100.0f;
			const float lo = m_SelectedLOD + 1 < (int)m_LODs.size() ? m_LODs[m_SelectedLOD + 1].ScreenRelativeTransitionHeight * 100.0f : 0.0f;
			l.ScreenRelativeTransitionHeight = std::clamp(pct, lo, hi) / 100.0f;
		}
		if (m_FadeMode == FadeCrossFade && !m_AnimateCrossFading)
			Slider("Fade Transition Width", &l.FadeTransitionWidth, 0.0f, 1.0f);
		Label("Renderers");
		for (size_t k = 0; k < l.Renderers.size(); ++k)
		{
			char label[32];
			snprintf(label, sizeof(label), "Element %d", (int)k);
			uint64 id = l.Renderers[k];
			if (GameObjectField(label, &id, 1))
			{
				if (id == 0)
				{
					l.Renderers.erase(l.Renderers.begin() + k);
					break;
				}
				l.Renderers[k] = id;
				RecalculateBounds();
			}
		}
		uint64 added = 0;
		if (GameObjectField("Add", &added, 1) && added != 0)
		{
			Scene* scene = SceneManager::GetI()->GetCurrentScene();
			AddRenderers(m_SelectedLOD, scene ? scene->FindByFileID(added) : nullptr);
		}
	}
	Spacing(6.0f);
	if (CenterButton("Recalculate Bounds"))
		RecalculateBounds();
}

void LODGroup::RemapFileIDs(const std::unordered_map<uint64, uint64>& map)
{
	for (LOD& l : m_LODs)
		for (uint64& id : l.Renderers)
			if (auto it = map.find(id); it != map.end())
				id = it->second;
}

void LODGroup::RegisterEditor()
{
	// nova lod info | assign --name G --lod 1 --object O | set --name G ... | recalc --name G
	CliServer::Register("lod", "LOD Group op: {op: info | assign | set | recalc, name?, lod?, object?} (nova lod help)", [](const nlohmann::json& args, nlohmann::json& result, std::string& error) {
		const std::string op = args.value("op", std::string("help"));
		const std::string name = args.value("name", std::string());
		Scene* scene = SceneManager::GetI()->GetCurrentScene();
		auto findGroup = [&]() -> LODGroup* {
			for (LODGroup* g : All())
				if (NameOf(g) == name)
					return g;
			return nullptr;
		};
		if (op == "help")
		{
			result = { { "ops", { "info: LOD Groups — LODs (height, fade width, renderers), Object Size, the LOD each view picked (-1 = Culled) and its screen height",
				"assign --name G --lod N --object O: add O (and children with renderers) to LOD N, then recalculate bounds",
				"set --name G [--fadeMode 0|1] [--animate true|false] [--lod N --height 0.3 --fadeWidth 0.5]: change values (renderers kept)",
				"recalc --name G: Recalculate Bounds" } } };
			return true;
		}
		if (op == "info")
		{
			nlohmann::json list = nlohmann::json::array();
			for (LODGroup* g : All())
			{
				nlohmann::json lods = nlohmann::json::array();
				for (const LOD& l : g->m_LODs)
				{
					nlohmann::json names = nlohmann::json::array();
					for (uint64 id : l.Renderers)
					{
						GameObject* go = scene ? scene->FindByFileID(id) : nullptr;
						names.push_back(go ? go->GetName() : std::string("(missing)"));
					}
					lods.push_back({ { "height", l.ScreenRelativeTransitionHeight }, { "fadeTransitionWidth", l.FadeTransitionWidth }, { "renderers", names } });
				}
				list.push_back({ { "name", NameOf(g) }, { "fadeMode", g->m_FadeMode == FadeCrossFade ? "CrossFade" : "None" },
					{ "animateCrossFading", g->m_AnimateCrossFading }, { "size", g->m_Size },
					{ "localReferencePoint", { g->m_LocalReferencePoint.x, g->m_LocalReferencePoint.y, g->m_LocalReferencePoint.z } },
					{ "lods", lods }, { "gameLOD", g->CurrentLOD(false) }, { "gameHeight", g->CurrentHeight(false) },
					{ "sceneLOD", g->CurrentLOD(true) }, { "sceneHeight", g->CurrentHeight(true) } });
			}
			result = { { "groups", list } };
			return true;
		}
		LODGroup* g = findGroup();
		if (g == nullptr) { error = "no LOD Group on '" + name + "'"; return false; }
		if (op == "recalc")
		{
			g->RecalculateBounds();
			result = { { "size", g->m_Size }, { "localReferencePoint", { g->m_LocalReferencePoint.x, g->m_LocalReferencePoint.y, g->m_LocalReferencePoint.z } } };
			return true;
		}
		if (op == "set")
		{
			// 렌더러 목록은 그대로 두고 값만 (JSON 왕복 없이 — fileID 가 64 비트)
			if (args.contains("fadeMode"))
				g->m_FadeMode = std::clamp(args.value("fadeMode", 0), 0, 1);
			if (args.contains("animate"))
				g->m_AnimateCrossFading = args.value("animate", false);
			if (args.contains("lod"))
			{
				const int lod = args.value("lod", 0);
				if (lod < 0 || lod >= (int)g->m_LODs.size()) { error = "LOD index out of range"; return false; }
				std::vector<LOD> lods = g->m_LODs;
				if (args.contains("height"))
					lods[lod].ScreenRelativeTransitionHeight = args.value("height", 0.0f);
				if (args.contains("fadeWidth"))
					lods[lod].FadeTransitionWidth = args.value("fadeWidth", 0.0f);
				g->SetLODs(lods);
			}
			result = { { "fadeMode", g->m_FadeMode }, { "animateCrossFading", g->m_AnimateCrossFading } };
			return true;
		}
		if (op == "assign")
		{
			const int lod = args.value("lod", 0);
			const std::string objName = args.value("object", std::string());
			GameObject* obj = nullptr;
			if (scene)
				for (GameObject* go : scene->GetAllGameObjects())
					if (go && go->GetName() == objName) { obj = go; break; }
			if (obj == nullptr) { error = "no GameObject '" + objName + "'"; return false; }
			if (lod < 0 || lod >= (int)g->m_LODs.size()) { error = "LOD index out of range"; return false; }
			g->AddRenderers(lod, obj);
			result = { { "lod", lod }, { "renderers", g->m_LODs[lod].Renderers.size() }, { "size", g->m_Size } };
			return true;
		}
		error = "unknown op '" + op + "' (nova lod help)";
		return false;
	});
}

GENERATE_COMPONENT_FUNC_TOJSON(LODGroup)
{
	json j;
	SERIALIZE_TYPE(j, LODGroup);
	j["enabled"] = m_Enabled;
	j["fadeMode"] = m_FadeMode;
	j["animateCrossFading"] = m_AnimateCrossFading;
	j["localReferencePoint"] = { m_LocalReferencePoint.x, m_LocalReferencePoint.y, m_LocalReferencePoint.z };
	j["size"] = m_Size;
	json lods = json::array();
	for (const LOD& l : m_LODs)
		lods.push_back({ { "screenRelativeTransitionHeight", l.ScreenRelativeTransitionHeight }, { "fadeTransitionWidth", l.FadeTransitionWidth }, { "renderers", l.Renderers } });
	j["lods"] = lods;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(LODGroup)
{
	m_Enabled = j.value("enabled", true);
	m_FadeMode = std::clamp(j.value("fadeMode", 0), 0, 1);
	m_AnimateCrossFading = j.value("animateCrossFading", false);
	if (j.contains("localReferencePoint") && j["localReferencePoint"].is_array() && j["localReferencePoint"].size() == 3)
		m_LocalReferencePoint = Vec3(j["localReferencePoint"][0].get<float>(), j["localReferencePoint"][1].get<float>(), j["localReferencePoint"][2].get<float>());
	m_Size = (std::max)(j.value("size", 1.0f), 1e-4f);
	if (j.contains("lods") && j["lods"].is_array())
	{
		std::vector<LOD> lods;
		for (const json& e : j["lods"])
		{
			LOD l;
			l.ScreenRelativeTransitionHeight = e.value("screenRelativeTransitionHeight", 0.0f);
			l.FadeTransitionWidth = e.value("fadeTransitionWidth", 0.0f);
			if (e.contains("renderers") && e["renderers"].is_array())
				for (const json& id : e["renderers"])
					if (id.is_number_unsigned() || id.is_number_integer())
						l.Renderers.push_back(id.get<uint64>());
			lods.push_back(l);
		}
		SetLODs(lods);
	}
}
