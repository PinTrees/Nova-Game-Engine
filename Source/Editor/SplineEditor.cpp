#include "pch.h"
#include "SplineEditor.h"
#include "Spline.h"
#include "SplinePointEditor.h"
#include "WaterBody.h"
#include "Transform.h"
#include "UnityGUI.h"
#include "SelectionManager.h"
#include "UndoSystem.h"

namespace
{
	SplinePointEditor::State s_State;

	struct Knots : SplinePointSource
	{
		SplineContainer& S;
		explicit Knots(SplineContainer& s) : S(s) {}
		int Count() const override { return (int)S.Knots.size(); }
		Vec3 LocalPos(int i) const override { return S.Knots[i]; }
		void SetLocalPos(int i, const Vec3& p) override { S.Knots[i] = p; }
		void Insert(int at, int, const Vec3& local) override { S.Knots.insert(S.Knots.begin() + at, local); }
		void Erase(int i) override { S.Knots.erase(S.Knots.begin() + i); }
		XMMATRIX World() const override { return S.WorldMatrix(); }
		bool Closed() const override { return S.Closed; }
		int MinPoints() const override { return 2; }
		Vec3 Place(const Vec3& hit, int) const override
		{
			Vec3 w = hit;
			float g;
			if (WaterBody::GroundAt(hit.x, hit.z, g))
				w.y = g;
			return w;
		}
		float FallbackPlaneY() const override { return S.Knots.empty() ? 0.0f : DisplayWorld(0).y; }
		void Changed() override { ++S.Revision; }
		const char* Noun() const override { return "Knot"; }
	};

	SplineContainer* Selected()
	{
		GameObject* go = SelectionManager::GetSelectedObjectType() == SelectionType::GAMEOBJECT ? SelectionManager::GetSelectedGameObject() : nullptr;
		SplineContainer* s = go ? go->GetComponent<SplineContainer>() : nullptr;
		SplinePointEditor::SetOwner(s_State, s);
		return s;
	}
}

namespace SplineEditor
{
	void InspectorKnots(SplineContainer& spline)
	{
		using namespace UnityGUI;
		Selected();
		SplinePointEditor::EditButton(s_State, (int)spline.Knots.size());
		if (s_State.Selected >= 0 && s_State.Selected < (int)spline.Knots.size())
		{
			char label[32];
			snprintf(label, sizeof(label), "Knot %d", s_State.Selected);
			Label(label, 1, true);
			if (UnityGUI::Vector3("Position", &spline.Knots[s_State.Selected].x, false, 1))
				++spline.Revision;
		}
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 18.0f);
		if (ImGui::Button("Snap Knots To Ground", ImVec2(170, 0)))
		{
			const XMMATRIX w = spline.WorldMatrix();
			const XMMATRIX inv = XMMatrixInverse(nullptr, w);
			for (Vec3& k : spline.Knots)
			{
				Vec3 p = XMVector3TransformCoord(k, w);
				float g;
				if (WaterBody::GroundAt(p.x, p.z, g))
				{
					p.y = g;
					k = XMVector3TransformCoord(p, inv);
				}
			}
			++spline.Revision;
			Undo::SetActionName("Snap Spline Knots");
			Undo::RequestCheck();
		}
		ImGui::SameLine();
		if (ImGui::Button("Reset Shape", ImVec2(110, 0)))
		{
			spline.ResetShape();
			s_State.Selected = -1;
			Undo::SetActionName("Reset Spline");
			Undo::RequestCheck();
		}
	}

	void DrawKnots(const SplineContainer& spline)
	{
		Knots src(const_cast<SplineContainer&>(spline));
		SplinePointEditor::State shown = s_State;
		if (shown.Owner != &spline)
			shown.Selected = -1;
		SplinePointEditor::DrawPoints(src, shown, IM_COL32(90, 200, 255, 255));
	}

	bool SceneGUI(EditorCamera* camera, const ImVec2& viewMin, const ImVec2& viewMax, bool viewHovered)
	{
		SplineContainer* s = Selected();
		if (s == nullptr)
		{
			s_State.Dragging = -1;
			return false;
		}
		Knots src(*s);
		return SplinePointEditor::SceneGUI(src, s_State, camera, viewMin, viewMax, viewHovered);
	}
}
