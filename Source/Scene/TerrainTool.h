#pragma once
#include "Component.h"

class TerrainTool : public Component
{
private:
    struct VertexType
    {
        XMFLOAT3 position;
        XMFLOAT4 color;
    };

    struct HeightMapType
    {
        float x, y, z;
    };

    int m_terrainWidth = 0;
    int m_terrainHeight = 0;
    int m_vertexCount = 0;
    int m_indexCount = 0;
    ID3D11Buffer* m_vertexBuffer = nullptr;
    ID3D11Buffer* m_indexBuffer = nullptr;
    HeightMapType* m_heightMap = nullptr;

    float m_brushSize;
    float m_brushStrength;
    bool m_useHeightmap;

public:
    TerrainTool();
    TerrainTool(const TerrainTool& other);
    ~TerrainTool();

    bool Initialize(ID3D11Device* device, int terrainWidth, int terrainHeight);
    bool Initialize(ID3D11Device* device, const char* heightmapFilename);
    void TerrainRender(ID3D11DeviceContext* deviceContext);
    void Shutdown();

    int GetIndexCount();

private:
    bool LoadHeightmap(const char* filename);
    void NormalizeHeightmap();
    void ShutdownHeightmap();

    bool InitializeBuffers(ID3D11Device* device);
    bool InitializeHeightmapBuffers(ID3D11Device* device);
    void ShutdownBuffers();
    void RenderBuffers(ID3D11DeviceContext* deviceContext);

    virtual void Awake() override;
    virtual void Update() override;
    virtual void Render() override;

    virtual void OnInspectorGUI() override;

public:
    //void PaintTerrain();
    //void ApplyHeightmap();

public:
    GENERATE_COMPONENT_BODY(TerrainTool)
};

REGISTER_COMPONENT(TerrainTool)
