#include "pch.h"
#include "GameObjectMenu.h"
#include "GameObjectFactory.h"
#include "EditorTheme.h"

namespace GameObjectMenu
{
	void PushContextStyle()
	{
		// Unity 컨텍스트 메뉴: 밝은 회색 배경 + 어두운 글자, 항목 높이 22px, 호버는 연한 파랑
		ImGui::PushStyleColor(ImGuiCol_PopupBg, EditorTheme::Rgb(242, 242, 242));
		ImGui::PushStyleColor(ImGuiCol_Border, EditorTheme::Rgb(150, 150, 150));
		ImGui::PushStyleColor(ImGuiCol_Text, EditorTheme::Rgb(22, 22, 22));
		ImGui::PushStyleColor(ImGuiCol_TextDisabled, EditorTheme::Rgb(146, 146, 146));
		ImGui::PushStyleColor(ImGuiCol_Header, EditorTheme::Rgb(153, 201, 239));
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, EditorTheme::Rgb(153, 201, 239));
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, EditorTheme::Rgb(153, 201, 239));
		ImGui::PushStyleColor(ImGuiCol_Separator, EditorTheme::Rgb(205, 205, 205));
		ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(30.0f, 3.0f));   // 왼쪽 여백 (Unity 메뉴처럼 글자가 안쪽에서 시작)
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 8.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 2.0f));
	}

	void PopContextStyle()
	{
		ImGui::PopStyleVar(5);
		ImGui::PopStyleColor(8);
	}

	void SetMenuWidth(float width)
	{
		ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0.0f), ImVec2(FLT_MAX, FLT_MAX));
	}

	namespace
	{
		// 비활성 항목 (Unity 에서는 존재하지만 이 엔진에서는 아직 지원하지 않음)
		void Disabled(const char* label)
		{
			ImGui::MenuItem(label, nullptr, false, false);
		}

		// 서브메뉴 안의 비활성 항목들만 있는 메뉴
		void DisabledSubMenu(const char* label, std::initializer_list<const char*> items)
		{
			SetMenuWidth(190.0f);
			if (ImGui::BeginMenu(label, false))   // 지원하지 않는 항목은 서브메뉴도 비활성 (회색)
			{
				for (const char* it : items)
					Disabled(it);
				ImGui::EndMenu();
			}
		}
	}

	void DrawCreateItems(Scene* scene, GameObject* parent)
	{
		auto add = [&](GameObject* obj)
		{
			if (obj == nullptr || scene == nullptr)
				return;
			if (parent != nullptr)
				obj->SetParent(parent, false);   // Unity: 자식으로 만들면 부모 원점에 놓인다
			else
				scene->AddRootGameObject(obj);
			SelectionManager::SetSelectedGameObject(obj);
		};

		if (ImGui::MenuItem("Create Empty", "Ctrl+Shift+N"))
			add(GameObjectFactory::CreateEmpty());

		DisabledSubMenu("2D Object", { "Sprites", "Physics", "Tilemap", "Sprite Mask" });

		SetMenuWidth(190.0f);
		if (ImGui::BeginMenu("3D Object"))
		{
			if (ImGui::MenuItem("Cube")) add(GameObjectFactory::CreateCube());
			if (ImGui::MenuItem("Sphere")) add(GameObjectFactory::CreateSphere());
			if (ImGui::MenuItem("Capsule")) add(GameObjectFactory::CreateCapsule());
			if (ImGui::MenuItem("Cylinder")) add(GameObjectFactory::CreateCylinder());
			if (ImGui::MenuItem("Plane")) add(GameObjectFactory::CreatePlane());
			if (ImGui::MenuItem("Quad")) add(GameObjectFactory::CreateQuad());
			ImGui::Separator();
			Disabled("Text - TextMeshPro");
			Disabled("Legacy");
			ImGui::Separator();
			Disabled("Ragdoll...");
			ImGui::Separator();
			Disabled("Terrain");
			Disabled("Tree");
			Disabled("Wind Zone");
			ImGui::EndMenu();
		}

		DisabledSubMenu("Effects", { "Particle System", "Trail", "Line" });

		SetMenuWidth(190.0f);
		if (ImGui::BeginMenu("Light"))
		{
			if (ImGui::MenuItem("Directional Light")) add(GameObjectFactory::CreateDirectionalLight());
			if (ImGui::MenuItem("Point Light")) add(GameObjectFactory::CreatePointLight());
			if (ImGui::MenuItem("Spot Light")) add(GameObjectFactory::CreateSpotLight());
			Disabled("Area Light");
			ImGui::Separator();
			Disabled("Reflection Probe");
			Disabled("Light Probe Group");
			ImGui::EndMenu();
		}

		DisabledSubMenu("Audio", { "Audio Source", "Audio Reverb Zone" });
		DisabledSubMenu("Video", { "Video Player" });
		DisabledSubMenu("UI (Canvas)", { "Canvas", "Text", "Image", "Button", "Panel" });
		DisabledSubMenu("AI", { "Navigation" });
		DisabledSubMenu("UI Toolkit", { "UI Document" });
		DisabledSubMenu("Rendering", { "Volume", "Reflection Probe" });
		DisabledSubMenu("Volume", { "Global Volume", "Box Volume", "Sphere Volume", "Convex Mesh Volume" });

		if (ImGui::MenuItem("Camera"))
			add(GameObjectFactory::CreateCamera("Camera"));

		Disabled("Visual Scripting Scene Variables");
		DisabledSubMenu("UI", { "Text", "Image", "Button" });
		DisabledSubMenu("Navigation", { "NavMesh Surface", "NavMesh Modifier" });
	}
}
