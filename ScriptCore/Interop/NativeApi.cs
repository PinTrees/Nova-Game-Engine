using System;
using System.Runtime.InteropServices;
using System.Text;

namespace NovaEngine.Interop
{
    [StructLayout(LayoutKind.Sequential)]
    internal struct TimeData
    {
        public float deltaTime;
        public float unscaledDeltaTime;
        public float time;
        public float unscaledTime;
        public float fixedDeltaTime;
        public float timeScale;
        public float realtimeSinceStartup;
        public int frameCount;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct RaycastData
    {
        public Vector3 point;
        public Vector3 normal;
        public float distance;
        public ulong gameObject;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct Ray2DData
    {
        public Vector2 point;
        public Vector2 normal;
        public float distance;
        public float fraction;
        public ulong gameObject;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct ControllerHitData
    {
        public Vector3 point;
        public Vector3 normal;
        public Vector3 moveDirection;
        public float moveLength;
        public ulong gameObject;
    }

    // 네이티브(C++)가 채워 주는 함수 표. 순서와 시그니처는 Source/Scripting/ScriptBindings.cpp 의 NativeApiTable 과 반드시 같아야 한다.
    [StructLayout(LayoutKind.Sequential)]
    internal unsafe struct NativeApiTable
    {
        public int Size;   // sizeof(NativeApiTable) (C++), 불일치 검사용

        // 로그: level 0 Log, 1 Warning, 2 Error / 메시지, 스택, 파일, 줄
        public delegate* unmanaged<int, byte*, byte*, byte*, int, void> Log;

        // GameObject
        public delegate* unmanaged<ulong, int> GO_IsValid;
        public delegate* unmanaged<ulong, byte*> GO_GetName;
        public delegate* unmanaged<ulong, byte*, void> GO_SetName;
        public delegate* unmanaged<ulong, int, int> GO_GetActive;         // 0 activeSelf, 1 activeInHierarchy
        public delegate* unmanaged<ulong, int, void> GO_SetActive;
        public delegate* unmanaged<ulong, byte*> GO_GetTag;
        public delegate* unmanaged<ulong, byte*, void> GO_SetTag;
        public delegate* unmanaged<byte*, int, ulong> GO_Find;             // 0 이름, 1 태그
        public delegate* unmanaged<byte*, ulong> GO_Create;
        public delegate* unmanaged<int, ulong> GO_CreatePrimitive;
        public delegate* unmanaged<ulong, float, void> GO_Destroy;
        public delegate* unmanaged<ulong, int, Vector3*, Quaternion*, ulong, ulong> GO_Instantiate;
        public delegate* unmanaged<ulong, byte*, int> GO_HasComponent;
        public delegate* unmanaged<ulong, byte*, IntPtr> GO_AddComponent;  // 스크립트면 새 인스턴스 핸들, 네이티브 컴포넌트면 1
        public delegate* unmanaged<ulong, byte*, void> GO_RemoveComponent;

        // Transform: 벡터 속성 0 position, 1 localPosition, 2 localScale, 3 eulerAngles, 4 localEulerAngles, 5 lossyScale, 6 forward, 7 right, 8 up
        public delegate* unmanaged<ulong, int, Vector3*, void> TR_GetVector;
        public delegate* unmanaged<ulong, int, Vector3*, void> TR_SetVector;
        public delegate* unmanaged<ulong, int, Quaternion*, void> TR_GetQuat;   // 0 rotation, 1 localRotation
        public delegate* unmanaged<ulong, int, Quaternion*, void> TR_SetQuat;
        public delegate* unmanaged<ulong, ulong> TR_GetParent;
        public delegate* unmanaged<ulong, ulong, int, void> TR_SetParent;
        public delegate* unmanaged<ulong, int> TR_GetChildCount;
        public delegate* unmanaged<ulong, int, ulong> TR_GetChild;

        // Time / Input / Screen
        public delegate* unmanaged<TimeData*, void> Time_Get;
        public delegate* unmanaged<int, int, int> Input_GetKey;            // 가상 키 코드, 0 누르는 중 1 눌림 2 뗌
        public delegate* unmanaged<int, int, int> Input_GetMouseButton;
        public delegate* unmanaged<Vector4*, void> Input_GetMouse;         // xy 위치(게임 화면 픽셀, 왼쪽 아래 원점), zw 휠
        public delegate* unmanaged<int*, int*, void> Screen_Get;

        // Rigidbody
        public delegate* unmanaged<ulong, int, Vector3*, void> RB_GetVector;   // 0 velocity, 1 angularVelocity, 2 worldCenterOfMass
        public delegate* unmanaged<ulong, int, Vector3*, void> RB_SetVector;
        public delegate* unmanaged<ulong, int, Vector3*, int, void> RB_AddForce; // 0 힘, 1 토크
        public delegate* unmanaged<ulong, int, float> RB_GetFloat;             // 0 mass, 1 linearDamping, 2 angularDamping
        public delegate* unmanaged<ulong, int, float, void> RB_SetFloat;
        public delegate* unmanaged<ulong, int, int> RB_GetBool;                // 0 useGravity, 1 isKinematic, 2 isSleeping
        public delegate* unmanaged<ulong, int, int, void> RB_SetBool;
        public delegate* unmanaged<ulong, int, Vector4*, void> RB_Move;         // 0 MovePosition, 1 MoveRotation

        // AudioSource
        public delegate* unmanaged<ulong, int, void> AS_Call;                  // 0 Play, 1 Stop, 2 Pause, 3 UnPause
        public delegate* unmanaged<ulong, int, float> AS_GetFloat;             // 0 volume, 1 pitch, 2 panStereo, 3 spatialBlend, 4 time
        public delegate* unmanaged<ulong, int, float, void> AS_SetFloat;
        public delegate* unmanaged<ulong, int, int> AS_GetBool;                // 0 isPlaying, 1 loop, 2 mute, 3 playOnAwake
        public delegate* unmanaged<ulong, int, int, void> AS_SetBool;
        public delegate* unmanaged<ulong, byte*, float, void> AS_PlayOneShot;
        public delegate* unmanaged<ulong, byte*> AS_GetClip;
        public delegate* unmanaged<ulong, byte*, void> AS_SetClip;
        public delegate* unmanaged<byte*, float> Audio_ClipLength;

        // Physics / Camera
        public delegate* unmanaged<Vector3*, Vector3*, float, RaycastData*, int> PH_Raycast;
        public delegate* unmanaged<ulong> Camera_Main;

        // 스크립트 컴포넌트: C# 에서 enabled 를 바꾸면 네이티브 컴포넌트(Inspector 체크박스)도 바꾼다
        public delegate* unmanaged<IntPtr, int, void> Script_SetEnabled;

        // UI (Source/UI/UIScriptBindings.cpp 의 번호표와 같아야 한다)
        //  벡터: 0 anchoredPosition, 1 sizeDelta, 2 anchorMin, 3 anchorMax, 4 pivot, 5 rect(x,y,w,h), 6 offsetMin, 7 offsetMax
        //        Graphic(+0 Image, +100 Text): 10 color, 11 raycastTarget, 12 enabled
        //        Image: 20 fillAmount, 21 type, 22 fillMethod, 23 fillOrigin, 24 preserveAspect, 25 SetNativeSize
        //        Text: 30 fontSize, 31 alignment, 32 lineSpacing, 33 fontStyle / Selectable: 40 interactable, 41 enabled / Canvas: 50 sortingOrder, 51 scaleFactor
        //        Slider: 60 value, 61 min, 62 max, 63 wholeNumbers, 64 normalizedValue, 65 value(알림 없이)
        //        Toggle: 70 isOn, 71 isOn(알림 없이) / InputField: 75 characterLimit, 76 isFocused / ScrollRect: 80 normalizedPosition
        //  문자열: 0 Text.text, 1 Text.font, 2 Image.sprite, 3 InputField.text, 4 InputField.text(알림 없이)
        public delegate* unmanaged<ulong, int, Vector4*, int> UI_GetVec;
        public delegate* unmanaged<ulong, int, Vector4*, void> UI_SetVec;
        public delegate* unmanaged<ulong, int, byte*> UI_GetString;
        public delegate* unmanaged<ulong, int, byte*, void> UI_SetString;

        // 씬 / 앱 (SceneManagement, Application)
        public delegate* unmanaged<byte*, int, int> Scene_Load;          // 이름·경로 (null 이면 index) → 1 = 시작함
        public delegate* unmanaged<int*, byte*> Scene_Active;            // 활성 씬 경로, buildIndex
        public delegate* unmanaged<int, byte*> Scene_PathAt;             // 빌드 목록 i 번째 경로 (없으면 null)
        public delegate* unmanaged<int> Scene_Count;                     // 빌드 목록 수
        public delegate* unmanaged<int, int> App_Info;                   // 0 = 플레이어인지
        public delegate* unmanaged<byte*> App_ProductName;
        public delegate* unmanaged<void> App_Quit;

        // Particle System
        //  Call: 0 Play(arg 자식 포함), 1 Stop(arg 비트 1 자식 포함, 2 지우기), 2 Pause, 3 Clear, 4 Emit(arg 개수), 5 Restart
        //  float: 0 time, 1 particleCount, 2 duration, 3 simulationSpeed, 4 maxParticles, 5 isPlaying, 6 isPaused, 7 isStopped,
        //         8 isEmitting, 9 loop, 10 playOnAwake, 11 simulationSpace, 12 emission.enabled, 13 shape.enabled, 14 shape.radius,
        //         15 shape.angle, 16 shape.shapeType(NOVA 순서), 17 IsAlive(자식 포함), 18 shape.arc, 19 colorOverLifetime.enabled, 20 IsAlive(자신만),
        //         21 trails.enabled, 22 collision.enabled, 23 subEmitters.enabled
        //  곡선: 0 startDelay, 1 startLifetime, 2 startSpeed, 3 startSize, 4 startRotation(도), 5 gravityModifier, 6 rateOverTime, 7 rateOverDistance
        //  색: 0 startColor, 1 colorOverLifetime
        public delegate* unmanaged<ulong, int, int, void> PS_Call;
        public delegate* unmanaged<ulong, int, float> PS_GetFloat;
        public delegate* unmanaged<ulong, int, float, void> PS_SetFloat;
        public delegate* unmanaged<ulong, int, Vector4*, void> PS_GetCurve;           // x mode, y min, z max, w multiplier
        public delegate* unmanaged<ulong, int, int, float, float, void> PS_SetCurve;  // mode, min, max
        public delegate* unmanaged<ulong, int, Vector4*, Vector4*, int> PS_GetColor;  // → mode
        public delegate* unmanaged<ulong, int, int, Vector4*, Vector4*, void> PS_SetColor;

        // Character Controller — float: 0 slopeLimit, 1 stepOffset, 2 skinWidth, 3 minMoveDistance, 4 radius, 5 height
        //  vector: 0 velocity, 1 center / int: 0 collisionFlags, 1 isGrounded, 2 detectCollisions
        public delegate* unmanaged<ulong, Vector3*, float, int, int> CC_Move;   // simple 1 = SimpleMove (→ isGrounded)
        public delegate* unmanaged<ulong, int, float> CC_GetFloat;
        public delegate* unmanaged<ulong, int, float, void> CC_SetFloat;
        public delegate* unmanaged<ulong, int, Vector3*, void> CC_GetVector;
        public delegate* unmanaged<ulong, int, Vector3*, void> CC_SetVector;
        public delegate* unmanaged<ulong, int, int> CC_GetInt;
        public delegate* unmanaged<ulong, int, int, void> CC_SetInt;
        public delegate* unmanaged<ulong, int, ControllerHitData*, int> CC_GetHit;

        // Joint (kind 0 Fixed, 1 Hinge, 2 Spring) — 번호는 ScriptBindings.cpp 의 JT_GetFloat 설명
        public delegate* unmanaged<ulong, int, int, float> JT_GetFloat;
        public delegate* unmanaged<ulong, int, int, float, void> JT_SetFloat;
        public delegate* unmanaged<ulong, int, int, Vector3*, void> JT_GetVector;
        public delegate* unmanaged<ulong, int, int, Vector3*, void> JT_SetVector;
        public delegate* unmanaged<ulong, int, ulong> JT_GetConnected;
        public delegate* unmanaged<ulong, int, ulong, void> JT_SetConnected;

        // AudioSource.outputAudioMixerGroup ("믹서|그룹") · AudioMixer (경로로)
        public delegate* unmanaged<ulong, byte*> AS_GetOutput;
        public delegate* unmanaged<ulong, byte*, byte*, void> AS_SetOutput;
        public delegate* unmanaged<byte*, int> MX_Load;
        public delegate* unmanaged<byte*, byte*, float, int> MX_SetFloat;
        public delegate* unmanaged<byte*, byte*, float*, int> MX_GetFloat;
        public delegate* unmanaged<byte*, byte*, int> MX_ClearFloat;
        public delegate* unmanaged<byte*, byte*, float, int> MX_Transition;
        public delegate* unmanaged<byte*, int, byte*> MX_Names;
        public delegate* unmanaged<byte*, byte*, float> MX_GroupLevel;

        // Text (TextMeshPro 기능): textInfo · 링크 · 글자 애니메이션 (번호표 = UIScriptBindings.h)
        public delegate* unmanaged<ulong, int, int, float*, int, int> TX_Info;
        public delegate* unmanaged<ulong, int, int, byte*> TX_Link;
        public delegate* unmanaged<float, float, ulong> UI_RaycastScreen;

        // SkinnedMeshRenderer BlendShape
        public delegate* unmanaged<ulong, int> SMR_Count;
        public delegate* unmanaged<ulong, int, byte*> SMR_Name;
        public delegate* unmanaged<ulong, byte*, int> SMR_Index;
        public delegate* unmanaged<ulong, int, float> SMR_GetWeight;
        public delegate* unmanaged<ulong, int, float, void> SMR_SetWeight;

        // SpriteRenderer
        public delegate* unmanaged<ulong, float*, int> SR_GetColor;
        public delegate* unmanaged<ulong, float*, void> SR_SetColor;
        public delegate* unmanaged<ulong, int, int> SR_GetInt;
        public delegate* unmanaged<ulong, int, int, void> SR_SetInt;
        public delegate* unmanaged<ulong, byte*> SR_GetSprite;
        public delegate* unmanaged<ulong, byte*, void> SR_SetSprite;

        // 레이어
        public delegate* unmanaged<ulong, int> GO_GetLayer;
        public delegate* unmanaged<ulong, int, void> GO_SetLayer;
        public delegate* unmanaged<byte*, int> LM_NameToLayer;
        public delegate* unmanaged<int, byte*> LM_LayerToName;
        public delegate* unmanaged<Vector3*, Vector3*, float, int, int, RaycastData*, int> PH_RaycastMask;
        public delegate* unmanaged<int, int, int, void> PH_IgnoreLayer;
        public delegate* unmanaged<int, int, int> PH_GetIgnoreLayer;
        public delegate* unmanaged<Vector3*, void> PH_GetGravity;
        public delegate* unmanaged<Vector3*, void> PH_SetGravity;

        // Sprite Animator + Sorting Layer
        public delegate* unmanaged<ulong, byte*, int> SA_Play;
        public delegate* unmanaged<ulong, void> SA_Stop;
        public delegate* unmanaged<ulong, int, int> SA_GetInt;
        public delegate* unmanaged<ulong, byte*> SA_Clip;
        public delegate* unmanaged<ulong, float> SA_GetSpeed;
        public delegate* unmanaged<ulong, float, void> SA_SetSpeed;
        public delegate* unmanaged<ulong, byte*> SR_GetSortingLayer;
        public delegate* unmanaged<ulong, byte*, int> SR_SetSortingLayer;

        // Camera / Light cullingMask
        public delegate* unmanaged<ulong, int, int> CL_GetMask;
        public delegate* unmanaged<ulong, int, int, void> CL_SetMask;

        // 2D 물리
        public delegate* unmanaged<ulong, int, Vector2*, void> R2_GetVec;
        public delegate* unmanaged<ulong, int, Vector2*, void> R2_SetVec;
        public delegate* unmanaged<ulong, int, float> R2_GetFloat;
        public delegate* unmanaged<ulong, int, float, void> R2_SetFloat;
        public delegate* unmanaged<ulong, int, Vector2*, Vector2*, float, int, void> R2_Act;
        public delegate* unmanaged<ulong, byte*, int, float*, int> C2_Get;
        public delegate* unmanaged<ulong, byte*, int, float*, void> C2_Set;
        public delegate* unmanaged<Vector2*, Vector2*, float, int, Ray2DData*, int> P2_Raycast;
        public delegate* unmanaged<int, Vector2*, Vector2*, float, int, ulong> P2_Overlap;
        public delegate* unmanaged<int, Vector2*, void> P2_Gravity;
        public delegate* unmanaged<int, int, int, void> P2_IgnoreLayer;
        public delegate* unmanaged<int, int, int> P2_GetIgnoreLayer;
        public delegate* unmanaged<ulong, ulong, float*, int> P2_Contact;
        public delegate* unmanaged<ulong, int, int, int, float> J2_GetFloat;
        public delegate* unmanaged<ulong, int, int, int, float, void> J2_SetFloat;
        public delegate* unmanaged<ulong, int, int, int, Vector2*, void> J2_GetVec;
        public delegate* unmanaged<ulong, int, int, int, Vector2*, void> J2_SetVec;
        public delegate* unmanaged<ulong, int, int, ulong> J2_GetConnected;
        public delegate* unmanaged<ulong, int, int, ulong, void> J2_SetConnected;
        public delegate* unmanaged<ulong, int, int, int> J2_Find;
        public delegate* unmanaged<ulong, int, int, void> J2_Remove;
    }

    internal static unsafe class Native
    {
        internal static NativeApiTable Api;

        // 네이티브가 돌려주는 UTF-8 문자열 (네이티브 쪽 버퍼, 다음 호출 전까지 유효) → C# 문자열
        internal static string Str(byte* p) => p == null ? null : Marshal.PtrToStringUTF8((IntPtr)p);

        // C# 문자열 → 널 끝 UTF-8 (호출하는 동안만 쓰는 임시 버퍼)
        internal static byte[] Utf8(string s) => Encoding.UTF8.GetBytes((s ?? string.Empty) + "\0");
    }
}
