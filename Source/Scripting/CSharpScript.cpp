#include "pch.h"
#include "Debug.h"
#include "CSharpScript.h"
#include "CharacterController.h"
#include "ScriptEngine.h"
#include "ScriptFieldGUI.h"
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

void CSharpScript::OnHierarchyActiveChanged(bool active)
{
	// 컴포넌트가 켜져 있는 스크립트만 (꺼진 스크립트는 OnEnable 을 받지 않는다). 아직 Awake 전이면 켜질 때 Awake → OnEnable 이 함께 온다
	if (!Application::IsPlaying() || m_Handle == nullptr || !m_InstanceEnabled)
		return;
	ScriptEngine::Invoke(m_Handle, active ? ScriptEngine::Message::OnEnable : ScriptEngine::Message::OnDisable);
}

void CSharpScript::OnDestroy()
{
	if (m_Handle)
	{
		if (m_InstanceEnabled && m_pGameObject && m_pGameObject->m_HierarchyActiveSeen)   // 꺼진 오브젝트는 이미 OnDisable 을 받았다
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

void CSharpScript::Collision2D(GameObject* other, bool trigger, int phase)
{
	if (!Ready() || other == nullptr)
		return;
	ScriptEngine::InvokeCollision(m_Handle, trigger ? 5 : 4, phase, other->GetFileID());   // kind 4 = Collision2D, 5 = 트리거 2D
}

void CSharpScript::OnCollisionEnter(Collider* other) { Collision(other, false, 0); }
void CSharpScript::OnCollisionStay(Collider* other) { Collision(other, false, 1); }
void CSharpScript::OnCollisionExit(Collider* other) { Collision(other, false, 2); }
void CSharpScript::OnTriggerEnter(Collider* other) { Collision(other, true, 0); }
void CSharpScript::OnTriggerStay(Collider* other) { Collision(other, true, 1); }
void CSharpScript::OnTriggerExit(Collider* other) { Collision(other, true, 2); }
void CSharpScript::OnJointBreak(float breakForce)
{
	// kind 3: 힘은 float 비트를 그대로 other 에 담아 보낸다
	uint32_t bits = 0;
	memcpy(&bits, &breakForce, sizeof(bits));
	if (m_Handle && Application::IsPlaying())
		ScriptEngine::InvokeCollision(m_Handle, 3, 0, (uint64_t)bits);
}

void CSharpScript::OnJointBreak2D(int kind, int componentId)
{
	if (m_Handle && Application::IsPlaying())
		ScriptEngine::InvokeCollision(m_Handle, 6, kind, (uint64)componentId);
}

void CSharpScript::OnControllerColliderHit(const ControllerColliderHit& hit, int index)
{
	// C# 이 CC_GetHit 로 점·법선을 읽는다 (kind 2, phase = 번호)
	if (m_Handle && hit.gameObject && Application::IsPlaying())
		ScriptEngine::InvokeCollision(m_Handle, 2, index, hit.gameObject->GetFileID());
}

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

	const std::string key = "script:" + std::to_string((uintptr_t)this) + ":";
	for (const ScriptEngine::FieldInfo& f : ci->Fields)
	{
		json v = values.contains(f.Name) ? values[f.Name] : f.Default;
		if (!ScriptFieldGUI::Field(f, v, key))
			continue;
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
