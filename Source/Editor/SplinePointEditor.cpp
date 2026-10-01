#include "pch.h"
#include "SplinePointEditor.h"
#include "Terrain.h"
#include "UnityGUI.h"
#include "SceneViewOverlay.h"
#include "UndoSystem.h"
#include "EditorCamera.h"

namespace
{
	constexpr float kHandleRadius = 6.0f;
}

Vec3 SplinePointSource::DisplayWorld(int i) const
{
	return XMVector3TransformCoord(LocalPos(i), World());
}

namespace SplinePointEditor
{
	void SetOwner(State& s, const void* owner)
	{
		if (s.Owner != owner)
		{
			s.Owner = owner;
			s.Selected = -1;
			s.Dragging = -1;
		}
	}

	bool EditButton(State& s, int count)
	{
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 18.0f);
		const bool wasEdit = s.Edit;
		if (wasEdit)
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.45f, 0.72f, 1.0f));
		const bool clicked = ImGui::Button(s.Edit ? "Editing Points (click to stop)" : "Edit Points", ImVec2(220, 0));
		if (clicked)
			s.Edit = !s.Edit;
		if (wasEdit)
			ImGui::PopStyleColor();
		ImGui::SameLine();
		ImGui::TextDisabled("%d points", count);
		if (s.Edit)
			UnityGUI::HelpBox("Drag a point to move it over the terrain.\nCtrl+Click = add a point, Shift+Click or Delete = remove the selected point.", false, 1);
		return clicked;
	}

	void DrawPoints(const SplinePointSource& src, const State& s, ImU32 color)
	{
		ImDrawList* dl = ImGui::GetWindowDrawList();
		for (int i = 0; i < src.Count(); ++i)
		{
			const Vec3 w = src.DisplayWorld(i);
			ImVec2 p;
			if (!SceneViewOverlay::Project(XMFLOAT3(w.x, w.y + 0.3f, w.z), p))
				continue;
			const bool sel = i == s.Selected;
			const ImU32 fill = sel ? IM_COL32(255, 210, 80, 255) : (s.Edit ? color : (color & 0x00FFFFFF) | 0x96000000);
			dl->AddCircleFilled(p, sel ? kHandleRadius + 1.5f : kHandleRadius, fill);
			dl->AddCircle(p, sel ? kHandleRadius + 1.5f : kHandleRadius, IM_COL32(20, 30, 40, 255), 0, 1.5f);
			if (const char* label = src.EndLabel(i))
				dl->AddText(ImVec2(p.x + 9, p.y - 8), IM_COL32(220, 240, 255, 230), label);
		}
	}

	bool SceneGUI(SplinePointSource& src, State& s, EditorCamera* camera, const ImVec2& viewMin, const ImVec2& viewMax, bool viewHovered)
	{
		if (!s.Edit || camera == nullptr)
		{
			s.Dragging = -1;
			return false;
		}
		ImGuiIO& io = ImGui::GetIO();
		const XMMATRIX viewProj = camera->View() * camera->Proj();
		const float nx = (io.MousePos.x - viewMin.x) / (viewMax.x - viewMin.x) * 2.0f - 1.0f;
		const float ny = 1.0f - (io.MousePos.y - viewMin.y) / (viewMax.y - viewMin.y) * 2.0f;
		const XMMATRIX inv = XMMatrixInverse(nullptr, viewProj);
		const Vec3 nearP = XMVector3TransformCoord(XMVectorSet(nx, ny, 0.0f, 1.0f), inv);
		const Vec3 farP = XMVector3TransformCoord(XMVectorSet(nx, ny, 1.0f, 1.0f), inv);
		Vec3 dir = farP - nearP;
		const float len = dir.Length();
		if (!(len > 1e-6f) || !std::isfinite(len))
			return true;
		dir *= 1.0f / len;
		// 마우스가 가리키는 땅: 지형, 없으면 Source 가 정한 높이의 평면
		Vec3 hit;
		bool onGround = false;
		for (Terrain* t : Terrain::GetActiveTerrains())
			if (t->Raycast(nearP, dir, len, hit)) { onGround = true; break; }
		if (!onGround && fabsf(dir.y) > 1e-4f)
		{
			const float t = (src.FallbackPlaneY() - nearP.y) / dir.y;
			if (t > 0.0f) { hit = nearP + dir * t; onGround = true; }
		}

		const XMMATRIX world = src.World();
		const XMMATRIX toLocal = XMMatrixInverse(nullptr, world);
		auto worldOf = [&](int i) { return Vec3(XMVector3TransformCoord(src.LocalPos(i), world)); };

		// 가장 가까운 점 (화면)
		int hover = -1;
		float bestD = kHandleRadius + 4.0f;
		ImVec2 hoverPos;
		for (int i = 0; i < src.Count(); ++i)
		{
			const Vec3 w = src.DisplayWorld(i);
			ImVec2 p;
			if (!SceneViewOverlay::Project(XMFLOAT3(w.x, w.y + 0.3f, w.z), p))
				continue;
			const float d = sqrtf((p.x - io.MousePos.x) * (p.x - io.MousePos.x) + (p.y - io.MousePos.y) * (p.y - io.MousePos.y));
			if (d < bestD) { bestD = d; hover = i; hoverPos = p; }
		}
		if (hover >= 0 && s.Dragging < 0)
			ImGui::GetWindowDrawList()->AddCircle(hoverPos, kHandleRadius + 4.0f, IM_COL32(255, 255, 255, 200), 0, 1.5f);

		const std::string noun = src.Noun();
		const bool click = viewHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt;
		auto removePoint = [&](int i) {
			if (i < 0 || i >= src.Count() || src.Count() <= src.MinPoints())
				return;
			src.Erase(i);
			s.Selected = -1;
			src.Changed();
			Undo::SetActionName("Remove " + noun);
			Undo::RequestCheck();
		};

		if (click && io.KeyShift && hover >= 0)
			removePoint(hover);
		else if (click && io.KeyCtrl && onGround)
		{
			// 점 추가: 가장 가까운 변에 끼운다 (열린 곡선은 끝 바깥이면 그 끝을 늘린다)
			const int n = src.Count();
			int insertAt = n;
			float best = FLT_MAX;
			const int edges = src.Closed() ? n : n - 1;
			for (int i = 0; i < edges; ++i)
			{
				const Vec3 a = worldOf(i), b = worldOf((i + 1) % n);
				const float abx = b.x - a.x, abz = b.z - a.z, l2 = abx * abx + abz * abz;
				const float t = l2 > 1e-6f ? std::clamp(((hit.x - a.x) * abx + (hit.z - a.z) * abz) / l2, 0.0f, 1.0f) : 0.0f;
				const float dx = a.x + abx * t - hit.x, dz = a.z + abz * t - hit.z;
				const float d = dx * dx + dz * dz;
				if (d < best)
				{
					best = d;
					insertAt = (!src.Closed() && t <= 0.0f && i == 0) ? 0 : (!src.Closed() && t >= 1.0f && i == edges - 1) ? n : i + 1;
				}
			}
			const int copyFrom = n == 0 ? -1 : (std::min)(insertAt, n - 1);
			const Vec3 w = src.Place(hit, copyFrom);
			src.Insert(insertAt, copyFrom, XMVector3TransformCoord(w, toLocal));
			s.Selected = insertAt;
			src.Changed();
			Undo::SetActionName("Add " + noun);
			Undo::RequestCheck();
		}
		else if (click && hover >= 0)
		{
			s.Selected = hover;
			s.Dragging = hover;
		}

		if (s.Dragging >= 0)
		{
			if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
				s.Dragging = -1;   // 놓으면 씬 Undo 가 기록
			else if (onGround && s.Dragging < src.Count())
			{
				Vec3 local = XMVector3TransformCoord(src.Place(hit, s.Dragging), toLocal);
				if (src.KeepLocalY())
					local.y = src.LocalPos(s.Dragging).y;
				src.SetLocalPos(s.Dragging, local);
				src.Changed();
				Undo::SetActionName("Move " + noun);
			}
		}
		if (viewHovered && ImGui::IsKeyPressed(ImGuiKey_Delete) && s.Selected >= 0)
			removePoint(s.Selected);
		return true;
	}
}
