#include "pch.h"
#include "SceneViewOverlay.h"
#include "EditorCamera.h"

bool SceneViewOverlay::s_Active = false;
ImVec2 SceneViewOverlay::s_Min = ImVec2(0, 0);
ImVec2 SceneViewOverlay::s_Max = ImVec2(0, 0);
XMMATRIX SceneViewOverlay::s_ViewProj = XMMatrixIdentity();
XMFLOAT3 SceneViewOverlay::s_CameraPos = XMFLOAT3(0, 0, 0);

void SceneViewOverlay::Begin(const ImVec2& viewMin, const ImVec2& viewMax, EditorCamera* camera)
{
	s_Active = true;
	s_Min = viewMin;
	s_Max = viewMax;
	s_ViewProj = camera->View() * camera->Proj();
	s_CameraPos = camera->GetPosition();
}

void SceneViewOverlay::End()
{
	s_Active = false;
}

namespace
{
	ImU32 Lerp(ImU32 a, ImU32 b, float t)
	{
		ImVec4 ca = ImGui::ColorConvertU32ToFloat4(a);
		ImVec4 cb = ImGui::ColorConvertU32ToFloat4(b);
		return ImGui::ColorConvertFloat4ToU32(ImVec4(ca.x + (cb.x - ca.x) * t, ca.y + (cb.y - ca.y) * t, ca.z + (cb.z - ca.z) * t, 1.0f));
	}
}

void SceneViewOverlay::DrawBackground(const ImVec2& viewMin, const ImVec2& viewMax, EditorCamera* camera)
{
	// 카메라 수평 시선 방향의 아주 먼 점을 투영해 지평선 위치를 구한다.
	XMFLOAT3 look = camera->GetLook();
	XMFLOAT3 pos = camera->GetPosition();
	float len = sqrtf(look.x * look.x + look.z * look.z);
	float horizonY = (viewMin.y + viewMax.y) * 0.5f;

	XMMATRIX viewProj = camera->View() * camera->Proj();
	if (len > 1e-4f)
	{
		XMVECTOR p = XMVectorSet(pos.x + look.x / len * 10000.0f, 0.0f, pos.z + look.z / len * 10000.0f, 1.0f);
		XMVECTOR c = XMVector4Transform(p, viewProj);
		float w = XMVectorGetW(c);
		if (w > 1e-4f)
		{
			float ndcY = XMVectorGetY(c) / w;
			horizonY = viewMin.y + (0.5f - ndcY * 0.5f) * (viewMax.y - viewMin.y);
		}
	}
	horizonY = std::clamp(horizonY, viewMin.y, viewMax.y);

	const ImU32 skyTop     = IM_COL32(88, 110, 142, 255);
	const ImU32 skyHorizon = IM_COL32(182, 196, 210, 255);
	const ImU32 groundNear = IM_COL32(112, 118, 124, 255);
	const ImU32 groundFar  = IM_COL32(170, 178, 186, 255);

	ImDrawList* dl = ImGui::GetWindowDrawList();
	if (horizonY > viewMin.y)
		dl->AddRectFilledMultiColor(viewMin, ImVec2(viewMax.x, horizonY), skyTop, skyTop, skyHorizon, skyHorizon);
	if (horizonY < viewMax.y)
		dl->AddRectFilledMultiColor(ImVec2(viewMin.x, horizonY), viewMax, groundFar, groundFar, groundNear, groundNear);
	(void)Lerp;
}

bool SceneViewOverlay::Project(const XMFLOAT3& world, ImVec2& out)
{
	XMVECTOR c = XMVector4Transform(XMVectorSet(world.x, world.y, world.z, 1.0f), s_ViewProj);
	float w = XMVectorGetW(c);
	if (w <= 1e-4f)
		return false;
	float x = XMVectorGetX(c) / w;
	float y = XMVectorGetY(c) / w;
	out = ImVec2(s_Min.x + (x * 0.5f + 0.5f) * (s_Max.x - s_Min.x), s_Min.y + (0.5f - y * 0.5f) * (s_Max.y - s_Min.y));
	return true;
}

void SceneViewOverlay::DrawLine(const XMFLOAT3& a, const XMFLOAT3& b, ImU32 color, float thickness)
{
	// 클립 공간에서 z >= 0(가까운 평면) 으로 선분 자르기
	XMVECTOR ca = XMVector4Transform(XMVectorSet(a.x, a.y, a.z, 1.0f), s_ViewProj);
	XMVECTOR cb = XMVector4Transform(XMVectorSet(b.x, b.y, b.z, 1.0f), s_ViewProj);
	float za = XMVectorGetZ(ca), zb = XMVectorGetZ(cb);
	if (za < 0.0f && zb < 0.0f)
		return;
	if (za < 0.0f)
		ca = XMVectorLerp(ca, cb, za / (za - zb));
	else if (zb < 0.0f)
		cb = XMVectorLerp(cb, ca, zb / (zb - za));

	float wa = XMVectorGetW(ca), wb = XMVectorGetW(cb);
	if (wa <= 1e-5f || wb <= 1e-5f)
		return;

	auto toScreen = [&](XMVECTOR c, float w)
	{
		float x = XMVectorGetX(c) / w, y = XMVectorGetY(c) / w;
		return ImVec2(s_Min.x + (x * 0.5f + 0.5f) * (s_Max.x - s_Min.x), s_Min.y + (0.5f - y * 0.5f) * (s_Max.y - s_Min.y));
	};

	ImDrawList* dl = ImGui::GetWindowDrawList();
	dl->PushClipRect(s_Min, s_Max, true);
	dl->AddLine(toScreen(ca, wa), toScreen(cb, wb), color, thickness);
	dl->PopClipRect();
}

void SceneViewOverlay::DrawGrid()
{
	const float extent = 60.0f;       // 카메라 주변 ±60 유닛
	const float fadeDist = 70.0f;
	const float cx = floorf(s_CameraPos.x);
	const float cz = floorf(s_CameraPos.z);

	auto fadeAlpha = [&](float x, float z, float base)
	{
		float dx = x - s_CameraPos.x, dz = z - s_CameraPos.z;
		float d = sqrtf(dx * dx + dz * dz);
		return std::clamp(1.0f - d / fadeDist, 0.0f, 1.0f) * base;
	};

	// 1 유닛 간격선 (세그먼트를 잘게 나눠 거리별 페이드 근사)
	const int segments = 6;
	for (int i = -(int)extent; i <= (int)extent; ++i)
	{
		float x = cx + i;
		float z = cz + i;
		bool majorX = (((int)x) % 10 == 0);
		bool majorZ = (((int)z) % 10 == 0);

		for (int s = 0; s < segments; ++s)
		{
			float t0 = -extent + (2.0f * extent) * s / segments;
			float t1 = -extent + (2.0f * extent) * (s + 1) / segments;

			// x 고정, z 방향 선
			float zmid = cz + (t0 + t1) * 0.5f;
			float a1 = fadeAlpha(x, zmid, majorX ? 0.85f : 0.55f);
			if (a1 > 0.02f)
				DrawLine(XMFLOAT3(x, 0, cz + t0), XMFLOAT3(x, 0, cz + t1), IM_COL32(92, 98, 106, (int)(a1 * 255)));

			// z 고정, x 방향 선
			float xmid = cx + (t0 + t1) * 0.5f;
			float a2 = fadeAlpha(xmid, z, majorZ ? 0.85f : 0.55f);
			if (a2 > 0.02f)
				DrawLine(XMFLOAT3(cx + t0, 0, z), XMFLOAT3(cx + t1, 0, z), IM_COL32(92, 98, 106, (int)(a2 * 255)));
		}
	}
}

void SceneViewOverlay::DrawFrustum(const XMMATRIX& worldMatrix, float nearZ, float farZ, float fovYDegrees, ImU32 color)
{
	(void)nearZ;
	const float aspect = 16.0f / 9.0f;
	const float t = tanf(XMConvertToRadians(fovYDegrees) * 0.5f);
	const float h = farZ * t, w = h * aspect;

	XMFLOAT3 apex;
	XMStoreFloat3(&apex, XMVector3TransformCoord(XMVectorZero(), worldMatrix));

	XMFLOAT3 local[4] = { { -w, h, farZ }, { w, h, farZ }, { w, -h, farZ }, { -w, -h, farZ } };
	XMFLOAT3 f[4];
	for (int i = 0; i < 4; ++i)
		XMStoreFloat3(&f[i], XMVector3TransformCoord(XMLoadFloat3(&local[i]), worldMatrix));

	for (int i = 0; i < 4; ++i)
	{
		DrawLine(f[i], f[(i + 1) % 4], color, 1.0f);
		DrawLine(apex, f[i], color, 1.0f);
	}
}

void SceneViewOverlay::DrawLightGizmo(const XMFLOAT3& p, const XMFLOAT3& dir, int kind, bool selected)
{
	// 화면에서 일정한 크기가 되도록 카메라 거리에 비례한 월드 크기 (Unity HandleUtility.GetHandleSize 와 같은 생각)
	const float dist = sqrtf((p.x - s_CameraPos.x) * (p.x - s_CameraPos.x) + (p.y - s_CameraPos.y) * (p.y - s_CameraPos.y) + (p.z - s_CameraPos.z) * (p.z - s_CameraPos.z));
	const float handle = (std::max)(0.05f, dist * 0.12f);
	const ImU32 lineColor = selected ? IM_COL32(255, 247, 140, 255) : IM_COL32(255, 238, 120, 190);
	const float thickness = selected ? 2.2f : 1.6f;

	XMVECTOR d = XMVectorSet(dir.x, dir.y, dir.z, 0.0f);
	if (XMVectorGetX(XMVector3LengthSq(d)) < 1e-8f)
		d = XMVectorSet(0, -1, 0, 0);
	d = XMVector3Normalize(d);
	const XMVECTOR up = fabsf(XMVectorGetY(d)) > 0.95f ? XMVectorSet(1, 0, 0, 0) : XMVectorSet(0, 1, 0, 0);
	const XMVECTOR side = XMVector3Normalize(XMVector3Cross(up, d));
	const XMVECTOR side2 = XMVector3Cross(d, side);
	const XMVECTOR center = XMVectorSet(p.x, p.y, p.z, 1.0f);
	auto F3 = [](XMVECTOR v) { XMFLOAT3 f; XMStoreFloat3(&f, v); return f; };

	if (kind != 1)   // Point 는 방향이 없다
	{
		// 방향 표시: 빛 방향에 수직인 원 + 원 둘레에서 빛 방향으로 뻗는 평행선 8개 + 가운데 화살표 (Unity Directional Light 기즈모)
		const float radius = handle * 0.35f;
		const float length = handle * (kind == 0 ? 1.6f : 1.2f);
		const int segments = 32;
		XMFLOAT3 prev = F3(XMVectorAdd(center, XMVectorScale(side, radius)));
		for (int k = 1; k <= segments; ++k)
		{
			const float a = XM_2PI * k / segments;
			XMFLOAT3 cur = F3(XMVectorAdd(center, XMVectorAdd(XMVectorScale(side, cosf(a) * radius), XMVectorScale(side2, sinf(a) * radius))));
			DrawLine(prev, cur, lineColor, thickness);
			prev = cur;
		}
		for (int k = 0; k < 8; ++k)
		{
			const float a = XM_2PI * k / 8;
			XMVECTOR start = XMVectorAdd(center, XMVectorAdd(XMVectorScale(side, cosf(a) * radius), XMVectorScale(side2, sinf(a) * radius)));
			DrawLine(F3(start), F3(XMVectorAdd(start, XMVectorScale(d, length))), lineColor, thickness);
		}
		const XMVECTOR tip = XMVectorAdd(center, XMVectorScale(d, length * 1.25f));
		DrawLine(F3(center), F3(tip), lineColor, thickness);
		const XMVECTOR back = XMVectorSubtract(tip, XMVectorScale(d, handle * 0.25f));
		for (int k = 0; k < 4; ++k)
		{
			const float a = XM_PIDIV2 * k;
			XMVECTOR wing = XMVectorAdd(back, XMVectorAdd(XMVectorScale(side, cosf(a) * handle * 0.1f), XMVectorScale(side2, sinf(a) * handle * 0.1f)));
			DrawLine(F3(tip), F3(wing), lineColor, thickness);
		}
	}

	// 화면 고정 크기 아이콘 (해 모양) - 밝은 하늘 위에서도 보이도록 어두운 테두리
	ImVec2 c;
	if (!Project(p, c))
		return;
	ImDrawList* dl = ImGui::GetWindowDrawList();
	dl->PushClipRect(s_Min, s_Max, true);
	const float r = 7.0f;
	const ImU32 fill = IM_COL32(255, 214, 64, 255), outline = IM_COL32(40, 32, 10, 200);
	if (selected)
		dl->AddCircleFilled(c, 19.0f, IM_COL32(255, 230, 120, 45));
	for (int k = 0; k < 8; ++k)
	{
		const float a = XM_2PI * k / 8;
		const ImVec2 a0(c.x + cosf(a) * (r + 3.5f), c.y + sinf(a) * (r + 3.5f)), a1(c.x + cosf(a) * (r + 9.0f), c.y + sinf(a) * (r + 9.0f));
		dl->AddLine(a0, a1, outline, 4.5f);
		dl->AddLine(a0, a1, fill, 2.5f);
	}
	dl->AddCircleFilled(c, r + 1.5f, outline);
	dl->AddCircleFilled(c, r, fill);
	if (kind == 1)   // Point: 가운데 점
		dl->AddCircleFilled(c, 2.5f, outline);
	dl->PopClipRect();
}
