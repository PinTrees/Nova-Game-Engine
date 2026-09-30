#pragma once

class UMaterial;

// Unity URP Lit 재질 Inspector (Surface Options / Surface Inputs / Advanced Options) 와 구 미리보기.
//  - 재질 에셋을 골랐을 때: 머리글(미리보기 아이콘, 이름, Shader) + 전체 + 아래쪽 Preview (끌어서 돌리기)
//  - Mesh Renderer 아래(embedded): 접을 수 있는 머리글 + 전체 (내장 Default-Material 은 읽기 전용)
class MaterialInspector
{
public:
	static void Draw(UMaterial& material, bool embedded);
	// 재질 Undo 감시 (Inspector 가 그릴 때마다 부른다)
	static void WatchUndo(const std::shared_ptr<UMaterial>& material);
	// 재질을 구에 그린 이미지 (size x size, 매 호출마다 다시 그림). yaw = 구 회전(라디안)
	static ImTextureID RenderPreview(UMaterial& material, int size, float yaw);

	// Renderer 의 Materials 목록 한 줄 (Element N | 재질 이름 ⊙).
	//  ⊙ = "Select Material" 창(None / builtin Default-Material / 프로젝트의 .mat), Project 창에서 .mat 끌어 놓기.
	//  바뀌면 material / path 를 고치고 true (None = nullptr, 빈 경로)
	static bool MaterialSlot(const char* label, const std::string& key, std::shared_ptr<UMaterial>& material, std::wstring& path);
	// 프로젝트의 모든 재질 (Assets / Resources\Packages 의 .mat, 앞에 builtin:Default-Material)
	static std::vector<std::string> FindAllMaterials();
};
