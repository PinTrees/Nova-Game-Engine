#include "pch.h"
#include "SceneDimensions.h"
#include "SceneViewOverlay.h"
#include "SelectionManager.h"
#include "GameObject.h"
#include "Transform.h"
#include "MeshFilter.h"
#include "MeshRenderer.h"
#include "Mesh.h"

namespace
{
	// 메시 범위 (정점 8 꼭짓점 상자 → 월드). 정점을 다 돌지 않는다 — 메시 로컬 상자의 8 점만
	void Accumulate(GameObject* g, Vec3& mn, Vec3& mx, bool& any)
	{
		if (g == nullptr || !g->IsActiveInHierarchy())
			return;
		MeshFilter* mf = g->GetComponent<MeshFilter>();
		std::shared_ptr<Mesh> mesh = mf ? mf->GetMesh() : nullptr;
		if (!mesh)
			if (MeshRenderer* mr = g->GetComponent<MeshRenderer>())
				mesh = mr->GetMesh();
		if (mesh && !mesh->Vertices.empty())
		{
			Vec3 lo(FLT_MAX, FLT_MAX, FLT_MAX), hi(-FLT_MAX, -FLT_MAX, -FLT_MAX);
			for (const auto& v : mesh->Vertices)
			{
				lo = Vec3::Min(lo, Vec3(v.pos));
				hi = Vec3::Max(hi, Vec3(v.pos));
			}
			const Matrix w = g->GetTransform()->GetWorldMatrix();
			for (int c = 0; c < 8; ++c)
			{
				const Vec3 p((c & 1) ? hi.x : lo.x, (c & 2) ? hi.y : lo.y, (c & 4) ? hi.z : lo.z);
				const Vec3 q = Vec3::Transform(p, w);
				mn = Vec3::Min(mn, q);
				mx = Vec3::Max(mx, q);
			}
			any = true;
		}
		for (GameObject* c : g->Children())
			Accumulate(c, mn, mx, any);
	}

	void Label(const XMFLOAT3& a, const XMFLOAT3& b, float meters, ImU32 color)
	{
		SceneViewOverlay::DrawLine(a, b, color, 2.0f);
		ImVec2 pa, pb;
		if (!SceneViewOverlay::Project(a, pa) || !SceneViewOverlay::Project(b, pb))
			return;
		char t[32];
		snprintf(t, sizeof(t), meters >= 100.0f ? "%.0f m" : "%.2f m", meters);
		const ImVec2 mid((pa.x + pb.x) * 0.5f, (pa.y + pb.y) * 0.5f);
		const ImVec2 size = ImGui::CalcTextSize(t);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 p0(mid.x - size.x * 0.5f - 4, mid.y - size.y * 0.5f - 2), p1(mid.x + size.x * 0.5f + 4, mid.y + size.y * 0.5f + 2);
		dl->AddRectFilled(p0, p1, IM_COL32(20, 22, 26, 200), 3.0f);
		dl->AddText(ImVec2(p0.x + 4, p0.y + 2), color, t);
	}
}

namespace SceneDimensions
{
	bool SelectionBounds(Vec3& mn, Vec3& mx)
	{
		if (SelectionManager::GetSelectedObjectType() != SelectionType::GAMEOBJECT)
			return false;
		GameObject* go = SelectionManager::GetSelectedGameObject();
		if (go == nullptr)
			return false;
		mn = Vec3(FLT_MAX, FLT_MAX, FLT_MAX);
		mx = Vec3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
		bool any = false;
		Accumulate(go, mn, mx, any);
		return any;
	}

	void Draw()
	{
		if (!SceneViewOverlay::IsActive())
			return;
		Vec3 mn, mx;
		if (!SelectionBounds(mn, mx))
			return;
		const Vec3 s = mx - mn;
		if (s.x > 2000.0f || s.y > 2000.0f || s.z > 2000.0f)
			return;   // 지형처럼 아주 큰 것은 그리지 않는다
		// 바닥 앞 모서리 (X), 앞 왼쪽 세로 (Y), 바닥 왼쪽 (Z) — Unity 의 X 빨강 · Y 초록 · Z 파랑
		Label({ mn.x, mn.y, mn.z }, { mx.x, mn.y, mn.z }, s.x, IM_COL32(240, 110, 100, 255));
		Label({ mn.x, mn.y, mn.z }, { mn.x, mx.y, mn.z }, s.y, IM_COL32(140, 220, 110, 255));
		Label({ mn.x, mn.y, mn.z }, { mn.x, mn.y, mx.z }, s.z, IM_COL32(110, 160, 245, 255));
	}
}
