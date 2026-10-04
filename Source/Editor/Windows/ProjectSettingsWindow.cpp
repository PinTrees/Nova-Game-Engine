#include "pch.h"
#include "ProjectSettingsWindow.h"
#include "UnityGUI.h"
#include "VolumeEditor.h"
#include "VolumeProfile.h"
#include "RenderPipelineSettings.h"
#include "BuildSettings.h"
#include "GraphicsSettings.h"
#include "TagsAndLayers.h"
#include "PhysicsSettings.h"
#include "Physics2DSettings.h"

namespace
{
	bool s_Open = false;
	bool s_FocusNext = false;
	std::string s_Category = "Graphics";

	const char* kCategories[] = { "Graphics", "Physics", "Physics 2D", "Player", "Tags and Layers" };

	// Unity 의 Graphics APIs for Windows 목록: 위가 우선. 선택한 줄을 위/아래로, + 로 추가, - 로 제거 (하나는 남긴다)
	bool DrawGraphicsApiList(std::vector<GraphicsAPI>& list)
	{
		static int s_Selected = 0;
		bool changed = false;
		s_Selected = std::clamp(s_Selected, 0, (int)list.size() - 1);
		UnityGUI::Label("Graphics APIs for Windows", 1);
		ImGui::Indent(18.0f);
		const float w = (std::min)(460.0f, ImGui::GetContentRegionAvail().x - 8.0f);
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.17f, 0.17f, 0.17f, 1.0f));
		ImGui::BeginChild("##graphicsApis", ImVec2(w, list.size() * 24.0f + 10.0f), true);
		bool anyUnsupported = false;
		for (int i = 0; i < (int)list.size(); ++i)
		{
			std::string reason;
			const bool ok = GraphicsSettings::IsSupported(list[i], &reason);
			anyUnsupported |= !ok;
			std::string label = std::string(GraphicsAPIToString(list[i])) + (ok ? (GraphicsSettings::IsExperimental(list[i]) ? "   (experimental)" : "") : "   (not available: " + reason + ")") + "##api" + std::to_string(i);
			if (!ok)
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.65f, 0.65f, 1.0f));
			if (ImGui::Selectable(label.c_str(), s_Selected == i))
				s_Selected = i;
			if (!ok)
				ImGui::PopStyleColor();
		}
		ImGui::EndChild();
		ImGui::PopStyleColor();
		// 버튼: ▲ ▼  -  +
		ImGui::BeginDisabled(s_Selected <= 0);
		if (ImGui::Button(ICON_FA_ARROW_UP "##apiUp", ImVec2(28, 0)))
		{
			std::swap(list[s_Selected], list[s_Selected - 1]);
			--s_Selected;
			changed = true;
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(s_Selected >= (int)list.size() - 1);
		if (ImGui::Button(ICON_FA_ARROW_DOWN "##apiDown", ImVec2(28, 0)))
		{
			std::swap(list[s_Selected], list[s_Selected + 1]);
			++s_Selected;
			changed = true;
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(list.size() <= 1);
		if (ImGui::Button(ICON_FA_MINUS "##apiRemove", ImVec2(28, 0)))
		{
			list.erase(list.begin() + s_Selected);
			s_Selected = (std::max)(0, s_Selected - 1);
			changed = true;
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		std::vector<GraphicsAPI> missing;
		for (GraphicsAPI api : GraphicsSettings::AllAPIs())
			if (std::find(list.begin(), list.end(), api) == list.end())
				missing.push_back(api);
		ImGui::BeginDisabled(missing.empty());
		if (ImGui::Button(ICON_FA_PLUS "##apiAdd", ImVec2(28, 0)))
			ImGui::OpenPopup("##addApi");
		ImGui::EndDisabled();
		if (ImGui::BeginPopup("##addApi"))
		{
			for (GraphicsAPI api : missing)
				if (ImGui::MenuItem(GraphicsAPIToString(api)))
				{
					list.push_back(api);
					s_Selected = (int)list.size() - 1;
					changed = true;
				}
			ImGui::EndPopup();
		}
		ImGui::Unindent(18.0f);
		if (anyUnsupported)
			UnityGUI::HelpBox("APIs that are not available on the player PC (or not finished in this engine version) are skipped: the game starts with the next API in the list.", false);
		return changed;
	}

	// Unity 의 Project Settings > Player (Windows 탭의 Resolution and Presentation 까지)
	void DrawPlayer()
	{
		ImGui::PushFont(UnityGUI::HeaderFont());
		ImGui::TextUnformatted("Player");
		ImGui::PopFont();
		ImGui::Spacing();
		BuildSettings::Player& p = BuildSettings::GetPlayer();
		bool changed = false;
		changed |= UnityGUI::TextField("Company Name", &p.CompanyName);
		std::string product = p.ProductName.empty() ? BuildSettings::ProductName() : p.ProductName;
		if (UnityGUI::TextField("Product Name", &product)) { p.ProductName = product; changed = true; }
		changed |= UnityGUI::TextField("Version", &p.Version);
		UnityGUI::ValueLabel("Default Icon", "NOVA logo (exe icon)");
		UnityGUI::Spacing(8.0f);
		ImGui::PushFont(UnityGUI::BoldFont());
		ImGui::TextUnformatted("Resolution and Presentation");
		ImGui::PopFont();
		static const char* kModes[] = { "Fullscreen Window", "Maximized Window", "Windowed" };
		int mode = (int)p.Mode;
		if (UnityGUI::Dropdown("Fullscreen Mode", &mode, kModes, 3)) { p.Mode = (BuildSettings::FullscreenMode)mode; changed = true; }
		if (p.Mode == BuildSettings::FullscreenMode::Windowed)
		{
			if (UnityGUI::Int("Default Screen Width", &p.Width, 1)) { p.Width = (std::max)(320, p.Width); changed = true; }
			if (UnityGUI::Int("Default Screen Height", &p.Height, 1)) { p.Height = (std::max)(240, p.Height); changed = true; }
		}
		if (p.Mode != BuildSettings::FullscreenMode::FullscreenWindow)
			changed |= UnityGUI::Toggle("Resizable Window", &p.Resizable);
		changed |= UnityGUI::Toggle("Run In Background", &p.RunInBackground);
		UnityGUI::Spacing(8.0f);
		ImGui::PushFont(UnityGUI::BoldFont());
		ImGui::TextUnformatted("Other Settings");
		ImGui::PopFont();
		UnityGUI::Label("Rendering", 0, true);
		changed |= UnityGUI::Toggle("Auto Graphics API for Windows", &p.AutoGraphicsAPI, 1);
		if (p.AutoGraphicsAPI)
			UnityGUI::HelpBox("Auto: the game uses DirectX 11 (like Unity). Turn Auto off and add OpenGL to the list to ship it too - the build then also includes the shader converter (about 23 MB).", false, 1);
		else
			changed |= DrawGraphicsApiList(p.GraphicsAPIs);
		// Android (Unity 의 Build Settings > Android > Texture Compression)
		UnityGUI::Label("Android", 0, true);
		static const char* kTc[] = { "ASTC", "ETC2 (GLES 3.0)", "DXT (BC)", "None (RGBA32)" };
		changed |= UnityGUI::Dropdown("Texture Compression", &p.AndroidTextureCompression, kTc, 4, 1);
		UnityGUI::HelpBox("Format for textures whose Android Format is Automatic (texture Import Settings > Android). ASTC = 6x6 (High Quality = 4x4). "
			"nova android export bakes them on the PC - phones have no image decoder or compressor.", false, 1);
		if (changed)
			BuildSettings::SavePlayer();
		UnityGUI::Spacing(6.0f);
		UnityGUI::HelpBox("The built game needs the .NET 8 (or newer) runtime on the target PC when the project uses C# scripts.", false);
	}

	void SectionTitle(const char* text)
	{
		ImFont* bold = UnityGUI::BoldFont();
		ImGui::PushFont(bold);
		ImGui::TextUnformatted(text);
		ImGui::PopFont();
	}

	// ---------------------------------------------------------------- Tags and Layers
	void DrawTagsAndLayers()
	{
		ImGui::PushFont(UnityGUI::HeaderFont());
		ImGui::TextUnformatted("Tags and Layers");
		ImGui::PopFont();
		ImGui::Spacing();

		if (UnityGUI::FoldoutPlain("Tags"))
		{
			const std::vector<std::string> tags = TagsAndLayers::Tags();
			for (size_t i = 0; i < tags.size(); ++i)
			{
				const bool builtin = TagsAndLayers::IsBuiltinTag(tags[i]);
				ImGui::PushID((int)i);
				ImGui::Indent(18.0f);
				ImGui::AlignTextToFramePadding();
				ImGui::TextColored(builtin ? ImVec4(0.6f, 0.6f, 0.6f, 1) : ImVec4(0.9f, 0.9f, 0.9f, 1), "Tag %d   %s", (int)i, tags[i].c_str());
				if (!builtin)
				{
					ImGui::SameLine(320.0f);
					if (ImGui::SmallButton(ICON_FA_MINUS))
						TagsAndLayers::RemoveTag(tags[i]);
				}
				ImGui::Unindent(18.0f);
				ImGui::PopID();
			}
			static char s_NewTag[64] = {};
			ImGui::Indent(18.0f);
			ImGui::SetNextItemWidth(220.0f);
			const bool enter = ImGui::InputTextWithHint("##newTag", "New tag name", s_NewTag, sizeof(s_NewTag), ImGuiInputTextFlags_EnterReturnsTrue);
			ImGui::SameLine();
			if ((ImGui::Button(ICON_FA_PLUS " Add Tag") || enter) && s_NewTag[0])
			{
				TagsAndLayers::AddTag(s_NewTag);
				s_NewTag[0] = 0;
			}
			ImGui::Unindent(18.0f);
			UnityGUI::Spacing(6.0f);
		}

		if (UnityGUI::FoldoutPlain("Sorting Layers"))
		{
			// 위 = 먼저 그림 (가장 뒤). 이름 바꾸기 · 위아래로 옮기기 · 지우기 (Default 는 지울 수 없다)
			static char s_SortNames[64][64];
			static int s_SortEditing = -1;
			const std::vector<TagsAndLayers::SortingLayer> list = TagsAndLayers::SortingLayers();
			for (int i = 0; i < (int)list.size() && i < 64; ++i)
			{
				const TagsAndLayers::SortingLayer& l = list[i];
				ImGui::PushID(1000 + l.Id);
				ImGui::Indent(18.0f);
				ImGui::AlignTextToFramePadding();
				ImGui::TextColored(ImVec4(0.75f, 0.75f, 0.75f, 1), "Layer %d", i);
				ImGui::SameLine(110.0f);
				ImGui::SetNextItemWidth(220.0f);
				if (s_SortEditing != l.Id)
					strncpy_s(s_SortNames[i], l.Name.c_str(), _TRUNCATE);
				ImGui::InputText("##sort", s_SortNames[i], sizeof(s_SortNames[i]));
				if (ImGui::IsItemActive())
					s_SortEditing = l.Id;
				if (ImGui::IsItemDeactivated())
				{
					TagsAndLayers::RenameSortingLayer(l.Id, s_SortNames[i]);
					s_SortEditing = -1;
				}
				ImGui::SameLine();
				ImGui::BeginDisabled(i == 0);
				if (ImGui::SmallButton(ICON_FA_ARROW_UP)) TagsAndLayers::MoveSortingLayer(l.Id, -1);
				ImGui::EndDisabled();
				ImGui::SameLine();
				ImGui::BeginDisabled(i + 1 >= (int)list.size());
				if (ImGui::SmallButton(ICON_FA_ARROW_DOWN)) TagsAndLayers::MoveSortingLayer(l.Id, +1);
				ImGui::EndDisabled();
				ImGui::SameLine();
				ImGui::BeginDisabled(l.Id == 0);
				if (ImGui::SmallButton(ICON_FA_MINUS)) TagsAndLayers::RemoveSortingLayer(l.Id);
				ImGui::EndDisabled();
				ImGui::Unindent(18.0f);
				ImGui::PopID();
			}
			static char s_NewSort[64] = {};
			ImGui::Indent(18.0f);
			ImGui::SetNextItemWidth(220.0f);
			const bool enter = ImGui::InputTextWithHint("##newSort", "New sorting layer", s_NewSort, sizeof(s_NewSort), ImGuiInputTextFlags_EnterReturnsTrue);
			ImGui::SameLine();
			if ((ImGui::Button(ICON_FA_PLUS " Add Sorting Layer") || enter) && s_NewSort[0])
			{
				TagsAndLayers::AddSortingLayer(s_NewSort);
				s_NewSort[0] = 0;
			}
			ImGui::TextDisabled("Sprites draw layer by layer from the top of this list (the last one is in front).");
			ImGui::Unindent(18.0f);
			UnityGUI::Spacing(6.0f);
		}

		if (UnityGUI::FoldoutPlain("Layers"))
		{
			// Builtin = 이름 고정 (회색), User Layer = 이름을 적어 쓴다 (Enter 또는 칸을 떠나면 저장)
			static char s_Names[TagsAndLayers::kLayerCount][64];
			static int s_Editing = -1;
			for (int i = 0; i < TagsAndLayers::kLayerCount; ++i)
			{
				ImGui::PushID(i);
				ImGui::Indent(18.0f);
				ImGui::AlignTextToFramePadding();
				const bool builtin = TagsAndLayers::IsBuiltinLayer(i);
				ImGui::TextColored(ImVec4(0.75f, 0.75f, 0.75f, 1), "%s %d", builtin ? "Builtin Layer" : "User Layer", i);
				ImGui::SameLine(170.0f);
				ImGui::SetNextItemWidth((std::min)(320.0f, ImGui::GetContentRegionAvail().x - 8.0f));
				if (builtin)
				{
					ImGui::BeginDisabled();
					char buf[64];
					strncpy_s(buf, TagsAndLayers::LayerName(i).c_str(), _TRUNCATE);
					ImGui::InputText("##layer", buf, sizeof(buf), ImGuiInputTextFlags_ReadOnly);
					ImGui::EndDisabled();
				}
				else
				{
					if (s_Editing != i)
						strncpy_s(s_Names[i], TagsAndLayers::LayerName(i).c_str(), _TRUNCATE);
					ImGui::InputText("##layer", s_Names[i], sizeof(s_Names[i]));
					if (ImGui::IsItemActive())
						s_Editing = i;
					if (ImGui::IsItemDeactivated())
					{
						if (!TagsAndLayers::SetLayerName(i, s_Names[i]))
							strncpy_s(s_Names[i], TagsAndLayers::LayerName(i).c_str(), _TRUNCATE);   // 같은 이름이 이미 있다
						s_Editing = -1;
					}
				}
				ImGui::Unindent(18.0f);
				ImGui::PopID();
			}
		}
	}

	// ---------------------------------------------------------------- Physics
	// 글자를 세로로 (아래 → 위로 읽게): 그린 정점을 pos 를 중심으로 -90° 돌린다
	void AddTextVertical(ImDrawList* dl, ImVec2 pos, ImU32 col, const char* text)
	{
		const int start = dl->VtxBuffer.Size;
		dl->AddText(pos, col, text);
		for (int i = start; i < dl->VtxBuffer.Size; ++i)
		{
			ImDrawVert& v = dl->VtxBuffer[i];
			const float dx = v.pos.x - pos.x, dy = v.pos.y - pos.y;
			v.pos = ImVec2(pos.x + dy, pos.y - dx);
		}
	}

	// Unity 의 Layer Collision Matrix: 왼쪽 = 레이어 (위 → 아래 번호 순), 위 = 레이어 (세로 글자, 오른쪽 → 왼쪽 번호 순), 삼각형
	// 3D (Physics) 와 2D (Physics 2D) 가 같은 그림을 쓴다
	struct MatrixOps
	{
		bool (*Get)(int, int);
		void (*Set)(int, int, bool);
		void (*All)(bool);
	};

	void DrawCollisionMatrix(const MatrixOps& ops)
	{
		const std::vector<int> layers = TagsAndLayers::NamedLayers();
		const int n = (int)layers.size();
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const float cell = 18.0f;
		float labelW = 0.0f;
		for (int id : layers)
			labelW = (std::max)(labelW, ImGui::CalcTextSize(TagsAndLayers::LayerName(id).c_str()).x);
		labelW += 28.0f;
		const float headerH = labelW - 10.0f;
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const ImVec2 grid(origin.x + labelW, origin.y + headerH);
		const float fh = ImGui::GetFontSize();
		// 위 머리글: 열 c = layers[n-1-c]
		for (int c = 0; c < n; ++c)
		{
			const char* name = TagsAndLayers::LayerName(layers[n - 1 - c]).c_str();
			AddTextVertical(dl, ImVec2(grid.x + c * cell + (cell - fh) * 0.5f, grid.y - 6.0f), IM_COL32(200, 200, 200, 255), name);
		}
		int hoverA = -1, hoverB = -1;
		for (int r = 0; r < n; ++r)
		{
			const int a = layers[r];
			const char* name = TagsAndLayers::LayerName(a).c_str();
			const float y = grid.y + r * cell;
			const ImVec2 ts = ImGui::CalcTextSize(name);
			dl->AddText(ImVec2(grid.x - ts.x - 10.0f, y + (cell - ts.y) * 0.5f), IM_COL32(200, 200, 200, 255), name);
			for (int c = 0; c < n - r; ++c)
			{
				const int b = layers[n - 1 - c];
				const ImVec2 p0(grid.x + c * cell + 2.0f, y + 2.0f), p1(p0.x + cell - 4.0f, p0.y + cell - 4.0f);
				ImGui::SetCursorScreenPos(p0);
				ImGui::PushID(a * 64 + b);
				if (ImGui::InvisibleButton("##m", ImVec2(cell - 4.0f, cell - 4.0f)))
					ops.Set(a, b, !ops.Get(a, b));
				const bool hovered = ImGui::IsItemHovered();
				ImGui::PopID();
				if (hovered) { hoverA = a; hoverB = b; }
				const bool on = ops.Get(a, b);
				dl->AddRectFilled(p0, p1, hovered ? IM_COL32(80, 80, 80, 255) : IM_COL32(56, 56, 56, 255), 2.0f);
				dl->AddRect(p0, p1, IM_COL32(30, 30, 30, 255), 2.0f);
				if (on)
				{
					// 체크 표시
					const ImVec2 a0(p0.x + 3.0f, p0.y + (p1.y - p0.y) * 0.5f), a1(p0.x + (p1.x - p0.x) * 0.42f, p1.y - 3.5f), a2(p1.x - 2.5f, p0.y + 3.0f);
					dl->AddLine(a0, a1, IM_COL32(220, 220, 220, 255), 2.0f);
					dl->AddLine(a1, a2, IM_COL32(220, 220, 220, 255), 2.0f);
				}
			}
		}
		if (hoverA >= 0)
			ImGui::SetTooltip("%s / %s", TagsAndLayers::LayerName(hoverA).c_str(), TagsAndLayers::LayerName(hoverB).c_str());
		ImGui::SetCursorScreenPos(ImVec2(origin.x, grid.y + n * cell + 8.0f));
		ImGui::Dummy(ImVec2(labelW + n * cell, 1.0f));
		if (ImGui::Button("Disable All"))
			ops.All(false);
		ImGui::SameLine();
		if (ImGui::Button("Enable All"))
			ops.All(true);
	}

	void DrawPhysics()
	{
		ImGui::PushFont(UnityGUI::HeaderFont());
		ImGui::TextUnformatted("Physics");
		ImGui::PopFont();
		ImGui::Spacing();
		Vec3 g = PhysicsSettings::Gravity();
		float gv[3] = { g.x, g.y, g.z };
		if (UnityGUI::Vector3("Gravity", gv))
			PhysicsSettings::SetGravity(Vec3(gv[0], gv[1], gv[2]));
		UnityGUI::Spacing(8.0f);
		if (UnityGUI::FoldoutPlain("Layer Collision Matrix"))
		{
			ImGui::Indent(18.0f);
			DrawCollisionMatrix({ PhysicsSettings::LayersCollide, [](int a, int b, bool c) { PhysicsSettings::SetLayersCollide(a, b, c); }, PhysicsSettings::SetAllCollide });
			ImGui::Unindent(18.0f);
			UnityGUI::HelpBox("Unchecked pairs of layers do not collide (triggers and Character Controllers too). The Include / Exclude Layers of a collider or Rigidbody override this matrix. Name more layers in Tags and Layers.", false);
		}
	}

	// Unity 의 Project Settings > Physics 2D (3D 와 따로: Box2D)
	void DrawPhysics2D()
	{
		ImGui::PushFont(UnityGUI::HeaderFont());
		ImGui::TextUnformatted("Physics 2D");
		ImGui::PopFont();
		ImGui::Spacing();
		Vec2 g = Physics2DSettings::Gravity();
		if (UnityGUI::Vector2Pair("Gravity", "X", &g.x, "Y", &g.y))
			Physics2DSettings::SetGravity(g);
		bool hit = Physics2DSettings::QueriesHitTriggers();
		if (UnityGUI::Toggle("Queries Hit Triggers", &hit))
			Physics2DSettings::SetQueriesHitTriggers(hit);
		UnityGUI::Spacing(8.0f);
		if (UnityGUI::FoldoutPlain("Layer Collision Matrix"))
		{
			ImGui::Indent(18.0f);
			DrawCollisionMatrix({ Physics2DSettings::LayersCollide, [](int a, int b, bool c) { Physics2DSettings::SetLayersCollide(a, b, c); }, Physics2DSettings::SetAllCollide });
			ImGui::Unindent(18.0f);
			UnityGUI::HelpBox("2D physics (Rigidbody 2D, Collider 2D) uses its own matrix, like Unity. Unchecked pairs do not collide or trigger.", false);
		}
	}

	void DrawGraphics()
	{
		ImGui::PushFont(UnityGUI::HeaderFont());
		ImGui::TextUnformatted("Graphics");
		ImGui::PopFont();
		ImGui::Spacing();

		SectionTitle("Pipeline Specific Settings");
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.72f, 0.72f, 0.72f, 1.0f));
		ImGui::TextWrapped("Set the default values of all the scenes within the project. These settings are used by the Universal Render Pipeline of this engine.");
		ImGui::PopStyleColor();
		ImGui::Spacing();

		SectionTitle("Volume");
		// Unity 처럼 기본 프로파일은 항상 있다: 없으면 Assets/Settings/DefaultVolumeProfile 을 만든다
		auto profile = RenderPipelineSettings::EnsureDefaultVolumeProfile();
		std::string path = RenderPipelineSettings::DefaultVolumeProfilePath();
		if (VolumeEditor::ProfileField("Default Profile", path, "DefaultVolumeProfile", false))
		{
			RenderPipelineSettings::SetDefaultVolumeProfilePath(path);
			profile = RenderPipelineSettings::DefaultVolumeProfile();
		}
		UnityGUI::HelpBox("The values in the Default Volume can be overridden by Volumes inside scenes.", false);
		if (profile == nullptr)
			return;

		// 기본 프로파일에는 모든 효과가 있어야 한다 (빠진 것은 기본값으로 채움)
		bool added = false;
		for (const std::string& type : VolumeComponent::Types())
			if (!profile->Has(type))
			{
				profile->Add(type);
				added = true;
			}
		if (added)
			profile->Save();

		UnityGUI::Spacing(6.0f);
		SectionTitle("Post-processing");
		VolumeEditor::DrawProfile(profile, true);
	}
}

namespace ProjectSettingsWindow
{
	void Close() { s_Open = false; }
	bool IsOpen() { return s_Open; }

	void Open(const char* category)
	{
		s_Open = true;
		s_FocusNext = true;
		if (category)
			s_Category = category;
		EditorLog::Write("App", "Project Settings opened (%s)", s_Category.c_str());
	}

	void Draw()
	{
		if (!s_Open)
			return;
		const ImGuiViewport* vp = ImGui::GetMainViewport();
		ImGui::SetNextWindowSize(ImVec2(920, 720), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
		if (s_FocusNext)
		{
			ImGui::SetNextWindowFocus();
			s_FocusNext = false;
		}
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.22f, 0.22f, 0.22f, 1.0f));
		if (!ImGui::Begin("Project Settings", &s_Open, ImGuiWindowFlags_NoCollapse))
		{
			ImGui::End();
			ImGui::PopStyleColor();
			return;
		}

		// 왼쪽: 분류 목록
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
		ImGui::BeginChild("##psCategories", ImVec2(190, 0), true);
		for (const char* c : kCategories)
			if (ImGui::Selectable(c, s_Category == c))
				s_Category = c;
		ImGui::EndChild();
		ImGui::PopStyleColor();

		ImGui::SameLine();
		ImGui::BeginChild("##psBody", ImVec2(0, 0), false);
		if (s_Category == "Graphics")
			DrawGraphics();
		else if (s_Category == "Player")
			DrawPlayer();
		else if (s_Category == "Physics")
			DrawPhysics();
		else if (s_Category == "Physics 2D")
			DrawPhysics2D();
		else if (s_Category == "Tags and Layers")
			DrawTagsAndLayers();
		ImGui::EndChild();

		ImGui::End();
		ImGui::PopStyleColor();
	}
}
