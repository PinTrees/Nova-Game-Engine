#include "pch.h"
#include "ParticleSystem.h"
#include "ParticleSystemEditor.h"
#include "ParticleTextures.h"
#include "UnityGUI.h"
#include "ObjectPicker.h"
#include "UISprites.h"
#include "GameObjectFactory.h"

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

	// 텍스처 [ 이름 ⊙ ] (⊙ = 선택 창, Project 의 이미지를 끌어 놓기). 바뀌면 true
	bool TextureField(const char* label, std::string* value, const std::string& key)
	{
		bool changed = false;
		const std::string text = value->empty() ? "None (Texture 2D)" : ParticleTextures::DisplayName(*value);
		ImVec2 fmin, fmax;
		const int pressed = UnityGUI::ObjectFieldButtons(label, text.c_str(), "texture", nullptr, 0, &fmin, &fmax);
		const ImVec2 after = ImGui::GetCursorScreenPos();
		if (pressed == -1)
		{
			ObjectPicker::Options opt;
			opt.TypeName = "Texture";
			opt.Icon = "texture";
			opt.Items = ParticleTextures::FindAll();
			opt.Current = *value;
			opt.Describe = [](const std::string& p) {
				return ParticleTextures::IsBuiltin(p) ? std::string("Built-in particle texture") : p;
			};
			ObjectPicker::Open(key, std::move(opt));
		}
		std::string picked;
		if (ObjectPicker::Poll(key, picked))
		{
			*value = picked;
			changed = true;
		}
		ImGui::PushID(label);
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
				{
					*value = dropped;
					changed = true;
				}
			}
			ImGui::EndDragDropTarget();
		}
		ImGui::PopID();
		ImGui::SetCursorScreenPos(after);
		return changed;
	}
}
namespace
{
	const ImU32 kDim = IM_COL32(140, 140, 140, 255);

	// 오브젝트 칸 모양 (이름 + 아이콘). 반환: GAME_OBJECT 를 끌어 놓았으면 그 오브젝트
	GameObject* ObjectSlot(const char* id, const char* text, ImVec2 pos, float width, bool missing)
	{
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 b(pos.x + width, pos.y + UnityGUI::kRowHeight);
		dl->AddRectFilled(pos, b, IM_COL32(42, 42, 42, 255), 3.0f);
		dl->AddRect(pos, b, IM_COL32(26, 26, 26, 255), 3.0f);
		UnityGUI::DrawIcon(dl, "gameobject", ImVec2(pos.x + 3.0f, pos.y + 1.0f), 16.0f);
		dl->AddText(ImVec2(pos.x + 22.0f, pos.y + 2.0f), missing ? IM_COL32(220, 110, 110, 255) : IM_COL32(230, 230, 230, 255), text);
		ImGui::SetCursorScreenPos(pos);
		ImGui::InvisibleButton(id, ImVec2(width, UnityGUI::kRowHeight));
		GameObject* dropped = nullptr;
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("GAME_OBJECT"))
				dropped = *static_cast<GameObject* const*>(p->Data);
			ImGui::EndDragDropTarget();
		}
		return dropped;
	}

	bool RemoveButton(const char* id, ImVec2 pos)
	{
		ImGui::SetCursorScreenPos(pos);
		const bool clicked = ImGui::InvisibleButton(id, ImVec2(16, UnityGUI::kRowHeight));
		ImGui::GetWindowDrawList()->AddText(ImVec2(pos.x + 4.0f, pos.y + 1.0f), ImGui::IsItemHovered() ? IM_COL32_WHITE : kDim, "x");
		return clicked;
	}

	// 새 자식 GameObject (부모 원점, 월드 회전 없음)
	GameObject* CreateChild(GameObject* parent, const std::string& name)
	{
		GameObject* go = GameObjectFactory::CreateEmpty(name);
		go->SetParent(parent, false);
		go->GetTransform()->SetRotation(Quaternion::Identity);
		return go;
	}

	void DrawCollision(ParticleSystem& ps)
	{
		if (!ModuleHeader("##collision", "Collision", &ps.CollisionEnabled))
			return;
		static const char* kTypes[] = { "Planes", "World" };
		int type = (int)ps.CollisionMode;
		if (UnityGUI::Dropdown("Type", &type, kTypes, 2))
			ps.CollisionMode = (ParticleSystem::CollisionType)type;
		if (ps.CollisionMode == ParticleSystem::CollisionType::Planes)
		{
			UnityGUI::Label("Planes");
			int remove = -1;
			for (int i = 0; i < (int)ps.CollisionPlanes.size(); ++i)
			{
				ImGui::PushID(i);
				UnityGUI::FieldRow row = UnityGUI::BeginFieldRow(("  Plane " + std::to_string(i)).c_str(), 1);
				GameObject* go = ps.FindSceneObject(ps.CollisionPlanes[i]);
				if (GameObject* dropped = ObjectSlot("##plane", go ? go->GetName().c_str() : "Missing (Transform)", ImVec2(row.fieldX, row.p.y), row.fieldW - 20.0f, go == nullptr))
					ps.CollisionPlanes[i] = dropped->GetFileID();
				if (RemoveButton("##rm", ImVec2(row.fieldX + row.fieldW - 16.0f, row.p.y)))
					remove = i;
				UnityGUI::EndFieldRow(row);
				ImGui::PopID();
			}
			if (remove >= 0)
				ps.CollisionPlanes.erase(ps.CollisionPlanes.begin() + remove);
			bool plus = false, minus = false;
			UnityGUI::PlusMinus(&plus, &minus);
			if (plus && ps.GetGameObject())
			{
				// Unity 처럼 + 는 새 평면 오브젝트를 자식으로 만든다 (위쪽 = 월드 +Y)
				GameObject* plane = CreateChild(ps.GetGameObject(), "Collision Plane " + std::to_string(ps.CollisionPlanes.size()));
				ps.CollisionPlanes.push_back(plane->GetFileID());
			}
			if (minus && !ps.CollisionPlanes.empty())
				ps.CollisionPlanes.pop_back();
			if (UnityGUI::Float("Visualization Size", &ps.CollisionPlaneGizmoSize))
				ps.CollisionPlaneGizmoSize = (std::max)(0.1f, ps.CollisionPlaneGizmoSize);
		}
		else
			UnityGUI::HelpBox("World: Collider 가 있는 물체에 부딪힙니다 (물리가 도는 Play 모드에서만).", false);
		CurveField("Dampen", &ps.CollisionDampen);
		CurveField("Bounce", &ps.CollisionBounce);
		CurveField("Lifetime Loss", &ps.CollisionLifetimeLoss);
		UnityGUI::Float("Min Kill Speed", &ps.CollisionMinKillSpeed);
		UnityGUI::Float("Max Kill Speed", &ps.CollisionMaxKillSpeed);
		if (UnityGUI::Float("Radius Scale", &ps.CollisionRadiusScale))
			ps.CollisionRadiusScale = (std::max)(0.0f, ps.CollisionRadiusScale);
		ModuleEnd();
	}

	void DrawSubEmitters(ParticleSystem& ps)
	{
		if (!ModuleHeader("##subEmitters", "Sub Emitters", &ps.SubEmittersEnabled))
			return;
		// 대상 후보: 자식(손자 포함)의 Particle System
		std::vector<ParticleSystem*> children;
		ps.CollectHierarchy(children);
		children.erase(children.begin());
		static const char* kTypes[] = { "Birth", "Collision", "Death" };
		int remove = -1;
		for (int i = 0; i < (int)ps.SubEmitters.size(); ++i)
		{
			ParticleSystem::SubEmitter& s = ps.SubEmitters[i];
			ImGui::PushID(i);
			if (i > 0)
				UnityGUI::Spacing(4.0f);
			int type = (int)s.Type;
			if (UnityGUI::Dropdown("Type", &type, kTypes, 3))
				s.Type = (ParticleSystem::SubEmitterType)type;
			// Emitter: 자식 Particle System 목록
			{
				UnityGUI::FieldRow row = UnityGUI::BeginFieldRow("Emitter");
				GameObject* go = ps.FindSceneObject(s.Target);
				const std::string text = go ? go->GetName() : "None (Particle System)";
				ImGui::SetCursorScreenPos(ImVec2(row.fieldX, row.p.y));
				ImGui::SetNextItemWidth(row.fieldW - 20.0f);
				if (ImGui::BeginCombo("##emitter", text.c_str()))
				{
					for (ParticleSystem* c : children)
						if (ImGui::Selectable(c->GetGameObject()->GetName().c_str(), c->GetGameObject() == go))
							s.Target = c->GetGameObject()->GetFileID();
					if (children.empty())
						ImGui::TextDisabled("(no child Particle System)");
					ImGui::EndCombo();
				}
				if (RemoveButton("##rm", ImVec2(row.fieldX + row.fieldW - 16.0f, row.p.y)))
					remove = i;
				UnityGUI::EndFieldRow(row);
			}
			// Inherit: Color / Size / Rotation (여러 개)
			{
				UnityGUI::FieldRow row = UnityGUI::BeginFieldRow("Inherit");
				std::string text;
				if (s.Inherit == 0) text = "Nothing";
				else
				{
					if (s.Inherit & ParticleSystem::InheritColor) text += "Color ";
					if (s.Inherit & ParticleSystem::InheritSize) text += "Size ";
					if (s.Inherit & ParticleSystem::InheritRotation) text += "Rotation";
				}
				ImGui::SetCursorScreenPos(ImVec2(row.fieldX, row.p.y));
				ImGui::SetNextItemWidth(row.fieldW);
				if (ImGui::BeginCombo("##inherit", text.c_str()))
				{
					auto flag = [&](const char* name, int bit) {
						bool on = (s.Inherit & bit) != 0;
						if (ImGui::MenuItem(name, nullptr, on, true))
							s.Inherit ^= bit;
					};
					flag("Color", ParticleSystem::InheritColor);
					flag("Size", ParticleSystem::InheritSize);
					flag("Rotation", ParticleSystem::InheritRotation);
					ImGui::EndCombo();
				}
				UnityGUI::EndFieldRow(row);
			}
			if (UnityGUI::Slider("Emit Probability", &s.Probability, 0.0f, 1.0f))
				s.Probability = std::clamp(s.Probability, 0.0f, 1.0f);
			ImGui::PopID();
		}
		if (remove >= 0)
			ps.SubEmitters.erase(ps.SubEmitters.begin() + remove);
		bool plus = false, minus = false;
		UnityGUI::PlusMinus(&plus, &minus);
		if (plus && ps.GetGameObject())
		{
			// + : 불꽃처럼 한 번에 터지는 자식 Particle System 을 만들어 Death 로 연결
			GameObject* child = GameObjectFactory::CreateEmpty("SubEmitter" + std::to_string(ps.SubEmitters.size()));
			ParticleSystem* sub = child->AddComponent<ParticleSystem>();
			sub->PlayOnAwake = false;
			sub->Looping = false;
			sub->Duration = 1.0f;
			sub->RateOverTime = MinMaxCurve(0.0f);
			sub->Bursts = { ParticleSystem::Burst() };
			sub->StartLifetime.Mode = ParticleCurveMode::TwoConstants; sub->StartLifetime.ConstantMin = 0.5f; sub->StartLifetime.ConstantMax = 1.2f;
			sub->StartSpeed.Mode = ParticleCurveMode::TwoConstants; sub->StartSpeed.ConstantMin = 2.0f; sub->StartSpeed.ConstantMax = 5.0f;
			sub->StartSize.Mode = ParticleCurveMode::TwoConstants; sub->StartSize.ConstantMin = 0.05f; sub->StartSize.ConstantMax = 0.15f;
			sub->GravityModifier = MinMaxCurve(0.5f);
			sub->SimulationSpace = 1;
			sub->Shape = ParticleSystem::ShapeType::Sphere;
			sub->ShapeRadius = 0.1f;
			sub->Texture = "builtin:Glow";
			sub->Blend = ParticleSystem::BlendMode::Additive;
			sub->ColorEnabled = true;
			child->SetParent(ps.GetGameObject(), false);
			ParticleSystem::SubEmitter s;
			s.Target = child->GetFileID();
			ps.SubEmitters.push_back(s);
		}
		if (minus && !ps.SubEmitters.empty())
			ps.SubEmitters.pop_back();
		ModuleEnd();
	}

	void DrawTrails(ParticleSystem& ps)
	{
		if (!ModuleHeader("##trails", "Trails", &ps.TrailsEnabled))
			return;
		UnityGUI::ValueLabel("Mode", "Particles");
		if (UnityGUI::Slider("Ratio", &ps.TrailRatio, 0.0f, 1.0f))
			ps.TrailRatio = std::clamp(ps.TrailRatio, 0.0f, 1.0f);
		CurveField("Lifetime", &ps.TrailLifetime, 0, false);
		if (UnityGUI::Float("Minimum Vertex Distance", &ps.TrailMinVertexDistance))
			ps.TrailMinVertexDistance = (std::max)(0.001f, ps.TrailMinVertexDistance);
		UnityGUI::Toggle("World Space", &ps.TrailWorldSpace);
		UnityGUI::Toggle("Die with Particles", &ps.TrailDieWithParticles);
		UnityGUI::ValueLabel("Texture Mode", "Stretch");
		UnityGUI::Toggle("Size affects Width", &ps.TrailSizeAffectsWidth);
		UnityGUI::Toggle("Inherit Particle Color", &ps.TrailInheritParticleColor);
		GradientField("Color over Lifetime", &ps.TrailColorOverLifetime);
		CurveField("Width over Trail", &ps.TrailWidthOverTrail);
		GradientField("Color over Trail", &ps.TrailColorOverTrail);
		ModuleEnd();
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

	DrawCollision(*this);
	ModuleHeader("##triggers", "Triggers", nullptr, false, false);
	DrawSubEmitters(*this);

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

	if (ModuleHeader("##lights", "Lights", &LightsEnabled))
	{
		if (UnityGUI::Slider("Ratio", &LightRatio, 0.0f, 1.0f))
			LightRatio = std::clamp(LightRatio, 0.0f, 1.0f);
		UnityGUI::Color("Light Color", &LightColor.x);
		UnityGUI::Toggle("Use Particle Color", &LightUseParticleColor);
		UnityGUI::Toggle("Size Affects Range", &LightSizeAffectsRange);
		UnityGUI::Toggle("Alpha Affects Intensity", &LightAlphaAffectsIntensity);
		if (UnityGUI::Float("Range", &LightRange, 2))
			LightRange = (std::max)(0.01f, LightRange);
		if (UnityGUI::Float("Intensity", &LightIntensity, 2))
			LightIntensity = (std::max)(0.0f, LightIntensity);
		if (UnityGUI::Int("Maximum Lights", &LightMaxLights))
			LightMaxLights = std::clamp(LightMaxLights, 0, 4);
		UnityGUI::HelpBox("Particle lights share the 4 point-light slots with the scene's Point Lights and do not cast shadows.", false);
		ModuleEnd();
	}
	DrawTrails(*this);
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
		if (TextureField("Texture", &Texture, "particleTex:" + std::to_string((uintptr_t)this)) && Texture == "builtin:Flame-Sheet")
		{
			// 불꽃 플립북을 고르면 4 x 4 칸으로 (Unity 에서 시트 텍스처를 쓸 때 하는 설정)
			SheetEnabled = true;
			SheetTilesX = SheetTilesY = 4;
		}
		if (TrailsEnabled)
			TextureField("Trail Texture", &TrailTexture, "particleTrailTex:" + std::to_string((uintptr_t)this));
		static const char* kBlend[] = { "Alpha Blended", "Additive" };
		int blend = (int)Blend;
		if (EnumRow("Blend Mode", &blend, kBlend, 2))
			Blend = (BlendMode)blend;
		static const char* kSort[] = { "None", "By Distance", "Oldest in Front", "Youngest in Front" };
		int sort = (int)Sort;
		if (EnumRow("Sort Mode", &sort, kSort, 4))
			Sort = (SortMode)sort;
		UnityGUI::Float("Sorting Fudge", &SortingFudge);
		static const char* kLighting[] = { "Unlit", "Lit" };
		int lighting = Lit ? 1 : 0;
		if (EnumRow("Lighting", &lighting, kLighting, 2))
			Lit = lighting == 1;
		UnityGUI::Toggle("Soft Particles", &SoftParticles);
		if (SoftParticles)
		{
			UnityGUI::Float("Soft Distance", &SoftDistance, 2);
			SoftDistance = (std::max)(0.01f, SoftDistance);
		}
		ModuleEnd();
	}

	// 에디터 미리보기 상태 (Unity 는 Scene 뷰 창에 표시)
	UnityGUI::Spacing(4.0f);
	char buf[64];
	snprintf(buf, sizeof(buf), "%d  (%s)", ParticleCount(), IsPlaying() ? "Playing" : IsPaused() ? "Paused" : IsAlive() ? "Stopping" : "Stopped");
	UnityGUI::ValueLabel("Particles", buf);
}
