#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

// C# 스크립팅 (Unity 의 Mono/Scripting 백엔드에 해당).
//  - .NET 런타임을 hostfxr 로 띄워 Binaries/Scripting/NovaScriptCore.dll(엔진 C# API)을 읽는다.
//  - 프로젝트의 Assets/**/*.cs 를 <프로젝트>/Assembly-CSharp.csproj 로 dotnet build 해서
//    Library/ScriptAssemblies/Assembly-CSharp.dll 을 만들고 읽는다 (파일이 바뀌면 백그라운드에서 다시 빌드 → 핫 리로드).
//  - Play 중에 바뀌면 Play 가 끝난 뒤 다시 읽는다 (Unity 의 "Recompile After Finished Playing").
//  - 컴파일 오류는 Console 에 파일(줄)과 함께 표시하고, 오류가 있으면 Play 에 들어가지 않는다.
namespace ScriptEngine
{
	struct FieldInfo
	{
		std::string Name;
		std::string Label;
		std::string Type;            // int, float, bool, string, Vector2, Vector3, Vector4, Color, enum, GameObject, Transform, AudioClip, unsupported
		std::string TypeName;        // unsupported 일 때 C# 타입 이름
		nlohmann::json Default;
		std::string Header, Tooltip;
		float Space = 0.0f;
		bool HasRange = false;
		float RangeMin = 0.0f, RangeMax = 0.0f;
		std::vector<std::string> Options;   // enum 표시 이름
		std::vector<long long> OptionValues;
	};

	// Button 의 On Click () 에서 고를 수 있는 public 메서드 (반환 void, 인자 없음 또는 int/float/string/bool 하나)
	struct MethodInfo
	{
		std::string Name;
		std::string ParamType;   // "", "int", "float", "string", "bool"
	};

	struct ClassInfo
	{
		std::string Name;
		std::string FullName;
		std::vector<FieldInfo> Fields;
		std::vector<MethodInfo> Methods;
	};

	enum class State { NotStarted, Unavailable, Idle, Compiling };

	void Init();          // 에디터 시작 시 (프로젝트 경로가 정해진 뒤)
	void Shutdown();
	void Update();        // 매 프레임: 파일 변경 감시, 컴파일 완료 처리, 다시 읽기, 지연 Destroy
	void BeginFrame();    // Play 중 게임 업데이트 직전: 입력 상태, 시간
	void OnPlayModeChanged(bool playing);
	void OnSceneSwapped();
	// 앱 상태 (Unity 의 OnApplicationPause · OnApplicationFocus — C# AppEvents): 안드로이드 뒤로/앞으로, Windows 빌드된 게임의 창 활성
	void OnApplicationPause(bool paused);
	void OnApplicationFocus(bool focused);
	// 씬 알림 (C# SceneManager.sceneLoaded · sceneUnloaded · activeSceneChanged · AsyncOperation.completed — AppEvents.SceneEvent)
	//  kind 0 = 읽음 (핸들, 모드), 1 = 내림 (핸들), 2 = 활성 씬 바뀜 (이전, 새), 3 = 작업 끝 (작업 번호, 성공)
	void OnSceneEvent(int kind, int a, int b);   // Play 중 씬 교체 뒤, 새 씬 Enter 전 (id 캐시·UI 리스너 비우기)

	State GetState();
	bool IsAvailable();              // .NET 런타임을 띄웠는지
	bool IsCompiling();
	bool HasCompileErrors();
	const std::string& StatusText(); // 상태 표시줄 (예: "Compiling scripts...")
	// Play 에 들어가도 되는지 (컴파일 중이면 끝날 때까지 기다리고, 오류가 있으면 false + Console 안내)
	bool CanEnterPlayMode();
	void RequestRecompile();         // 강제로 다시 빌드 (Assets > Refresh 등)

	const std::vector<ClassInfo>& Classes();
	const ClassInfo* FindClass(const std::string& name);   // 이름 또는 전체 이름
	// 클래스 이름과 같은 이름의 .cs 파일 (Assets 아래, 절대 경로, 없으면 빈 문자열)
	std::wstring FindScriptFile(const std::string& className);
	// 새 스크립트 파일 (Unity 의 MonoBehaviour Script 템플릿). 만든 절대 경로
	std::wstring CreateScriptAsset(const std::wstring& directory, const std::string& className = "NewMonoBehaviourScript");
	// .cs 이름을 바꾼 뒤: 파일 안의 클래스 이름도 새 이름으로 (Unity 가 새 스크립트를 이름 지을 때와 같음)
	void RenameScriptClass(const std::wstring& newPath, const std::string& oldClassName);
	// 코드 편집기에서 열기 (Preferences > External Tools 에서 고른 편집기, 기본은 내장 NOVA Code)
	void OpenInCodeEditor(const std::wstring& file, int line = 0);
	// Assembly-CSharp.csproj 다시 쓰기 (Preferences 의 Regenerate project files)
	void RegenerateProjectFiles();
	// 엔진 C# API 목록 JSON (NOVA Code 자동 완성용). 스크립팅이 없으면 빈 문자열
	std::string GetApiJson();
	// Assets 아래의 .cs 파일들 (절대 경로, 정렬)
	std::vector<std::wstring> ScriptFilePaths();

	// ---- 관리 코드 호출 (CSharpScript 가 쓴다) ----
	void* CreateInstance(const std::string& className, uint64_t gameObjectId, void* nativeComponent, const std::string& fieldsJson, bool enabled);
	void DestroyInstance(void* handle);
	enum class Message { Awake = 0, OnEnable, Start, Update, LateUpdate, FixedUpdate, OnDisable, OnDestroy };
	void Invoke(void* handle, Message message);
	void SetInstanceEnabled(void* handle, bool enabled);
	void InvokeCollision(void* handle, bool trigger, int phase, uint64_t otherGameObject);
	void InvokeCollision(void* handle, int kind, int phase, uint64_t otherGameObject);   // kind 0 충돌, 1 트리거, 2 OnControllerColliderHit
	std::string GetFieldsJson(void* handle);
	void SetFieldsJson(void* handle, const std::string& json);
	// UI: 이 GameObject 의 스크립트(className)에서 메서드 호출 (Button On Click 의 저장된 호출). 찾으면 true
	bool InvokeMethod(uint64_t gameObjectId, const std::string& className, const std::string& method, const std::string& argument);
	// NOVA CLI (nova exec): C# 코드 조각을 빌드해 에디터 안에서 실행 (식이면 값, 문장이면 마지막 return). 메인 스레드에서 빌드를 기다린다
	bool Exec(const std::string& code, std::string& result, std::string& error);
	// UI: C# 에서 AddListener 로 등록한 이벤트 실행
	// kind 0 Button.onClick, 1 Slider.onValueChanged(number), 2 Toggle.onValueChanged(number != 0),
	//      3 InputField.onValueChanged(text), 4 InputField.onEndEdit(text)
	void InvokeUIEvent(uint64_t gameObjectId, int kind, float number = 0.0f, const std::string& text = std::string());

	// ---- 입력 (Game 뷰가 포커스일 때만) ----
	bool KeyState(int vk, int mode);           // mode 0 누르는 중, 1 이번 프레임 눌림, 2 이번 프레임 뗌
	bool MouseButtonState(int button, int mode);
	float PlayTime();                          // Play 시작 후 시간 (Time.time)
	int FrameCount();
}
