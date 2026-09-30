#include "pch.h"
#include "Gizmo.h"
#include "Effects.h"
#include "MathHelper.h"
#include "SceneEditorWindow.h"
#include "EditorCamera.h"

bool Gizmo::isDragging = false;
ImVec2 Gizmo::lastMousePos = ImVec2(0, 0);
int Gizmo::activeAxis = 0;

void Gizmo::DrawVector(const Matrix worldMatrix, const Vec3 vector)
{
    auto context = Application::GetI()->GetDeviceContext();

    // 카메라의 뷰 프로젝션 행렬 가져오기
    Matrix viewProj = RenderManager::GetI()->EditorCameraViewProjectionMatrix;
    Matrix worldViewProj = worldMatrix * viewProj;

    // 뷰포트 크기 가져오기
    D3D11_VIEWPORT viewport;
    UINT numViewports = 1;
    context->RSGetViewports(&numViewports, &viewport);

    // 카메라의 위치를 가져오기
    EditorCamera* sceneViewCamera = SceneViewManager::GetI()->m_LastActiveSceneEditorWindow->GetSceneCamera();
    Vec3 cameraPosition = sceneViewCamera->GetPosition();
    Vec3 position = Vec3(worldMatrix._41, worldMatrix._42, worldMatrix._43);

    // 카메라와 오브젝트 간의 거리 계산
    float distance = (position - cameraPosition).Length();
    distance = max(distance, 0.1f); // 최소 거리 제한

    // 거리 기반 스케일링 팩터 계산
    float scale = distance / 7.0f;

    // 현재 창의 위치와 크기를 가져옴
    ImVec2 windowPos = ImGui::GetWindowPos();
    ImVec2 contentRegionMin = ImGui::GetWindowContentRegionMin();
    ImVec2 contentRegionMax = ImGui::GetWindowContentRegionMax();
    ImVec2 offset = ImVec2(contentRegionMin.x + windowPos.x, contentRegionMin.y + windowPos.y);

    auto WorldToScreen = [&](const Vec3& worldPos) -> ImVec2
        {
            Vec3 pos = Vector3::Transform(worldPos, worldViewProj);
            float x = (pos.x / pos.z) * 0.5f + 0.5f;
            float y = (pos.y / pos.z) * -0.5f + 0.5f;
            return ImVec2(x * viewport.Width + viewport.TopLeftX + offset.x, y * viewport.Height + viewport.TopLeftY + offset.y);
        };

    const ImU32 color = IM_COL32(255, 0, 0, 255);
    const float thickness = 2.0f;

    // 시작점 및 끝점 계산
    Vec3 start = Vector3::Transform(Vec3::Zero, worldMatrix);
    Vec3 end = Vector3::Transform(vector * scale, worldMatrix);

    // 화면 좌표로 변환
    ImVec2 startScreen = WorldToScreen(start);
    ImVec2 endScreen = WorldToScreen(end);

    // 벡터 그리기
    ImGui::GetWindowDrawList()->AddLine(startScreen, endScreen, color, thickness);
}

void Gizmo::DrawCube(const XMMATRIX& worldMatrix, const Vec3& size)
{
    auto context = Application::GetI()->GetDeviceContext();

    // 박스의 8개의 꼭짓점 정의
    XMFLOAT3 halfSize = { size.x * 0.5f, size.y * 0.5f, size.z * 0.5f };
    XMFLOAT3 vertices[8] = {
        { -halfSize.x, -halfSize.y, -halfSize.z },
        { -halfSize.x,  halfSize.y, -halfSize.z },
        {  halfSize.x,  halfSize.y, -halfSize.z },
        {  halfSize.x, -halfSize.y, -halfSize.z },
        { -halfSize.x, -halfSize.y,  halfSize.z },
        { -halfSize.x,  halfSize.y,  halfSize.z },
        {  halfSize.x,  halfSize.y,  halfSize.z },
        {  halfSize.x, -halfSize.y,  halfSize.z }
    };

    // 카메라의 뷰 프로젝션 행렬 가져오기
    XMMATRIX viewProj = RenderManager::GetI()->EditorCameraViewProjectionMatrix;
    XMMATRIX worldViewProj = worldMatrix * viewProj;

    // 뷰포트 크기 가져오기
    D3D11_VIEWPORT viewport;
    UINT numViewports = 1;
    context->RSGetViewports(&numViewports, &viewport);

    // 현재 창의 위치와 크기를 가져옴
    ImVec2 windowPos = ImGui::GetWindowPos();
    ImVec2 contentRegionMin = ImGui::GetWindowContentRegionMin();
    ImVec2 contentRegionMax = ImGui::GetWindowContentRegionMax();
    ImVec2 offset = ImVec2(contentRegionMin.x + windowPos.x, contentRegionMin.y + windowPos.y);

    // 꼭짓점을 스크린 좌표로 변환
    ImVec2 screenVertices[8];
    for (int i = 0; i < 8; ++i)
    {
        XMVECTOR pos = XMVector3TransformCoord(XMLoadFloat3(&vertices[i]), worldViewProj);

        // NDC to screen space conversion
        float x = (XMVectorGetX(pos) / XMVectorGetW(pos)) * 0.5f + 0.5f;
        float y = (XMVectorGetY(pos) / XMVectorGetW(pos)) * -0.5f + 0.5f;
        screenVertices[i] = ImVec2(x * (contentRegionMax.x - contentRegionMin.x) + offset.x,
            y * (contentRegionMax.y - contentRegionMin.y) + offset.y);
    }

    // ImGui를 사용하여 박스의 엣지를 그림
    const ImU32 color = IM_COL32(0, 255, 0, 255);
    const float thickness = 1.0f;

    // 박스의 12개의 엣지 그리기
    const int edges[12][2] = {
        {0, 1}, {1, 2}, {2, 3}, {3, 0},
        {4, 5}, {5, 6}, {6, 7}, {7, 4},
        {0, 4}, {1, 5}, {2, 6}, {3, 7}
    };

    for (int i = 0; i < 12; ++i)
    {
        ImGui::GetWindowDrawList()->AddLine(screenVertices[edges[i][0]], screenVertices[edges[i][1]], color, thickness);
    }
}

// Fixed
void Gizmo::DrawSphere(const XMMATRIX& worldMatrix, float radius)
{
    auto context = Application::GetI()->GetDeviceContext();

    const int circleSegments = 25;  // 원을 구성하는 세그먼트 수

    std::vector<XMFLOAT3> vertices;
    vertices.reserve(circleSegments + 1);

    // 카메라의 뷰 프로젝션 행렬 가져오기
    XMMATRIX viewProj = RenderManager::GetI()->EditorCameraViewProjectionMatrix;
    XMMATRIX worldViewProj = worldMatrix * viewProj;

    // 뷰포트 크기 가져오기
    D3D11_VIEWPORT viewport;
    UINT numViewports = 1;
    context->RSGetViewports(&numViewports, &viewport);

    // 현재 창의 위치와 크기를 가져옴
    ImVec2 windowPos = ImGui::GetWindowPos();
    ImVec2 contentRegionMin = ImGui::GetWindowContentRegionMin();
    ImVec2 contentRegionMax = ImGui::GetWindowContentRegionMax();
    ImVec2 offset = ImVec2(contentRegionMin.x + windowPos.x, contentRegionMin.y + windowPos.y);

    auto WorldToScreen = [&](const XMFLOAT3& worldPos) -> ImVec2
        {
            XMVECTOR pos = XMVector3TransformCoord(XMLoadFloat3(&worldPos), worldViewProj);
            float x = (XMVectorGetX(pos) / XMVectorGetW(pos)) * 0.5f + 0.5f;
            float y = (XMVectorGetY(pos) / XMVectorGetW(pos)) * -0.5f + 0.5f;
            return ImVec2(x * (contentRegionMax.x - contentRegionMin.x) + offset.x, y * (contentRegionMax.y - contentRegionMin.y) + offset.y);
        };

    const ImU32 color = IM_COL32(0, 255, 0, 255);
    const float thickness = 1.0f;

    auto DrawCircle = [&](const XMFLOAT3& center, const XMFLOAT3& up, const XMFLOAT3& right)
        {
            vertices.clear();
            for (int i = 0; i <= circleSegments; ++i)
            {
                float theta = i * XM_2PI / circleSegments;
                XMFLOAT3 pos;
                pos.x = center.x + radius * (right.x * cosf(theta) + up.x * sinf(theta));
                pos.y = center.y + radius * (right.y * cosf(theta) + up.y * sinf(theta));
                pos.z = center.z + radius * (right.z * cosf(theta) + up.z * sinf(theta));
                vertices.push_back(pos);
            }

            for (int i = 0; i < circleSegments; ++i)
            {
                ImGui::GetWindowDrawList()->AddLine(WorldToScreen(vertices[i]), WorldToScreen(vertices[i + 1]), color, thickness);
            }
        };

    // 각 축에 대해 원을 그림
    XMFLOAT3 center(0.0f, 0.0f, 0.0f);

    // 단위 벡터를 사용하여 회전만 적용
    XMMATRIX rotationMatrix = XMMatrixRotationQuaternion(XMQuaternionRotationMatrix(worldMatrix));

    XMVECTOR right = XMVector3TransformNormal(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), rotationMatrix); 
    XMVECTOR up = XMVector3TransformNormal(XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), rotationMatrix); 
    XMVECTOR forward = XMVector3TransformNormal(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), rotationMatrix); 

    XMFLOAT3 rightF, upF, forwardF;
    XMStoreFloat3(&rightF, right);
    XMStoreFloat3(&upF, up);
    XMStoreFloat3(&forwardF, forward);

    DrawCircle(center, upF, rightF);  // XY 평면 
    DrawCircle(center, forwardF, rightF);  // XZ 평면 
    DrawCircle(center, upF, forwardF);  // YZ 평면 
     
    //EditorCamera* sceneViewCamera = SceneViewManager::GetI()->m_LastActiveSceneEditorWindow->GetSceneCamera();
    //XMMATRIX camWorldMatrix = sceneViewCamera->GetWorldMatrix();  
    //
    //XMFLOAT3 camRight = sceneViewCamera->GetRight();
    //XMFLOAT3 camUp = sceneViewCamera->GetUp();
    //XMFLOAT3 camRightF, camUpF;
    //
    //XMVECTOR camRightV = XMVector3TransformNormal(XMVectorSet(camRight.x, camRight.y, camRight.z, 0.0f), worldMatrix); 
    //XMVECTOR camUpV = XMVector3TransformNormal(XMVectorSet(camUp.x, camUp.y, camUp.z, 0.0f), worldMatrix);
    //
    //XMStoreFloat3(&camRightF, camRightV);
    //XMStoreFloat3(&camUpF, camUpV);
    //
    //DrawCircle(center, camUpF, camRightF);  // XY 평면 
}

void Gizmo::DrawArrow(Vector3 position, Vec3 dir, ImVec4 color) 
{
    EditorCamera* sceneViewCamera = SceneViewManager::GetI()->m_LastActiveSceneEditorWindow->GetSceneCamera();

    // World Matrix 가져오기
    Matrix worldMatrix = Matrix::CreateTranslation(position);

    auto context = Application::GetI()->GetDeviceContext();
    Matrix viewProj = RenderManager::GetI()->EditorCameraViewProjectionMatrix;

    // 위치를 추출
    Vector3 pos = position;

    // 카메라의 위치를 가져오기
    Vector3 cameraPosition = sceneViewCamera->GetPosition();

    // 카메라와 오브젝트 간의 거리 계산
    float distance = Vector3::Distance(pos, cameraPosition);
    distance = max(distance, 0.1f); // 최소 거리 제한

    // 거리 기반 스케일링 팩터 계산
    float scale = distance / 10;

    // 3D에서 2D로 투영
    D3D11_VIEWPORT viewport;
    UINT numViewports = 1;
    context->RSGetViewports(&numViewports, &viewport);

    // 현재 창의 위치와 크기를 가져옴
    ImVec2 windowPos = ImGui::GetWindowPos();
    ImVec2 contentRegionMin = ImGui::GetWindowContentRegionMin();
    ImVec2 contentRegionMax = ImGui::GetWindowContentRegionMax();
    ImVec2 offset = ImVec2(contentRegionMin.x + windowPos.x, contentRegionMin.y + windowPos.y);

    auto WorldToScreen = [&](const Vector3& worldPos) -> Vec2
        {
            DirectX::XMFLOAT4 projectedPos; 
            DirectX::XMStoreFloat4(&projectedPos, DirectX::XMVector3TransformCoord(worldPos, viewProj)); 

            if (projectedPos.w <= 0.0f)
            {
                return Vec2(-1000.0f, -1000.0f);
            }

            Vec2 screenPos;
            screenPos.x = (projectedPos.x / projectedPos.w * 0.5f + 0.5f) * (contentRegionMax.x - contentRegionMin.x) + offset.x;
            screenPos.y = (1.0f - (projectedPos.y / projectedPos.w * 0.5f + 0.5f)) * (contentRegionMax.y - contentRegionMin.y) + offset.y;
            return screenPos;
        };

    Vec2 handlePos = WorldToScreen(pos);
    Vec2 endPos = WorldToScreen(pos + dir * scale); 

    // 핸들 그리기
    DrawwPoinVector(handlePos, endPos, color);
}

void Gizmo::DrawFrustum(const XMMATRIX& worldMatrix, float _near, float _far, float fieldOfView)
{
    auto context = Application::GetI()->GetDeviceContext();

    // 카메라의 종횡비 (가로 세로 비율) 가져오기 - 예제에서는 16:9로 고정
    float aspectRatio = 16.0f / 9.0f;

    // 시야각을 라디안으로 변환하고 tangent 값 계산
    float tanFov = tanf(XMConvertToRadians(fieldOfView) * 0.5f);

    // 근평면과 원평면의 반높이 및 반너비 계산
    float nearHeight = _near * tanFov;
    float nearWidth = nearHeight * aspectRatio;
    float farHeight = _far * tanFov;
    float farWidth = farHeight * aspectRatio;

    // 근평면과 원평면의 꼭짓점 정의
    XMFLOAT3 nearVertices[4] = {
        { -nearWidth, nearHeight, _near },  // 좌상
        { nearWidth, nearHeight, _near },   // 우상
        { nearWidth, -nearHeight, _near },  // 우하
        { -nearWidth, -nearHeight, _near }  // 좌하
    };

    XMFLOAT3 farVertices[4] = {
        { -farWidth, farHeight, _far },  // 좌상
        { farWidth, farHeight, _far },   // 우상
        { farWidth, -farHeight, _far },  // 우하
        { -farWidth, -farHeight, _far }  // 좌하
    };

    // 카메라의 뷰 프로젝션 행렬 가져오기
    XMMATRIX viewProj = RenderManager::GetI()->EditorCameraViewProjectionMatrix;
    XMMATRIX worldViewProj = worldMatrix * viewProj;

    // 뷰포트 크기 가져오기
    D3D11_VIEWPORT viewport;
    UINT numViewports = 1;
    context->RSGetViewports(&numViewports, &viewport);

    // 현재 창의 위치와 크기를 가져옴
    ImVec2 windowPos = ImGui::GetWindowPos();
    ImVec2 contentRegionMin = ImGui::GetWindowContentRegionMin();
    ImVec2 contentRegionMax = ImGui::GetWindowContentRegionMax();
    ImVec2 offset = ImVec2(contentRegionMin.x + windowPos.x, contentRegionMin.y + windowPos.y);

    // 근평면과 원평면의 꼭짓점을 스크린 좌표로 변환
    ImVec2 screenNearVertices[4];
    ImVec2 screenFarVertices[4];
    for (int i = 0; i < 4; ++i)
    {
        XMVECTOR nearPos = XMVector3TransformCoord(XMLoadFloat3(&nearVertices[i]), worldViewProj);
        XMVECTOR farPos = XMVector3TransformCoord(XMLoadFloat3(&farVertices[i]), worldViewProj);

        // NDC to screen space conversion
        float nearX = (XMVectorGetX(nearPos) / XMVectorGetW(nearPos)) * 0.5f + 0.5f;
        float nearY = (XMVectorGetY(nearPos) / XMVectorGetW(nearPos)) * -0.5f + 0.5f;
        screenNearVertices[i] = ImVec2(nearX * (contentRegionMax.x - contentRegionMin.x) + offset.x,
            nearY * (contentRegionMax.y - contentRegionMin.y) + offset.y);

        float farX = (XMVectorGetX(farPos) / XMVectorGetW(farPos)) * 0.5f + 0.5f;
        float farY = (XMVectorGetY(farPos) / XMVectorGetW(farPos)) * -0.5f + 0.5f;
        screenFarVertices[i] = ImVec2(farX * (contentRegionMax.x - contentRegionMin.x) + offset.x,
            farY * (contentRegionMax.y - contentRegionMin.y) + offset.y);
    }

    // ImGui를 사용하여 프러스텀의 엣지를 그림
    const ImU32 color = IM_COL32(255, 255, 255, 255); // 하얀색
    const float thickness = 1.0f;

    // 근평면 그리기
    for (int i = 0; i < 4; ++i)
    {
        int next = (i + 1) % 4;
        ImGui::GetWindowDrawList()->AddLine(screenNearVertices[i], screenNearVertices[next], color, thickness);
    }

    // 원평면 그리기
    for (int i = 0; i < 4; ++i)
    {
        int next = (i + 1) % 4;
        ImGui::GetWindowDrawList()->AddLine(screenFarVertices[i], screenFarVertices[next], color, thickness);
    }

    // 근평면과 원평면을 연결하는 엣지 그리기
    for (int i = 0; i < 4; ++i)
    {
        ImGui::GetWindowDrawList()->AddLine(screenNearVertices[i], screenFarVertices[i], color, thickness);
    }
}

void Gizmo::DrawTransformHandler(Transform* transform)
{
    EditorCamera* sceneViewCamera = SceneViewManager::GetI()->m_LastActiveSceneEditorWindow->GetSceneCamera();

    XMMATRIX worldMatrix = transform->GetWorldMatrix();

    auto context = Application::GetI()->GetDeviceContext();
    XMMATRIX viewProj = RenderManager::GetI()->EditorCameraViewProjectionMatrix;

    // 위치를 추출
    XMVECTOR position = transform->GetPosition();

    // 카메라의 위치를 가져오기
    XMFLOAT3 cameraPositionFloat3 = sceneViewCamera->GetPosition();
    XMVECTOR cameraPosition = XMLoadFloat3(&cameraPositionFloat3);

    // 카메라와 오브젝트 간의 거리 계산
    float distance = XMVectorGetX(XMVector3Length(position - cameraPosition));
    distance = max(distance, 0.1f); // 최소 거리 제한

    // 거리 기반 스케일링 팩터 계산
    float scale = distance / 7;

    // 각 축의 방향 벡터를 정의
    XMVECTOR xAxis = XMVector3Normalize(XMVector3TransformNormal(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), worldMatrix)) * scale;
    XMVECTOR yAxis = XMVector3Normalize(XMVector3TransformNormal(XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), worldMatrix)) * scale;
    XMVECTOR zAxis = XMVector3Normalize(XMVector3TransformNormal(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), worldMatrix)) * scale;

    // 3D에서 2D로 투영
    D3D11_VIEWPORT viewport;
    UINT numViewports = 1;
    context->RSGetViewports(&numViewports, &viewport);

    // 현재 창의 위치와 크기를 가져옴
    ImVec2 windowPos = ImGui::GetWindowPos();
    ImVec2 contentRegionMin = ImGui::GetWindowContentRegionMin();
    ImVec2 contentRegionMax = ImGui::GetWindowContentRegionMax();
    ImVec2 offset = ImVec2(contentRegionMin.x + windowPos.x, contentRegionMin.y + windowPos.y);

    auto WorldToScreen = [&](const XMVECTOR& worldPos) -> ImVec2
    {
            DirectX::XMFLOAT4 projectedPos;
            DirectX::XMStoreFloat4(&projectedPos, DirectX::XMVector3TransformCoord(worldPos, viewProj));

            // Z-분할이 0보다 작은 경우를 처리
            if (projectedPos.w <= 0.0f)
            {
                return ImVec2(-1000.0f, -1000.0f);
            }

            ImVec2 screenPos;
            screenPos.x = (projectedPos.x / projectedPos.w * 0.5f + 0.5f) * (contentRegionMax.x - contentRegionMin.x) + offset.x;
            screenPos.y = (1.0f - (projectedPos.y / projectedPos.w * 0.5f + 0.5f)) * (contentRegionMax.y - contentRegionMin.y) + offset.y;
            return screenPos;
    };

    ImVec2 handlePos = WorldToScreen(position);
    ImVec2 xEnd = WorldToScreen(position + xAxis);
    ImVec2 yEnd = WorldToScreen(position + yAxis);
    ImVec2 zEnd = WorldToScreen(position + zAxis);

    // 핸들 그리기
    ImVec4 xColor = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
    ImVec4 yColor = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
    ImVec4 zColor = ImVec4(0.0f, 0.0f, 1.0f, 1.0f);

    DrawAxisHandle(handlePos, xEnd, xColor);
    DrawAxisHandle(handlePos, yEnd, yColor);
    DrawAxisHandle(handlePos, zEnd, zColor);

    // 마우스 입력 처리 및 변환 적용
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        if (IsMouseOverHandle(handlePos))
        {
            isDragging = true;
            lastMousePos = ImGui::GetMousePos();
        }
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        isDragging = false;
    }

    if (isDragging)
    {
        //ImVec2 mousePos = ImGui::GetMousePos();
        //ImVec2 delta = ImVec2(mousePos.x - lastMousePos.x, mousePos.y - lastMousePos.y);
        //
        //switch (activeAxis)
        //{
        //case 1: // X axis
        //    transformMatrix._41 += delta.x * 0.01f;
        //    break;
        //case 2: // Y axis
        //    transformMatrix._42 += delta.y * 0.01f;
        //    break;
        //case 3: // Z axis
        //    transformMatrix._43 += delta.x * 0.01f; // 단순 예제이므로 Z축도 X좌표에 대해 변경
        //    break;
        //}
        //
        //lastMousePos = mousePos;

        //transform = DirectX::XMLoadFloat4x4(&transformMatrix);
    }
}

bool Gizmo::IsMouseOverHandle(const ImVec2& handlePos)
{
    ImVec2 mousePos = ImGui::GetMousePos();
    return (mousePos.x >= handlePos.x && mousePos.x <= handlePos.x + 10 &&
        mousePos.y >= handlePos.y && mousePos.y <= handlePos.y + 10);
}

void Gizmo::DrawAxisHandle(const ImVec2& pos, const ImVec2& dir, const ImVec4& color)
{
    ImGui::GetWindowDrawList()->AddLine(pos, dir, ImGui::GetColorU32(color), 2.0f);
    ImGui::GetWindowDrawList()->AddCircleFilled(dir, 5.0f, ImGui::GetColorU32(color));
}

void Gizmo::DrawwPoinVector(const Vec2& pos, const Vec2& dir, const ImVec4& color)
{
    ImGui::GetWindowDrawList()->AddLine(ImVec2(pos.x, pos.y), ImVec2(dir.x, dir.y), ImGui::GetColorU32(color), 1.0f);
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(pos.x, pos.y), 3.0f, ImGui::GetColorU32(color));

    // 방향 벡터 계산
    Vec2 direction = dir - pos;
    float length = sqrtf(direction.x * direction.y + direction.y * direction.y); 

    // 삼각형의 다른 두 점을 계산
    Vec2 directionNorm = direction / length;
    Vec2 orthogonal(-directionNorm.y, directionNorm.x); // orthogonal 벡터 

    float arrowSize = 10.0f; // 삼각형의 크기를 조정하기 위한 값 
    Vec2 p1 = dir;
    Vec2 p2 = dir - directionNorm * arrowSize + orthogonal * arrowSize * 0.5f;  
    Vec2 p3 = dir - directionNorm * arrowSize - orthogonal * arrowSize * 0.5f;  

    // 삼각형 그리기
    ImGui::GetWindowDrawList()->AddTriangleFilled(
        ImVec2(p1.x, p1.y), 
        ImVec2(p2.x, p2.y), 
        ImVec2(p3.x, p3.y), 
        ImGui::GetColorU32(color));
}