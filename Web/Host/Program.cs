using System;
using System.Runtime.InteropServices;
using NovaEngine.Interop;

// NOVA 웹 플레이어의 C# 진입점: .NET 런타임이 뜬 뒤 (셸의 dotnet.runMain) 엔진에 Bridge 진입점 주소를 넘기고 엔진을 시작한다.
//  엔진은 브라우저 프레임 루프 (requestAnimationFrame) 로 계속 돌고 Main 은 바로 돌아온다 (런타임은 남는다)
//  주소의 차례 = Android/Source/Engine/ScriptEngineAndroid.cpp 의 웹 InitRuntime
public static unsafe class NovaWebHost
{
    [DllImport("NovaWeb")] static extern void nova_web_set_managed(IntPtr* entries, int count);
    [DllImport("NovaWeb")] static extern int nova_web_start();

    public static int Main()
    {
        IntPtr* e = stackalloc IntPtr[18];
        e[0] = (IntPtr)(delegate* unmanaged<IntPtr, int>)&Bridge.Initialize;
        e[1] = (IntPtr)(delegate* unmanaged<void>)&Bridge.BeginFrame;
        e[2] = (IntPtr)(delegate* unmanaged<IntPtr, void>)&Bridge.FreeString;
        e[3] = (IntPtr)(delegate* unmanaged<byte*, int>)&Bridge.LoadGameAssembly;
        e[4] = (IntPtr)(delegate* unmanaged<void>)&Bridge.UnloadGameAssembly;
        e[5] = (IntPtr)(delegate* unmanaged<IntPtr>)&Bridge.GetClassesJson;
        e[6] = (IntPtr)(delegate* unmanaged<byte*, ulong, IntPtr, byte*, int, IntPtr>)&Bridge.CreateInstance;
        e[7] = (IntPtr)(delegate* unmanaged<IntPtr, void>)&Bridge.DestroyInstance;
        e[8] = (IntPtr)(delegate* unmanaged<IntPtr, int, void>)&Bridge.Invoke;
        e[9] = (IntPtr)(delegate* unmanaged<IntPtr, int, void>)&Bridge.SetEnabled;
        e[10] = (IntPtr)(delegate* unmanaged<IntPtr, int, int, ulong, void>)&Bridge.InvokeCollision;
        e[11] = (IntPtr)(delegate* unmanaged<IntPtr, IntPtr>)&Bridge.GetFieldsJson;
        e[12] = (IntPtr)(delegate* unmanaged<IntPtr, byte*, void>)&Bridge.SetFieldsJson;
        e[13] = (IntPtr)(delegate* unmanaged<ulong, byte*, byte*, byte*, int>)&Bridge.InvokeMethod;
        e[14] = (IntPtr)(delegate* unmanaged<ulong, int, float, byte*, void>)&Bridge.InvokeUIEvent;
        e[15] = (IntPtr)(delegate* unmanaged<int, void>)&AppEvents.Pause;
        e[16] = (IntPtr)(delegate* unmanaged<int, void>)&AppEvents.Focus;
        e[17] = (IntPtr)(delegate* unmanaged<int, int, int, void>)&AppEvents.SceneEvent;
        nova_web_set_managed(e, 18);
        int ok = nova_web_start();
        if (ok == 0) Console.Error.WriteLine("NOVA: the engine could not start");
        return 0;
    }
}
