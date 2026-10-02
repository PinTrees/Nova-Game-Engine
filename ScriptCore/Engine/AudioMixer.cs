using System;
using System.Collections.Generic;
using NovaEngine.Interop;

namespace NovaEngine.Audio
{
    /// <summary>
    /// Unity 의 AudioMixer (.mixer 에셋). 노출한 파라미터를 SetFloat 로 바꾸고, 스냅숏으로 부드럽게 전환한다.
    /// NOVA: 스크립트에서는 AudioMixer.Load("Assets/Main.mixer") 또는 audioSource.outputAudioMixerGroup.audioMixer 로 얻는다.
    /// </summary>
    public sealed class AudioMixer
    {
        internal readonly string m_Path;
        internal AudioMixer(string path) { m_Path = path; }

        public string name => System.IO.Path.GetFileNameWithoutExtension(m_Path);

        /// <summary>NOVA: 경로로 믹서를 불러온다 (없으면 null)</summary>
        public static unsafe AudioMixer Load(string path)
        {
            fixed (byte* p = Native.Utf8(path)) return Native.Api.MX_Load(p) != 0 ? new AudioMixer(path) : null;
        }

        /// <summary>노출한 파라미터 값 (볼륨은 dB). 스냅숏보다 우선한다</summary>
        public unsafe bool SetFloat(string name, float value)
        {
            fixed (byte* p = Native.Utf8(m_Path)) fixed (byte* n = Native.Utf8(name)) return Native.Api.MX_SetFloat(p, n, value) != 0;
        }

        public unsafe bool GetFloat(string name, out float value)
        {
            float v;
            bool ok;
            fixed (byte* p = Native.Utf8(m_Path)) fixed (byte* n = Native.Utf8(name)) ok = Native.Api.MX_GetFloat(p, n, &v) != 0;
            value = v;
            return ok;
        }

        /// <summary>SetFloat 를 풀어 다시 스냅숏 값을 따른다</summary>
        public unsafe bool ClearFloat(string name)
        {
            fixed (byte* p = Native.Utf8(m_Path)) fixed (byte* n = Native.Utf8(name)) return Native.Api.MX_ClearFloat(p, n) != 0;
        }

        public AudioMixerSnapshot FindSnapshot(string name)
        {
            foreach (var s in Names(1))
                if (s == name) return new AudioMixerSnapshot(this, s);
            return null;
        }

        /// <summary>이름에 subPath 가 들어간 그룹 (Unity 는 "Master/SFX" 경로, NOVA 는 그룹 이름)</summary>
        public AudioMixerGroup[] FindMatchingGroups(string subPath)
        {
            var list = new List<AudioMixerGroup>();
            string last = subPath ?? "";
            int slash = last.LastIndexOf('/');
            if (slash >= 0) last = last.Substring(slash + 1);
            foreach (var g in Names(0))
                if (last.Length == 0 || g.IndexOf(last, StringComparison.OrdinalIgnoreCase) >= 0) list.Add(new AudioMixerGroup(this, g));
            return list.ToArray();
        }

        /// <summary>NOVA: 그룹의 최근 출력 최대 레벨 (dB, 소리가 없으면 -80)</summary>
        public unsafe float GetGroupLevel(string group)
        {
            fixed (byte* p = Native.Utf8(m_Path)) fixed (byte* g = Native.Utf8(group)) return Native.Api.MX_GroupLevel(p, g);
        }

        internal unsafe string[] Names(int what)
        {
            string s;
            fixed (byte* p = Native.Utf8(m_Path)) s = Native.Str(Native.Api.MX_Names(p, what));
            return string.IsNullOrEmpty(s) ? Array.Empty<string>() : s.TrimEnd('\n').Split('\n');
        }

        public override bool Equals(object obj) => obj is AudioMixer m && m.m_Path == m_Path;
        public override int GetHashCode() => m_Path.GetHashCode();
    }

    public sealed class AudioMixerSnapshot
    {
        public AudioMixer audioMixer { get; }
        public string name { get; }
        internal AudioMixerSnapshot(AudioMixer mixer, string name) { audioMixer = mixer; this.name = name; }

        /// <summary>timeToReach 초 동안 이 스냅숏의 값으로 옮겨 간다</summary>
        public unsafe void TransitionTo(float timeToReach)
        {
            fixed (byte* p = Native.Utf8(audioMixer.m_Path)) fixed (byte* n = Native.Utf8(name)) Native.Api.MX_Transition(p, n, timeToReach);
        }
    }

    public sealed class AudioMixerGroup
    {
        public AudioMixer audioMixer { get; }
        public string name { get; }
        internal AudioMixerGroup(AudioMixer mixer, string name) { audioMixer = mixer; this.name = name; }
    }
}
