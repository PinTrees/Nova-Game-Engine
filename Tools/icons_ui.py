# UI(Canvas) 컴포넌트 아이콘 (Inspector 헤더, Add Component, Object Picker). make_icons.py 가 ICONS 에 합친다.

def _svg(body):
    return '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">\n  ' + body + '\n</svg>\n'


UI_ICONS = {
    # Rect Transform: 사각형 + 네 모서리 점 (Unity 아이콘과 같은 모양)
    "rect_transform": _svg('''<rect x="3" y="3" width="10" height="10" fill="none" stroke="#C8C8C8" stroke-width="1.2"/>
  <rect x="1.5" y="1.5" width="3" height="3" fill="#6FA8DC"/>
  <rect x="11.5" y="1.5" width="3" height="3" fill="#6FA8DC"/>
  <rect x="1.5" y="11.5" width="3" height="3" fill="#6FA8DC"/>
  <rect x="11.5" y="11.5" width="3" height="3" fill="#6FA8DC"/>'''),
    # Canvas: 화면 틀 + 안쪽 UI 요소
    "canvas": _svg('''<rect x="1.5" y="2.5" width="13" height="11" rx="1" fill="#2E2E2E" stroke="#C8C8C8" stroke-width="1.2"/>
  <rect x="3.5" y="4.5" width="5" height="3" fill="#6FA8DC"/>
  <rect x="3.5" y="9" width="9" height="2.5" fill="#9FC5E8"/>'''),
    # Canvas Scaler: 틀 + 대각선 확대 화살표
    "canvas_scaler": _svg('''<rect x="1.5" y="2.5" width="13" height="11" rx="1" fill="none" stroke="#C8C8C8" stroke-width="1.2"/>
  <line x1="4.5" y1="11" x2="11.5" y2="5" stroke="#6FA8DC" stroke-width="1.4"/>
  <polygon points="12,4.5 8.8,4.8 11.7,7.7" fill="#6FA8DC"/>'''),
    # Graphic Raycaster: 마우스 포인터
    "graphic_raycaster": _svg('''<polygon points="4,2 4,13.5 7,10.8 9,14.5 10.8,13.6 8.8,10 12.5,10" fill="#E6E6E6" stroke="#303030" stroke-width="0.6"/>'''),
    # Event System: 두 개의 둥근 화살표 (입력 → UI)
    "event_system": _svg('''<circle cx="8" cy="8" r="5.5" fill="none" stroke="#C8C8C8" stroke-width="1.3"/>
  <polygon points="12.8,4.2 14.8,7.8 11,7.6" fill="#6FA8DC"/>
  <circle cx="8" cy="8" r="1.8" fill="#6FA8DC"/>'''),
    # Image: 그림 틀 + 산 + 해
    "ui_image": _svg('''<rect x="1.5" y="2.5" width="13" height="11" rx="1" fill="#3A5F85" stroke="#C8C8C8" stroke-width="1.1"/>
  <polygon points="2.5,12.5 6.5,7.5 9,10.3 10.8,8.6 13.5,12.5" fill="#9FC5E8"/>
  <circle cx="11" cy="5.5" r="1.4" fill="#F6D365"/>'''),
    # Sprite Renderer: 기울어진 그림 카드 (Unity 의 Sprite Renderer 아이콘처럼)
    "sprite_renderer": _svg('''<rect x="3" y="2" width="10" height="12" rx="1" fill="#3A5F85" stroke="#C8C8C8" stroke-width="1.1" transform="rotate(-10 8 8)"/>
  <circle cx="8" cy="6.6" r="2.1" fill="#F6D365"/>
  <path d="M5 12 Q8 8.5 11 12 Z" fill="#9FC5E8"/>'''),
    # Sprite Animator (프레임 애니메이션): 겹친 그림 카드 세 장 + 재생 삼각형
    "sprite_animator": _svg('''<rect x="5.5" y="1.5" width="9" height="9" rx="1" fill="#2C4A68" stroke="#A0A0A0" stroke-width="0.9"/>
  <rect x="3.5" y="3.5" width="9" height="9" rx="1" fill="#335A80" stroke="#B4B4B4" stroke-width="0.9"/>
  <rect x="1.5" y="5.5" width="9" height="9" rx="1" fill="#3A6A98" stroke="#C8C8C8" stroke-width="1"/>
  <polygon points="4.5,7.8 4.5,12.6 8.5,10.2" fill="#F6D365"/>'''),
    # Sprite Skinned Renderer (2D 뼈대): 그림 카드 + 뼈 두 마디
    "sprite_skinned_renderer": _svg('''<rect x="1.5" y="2" width="10" height="12" rx="1" fill="#3A5F85" stroke="#C8C8C8" stroke-width="1.1"/>
  <polygon points="5,4 6.4,5.4 12.6,11.6 11.2,13 5,6.8 3.6,5.4" fill="#E6E6E6"/>
  <circle cx="4.4" cy="4.4" r="1.6" fill="#F6B26B"/>
  <circle cx="12.2" cy="12.2" r="1.6" fill="#F6B26B"/>'''),
    # Texture (스프라이트 선택): 체크무늬 그림
    "texture": _svg('''<rect x="1.5" y="1.5" width="13" height="13" fill="#B8B8B8"/>
  <rect x="1.5" y="1.5" width="6.5" height="6.5" fill="#E6E6E6"/>
  <rect x="8" y="8" width="6.5" height="6.5" fill="#E6E6E6"/>
  <rect x="1.5" y="1.5" width="13" height="13" fill="none" stroke="#707070" stroke-width="0.8"/>'''),
    # Text: 굵은 T
    "ui_text": _svg('''<rect x="2.5" y="2.5" width="11" height="2.6" fill="#E6E6E6"/>
  <rect x="6.7" y="2.5" width="2.6" height="11" fill="#E6E6E6"/>'''),
    # Button: 둥근 버튼 + 손가락 누름 표시
    "ui_button": _svg('''<rect x="1.5" y="4" width="13" height="8" rx="2.5" fill="#D9D9D9" stroke="#8C8C8C" stroke-width="0.8"/>
  <rect x="4.5" y="7.3" width="7" height="1.6" fill="#555555"/>'''),
    # Toggle: 체크 상자
    "ui_toggle": _svg('''<rect x="2" y="2" width="12" height="12" rx="2" fill="#D9D9D9" stroke="#8C8C8C" stroke-width="0.8"/>
  <polygon points="4,8.2 5.4,6.8 7,8.4 10.8,4.6 12.2,6 7,11.2" fill="#3A3A3A"/>'''),
    # Slider: 막대 + 손잡이
    "ui_slider": _svg('''<rect x="1.5" y="7" width="13" height="2.4" rx="1.2" fill="#9A9A9A"/>
  <rect x="1.5" y="7" width="7" height="2.4" rx="1.2" fill="#6FA8DC"/>
  <circle cx="8.5" cy="8.2" r="3" fill="#E6E6E6" stroke="#606060" stroke-width="0.6"/>'''),
    # Input Field: 입력 상자 + 커서
    "ui_input_field": _svg('''<rect x="1.5" y="4" width="13" height="8" rx="1.5" fill="#EDEDED" stroke="#8C8C8C" stroke-width="0.8"/>
  <rect x="4" y="5.8" width="1" height="4.4" fill="#303030"/>'''),
    # Mask: 점선 사각형 안의 원
    "ui_mask": _svg('''<rect x="2" y="2" width="12" height="12" fill="none" stroke="#C8C8C8" stroke-width="1.2" stroke-dasharray="2,1.5"/>
  <circle cx="8" cy="8" r="3.5" fill="#6FA8DC"/>'''),
    # Scroll Rect: 틀 + 세로 스크롤 막대
    "ui_scroll_rect": _svg('''<rect x="1.5" y="1.5" width="13" height="13" rx="1" fill="none" stroke="#C8C8C8" stroke-width="1.1"/>
  <rect x="11" y="3" width="2" height="10" fill="#6A6A6A"/>
  <rect x="11" y="4" width="2" height="4" fill="#D0D0D0"/>
  <rect x="3.5" y="4" width="6" height="1.4" fill="#9FC5E8"/>
  <rect x="3.5" y="7" width="6" height="1.4" fill="#9FC5E8"/>
  <rect x="3.5" y="10" width="6" height="1.4" fill="#9FC5E8"/>'''),
    # Font: A
    "font": _svg('''<polygon points="8,2 13.5,14 11.3,14 10.1,11.2 5.9,11.2 4.7,14 2.5,14" fill="#E6E6E6"/>
  <polygon points="8,6 9.4,9.4 6.6,9.4" fill="#383838"/>'''),
}
