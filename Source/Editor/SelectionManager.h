#pragma once

class GameObject;
class UMaterial;
class MeshFile;

enum class SelectionType
{
    NONE,
    FILE,
    GAMEOBJECT,
    CUSTOM,         // 패키지 창이 고른 것 (예: Animator 창의 상태 / 전이) — Inspector 는 그 함수로 그린다
};

enum class SelectionSubType
{
    NONE,
    MATERIAL,
    FBX,
    CUSTOM_ASSET,   // 패키지가 등록한 에셋 종류 (EditorExtensions, 예: .controller)
    VOLUME_PROFILE,
    AUDIO_CLIP,
    SCRIPT,
};

// 패키지 창이 고른 것: 데이터 + Inspector 에 그리는 함수
struct CustomSelection
{
    std::string Owner;                 // 고른 창 / 패키지 ("Animator")
    std::shared_ptr<void> Data;
    std::function<void()> DrawInspector;
};

class NOVA_API SelectionManager
{
	SINGLE_HEADER(SelectionManager)

private:
    static SelectionType m_SelectedType;
    static SelectionSubType m_SelectedSubType;

    static std::wstring m_SelectedFilePath;
    static GameObject* m_SelectedGameObject;

    static shared_ptr<UMaterial> m_SelectedFile_Material;
    static shared_ptr<MeshFile> m_SelectFile_FbxModel;
    static CustomSelection m_Custom;

public:
    static void ClearSelection();

    static void SetSelectedFile(const std::wstring& filePath);
    static const std::wstring& GetSelectedFile() { return m_SelectedFilePath; }

    static void SetSelectedGameObject(GameObject* gameObject);
    static GameObject* GetSelectedGameObject() { return m_SelectedGameObject; }

    static SelectionType GetSelectedObjectType() { return m_SelectedType; }
    static SelectionSubType GetSelectedSubType() { return m_SelectedSubType; }

    static shared_ptr<UMaterial> GetSelectMaterial() { return m_SelectedFile_Material; }

    static shared_ptr<MeshFile> GetSelectFbxModel() { return m_SelectFile_FbxModel; }

    static void SetCustomSelection(const std::string& owner, std::shared_ptr<void> data, std::function<void()> drawInspector);
    static const CustomSelection& GetCustomSelection() { return m_Custom; }
};

