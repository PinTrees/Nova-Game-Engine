using System.Runtime.InteropServices;

namespace NovaEngine.AI
{
    // com.nova.ai.navigation: Unity 의 UnityEngine.AI (NavMeshAgent, NavMesh, NavMeshPath) + Unity.AI.Navigation 의 NavMeshSurface
    internal static unsafe class NavNative
    {
        const string Dll = "NovaNavigation";
        [DllImport(Dll)] internal static extern float NavAgent_GetFloat(ulong go, int prop);
        [DllImport(Dll)] internal static extern void NavAgent_SetFloat(ulong go, int prop, float v);
        [DllImport(Dll)] internal static extern int NavAgent_GetBool(ulong go, int prop);
        [DllImport(Dll)] internal static extern void NavAgent_SetBool(ulong go, int prop, int v);
        [DllImport(Dll)] internal static extern void NavAgent_GetVector(ulong go, int prop, Vector3* v);
        [DllImport(Dll)] internal static extern void NavAgent_SetVelocity(ulong go, Vector3* v);
        [DllImport(Dll)] internal static extern int NavAgent_SetDestination(ulong go, Vector3* target);
        [DllImport(Dll)] internal static extern void NavAgent_ResetPath(ulong go);
        [DllImport(Dll)] internal static extern int NavAgent_Warp(ulong go, Vector3* p);
        [DllImport(Dll)] internal static extern int NavAgent_GetCorners(ulong go, Vector3* outCorners, int max);
        [DllImport(Dll)] internal static extern int NavMesh_CalculatePath(Vector3* start, Vector3* end, Vector3* outCorners, int max);
        [DllImport(Dll)] internal static extern int NavMesh_SamplePosition(Vector3* p, float maxDistance, Vector3* outPos);
        [DllImport(Dll)] internal static extern int NavSurface_Build(ulong go);
    }

    public enum NavMeshPathStatus { PathComplete = 0, PathPartial = 1, PathInvalid = 2 }

    public sealed class NavMeshPath
    {
        public Vector3[] corners { get; internal set; } = new Vector3[0];
        public NavMeshPathStatus status { get; internal set; } = NavMeshPathStatus.PathInvalid;
        public void ClearCorners() { corners = new Vector3[0]; status = NavMeshPathStatus.PathInvalid; }
    }

    public struct NavMeshHit
    {
        public Vector3 position;
        public float distance;
        public bool hit;
    }

    public static class NavMesh
    {
        public const int AllAreas = -1;

        public static unsafe bool CalculatePath(Vector3 sourcePosition, Vector3 targetPosition, int areaMask, NavMeshPath path)
        {
            var buf = new Vector3[512];
            int n;
            fixed (Vector3* b = buf) n = NavNative.NavMesh_CalculatePath(&sourcePosition, &targetPosition, b, buf.Length);
            if (path == null) return n > 0;
            if (n <= 0) { path.ClearCorners(); return false; }
            var c = new Vector3[System.Math.Min(n, buf.Length)];
            System.Array.Copy(buf, c, c.Length);
            path.corners = c;
            path.status = NavMeshPathStatus.PathComplete;
            return true;
        }

        public static unsafe bool SamplePosition(Vector3 sourcePosition, out NavMeshHit hit, float maxDistance, int areaMask)
        {
            Vector3 p;
            bool ok = NavNative.NavMesh_SamplePosition(&sourcePosition, maxDistance, &p) != 0;
            hit = new NavMeshHit { position = ok ? p : sourcePosition, distance = ok ? Vector3.Distance(sourcePosition, p) : float.PositiveInfinity, hit = ok };
            return ok;
        }
    }

    [NativeComponent("NavMeshAgent")]
    public sealed class NavMeshAgent : Component
    {
        internal NavMeshAgent() { }
        float F(int p) => NavNative.NavAgent_GetFloat(nativeId, p);
        void SetF(int p, float v) => NavNative.NavAgent_SetFloat(nativeId, p, v);
        bool B(int p) => NavNative.NavAgent_GetBool(nativeId, p) != 0;
        unsafe Vector3 V(int p) { Vector3 v; NavNative.NavAgent_GetVector(nativeId, p, &v); return v; }

        public float speed { get => F(0); set => SetF(0, value); }
        public float angularSpeed { get => F(1); set => SetF(1, value); }
        public float acceleration { get => F(2); set => SetF(2, value); }
        public float stoppingDistance { get => F(3); set => SetF(3, value); }
        public float radius { get => F(4); set => SetF(4, value); }
        public float height { get => F(5); set => SetF(5, value); }
        public float baseOffset { get => F(6); set => SetF(6, value); }
        /// <summary>회피 우선순위 0~99 (낮을수록 덜 밀린다)</summary>
        public int avoidancePriority { get => (int)F(8); set => SetF(8, value); }
        /// <summary>남은 길이 (경로가 없으면 Infinity)</summary>
        public float remainingDistance => F(7);
        public bool isStopped { get => B(0); set => NavNative.NavAgent_SetBool(nativeId, 0, value ? 1 : 0); }
        public bool hasPath => B(1);
        public bool autoBraking { get => B(2); set => NavNative.NavAgent_SetBool(nativeId, 2, value ? 1 : 0); }
        public bool isOnNavMesh => B(3);
        public bool pathPending => false;
        public Vector3 destination { get => V(0); set => SetDestination(value); }
        public unsafe Vector3 velocity { get => V(1); set => NavNative.NavAgent_SetVelocity(nativeId, &value); }
        public Vector3 steeringTarget => V(2);

        public unsafe bool SetDestination(Vector3 target) => NavNative.NavAgent_SetDestination(nativeId, &target) != 0;
        public void ResetPath() => NavNative.NavAgent_ResetPath(nativeId);
        public unsafe bool Warp(Vector3 newPosition) => NavNative.NavAgent_Warp(nativeId, &newPosition) != 0;
        public unsafe NavMeshPath path
        {
            get
            {
                var buf = new Vector3[512];
                int n;
                fixed (Vector3* b = buf) n = NavNative.NavAgent_GetCorners(nativeId, b, buf.Length);
                var p = new NavMeshPath();
                if (n > 0)
                {
                    var c = new Vector3[System.Math.Min(n, buf.Length)];
                    System.Array.Copy(buf, c, c.Length);
                    p.corners = c;
                    p.status = NavMeshPathStatus.PathComplete;
                }
                return p;
            }
        }
    }

    [NativeComponent("NavMeshSurface")]
    public sealed class NavMeshSurface : Component
    {
        internal NavMeshSurface() { }
        /// <summary>지금 씬으로 다시 굽는다 (Play 중에도 — 지형이 바뀐 뒤 등)</summary>
        public void BuildNavMesh() => NavNative.NavSurface_Build(nativeId);
    }
}
