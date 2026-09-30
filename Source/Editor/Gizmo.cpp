#include "pch.h"
#include "Gizmo.h"
#include "SceneViewOverlay.h"
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

    // Ä«¸Ş¶óÀÇ ºä ÇÁ·ÎÁ§¼Ç Çà·Ä °¡Á®¿À±â
    Matrix viewProj = RenderManager::GetI()->EditorCameraViewProjectionMatrix;
    Matrix worldViewProj = worldMatrix * viewProj;

    // ºäÆ÷Æ® Å©±â °¡Á®¿À±â
    D3D11_VIEWPORT viewport;
    UINT numViewports = 1;
    context->RSGetViewports(&numViewports, &viewport);

    // Ä«¸Ş¶óÀÇ À§Ä¡¸¦ °¡Á®¿À±â
    EditorCamera* sceneViewCamera = SceneViewManager::GetI()->m_LastActiveSceneEditorWindow->GetSceneCamera();
    Vec3 cameraPosition = sceneViewCamera->GetPosition();
    Vec3 position = Vec3(worldMatrix._41, worldMatrix._42, worldMatrix._43);

    // Ä«¸Ş¶ó¿Í ¿ÀºêÁ§Æ® °£ÀÇ °Å¸® °è»ê
    float distance = (position - cameraPosition).Length();
    distance = max(distance, 0.1f); // ÃÖ¼Ò °Å¸® Á¦ÇÑ

    // °Å¸® ±â¹İ ½ºÄÉÀÏ¸µ ÆÑÅÍ °è»ê
    float scale = distance / 7.0f;

    // ÇöÀç Ã¢ÀÇ À§Ä¡¿Í Å©±â¸¦ °¡Á®¿È
    ImVec2 contentRegionMin, contentRegionMax, offset;
    SceneViewOverlay::GetViewRect(contentRegionMin, contentRegionMax, offset);   // Scene ì´ë¯¸ì§€ ì˜ì—­ (íˆ´ë°” ì œì™¸)

    auto WorldToScreen = [&](const Vec3& worldPos) -> ImVec2
        {
            Vec3 pos = Vector3::Transform(worldPos, worldViewProj);
            float x = (pos.x / pos.z) * 0.5f + 0.5f;
            float y = (pos.y / pos.z) * -0.5f + 0.5f;
            return ImVec2(x * viewport.Width + viewport.TopLeftX + offset.x, y * viewport.Height + viewport.TopLeftY + offset.y);
        };

    const ImU32 color = IM_COL32(255, 0, 0, 255);
    const float thickness = 2.0f;

    // ½ÃÀÛÁ¡ ¹× ³¡Á¡ °è»ê
    Vec3 start = Vector3::Transform(Vec3::Zero, worldMatrix);
    Vec3 end = Vector3::Transform(vector * scale, worldMatrix);

    // È­¸é ÁÂÇ¥·Î º¯È¯
    ImVec2 startScreen = WorldToScreen(start);
    ImVec2 endScreen = WorldToScreen(end);

    // º¤ÅÍ ±×¸®±â
    ImGui::GetWindowDrawList()->AddLine(startScreen, endScreen, color, thickness);
}

void Gizmo::DrawCube(const XMMATRIX& worldMatrix, const Vec3& size)
{
    auto context = Application::GetI()->GetDeviceContext();

    // ¹Ú½ºÀÇ 8°³ÀÇ ²ÀÁşÁ¡ Á¤ÀÇ
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

    // Ä«¸Ş¶óÀÇ ºä ÇÁ·ÎÁ§¼Ç Çà·Ä °¡Á®¿À±â
    XMMATRIX viewProj = RenderManager::GetI()->EditorCameraViewProjectionMatrix;
    XMMATRIX worldViewProj = worldMatrix * viewProj;

    // ºäÆ÷Æ® Å©±â °¡Á®¿À±â
    D3D11_VIEWPORT viewport;
    UINT numViewports = 1;
    context->RSGetViewports(&numViewports, &viewport);

    // ÇöÀç Ã¢ÀÇ À§Ä¡¿Í Å©±â¸¦ °¡Á®¿È
    ImVec2 contentRegionMin, contentRegionMax, offset;
    SceneViewOverlay::GetViewRect(contentRegionMin, contentRegionMax, offset);   // Scene ì´ë¯¸ì§€ ì˜ì—­ (íˆ´ë°” ì œì™¸)

    // ²ÀÁşÁ¡À» ½ºÅ©¸° ÁÂÇ¥·Î º¯È¯
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

    // ImGui¸¦ »ç¿ëÇÏ¿© ¹Ú½ºÀÇ ¿§Áö¸¦ ±×¸²
    const ImU32 color = IM_COL32(0, 255, 0, 255);
    const float thickness = 1.0f;

    // ¹Ú½ºÀÇ 12°³ÀÇ ¿§Áö ±×¸®±â
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

    const int circleSegments = 25;  // ¿øÀ» ±¸¼ºÇÏ´Â ¼¼±×¸ÕÆ® ¼ö

    std::vector<XMFLOAT3> vertices;
    vertices.reserve(circleSegments + 1);

    // Ä«¸Ş¶óÀÇ ºä ÇÁ·ÎÁ§¼Ç Çà·Ä °¡Á®¿À±â
    XMMATRIX viewProj = RenderManager::GetI()->EditorCameraViewProjectionMatrix;
    XMMATRIX worldViewProj = worldMatrix * viewProj;

    // ºäÆ÷Æ® Å©±â °¡Á®¿À±â
    D3D11_VIEWPORT viewport;
    UINT numViewports = 1;
    context->RSGetViewports(&numViewports, &viewport);

    // ÇöÀç Ã¢ÀÇ À§Ä¡¿Í Å©±â¸¦ °¡Á®¿È
    ImVec2 contentRegionMin, contentRegionMax, offset;
    SceneViewOverlay::GetViewRect(contentRegionMin, contentRegionMax, offset);   // Scene ì´ë¯¸ì§€ ì˜ì—­ (íˆ´ë°” ì œì™¸)

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

    // °¢ Ãà¿¡ ´ëÇØ ¿øÀ» ±×¸²
    XMFLOAT3 center(0.0f, 0.0f, 0.0f);

    // ´ÜÀ§ º¤ÅÍ¸¦ »ç¿ëÇÏ¿© È¸Àü¸¸ Àû¿ë
    XMMATRIX rotationMatrix = XMMatrixRotationQuaternion(XMQuaternionRotationMatrix(worldMatrix));

    XMVECTOR right = XMVector3TransformNormal(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), rotationMatrix); 
    XMVECTOR up = XMVector3TransformNormal(XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), rotationMatrix); 
    XMVECTOR forward = XMVector3TransformNormal(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), rotationMatrix); 

    XMFLOAT3 rightF, upF, forwardF;
    XMStoreFloat3(&rightF, right);
    XMStoreFloat3(&upF, up);
    XMStoreFloat3(&forwardF, forward);

    DrawCircle(center, upF, rightF);  // XY Æò¸é 
    DrawCircle(center, forwardF, rightF);  // XZ Æò¸é 
    DrawCircle(center, upF, forwardF);  // YZ Æò¸é 
     
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
    //DrawCircle(center, camUpF, camRightF);  // XY Æò¸é 
}

void Gizmo::DrawArrow(Vector3 position, Vec3 dir, ImVec4 color) 
{
    EditorCamera* sceneViewCamera = SceneViewManager::GetI()->m_LastActiveSceneEditorWindow->GetSceneCamera();

    // World Matrix °¡Á®¿À±â
    Matrix worldMatrix = Matrix::CreateTranslation(position);

    auto context = Application::GetI()->GetDeviceContext();
    Matrix viewProj = RenderManager::GetI()->EditorCameraViewProjectionMatrix;

    // À§Ä¡¸¦ ÃßÃâ
    Vector3 pos = position;

    // Ä«¸Ş¶óÀÇ À§Ä¡¸¦ °¡Á®¿À±â
    Vector3 cameraPosition = sceneViewCamera->GetPosition();

    // Ä«¸Ş¶ó¿Í ¿ÀºêÁ§Æ® °£ÀÇ °Å¸® °è»ê
    float distance = Vector3::Distance(pos, cameraPosition);
    distance = max(distance, 0.1f); // ÃÖ¼Ò °Å¸® Á¦ÇÑ

    // °Å¸® ±â¹İ ½ºÄÉÀÏ¸µ ÆÑÅÍ °è»ê
    float scale = distance / 10;

    // 3D¿¡¼­ 2D·Î Åõ¿µ
    D3D11_VIEWPORT viewport;
    UINT numViewports = 1;
    context->RSGetViewports(&numViewports, &viewport);

    // ÇöÀç Ã¢ÀÇ À§Ä¡¿Í Å©±â¸¦ °¡Á®¿È
    ImVec2 contentRegionMin, contentRegionMax, offset;
    SceneViewOverlay::GetViewRect(contentRegionMin, contentRegionMax, offset);   // Scene ì´ë¯¸ì§€ ì˜ì—­ (íˆ´ë°” ì œì™¸)

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

    // ÇÚµé ±×¸®±â
    DrawwPoinVector(handlePos, endPos, color);
}

void Gizmo::DrawFrustum(const XMMATRIX& worldMatrix, float _near, float _far, float fieldOfView)
{
    auto context = Application::GetI()->GetDeviceContext();

    // Ä«¸Ş¶óÀÇ Á¾È¾ºñ (°¡·Î ¼¼·Î ºñÀ²) °¡Á®¿À±â - ¿¹Á¦¿¡¼­´Â 16:9·Î °íÁ¤
    float aspectRatio = 16.0f / 9.0f;

    // ½Ã¾ß°¢À» ¶óµğ¾ÈÀ¸·Î º¯È¯ÇÏ°í tangent °ª °è»ê
    float tanFov = tanf(XMConvertToRadians(fieldOfView) * 0.5f);

    // ±ÙÆò¸é°ú ¿øÆò¸éÀÇ ¹İ³ôÀÌ ¹× ¹İ³Êºñ °è»ê
    float nearHeight = _near * tanFov;
    float nearWidth = nearHeight * aspectRatio;
    float farHeight = _far * tanFov;
    float farWidth = farHeight * aspectRatio;

    // ±ÙÆò¸é°ú ¿øÆò¸éÀÇ ²ÀÁşÁ¡ Á¤ÀÇ
    XMFLOAT3 nearVertices[4] = {
        { -nearWidth, nearHeight, _near },  // ÁÂ»ó
        { nearWidth, nearHeight, _near },   // ¿ì»ó
        { nearWidth, -nearHeight, _near },  // ¿ìÇÏ
        { -nearWidth, -nearHeight, _near }  // ÁÂÇÏ
    };

    XMFLOAT3 farVertices[4] = {
        { -farWidth, farHeight, _far },  // ÁÂ»ó
        { farWidth, farHeight, _far },   // ¿ì»ó
        { farWidth, -farHeight, _far },  // ¿ìÇÏ
        { -farWidth, -farHeight, _far }  // ÁÂÇÏ
    };

    // Ä«¸Ş¶óÀÇ ºä ÇÁ·ÎÁ§¼Ç Çà·Ä °¡Á®¿À±â
    XMMATRIX viewProj = RenderManager::GetI()->EditorCameraViewProjectionMatrix;
    XMMATRIX worldViewProj = worldMatrix * viewProj;

    // ºäÆ÷Æ® Å©±â °¡Á®¿À±â
    D3D11_VIEWPORT viewport;
    UINT numViewports = 1;
    context->RSGetViewports(&numViewports, &viewport);

    // ÇöÀç Ã¢ÀÇ À§Ä¡¿Í Å©±â¸¦ °¡Á®¿È
    ImVec2 contentRegionMin, contentRegionMax, offset;
    SceneViewOverlay::GetViewRect(contentRegionMin, contentRegionMax, offset);   // Scene ì´ë¯¸ì§€ ì˜ì—­ (íˆ´ë°” ì œì™¸)

    // ±ÙÆò¸é°ú ¿øÆò¸éÀÇ ²ÀÁşÁ¡À» ½ºÅ©¸° ÁÂÇ¥·Î º¯È¯
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

    // ImGui¸¦ »ç¿ëÇÏ¿© ÇÁ·¯½ºÅÒÀÇ ¿§Áö¸¦ ±×¸²
    const ImU32 color = IM_COL32(255, 255, 255, 255); // ÇÏ¾á»ö
    const float thickness = 1.0f;

    // ±ÙÆò¸é ±×¸®±â
    for (int i = 0; i < 4; ++i)
    {
        int next = (i + 1) % 4;
        ImGui::GetWindowDrawList()->AddLine(screenNearVertices[i], screenNearVertices[next], color, thickness);
    }

    // ¿øÆò¸é ±×¸®±â
    for (int i = 0; i < 4; ++i)
    {
        int next = (i + 1) % 4;
        ImGui::GetWindowDrawList()->AddLine(screenFarVertices[i], screenFarVertices[next], color, thickness);
    }

    // ±ÙÆò¸é°ú ¿øÆò¸éÀ» ¿¬°áÇÏ´Â ¿§Áö ±×¸®±â
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

    // À§Ä¡¸¦ ÃßÃâ
    XMVECTOR position = transform->GetPosition();

    // Ä«¸Ş¶óÀÇ À§Ä¡¸¦ °¡Á®¿À±â
    XMFLOAT3 cameraPositionFloat3 = sceneViewCamera->GetPosition();
    XMVECTOR cameraPosition = XMLoadFloat3(&cameraPositionFloat3);

    // Ä«¸Ş¶ó¿Í ¿ÀºêÁ§Æ® °£ÀÇ °Å¸® °è»ê
    float distance = XMVectorGetX(XMVector3Length(position - cameraPosition));
    distance = max(distance, 0.1f); // ÃÖ¼Ò °Å¸® Á¦ÇÑ

    // °Å¸® ±â¹İ ½ºÄÉÀÏ¸µ ÆÑÅÍ °è»ê
    float scale = distance / 7;

    // °¢ ÃàÀÇ ¹æÇâ º¤ÅÍ¸¦ Á¤ÀÇ
    XMVECTOR xAxis = XMVector3Normalize(XMVector3TransformNormal(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), worldMatrix)) * scale;
    XMVECTOR yAxis = XMVector3Normalize(XMVector3TransformNormal(XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), worldMatrix)) * scale;
    XMVECTOR zAxis = XMVector3Normalize(XMVector3TransformNormal(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), worldMatrix)) * scale;

    // 3D¿¡¼­ 2D·Î Åõ¿µ
    D3D11_VIEWPORT viewport;
    UINT numViewports = 1;
    context->RSGetViewports(&numViewports, &viewport);

    // ÇöÀç Ã¢ÀÇ À§Ä¡¿Í Å©±â¸¦ °¡Á®¿È
    ImVec2 contentRegionMin, contentRegionMax, offset;
    SceneViewOverlay::GetViewRect(contentRegionMin, contentRegionMax, offset);   // Scene ì´ë¯¸ì§€ ì˜ì—­ (íˆ´ë°” ì œì™¸)

    auto WorldToScreen = [&](const XMVECTOR& worldPos) -> ImVec2
    {
            DirectX::XMFLOAT4 projectedPos;
            DirectX::XMStoreFloat4(&projectedPos, DirectX::XMVector3TransformCoord(worldPos, viewProj));

            // Z-ºĞÇÒÀÌ 0º¸´Ù ÀÛÀº °æ¿ì¸¦ Ã³¸®
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

    // ÇÚµé ±×¸®±â
    ImVec4 xColor = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
    ImVec4 yColor = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
    ImVec4 zColor = ImVec4(0.0f, 0.0f, 1.0f, 1.0f);

    DrawAxisHandle(handlePos, xEnd, xColor);
    DrawAxisHandle(handlePos, yEnd, yColor);
    DrawAxisHandle(handlePos, zEnd, zColor);

    // ¸¶¿ì½º ÀÔ·Â Ã³¸® ¹× º¯È¯ Àû¿ë
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
        //    transformMatrix._43 += delta.x * 0.01f; // ´Ü¼ø ¿¹Á¦ÀÌ¹Ç·Î ZÃàµµ XÁÂÇ¥¿¡ ´ëÇØ º¯°æ
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

    // ¹æÇâ º¤ÅÍ °è»ê
    Vec2 direction = dir - pos;
    float length = sqrtf(direction.x * direction.y + direction.y * direction.y); 

    // »ï°¢ÇüÀÇ ´Ù¸¥ µÎ Á¡À» °è»ê
    Vec2 directionNorm = direction / length;
    Vec2 orthogonal(-directionNorm.y, directionNorm.x); // orthogonal º¤ÅÍ 

    float arrowSize = 10.0f; // »ï°¢ÇüÀÇ Å©±â¸¦ Á¶Á¤ÇÏ±â À§ÇÑ °ª 
    Vec2 p1 = dir;
    Vec2 p2 = dir - directionNorm * arrowSize + orthogonal * arrowSize * 0.5f;  
    Vec2 p3 = dir - directionNorm * arrowSize - orthogonal * arrowSize * 0.5f;  

    // »ï°¢Çü ±×¸®±â
    ImGui::GetWindowDrawList()->AddTriangleFilled(
        ImVec2(p1.x, p1.y), 
        ImVec2(p2.x, p2.y), 
        ImVec2(p3.x, p3.y), 
        ImGui::GetColorU32(color));
}