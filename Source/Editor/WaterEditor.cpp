#include "pch.h"
#include "WaterEditor.h"
#include "WaterBody.h"
#include "SplinePointEditor.h"
#include "Transform.h"
#include "UnityGUI.h"
#include "SelectionManager.h"
#include "UndoSystem.h"

namespace
{
	SplinePointEditor::State s_State;

	// 물 바디 점 → 공용 점 편집기
	struct WaterPoints : SplinePointSource
	{
		WaterBody& B;
		explicit WaterPoints(WaterBody& b) : B(b) {}
		int Count() const override { return (int)B.Points.size(); }
		Vec3 LocalPos(int i) const override { return B.Points[i].Position; }
		void SetLocalPos(int i, const Vec3& p) override { B.Points[i].Position = p; }
		void Insert(int at, int copyFrom, const Vec3& local) override
		{
			WaterBody::Point p = copyFrom >= 0 ? B.Points[copyFrom] : WaterBody::Point();
			p.Position = local;
			B.Points.insert(B.Points.begin() + at, p);
		}
		void Erase(int i) override { B.Points.erase(B.Points.begin() + i); }
		XMMATRIX World() const override { return B.WorldMatrix(); }
		bool Closed() const override { return B.BodyType == WaterBody::Type::Lake; }
		int MinPoints() const override { return Closed() ? 3 : 2; }
		Vec3 DisplayWorld(int i) const override
		{
			Vec3 w = SplinePointSource::DisplayWorld(i);
			if (B.BodyType == WaterBody::Type::Lake)
				w.y = B.SurfaceY();
			return w;
		}
		// 강 = 땅(물로 파기 전) - 0.6 m, 호수 = 수면 높이
		Vec3 Place(const Vec3& hit, int) const override
		{
			Vec3 w = hit;
			float g;
			if (B.BodyType == WaterBody::Type::Lake)
				w.y = B.SurfaceY();
			else if (WaterBody::GroundAt(hit.x, hit.z, g))
				w.y = g - 0.6f;
			return w;
		}
		float FallbackPlaneY() const override
		{
			return B.BodyType == WaterBody::Type::Lake || B.Points.empty() ? B.SurfaceY() : DisplayWorld(0).y;
		}
		const char* EndLabel(int i) const override
		{
			if (B.BodyType != WaterBody::Type::River) return nullptr;
			return i == 0 ? "Source" : (i + 1 == Count() ? "Mouth" : nullptr);
		}
		bool KeepLocalY() const override { return B.BodyType == WaterBody::Type::Lake; }
		void Changed() override { ++B.Revision; }
		const char* Noun() const override { return "Water Point"; }
	};

	WaterBody* SelectedBody()
	{
		GameObject* go = SelectionManager::GetSelectedObjectType() == SelectionType::GAMEOBJECT ? SelectionManager::GetSelectedGameObject() : nullptr;
		WaterBody* b = go ? go->GetComponent<WaterBody>() : nullptr;
		SplinePointEditor::SetOwner(s_State, b);
		return b;
	}
}

namespace WaterEditor
{
	bool IsEditing() { return s_State.Edit; }

	void InspectorPoints(WaterBody& body)
	{
		using namespace UnityGUI;
		SelectedBody();
		const bool river = body.BodyType == WaterBody::Type::River;
		SplinePointEditor::EditButton(s_State, (int)body.Points.size());

		if (s_State.Selected >= 0 && s_State.Selected < (int)body.Points.size())
		{
			WaterBody::Point& p = body.Points[s_State.Selected];
			char label[64];
			snprintf(label, sizeof(label), "Point %d", s_State.Selected);
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
			s_State.Selected = -1;
			Undo::SetActionName("Reset Water Shape");
			Undo::RequestCheck();
		}
	}

	void DrawPoints(const WaterBody& body)
	{
		WaterPoints src(const_cast<WaterBody&>(body));
		SplinePointEditor::State shown = s_State;
		if (shown.Owner != &body)
			shown.Selected = -1;
		SplinePointEditor::DrawPoints(src, shown, IM_COL32(80, 190, 255, 255));
	}

	bool SceneGUI(EditorCamera* camera, const ImVec2& viewMin, const ImVec2& viewMax, bool viewHovered)
	{
		WaterBody* body = SelectedBody();
		if (body == nullptr || body->BodyType == WaterBody::Type::Ocean)
		{
			s_State.Dragging = -1;
			return false;
		}
		WaterPoints src(*body);
		return SplinePointEditor::SceneGUI(src, s_State, camera, viewMin, viewMax, viewHovered);
	}
}
