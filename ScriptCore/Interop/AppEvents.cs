using System;
using System.Reflection;
using System.Runtime.InteropServices;
using NovaEngine;

namespace NovaEngine.Interop
{
    // 앱 상태 메시지 (Unity 의 OnApplicationPause · OnApplicationFocus): 네이티브 (ScriptEngine::OnApplicationPause / Focus) 가 부른다.
    //  안드로이드 = 앱이 뒤로 / 앞으로, Windows 빌드된 게임 = 창 활성 바뀜. 모든 스크립트 인스턴스의 같은 이름 메서드 (bool 하나) 를 부른다
    public static class AppEvents
    {
        [UnmanagedCallersOnly]
        public static void Pause(int paused) => Send("OnApplicationPause", paused != 0);

        [UnmanagedCallersOnly]
        public static void Focus(int focused) => Send("OnApplicationFocus", focused != 0);

        static void Send(string message, bool value)
        {
            const BindingFlags flags = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic;
            foreach (MonoBehaviour mb in ScriptRegistry.All())
            {
                MethodInfo m = mb.GetType().GetMethod(message, flags, null, new[] { typeof(bool) }, null);
                if (m == null) continue;
                try { m.Invoke(mb, new object[] { value }); }
                catch (TargetInvocationException e) { Debug.LogException(e.InnerException ?? e); }
                catch (Exception e) { Debug.LogException(e); }
            }
        }
    }
}
