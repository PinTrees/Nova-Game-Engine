# 테스트용 효과음 WAV 를 만든다 (엔진 패키지: Resources/Packages/Audio/SFX).
#  python Tools/make_test_audio.py
# 모두 직접 합성한 소리라 저작권 걱정이 없다. 44.1kHz 16비트 모노 (BGM 은 스테레오).
import math, os, random, struct, wave

RATE = 44100
OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'Resources', 'Packages', 'Audio', 'SFX')


def env(t, dur, attack=0.005, release=0.08):
    a = min(1.0, t / attack) if attack > 0 else 1.0
    r = min(1.0, (dur - t) / release) if release > 0 else 1.0
    return max(0.0, min(a, r))


def write(name, samples, channels=1):
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, name + '.wav')
    with wave.open(path, 'wb') as w:
        w.setnchannels(channels)
        w.setsampwidth(2)
        w.setframerate(RATE)
        frames = bytearray()
        for s in samples:
            frames += struct.pack('<h', int(max(-1.0, min(1.0, s)) * 32000))
        w.writeframes(bytes(frames))
    print(f'{path}  ({len(samples) // channels / RATE:.2f}s)')


def beep():  # 880Hz 짧은 삐
    dur = 0.25
    return [0.6 * math.sin(2 * math.pi * 880 * i / RATE) * env(i / RATE, dur) for i in range(int(dur * RATE))]


def coin():  # 두 음 (B5 → E6) 사각파 느낌
    out = []
    for f, d in ((988, 0.08), (1319, 0.32)):
        for i in range(int(d * RATE)):
            t = i / RATE
            s = math.sin(2 * math.pi * f * t) + 0.3 * math.sin(2 * math.pi * f * 3 * t)
            out.append(0.4 * s * env(t, d, 0.002, 0.25 if d > 0.1 else 0.01))
    return out


def jump():  # 올라가는 스윕
    dur, phase, out = 0.35, 0.0, []
    for i in range(int(dur * RATE)):
        t = i / RATE
        f = 220 + 900 * (t / dur) ** 0.7
        phase += 2 * math.pi * f / RATE
        out.append(0.45 * (1 if math.sin(phase) > 0 else -1) * 0.6 * env(t, dur, 0.003, 0.12))
    return out


def explosion():  # 낮은 노이즈가 길게 사라짐
    random.seed(7)
    dur, lp, out = 1.2, 0.0, []
    for i in range(int(dur * RATE)):
        t = i / RATE
        lp += (random.uniform(-1, 1) - lp) * (0.08 if t < 0.1 else 0.03)
        boom = math.sin(2 * math.pi * (60 - 30 * t) * t)
        out.append((0.9 * lp * 4 + 0.5 * boom) * math.exp(-3.2 * t) * env(t, dur, 0.002, 0.2))
    return out


def click():  # UI 클릭
    dur = 0.04
    return [0.7 * math.sin(2 * math.pi * 2000 * i / RATE) * math.exp(-120 * i / RATE) for i in range(int(dur * RATE))]


def bgm_loop():  # 2초 반복 아르페지오 (스테레오, 이음새 없이 반복되도록 길이를 박자에 맞춤)
    notes = [261.63, 329.63, 392.00, 523.25, 392.00, 329.63, 293.66, 349.23]
    step = 0.25
    out = []
    for n, f in enumerate(notes):
        for i in range(int(step * RATE)):
            t = i / RATE
            s = 0.3 * math.sin(2 * math.pi * f * t) * env(t, step, 0.005, 0.1)
            bass = 0.15 * math.sin(2 * math.pi * (f / 4) * (t + n * step))
            pan = 0.5 + 0.4 * math.sin(n * 0.8)
            out += [(s + bass) * (1 - pan) * 1.4, (s + bass) * pan * 1.4]
    return out


if __name__ == '__main__':
    write('Beep', beep())
    write('Coin', coin())
    write('Jump', jump())
    write('Explosion', explosion())
    write('Click', click())
    write('BGM_Loop', bgm_loop(), channels=2)
