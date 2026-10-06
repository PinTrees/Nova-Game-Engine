# 2D Tilemap 패키지 아이콘 (Grid · Tilemap · Tilemap Renderer · Tilemap Collider 2D · Tile 에셋). make_icons.py 가 ICONS 에 합친다.

def _svg(body):
    return '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">\n  ' + body + '\n</svg>\n'


TILEMAP_ICONS = {
    # Grid: 3 x 3 격자
    "grid": _svg('''<rect x="1.5" y="1.5" width="13" height="13" fill="none" stroke="#C8C8C8" stroke-width="1.1"/>
  <line x1="5.8" y1="1.5" x2="5.8" y2="14.5" stroke="#C8C8C8" stroke-width="0.9"/>
  <line x1="10.2" y1="1.5" x2="10.2" y2="14.5" stroke="#C8C8C8" stroke-width="0.9"/>
  <line x1="1.5" y1="5.8" x2="14.5" y2="5.8" stroke="#C8C8C8" stroke-width="0.9"/>
  <line x1="1.5" y1="10.2" x2="14.5" y2="10.2" stroke="#C8C8C8" stroke-width="0.9"/>'''),
    # Tilemap: 격자 일부 칸이 칠해진 모양 (땅 · 풀)
    "tilemap": _svg('''<rect x="1.5" y="10" width="4.3" height="4.5" fill="#8B5A2B"/>
  <rect x="5.8" y="10" width="4.4" height="4.5" fill="#8B5A2B"/>
  <rect x="10.2" y="10" width="4.3" height="4.5" fill="#8B5A2B"/>
  <rect x="1.5" y="10" width="13" height="1.5" fill="#6AA84F"/>
  <rect x="10.2" y="5.8" width="4.3" height="4.2" fill="#3A5F85"/>
  <rect x="1.5" y="1.5" width="13" height="13" fill="none" stroke="#C8C8C8" stroke-width="1"/>
  <line x1="5.8" y1="1.5" x2="5.8" y2="14.5" stroke="#9A9A9A" stroke-width="0.6"/>
  <line x1="10.2" y1="1.5" x2="10.2" y2="14.5" stroke="#9A9A9A" stroke-width="0.6"/>
  <line x1="1.5" y1="5.8" x2="14.5" y2="5.8" stroke="#9A9A9A" stroke-width="0.6"/>'''),
    # Tilemap Renderer: 칠한 칸 + 붓
    "tilemap_renderer": _svg('''<rect x="1.5" y="8.5" width="6" height="6" fill="#3A5F85" stroke="#C8C8C8" stroke-width="0.9"/>
  <rect x="7.5" y="8.5" width="6" height="6" fill="#335A80" stroke="#C8C8C8" stroke-width="0.9"/>
  <polygon points="12.5,1.2 14.8,3.5 9.2,9.1 6.9,6.8" fill="#F6D365"/>
  <polygon points="6.9,6.8 9.2,9.1 7.4,10.6 5.3,8.5" fill="#B4B4B4"/>'''),
    # Tilemap Collider 2D: 계단 모양 초록 윤곽
    "tilemap_collider": _svg('''<polygon points="1.5,14.5 1.5,9.5 6,9.5 6,5.5 10.5,5.5 10.5,1.5 14.5,1.5 14.5,14.5" fill="#2E4A2E" stroke="#7CE07C" stroke-width="1.2"/>'''),
    # Tile 에셋: 한 칸 (풀 + 흙)
    "tile": _svg('''<rect x="2" y="2" width="12" height="12" fill="#8B5A2B" stroke="#C8C8C8" stroke-width="1"/>
  <rect x="2" y="2" width="12" height="3.5" fill="#6AA84F"/>
  <circle cx="6" cy="9.5" r="0.9" fill="#6B4320"/>
  <circle cx="10.5" cy="11" r="0.9" fill="#6B4320"/>'''),
}
