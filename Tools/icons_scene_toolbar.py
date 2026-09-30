"""Scene 뷰 툴바 / 도구 팔레트 아이콘 정의. make_icons.py 가 가져다 ICONS 에 합친다."""

G = "#C4C4C4"


def svg16(body):
    return f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">\n{body}\n</svg>\n'


SCENE_TOOLBAR_ICONS = {
    "tool_hand": svg16(
        f'  <path d="M5.2 8.4 V3.6 Q5.2 2.6 6.1 2.6 Q7 2.6 7 3.6 V7 M7 6.2 V2.8 Q7 1.8 7.9 1.8 Q8.8 1.8 8.8 2.8 V7 M8.8 6.4 V3.4 Q8.8 2.4 9.7 2.4 Q10.6 2.4 10.6 3.4 V7.6 M10.6 6.8 Q10.6 5.8 11.5 5.8 Q12.4 5.8 12.4 6.8 V10 Q12.4 13.6 9 13.6 H7.6 Q6 13.6 5 12.2 L3 9.2 Q2.6 8.4 3.4 8 Q4.2 7.7 5.2 9.2" fill="none" stroke="{G}" stroke-width="1.1" stroke-linejoin="round" stroke-linecap="round"/>'),
    "tool_move": svg16(
        f'  <line x1="8" y1="2" x2="8" y2="14" stroke="{G}" stroke-width="1.2"/>\n'
        f'  <line x1="2" y1="8" x2="14" y2="8" stroke="{G}" stroke-width="1.2"/>\n'
        f'  <polygon points="8,0.9 10.2,3.4 5.8,3.4" fill="{G}"/>\n'
        f'  <polygon points="8,15.1 10.2,12.6 5.8,12.6" fill="{G}"/>\n'
        f'  <polygon points="0.9,8 3.4,5.8 3.4,10.2" fill="{G}"/>\n'
        f'  <polygon points="15.1,8 12.6,5.8 12.6,10.2" fill="{G}"/>'),
    "tool_rotate": svg16(
        f'  <path d="M13.2 8 A5.2 5.2 0 1 1 10.4 3.4" fill="none" stroke="{G}" stroke-width="1.3" stroke-linecap="round"/>\n'
        f'  <polygon points="8.4,1.4 12.6,2.6 9.8,5.8" fill="{G}"/>'),
    "tool_scale": svg16(
        f'  <rect x="2.2" y="5.6" width="8.2" height="8.2" fill="none" stroke="{G}" stroke-width="1.2"/>\n'
        f'  <line x1="6" y1="10" x2="13.4" y2="2.6" stroke="{G}" stroke-width="1.2"/>\n'
        f'  <polygon points="14,2 14,6.2 9.8,2" fill="{G}"/>'),
    "tool_rect": svg16(
        f'  <rect x="2.6" y="3.6" width="10.8" height="8.8" fill="none" stroke="{G}" stroke-width="1.1"/>\n'
        f'  <rect x="1.4" y="2.4" width="2.4" height="2.4" fill="{G}"/>\n'
        f'  <rect x="12.2" y="2.4" width="2.4" height="2.4" fill="{G}"/>\n'
        f'  <rect x="1.4" y="11.2" width="2.4" height="2.4" fill="{G}"/>\n'
        f'  <rect x="12.2" y="11.2" width="2.4" height="2.4" fill="{G}"/>'),
    "tool_transform": svg16(
        f'  <path d="M12.6 8 A4.6 4.6 0 1 1 9.9 3.8" fill="none" stroke="{G}" stroke-width="1.1" stroke-linecap="round"/>\n'
        f'  <line x1="8" y1="3" x2="8" y2="13" stroke="{G}" stroke-width="1"/>\n'
        f'  <line x1="3" y1="8" x2="13" y2="8" stroke="{G}" stroke-width="1"/>\n'
        f'  <polygon points="8,0.8 9.7,2.8 6.3,2.8" fill="{G}"/>\n'
        f'  <polygon points="15.2,8 13.2,6.3 13.2,9.7" fill="{G}"/>\n'
        f'  <rect x="1.2" y="7" width="2" height="2" fill="{G}"/>'),
    "tool_cube": svg16(
        f'  <path d="M8 1.8 L13.6 4.9 L13.6 11.1 L8 14.2 L2.4 11.1 L2.4 4.9 Z" fill="none" stroke="{G}" stroke-width="1.2" stroke-linejoin="round"/>\n'
        f'  <path d="M2.4 4.9 L8 8 L13.6 4.9" fill="none" stroke="{G}" stroke-width="1.1"/>\n'
        f'  <line x1="8" y1="8" x2="8" y2="14.2" stroke="{G}" stroke-width="1.1"/>'),
    "pivot": svg16(
        f'  <rect x="2" y="2" width="10" height="10" fill="none" stroke="{G}" stroke-width="1.1"/>\n'
        f'  <line x1="4" y1="14" x2="14" y2="4" stroke="#F4645F" stroke-width="1.3"/>\n'
        f'  <circle cx="9" cy="9" r="1.7" fill="{G}"/>'),
    "pivot_center": svg16(
        f'  <rect x="2" y="2" width="12" height="12" fill="none" stroke="{G}" stroke-width="1.1"/>\n'
        f'  <circle cx="8" cy="8" r="1.8" fill="{G}"/>'),
    "space_local": svg16(
        f'  <path d="M7 1.8 L12.4 4.6 L12.4 10 L7 12.8 L1.6 10 L1.6 4.6 Z" fill="none" stroke="{G}" stroke-width="1.1" stroke-linejoin="round"/>\n'
        f'  <path d="M1.6 4.6 L7 7.4 L12.4 4.6" fill="none" stroke="{G}" stroke-width="1"/>\n'
        f'  <line x1="7" y1="7.4" x2="7" y2="12.8" stroke="{G}" stroke-width="1"/>\n'
        f'  <circle cx="12" cy="12" r="2.6" fill="#5AA7FF"/>'),
    "space_global": svg16(
        f'  <circle cx="8" cy="8" r="6" fill="none" stroke="{G}" stroke-width="1.1"/>\n'
        f'  <ellipse cx="8" cy="8" rx="2.6" ry="6" fill="none" stroke="{G}" stroke-width="1"/>\n'
        f'  <line x1="2" y1="8" x2="14" y2="8" stroke="{G}" stroke-width="1"/>'),
    "snap_grid": svg16(
        f'  <path d="M2 4 H12 M2 8 H12 M2 12 H12 M4.5 1.6 V14 M8 1.6 V14 M11.5 1.6 V14" fill="none" stroke="{G}" stroke-width="1"/>\n'
        f'  <path d="M9.6 9 L14.4 10.8 L12.2 11.8 L13.4 14.2 L12.2 14.8 L11 12.4 L9.6 13.6 Z" fill="{G}"/>'),
    "snap_move": svg16(
        f'  <path d="M2 2 H14 M2 8 H9 M2 14 H9" fill="none" stroke="{G}" stroke-width="1"/>\n'
        f'  <path d="M3 2 V14" fill="none" stroke="{G}" stroke-width="1"/>\n'
        f'  <path d="M8.6 3 L14.6 6.2 L11.9 7.4 L13.4 10.6 L12 11.2 L10.5 8 L8.6 9.6 Z" fill="#F4645F"/>'),
    "draw_wire": svg16(
        f'  <circle cx="8" cy="8" r="6" fill="none" stroke="{G}" stroke-width="1.1"/>\n'
        f'  <ellipse cx="8" cy="8" rx="2.8" ry="6" fill="none" stroke="{G}" stroke-width="1"/>\n'
        f'  <line x1="2" y1="8" x2="14" y2="8" stroke="{G}" stroke-width="1"/>\n'
        f'  <path d="M3.2 4.6 Q8 6.4 12.8 4.6 M3.2 11.4 Q8 9.6 12.8 11.4" fill="none" stroke="{G}" stroke-width="0.9"/>'),
    "draw_shaded_wire": svg16(
        f'  <circle cx="8" cy="8" r="6" fill="none" stroke="{G}" stroke-width="1.1"/>\n'
        f'  <path d="M8 2 A6 6 0 0 1 8 14 Z" fill="{G}"/>\n'
        f'  <path d="M3.2 4.6 Q8 6.4 12.8 4.6 M3.2 11.4 Q8 9.6 12.8 11.4" fill="none" stroke="#383838" stroke-width="0.9"/>'),
    "draw_shaded": svg16(
        f'  <circle cx="8" cy="8" r="6.2" fill="{G}"/>'),
    "scene_light": svg16(
        f'  <path d="M12.4 10.6 A6 6 0 1 1 6.2 2.4 A4.8 4.8 0 0 0 12.4 10.6 Z" fill="{G}"/>'),
    "bug": svg16(
        f'  <ellipse cx="8" cy="9.4" rx="3.2" ry="4.4" fill="{G}"/>\n'
        f'  <circle cx="8" cy="4" r="1.9" fill="{G}"/>\n'
        f'  <path d="M4.8 7.4 L2 6 M4.6 10 L1.8 10.4 M5 12.4 L2.6 14 M11.2 7.4 L14 6 M11.4 10 L14.2 10.4 M11 12.4 L13.4 14" fill="none" stroke="{G}" stroke-width="1.1" stroke-linecap="round"/>'),
    "audio_off": svg16(
        f'  <path d="M2 6 H5 L8.8 3 V13 L5 10 H2 Z" fill="{G}"/>\n'
        f'  <line x1="10.6" y1="5.6" x2="14.2" y2="10.4" stroke="{G}" stroke-width="1.2" stroke-linecap="round"/>\n'
        f'  <line x1="14.2" y1="5.6" x2="10.6" y2="10.4" stroke="{G}" stroke-width="1.2" stroke-linecap="round"/>'),
    "audio_on": svg16(
        f'  <path d="M2 6 H5 L8.8 3 V13 L5 10 H2 Z" fill="{G}"/>\n'
        f'  <path d="M11 5.6 Q13 8 11 10.4 M12.8 3.8 Q16 8 12.8 12.2" fill="none" stroke="{G}" stroke-width="1.1" stroke-linecap="round"/>'),
    "effects": svg16(
        f'  <polygon points="8,2 14.4,5.4 8,8.8 1.6,5.4" fill="{G}"/>\n'
        f'  <polyline points="1.6,8.2 8,11.6 14.4,8.2" fill="none" stroke="{G}" stroke-width="1.2"/>\n'
        f'  <polyline points="1.6,11 8,14.4 14.4,11" fill="none" stroke="{G}" stroke-width="1.2"/>'),
    "eye": svg16(
        f'  <path d="M1 8 Q8 1.6 15 8 Q8 14.4 1 8 Z" fill="none" stroke="{G}" stroke-width="1.2"/>\n'
        f'  <circle cx="8" cy="8" r="2.4" fill="{G}"/>'),
    "layers": svg16(
        f'  <polygon points="8,2 14.4,5.4 8,8.8 1.6,5.4" fill="none" stroke="{G}" stroke-width="1.2" stroke-linejoin="round"/>\n'
        f'  <polyline points="1.6,8.2 8,11.6 14.4,8.2" fill="none" stroke="{G}" stroke-width="1.2"/>\n'
        f'  <polyline points="1.6,11 8,14.4 14.4,11" fill="none" stroke="{G}" stroke-width="1.2"/>'),
    "scene_camera": svg16(
        f'  <rect x="1" y="4.4" width="9" height="7.2" rx="1.2" fill="{G}"/>\n'
        f'  <polygon points="10.6,8 15,5 15,11" fill="{G}"/>'),
    "gizmos": svg16(
        f'  <circle cx="8" cy="8" r="6.4" fill="none" stroke="{G}" stroke-width="1.2"/>\n'
        f'  <line x1="8" y1="8" x2="8" y2="3" stroke="#7ED957" stroke-width="1.4" stroke-linecap="round"/>\n'
        f'  <line x1="8" y1="8" x2="12.4" y2="10.6" stroke="#F4645F" stroke-width="1.4" stroke-linecap="round"/>\n'
        f'  <line x1="8" y1="8" x2="3.6" y2="10.6" stroke="#5AA7FF" stroke-width="1.4" stroke-linecap="round"/>'),
    "grip": svg16(
        '  <line x1="3" y1="5" x2="13" y2="5" stroke="#6E6E6E" stroke-width="1.2"/>\n'
        '  <line x1="3" y1="8" x2="13" y2="8" stroke="#6E6E6E" stroke-width="1.2"/>'),
}
