# 묻힌 그림이 있는 FBX 검사 자료 (Tools/tests/data/EmbeddedTextures.fbx) 를 Blender 로 만든다 — run_tests.ps1 -Only model 의 FBX 그림 꺼내기
#  "C:\Program Files\Blender Foundation\Blender 5.2\blender.exe" -b --factory-startup --python Tools/tests/make_embedded_fbx.py
#  정육면체 둘: Checker (빨강 · 흰 체커 64x64 PNG = Base Color, 파일 이름 checker_red.png) · Stripe (파랑 줄무늬 PNG + 노멀 맵 없음)
#  FBX 내보내기 path_mode = COPY + embed_textures → Video 노드에 PNG 바이트가 들어간다 (Assimp 가 "*0" 또는 원래 파일 이름으로 찾는다)
import bpy, os, tempfile

out = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'data', 'EmbeddedTextures.fbx')
os.makedirs(os.path.dirname(out), exist_ok=True)
tmp = tempfile.mkdtemp()

bpy.ops.wm.read_factory_settings(use_empty=True)


def image(name, pixel):
    img = bpy.data.images.new(name, 64, 64, alpha=False)
    px = []
    for y in range(64):
        for x in range(64):
            px.extend(pixel(x, y))
    img.pixels = px
    path = os.path.join(tmp, name + '.png')
    img.filepath_raw = path
    img.file_format = 'PNG'
    img.save()
    return img


def material(name, img):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    bsdf = nt.nodes.get('Principled BSDF')
    tex = nt.nodes.new('ShaderNodeTexImage')
    tex.image = img
    nt.links.new(tex.outputs['Color'], bsdf.inputs['Base Color'])
    return m


checker = image('checker_red', lambda x, y: (1, 0, 0, 1) if ((x // 8) + (y // 8)) % 2 == 0 else (1, 1, 1, 1))
stripe = image('stripe_blue', lambda x, y: (0, 0.2, 1, 1) if (x // 8) % 2 == 0 else (0.9, 0.9, 0.9, 1))

for name, img, x in (('Checker', checker, -1.5), ('Stripe', stripe, 1.5)):
    bpy.ops.mesh.primitive_cube_add(size=2, location=(x, 0, 1))
    ob = bpy.context.active_object
    ob.name = name
    ob.data.materials.append(material(name + 'Mat', img))

bpy.ops.export_scene.fbx(filepath=out, path_mode='COPY', embed_textures=True, use_selection=False, apply_unit_scale=True)
print('written', out, os.path.getsize(out))
