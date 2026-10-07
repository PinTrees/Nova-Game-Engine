#pragma once
#include "MonoBehaviour.h"

// 프로젝트 C# 스크립트(MonoBehaviour 파생 클래스) 하나를 GameObject 에 붙이는 컴포넌트.
//  - 편집 중: 클래스 이름과 Inspector 필드 값(JSON)만 저장한다 (C# 인스턴스 없음, Unity 와 같음).
//  - Play 중: Awake 때 C# 인스턴스를 만들고 저장된 값을 넣은 뒤 Awake/OnEnable/Start/Update/LateUpdate/FixedUpdate/
//    OnDisable/OnDestroy 와 충돌/트리거 메시지를 전달한다. Inspector 는 실시간 값을 보여 주고 고치면 바로 반영(Play 끝나면 원래대로).
// C++ MonoBehaviour 를 상속해 물리의 충돌/트리거 이벤트를 그대로 받는다.
class CSharpScript : public MonoBehaviour
{
private:
	std::string m_ClassName;
	json m_FieldValues = json::object();
	void* m_Handle = nullptr;
	bool m_Awoken = false;
	bool m_Started = false;
	bool m_InstanceEnabled = true;   // 마지막으로 C# 에 알린 enabled
	json m_LiveCache;                // Play 중 Inspector 가 읽은 값
	int m_LiveFrame = -1;

public:
	CSharpScript();
	virtual ~CSharpScript();

	static std::shared_ptr<CSharpScript> Create(const std::string& className);
	const std::string& GetClassName() const { return m_ClassName; }
	void* GetHandle() const { return m_Handle; }
	void SetFieldValue(const std::string& name, const json& value) { m_FieldValues[name] = value; }

	// Play 중: 인스턴스를 만들고 Awake(+OnEnable). AddComponent / Instantiate 에서도 부른다
	void AwakeNow();
	void SetEnabledFromScript(bool enabled);

	virtual void Awake() override;
	virtual void Start() override;
	virtual void Update() override;
	virtual void LateUpdate() override;
	virtual void FixedUpdate() override;
	virtual void OnDestroy() override;
	virtual void OnHierarchyActiveChanged(bool active) override;   // Unity: SetActive(false/true) → OnDisable / OnEnable

	virtual void OnCollisionEnter(Collider* other) override;
	virtual void OnCollisionStay(Collider* other) override;
	virtual void OnCollisionExit(Collider* other) override;
	virtual void OnTriggerEnter(Collider* other) override;
	virtual void OnControllerColliderHit(const struct ControllerColliderHit& hit, int index) override;
	virtual void OnJointBreak(float breakForce) override;
	void OnJointBreak2D(int kind, int componentId);
	virtual void OnTriggerStay(Collider* other) override;
	virtual void OnTriggerExit(Collider* other) override;

	virtual void OnInspectorGUI() override;
	virtual bool UsesUnityInspector() const override { return true; }
	virtual const char* InspectorIconName() const override { return "script_cs"; }
	virtual std::string InspectorTitle() const override;

	virtual json toJson() const override;
	virtual void fromJson(const json& j) override;
	virtual std::string GetType() const override { return "CSharpScript"; }

	// 2D 물리 (Physics2DManager): OnCollision/OnTrigger Enter·Stay·Exit 2D — phase 0 Enter, 1 Stay, 2 Exit
	void Collision2D(GameObject* other, bool trigger, int phase);

private:
	bool Ready();          // 인스턴스가 있고 켜져 있고 GameObject 가 활성
	void SyncEnabled();
	void Release();
	void Collision(Collider* other, bool trigger, int phase);
};
