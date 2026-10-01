#include "pch.h"
#include "TerrainSplineEditor.h"
#include "TerrainSpline.h"
#include "SplinePointEditor.h"
#include "WaterBody.h"
#include "Terrain.h"
#include "Transform.h"
#include "UnityGUI.h"
#include "SelectionManager.h"
#include "UndoSystem.h"

namespace
{
	SplinePointEditor::State s_State;

	struct SplinePoints : SplinePointSource
	{
		TerrainSpline& S;
		explicit SplinePoints(TerrainSpline& s) : S(s) {}
		int Count() const override { return (int)S.Points.size(); }
		Vec3 LocalPos(int i) const override { return S.Points[i].Position; }
		void SetLocalPos(int i, const Vec3& p) override { S.Points[i].Position = p; }
		void Insert(int at, int copyFrom, const Vec3& local) override
		{
			TerrainSpline::Point p = copyFrom >= 0 ? S.Points[copyFrom] : TerrainSpline::Point();
			p.Position = local;
			S.Points.insert(S.Points.begin() + at, p);
		}
		void Erase(int i) override { S.Points.erase(S.Points.begin() + i); }
		XMMATRIX World() const override { return S.WorldMatrix(); }
		bool Closed() const override { return false; }
		int MinPoints() const override { return 2; }
		// 지면 높이 (도로가 깎기 전 땅 — 끌 때 자기가 깎은 길에 붙지 않게)
		Vec3 Place(const Vec3& hit, int) const override
		{
			Vec3 w = hit;
			float g;
			if (WaterBody::GroundAt(hit.x, hit.z, g))
				w.y = g;
			return w;
		}
		float FallbackPlaneY() const override { return S.Points.empty() ? 0.0f : DisplayWorld(0).y; }
		void Changed() override { ++S.Revision; }
		const char* Noun() const override { return "Spline Point"; }
	};

	TerrainSpline* Selected()
	{
		GameObject* go = SelectionManager::GetSelectedObjectType() == SelectionType::GAMEOBJECT ? SelectionManager::GetSelectedGameObject() : nullptr;
		TerrainSpline* s = go ? go->GetComponent<TerrainSpline>() : nullptr;
		SplinePointEditor::SetOwner(s_State, s);
		return s;
	}
}

namespace TerrainSplineEditor
{
	void InspectorPoints(TerrainSpline& spline)
	{
		using namespace UnityGUI;
		Selected();
		SplinePointEditor::EditButton(s_State, (int)spline.Points.size());
		if (s_State.Selected >= 0 && s_State.Selected < (int)spline.Points.size())
		{
			TerrainSpline::Point& p = spline.Points[s_State.Selected];
			char label[64];
			snprintf(label, sizeof(label), "Point %d", s_State.Selected);
			Label(label, 1, true);
			UnityGUI::Vector3("Position", &p.Position.x, false, 1);
			Slider("Width", &p.Width, 0.5f, 200.0f, 1);
		}
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 18.0f);
		if (ImGui::Button("Snap Points To Ground", ImVec2(180, 0)))
		{
			spline.SnapToGround();
			Undo::SetActionName("Snap Spline Points");
			Undo::RequestCheck();
		}
		ImGui::SameLine();
		if (ImGui::Button("Reset Shape", ImVec2(110, 0)))
		{
			spline.ResetShape();
			s_State.Selected = -1;
			Undo::SetActionName("Reset Spline Shape");
			Undo::RequestCheck();
		}
	}

	void DrawPoints(const TerrainSpline& spline)
	{
		SplinePoints src(const_cast<TerrainSpline&>(spline));
		SplinePointEditor::State shown = s_State;
		if (shown.Owner != &spline)
			shown.Selected = -1;
		SplinePointEditor::DrawPoints(src, shown, IM_COL32(255, 200, 90, 255));
	}

	bool SceneGUI(EditorCamera* camera, const ImVec2& viewMin, const ImVec2& viewMax, bool viewHovered)
	{
		TerrainSpline* s = Selected();
		if (s == nullptr)
		{
			s_State.Dragging = -1;
			return false;
		}
		SplinePoints src(*s);
		return SplinePointEditor::SceneGUI(src, s_State, camera, viewMin, viewMax, viewHovered);
	}
}
