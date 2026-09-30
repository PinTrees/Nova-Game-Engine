#include "pch.h"
#include "ParticleSystem.h"
#include "ParticleSystemEditor.h"
#include "ParticleTextures.h"
#include "UnityGUI.h"
#include "ObjectPicker.h"
#include "UISprites.h"

using namespace ParticleSystemEditor;

namespace
{
	bool Vec3Row(const char* label, Vec3* v, int indent = 0)
	{
		float xyz[3] = { v->x, v->y, v->z };
		if (!UnityGUI::Vector3(label, xyz, false, indent))
			return false;
		*v = Vec3(xyz[0], xyz[1], xyz[2]);
		return true;
	}

	bool EnumRow(const char* label, int* value, const char* const* items, int count, int indent = 0)
	{
		return UnityGUI::Dropdown(label, value, items, count, indent);
	}
}

// ------------------------------------------------------------------ Inspector
void ParticleSystem::OnInspectorGUI()
{
	// Unity: 컴포넌트 머리글 아래에 시스템 이름 띠 → 모듈 목록 (Main 은 오브젝트 이름)
	ImGui::PushID(this);
	DrawMainModule();
	DrawModules();
	ImGui::PopID();
}

void ParticleSystem::DrawMainModule()
{
	const std::string title = m_pGameObject ? m_pGameObject->GetName() : std::string("Particle System");
	if (!ModuleHeader("##main", title.c_str(), nullptr, true))
		return;
	if (UnityGUI::Float("Duration", &Duration))
		Duration = (std::max)(0.05f, Duration);
	UnityGUI::Toggle("Looping", &Looping);
	if (Looping)
		UnityGUI::Toggle("Prewarm", &Prewarm);
	CurveField("Start Delay", &StartDelay, 0, false);
	CurveField("Start Lifetime", &StartLifetime);
	CurveField("Start Speed", &StartSpeed);
	CurveField("Start Size", &StartSize);
	CurveField("Start Rotation", &StartRotation);
	if (UnityGUI::Slider("Flip Rotation", &FlipRotation, 0.0f, 1.0f))
		FlipRotation = std::clamp(FlipRotation, 0.0f, 1.0f);
	GradientField("Start Color", &StartColor);
	CurveField("Gravity Modifier", &GravityModifier);
	static const char* kSpace[] = { "Local", "World" };
	EnumRow("Simulation Space", &SimulationSpace, kSpace, 2);
	if (UnityGUI::Float("Simulation Speed", &SimulationSpeed))
		SimulationSpeed = (std::max)(0.0f, SimulationSpeed);
	UnityGUI::Toggle("Play On Awake*", &PlayOnAwake);
	if (UnityGUI::Int("Max Particles", &MaxParticles))
		MaxParticles = std::clamp(MaxParticles, 0, 100000);
	UnityGUI::Toggle("Auto Random Seed", &AutoRandomSeed);
	if (!AutoRandomSeed)
	{
		int seed = (int)RandomSeed;
		if (UnityGUI::Int("Random Seed", &seed))
			RandomSeed = (uint32)seed;
	}
	static const char* kStop[] = { "None", "Disable", "Destroy" };
	int stop = (int)OnStop;
	if (EnumRow("Stop Action", &stop, kStop, 3))
		OnStop = (StopAction)stop;
	ModuleEnd();
}

void ParticleSystem::DrawModules()
{
	// ---- Emission
	if (ModuleHeader("##emission", "Emission", &EmissionEnabled, true))
	{
		CurveField("Rate over Time", &RateOverTime);
		CurveField("Rate over Distance", &RateOverDistance);
		UnityGUI::Spacing(2.0f);
		UnityGUI::Label("Bursts");
		// 표: Time | Count | Cycles | Interval | Probability
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const float w = ImGui::GetContentRegionAvail().x;
		const float x0 = p.x + UnityGUI::kBaseIndent, x1 = p.x + w - 12.0f;
		const float colW = (x1 - x0) / 5.0f;
		ImDrawList* dl = ImGui::GetWindowDrawList();
		static const char* kCols[] = { "Time", "Count", "Cycles", "Interval", "Probability" };
		dl->AddRectFilled(ImVec2(x0, p.y), ImVec2(x1, p.y + 18.0f), IM_COL32(50, 50, 50, 255), 2.0f);
		for (int c = 0; c < 5; ++c)
			dl->AddText(ImVec2(x0 + colW * c + 4.0f, p.y + 2.0f), IM_COL32(196, 196, 196, 255), kCols[c]);
		float y = p.y + 20.0f;
		int remove = -1;
		for (int i = 0; i < (int)Bursts.size(); ++i)
		{
			Burst& b = Bursts[i];
			ImGui::PushID(i);
			UnityGUI::FloatBox("##t", &b.Time, ImVec2(x0, y), colW - 4.0f);
			float count = b.Count.ConstantMax;
			if (UnityGUI::FloatBox("##n", &count, ImVec2(x0 + colW, y), colW - 4.0f))
				b.Count = MinMaxCurve((std::max)(0.0f, floorf(count + 0.5f)));
			ImGui::SetCursorScreenPos(ImVec2(x0 + colW * 2, y));
			ImGui::SetNextItemWidth(colW - 4.0f);
			// Cycles: 0 = Infinite
			if (b.Cycles <= 0)
			{
				if (ImGui::Button("Infinite", ImVec2(colW - 4.0f, 18.0f)))
					b.Cycles = 1;
			}
			else
			{
				float cycles = (float)b.Cycles;
				if (UnityGUI::FloatBox("##cy", &cycles, ImVec2(x0 + colW * 2, y), colW - 4.0f))
					b.Cycles = (std::max)(0, (int)cycles);
			}
			if (UnityGUI::FloatBox("##iv", &b.Interval, ImVec2(x0 + colW * 3, y), colW - 4.0f))
				b.Interval = (std::max)(0.01f, b.Interval);
			if (UnityGUI::FloatBox("##pr", &b.Probability, ImVec2(x0 + colW * 4, y), colW - 22.0f))
				b.Probability = std::clamp(b.Probability, 0.0f, 1.0f);
			ImGui::SetCursorScreenPos(ImVec2(x1 - 16.0f, y));
			if (ImGui::InvisibleButton("##del", ImVec2(16, 18)))
				remove = i;
			dl->AddText(ImVec2(x1 - 12.0f, y + 1.0f), ImGui::IsItemHovered() ? IM_COL32_WHITE : IM_COL32(160, 160, 160, 255), "x");
			ImGui::PopID();
			y += 20.0f;
		}
		if (remove >= 0)
			Bursts.erase(Bursts.begin() + remove);
		ImGui::SetCursorScreenPos(ImVec2(p.x, y));
		ImGui::Dummy(ImVec2(w, 2.0f));
		bool plus = false, minus = false;
		UnityGUI::PlusMinus(&plus, &minus);
		if (plus)
		{
			Burst b;
			if (!Bursts.empty())
				b.Time = Bursts.back().Time + 1.0f;
			Bursts.push_back(b);
		}
		if (minus && !Bursts.empty())
			Bursts.pop_back();
		ModuleEnd();
	}

	// ---- Shape
	if (ModuleHeader("##shape", "Shape", &ShapeEnabled, true))
	{
		static const char* kShapes[] = { "Sphere", "Hemisphere", "Cone", "Box", "Circle", "Edge" };
		int shape = (int)Shape;
		if (EnumRow("Shape", &shape, kShapes, 6))
			Shape = (ShapeType)shape;
		if (Shape == ShapeType::Cone)
		{
			if (UnityGUI::Float("Angle", &ShapeAngle))
				ShapeAngle = std::clamp(ShapeAngle, 0.0f, 90.0f);
		}
		if (Shape != ShapeType::Box)
		{
			if (UnityGUI::Float("Radius", &ShapeRadius))
				ShapeRadius = (std::max)(0.0001f, ShapeRadius);
		}
		if (Shape != ShapeType::Box && Shape != ShapeType::Edge)
		{
			if (UnityGUI::Slider("Radius Thickness", &ShapeRadiusThickness, 0.0f, 1.0f))
				ShapeRadiusThickness = std::clamp(ShapeRadiusThickness, 0.0f, 1.0f);
		}
		if (Shape == ShapeType::Cone || Shape == ShapeType::Circle)
		{
			if (UnityGUI::Slider("Arc", &ShapeArc, 0.0f, 360.0f))
				ShapeArc = std::clamp(ShapeArc, 0.0f, 360.0f);
		}
		if (Shape == ShapeType::Cone)
		{
			if (UnityGUI::Float("Length", &ShapeLength))
				ShapeLength = (std::max)(0.0f, ShapeLength);
			static const char* kFrom[] = { "Base", "Volume" };
			EnumRow("Emit from:", &ConeEmitFrom, kFrom, 2);
		}
		UnityGUI::Spacing(4.0f);
		Vec3Row("Position", &ShapePosition);
		Vec3Row("Rotation", &ShapeRotation);
		Vec3Row("Scale", &ShapeScale);
		UnityGUI::Spacing(4.0f);
		if (UnityGUI::Slider("Randomize Direction", &RandomizeDirection, 0.0f, 1.0f))
			RandomizeDirection = std::clamp(RandomizeDirection, 0.0f, 1.0f);
		if (UnityGUI::Slider("Spherize Direction", &SpherizeDirection, 0.0f, 1.0f))
			SpherizeDirection = std::clamp(SpherizeDirection, 0.0f, 1.0f);
		ModuleEnd();
	}

	static const char* kSpace[] = { "Local", "World" };

	// ---- Velocity over Lifetime
	if (ModuleHeader("##velocity", "Velocity over Lifetime", &VelocityEnabled))
	{
		UnityGUI::Label("Linear");
		CurveField("X", &VelocityX, 1);
		CurveField("Y", &VelocityY, 1);
		CurveField("Z", &VelocityZ, 1);
		EnumRow("Space", &VelocitySpace, kSpace, 2);
		CurveField("Speed Modifier", &SpeedModifier);
		ModuleEnd();
	}

	// ---- Limit Velocity over Lifetime
	if (ModuleHeader("##limit", "Limit Velocity over Lifetime", &LimitEnabled))
	{
		CurveField("Speed", &LimitSpeed);
		if (UnityGUI::Slider("Dampen", &LimitDampen, 0.0f, 1.0f))
			LimitDampen = std::clamp(LimitDampen, 0.0f, 1.0f);
		CurveField("Drag", &LimitDrag);
		ModuleEnd();
	}

	ModuleHeader("##inherit", "Inherit Velocity", nullptr, false, false);
	ModuleHeader("##lifetimeByEmitter", "Lifetime by Emitter Speed", nullptr, false, false);

	// ---- Force over Lifetime
	if (ModuleHeader("##force", "Force over Lifetime", &ForceEnabled))
	{
		CurveField("X", &ForceX);
		CurveField("Y", &ForceY);
		CurveField("Z", &ForceZ);
		EnumRow("Space", &ForceSpace, kSpace, 2);
		ModuleEnd();
	}

	// ---- Color over Lifetime
	if (ModuleHeader("##color", "Color over Lifetime", &ColorEnabled))
	{
		if (ColorOverLifetime.Mode != ParticleGradientMode::Gradient && ColorOverLifetime.Mode != ParticleGradientMode::TwoGradients)
			ColorOverLifetime.Mode = ParticleGradientMode::Gradient;
		GradientField("Color", &ColorOverLifetime, 0, false);
		ModuleEnd();
	}
	ModuleHeader("##colorBySpeed", "Color by Speed", nullptr, false, false);

	// ---- Size over Lifetime
	if (ModuleHeader("##size", "Size over Lifetime", &SizeEnabled))
	{
		CurveField("Size", &SizeOverLifetime);
		ModuleEnd();
	}
	ModuleHeader("##sizeBySpeed", "Size by Speed", nullptr, false, false);

	// ---- Rotation over Lifetime
	if (ModuleHeader("##rotation", "Rotation over Lifetime", &RotationEnabled))
	{
		CurveField("Angular Velocity", &AngularVelocity);
		ModuleEnd();
	}
	ModuleHeader("##rotationBySpeed", "Rotation by Speed", nullptr, false, false);
	ModuleHeader("##externalForces", "External Forces", nullptr, false, false);

	// ---- Noise
	if (ModuleHeader("##noise", "Noise", &NoiseEnabled))
	{
		CurveField("Strength", &NoiseStrength);
		if (UnityGUI::Float("Frequency", &NoiseFrequency))
			NoiseFrequency = (std::max)(0.0001f, NoiseFrequency);
		CurveField("Scroll Speed", &NoiseScrollSpeed);
		UnityGUI::Toggle("Damping", &NoiseDamping);
		if (UnityGUI::Int("Octaves", &NoiseOctaves))
			NoiseOctaves = std::clamp(NoiseOctaves, 1, 4);
		if (NoiseOctaves > 1)
		{
			if (UnityGUI::Slider("Octave Multiplier", &NoiseOctaveMultiplier, 0.0f, 1.0f, 1))
				NoiseOctaveMultiplier = std::clamp(NoiseOctaveMultiplier, 0.0f, 1.0f);
			if (UnityGUI::Slider("Octave Scale", &NoiseOctaveScale, 1.0f, 4.0f, 1))
				NoiseOctaveScale = std::clamp(NoiseOctaveScale, 1.0f, 4.0f);
		}
		ModuleEnd();
	}

	ModuleHeader("##collision", "Collision", nullptr, false, false);
	ModuleHeader("##triggers", "Triggers", nullptr, false, false);
	ModuleHeader("##subEmitters", "Sub Emitters", nullptr, false, false);

	// ---- Texture Sheet Animation
	if (ModuleHeader("##sheet", "Texture Sheet Animation", &SheetEnabled))
	{
		UnityGUI::ValueLabel("Mode", "Grid");
		float tx = (float)SheetTilesX, ty = (float)SheetTilesY;
		if (UnityGUI::Vector2Pair("Tiles", "X", &tx, "Y", &ty))
		{
			SheetTilesX = std::clamp((int)tx, 1, 64);
			SheetTilesY = std::clamp((int)ty, 1, 64);
		}
		static const char* kAnim[] = { "Whole Sheet", "Single Row" };
		EnumRow("Animation", &SheetAnimation, kAnim, 2);
		if (SheetAnimation == 1)
		{
			UnityGUI::Toggle("Random Row", &SheetRandomRow, 1);
			if (!SheetRandomRow && UnityGUI::Int("Row", &SheetRowIndex, 1))
				SheetRowIndex = std::clamp(SheetRowIndex, 0, SheetTilesY - 1);
		}
		CurveField("Frame over Time", &SheetFrameOverTime);
		CurveField("Start Frame", &SheetStartFrame);
		if (UnityGUI::Float("Cycles", &SheetCycles))
			SheetCycles = (std::max)(0.0001f, SheetCycles);
		ModuleEnd();
	}

	ModuleHeader("##lights", "Lights", nullptr, false, false);
	ModuleHeader("##trails", "Trails", nullptr, false, false);
	ModuleHeader("##customData", "Custom Data", nullptr, false, false);

	// ---- Renderer
	if (ModuleHeader("##renderer", "Renderer", &RendererEnabled, true))
	{
		static const char* kModes[] = { "Billboard", "Stretched Billboard", "Horizontal Billboard", "Vertical Billboard" };
		int mode = (int)Render;
		if (EnumRow("Render Mode", &mode, kModes, 4))
			Render = (RenderMode)mode;
		if (Render == RenderMode::StretchedBillboard)
		{
			UnityGUI::Float("Speed Scale", &SpeedScale, 1);
			UnityGUI::Float("Length Scale", &LengthScale, 1);
		}
		// Texture [ 이름 ⊙ ] (⊙ = 선택 창, Project 의 이미지를 끌어 놓기)
		{
			const std::string text = Texture.empty() ? "None (Texture 2D)" : ParticleTextures::DisplayName(Texture);
			ImVec2 fmin, fmax;
			const int pressed = UnityGUI::ObjectFieldButtons("Texture", text.c_str(), "texture", nullptr, 0, &fmin, &fmax);
			const ImVec2 after = ImGui::GetCursorScreenPos();
			const std::string key = "particleTex:" + std::to_string((uintptr_t)this);
			if (pressed == -1)
			{
				ObjectPicker::Options opt;
				opt.TypeName = "Texture";
				opt.Icon = "texture";
				opt.Items = ParticleTextures::FindAll();
				opt.Current = Texture;
				opt.Describe = [](const std::string& p) {
					return ParticleTextures::IsBuiltin(p) ? std::string("Built-in particle texture") : p;
				};
				ObjectPicker::Open(key, std::move(opt));
			}
			std::string picked;
			if (ObjectPicker::Poll(key, picked))
			{
				Texture = picked;
				// 불꽃 플립북을 고르면 4 x 4 칸으로 (Unity 에서 시트 텍스처를 쓸 때 하는 설정)
				if (Texture == "builtin:Flame-Sheet")
				{
					SheetEnabled = true;
					SheetTilesX = SheetTilesY = 4;
				}
			}
			ImGui::SetCursorScreenPos(fmin);
			ImGui::InvisibleButton("##texDrop", ImVec2((std::max)(1.0f, fmax.x - fmin.x - 22.0f), fmax.y - fmin.y));
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_FILE"))
				{
					std::string dropped(static_cast<const char*>(payload->Data));
					const std::string root = wstring_to_string(PathManager::GetI()->GetContentPathW());
					if (_strnicmp(dropped.c_str(), root.c_str(), root.size()) == 0)
						dropped = dropped.substr(root.size());
					if (UISprites::IsImagePath(dropped))
						Texture = dropped;
				}
				ImGui::EndDragDropTarget();
			}
			ImGui::SetCursorScreenPos(after);
		}
		static const char* kBlend[] = { "Alpha Blended", "Additive" };
		int blend = (int)Blend;
		if (EnumRow("Blend Mode", &blend, kBlend, 2))
			Blend = (BlendMode)blend;
		static const char* kSort[] = { "None", "By Distance", "Oldest in Front", "Youngest in Front" };
		int sort = (int)Sort;
		if (EnumRow("Sort Mode", &sort, kSort, 4))
			Sort = (SortMode)sort;
		UnityGUI::Float("Sorting Fudge", &SortingFudge);
		ModuleEnd();
	}

	// 에디터 미리보기 상태 (Unity 는 Scene 뷰 창에 표시)
	UnityGUI::Spacing(4.0f);
	char buf[64];
	snprintf(buf, sizeof(buf), "%d  (%s)", ParticleCount(), IsPlaying() ? "Playing" : IsPaused() ? "Paused" : IsAlive() ? "Stopping" : "Stopped");
	UnityGUI::ValueLabel("Particles", buf);
}
