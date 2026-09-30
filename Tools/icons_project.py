# Project 창 아이콘 (폴더, 에셋 종류, 툴바). make_icons.py 가 ICONS 에 합친다.

def _svg(body):
    return '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">\n  ' + body + '\n</svg>\n'


PROJECT_ICONS = {
    # Volume 컴포넌트: 겹친 반투명 상자
    "volume": _svg('''<rect x="1.5" y="4.5" width="9" height="9" rx="1" fill="#6FA8DC" fill-opacity="0.35" stroke="#9CC6EE" stroke-width="1.1"/>
  <rect x="5.5" y="1.5" width="9" height="9" rx="1" fill="#6FA8DC" fill-opacity="0.55" stroke="#C8DFF5" stroke-width="1.1"/>'''),
    # Volume Profile 에셋: 문서 + 슬라이더 3개
    "volume_profile": _svg('''<path d="M3 1.5 H10 L13 4.5 V14.5 H3 Z" fill="#5E7FA3"/>
  <polygon points="10,1.5 13,4.5 10,4.5" fill="#3F5C7D"/>
  <line x1="5" y1="7" x2="11" y2="7" stroke="#DDE8F4" stroke-width="1"/>
  <circle cx="7" cy="7" r="1.2" fill="#FFFFFF"/>
  <line x1="5" y1="9.5" x2="11" y2="9.5" stroke="#DDE8F4" stroke-width="1"/>
  <circle cx="9.5" cy="9.5" r="1.2" fill="#FFFFFF"/>
  <line x1="5" y1="12" x2="11" y2="12" stroke="#DDE8F4" stroke-width="1"/>
  <circle cx="6" cy="12" r="1.2" fill="#FFFFFF"/>'''),
    "folder": _svg('''<path d="M1.5 3.5 H6 L7.5 5 H14.5 V13.5 H1.5 Z" fill="#BDBDBD"/>
  <rect x="1.5" y="6" width="13" height="7.5" fill="#D6D6D6"/>'''),
    "folder_open": _svg('''<path d="M1.5 3.5 H6 L7.5 5 H13.5 V7 H1.5 Z" fill="#BDBDBD"/>
  <polygon points="1.5,13.5 3.5,7 15.5,7 13.5,13.5" fill="#D6D6D6"/>'''),
    "asset_scene": _svg('''<polygon points="8,1.5 14,5 14,11 8,14.5 2,11 2,5" fill="#3C3C3C" stroke="#D0D0D0" stroke-width="1.2"/>
  <polygon points="8,4.5 11.2,6.3 11.2,9.9 8,11.7 4.8,9.9 4.8,6.3" fill="#D0D0D0"/>'''),
    "asset_material": _svg('''<circle cx="8" cy="8" r="6.3" fill="#8A8A8A"/>
  <circle cx="8" cy="8" r="6.3" fill="none" stroke="#5A5A5A" stroke-width="0.8"/>
  <circle cx="6" cy="5.8" r="2.4" fill="#E0E0E0"/>'''),
    "asset_model": _svg('''<polygon points="8,1.5 14,4.8 8,8.1 2,4.8" fill="#9EC3E8"/>
  <polygon points="2,4.8 8,8.1 8,14.6 2,11.3" fill="#6E95BF"/>
  <polygon points="14,4.8 8,8.1 8,14.6 14,11.3" fill="#4E739B"/>
  <polyline points="2,4.8 8,8.1 14,4.8" fill="none" stroke="#DDE8F4" stroke-width="0.8"/>'''),
    "asset_texture": _svg('''<rect x="1.5" y="2.5" width="13" height="11" rx="1" fill="#3A3A3A" stroke="#C8C8C8" stroke-width="1"/>
  <polygon points="2.5,12.5 6,7.5 8.5,10.5 10.5,8.5 13.5,12.5" fill="#79B865"/>
  <circle cx="11" cy="5.5" r="1.4" fill="#F2D060"/>'''),
    "asset_text": _svg('''<path d="M3 1.5 H10 L13 4.5 V14.5 H3 Z" fill="#E6E6E6"/>
  <polygon points="10,1.5 13,4.5 10,4.5" fill="#B0B0B0"/>
  <line x1="5" y1="7" x2="11" y2="7" stroke="#8A8A8A" stroke-width="1"/>
  <line x1="5" y1="9.5" x2="11" y2="9.5" stroke="#8A8A8A" stroke-width="1"/>
  <line x1="5" y1="12" x2="9" y2="12" stroke="#8A8A8A" stroke-width="1"/>'''),
    "asset_terrain_layer": _svg('''<polygon points="8,2 14.5,5 8,8 1.5,5" fill="#7DB36A"/>
  <polygon points="1.5,8 8,11 14.5,8 14.5,9.5 8,12.5 1.5,9.5" fill="#A68A5E"/>
  <polygon points="1.5,11 8,14 14.5,11 14.5,12 8,15 1.5,12" fill="#8A8A8A"/>'''),
    "filter_type": _svg('''<circle cx="5" cy="5" r="3" fill="none" stroke="#C8C8C8" stroke-width="1.3"/>
  <rect x="8.5" y="8.5" width="5.5" height="5.5" fill="none" stroke="#C8C8C8" stroke-width="1.3"/>
  <polygon points="11.2,1.8 14.2,6.5 8.2,6.5" fill="none" stroke="#C8C8C8" stroke-width="1.2"/>'''),
    "label": _svg('''<path d="M2 2 H8 L14 8 L8 14 L2 8 Z" fill="none" stroke="#C8C8C8" stroke-width="1.3"/>
  <circle cx="5" cy="5" r="1.2" fill="#C8C8C8"/>'''),
    "star": _svg('''<polygon points="8,1.5 9.9,6 14.5,6.2 10.9,9.1 12.1,14 8,11.3 3.9,14 5.1,9.1 1.5,6.2 6.1,6" fill="none" stroke="#C8C8C8" stroke-width="1.2" stroke-linejoin="round"/>'''),
    "refresh": _svg('''<path d="M13 8 A5 5 0 1 1 11.5 4.5" fill="none" stroke="#C8C8C8" stroke-width="1.5" stroke-linecap="round"/>
  <polygon points="10,1.5 14,3.5 11,6.5" fill="#C8C8C8"/>'''),
    "prefab": _svg('''<polygon points="8,1.5 14.5,4.8 8,8.1 1.5,4.8" fill="#8CC4FF"/>
  <polygon points="1.5,4.8 8,8.1 8,14.8 1.5,11.4" fill="#4B93E8"/>
  <polygon points="14.5,4.8 8,8.1 8,14.8 14.5,11.4" fill="#2F6FC4"/>'''),
}
