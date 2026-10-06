#!/usr/bin/env python3
"""에디터(Inspector) 아이콘 SVG 를 생성하고 PNG 로 래스터화한다.

SVG 원본:  ProjectSetting/icons/svg/*.svg   (수정은 여기서)
런타임 PNG: ProjectSetting/icons/svg/png/*.png  (엔진이 텍스처로 로드)

실행: python Tools/make_icons.py
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SVG_DIR = os.path.join(ROOT, "ProjectSetting", "icons", "svg")
PNG_DIR = os.path.join(SVG_DIR, "png")

G = "#C4C4C4"   # 기본 아이콘 색 (Unity 다크 테마 글자색)


def svg16(body):
    return f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">\n{body}\n</svg>\n'


ICONS = {
    "arrow_right": svg16(f'  <polygon points="5.2,3 11.2,8 5.2,13" fill="{G}"/>'),
    "arrow_down": svg16(f'  <polygon points="3,5.2 13,5.2 8,11.2" fill="{G}"/>'),
    "arrow_left": svg16(f'  <polygon points="10.8,3 4.8,8 10.8,13" fill="{G}"/>'),
    "search": svg16(
        f'  <circle cx="6.8" cy="6.8" r="4.3" fill="none" stroke="{G}" stroke-width="1.5"/>\n'
        f'  <line x1="10" y1="10" x2="13.8" y2="13.8" stroke="{G}" stroke-width="1.7" stroke-linecap="round"/>'),
    # Unity 씬 에셋 아이콘 (유니티 로고 형태의 큐브)
    "scene": svg16(
        '  <path d="M8 1.2 L14 4.6 L14 11.4 L8 14.8 L2 11.4 L2 4.6 Z" fill="none" stroke="#D8D8D8" stroke-width="1.3" stroke-linejoin="round"/>\n'
        '  <path d="M8 8 L8 14.8 M8 8 L2 4.6 M8 8 L14 4.6" fill="none" stroke="#D8D8D8" stroke-width="1.3"/>\n'
        '  <circle cx="8" cy="8" r="1.6" fill="#D8D8D8"/>'),
    "dropdown": svg16(f'  <polygon points="4,6 12,6 8,10.6" fill="{G}"/>'),
    "help": svg16(
        f'  <circle cx="8" cy="8" r="6.4" fill="none" stroke="{G}" stroke-width="1.2"/>\n'
        f'  <path d="M6.1 6.4 Q6.1 4.7 8 4.7 Q9.9 4.7 9.9 6.3 Q9.9 7.3 8.6 7.9 Q8 8.3 8 9.3" fill="none" stroke="{G}" stroke-width="1.2" stroke-linecap="round"/>\n'
        f'  <circle cx="8" cy="11.3" r="0.85" fill="{G}"/>'),
    "presets": svg16(
        f'  <line x1="2" y1="5" x2="14" y2="5" stroke="{G}" stroke-width="1.2" stroke-linecap="round"/>\n'
        f'  <circle cx="10" cy="5" r="1.9" fill="{G}"/>\n'
        f'  <line x1="2" y1="11" x2="14" y2="11" stroke="{G}" stroke-width="1.2" stroke-linecap="round"/>\n'
        f'  <circle cx="6" cy="11" r="1.9" fill="{G}"/>'),
    "kebab": svg16(
        f'  <circle cx="8" cy="3.2" r="1.35" fill="{G}"/>\n'
        f'  <circle cx="8" cy="8" r="1.35" fill="{G}"/>\n'
        f'  <circle cx="8" cy="12.8" r="1.35" fill="{G}"/>'),
    "check": svg16(f'  <polyline points="3.4,8.6 6.6,11.8 12.8,4.4" fill="none" stroke="#E6E6E6" stroke-width="1.9" stroke-linecap="round"/>'),
    "link_off": svg16(
        f'  <rect x="1.2" y="5.8" width="6.4" height="4.4" rx="2.2" fill="none" stroke="{G}" stroke-width="1.2"/>\n'
        f'  <rect x="8.4" y="5.8" width="6.4" height="4.4" rx="2.2" fill="none" stroke="{G}" stroke-width="1.2"/>\n'
        f'  <line x1="2.6" y1="13.4" x2="13.4" y2="2.6" stroke="{G}" stroke-width="1.3" stroke-linecap="round"/>'),
    "eyedropper": svg16(
        f'  <line x1="3.2" y1="12.8" x2="9.6" y2="6.4" stroke="{G}" stroke-width="2.2" stroke-linecap="round"/>\n'
        f'  <circle cx="11.4" cy="4.6" r="2.7" fill="{G}"/>\n'
        f'  <circle cx="2.8" cy="13.2" r="1" fill="{G}"/>'),
    "target": svg16(
        f'  <circle cx="8" cy="8" r="5.6" fill="none" stroke="{G}" stroke-width="1.2"/>\n'
        f'  <circle cx="8" cy="8" r="1.9" fill="{G}"/>'),
    "lock": svg16(
        f'  <rect x="3.6" y="7.2" width="8.8" height="6.4" rx="1.2" fill="{G}"/>\n'
        f'  <path d="M5.6 7.2 V5.2 Q5.6 2.6 8 2.6 Q10.4 2.6 10.4 5.2 V7.2" fill="none" stroke="{G}" stroke-width="1.4"/>'),
    "info": svg16(
        f'  <circle cx="8" cy="8" r="7" fill="{G}"/>\n'
        f'  <circle cx="8" cy="4.9" r="1.05" fill="#383838"/>\n'
        f'  <rect x="7.2" y="6.9" width="1.6" height="5" rx="0.6" fill="#383838"/>'),
    "plus": svg16(
        f'  <line x1="3" y1="8" x2="13" y2="8" stroke="{G}" stroke-width="1.5" stroke-linecap="round"/>\n'
        f'  <line x1="8" y1="3" x2="8" y2="13" stroke="{G}" stroke-width="1.5" stroke-linecap="round"/>'),
    "minus": svg16(f'  <line x1="3" y1="8" x2="13" y2="8" stroke="{G}" stroke-width="1.5" stroke-linecap="round"/>'),
    "transform": svg16(
        '  <line x1="6.5" y1="9.5" x2="6.5" y2="2.4" stroke="#7ED957" stroke-width="1.6" stroke-linecap="round"/>\n'
        '  <circle cx="6.5" cy="2.4" r="1.5" fill="#7ED957"/>\n'
        '  <line x1="6.5" y1="9.5" x2="13.6" y2="12.2" stroke="#F4645F" stroke-width="1.6" stroke-linecap="round"/>\n'
        '  <circle cx="13.6" cy="12.2" r="1.5" fill="#F4645F"/>\n'
        '  <line x1="6.5" y1="9.5" x2="2.2" y2="13.6" stroke="#5AA7FF" stroke-width="1.6" stroke-linecap="round"/>\n'
        '  <circle cx="2.2" cy="13.6" r="1.5" fill="#5AA7FF"/>'),
    "camera": svg16(
        '  <rect x="0.8" y="4.2" width="9.6" height="7.6" rx="1.4" fill="#6FC3F7"/>\n'
        '  <polygon points="10.9,8 15.4,4.7 15.4,11.3" fill="#6FC3F7"/>'),
    "light_directional": svg16(
        '  <circle cx="8" cy="8" r="3.1" fill="#F5C542"/>\n'
        '  <line x1="8" y1="1.2" x2="8" y2="3.4" stroke="#F5C542" stroke-width="1.4" stroke-linecap="round"/>\n'
        '  <line x1="8" y1="12.6" x2="8" y2="14.8" stroke="#F5C542" stroke-width="1.4" stroke-linecap="round"/>\n'
        '  <line x1="1.2" y1="8" x2="3.4" y2="8" stroke="#F5C542" stroke-width="1.4" stroke-linecap="round"/>\n'
        '  <line x1="12.6" y1="8" x2="14.8" y2="8" stroke="#F5C542" stroke-width="1.4" stroke-linecap="round"/>\n'
        '  <line x1="3.2" y1="3.2" x2="4.7" y2="4.7" stroke="#F5C542" stroke-width="1.4" stroke-linecap="round"/>\n'
        '  <line x1="11.3" y1="11.3" x2="12.8" y2="12.8" stroke="#F5C542" stroke-width="1.4" stroke-linecap="round"/>\n'
        '  <line x1="12.8" y1="3.2" x2="11.3" y2="4.7" stroke="#F5C542" stroke-width="1.4" stroke-linecap="round"/>\n'
        '  <line x1="4.7" y1="11.3" x2="3.2" y2="12.8" stroke="#F5C542" stroke-width="1.4" stroke-linecap="round"/>'),
    "light_point": svg16(
        '  <circle cx="8" cy="6.4" r="4.4" fill="#F7D65A"/>\n'
        '  <rect x="5.7" y="10.4" width="4.6" height="3.4" rx="1" fill="#B9B9B9"/>\n'
        '  <rect x="6.5" y="13.4" width="3" height="1.4" rx="0.7" fill="#8E8E8E"/>'),
    "light_spot": svg16(
        '  <polygon points="8,1.8 13.6,12.6 2.4,12.6" fill="#F5C542" fill-opacity="0.9"/>\n'
        '  <ellipse cx="8" cy="12.6" rx="5.6" ry="1.7" fill="#F7D65A"/>'),
    "warning": (
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32 32" width="32" height="32">\n'
        '  <polygon points="16,3.5 29.5,27.5 2.5,27.5" fill="#F5C542" stroke="#F5C542" stroke-width="2.4" stroke-linejoin="round"/>\n'
        '  <rect x="14.6" y="11" width="2.8" height="9.6" rx="1.2" fill="#3A3A3A"/>\n'
        '  <circle cx="16" cy="24" r="1.7" fill="#3A3A3A"/>\n'
        '</svg>\n'),
    "info_box": (
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32 32" width="32" height="32">\n'
        '  <circle cx="16" cy="16" r="12.5" fill="#4C8DFF"/>\n'
        '  <circle cx="16" cy="10.6" r="1.8" fill="#FFFFFF"/>\n'
        '  <rect x="14.6" y="14" width="2.8" height="9" rx="1.2" fill="#FFFFFF"/>\n'
        '</svg>\n'),
    "mesh_filter": svg16(
        '  <rect x="1.8" y="1.8" width="12.4" height="12.4" rx="1" fill="none" stroke="#4EA8FF" stroke-width="1.5"/>\n'
        '  <line x1="8" y1="1.8" x2="8" y2="14.2" stroke="#4EA8FF" stroke-width="1.3"/>\n'
        '  <line x1="1.8" y1="8" x2="14.2" y2="8" stroke="#4EA8FF" stroke-width="1.3"/>'),
    "mesh_renderer": svg16(
        '  <rect x="1.8" y="1.8" width="12.4" height="12.4" rx="1" fill="none" stroke="#4EA8FF" stroke-width="1.5"/>\n'
        '  <line x1="8" y1="1.8" x2="8" y2="14.2" stroke="#4EA8FF" stroke-width="1.3"/>\n'
        '  <line x1="1.8" y1="8" x2="14.2" y2="8" stroke="#4EA8FF" stroke-width="1.3"/>\n'
        '  <circle cx="11.6" cy="4.6" r="1.7" fill="#E8B84A"/>'),
    "box_collider": svg16(
        '  <path d="M8 1.6 L14 4.8 L14 11.2 L8 14.4 L2 11.2 L2 4.8 Z" fill="#6FD46F" fill-opacity="0.25" stroke="#6FD46F" stroke-width="1.4"/>\n'
        '  <path d="M2 4.8 L8 8 L14 4.8" fill="none" stroke="#6FD46F" stroke-width="1.2"/>\n'
        '  <line x1="8" y1="8" x2="8" y2="14.4" stroke="#6FD46F" stroke-width="1.2"/>'),
    "sphere_collider": svg16(
        '  <circle cx="8" cy="8" r="6.2" fill="#6FD46F" fill-opacity="0.22" stroke="#6FD46F" stroke-width="1.4"/>\n'
        '  <ellipse cx="8" cy="8" rx="6.2" ry="2.4" fill="none" stroke="#6FD46F" stroke-width="1.1"/>'),
    "capsule_collider": svg16(
        '  <rect x="4.4" y="1.4" width="7.2" height="13.2" rx="3.6" fill="#6FD46F" fill-opacity="0.22" stroke="#6FD46F" stroke-width="1.4"/>\n'
        '  <ellipse cx="8" cy="8" rx="3.6" ry="1.3" fill="none" stroke="#6FD46F" stroke-width="1.0"/>'),
    "mesh_collider": svg16(
        '  <polygon points="8,1.6 14.2,5.2 12.6,13.4 3.4,13.4 1.8,5.2" fill="#6FD46F" fill-opacity="0.22" stroke="#6FD46F" stroke-width="1.3" stroke-linejoin="round"/>\n'
        '  <path d="M8 1.6 L8 8.4 L1.8 5.2 M8 8.4 L14.2 5.2 M8 8.4 L3.4 13.4 M8 8.4 L12.6 13.4" fill="none" stroke="#6FD46F" stroke-width="0.9"/>'),
    # Animation 컴포넌트 (재생 버튼 원)
    "animation": svg16(
        '  <circle cx="8" cy="8" r="6.6" fill="none" stroke="#9BD6A0" stroke-width="1.3"/>\n'
        '  <polygon points="6.3,5 11.3,8 6.3,11" fill="#9BD6A0"/>'),
    # Animation Clip 에셋 (필름 + 재생)
    "animation_clip": svg16(
        '  <rect x="1.6" y="3" width="12.8" height="10" rx="1.2" fill="none" stroke="#C4C4C4" stroke-width="1.2"/>\n'
        '  <line x1="1.6" y1="5.6" x2="14.4" y2="5.6" stroke="#C4C4C4" stroke-width="1"/>\n'
        '  <polygon points="6.6,7.2 10.6,9.4 6.6,11.6" fill="#9BD6A0"/>'),
    # Skinned Mesh Renderer (사람 실루엣 + 메시)
    "skinned_mesh_renderer": svg16(
        '  <circle cx="8" cy="3.4" r="2" fill="#4EA8FF"/>\n'
        '  <path d="M4 14.6 L5.2 7.2 L10.8 7.2 L12 14.6" fill="none" stroke="#4EA8FF" stroke-width="1.4" stroke-linejoin="round"/>\n'
        '  <line x1="2.4" y1="8.6" x2="13.6" y2="8.6" stroke="#4EA8FF" stroke-width="1.2" stroke-linecap="round"/>'),
    # Unity Rigidbody 아이콘: 초록/검정 4분할 공
    "rigidbody": svg16(
        '  <circle cx="8" cy="8" r="6.6" fill="#1E1E1E"/>\n'
        '  <path d="M8 8 L8 1.4 A6.6 6.6 0 0 0 1.4 8 Z" fill="#8BD13F"/>\n'
        '  <path d="M8 8 L8 14.6 A6.6 6.6 0 0 0 14.6 8 Z" fill="#8BD13F"/>\n'
        '  <circle cx="8" cy="8" r="6.6" fill="none" stroke="#DADADA" stroke-width="1.0"/>'),
    "edit_collider": svg16(
        '  <polyline points="3,12.6 5.4,4.2 12.4,6.4 10.2,12.8 3,12.6" fill="none" stroke="#E6E6E6" stroke-width="1.3" stroke-linejoin="round"/>\n'
        '  <circle cx="3" cy="12.6" r="1.6" fill="#E6E6E6"/>\n'
        '  <circle cx="5.4" cy="4.2" r="1.6" fill="#E6E6E6"/>\n'
        '  <circle cx="12.4" cy="6.4" r="1.6" fill="#E6E6E6"/>\n'
        '  <circle cx="10.2" cy="12.8" r="1.6" fill="#E6E6E6"/>'),
    "handle": svg16(
        '  <line x1="3" y1="6" x2="13" y2="6" stroke="#8C8C8C" stroke-width="1.4" stroke-linecap="round"/>\n'
        '  <line x1="3" y1="10" x2="13" y2="10" stroke="#8C8C8C" stroke-width="1.4" stroke-linecap="round"/>'),
    "list": svg16(
        '  <line x1="3" y1="4.5" x2="13" y2="4.5" stroke="#C4C4C4" stroke-width="1.4" stroke-linecap="round"/>\n'
        '  <line x1="3" y1="8" x2="13" y2="8" stroke="#C4C4C4" stroke-width="1.4" stroke-linecap="round"/>\n'
        '  <line x1="3" y1="11.5" x2="13" y2="11.5" stroke="#C4C4C4" stroke-width="1.4" stroke-linecap="round"/>'),
    "material_ball": (
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32 32" width="32" height="32">\n'
        '  <circle cx="16" cy="16" r="14" fill="#E4E4E4"/>\n'
        '  <circle cx="13" cy="12.5" r="9" fill="#FFFFFF" fill-opacity="0.85"/>\n'
        '</svg>\n'),
    "mesh_small": svg16(
        '  <rect x="2.4" y="2.4" width="11.2" height="11.2" rx="1" fill="none" stroke="#4EA8FF" stroke-width="1.4"/>\n'
        '  <line x1="8" y1="2.4" x2="8" y2="13.6" stroke="#4EA8FF" stroke-width="1.2"/>\n'
        '  <line x1="2.4" y1="8" x2="13.6" y2="8" stroke="#4EA8FF" stroke-width="1.2"/>'),
    "component": svg16(
        '  <rect x="2" y="2" width="12" height="12" rx="2" fill="none" stroke="#C4C4C4" stroke-width="1.3"/>\n'
        '  <line x1="5" y1="6" x2="11" y2="6" stroke="#C4C4C4" stroke-width="1.2"/>\n'
        '  <line x1="5" y1="10" x2="11" y2="10" stroke="#C4C4C4" stroke-width="1.2"/>'),
    # 32x32 GameObject 큐브 아이콘 (Inspector 헤더)
    "gameobject": (
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32 32" width="32" height="32">\n'
        '  <path d="M16 3.2 L27.6 9.6 L27.6 22.4 L16 28.8 L4.4 22.4 L4.4 9.6 Z" fill="#8FB4D9" fill-opacity="0.18" stroke="#DCE6F0" stroke-width="1.7"/>\n'
        '  <path d="M4.4 9.6 L16 16 L27.6 9.6" fill="none" stroke="#DCE6F0" stroke-width="1.5"/>\n'
        '  <line x1="16" y1="16" x2="16" y2="28.8" stroke="#DCE6F0" stroke-width="1.5"/>\n'
        '</svg>\n'),
}


def main():
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from icons_scene_toolbar import SCENE_TOOLBAR_ICONS
    from icons_project import PROJECT_ICONS
    from icons_ui import UI_ICONS
    from icons_effects import EFFECT_ICONS
    from icons_tilemap import TILEMAP_ICONS
    ICONS.update(SCENE_TOOLBAR_ICONS)
    ICONS.update(PROJECT_ICONS)
    ICONS.update(UI_ICONS)
    ICONS.update(EFFECT_ICONS)
    ICONS.update(TILEMAP_ICONS)
    os.makedirs(SVG_DIR, exist_ok=True)
    for name, svg in ICONS.items():
        with open(os.path.join(SVG_DIR, name + ".svg"), "w", encoding="utf-8", newline="\n") as f:
            f.write(svg)

    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import svg_raster
    os.makedirs(PNG_DIR, exist_ok=True)
    for name in ICONS:
        img = svg_raster.render_svg(os.path.join(SVG_DIR, name + ".svg"), 4)
        img.save(os.path.join(PNG_DIR, name + ".png"))
    print(f"{len(ICONS)} icons written")


if __name__ == "__main__":
    main()
