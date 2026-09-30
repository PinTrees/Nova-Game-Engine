#include "pch.h"
#include "Debug.h"
#include "CSharpScript.h"
#include "ScriptEngine.h"
#include "UnityGUI.h"
#include "ObjectPicker.h"
#include "AudioClip.h"
#include "AudioManager.h"
#include "Collider.h"

namespace
{
	bool ActiveInHierarchy(GameObject* go)
	{
		for (GameObject* g = go; g != nullptr; g = g->GetParent())
			if (!g->IsActive())
				return false;
		return go != nullptr;
	}

	// "PlayerController" → "Player Controller" (Unity 의 컴포넌트 제목)
	std::string NicifyClass(const std::string& n)
	{
		std::string shortName = n;
		const size_t dot = shortName.rfind('.');
		if (dot != std::string::npos)
			shortName = shortName.substr(dot + 1);
		std::string out;
		for (size_t i = 0; i < shortName.size(); ++i)
		{
			const char c = shortName[i];
			if (i > 0 && isupper((unsigned char)c) && (islower((unsigned char)shortName[i - 1]) ||
				(i + 1 < shortName.size() && islower((unsigned char)shortName[i + 1]) && isupper((unsigned char)shortName[i - 1]))))
				out += ' ';
			out += c == '_' ? ' ' : c;
		}
		return out;
	}

	std::string ObjectName(uint64 id, const char* noneText)
	{
		if (id == 0)
			return noneText;
		Scene* scene = SceneManager::GetI()->GetCurrentScene();
		GameObject* go = scene ? scene->FindByFileID(id) : nullptr;
		return go ? go->GetName() : std::string("Missing");
	}
}

CSharpScript::CSharpScript()
{
	m_InspectorTitleName = "Script";
}

CSharpScript::~CSharpScript()
{
	Release();
}

std::shared_ptr<CSharpScript> CSharpScript::Create(const std::string& className)
{
	auto s = std::make_shared<CSharpScript>();
	s->m_ClassName = className;
	s->SetScriptName(className);
	// 필드 기본값을 복사해 둔다 (Unity 도 붙이는 순간의 기본값을 저장)
	if (const ScriptEngine::ClassInfo* ci = ScriptEngine::FindClass(className))
	{
		s->m_ClassName = ci->FullName;
		for (const ScriptEngine::FieldInfo& f : ci->Fields)
			if (f.Type != "unsupported")
				s->m_FieldValues[f.Name] = f.Default;
	}
	return s;
}

std::string CSharpScript::InspectorTitle() const
{
	return NicifyClass(m_ClassName.empty() ? std::string("Missing") : m_ClassName) + " (Script)";
}

// ------------------------------------------------------------------ 수명
bool CSharpScript::Ready()
{
	return m_Handle != nullptr && Component::IsEnabled() && ActiveInHierarchy(m_pGameObject);
}

void CSharpScript::SyncEnabled()
{
	const bool enabled = Component::IsEnabled();
	if (MonoBehaviour::IsEnabled() != enabled)
		MonoBehaviour::SetEnabled(enabled);   // 물리 충돌 전달이 이 값을 본다
	if (m_Handle && enabled != m_InstanceEnabled)
	{
		m_InstanceEnabled = enabled;
		ScriptEngine::SetInstanceEnabled(m_Handle, enabled);
		ScriptEngine::Invoke(m_Handle, enabled ? ScriptEngine::Message::OnEnable : ScriptEngine::Message::OnDisable);
	}
}

void CSharpScript::SetEnabledFromScript(bool enabled)
{
	// C# 에서 enabled = false: 네이티브 체크박스와 OnDisable/OnEnable
	Component::SetEnabled(enabled);
	SyncEnabled();
}

void CSharpScript::AwakeNow()
{
	if (m_Awoken || !Application::IsPlaying() || m_pGameObject == nullptr)
		return;
	if (!ActiveInHierarchy(m_pGameObject))
		return;   // 비활성 오브젝트의 스크립트는 활성화될 때까지 Awake 하지 않는다 (Unity)
	m_Awoken = true;
	m_Handle = ScriptEngine::CreateInstance(m_ClassName, m_pGameObject->GetFileID(), this, m_FieldValues.dump(), Component::IsEnabled());
	if (m_Handle == nullptr)
	{
		if (ScriptEngine::IsAvailable())
			Debug::Write(LogType::Warning, "The referenced script (" + m_ClassName + ") on '" + m_pGameObject->GetName() + "' is missing or has compile errors.");
		return;
	}
	m_InstanceEnabled = Component::IsEnabled();
	ScriptEngine::Invoke(m_Handle, ScriptEngine::Message::Awake);
	if (m_Handle && m_InstanceEnabled)
		ScriptEngine::Invoke(m_Handle, ScriptEngine::Message::OnEnable);
}

void CSharpScript::Awake()
{
	AwakeNow();
}

void CSharpScript::Start()
{
	if (!Ready() || m_Started)
		return;
	m_Started = true;
	ScriptEngine::Invoke(m_Handle, ScriptEngine::Message::Start);
}

void CSharpScript::Update()
{
	if (!Application::IsPlaying())
		return;
	if (!m_Awoken)
		AwakeNow();   // 나중에 활성화된 오브젝트
	SyncEnabled();
	if (!Ready())
		return;
	if (!m_Started)
		Start();      // AddComponent / Instantiate 로 생긴 스크립트: 첫 Update 직전에 Start
	if (m_Handle)
		ScriptEngine::Invoke(m_Handle, ScriptEngine::Message::Update);
}

void CSharpScript::LateUpdate()
{
	if (Application::IsPlaying() && Ready() && m_Started)
		ScriptEngine::Invoke(m_Handle, ScriptEngine::Message::LateUpdate);
}

void CSharpScript::FixedUpdate()
{
	if (Application::IsPlaying() && Ready() && m_Started)
		ScriptEngine::Invoke(m_Handle, ScriptEngine::Message::FixedUpdate);
}

void CSharpScript::OnDestroy()
{
	if (m_Handle)
	{
		if (m_InstanceEnabled)
			ScriptEngine::Invoke(m_Handle, ScriptEngine::Message::OnDisable);
		ScriptEngine::Invoke(m_Handle, ScriptEngine::Message::OnDestroy);
	}
	Release();
}

void CSharpScript::Release()
{
	if (m_Handle)
	{
		ScriptEngine::DestroyInstance(m_Handle);
		m_Handle = nullptr;
	}
}

void CSharpScript::Collision(Collider* other, bool trigger, int phase)
{
	if (!Ready() || other == nullptr || other->GetGameObject() == nullptr)
		return;
	ScriptEngine::InvokeCollision(m_Handle, trigger, phase, other->GetGameObject()->GetFileID());
}

void CSharpScript::OnCollisionEnter(Collider* other) { Collision(other, false, 0); }
void CSharpScript::OnCollisionStay(Collider* other) { Collision(other, false, 1); }
void CSharpScript::OnCollisionExit(Collider* other) { Collision(other, false, 2); }
void CSharpScript::OnTriggerEnter(Collider* other) { Collision(other, true, 0); }
void CSharpScript::OnTriggerStay(Collider* other) { Collision(other, true, 1); }
void CSharpScript::OnTriggerExit(Collider* other) { Collision(other, true, 2); }

// ------------------------------------------------------------------ Inspector
void CSharpScript::OnInspectorGUI()
{
	const std::wstring scriptFile = ScriptEngine::FindScriptFile(m_ClassName);

	// Script 행: 클릭하면 Project 에서 선택, 더블클릭하면 코드 편집기
	{
		ImVec2 fmin, fmax;
		const std::string shortName = m_ClassName.substr(m_ClassName.rfind('.') == std::string::npos ? 0 : m_ClassName.rfind('.') + 1);
		ImGui::BeginDisabled();
		UnityGUI::ObjectFieldButtons("Script", shortName.empty() ? "None (Mono Script)" : shortName.c_str(), "script_cs", nullptr, 0, &fmin, &fmax);
		ImGui::EndDisabled();
		const ImVec2 after = ImGui::GetCursorScreenPos();
		ImGui::SetCursorScreenPos(fmin);
		ImGui::InvisibleButton("##scriptField", ImVec2((std::max)(1.0f, fmax.x - fmin.x), fmax.y - fmin.y));
		if (ImGui::IsItemClicked() && !scriptFile.empty())
			SelectionManager::SetSelectedFile(scriptFile);
		if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !scriptFile.empty())
			ScriptEngine::OpenInCodeEditor(scriptFile, 1);
		ImGui::SetCursorScreenPos(after);
	}

	const ScriptEngine::ClassInfo* ci = ScriptEngine::FindClass(m_ClassName);
	if (ci == nullptr)
	{
		if (ScriptEngine::IsCompiling())
			UnityGUI::HelpBox("Compiling scripts...", false);
		else
			UnityGUI::HelpBox("The associated script can not be loaded.\nPlease fix any compile errors\nand assign a valid script.", true);
		return;
	}

	// 값: Play 중이면 C# 인스턴스의 현재 값, 아니면 저장된 값
	const bool live = Application::IsPlaying() && m_Handle != nullptr;
	if (live && m_LiveFrame != ImGui::GetFrameCount())
	{
		json j = json::parse(ScriptEngine::GetFieldsJson(m_Handle), nullptr, false);
		m_LiveCache = j.is_discarded() ? json::object() : j;
		m_LiveFrame = ImGui::GetFrameCount();
	}
	json& values = live ? m_LiveCache : m_FieldValues;

	for (const ScriptEngine::FieldInfo& f : ci->Fields)
	{
		if (!f.Header.empty())
		{
			UnityGUI::Spacing(6.0f);
			UnityGUI::Label(f.Header.c_str(), 0, true);
		}
		if (f.Space > 0.0f)
			UnityGUI::Spacing(f.Space);
		ImGui::PushID(f.Name.c_str());
		json v = values.contains(f.Name) ? values[f.Name] : f.Default;
		bool changed = false;
		const char* label = f.Label.c_str();

		if (f.Type == "float")
		{
			float x = v.is_number() ? v.get<float>() : 0.0f;
			changed = f.HasRange ? UnityGUI::Slider(label, &x, f.RangeMin, f.RangeMax) : UnityGUI::Float(label, &x);
			if (f.HasRange) x = std::clamp(x, f.RangeMin, f.RangeMax);
			if (changed) v = x;
		}
		else if (f.Type == "int")
		{
			int x = v.is_number() ? v.get<int>() : 0;
			if (f.HasRange)
			{
				float fx = (float)x;
				changed = UnityGUI::Slider(label, &fx, f.RangeMin, f.RangeMax);
				x = (int)std::lround(std::clamp(fx, f.RangeMin, f.RangeMax));
			}
			else
				changed = UnityGUI::Int(label, &x);
			if (changed) v = x;
		}
		else if (f.Type == "bool")
		{
			bool x = v.is_boolean() && v.get<bool>();
			if ((changed = UnityGUI::Toggle(label, &x))) v = x;
		}
		else if (f.Type == "string")
		{
			std::string x = v.is_string() ? v.get<std::string>() : std::string();
			if ((changed = UnityGUI::TextField(label, &x))) v = x;
		}
		else if (f.Type == "Vector2" || f.Type == "Vector3" || f.Type == "Vector4" || f.Type == "Color")
		{
			float xyzw[4] = { 0, 0, 0, f.Type == "Color" ? 1.0f : 0.0f };
			if (v.is_array())
				for (size_t i = 0; i < v.size() && i < 4; ++i) xyzw[i] = v[i].get<float>();
			if (f.Type == "Vector2")
				changed = UnityGUI::Vector2Pair(label, "X", &xyzw[0], "Y", &xyzw[1]);
			else if (f.Type == "Vector3")
				changed = UnityGUI::Vector3(label, xyzw);
			else if (f.Type == "Vector4")
			{
				changed = UnityGUI::Vector2Pair(label, "X", &xyzw[0], "Y", &xyzw[1]);
				changed |= UnityGUI::Vector2Pair("", "Z", &xyzw[2], "W", &xyzw[3]);
			}
			else
				changed = UnityGUI::Color(label, xyzw);
			if (changed)
			{
				const int n = f.Type == "Vector2" ? 2 : (f.Type == "Vector3" ? 3 : 4);
				v = json::array();
				for (int i = 0; i < n; ++i) v.push_back(xyzw[i]);
			}
		}
		else if (f.Type == "enum")
		{
			const long long cur = v.is_number() ? v.get<long long>() : 0;
			int index = 0;
			for (size_t i = 0; i < f.OptionValues.size(); ++i)
				if (f.OptionValues[i] == cur) index = (int)i;
			std::vector<const char*> items;
			for (const std::string& s : f.Options) items.push_back(s.c_str());
			if (!items.empty() && (changed = UnityGUI::Dropdown(label, &index, items.data(), (int)items.size())))
				v = f.OptionValues[index];
		}
		else if (f.Type == "GameObject" || f.Type == "Transform")
		{
			// 오브젝트 참조: Hierarchy 에서 끌어 놓기 (값 = fileID)
			const uint64 id = v.is_number_unsigned() || v.is_number_integer() ? v.get<uint64>() : 0;
			const bool isGo = f.Type == "GameObject";
			const std::string text = id ? ObjectName(id, "") + (isGo ? "" : " (Transform)") : (isGo ? "None (Game Object)" : "None (Transform)");
			ImVec2 fmin, fmax;
			const int pressed = UnityGUI::ObjectFieldButtons(label, text.c_str(), isGo ? "gameobject" : "transform", nullptr, 0, &fmin, &fmax);
			const ImVec2 after = ImGui::GetCursorScreenPos();
			ImGui::SetCursorScreenPos(fmin);
			ImGui::InvisibleButton("##ref", ImVec2((std::max)(1.0f, fmax.x - fmin.x - 22.0f), fmax.y - fmin.y));
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("GAME_OBJECT"))
				{
					GameObject* go = *(GameObject**)payload->Data;
					if (go) { v = go->GetFileID(); changed = true; }
				}
				ImGui::EndDragDropTarget();
			}
			if (ImGui::IsItemClicked() && id)
				if (Scene* scene = SceneManager::GetI()->GetCurrentScene())
					if (GameObject* target = scene->FindByFileID(id))
						SelectionManager::SetSelectedGameObject(target);
			if (pressed == -1 && id)
			{
				v = 0;   // ⊙ = 비우기 (씬 오브젝트 선택 창은 아직 없음)
				changed = true;
			}
			ImGui::SetCursorScreenPos(after);
		}
		else if (f.Type == "AudioClip")
		{
			const std::string path = v.is_string() ? v.get<std::string>() : std::string();
			const std::string text = path.empty() ? "None (Audio Clip)" : std::filesystem::path(path).stem().string();
			ImVec2 fmin, fmax;
			const int pressed = UnityGUI::ObjectFieldButtons(label, text.c_str(), "audio_clip", nullptr, 0, &fmin, &fmax);
			const ImVec2 after = ImGui::GetCursorScreenPos();
			const std::string key = "scriptclip:" + std::to_string((uintptr_t)this) + ":" + f.Name;
			if (pressed == -1)
			{
				ObjectPicker::Options opt;
				opt.TypeName = "AudioClip";
				opt.Icon = "audio_clip";
				opt.Items = AudioClip::FindAll();
				opt.Current = path;
				opt.Preview = [](const std::string& p) { AudioManager::PlayPreview(AudioClip::Load(p)); };
				ObjectPicker::Open(key, std::move(opt));
			}
			std::string picked;
			if (ObjectPicker::Poll(key, picked)) { v = picked; changed = true; }
			ImGui::SetCursorScreenPos(fmin);
			ImGui::InvisibleButton("##clipdrop", ImVec2((std::max)(1.0f, fmax.x - fmin.x - 22.0f), fmax.y - fmin.y));
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_FILE"))
				{
					const std::string dropped(static_cast<const char*>(payload->Data));
					if (AudioClip::IsAudioPath(dropped)) { v = dropped; changed = true; }
				}
				ImGui::EndDragDropTarget();
			}
			ImGui::SetCursorScreenPos(after);
		}
		else
			UnityGUI::ValueLabel(label, (f.TypeName + " (not supported in Inspector yet)").c_str());

		if (!f.Tooltip.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
			ImGui::SetTooltip("%s", f.Tooltip.c_str());

		if (changed)
		{
			if (live)
			{
				m_LiveCache[f.Name] = v;
				json one = json::object();
				one[f.Name] = v;
				ScriptEngine::SetFieldsJson(m_Handle, one.dump());   // Play 중 변경은 Play 가 끝나면 사라진다 (Unity)
			}
			else
				m_FieldValues[f.Name] = v;
		}
		ImGui::PopID();
	}
}

// ------------------------------------------------------------------ 저장
json CSharpScript::toJson() const
{
	json j;
	j["type"] = "CSharpScript";
	j["class"] = m_ClassName;
	j["enabled"] = m_Enabled;
	j["fields"] = m_FieldValues;
	return j;
}

void CSharpScript::fromJson(const json& j)
{
	m_ClassName = j.value("class", std::string());
	SetScriptName(m_ClassName);
	m_Enabled = j.value("enabled", true);
	m_FieldValues = j.contains("fields") && j["fields"].is_object() ? j["fields"] : json::object();
}
