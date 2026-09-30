#include "pch.h"
#include "TerrainTool.h"
#include "GameObject.h"
#include "EditorGUI.h"

TerrainTool::TerrainTool() : m_brushSize(10.0f), m_brushStrength(10.0f), m_useHeightmap(false)
{
    m_InspectorTitleName = "TerrainTool";
}

TerrainTool::TerrainTool(const TerrainTool& other) : m_brushSize(10.0f), m_brushStrength(10.0f), m_useHeightmap(false)
{
}

TerrainTool::~TerrainTool()
{
}

bool TerrainTool::Initialize(ID3D11Device* device, int terrainWidth, int terrainHeight)
{
    // 지형의 너비와 높이를 설정
    m_terrainWidth = terrainWidth;
    m_terrainHeight = terrainHeight;

    // 지형에 대한 지오 메트릭을 포함하는 정점 및 인덱스 버퍼를 초기화
    return InitializeBuffers(device);
}

// heightmap
bool TerrainTool::Initialize(ID3D11Device* device, const char* heightmapFilename)
{
    // 지형의 높이 맵을 로드
    if (!LoadHeightmap(heightmapFilename))
        return false;

    // 높이 맵의 높이를 표준화
    NormalizeHeightmap();

    // 지형에 대한 지오 메트릭을 포함하는 정점 및 인덱스 버퍼를 초기화
    return InitializeBuffers(device);
}

void TerrainTool::TerrainRender(ID3D11DeviceContext* deviceContext)
{
    // 파이프라인에 꼭지점 인덱스 버퍼 놓기
    RenderBuffers(deviceContext);
}

void TerrainTool::Shutdown()
{
    ShutdownBuffers();
    ShutdownHeightmap();
}

int TerrainTool::GetIndexCount()
{
    return m_indexCount;
}

bool TerrainTool::LoadHeightmap(const char* filename)
{
    // 바이너리 모드로 하이트맵 오픈
    FILE* filePtr = nullptr;
    if (fopen_s(&filePtr, filename, "rb") != 0)
        return false;

    // 파일 헤더 리드
    BITMAPFILEHEADER bitmapFileHeader;
    if (fread(&bitmapFileHeader, sizeof(BITMAPFILEHEADER), 1, filePtr) != 1)
        return false;

    // 비트맵 정보 헤더 리드
    BITMAPINFOHEADER bitmapInfoHeader;
    if (fread(&bitmapInfoHeader, sizeof(BITMAPINFOHEADER), 1, filePtr) != 1)
        return false;

    // 지형의 크기를 저장
    m_terrainWidth = bitmapInfoHeader.biWidth;
    m_terrainHeight = bitmapInfoHeader.biHeight;

    // 비트맵 이미지 데이터의 크기를 계산
    int imageSize = m_terrainWidth * m_terrainHeight * 3;

    // 비트맵 이미지 데이터에 메모리를 할당
    unsigned char* bitmapImage = new unsigned char[imageSize];
    if (!bitmapImage)
    {
        return false;
    }

    // 비트맵 데이터의 시작 부분으로 이동
    fseek(filePtr, bitmapFileHeader.bfOffBits, SEEK_SET);

    // 비트맵 이미지 데이터 리드
    if (fread(bitmapImage, 1, imageSize, filePtr) != imageSize)
        return false;

    // 파일을 닫습니다.
    if (fclose(filePtr) != 0)
        return false;

    // 높이 맵 데이터를 저장할 구조체
    m_heightMap = new HeightMapType[m_terrainWidth * m_terrainHeight];
    if (!m_heightMap)
        return false;

    // 버퍼 위치 초기화
    int k = 0;

    // 하이트맵 로드
    for (int j = 0; j < m_terrainHeight; j++)
    {
        for (int i = 0; i < m_terrainWidth; i++)
        {
            unsigned char height = bitmapImage[k];

            int index = (m_terrainHeight * j) + i;

            m_heightMap[index].x = (float)i;
            m_heightMap[index].y = (float)height;
            m_heightMap[index].z = (float)j;

            k += 3;
        }
    }

    // 배열 해제
    delete[] bitmapImage;
    bitmapImage = 0;

    return true;
}

void TerrainTool::NormalizeHeightmap()
{
    for (int j = 0; j < m_terrainHeight; j++)
    {
        for (int i = 0; i < m_terrainWidth; i++)
        {
            m_heightMap[(m_terrainHeight * j) + i].y /= 15.0f;
        }
    }
}

void TerrainTool::ShutdownHeightmap()
{
    if (m_heightMap)
    {
        delete[] m_heightMap;
        m_heightMap = 0;
    }
}

bool TerrainTool::InitializeBuffers(ID3D11Device* device)
{
    // 지형 메쉬의 정점 수를 계산
    m_vertexCount = (m_terrainWidth - 1) * (m_terrainHeight - 1) * 8;

    // 인덱스 수를 꼭지점 수와 같게 설정
    m_indexCount = m_vertexCount;

    // 정점 배열
    VertexType* vertices = new VertexType[m_vertexCount];
    if (!vertices)
        return false;

    // 인덱스 배열
    unsigned long* indices = new unsigned long[m_indexCount];
    if (!indices)
        return false;

    // 정점 배열에 대한 인덱스를 초기화
    int index = 0;

    // 지형 데이터로 정점 및 인덱스 배열을 로드
    for (int j = 0; j < (m_terrainHeight - 1); j++)
    {
        for (int i = 0; i < (m_terrainWidth - 1); i++)
        {
            // LINE 1
            // 왼쪽 위
            float positionX = (float)i;
            float positionZ = (float)(j + 1);

            vertices[index].position = XMFLOAT3(positionX, 0.0f, positionZ);
            vertices[index].color = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
            indices[index] = index;
            index++;

            // 오른쪽 위
            positionX = (float)(i + 1);
            positionZ = (float)(j + 1);

            vertices[index].position = XMFLOAT3(positionX, 0.0f, positionZ);
            vertices[index].color = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
            indices[index] = index;
            index++;

            // LINE 2
            // 오른쪽 위
            positionX = (float)(i + 1);
            positionZ = (float)(j + 1);

            vertices[index].position = XMFLOAT3(positionX, 0.0f, positionZ);
            vertices[index].color = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
            indices[index] = index;
            index++;

            // 오른쪽 아래
            positionX = (float)(i + 1);
            positionZ = (float)j;

            vertices[index].position = XMFLOAT3(positionX, 0.0f, positionZ);
            vertices[index].color = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
            indices[index] = index;
            index++;

            // LINE 3
            // 오른쪽 아래
            positionX = (float)(i + 1);
            positionZ = (float)j;

            vertices[index].position = XMFLOAT3(positionX, 0.0f, positionZ);
            vertices[index].color = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
            indices[index] = index;
            index++;

            // 왼쪽 아래
            positionX = (float)i;
            positionZ = (float)j;

            vertices[index].position = XMFLOAT3(positionX, 0.0f, positionZ);
            vertices[index].color = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
            indices[index] = index;
            index++;

            // LINE 4
            // 왼쪽 아래
            positionX = (float)i;
            positionZ = (float)j;

            vertices[index].position = XMFLOAT3(positionX, 0.0f, positionZ);
            vertices[index].color = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
            indices[index] = index;
            index++;

            // 왼쪽 위
            positionX = (float)i;
            positionZ = (float)(j + 1);

            vertices[index].position = XMFLOAT3(positionX, 0.0f, positionZ);
            vertices[index].color = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
            indices[index] = index;
            index++;
        }
    }

    // 정적 정점 버퍼의 구조체를 설정
    D3D11_BUFFER_DESC vertexBufferDesc;
    vertexBufferDesc.Usage = D3D11_USAGE_DEFAULT;
    vertexBufferDesc.ByteWidth = sizeof(VertexType) * m_vertexCount;
    vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vertexBufferDesc.CPUAccessFlags = 0;
    vertexBufferDesc.MiscFlags = 0;
    vertexBufferDesc.StructureByteStride = 0;

    // subresource 구조에 정점 데이터에 대한 포인터를 제공
    D3D11_SUBRESOURCE_DATA vertexData;
    vertexData.pSysMem = vertices;
    vertexData.SysMemPitch = 0;
    vertexData.SysMemSlicePitch = 0;

    // 정점 버퍼
    // if (CHECK(device->CreateBuffer(&vertexBufferDesc, &vertexData, &m_vertexBuffer)))
    //    return false;

    // 정적 인덱스 버퍼의 구조체를 설정
    D3D11_BUFFER_DESC indexBufferDesc;
    indexBufferDesc.Usage = D3D11_USAGE_DEFAULT;
    indexBufferDesc.ByteWidth = sizeof(unsigned long) * m_indexCount;
    indexBufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    indexBufferDesc.CPUAccessFlags = 0;
    indexBufferDesc.MiscFlags = 0;
    indexBufferDesc.StructureByteStride = 0;

    // 하위 리소스 구조에 인덱스 데이터에 대한 포인터를 제공
    D3D11_SUBRESOURCE_DATA indexData;
    indexData.pSysMem = indices;
    indexData.SysMemPitch = 0;
    indexData.SysMemSlicePitch = 0;

    // 인덱스 버퍼
    if (FAILED(device->CreateBuffer(&indexBufferDesc, &indexData, &m_indexBuffer)))
        return false;

    // 배열 해제
    delete[] vertices;
    vertices = 0;

    delete[] indices;
    indices = 0;

    return true;
}

bool TerrainTool::InitializeHeightmapBuffers(ID3D11Device* device)
{
    // 지형 메쉬의 정점 수를 계산
    m_vertexCount = (m_terrainWidth - 1) * (m_terrainHeight - 1) * 12;

    // 인덱스 수를 꼭지점 수와 같게 설정
    m_indexCount = m_vertexCount;

    // 정점 배열
    VertexType* vertices = new VertexType[m_vertexCount];
    if (!vertices)
    {
        return false;
    }

    // 인덱스 배열
    unsigned long* indices = new unsigned long[m_indexCount];
    if (!indices)
    {
        return false;
    }

    // 정점 배열에 대한 인덱스를 초기화
    int index = 0;

    // 지형 데이터로 정점 및 인덱스 배열을 로드
    for (int j = 0; j < (m_terrainHeight - 1); j++)
    {
        for (int i = 0; i < (m_terrainWidth - 1); i++)
        {
            int index1 = (m_terrainHeight * j) + i;          // 왼쪽 아래
            int index2 = (m_terrainHeight * j) + (i + 1);      // 오른쪽 아래
            int index3 = (m_terrainHeight * (j + 1)) + i;      // 왼쪽 위
            int index4 = (m_terrainHeight * (j + 1)) + (i + 1);  // 오른쪽 위

            // 왼쪽 위
            vertices[index].position = XMFLOAT3(m_heightMap[index3].x, m_heightMap[index3].y, m_heightMap[index3].z);
            vertices[index].color = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
            indices[index] = index;
            index++;

            // 오른쪽 위
            vertices[index].position = XMFLOAT3(m_heightMap[index4].x, m_heightMap[index4].y, m_heightMap[index4].z);
            vertices[index].color = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
            indices[index] = index;
            index++;

            // 오른쪽 위
            vertices[index].position = XMFLOAT3(m_heightMap[index4].x, m_heightMap[index4].y, m_heightMap[index4].z);
            vertices[index].color = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
            indices[index] = index;
            index++;

            // 왼쪽 아래
            vertices[index].position = XMFLOAT3(m_heightMap[index1].x, m_heightMap[index1].y, m_heightMap[index1].z);
            vertices[index].color = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
            indices[index] = index;
            index++;

            // 왼쪽 아래
            vertices[index].position = XMFLOAT3(m_heightMap[index1].x, m_heightMap[index1].y, m_heightMap[index1].z);
            vertices[index].color = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
            indices[index] = index;
            index++;

            // 왼쪽 위
            vertices[index].position = XMFLOAT3(m_heightMap[index3].x, m_heightMap[index3].y, m_heightMap[index3].z);
            vertices[index].color = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
            indices[index] = index;
            index++;

            // 왼쪽 아래
            vertices[index].position = XMFLOAT3(m_heightMap[index1].x, m_heightMap[index1].y, m_heightMap[index1].z);
            vertices[index].color = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
            indices[index] = index;
            index++;

            // 오른쪽 위
            vertices[index].position = XMFLOAT3(m_heightMap[index4].x, m_heightMap[index4].y, m_heightMap[index4].z);
            vertices[index].color = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
            indices[index] = index;
            index++;

            // 오른쪽 위
            vertices[index].position = XMFLOAT3(m_heightMap[index4].x, m_heightMap[index4].y, m_heightMap[index4].z);
            vertices[index].color = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
            indices[index] = index;
            index++;

            // 오른쪽 아래
            vertices[index].position = XMFLOAT3(m_heightMap[index2].x, m_heightMap[index2].y, m_heightMap[index2].z);
            vertices[index].color = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
            indices[index] = index;
            index++;

            // 오른쪽 아래
            vertices[index].position = XMFLOAT3(m_heightMap[index2].x, m_heightMap[index2].y, m_heightMap[index2].z);
            vertices[index].color = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
            indices[index] = index;
            index++;

            // 왼쪽 아래
            vertices[index].position = XMFLOAT3(m_heightMap[index1].x, m_heightMap[index1].y, m_heightMap[index1].z);
            vertices[index].color = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
            indices[index] = index;
            index++;
        }
    }

    // 정적 정점 버퍼의 구조체를 설정
    D3D11_BUFFER_DESC vertexBufferDesc;
    vertexBufferDesc.Usage = D3D11_USAGE_DEFAULT;
    vertexBufferDesc.ByteWidth = sizeof(VertexType) * m_vertexCount;
    vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vertexBufferDesc.CPUAccessFlags = 0;
    vertexBufferDesc.MiscFlags = 0;
    vertexBufferDesc.StructureByteStride = 0;

    // subresource 구조에 정점 데이터에 대한 포인터를 제공
    D3D11_SUBRESOURCE_DATA vertexData;
    vertexData.pSysMem = vertices;
    vertexData.SysMemPitch = 0;
    vertexData.SysMemSlicePitch = 0;

    // 정점 버퍼
    //if (CHECK(device->CreateBuffer(&vertexBufferDesc, &vertexData, &m_vertexBuffer)))
    //    return false;

    // 정적 인덱스 버퍼의 구조체를 설정
    D3D11_BUFFER_DESC indexBufferDesc;
    indexBufferDesc.Usage = D3D11_USAGE_DEFAULT;
    indexBufferDesc.ByteWidth = sizeof(unsigned long) * m_indexCount;
    indexBufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    indexBufferDesc.CPUAccessFlags = 0;
    indexBufferDesc.MiscFlags = 0;
    indexBufferDesc.StructureByteStride = 0;

    // 하위 리소스 구조에 인덱스 데이터에 대한 포인터를 제공
    D3D11_SUBRESOURCE_DATA indexData;
    indexData.pSysMem = indices;
    indexData.SysMemPitch = 0;
    indexData.SysMemSlicePitch = 0;

    // 인덱스 버퍼
    //if (CHECK(device->CreateBuffer(&indexBufferDesc, &indexData, &m_indexBuffer)))
    //    return false;

    // 배열 해제
    delete[] vertices;
    vertices = 0;

    delete[] indices;
    indices = 0;

    return true;
}

void TerrainTool::ShutdownBuffers()
{
    // 인덱스 버퍼를 해제
    if (m_indexBuffer)
    {
        m_indexBuffer->Release();
        m_indexBuffer = 0;
    }

    // 버텍스 버퍼를 해제
    if (m_vertexBuffer)
    {
        m_vertexBuffer->Release();
        m_vertexBuffer = 0;
    }
}

void TerrainTool::RenderBuffers(ID3D11DeviceContext* deviceContext)
{
    // 정점 버퍼 보폭 및 오프셋을 설정
    unsigned int stride = sizeof(VertexType);
    unsigned int offset = 0;

    // 렌더링 할 수 있도록 입력 어셈블러에서 정점 버퍼를 활성으로 설정
    deviceContext->IASetVertexBuffers(0, 1, &m_vertexBuffer, &stride, &offset);

    // 렌더링 할 수 있도록 입력 어셈블러에서 인덱스 버퍼를 활성으로 설정
    deviceContext->IASetIndexBuffer(m_indexBuffer, DXGI_FORMAT_R32_UINT, 0);

    // 라인리스트
    deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
}

void TerrainTool::Awake()
{

}

void TerrainTool::Update()
{
    //if (m_useHeightmap)
    //    ApplyHeightmap();
    //else
    //    PaintTerrain();
}

void TerrainTool::Render()
{
    TerrainRender(Application::GetI()->GetDeviceContext());
}

void TerrainTool::OnInspectorGUI()
{
    // brush 값 설정
    EditorGUI::LabelHeader("Brush");
    
    if (EditorGUI::FloatField("Brush Size", m_brushSize))
    {
        m_brushSize = max(m_brushSize, 0.001f);
        m_brushSize = min(m_brushSize, 999);
    }
    if (EditorGUI::FloatField("Brush Strength", m_brushStrength))
    {
        m_brushStrength = max(m_brushStrength, 0.001f);
        m_brushStrength = min(m_brushStrength, 999);
    }
    
    // heightmap
    EditorGUI::LabelHeader("Heightmap");

    if (EditorGUI::BoolField("Use Heightmap", m_useHeightmap))
    {

    }

}

GENERATE_COMPONENT_FUNC_TOJSON(TerrainTool)
{
    json j = {};

    SERIALIZE_TYPE(j, TerrainTool);

    SERIALIZE_FLOAT(j, m_brushSize, "brushSize");
    SERIALIZE_FLOAT(j, m_brushStrength, "brushStrength");
    SERIALIZE_BOOL(j, m_useHeightmap, "m_UseHeightmap");

    return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(TerrainTool)
{
    DE_SERIALIZE_FLOAT(j, m_brushSize, "brushSize");
    DE_SERIALIZE_FLOAT(j, m_brushStrength, "brushStrength");
    DE_SERIALIZE_BOOL(j, m_useHeightmap, "m_UseHeightmap");
}
