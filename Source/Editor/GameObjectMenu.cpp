#include "pch.h"
#include "Debug.h"
#include "GameObjectMenu.h"
#include "GameObjectFactory.h"
#include "TerrainStamp.h"
#include "TerrainBiomes.h"
#include "TerrainSpline.h"
#include "RockDesc.h"
#include "EditorTheme.h"
#include "UISystem.h"
#include "ReflectionProbe.h"
#include "Light2D.h"
#include "AdaptiveProbeVolume.h"
#include "DecalProjector.h"
#include "EditorExtensions.h"
#include "Ragdoll.h"
#include "UndoSystem.h"

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

		// 패키지가 등록한 GameObject 메뉴 항목 (EditorExtensions::RegisterCreateMenu) — 하위 메뉴까지
		void PackageMenu(const std::string& folder, Scene* scene, GameObject* parent)
		{
			for (const std::string& sub : EditorExtensions::CreateMenuFolders(folder))
			{
				SetMenuWidth(210.0f);
				if (ImGui::BeginMenu(sub.c_str()))
				{
					PackageMenu(folder + "/" + sub, scene, parent);
					ImGui::EndMenu();
				}
			}
			for (const EditorExtensions::CreateMenuItem* it : EditorExtensions::CreateMenuItems(folder))
				if (ImGui::MenuItem(it->Path.substr(it->Path.rfind('/') + 1).c_str()) && it->Create && scene)
					if (GameObject* made = it->Create(scene, parent))
						SelectionManager::SetSelectedGameObject(made);
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

		// 2D Object > Sprites (Unity 와 같은 내장 도형), Tilemap (2D Tilemap 패키지가 항목을 등록 — 없으면 회색) — Physics · Sprite Mask 는 아직
		SetMenuWidth(190.0f);
		if (ImGui::BeginMenu("2D Object"))
		{
			SetMenuWidth(150.0f);
			if (ImGui::BeginMenu("Sprites"))
			{
				for (const char* shape : { "Square", "Circle", "Capsule", "Triangle" })
					if (ImGui::MenuItem(shape))
						add(GameObjectFactory::CreateSprite(shape, std::string("builtin:") + shape));
				ImGui::EndMenu();
			}
			Disabled("Physics");
			const auto tilemapItems = EditorExtensions::CreateMenuItems("2D Object/Tilemap");
			if (tilemapItems.empty())
				Disabled("Tilemap");
			else
			{
				SetMenuWidth(190.0f);
				if (ImGui::BeginMenu("Tilemap"))
				{
					for (const EditorExtensions::CreateMenuItem* it : tilemapItems)
						if (ImGui::MenuItem(it->Path.substr(it->Path.rfind('/') + 1).c_str()) && it->Create && scene)
							if (GameObject* made = it->Create(scene, parent))
								SelectionManager::SetSelectedGameObject(made);
					ImGui::EndMenu();
				}
			}
			Disabled("Sprite Mask");
			ImGui::EndMenu();
		}

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
			if (ImGui::MenuItem("Character")) add(GameObjectFactory::CreateAnimatedCharacter());   // 엔진 패키지 기본 캐릭터 + Animator(DefaultCharacter.controller)
			if (ImGui::MenuItem("Third Person Character"))   // + Character Controller + ThirdPersonController + Follow Camera (패키지 자동)
			{
				std::string note;
				add(GameObjectFactory::CreateThirdPersonCharacter("Player", &note));
				Debug::Log(note);
			}
			// Starter Assets 예제: 운전할 수 있는 차 (프리팹) · 맞으면 래그돌로 쓰러지는 표적 (Main Camera 에 클릭 = 쏘기)
			if (ImGui::MenuItem("Car"))
			{
				std::string note;
				add(GameObjectFactory::CreateCar("Car", &note));
				Debug::Log(note);
			}
			if (ImGui::MenuItem("Ragdoll Target"))
			{
				std::string note;
				add(GameObjectFactory::CreateRagdollTarget("Ragdoll Target", &note));
				Debug::Log(note);
			}
			ImGui::Separator();
			Disabled("Text - TextMeshPro");
			Disabled("Legacy");
			ImGui::Separator();
			// Unity 의 Ragdoll Wizard: 고른 휴머노이드 캐릭터 (또는 그 부모) 에 바디 11 개 + Character Joint + Ragdoll
			if (ImGui::MenuItem("Ragdoll...", nullptr, false, SelectionManager::GetSelectedObjectType() == SelectionType::GAMEOBJECT && !Application::IsPlaying()))
			{
				std::string error, firstError;
				Ragdoll* made = nullptr;
				for (GameObject* g = SelectionManager::GetSelectedGameObject(); g && made == nullptr; g = g->GetParent())
				{
					made = Ragdoll::Build(g, 20.0f, error);
					if (made == nullptr && firstError.empty())
						firstError = error;
					if (made)
					{
						Undo::SetActionName("Create Ragdoll");
						Undo::Touch(g);
						Undo::RequestCheck();
						Debug::Log("Ragdoll: " + std::to_string(made->Parts.size()) + " bodies on '" + g->GetName() + "'");
					}
				}
				if (made == nullptr)
					Debug::LogWarning("Ragdoll: " + firstError);
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Terrain")) add(GameObjectFactory::CreateTerrain());   // 새 TerrainData(Assets) + Terrain + Terrain Collider
			if (ImGui::MenuItem("Tree")) add(GameObjectFactory::CreateTree());   // 절차적 나무 (텍스처 없는 셰이더)
			if (ImGui::BeginMenu("Rock"))   // 절차적 바위·절벽 (SDF 조형, 인스턴싱)
			{
				for (int p = 0; p < (int)RockDesc::PresetCount; ++p)
					if (ImGui::MenuItem(RockDesc::PresetName(p))) add(GameObjectFactory::CreateRock(p));
				ImGui::Separator();
				if (ImGui::BeginMenu("Scatter"))   // 영역에 무작위로 수백 개 (GameObject 하나)
				{
					for (int p = 0; p < (int)RockDesc::PresetCount; ++p)
						if (ImGui::MenuItem(RockDesc::PresetName(p))) add(GameObjectFactory::CreateRockScatter(p));
					ImGui::EndMenu();
				}
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Terrain Stamp"))   // 지형 생성기(Generate 도구)가 켜진 지형에 합쳐지는 지형 요소
			{
				for (int s = 0; s < (int)TerrainStamp::Shape::Count; ++s)
					if (ImGui::MenuItem(TerrainStamp::ShapeName((TerrainStamp::Shape)s))) add(GameObjectFactory::CreateTerrainStamp(s));
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Terrain Spline"))   // 곡선을 따라 길·협곡·능선 (지형 생성기)
			{
				for (int m = 0; m < (int)TerrainSpline::Mode::Count; ++m)
					if (ImGui::MenuItem(TerrainSpline::ModeName((TerrainSpline::Mode)m))) add(GameObjectFactory::CreateTerrainSpline(m));
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Water"))   // 바다·호수·강 (물 패키지 프로파일)
			{
				if (ImGui::MenuItem("Ocean")) add(GameObjectFactory::CreateWaterBody(0));
				if (ImGui::MenuItem("Lake")) add(GameObjectFactory::CreateWaterBody(1));
				if (ImGui::MenuItem("River")) add(GameObjectFactory::CreateWaterBody(2));
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Terrain Biome"))   // 영역마다 다른 지형 특성·재질 (바이옴 프리셋)
			{
				for (const auto& p : TerrainBiomes::List())
					if (ImGui::MenuItem(p.Name.c_str())) add(GameObjectFactory::CreateTerrainBiome(p.Name));
				ImGui::EndMenu();
			}
			Disabled("Wind Zone");
			ImGui::EndMenu();
		}

		SetMenuWidth(190.0f);
		if (ImGui::BeginMenu("Effects"))
		{
			if (ImGui::MenuItem("Particle System")) add(GameObjectFactory::CreateParticleSystem());
			Disabled("Particle System Force Field");
			if (ImGui::MenuItem("Visual Effect")) add(GameObjectFactory::CreateVisualEffect());
			if (ImGui::MenuItem("Trail")) add(GameObjectFactory::CreateLineEffect(true));
			if (ImGui::MenuItem("Line")) add(GameObjectFactory::CreateLineEffect(false));
			ImGui::EndMenu();
		}

		SetMenuWidth(190.0f);
		if (ImGui::BeginMenu("Light"))
		{
			if (ImGui::MenuItem("Directional Light")) add(GameObjectFactory::CreateDirectionalLight());
			if (ImGui::MenuItem("Point Light")) add(GameObjectFactory::CreatePointLight());
			if (ImGui::MenuItem("Spot Light")) add(GameObjectFactory::CreateSpotLight());
			Disabled("Area Light");
			ImGui::Separator();
			// 2D 빛 (Unity 6 의 Light > 2D)
			auto light2D = [&](const char* name, Light2D::Type t) {
				GameObject* o = GameObjectFactory::CreateEmpty(name);
				o->AddComponent<Light2D>()->LightType = t;
				add(o);
			};
			if (ImGui::MenuItem("Global Light 2D")) light2D("Global Light 2D", Light2D::Type::Global);
			if (ImGui::MenuItem("Spot Light 2D")) light2D("Spot Light 2D", Light2D::Type::Point);
			ImGui::Separator();
			if (ImGui::MenuItem("Reflection Probe")) { GameObject* o = GameObjectFactory::CreateEmpty("Reflection Probe"); o->AddComponent<ReflectionProbe>(); add(o); }
			if (ImGui::MenuItem("Adaptive Probe Volume")) { GameObject* o = GameObjectFactory::CreateEmpty("Adaptive Probe Volume"); o->AddComponent<AdaptiveProbeVolume>(); add(o); }
			ImGui::EndMenu();
		}

		SetMenuWidth(190.0f);
		if (ImGui::BeginMenu("Audio"))
		{
			if (ImGui::MenuItem("Audio Source")) add(GameObjectFactory::CreateAudioSource());
			Disabled("Audio Reverb Zone");
			ImGui::EndMenu();
		}
		DisabledSubMenu("Video", { "Video Player" });
		// Unity 6 의 GameObject > UI (Canvas): 캔버스가 없으면 Canvas + EventSystem 을 함께 만든다
		auto uiItems = [&]() {
			auto create = [&](const char* kind) { UISystem::Create(kind, scene, parent); };
			if (ImGui::MenuItem("Image")) create("Image");
			if (ImGui::MenuItem("Text")) create("Text");
			Disabled("Raw Image");
			if (ImGui::MenuItem("Panel")) create("Panel");
			if (ImGui::MenuItem("Toggle")) create("Toggle");
			if (ImGui::MenuItem("Slider")) create("Slider");
			if (ImGui::MenuItem("Scrollbar")) create("Scrollbar");
			if (ImGui::MenuItem("Scroll View")) create("ScrollView");
			if (ImGui::MenuItem("Button")) create("Button");
			if (ImGui::MenuItem("Dropdown")) create("Dropdown");
			if (ImGui::MenuItem("Input Field")) create("InputField");
			ImGui::Separator();
			if (ImGui::MenuItem("Canvas")) create("Canvas");
			if (ImGui::MenuItem("Event System")) create("EventSystem");
		};
		SetMenuWidth(190.0f);
		if (ImGui::BeginMenu("UI (Canvas)"))
		{
			uiItems();
			ImGui::EndMenu();
		}
		DisabledSubMenu("AI", { "Navigation" });
		DisabledSubMenu("UI Toolkit", { "UI Document" });
		SetMenuWidth(190.0f);
		if (ImGui::BeginMenu("Rendering"))
		{
			if (ImGui::MenuItem("Reflection Probe")) { GameObject* o = GameObjectFactory::CreateEmpty("Reflection Probe"); o->AddComponent<ReflectionProbe>(); add(o); }
			if (ImGui::MenuItem("URP Decal Projector")) { GameObject* o = GameObjectFactory::CreateEmpty("Decal Projector"); o->AddComponent<DecalProjector>(); add(o); }
			ImGui::EndMenu();
		}

		SetMenuWidth(190.0f);
		if (ImGui::BeginMenu("Volume"))
		{
			if (ImGui::MenuItem("Global Volume")) add(GameObjectFactory::CreateVolume(GameObjectFactory::VolumeShape::Global));
			if (ImGui::MenuItem("Box Volume")) add(GameObjectFactory::CreateVolume(GameObjectFactory::VolumeShape::Box));
			if (ImGui::MenuItem("Sphere Volume")) add(GameObjectFactory::CreateVolume(GameObjectFactory::VolumeShape::Sphere));
			Disabled("Convex Mesh Volume");
			ImGui::EndMenu();
		}

		if (ImGui::MenuItem("Camera"))
			add(GameObjectFactory::CreateCamera("Camera"));

		// Cinemachine (Cameras 패키지가 항목을 등록 — 없으면 회색)
		SetMenuWidth(190.0f);
		if (ImGui::BeginMenu("Cinemachine", !EditorExtensions::CreateMenuItems("Cinemachine").empty()))
		{
			PackageMenu("Cinemachine", scene, parent);
			ImGui::EndMenu();
		}

		Disabled("Visual Scripting Scene Variables");
		SetMenuWidth(190.0f);
		if (ImGui::BeginMenu("UI"))
		{
			uiItems();
			ImGui::EndMenu();
		}
		DisabledSubMenu("Navigation", { "NavMesh Surface", "NavMesh Modifier" });
	}
}
