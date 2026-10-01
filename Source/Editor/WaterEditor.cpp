#include "pch.h"
#include "WaterEditor.h"
#include "WaterBody.h"
#include "Terrain.h"
#include "Transform.h"
#include "UnityGUI.h"
#include "SceneViewOverlay.h"
#include "SelectionManager.h"
#include "UndoSystem.h"
#include "EditorCamera.h"

namespace
{
	bool s_Edit = false;
	int s_Selected = -1;
	int s_Dragging = -1;
	const WaterBody* s_Body = nullptr;   // 편집 상태가 가리키는 물 (선택이 바뀌면 초기화)

	constexpr float kHandleRadius = 6.0f;

	WaterBody* SelectedBody()
	{
		GameObject* go = SelectionManager::GetSelectedObjectType() == SelectionType::GAMEOBJECT ? SelectionManager::GetSelectedGameObject() : nullptr;
		WaterBody* b = go ? go->GetComponent<WaterBody>() : nullptr;
		if (b != s_Body)
		{
			s_Body = b;
			s_Selected = -1;
			s_Dragging = -1;
		}
		return b;
	}

	Vec3 ToWorld(const WaterBody& b, const Vec3& local)
	{
		return XMVector3TransformCoord(local, b.WorldMatrix());
	}

	Vec3 ToLocal(const WaterBody& b, const Vec3& world)
	{
		const XMMATRIX inv = XMMatrixInverse(nullptr, b.WorldMatrix());
		return XMVector3TransformCoord(world, inv);
	}

	// 점이 놓일 월드 위치: 강 = 땅(물로 파기 전) - 0.6 m, 호수 = 수면 높이
	Vec3 PlacePoint(const WaterBody& b, Vec3 hit, float width)
	{
		(void)width;
		if (b.BodyType == WaterBody::Type::Lake)
			hit.y = b.SurfaceY();
		else
		{
			float g;
			if (WaterBody::GroundAt(hit.x, hit.z, g))
				hit.y = g - 0.6f;
		}
		return hit;
	}
}

namespace WaterEditor
{
	bool IsEditing() { return s_Edit; }

	void InspectorPoints(WaterBody& body)
	{
		using namespace UnityGUI;
		SelectedBody();
		const bool river = body.BodyType == WaterBody::Type::River;
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 18.0f);
		const bool wasEdit = s_Edit;
		if (wasEdit)
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.45f, 0.72f, 1.0f));
		if (ImGui::Button(s_Edit ? "Editing Points (click to stop)" : "Edit Points", ImVec2(220, 0)))
			s_Edit = !s_Edit;
		if (wasEdit)
			ImGui::PopStyleColor();
		ImGui::SameLine();
		ImGui::TextDisabled("%d points", (int)body.Points.size());
		if (s_Edit)
			HelpBox("Drag a point to move it over the terrain.\nCtrl+Click = add a point, Shift+Click or Delete = remove the selected point.", false, 1);

		if (s_Selected >= 0 && s_Selected < (int)body.Points.size())
		{
			WaterBody::Point& p = body.Points[s_Selected];
			char label[64];
			snprintf(label, sizeof(label), "Point %d", s_Selected);
			Label(label, 1, true);
			UnityGUI::Vector3("Position", &p.Position.x, false, 1);
			if (river)
			{
				Slider("Width", &p.Width, 1.0f, 200.0f, 1);
				Slider("Depth", &p.Depth, 0.2f, 20.0f, 1);
				Slider("Speed", &p.Speed, 0.0f, 4.0f, 1);
			}
		}

		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 18.0f);
		if (ImGui::Button(river ? "Snap Points To Ground" : "Fit Level To Shore", ImVec2(180, 0)))
		{
			body.SnapToGround();
			++body.Revision;
			Undo::SetActionName(river ? "Snap River Points" : "Fit Lake Level");
			Undo::RequestCheck();
		}
		ImGui::SameLine();
		if (ImGui::Button("Reset Shape", ImVec2(110, 0)))
		{
			body.ResetShape();
			s_Selected = -1;
			Undo::SetActionName("Reset Water Shape");
			Undo::RequestCheck();
		}
	}

	void DrawPoints(const WaterBody& body)
	{
		ImDrawList* dl = ImGui::GetWindowDrawList();
		for (int i = 0; i < (int)body.Points.size(); ++i)
		{
			Vec3 w = ToWorld(body, body.Points[i].Position);
			if (body.BodyType == WaterBody::Type::Lake)
				w.y = body.SurfaceY();
			ImVec2 s;
			if (!SceneViewOverlay::Project(XMFLOAT3(w.x, w.y + 0.3f, w.z), s))
				continue;
			const bool sel = i == s_Selected && s_Body == &body;
			const ImU32 fill = sel ? IM_COL32(255, 210, 80, 255) : (s_Edit ? IM_COL32(80, 190, 255, 255) : IM_COL32(80, 190, 255, 150));
			dl->AddCircleFilled(s, sel ? kHandleRadius + 1.5f : kHandleRadius, fill);
			dl->AddCircle(s, sel ? kHandleRadius + 1.5f : kHandleRadius, IM_COL32(20, 30, 40, 255), 0, 1.5f);
			if (body.BodyType == WaterBody::Type::River && (i == 0 || i + 1 == (int)body.Points.size()))
				dl->AddText(ImVec2(s.x + 9, s.y - 8), IM_COL32(220, 240, 255, 230), i == 0 ? "Source" : "Mouth");
		}
	}

	bool SceneGUI(EditorCamera* camera, const ImVec2& viewMin, const ImVec2& viewMax, bool viewHovered)
	{
		WaterBody* body = SelectedBody();
		if (body == nullptr || body->BodyType == WaterBody::Type::Ocean || !s_Edit || camera == nullptr)
		{
			s_Dragging = -1;
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
		// 마우스가 가리키는 땅: 지형, 없으면 수면 높이 평면
		Vec3 hit;
		bool onGround = false;
		for (Terrain* t : Terrain::GetActiveTerrains())
			if (t->Raycast(nearP, dir, len, hit)) { onGround = true; break; }
		if (!onGround && fabsf(dir.y) > 1e-4f)
		{
			const float planeY = body->BodyType == WaterBody::Type::Lake ? body->SurfaceY() : (body->Points.empty() ? body->SurfaceY() : ToWorld(*body, body->Points[0].Position).y);
			const float t = (planeY - nearP.y) / dir.y;
			if (t > 0.0f) { hit = nearP + dir * t; onGround = true; }
		}

		// 가장 가까운 점 (화면)
		int hover = -1;
		float bestD = kHandleRadius + 4.0f;
		for (int i = 0; i < (int)body->Points.size(); ++i)
		{
			Vec3 w = ToWorld(*body, body->Points[i].Position);
			if (body->BodyType == WaterBody::Type::Lake)
				w.y = body->SurfaceY();
			ImVec2 s;
			if (!SceneViewOverlay::Project(XMFLOAT3(w.x, w.y + 0.3f, w.z), s))
				continue;
			const float d = sqrtf((s.x - io.MousePos.x) * (s.x - io.MousePos.x) + (s.y - io.MousePos.y) * (s.y - io.MousePos.y));
			if (d < bestD) { bestD = d; hover = i; }
		}
		if (hover >= 0 && s_Dragging < 0)
		{
			ImVec2 s;
			Vec3 w = ToWorld(*body, body->Points[hover].Position);
			if (body->BodyType == WaterBody::Type::Lake) w.y = body->SurfaceY();
			if (SceneViewOverlay::Project(XMFLOAT3(w.x, w.y + 0.3f, w.z), s))
				ImGui::GetWindowDrawList()->AddCircle(s, kHandleRadius + 4.0f, IM_COL32(255, 255, 255, 200), 0, 1.5f);
		}

		const bool click = viewHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt;
		const int minPoints = body->BodyType == WaterBody::Type::Lake ? 3 : 2;
		auto removePoint = [&](int i) {
			if (i < 0 || i >= (int)body->Points.size() || (int)body->Points.size() <= minPoints)
				return;
			body->Points.erase(body->Points.begin() + i);
			s_Selected = -1;
			++body->Revision;
			Undo::SetActionName("Remove Water Point");
			Undo::RequestCheck();
		};

		if (click && io.KeyShift && hover >= 0)
			removePoint(hover);
		else if (click && io.KeyCtrl && onGround)
		{
			// 점 추가: 가까운 변에 끼운다 (강은 끝점 바깥이면 그 끝을 늘린다)
			const int n = (int)body->Points.size();
			const Vec3 w = PlacePoint(*body, hit, n > 0 ? body->Points[n - 1].Width : 14.0f);
			int insertAt = n;
			float best = FLT_MAX;
			const int edges = body->BodyType == WaterBody::Type::Lake ? n : n - 1;
			for (int i = 0; i < edges; ++i)
			{
				const Vec3 a = ToWorld(*body, body->Points[i].Position), b = ToWorld(*body, body->Points[(i + 1) % n].Position);
				const float abx = b.x - a.x, abz = b.z - a.z, l2 = abx * abx + abz * abz;
				const float t = l2 > 1e-6f ? std::clamp(((w.x - a.x) * abx + (w.z - a.z) * abz) / l2, 0.0f, 1.0f) : 0.0f;
				const float dx = a.x + abx * t - w.x, dz = a.z + abz * t - w.z;
				const float d = dx * dx + dz * dz;
				if (d < best)
				{
					best = d;
					insertAt = (body->BodyType == WaterBody::Type::River && t <= 0.0f && i == 0) ? 0
						: (body->BodyType == WaterBody::Type::River && t >= 1.0f && i == edges - 1) ? n : i + 1;
				}
			}
			WaterBody::Point p = body->Points.empty() ? WaterBody::Point() : body->Points[(std::min)(insertAt, n - 1)];
			p.Position = ToLocal(*body, w);
			body->Points.insert(body->Points.begin() + insertAt, p);
			s_Selected = insertAt;
			++body->Revision;
			Undo::SetActionName("Add Water Point");
			Undo::RequestCheck();
		}
		else if (click && hover >= 0)
		{
			s_Selected = hover;
			s_Dragging = hover;
		}

		if (s_Dragging >= 0)
		{
			if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
				s_Dragging = -1;   // 놓으면 씬 Undo 가 "Move Water Point" 로 기록
			else if (onGround && s_Dragging < (int)body->Points.size())
			{
				const Vec3 w = PlacePoint(*body, hit, body->Points[s_Dragging].Width);
				Vec3 local = ToLocal(*body, w);
				if (body->BodyType == WaterBody::Type::Lake)
					local.y = body->Points[s_Dragging].Position.y;
				body->Points[s_Dragging].Position = local;
				++body->Revision;
				Undo::SetActionName("Move Water Point");
			}
		}
		if (viewHovered && ImGui::IsKeyPressed(ImGuiKey_Delete) && s_Selected >= 0)
			removePoint(s_Selected);
		return true;
	}
}
