# com.nova.weather 의 소리를 수식으로 만든다 (녹음 · 외부 에셋 없이): 빗소리 두 가지 · 바람 · 천둥 셋.
#  python Tools/gen_weather_audio.py  → Packages/com.nova.weather/Resources/Audio/*.wav (44.1 kHz 16 비트 스테레오)
#  고리 소리 (rain_light · rain_heavy · wind) 는 끝과 처음을 겹쳐 이어 붙여 반복해도 이음매가 들리지 않는다
import os
import numpy as np
from scipy import signal
from scipy.io import wavfile

SR = 44100
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'Packages', 'com.nova.weather', 'Resources', 'Audio')
rng = np.random.default_rng(20261006)


def band(x, lo, hi, order=4):
    if lo <= 0:
        sos = signal.butter(order, hi, 'lowpass', fs=SR, output='sos')
    elif hi >= SR / 2:
        sos = signal.butter(order, lo, 'highpass', fs=SR, output='sos')
    else:
        sos = signal.butter(order, [lo, hi], 'bandpass', fs=SR, output='sos')
    return signal.sosfilt(sos, x, axis=0)


def pink(n, ch=2):
    # 1/f 잡음 (주파수 영역에서)
    w = rng.standard_normal((n, ch))
    f = np.fft.rfft(w, axis=0)
    k = np.arange(f.shape[0]); k[0] = 1
    f /= np.sqrt(k)[:, None]
    x = np.fft.irfft(f, n=n, axis=0)
    return x / np.max(np.abs(x))


def loop(x, fade):
    # 끝 fade 표본을 처음과 겹쳐 섞는다 → 길이 n - fade 의 이음매 없는 고리
    n = x.shape[0] - fade
    t = np.linspace(0, 1, fade)[:, None]
    head = x[:fade] * t + x[n:] * (1 - t)
    return np.concatenate([head, x[fade:n]], axis=0)


def drops(n, rate, ch=2, decay=0.004, lo=1500, hi=9000):
    # 빗방울 하나하나의 짧은 '톡' (지붕 · 잎에 부딪힘)
    out = np.zeros((n, ch))
    count = int(n / SR * rate)
    length = int(SR * decay * 6)
    env = np.exp(-np.arange(length) / (SR * decay))
    for _ in range(count):
        at = rng.integers(0, n - length)
        c = rng.integers(0, ch)
        tick = rng.standard_normal(length) * env * rng.uniform(0.2, 1.0)
        out[at:at + length, c] += tick
    return band(out, lo, hi, 2)


def save(name, x, gain=0.9):
    x = x / np.max(np.abs(x)) * gain
    os.makedirs(OUT, exist_ok=True)
    wavfile.write(os.path.join(OUT, name), SR, (x * 32767).astype(np.int16))
    print(name, '%.1f s' % (x.shape[0] / SR))


def rain_light():
    n = SR * 8 + SR // 2
    hiss = band(pink(n), 2500, 12000) * 0.35
    tick = drops(n, 900) * 1.2
    patter = band(pink(n), 400, 2500) * 0.12
    return loop(hiss + tick + patter, SR // 2)


def rain_heavy():
    n = SR * 8 + SR // 2
    roar = band(pink(n), 250, 9000) * 0.8
    low = band(pink(n), 60, 400) * 0.35
    tick = drops(n, 2600, lo=800, hi=7000) * 0.9
    # 빗줄기 세기가 천천히 오르내린다
    t = np.arange(n) / SR
    swell = (1.0 + 0.18 * np.sin(2 * np.pi * t / 4.25 + 1.3))[:, None]
    return loop((roar + low + tick) * swell, SR // 2)


def wind():
    n = SR * 12 + SR
    base = pink(n)
    t = np.arange(n) / SR
    out = np.zeros((n, 2))
    # 세기 · 높이가 바뀌는 돌풍: 여러 대역을 따로 흔든다
    for lo, hi, amp, period in ((80, 350, 0.9, 6.0), (250, 900, 0.55, 4.0), (700, 2200, 0.25, 3.0)):
        b = band(base, lo, hi, 2)
        g = 0.55 + 0.45 * np.sin(2 * np.pi * t / period + rng.uniform(0, 6.28))
        g2 = 0.7 + 0.3 * np.sin(2 * np.pi * t / (period * 2.7) + rng.uniform(0, 6.28))
        out += b * (g * g2)[:, None] * amp
    return loop(out, SR)


def thunder(seed, length=7.5, crack=True):
    local = np.random.default_rng(seed)
    n = int(SR * length)
    t = np.arange(n) / SR
    out = np.zeros((n, 2))
    if crack:
        # 가까운 벼락: 날카로운 시작 (몇 번 갈라지며)
        for k in range(local.integers(2, 5)):
            at = int(SR * (0.02 + k * local.uniform(0.04, 0.12)))
            ln = int(SR * 0.35)
            env = np.exp(-np.arange(ln) / (SR * 0.06))
            burst = band(local.standard_normal((ln, 2)), 300, 9000, 2) * env[:, None] * local.uniform(0.6, 1.0)
            out[at:at + ln] += burst
    # 우르릉: 아주 낮은 잡음, 느리게 오르내리며 길게 사라진다
    rum = band(pink(n), 25, 180, 2) * 1.6
    rum2 = band(pink(n), 120, 600, 2) * 0.5
    env = np.minimum(t / 0.25, 1.0) * np.exp(-t / (length * 0.33))
    wob = 0.6 + 0.4 * np.abs(np.sin(2 * np.pi * t * local.uniform(0.6, 1.4) + local.uniform(0, 6)))
    out += (rum + rum2) * (env * wob)[:, None]
    fade = np.minimum(1.0, (n - np.arange(n)) / (SR * 0.6))
    return out * fade[:, None]


if __name__ == '__main__':
    save('rain_light.wav', rain_light(), 0.7)
    save('rain_heavy.wav', rain_heavy(), 0.85)
    save('wind.wav', wind(), 0.8)
    save('thunder_1.wav', thunder(11, 7.5, True), 0.95)
    save('thunder_2.wav', thunder(23, 8.5, True), 0.95)
    save('thunder_3.wav', thunder(37, 9.0, False), 0.9)
