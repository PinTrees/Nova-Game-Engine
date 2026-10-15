namespace NovaEngine
{
    /// <summary>
    /// Unity 의 AnimationEvent: 클립의 정한 시간에 Animator 가 이 GameObject 의 스크립트에서 functionName 메서드를 부른다.
    /// 받는 메서드: 인자 없음 · AnimationEvent · float (floatParameter) · int / enum (intParameter) · string (stringParameter) · bool.
    /// 이벤트는 모델 파일 옆 <c>&lt;모델&gt;.animevents</c> 에 저장한다 (Animator 창 상태 Inspector 의 Events, <c>nova animevent</c>).
    /// </summary>
    public sealed class AnimationEvent
    {
        public string functionName = "";
        public string stringParameter = "";
        public float floatParameter;
        public int intParameter;
        /// <summary>클립 안의 시간 (초)</summary>
        public float time;
        public bool isFiredByAnimator;
        public bool isFiredByLegacy => !isFiredByAnimator;
        /// <summary>NOVA: 이벤트가 든 클립 · 상태 이름, 레이어 번호, 그 상태의 섞인 무게 (Unity 의 animatorClipInfo.weight)</summary>
        public string clipName = "";
        public string stateName = "";
        public int layer;
        public float weight = 1f;
    }
}
