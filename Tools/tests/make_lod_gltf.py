# LOD 노드 (Crate_LOD0 · 1 · 2) 가 있는 glTF — 모델 배치 검사용 (높이 2 · 1.4 · 0.8 상자)
import base64, json, struct, sys

def box(h):
    x, z = 0.5, 0.5
    faces = [((0, 0, 1), [(-x, 0, z), (x, 0, z), (x, h, z), (-x, h, z)]),
             ((0, 0, -1), [(x, 0, -z), (-x, 0, -z), (-x, h, -z), (x, h, -z)]),
             ((1, 0, 0), [(x, 0, z), (x, 0, -z), (x, h, -z), (x, h, z)]),
             ((-1, 0, 0), [(-x, 0, -z), (-x, 0, z), (-x, h, z), (-x, h, -z)]),
             ((0, 1, 0), [(-x, h, z), (x, h, z), (x, h, -z), (-x, h, -z)]),
             ((0, -1, 0), [(-x, 0, -z), (x, 0, -z), (x, 0, z), (-x, 0, z)])]
    pos, nrm, idx = [], [], []
    for n, quad in faces:
        b = len(pos)
        pos += quad
        nrm += [n] * 4
        idx += [b, b + 1, b + 2, b, b + 2, b + 3]
    return pos, nrm, idx

blob = b''
views, accessors, meshes, nodes = [], [], [], []
for i, h in enumerate([2.0, 1.4, 0.8]):
    pos, nrm, idx = box(h)
    for data, fmt, kind, target in ((pos, 'fff', 'VEC3', 34962), (nrm, 'fff', 'VEC3', 34962), (idx, 'H', 'SCALAR', 34963)):
        raw = b''.join(struct.pack('<' + fmt, *(v if isinstance(v, tuple) else (v,))) for v in data)
        while len(blob) % 4: blob += b'\0'
        views.append({'buffer': 0, 'byteOffset': len(blob), 'byteLength': len(raw), 'target': target})
        blob += raw
        acc = {'bufferView': len(views) - 1, 'componentType': 5126 if fmt == 'fff' else 5123, 'count': len(data), 'type': kind}
        if data is pos:
            acc['min'] = [min(p[k] for p in pos) for k in range(3)]
            acc['max'] = [max(p[k] for p in pos) for k in range(3)]
        accessors.append(acc)
    a = len(accessors) - 3
    meshes.append({'name': 'Crate_LOD%d' % i, 'primitives': [{'attributes': {'POSITION': a, 'NORMAL': a + 1}, 'indices': a + 2}]})
    nodes.append({'name': 'Crate_LOD%d' % i, 'mesh': i})
doc = {'asset': {'version': '2.0'}, 'scene': 0, 'scenes': [{'nodes': [0, 1, 2]}], 'nodes': nodes, 'meshes': meshes,
       'accessors': accessors, 'bufferViews': views,
       'buffers': [{'byteLength': len(blob), 'uri': 'data:application/octet-stream;base64,' + base64.b64encode(blob).decode()}]}
open(sys.argv[1], 'w').write(json.dumps(doc))
print('wrote', sys.argv[1])
