#pragma once

class GameObject;
class UMaterial;
class MeshFile;
class AnimatorController;

enum class SelectionType
{
    NONE,
    FILE,
    GAMEOBJECT,
    ANIMATOR,       // Animator 창에서 고른 상태 / 전이
};

enum class SelectionSubType
{
    NONE,
    MATERIAL,
    FBX,
    ANIMATOR_CONTROLLER,
    VOLUME_PROFILE,
    AUDIO_CLIP,
    SCRIPT,
};

// Animator 창 선택 (State >= 0 이면 상태, Transition >= 0 이면 전이)
struct AnimatorSelection
{
    shared_ptr<AnimatorController> Controller;
    int Layer = 0;
    int State = -1;
    int Transition = -1;
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
    static shared_ptr<AnimatorController> m_SelectFile_Controller;
    static AnimatorSelection m_AnimatorSelection;

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

    static shared_ptr<AnimatorController> GetSelectAnimatorController() { return m_SelectFile_Controller; }

    static void SetSelectedAnimatorItem(shared_ptr<AnimatorController> controller, int layer, int state, int transition);
    static const AnimatorSelection& GetAnimatorSelection() { return m_AnimatorSelection; }
};

