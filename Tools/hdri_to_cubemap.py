# 등장방형(equirectangular) Radiance .hdr → 큐브맵 DDS (R9G9B9E5_SHAREDEXP, 밉 전체)
#  - 엔진 파이프라인이 감마 공간이라 값은 pow(1/2.2) 로 넣는다 (HDR 이라 1 을 넘는 값도 남는다)
#  - 노출: 위쪽 하늘 밝기의 중간값이 --sky-median 이 되도록 맞추고, 태양처럼 아주 밝은 값은 --clamp 로 자른다
#  사용: python Tools/hdri_to_cubemap.py in.hdr out.dds [--size 512]
import sys, struct, argparse
import numpy as np


def read_hdr(path):
    data = open(path, 'rb').read()
    pos = 0
    # 헤더: 빈 줄까지, 다음 줄이 "-Y h +X w"
    while True:
        end = data.index(b'\n', pos)
        line = data[pos:end]
        pos = end + 1
        if line.strip() == b'':
            break
    end = data.index(b'\n', pos)
    parts = data[pos:end].split()
    pos = end + 1
    h, w = int(parts[1]), int(parts[3])
    img = np.zeros((h, w, 4), np.uint8)
    buf = np.frombuffer(data, np.uint8)
    for y in range(h):
        if buf[pos] == 2 and buf[pos + 1] == 2:   # 새 형식 RLE
            pos += 4
            for c in range(4):
                x = 0
                while x < w:
                    n = int(buf[pos]); pos += 1
                    if n > 128:
                        n -= 128
                        img[y, x:x + n, c] = buf[pos]; pos += 1
                    else:
                        img[y, x:x + n, c] = buf[pos:pos + n]; pos += n
                    x += n
        else:
            img[y] = buf[pos:pos + w * 4].reshape(w, 4); pos += w * 4
    e = img[..., 3].astype(np.int32)
    scale = np.where(e == 0, 0.0, np.ldexp(1.0, e - 136)).astype(np.float32)
    return img[..., :3].astype(np.float32) * scale[..., None]


def sample_equirect(eq, d):
    h, w, _ = eq.shape
    x, y, z = d[..., 0], d[..., 1], d[..., 2]
    u = 0.5 + np.arctan2(x, z) / (2 * np.pi)       # 가운데 = +Z (앞), 오른쪽 = +X
    v = np.arccos(np.clip(y, -1, 1)) / np.pi       # 위 = 0
    fx = u * w - 0.5; fy = v * h - 0.5
    x0 = np.floor(fx).astype(int); y0 = np.floor(fy).astype(int)
    tx = (fx - x0)[..., None]; ty = (fy - y0)[..., None]
    x1 = (x0 + 1) % w; x0 = x0 % w
    y0c = np.clip(y0, 0, h - 1); y1c = np.clip(y0 + 1, 0, h - 1)
    a = eq[y0c, x0] * (1 - tx) + eq[y0c, x1] * tx
    b = eq[y1c, x0] * (1 - tx) + eq[y1c, x1] * tx
    return a * (1 - ty) + b * ty


def face_dirs(face, n):
    t = (np.arange(n) + 0.5) / n * 2 - 1
    u, v = np.meshgrid(t, t)                       # u 오른쪽, v 아래
    one = np.ones_like(u)
    d = {0: (one, -v, -u), 1: (-one, -v, u), 2: (u, one, v), 3: (u, -one, -v), 4: (u, -v, one), 5: (-u, -v, -one)}[face]
    d = np.stack(d, -1)
    return d / np.linalg.norm(d, axis=-1, keepdims=True)


def to_rgb9e5(c):
    c = np.clip(c, 0, 65408.0).astype(np.float64)
    maxc = np.maximum(np.max(c, -1), 1e-12)
    exp_shared = np.maximum(-16, np.floor(np.log2(maxc))) + 1 + 15
    denom = np.power(2.0, exp_shared - 15 - 9)
    maxm = np.floor(maxc / denom + 0.5)
    exp_shared = np.where(maxm == 512, exp_shared + 1, exp_shared)
    denom = np.power(2.0, exp_shared - 15 - 9)
    m = np.clip(np.floor(c / denom[..., None] + 0.5), 0, 511).astype(np.uint32)
    e = np.clip(exp_shared, 0, 31).astype(np.uint32)
    return m[..., 0] | (m[..., 1] << 9) | (m[..., 2] << 18) | (e << 27)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('src'); ap.add_argument('dst')
    ap.add_argument('--size', type=int, default=512)
    ap.add_argument('--sky-median', type=float, default=0.55)
    ap.add_argument('--clamp', type=float, default=48.0)
    a = ap.parse_args()
    eq = read_hdr(a.src)
    h = eq.shape[0]
    lum = eq[: h // 2] @ np.array([0.2126, 0.7152, 0.0722], np.float32)
    exposure = a.sky_median / max(float(np.median(lum)), 1e-6)
    eq = np.minimum(eq * exposure, a.clamp)
    print(f'{eq.shape[1]}x{h}, exposure x{exposure:.3f}')

    n = a.size
    mips = int(np.log2(n)) + 1
    faces = []
    for f in range(6):
        base = sample_equirect(eq, face_dirs(f, n))
        chain = [base]
        while chain[-1].shape[0] > 1:
            p = chain[-1]
            chain.append((p[0::2, 0::2] + p[1::2, 0::2] + p[0::2, 1::2] + p[1::2, 1::2]) * 0.25)
        faces.append(chain)

    with open(a.dst, 'wb') as out:
        # DDS 헤더 + DX10 확장 (DXGI_FORMAT_R9G9B9E5_SHAREDEXP = 67, TEXTURECUBE)
        flags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x8
        caps = 0x1000 | 0x8 | 0x400000
        caps2 = 0x200 | 0xFC00
        hdr = struct.pack('<4sIIIIIII44x', b'DDS ', 124, flags, n, n, n * 4, 0, mips)
        pf = struct.pack('<II4sIIIII', 32, 0x4, b'DX10', 0, 0, 0, 0, 0)
        hdr += pf + struct.pack('<IIIII', caps, caps2, 0, 0, 0)
        dx10 = struct.pack('<IIIII', 67, 3, 0x4, 1, 0)
        out.write(hdr + dx10)
        for f in range(6):
            for m in faces[f]:
                g = np.power(np.maximum(m, 0.0), 1.0 / 2.2)   # 감마 공간으로
                out.write(to_rgb9e5(g).astype('<u4').tobytes())
    print('written', a.dst)


if __name__ == '__main__':
    main()
